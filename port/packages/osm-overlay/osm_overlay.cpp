// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// osm_overlay.cpp — the peregrine.osm package: the catalog's OpenStreetMap
/// coverage drawn as an overlay, with a style sheet chosen on its own
/// Overlay ▸ Options page rather than Map ▸ Options' `osm.style`.
///
/// The pyramids come from `OverlayHost::Coverage("osm")`, the rows the base
/// map draws from, so Generate Coverage is the only setup. Each pyramid is a
/// `VectorMapOverlay`; all of them share one style engine.
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fv_osm_vector_source.h"
#include "fvkit/app/overlay_host.h"
#include "fvkit/app/properties.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/vector_map_overlay.h"
#include "fvkit/style/maplibre_style.h"
#include "peregrine/fv_native.h"

namespace {

/// A GL style is authored in CSS pixels: 96 per inch at display scale 1.
constexpr double kStyleNominalDpi = 96.0;
/// The reference latitude is quantised so a pan does not rebuild the scene.
constexpr double kRefLatStep = 0.5;

class OsmOverlay : public fv::Overlay, public fv::app::Properties, public fv::app::HostAware {
 public:
  explicit OsmOverlay(std::string default_style)
      : fv::Overlay("OpenStreetMap"), default_style_(std::move(default_style)),
        style_path_(default_style_) {
    fv::app::PropertySpec style;
    style.key = "style";
    style.label = "Style sheet";
    style.group = "Style";
    style.type = fv::app::PropertyType::kPath;
    style.default_value = fv::app::PropertyValue::Path(default_style_);
    style.path_filters = {{"MapLibre GL style", "*.json"}};
    style.help = "A MapLibre style without a background layer; the default draws "
                 "linework and names only.";
    fv::app::PropertySpec labels;
    labels.key = "labels";
    labels.label = "Show names";
    labels.group = "Style";
    labels.type = fv::app::PropertyType::kBool;
    labels.default_value = fv::app::PropertyValue::Bool(true);
    specs_ = {style, labels};
  }

  fv::app::Properties* AsProperties() override { return this; }
  fv::app::HostAware* AsHostAware() override { return this; }
  fv::app::HitTest* AsHitTest() override { return &hit_; }
  bool SupportsOpacity() const override { return true; }

  /// Reopens the pyramids from the host's catalog; called on join, on a
  /// catalog change and on a display-scale change.
  void SetHost(fv::app::OverlayHost* host) override {
    fv::app::HostAware::SetHost(host);
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<fv::CoverageRow> rows;
    if (host != nullptr) rows = host->Coverage("osm", fv::GeoRect::World());
    label_font_ = host != nullptr ? host->LabelFont() : std::string();
    display_scale_ = host != nullptr ? host->DisplayScale() : 1.0;

    std::map<std::string, Layer> next;
    for (const fv::CoverageRow& r : rows) {
      if (next.count(r.path)) continue;
      auto old = layers_.find(r.path);
      if (old != layers_.end()) {
        next.emplace(r.path, std::move(old->second));
        continue;
      }
      auto src = std::make_shared<fv::OsmVectorSource>();
      if (!src->Open(r.path).ok()) continue;
      Layer l;
      l.source = src;
      l.map = std::make_unique<fv::VectorMapOverlay>(r.series_key, src);
      next.emplace(r.path, std::move(l));
    }
    layers_ = std::move(next);
    engine_dirty_ = true;
  }

  fv::Status HostStatus() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (layers_.empty())
      return fv::Status::Error(fv::kNotFound,
                               "needs OpenStreetMap tiles in the catalog (Generate Coverage)");
    return fv::Status::Ok();
  }

  fv::Status OnDraw(const fv::MapProjection& proj, fv::ICanvas& canvas) override {
    std::lock_guard<std::mutex> lock(mutex_);
    hit_.Clear();
    if (layers_.empty() || !proj.Ready() || opacity() <= 0) return fv::Status::Ok();
    fv::Status s = EnsureEngine();
    if (!s.ok()) return s;
    if (opacity() >= 100) return DrawLayers(proj, canvas);

    // Drawn opaque off screen and faded once, so crossing strokes fade as one.
    const fv::PixelSize size = canvas.Size();
    fv::CpuCanvas layer(size.width, size.height);
    layer.Clear({0, 0, 0, 0});
    if (!label_font_.empty()) layer.SetDefaultFont(label_font_);
    s = DrawLayers(proj, layer);
    if (!s.ok()) return s;
    fv::PixelBuffer& buf = layer.Buffer();
    const int alpha = opacity();
    for (int y = 0; y < buf.Height(); ++y) {
      unsigned char* row = buf.Row(y);
      for (int x = 0; x < buf.Width(); ++x)
        row[4 * x + 3] = static_cast<unsigned char>(row[4 * x + 3] * alpha / 100);
    }
    return canvas.DrawPixmap(buf, 0, 0);
  }

  // fv::app::Properties
  const std::vector<fv::app::PropertySpec>& Describe() const override { return specs_; }

  fv::Status GetProperty(const std::string& key, fv::app::PropertyValue* out) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key == "style")
      *out = fv::app::PropertyValue::Path(style_path_);
    else if (key == "labels")
      *out = fv::app::PropertyValue::Bool(labels_);
    else
      return fv::Status::Error(fv::kNotFound, "no property " + key);
    return fv::Status::Ok();
  }

  fv::Status SetProperty(const std::string& key, const fv::app::PropertyValue& v) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key == "style" && (v.type == fv::app::PropertyType::kPath ||
                           v.type == fv::app::PropertyType::kString)) {
      // An empty path is the package's own sheet, so Reset to Defaults works
      // wherever the package is installed.
      const std::string path = v.s.empty() ? default_style_ : v.s;
      std::error_code ec;
      if (!std::filesystem::is_regular_file(path, ec))
        return fv::Status::Error(fv::kNotFound, "no style sheet at " + path);
      style_path_ = path;
    } else if (key == "labels" && v.type == fv::app::PropertyType::kBool) {
      labels_ = v.b;
    } else {
      return fv::Status::Error(fv::kInvalidArg, "bad value for " + key);
    }
    engine_dirty_ = true;
    return fv::Status::Ok();
  }

 private:
  struct Layer {
    std::shared_ptr<fv::OsmVectorSource> source;
    std::unique_ptr<fv::VectorMapOverlay> map;
  };

  /// Forwards hover to whichever pyramids the last draw reached.
  class LayersHitTest : public fv::app::HitTest {
   public:
    explicit LayersHitTest(std::mutex& m) : mutex_(m) {}
    void Clear() { drawn_.clear(); }
    void Add(fv::app::HitTest* h) {
      if (h != nullptr) drawn_.push_back(h);
    }
    void HitTestPoint(const fv::MapProjection& proj, fv::PixelPoint p, double tolerance_px,
                      std::vector<fv::app::HitItem>& out) override {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto it = drawn_.rbegin(); it != drawn_.rend(); ++it)
        (*it)->HitTestPoint(proj, p, tolerance_px, out);
    }

   private:
    std::mutex& mutex_;  ///< the overlay's: hover runs on the UI thread, draws on the worker
    std::vector<fv::app::HitTest*> drawn_;
  };

  /// Loads the style sheet into a new engine when a property or the host
  /// changed, and hands it to every pyramid. Caller holds `mutex_`.
  fv::Status EnsureEngine() {
    if (!engine_dirty_ && engine_) return fv::Status::Ok();
    auto e = std::make_shared<fv::MapLibreStyleEngine>();
    std::string detail;
    const fv::Status s = e->LoadFile(style_path_, &detail);
    if (!s.ok())
      return fv::Status::Error(s.code, "OpenStreetMap overlay style " + style_path_ + ": " +
                                           (detail.empty() ? s.message : detail));
    e->SetDrawLabels(labels_ && !label_font_.empty());
    if (!label_font_.empty()) e->SetLabelFont(label_font_);
    e->SetDisplayMmPerPixel(fv::kNativeDisplayMmPerPixel);
    engine_ = std::move(e);
    ref_lat_set_ = false;
    for (auto& [path, l] : layers_) {
      l.source->SetDisplayMmPerPixel(fv::kNativeDisplayMmPerPixel);
      l.map->SetStyle(engine_);
    }
    engine_dirty_ = false;
    return fv::Status::Ok();
  }

  /// Draws every pyramid that meets the view. Caller holds `mutex_`.
  fv::Status DrawLayers(const fv::MapProjection& proj, fv::ICanvas& canvas) {
    fv::GeoPoint centre;
    const fv::PixelSize size = canvas.Size();
    if (proj.SurfaceToGeo(size.width / 2.0, size.height / 2.0, &centre).ok()) {
      const double lat = std::round(centre.lat / kRefLatStep) * kRefLatStep;
      if (!ref_lat_set_ || lat != ref_lat_) {
        engine_->SetReferenceLatitude(lat);
        ref_lat_ = lat;
        ref_lat_set_ = true;
      }
    }
    for (auto& [path, l] : layers_) {
      l.map->SetDeviceDpi(kStyleNominalDpi * display_scale_);
      const fv::Status s = l.map->OnDraw(proj, canvas);
      if (!s.ok()) return s;
      if (l.map->last_draw_features() > 0) hit_.Add(l.map->AsHitTest());
    }
    return fv::Status::Ok();
  }

  const std::string default_style_;
  std::vector<fv::app::PropertySpec> specs_;
  mutable std::mutex mutex_;  ///< SetHost and properties on the UI thread, draws on the worker
  std::map<std::string, Layer> layers_;  ///< by .mbtiles path
  std::shared_ptr<fv::MapLibreStyleEngine> engine_;
  bool engine_dirty_ = true;
  std::string style_path_;
  bool labels_ = true;
  std::string label_font_;
  double display_scale_ = 1.0;
  double ref_lat_ = 0;
  bool ref_lat_set_ = false;
  LayersHitTest hit_{mutex_};
};

}  // namespace

FV_NATIVE_PLUGIN_BUILD_ID()

FV_NATIVE_EXPORT int fv_native_register(fv::desk::NativeRegistrar* r) {
  const std::string style =
      (std::filesystem::path(r->package_dir()) / "resources" / "osm-overlay.json").string();
  fv::desk::NativeOverlay osm;
  osm.factory = [style] { return std::make_shared<OsmOverlay>(style); };
  if (!r->AddOverlay("osm", std::move(osm)).ok()) return 1;
  return 0;
}
