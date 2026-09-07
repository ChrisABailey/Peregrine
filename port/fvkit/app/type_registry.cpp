// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/app/type_registry.h"

#include <cctype>

#include "fvkit/overlay/contour_overlay.h"
#include "fvkit/overlay/grid.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/overlay/ta_mask_overlay.h"

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

bool OverlayTypeRegistry::SetEditorFactory(
    const TypeId& id, std::function<std::unique_ptr<OverlayEditor>()> factory) {
  auto it = by_id_.find(id);
  if (it == by_id_.end()) return false;
  it->second->editor_factory = std::move(factory);
  return true;
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
  s = registry.Register(std::move(points));
  if (!s.ok()) return s;

  // Contour lines (plan C2). STATIC -- there is one terrain and it has no
  // document -- and it sits BELOW the graticule at 880, because contours are
  // part of the ground being described and a grid is drawn over the ground.
  //
  // NOT restored at startup, unlike the grid. A restored contour overlay with
  // no elevation source attached yet draws nothing and says nothing, which
  // looks exactly like a broken overlay; a user turning it on is the moment a
  // shell knows to attach a source.
  OverlayTypeDesc contour;
  contour.id = ContourOverlay::kTypeId;
  contour.display_name = "Contour Lines";
  contour.icon = "contour";
  contour.default_display_order = 880;
  contour.factory = [] { return std::make_shared<ContourOverlay>(); };
  s = registry.Register(std::move(contour));
  if (!s.ok()) return s;

  // The terrain avoidance mask (plan TA6). STATIC, like the contours and for
  // the same reason -- there is one terrain and it has no document -- and NOT
  // restored at startup, because an overlay with no elevation source attached
  // yet draws nothing and says nothing, which looks exactly like a broken
  // overlay.
  //
  // 890 puts it ABOVE the contours (880) and below the graticule: the two
  // describe the same ground, and the mask is a wash of colour that a contour
  // line drawn under it would disappear into.
  OverlayTypeDesc tamask;
  tamask.id = TAMaskOverlay::kTypeId;
  tamask.display_name = "Terrain Avoidance Mask";
  tamask.icon = "tamask";
  tamask.default_display_order = 890;
  tamask.factory = [] { return std::make_shared<TAMaskOverlay>(); };
  s = registry.Register(std::move(tamask));
  if (!s.ok()) return s;

  // The moving map (MM4). STATIC, like the grid and for the same reason: there
  // is one ship and it has no document. It is NOT restored at startup —
  // FalconView's own moving map is a mode the user enters, and an overlay that
  // came back by itself would start asking a receiver for fixes on every run.
  OverlayTypeDesc moving_map;
  moving_map.id = MovingMapOverlay::kTypeId;
  moving_map.display_name = "Moving Map";
  moving_map.icon = "movingmap";
  // Above the points and the graticule: the ship is what the user is watching,
  // and nothing on the chart should be drawn over it. Below a top-most HUD.
  moving_map.default_display_order = 980;
  moving_map.factory = [] { return std::make_shared<MovingMapOverlay>(); };
  return registry.Register(std::move(moving_map));
}

}  // namespace app
}  // namespace fv
