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
#include "fvkit/log.h"

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
  /** One cell of the set: parsed now, parsed on demand, or unreadable. */
  struct Slot {
    std::string path;
    GeoRect bounds;
    std::unique_ptr<S57Cell> cell;  // null while not resident
    bool unreadable = false;
    uint64_t last_use = 0;
    // record_id -> feature index, built on first Describe of this cell.
    std::unordered_map<int32_t, size_t> feature_index;
  };

  bool open = false;
  bool lazy = false;
  std::string path;
  std::vector<Slot> slots;
  std::vector<std::string> unreadable;  // "path: reason"
  S57ObjectCatalog catalog;
  size_t max_resident = 0;
  uint64_t use_clock = 0;

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

  Status LoadCatalog(const std::string& catalog_dir, const std::string& near) {
    const std::string cat_dir =
        catalog_dir.empty() ? FindCatalogDir(near) : catalog_dir;
    if (cat_dir.empty()) {
      return Status::Error(kNotFound,
                           "S-57 Appendix A catalogue (s57objectclasses.csv) "
                           "not found near " + near + "; pass catalog_dir");
    }
    Status s = catalog.Load(cat_dir);
    if (!s.ok()) return s;
    const S57AttributeDef* scamin = catalog.AttributeByAcronym("SCAMIN");
    if (scamin != nullptr) scamin_code = scamin->code;
    return Status::Ok();
  }

  /** Parses `slot` if it is not resident. False when the cell cannot be read;
   *  the failure is logged once and the cell is skipped from then on. */
  bool Ensure(Slot* slot) {
    slot->last_use = ++use_clock;
    if (slot->cell) return true;
    if (slot->unreadable) return false;
    std::unique_ptr<S57Cell> cell(new S57Cell);
    const Status s = cell->Open(slot->path);
    if (!s.ok()) {
      slot->unreadable = true;
      unreadable.push_back(slot->path + ": " + s.message);
      FV_LOG_WARNING("S-57: skipping unreadable cell " << slot->path << ": "
                                                        << s.message);
      return false;
    }
    slot->cell = std::move(cell);
    return true;
  }

  /** Drops the least recently used cells beyond `max_resident`, keeping any
   *  used at or after `keep_from` (the query in progress needed them). */
  void Trim(uint64_t keep_from) {
    if (max_resident == 0) return;
    std::vector<Slot*> resident;
    for (Slot& s : slots)
      if (s.cell) resident.push_back(&s);
    if (resident.size() <= max_resident) return;
    std::sort(resident.begin(), resident.end(), [](const Slot* a, const Slot* b) {
      return a->last_use < b->last_use;
    });
    size_t excess = resident.size() - max_resident;
    for (Slot* s : resident) {
      if (excess == 0 || s->last_use >= keep_from) break;
      s->cell.reset();
      s->feature_index.clear();
      --excess;
    }
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
  Status s = impl_->LoadCatalog(catalog_dir, cell_paths.front());
  if (!s.ok()) return s;

  std::map<std::string, int> seen_layers;
  std::string first_error;
  for (const std::string& p : cell_paths) {
    Impl::Slot slot;
    slot.path = p;
    impl_->slots.push_back(std::move(slot));
    Impl::Slot& added = impl_->slots.back();
    // An unreadable cell keeps its slot (and so its tile id) but is skipped.
    if (!impl_->Ensure(&added)) {
      if (first_error.empty()) first_error = impl_->unreadable.back();
      continue;
    }
    added.bounds = added.cell->bounds();
    impl_->Grow(added.bounds);
    for (const S57Feature& f : added.cell->features()) {
      if (f.geometry.parts.empty()) continue;
      seen_layers[impl_->Acronym(f.object_class)] = 0;
    }
  }
  if (impl_->unreadable.size() == cell_paths.size())
    return Status::Error(kIoError, "no readable S-57 cell; " + first_error);

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

Status EncVectorSource::OpenIndexed(const std::vector<EncCellEntry>& cells,
                                    const std::string& catalog_dir) {
  const size_t max_resident = impl_->max_resident;
  impl_.reset(new Impl);
  impl_->max_resident = max_resident;
  if (cells.empty())
    return Status::Error(kInvalidArg, "no S-57 base cells given");
  Status s = impl_->LoadCatalog(catalog_dir, cells.front().path);
  if (!s.ok()) return s;

  impl_->lazy = true;
  impl_->slots.reserve(cells.size());
  for (const EncCellEntry& e : cells) {
    Impl::Slot slot;
    slot.path = e.path;
    slot.bounds = e.bounds;
    impl_->Grow(e.bounds);
    impl_->slots.push_back(std::move(slot));
  }
  // Which classes occur is unknown until every cell is read, so the layer
  // list is the whole catalogue; tile and layer ids then never shift.
  impl_->layers = impl_->catalog.ObjectClassAcronyms();
  for (size_t i = 0; i < impl_->layers.size(); ++i)
    impl_->layer_index[impl_->layers[i]] = static_cast<int>(i);

  impl_->path = cells.front().path;
  impl_->open = true;
  return Status::Ok();
}

void EncVectorSource::SetMaxResidentCells(size_t n) {
  impl_->max_resident = n;
  impl_->Trim(impl_->use_clock + 1);
}

size_t EncVectorSource::resident_cell_count() const {
  size_t n = 0;
  for (const Impl::Slot& s : impl_->slots)
    if (s.cell) ++n;
  return n;
}

void EncVectorSource::ReleaseCells() {
  if (!impl_->lazy) return;
  for (Impl::Slot& s : impl_->slots) {
    s.cell.reset();
    s.feature_index.clear();
  }
}

const std::vector<std::string>& EncVectorSource::unreadable_cells() const {
  return impl_->unreadable;
}

bool EncVectorSource::IsOpen() const { return impl_->open; }

GeoRect EncVectorSource::Bounds() const { return impl_->bounds; }

std::vector<std::string> EncVectorSource::Layers() const {
  return impl_->layers;
}

size_t EncVectorSource::cell_count() const { return impl_->slots.size(); }

const std::string& EncVectorSource::cell_path(size_t index) const {
  static const std::string kEmpty;
  if (index >= impl_->slots.size()) return kEmpty;
  return impl_->slots[index].path;
}

std::string EncVectorSource::StalenessWarning() const {
  std::string all;
  for (const Impl::Slot& s : impl_->slots) {
    if (!s.cell) continue;
    const std::string w = s.cell->StalenessWarning();
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
  const uint64_t query_start = impl_->use_clock + 1;
  Status result = QueryCells(q, out);
  impl_->Trim(query_start);
  return result;
}

Status EncVectorSource::QueryCells(const VectorQuery& q,
                                   std::vector<VectorFeature>* out) {
  for (size_t ci = 0; ci < impl_->slots.size(); ++ci) {
    Impl::Slot& slot = impl_->slots[ci];
    if (slot.unreadable || !slot.bounds.Intersects(q.area)) continue;
    if (!impl_->Ensure(&slot)) continue;
    const S57Cell& cell = *slot.cell;
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

      vf.attributes.reserve(f.attributes.size() + 1);
      for (const S57Attribute& a : f.attributes)
        vf.attributes.emplace_back(impl_->AttributeAcronym(a.code), a.value);

      // A sounding feature is a cloud of 3-D vertices, each its own depth.
      // Each becomes one point feature carrying its value as the
      // pseudo-attribute DEPTH (Appendix A has no attribute for it), so every
      // sounding is drawn and symbolized, not only the first. All keep the
      // record's ref, so a tap on any of them describes the same record.
      bool sounding = false;
      for (const std::vector<S57Vertex>& part : f.geometry.parts)
        for (const S57Vertex& v : part) sounding = sounding || v.has_depth;
      if (sounding) {
        for (const std::vector<S57Vertex>& part : f.geometry.parts) {
          for (const S57Vertex& v : part) {
            if (!v.has_depth) continue;
            const GeoPoint g{v.lat, v.lon};
            if (!q.area.Contains(g)) continue;
            VectorFeature one = vf;
            one.type = VectorGeometryType::kPoint;
            one.bounds = GeoRect{g, g};
            one.parts.assign(1, std::vector<GeoPoint>{g});
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f", v.depth);
            one.attributes.emplace_back("DEPTH", buf);
            out->push_back(std::move(one));
            if (q.max_features != 0 && out->size() >= q.max_features)
              return Status::Ok();
          }
        }
        continue;
      }

      vf.parts.reserve(f.geometry.parts.size());
      for (const std::vector<S57Vertex>& part : f.geometry.parts) {
        std::vector<GeoPoint> run;
        run.reserve(part.size());
        for (const S57Vertex& v : part) run.push_back(GeoPoint{v.lat, v.lon});
        vf.parts.push_back(std::move(run));
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
  if (ref.tile < 0 || static_cast<size_t>(ref.tile) >= impl_->slots.size())
    return Status::Error(kNotFound, "no such cell in this source");

  Impl::Slot& slot = impl_->slots[ref.tile];
  if (!impl_->Ensure(&slot))
    return Status::Error(kIoError, "cell unreadable: " + slot.path);
  const S57Cell& cell = *slot.cell;
  std::unordered_map<int32_t, size_t>& index = slot.feature_index;
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
