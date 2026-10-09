// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_vector_map.h"

#include <cmath>
#include <filesystem>
#include <mutex>
#include <set>
#include <vector>

#include "fv_enc_vector_source.h"
#include "fv_geosym_style.h"
#include "fv_osm_style.h"
#include "fv_osm_vector_source.h"
#include "fv_s52_style.h"
#include "fv_vpf_vector_source.h"
#include "fvkit/canvas/canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/vector/mariner.h"
#include "fvkit/vector/renderer.h"

namespace fv {
namespace desk {
namespace {

std::mutex g_config_mu;
VectorMapConfig g_config;

/// What a GL style's `px` is authored against (a CSS pixel).
constexpr double kStyleNominalDpi = 96.0;
/// Fraction of the window kept styled beyond it, so a pan re-projects.
constexpr double kSceneMargin = 0.25;
/// Step of the OSM style's reference latitude; a finer one would rebuild
/// the retained scene on every pan.
constexpr double kOsmRefLatStep = 0.5;

}  // namespace

void MarinerOverrides::ApplyTo(MarinerSettings* m) const {
  if (safety_contour) m->safety_contour = *safety_contour;
  if (shallow_contour) m->shallow_contour = *shallow_contour;
  if (deep_contour) m->deep_contour = *deep_contour;
  if (safety_depth) m->safety_depth = *safety_depth;
  if (two_shades) m->two_shades = *two_shades;
  if (shallow_pattern) m->shallow_pattern = *shallow_pattern;
}

bool MarinerOverrides::operator==(const MarinerOverrides& o) const {
  return safety_contour == o.safety_contour && shallow_contour == o.shallow_contour &&
         deep_contour == o.deep_contour && safety_depth == o.safety_depth &&
         two_shades == o.two_shades && shallow_pattern == o.shallow_pattern;
}

VectorMapConfig CurrentVectorMapConfig() {
  std::lock_guard<std::mutex> lock(g_config_mu);
  return g_config;
}

void SetVectorMapConfig(const VectorMapConfig& config) {
  std::lock_guard<std::mutex> lock(g_config_mu);
  g_config = config;
}

VectorBaseMap::VectorBaseMap() = default;
VectorBaseMap::~VectorBaseMap() = default;

bool VectorBaseMap::CanDraw(const std::string& format) {
  return format == "vpf" || format == "enc" || format == "osm";
}

void VectorBaseMap::Reset(std::shared_ptr<Catalog> catalog) {
  series_.clear();
  geosym_.reset();
  s52_.reset();
  osm_.reset();
  osm_ref_lat_.reset();
  catalog_ = std::move(catalog);
  config_ = CurrentVectorMapConfig();
}

Status VectorBaseMap::OpenSource(const view::LadderProduct& product,
                                 std::shared_ptr<IVectorSource>* out) {
  std::vector<CoverageRow> rows;
  Status s = catalog_->SelectByGeoRect(GeoRect::World(), &rows, product.series_id);
  if (!s.ok()) return s;
  if (rows.empty())
    return Status::Error(kNotFound, "no coverage rows for " + product.series_key);

  if (product.format == "osm") {
    // One row is one .mbtiles pyramid.
    auto src = std::make_shared<OsmVectorSource>();
    s = src->Open(rows.front().path);
    *out = src;
  } else if (product.format == "enc") {
    // A series is a usage band: open exactly its cells, not their directory,
    // or a harbour chart draws over the general cells beneath it.
    std::set<std::string> cells;
    for (const CoverageRow& r : rows) cells.insert(r.path);
    auto src = std::make_shared<EncVectorSource>();
    s = src->OpenCells(std::vector<std::string>(cells.begin(), cells.end()), config_.enc_dir);
    *out = src;
  } else {
    // A VPF locator is "<database root>|<library>|<tile>".
    const std::string& p = rows.front().path;
    const std::string root = p.substr(0, p.find('|'));
    auto src = std::make_shared<VpfVectorSource>();
    s = src->Open((std::filesystem::path(root) / product.series_key).string());
    *out = src;
  }
  return s;
}

Status VectorBaseMap::OpenSeries(const view::LadderProduct& product, Series* out) {
  std::shared_ptr<IStyleEngine> style;
  if (product.format == "osm") {
    if (!osm_) {
      if (config_.osm_style.empty())
        return Status::Error(kInvalidArg, "OpenStreetMap needs a style sheet (osm.style)");
      auto e = std::make_shared<OsmStyleEngine>();
      std::string detail;
      const Status s = e->LoadFile(config_.osm_style, &detail);
      if (!s.ok())
        return Status::Error(s.code, "osm.style " + config_.osm_style + ": " +
                                         (detail.empty() ? s.message : detail));
      e->SetDrawLabels(false);
      osm_ = std::move(e);
    }
    style = osm_;
  } else if (product.format == "enc") {
    if (!s52_) {
      if (config_.enc_dir.empty())
        return Status::Error(kInvalidArg, "ENC needs the S-52 library directory (enc.data_dir)");
      auto e = std::make_shared<S52StyleEngine>();
      const Status s = e->Open(config_.enc_dir);
      if (!s.ok()) return Status::Error(s.code, "enc.data_dir: " + s.message);
      e->SetShowMetaObjects(config_.enc_show_meta);
      config_.mariner.ApplyTo(&e->mutable_mariner());
      e->SetDrawLabels(false);
      s52_ = std::move(e);
    }
    style = s52_;
  } else {
    if (!geosym_) {
      if (config_.geosym_dir.empty())
        return Status::Error(kInvalidArg, "DNC needs the GeoSym directory (geosym.data_dir)");
      auto e = std::make_shared<GeoSymStyleEngine>();
      const Status s = e->Open(config_.geosym_dir, kGeoSymDnc);
      if (!s.ok()) return Status::Error(s.code, "geosym.data_dir: " + s.message);
      e->SetColorAdjust(config_.geosym_brightness, config_.geosym_contrast);
      config_.mariner.ApplyTo(&e->mutable_mariner());
      e->SetDrawLabels(false);
      geosym_ = std::move(e);
    }
    style = geosym_;
  }

  std::shared_ptr<IVectorSource> source;
  const Status s = OpenSource(product, &source);
  if (!s.ok()) return s;
  out->source = source;
  out->renderer = std::make_unique<VectorRenderer>(source, style);
  out->renderer->SetSceneMargin(kSceneMargin);
  return Status::Ok();
}

Status VectorBaseMap::Render(const view::Viewport& v, const view::LadderProduct& product,
                             ICanvas& canvas) {
  if (!catalog_ || !CanDraw(product.format)) return Status::Ok();
  auto it = series_.find(product.series_id);
  if (it == series_.end()) {
    Series opened;
    const Status s = OpenSeries(product, &opened);
    if (!s.ok()) return s;
    it = series_.emplace(product.series_id, std::move(opened)).first;
  }
  VectorRenderer& r = *it->second.renderer;
  const MapProjection& proj = v.Projection();

  FvColor ground{255, 255, 255, 255};  // a paper chart
  if (product.format == "osm") {
    // A GL style is authored in points: the source and the style take the
    // pitch per point, and the device DPI is the style's 96 per point.
    auto* src = static_cast<OsmVectorSource*>(it->second.source.get());
    src->SetDisplayMmPerPixel(v.MmPerPoint());
    osm_->SetDisplayMmPerPixel(v.MmPerPoint());
    const double lat = std::round(v.Center().lat / kOsmRefLatStep) * kOsmRefLatStep;
    if (osm_ref_lat_ != lat) {
      osm_->SetReferenceLatitude(lat);
      osm_ref_lat_ = lat;
    }
    r.SetDeviceDpi(kStyleNominalDpi * v.DisplayScale());
    osm_->background(proj.Scale(), &ground);
    ground.a = 255;
  } else {
    // Chart symbology is sized in physical units at the true pixel pitch.
    r.SetDeviceDpi(25.4 * v.DisplayScale() / v.MmPerPoint());
  }
  canvas.Clear(ground);
  return r.Render(proj, &canvas);
}

}  // namespace desk
}  // namespace fv
