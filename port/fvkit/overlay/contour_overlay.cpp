// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The Contour Lines overlay — see fvkit/overlay/contour_overlay.h.

#include "fvkit/overlay/contour_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fvkit/canvas/geo_draw.h"
#include "fvkit/canvas/label_placer.h"
#include "fvkit/canvas/path_shaping.h"
#include "fvkit/geo/elevation_tiling.h"
#include "fvkit/scale_table.h"

namespace fv {
namespace {

using app::PropertySpec;
using app::PropertyType;
using app::PropertyValue;

enum Prop {
  kDisplayThreshold = 0,
  kMajorInterval,
  kIntervalUnit,
  kDivisions,
  kLineColor,
  kMajorWidthPx,
  kMinorWidthPx,
  kShowMinorLines,
  kLabelThreshold,
  kShowLabels,
  kLabelSizePx,
  kLabelColor,
  kLabelHaloColor,
  kLabelHaloWidthPx,
  kLabelFont,
  kSmoothing,
  kThinningPx,
  kMaxSamplesPerDraw,
  kPropCount
};

// Interval unit choices; the VALUE is the index (properties.h).
enum Unit { kFeet = 0, kMeters = 1 };
enum Smoothing { kNone = 0, kChaikin = 1, kSpline = 2 };
constexpr double kMetersPerFoot = 0.3048;

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
    v.push_back(Spec("display_threshold", "Show at 1:N or larger", "Data",
                     PropertyValue::Double(250000.0), 1000.0, 50000000.0,
                     "Contours are not drawn on a map smaller in scale than "
                     "this. FalconView's default is 1:250 K and the reason is "
                     "legibility, not cost: at 1:5 M a contour set is a "
                     "smear."));
    v.push_back(Spec("major_interval", "Major interval", "Interval",
                     PropertyValue::Double(1000.0), 1.0, 30000.0,
                     "In whichever unit `interval_unit` names. The default is "
                     "1000 feet, as in FalconView."));
    PropertySpec unit = Spec("interval_unit", "Interval unit", "Interval",
                             PropertyValue::Choice(kFeet));
    unit.type = PropertyType::kChoice;
    unit.choices = {"feet", "meters"};
    unit.help =
        "What `major_interval` is written in, and what a contour label says. "
        "The data is metres either way.";
    v.push_back(unit);
    v.push_back(Spec("divisions", "Minor lines per major", "Interval",
                     PropertyValue::Int(5), 1, 10,
                     "The minor interval is the major one divided by this, so "
                     "1000 feet in 5 divisions traces every 200 feet and "
                     "draws every fifth line heavy."));
    v.push_back(Spec("line_color", "Line colour", "Lines",
                     PropertyValue::Color(FvColor{192, 0, 64, 255}),
                     0, 0, "FalconView's RGB(192, 0, 64)."));
    v.push_back(Spec("major_width_px", "Major line width", "Lines",
                     PropertyValue::Int(2), 1, 8, "pixels"));
    v.push_back(Spec("minor_width_px", "Minor line width", "Lines",
                     PropertyValue::Int(1), 1, 8, "pixels"));
    v.push_back(Spec("show_minor_lines", "Show minor lines", "Lines",
                     PropertyValue::Bool(true)));
    v.push_back(Spec("label_threshold", "Label at 1:N or larger", "Labels",
                     PropertyValue::Double(250000.0), 1000.0, 50000000.0));
    v.push_back(Spec("show_labels", "Show labels", "Labels",
                     PropertyValue::Bool(false),
                     0, 0,
                     "Major lines only, one label per line, with the line "
                     "broken for the text. Off by default, as in FalconView."));
    v.push_back(Spec("label_size_px", "Label size", "Labels",
                     PropertyValue::Double(11.0), 6.0, 48.0, "pixels"));
    v.push_back(Spec("label_color", "Label colour", "Labels",
                     PropertyValue::Color(FvColor{255, 255, 255, 255})));
    v.push_back(Spec("label_halo_color", "Label outline", "Labels",
                     PropertyValue::Color(FvColor{0, 0, 0, 200})));
    v.push_back(Spec("label_halo_width_px", "Label outline width", "Labels",
                     PropertyValue::Double(1.0), 0.0, 4.0,
                     "pixels; 0 = none"));
    v.push_back(Spec("label_font", "Label font", "Labels",
                     PropertyValue::String(std::string()),
                     0, 0, "Path to a TTF/TTC file; empty = the canvas's own."));
    PropertySpec smooth = Spec("smoothing", "Smoothing", "Lines",
                               PropertyValue::Choice(kChaikin));
    smooth.type = PropertyType::kChoice;
    smooth.choices = {"none", "chaikin", "spline"};
    smooth.help =
        "A contour traced from elevation posts is a staircase of straight "
        "legs between them, and on a large-scale map the posts are tens of "
        "pixels apart, so the staircase shows. 'chaikin' rounds the corners "
        "and cannot overshoot; 'spline' (centripetal Catmull-Rom) passes "
        "through every traced crossing exactly; 'none' is FalconView's own "
        "geometry, vertex for vertex.";
    v.push_back(smooth);
    v.push_back(Spec("thinning_px", "Vertex thinning", "Lines",
                     PropertyValue::Double(0.5), 0.0, 4.0,
                     "Douglas-Peucker tolerance in pixels, applied before "
                     "smoothing. Zoomed out, most of a traced contour's "
                     "vertices are within half a pixel of each other and cost "
                     "ink nobody can see. 0 = keep every vertex. This is what "
                     "FalconView's ThinningLevel was for; over there it was "
                     "stored, clamped, saved to the registry and never read."));
    v.push_back(Spec("max_samples_per_draw", "Sample budget", "Data",
                     PropertyValue::Int(1000000), 10000, 40000000,
                     "A safety valve, not a tuning knob: elevation posts read "
                     "in one draw. Tiles beyond it are left untraced and are "
                     "picked up by the next draw, so a mistyped interval or a "
                     "vast viewport costs a slow frame rather than a hung "
                     "one."));
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

// The label a contour carries: its elevation, in the display unit, with no
// unit suffix — a contour map says "1200", not "1200 ft", because every line
// on it is in the same unit and the map has a legend.
std::string ContourLabelText(double level_m, int unit) {
  const double v = unit == kFeet ? level_m / kMetersPerFoot : level_m;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld",
                static_cast<long long>(std::llround(v)));
  return buf;
}

}  // namespace

const char ContourOverlay::kTypeId[] = "fv.contour";

ContourOverlay::ContourOverlay() : Overlay("contour") {
  values_.reserve(Specs().size());
  for (const PropertySpec& s : Specs()) values_.push_back(s.default_value);
}

void ContourOverlay::SetElevationSource(std::shared_ptr<IElevationSource> src) {
  source_ = std::move(src);
  ClearCache();
}

void ContourOverlay::ClearCache() {
  tiles_.clear();
  tiling_.Reset();
}

const std::vector<app::PropertySpec>& ContourOverlay::Describe() const {
  return Specs();
}

Status ContourOverlay::GetProperty(const std::string& key,
                                   app::PropertyValue* out) const {
  const int i = IndexOf(key);
  if (i < 0)
    return Status::Error(kNotFound, "no contour property '" + key + "'");
  if (out != nullptr) *out = values_[static_cast<size_t>(i)];
  return Status::Ok();
}

Status ContourOverlay::SetProperty(const std::string& key,
                                   const app::PropertyValue& value) {
  const int i = IndexOf(key);
  if (i < 0)
    return Status::Error(kNotFound, "no contour property '" + key + "'");
  const PropertySpec& spec = Specs()[static_cast<size_t>(i)];
  if (value.type != spec.type)
    return Status::Error(kInvalidArg,
                         "wrong type for contour property '" + key + "'");
  if (spec.min != spec.max) {
    const double v = spec.type == PropertyType::kDouble
                         ? value.d
                         : static_cast<double>(value.i);
    if (v < spec.min || v > spec.max)
      return Status::Error(kInvalidArg,
                           "contour property '" + key + "' out of range");
  }
  if (spec.type == PropertyType::kChoice &&
      (value.i < 0 ||
       value.i >= static_cast<long long>(spec.choices.size())))
    return Status::Error(kInvalidArg,
                         "contour property '" + key + "' has no such choice");
  values_[static_cast<size_t>(i)] = value;
  // The traced geometry depends on three of these and on nothing else. The
  // cache is dropped by the DRAW when the interval no longer matches what was
  // traced (traced_interval_m_), so a shell that sets major_interval and
  // divisions in either order re-traces once, not twice.
  return Status::Ok();
}

double ContourOverlay::MajorIntervalMeters() const {
  const double v = GetDouble("major_interval", 1000.0);
  return GetInt("interval_unit", kFeet) == kFeet ? v * kMetersPerFoot : v;
}

double ContourOverlay::IntervalMeters() const {
  const long long div = GetInt("divisions", 5);
  if (div < 1) return MajorIntervalMeters();
  return MajorIntervalMeters() / static_cast<double>(div);
}

// ---------------------------------------------------------------------------
// Shaping the projected line (plan C5)
// ---------------------------------------------------------------------------
//
// WHY THIS IS IN SURFACE SPACE AND NOT IN THE CACHE. A contour traced from
// elevation posts is a chain of straight legs between crossings on the post
// lattice, and at a large scale those posts are tens of pixels apart -- so the
// staircase is visible, which is Chris's complaint about the Windows overlay
// (2026-08-29) and the thing FalconView never addressed at all.
//
// "Blocky" is a statement about PIXELS, so the pixel is where the error bound
// belongs. Three things follow, and together they are the reason this runs on
// the projected path rather than on the traced geometry:
//
//   * zoomed out there is almost nothing on screen to smooth, so the cost
//     scales with what is visible rather than with what was traced;
//   * changing the setting -- or the zoom, or the rotation -- re-shapes
//     without re-tracing a single elevation post;
//   * the decimation below can be stated as half a pixel, which is the only
//     unit in which "this vertex does not change the picture" is true.

namespace {

// Project, thin, smooth -- in that order, and the order is the point. Thinning
// first means the smoother is not handed a thousand sub-pixel vertices to
// round the corners between; smoothing second means the curve is fitted to
// the vertices that actually carry the shape.
std::vector<SurfacePoint> ProjectLine(const MapProjection& proj,
                                      const ContourLine& line, int smoothing,
                                      double thinning_px) {
  std::vector<SurfacePoint> pts;
  pts.reserve(line.points.size());
  for (const GeoPoint& p : line.points) {
    double x = 0, y = 0;
    proj.GeoToSurface(p, &x, &y);
    pts.push_back(SurfacePoint{x, y});
  }
  if (thinning_px > 0.0) pts = DecimatePath(pts, thinning_px);
  // Four pixels: below that a corner is already round to the eye, and every
  // further iteration doubles the vertex count for nothing.
  if (smoothing == kChaikin)
    pts = SmoothPathChaikin(std::move(pts), line.closed, 3, 4.0);
  else if (smoothing == kSpline)
    pts = SmoothPathCatmullRom(pts, line.closed, 4.0);
  return pts;
}

}  // namespace

// ---------------------------------------------------------------------------
// Sampling and tiles
// ---------------------------------------------------------------------------

bool ContourOverlay::AdoptSampling(const MapProjection& proj) {
  bool invalidated = false;
  if (!tiling_.Adopt(proj, source_.get(), &invalidated)) return false;
  if (invalidated) tiles_.clear();
  return true;
}

const ContourOverlay::Tile* ContourOverlay::TileAt(TileIndex index,
                                                   long long* budget) {
  const int64_t key = ElevationTiling::Key(index);
  auto it = tiles_.find(key);
  if (it != tiles_.end()) return &it->second;
  if (!tiling_.Valid(index)) return nullptr;

  // Posts INCLUDING both edges, so adjacent tiles share the posts on the edge
  // between them and their contours meet there. (FalconView asked its DTED
  // server for one extra post north and east for the same reason.)
  const int nx = tiling_.PostsX();
  const int ny = tiling_.PostsY();
  const long long cost = static_cast<long long>(nx) * ny;
  if (cost > *budget) {
    ++stats_.tiles_over_budget;
    return nullptr;
  }

  Tile tile;
  tile.bounds = tiling_.BoundsOf(index);
  ElevationGrid grid;
  if (!SampleElevationGrid(*source_, tile.bounds, nx, ny, &grid).ok())
    return nullptr;
  *budget -= cost;
  stats_.samples += cost;
  ++stats_.tiles_traced;
  tile.lines = TraceElevationContours(grid, traced_interval_m_);
  auto ins = tiles_.emplace(key, std::move(tile));
  return &ins.first->second;
}

void ContourOverlay::EvictOutside(const GeoRect& keep) {
  for (auto it = tiles_.begin(); it != tiles_.end();) {
    if (it->second.bounds.Intersects(keep))
      ++it;
    else
      it = tiles_.erase(it);
  }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

Status ContourOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  stats_ = DrawStats{};
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");
  if (source_ == nullptr) {
    stats_.no_source = true;
    return Status::Ok();
  }

  const double scale = ScaleDenominatorFor(proj);
  stats_.scale_denominator = scale;
  if (scale > GetDouble("display_threshold", 250000.0)) {
    stats_.below_threshold = true;
    return Status::Ok();
  }

  const double interval = IntervalMeters();
  stats_.interval_m = interval;
  if (!(interval > 0.0)) return Status::Ok();
  if (interval != traced_interval_m_) {
    tiles_.clear();
    traced_interval_m_ = interval;
  }
  if (!AdoptSampling(proj)) return Status::Ok();
  stats_.sample_lat_deg = tiling_.sample_lat();
  stats_.sample_lon_deg = tiling_.sample_lon();
  stats_.tile_deg = tiling_.tile_deg();

  long long budget = GetInt("max_samples_per_draw", 1000000);

  const long long divisions = std::max<long long>(1, GetInt("divisions", 5));
  const bool show_minor = GetBool("show_minor_lines", true);
  const FvColor color = GetColor("line_color", FvColor{192, 0, 64, 255});
  const GeoLineStyle minor_style = SolidGeoLine(
      color, static_cast<int>(GetInt("minor_width_px", 1)));
  const GeoLineStyle major_style = SolidGeoLine(
      color, static_cast<int>(GetInt("major_width_px", 2)));

  const bool labels = GetBool("show_labels", false) &&
                      scale <= GetDouble("label_threshold", 250000.0);

  // Shaping (C5). With neither smoothing nor thinning asked for, a line goes
  // to the seam as geography and is clipped and projected by BuildGeoPath the
  // way every other overlay's line is -- FalconView's geometry, vertex for
  // vertex. Ask for either and the overlay projects the line itself, because
  // both are answers to questions about pixels.
  const int smoothing = static_cast<int>(GetInt("smoothing", kChaikin));
  const double thinning_px = GetDouble("thinning_px", 0.5);
  const bool shaping = smoothing != kNone || thinning_px > 0.0;

  GeoDraw gd(proj, &canvas);
  LabelPlacer placer;
  placer.SetMargin(3);
  Status first = Status::Ok();
  auto note = [&first](const Status& s) {
    if (!s.ok() && first.ok()) first = s;
  };

  LabelStyle label_style;
  if (labels) {
    label_style.valid = true;
    label_style.style.color = GetColor("label_color", FvColor{255, 255, 255, 255});
    label_style.style.size = GetDouble("label_size_px", 11.0);
    label_style.style.font_path = GetString("label_font");
    label_style.halo_width = GetDouble("label_halo_width_px", 1.0);
    label_style.halo_color = GetColor("label_halo_color", FvColor{0, 0, 0, 200});
    label_style.placement = LabelPlacement::kAlongPath;
    label_style.along_anchor = LabelAlongAnchor::kCenter;
  }

  // The lattice cells the viewport touches. A tile is 0.05 to 1 degree, so
  // this is tens of cells at the display threshold and single figures when
  // zoomed in.
  std::vector<const Tile*> visible;
  for (TileIndex index : tiling_.VisibleCells(proj)) {
    ++stats_.tiles_considered;
    const Tile* t = TileAt(index, &budget);
    if (t != nullptr) visible.push_back(t);
  }

  // Minor lines first, then major over them: a major line is wider, and drawn
  // second it is not nibbled at every crossing by the thinner lines beside it
  // (the same ordering rule the graticule's casings follow).
  for (int pass = 0; pass < 2; ++pass) {
    const bool major_pass = pass == 1;
    for (const Tile* t : visible) {
      for (const ContourLine& line : t->lines) {
        const bool major = (line.level_index % divisions) == 0;
        if (major != major_pass) continue;
        if (!major && !show_minor) continue;
        ++stats_.lines_drawn;
        if (major) ++stats_.major_lines;
        stats_.vertices += static_cast<int>(line.points.size());
        if (major && labels &&
            DrawLabelledContour(proj, gd, canvas, placer, line, major_style,
                                label_style))
          continue;
        if (shaping) {
          std::vector<std::vector<SurfacePoint>> path(1);
          path[0] = ProjectLine(proj, line, smoothing, thinning_px);
          stats_.shaped_vertices += static_cast<int>(path[0].size());
          if (path[0].size() >= 2)
            note(gd.DrawSurfacePath(path, major ? major_style : minor_style));
        } else {
          stats_.shaped_vertices += static_cast<int>(line.points.size());
          note(gd.DrawGeoPolyline(line.points, LineKind::kSimple,
                                  major ? major_style : minor_style));
        }
      }
    }
  }

  EvictOutside(tiling_.KeepRect(proj));
  return first;
}


// ---------------------------------------------------------------------------
// Labels (plan C3)
// ---------------------------------------------------------------------------
//
// FalconView's rule, kept: ONE label per line, on major lines only, near the
// middle, with the line BROKEN for the text. The break is the part that
// matters -- a number sitting on top of its own contour is unreadable, and it
// is why draw_and_label_contour walks the line rather than calling a text
// routine at a point.
//
// Two things it did not do and this does. It took the vertex at
// `count / 2` -- a COUNT, not a length, so on a line whose vertices bunch up
// the label landed nowhere near the middle; the anchor here is half the
// projected LENGTH. And it never asked whether the text would land on another
// label, which at a tile boundary stacks two copies of the same number on top
// of each other; a LabelPlacer answers that.

namespace {

double PathLength(const std::vector<SurfacePoint>& pts, std::vector<double>* cum) {
  cum->assign(pts.size(), 0.0);
  double total = 0.0;
  for (size_t i = 1; i < pts.size(); ++i) {
    total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    (*cum)[i] = total;
  }
  return total;
}

// The point at arc length `s`, and the index of the vertex before it.
SurfacePoint PointAt(const std::vector<SurfacePoint>& pts,
                     const std::vector<double>& cum, double s, size_t* before) {
  size_t i = 1;
  while (i + 1 < pts.size() && cum[i] < s) ++i;
  *before = i - 1;
  const double seg = cum[i] - cum[i - 1];
  const double t = seg > 0.0 ? (s - cum[i - 1]) / seg : 0.0;
  return SurfacePoint{pts[i - 1].x + t * (pts[i].x - pts[i - 1].x),
                      pts[i - 1].y + t * (pts[i].y - pts[i - 1].y)};
}

// Is the stretch between two arc lengths straight enough to lay text on?
//
// MEASURED AS DEVIATION FROM THE CHORD, not as an angle between segments. A
// contour traced off elevation posts is jagged at the vertex scale even when
// it runs dead straight over the length of a four-digit number -- the
// staircase C5 exists to hide -- so a segment-to-segment angle test rejects
// almost every line on a real DTED tile, which is exactly what it did here
// before this was measured (2 labels on 109 major lines). What the text
// actually needs is that the LINE does not wander out from under it.
bool SpanIsStraight(const std::vector<SurfacePoint>& pts,
                    const std::vector<double>& cum, double s0, double s1,
                    const SurfacePoint& p0, const SurfacePoint& p1,
                    double max_dev_px) {
  const double dx = p1.x - p0.x, dy = p1.y - p0.y;
  const double len = std::hypot(dx, dy);
  if (len <= 0.0) return false;
  for (size_t i = 0; i < pts.size(); ++i) {
    if (cum[i] <= s0) continue;
    if (cum[i] >= s1) break;
    const double cross = (pts[i].x - p0.x) * dy - (pts[i].y - p0.y) * dx;
    if (std::fabs(cross) / len > max_dev_px) return false;
  }
  return true;
}

}  // namespace

bool ContourOverlay::DrawLabelledContour(const MapProjection& proj, GeoDraw& gd,
                                         ICanvas& canvas, LabelPlacer& placer,
                                         const ContourLine& line,
                                         const GeoLineStyle& style,
                                         const LabelStyle& label_style) {
  if (line.points.size() < 2) return false;

  // The SAME shaped path the plain draw would have used: a label sits on the
  // line the user can see, so the break has to be cut out of that line and not
  // out of the unsmoothed one under it.
  std::vector<SurfacePoint> pts =
      ProjectLine(proj, line, static_cast<int>(GetInt("smoothing", kChaikin)),
                  GetDouble("thinning_px", 0.5));
  if (pts.size() < 2) return false;
  std::vector<double> cum;
  const double total = PathLength(pts, &cum);
  if (!(total > 0.0)) return false;

  // THE LABEL GOES ON THE PART THE USER CAN SEE. A traced tile is bigger than
  // the viewport by design (that is what makes a pan free), so most of a
  // contour is off screen and its midpoint usually is too -- FalconView put
  // the label at the middle VERTEX whatever was on screen, and measured here
  // that put 2 labels on 109 major lines. So the anchors below are fractions
  // of the longest run of the line that is actually inside the canvas, and a
  // line with nothing on screen is not a label failure at all.
  const PixelSize canvas_size = canvas.Size();
  size_t run_lo = 0, run_hi = 0, cur_lo = 0;
  bool in_run = false;
  for (size_t i = 0; i < pts.size(); ++i) {
    const bool inside = pts[i].x >= 0.0 && pts[i].y >= 0.0 &&
                        pts[i].x <= canvas_size.width - 1 &&
                        pts[i].y <= canvas_size.height - 1;
    if (!inside) {
      in_run = false;
      continue;
    }
    if (!in_run) {
      cur_lo = i;
      in_run = true;
    }
    if (i - cur_lo > run_hi - run_lo) {
      run_lo = cur_lo;
      run_hi = i;
    }
  }
  if (run_hi <= run_lo) {
    ++stats_.labels_offscreen;
    return false;  // nothing of this line is on screen
  }
  const double vis_lo = cum[run_lo], vis_hi = cum[run_hi];

  const std::string text =
      ContourLabelText(line.level_m, static_cast<int>(GetInt("interval_unit", kFeet)));

  // Measured as an ordinary point label, which is what the placer reserves.
  // A rotated label's true footprint is a turned box and this is its upright
  // one -- an approximation, and a deliberate one: it is cheap, it is never
  // smaller than the glyph run's length, and the alternative is a rotated
  // overlap test for a label that is one to four digits long.
  LabelStyle upright = label_style;
  upright.placement = LabelPlacement::kPoint;
  upright.halign = LabelHAlign::kCenter;
  upright.valign = LabelVAlign::kCenter;

  // The break has a little air on each side of the text, so the line does not
  // touch the digits.
  const double pad = std::max(2.0, label_style.style.size * 0.25);

  // Halfway first, then outwards. A contour's middle is as good a place as
  // any and is where FalconView put it, but a jagged stretch or another
  // label's box has to have somewhere to go, and walking outwards keeps the
  // label away from the ends where two tiles' lines meet.
  static const double kAnchors[] = {0.5, 0.35, 0.65, 0.2, 0.8};
  for (double frac : kAnchors) {
    size_t before = 0;
    const double at = vis_lo + (vis_hi - vis_lo) * frac;
    const SurfacePoint anchor = PointAt(pts, cum, at, &before);
    LabelInk ink =
        MeasureLabelInk(canvas, anchor.x, anchor.y, text, upright.style, upright);
    const double width = ink.measured ? ink.box.width : label_style.style.size * 2.0;
    const double span = width + 2.0 * pad;
    const double s0 = at - span / 2.0;
    const double s1 = at + span / 2.0;
    // The break has to fit on the visible run, and inside the line itself.
    if (s0 <= vis_lo || s1 >= vis_hi || s0 <= 0.0 || s1 >= total) continue;

    // Refuse a break across a bend: the digits would fan out and the line
    // would wander out from under its own label.
    size_t i0 = 0, i1 = 0;
    const SurfacePoint p0 = PointAt(pts, cum, s0, &i0);
    const SurfacePoint p1 = PointAt(pts, cum, s1, &i1);
    const double height = ink.measured ? ink.box.height : label_style.style.size;
    if (!SpanIsStraight(pts, cum, s0, s1, p0, p1, height * 0.5)) continue;

    if (!placer.Place(canvas, anchor.x, anchor.y, text, upright.style, upright,
                      &ink)) {
      ++stats_.labels_rejected;
      continue;
    }

    std::vector<std::vector<SurfacePoint>> broken(2);
    broken[0].assign(pts.begin(), pts.begin() + static_cast<long>(i0) + 1);
    broken[0].push_back(p0);
    broken[1].push_back(p1);
    broken[1].insert(broken[1].end(), pts.begin() + static_cast<long>(i1) + 1,
                     pts.end());
    gd.DrawSurfacePath(broken, style);

    std::vector<std::vector<SurfacePoint>> run(1);
    run[0].push_back(p0);
    for (size_t i = i0 + 1; i <= i1 && i < pts.size(); ++i) run[0].push_back(pts[i]);
    run[0].push_back(p1);
    gd.DrawLabelAlongPath(run, text, label_style);
    ++stats_.labels_placed;
    return true;
  }
  ++stats_.labels_rejected;
  return false;
}

}  // namespace fv
