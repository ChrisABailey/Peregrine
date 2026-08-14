// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/app/type_registry.h"

#include <cctype>

#include "fvkit/overlay/grid.h"
#include "fvkit/overlay/point_overlay.h"

namespace fv {
namespace app {
namespace {

// Extensions compare case-insensitively and without a leading dot, so
// "RTE", ".rte" and "rte" are one extension. ASCII-only on purpose: a file
// extension that needs a locale is not a thing this dispatch should invent.
std::string NormalizeExt(const std::string& ext) {
  std::string out;
  out.reserve(ext.size());
  for (char c : ext) {
    if (out.empty() && c == '.') continue;  // strip one leading dot
    out.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

}  // namespace

const char kGridTypeId[] = "fv.grid";

Status OverlayTypeRegistry::Register(OverlayTypeDesc desc) {
  if (desc.id.empty()) {
    return Status::Error(kInvalidArg, "overlay type id is empty");
  }
  if (!desc.factory) {
    return Status::Error(kInvalidArg,
                         "overlay type '" + desc.id + "' has no factory");
  }
  if (by_id_.count(desc.id) != 0) {
    return Status::Error(kInvalidArg,
                         "overlay type '" + desc.id + "' already registered");
  }
  auto owned = std::make_unique<OverlayTypeDesc>(std::move(desc));
  by_id_[owned->id] = owned.get();
  order_.push_back(std::move(owned));
  return Status::Ok();
}

const OverlayTypeDesc* OverlayTypeRegistry::Find(const TypeId& id) const {
  auto it = by_id_.find(id);
  return it == by_id_.end() ? nullptr : it->second;
}

const OverlayTypeDesc* OverlayTypeRegistry::FindByExtension(
    const std::string& ext) const {
  const std::string want = NormalizeExt(ext);
  if (want.empty()) return nullptr;
  // Registration order, so the first claimant of an extension keeps it.
  for (const auto& d : order_) {
    if (!d->file) continue;
    if (NormalizeExt(d->file->default_extension) == want) return d.get();
  }
  return nullptr;
}

std::vector<const OverlayTypeDesc*> OverlayTypeRegistry::All() const {
  std::vector<const OverlayTypeDesc*> out;
  out.reserve(order_.size());
  for (const auto& d : order_) out.push_back(d.get());
  return out;
}

std::vector<const OverlayTypeDesc*> OverlayTypeRegistry::WithEditors() const {
  std::vector<const OverlayTypeDesc*> out;
  for (const auto& d : order_) {
    if (d->editor_factory) out.push_back(d.get());
  }
  return out;
}

bool OverlayTypeRegistry::IsStatic(const TypeId& id) const {
  const OverlayTypeDesc* d = Find(id);
  return d != nullptr && !d->file.has_value();
}

bool OverlayTypeRegistry::IsFile(const TypeId& id) const {
  const OverlayTypeDesc* d = Find(id);
  return d != nullptr && d->file.has_value();
}

Status RegisterBuiltinOverlayTypes(OverlayTypeRegistry& registry) {
  OverlayTypeDesc grid;
  grid.id = kGridTypeId;
  grid.display_name = "Lat/Lon Grid";
  grid.icon = "grid";
  // High, but not top-most: the graticule belongs over the map and under a
  // crosshair or a route being edited.
  grid.default_display_order = 900;
  grid.restore_at_startup = true;
  // No `file` => static: at most one, toggled (plan §1.2).
  grid.factory = [] { return std::make_shared<GridOverlay>(); };
  Status s = registry.Register(std::move(grid));
  if (!s.ok()) return s;

  // The first FILE type (A6), and the first C++ overlay that answers a pick.
  // Registered as a built-in for the same reason the grid is: a shell should
  // get the port's own overlays by calling one function, and this one is what
  // makes File > Open reachable before any shell has written a type of its own.
  OverlayTypeDesc points;
  points.id = PointOverlay::kTypeId;
  points.display_name = "Points";
  points.icon = "points";
  // Above the map and the graticule, below a route being edited (1000).
  points.default_display_order = 950;
  FileTypeDesc file;
  file.default_extension = PointOverlay::kExtension;
  file.open_filters = {{"Point Files (*.fvpoints)", "*.fvpoints"}};
  file.save_filters = file.open_filters;
  points.file = std::move(file);
  points.factory = [] { return std::make_shared<PointOverlay>(); };
  return registry.Register(std::move(points));
}

}  // namespace app
}  // namespace fv
