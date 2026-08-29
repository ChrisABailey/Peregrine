// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// GridOverlay — see fvkit/overlay/grid.h.
//
// Ported from Applications/FalconView/grid_map: grdelem.cpp (the line walk),
// gline.cpp (major/minor), tickmark.cpp (ticks), label.cpp (label text and
// placement), grid_ovl.cpp (the draw order). What changed and why is in the
// header; what the numbers are is in grid_spacing.h.

#include "fvkit/overlay/grid.h"

#include <cmath>
#include <cstdio>

#include "fvkit/canvas/geo_draw.h"
#include "fvkit/canvas/label_placer.h"
#include "fvkit/scale_table.h"

namespace fv {

namespace {

// ---------------------------------------------------------------------------
// The property schema
// ---------------------------------------------------------------------------

enum Prop {
  kLineColor = 0,
  kLineWidth,
  kShowCasing,
  kCasingColor,
  kShowMinorLines,
  kShowTicks,
  kTickLengthPx,
  kShowLabels,
  kLabelMinorLines,
  kLabelSizePx,
  kLabelColor,
  kLabelHaloColor,
  kLabelHaloWidthPx,
  kLabelFont,
  kPropCount
};

using app::PropertySpec;
using app::PropertyType;
using app::PropertyValue;

PropertySpec Spec(const char* key, const char* label, const char* group,
                  PropertyValue def, double lo = 0.0, double hi = 0.0,
                  const char* help = "") {
  PropertySpec s;
  s.key = key;
  s.label = label;
  s.group = group;
  s.type = def.type;
  s.default_value = def;
  s.min = lo;
  s.max = hi;
  s.help = help;
  return s;
}

const std::vector<PropertySpec>& Specs() {
  static const std::vector<PropertySpec> specs = [] {
    std::vector<PropertySpec> v;
    v.reserve(kPropCount);
    // The order here is the order a generic dialog lays them out, so it is
    // grouped rather than alphabetical.
    v.push_back(Spec("line_color", "Line colour", "Lines",
                     PropertyValue::Color(FvColor{255, 255, 255, 96})));
    v.push_back(Spec("line_width", "Line width", "Lines",
                     PropertyValue::Int(1), 1, 8, "pixels"));
    v.push_back(Spec("show_casing", "Outline lines", "Lines",
                     PropertyValue::Bool(true),
                     0, 0,
                     "Draws a wider line underneath in the casing colour, so "
                     "the grid stays visible over light and dark chart alike. "
                     "FalconView called this the background line."));
    v.push_back(Spec("casing_color", "Outline colour", "Lines",
                     PropertyValue::Color(FvColor{0, 0, 0, 128})));
    v.push_back(Spec("show_minor_lines", "Show minor lines", "Lines",
                     PropertyValue::Bool(true)));
    v.push_back(Spec("show_ticks", "Show tick marks", "Ticks",
                     PropertyValue::Bool(true),
                     0, 0,
                     "Unlabelled marks along the major lines, subdividing "
                     "them without adding lines to the picture."));
    v.push_back(Spec("tick_length_px", "Tick length", "Ticks",
                     PropertyValue::Int(12), 2, 40, "pixels"));
    v.push_back(Spec("show_labels", "Show labels", "Labels",
                     PropertyValue::Bool(true)));
    v.push_back(Spec("label_minor_lines", "Label minor lines too", "Labels",
                     PropertyValue::Bool(false),
                     0, 0,
                     "Off = only major lines are labelled, which is "
                     "FalconView's default and is what keeps a dense "
                     "graticule readable."));
    v.push_back(Spec("label_size_px", "Label size", "Labels",
                     PropertyValue::Double(12.0), 6, 48, "pixels"));
    v.push_back(Spec("label_color", "Label colour", "Labels",
                     PropertyValue::Color(FvColor{255, 255, 255, 255})));
    v.push_back(Spec("label_halo_color", "Label outline", "Labels",
                     PropertyValue::Color(FvColor{0, 0, 0, 200})));
    v.push_back(Spec("label_halo_width_px", "Label outline width", "Labels",
                     PropertyValue::Double(1.0), 0, 4, "pixels; 0 = none"));
    v.push_back(Spec("label_font", "Label font", "Labels",
                     PropertyValue::String(std::string()),
                     0, 0,
                     "Path to a TTF/TTC file. Empty means the canvas's own "
                     "default font, which is what a shell that has already "
                     "chosen one wants."));
    return v;
  }();
  return specs;
}

int IndexOf(const std::string& key) {
  const std::vector<PropertySpec>& specs = Specs();
  for (size_t i = 0; i < specs.size(); ++i)
    if (specs[i].key == key) return static_cast<int>(i);
  return -1;
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

// A graticule line: the constant coordinate, and whether it is a major.
struct GridLineDef {
  double value = 0.0;
  bool major = false;
};

// FalconView's is_major, with the wrap case closed. It tested only
// `fmod(|v|, major) <= 1e-6`, which misses every line whose remainder lands
// just BELOW the spacing rather than just above zero -- and that is the
// common case when the value came out of a floating multiply. Both ends count
// here, so a major line cannot silently demote itself to a minor one.
bool IsMajor(double value, double major_spacing) {
  if (!(major_spacing > 0.0)) return false;
  const double d = std::fmod(std::fabs(value), major_spacing);
  const double eps = 1e-6;
  return d <= eps || major_spacing - d <= eps;
}

// Lines of one family across [lo, hi], stepping by the minor spacing.
//
// The value is index * step rather than a running sum: a 5-arc-second
// graticule across a degree is 720 steps, and accumulated addition puts the
// last line visibly off its own coordinate.
void BuildLines(double lo, double hi, const GraticuleSpacing& sp,
                std::vector<GridLineDef>* out) {
  if (!sp.has_lines()) return;
  const double step = sp.minor_line_deg;
  // A viewport the table has no business drawing (zoomed far past the finest
  // row, or a degenerate projection) would otherwise spin here.
  constexpr int kMaxLines = 4096;
  const long long first =
      static_cast<long long>(std::ceil(lo / step - 1e-9));
  for (int n = 0; n < kMaxLines; ++n) {
    const double v = static_cast<double>(first + n) * step;
    if (v > hi + 1e-12) break;
    GridLineDef d;
    d.value = v;
    d.major = IsMajor(v, sp.major_line_deg);
    out->push_back(d);
  }
}

// FalconView's adjust_lat_spacing: near the poles a parallel's ticks are
// spread out, because a degree of longitude has shrunk and the marks would
// otherwise merge. Four scales only, exactly as the original had it -- keyed
// on the table row we actually resolved to rather than on MapScale equality.
void AdjustPolarTickSpacing(double scale, double lat, double* major,
                            double* minor) {
  const double a = std::fabs(lat);
  auto set = [&](double maj, double min) {
    *major = maj;
    *minor = min;
  };
  if (scale == 5000000.0 && a >= 65.0) set(15.0 / 60.0, 0.0);
  else if (scale == 2000000.0 && a >= 73.0) set(30.0 / 60.0, 15.0 / 60.0);
  else if (scale == 1000000.0 && a >= 64.0) set(10.0 / 60.0, 5.0 / 60.0);
  else if (scale == 500000.0 && a >= 76.0) set(5.0 / 60.0, 0.0);
}

// A parallel or meridian as a chain of geographic points. Legs are kept under
// 30 degrees so no single leg can be taken the wrong way round the earth by
// the projection's short-way longitude rule.
std::vector<GeoPoint> LinePoints(GridAxis axis, double value, double lo,
                                 double hi) {
  std::vector<GeoPoint> pts;
  constexpr double kMaxLegDeg = 30.0;
  const double span = hi - lo;
  int legs = static_cast<int>(std::ceil(span / kMaxLegDeg));
  if (legs < 1) legs = 1;
  pts.reserve(static_cast<size_t>(legs) + 1);
  for (int i = 0; i <= legs; ++i) {
    const double t = lo + span * (static_cast<double>(i) / legs);
    if (axis == GridAxis::kLatitude)
      pts.push_back({value, NormalizeLon(t)});   // a parallel: t is longitude
    else
      pts.push_back({t, NormalizeLon(value)});   // a meridian: t is latitude
  }
  return pts;
}

// The point at which a line crosses into the surface, scanning from `from`
// toward `to`. This is where the label goes -- FalconView kept the same point
// (GridLine::m_start_point) for the same purpose.
//
// SCANNING RATHER THAN SOLVING is what makes it correct under rotation: the
// entry edge of a turned viewport is whichever edge the line actually crosses,
// and no trigonometry here has to know which one that is. Swapping `from` and
// `to` gives the other end, which is the second candidate a label gets.
//
// The coarse scan finds the step that crossed; the bisection then puts the
// point within a pixel of the boundary, which matters because the ALIGNMENT is
// chosen from how close the anchor is to an edge.
bool EntryPoint(const MapProjection& proj, GridAxis axis, double value,
                double from, double to, SurfacePoint* out) {
  const PixelSize surf = proj.SurfaceSize();
  auto project = [&](double t, double* sx, double* sy) {
    const GeoPoint g = axis == GridAxis::kLatitude
                           ? GeoPoint{value, NormalizeLon(t)}
                           : GeoPoint{t, NormalizeLon(value)};
    return proj.GeoToSurface(g, sx, sy).ok();
  };
  auto inside = [&](double sx, double sy) {
    return sx >= 0.0 && sy >= 0.0 && sx < surf.width && sy < surf.height;
  };

  constexpr int kSamples = 96;
  double prev_t = from;
  bool have_prev = false;
  for (int i = 0; i <= kSamples; ++i) {
    const double t = from + (to - from) * (static_cast<double>(i) / kSamples);
    double sx = 0.0, sy = 0.0;
    if (!project(t, &sx, &sy)) continue;
    if (!inside(sx, sy)) {
      prev_t = t;
      have_prev = true;
      continue;
    }
    // Crossed between prev_t and t. Bisect toward the boundary; 12 halvings
    // of one sample step is well under a pixel at any surface size.
    if (have_prev) {
      double lo = prev_t, hi = t;
      for (int k = 0; k < 12; ++k) {
        const double mid = 0.5 * (lo + hi);
        double mx = 0.0, my = 0.0;
        if (project(mid, &mx, &my) && inside(mx, my))
          hi = mid;
        else
          lo = mid;
      }
      if (project(hi, &sx, &sy) && inside(sx, sy)) {
        out->x = sx;
        out->y = sy;
        return true;
      }
    }
    out->x = sx;
    out->y = sy;
    return true;
  }
  return false;
}

// Which way a label hangs off its anchor.
//
// THIS IS THE 200 LINES OF grid_map/label.cpp's set_and_adjust_anchor, and it
// is ten. The original enumerated rotation octants and screen edges in
// trigonometry, arriving at the same rule: a label sitting on an edge of the
// screen must hang INWARD from it, or half of it is off the map. Asking where
// the anchor landed answers that for every rotation at once, because the
// anchor already IS the rotated position.
//
// Off an edge entirely (a line entering through a corner the scan stepped
// past), the horizontal falls back to centred and the vertical to
// FalconView's own side rule: a latitude label hangs on the POLE side of its
// own line, so the labels either side of the equator do not collide.
LabelStyle EdgeAnchoredStyle(const LabelStyle& base, const SurfacePoint& at,
                             PixelSize surf, GridAxis axis, double value) {
  constexpr double kEdgePx = 6.0;
  constexpr int kNudge = 3;
  LabelStyle s = base;

  if (at.x <= kEdgePx) {
    s.halign = LabelHAlign::kLeft;
    s.dx = kNudge;
  } else if (at.x >= surf.width - 1 - kEdgePx) {
    s.halign = LabelHAlign::kRight;
    s.dx = -kNudge;
  } else {
    s.halign = LabelHAlign::kCenter;
    s.dx = 0;
  }

  if (at.y <= kEdgePx) {
    s.valign = LabelVAlign::kTop;
    s.dy = kNudge;
  } else if (at.y >= surf.height - 1 - kEdgePx) {
    s.valign = LabelVAlign::kBottom;
    s.dy = -kNudge;
  } else if (axis == GridAxis::kLatitude) {
    s.valign = value >= 0.0 ? LabelVAlign::kTop : LabelVAlign::kBottom;
    s.dy = value >= 0.0 ? 2 : -2;
  } else {
    s.valign = LabelVAlign::kTop;
    s.dy = 2;
  }
  return s;
}


// Tick marks along one major line, at one spacing.
//
// The DIRECTION is FalconView's, and it is not "perpendicular, either way":
// ticks on a parallel point AWAY FROM THE EQUATOR and ticks on a meridian
// point AWAY FROM THE PRIME MERIDIAN, with both sides drawn on the equator and
// on the prime meridian themselves. The original got that by nudging screen
// coordinates in the unrotated virtual surface; here the nudge is GEOGRAPHIC
// -- step a hair along the perpendicular coordinate, project both points, and
// use the screen delta -- which is the same picture on a north-up chart and
// the right one on a turned chart, with no angle arithmetic of its own.
Status DrawTicks(const MapProjection& proj, GeoDraw& gd, GridAxis line_axis,
                 double line_value, double lo, double hi, double spacing,
                 int length_px, const GeoLineStyle& style, int* count) {
  if (!(spacing > 0.0) || length_px <= 0) return Status::Ok();

  const PixelSize surf = proj.SurfaceSize();
  constexpr int kMaxTicks = 4096;
  constexpr double kNudgeDeg = 1e-3;  // the transform is linear: any step does
  const long long first = static_cast<long long>(std::ceil(lo / spacing - 1e-9));
  Status first_err = Status::Ok();

  for (int n = 0; n < kMaxTicks; ++n) {
    const double t = static_cast<double>(first + n) * spacing;
    if (t > hi + 1e-12) break;

    GeoPoint at, toward;
    bool both_sides = false;
    if (line_axis == GridAxis::kLatitude) {
      at = {line_value, NormalizeLon(t)};
      const double side = line_value >= 0.0 ? 1.0 : -1.0;
      toward = {line_value + side * kNudgeDeg, at.lon};
      both_sides = line_value == 0.0;
    } else {
      const double lon = NormalizeLon(line_value);
      at = {t, lon};
      const double side = lon >= 0.0 ? 1.0 : -1.0;
      toward = {t, NormalizeLon(lon + side * kNudgeDeg)};
      both_sides = lon == 0.0;
    }

    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    if (!proj.GeoToSurface(at, &ax, &ay).ok()) continue;
    // ~ map->geo_in_surface: a tick whose foot is off screen is not drawn, so
    // a line running off the edge does not sprout marks beyond it.
    if (ax < 0.0 || ay < 0.0 || ax >= surf.width || ay >= surf.height) continue;
    if (!proj.GeoToSurface(toward, &bx, &by).ok()) continue;

    double dx = bx - ax, dy = by - ay;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.0)) continue;
    dx = dx / len * length_px;
    dy = dy / len * length_px;

    std::vector<std::vector<SurfacePoint>> path(1);
    path[0].push_back({ax, ay});
    path[0].push_back({ax + dx, ay + dy});
    Status s = gd.DrawSurfacePath(path, style);
    if (!s.ok() && first_err.ok()) first_err = s;
    if (count != nullptr) ++*count;

    if (both_sides) {
      path[0][1] = SurfacePoint{ax - dx, ay - dy};
      s = gd.DrawSurfacePath(path, style);
      if (!s.ok() && first_err.ok()) first_err = s;
      if (count != nullptr) ++*count;
    }
  }
  return first_err;
}

}  // namespace

// ---------------------------------------------------------------------------
// The label text
// ---------------------------------------------------------------------------

std::string GraticuleLabelText(double degrees, GridAxis axis,
                               double minor_spacing_deg) {
  const bool is_lat = axis == GridAxis::kLatitude;
  char dir;
  if (degrees < 0.0) {
    degrees = -degrees;
    dir = is_lat ? 'S' : 'W';
  } else {
    dir = is_lat ? 'N' : 'E';
  }

  // The ladder, from the original's calculate_grid_line_format. The degree
  // field is two digits for latitude and three for longitude so that a column
  // of labels lines up.
  const char* deg_fmt = is_lat ? "%c %02d\xc2\xb0" : "%c %03d\xc2\xb0";
  char buf[48];

  const double kMinute = 1.0 / 60.0;
  const double kSecond = 1.0 / 3600.0;

  if (minor_spacing_deg >= 1.0) {
    const int d = static_cast<int>(degrees + 0.5);
    std::snprintf(buf, sizeof(buf), deg_fmt, dir, d);
    return buf;
  }

  int d = static_cast<int>(degrees);
  double rem_min = (degrees - d) * 60.0;

  if (minor_spacing_deg >= kMinute) {
    int m = static_cast<int>(rem_min + 0.5);
    if (m == 60) { m = 0; ++d; }
    std::snprintf(buf, sizeof(buf), is_lat ? "%c %02d\xc2\xb0 %02d'"
                                           : "%c %03d\xc2\xb0 %02d'",
                  dir, d, m);
    return buf;
  }

  int m = static_cast<int>(rem_min);
  double rem_sec = (rem_min - m) * 60.0;

  if (minor_spacing_deg >= kSecond) {
    int s = static_cast<int>(rem_sec + 0.5);
    if (s == 60) { s = 0; ++m; }
    if (m == 60) { m = 0; ++d; }
    std::snprintf(buf, sizeof(buf), is_lat ? "%c %02d\xc2\xb0 %02d' %02d\""
                                           : "%c %03d\xc2\xb0 %02d' %02d\"",
                  dir, d, m, s);
    return buf;
  }

  int s = static_cast<int>(rem_sec);
  int tenths = static_cast<int>((rem_sec - s) * 10.0 + 0.5);
  if (tenths == 10) { tenths = 0; ++s; }
  if (s == 60) { s = 0; ++m; }
  if (m == 60) { m = 0; ++d; }
  std::snprintf(buf, sizeof(buf), is_lat ? "%c %02d\xc2\xb0 %02d' %02d.%d\""
                                         : "%c %03d\xc2\xb0 %02d' %02d.%d\"",
                dir, d, m, s, tenths);
  return buf;
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

GridOverlay::GridOverlay() : Overlay("grid") {
  values_.reserve(Specs().size());
  for (const PropertySpec& s : Specs()) values_.push_back(s.default_value);
}

const std::vector<app::PropertySpec>& GridOverlay::Describe() const {
  return Specs();
}

Status GridOverlay::GetProperty(const std::string& key,
                                app::PropertyValue* out) const {
  const int i = IndexOf(key);
  if (i < 0) return Status::Error(kNotFound, "no grid property '" + key + "'");
  if (out != nullptr) *out = values_[static_cast<size_t>(i)];
  return Status::Ok();
}

Status GridOverlay::SetProperty(const std::string& key,
                                const app::PropertyValue& value) {
  const int i = IndexOf(key);
  if (i < 0) return Status::Error(kNotFound, "no grid property '" + key + "'");
  const PropertySpec& spec = Specs()[static_cast<size_t>(i)];
  if (value.type != spec.type)
    return Status::Error(kInvalidArg, "wrong type for grid property '" + key + "'");
  // The declared range is enforced here rather than trusted from the dialog:
  // a binding, a settings file and a test all reach this and none of them is
  // a spinner.
  if (spec.min != spec.max) {
    const double v = spec.type == PropertyType::kDouble
                         ? value.d
                         : static_cast<double>(value.i);
    if (v < spec.min || v > spec.max)
      return Status::Error(kInvalidArg,
                           "grid property '" + key + "' out of range");
  }
  values_[static_cast<size_t>(i)] = value;
  return Status::Ok();
}

void GridOverlay::SetColor(const FvColor& c) {
  values_[kLineColor] = app::PropertyValue::Color(c);
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

Status GridOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  stats_ = DrawStats{};
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");

  const double scale = ScaleDenominatorFor(proj);
  stats_.scale_denominator = scale;
  const GraticuleSpacing lat_sp =
      GraticuleSpacingFor(scale, GridAxis::kLatitude);
  const GraticuleSpacing lon_sp =
      GraticuleSpacingFor(scale, GridAxis::kLongitude);
  stats_.lat_spacing = lat_sp;
  stats_.lon_spacing = lon_sp;
  if (!lat_sp.has_lines() && !lon_sp.has_lines()) return Status::Ok();

  // The viewport, as a lat window and a longitude window unwrapped around the
  // centre so an antimeridian-crossing map is one interval and not two.
  const GeoRect b = proj.VmapBounds();
  const double south = b.ll.lat;
  const double north = b.ur.lat;
  double west = UnwrapLonNear(b.ll.lon, proj.Center().lon);
  double east = UnwrapLonNear(b.ur.lon, proj.Center().lon);
  if (east < west) east += 360.0;

  std::vector<GridLineDef> parallels, meridians;
  BuildLines(south, north, lat_sp, &parallels);
  BuildLines(west, east, lon_sp, &meridians);

  const bool show_minor = GetBool("show_minor_lines", true);
  const FvColor line_color = GetColor("line_color", FvColor{255, 255, 255, 96});
  const int line_width = static_cast<int>(GetInt("line_width", 1));

  GeoLineStyle stroke_only = SolidGeoLine(line_color, line_width);
  GeoLineStyle casing_only;
  if (GetBool("show_casing", true)) {
    GeoLineStyle both = stroke_only;
    AddCasing(&both, GetColor("casing_color", FvColor{0, 0, 0, 128}), 1);
    casing_only.stroke = both.casing;   // the casing pen, drawn as the stroke
  }

  GeoDraw gd(proj, &canvas);
  Status first = Status::Ok();
  auto note = [&first](const Status& s) {
    if (!s.ok() && first.ok()) first = s;
  };

  // Two passes over the same geometry, casings first. FalconView drew every
  // background line before any foreground one for this reason: a casing is
  // wider than the line it sits under, so a casing drawn later would nibble
  // the ink of a line already down -- visible at every crossing of a grid,
  // which is a picture made almost entirely of crossings.
  auto stroke_all = [&](const GeoLineStyle& style) {
    if (!style.stroke.valid && !style.pattern.valid) return;
    for (const GridLineDef& d : parallels) {
      if (!d.major && !show_minor) continue;
      note(gd.DrawGeoPolyline(LinePoints(GridAxis::kLatitude, d.value, west, east),
                              LineKind::kSimple, style));
    }
    for (const GridLineDef& d : meridians) {
      if (!d.major && !show_minor) continue;
      note(gd.DrawGeoPolyline(LinePoints(GridAxis::kLongitude, d.value, south, north),
                              LineKind::kSimple, style));
    }
  };
  stroke_all(casing_only);
  stroke_all(stroke_only);

  for (const GridLineDef& d : parallels) {
    if (d.major) ++stats_.major_lines;
    if (d.major || show_minor) ++stats_.parallels;
  }
  for (const GridLineDef& d : meridians) {
    if (d.major) ++stats_.major_lines;
    if (d.major || show_minor) ++stats_.meridians;
  }

  // --- ticks: on major lines only, at both tick spacings ---------------
  //
  // The spacing comes from the OTHER axis's row, because a tick steps along
  // the line it decorates: the marks on a parallel are spaced in longitude.
  if (GetBool("show_ticks", true)) {
    const int tick_px = static_cast<int>(GetInt("tick_length_px", 12));
    for (int pass = 0; pass < 2; ++pass) {
      const GeoLineStyle& style = pass == 0 ? casing_only : stroke_only;
      if (!style.stroke.valid) continue;
      const bool counting = pass == 1;
      for (const GridLineDef& d : parallels) {
        if (!d.major) continue;
        double maj = lon_sp.major_tick_deg, min = lon_sp.minor_tick_deg;
        AdjustPolarTickSpacing(scale, d.value, &maj, &min);
        note(DrawTicks(proj, gd, GridAxis::kLatitude, d.value, west, east,
                       min, tick_px / 2 + line_width, style,
                       counting ? &stats_.ticks : nullptr));
        note(DrawTicks(proj, gd, GridAxis::kLatitude, d.value, west, east,
                       maj, tick_px + line_width, style,
                       counting ? &stats_.ticks : nullptr));
      }
      for (const GridLineDef& d : meridians) {
        if (!d.major) continue;
        note(DrawTicks(proj, gd, GridAxis::kLongitude, d.value, south, north,
                       lat_sp.minor_tick_deg, tick_px / 2 + line_width, style,
                       counting ? &stats_.ticks : nullptr));
        note(DrawTicks(proj, gd, GridAxis::kLongitude, d.value, south, north,
                       lat_sp.major_tick_deg, tick_px + line_width, style,
                       counting ? &stats_.ticks : nullptr));
      }
    }
  }

  // --- labels, last, and latitude first ----------------------------------
  //
  // Call order IS priority in the placer (label_placer.h), and this is the
  // order FalconView's overlap test hard-coded: latitude labels were drawn
  // first into a rectangle array, and any longitude label intersecting one was
  // dropped. Here the rule is the placer's and the priority is just the order.
  if (GetBool("show_labels", true)) {
    LabelPlacer placer;
    placer.SetMargin(3);
    placer.SetPadding(2);

    LabelStyle ls;
    ls.valid = true;
    ls.style.font_path = GetString("label_font");
    ls.style.size = GetDouble("label_size_px", 12.0);
    ls.style.color = GetColor("label_color", FvColor{255, 255, 255, 255});
    ls.halo_width = GetDouble("label_halo_width_px", 1.0);
    ls.halo_color = GetColor("label_halo_color", FvColor{0, 0, 0, 200});

    const bool label_minor = GetBool("label_minor_lines", false);

    const PixelSize surf = proj.SurfaceSize();

    auto label_family = [&](GridAxis axis, const std::vector<GridLineDef>& lines,
                            const GraticuleSpacing& sp, double from, double to) {
      for (const GridLineDef& d : lines) {
        if (!d.major && !(show_minor && label_minor)) continue;

        // BOTH ends of the line, in preference order. The first is the edge
        // FalconView labelled; the second exists because a turned chart puts
        // some lines' entry points in a crowded corner, and a line labelled at
        // its far end is far better than a line not labelled at all. Measured
        // at 30 degrees of rotation: one label placed with the entry point
        // alone, four with both.
        SurfacePoint anchors[2];
        int n = 0;
        if (EntryPoint(proj, axis, d.value, from, to, &anchors[n])) ++n;
        if (EntryPoint(proj, axis, d.value, to, from, &anchors[n])) ++n;
        if (n == 0) continue;

        const std::string text =
            GraticuleLabelText(d.value, axis, sp.minor_line_deg);

        for (int i = 0; i < n; ++i) {
          const LabelStyle s =
              EdgeAnchoredStyle(ls, anchors[i], surf, axis, d.value);
          LabelInk ink;
          if (!placer.Place(canvas, anchors[i].x, anchors[i].y, text, s.style,
                            s, &ink))
            continue;
          // A label that will not draw -- most often a canvas with no font
          // configured -- is deliberately NOT propagated. The lines are the
          // graticule; the labels annotate it, and a chart with an unlabelled
          // grid on it is a far better outcome than an overlay that reports
          // failure and leaves the map bare.
          gd.DrawLabelAtPixel(anchors[i].x, anchors[i].y, text, s);
          break;
        }
      }
    };

    // The scan DIRECTION picks the edge a label lands on, and that is the
    // whole of what replaced the original's 200 lines of edge trigonometry.
    // Parallels are walked west to east, so their labels sit where each line
    // comes IN from the left; meridians are walked north to south, so theirs
    // sit at the top. Under rotation the "left" and "top" edges are whichever
    // edges the lines actually cross, and nothing here has to know which.
    label_family(GridAxis::kLatitude, parallels, lat_sp, west, east);
    label_family(GridAxis::kLongitude, meridians, lon_sp, north, south);

    stats_.labels_placed = static_cast<int>(placer.placed_count());
    stats_.labels_rejected = static_cast<int>(placer.rejected_count());
  }

  return first;
}

}  // namespace fv
