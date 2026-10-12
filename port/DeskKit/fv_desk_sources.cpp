// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_sources.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>

#include "fv_desk_desk.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/source_spec.h"
#include "fvkit/app/type_registry.h"

namespace fv {
namespace desk {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// "GeoPackage" for a `.gpkg` the type saves as "GeoPackage (*.gpkg)"; else the
/// extension in capitals.
std::string FormatName(const app::OverlayTypeDesc* d, const std::string& path) {
  std::string ext = std::filesystem::path(path).extension().string();
  if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
  if (d != nullptr && d->file) {
    const std::string pattern = "*." + Lower(ext);
    for (const auto* filters : {&d->file->save_filters, &d->file->open_filters})
      for (const auto& f : *filters)
        if (Lower(f.second) == pattern) return f.first.substr(0, f.first.find(" ("));
  }
  for (char& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return ext;
}

std::string Bytes(uintmax_t n) {
  char buf[32];
  if (n < 1024) std::snprintf(buf, sizeof buf, "%ju bytes", n);
  else if (n < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", n / 1024.0);
  else std::snprintf(buf, sizeof buf, "%.1f MB", n / (1024.0 * 1024.0));
  return buf;
}

/// The file's modification time, local, "2026-10-12 14:05"; empty when unknown.
std::string Modified(const std::string& path) {
  std::error_code ec;
  const auto t = std::filesystem::last_write_time(path, ec);
  if (ec) return std::string();
  const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
  std::tm tm{};
#if defined(_WIN32)  // localtime_r is POSIX
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
  return buf;
}

std::string GeometryName(GeometryType t) {
  switch (t) {
    case GeometryType::kPoint:
    case GeometryType::kMultiPoint: return "Point";
    case GeometryType::kLineString:
    case GeometryType::kMultiLineString: return "Line";
    case GeometryType::kPolygon:
    case GeometryType::kMultiPolygon: return "Area";
    default: return "Any";
  }
}

std::string Extent(const GeoRect& r) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "%.5f, %.5f to %.5f, %.5f", r.ll.lat, r.ll.lon, r.ur.lat, r.ur.lon);
  return buf;
}

const app::OverlayTypeDesc* DescOf(OverlayManager& stack, const Overlay& o) {
  const app::OverlayTypeRegistry* registry = stack.type_registry();
  return registry == nullptr ? nullptr : registry->Find(o.type_id());
}

/// The file rows of a Properties sheet for the file at `path`.
void FileRows(const app::OverlayTypeDesc* d, const std::string& path, std::vector<PropertyRow>* out) {
  out->push_back({"Source", "Path", path});
  out->push_back({"Source", "Format", FormatName(d, path)});
  std::error_code ec;
  const uintmax_t size = std::filesystem::file_size(path, ec);
  if (!ec) out->push_back({"Source", "Size", Bytes(size)});
  const std::string when = Modified(path);
  if (!when.empty()) out->push_back({"Source", "Modified", when});
}

}  // namespace

std::vector<SourceRow> SourcesModel::Rows() const {
  const std::vector<LayerRow> rows = layers_.Rows();
  std::vector<SourceRow> out;
  std::map<std::string, size_t> by_path;
  for (size_t i = 0; i < rows.size(); ++i) {
    const LayerRow& r = rows[i];
    size_t at;
    const auto it = r.source.empty() ? by_path.end() : by_path.find(r.source);
    if (it != by_path.end()) {
      at = it->second;
    } else {
      at = out.size();
      SourceRow s;
      s.path = r.source;
      // A built-in overlay goes by its type's name ("Lat/Lon Grid", not "grid").
      const app::OverlayTypeDesc* d = r.overlay != nullptr ? DescOf(stack_, *r.overlay) : nullptr;
      const bool built_in = d != nullptr && !d->file && !d->display_name.empty();
      s.name = !r.source.empty() ? std::filesystem::path(r.source).filename().string()
               : built_in       ? d->display_name
                                : r.name;
      s.type_name = r.type_name;
      s.icon = r.icon;
      s.top_most = r.top_most;
      out.push_back(std::move(s));
      if (!r.source.empty()) by_path[r.source] = at;
    }
    SourceRow& s = out[at];
    s.layers.push_back(i);
    s.current = s.current || r.current;
    s.dirty = s.dirty || r.dirty;
  }
  for (SourceRow& s : out) {
    size_t shown = 0;
    for (size_t i : s.layers) shown += rows[i].visible ? 1 : 0;
    s.shown = shown == 0 ? SourceShown::kNone : shown == s.layers.size() ? SourceShown::kAll : SourceShown::kSome;
  }
  return out;
}

Status SourcesModel::SetVisible(size_t source, bool visible) {
  const std::vector<SourceRow> rows = Rows();
  if (source >= rows.size()) return Status::Error(kInvalidArg, "no source row " + std::to_string(source));
  for (size_t i : rows[source].layers) {
    Status s = layers_.SetVisible(i, visible);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

Status SourcesModel::Move(size_t from, size_t to) {
  const std::vector<SourceRow> rows = Rows();
  if (from >= rows.size() || to >= rows.size())
    return Status::Error(kInvalidArg, "no source row " + std::to_string(from >= rows.size() ? from : to));
  if (from == to) return Status::Ok();
  if (rows[from].top_most != rows[to].top_most)
    return Status::Error(kInvalidArg, "\"" + rows[rows[from].top_most ? from : to].name +
                                          "\" stays above the other layers");
  const std::vector<LayerRow> layer_rows = layers_.Rows();
  auto shared = [&](size_t row) { return stack_.FindShared(layer_rows[row].overlay); };
  const std::vector<size_t>& moving = rows[from].layers;
  if (to < from) {
    // Each lands directly above the target's top layer, so the source keeps its order.
    const std::shared_ptr<Overlay> anchor = shared(rows[to].layers.front());
    for (size_t i : moving) {
      Status s = stack_.MoveAbove(shared(i), anchor);
      if (!s.ok()) return s;
    }
  } else {
    const std::shared_ptr<Overlay> anchor = shared(rows[to].layers.back());
    for (auto it = moving.rbegin(); it != moving.rend(); ++it) {
      Status s = stack_.MoveBelow(shared(*it), anchor);
      if (!s.ok()) return s;
    }
  }
  return Status::Ok();
}

Status SourcesModel::Select(size_t source) {
  const std::vector<SourceRow> rows = Rows();
  if (source >= rows.size()) return Status::Error(kInvalidArg, "no source row " + std::to_string(source));
  return layers_.Select(rows[source].layers.front());
}

bool LayerExtent(const std::shared_ptr<Overlay>& overlay, GeoRect* out) {
  const std::shared_ptr<IEditableFeatureLayer> layer = Desk::FeatureLayerOf(overlay);
  return layer != nullptr && layer->Count() > 0 && layer->Extent(out).ok();
}

std::vector<PropertyRow> LayerProperties(OverlayManager& stack, Overlay& overlay) {
  std::vector<PropertyRow> out;
  const app::OverlayTypeDesc* d = DescOf(stack, overlay);
  out.push_back({"Layer", "Name", overlay.Name()});
  if (d != nullptr) out.push_back({"Layer", "Type", d->display_name});
  out.push_back({"Layer", "Visible", overlay.IsVisible() ? "Yes" : "No"});
  if (overlay.SupportsOpacity()) out.push_back({"Layer", "Opacity", std::to_string(overlay.opacity()) + "%"});

  const std::shared_ptr<Overlay> shared = stack.FindShared(&overlay);
  if (const std::shared_ptr<IEditableFeatureLayer> layer = shared ? Desk::FeatureLayerOf(shared) : nullptr) {
    const FeatureSchema& schema = layer->Schema();
    out.push_back({"Layer", "Geometry", GeometryName(schema.geometry_type)});
    out.push_back({"Layer", "Features", std::to_string(layer->Count())});
    std::string fields;
    for (const FieldDef& f : schema.fields) fields += (fields.empty() ? "" : ", ") + f.name;
    out.push_back({"Layer", "Fields", fields.empty() ? "None" : fields});
    GeoRect box;
    if (LayerExtent(shared, &box)) out.push_back({"Layer", "Extent", Extent(box)});
  }

  if (app::Persistence* p = overlay.AsPersistence()) {
    const app::SourceSpec spec = app::SplitSourceSpec(p->file_spec());
    if (spec.path.empty()) {
      out.push_back({"Source", "Path", "Not saved"});
    } else {
      FileRows(d, spec.path, &out);
      if (!spec.layer.empty()) out.push_back({"Source", "Table", spec.layer});
    }
    out.push_back({"Source", "Unsaved edits", p->is_dirty() ? "Yes" : "No"});
    if (p->is_read_only()) out.push_back({"Source", "Read-only", "Yes"});
  } else {
    out.push_back({"Source", "Path", "Built in"});
  }
  return out;
}

std::vector<PropertyRow> SourceProperties(OverlayManager& stack, const SourceRow& source) {
  std::vector<PropertyRow> out;
  const LayersModel layers(stack);
  const std::vector<LayerRow> rows = layers.Rows();
  if (source.layers.empty() || source.layers.front() >= rows.size()) return out;
  const std::shared_ptr<Overlay> top = stack.FindShared(rows[source.layers.front()].overlay);
  if (!top) return out;
  out.push_back({"Source", "Name", source.name});
  if (source.path.empty()) out.push_back({"Source", "Path", top->AsPersistence() ? "Not saved" : "Built in"});
  else FileRows(DescOf(stack, *top), source.path, &out);
  out.push_back({"Source", "Layers", std::to_string(source.layers.size())});
  for (size_t i : source.layers) {
    if (i >= rows.size()) continue;
    const std::shared_ptr<Overlay> o = stack.FindShared(rows[i].overlay);
    const std::shared_ptr<IEditableFeatureLayer> layer = o ? Desk::FeatureLayerOf(o) : nullptr;
    std::string what = rows[i].type_name;
    if (layer) what = GeometryName(layer->Schema().geometry_type) + ", " + std::to_string(layer->Count()) + " features";
    out.push_back({"Layers", rows[i].name, what});
  }
  return out;
}

}  // namespace desk
}  // namespace fv
