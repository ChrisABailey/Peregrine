# ENC (S-57 / S-52) rendering review — 2026-11-09

Worklist from a read-through of the ENC drawing path: the S-57 reader, `EncVectorSource`,
the S-52 style engine and presentation library, the shared FvKit scene/renderer, and
Osprey's `EncChart`.

**Basis.** Code reading plus a script over `apps/Osprey/Data/s52/chartsymbols.xml`.
Nothing was built, run or profiled, so every item is unconfirmed on screen. Line numbers
are approximate (marked `~`) and were taken at commit `ac812879`. Paths are relative to
`port/`.

**Ordering.** Within each section, items are ordered by expected visible impact.
Each has a checkbox so it can be ticked off as it lands.

---

## A. Drawing bugs

### A1. Lookup rows shadowed by catch-all rows
- [x] Done 2026-11-09 — `SelectLookup` takes the matching row with the most conditions
  (earliest on a tie), then the first unconditioned row, then the last row. Test
  `S52Style.ACatchAllRowAheadOfSpecificRowsDoesNotShadowThem`.

**Where:** `Enc/fv_s52_style.cpp` `SelectLookup` (~854), `Matches`.

**What:** The first row in file order whose conditions hold wins; a row with no conditions
always matches. This assumes the unconditioned row is last. When nothing matches, the
fallback is `rows.back()`, not the unconditioned row.

**Evidence from `chartsymbols.xml`:** of 252 multi-row (table, object) groups, the
catch-all row is last in 234, first in 6 and mid-list in 12. There are 156 row pairs where
an earlier row's conditions are a subset of a later row's, making the later row
unreachable.

Affected groups:

| Table | Object | Effect |
|---|---|---|
| Plain, Symbolized | `VEGATN` | Row 0 is the catch-all `AP(CROSSX02);LS(DASH,1,LANDF)`; all 14 `CATVEG` rows are dead. Every vegetation area draws the same pattern. |
| Plain, Symbolized | `ACHARE` | Row 1 `SY(ACHARE51);CS(RESARE02)` shadows row 2. |
| Plain, Symbolized | `LNDARE` | Duplicate rows (harmless). |
| Simplified | `BCNLAT` | Catch-all at row 31 of 33. |
| Simplified | `TOPMAR` | Row 0 has an empty instruction and shadows the `TOPSHP` rows. |
| Paper | `BCNLAT` | Catch-all at 34 of 37. |
| Paper | `BOYLAT` | Catch-all at 45 of 55. |
| Paper | `DAYMAR` | Catch-all at 142 of 150. |
| Paper | `TOPMAR` | Catch-all at 74 of 81. |
| Paper | `LIGHTS` | 5 identical rows (harmless). |
| Inland (lowercase) | `bcnlat`, `boylat`, `boywtw`, `notmrk`, `topmar` | Same pattern. |

**Fix direction:** treat an unconditioned row as the fallback only — pick the first
conditioned row that matches, else the unconditioned row, else nothing. Consider
most-conditions-first among matches for the subset pairs. `fv_s52_style.h` documents the
first-match rule in its header comment and at `SelectLookup`; update both.

**Test:** `Enc/test/s52_style_test.cpp` — a `VEGATN` feature with `CATVEG` set must not
select row 0.

---

### A2. Safety contour is matched by exact equality
- [x] Done 2026-11-09 — `EncVectorSource` indexes each cell's distinct `VALDCO` values at
  parse time and gives every `DEPCNT` the pseudo-attribute `VALDCO_SHOALER`
  (`kEncShoalerContourAttribute`, the next shoaler contour in the same cell). `DepCnt02`
  draws a contour as the safety contour when it is not shoaler than the setting and its
  shoaler neighbour is (or it has none). Per cell, as S-52 specifies. `DEPARE01` needed no
  change: depth areas are bounded by contours, so its safe/unsafe split already falls on
  the same contour. Tests `S52Style.SafetyContourIsTheNextDeeperContourWhenTheSettingIsBetween`,
  `EncVectorSource.ContoursCarryTheNextShoalerContourOfTheirCell`. Still open: DEPARE
  boundaries are not promoted to safety contours (the spec's DEPARE-edge case).

**Where:** `Enc/fv_s52_style.cpp` `DepCnt02` (~363); settings in
`include/fvkit/vector/mariner.h`; choices in `apps/Pippin/Pippin/ChartSettings.swift`.

**What:** A contour is drawn as the safety contour only when
`fabs(VALDCO - safety_contour) < 1e-6`. S-52 selects the next available contour at or
deeper than the mariner's value.

**Effect:** Osprey offers 6/10/15/20/30/50/100 ft (×0.3048) or 2/3/5/10/15/20/30 m. NOAA
cells carry contours at roughly 1.8/3.6/5.4/9.1/18.2 m (from memory — check against the
Charleston cells). The small-craft default of 15 ft = 4.572 m matches none, so no contour
is drawn bold. The `DEPARE01` shading still changes at the setting, so the bold line and
the colour boundary disagree.

**Fix direction:** the effective safety contour is the smallest `VALDCO` ≥ the setting
among the loaded cells (per cell, or per view). That needs the available contour values
passed into the style context; `DEPARE01`'s safe/unsafe split should use the same value.

**Test:** existing DEPCNT safety tests are at `Enc/test/s52_style_test.cpp` ~355; they
use a setting equal to a contour value. Add one where it falls between two contours.

---

### A3. `RESTRN01` marks objects that carry no restriction
- [x] Done 2026-11-09 — `Restrn01` returns nothing when `RESTRN` is absent or empty;
  `RESARE02` keeps the `RSRDEF51` default. Test
  `S52Style.Restrn01DrawsNothingWhenRestrnIsAbsent`. Charleston golden re-pinned
  (5056 px, mostly removed `RSRDEF51` marks and dashed magenta boundaries).

**Where:** `Enc/fv_s52_style.cpp` `RestrictionInstructions` (~714), used by both
`RESARE02` and `RESTRN01`.

**What:** When `RESTRN` is absent or unrecognised the function returns `SY(RSRDEF51)`,
plus `LS(DASH,2,CHMGD)` for non-points. `RESTRN01` is attached to 67 lookup rows
(`ACHARE`, cable and pipeline areas, fairways, …) where `RESTRN` is normally absent.

**Effect:** a spurious "undefined restriction" symbol and dashed magenta line on each such
object.

**Fix direction:** `RESTRN01` emits nothing when `RESTRN` is absent. `RESARE02` keeps its
default for a restricted area with an unrecognised value.

**Related:** with a recognised family the function emits `LC(<family>51)` as the boundary
rather than a centred symbol — check against the S-52 procedure when fixing.

---

### A4. Area symbols and text are anchored at the first boundary vertex of every ring
- [x] Done 2026-11-10 — `PointSymbolStyle::anchor` added; the S-52 engine sets
  `LabelAnchor::kCentre` on every `SY` and `TX`/`TE`. `VectorScene::Build` computes
  `SceneItem::centre` once per centred area with `AreaInteriorPoint` (centroid of
  the unclipped outer ring in degrees; if that falls outside the area or in a hole,
  the midpoint of the widest interior span of the parallel through it), and the
  renderer draws the symbol and label there on the outer ring's pass only. Lines
  centre at the part's middle vertex. Nothing is moved on screen to stay visible.
  `RestrictionInstructions` now emits the family's centred `SY` for areas as well
  as its `LC`. `LabelAnchor::kCentre` on an area (MapLibre point labels) now uses
  the same interior point instead of the projected box centre; the OSM Atlanta
  golden is unchanged. Tests `AreaInteriorPoint.*` (4),
  `VectorRenderer.ACentredAreaSymbolIsDrawnOnceAtTheWholeAreasCentre`,
  `…StaysOnTheGroundAsTheMapPans`, `…OffTheCanvasIsNotMovedOnto`. Charleston:
  9856 px, the largest new mark `MAGVAR51` on the magnetic-variation areas.
  Not changed: GeoSym/DNC area point symbols still anchor at the first vertex
  (the Windows placement has not been checked).

**Where:** `fvkit/vector/renderer.cpp` point-symbol branch (~1075) and label branch
(~1113); `fvkit/vector/style.h` (~153, `LabelAnchor::kFirstVertex` default);
`Enc/fv_s52_style.cpp` `StyleFeature` (~1046) builds labels without setting
`label.anchor`.

**What:** `sr.symbol` is placed at `proj_part.front()` for every part. Labels default to
`kFirstVertex`; the `p != 0` skip applies only to `kCentre`.

**Effect:**
- Area `SY` (`ACHARE51`, `CTNARE51`, danger symbols, area-obstruction sounding digits)
  lands on a boundary corner, once for the outer ring and once per hole.
- `TX`/`TE` on areas and lines draws at the first vertex of each part, holes included.
- Line `SY` lands at the start vertex rather than the line's midpoint.

**Fix direction:** S-52 engine sets a centre anchor for area and line results; the
renderer places area symbols and text at one interior point of the unclipped outer ring
and skips `p != 0`.

**Decision (Chris, 2026-11-10):** area symbols (`SY`) and area text (`TX`/`TE`) are fixed
in geographic space, like a printed chart: each is placed at the middle of the area,
computed from the unclipped outer ring, so nothing moves as the map pans or as the screen
clips the area differently. Never at a point chosen from the on-screen part of the area.
A symbol or label whose anchor is off-screen is not drawn.

---

### A5. Area patterns ignore holes
- [x] Done 2026-11-10 — `PlaceOverArea` takes every exact projected ring and
  tests lattice sites even-odd with one sorted crossing list per lattice row
  (O(ring vertices) per row, was per site; same half-open rule, so a ring without
  holes gets the same stamps). `EncVectorSource` orders exterior rings before
  holes from `part_is_hole`, so `parts[0]` is the outer ring. Tests
  `PlaceOverArea.NoStampLandsInAHole`,
  `VectorRenderer.AnAreaPatternIsNotStampedInsideAHole`.

**Where:** `fvkit/vector/renderer.cpp` (~947–968), `PlaceOverArea(proj_part, …)`.

**What:** only the outer ring is passed, so pattern stamps (`DIAMOND1`, marsh, `VEGATN`,
…) are drawn inside holes.

**Related:** `Enc/fv_enc_vector_source.cpp` copies parts without `part_is_hole`. The
renderer currently infers "hole" from `p > 0`; confirm that holds for multi-polygon
features before relying on it.

**Fix direction:** test lattice sites against all rings (even-odd), or clip the stamp pass
to the polygon path. See B4 for the cost side of the same code.

---

### A6. Boundary pens are stroked on the clipped ring
- [x] Done 2026-11-10 — the area branch fills the clipped rings with no pen; every
  ring of an area (outer and holes) is stroked once through the polyline path from
  the unclipped projected ring, closed, with dashes and `LC` patterns placed along
  it. Tests `VectorRenderer.AnAreaClippedByTheCanvasHasNoPenAlongTheCanvasEdge`,
  `VectorRenderer.AHoleOfAFilledAreaIsStrokedOnce`. Charleston: 369 px, 326 on the
  canvas border. Atlanta: 479 px, 309 on the border, the rest building corners the
  rounded clip had cut.

**Where:** `fvkit/vector/renderer.cpp` area branch (~920–942);
`fvkit/canvas/cpu_canvas.cpp` `DrawPolyPolygon` (~288).

**What:** rings are clipped by Sutherland–Hodgman to the canvas rectangle, then
`DrawPolyPolygon(rings, brush, pen)` closes and strokes each clipped ring.

**Effect:**
- The boundary pen is drawn along the canvas edge wherever an area was clipped. On the
  apron texture this shows as lines that appear at the texture border after a pan.
- Dashed pens start at the clip point, so the dash phase shifts between renders.
- Hole rings (`p > 0`) of a filled and stroked area fall through to the
  `else if (sr.stroke.valid)` polyline branch and are stroked twice.

**Fix direction:** fill with the clipped rings and no pen; stroke the unclipped rings
through the polyline path (line clip), once each.

---

### A7. Masked and truncated edges are drawn
- [x] Done 2026-11-10 — `S57Geometry::unstroked_edges` / `VectorFeature::unstroked_edges`
  carry one flag per vertex (edge to the next vertex) for FSPT `MASK` = 1 or
  `USAG` = 3, on lines and areas; `VectorScene::unstroked()` keeps them parallel to
  the scene points (allocated only when some part has one; such parts are not
  simplified). The renderer splits a ring's stroke at hidden edges, starting after
  one so no run breaks at vertex 0; the fill is unchanged. Charleston cells: 508
  features carry hidden edges. Tests `S57Cell.MaskedAndDataLimitEdgesAreFlaggedUnstroked`,
  `S57Cell.UnstrokedEdgeFlagsMatchTheirParts`,
  `VectorRenderer.AnUnstrokedEdgeIsFilledButNotStroked`. Charleston golden: 5567 px
  in 1304 clusters, the largest the cell limit at x = 156.

**Where:** `Enc/fv_s57.cpp` `AssembleGeometry` (~301, "MASK (1 = masked) is ignored").

**What:** `MASK` = 1 edges and `USAG` = 3 (truncated by the data limit) edges are kept in
the ring and later stroked.

**Effect:** area boundaries drawn along cell limits — visible as straight lines where
depth areas, restricted areas and the like meet the cell edge, and doubled where two cells
abut.

**Fix direction:** carry a per-edge "do not stroke" flag through the geometry (the fill
still needs the edge). Depends on A6, since the stroke must be separable from the fill.

---

### A8. Every point symbol rotates with the chart
- [x] Done 2026-11-10 — `PointSymbolStyle::turns_with_chart` (default true). The shared
  default stays: Windows VPF (`Vpf/tile.cpp`) adds `map->actual_rotation()` to every
  GeoSym point symbol. The S-52 engine clears it except for light-sector symbols and an
  attribute rotation (`SY(x,ORIENT)`); a literal angle (the flare's 135) is screen-relative.
  The renderer and `GeoDraw::StampSymbol` honour it. Soundings are labels and were never
  turned. Tests `VectorRendererRotation.AScreenUprightSymbolIgnoresTheChartTurn`,
  `S52Style.OnlyAttributeBearingsTurnWithTheChart`. Not yet checked on screen.

**Where:** `fvkit/vector/renderer.cpp` `SymbolAngleOnChart(deg, chart_rotation)`.

**What:** returns `deg - chart_rotation` whenever the chart is rotated, for all point
symbols including those with rotation 0 and raster tiles. This was a deliberate
shared-renderer choice; it is wrong for S-52, where only `ORIENT`-driven symbols and
sector bearings turn with the chart.

**Effect:** in course-up, buoys, beacons and sounding digits tilt, and raster tiles are
resampled. Area pattern stamps stay upright, so the two disagree.

**Fix direction:** a per-result flag ("rotation is a true bearing") set by the S-52 engine
only when the rotation came from an attribute; the renderer leaves other symbols
screen-upright. Check what GeoSym needs before changing the default.

---

### A9. Further correctness items
- [x] Done 2026-11-10 — `OBSTRN04` area without `VALSOU` always emitted `AC(DEPVS)`
  (`Enc/fv_s52_style.cpp` `Obstrn04`). Now: `CATOBS` 6 → `AP(FOULAR01);LS(DOTT,2,CHBLK)`
  (no fill, so the depth area beneath shows), `WATLEV` 1/2 → `AC(CHBRN);LS(SOLD,2,CSTLN)`,
  4 → `AC(DEPIT);LS(DASH,2,CSTLN)`, otherwise `AC(DEPVS);LS(DOTT,2,CHBLK)`. Reach: in the
  Florida set 268 foul areas and 77 covers-and-uncovers areas were drawn as very shallow
  water; Charleston 2 and 9. `FOULAR01` is raster-only in the delivered library, so the
  foul pattern itself is not drawn (see A10). Test
  `S52Style.AnObstructionAreaWithoutADepthIsFilledByItsWaterLevel`. Not yet checked on screen.
- [x] Done 2026-11-10 — the conversion spec is parsed and formatted on its own, the text
  around it kept. Reach: Florida's 50 `TS_FEB` tidal-stream rates showed without `kn`;
  `CTRPNT` heights without `(m)`. Test `S52Style.TextFormatsKeepTheirUnits`.
  Was: `ApplyTextFormat` (~1026) picks the conversion from `format.back()`. `'%4.1lf kn'`
  and `'%4.1lf (m)'` fall through to the raw attribute value, losing formatting and unit.
  `'%03.0lf deg'` works only because it ends in `g`. Formats in the XML:
  `%03.0lf deg`, `%4.1lf (m)`, `%4.1lf kn`, `%s`, `Ch %s`, `LtV %s`, `Plt %s`, `Prod %s`,
  `bn %s`, `by %s`, `ch %s`, `clr %4.1lf`, `clr cl %4.1lf`, `clr op %4.1lf`,
  `sf clr %4.1lf`, `swept to %5.1lf`, `varn %s`. Fix: find the conversion spec rather than
  the last character.
- [x] Done 2026-11-10 — `SYMINS01`: a `NEWOBJ` draws the instructions in its own
  `SYMINS`. Reach: Florida's 23 virtual AIS aids to navigation (13 cells), e.g.
  `SY(BRTHNO01);SY(BOYLAT23);TX('V-AIS',...)`, were drawn as `QUESMRK1`. A quoted TX
  text is now a literal (`S52Instruction::quoted`); before, it was looked up as an
  attribute. Test `S52Style.NewObjectDrawsItsOwnSymbolInstructions`.
- [ ] Not pursued, no reach in the Charleston or Florida sets: `RESARE01` (its Symbolized
  `ACHARE` row is shadowed by the catch-all row before it, A1), `DEPARE02` (inland
  `depare`, `excnst`) and `TOPMARI1` (inland `topmar`). They draw `QUESMRK1`.
- [x] Done 2026-11-10 — `fv_s52_style.h` header comment lists the registered procedures.
- [x] `fv_enc_vector_source.cpp` drops `part_is_hole` when copying parts — exterior rings now come first (A5).
- [ ] Label repeat/collision state is per renderer, so it is not shared across usage
  bands (`apps/Pippin/PippinKit/PPEncChart.cpp` `Render`); a coarse-band label can overlap
  a fine-band one.

### A10. Documented reductions (known, listed for completeness)
- [ ] `Obstrn04` / `Wrecks02` / `DangerSymbol`: `UDWHAZ` is reduced — any `VALSOU` ≤
  safety contour gets `ISODGR01`, without the test for surrounding safe water. Over-warns.
- [ ] Raster-only area patterns (`FOULAR01`, the foul-area pattern) are not drawn: the AP
  path resolves vector patterns only. B4's tile accepts a pixmap symbol, so what remains
  is the style engine resolving the raster pattern.
- [x] Done 2026-11-10 — `Lights05` light description (`LITDSN01`): `LITCHR`, `SIGGRP`,
  `COLOUR`, `SIGPER`, `HEIGHT`, `VALNMR`, space-separated as on US charts
  ("Fl(2) R 6s 12m 5M"), right of the light, a body height up beside a flare and level
  beside a sector. Drawn only when labels are on; there is no separate text-group 23
  toggle. Reach: 213 Charleston and 6,599 Florida lights state a character.
  Checked on the Charleston lights render. Test `S52Style.LightsCarryTheirDescription`.
- [ ] `Lights05`: flare fixed at 135°, single 20 mm sector
  radius, no directional lights.
- [ ] `TopMar01`: always the buoy (floating) family.
- [x] Done 2026-11-10 — `SNDFRM03` frame symbols: `B1` swept (`TECSOU` 6), `C2` low
  accuracy (`QUASOU` 3, 4, 5, 8, 9), `A1` drying-height bar, `SOUNDS`/`SOUNDG` by the
  safety depth. Hazards' `VALSOU` goes through the same frame. Reach: no drying
  soundings in either set (7,334 Charleston, 314,849 Florida soundings); 28 Florida
  hazards with a negative `VALSOU`; 4 + 10 swept; 3 low-accuracy in Charleston. The
  vector `SOUND*` symbols are exempt from the pivot re-anchor, like the raster digits.
  `QUAPOS` (on the spatial record) is not read. Test
  `S52Style.SoundingFramesMarkDryingSweptAndLowAccuracySoundings`.
- [ ] `LC` strokes with the line style's pen where the along-path placer is not used; `AP`
  as described in the header.
- [ ] `ReanchorIfPivotIsOffTheGlyph` in `Enc/fv_s52_preslib.cpp` moves the pivot for 61
  symbols.

---

## B. Performance

### B1. Coarser usage bands are stacked more often than needed
- [x] Done 2026-11-10 — `BandsForView` stops at the first band whose in-view cell bounds
  together hold the view (`EncCellsCoverRect`: splits the view at every cell edge and
  tests each piece; antimeridian-safe). Bounds only, not `M_COVR` — the index carries no
  coverage polygons, and the single-cell rule it replaces made the same approximation.
  Florida set, 41 draws down the Keys on a 1320×2868 surface: at 1:50000, 85 → 45 bands,
  1832 → 437 ms total, worst draw 243 → 47 ms, footprint 118 → 81 MB; at 1:12000,
  61 → 42 bands, 1097 → 386 ms, worst 290 → 34 ms (`EncChartBench.DISABLED_FloridaFootprint`).
  Tests `EncCellsCoverRect.*`, `EncChart.AViewAcrossTheHarbourCellSeamsDrawsOnlyTheHarbourBand`
  (the Charleston harbour-cell corner drew {4, 5}, now {5}). Not yet checked on screen.

**Where:** `apps/Pippin/PippinKit/PPEncChart.cpp` `BandsForView` (~497), `Render`.

**What:** walks from the wanted band to the coarsest and adds every band that meets the
view, stopping only when a single cell's bounding box contains all four view corners.

**Effect:** a view that straddles two cells of the same band, or sits on irregular
coverage, draws every coarser band in full underneath. Each band has its own source
query, style pass, scene and draw.

**Fix direction:** stop when the union of the band's cell coverage (`M_COVR` polygons, or
at least the union of bounding boxes) contains the view.

---

### B2. Scene rebuilds are frequent and expensive
- [ ] Open

**Where:** `fvkit/vector/scene.cpp` `CanServe`; `Enc/fv_enc_vector_source.cpp` `Query`
(~366); `Enc/fv_s52_style.cpp` `StyleFeature` (~1046); `Enc/fv_s52_preslib.cpp`
`Lookups`, `Build`.

**Trigger:** `CanServe` requires exact equality of scale denominator, DPI, symbol scale
and style epoch, plus the view inside the built area. Any zoom change rebuilds; a pinch
rebuilds repeatedly.

**Cost per rebuild:**
- [ ] `Query` scans every feature of every intersecting cell; there is no spatial index.
- [x] Done 2026-11-10 (not in the original list) — `GeoRect::Intersects`, called per
  feature by `Query`, allocated the antimeridian split of both rects on every call; it now
  compares directly when neither crosses. Charleston pinch profile: 126 → 8 samples,
  `Query`'s work outside cell loading roughly halved.
- [ ] It builds an acronym string per feature and per attribute (catalogue lookup + copy)
  and deep-copies all geometry.
- [ ] It scans all vertices of every feature for `has_depth`, and parses `SCAMIN` with
  `strtod` per feature.
- [ ] Soundings are exploded into one point `VectorFeature` each, with a `DEPTH`
  pseudo-attribute formatted `"%.1f"`, each producing 2–3 `SY` results.
- [ ] Lazily loaded cells are parsed synchronously inside `Query` (`Ensure`), on the
  render path. `max_resident_cells_` defaults to 32.
- [x] Done 2026-11-10 — `StyleFeature` walks `row->instructions` in place, with CS output
  as a second frame (a CS inside CS output is skipped); `Lookups()` returns a const
  reference into a per-table index resolved at load.
- [ ] CS output is built as a string and re-parsed with `ParseS52Instructions` per
  feature. Deferred: 10 of ~2,800 render samples in the Charleston pinch profile, against
  rewriting the twelve procedures' ~50 emission points.
- [x] Done 2026-11-10 — `Build()` allocated the cache key `active_table + "\x1f" + kind +
  "\x1f" + name` on every `Symbol`/`LineStyle`/`Pattern` call. The caches (and the raster
  `Bitmap` cache) are now per colour table, keyed by name, the active table's resolved on
  `SetActiveColorTable`.

**Measured 2026-11-10** (`EncChartBench.DISABLED_CharlestonPinch`, 60 rebuilds over resident
cells, phone surface, CPU canvas): scene build is a third of render samples, half of it
first-touch work (cell parsing, raster sheet decode); per frame, `Query` is ~8% and
styling ~3%, and the rest is drawing. The cheap
style items above are within noise on a whole frame (5.46 → 5.36 CPU ms); the
`Intersects` fix is the measurable one. Cell parsing (`Ensure`) and the drawing itself
dominate.

**Fix direction, cheapest first:** return lookups and instructions by const reference;
have CS procedures emit instruction structs; key the symbol cache on an interned id;
pre-resolve acronyms and `SCAMIN` at cell load; add a per-cell grid or R-tree; let
`CanServe` accept a scale band rather than an exact scale.

---

### B3. Every render projects and clips the whole scene
- [x] Screen cull done; the rest not pursued (see Measured)

**Where:** `fvkit/vector/renderer.cpp` `Render` (~807–1274).

**What:** every part of every scene item is projected (`GeoToSurface` per vertex) and
clipped on each render. There is no per-item bounding-box cull. The scene keeps a 0.25
margin, and geometry is stored once per `StyleResult`, so a feature with several results
is projected several times. `vector.simplify_pixels` defaults to 0.0 (`PPMap.mm` ~945),
so full vertex counts are used at every scale.

**Fix direction:** store a geographic bounding box per item and cull before projecting;
share geometry between a feature's results; cache projected geometry while the viewport
is unchanged. Turning simplification on needs care — it runs per ring, so adjacent areas
would open slivers.

**Screen cull done (2026-11-10):** `VectorScene::part_bounds()` holds a geographic box per
part. Under an affine projection `Render` projects the box's corners and skips the part
when the box, grown by the style's ink reach (`VectorRenderer::InkReach`: symbol extent,
label text, pen), misses the canvas. Pixel-exact: `S52Render.TheScreenCullLeavesThePictureUnchanged`
compares cull on and off over Charleston at three rotations. Not culled: non-affine
projections, a part whose box is not within 180° of the centre, a symbol whose library
states no extent, and an along-path label while label collision is on (an off-canvas run
still takes discs that can hide a run on the canvas).

**Measured** (`EncChartBench.DISABLED_CharlestonCourseUp`: 60 draws up the harbour at
1:12000, 1320×2868, turning 2.5° a draw, scene margin 0.25): 48 draws reuse the scene and
12 rebuild it. Reused-scene draw 6.83 → 6.47 CPU ms, rebuild 8.1 → 7.9; the finest band's
draw calls fall 39,735 → 29,687 and over half of its parts are skipped (e.g. 448 of 760).
The gain is small because the band holds only ~20,000 vertices: projecting and clipping
them was never the bulk of a draw, rasterizing the visible fills is. Sharing geometry
between a feature's results and caching projected geometry are therefore not pursued;
the next cost to look at is the fill itself.

---

### B4. Area pattern fills
- [x] Done 2026-11-11 — one period of the pattern is drawn to a tile
  (`PatternTile`, cached in the renderer until the style epoch changes) and the area is
  filled with it by `ICanvas::FillPolyPolygonTiled`, in the same scanline pass as the solid
  fill. Cells are whole pixels. The pattern is now clipped to the area, where whole stamps
  used to overrun the boundary and appear or vanish as their site crossed it. A canvas
  without a tiled fill still gets stamps (`PlaceOverArea`).
  Also fixes the crawl on pan: a staggered pattern (fill type `S`: marsh, `DIAMOND1`)
  chose its offset rows by counting from the first row on the canvas, so the half-cell
  offset flipped whenever an area's top, or the canvas edge across an area, crossed a row.
  The tile holds both rows and the lattice anchor moves by whole two-row periods.
  Measured with `S52RenderBench.DISABLED_AreaPatternTileAgainstStamps` (Charleston
  1:50000, `DIAMOND1` on, 1320x2868): tiled 18.5 ms, stamped 18.6 ms at phone density;
  29.0 against 29.4 at desktop density. The Charleston cells hold too few pattern sites
  for the stamps to cost anything, so this is not a speed-up there; the cost no longer
  grows with the number of sites.

**Where:** `fvkit/vector/renderer.cpp` `PlaceOverArea`, `PointInRing`,
`kMaxPatternStamps = 20000`.

**What:** each lattice site runs `PointInRing`, which is O(ring vertices). Each accepted
site is a full `DrawResolvedSymbol`; there is no cached pattern tile.

**Effect:** a large depth area with a detailed coastline and `DIAMOND1` on is the worst
case: sites × vertices tests, then up to 20,000 vector stamps.

**Fix direction:** render the pattern cell once to a tile and fill the polygon path with
it as a tiled brush.

**Partly addressed (A5, 2026-11-10):** membership is now a per-row scanline, so the
sites × vertices cost is gone; the per-stamp `DrawResolvedSymbol` remains.

---

### B5. Thread safety of caches and counters
- [ ] Open

**Where:** `Enc/fv_s52_style.cpp` (`impl_->unhandled_cs`, counters);
`Enc/fv_s52_preslib.cpp` `Build` cache.

**What:** only `light_symbols` holds a mutex. The diagnostics map, the counters and the
presentation-library cache are mutated without one. Not a problem while styling is
single-threaded; it blocks moving scene builds off the render thread (the natural fix for
B2's stalls).

---

## C. Suggested order

Done: A1–A8, B1, B2 cheap items, A9's `OBSTRN04`, `ApplyTextFormat`, `SYMINS01` and
header comment, and items 1–3 below (2026-11-10).

**Ranking rule (2026-11-10):** correctness before performance, and correctness items by
how wrong the information shown to the user is, not by how much of the chart they touch.
An item that shows a plausible but wrong value ranks above one that leaves something out,
which ranks above one that only looks different. Count the item's reach in the Charleston
and Florida sets before changing code.

Correctness:

1. ~~`Soundg02` drying heights (A10).~~ Done.
2. ~~Unimplemented CS procedures (A9).~~ `SYMINS01` done; the other three have no reach.
3. ~~`Lights05` light description text (A10).~~ Done.
4. `UDWHAZ` reduction (A10). Errs toward warning: more danger symbols than a full ECDIS,
   never fewer.
5. Lower stakes: `TopMar01` buoy family, raster-only area patterns (`FOULAR01`; shares
   B4's tiled-brush mechanism), label repeat state across bands, the remaining A10
   reductions.

Performance, aimed at the moving map (fixed scale, ship-centred, north- or course-up):

6. A moving-map bench: replay a track through Charleston harbour at 1:12000 in
   course-up under the app's base-cache rules (`PPBaseCoverage.h`, 2.5° turn limit),
   counting base redraws and scene rebuilds and timing each.
7. B3 — done as a per-part screen cull (5% of a reused-scene draw). Course-up still
   redraws on every 2.5° of heading change; the remaining cost is rasterizing.
8. Cell loading on the render path (B2 `Ensure`) — a stall as the ship enters new cells;
   moving it off the render thread needs B5 first.
9. ~~B4 tiled pattern brush~~ done; B2 spatial index (pays only at rebuilds).

Not pursued: letting `CanServe` accept a scale band. It only helps a pinch or keyboard
zoom, which is not worth optimizing (Chris, 2026-11-10).
