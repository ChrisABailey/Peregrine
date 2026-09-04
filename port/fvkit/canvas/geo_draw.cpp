// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/canvas/geo_draw.h"

#include "fvkit/canvas/label_placer.h"

#include <algorithm>
#include <cmath>

#include "fvkit/symbol/builtin.h"
#include "fvkit/vector/renderer.h"
#include "fvkit/vector/symbol_draw.h"
#include "fvkit/vector/text_draw.h"

namespace fv {

namespace {

const double kPi = 3.14159265358979323846;

// Smallest hit box a stamped symbol gets, whatever it actually inked — the
// same number and the same reason as VectorRenderer's: a 2-px marker is still
// something a user aims at.
constexpr int kMinPickBox = 9;

Pen WidenedPen(const Pen& base, int extra_each_side) {
  Pen p = base;
  p.width = std::max(1, base.width + 2 * std::max(0, extra_each_side));
  return p;
}

// Diverts one pass's bookkeeping (G4). A highlight is ink UNDER a feature and
// not ink OF it: it must not enter the pick index, because the user aims at
// the waypoint and not at its glow, and it must not count as a draw the caller
// asked for. Scoped so an early return out of the pass cannot leave picking
// switched off.
class AsHighlightPass {
 public:
  AsHighlightPass(bool* pick, size_t* emitted, size_t* highlight)
      : pick_(pick),
        emitted_(emitted),
        highlight_(highlight),
        pick_was_(*pick),
        emitted_was_(*emitted) {
    *pick_ = false;
  }
  ~AsHighlightPass() {
    *pick_ = pick_was_;
    *highlight_ += *emitted_ - emitted_was_;
    *emitted_ = emitted_was_;
  }
  AsHighlightPass(const AsHighlightPass&) = delete;
  AsHighlightPass& operator=(const AsHighlightPass&) = delete;

 private:
  bool* pick_;
  size_t* emitted_;
  size_t* highlight_;
  bool pick_was_;
  size_t emitted_was_;
};

}  // namespace

// ---------------------------------------------------------------------------
// Styles and presets
// ---------------------------------------------------------------------------

namespace line_preset {

const char* const kAll[] = {kSolid, kDash,     kLongDash, kDot,   kDashDot,
                            kRailroad, kArrow, kTick,     kNotch, kFeba,
                            nullptr};

}  // namespace line_preset

GeoLineStyle SolidGeoLine(FvColor color, int width_px) {
  GeoLineStyle s;
  s.stroke.valid = true;
  s.stroke.pen.color = color;
  s.stroke.pen.width = std::max(1, width_px);
  return s;
}

GeoLineStyle PresetGeoLine(const std::string& preset, FvColor color,
                           int width_px) {
  GeoLineStyle s = SolidGeoLine(color, width_px);
  s.pattern = MakeLinePreset(preset, s.stroke.pen);
  // A patterned line's plain stroke would draw the solid line the pattern
  // exists to replace, so the pattern REPLACES it rather than sitting over it.
  // (VectorRenderer keeps both because S-52 legitimately puts an LC over an LS
  // casing; here the casing slot is where that belongs and is explicit.)
  if (s.pattern.valid) s.stroke.valid = false;
  return s;
}

void AddCasing(GeoLineStyle* style, FvColor color, int extra_px) {
  if (style == nullptr) return;
  const Pen& base =
      style->pattern.valid ? style->pattern.pen : style->stroke.pen;
  style->casing.valid = true;
  style->casing.pen = WidenedPen(base, extra_px);
  style->casing.pen.color = color;
}

LinePatternStyle MakeLinePreset(const std::string& name, const Pen& pen,
                                double scale) {
  LinePatternStyle s;
  s.pen = pen;
  if (scale <= 0.0) scale = 1.0;

  auto dash = [&](double len) {
    PathRun r;
    r.type = PathRunType::kDash;
    r.length = len * scale;
    s.runs.push_back(r);
  };
  auto gap = [&](double len) {
    PathRun r;
    r.type = PathRunType::kGap;
    r.length = len * scale;
    s.runs.push_back(r);
  };
  // A stamp advances nothing of its own: the dash run before it carries the
  // period, so a preset's spacing reads off one number instead of two that
  // have to agree.
  auto stamp = [&](const char* id, double offset = 0.0, double rot = 0.0) {
    PathRun r;
    r.type = PathRunType::kSymbol;
    r.symbol_id = id;
    r.symbol_scale = scale;
    r.offset = offset * scale;
    r.rotation_deg = rot;
    s.runs.push_back(r);
  };

  namespace bs = builtin_symbol;
  if (name == line_preset::kDash) {
    dash(8); gap(6);
  } else if (name == line_preset::kLongDash) {
    dash(18); gap(8);
  } else if (name == line_preset::kDot) {
    // A dot is one pixel of dash, not a stamp: a stamped dot would be a symbol
    // lookup per pixel of line for something the pen already draws.
    dash(1); gap(4);
  } else if (name == line_preset::kDashDot) {
    dash(12); gap(5); dash(1); gap(5);
  } else if (name == line_preset::kRailroad) {
    dash(10); stamp(bs::kCrosstie);
  } else if (name == line_preset::kArrow) {
    dash(28); stamp(bs::kArrowhead);
  } else if (name == line_preset::kTick) {
    dash(14); stamp(bs::kTick);
  } else if (name == line_preset::kNotch) {
    // Half-tick to the LEFT of travel — the sense PathRun::offset is stated
    // in, and the sense the builtin notch is authored in.
    dash(12); stamp(bs::kNotch);
  } else if (name == line_preset::kFeba) {
    dash(20); stamp(bs::kTee);
  } else {
    return s;  // solid, or a name this build does not know: invalid = plain pen
  }
  s.valid = true;
  return s;
}

// ---------------------------------------------------------------------------
// GeoDraw
// ---------------------------------------------------------------------------

GeoDraw::GeoDraw(const MapProjection& proj, ICanvas* canvas,
                 ISymbolLibrary* symbols)
    : proj_(proj), canvas_(canvas), symbols_(symbols) {
  feature_.layer = 0;  // an overlay's own ink: one notional layer
}

void GeoDraw::SetHighlight(FvColor color, double width_px) {
  highlight_color_ = color;
  highlight_px_ = width_px > 0.0 ? width_px : 1.0;
}

void GeoDraw::SetFeature(int32_t id, int priority) {
  FeatureRef r;
  r.layer = 0;
  r.feature = id;
  SetFeature(r, priority);
}

void GeoDraw::SetFeature(const FeatureRef& ref, int priority) {
  feature_ = ref;
  priority_ = priority;
}

double GeoDraw::PxPerHimetric() const {
  double units = symbols_ != nullptr ? symbols_->himetric_per_symbol_pixel()
                                     : kHimetricPerHundredthInch;
  if (!(units > 0.0)) units = kHimetricPerHundredthInch;
  return symbol_scale_ * dpi_scale_ / units;
}

// --- lines -----------------------------------------------------------------

Status GeoDraw::StrokePaths(
    const std::vector<std::vector<SurfacePoint>>& paths,
    const StrokeStyle& stroke) {
  if (!stroke.valid) return Status::Ok();
  const PixelSize size = canvas_->Size();
  const double half = std::max(0.5, stroke.pen.width / 2.0);
  // A dashed pen is laid down by the placer, against the unclipped path, for
  // the reason PatternPaths already is: the rasterizer starts its cycle at the
  // first point it is given, so a clipped line would re-phase its dashes on
  // every pan. Measured from the path's first vertex instead, a casing and the
  // line it cases produce the same dash pieces and register exactly.
  const std::vector<PathRun> dash = DashRuns(stroke.pen.dash);
  Pen pen = stroke.pen;
  pen.dash.clear();
  Status first = Status::Ok();
  for (const std::vector<SurfacePoint>& sub : paths) {
    if (!dash.empty() && sub.size() >= 2) {
      for (const std::vector<SurfacePoint>& d :
           PlaceAlongPath(sub, dash, stroke.dash_phase).dashes) {
        for (auto& run : ClipPolyline(d, size.width, size.height)) {
          Status s = canvas_->DrawLines(run, pen);
          if (!s.ok() && first.ok()) first = s;
          ++draws_emitted_;
        }
      }
      // Picked along the whole line: a tap in a gap is still a tap on it.
      if (pick_enabled_) {
        for (auto& run : ClipPolyline(sub, size.width, size.height))
          pick_.AddStroke(feature_, priority_, run, half);
      }
      continue;
    }
    for (auto& run : ClipPolyline(sub, size.width, size.height)) {
      Status s = canvas_->DrawLines(run, pen);
      if (!s.ok() && first.ok()) first = s;
      ++draws_emitted_;
      if (pick_enabled_) pick_.AddStroke(feature_, priority_, run, half);
    }
  }
  return first;
}

Status GeoDraw::PatternPaths(
    const std::vector<std::vector<SurfacePoint>>& paths,
    const LinePatternStyle& pattern, const Pen* pen_override,
    double width_override, const FvColor* stamp_tint) {
  if (!pattern.valid || pattern.runs.empty()) return Status::Ok();
  const PixelSize size = canvas_->Size();
  const Pen& pen = pen_override != nullptr ? *pen_override : pattern.pen;
  const double half = std::max(0.5, pen.width / 2.0);
  // The casing pass stamps the SAME symbols one size up, which is what makes a
  // railroad's crossties keep their outline; width_override carries that as a
  // multiplier on the symbol scale rather than a second pattern to build.
  const double sym_scale = width_override > 0.0 ? width_override : 1.0;
  const double px_per_himetric = PxPerHimetric();

  Status first = Status::Ok();
  for (const std::vector<SurfacePoint>& sub : paths) {
    if (sub.size() < 2) continue;
    PathPlacement placed = PlaceAlongPath(sub, pattern.runs, pattern.phase);
    for (const std::vector<SurfacePoint>& d : placed.dashes) {
      for (auto& run : ClipPolyline(d, size.width, size.height)) {
        Status s = canvas_->DrawLines(run, pen);
        if (!s.ok() && first.ok()) first = s;
        ++draws_emitted_;
        if (pick_enabled_) pick_.AddStroke(feature_, priority_, run, half);
      }
    }
    if (symbols_ == nullptr) continue;
    for (const PlacedSymbol& ps : placed.symbols) {
      if (ps.x < -1e3 || ps.y < -1e3 || ps.x > size.width + 1e3 ||
          ps.y > size.height + 1e3)
        continue;
      InkBox ink;
      const double sc = ps.scale * sym_scale;
      if (!DrawResolvedSymbol(canvas_, ResolveSymbol(symbols_, ps.symbol_id),
                              ps.x, ps.y, px_per_himetric * sc,
                              symbol_scale_ * dpi_scale_ * sc,
                              ps.rotation_deg * kPi / 180.0,
                              pick_enabled_ ? &ink : nullptr, stamp_tint))
        continue;
      ++draws_emitted_;
      if (pick_enabled_) {
        ink.Add(ps.x, ps.y);
        pick_.AddBox(feature_, priority_, ink.ToRect(1.0));
      }
    }
  }
  return first;
}

// A LINE'S HIGHLIGHT IS ONE WIDER STROKE, not eight offset ones (G4). Offset-
// stamping a polyline in N directions and stroking it once at N pixels wider
// draw the same picture — a line has no interior for the offsets to reveal,
// which is exactly what makes the halo trick worth its cost on a GLYPH — so
// the line takes the cheap form. It goes under the casing as well as under the
// line, because a casing is part of what is being highlighted.
void GeoDraw::HighlightPaths(
    const std::vector<std::vector<SurfacePoint>>& paths,
    const GeoLineStyle& style) {
  if (state_ != RenderState::kHighlighted) return;
  const bool patterned = style.pattern.valid;
  if (!patterned && !style.stroke.valid && !style.casing.valid) return;

  const Pen& base = patterned ? style.pattern.pen : style.stroke.pen;
  int base_w = std::max(1, base.width);
  if (style.casing.valid) base_w = std::max(base_w, style.casing.pen.width);
  const int extra = std::max(1, static_cast<int>(std::lround(highlight_px_)));

  Pen hp = base;
  hp.color = highlight_color_;
  hp.width = std::max(1, base_w + 2 * extra);

  AsHighlightPass pass(&pick_enabled_, &draws_emitted_, &highlight_draws_);
  if (patterned) {
    // The stamps grow with the highlight AND take its colour, so a highlighted
    // railroad glows around its crossties rather than around its rail only.
    //
    // CAPPED AT 2x, which the casing (same arithmetic, G3) is not. The ratio
    // is the widened pen over the plain one, and on a THIN line that is a big
    // number — a 3-px highlight on a 2-px railroad asks for 4x crossties, and
    // a 4x crosstie is not that railroad glowing, it is a different and much
    // coarser railroad drawn underneath. Doubling is already unmistakable.
    const double grow = std::min(
        2.0, hp.width / static_cast<double>(std::max(1, style.pattern.pen.width)));
    PatternPaths(paths, style.pattern, &hp, grow, &highlight_color_);
  } else {
    StrokeStyle hs;
    hs.valid = true;
    hs.pen = hp;
    StrokePaths(paths, hs);
  }
}

Status GeoDraw::DrawSurfacePath(
    const std::vector<std::vector<SurfacePoint>>& paths,
    const GeoLineStyle& style) {
  if (canvas_ == nullptr) return Status::Error(kInvalidArg, "null canvas");
  Status first = Status::Ok();

  HighlightPaths(paths, style);

  // The casing goes down first, and it follows whichever of the two passes is
  // actually drawing: a solid casing under a dashed line would read as a solid
  // white line with blue dashes on it, which is not what "halo" means.
  if (style.casing.valid) {
    Status s;
    if (style.pattern.valid) {
      // The stamps grow with the casing so their outline shows too; the ratio
      // is the casing's own width over the line's.
      const double base = std::max(1, style.pattern.pen.width);
      const double grow = style.casing.pen.width / base;
      s = PatternPaths(paths, style.pattern, &style.casing.pen, grow);
    } else {
      s = StrokePaths(paths, style.casing);
    }
    if (!s.ok() && first.ok()) first = s;
  }

  Status s = StrokePaths(paths, style.stroke);
  if (!s.ok() && first.ok()) first = s;
  s = PatternPaths(paths, style.pattern, nullptr, 0.0);
  if (!s.ok() && first.ok()) first = s;
  return first;
}

Status GeoDraw::DrawContour(IGeoContour& contour, const GeoLineStyle& style) {
  if (canvas_ == nullptr) return Status::Error(kInvalidArg, "null canvas");
  if (!proj_.Ready())
    return Status::Error(kInvalidArg, "projection not ready");
  return DrawSurfacePath(BuildGeoPath(proj_, contour), style);
}

Status GeoDraw::DrawGeoLine(const GeoPoint& a, const GeoPoint& b,
                            LineKind kind, const GeoLineStyle& style) {
  if (!proj_.Ready())
    return Status::Error(kInvalidArg, "projection not ready");
  GeoContourPtr c = MakeGeoLine(proj_, a, b, kind, clip_);
  return DrawContour(*c, style);
}

Status GeoDraw::DrawGeoPolyline(const std::vector<GeoPoint>& points,
                                LineKind kind, const GeoLineStyle& style,
                                bool closed) {
  if (points.size() < 2) return Status::Ok();
  if (!proj_.Ready())
    return Status::Error(kInvalidArg, "projection not ready");
  PolylineContour c(proj_, points, kind, clip_, closed);
  return DrawContour(c, style);
}

Status GeoDraw::DrawGeoCircle(const GeoPoint& center, double radius_m,
                              const GeoLineStyle& style, int num_points) {
  GeoCircleContour c(center, radius_m, num_points);
  return DrawContour(c, style);
}

Status GeoDraw::DrawGeoEllipse(const GeoPoint& center, double vert_radius_m,
                               double horz_radius_m, double rotation_deg,
                               const GeoLineStyle& style, int num_points) {
  GeoEllipseContour c(center, vert_radius_m, horz_radius_m, rotation_deg,
                      num_points);
  return DrawContour(c, style);
}

Status GeoDraw::DrawGeoArc(const GeoPoint& center, double radius_m,
                           double start_bearing_deg, double sweep_deg,
                           const GeoLineStyle& style, int points_per_circle) {
  GeoArcContour c(center, radius_m, start_bearing_deg, sweep_deg,
                  points_per_circle);
  return DrawContour(c, style);
}

// --- symbols ---------------------------------------------------------------

Status GeoDraw::StampSymbol(double x, double y, const std::string& symbol_id,
                            const PointSymbolStyle& style,
                            double chart_rotation_deg) {
  if (canvas_ == nullptr) return Status::Error(kInvalidArg, "null canvas");
  if (symbols_ == nullptr)
    return Status::Error(kInvalidArg, "no symbol library");
  ResolvedSymbol sym = ResolveSymbol(symbols_, symbol_id);
  if (!sym.drawable())
    return Status::Error(kNotFound, "no symbol '" + symbol_id + "'");

  const double sc = style.scale > 0.0 ? style.scale : 1.0;
  // PR2: the one conversion, done once, so the highlight silhouette and the
  // symbol over it can never disagree about which way the chart is turned.
  const double rot_rad =
      SymbolAngleOnChart(style.rotation_deg, chart_rotation_deg) * kPi / 180.0;

  // G4: T2's halo, applied to a shape instead of to a string. The symbol's own
  // SILHOUETTE stamped at the halo offsets in the highlight colour, then the
  // symbol over it — so the marker keeps its own colour, which is the property
  // the user identifies it by and the thing a swapped fill colour destroys.
  // The offsets are literally HaloOffsets, so a highlight and a text halo can
  // never drift apart.
  if (state_ == RenderState::kHighlighted) {
    double hdx[kMaxHaloOffsets], hdy[kMaxHaloOffsets];
    const int n = HaloOffsets(highlight_px_, hdx, hdy);
    AsHighlightPass pass(&pick_enabled_, &draws_emitted_, &highlight_draws_);
    for (int i = 0; i < n; ++i) {
      if (DrawResolvedSymbol(canvas_, sym, x + hdx[i], y + hdy[i],
                             PxPerHimetric() * sc,
                             symbol_scale_ * dpi_scale_ * sc, rot_rad, nullptr,
                             &highlight_color_))
        ++draws_emitted_;
    }
  }

  InkBox ink;
  if (!DrawResolvedSymbol(canvas_, sym, x, y, PxPerHimetric() * sc,
                          symbol_scale_ * dpi_scale_ * sc, rot_rad,
                          pick_enabled_ ? &ink : nullptr))
    return Status::Ok();  // resolved, but wholly off the canvas
  ++draws_emitted_;
  if (pick_enabled_) {
    ink.Add(x, y);
    PixelRect box = ink.ToRect(1.0);
    if (box.width < kMinPickBox) {
      box.x -= (kMinPickBox - box.width) / 2;
      box.width = kMinPickBox;
    }
    if (box.height < kMinPickBox) {
      box.y -= (kMinPickBox - box.height) / 2;
      box.height = kMinPickBox;
    }
    pick_.AddBox(feature_, priority_, box);
  }
  return Status::Ok();
}

Status GeoDraw::DrawSymbol(const GeoPoint& at, const std::string& symbol_id,
                           const PointSymbolStyle& style) {
  if (!proj_.Ready())
    return Status::Error(kInvalidArg, "projection not ready");
  double x = 0.0, y = 0.0;
  Status s = proj_.GeoToSurface(at, &x, &y);
  if (!s.ok()) return s;
  return StampSymbol(x, y, symbol_id, style, proj_.Rotation());
}

Status GeoDraw::DrawSymbolAtPixel(double x, double y,
                                  const std::string& symbol_id,
                                  const PointSymbolStyle& style) {
  // Zero, deliberately: see the header. The pixel is the caller's and so is
  // the angle.
  return StampSymbol(x, y, symbol_id, style, 0.0);
}

// --- labels ----------------------------------------------------------------

Status GeoDraw::DrawLabelAtPixel(double x, double y, const std::string& text,
                                 const LabelStyle& style) {
  if (canvas_ == nullptr) return Status::Error(kInvalidArg, "null canvas");
  if (text.empty()) return Status::Ok();

  TextStyle ts = style.style;
  // A ground-sized label needs this frame's metres per pixel; a pixel-sized
  // one is left exactly as authored (the ref_scale argument is the chart
  // renderer's and is deliberately not plumbed here — an overlay label that
  // wants to zoom says kMeters).
  const double mpp = proj_.Ready()
                         ? proj_.DegPerPixelLat() * kMetersPerDegreeLat
                         : 0.0;
  ts.size = LabelPixelSize(style, proj_.Ready() ? proj_.Scale() : 0.0, mpp,
                           0.0);
  if (ts.size < kMinLabelPx) return Status::Ok();

  const double halo_px = HaloPixels(style, ts.size);
  double hdx[kMaxHaloOffsets], hdy[kMaxHaloOffsets];
  const int halo_n = halo_px > 0.0 ? HaloOffsets(halo_px, hdx, hdy) : 0;
  TextStyle hs = ts;
  hs.color = style.halo_color;

  // Where the ink lands is MeasureLabelInk's answer, not a second copy of the
  // same arithmetic — LabelPlacer tests a label with that function and this
  // draws it, so a placer can never disagree with the ink it reserved room
  // for. Measuring is only worth its cost when alignment or picking needs it;
  // otherwise the origin is the anchor plus the offset, as it always was.
  int lx, ly;
  PixelRect ink_box;
  bool have_ext = false;
  if (style.halign != LabelHAlign::kLeft ||
      style.valign != LabelVAlign::kBaseline || pick_enabled_) {
    const LabelInk ink = MeasureLabelInk(*canvas_, x, y, text, ts, style);
    lx = ink.origin.x;
    ly = ink.origin.y;
    ink_box = ink.box;
    have_ext = ink.measured;
  } else {
    lx = static_cast<int>(std::lround(x)) + style.dx;
    ly = static_cast<int>(std::lround(y)) + style.dy;
  }

  const PixelSize size = canvas_->Size();
  if (lx < -1000 || ly < -1000 || lx > size.width + 1000 ||
      ly > size.height + 1000)
    return Status::Ok();

  // G4. A highlighted label wears the highlight OUTSIDE its own halo, at the
  // sum of the two radii — a name that already has a white outline for
  // legibility keeps it and gains a coloured one around it, rather than
  // trading legibility for selection.
  if (state_ == RenderState::kHighlighted) {
    double sx[kMaxHaloOffsets], sy[kMaxHaloOffsets];
    const int n = HaloOffsets(halo_px + highlight_px_, sx, sy);
    TextStyle hi = ts;
    hi.color = highlight_color_;
    for (int i = 0; i < n; ++i)
      canvas_->DrawTextString(text, lx + static_cast<int>(std::lround(sx[i])),
                              ly + static_cast<int>(std::lround(sy[i])), hi);
    highlight_draws_ += static_cast<size_t>(n);
  }

  for (int i = 0; i < halo_n; ++i) {
    canvas_->DrawTextString(text, lx + static_cast<int>(std::lround(hdx[i])),
                            ly + static_cast<int>(std::lround(hdy[i])), hs);
  }
  halo_draws_ += static_cast<size_t>(halo_n);
  Status s = canvas_->DrawTextString(text, lx, ly, ts);
  if (!s.ok()) return s;
  ++draws_emitted_;
  if (pick_enabled_ && have_ext) pick_.AddBox(feature_, priority_, ink_box);
  return Status::Ok();
}

Status GeoDraw::DrawLabel(const GeoPoint& at, const std::string& text,
                          const LabelStyle& style) {
  if (!proj_.Ready())
    return Status::Error(kInvalidArg, "projection not ready");
  double x = 0.0, y = 0.0;
  Status s = proj_.GeoToSurface(at, &x, &y);
  if (!s.ok()) return s;
  return DrawLabelAtPixel(x, y, text, style);
}

Status GeoDraw::DrawLabelAlongPath(
    const std::vector<std::vector<SurfacePoint>>& paths,
    const std::string& text, const LabelStyle& style) {
  if (canvas_ == nullptr) return Status::Error(kInvalidArg, "null canvas");
  if (text.empty()) return Status::Ok();

  TextStyle ts = style.style;
  const double mpp = proj_.Ready()
                         ? proj_.DegPerPixelLat() * kMetersPerDegreeLat
                         : 0.0;
  ts.size = LabelPixelSize(style, proj_.Ready() ? proj_.Scale() : 0.0, mpp,
                           0.0);
  if (ts.size < kMinLabelPx) return Status::Ok();

  std::vector<double> adv;
  if (!GlyphAdvances(canvas_, text, ts, &adv))
    return Status::Error(kUnsupported, "canvas cannot measure text");

  const double halo_px = HaloPixels(style, ts.size);
  double hdx[kMaxHaloOffsets], hdy[kMaxHaloOffsets];
  const int halo_n = halo_px > 0.0 ? HaloOffsets(halo_px, hdx, hdy) : 0;
  TextStyle hs = ts;
  hs.color = style.halo_color;
  const PixelSize size = canvas_->Size();

  auto on_canvas = [&](const PlacedGlyph& g) {
    return !(g.x < -ts.size * 2 || g.y < -ts.size * 2 ||
             g.x > size.width + ts.size * 2 || g.y > size.height + ts.size * 2);
  };

  for (const std::vector<SurfacePoint>& sub : paths) {
    if (sub.size() < 2) continue;
    for (const PlacedTextRun& run :
         PlaceTextAlongPath(sub, adv, style.spacing_px, style.max_angle_deg,
                            style.offset_px +
                                AlongPathAnchorShift(style, ts.size))) {
      InkBox ink;
      bool drew = false;
      // G4's highlight is the outermost band and so goes down first, and for
      // the same reason as the halo below it is a WHOLE-RUN pass.
      if (state_ == RenderState::kHighlighted) {
        double sx[kMaxHaloOffsets], sy[kMaxHaloOffsets];
        const int n = HaloOffsets(halo_px + highlight_px_, sx, sy);
        TextStyle hi = ts;
        hi.color = highlight_color_;
        for (const PlacedGlyph& g : run.glyphs) {
          if (!on_canvas(g)) continue;
          const std::string gs = text.substr(g.index, 1);
          for (int i = 0; i < n; ++i)
            canvas_->DrawRotatedTextString(gs, g.x + sx[i], g.y + sy[i],
                                           g.angle_rad, hi);
          highlight_draws_ += static_cast<size_t>(n);
        }
      }
      // The WHOLE run's halo before ANY of its fill: per glyph, glyph N's halo
      // lands on glyph N-1's face at a tight bend and eats it.
      if (halo_n > 0) {
        for (const PlacedGlyph& g : run.glyphs) {
          if (!on_canvas(g)) continue;
          const std::string gs = text.substr(g.index, 1);
          for (int i = 0; i < halo_n; ++i)
            canvas_->DrawRotatedTextString(gs, g.x + hdx[i], g.y + hdy[i],
                                           g.angle_rad, hs);
          halo_draws_ += static_cast<size_t>(halo_n);
        }
      }
      for (const PlacedGlyph& g : run.glyphs) {
        if (!on_canvas(g)) continue;
        canvas_->DrawRotatedTextString(text.substr(g.index, 1), g.x, g.y,
                                       g.angle_rad, ts);
        drew = true;
        if (pick_enabled_) ink.Add(g.x, g.y);
      }
      if (drew) {
        ++draws_emitted_;
        if (pick_enabled_)
          pick_.AddBox(feature_, priority_, ink.ToRect(ts.size));
      }
    }
  }
  return Status::Ok();
}

}  // namespace fv
