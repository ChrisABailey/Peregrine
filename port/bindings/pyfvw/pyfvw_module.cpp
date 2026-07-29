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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/engine.h"
#include "fvkit/overlay/grid.h"
#include "fvkit/overlay/manager.h"
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
#include "fvkit/raster.h"
#include "fvkit/vector/vector.h"
#include "fvkit/vector/style.h"
#include "fvkit/vector/renderer.h"

#include "fv_vpf_vector_source.h"  // fv::VpfVectorSource
#include "fv_geosym_style.h"       // fv::GeoSymStyleEngine
#include "fv_enc_vector_source.h"  // fv::EncVectorSource
#include "fv_s52_style.h"          // fv::S52StyleEngine
#include "fv_enc_format.h"         // fv::RegisterEncFormat

#include "geo_tool.h"  // GEO_string_to_lat_lon (fv_geo_tool)

namespace py = pybind11;
using namespace pybind11::literals;

namespace {

// C++-side carrier; the translator below re-raises it as pyfvw.FvError.
struct FvErrorCpp {
  fv::Status status;
};

void ThrowIfError(const fv::Status& s) {
  if (!s.ok()) throw FvErrorCpp{s};
}

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

  // Trampoline: Python exceptions never cross the SPI (contracts D3) —
  // on_draw errors become failed Status, event-handler errors log + decline.
  class PyOverlay : public fv::Overlay {
   public:
    using fv::Overlay::Overlay;

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
    bool OnKeyDown(int key) override {
      py::gil_scoped_acquire gil;
      py::function o = py::get_override(this, "on_key_down");
      if (!o) return false;
      try {
        return py::cast<bool>(o(key));
      } catch (py::error_already_set& err) {
        PyErr_Clear();
        return false;
      }
    }

   private:
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
  };

  py::class_<fv::Overlay, PyOverlay, std::shared_ptr<fv::Overlay>>(
      ovl, "Overlay",
      "Subclass and override on_draw(proj, canvas), on_mouse_down(e) -> "
      "bool, on_key_down(key) -> bool, ... Handlers returning True stop "
      "top-down routing; exceptions are contained (draw -> FvError from "
      "draw_all, events -> unhandled).")
      .def(py::init<std::string>(), "name"_a)
      .def_property_readonly("name", &fv::Overlay::Name)
      .def_property("visible", &fv::Overlay::IsVisible,
                    &fv::Overlay::SetVisible);

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
      .def("route_key_down", &fv::OverlayManager::RouteKeyDown, "key"_a);

  // ---- pyfvw.catalog ---------------------------------------------------
  py::module_ catalog =
      m.def_submodule("catalog", "L2 coverage catalog (SQLite + R-tree)");

  // ENC registers separately in C++ (fv_enc links fv_fvkit, so fvkit cannot
  // name it without a dependency cycle — see fv_enc_format.h). That is a
  // build-layering detail, not something a Python caller should have to know,
  // so one call still registers everything the port can read.
  catalog.def("register_builtin_formats",
              [] {
                fv::RegisterBuiltinFormats();
                fv::RegisterEncFormat();
              },
              "Register the built-in format adapters (dted, geotiff, cadrg, "
              "tiros, vpf, gpkg, dted-shaded, enc) with the scan registry. "
              "Idempotent; call before Catalog.scan.");

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
      .def_property_readonly("deg_per_pixel_lat", &fv::MapProjection::DegPerPixelLat)
      .def_property_readonly("deg_per_pixel_lon", &fv::MapProjection::DegPerPixelLon)
      .def_property_readonly("scale", &fv::MapProjection::Scale)
      .def_property_readonly("mm_per_pixel", &fv::MapProjection::MmPerPixel)
      .def_property_readonly("bounds", &fv::MapProjection::VmapBounds)
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

  py::class_<fv::S52MarinerSettings>(
      vec, "S52MarinerSettings",
      "The MARINER's chart settings, not the producer's — S-52 makes these "
      "the mariner's, and they change WHAT is drawn, not just how. The "
      "safety contour is the single most important line on an ECDIS display. "
      "Depths in metres.")
      .def_readwrite("safety_contour", &fv::S52MarinerSettings::safety_contour)
      .def_readwrite("shallow_contour",
                     &fv::S52MarinerSettings::shallow_contour)
      .def_readwrite("deep_contour", &fv::S52MarinerSettings::deep_contour)
      .def_readwrite("safety_depth", &fv::S52MarinerSettings::safety_depth)
      .def_readwrite("two_shades", &fv::S52MarinerSettings::two_shades)
      .def_readwrite("shallow_pattern",
                     &fv::S52MarinerSettings::shallow_pattern);

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
      .def("mariner", py::overload_cast<>(&fv::S52StyleEngine::mariner),
           py::return_value_policy::reference_internal,
           "The engine's own S52MarinerSettings, by reference — "
           "`engine.mariner().safety_contour = 10` re-styles the chart. "
           "NOTE: calling this bumps the style epoch (the C++ contract), so "
           "a retained VectorScene rebuilds; read the values off a saved "
           "reference if that matters in a loop.")
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
}
