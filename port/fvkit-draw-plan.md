# FvKit Overlay Drawing — geographic lines, symbols and render state (plan)

**Status: G1 built 2026-08-13; G2 and G3 built 2026-08-14. G4 (render state) and G5 (SVG)
not started.** Where the built code differs from what is written below, the LEDGER row is the
record — notably G3's `GeoLineStyle` grew a `casing` (a halo for a line, and the thing that makes
an overlay line readable over a chart), and picking is off by default there rather than on. Companion docs: `port/fvkit-app-plan.md` (the app
layer this serves, A1–A6 done), `port/vpf-geosym-plan.md` (§5 = the vector seam this reuses),
`port/fvkit-contracts.md` (D1–D6 bind here too), ledger `port/PORTING.md`.

Reference implementation this is measured against: `fvw_core/FvMappingGraphics/` —
`GeographicContourIterator.{h,cpp}` (2037 lines), `LineSegmentRenderer.{h,cpp}` (913 lines),
`LineStyles.h`, `GraphicsUtilities.cpp`.

## 0. What this is, and why now

A6 gave the port real overlays — `fv::PointOverlay`, PythonView's `fv.route` — and drawing one
exposed the hole. **An overlay gets `MapProjection` + `ICanvas` and nothing else.** Every line it
draws is straight in screen space because that is all it can afford to write by hand: `route.py`
projects each waypoint and calls `draw_lines`; `PointOverlay` hand-rolls six polygons. Meanwhile
the vector seam three directories away already knows how to stamp a CGM symbol, walk a SAMI cycle
along a path, resolve a symbol to a display list *or* a PNG tile, and clip both — and none of it
is reachable without an `IVectorSource`.

So this plan does three things and one small fourth:

1. **Geographic contours** — great circle / rhumb / simple, plus circles, ellipses and arcs, as a
   pull iterator over `MapProjection`. Ported from `GeographicContourIterator`, algorithms intact.
2. **A symbol library seam** — the two accessors `IStyleEngine` already has, named as their own
   interface, with PNG / CGM / builtin implementations. A GeoSym engine then *is* a symbol
   library, and an overlay can stamp a chart symbol without owning a chart.
3. **`GeoDraw`** — the surface an overlay actually calls: geo-space verbs (`DrawGeoLine`,
   `DrawGeoCircle`, `DrawSymbol`, `DrawLabel`) styled with **the vector seam's own structs**, so a
   SAMI cycle, an S-52 complex line or an OSM dasharray is expressible on an overlay line without
   a second vocabulary.
4. **Render state** — normal / highlighted / dimmed, resolved from the stack, applied uniformly.

**This is not a port of `FvMappingGraphics`.** The contour math is ported (it is good, it is
subtle, and its geodesy dependencies are already in the tree). `LineSegmentRenderer.cpp` is *not*:
see §3c, where its 15 classes collapse into a table.

## 1. What already exists (so no session goes looking)

**All of the geodesy the contour iterators need is already compiled in the port.** `fv_geo_tool`
builds `distance.cpp`, `bound.cpp`, `check.cpp`, `east_of.cpp`, `overlap.cpp`, `mercator.cpp` —
i.e. `GEO_calc_end_point`, `GEO_geo_to_distance`, `GEO_bounds_check_degrees`, `GEO_in_bounds`,
`GEO_east_of_degrees`, `GEO_intersect_degrees`, `GEO_fudge_polar_lat_for_rhumb_line` and the
`Mercator` class. That is the complete dependency list of `GeographicContourIterator.cpp` apart
from `IDrawingToolsProjection`, which is `MapProjection` here. G1 is mostly mechanical.

**The vector seam already carries the whole styling vocabulary** (`fvkit/vector/style.h`):
`PathRun`/`LinePatternStyle` (a cyclic sequence of dash / gap / symbol runs measured in pixels —
E3b built it precisely so SAMI, S-52 `LC` and OSM line patterns would be one primitive),
`AreaPatternStyle`, `PointSymbolStyle`, `LabelStyle` (with T2's halo and E8's align), `VectorSymbol`
(a product-neutral display list) and `SymbolPixmap` (a PNG tile with a pivot). **All three products
already emit `LinePatternStyle`** — `fv_geosym_style.cpp:746`, `fv_s52_style.cpp:952`, and OSM's
dasharray path. Nothing here needs inventing.

**The placers are already public and already pure pixel geometry** (`fvkit/vector/renderer.h`):
`PlaceAlongPath`, `PlaceOverArea`, `PlaceTextAlongPath`, `ClipPolyline`, `ClipPolygon`. They take
pixels and return pixels, know nothing about a canvas or a product, and are tested that way.

**The symbol drawing is written and is locked in an anonymous namespace.** `renderer.cpp` holds
`DrawSymbolAt` (display list → canvas, with the CCGMSymbol y-flip and HIMETRIC scaling verbatim),
`DrawPixmapSymbolAt` (tile → canvas, exact blit at identity, nearest-neighbour when scaled or
rotated, with the half-pixel reasoning already worked out), `ResolveSymbol` (vector first, then
pixmap) and `DrawResolvedSymbol`. Roughly 240 lines that every overlay wants and no overlay can
reach. Same story one directory over: **`ToVectorSymbol` is file-local in
`fv_geosym_style.cpp:179`**, so CGM → `VectorSymbol` is written and unreachable too.

**A PNG reader exists** (`fvkit/tools/png_io.h`, built for S-52's raster symbol sheet) and
`fv_png` is already a `PUBLIC` dependency of `fv_fvkit`.

**The state that decides "dimmed" already exists**: A2's `OverlayManager::current()` / `OfType()`,
A6's `Overlay::type_id()`, A4's `EditorManager::edited()`. No new bookkeeping.

## 2. Design rules

- **R1 — One vocabulary.** Overlay styling uses `StrokeStyle` / `LinePatternStyle` /
  `PointSymbolStyle` / `LabelStyle` from `fvkit/vector/style.h` unchanged. If an overlay needs
  something the chart products cannot say, the seam grows for both, not for one.
- **R2 — Extract, never fork.** `DrawSymbolAt` and friends MOVE out of the anonymous namespace.
  The acceptance test for that move is that **every pinned golden is byte-identical afterwards**.
  Any change in behaviour belongs to a later, named session.
- **R3 — Clip in geographic space, before densifying.** The whole reason the Windows contour code
  is 2000 lines is that it never densifies a geodesic and then throws pixels away. See §3a.
- **R4 — Pure geometry stays pure.** A contour yields `GeoPoint`s and knows nothing about a
  canvas. A path yields `SurfacePoint`s and knows nothing about a style. Only `GeoDraw` touches
  all three, which is what keeps the first two testable with no data and no canvas.
- **R5 — Bit-faithful applies to the ported math, not to new symbology.** The great-circle step
  size, the polar quirk, the Mercator rhumb walk and the crossover search are preserved as-is.
  Line *presets* (§3c) are new work and are pinned by their own goldens.
- **R6 — No new `ICanvas` virtuals.** Everything here composes the existing primitive set, the way
  T2's halo did. A backend that never grew a method (including pyfvw's Python `ICanvas`
  subclasses) gets all of it.

## 3. Components

### 3a. G1 — `fvkit/geo/contour.h`: geographic contours

```cpp
struct IGeoContour {
  virtual void MoveFirst() = 0;
  virtual bool NextPoint(GeoPoint* p) = 0;
};
```

The pull shape is kept because it is right: O(1) memory over a line that may densify to thousands
of points, and it composes — the path builder wraps *any* contour.

Concrete contours, ported from `GeographicContourIterator.cpp`:

| Class | From | Notes |
|---|---|---|
| `SimpleGeoLine` | `CSimpleGeoLinePoints` | Two points, straight in the current projection. Clipped by borrowing the rhumb clipper, exactly as the original does. |
| `GreatCircleContour` | `CGreatCirclePoints` | The rotation math (`get_angles` / `calc_intermediate_geo` / `forward_conversion`), the dpp-derived step, the crossover search. |
| `RhumbLineContour` | `CRhumbLinePoints` | Mercator parametric walk + the 4-point clipper (`get_clipped_points`, `clip_line`, `clip_t`, `wrap_around`). Composes `Mercator`, does not inherit it. |
| `GeoCircleContour` | `CGeoCirclePoints` | `GEO_calc_end_point` at N bearings; first point repeated to close. |
| `GeoEllipseContour` | `CGeoEllipsePoints` | Same, with a rotated radius. |
| `GeoArcContour` | *new* | A circle with a bearing range. FalconView spells this several places; one implementation here. |
| `PolylineContour` | *new* | A stored list of `GeoPoint`s with a per-leg `LineKind`, so a route, a coastline or a hand-drawn shape goes through the same pipe. |

**The clip is the point, not a nicety (R3).** A great circle from New York to Tokyo on a harbour
-scale map must cost a *search for where the arc enters the viewport*, not 40,000 densified points
that are then discarded. That is what `MoveFirst`'s 300 lines of bounds-flag algebra buy, and it
is the "efficient" in the ask. Preserve it verbatim.

Two quirks preserved under R5, both load-bearing:
- Step size is `delta_angle = 10 * (pixel_width_km + pixel_height_km) / EARTH_RADIUS` — about a
  20-pixel chord, derived from the projection's dpp, so the point count tracks the *screen*, not
  the line's length.
- Above |lat| 70 the step is `delta_angle / 5`, because a constant angular step covers far more
  screen near the poles.

**`GeoPath` — the segment producer** (`CGeographicContourLineSegments`'s replacement). Walks a
contour, projects with `MapProjection::GeoToSurface`, and yields **`std::vector<SurfacePoint>`
sub-paths** rather than the original's `(x1,y1,x2,y2)` int segments. Sub-paths because everything
downstream already speaks them — `PlaceAlongPath`, `ClipPolyline`, `ICanvas::DrawLines` — and
because a break (a point that will not project, an antimeridian crossing) is naturally the start
of a new sub-path rather than a special flag.

**Declared deviations**, both because `MapProjection` is equal-arc with no rotation and no
world-wrap duplication:
- `IDrawingToolsProjection::geoline_to_surface` returns a *second, wrapped* segment when a
  world-spanning map shows the same line at both edges. We emit one path. Note it; revisit if a
  world view ever needs it.
- Rotated maps have no representation here at all (`MapProjection` has no rotation yet), so the
  original's page-space/world-space transform dance has no analogue and is not ported.

Tests, and note that a golden hash proves nothing about orientation (standing rule): pin a
great circle's midpoint against the known geodesic (a JFK–NRT arc crosses well north of both
endpoints — a directional assertion the hash cannot make), pin that a rhumb line holds a constant
bearing along its length, pin that the point count scales with dpp and *not* with line length, and
pin that a line wholly off the map returns nothing after a search rather than after a walk.

### 3b. G2 — `fvkit/symbol/library.h`: symbols, and the extraction

```cpp
struct ISymbolLibrary {
  virtual const VectorSymbol* Symbol(const std::string& id) = 0;
  virtual const SymbolPixmap* Pixmap(const std::string& id) { return nullptr; }
  virtual double himetric_per_symbol_pixel() const { return 25.4; }
};
```

Those are, deliberately and exactly, three methods `IStyleEngine` already declares. So
**`IStyleEngine : public ISymbolLibrary`** is a two-line change and every existing engine —
GeoSym, S-52, OSM, `LookupTableStyleEngine` — becomes a symbol library with no other edit. That is
the whole of "expose the point drawing capabilities implemented in the vector map / GeoSym code":
do not move the code, name the interface that is already sitting there.

Implementations:

- **`PngSymbolLibrary`** — a directory of `<id>.png`, or a sprite sheet plus its JSON. Pivot from
  a sidecar, centre by default; `@2x` suffix for a high-DPI variant. **The sheet form also closes
  the ledger's "no sprite sheet, so OSM has no icons" gap (§2b)** — one loader, two consumers, so
  build it sheet-capable even though an overlay only needs loose files.
- **`CgmSymbolLibrary`** — a directory of `.cgm` over the existing `fv::CgmSymbol`. Needs
  `ToVectorSymbol` lifted out of `fv_geosym_style.cpp` into a header (R2: no behaviour change; the
  GeoSym goldens are the test). GeoSym's symbol set then draws in any overlay.
- **`BuiltinSymbolLibrary`** — the few the port authors itself, as `VectorSymbol` literals so they
  scale and rotate and need no data files: `PointOverlay`'s six shapes, the decorations §3c needs
  (tick, arrowhead, crosstie, T, diamond, notch), a north arrow, a crosshair.
- **`CompositeSymbolLibrary`** — ordered lookup across several, so an overlay says "mine first,
  then GeoSym's".

**Extraction, same session:** `DrawSymbolAt` / `DrawPixmapSymbolAt` / `ResolveSymbol` /
`DrawResolvedSymbol` move from `renderer.cpp`'s anonymous namespace into
`fvkit/vector/symbol_draw.h`, re-typed to take `ISymbolLibrary*` instead of `IStyleEngine*`.
`renderer.cpp` includes it and loses the definitions. **Acceptance: every pinned golden byte-
identical.**

**CGM or SVG — recommendation: CGM now, SVG as its own optional session.** Chris said "whichever
is easier", so plainly: CGM is easier by a wide margin and is already written and tested. SVG is
not the small job it looks like — a useful subset needs a path-data parser (`M/L/C/Q/A/Z` with
relative forms and implicit repeats), a transform stack, a presentation-attribute cascade, and
Bézier flattening; and `VectorSymbol` has no curve primitive, so flattening tolerance becomes a
new decision at the seam rather than an implementation detail. SVG's real argument is that it is
what everyone else ships, which matters for third-party symbol sets and not for us yet — so it is
G5, gated on wanting someone else's symbols.

**The DPI question this reopens, and the way out.** The ledger's standing defect (§2b) is that
symbol size ignores device DPI while line widths and text honour it, and the reason it is unfixed
is that fixing it moves every GeoSym golden. `GeoDraw` should therefore carry an explicit
`symbol_dpi_scale` **defaulting to 1.0**: overlay symbology has no goldens, so it can be made
DPI-correct from day one, while the chart products stay bit-faithful until someone decides to
re-pin. This gives a device-correct answer for new work without golden churn.

### 3c. G3 — `fvkit/canvas/geo_draw.h`: the surface an overlay calls

```cpp
GeoDraw d(proj, canvas, &symbols);
d.SetState(RenderState::kDimmed);
d.DrawGeoLine(a, b, LineKind::kGreatCircle, style);
d.DrawGeoPolyline(pts, LineKind::kRhumb, style);
d.DrawGeoCircle(centre, radius_m, style);
d.DrawSymbol(pos, "fv.arrow", sym_style);
d.DrawLabel(pos, "Fort Sumter", label_style);
```

`GeoLineStyle` is `{ StrokeStyle stroke; LinePatternStyle pattern; }` — the seam's own structs
(R1). Internals: contour → `GeoPath` → sub-paths → `PlaceAlongPath` when patterned,
`ClipPolyline` + `DrawLines` when plain, `DrawResolvedSymbol` for each stamp.

`GeoDraw` also fills a `PickIndex` when asked, from the primitives it *emits* — the vector seam's
existing rule, so an overlay's `HitTest` capability (A5) gets hit-testing of its own ink for free
instead of re-deriving screen positions in a second code path that can disagree with the first.

**The line-preset table, and the finding behind it.** `LineStyles.h` names ~35 styles and
`LineSegmentRenderer.cpp` spends 913 lines of GDI world-transform code drawing them —
railroad, powerline, zigzag, FEBA, FLOT, notched, T-mark, diamond, wire, arrow, borders. Read
them together and **they are all one thing**: stamp a small shape every N pixels along the path,
offset to one side, rotated to the tangent. That is exactly what `PathRun{kSymbol, offset,
rotation_deg}` says, and `PlaceAlongPath` already walks it. So the 15 renderer classes collapse
into **a table of named `LinePatternStyle`s over `BuiltinSymbolLibrary`** — data, not code — and
FalconView's line styles, GeoSym SAMI, S-52 `LC` and OSM dasharrays all become the same
expression. This is the single largest simplification in the plan and is worth stating up front
so nobody ports `LineSegmentRenderer.cpp`.

(Chris said "SAMI lines from S-57": SAMI is GeoSym/DNC's complex-line encoding and `LC` is
S-52's. Both are already reduced to `PathRun` cycles by their style engines, so both are
available here — the naming is the only thing that differs.)

### 3d. G4 — render state: highlighted

```cpp
enum class RenderState { kNormal, kHighlighted };
```

`kHighlighted` is the caller's to set — a selected waypoint, a hovered feature — which is a thing
every overlay already wants and `PointOverlay` and `route.py` both fake today by swapping a fill
colour by hand.

**Highlight is the T2 halo mechanism reused** — the shape stamped offset in N directions in the
highlight colour, then the normal pass over it. Nothing new from `ICanvas` (R6), so every backend
including pyfvw's Python `ICanvas` subclasses gets it.

**DIMMING IS DEFERRED (Chris, 2026-08-13): decide later if and when it is needed.** Recorded here
only so the two decisions already worked out are not re-derived from scratch when it comes back:

- The *resolution rule* would be: an overlay is dimmed when it is **not the current overlay AND
  another open overlay shares its `TypeId`**. The second clause is the requirement — a lone file
  overlay of its type is never dimmed, because there is nothing to disambiguate it from. It is
  computable from A2's `OverlayManager` and A6's `type_id()` with no new state, so it belongs in
  the app layer as `app::StateForOverlay(manager, overlay)` rather than per-overlay code.
- Dim would be **a blend toward the background, not an alpha reduction**. Alpha is the obvious
  wrong answer: `ICanvas` composites src-over, so a stack of half-transparent symbols reads as
  noise, while a colour blended toward the map background reads as "behind". A pixmap symbol has
  no colours to filter, so it would blend per-pixel into a temporary buffer —
  `DrawPixmapSymbolAt` already builds one on its resample path — cached per `(symbol_id, state)`.

Adding it later costs one enum value and one filter; nothing in G1–G3 is shaped around its
absence, which is why deferring it is cheap.

### 3e. Bindings, and the acceptance test

`pyfvw.draw` (GeoDraw, contours, line presets) and `pyfvw.symbol` (the libraries), so every
Python overlay gets all of it. **`route.py` is this plan's acceptance test the way `PointOverlay`
was A6's**: its legs become real great-circle or rhumb lines with a chosen line style, and its
selected waypoint highlights through `RenderState` instead of the hand-swapped fill colour it
uses today. If that reads right on screen, the plan landed.

## 4. What is deliberately not here

- **No new `ICanvas` operation.** Layer alpha, a clip region and a real pattern brush are all
  still open (§2b) and all still one canvas session; nothing here is blocked on them, and
  §3d's dim is designed specifically not to need layer alpha.
- **No label collision.** Still the ledger's item, still belongs in the renderer/scene, and
  `GeoDraw` emitting label boxes into a `PickIndex` is the same shape the eventual fix wants —
  but making it work across an overlay *and* a chart is a bigger decision than this plan.
- **No rotated maps**, because `MapProjection` has none.
- **No area fills beyond what the seam already does** — `PlaceOverArea` exists and is reachable;
  its outer-ring-only and bleed-past-the-ring limits (§2b) are inherited, not fixed.

## 5. Milestones (one session each, ledger-style)

| # | Session | Depends on |
|---|---------|-----------|
| **G1** | `fvkit/geo/contour.h` — the six ported contours + `PolylineContour`/`GeoArcContour` + `GeoPath`. Pure geometry, no canvas. | — |
| **G2** | `ISymbolLibrary` + PNG/CGM/builtin/composite; `IStyleEngine` inherits it; `symbol_draw.h` extraction (goldens byte-identical); `ToVectorSymbol` lifted. | — |
| **G3** | `GeoDraw` + the line-preset table. | G1, G2 |
| **G4** | `RenderState{kNormal,kHighlighted}` + the halo highlight pass. Dimming deferred (§3d). | G3 |
| **G5** | *(optional)* SVG symbol library — only if third-party symbol sets are wanted. | G2 |

Order note: G1 and G2 are independent and either can go first; G3 needs both. Bindings go with
the session that introduces the surface (G3 for `pyfvw.draw`, G2 for `pyfvw.symbol`), and
`route.py` gets rewritten in G4 when there is a dim state to show.

## 6. Risks

1. **The great-circle crossover search is the hairiest code in the reference file** — ~300 lines
   of bounds-flag algebra — and its failure mode is *silent*: a line that should cross the map
   simply draws nothing. Mitigation: port it verbatim first, then test it by **rendering** rather
   than by unit values — a grid of (line, viewport) pairs where the pixel answer is checkable, so
   a missing line is a failed assertion and not a blank corner nobody looked at.
2. **`Mercator` carries mutable state and `CRhumbLinePoints` gets it by private inheritance.**
   Compose it; do not inherit it. The original re-centres it per line.
3. **The extraction in G2 touches the file every vector golden runs through.** It is the reason
   R2 states the acceptance test as byte-identical output — if a golden moves, the move was not
   mechanical and the session should stop and find out why rather than re-pin.
4. **`PolylineContour` invites an overlay to hand in 100k points.** The contour seam is O(1) per
   point but `GeoDraw` is not free per sub-path; the simplify/scene machinery that solves this for
   charts (R3a/R3c) has no overlay analogue and is out of scope. Note the ceiling rather than
   pretend it is not there.
