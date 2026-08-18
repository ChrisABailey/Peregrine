// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// pyfvw — Python bindings for FvKit (slice 1: L0 types + L1 adapters).
// Conventions per port/fvkit-contracts.md D3/D5:
//   - snake_case, parameterless getters become properties
//   - non-ok Status raises pyfvw.FvError (.code, .message); out-params
//     become return values — Python never sees Status
//   - PixelBuffer exposes the buffer protocol: np.asarray(buf) is a
//     zero-copy (h, w, 4) uint8 view
//   - GIL released around decode/I/O-bound calls
// Name mapping vs the legacy COM ICD: ICD-MAPPING.md alongside this file.

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "pyfvw_common.h"

#include "fvkit/app/capabilities.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/engine.h"
#include "fvkit/overlay/grid.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/proj.h"
#include "fvkit/settings.h"
#include "fvkit/store/tile_pack.h"
#include "fvkit/formats/cadrg.h"
#include "fvkit/formats/dted.h"
#include "fvkit/formats/geotiff.h"
#include "fvkit/formats/registry.h"
#include "fvkit/formats/tiros.h"
#include "fvkit/formats/vpf.h"
#include "fvkit/geo.h"
#include "fvkit/geo/contour.h"
#include "fvkit/raster.h"
#include "fvkit/vector/vector.h"
#include "fvkit/vector/families.h"
#include "fvkit/vector/style.h"
#include "fvkit/vector/renderer.h"

#include "fv_vpf_vector_source.h"  // fv::VpfVectorSource
#include "fv_geosym_style.h"       // fv::GeoSymStyleEngine
#include "fv_enc_vector_source.h"  // fv::EncVectorSource
#include "fv_s52_style.h"          // fv::S52StyleEngine
#include "fv_enc_format.h"         // fv::RegisterEncFormat
#include "fv_osm_vector_source.h"  // fv::OsmVectorSource
#include "fv_osm_style.h"          // fv::OsmStyleEngine
#include "fv_osm_format.h"         // fv::RegisterOsmFormat

#include "fv_road_graph.h"   // fv::routing::RoadGraph (O4)
#include "fv_route_rules.h"  // fv::routing::RouteRulesFile (O5c)
#include "fv_router.h"       // fv::routing::Router

#include "geo_tool.h"  // GEO_string_to_lat_lon (fv_geo_tool)

namespace py = pybind11;
using namespace pybind11::literals;

namespace {

// The Status carrier and the "a Python overlay as a shared_ptr" helper both
// moved into pyfvw_common.h when A6 split pyfvw.app into its own translation
// unit -- the translator below still re-raises the same type, and it has to be
// the SAME type in both TUs for the catch to match.
using pyfvw::FvErrorCpp;
using pyfvw::OverlayFromPython;
using pyfvw::ThrowIfError;
}  // namespace

namespace pyfvw {
// pyfvw_app.cpp — the whole of fv::app, big enough to be its own TU (A6).
void BindApp(py::module_& m);
// pyfvw_draw.cpp — pyfvw.symbol + pyfvw.draw, the G2/G3 surface.
void BindDraw(py::module_& m);
// pyfvw_nav.cpp — pyfvw.nav, the moving map (MM1-MM4).
void BindNav(py::module_& m);
}  // namespace pyfvw

namespace {

// Trampoline: Python exceptions never cross the SPI (contracts D3) —
// on_draw errors become failed Status, event-handler errors log + decline.
//
// A6 made it the CAPABILITY carrier as well, and the shape needs explaining.
// A C++ overlay opts into a capability by overriding one accessor and
// returning `this` (rule R2); a Python subclass cannot return a C++ interface
// pointer, and `dynamic_cast` across a trampoline is exactly what R2 exists to
// avoid. So the trampoline inherits EVERY capability and answers each accessor
// by asking whether the Python subclass defined the methods that capability
// needs: `file_open` makes an overlay persistent, `hit_test_point` makes it
// pickable, and so on. The state a capability carries (Persistence's dirty
// flag and file spec) then lives in C++, where the stack's broadcast and the
// session flows already expect it.
//
// The answer is CACHED per instance: an accessor is consulted on every mouse
// move and a py::hasattr is a dictionary walk. The cost of caching is that
// adding a method to an overlay's class after it exists does not grant it a
// capability, which no reasonable program does and every reasonable one would
// find confusing if it half-worked.
class PyOverlay : public fv::Overlay,
                  public fv::app::Persistence,
                  public fv::app::HitTest,
                  public fv::app::SnapTo,
                  public fv::app::ContextMenu,
                  public fv::app::RoutingOverrides,
                  public fv::app::EditTarget {
 public:
  using fv::Overlay::Overlay;

  // --- capability discovery (A6) ---------------------------------------

  fv::app::Persistence* AsPersistence() override {
    return Capable(kCapPersistence, {"file_open", "file_new", "file_save_as"})
               ? this
               : nullptr;
  }
  fv::app::HitTest* AsHitTest() override {
    return Capable(kCapHitTest, {"hit_test_point"}) ? this : nullptr;
  }
  fv::app::SnapTo* AsSnapTo() override {
    return Capable(kCapSnapTo, {"snap_to_point"}) ? this : nullptr;
  }
  fv::app::ContextMenu* AsContextMenu() override {
    return Capable(kCapContextMenu, {"menu_items"}) ? this : nullptr;
  }
  fv::app::RoutingOverrides* AsRoutingOverrides() override {
    return Capable(kCapRouting, {"wants_direct_routing"}) ? this : nullptr;
  }
  fv::app::EditTarget* AsEditTarget() override {
    return Capable(kCapEditTarget,
                   {"enter_edit_focus", "release_edit_focus", "undo", "redo"})
               ? this
               : nullptr;
  }

  // --- Persistence ------------------------------------------------------

  fv::Status FileNew() override { return CallStatus("file_new"); }
  fv::Status FileOpen(const std::string& spec) override {
    return CallStatus("file_open", spec);
  }
  fv::Status FileSaveAs(const std::string& spec, int format_index) override {
    return CallStatus("file_save_as", spec, format_index);
  }
  bool SupportsRevert() const override {
    py::gil_scoped_acquire gil;
    return (bool)py::get_override(this, "revert");
  }
  fv::Status Revert(const std::string& spec) override {
    return CallStatus("revert", spec);
  }

  // --- HitTest / SnapTo / ContextMenu -----------------------------------

  void HitTestPoint(const fv::MapProjection& proj, fv::PixelPoint p,
                    double tolerance_px,
                    std::vector<fv::app::HitItem>& out) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "hit_test_point");
    if (!o) return;
    try {
      py::object hits = o(py::cast(proj, py::return_value_policy::reference),
                          p.x, p.y, tolerance_px);
      if (hits.is_none()) return;
      for (py::handle h : hits) {
        fv::app::HitItem item = h.cast<fv::app::HitItem>();
        // Stamped here, never in Python: an overlay must not be able to
        // attribute a hit to somebody else's overlay.
        item.overlay = this;
        out.push_back(std::move(item));
      }
    } catch (py::error_already_set&) {
      PyErr_Clear();
    }
  }

  void SnapToPoint(const fv::MapProjection& proj, fv::PixelPoint p,
                   double tolerance_px,
                   std::vector<fv::app::SnapToItem>& out) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "snap_to_point");
    if (!o) return;
    try {
      py::object items = o(py::cast(proj, py::return_value_policy::reference),
                           p.x, p.y, tolerance_px);
      if (items.is_none()) return;
      for (py::handle h : items) {
        fv::app::SnapToItem item = h.cast<fv::app::SnapToItem>();
        item.overlay = this;
        out.push_back(std::move(item));
      }
    } catch (py::error_already_set&) {
      PyErr_Clear();
    }
  }

  void AppendMenuItems(const fv::MapProjection& proj, fv::PixelPoint p,
                       fv::app::MenuNode& menu) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "menu_items");
    if (!o) return;
    try {
      // Python RETURNS its section rather than mutating a menu passed in:
      // MenuNode.children is a value member, so a mutation through a binding
      // would land on a copy and vanish.
      py::object items = o(py::cast(proj, py::return_value_policy::reference),
                           p.x, p.y);
      if (items.is_none()) return;
      for (py::handle h : items)
        menu.children.push_back(h.cast<fv::app::MenuNode>());
    } catch (py::error_already_set&) {
      PyErr_Clear();
    }
  }

  // --- RoutingOverrides / EditTarget ------------------------------------

  bool WantsDirectRouting() const override {
    return CallBool("wants_direct_routing");
  }
  void EnterEditFocus() override { CallVoid("enter_edit_focus"); }
  void ReleaseEditFocus() override { CallVoid("release_edit_focus"); }
  bool CanUndo() const override { return CallBool("can_undo"); }
  void Undo() override { CallVoid("undo"); }
  bool CanRedo() const override { return CallBool("can_redo"); }
  void Redo() override { CallVoid("redo"); }

  fv::Status OnDraw(const fv::MapProjection& proj,
                    fv::ICanvas& canvas) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "on_draw");
    if (!o) return fv::Status::Ok();
    try {
      o(py::cast(proj, py::return_value_policy::reference),
        py::cast(canvas, py::return_value_policy::reference));
      return fv::Status::Ok();
    } catch (py::error_already_set& e) {
      return fv::Status::Error(fv::kInternal,
                               std::string("python overlay: ") + e.what());
    }
  }

  bool OnMouseDown(const fv::MouseEvent& e) override {
    return Event("on_mouse_down", e);
  }
  bool OnMouseUp(const fv::MouseEvent& e) override {
    return Event("on_mouse_up", e);
  }
  bool OnMouseMove(const fv::MouseEvent& e) override {
    return Event("on_mouse_move", e);
  }
  bool OnDoubleClick(const fv::MouseEvent& e) override {
    return Event("on_double_click", e);
  }
  bool OnMouseWheel(const fv::MouseEvent& e, double delta) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "on_mouse_wheel");
    if (!o) return false;
    try {
      return py::cast<bool>(o(e, delta));
    } catch (py::error_already_set& err) {
      PyErr_Clear();
      return false;
    }
  }
  bool OnKeyDown(const fv::KeyEvent& e) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "on_key_down");
    if (!o) return false;
    try {
      return py::cast<bool>(o(e));
    } catch (py::error_already_set& err) {
      PyErr_Clear();
      return false;
    }
  }

 private:
  enum CapSlot {
    kCapPersistence,
    kCapHitTest,
    kCapSnapTo,
    kCapContextMenu,
    kCapRouting,
    kCapEditTarget,
    kCapCount,
  };

  bool Event(const char* name, const fv::MouseEvent& e) {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, name);
    if (!o) return false;
    try {
      return py::cast<bool>(o(e));
    } catch (py::error_already_set& err) {
      PyErr_Clear();
      return false;
    }
  }

  // Any ONE of the names is enough: a partial capability is still a
  // capability, and the missing methods answer kUnsupported at the point
  // where somebody actually calls them.
  bool Capable(CapSlot slot, std::initializer_list<const char*> names) {
    if (cap_[slot] >= 0) return cap_[slot] != 0;
    py::gil_scoped_acquire gil;
    char answer = 0;
    for (const char* n : names) {
      if (py::get_override(this, n)) {
        answer = 1;
        break;
      }
    }
    cap_[slot] = answer;
    return answer != 0;
  }

  template <typename... Args>
  fv::Status CallStatus(const char* name, Args&&... args) {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, name);
    if (!o)
      return fv::Status::Error(
          fv::kUnsupported,
          std::string("overlay '") + Name() + "' has no " + name);
    try {
      o(std::forward<Args>(args)...);
      return fv::Status::Ok();
    } catch (py::error_already_set& e) {
      // A raising handler IS the failure path: a Python overlay says "could
      // not open that" by raising, the way Python code says everything. An
      // exception carrying a `code` (pyfvw.FvError does) keeps it, so an
      // overlay can report kNotFound and have the flow read it as such.
      int code = fv::kInternal;
      try {
        if (py::hasattr(e.value(), "code"))
          code = e.value().attr("code").cast<int>();
      } catch (py::error_already_set&) {
        PyErr_Clear();
      }
      return fv::Status::Error(code, std::string("python overlay ") + name +
                                         ": " + e.what());
    }
  }

  bool CallBool(const char* name) const {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, name);
    if (!o) return false;
    try {
      return py::cast<bool>(o());
    } catch (py::error_already_set&) {
      PyErr_Clear();
      return false;
    }
  }

  void CallVoid(const char* name) {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, name);
    if (!o) return;
    try {
      o();
    } catch (py::error_already_set&) {
      PyErr_Clear();
    }
  }

  // -1 = not asked yet. Mutable-free: every accessor is non-const.
  signed char cap_[kCapCount] = {-1, -1, -1, -1, -1, -1};
};

PyObject* g_fv_error = nullptr;  // pyfvw.FvError type (owned by the module)

template <typename Enumerator>
void BindEnumerator(py::module_& m, const char* name, const char* doc) {
  py::class_<Enumerator, std::shared_ptr<Enumerator>>(m, name, doc)
      .def(py::init<>())
      .def(
          "frames",
          [](Enumerator& e, const std::string& dir) {
            fv::Status s;
            std::vector<fv::FrameInfo> v;
            {
              py::gil_scoped_release release;
              s = e.Begin(dir);
              if (s.ok()) {
                fv::FrameInfo f;
                while (e.Next(&f)) v.push_back(f);
              }
            }
            ThrowIfError(s);
            return v;
          },
          "dir"_a,
          "Scan one data directory; returns the full frame list, sorted by "
          "path.");
}

// Binds an IRasterSource subclass with the standard method set (open, bounds,
// info, read_block -> PixelBuffer, pixel_to_geo, geo_to_pixel), GIL released
// around Open/ReadBlock.
template <typename Source>
py::class_<Source, fv::IRasterSource, std::shared_ptr<Source>> BindRasterSource(py::module_& m,
                                                             const char* name,
                                                             const char* doc) {
  return py::class_<Source, fv::IRasterSource, std::shared_ptr<Source>>(m, name, doc)
      .def(py::init<>())
      .def(
          "open",
          [](Source& s, const std::string& path) {
            fv::Status st;
            {
              py::gil_scoped_release release;
              st = s.Open(path);
            }
            ThrowIfError(st);
          },
          "path"_a)
      .def_property_readonly("bounds", &Source::Bounds)
      .def_property_readonly("info",
                             [](const Source& s) {
                               fv::ImageInfo i;
                               ThrowIfError(s.Info(&i));
                               return i;
                             })
      .def(
          "read_block",
          [](Source& s, int x, int y, int width, int height) {
            auto buf = std::make_unique<fv::PixelBuffer>();
            fv::Status st;
            {
              py::gil_scoped_release release;
              st = s.ReadBlock(fv::PixelRect{x, y, width, height}, buf.get());
            }
            ThrowIfError(st);
            return buf;
          },
          "x"_a, "y"_a, "width"_a, "height"_a,
          "Decode a block (must lie entirely inside the image) to a "
          "PixelBuffer; np.asarray() it for a zero-copy (h, w, 4) view.")
      .def(
          "pixel_to_geo",
          [](const Source& s, double px, double py_) {
            fv::GeoPoint p;
            ThrowIfError(s.PixelToGeo(px, py_, &p));
            return p;
          },
          "px"_a, "py"_a)
      .def(
          "geo_to_pixel",
          [](const Source& s, const fv::GeoPoint& p) {
            double px = 0, py_ = 0;
            ThrowIfError(s.GeoToPixel(p, &px, &py_));
            return py::make_tuple(px, py_);
          },
          "p"_a);
}

// The route rule files this process has been asked for, one watcher each
// (O5c). A watcher has to OUTLIVE the call that used it or nothing is ever
// reloaded — its whole job is to remember the timestamp it last saw — so they
// live here rather than being made per route() call. One per path, so the
// application polls once per file however many routers it has.
fv::routing::RouteRulesFile& RulesFileFor(const std::string& path) {
  static std::mutex mutex;
  static std::map<std::string, std::unique_ptr<fv::routing::RouteRulesFile>> files;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = files.find(path);
  if (it == files.end()) {
    it = files.emplace(path, std::make_unique<fv::routing::RouteRulesFile>(path)).first;
  }
  return *it->second;
}

// Resolves `profile` against `rules_path` onto `options`. An empty profile
// name leaves the options as the boolean arguments set them, which is the
// pre-O5c behaviour and what every existing caller gets.
void ApplyProfile(const std::string& rules_path, const std::string& profile,
                  fv::routing::RouteOptions* options) {
  if (profile.empty()) return;
  const std::shared_ptr<const fv::routing::RouteRules> rules =
      rules_path.empty() ? fv::routing::RouteRules::Builtin()
                         : RulesFileFor(rules_path).rules();
  const fv::Status s = fv::routing::SelectProfile(rules, profile, options);
  if (!s.ok()) throw FvErrorCpp{s};
}

// A toll/ferry avoidance argument (O5e). None leaves whatever is already
// there — the profile's default, or the builtin 1.0 — which is what makes the
// argument optional without a second "did the caller say?" flag. False and
// "exclude" are the same refusal the rule file spells those two ways; a number
// is a multiplier and must be positive, since 0 would make the arc free.
void ApplyAvoidance(const py::object& value, const char* what, double* slot) {
  if (value.is_none()) return;
  if (py::isinstance<py::bool_>(value)) {
    if (value.cast<bool>())
      throw FvErrorCpp{fv::Status::Error(
          fv::kInvalidArg, std::string(what) + ": True is not a penalty; give a number")};
    *slot = fv::routing::kAvoidExcluded;
    return;
  }
  if (py::isinstance<py::str>(value)) {
    if (value.cast<std::string>() != "exclude")
      throw FvErrorCpp{fv::Status::Error(
          fv::kInvalidArg, std::string(what) + ": expected a number, False, or 'exclude'")};
    *slot = fv::routing::kAvoidExcluded;
    return;
  }
  const double v = value.cast<double>();
  if (!(v > 0.0))
    throw FvErrorCpp{fv::Status::Error(
        fv::kInvalidArg, std::string(what) + ": must be greater than zero (1.0 = no preference)")};
  *slot = v;
}

// Router.route and Router.route_via take the same cost arguments and must read
// them the same way — above all ApplyProfile LAST, so a named profile decides
// everything the arguments before it would have, and so the rule file is
// consulted (and reloaded if it has changed) on THIS call.
fv::routing::RouteOptions RouteOptionsFrom(bool driving, const std::string& metric,
                                           double snap_meters, bool bidirectional,
                                           bool cycle_only, double private_penalty,
                                           py::object toll_penalty, py::object ferry_penalty,
                                           const std::string& profile, const std::string& rules) {
  fv::routing::RouteOptions options;
  options.driving = driving;
  options.cycle_only = cycle_only;
  options.snap_meters = snap_meters;
  options.private_penalty = private_penalty;
  options.bidirectional = bidirectional;
  if (metric == "time") {
    options.metric = fv::routing::RouteMetric::kTime;
  } else if (metric == "distance") {
    options.metric = fv::routing::RouteMetric::kDistance;
  } else {
    throw FvErrorCpp{
        fv::Status::Error(fv::kInvalidArg, "metric must be 'time' or 'distance'")};
  }
  ApplyProfile(rules, profile, &options);
  // After the profile, deliberately: these two are the ones a caller overrides
  // per query ("this profile, but no ferries today"), so the argument has to
  // outrank the profile's default rather than be overwritten by it.
  ApplyAvoidance(toll_penalty, "toll_penalty", &options.toll_penalty);
  ApplyAvoidance(ferry_penalty, "ferry_penalty", &options.ferry_penalty);
  return options;
}

}  // namespace

PYBIND11_MODULE(pyfvw, m) {
  m.doc() =
      "FalconView portable core (FvKit) bindings — slice 1: geo primitives, "
      "DTED elevation, GeoTIFF rasters.";
  m.attr("__version__") = "0.1.0";

  // ---- FvError ------------------------------------------------------------
  g_fv_error = PyErr_NewException("pyfvw.FvError", PyExc_RuntimeError, nullptr);
  m.attr("FvError") = py::reinterpret_borrow<py::object>(g_fv_error);
  py::register_exception_translator([](std::exception_ptr p) {
    try {
      if (p) std::rethrow_exception(p);
    } catch (const FvErrorCpp& e) {
      py::object type = py::reinterpret_borrow<py::object>(g_fv_error);
      py::object inst = type(e.status.code, e.status.message);
      inst.attr("code") = py::cast(e.status.code);
      inst.attr("message") = py::cast(e.status.message);
      PyErr_SetObject(g_fv_error, inst.ptr());
    }
  });

  // Status codes as module constants (values from fvkit/geo.h)
  m.attr("OK") = (int)fv::kOk;
  m.attr("INVALID_ARG") = (int)fv::kInvalidArg;
  m.attr("NOT_FOUND") = (int)fv::kNotFound;
  m.attr("IO_ERROR") = (int)fv::kIoError;
  m.attr("UNSUPPORTED") = (int)fv::kUnsupported;
  m.attr("OUT_OF_COVERAGE") = (int)fv::kOutOfCoverage;

  // ---- pyfvw.geo ----------------------------------------------------------
  py::module_ geo = m.def_submodule("geo", "Geographic primitives (WGS-84 "
                                           "decimal degrees, lat before lon)");

  py::class_<fv::GeoPoint>(geo, "GeoPoint")
      .def(py::init<>())
      .def(py::init([](double lat, double lon) {
             return fv::GeoPoint{lat, lon};
           }),
           "lat"_a, "lon"_a)
      .def_readwrite("lat", &fv::GeoPoint::lat)
      .def_readwrite("lon", &fv::GeoPoint::lon)
      .def("normalize", &fv::GeoPoint::Normalize,
           "Canonicalize: lon into (-180, +180], lat clamped to [-90, +90].")
      .def("__repr__", [](const fv::GeoPoint& p) {
        return "GeoPoint(lat=" + std::to_string(p.lat) +
               ", lon=" + std::to_string(p.lon) + ")";
      });

  py::class_<fv::GeoRect>(geo, "GeoRect",
                          "ll.lon > ur.lon means the rect crosses the "
                          "antimeridian (FalconView convention).")
      .def(py::init<>())
      .def(py::init([](const fv::GeoPoint& ll, const fv::GeoPoint& ur) {
             return fv::GeoRect{ll, ur};
           }),
           "ll"_a, "ur"_a)
      .def_readwrite("ll", &fv::GeoRect::ll)
      .def_readwrite("ur", &fv::GeoRect::ur)
      .def_property_readonly("crosses_antimeridian",
                             &fv::GeoRect::CrossesAntimeridian)
      .def("contains", &fv::GeoRect::Contains, "p"_a)
      .def("intersects", &fv::GeoRect::Intersects, "other"_a)
      .def_static("world", &fv::GeoRect::World)
      .def("__repr__", [](const fv::GeoRect& r) {
        return "GeoRect(ll=(" + std::to_string(r.ll.lat) + ", " +
               std::to_string(r.ll.lon) + "), ur=(" + std::to_string(r.ur.lat) +
               ", " + std::to_string(r.ur.lon) + "))";
      });

  geo.def("normalize_lon", &fv::NormalizeLon, "lon"_a,
          "Map an arbitrary longitude into (-180, +180]; +/-180 -> +180.");

  geo.def(
      "parse_location",
      [](const std::string& text, const std::string& datum) {
        degrees_t lat = 0, lon = 0;
        int rc = GEO_string_to_lat_lon(text.c_str(), datum.c_str(), &lat, &lon);
        if (rc != 0)
          throw FvErrorCpp{fv::Status::Error(
              fv::kInvalidArg, "could not parse location \"" + text +
                                   "\" (geo_tool code " + std::to_string(rc) +
                                   ")")};
        return fv::GeoPoint{lat, lon};
      },
      "text"_a, "datum"_a = "WGE",
      "Parse a location string to a WGS-84 GeoPoint. Accepts decimal or "
      "degrees-minutes-seconds lat/lon and MGRS/milgrid; datum is a GEOTRANS "
      "5-char code (default WGE = WGS-84). Needs MSPCCS_DATA set. Raises "
      "FvError on an unparseable string.");

  // ---- geographic contours (G1) -------------------------------------------
  //
  // The pull-iterator seam itself is deliberately NOT bound. An IGeoContour is
  // walked point by point and a per-point call across the binding would cost
  // more than the geodesy it is calling; what a caller actually wants is the
  // finished sub-paths, so every contour is exposed as one *_path function
  // that builds it and projects it in a single crossing. `_points` is the
  // exception, and exists for asserting geography rather than for drawing.
  //
  // A SUB-PATH is a run of (x, y) surface points to be stroked as one
  // polyline. There is more than one when the contour leaves and re-enters the
  // viewport, when a point fails to project, or at the antimeridian seam --
  // and joining two sub-paths would draw a line the geometry does not have.
  auto paths_to_py =
      [](const std::vector<std::vector<fv::SurfacePoint>>& in) {
        std::vector<std::vector<std::pair<double, double>>> out;
        out.reserve(in.size());
        for (const auto& sub : in) {
          std::vector<std::pair<double, double>> path;
          path.reserve(sub.size());
          for (const auto& s : sub) path.emplace_back(s.x, s.y);
          out.push_back(std::move(path));
        }
        return out;
      };

  auto contour_to_py = [paths_to_py](const fv::MapProjection& proj,
                                     fv::IGeoContour& c) {
    return paths_to_py(fv::BuildGeoPath(proj, c));
  };

  py::enum_<fv::LineKind>(geo, "LineKind",
                          "How the space between two geographic points is "
                          "filled in.")
      .value("SIMPLE", fv::LineKind::kSimple,
             "Straight in the CURRENT projection: the endpoints are projected "
             "and joined.")
      .value("RHUMB", fv::LineKind::kRhumb,
             "Constant bearing (loxodrome) -- straight on a Mercator.")
      .value("GREAT_CIRCLE", fv::LineKind::kGreatCircle,
             "Shortest path over the sphere.");

  geo.def(
      "line_path",
      [contour_to_py](const fv::MapProjection& proj, const fv::GeoPoint& a,
                      const fv::GeoPoint& b, fv::LineKind kind, bool clip) {
        fv::GeoContourPtr c = fv::MakeGeoLine(proj, a, b, kind, clip);
        return contour_to_py(proj, *c);
      },
      "proj"_a, "a"_a, "b"_a, "kind"_a = fv::LineKind::kGreatCircle,
      "clip"_a = true,
      "A two-point line as surface sub-paths: [[(x, y), ...], ...].\n\n"
      "The step size comes from the projection's degrees-per-pixel (~20-pixel "
      "chords), so the point count tracks the SCREEN and not the line's "
      "length. With clip=True the shape is clipped in GEOGRAPHIC space before "
      "it is densified, so an intercontinental arc on a harbour map costs a "
      "search rather than a walk -- leave it on unless you need points "
      "outside the viewport.");

  geo.def(
      "line_points",
      [](const fv::MapProjection& proj, const fv::GeoPoint& a,
         const fv::GeoPoint& b, fv::LineKind kind, bool clip) {
        fv::GeoContourPtr c = fv::MakeGeoLine(proj, a, b, kind, clip);
        std::vector<fv::GeoPoint> out;
        c->MoveFirst();
        fv::GeoPoint p;
        while (c->NextPoint(&p)) out.push_back(p);
        return out;
      },
      "proj"_a, "a"_a, "b"_a, "kind"_a = fv::LineKind::kGreatCircle,
      "clip"_a = true,
      "The same line as GEOGRAPHIC points, unprojected. For asserting "
      "geography (does the arc bow poleward?); line_path is what you draw.");

  geo.def(
      "polyline_path",
      [contour_to_py](const fv::MapProjection& proj,
                      std::vector<fv::GeoPoint> points, fv::LineKind kind,
                      bool clip, bool closed) {
        fv::PolylineContour c(proj, std::move(points), kind, clip, closed);
        return contour_to_py(proj, c);
      },
      "proj"_a, "points"_a, "kind"_a = fv::LineKind::kGreatCircle,
      "clip"_a = true, "closed"_a = false,
      "A run of points with ONE kind for every leg -- a route, a coastline, a "
      "hand-drawn shape. Legs are densified one at a time and never "
      "materialised whole. A leg that clips away BREAKS the run, which is why "
      "this returns sub-paths and not one list.");

  geo.def(
      "circle_path",
      [contour_to_py](const fv::MapProjection& proj,
                      const fv::GeoPoint& center, double radius_m,
                      int num_points) {
        fv::GeoCircleContour c(center, radius_m, num_points);
        return contour_to_py(proj, c);
      },
      "proj"_a, "center"_a, "radius_m"_a,
      "num_points"_a = fv::kDefaultCirclePoints,
      "A circle of constant GROUND radius, closed. Not clipped: it is bounded "
      "by construction and num_points is small.");

  geo.def(
      "ellipse_path",
      [contour_to_py](const fv::MapProjection& proj,
                      const fv::GeoPoint& center, double vert_radius_m,
                      double horz_radius_m, double rotation_deg,
                      int num_points) {
        fv::GeoEllipseContour c(center, vert_radius_m, horz_radius_m,
                                rotation_deg, num_points);
        return contour_to_py(proj, c);
      },
      "proj"_a, "center"_a, "vert_radius_m"_a, "horz_radius_m"_a,
      "rotation_deg"_a = 0.0, "num_points"_a = fv::kDefaultCirclePoints,
      "rotation_deg turns the ellipse clockwise from north, the bearing "
      "convention the radii are stated in.");

  geo.def(
      "arc_path",
      [contour_to_py](const fv::MapProjection& proj,
                      const fv::GeoPoint& center, double radius_m,
                      double start_bearing_deg, double sweep_deg,
                      int points_per_circle) {
        fv::GeoArcContour c(center, radius_m, start_bearing_deg, sweep_deg,
                            points_per_circle);
        return contour_to_py(proj, c);
      },
      "proj"_a, "center"_a, "radius_m"_a, "start_bearing_deg"_a, "sweep_deg"_a,
      "points_per_circle"_a = fv::kDefaultCirclePoints,
      "A circular arc, clockwise from start_bearing_deg through sweep_deg "
      "(negative sweeps run counter-clockwise, |sweep| is clamped to 360). "
      "The spacing matches a full circle of the same radius, so a quarter arc "
      "gets a quarter of the points rather than the same number crammed in. "
      "Not closed.");

  // ---- raster types (top level) --------------------------------------------
  py::class_<fv::PixelBuffer>(m, "PixelBuffer", py::buffer_protocol(),
                              "Owning interleaved RGBA8, top-down. "
                              "np.asarray(buf) is a zero-copy (h, w, 4) view.")
      .def_property_readonly("width", &fv::PixelBuffer::Width)
      .def_property_readonly("height", &fv::PixelBuffer::Height)
      .def_property_readonly("stride_bytes", &fv::PixelBuffer::StrideBytes)
      .def_buffer([](fv::PixelBuffer& b) -> py::buffer_info {
        return py::buffer_info(
            b.Data(), 1, py::format_descriptor<unsigned char>::format(), 3,
            {(py::ssize_t)b.Height(), (py::ssize_t)b.Width(), (py::ssize_t)4},
            {(py::ssize_t)b.StrideBytes(), (py::ssize_t)4, (py::ssize_t)1});
      });

  py::class_<fv::ImageInfo>(m, "ImageInfo")
      .def_property_readonly(
          "width", [](const fv::ImageInfo& i) { return i.size.width; })
      .def_property_readonly(
          "height", [](const fv::ImageInfo& i) { return i.size.height; })
      .def_readonly("bounds", &fv::ImageInfo::bounds);

  // ---- pyfvw.Settings -------------------------------------------------------
  // The port's registry replacement: a hand-edited INI read at startup. See
  // port/include/fvkit/settings.h and port/peregrine.ini.sample.
  py::class_<fv::Settings>(m, "Settings")
      .def(py::init<>())
      .def("load",
           [](fv::Settings& s, const std::string& path) {
             // '' means "wherever you normally look", so a caller with an
             // optional --settings flag has one call site, not two.
             ThrowIfError(path.empty() ? s.LoadDefault() : s.Load(path));
           },
           "path"_a = std::string(),
           "Read a named file (FvError if missing); '' searches the default "
           "path and succeeds even when nothing is found.")
      .def("load_default",
           [](fv::Settings& s) { ThrowIfError(s.LoadDefault()); },
           "Read the first file in default_paths() that exists. Finding none "
           "is not an error.")
      .def("load_string",
           [](fv::Settings& s, const std::string& text) {
             ThrowIfError(s.LoadFromString(text));
           },
           "text"_a)
      .def_property_readonly("path", &fv::Settings::path)
      .def("has", &fv::Settings::Has, "key"_a)
      .def("get", &fv::Settings::GetString, "key"_a, "default"_a = std::string())
      .def("get_float", &fv::Settings::GetDouble, "key"_a, "default"_a)
      .def("get_int", &fv::Settings::GetInt, "key"_a, "default"_a)
      .def("get_bool", &fv::Settings::GetBool, "key"_a, "default"_a)
      .def("set", &fv::Settings::Set, "key"_a, "value"_a)
      .def("keys", &fv::Settings::Keys)
      .def_property_readonly("warnings", &fv::Settings::warnings)
      .def("clear_warnings", &fv::Settings::ClearWarnings);

  m.def("default_settings_paths", &fv::DefaultSettingsPaths,
        "Where load_default() looks, in order. Returned whether or not they "
        "exist, so an app can say 'put it in one of these'.");

  // ---- pyfvw.formats --------------------------------------------------------
  py::module_ formats =
      m.def_submodule("formats", "Format adapters over the ported readers");

  py::class_<fv::FrameInfo>(formats, "FrameInfo")
      .def_readonly("path", &fv::FrameInfo::path)
      .def_readonly("bounds", &fv::FrameInfo::bounds)
      .def_readonly("series_key", &fv::FrameInfo::series_key)
      .def_readonly("edition", &fv::FrameInfo::edition)
      .def_readonly("size_bytes", &fv::FrameInfo::size_bytes)
      .def("__repr__", [](const fv::FrameInfo& f) {
        return "FrameInfo(" + f.series_key + ", " + f.path + ")";
      });

  BindEnumerator<fv::DtedFrameEnumerator>(
      formats, "DtedFrameEnumerator",
      "Walks a DTED tree (dted/w082/n31.dt1); bounds derive from the path.");
  BindEnumerator<fv::GeoTiffFrameEnumerator>(
      formats, "GeoTiffFrameEnumerator",
      "Lists *.tif in a directory; reads each header for coverage "
      "(needs MSPCCS_DATA for the datum shift).");

  // abstract interface bases (D1): registered so derived classes convert
  py::class_<fv::IElevationSource, std::shared_ptr<fv::IElevationSource>>(
      formats, "IElevationSource");
  py::class_<fv::IRasterSource, std::shared_ptr<fv::IRasterSource>>(
      formats, "IRasterSource");

  py::class_<fv::DtedElevationSource, fv::IElevationSource,
             std::shared_ptr<fv::DtedElevationSource>>(
      formats, "DtedElevationSource",
      "Elevation queries over a DTED tree. Meters as float; DTED void posts "
      "come back as NaN; a point outside coverage raises FvError "
      "(OUT_OF_COVERAGE).")
      .def(py::init([](const std::string& root_dir) {
             std::shared_ptr<fv::DtedElevationSource> p;
             {
               py::gil_scoped_release release;
               p = std::make_shared<fv::DtedElevationSource>(root_dir);
             }
             return p;
           }),
           "root_dir"_a)
      .def_property_readonly("bounds", &fv::DtedElevationSource::Bounds)
      .def(
          "get_elevation",
          [](fv::DtedElevationSource& s, double lat, double lon) {
            float e = 0;
            fv::Status st;
            {
              py::gil_scoped_release release;
              st = s.GetElevation(fv::GeoPoint{lat, lon}, &e);
            }
            ThrowIfError(st);
            return e;
          },
          "lat"_a, "lon"_a);

  BindRasterSource<fv::GeoTiffRasterSource>(
      formats, "GeoTiffRasterSource",
      "One GeoTIFF frame: open once, then read RGBA blocks and transform "
      "between pixel and WGS-84 coordinates.");

  BindRasterSource<fv::CadrgRasterSource>(
      formats, "CadrgRasterSource",
      "One CADRG/RPF frame (1536x1536 chart): open, then read RGBA blocks. "
      "Decodes the whole frame once on the first read_block. Non-polar "
      "(equal-arc) frames support pixel<->geo; polar frames raise FvError.");

  BindEnumerator<fv::CadrgFrameEnumerator>(
      formats, "CadrgFrameEnumerator",
      "Recursively lists RPF frame files under a directory; bounds from the "
      "filename/scale (no pixels decoded).");

  BindRasterSource<fv::TirosRasterSource>(
      formats, "TirosRasterSource",
      "One TIROS JPEG tile (1350x1350): open, then read RGBA blocks. Equal-arc "
      "pixel<->geo from the filename-derived bounds.");

  BindEnumerator<fv::VpfFrameEnumerator>(
      formats, "VpfFrameEnumerator",
      "Enumerates a VPF/DNC database's library tiles into FrameInfo rows "
      "(series_key = library; path = db|library|tile locator).");

  BindEnumerator<fv::TirosFrameEnumerator>(
      formats, "TirosFrameEnumerator",
      "Recursively lists TIROS *.wld tiles; bounds from the filename grid.");

  // ---- pyfvw.canvas ------------------------------------------------------
  py::module_ canvas = m.def_submodule(
      "canvas", "L2.5 drawing: CpuCanvas over an RGBA8 PixelBuffer");

  // colors are (r, g, b[, a]) tuples on the Python side
  auto to_color = [](py::sequence s) {
    fv::FvColor c;
    c.r = (unsigned char)py::cast<int>(s[0]);
    c.g = (unsigned char)py::cast<int>(s[1]);
    c.b = (unsigned char)py::cast<int>(s[2]);
    c.a = s.size() > 3 ? (unsigned char)py::cast<int>(s[3]) : 255;
    return c;
  };
  auto to_points = [](const std::vector<std::pair<int, int>>& v) {
    std::vector<fv::PixelPoint> pts;
    pts.reserve(v.size());
    for (auto& p : v) pts.push_back({p.first, p.second});
    return pts;
  };

  // ICanvas base carries every draw method, so Python overlays receive any
  // canvas implementation uniformly.
  py::class_<fv::ICanvas, std::shared_ptr<fv::ICanvas>> icanvas(
      canvas, "ICanvas", "Drawing surface (see CpuCanvas).");

  py::class_<fv::CpuCanvas, fv::ICanvas, std::shared_ptr<fv::CpuCanvas>>(
      canvas, "CpuCanvas",
      "Deterministic CPU rasterizer. buffer is the underlying PixelBuffer "
      "(np.asarray it for a zero-copy (h, w, 4) view).")
      .def(py::init<int, int>(), "width"_a, "height"_a)
      .def_property_readonly(
          "buffer", [](fv::CpuCanvas& c) -> fv::PixelBuffer& { return c.Buffer(); },
          py::return_value_policy::reference_internal)
      .def("set_default_font",
           [](fv::CpuCanvas& c, const std::string& path) {
             ThrowIfError(c.SetDefaultFont(path));
           },
           "font_path"_a);

  icanvas
      .def("clear",
           [to_color](fv::ICanvas& c, py::sequence color) {
             c.Clear(to_color(color));
           },
           "color"_a)
      .def(
          "draw_lines",
          [to_color, to_points](fv::ICanvas& c,
                                const std::vector<std::pair<int, int>>& pts,
                                py::sequence color, int width,
                                const std::vector<int>& dash) {
            fv::Pen pen{to_color(color), width, dash};
            ThrowIfError(c.DrawLines(to_points(pts), pen));
          },
          "points"_a, "color"_a, "width"_a = 1,
          "dash"_a = std::vector<int>{})
      .def(
          "fill_polygon",
          [to_color, to_points](
              fv::ICanvas& c,
              const std::vector<std::vector<std::pair<int, int>>>& rings,
              py::object fill, py::object outline, int outline_width) {
            std::vector<std::vector<fv::PixelPoint>> rs;
            for (auto& r : rings) rs.push_back(to_points(r));
            fv::Brush brush;
            fv::Pen pen;
            fv::Brush* pb = nullptr;
            fv::Pen* pp = nullptr;
            if (!fill.is_none()) {
              brush.color = to_color(fill);
              pb = &brush;
            }
            if (!outline.is_none()) {
              pen.color = to_color(outline);
              pen.width = outline_width;
              pp = &pen;
            }
            ThrowIfError(c.DrawPolyPolygon(rs, pb, pp));
          },
          "rings"_a, "fill"_a = py::none(), "outline"_a = py::none(),
          "outline_width"_a = 1,
          "Even-odd fill (GDI ALTERNATE) of one or more rings.")
      .def(
          "draw_text",
          [to_color](fv::ICanvas& c, const std::string& text, int x, int y,
                     py::sequence color, double size,
                     const std::string& font_path) {
            fv::TextStyle ts;
            ts.color = to_color(color);
            ts.size = size;
            ts.font_path = font_path;
            ThrowIfError(c.DrawTextString(text, x, y, ts));
          },
          "text"_a, "x"_a, "y"_a, "color"_a, "size"_a = 12.0,
          "font_path"_a = "",
          "Baseline-left at (x, y). font_path empty uses set_default_font.")
      .def(
          "draw_pixmap",
          [](fv::ICanvas& c, const fv::PixelBuffer& src, int x, int y) {
            ThrowIfError(c.DrawPixmap(src, x, y));
          },
          "src"_a, "x"_a, "y"_a, "Alpha-blend a PixelBuffer at (x, y).");

  // ---- pyfvw.overlay -----------------------------------------------------
  py::module_ ovl = m.def_submodule(
      "overlay", "L4 overlay SPI: subclass Overlay in Python (override "
                 "on_draw / on_mouse_* / on_key_down), stack via "
                 "OverlayManager.");

  py::class_<fv::MouseEvent>(ovl, "MouseEvent")
      .def(py::init([](int x, int y, int button, bool shift, bool ctrl) {
             return fv::MouseEvent{x, y, button, shift, ctrl};
           }),
           "x"_a, "y"_a, "button"_a = 0, "shift"_a = false, "ctrl"_a = false)
      .def_readwrite("x", &fv::MouseEvent::x)
      .def_readwrite("y", &fv::MouseEvent::y)
      .def_readwrite("button", &fv::MouseEvent::button)
      .def_readwrite("shift", &fv::MouseEvent::shift)
      .def_readwrite("ctrl", &fv::MouseEvent::ctrl);

  py::class_<fv::KeyEvent>(
      ovl, "KeyEvent",
      "One key press. `key` is a pyfvw.overlay.key.* virtual-key code (the "
      "physical intent, stable across keyboard layouts) and is what a "
      "shortcut compares against; `text` is the Unicode code point the "
      "layout produced (0 for a non-printing key) and is what typed input "
      "should insert. A shell with no name for a key sends key=0.")
      .def(py::init([](int key, uint32_t text, bool shift, bool ctrl, bool alt,
                       bool meta) {
             return fv::KeyEvent{key, text, shift, ctrl, alt, meta};
           }),
           "key"_a = 0, "text"_a = 0, "shift"_a = false, "ctrl"_a = false,
           "alt"_a = false, "meta"_a = false)
      .def_readwrite("key", &fv::KeyEvent::key)
      .def_readwrite("text", &fv::KeyEvent::text)
      .def_readwrite("shift", &fv::KeyEvent::shift)
      .def_readwrite("ctrl", &fv::KeyEvent::ctrl)
      .def_readwrite("alt", &fv::KeyEvent::alt)
      .def_readwrite("meta", &fv::KeyEvent::meta)
      .def("__repr__", [](const fv::KeyEvent& e) {
        std::string mods;
        if (e.ctrl) mods += "ctrl+";
        if (e.alt) mods += "alt+";
        if (e.shift) mods += "shift+";
        if (e.meta) mods += "meta+";
        return "<KeyEvent " + mods + "key=0x" +
               [](int v) {
                 char b[16];
                 std::snprintf(b, sizeof(b), "%02X", v);
                 return std::string(b);
               }(e.key) +
               " text=" + std::to_string(e.text) + ">";
      });

  // Win32 virtual-key codes, verbatim and never renumbered (overlay.h says
  // why). Letters and digits are their ASCII uppercase values, so
  // `e.key == ord('A')` needs no constant at all.
  py::module_ keys = ovl.def_submodule(
      "key", "Virtual-key codes for KeyEvent.key (Win32 VK values). Letters "
             "and digits are ord() of their uppercase character.");
  keys.attr("NONE") = int(fv::Key::kNone);
  keys.attr("BACKSPACE") = int(fv::Key::kBackspace);
  keys.attr("TAB") = int(fv::Key::kTab);
  keys.attr("RETURN") = int(fv::Key::kReturn);
  keys.attr("ESCAPE") = int(fv::Key::kEscape);
  keys.attr("SPACE") = int(fv::Key::kSpace);
  keys.attr("PAGE_UP") = int(fv::Key::kPageUp);
  keys.attr("PAGE_DOWN") = int(fv::Key::kPageDown);
  keys.attr("END") = int(fv::Key::kEnd);
  keys.attr("HOME") = int(fv::Key::kHome);
  keys.attr("LEFT") = int(fv::Key::kLeft);
  keys.attr("UP") = int(fv::Key::kUp);
  keys.attr("RIGHT") = int(fv::Key::kRight);
  keys.attr("DOWN") = int(fv::Key::kDown);
  keys.attr("INSERT") = int(fv::Key::kInsert);
  keys.attr("DELETE") = int(fv::Key::kDelete);
  for (int n = 1; n <= 12; ++n)
    keys.attr(("F" + std::to_string(n)).c_str()) = int(fv::Key::kF1) + n - 1;


  py::class_<fv::Overlay, PyOverlay, std::shared_ptr<fv::Overlay>>(
      ovl, "Overlay",
      "Subclass and override on_draw(proj, canvas), on_mouse_down(e) -> "
      "bool, on_key_down(KeyEvent) -> bool, ... Handlers returning True stop "
      "top-down routing; exceptions are contained (draw -> FvError from "
      "draw_all, events -> unhandled).\n\n"
      "CAPABILITIES (A6) are opted into by DEFINING METHODS, and the app layer "
      "then finds them:\n"
      "  file_new() / file_open(spec) / file_save_as(spec, format_index) "
      "[/ revert(spec)] -> a FILE overlay, with .dirty, .file_spec and the "
      "OverlaySession flows. Report a failure by raising.\n"
      "  hit_test_point(proj, x, y, tolerance_px) -> [app.HitItem] -> "
      "pickable (hover, click, context menu). `overlay` is stamped for you.\n"
      "  snap_to_point(proj, x, y, tolerance_px) -> [app.SnapToItem]\n"
      "  menu_items(proj, x, y) -> [app.MenuNode] -> a right-click section\n"
      "  wants_direct_routing() -> bool -> the mouse first, mid-gesture\n"
      "  enter_edit_focus() / release_edit_focus() / can_undo() / undo() / "
      "can_redo() / redo() -> the per-instance half of the editor contract.")
      .def(py::init<std::string>(), "name"_a)
      .def_property("name", &fv::Overlay::Name,
                    [](fv::Overlay& o, std::string n) { o.SetName(std::move(n)); })
      .def_property("visible", &fv::Overlay::IsVisible,
                    &fv::Overlay::SetVisible)
      .def_property("type_id", &fv::Overlay::type_id, &fv::Overlay::set_type_id,
                    "The registered type this instance came from. Stamped by "
                    "the session layer at creation; empty for an overlay made "
                    "outside the app layer, which is legal.")
      // The Persistence state, readable on ANY overlay (empty/False when it
      // has none) and settable only on one that does -- a silent no-op here
      // would hide the commonest mistake, which is forgetting to define
      // file_open and wondering why Save does nothing.
      .def_property_readonly(
          "is_file_overlay",
          [](fv::Overlay& o) { return o.AsPersistence() != nullptr; })
      .def_property_readonly("file_spec",
                             [](fv::Overlay& o) {
                               auto* p = o.AsPersistence();
                               return p ? p->file_spec() : std::string();
                             })
      .def_property(
          "dirty",
          [](fv::Overlay& o) {
            auto* p = o.AsPersistence();
            return p != nullptr && p->is_dirty();
          },
          [](fv::Overlay& o, bool v) {
            auto* p = o.AsPersistence();
            if (p == nullptr)
              throw FvErrorCpp{fv::Status::Error(
                  fv::kUnsupported, "overlay '" + o.Name() +
                                        "' is not a file overlay (it defines "
                                        "no file_open/file_new/file_save_as)")};
            p->set_dirty(v);
          },
          "Has unsaved edits -- what makes Close prompt. Setting it fires the "
          "stack's dirty broadcast, and only on an actual change.")
      .def_property_readonly("has_been_saved",
                             [](fv::Overlay& o) {
                               auto* p = o.AsPersistence();
                               return p != nullptr && p->has_been_saved();
                             })
      .def_property_readonly("read_only",
                             [](fv::Overlay& o) {
                               auto* p = o.AsPersistence();
                               return p != nullptr && p->is_read_only();
                             })
      .def_property_readonly("save_format_index", [](fv::Overlay& o) {
        auto* p = o.AsPersistence();
        return p ? p->save_format_index() : 0;
      });

  // The first C++ file overlay (A6): points from a SQLite document, drawn as
  // geometric shapes, answering picks with the row's own id.
  py::class_<fv::MapPoint>(
      ovl, "MapPoint",
      "One row of a .fvpoints document. `category` and `elevation_ft` are "
      "ordinary columns that ride into the pick's hint, which is what makes a "
      "hit attributable to one point rather than to 'something round'.")
      .def(py::init([](std::string name, double lat, double lon,
                       const std::string& shape, double size_px, py::sequence c,
                       std::string category, double elevation_ft,
                       std::string remarks, int64_t symbol_id, int64_t id) {
             fv::MapPoint p;
             p.id = id;
             p.symbol_id = symbol_id;
             p.name = std::move(name);
             p.position = {lat, lon};
             p.shape = fv::PointShapeFromString(shape);
             p.size_px = size_px;
             if (py::len(c) >= 3) {
               p.color.r = (unsigned char)py::cast<int>(c[0]);
               p.color.g = (unsigned char)py::cast<int>(c[1]);
               p.color.b = (unsigned char)py::cast<int>(c[2]);
               p.color.a =
                   py::len(c) > 3 ? (unsigned char)py::cast<int>(c[3]) : 255;
             }
             p.category = std::move(category);
             p.elevation_ft = elevation_ft;
             p.remarks = std::move(remarks);
             return p;
           }),
           "name"_a, "lat"_a, "lon"_a, "shape"_a = "circle", "size_px"_a = 9.0,
           "color"_a = py::make_tuple(200, 40, 40), "category"_a = "",
           "elevation_ft"_a = 0.0, "remarks"_a = "", "symbol_id"_a = 0,
           "id"_a = 0)
      .def_readwrite("id", &fv::MapPoint::id)
      .def_readwrite("name", &fv::MapPoint::name)
      .def_readwrite("position", &fv::MapPoint::position)
      .def_property(
          "shape",
          [](const fv::MapPoint& p) { return std::string(fv::ToString(p.shape)); },
          [](fv::MapPoint& p, const std::string& s) {
            p.shape = fv::PointShapeFromString(s);
          },
          "circle, square, triangle, diamond, cross or star.")
      .def_readwrite("size_px", &fv::MapPoint::size_px)
      .def_property(
          "color",
          [](const fv::MapPoint& p) {
            return py::make_tuple(p.color.r, p.color.g, p.color.b, p.color.a);
          },
          [](fv::MapPoint& p, py::sequence c) {
            p.color.r = (unsigned char)py::cast<int>(c[0]);
            p.color.g = (unsigned char)py::cast<int>(c[1]);
            p.color.b = (unsigned char)py::cast<int>(c[2]);
            p.color.a = py::len(c) > 3 ? (unsigned char)py::cast<int>(c[3]) : 255;
          })
      .def_readwrite("category", &fv::MapPoint::category)
      .def_readwrite("elevation_ft", &fv::MapPoint::elevation_ft)
      .def_readwrite("remarks", &fv::MapPoint::remarks)
      .def_readwrite("symbol_id", &fv::MapPoint::symbol_id,
                     "The document's own symbol row this point wears, 0 for "
                     "none. Many points may name one row -- that is what the "
                     "second table is for. An id with no row draws as the "
                     "`shape`, which is also the badge under the icon.")
      .def("__repr__", [](const fv::MapPoint& p) {
        return "<MapPoint " + std::to_string(p.id) + " '" + p.name + "'>";
      });

  // Schema 2: the artwork, carried inside the document so a .fvpoints file
  // opens on a machine that has never seen the icon set it was authored with.
  py::class_<fv::PointSymbol>(
      ovl, "PointSymbol",
      "One row of a .fvpoints document's symbol table: a PNG and how to place "
      "it. `image` is the file's own bytes, decoded lazily on the first draw "
      "that needs them.")
      .def(py::init([](std::string name, py::bytes image, double pixel_ratio,
                       py::object pivot, int64_t id) {
             fv::PointSymbol s;
             s.id = id;
             s.name = std::move(name);
             const std::string bytes = image;
             s.image.assign(bytes.begin(), bytes.end());
             s.pixel_ratio = pixel_ratio;
             if (!pivot.is_none()) {
               py::sequence p = py::cast<py::sequence>(pivot);
               s.has_pivot = true;
               s.pivot_x = py::cast<double>(p[0]);
               s.pivot_y = py::cast<double>(p[1]);
             }
             return s;
           }),
           "name"_a, "image"_a, "pixel_ratio"_a = 1.0,
           "pivot"_a = py::none(), "id"_a = 0,
           "`pivot` is (x, y) in TILE pixels with y down; None (the default, "
           "and what a marker wants) is the tile's centre.")
      .def_readwrite("id", &fv::PointSymbol::id)
      .def_readwrite("name", &fv::PointSymbol::name)
      .def_property(
          "image",
          [](const fv::PointSymbol& s) {
            return py::bytes(reinterpret_cast<const char*>(s.image.data()),
                             s.image.size());
          },
          [](fv::PointSymbol& s, py::bytes b) {
            const std::string bytes = b;
            s.image.assign(bytes.begin(), bytes.end());
          },
          "The PNG, as bytes.")
      .def_readwrite("pixel_ratio", &fv::PointSymbol::pixel_ratio,
                     "Tile pixels per NOMINAL pixel: 2 for artwork drawn at "
                     "2x, which then comes out the same size with more detail "
                     "in it.")
      .def_property(
          "pivot",
          [](const fv::PointSymbol& s) -> py::object {
            if (!s.has_pivot) return py::none();
            return py::make_tuple(s.pivot_x, s.pivot_y);
          },
          [](fv::PointSymbol& s, py::object v) {
            if (v.is_none()) {
              s.has_pivot = false;
              return;
            }
            py::sequence p = py::cast<py::sequence>(v);
            s.has_pivot = true;
            s.pivot_x = py::cast<double>(p[0]);
            s.pivot_y = py::cast<double>(p[1]);
          })
      .def("__repr__", [](const fv::PointSymbol& s) {
        return "<PointSymbol " + std::to_string(s.id) + " '" + s.name + "' " +
               std::to_string(s.image.size()) + " bytes>";
      });

  py::class_<fv::PointOverlay, fv::Overlay, std::shared_ptr<fv::PointOverlay>>(
      ovl, "PointOverlay",
      "A point set read from a SQLite document (.fvpoints). The first C++ "
      "FILE overlay: it is persistent, pickable and has a context menu, and "
      "its type is registered by app.register_builtin_types().")
      .def(py::init<std::string>(), "name"_a = "Points")
      .def_property_readonly("points", &fv::PointOverlay::points)
      .def("set_points", &fv::PointOverlay::SetPoints, "points"_a)
      .def("add_point", &fv::PointOverlay::AddPoint, "point"_a,
           "Returns the id it was given; an id of 0 gets the next free one.")
      .def("remove_point", &fv::PointOverlay::RemovePoint, "point_id"_a)
      .def("find", &fv::PointOverlay::Find, "point_id"_a,
           py::return_value_policy::reference_internal)
      // --- the embedded palette (schema 2) -------------------------------
      .def_property_readonly("symbols", &fv::PointOverlay::symbols)
      .def("set_symbols", &fv::PointOverlay::SetSymbols, "symbols"_a)
      .def("add_symbol", &fv::PointOverlay::AddSymbol, "symbol"_a,
           "Returns the id it was given -- or the id of the row that already "
           "has that NAME, which is how many points come to share one symbol "
           "without the caller tracking ids.")
      .def("add_symbol_from_png",
           [](fv::PointOverlay& o, const std::string& path,
              const std::string& name) {
             int64_t id = 0;
             ThrowIfError(o.AddSymbolFromPngFile(path, name, &id));
             return id;
           },
           "path"_a, "name"_a = "",
           "Embeds a PNG from disk. An empty name takes the file's stem, and "
           "an `@2x` stem is read as 2x artwork of the un-suffixed name. The "
           "bytes are copied in: the file is never referenced again.")
      .def("remove_symbol", &fv::PointOverlay::RemoveSymbol, "symbol_id"_a,
           "The points that wore it fall back to their shape; they are not "
           "rewritten, so putting the row back makes them wear it again.")
      .def("find_symbol", &fv::PointOverlay::FindSymbol, "symbol_id"_a,
           py::return_value_policy::reference_internal)
      .def("find_symbol_by_name", &fv::PointOverlay::FindSymbolByName,
           "name"_a, py::return_value_policy::reference_internal)
      .def_property("selected", &fv::PointOverlay::selected,
                    &fv::PointOverlay::SetSelected,
                    "The highlighted point's id, 0 for none. Selecting is not "
                    "a document change and does not dirty the overlay.")
      .def_property("show_labels", &fv::PointOverlay::show_labels,
                    &fv::PointOverlay::SetShowLabels)
      .def("file_new", [](fv::PointOverlay& o) { ThrowIfError(o.FileNew()); })
      .def("file_open",
           [](fv::PointOverlay& o, const std::string& spec) {
             ThrowIfError(o.FileOpen(spec));
           },
           "spec"_a)
      .def("file_save_as",
           [](fv::PointOverlay& o, const std::string& spec, int format_index) {
             ThrowIfError(o.FileSaveAs(spec, format_index));
           },
           "spec"_a, "format_index"_a = 0)
      .def_static("write_sample_file",
                  [](const std::string& spec, const std::string& symbol_dir) {
                    ThrowIfError(
                        fv::PointOverlay::WriteSampleFile(spec, symbol_dir));
                  },
                  "spec"_a, "symbol_dir"_a = "",
                  "Writes an arbitrary starter document (Kiawah Island and "
                  "Charleston harbour), including two pairs of points that "
                  "overlap within the pick tolerance.\n\n"
                  "`symbol_dir` is a directory of loose <name>.png icons -- "
                  "testdata/GeoSymbol/makiPng is the port's own -- whose "
                  "artwork is EMBEDDED in the document. Empty, or missing an "
                  "icon, draws those points as plain shapes.")
      .def_static("sample_symbols", &fv::PointOverlay::SampleSymbols,
                  "symbol_dir"_a,
                  "The palette write_sample_file() would embed from that "
                  "directory: one row per DISTINCT icon its points name, so "
                  "the three forts cost one castle between them.")
      .def_property_readonly_static(
          "TYPE_ID", [](py::object) { return fv::PointOverlay::kTypeId; })
      .def_property_readonly_static(
          "EXTENSION", [](py::object) { return fv::PointOverlay::kExtension; });

  py::class_<fv::GridOverlay, fv::Overlay, std::shared_ptr<fv::GridOverlay>>(
      ovl, "GridOverlay", "Built-in lat/lon graticule.")
      .def(py::init<>())
      .def("set_color",
           [](fv::GridOverlay& g, py::sequence c) {
             fv::FvColor col;
             col.r = (unsigned char)py::cast<int>(c[0]);
             col.g = (unsigned char)py::cast<int>(c[1]);
             col.b = (unsigned char)py::cast<int>(c[2]);
             col.a = c.size() > 3 ? (unsigned char)py::cast<int>(c[3]) : 255;
             g.SetColor(col);
           },
           "color"_a);

  py::class_<fv::OverlayManager>(ovl, "OverlayManager")
      .def(py::init<>())
      .def("add",
           [](fv::OverlayManager& m2, std::shared_ptr<fv::Overlay> o) {
             ThrowIfError(m2.Add(std::move(o)));
           },
           "overlay"_a,
           // keep the PYTHON half of a subclassed overlay alive as long as
           // the manager: without this, add(MyOverlay()) with no other
           // reference silently loses every override (the trampoline
           // lifetime risk the FvKit plan called out)
           py::keep_alive<1, 2>())
      .def("remove",
           [](fv::OverlayManager& m2, const std::shared_ptr<fv::Overlay>& o) {
             ThrowIfError(m2.Remove(o));
           },
           "overlay"_a)
      .def("move_to_top",
           [](fv::OverlayManager& m2, const std::shared_ptr<fv::Overlay>& o) {
             ThrowIfError(m2.MoveToTop(o));
           },
           "overlay"_a)
      // --- A2: type information, order, current, queries, declutter -------
      .def("set_type_registry", &fv::OverlayManager::SetTypeRegistry,
           "registry"_a.none(true), py::keep_alive<1, 2>(),
           "Optional. Without one, every overlay has display order 0 and "
           "add() is a plain append -- the pre-A2 stack, unchanged.")
      .def("move_to_bottom",
           [](fv::OverlayManager& m2, const std::shared_ptr<fv::Overlay>& o) {
             ThrowIfError(m2.MoveToBottom(o));
           },
           "overlay"_a)
      .def("move_above",
           [](fv::OverlayManager& m2, const std::shared_ptr<fv::Overlay>& move,
              const std::shared_ptr<fv::Overlay>& anchor) {
             ThrowIfError(m2.MoveAbove(move, anchor));
           },
           "overlay"_a, "anchor"_a)
      .def("move_below",
           [](fv::OverlayManager& m2, const std::shared_ptr<fv::Overlay>& move,
              const std::shared_ptr<fv::Overlay>& anchor) {
             ThrowIfError(m2.MoveBelow(move, anchor));
           },
           "overlay"_a, "anchor"_a)
      .def("reorder",
           [](fv::OverlayManager& m2,
              const std::vector<std::shared_ptr<fv::Overlay>>& order) {
             ThrowIfError(m2.Reorder(order));
           },
           "full_order"_a,
           "A TOTAL permutation, bottom-first. Anything that is not one is "
           "rejected whole -- a stale reorder dialog must not half-apply.")
      .def("make_current",
           [](fv::OverlayManager& m2, const std::shared_ptr<fv::Overlay>& o) {
             ThrowIfError(m2.MakeCurrent(o));
           },
           "overlay"_a.none(true), "None clears it.")
      .def_property_readonly("current", &fv::OverlayManager::current_ptr)
      .def("contains", &fv::OverlayManager::Contains, "overlay"_a)
      .def("first_of_type", &fv::OverlayManager::FirstOfType, "type_id"_a,
           "The TOPMOST overlay of the type, or None.")
      .def("of_type", &fv::OverlayManager::OfType, "type_id"_a,
           "Every overlay of the type, top-down.")
      .def("find_by_file_spec", &fv::OverlayManager::FindByFileSpec,
           "type_id"_a, "file_spec"_a,
           "The open-dedup lookup. An empty type_id matches any type; the "
           "spec is compared exactly, because canonicalising a path is the "
           "shell's job.")
      .def_property("declutter", &fv::OverlayManager::declutter,
                    &fv::OverlayManager::SetDeclutter,
                    "On: only the current overlay draws and routes. Nothing is "
                    "hidden -- turning it off restores exactly what was there.")
      .def("capture_mouse",
           [](fv::OverlayManager& m2, fv::Overlay* o) {
             ThrowIfError(m2.CaptureMouse(o));
           },
           "overlay"_a.none(true))
      .def("release_mouse", &fv::OverlayManager::ReleaseMouse)
      .def_property_readonly("mouse_capture", &fv::OverlayManager::mouse_capture,
                             py::return_value_policy::reference)
      .def("add_observer", &fv::OverlayManager::AddObserver, "observer"_a,
           py::keep_alive<1, 2>())
      .def("remove_observer", &fv::OverlayManager::RemoveObserver, "observer"_a)
      .def("draw_order", &fv::OverlayManager::DrawOrder,
           py::return_value_policy::reference,
           "The overlays ON SCREEN in DRAW order (bottom-up, top-most band "
           "last). Reversed, this is exactly who a pick asks.")
      .def_property_readonly("overlays", &fv::OverlayManager::Overlays)
      .def("draw_all",
           [](fv::OverlayManager& m2, const fv::MapProjection& proj,
              fv::ICanvas& canvas) { ThrowIfError(m2.DrawAll(proj, canvas)); },
           "proj"_a, "canvas"_a)
      .def("route_mouse_down", &fv::OverlayManager::RouteMouseDown, "e"_a)
      .def("route_mouse_up", &fv::OverlayManager::RouteMouseUp, "e"_a)
      .def("route_mouse_move", &fv::OverlayManager::RouteMouseMove, "e"_a)
      .def("route_double_click", &fv::OverlayManager::RouteDoubleClick, "e"_a)
      .def("route_mouse_wheel", &fv::OverlayManager::RouteMouseWheel, "e"_a,
           "delta"_a)
      .def("route_key_down", &fv::OverlayManager::RouteKeyDown, "event"_a);

  // ---- pyfvw.catalog ---------------------------------------------------
  py::module_ catalog =
      m.def_submodule("catalog", "L2 coverage catalog (SQLite + R-tree)");

  // ENC and OSM register separately in C++ (fv_enc and fv_osm both link
  // fv_fvkit, so fvkit cannot name them without a dependency cycle — see
  // fv_enc_format.h). That is a build-layering detail, not something a Python
  // caller should have to know, so one call still registers everything the
  // port can read.
  catalog.def("register_builtin_formats",
              [] {
                fv::RegisterBuiltinFormats();
                fv::RegisterEncFormat();
                fv::RegisterOsmFormat();
              },
              "Register the built-in format adapters (dted, geotiff, cadrg, "
              "tiros, vpf, gpkg, dted-shaded, enc, osm) with the scan "
              "registry. Idempotent; call before Catalog.scan.");

  catalog.def("registered_format_keys", &fv::RegisteredFormatKeys,
              "Format keys currently registered, sorted.");

  py::class_<fv::SeriesRow>(catalog, "SeriesRow")
      .def_readonly("id", &fv::SeriesRow::id)
      .def_readonly("format", &fv::SeriesRow::format)
      .def_readonly("series_key", &fv::SeriesRow::series_key)
      .def_readonly("scale", &fv::SeriesRow::scale)
      .def_readonly("scale_units", &fv::SeriesRow::scale_units)
      .def_readonly("scale_denom", &fv::SeriesRow::scale_denom)
      .def("__repr__", [](const fv::SeriesRow& r) {
        return "SeriesRow(" + r.format + ":" + r.series_key + ", 1:" +
               std::to_string((long long)r.scale_denom) + ")";
      });

  py::class_<fv::CoverageRow>(catalog, "CoverageRow")
      .def_readonly("id", &fv::CoverageRow::id)
      .def_readonly("series_id", &fv::CoverageRow::series_id)
      .def_readonly("series_key", &fv::CoverageRow::series_key)
      .def_readonly("path", &fv::CoverageRow::path)
      .def_readonly("bounds", &fv::CoverageRow::bounds)
      .def_readonly("size_bytes", &fv::CoverageRow::size_bytes)
      .def("__repr__", [](const fv::CoverageRow& r) {
        return "CoverageRow(" + r.series_key + ", " + r.path + ")";
      });

  py::class_<fv::Catalog, std::shared_ptr<fv::Catalog>>(
      catalog, "Catalog",
      "Coverage catalog: add_data_source + scan populate it via the format "
      "registry; select_by_geo_rect answers viewport queries through an "
      "antimeridian-aware R-tree.")
      .def(py::init([](const std::string& db_path) {
             auto c = std::make_shared<fv::Catalog>();
             ThrowIfError(c->Open(db_path));
             return c;
           }),
           "db_path"_a = ":memory:")
      .def(
          "add_data_source",
          [](fv::Catalog& c, const std::string& path,
             const std::string& format_key, int priority) {
            int64_t id = 0;
            ThrowIfError(c.AddDataSource(path, format_key, priority, &id));
            return id;
          },
          "path"_a, "format_key"_a, "priority"_a = 0)
      .def(
          "scan",
          [](fv::Catalog& c, int64_t data_source_id) {
            int added = 0;
            fv::Status s;
            {
              py::gil_scoped_release release;
              s = c.Scan(data_source_id, &added);
            }
            ThrowIfError(s);
            return added;
          },
          "data_source_id"_a, "Enumerate the source; returns frames added.")
      .def("series",
           [](const fv::Catalog& c) {
             std::vector<fv::SeriesRow> v;
             ThrowIfError(c.Series(&v));
             return v;
           })
      .def(
          "select_by_geo_rect",
          [](const fv::Catalog& c, const fv::GeoRect& rect, int64_t series_id) {
            std::vector<fv::CoverageRow> v;
            ThrowIfError(c.SelectByGeoRect(rect, &v, series_id));
            return v;
          },
          "rect"_a, "series_id"_a = 0)
      .def(
          "best_series_for_scale",
          [](const fv::Catalog& c, double target_scale_denom) {
            fv::SeriesRow r;
            ThrowIfError(c.BestSeriesForScale(target_scale_denom, &r));
            return r;
          },
          "target_scale_denom"_a)
      .def("remove_data_source",
           [](fv::Catalog& c, int64_t id) { ThrowIfError(c.RemoveDataSource(id)); },
           "data_source_id"_a);

  // ---- pyfvw.engine ------------------------------------------------------
  py::module_ eng = m.def_submodule(
      "engine", "L3 map engine: catalog-driven viewport compositing");

  // Reference display pitch (mm/pixel) that defines "100%" for imagery.
  eng.attr("NATIVE_DISPLAY_MM_PER_PIXEL") = fv::kNativeDisplayMmPerPixel;

  py::class_<fv::MapProjection>(eng, "MapProjection")
      // Constructable + configurable from Python so a vector viewer can drive
      // the projection directly (the raster path gets one from the engine).
      .def(py::init<>())
      .def("set_surface_size",
           [](fv::MapProjection& p, int w, int h) {
             ThrowIfError(p.SetSurfaceSize(w, h));
           },
           "width"_a, "height"_a)
      .def("set_center",
           [](fv::MapProjection& p, const fv::GeoPoint& c) {
             ThrowIfError(p.SetCenter(c));
           },
           "center"_a)
      .def("set_scale",
           [](fv::MapProjection& p, double denom) {
             ThrowIfError(p.SetScale(denom));
           },
           "scale_denominator"_a,
           "1:N equal-arc (dpp via MapScaleUtil). Mutually exclusive with the "
           "other set_* scale calls; last one wins.")
      .def("set_resolution",
           [](fv::MapProjection& p, double dpp_lat, double dpp_lon) {
             ThrowIfError(p.SetResolution(dpp_lat, dpp_lon));
           },
           "dpp_lat"_a, "dpp_lon"_a, "Explicit degrees-per-pixel per axis.")
      .def("set_physical_scale",
           [](fv::MapProjection& p, double denom, double mm_per_pixel) {
             ThrowIfError(p.SetPhysicalScale(denom, mm_per_pixel));
           },
           "scale_denominator"_a, "mm_per_pixel"_a,
           "1:N at a known display pitch, with correct latitude-dependent "
           "aspect (WGS84 metres-per-degree, not MapScaleUtil).")
      .def("set_rotation",
           [](fv::MapProjection& p, double deg) {
             ThrowIfError(p.SetRotation(deg));
           },
           "degrees"_a,
           "Turn the chart CLOCKWISE on screen about the surface centre (PR1). "
           "Any finite angle, wrapped into [0, 360); 0 is the exact identity. "
           "Orthogonal to every set_* scale call.\n\n"
           "BOTH paths honour it. Vector (PR2): geometry, labels along a path "
           "and north-up point symbols all turn, point labels stay upright, "
           "and picking follows the ink. Raster (PR3): MapEngine resamples "
           "each frame through the turned projection and masks what falls "
           "outside it, so a turned frame keeps its own edges. The price is "
           "in the query — a turned viewport's bounds grow to (w cos + h sin) "
           "by (w sin + h cos), so a turned frame reads more data.")
      .def_property_readonly("rotation", &fv::MapProjection::Rotation,
                             "Clockwise chart rotation in degrees, [0, 360).")
      .def_property_readonly("deg_per_pixel_lat", &fv::MapProjection::DegPerPixelLat)
      .def_property_readonly("deg_per_pixel_lon", &fv::MapProjection::DegPerPixelLon)
      .def_property_readonly("scale", &fv::MapProjection::Scale)
      .def_property_readonly("mm_per_pixel", &fv::MapProjection::MmPerPixel)
      .def_property_readonly("bounds", &fv::MapProjection::VmapBounds)
      // center + surface_size complete the set a caller needs to COPY a
      // projection (set_surface_size / set_center / set_resolution reproduce
      // the same linear transform exactly). An overlay wants that: OnDraw is
      // handed a projection by reference and OnMouseDown is handed none, so
      // un-projecting a click means snapshotting the drawing projection —
      // and snapshotting it beats holding the borrowed reference, which
      // outlives nothing in particular.
      .def_property_readonly("center", &fv::MapProjection::Center)
      .def_property_readonly(
          "surface_size",
          [](const fv::MapProjection& p) {
            const fv::PixelSize s = p.SurfaceSize();
            return py::make_tuple(s.width, s.height);
          })
      .def("geo_to_surface",
           [](const fv::MapProjection& p, const fv::GeoPoint& g) {
             double sx = 0, sy = 0;
             ThrowIfError(p.GeoToSurface(g, &sx, &sy));
             return py::make_tuple(sx, sy);
           },
           "p"_a)
      .def("surface_to_geo",
           [](const fv::MapProjection& p, double sx, double sy) {
             fv::GeoPoint g;
             ThrowIfError(p.SurfaceToGeo(sx, sy, &g));
             return g;
           },
           "sx"_a, "sy"_a);

  py::class_<fv::MapEngine, std::shared_ptr<fv::MapEngine>>(
      eng, "MapEngine",
      "Configure surface/center/scale, then render(canvas) composites the "
      "catalog's coverage for the viewport (the demo loop, in C++).")
      .def(py::init<std::shared_ptr<fv::Catalog>, size_t>(), "catalog"_a,
           "source_cache_capacity"_a = 32)
      .def("set_surface",
           [](fv::MapEngine& e, int w, int h) {
             ThrowIfError(e.SetSurfaceDimensions(w, h));
           },
           "width"_a, "height"_a)
      .def("set_center",
           [](fv::MapEngine& e, const fv::GeoPoint& c) {
             ThrowIfError(e.SetCenter(c));
           },
           "center"_a)
      .def("set_scale",
           [](fv::MapEngine& e, double d) { ThrowIfError(e.SetScale(d)); },
           "scale_denominator"_a)
      .def("set_physical_scale",
           [](fv::MapEngine& e, double series_scale, int series_scale_units,
              double mm_per_pixel) {
             ThrowIfError(
                 e.SetPhysicalScale(series_scale, series_scale_units, mm_per_pixel));
           },
           "series_scale"_a, "series_scale_units"_a, "mm_per_pixel"_a,
           "Display a series at its native physical scale on an mm_per_pixel "
           "screen: a 1:N map draws at 1:N, imagery draws at 100% at the "
           "reference pitch. Pass a SeriesRow's .scale and .scale_units; "
           "mm_per_pixel is the zoom knob (larger = zoomed out).")
      .def("set_rotation",
           [](fv::MapEngine& e, double deg) { ThrowIfError(e.SetRotation(deg)); },
           "degrees"_a,
           "Turn the chart CLOCKWISE on screen (PR3). The base map is "
           "resampled through the turned projection, so the image turns with "
           "the vectors over it; 0 is the exact identity and is the blit the "
           "engine has always done.")
      .def_property_readonly("proj", &fv::MapEngine::CurrentProj,
                             py::return_value_policy::reference_internal)
      .def(
          "render",
          [](fv::MapEngine& e, fv::CpuCanvas& canvas, int64_t series_id) {
            int drawn = 0;
            fv::Status s;
            {
              py::gil_scoped_release release;
              s = e.RenderBaseMap(canvas, series_id, {}, &drawn);
            }
            ThrowIfError(s);
            return drawn;
          },
          "canvas"_a, "series_id"_a = 0,
          "Composite the viewport into canvas; returns frames drawn.")
      .def("set_elevation_source", &fv::MapEngine::SetElevationSource,
           "src"_a)
      .def(
          "get_elevation",
          [](fv::MapEngine& e, double lat, double lon) {
            float v = 0;
            fv::Status s;
            {
              py::gil_scoped_release release;
              s = e.GetElevation(fv::GeoPoint{lat, lon}, &v);
            }
            ThrowIfError(s);
            return v;
          },
          "lat"_a, "lon"_a);

  // ---- pyfvw.store -------------------------------------------------------
  py::module_ store = m.def_submodule(
      "store", "L5 store: GeoPackage tile pyramids (offline pre-render)");

  py::class_<fv::TilePackWriter>(store, "TilePackWriter",
      "Create(bounds) then write_level(engine, z) renders pyramid levels "
      "into a GeoPackage; packs are consumed via the 'gpkg' catalog format.")
      .def(py::init<>())
      .def("create",
           [](fv::TilePackWriter& w, const std::string& path,
              const std::string& table, const fv::GeoRect& bounds,
              int tile_size) {
             ThrowIfError(w.Create(path, table, bounds, tile_size));
           },
           "path"_a, "table"_a, "bounds"_a, "tile_size"_a = 256)
      .def("write_level",
           [](fv::TilePackWriter& w, fv::MapEngine& engine, int zoom,
              int64_t series_id) {
             int n = 0;
             fv::Status s;
             {
               py::gil_scoped_release release;
               s = w.WriteLevel(engine, zoom, series_id, &n);
             }
             ThrowIfError(s);
             return n;
           },
           "engine"_a, "zoom"_a, "series_id"_a = 0,
           "Render one pyramid level; returns tiles written.")
      .def("close", [](fv::TilePackWriter& w) { ThrowIfError(w.Close()); });

  py::class_<fv::CadrgFrameCache>(
      formats, "CadrgFrameCache",
      "LRU cache of opened CADRG frames keyed by path; bounds how many "
      "decoded frames stay resident while panning.")
      .def(py::init<size_t>(), "capacity"_a = 24)
      .def(
          "get",
          [](fv::CadrgFrameCache& c, const std::string& path) {
            fv::Status st;
            std::shared_ptr<fv::CadrgRasterSource> s;
            {
              py::gil_scoped_release release;
              s = c.Get(path, &st);
            }
            ThrowIfError(st);
            return s;
          },
          "path"_a, "Opened source for path (cached; LRU-evicted).")
      .def_property_readonly("size", &fv::CadrgFrameCache::Size)
      .def_property_readonly("capacity", &fv::CadrgFrameCache::Capacity);

  // --- vector charting: IVectorSource -> IStyleEngine -> VectorRenderer -----
  py::module_ vec = m.def_submodule(
      "vector",
      "Vector charting seam: a product source (VPF/DNC) styled by an engine "
      "(GeoSym) and drawn by the shared VectorRenderer onto an ICanvas.");

  // GeoSym product ids (fullsym.txt's pid column).
  vec.attr("GEOSYM_VMAP0") = static_cast<int>(fv::kGeoSymVmapLevel0);
  vec.attr("GEOSYM_VMAP1") = static_cast<int>(fv::kGeoSymVmapLevel1);
  vec.attr("GEOSYM_VMAP2") = static_cast<int>(fv::kGeoSymVmapLevel2);
  vec.attr("GEOSYM_DNC") = static_cast<int>(fv::kGeoSymDnc);

  // --- identify (plan §5.3) ------------------------------------------------

  py::class_<fv::FeatureRef>(
      vec, "FeatureRef",
      "Handle naming one feature in one source: what the render path and the "
      "pick index carry instead of a bag of attributes. Pass it to "
      "IVectorSource.describe().")
      .def(py::init<>())
      .def_readwrite("source", &fv::FeatureRef::source)
      .def_readwrite("layer", &fv::FeatureRef::layer)
      .def_readwrite("tile", &fv::FeatureRef::tile)
      .def_readwrite("feature", &fv::FeatureRef::feature)
      .def_property_readonly("valid", &fv::FeatureRef::valid)
      .def("__eq__",
           [](const fv::FeatureRef& a, const fv::FeatureRef& b) {
             return a == b;
           },
           py::is_operator())
      .def("__hash__",
           [](const fv::FeatureRef& r) {
             return py::hash(py::make_tuple(r.source, r.layer, r.tile,
                                            r.feature));
           })
      .def("__repr__", [](const fv::FeatureRef& r) {
        return "FeatureRef(layer=" + std::to_string(r.layer) + ", tile=" +
               std::to_string(r.tile) + ", feature=" +
               std::to_string(r.feature) + ")";
      });

  py::class_<fv::FeatureAttribute>(
      vec, "FeatureAttribute",
      "One attribute as a popup shows it: code (column name), name (the "
      "product's own description), raw (stored text) and display (raw decoded "
      "through the product dictionary, falling back to raw).")
      .def_readonly("code", &fv::FeatureAttribute::code)
      .def_readonly("name", &fv::FeatureAttribute::name)
      .def_readonly("raw", &fv::FeatureAttribute::raw)
      .def_readonly("display", &fv::FeatureAttribute::display)
      .def("__repr__", [](const fv::FeatureAttribute& a) {
        return "FeatureAttribute(" + a.code + "=" + a.display + ")";
      });

  py::class_<fv::FeatureDescription>(
      vec, "FeatureDescription", "The answer to 'what did I just click on?'")
      .def_readonly("ref", &fv::FeatureDescription::ref)
      .def_readonly("title", &fv::FeatureDescription::title)
      .def_readonly("class_name", &fv::FeatureDescription::class_name)
      .def_readonly("layer_name", &fv::FeatureDescription::layer_name)
      .def_readonly("attributes", &fv::FeatureDescription::attributes)
      .def_readonly("source_note", &fv::FeatureDescription::source_note)
      .def("__repr__", [](const fv::FeatureDescription& d) {
        return "FeatureDescription('" + d.title + "', " + d.layer_name + ", " +
               std::to_string(d.attributes.size()) + " attrs)";
      });

  py::class_<fv::PickHit>(vec, "PickHit",
                          "One feature under a hit-test point.")
      .def_readonly("ref", &fv::PickHit::ref)
      .def_readonly("priority", &fv::PickHit::priority)
      .def_readonly("distance", &fv::PickHit::distance);

  py::class_<fv::PickIndex>(
      vec, "PickIndex",
      "Hit-testing over what was actually DRAWN by the last render, in canvas "
      "pixels — so a tap agrees with what is on screen.")
      .def("hit_test", &fv::PickIndex::HitTest, "x"_a, "y"_a,
           "tolerance"_a = 3.0,
           "Features whose ink is within `tolerance` px of (x, y), TOPMOST "
           "FIRST. One entry per feature.")
      .def_property_readonly("shape_count", &fv::PickIndex::shape_count)
      .def("__len__", &fv::PickIndex::shape_count);

  py::class_<fv::IVectorSource, std::shared_ptr<fv::IVectorSource>>(
      vec, "IVectorSource", "A product-specific reader of drawable features.")
      .def("is_open", &fv::IVectorSource::IsOpen)
      .def("layers", &fv::IVectorSource::Layers)
      .def_property_readonly("bounds", &fv::IVectorSource::Bounds)
      .def("describe",
           [](fv::IVectorSource& s, const fv::FeatureRef& ref) {
             fv::FeatureDescription d;
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = s.Describe(ref, &d);
             }
             ThrowIfError(st);
             return d;
           },
           "ref"_a,
           "Full, decoded metadata for one feature (identify/tap). Raises "
           "FvError if the source cannot describe it.");

  py::class_<fv::VpfVectorSource, fv::IVectorSource,
             std::shared_ptr<fv::VpfVectorSource>>(
      vec, "VpfVectorSource",
      "DNC/VPF as a vector source. open() takes a DNC LIBRARY directory "
      "(e.g. .../dnc17/h1707300). Serves point, line and (V5c) area features.")
      .def(py::init<>())
      .def("open",
           [](fv::VpfVectorSource& s, const std::string& path) {
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = s.Open(path);
             }
             ThrowIfError(st);
           },
           "library_dir"_a);

  py::class_<fv::EncVectorSource, fv::IVectorSource,
             std::shared_ptr<fv::EncVectorSource>>(
      vec, "EncVectorSource",
      "S-57 ENC as a vector source. open() takes one base cell "
      "(US5CHSDC.000) or a directory with cells below it; every cell found "
      "becomes one tile. style_key and layer are both the object-class "
      "acronym (DEPARE, LIGHTS). The S-57 Appendix A catalogue (the CSVs) is "
      "REQUIRED and is looked for beside the cells unless catalog_dir names "
      "it — without it nothing could be symbolized.")
      .def(py::init<>())
      .def("open",
           [](fv::EncVectorSource& s, const std::string& path,
              const std::string& catalog_dir) {
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = catalog_dir.empty() ? s.Open(path)
                                        : s.Open(path, catalog_dir);
             }
             ThrowIfError(st);
           },
           "path"_a, "catalog_dir"_a = std::string())
      .def("open_cells",
           [](fv::EncVectorSource& s, const std::vector<std::string>& cells,
              const std::string& catalog_dir) {
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = s.OpenCells(cells, catalog_dir);
             }
             ThrowIfError(st);
           },
           "cells"_a, "catalog_dir"_a = std::string(),
           "Opens an EXPLICIT list of base cells instead of a directory. What "
           "a caller with a catalog wants: an exchange set holds several usage "
           "bands over the same water, so a source opened on the shared root "
           "serves all of them at once and stacks a 1:1,000,000 general cell "
           "under a 1:12,000 harbour one.")
      .def_property_readonly("cell_count", &fv::EncVectorSource::cell_count)
      .def("cell_path", &fv::EncVectorSource::cell_path, "index"_a)
      .def_property_readonly(
          "staleness_warning", &fv::EncVectorSource::StalenessWarning,
          "Non-empty when an open cell has update files on disk that the "
          "reader does not apply — the chart shown is out of date.")
      .def_property_readonly(
          "last_query_scamin_skipped",
          &fv::EncVectorSource::last_query_scamin_skipped,
          "Features the last query dropped for SCAMIN (S-57's own "
          "scale-thinning). Only ever non-zero when the query named a scale.");

  py::class_<fv::OsmVectorSource, fv::IVectorSource,
             std::shared_ptr<fv::OsmVectorSource>>(
      vec, "OsmVectorSource",
      "OSM vector tiles as a vector source. open() takes an .mbtiles file of "
      "MVT tiles. Unlike DNC and ENC, the query's SCALE picks a pyramid level "
      "(one zoom<->scale relation, shared with OsmStyleEngine), the unit of "
      "I/O is a tile, and geometry is clipped to the tile it came from so a "
      "feature straddling a seam is not drawn twice.")
      .def(py::init<>())
      .def("open",
           [](fv::OsmVectorSource& s, const std::string& path) {
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = s.Open(path);
             }
             ThrowIfError(st);
           },
           "mbtiles_path"_a)
      .def("set_display_mm_per_pixel", &fv::OsmVectorSource::SetDisplayMmPerPixel,
           "mm_per_pixel"_a,
           "The display pitch a scale is turned into a zoom with. Pass the "
           "SAME value the projection and the style engine got.")
      .def_property_readonly("display_mm_per_pixel",
                             &fv::OsmVectorSource::display_mm_per_pixel)
      .def("set_zoom_override", &fv::OsmVectorSource::SetZoomOverride, "z"_a,
           "Force a zoom level; < 0 restores automatic choice. Honoured "
           "exactly (not tile-capped).")
      .def_property_readonly("zoom_override",
                             &fv::OsmVectorSource::zoom_override)
      .def("set_max_tiles_per_query",
           &fv::OsmVectorSource::SetMaxTilesPerQuery, "n"_a)
      .def("set_tile_cache_capacity",
           &fv::OsmVectorSource::SetTileCacheCapacity, "n"_a)
      .def("set_clip_to_tile", &fv::OsmVectorSource::SetClipToTile, "on"_a,
           "Clip line/area geometry to its own tile (default on). Off gives a "
           "feature's WHOLE geometry, buffer included — what a graph builder "
           "wants and what a renderer must not have.")
      .def_property_readonly("clip_to_tile", &fv::OsmVectorSource::clip_to_tile)
      .def("set_style_key_tags",
           [](fv::OsmVectorSource& s, std::vector<std::string> tags) {
             s.SetStyleKeyTags(std::move(tags));
           },
           "tags"_a,
           "Tag names consulted in order for a feature's style_key; the "
           "layer name is the fallback. Default ['class'].")
      .def_property_readonly("last_query_zoom",
                             &fv::OsmVectorSource::last_query_zoom)
      .def_property_readonly("last_query_tiles_read",
                             &fv::OsmVectorSource::last_query_tiles_read)
      .def_property_readonly("last_query_tiles_missing",
                             &fv::OsmVectorSource::last_query_tiles_missing)
      .def_property_readonly("last_query_zoom_capped",
                             &fv::OsmVectorSource::last_query_zoom_capped)
      .def_property_readonly("last_query_truncated",
                             &fv::OsmVectorSource::last_query_truncated)
      .def_property_readonly("last_query_buffer_dropped",
                             &fv::OsmVectorSource::last_query_buffer_dropped)
      .def_property_readonly("last_query_clipped",
                             &fv::OsmVectorSource::last_query_clipped)
      .def_property_readonly("last_query_clipped_away",
                             &fv::OsmVectorSource::last_query_clipped_away)
      .def_property_readonly(
          "last_query_overzoom", &fv::OsmVectorSource::last_query_overzoom,
          "Levels the display is past the pyramid's deepest tiles; 0 "
          "normally. z14 geometry under z15+ rules is the intended "
          "behaviour, not a fault — this is how far it has gone.")
      .def_property_readonly("undeclared_layers",
                             &fv::OsmVectorSource::undeclared_layers)
      .def_property_readonly(
          "name", [](const fv::OsmVectorSource& s) { return s.file().name(); },
          "The tileset's own `name` metadata (a title, not a key).")
      .def_property_readonly("attribution",
                             [](const fv::OsmVectorSource& s) {
                               return s.file().attribution();
                             },
                             "ODbL for OSM data — display it.")
      .def_property_readonly(
          "min_zoom",
          [](const fv::OsmVectorSource& s) { return s.file().min_zoom(); })
      .def_property_readonly(
          "max_zoom",
          [](const fv::OsmVectorSource& s) { return s.file().max_zoom(); });

  py::class_<fv::IStyleEngine, std::shared_ptr<fv::IStyleEngine>>(
      vec, "IStyleEngine", "Maps (feature, scale) to draw passes.");

  // --- the cross-product rule layer (R2) -----------------------------------
  // Bound by REFERENCE off the engine that owns them (return_value_policy::
  // reference_internal), so `engine.rules().load_file(...)` mutates the
  // engine's own set rather than a copy that is thrown away.
  py::class_<fv::RuleSet>(
      vec, "RuleSet",
      "Feature rules in one syntax shared by every vector product.\n"
      "One rule per line:\n"
      "    hide layer=hydline\n"
      "    show key=BE010 scale=..50000\n"
      "    set  key=DA010 priority=3 labels=off\n"
      "    hide key=BH140 where hdp exists and hdp < 3\n"
      "Actions: show | hide | set. Selectors: layer=, key=, geom=, scale=, "
      "group=, category=. Effects (set): priority=, labels=, symbolscale=.")
      .def(py::init<>())
      .def("load_text",
           [](fv::RuleSet& rs, const std::string& text) {
             std::string err;
             ThrowIfError(rs.LoadText(text, &err));
           },
           "text"_a, "Parse rules from a string. All-or-nothing.")
      .def("load_file",
           [](fv::RuleSet& rs, const std::string& path) {
             std::string err;
             ThrowIfError(rs.LoadFile(path, &err));
           },
           "path"_a)
      .def("clear", &fv::RuleSet::Clear)
      .def("__len__", &fv::RuleSet::size);

  py::class_<fv::FeatureFamily>(
      vec, "FeatureFamily",
      "One named group of features: a name, a human title, and the rule-file "
      "selectors it covers. `enabled` is the switch.")
      .def_readonly("name", &fv::FeatureFamily::name)
      .def_readonly("title", &fv::FeatureFamily::title)
      .def_readonly("note", &fv::FeatureFamily::note)
      .def_readonly("enabled", &fv::FeatureFamily::enabled)
      .def_readonly("select", &fv::FeatureFamily::select)
      .def("__repr__", [](const fv::FeatureFamily& f) {
        return "<FeatureFamily " + f.name + (f.enabled ? " on>" : " OFF>");
      });

  py::class_<fv::FamilySet>(
      vec, "FamilySet",
      "Named groups of vector features a user can switch off, loaded from a "
      "JSON file (port/families/{dnc,enc,osm}-families.json ship as starters; "
      "comments are allowed in them).\n"
      "A family is a name over a list of rule-file SELECTORS, so switching one "
      "off is just `hide <selector>` in the engine's RuleSet — no second "
      "filter, and a selector can be anything a rule can say.\n"
      "Apply it FIRST and your own rule file second, so a rule can put one "
      "thing back:\n"
      "    fams.load_file(path)\n"
      "    fams.set_enabled('poi', False)\n"
      "    engine.rules().clear()\n"
      "    fams.append_rules(engine.rules())")
      .def(py::init<>())
      .def("load_file",
           [](fv::FamilySet& f, const std::string& path) {
             std::string err;
             ThrowIfError(f.LoadFile(path, &err));
           },
           "path"_a, "Read a family file. All-or-nothing.")
      .def("load_json",
           [](fv::FamilySet& f, const std::string& text) {
             std::string err;
             ThrowIfError(f.LoadJson(text, &err));
           },
           "text"_a)
      .def("append_rules",
           [](const fv::FamilySet& f, fv::RuleSet& rules) {
             std::string err;
             ThrowIfError(f.AppendRules(&rules, &err));
           },
           "rules"_a,
           "Append one hide rule per selector of every DISABLED family. An "
           "enabled family emits nothing.")
      .def("enabled", &fv::FamilySet::Enabled, "name"_a,
           "True for an unknown name: a family nobody declared hides nothing.")
      .def("set_enabled",
           [](fv::FamilySet& f, const std::string& name, bool on) {
             if (!f.SetEnabled(name, on))
               throw py::key_error("no such family: " + name);
           },
           "name"_a, "on"_a)
      .def_property_readonly("families", &fv::FamilySet::families)
      .def_property_readonly("product", &fv::FamilySet::product)
      .def_property_readonly("description", &fv::FamilySet::description)
      .def_property_readonly("path", &fv::FamilySet::path)
      .def_property_readonly("disabled_count", &fv::FamilySet::disabled_count)
      .def_property_readonly("epoch", &fv::FamilySet::epoch)
      .def("__len__",
           [](const fv::FamilySet& f) { return f.families().size(); });

  py::class_<fv::ViewingGroupSet>(
      vec, "ViewingGroupSet",
      "Runtime on/off state for a product's numbered viewing groups (GeoSym's "
      "IHO vgroup/txtgroup, S-52's viewing groups) plus the IMO display-"
      "category threshold. Group 0 is 'ungrouped' and always on.")
      .def("set", &fv::ViewingGroupSet::Set, "group"_a, "on"_a)
      .def("set_range", &fv::ViewingGroupSet::SetRange, "lo"_a, "hi"_a, "on"_a)
      .def("set_default", &fv::ViewingGroupSet::SetDefault, "on"_a)
      .def("clear_overrides", &fv::ViewingGroupSet::ClearOverrides)
      .def("enabled", &fv::ViewingGroupSet::Enabled, "group"_a)
      .def("set_max_category", &fv::ViewingGroupSet::SetMaxCategory,
           "category"_a,
           "1 = Display Base, 2 = Standard, 3 = Other (everything).")
      .def_property_readonly("max_category",
                             &fv::ViewingGroupSet::max_category);

  vec.attr("DISPLAY_BASE") = static_cast<int>(fv::kDisplayBase);
  vec.attr("DISPLAY_STANDARD") = static_cast<int>(fv::kDisplayStandard);
  vec.attr("DISPLAY_OTHER") = static_cast<int>(fv::kDisplayOther);

  py::class_<fv::GeoSymStyleEngine, fv::IStyleEngine,
             std::shared_ptr<fv::GeoSymStyleEngine>>(
      vec, "GeoSymStyleEngine",
      "GeoSym rule-table style engine (SymAssign/Graphics under data_dir). "
      "Labels are off by default (they need a host font on the canvas).")
      .def(py::init<>())
      .def("open",
           [](fv::GeoSymStyleEngine& e, const std::string& data_dir,
              int product_id) {
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = e.Open(data_dir, product_id);
             }
             ThrowIfError(st);
           },
           "data_dir"_a, "product_id"_a = static_cast<int>(fv::kGeoSymDnc))
      .def("set_draw_labels", &fv::GeoSymStyleEngine::SetDrawLabels, "on"_a)
      .def("set_color_adjust", &fv::GeoSymStyleEngine::SetColorAdjust,
           "brightness"_a, "contrast"_a)
      .def("mariner", &fv::GeoSymStyleEngine::mutable_mariner,
           py::return_value_policy::reference_internal,
           "The engine's MarinerSettings, by reference — "
           "`engine.mariner().safety_contour = 15` moves DNC's depth ramp "
           "(GeoSym ssdc/msdc/mssc/idsm/isdm under their S-52 names). "
           "NOTE: calling this bumps the style epoch, so a retained "
           "VectorScene rebuilds; use set_mariner() in a loop.")
      .def("set_mariner", &fv::GeoSymStyleEngine::SetMariner, "settings"_a,
           "Replace the settings wholesale. Bumps the style epoch only when "
           "something actually moved, so pushing the same values every frame "
           "is free.")
      .def("rules",
           py::overload_cast<>(&fv::GeoSymStyleEngine::rules),
           py::return_value_policy::reference_internal,
           "The engine's user/override RuleSet (empty by default).")
      .def("viewing_groups",
           py::overload_cast<>(&fv::GeoSymStyleEngine::viewing_groups),
           py::return_value_policy::reference_internal,
           "The engine's ViewingGroupSet: GeoSym's vgroup/txtgroup/dispcat "
           "columns as runtime toggles.")
      .def_property_readonly(
          "rule_predicate_evaluations",
          &fv::GeoSymStyleEngine::rule_predicate_evaluations,
          "Predicate evaluations performed by the rule plan; 0 for a "
          "key-only rule set however many features were styled.");

  // --- S-52 / ENC style engine (E3a-c), the second IStyleEngine ------------

  py::class_<fv::MarinerSettings>(
      vec, "MarinerSettings",
      "The MARINER's chart settings, not the producer's — S-52 makes these "
      "the mariner's, and they change WHAT is drawn, not just how. The "
      "safety contour is the single most important line on an ECDIS display. "
      "Depths in metres.\n"
      "SHARED BY BOTH CHART PRODUCTS: on DNC these are GeoSym's "
      "ssdc/msdc/mssc/idsm/isdm under their S-52 names (see "
      "fvkit/vector/mariner.h). DNC honours all of them except safety_depth — "
      "its ssdc is both the contour and the sounding threshold — and its "
      "defaults differ (safety 10 m, shallow pattern on).")
      .def(py::init<>())
      .def_readwrite("safety_contour", &fv::MarinerSettings::safety_contour)
      .def_readwrite("shallow_contour",
                     &fv::MarinerSettings::shallow_contour)
      .def_readwrite("deep_contour", &fv::MarinerSettings::deep_contour)
      .def_readwrite("safety_depth", &fv::MarinerSettings::safety_depth)
      .def_readwrite("two_shades", &fv::MarinerSettings::two_shades)
      .def_readwrite("shallow_pattern",
                     &fv::MarinerSettings::shallow_pattern);
  // The name every caller in the tree already uses. Same class object, so
  // isinstance() and the engine accessors agree either way.
  vec.attr("S52MarinerSettings") = vec.attr("MarinerSettings");

  vec.attr("S52_DAY") = static_cast<int>(fv::S52ColorScheme::kDay);
  vec.attr("S52_DUSK") = static_cast<int>(fv::S52ColorScheme::kDusk);
  vec.attr("S52_NIGHT") = static_cast<int>(fv::S52ColorScheme::kNight);
  vec.attr("S52_PAPER_CHART") = static_cast<int>(fv::S52PointStyle::kPaperChart);
  vec.attr("S52_SIMPLIFIED") = static_cast<int>(fv::S52PointStyle::kSimplified);
  vec.attr("S52_PLAIN_BOUNDARIES") =
      static_cast<int>(fv::S52AreaStyle::kPlainBoundaries);
  vec.attr("S52_SYMBOLIZED_BOUNDARIES") =
      static_cast<int>(fv::S52AreaStyle::kSymbolizedBoundaries);

  py::class_<fv::S52StyleEngine, fv::IStyleEngine,
             std::shared_ptr<fv::S52StyleEngine>>(
      vec, "S52StyleEngine",
      "Official S-52 presentation-library style engine for ENC. open() takes "
      "the directory holding chartsymbols.xml. The SECOND implementation of "
      "the same seam GeoSymStyleEngine implements — same VectorSymbol, same "
      "VectorRenderer, same rules()/viewing_groups().")
      .def(py::init<>())
      .def("open",
           [](fv::S52StyleEngine& e, const std::string& data_dir) {
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = e.Open(data_dir);
             }
             ThrowIfError(st);
           },
           "data_dir"_a)
      .def("set_draw_labels", &fv::S52StyleEngine::SetDrawLabels, "on"_a)
      .def("set_show_meta_objects", &fv::S52StyleEngine::SetShowMetaObjects,
           "on"_a,
           "Draw S-57 META objects (M_QUAL zones of confidence, M_COVR, "
           "M_NSYS...). OFF by default: they describe the DATA, not the "
           "world, and M_QUAL's triangles pattern over the whole chart.")
      .def_property_readonly("show_meta_objects",
                             &fv::S52StyleEngine::show_meta_objects)
      .def("set_color_scheme",
           [](fv::S52StyleEngine& e, int scheme) {
             ThrowIfError(
                 e.SetColorScheme(static_cast<fv::S52ColorScheme>(scheme)));
           },
           "scheme"_a, "S52_DAY / S52_DUSK / S52_NIGHT.")
      .def_property_readonly(
          "color_scheme",
          [](const fv::S52StyleEngine& e) {
            return static_cast<int>(e.color_scheme());
          })
      .def("set_point_style",
           [](fv::S52StyleEngine& e, int s) {
             e.SetPointStyle(static_cast<fv::S52PointStyle>(s));
           },
           "style"_a, "S52_PAPER_CHART / S52_SIMPLIFIED.")
      .def("set_area_style",
           [](fv::S52StyleEngine& e, int s) {
             e.SetAreaStyle(static_cast<fv::S52AreaStyle>(s));
           },
           "style"_a,
           "S52_PLAIN_BOUNDARIES / S52_SYMBOLIZED_BOUNDARIES.")
      .def("mariner", &fv::S52StyleEngine::mutable_mariner,
           py::return_value_policy::reference_internal,
           "The engine's own S52MarinerSettings, by reference — "
           "`engine.mariner().safety_contour = 10` re-styles the chart. "
           "NOTE: calling this bumps the style epoch (the C++ contract), so "
           "a retained VectorScene rebuilds; use set_mariner() in a loop.")
      .def("set_mariner", &fv::S52StyleEngine::SetMariner, "settings"_a,
           "Replace the settings wholesale. Bumps the style epoch only when "
           "something actually moved.")
      .def("rules", py::overload_cast<>(&fv::S52StyleEngine::rules),
           py::return_value_policy::reference_internal,
           "The engine's user/override RuleSet (empty by default).")
      .def("viewing_groups",
           py::overload_cast<>(&fv::S52StyleEngine::viewing_groups),
           py::return_value_policy::reference_internal,
           "S-52's viewing groups and the IMO display-category threshold "
           "(DISPLAY_BASE / STANDARD / OTHER).")
      .def_property_readonly(
          "unhandled_cs",
          [](const fv::S52StyleEngine& e) {
            py::dict d;
            for (const auto& kv : e.unhandled_cs())
              d[py::str(kv.first)] = kv.second;
            return d;
          },
          "Conditional-symbology procedures the cells asked for that are not "
          "implemented, and how often. Empty over the Charleston cells.")
      .def_property_readonly("placeholders_drawn",
                             &fv::S52StyleEngine::placeholders_drawn)
      .def_property_readonly("sector_lights_simplified",
                             &fv::S52StyleEngine::sector_lights_simplified)
      .def("reset_diagnostics", &fv::S52StyleEngine::ResetDiagnostics);

  // --- MapLibre GL style engine (O2), the third IStyleEngine ---------------

  py::class_<fv::OsmStyleEngine, fv::IStyleEngine,
             std::shared_ptr<fv::OsmStyleEngine>>(
      vec, "OsmStyleEngine",
      "MapLibre/Mapbox GL style-JSON loader over the shared lookup engine — "
      "the THIRD implementation of the same seam GeoSym and S-52 implement. "
      "load_file() is all-or-nothing and REJECTS anything outside the "
      "supported subset (expression filters, interpolate/step paint "
      "expressions, sprites) rather than ignoring it.")
      .def(py::init<>())
      .def("load_file",
           [](fv::OsmStyleEngine& e, const std::string& path) {
             std::string err;
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = e.LoadFile(path, &err);
             }
             ThrowIfError(st);
           },
           "path"_a)
      .def("load_text",
           [](fv::OsmStyleEngine& e, const std::string& json_text) {
             std::string err;
             ThrowIfError(e.LoadText(json_text, &err));
           },
           "json_text"_a)
      .def("set_draw_labels", &fv::OsmStyleEngine::SetDrawLabels, "on"_a)
      // The two halves of the zoom<->scale relation the source also needs.
      // Set BOTH to the same values the source got, or the style's minzoom
      // switches a layer on at a different scale than the tiles it styles.
      .def("set_reference_latitude", &fv::OsmStyleEngine::SetReferenceLatitude,
           "lat"_a,
           "The viewport's centre latitude. A StyleContext carries a scale "
           "and no geography, and Web Mercator's zoom<->scale relation is "
           "latitude-dependent: z12 is 1:270k at the equator and 1:190k off "
           "Charleston.")
      .def_property_readonly("reference_latitude",
                             &fv::OsmStyleEngine::reference_latitude)
      .def("set_display_mm_per_pixel",
           &fv::OsmStyleEngine::SetDisplayMmPerPixel, "mm_per_pixel"_a)
      .def_property_readonly("display_mm_per_pixel",
                             &fv::OsmStyleEngine::display_mm_per_pixel)
      .def("set_zoom_override", &fv::OsmStyleEngine::SetZoomOverride, "z"_a,
           "Pin the styling zoom; < 0 restores derivation from the scale. "
           "NOTE: do not use this to match a source zoom override that was "
           "clamped to the pyramid — past maxzoom the style is meant to keep "
           "going (overzoom).")
      .def_property_readonly("zoom_override",
                             &fv::OsmStyleEngine::zoom_override)
      .def("zoom_for_scale", &fv::OsmStyleEngine::ZoomForScale,
           "scale_denominator"_a,
           "The fractional, unclamped zoom this engine styles at.")
      .def("set_scaleless_zoom", &fv::OsmStyleEngine::SetScalelessZoom, "z"_a)
      .def_property_readonly("scaleless_zoom",
                             &fv::OsmStyleEngine::scaleless_zoom)
      .def("background",
           [](const fv::OsmStyleEngine& e, double scale_denominator) {
             fv::FvColor c{255, 255, 255, 255};
             if (!e.background(scale_denominator, &c)) return py::object(py::none());
             return py::object(py::make_tuple(c.r, c.g, c.b, c.a));
           },
           "scale_denominator"_a,
           "The style's `background` colour as (r, g, b, a), or None when the "
           "style has no background layer. A background is not a feature and "
           "cannot be a StyleResult — the application clears the canvas with "
           "it before rendering.")
      .def_property_readonly("style_name", &fv::OsmStyleEngine::style_name)
      .def_property_readonly(
          "layer_count",
          [](const fv::OsmStyleEngine& e) { return e.layers().size(); })
      .def("rules", py::overload_cast<>(&fv::OsmStyleEngine::rules),
           py::return_value_policy::reference_internal,
           "The engine's user/override RuleSet (empty by default).")
      .def("viewing_groups",
           py::overload_cast<>(&fv::OsmStyleEngine::viewing_groups),
           py::return_value_policy::reference_internal)
      .def_property_readonly(
          "ignored_icons",
          [](const fv::OsmStyleEngine& e) {
            py::dict d;
            for (const auto& kv : e.ignored_icons())
              d[py::str(kv.first)] = kv.second;
            return d;
          },
          "icon-image names the style asked for and did not get: there is no "
          "sprite sheet yet, so a symbol layer with an icon and no text draws "
          "nothing. Counted rather than silent.")
      .def_property_readonly("layers_that_drew",
                             &fv::OsmStyleEngine::layers_that_drew)
      .def_property_readonly("empty_labels", &fv::OsmStyleEngine::empty_labels)
      .def("reset_diagnostics", &fv::OsmStyleEngine::ResetDiagnostics);

  py::class_<fv::VectorRenderer>(
      vec, "VectorRenderer",
      "Queries the viewport, styles + sorts by priority, projects, clips and "
      "draws onto an ICanvas. Does NOT clear the canvas.")
      .def(py::init<fv::VectorSourcePtr, fv::StyleEnginePtr>(),
           "source"_a, "style"_a)
      // Line widths / text sizes go through device DPI; point-symbol size
      // goes through symbol_scale. Split so a demo can hold the map scale
      // fixed while growing feature symbology, or vice versa.
      .def("set_device_dpi", &fv::VectorRenderer::SetDeviceDpi, "dpi"_a)
      .def("set_symbol_scale", &fv::VectorRenderer::SetSymbolScale, "scale"_a)
      .def("set_max_features", &fv::VectorRenderer::SetMaxFeatures, "n"_a)
      // Identify: the pick index is built from the primitives the renderer
      // emits, so it is only valid for the LAST render.
      .def("set_pick_enabled", &fv::VectorRenderer::SetPickEnabled, "on"_a)
      .def_property_readonly("pick_enabled", &fv::VectorRenderer::pick_enabled)
      .def_property_readonly("pick_index", &fv::VectorRenderer::pick_index,
                             py::return_value_policy::reference_internal)
      .def("render",
           [](fv::VectorRenderer& r, const fv::MapProjection& proj,
              fv::ICanvas& canvas) {
             fv::Status st;
             {
               py::gil_scoped_release release;
               st = r.Render(proj, &canvas);
             }
             ThrowIfError(st);
           },
           "proj"_a, "canvas"_a)
      .def_property_readonly("features_queried",
                             &fv::VectorRenderer::features_queried)
      .def_property_readonly("draws_emitted",
                             &fv::VectorRenderer::draws_emitted)
      // R3a retained scene: a pan inside the margin skips query + style.
      .def("set_scene_margin", &fv::VectorRenderer::SetSceneMargin, "fraction"_a)
      .def("set_simplify_pixels", &fv::VectorRenderer::SetSimplifyPixels,
           "pixels"_a)
      .def("invalidate_scene", &fv::VectorRenderer::InvalidateScene)
      // Labels: 0 (the default) keeps text a constant size on screen; a
      // scale denominator makes every pixel-sized label scale with the map,
      // as if it had been authored at that scale.
      .def("set_label_reference_scale",
           &fv::VectorRenderer::SetLabelReferenceScale, "scale_denominator"_a)
      .def_property_readonly("label_reference_scale",
                             &fv::VectorRenderer::label_reference_scale)
      .def_property_readonly("scene_margin", &fv::VectorRenderer::scene_margin)
      .def_property_readonly("simplify_pixels",
                             &fv::VectorRenderer::simplify_pixels)
      .def_property_readonly("scene_reused", &fv::VectorRenderer::scene_reused)
      .def_property_readonly(
          "scene_vertices",
          [](const fv::VectorRenderer& r) { return r.scene().vertices_kept(); })
      .def_property_readonly(
          "scene_vertices_in",
          [](const fv::VectorRenderer& r) { return r.scene().vertices_in(); })
      .def_property_readonly("query_ms", &fv::VectorRenderer::query_ms)
      .def_property_readonly("style_ms", &fv::VectorRenderer::style_ms)
      .def_property_readonly("draw_ms", &fv::VectorRenderer::draw_ms);

  // ---- pyfvw.routing -----------------------------------------------------
  // O4: a routable road graph built offline from a RAW OSM extract, and a
  // shortest-path search over it. Deliberately not fed from the MVT pyramid
  // pyfvw.vector reads — vector tiles are simplified and tile-clipped, so
  // there is no topology in them to route on.
  py::module_ routing = m.def_submodule(
      "routing",
      "Road routing: RoadGraph.build/load an offline graph from a raw OSM "
      "extract, then Router.route() between two positions.");

  py::class_<fv::routing::RouteLeg>(routing, "RouteLeg",
      "A run of consecutive arcs sharing one road name.")
      .def_readonly("name", &fv::routing::RouteLeg::name)
      .def_readonly("road_class", &fv::routing::RouteLeg::klass)
      .def_readonly("length_m", &fv::routing::RouteLeg::length_m)
      .def_readonly("seconds", &fv::routing::RouteLeg::seconds)
      .def("__repr__", [](const fv::routing::RouteLeg& l) {
        return "<RouteLeg '" + l.name + "' " + l.klass + " " +
               std::to_string(l.length_m) + " m>";
      });

  py::class_<fv::routing::Route>(routing, "Route",
      "The result of a query. `found` is False when the two ends are simply "
      "not connected — that is an answer, not an error.")
      .def_readonly("found", &fv::routing::Route::found)
      .def_readonly("length_m", &fv::routing::Route::length_m)
      .def_readonly("seconds", &fv::routing::Route::seconds)
      .def_readonly("legs", &fv::routing::Route::legs)
      .def_readonly("nodes", &fv::routing::Route::nodes)
      .def_readonly("start_node", &fv::routing::Route::start_node)
      .def_readonly("end_node", &fv::routing::Route::end_node)
      .def_readonly("start_offset_m", &fv::routing::Route::start_offset_m,
                    "Metres from the requested start to the node it snapped to.")
      .def_readonly("end_offset_m", &fv::routing::Route::end_offset_m)
      .def_readonly("nodes_expanded", &fv::routing::Route::nodes_expanded)
      .def_readonly("stop_nodes", &fv::routing::Route::stop_nodes,
                    "One graph node per stop, in request order. Empty on a "
                    "two-point route.")
      .def_readonly("stop_offsets_m", &fv::routing::Route::stop_offsets_m,
                    "Metres from each requested stop to the node it snapped to.")
      .def_readonly("stop_geometry_index", &fv::routing::Route::stop_geometry_index,
                    "Where each stop falls in `geometry` — for marking the "
                    "stops, or cutting the line into per-leg pieces.")
      .def_readonly("u_turn_stops", &fv::routing::Route::u_turn_stops,
                    "The stops the route had to turn round at because there "
                    "was no other way out — a stop dropped up a driveway "
                    "rather than on the road that was meant.")
      .def_readonly("unreachable_leg", &fv::routing::Route::unreachable_leg,
                    "On found == False, which consecutive pair of stops has no "
                    "route between them (leg i runs from stop i to stop i+1). "
                    "Route.NO_LEG when the failure was not a leg's.")
      .def_readonly_static("NO_LEG", &fv::routing::Route::kNoLeg)
      .def_property_readonly(
          "geometry",
          [](const fv::routing::Route& r) { return r.geometry; },
          "The drawn line as [GeoPoint], road shape included — this is what "
          "goes onto a route overlay.")
      .def("__repr__", [](const fv::routing::Route& r) {
        if (!r.found) return std::string("<Route not found>");
        return "<Route " + std::to_string(r.length_m / 1000.0) + " km, " +
               std::to_string(r.seconds / 60.0) + " min>";
      });

  py::class_<fv::routing::RoadGraph, std::shared_ptr<fv::routing::RoadGraph>>(
      routing, "RoadGraph",
      "A noded road network: vertices only where ways meet, road shape kept "
      "as edge geometry.")
      .def_static(
          "load",
          [](const std::string& path) {
            auto g = std::make_shared<fv::routing::RoadGraph>();
            fv::Status s;
            {
              py::gil_scoped_release release;
              s = fv::routing::RoadGraph::Load(path, g.get());
            }
            ThrowIfError(s);
            return g;
          },
          "path"_a, "Open a .fvroad graph written by fvgraph or save().")
      .def_static(
          "build",
          [](const std::vector<std::string>& inputs, bool include_non_driveable,
             bool honor_oneway, bool honor_access, bool cycle_only,
             bool include_ferries) {
            fv::routing::RoadGraphBuildOptions options;
            options.include_non_driveable = include_non_driveable;
            options.honor_oneway = honor_oneway;
            options.honor_access = honor_access;
            options.cycle_only = cycle_only;
            options.include_ferries = include_ferries;
            auto g = std::make_shared<fv::routing::RoadGraph>();
            fv::Status s;
            {
              py::gil_scoped_release release;
              s = fv::routing::BuildRoadGraph(inputs, options, g.get(), nullptr);
            }
            ThrowIfError(s);
            return g;
          },
          "inputs"_a, "include_non_driveable"_a = true, "honor_oneway"_a = true,
          "honor_access"_a = true, "cycle_only"_a = false, "include_ferries"_a = true,
          "Build from raw .osm / .osm.pbf extracts. Minutes and gigabytes on a "
          "continent — this is the offline half; ship the .fvroad, not the pbf."
          " cycle_only keeps only the classes a bike may ride (and drops "
          "bicycle=no), giving a smaller bike-specific graph — the same as "
          "fvgraph build --cycle-only. include_ferries=False leaves "
          "`route=ferry` ways out, which can disconnect a coastal network — "
          "prefer the per-query ferry_penalty on route().")
      .def("save",
           [](const fv::routing::RoadGraph& g, const std::string& path) {
             ThrowIfError(g.Save(path));
           },
           "path"_a)
      .def_property_readonly("node_count", &fv::routing::RoadGraph::node_count)
      .def_property_readonly("arc_count", &fv::routing::RoadGraph::arc_count)
      .def_property_readonly("bounds", &fv::routing::RoadGraph::bounds)
      .def("location", &fv::routing::RoadGraph::location, "node"_a)
      .def(
          "nearest_node",
          [](const fv::routing::RoadGraph& g, const fv::GeoPoint& p,
             double max_meters) -> py::object {
            uint32_t node = 0;
            double meters = 0.0;
            if (!g.NearestNode(p, max_meters, &node, &meters)) return py::none();
            return py::make_tuple(node, meters);
          },
          "point"_a, "max_meters"_a = 500.0,
          "(node, metres) for the nearest graph node, or None when nothing is "
          "in range.");

  py::class_<fv::routing::Router>(routing, "Router",
      "Bidirectional Dijkstra over a RoadGraph. Holds a reference to the "
      "graph, which is kept alive for the router's lifetime.")
      .def(py::init<const fv::routing::RoadGraph&>(), "graph"_a,
           py::keep_alive<1, 2>())
      .def(
          "route",
          [](const fv::routing::Router& r, const fv::GeoPoint& from,
             const fv::GeoPoint& to, bool driving, const std::string& metric,
             double snap_meters, bool bidirectional, bool cycle_only,
             double private_penalty, py::object toll_penalty, py::object ferry_penalty,
             const std::string& profile, const std::string& rules) {
            const fv::routing::RouteOptions options =
                RouteOptionsFrom(driving, metric, snap_meters, bidirectional, cycle_only,
                                 private_penalty, toll_penalty, ferry_penalty, profile,
                                 rules);
            fv::routing::Route route;
            fv::Status s;
            {
              py::gil_scoped_release release;
              s = r.Route(from, to, options, &route);
            }
            ThrowIfError(s);
            return route;
          },
          "from_point"_a, "to_point"_a, "driving"_a = true, "metric"_a = "time",
          "snap_meters"_a = 500.0, "bidirectional"_a = true,
          "cycle_only"_a = false, "private_penalty"_a = 5.0,
          "toll_penalty"_a = py::none(), "ferry_penalty"_a = py::none(), "profile"_a = "",
          "rules"_a = "",
          "Snap both ends to the network and route between them. FvError "
          "(kOutOfCoverage) when an end is further than snap_meters from any "
          "road. cycle_only switches to the bicycle profile: it overrides "
          "driving and metric, riding only cycleable classes at a flat speed "
          "with cycleways and quiet streets preferred. private_penalty is what "
          "an access=private road costs as a multiple of its normal cost: it "
          "is passable (an address behind the gate has to be reachable) and "
          "priced, never deleted. 1.0 makes it free. profile names a profile "
          "from the JSON rule file (O5c) and OVERRIDES driving, cycle_only, "
          "metric and private_penalty; rules is the path to that file, "
          "defaulting to the built-in weights. The file is reread when it "
          "changes, so editing a weight changes the next route with nothing "
          "restarted — see routing.rule_profiles() and routing.rules_error().\n\n"
          "toll_penalty and ferry_penalty are the same idea for a tolled road "
          "and a ferry crossing, and they are the two that OUTRANK the "
          "profile rather than being overridden by it — so 'this profile, but "
          "no ferries today' needs no profile of its own. None (the default) "
          "takes the profile's own setting; a positive number is a "
          "multiplier; False or 'exclude' refuses those arcs outright, which "
          "can legitimately leave an island unreachable.")
      .def(
          "route_via",
          [](const fv::routing::Router& r, const std::vector<fv::GeoPoint>& stops,
             bool driving, const std::string& metric, double snap_meters,
             bool bidirectional, bool cycle_only, double private_penalty,
             py::object toll_penalty, py::object ferry_penalty,
             const std::string& profile, const std::string& rules,
             bool allow_u_turn_at_stops) {
            fv::routing::RouteOptions options =
                RouteOptionsFrom(driving, metric, snap_meters, bidirectional, cycle_only,
                                 private_penalty, toll_penalty, ferry_penalty, profile,
                                 rules);
            options.allow_u_turn_at_stops = allow_u_turn_at_stops;
            fv::routing::Route route;
            fv::Status s;
            {
              py::gil_scoped_release release;
              s = r.RouteVia(stops, options, &route);
            }
            ThrowIfError(s);
            return route;
          },
          "stops"_a, "driving"_a = true, "metric"_a = "time", "snap_meters"_a = 500.0,
          "bidirectional"_a = true, "cycle_only"_a = false, "private_penalty"_a = 5.0,
          "toll_penalty"_a = py::none(), "ferry_penalty"_a = py::none(),
          "profile"_a = "", "rules"_a = "", "allow_u_turn_at_stops"_a = false,
          "One route THROUGH the given stops, in order — not a concatenation "
          "of independent pairs. What the route arrives at a stop along "
          "constrains what it leaves along, so a turn restriction AT a stop "
          "binds, and by default the route will not turn round at a stop "
          "unless there is no other way out (the stops where it had to are in "
          "Route.u_turn_stops). allow_u_turn_at_stops=True drops that "
          "preference; signage is not a preference and always binds.\n\n"
          "All or nothing: one pair with no route between them makes "
          "found False with unreachable_leg naming it, and a stop further "
          "than snap_meters from any road raises FvError (kOutOfCoverage) "
          "saying which stop. Two stops is exactly route().");

  routing.def(
      "rule_profiles",
      [](const std::string& rules) {
        const std::shared_ptr<const fv::routing::RouteRules> r =
            rules.empty() ? fv::routing::RouteRules::Builtin() : RulesFileFor(rules).rules();
        py::list out;
        for (const std::string& name : r->names()) out.append(name);
        return out;
      },
      "rules"_a = "",
      "The profile names a rule file defines, for a menu to be built from. "
      "Rereads the file if it has changed, so a profile added while the "
      "application is running shows up here.");

  routing.def(
      "rules_error",
      [](const std::string& rules) {
        // "" means the rules in force came from the file. Anything else is why
        // they did not — with the last good (or built-in) weights still
        // routing, so this is a warning to show, not a failure to handle.
        return rules.empty() ? std::string() : RulesFileFor(rules).last_error();
      },
      "rules"_a = "",
      "Why the rule file is not in force, or '' when it is. A bad edit leaves "
      "the previously loaded weights routing and shows up here.");

  // ---- pyfvw.app -------------------------------------------------------
  // The application layer: overlay types as data, the shell seam, the file
  // flows, editors and picking. LAST, because it names types every submodule
  // above it registered (Overlay, MapProjection, Settings, OverlayManager).
  // ---- pyfvw.symbol / pyfvw.draw ---------------------------------------
  // After pyfvw.canvas and pyfvw.geo, whose types (ICanvas, MapProjection,
  // GeoPoint, LineKind) it names.
  pyfvw::BindDraw(m);

  // ---- pyfvw.nav -------------------------------------------------------
  // The moving map. After pyfvw.overlay (MovingMapOverlay derives from
  // Overlay) and pyfvw.geo/engine (it names GeoPoint and MapProjection).
  pyfvw::BindNav(m);

  pyfvw::BindApp(m);

}
