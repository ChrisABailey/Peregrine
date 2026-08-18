// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// pyfvw.symbol and pyfvw.draw — the G2/G3 surface, bound (draw plan §3e).
//
// Its own TU for the same reason pyfvw_app.cpp is: it is a whole layer, and
// pyfvw_module.cpp is already 2600 lines.
//
// WHY THE STYLE STRUCTS ARE NOT ALL BOUND. GeoLineStyle is, because a caller
// holds one, edits it and reuses it across frames. LabelStyle and
// PointSymbolStyle are NOT: they would be a dozen properties to set before one
// call, and every one of them has a natural keyword. So a label is drawn with
// keywords and the struct stays a C++ detail — which also means adding a field
// to LabelStyle does not oblige a Python caller to learn it.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <vector>

#include "pyfvw_common.h"

#include "fvkit/canvas/canvas.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/symbol/builtin.h"
#include "fvkit/symbol/library.h"
#include "fvkit/symbol/png_library.h"

namespace pyfvw {

namespace py = pybind11;
using namespace pybind11::literals;

namespace {

fv::FvColor ToColor(py::sequence s) {
  fv::FvColor c;
  c.r = static_cast<unsigned char>(py::cast<int>(s[0]));
  c.g = static_cast<unsigned char>(py::cast<int>(s[1]));
  c.b = static_cast<unsigned char>(py::cast<int>(s[2]));
  c.a = s.size() > 3 ? static_cast<unsigned char>(py::cast<int>(s[3])) : 255;
  return c;
}

fv::LabelStyle MakeLabelStyle(py::sequence color, double size,
                              const std::string& font_path, int dx, int dy,
                              py::object halo_color, double halo_width,
                              fv::LabelHAlign halign, fv::LabelVAlign valign,
                              double ground_size_m, double spacing_px,
                              double max_angle_deg, double offset_px) {
  fv::LabelStyle ls;
  ls.valid = true;
  ls.style.color = ToColor(color);
  ls.style.size = size;
  ls.style.font_path = font_path;
  ls.dx = dx;
  ls.dy = dy;
  ls.halign = halign;
  ls.valign = valign;
  ls.halo_width = halo_width;
  if (!halo_color.is_none())
    ls.halo_color = ToColor(py::cast<py::sequence>(halo_color));
  if (ground_size_m > 0.0) {
    ls.size_unit = fv::LabelSizeUnit::kMeters;
    ls.ground_size_m = ground_size_m;
  }
  ls.spacing_px = spacing_px;
  ls.max_angle_deg = max_angle_deg;
  ls.offset_px = offset_px;
  return ls;
}

using SurfacePaths = std::vector<std::vector<std::pair<double, double>>>;

std::vector<std::vector<fv::SurfacePoint>> FromPyPaths(const SurfacePaths& in) {
  std::vector<std::vector<fv::SurfacePoint>> out;
  out.reserve(in.size());
  for (const auto& sub : in) {
    std::vector<fv::SurfacePoint> path;
    path.reserve(sub.size());
    for (const auto& p : sub) path.push_back(fv::SurfacePoint{p.first, p.second});
    out.push_back(std::move(path));
  }
  return out;
}

}  // namespace

void BindDraw(py::module_& m) {
  // ---- pyfvw.symbol ------------------------------------------------------
  py::module_ sym = m.def_submodule(
      "symbol",
      "G2 symbol libraries: where a symbol_id turns into something drawable. "
      "Every style engine is one; these three are not style engines.");

  py::class_<fv::ISymbolLibrary, std::shared_ptr<fv::ISymbolLibrary>> ilib(
      sym, "ISymbolLibrary",
      "Base of every symbol library. The pointers it hands out are owned and "
      "cached by the library, which is what lets a caller resolve an id once "
      "and stamp it hundreds of times.");
  ilib.def(
          "has",
          [](fv::ISymbolLibrary& l, const std::string& id) {
            return l.Symbol(id) != nullptr || l.Pixmap(id) != nullptr;
          },
          "symbol_id"_a,
          "Whether this library can draw `symbol_id`, in either form.")
      .def_property_readonly("himetric_per_symbol_pixel",
                             &fv::ISymbolLibrary::himetric_per_symbol_pixel);

  py::class_<fv::BuiltinSymbolLibrary, fv::ISymbolLibrary,
             std::shared_ptr<fv::BuiltinSymbolLibrary>>(
      sym, "BuiltinSymbolLibrary",
      "The symbols the port authors itself, as display lists rather than "
      "files: six marker shapes, five line decorations, a north arrow and a "
      "crosshair. Colour is a LIBRARY setting, not a per-draw one — a caller "
      "that wants two colours makes two libraries, and these cost nothing.")
      .def(py::init<>())
      .def(
          "set_color",
          [](fv::BuiltinSymbolLibrary& l, py::sequence c) {
            l.SetColor(ToColor(c));
          },
          "color"_a, "Re-bakes every symbol in this colour.")
      .def(
          "set_stroke_width",
          [](fv::BuiltinSymbolLibrary& l, double px) { l.SetStrokeWidth(px); },
          "nominal_px"_a);

  py::class_<fv::PngSymbolLibrary, fv::ISymbolLibrary,
             std::shared_ptr<fv::PngSymbolLibrary>>(
      sym, "PngSymbolLibrary",
      "Loose <id>.png files in a directory, or a sprite sheet plus its "
      "MapLibre sprite.json. Lazy: a directory of 400 icons costs 400 "
      "filenames until one is asked for.")
      .def(py::init<>())
      .def("set_prefer_high_dpi", &fv::PngSymbolLibrary::SetPreferHighDpi,
           "on"_a,
           "Bind an id to its @2x / pixelRatio-2 artwork when it has both. "
           "MUST be called BEFORE open_*, since it decides which file an id "
           "binds to. Default false (the 1x), because only the caller knows "
           "the device — the same division of labour as the pick tolerance.")
      .def(
          "open_directory",
          [](fv::PngSymbolLibrary& l, const std::string& dir) {
            ThrowIfError(l.OpenDirectory(dir));
          },
          "directory"_a,
          "Every <id>.png in `dir` (non-recursive); the id is the stem. An "
          "empty directory is not an error — a library with nothing in it is "
          "a normal composite member.")
      .def_property_readonly("ids", &fv::PngSymbolLibrary::ids,
                             "The ids this library can answer, sorted.")
      .def_property_readonly("loaded", &fv::PngSymbolLibrary::loaded,
                             "Tiles actually decoded so far — the laziness, "
                             "observable.")
      .def("__len__", &fv::PngSymbolLibrary::size)
      .def(
          "open_sheet",
          [](fv::PngSymbolLibrary& l, const std::string& png,
             const std::string& json) {
            ThrowIfError(l.OpenSheet(png, json));
          },
          "png_path"_a, "json_path"_a,
          "A re-open REPLACES: stale entries indexing a different sheet is "
          "the worse failure.");

  py::class_<fv::CompositeSymbolLibrary, fv::ISymbolLibrary,
             std::shared_ptr<fv::CompositeSymbolLibrary>>(
      sym, "CompositeSymbolLibrary",
      "Ordered lookup across several libraries — 'mine first, then GeoSym's'. "
      "Members are BORROWED and must outlive the composite; keep a reference "
      "to each on the Python side.")
      .def(py::init<>())
      .def(
          "add",
          [](fv::CompositeSymbolLibrary& c, fv::ISymbolLibrary* lib) {
            c.Add(lib);
          },
          "library"_a, py::keep_alive<1, 2>());

  // The builtin ids, so a caller spells one without a typo.
  py::module_ bs = sym.def_submodule("builtin", "The builtin symbol ids.");
  bs.attr("CIRCLE") = fv::builtin_symbol::kCircle;
  bs.attr("SQUARE") = fv::builtin_symbol::kSquare;
  bs.attr("TRIANGLE") = fv::builtin_symbol::kTriangle;
  bs.attr("DIAMOND") = fv::builtin_symbol::kDiamond;
  bs.attr("CROSS") = fv::builtin_symbol::kCross;
  bs.attr("STAR") = fv::builtin_symbol::kStar;
  bs.attr("TICK") = fv::builtin_symbol::kTick;
  bs.attr("ARROW") = fv::builtin_symbol::kArrowhead;
  bs.attr("CROSSTIE") = fv::builtin_symbol::kCrosstie;
  bs.attr("TEE") = fv::builtin_symbol::kTee;
  bs.attr("NOTCH") = fv::builtin_symbol::kNotch;
  bs.attr("NORTH") = fv::builtin_symbol::kNorthArrow;
  bs.attr("CROSSHAIR") = fv::builtin_symbol::kCrosshair;
  bs.attr("OWNSHIP") = fv::builtin_symbol::kOwnship;
  {
    std::vector<std::string> all;
    for (const char* const* n = fv::builtin_symbol::kAll; *n != nullptr; ++n)
      all.push_back(*n);
    bs.attr("ALL") = all;
  }

  // ---- pyfvw.draw --------------------------------------------------------
  py::module_ draw = m.def_submodule(
      "draw",
      "G3 GeoDraw: geographic verbs over a canvas. An overlay draws lines "
      "that are really geodesics, stamps symbols from a library and writes "
      "haloed labels, with no geometry of its own.");

  py::enum_<fv::RenderState>(
      draw, "RenderState",
      "What a draw MEANS as opposed to how it is styled (G4). HIGHLIGHTED "
      "stamps the ink's own silhouette around it in the highlight colour, so "
      "a selected thing keeps the colour it is identified by.")
      .value("NORMAL", fv::RenderState::kNormal)
      .value("HIGHLIGHTED", fv::RenderState::kHighlighted);

  py::enum_<fv::LabelHAlign>(draw, "HAlign")
      .value("LEFT", fv::LabelHAlign::kLeft)
      .value("CENTER", fv::LabelHAlign::kCenter)
      .value("RIGHT", fv::LabelHAlign::kRight);
  py::enum_<fv::LabelVAlign>(draw, "VAlign")
      .value("BASELINE", fv::LabelVAlign::kBaseline)
      .value("BOTTOM", fv::LabelVAlign::kBottom)
      .value("CENTER", fv::LabelVAlign::kCenter)
      .value("TOP", fv::LabelVAlign::kTop);

  py::class_<fv::GeoLineStyle>(
      draw, "LineStyle",
      "How one geographic line is inked: a casing under a stroke under a "
      "pattern. Build one with solid_line() or preset_line().")
      .def(py::init<>())
      .def_property_readonly(
          "has_pattern",
          [](const fv::GeoLineStyle& s) { return s.pattern.valid; })
      .def_property_readonly(
          "width", [](const fv::GeoLineStyle& s) {
            return s.pattern.valid ? s.pattern.pen.width : s.stroke.pen.width;
          })
      .def(
          "set_color",
          [](fv::GeoLineStyle& s, py::sequence c) {
            const fv::FvColor col = ToColor(c);
            s.stroke.pen.color = col;
            s.pattern.pen.color = col;
          },
          "color"_a, "Recolours the line (not its casing).")
      .def(
          "add_casing",
          [](fv::GeoLineStyle& s, py::sequence c, int extra_px) {
            fv::AddCasing(&s, ToColor(c), extra_px);
            return &s;
          },
          "color"_a, "extra_px"_a = 2, py::return_value_policy::reference,
          "A halo for a line: the same geometry, `extra_px` wider on each "
          "side, drawn FIRST. On a patterned line the casing follows the "
          "pattern, because a solid bar under a dashed line reads as a solid "
          "line.")
      .def(
          "no_casing",
          [](fv::GeoLineStyle& s) {
            s.casing.valid = false;
            return &s;
          },
          py::return_value_policy::reference);

  draw.def(
      "solid_line",
      [](py::sequence color, int width) {
        return fv::SolidGeoLine(ToColor(color), width);
      },
      "color"_a, "width"_a = 1);

  draw.def(
      "preset_line",
      [](const std::string& preset, py::sequence color, int width) {
        return fv::PresetGeoLine(preset, ToColor(color), width);
      },
      "preset"_a, "color"_a, "width"_a = 1,
      "A named preset from PRESETS. An unknown name falls back to solid — a "
      "style file naming a preset this build does not have should draw a "
      "line, not nothing.");

  {
    std::vector<std::string> all;
    for (const char* const* n = fv::line_preset::kAll; *n != nullptr; ++n)
      all.push_back(*n);
    draw.attr("PRESETS") = all;
  }

  py::class_<fv::GeoDraw>(
      draw, "GeoDraw",
      "Holds a projection, a canvas and (optionally) a symbol library for the "
      "duration of one frame. Cheap to construct: make one per on_draw.")
      .def(py::init([](const fv::MapProjection& proj, fv::ICanvas& canvas,
                       fv::ISymbolLibrary* symbols) {
             return new fv::GeoDraw(proj, &canvas, symbols);
           }),
           "proj"_a, "canvas"_a, "symbols"_a = nullptr,
           py::keep_alive<1, 2>(), py::keep_alive<1, 3>(),
           py::keep_alive<1, 4>())
      .def_property("symbol_scale", &fv::GeoDraw::symbol_scale,
                    &fv::GeoDraw::SetSymbolScale)
      .def_property("symbol_dpi_scale", &fv::GeoDraw::symbol_dpi_scale,
                    &fv::GeoDraw::SetSymbolDpiScale,
                    "Device correction for symbol size, 1.0 by default. Chart "
                    "products are pinned to their own nominal pixel and cannot "
                    "move; overlay symbology has no goldens, so a shell that "
                    "knows its device can set dpi/100 here.")
      .def_property("state", &fv::GeoDraw::state, &fv::GeoDraw::SetState,
                    "RenderState for every following verb. An overlay sets it "
                    "for the selected feature, draws, and sets it back.")
      .def(
          "set_highlight",
          [](fv::GeoDraw& d, py::sequence color, double width_px) {
            d.SetHighlight(ToColor(color), width_px);
          },
          "color"_a, "width_px"_a = 3.0,
          "The highlight colour and how far past the ink it shows. Defaults "
          "to FalconView's selection yellow at 3 px.")
      .def("set_clip", &fv::GeoDraw::SetClip, "on"_a)
      .def_property("pick_enabled", &fv::GeoDraw::pick_enabled,
                    &fv::GeoDraw::SetPickEnabled)
      .def(
          "set_feature",
          [](fv::GeoDraw& d, int id, int priority) {
            d.SetFeature(static_cast<int32_t>(id), priority);
          },
          "id"_a, "priority"_a = 0,
          "What subsequent draws are attributed to in the pick index.")
      .def("clear_pick", &fv::GeoDraw::ClearPick)
      .def(
          "hit_test",
          [](const fv::GeoDraw& d, int x, int y, double tolerance) {
            std::vector<std::pair<int, double>> out;
            for (const fv::PickHit& h : d.pick_index().HitTest(x, y, tolerance))
              out.emplace_back(h.ref.feature, h.distance);
            return out;
          },
          "x"_a, "y"_a, "tolerance"_a = 3.0,
          "[(feature_id, distance_px), ...] over the ink THIS GeoDraw emitted, "
          "topmost first. Empty unless pick_enabled was set before drawing.")
      .def_property_readonly("draws_emitted", &fv::GeoDraw::draws_emitted)
      .def_property_readonly("halo_draws", &fv::GeoDraw::halo_draws)
      .def_property_readonly("highlight_draws", &fv::GeoDraw::highlight_draws,
                             "Highlight passes since the last reset. Never in "
                             "the pick index: the user aims at the feature, "
                             "not at its glow.")

      // --- lines ---------------------------------------------------------
      .def(
          "line",
          [](fv::GeoDraw& d, const fv::GeoPoint& a, const fv::GeoPoint& b,
             const fv::GeoLineStyle& style, fv::LineKind kind) {
            ThrowIfError(d.DrawGeoLine(a, b, kind, style));
          },
          "a"_a, "b"_a, "style"_a, "kind"_a = fv::LineKind::kGreatCircle)
      .def(
          "polyline",
          [](fv::GeoDraw& d, const std::vector<fv::GeoPoint>& pts,
             const fv::GeoLineStyle& style, fv::LineKind kind, bool closed) {
            ThrowIfError(d.DrawGeoPolyline(pts, kind, style, closed));
          },
          "points"_a, "style"_a, "kind"_a = fv::LineKind::kGreatCircle,
          "closed"_a = false,
          "A leg that clips away BREAKS the run rather than being joined "
          "across (G3).")
      .def(
          "circle",
          [](fv::GeoDraw& d, const fv::GeoPoint& c, double radius_m,
             const fv::GeoLineStyle& style, int num_points) {
            ThrowIfError(d.DrawGeoCircle(c, radius_m, style, num_points));
          },
          "center"_a, "radius_m"_a, "style"_a,
          "num_points"_a = fv::kDefaultCirclePoints)
      .def(
          "ellipse",
          [](fv::GeoDraw& d, const fv::GeoPoint& c, double vert_m,
             double horz_m, double rotation_deg, const fv::GeoLineStyle& style,
             int num_points) {
            ThrowIfError(d.DrawGeoEllipse(c, vert_m, horz_m, rotation_deg,
                                          style, num_points));
          },
          "center"_a, "vert_radius_m"_a, "horz_radius_m"_a,
          "rotation_deg"_a, "style"_a,
          "num_points"_a = fv::kDefaultCirclePoints)
      .def(
          "arc",
          [](fv::GeoDraw& d, const fv::GeoPoint& c, double radius_m,
             double start_deg, double sweep_deg, const fv::GeoLineStyle& style,
             int points_per_circle) {
            ThrowIfError(d.DrawGeoArc(c, radius_m, start_deg, sweep_deg, style,
                                      points_per_circle));
          },
          "center"_a, "radius_m"_a, "start_bearing_deg"_a, "sweep_deg"_a,
          "style"_a, "points_per_circle"_a = fv::kDefaultCirclePoints)
      .def(
          "surface_path",
          [](fv::GeoDraw& d, const SurfacePaths& paths,
             const fv::GeoLineStyle& style) {
            ThrowIfError(d.DrawSurfacePath(FromPyPaths(paths), style));
          },
          "paths"_a, "style"_a,
          "Already-projected sub-paths (what pyfvw.geo.*_path returns), for a "
          "caller that has its own geometry and only wants the styling.")

      // --- symbols -------------------------------------------------------
      .def(
          "symbol",
          [](fv::GeoDraw& d, const fv::GeoPoint& at, const std::string& id,
             double scale, double rotation_deg) {
            fv::PointSymbolStyle s;
            s.valid = true;
            s.symbol_id = id;
            s.scale = scale;
            s.rotation_deg = rotation_deg;
            ThrowIfError(d.DrawSymbol(at, id, s));
          },
          "at"_a, "symbol_id"_a, "scale"_a = 1.0, "rotation_deg"_a = 0.0,
          "Stamps a library symbol with its own origin on `at`. A symbol the "
          "library does not have raises FvError rather than drawing nothing — "
          "a mistyped id is the likeliest failure and is invisible otherwise.")
      .def(
          "symbol_at_pixel",
          [](fv::GeoDraw& d, double x, double y, const std::string& id,
             double scale, double rotation_deg) {
            fv::PointSymbolStyle s;
            s.valid = true;
            s.symbol_id = id;
            s.scale = scale;
            s.rotation_deg = rotation_deg;
            ThrowIfError(d.DrawSymbolAtPixel(x, y, id, s));
          },
          "x"_a, "y"_a, "symbol_id"_a, "scale"_a = 1.0,
          "rotation_deg"_a = 0.0,
          "For map furniture (a north arrow, a legend) that belongs to the "
          "CANVAS rather than to a position on the earth.")

      // --- labels --------------------------------------------------------
      .def(
          "label",
          [](fv::GeoDraw& d, const fv::GeoPoint& at, const std::string& text,
             py::sequence color, double size, const std::string& font_path,
             int dx, int dy, py::object halo_color, double halo_width,
             fv::LabelHAlign halign, fv::LabelVAlign valign,
             double ground_size_m) {
            ThrowIfError(d.DrawLabel(
                at, text,
                MakeLabelStyle(color, size, font_path, dx, dy, halo_color,
                               halo_width, halign, valign, ground_size_m, 0.0,
                               45.0, 0.0)));
          },
          "at"_a, "text"_a, "color"_a, "size"_a = 12.0, "font_path"_a = "",
          "dx"_a = 0, "dy"_a = 0, "halo_color"_a = py::none(),
          "halo_width"_a = 0.0, "halign"_a = fv::LabelHAlign::kLeft,
          "valign"_a = fv::LabelVAlign::kBaseline, "ground_size_m"_a = 0.0,
          "A haloed, aligned string at a geographic point. The halo is the "
          "Windows method — the same string stamped 4 or 8 times a pixel or "
          "two off, then the text over it — so every canvas backend gets it.")
      .def(
          "label_at_pixel",
          [](fv::GeoDraw& d, double x, double y, const std::string& text,
             py::sequence color, double size, const std::string& font_path,
             int dx, int dy, py::object halo_color, double halo_width,
             fv::LabelHAlign halign, fv::LabelVAlign valign) {
            ThrowIfError(d.DrawLabelAtPixel(
                x, y, text,
                MakeLabelStyle(color, size, font_path, dx, dy, halo_color,
                               halo_width, halign, valign, 0.0, 0.0, 45.0,
                               0.0)));
          },
          "x"_a, "y"_a, "text"_a, "color"_a, "size"_a = 12.0,
          "font_path"_a = "", "dx"_a = 0, "dy"_a = 0,
          "halo_color"_a = py::none(), "halo_width"_a = 0.0,
          "halign"_a = fv::LabelHAlign::kLeft,
          "valign"_a = fv::LabelVAlign::kBaseline)
      .def(
          "label_along_path",
          [](fv::GeoDraw& d, const SurfacePaths& paths,
             const std::string& text, py::sequence color, double size,
             const std::string& font_path, py::object halo_color,
             double halo_width, double spacing_px, double max_angle_deg,
             double offset_px) {
            fv::LabelStyle ls = MakeLabelStyle(
                color, size, font_path, 0, 0, halo_color, halo_width,
                fv::LabelHAlign::kLeft, fv::LabelVAlign::kBaseline, 0.0,
                spacing_px, max_angle_deg, offset_px);
            ls.placement = fv::LabelPlacement::kAlongPath;
            ThrowIfError(d.DrawLabelAlongPath(FromPyPaths(paths), text, ls));
          },
          "paths"_a, "text"_a, "color"_a, "size"_a = 12.0,
          "font_path"_a = "", "halo_color"_a = py::none(),
          "halo_width"_a = 0.0, "spacing_px"_a = 0.0,
          "max_angle_deg"_a = 45.0, "offset_px"_a = 0.0,
          "Glyph by glyph along projected sub-paths, each rotated to the local "
          "tangent. A run that does not fit, or that turns harder than "
          "max_angle_deg, is not drawn at all — half a name is worse than "
          "none.");
}

}  // namespace pyfvw
