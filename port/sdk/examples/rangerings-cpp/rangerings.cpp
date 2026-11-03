// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// rangerings.cpp — the native plugin example: range rings as a C++ overlay
/// with an editor palette, a properties page and a tool, the same pieces a
/// built-in overlay has.
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "fv_desk_desk.h"
#include "fvkit/app/properties.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/overlay/manager.h"
#include "peregrine/fv_native.h"

namespace {

constexpr char kTypeId[] = "com.example.nativerings.rings";

/// Rings about each centre the user placed. Placing is armed by the editor's
/// Place Rings tool and spent by one click.
class RingsOverlay : public fv::Overlay, public fv::app::Properties {
 public:
  RingsOverlay() : fv::Overlay("Range Rings (C++)") {
    fv::app::PropertySpec count;
    count.key = "ring_count";
    count.label = "Rings";
    count.type = fv::app::PropertyType::kInt;
    count.default_value = fv::app::PropertyValue::Int(3);
    count.min = 1;
    count.max = 20;
    fv::app::PropertySpec spacing;
    spacing.key = "spacing_m";
    spacing.label = "Spacing (m)";
    spacing.type = fv::app::PropertyType::kDouble;
    spacing.default_value = fv::app::PropertyValue::Double(1000.0);
    spacing.min = 10;
    spacing.max = 1e6;
    specs_ = {count, spacing};
  }

  fv::app::Properties* AsProperties() override { return this; }

  const std::vector<fv::GeoPoint>& centers() const { return centers_; }
  void AddCenter(const fv::GeoPoint& p) { centers_.push_back(p); }
  void Clear() { centers_.clear(); }
  bool placing() const { return placing_; }
  void SetPlacing(bool on) { placing_ = on; }

  fv::Status OnDraw(const fv::MapProjection& proj, fv::ICanvas& canvas) override {
    {
      std::lock_guard<std::mutex> lock(proj_mutex_);
      last_proj_ = proj;
    }
    fv::GeoDraw draw(proj, &canvas);
    const fv::GeoLineStyle style = fv::SolidGeoLine(fv::FvColor{255, 0, 0, 255}, 2);
    for (const fv::GeoPoint& c : centers_)
      for (int i = 1; i <= ring_count_; ++i) {
        const fv::Status s = draw.DrawGeoCircle(c, i * spacing_m_, style);
        if (!s.ok() && s.code != fv::kNotProjectable) return s;
      }
    return fv::Status::Ok();
  }

  bool OnMouseDown(const fv::MouseEvent& e) override {
    if (!placing_ || e.button != 0) return false;
    std::lock_guard<std::mutex> lock(proj_mutex_);
    fv::GeoPoint at;
    if (!last_proj_ || !last_proj_->SurfaceToGeo(e.x, e.y, &at).ok()) return false;
    centers_.push_back(at);
    placing_ = false;
    return true;
  }

  // fv::app::Properties
  const std::vector<fv::app::PropertySpec>& Describe() const override { return specs_; }

  fv::Status GetProperty(const std::string& key, fv::app::PropertyValue* out) const override {
    if (key == "ring_count")
      *out = fv::app::PropertyValue::Int(ring_count_);
    else if (key == "spacing_m")
      *out = fv::app::PropertyValue::Double(spacing_m_);
    else
      return fv::Status::Error(fv::kNotFound, "no property " + key);
    return fv::Status::Ok();
  }

  fv::Status SetProperty(const std::string& key, const fv::app::PropertyValue& v) override {
    if (key == "ring_count" && v.type == fv::app::PropertyType::kInt && v.i >= 1 && v.i <= 20)
      ring_count_ = static_cast<int>(v.i);
    else if (key == "spacing_m" && v.type == fv::app::PropertyType::kDouble && v.d >= 10 && v.d <= 1e6)
      spacing_m_ = v.d;
    else
      return fv::Status::Error(fv::kInvalidArg, "bad value for " + key);
    return fv::Status::Ok();
  }

 private:
  std::vector<fv::app::PropertySpec> specs_;
  std::vector<fv::GeoPoint> centers_;
  int ring_count_ = 3;
  double spacing_m_ = 1000.0;
  bool placing_ = false;
  std::mutex proj_mutex_;
  std::optional<fv::MapProjection> last_proj_;  ///< mouse events map through the last draw
};

/// The palette shown while editing range rings: Place Rings and Clear Rings.
class RingsEditor : public fv::app::OverlayEditor {
 public:
  explicit RingsEditor(fv::desk::NativeEditorContext ctx) : ctx_(ctx) {}

  fv::Status Activate() override { return fv::Status::Ok(); }
  fv::Status Deactivate() override {
    if (RingsOverlay* o = Edited()) o->SetPlacing(false);
    return fv::Status::Ok();
  }

  std::vector<fv::app::MenuNode> Tools() const override {
    RingsOverlay* o = Edited();
    fv::app::MenuNode place;
    place.label = "Place Rings";
    place.icon = "add_point";
    place.enabled = o != nullptr;
    place.checked = o != nullptr && o->placing();
    place.action = [this] {
      if (RingsOverlay* r = Edited()) r->SetPlacing(!r->placing());
    };
    fv::app::MenuNode clear;
    clear.label = "Clear Rings";
    clear.icon = "delete_point";
    clear.enabled = o != nullptr && !o->centers().empty();
    clear.action = [this] {
      if (RingsOverlay* r = Edited()) r->Clear();
    };
    return {place, clear};
  }

 private:
  RingsOverlay* Edited() const {
    return ctx_.editors ? dynamic_cast<RingsOverlay*>(ctx_.editors->edited()) : nullptr;
  }
  fv::desk::NativeEditorContext ctx_;
};

/// Adds a ring set at the centre of the view to the topmost rings overlay.
fv::Status RingsAtCenter(fv::desk::ToolContext& ctx) {
  fv::desk::Desk& desk = ctx.desk();
  auto rings = std::dynamic_pointer_cast<RingsOverlay>(desk.overlays().FirstOfType(kTypeId));
  if (rings == nullptr) return fv::Status::Error(fv::kNotFound, "no Range Rings (C++) overlay is open");
  rings->AddCenter(desk.map_view().View().Center());
  desk.shell().RequestInvalidate();
  return fv::Status::Ok();
}

}  // namespace

FV_NATIVE_PLUGIN_BUILD_ID()

FV_NATIVE_EXPORT int fv_native_register(fv::desk::NativeRegistrar* r) {
  fv::desk::NativeOverlay rings;
  rings.factory = [] { return std::make_shared<RingsOverlay>(); };
  rings.editor_factory = [](const fv::desk::NativeEditorContext& ctx) {
    return std::make_unique<RingsEditor>(ctx);
  };
  if (!r->AddOverlay("rings", std::move(rings)).ok()) return 1;
  if (!r->AddTool("at_center", RingsAtCenter).ok()) return 1;
  return 0;
}
