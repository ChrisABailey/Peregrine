// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_enc_vector_source.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "fv_s57.h"
#include "fv_s57_catalog.h"

namespace fs = std::filesystem;

namespace fv {
namespace {

// The catalogue CSVs sit at the exchange-set root while cells sit in per-cell
// folders, so a cell path has to look upward. Three levels covers
// <root>/ENC_ROOT/<producer>/<cell>/CELL.000 without ever leaving the set.
constexpr int kCatalogSearchParents = 3;

bool HasCatalog(const std::string& dir) {
  std::error_code ec;
  return fs::exists(fs::path(dir) / "s57objectclasses.csv", ec);
}

std::string FindCatalogDir(const std::string& start) {
  fs::path p(start);
  std::error_code ec;
  if (!fs::is_directory(p, ec)) p = p.parent_path();
  for (int i = 0; i <= kCatalogSearchParents && !p.empty(); ++i) {
    if (HasCatalog(p.string())) return p.string();
    if (!p.has_parent_path() || p.parent_path() == p) break;
    p = p.parent_path();
  }
  return {};
}

VectorGeometryType TypeFor(S57Primitive p) {
  switch (p) {
    case S57Primitive::kPoint: return VectorGeometryType::kPoint;
    case S57Primitive::kArea: return VectorGeometryType::kArea;
    default: return VectorGeometryType::kLine;
  }
}

}  // namespace

// ---------------------------------------------------------------------------

struct EncVectorSource::Impl {
  bool open = false;
  std::string path;
  std::vector<std::unique_ptr<S57Cell>> cells;
  S57ObjectCatalog catalog;

  // Layer names (object-class acronyms) present across the set, sorted, plus
  // the reverse map a FeatureRef needs.
  std::vector<std::string> layers;
  std::unordered_map<std::string, int> layer_index;

  GeoRect bounds{{90.0, 180.0}, {-90.0, -180.0}};
  size_t scamin_skipped = 0;
  // Resolved from the catalogue at Open rather than hardcoded: the code is
  // 133 in the delivered Appendix A, but a number spelled out here would be
  // exactly the kind of from-memory constant E1 refused to write.
  int scamin_code = -1;
  // record_id -> index, per cell, built on first Describe. A pick is a single
  // click, but an identify-heavy caller (or a test sweeping every feature)
  // would otherwise pay a linear scan per call.
  mutable std::vector<std::unordered_map<int32_t, size_t>> feature_index;

  // Acronym for an OBJL, or the number as text when the producer emitted a
  // code outside Appendix A. A visible "OBJL 17999" beats an invented acronym
  // and beats dropping the feature (plan §7: never silently drop).
  std::string Acronym(int object_class) const {
    const S57ObjectClass* oc = catalog.ObjectClass(object_class);
    if (oc != nullptr && !oc->acronym.empty()) return oc->acronym;
    return "OBJL " + std::to_string(object_class);
  }

  std::string AttributeAcronym(int code) const {
    const S57AttributeDef* a = catalog.Attribute(code);
    if (a != nullptr && !a->acronym.empty()) return a->acronym;
    return "ATTL " + std::to_string(code);
  }

  void Grow(const GeoRect& r) {
    bounds.ll.lat = (std::min)(bounds.ll.lat, r.ll.lat);
    bounds.ll.lon = (std::min)(bounds.ll.lon, r.ll.lon);
    bounds.ur.lat = (std::max)(bounds.ur.lat, r.ur.lat);
    bounds.ur.lon = (std::max)(bounds.ur.lon, r.ur.lon);
  }
};

EncVectorSource::EncVectorSource() : impl_(new Impl) {}
EncVectorSource::~EncVectorSource() = default;

Status EncVectorSource::Open(const std::string& path) {
  return Open(path, std::string());
}

Status EncVectorSource::Open(const std::string& path,
                             const std::string& catalog_dir) {
  std::vector<std::string> cell_paths;
  std::error_code ec;
  if (fs::is_directory(fs::path(path), ec)) {
    Status s = EnumerateEncCells(path, &cell_paths);
    if (!s.ok()) return s;
  } else {
    cell_paths.push_back(path);
  }
  if (cell_paths.empty())
    return Status::Error(kNotFound, "no S-57 base cells under " + path);

  Status s = OpenCells(cell_paths, catalog_dir);
  if (!s.ok()) return s;
  // The directory (or the single cell) the caller named, which is what a
  // diagnostic should echo back rather than the first cell it expanded to.
  impl_->path = path;
  return Status::Ok();
}

Status EncVectorSource::OpenCells(const std::vector<std::string>& cell_paths,
                                  const std::string& catalog_dir) {
  impl_.reset(new Impl);
  if (cell_paths.empty())
    return Status::Error(kInvalidArg, "no S-57 base cells given");

  const std::string cat_dir =
      catalog_dir.empty() ? FindCatalogDir(cell_paths.front()) : catalog_dir;
  if (cat_dir.empty()) {
    return Status::Error(kNotFound,
                         "S-57 Appendix A catalogue (s57objectclasses.csv) not "
                         "found near " + cell_paths.front() +
                         "; pass catalog_dir");
  }
  Status s = impl_->catalog.Load(cat_dir);
  if (!s.ok()) return s;
  const S57AttributeDef* scamin = impl_->catalog.AttributeByAcronym("SCAMIN");
  if (scamin != nullptr) impl_->scamin_code = scamin->code;

  std::map<std::string, int> seen_layers;
  for (const std::string& p : cell_paths) {
    std::unique_ptr<S57Cell> cell(new S57Cell);
    Status cs = cell->Open(p);
    if (!cs.ok()) return cs;
    impl_->Grow(cell->bounds());
    for (const S57Feature& f : cell->features()) {
      if (f.geometry.parts.empty()) continue;
      seen_layers[impl_->Acronym(f.object_class)] = 0;
    }
    impl_->cells.push_back(std::move(cell));
  }

  impl_->layers.reserve(seen_layers.size());
  for (const auto& kv : seen_layers) {
    impl_->layer_index[kv.first] = static_cast<int>(impl_->layers.size());
    impl_->layers.push_back(kv.first);
  }

  // Overwritten by Open() with the directory it was handed; on this path the
  // first cell is the most useful thing a diagnostic can name.
  impl_->path = cell_paths.front();
  impl_->open = true;
  return Status::Ok();
}

bool EncVectorSource::IsOpen() const { return impl_->open; }

GeoRect EncVectorSource::Bounds() const { return impl_->bounds; }

std::vector<std::string> EncVectorSource::Layers() const {
  return impl_->layers;
}

size_t EncVectorSource::cell_count() const { return impl_->cells.size(); }

const std::string& EncVectorSource::cell_path(size_t index) const {
  static const std::string kEmpty;
  if (index >= impl_->cells.size()) return kEmpty;
  return impl_->cells[index]->path();
}

std::string EncVectorSource::StalenessWarning() const {
  std::string all;
  for (const auto& c : impl_->cells) {
    const std::string w = c->StalenessWarning();
    if (w.empty()) continue;
    if (!all.empty()) all.push_back('\n');
    all += w;
  }
  return all;
}

size_t EncVectorSource::last_query_scamin_skipped() const {
  return impl_->scamin_skipped;
}

Status EncVectorSource::Query(const VectorQuery& q,
                              std::vector<VectorFeature>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "null out");
  if (!impl_->open) return Status::Error(kNotFound, "source not open");
  impl_->scamin_skipped = 0;

  for (size_t ci = 0; ci < impl_->cells.size(); ++ci) {
    const S57Cell& cell = *impl_->cells[ci];
    if (!cell.bounds().Intersects(q.area)) continue;

    for (const S57Feature& f : cell.features()) {
      if (f.geometry.parts.empty()) continue;
      if (!f.bounds.Intersects(q.area)) continue;

      // SCAMIN: "do not show below this scale". The comparison is on the
      // DENOMINATOR, so zooming OUT (larger denominator) is what hides a
      // feature. A non-numeric or absent value never hides anything.
      if (q.scale_denominator > 0.0 && impl_->scamin_code >= 0) {
        const S57Attribute* scamin = f.FindAttribute(impl_->scamin_code);
        if (scamin != nullptr && !scamin->value.empty()) {
          const double limit = std::strtod(scamin->value.c_str(), nullptr);
          if (limit > 0.0 && q.scale_denominator > limit) {
            ++impl_->scamin_skipped;
            continue;
          }
        }
      }

      VectorFeature vf;
      vf.type = TypeFor(f.primitive);
      vf.style_key = impl_->Acronym(f.object_class);
      vf.layer = vf.style_key;
      vf.bounds = f.bounds;
      vf.ref.layer = -1;
      auto li = impl_->layer_index.find(vf.style_key);
      if (li != impl_->layer_index.end()) vf.ref.layer = li->second;
      vf.ref.tile = static_cast<int32_t>(ci);
      vf.ref.feature = f.record_id;

      vf.parts.reserve(f.geometry.parts.size());
      for (const std::vector<S57Vertex>& part : f.geometry.parts) {
        std::vector<GeoPoint> run;
        run.reserve(part.size());
        for (const S57Vertex& v : part) run.push_back(GeoPoint{v.lat, v.lon});
        vf.parts.push_back(std::move(run));
      }

      vf.attributes.reserve(f.attributes.size() + 1);
      for (const S57Attribute& a : f.attributes)
        vf.attributes.emplace_back(impl_->AttributeAcronym(a.code), a.value);

      // Soundings carry their value in the geometry (3-D vertices), not in an
      // attribute, and S-52's SOUNDG procedure needs a number to symbolize.
      // Publishing the first vertex's depth as a pseudo-attribute keeps the
      // seam honest: it is named DEPTH, not an S-57 acronym, precisely because
      // Appendix A has no such attribute and a reader must not pretend it does.
      if (!f.geometry.parts.empty() && !f.geometry.parts[0].empty() &&
          f.geometry.parts[0][0].has_depth) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", f.geometry.parts[0][0].depth);
        vf.attributes.emplace_back("DEPTH", buf);
      }

      out->push_back(std::move(vf));
      if (q.max_features != 0 && out->size() >= q.max_features)
        return Status::Ok();
    }
  }
  return Status::Ok();
}

Status EncVectorSource::Describe(const FeatureRef& ref,
                                 FeatureDescription* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "null out");
  if (!impl_->open) return Status::Error(kNotFound, "source not open");
  if (ref.tile < 0 || static_cast<size_t>(ref.tile) >= impl_->cells.size())
    return Status::Error(kNotFound, "no such cell in this source");

  const S57Cell& cell = *impl_->cells[ref.tile];
  if (impl_->feature_index.empty())
    impl_->feature_index.resize(impl_->cells.size());
  std::unordered_map<int32_t, size_t>& index = impl_->feature_index[ref.tile];
  if (index.empty()) {
    const std::vector<S57Feature>& all = cell.features();
    index.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i) index[all[i].record_id] = i;
  }
  auto at = index.find(ref.feature);
  if (at == index.end()) return Status::Error(kNotFound, "no such feature");
  const S57Feature* found = &cell.features()[at->second];

  out->ref = ref;
  const std::string acronym = impl_->Acronym(found->object_class);
  const S57ObjectClass* oc = impl_->catalog.ObjectClass(found->object_class);
  out->title = (oc != nullptr && !oc->name.empty()) ? oc->name : acronym;
  out->class_name = acronym;
  out->layer_name = acronym;
  out->source_note = cell.info().name + " ed " + cell.info().edition +
                     " (RCID " + std::to_string(found->record_id) + ")";

  out->attributes.clear();
  out->attributes.reserve(found->attributes.size());
  for (const S57Attribute& a : found->attributes) {
    FeatureAttribute fa;
    fa.code = impl_->AttributeAcronym(a.code);
    const S57AttributeDef* def = impl_->catalog.Attribute(a.code);
    fa.name = def != nullptr ? def->name : fa.code;
    fa.raw = a.value;
    // DescribeValue decodes enumerated/list values through the expected-input
    // table and passes everything else through, so `display` is never empty
    // just because a decode missed — only when the value itself is null.
    fa.display = impl_->catalog.DescribeValue(a.code, a.value);
    out->attributes.push_back(std::move(fa));
  }
  return Status::Ok();
}

}  // namespace fv
