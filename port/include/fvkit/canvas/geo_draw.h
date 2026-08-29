// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/canvas/geo_draw.h — the surface an overlay draws through (draw plan G3).
//
//   IGeoContour (G1) -> BuildGeoPath -> [ GeoDraw ] -> ICanvas
//   ISymbolLibrary (G2) ---------------^
//
// WHAT WAS MISSING AND WHY THIS IS IT. An overlay gets a MapProjection and an
// ICanvas and nothing else, so every line it draws is straight in screen space
// and every marker is a hand-rolled polygon — that is literally what
// PointOverlay and route.py did before this file. Meanwhile the vector seam
// three directories away already knows how to walk a SAMI cycle along a path,
// resolve a symbol to a display list or a tile, and stamp it clipped. GeoDraw
// is the join: geographic verbs in, the seam's own primitives out.
//
// ONE VOCABULARY (draw plan R1). Styling is StrokeStyle / LinePatternStyle /
// PointSymbolStyle / LabelStyle from fvkit/vector/style.h, unchanged. A GeoSym
// SAMI cycle, an S-52 LC and an OSM dasharray are all LinePatternStyle, so an
// overlay line can be styled with any of them and nothing here has a second
// vocabulary of its own. What GeoDraw adds is COMPOSITION, not new structs:
// GeoLineStyle is a casing, a stroke and a pattern drawn in that order.
//
// NO NEW ICanvas OPERATION (R6). Everything is the existing primitive set, so
// a backend that never grew a method — including pyfvw's Python ICanvas
// subclasses — gets all of it.
//
// PICKING IS FILLED FROM WHAT IS EMITTED, the vector seam's rule since V5b: an
// overlay's A5 HitTest can be answered from the ink the user can actually see
// rather than from a second geometric derivation that is free to disagree with
// the first. Off by default here (unlike VectorRenderer, whose caller always
// wants identify) because an overlay that does its own analytic hit test —
// PointOverlay does — should not pay for an index nobody reads.

#ifndef FVKIT_CANVAS_GEO_DRAW_H_
#define FVKIT_CANVAS_GEO_DRAW_H_

#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"
#include "fvkit/geo.h"
#include "fvkit/geo/contour.h"
#include "fvkit/proj.h"
#include "fvkit/symbol/library.h"
#include "fvkit/vector/pick.h"
#include "fvkit/vector/style.h"

namespace fv {

// ---------------------------------------------------------------------------
// Render state (draw plan G4)
// ---------------------------------------------------------------------------

// What a draw MEANS, as opposed to how it is styled. A selected waypoint, a
// hovered feature and a search result are all "this one, not the others", and
// before G4 every overlay said it by swapping a colour by hand — which loses
// the thing's own colour, the one property the user identifies it by.
//
// kHighlighted is the caller's to set and nothing sets it for you: the app
// layer knows what is selected, this layer knows how to draw it.
//
// DIMMING IS DEFERRED (Chris, 2026-08-13). Adding it later is one enum value
// and one filter; the two decisions already worked out are parked in §3d of
// port/fvkit-draw-plan-COMPLETE.md so they are not re-derived.
enum class RenderState {
  kNormal,
  kHighlighted,
};

// ---------------------------------------------------------------------------
// Line style
// ---------------------------------------------------------------------------

// How one geographic line is inked. Three passes, in this order:
//
//   casing   a wider stroke UNDER everything else — the "halo" a route line
//            needs to stay readable over a chart. It is the line equivalent of
//            LabelStyle's halo and it is drawn the same cheap way: the same
//            geometry, wider, first. When `pattern` is set the casing follows
//            the PATTERN (a dashed line gets a dashed casing), because a solid
//            white bar under a dashed blue line reads as a solid white line.
//   stroke   the plain pen.
//   pattern  the dash/gap/symbol cycle, over the top — the same order
//            VectorRenderer draws an S-52 LC over its LS casing.
//
// Any of the three may be off; all three may be on. A GeoLineStyle with
// nothing valid draws nothing, which is a legitimate way to hide a line
// without branching at the call site.
struct GeoLineStyle {
  StrokeStyle casing;
  StrokeStyle stroke;
  LinePatternStyle pattern;
};

// A plain stroked line.
GeoLineStyle SolidGeoLine(FvColor color, int width_px);

// A named preset (see line_preset below) at `width_px`, in `color`.
// An unknown name falls back to solid — a style file naming a preset this
// build does not have should draw a line, not nothing.
GeoLineStyle PresetGeoLine(const std::string& preset, FvColor color,
                           int width_px);

// Adds a casing `extra_px` wider on EACH side, so the halo shows `extra_px` of
// itself past the line. Applied to whichever of stroke/pattern is set.
void AddCasing(GeoLineStyle* style, FvColor color, int extra_px = 2);

// ---------------------------------------------------------------------------
// The line presets
// ---------------------------------------------------------------------------
//
// THE FINDING THIS TABLE IS (draw plan §3c). LineStyles.h names ~35 styles and
// LineSegmentRenderer.cpp spends 913 lines and 15 classes drawing them —
// railroad, powerline, zigzag, FEBA, FLOT, notched, T-mark, arrow, borders.
// Read together they are ONE operation: stamp a small shape every N pixels
// along the path, offset to one side, rotated to the tangent. That is exactly
// PathRun{kSymbol, offset, rotation_deg}, and PlaceAlongPath already walks it.
// So the classes become DATA — a table of LinePatternStyles over
// BuiltinSymbolLibrary — and nobody ports LineSegmentRenderer.cpp.
//
// The set here is the shapes, not the catalogue: a caller that wants
// FalconView's 35 names builds them from these five runs plus a colour, and a
// caller that wants something else writes a LinePatternStyle by hand, which is
// the same struct GeoSym and S-52 emit.
namespace line_preset {

constexpr const char* kSolid = "solid";
constexpr const char* kDash = "dash";
constexpr const char* kLongDash = "long-dash";
constexpr const char* kDot = "dot";
constexpr const char* kDashDot = "dash-dot";
constexpr const char* kRailroad = "railroad";   // crossties on a solid line
constexpr const char* kArrow = "arrow";         // arrowheads along the way
constexpr const char* kTick = "tick";           // strokes across the line
constexpr const char* kNotch = "notch";         // half-ticks to the LEFT
constexpr const char* kFeba = "feba";           // T-marks to the left (FEBA/FLOT)

// Every name above, in this order, terminated by nullptr.
extern const char* const kAll[];

}  // namespace line_preset

// Builds one preset's pattern. `pen` strokes the dashes AND is the colour the
// caller expects the stamps in — a builtin symbol carries its own colour, so a
// caller drawing a coloured railroad hands GeoDraw a BuiltinSymbolLibrary set
// to that colour (SetColor). `scale` multiplies every length and every stamp,
// so a preset at scale 2 is the same style twice the size rather than a
// different style.
//
// Returns an invalid LinePatternStyle for kSolid and for an unknown name:
// solid IS the plain pen, and expressing it as a one-run cycle would cost a
// placer walk to draw what DrawLines draws.
LinePatternStyle MakeLinePreset(const std::string& name, const Pen& pen,
                                double scale = 1.0);

// ---------------------------------------------------------------------------
// GeoDraw
// ---------------------------------------------------------------------------

class GeoDraw {
 public:
  // `symbols` may be null — the line and label verbs do not need one, and an
  // overlay that only draws lines should not have to build a library. A symbol
  // verb with no library draws nothing and says so through Status.
  GeoDraw(const MapProjection& proj, ICanvas* canvas,
          ISymbolLibrary* symbols = nullptr);

  void SetSymbols(ISymbolLibrary* symbols) { symbols_ = symbols; }
  ISymbolLibrary* symbols() const { return symbols_; }

  // User zoom on symbology, exactly VectorRenderer::SetSymbolScale.
  void SetSymbolScale(double s) { symbol_scale_ = s > 0.0 ? s : 1.0; }
  double symbol_scale() const { return symbol_scale_; }

  // THE WAY OUT OF THE DPI DEFECT, and it is deliberately not applied by
  // default. A symbol is sized on its library's own nominal pixel (25.4
  // HIMETRIC for the builtins and GeoSym, 32 for S-52) and none of those three
  // numbers is the device's, so on a retina pitch every symbol is half its
  // physical size while the line widths and text beside it are right. Fixing
  // that inside the style engines would move every pinned chart golden, which
  // is why it has stood as a ledger defect — but OVERLAY symbology has no
  // goldens, so a shell that knows its device can set this to dpi/100 and get
  // a device-correct answer from day one.
  //
  // 1.0 is the pre-G3 sizing exactly.
  void SetSymbolDpiScale(double s) { dpi_scale_ = s > 0.0 ? s : 1.0; }
  double symbol_dpi_scale() const { return dpi_scale_; }

  // --- render state (G4) --------------------------------------------------
  //
  // Applies to every verb until it is set back. An overlay drawing a selected
  // feature sets it, draws, and clears it — the state is on the DRAW and not
  // on the style, because the same style at both states is the point.
  //
  // The highlight is T2's halo mechanism reused, which is what makes it need
  // NOTHING from ICanvas (R6): the same ink stamped at 4-8 offsets in the
  // highlight colour, then the normal pass over it. A line takes the cheap
  // equivalent — one wider stroke under everything, in the highlight colour —
  // because stamping a polyline eight times draws the same picture for eight
  // times the cost.
  void SetState(RenderState s) { state_ = s; }
  RenderState state() const { return state_; }

  // The highlight colour and how far past the ink it shows. The default is
  // FalconView's selection yellow at 3 px, which is what PointOverlay and
  // route.py both hand-rolled before G4 — a caller that wants a hover tint or
  // a search colour says so.
  void SetHighlight(FvColor color, double width_px = 3.0);
  FvColor highlight_color() const { return highlight_color_; }
  double highlight_width() const { return highlight_px_; }

  // Clip contours in geographic space before densifying (G1's whole point).
  // On by default; off is for a caller that wants the geometry off-canvas too.
  void SetClip(bool on) { clip_ = on; }

  // --- picking ------------------------------------------------------------
  //
  // OFF by default; see the header comment. When on, every primitive that
  // reaches the canvas is also added to the index under the CURRENT feature
  // (SetFeature), so an overlay's HitTest is a query over its own ink.
  void SetPickEnabled(bool on) { pick_enabled_ = on; }
  bool pick_enabled() const { return pick_enabled_; }

  // What subsequent draws are attributed to. The one-int form is the overlay
  // case — an overlay's features have identity in its own document, so the id
  // is the document's own (a row id, a waypoint index) and the other three
  // FeatureRef fields are unused. Whatever comes out of a HitTest is this
  // number back again.
  void SetFeature(int32_t id, int priority = 0);
  void SetFeature(const FeatureRef& ref, int priority = 0);

  // Valid until the next ClearPick(). An overlay clears at the top of OnDraw.
  const PickIndex& pick_index() const { return pick_; }
  void ClearPick() { pick_.Clear(); }

  // --- lines --------------------------------------------------------------

  Status DrawGeoLine(const GeoPoint& a, const GeoPoint& b, LineKind kind,
                     const GeoLineStyle& style);

  Status DrawGeoPolyline(const std::vector<GeoPoint>& points, LineKind kind,
                         const GeoLineStyle& style, bool closed = false);

  Status DrawGeoCircle(const GeoPoint& center, double radius_m,
                       const GeoLineStyle& style,
                       int num_points = kDefaultCirclePoints);

  Status DrawGeoEllipse(const GeoPoint& center, double vert_radius_m,
                        double horz_radius_m, double rotation_deg,
                        const GeoLineStyle& style,
                        int num_points = kDefaultCirclePoints);

  Status DrawGeoArc(const GeoPoint& center, double radius_m,
                    double start_bearing_deg, double sweep_deg,
                    const GeoLineStyle& style,
                    int points_per_circle = kDefaultCirclePoints);

  // Any contour at all, including one a caller wrote itself.
  Status DrawContour(IGeoContour& contour, const GeoLineStyle& style);

  // Already-projected sub-paths, for a caller that has its own geometry (a
  // followed road, a decoded shape) and only wants the styling.
  Status DrawSurfacePath(const std::vector<std::vector<SurfacePoint>>& paths,
                         const GeoLineStyle& style);

  // --- symbols and labels -------------------------------------------------

  // Stamps `symbol_id` from the library with the symbol's own origin on `at`.
  // A symbol the library does not have is a kNotFound Status and no ink —
  // never a silent nothing, because a mistyped id is the most likely failure
  // and it is invisible otherwise.
  //
  // PR2: `style.rotation_deg` is measured against the CHART'S NORTH, so a
  // turned projection turns this symbol with it (SymbolAngleOnChart).
  Status DrawSymbol(const GeoPoint& at, const std::string& symbol_id,
                    const PointSymbolStyle& style = PointSymbolStyle{});

  // The same at a surface pixel, for map furniture (a north arrow, a scale
  // bar) that belongs to the CANVAS and not to a position on the earth.
  //
  // PR2: and because it belongs to the canvas, `style.rotation_deg` is a
  // SCREEN angle here and the chart's rotation is NOT applied — the caller
  // that chose the pixel owns the angle too. A north arrow drawn this way asks
  // the projection for its rotation itself; the ownship (MM4) already does,
  // because `screen_angle_deg()` is heading + map rotation + convergence.
  Status DrawSymbolAtPixel(double x, double y, const std::string& symbol_id,
                           const PointSymbolStyle& style = PointSymbolStyle{});

  // A text string anchored at a geographic point. Carries T2's halo and E8's
  // alignment because it takes the seam's own LabelStyle — the same struct
  // OSM's text-halo and S-52's HJUST/VJUST land in, so an overlay label and a
  // chart label are styled the same way and look the same.
  //
  // kAlongPath placement is NOT honoured here (there is no path at a point);
  // use DrawLabelAlongPath.
  Status DrawLabel(const GeoPoint& at, const std::string& text,
                   const LabelStyle& style);

  Status DrawLabelAtPixel(double x, double y, const std::string& text,
                          const LabelStyle& style);

  // Text laid glyph by glyph along already-projected sub-paths, each glyph
  // rotated to the local tangent. A run that does not fit, or that turns
  // harder than style.max_angle_deg, is not drawn — half a name is worse than
  // none.
  Status DrawLabelAlongPath(
      const std::vector<std::vector<SurfacePoint>>& paths,
      const std::string& text, const LabelStyle& style);

  // --- diagnostics ---------------------------------------------------------

  // Primitives that reached the canvas since the last ResetCounts(). A haloed
  // label counts once, its halo stamps separately — the same split
  // VectorRenderer reports, for the same reason.
  size_t draws_emitted() const { return draws_emitted_; }
  size_t halo_draws() const { return halo_draws_; }
  // Highlight passes, counted separately for the same reason halo passes are:
  // they are ink the caller did not ask for by name, and a test that wants to
  // know the state changed anything asks THIS. A highlight pass is never in
  // the pick index either — the user aims at the feature, not at its glow.
  size_t highlight_draws() const { return highlight_draws_; }
  void ResetCounts() { draws_emitted_ = halo_draws_ = highlight_draws_ = 0; }

 private:
  Status StrokePaths(const std::vector<std::vector<SurfacePoint>>& paths,
                     const StrokeStyle& stroke);
  // `stamp_tint` recolours the pattern's SYMBOLS, which `pen_override` cannot
  // reach (it is the dash pen). Used by the highlight pass and deliberately
  // not by the casing: a casing's stamps have come out in the library's own
  // colour since G3 and every G3 assertion is pinned over that.
  Status PatternPaths(const std::vector<std::vector<SurfacePoint>>& paths,
                      const LinePatternStyle& pattern, const Pen* pen_override,
                      double width_override,
                      const FvColor* stamp_tint = nullptr);
  // px_per_himetric for a display list, given the library's own grid.
  double PxPerHimetric() const;
  // `chart_rotation_deg` is the turn `SymbolAngleOnChart` takes out of the
  // style's north-up angle: the projection's rotation for a GEOGRAPHIC anchor,
  // and zero for a PIXEL one, which is the whole difference between DrawSymbol
  // and DrawSymbolAtPixel.
  Status StampSymbol(double x, double y, const std::string& symbol_id,
                     const PointSymbolStyle& style,
                     double chart_rotation_deg);
  // Draws the highlight under one line's geometry. No-op at kNormal.
  void HighlightPaths(const std::vector<std::vector<SurfacePoint>>& paths,
                      const GeoLineStyle& style);

  const MapProjection& proj_;
  ICanvas* canvas_ = nullptr;
  ISymbolLibrary* symbols_ = nullptr;
  double symbol_scale_ = 1.0;
  double dpi_scale_ = 1.0;
  bool clip_ = true;
  RenderState state_ = RenderState::kNormal;
  FvColor highlight_color_{255, 220, 0, 255};
  double highlight_px_ = 3.0;
  bool pick_enabled_ = false;
  FeatureRef feature_;
  int priority_ = 0;
  PickIndex pick_;
  size_t draws_emitted_ = 0;
  size_t halo_draws_ = 0;
  size_t highlight_draws_ = 0;
};

}  // namespace fv

#endif  // FVKIT_CANVAS_GEO_DRAW_H_
