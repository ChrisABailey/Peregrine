# Map projections — plan (PJ0–PJ6)

Extends `fvkit/proj` from equal-arc only to the five 2D projections FalconView
ships: Mercator, Lambert Conformal Conic, Azimuthal Equidistant and
Orthographic, plus the existing Equal Arc. Source of truth is
`fvw_core/MapDataServer/MapRenderingEngine/proj/` (4.5k lines) and its raster
warp, `graph/projection_dc.cpp`. **Do not extract from
`Applications/FalconView/proj/`**: it is an older copy with no
`AzimEquiProj`, and its signatures take `MapSource`/`MapSeries` where the
fvw_core copy takes a scale denominator.

Read `port/PORTING.md` § "`proj.h` — the projection, and it ROTATES" (PR1–PR3)
first. The rotation work set the rules this plan keeps: gated arithmetic, the
identity path byte-exact, and the raster path as its own branch.

---

## 0. What the Windows engine does (read once, so nobody re-reads it)

### The projectors

`PROJ_create_projector` (`projectors.cpp:1978`) maps a `ProjectionEnum` onto a
`Projector` subclass. Each subclass supplies forward and inverse transforms,
`get_convergence`, bounds, centre validation and the per-projection
parameters. Rotation and zoom live once, in the base class:
`geo_to_surface` is `geo_to_xy` followed by the surface rotation.

| enum | class | file | notes |
|---|---|---|---|
| `EQUALARC_PROJECTION` | `EqualArcProj` | `equalarc.cpp` | what `fvkit/proj` already is |
| `MERCATOR_PROJECTION` | `MercatorProj` | `mercator.cpp` | `MERCATOR_MAX_LAT` 80° |
| `LAMBERT_PROJECTION` | `LambertProj` | `lambert.cpp` | parallels chosen per viewport, see below |
| `AZIMUTHAL_EQUIDISTANT_PROJECTION` | `AzimEquiProj` | `azim_equi.cpp` | also the default for the polar vsurfaces |
| `ORTHOGRAPHIC_PROJECTION` | `OrthoProj` | `ortho.cpp` | far hemisphere is not projectable |
| `GENERAL_PERSPECTIVE_PROJECTION` | `EqualArcProj` | — | the 3D view; see §4 |

All five are **spherical**. The radius is `WGS84_a_METERS`, and ground metres
go to pixels through `m_mosaic_meters_per_pixel_{lat,lon}`, which is derived
from the equal-arc dpp at the centre. The projectors do not use an ellipsoid,
so they must not be replaced with GEOTRANS calls. GEOTRANS is used only as a
test oracle (§3).

**The Lambert parallels move with the viewport**
(`initialize_projection_specific_parameters`, `lambert.cpp:433`):

- At `WORLD_OVERVIEW`: φ1 = centre latitude, φ2 = ±30°.
- Otherwise: φ1,2 = centre ± (surface height × dpp_lat) / 3.
- Parallels are pulled off the poles to ±89.999999, and the other parallel is
  reflected about the centre.
- Within one pixel of the equator (|centre lat| < dpp_lat), Lambert switches
  to the **Mercator equations**, because n → 0 there.

So a pan or zoom re-derives the projection's constants. This is correct: it
keeps distortion lowest on the visible map. It also means the constants are a
function of (centre, scale, surface size) and are never set by the caller.

### The raster warp

Map data is never drawn in the display projection. It happens in two stages:

1. Readers draw into a **virtual surface** in the data's own frame.
   `Projector` binds three of them: `EQUALARC_VSURFACE`,
   `NORTH_POLAR_VSURFACE` and `SOUTH_POLAR_VSURFACE`
   (`projectors.cpp:95–117`).
2. `gra_projection_dc::project_image_hlpr` (`projection_dc.cpp:542`) fills
   the screen **backwards**. For each screen pixel, `surface_to_vsurface` goes
   screen → geo (inverse display projection) → vsurface pixel. The result is
   sampled nearest-neighbour, and `BITMAP_BACKGROUND_COLOR` is left
   transparent. It stays fast through **adaptive bilinear subdivision**:
   - Map the four corners of a rectangle exactly, and map its centre exactly.
   - If bilinear interpolation of the corners lands within **0.5 px** of the
     true centre on both axes, fill the rectangle incrementally (two adds per
     pixel, plus a second-difference term per row).
   - Otherwise split into quadrants and recurse.
   - Below **8×8**, map every pixel exactly.
   - A corner that fails to project (off the globe) forces a split.
   - The FPU is forced to round-to-nearest for the `DOUBLE2INT` trick.

   Variants exist for 8-bit palette, 24-bit and alpha masks; the logic is
   identical.

**Vectors skip the vsurface.** Overlays and vector features go straight
through `geo_to_surface` vertex by vertex, and screen-space segments join the
vertices. Windows does not densify long segments.

`geoline_to_surface` (`projectors.cpp:1122`) handles antimeridian wrap. It
only checks for a pole on the surface when the projection is not
Equal Arc or Mercator.

---

## 1. Decisions

**D1 — REWRITE, not SHIM.** The projector math is closed-form and GDI-free.
It becomes standard C++17 in `port/fvkit/proj/`. The fvw_core sources are
read, never modified or compiled into the port. Bounds, validation and
per-projection constants are ported in the same arithmetic order so doubles
match the Windows formulas, which is the bit-faithful bar here.

**D2 — `MapProjection` stays a value type.** `MapEngine` holds it by value
(`engine.h:106`), and so do the bindings and PippinKit. There is no class
hierarchy. Add a `ProjectionType` enum and a small constants struct, and
switch on the type inside `GeoToSurface`/`SurfaceToGeo`. **The equal-arc
branch is today's code unchanged and is gated first, before any new
arithmetic.** This is the same rule as PR1: every golden in the tree is
equal-arc, and none of them may move.

**D3 — No virtual surface image.** Windows needed one because its readers
drew with GDI in their native frame. `IRasterSource` already exposes
`GeoToPixel`, so the port goes screen → geo → source pixel in one resample
rather than two. What gets ported from `projection_dc.cpp` is the **adaptive
bilinear subdivision**, with the Windows thresholds (0.5 px, 8×8), as the
engine's third raster branch. The polar vsurfaces are not ported as images
either. A polar CADRG frame becomes a source whose `GeoToPixel` is polar,
which is a separate CADRG item (see §5).

**D4 — Unprojectable points get their own status.** An orthographic point on
the far hemisphere, or a Mercator latitude beyond ±80°, is not an error in
the caller's input. Add `kNotProjectable` to `StatusCode` (approved by Chris
2026-09-22; record it in `fvkit-contracts.md` D3 when it lands). Callers that
draw treat it as "skip this vertex / pixel"; callers that pick treat it as a
miss.

**D5 — `IsAffine()` is the gate every consumer asks.** It is true for Equal
Arc at any rotation. Code that relies on linearity asks this question rather
than testing the projection type, so a future affine projection costs nothing.

**D6 — Local scale is taken at the centre, as Windows does.**
`DegPerPixelLat/Lon()` keep their meaning: the equal-arc dpp at the centre,
which is also what the Windows projectors derive their metres-per-pixel from.
Consumers that need scale **at a point** (§PJ5) get a new
`LocalScaleAt(GeoPoint) → {m_per_px_x, m_per_px_y, convergence_deg}`.

**D7 — Projected rendering is not pixel-compatible with Windows** (Chris,
2026-09-23). The bit-faithful rule does not apply to the non-affine raster
path or to the projectors' rendering output: correctness wins over matching
the GDI pixels. The formulas are still ported in Windows' arithmetic order
(D1) and still checked against GEOTRANS. A Windows rendering defect gets
fixed and recorded here. The first one: the adaptive warp's linearity test
checked only the centre, and a distortion odd about the centre (Mercator
latitude across the equator) passed it and was filled from the corners, 9+ px
wrong. `AdaptiveWarp` now also checks the four quarter points.

---

## 2. Phases

### PJ0 — The seam, with no behaviour change
- `ProjectionType { kEqualArc, kMercator, kLambert, kAzimuthalEquidistant,
  kOrthographic }`, `SetProjectionType()`, `ProjectionType()`, `IsAffine()`.
- Private constants struct: centre-for-calculations, mosaic m/px, Lambert n,
  F, ρ0 and the Mercator-fallback flag. `Update()` recomputes it whenever
  centre, scale or surface changes.
- `kNotProjectable` (D4).
- **Acceptance:** the full ctest suite passes with **zero golden diffs**, and
  a new test asserts that equal-arc `GeoToSurface`/`SurfaceToGeo` are
  bit-identical to a copy of the pre-PJ0 functions over a grid of points and
  rotations.

### PJ1 — The adaptive raster branch

**BUILT 2026-09-23** — see `port/PORTING.md`'s `port/projection-plan.md` row for the deviations
and the open linearity-test quirk.

- `MapEngine::CompositeRowProjected`, taken when `!proj_.IsAffine()`. It
  ports `project_image_hlpr` (the 32-bit variant, since `PixelBuffer` is
  RGBA). The per-pixel mapping is `SurfaceToGeo` then `src.GeoToPixel`.
- The target box is the bounds of the frame's overlap, projected by walking
  its edges (a projected rectangle is not a quad). It is masked like
  `CompositeRowTurned`, not clamped.
- Rounding: the Windows `_controlfp_s` + `DOUBLE2INT` trick rounds ties to
  **even**. The port uses `std::nearbyint` under the default `FE_TONEAREST`,
  which matches it exactly. `std::lround` rounds ties away from zero and
  would differ on exact halves. A test pins a tie in each direction.
- **Acceptance:**
  - Against a brute-force exact per-pixel render of the same frame, the
    adaptive render differs only on pixels whose exact source coordinate is
    within 0.5 px of a rounding boundary (the Windows error budget).
  - Rotation 0 and equal-arc goldens are unchanged.

PJ1 is testable before any real projection exists. Drive it with equal-arc
plus a flag that forces the branch, and compare with the affine path.

### PJ2 — Mercator
- Port `MercatorProj`: forward, inverse, bounds, `validate_center`, and the
  80° limit returning `kNotProjectable`.
- It is the easiest first projection because it is separable: meridians and
  parallels stay axis-aligned, so vector bugs show up as scale errors, not
  as shear.
- **Acceptance:** GEOTRANS oracle agreement (§3), round trip, and one raster
  golden and one vector golden on the Charleston fixture.

### PJ3 — Lambert Conformal Conic — BUILT 2026-09-23
- Port `LambertProj`: the per-viewport parallel rule, the WORLD_OVERVIEW
  case, the pole clamps and the near-equator Mercator fallback.
- Add `LocalScaleAt`'s convergence term: n × Δlon. This is the first
  projection where north is not up.
- **Acceptance:**
  - GEOTRANS oracle with the same two parallels.
  - A test that pans across the equator and asserts the switch to the
    Mercator equations happens within one pixel of the equator.
  - Goldens on the Lambert chart already in the tree
    (`PORTING.md` § the 17951×12354 LZW palette file). Drawn in Lambert, it
    should need no warp beyond round-off.

**What landed, and where it left the plan text.**

- **The WORLD_OVERVIEW rule is not ported, because it is unreachable.**
  `validate_center` sets the centre latitude to 0 at WORLD_OVERVIEW, and
  `initialize_projection_specific_parameters` then takes the near-equator
  branch and returns before the parallels are read. So the φ2 = ±30 rule and
  the `calc_meters_per_pixel` override are dead code in Windows, and
  WORLD_OVERVIEW is a sentinel scale denominator the port has no equivalent
  of. A world Lambert in the port reaches the same place by the same door:
  the centre is on the equator, so the Mercator equations draw it.
- **The oracle is the sec+tan form, not GEOTRANS** — same finding as PJ2, and
  it applies to PJ4 as well. Added to it: the scale factor is measured
  against ground distances rather than against either formula, which pins n
  and F together (it is exactly 1 on both standard parallels, above 1
  outside them and below 1 between them).
- **`LocalScaleAt` reports a SIGNED convergence**, where
  `LambertProj::get_convergence` cannot: it takes the delta through
  `GEO_delta_lon`, which returns an absolute value, so Windows cannot say
  which side of the centre meridian the point is on. No Windows caller is
  being matched, and a caller turning a symbol needs the sign.
- **The bounds walk the whole surface boundary.** Windows samples six points
  — three on each of the top and bottom edges, longitude from the two corners
  of the pole-ward edge — which is enough only when north is up. The port
  keeps the pole test (a pole on the surface opens longitude to the full
  circle and sets one latitude bound) and replaces the six points with 64
  steps per edge, which reproduces the Windows box at rotation 0.
- **The Lambert chart golden was not drawn.** In its place, PJ1's and PJ2's
  per-pixel brute-force comparison runs in Lambert too
  (`EngineProjected.LambertMatchesBruteForceRender`, both source shapes, at
  rotation 0 and 30), which is the same acceptance the earlier phases settled
  on and is stronger for correctness than a picture.
- **§6 world scale is now checked for both projected types**, closing PJ2's
  carry: `EngineWorld.TheProjectedPathWrapsToo` and
  `AWorldLambertFallsBackToMercatorAndWrapsToo` put a view 350° wide through
  the projected compositor and assert the same frames and source columns the
  equal-arc path reads.
- **Still open from PJ2's carry:** the visual Charleston golden (the
  brute-force comparison stands in for it) and a TIROS-data world render, as
  opposed to the synthetic hemisphere frames the world tests use.

### PJ4 — Azimuthal Equidistant and Orthographic
- Port both. The new problems are geometric, not numeric:
  - A pole can be on screen, so `VmapBounds` must return a full-longitude
    cap. Port each class's `calculate_map_bounds` rather than deriving
    bounds from corners.
  - Orthographic's far hemisphere is `kNotProjectable`. Its disc edge is
    where PJ1's "corner fails → split" rule does the work.
  - `geoline_to_surface`'s pole-on-surface check becomes a projection
    property.
- **Acceptance:**
  - GEOTRANS oracle for both.
  - A north-pole-centred AzEq render showing a full-circle graticule.
  - An orthographic render of the whole globe where every pixel outside the
    disc stays at alpha 0.

### PJ5 — Consumers that assume linearity — BUILT 2026-09-23

Every site below has its outcome. `IsAffine()` gates each change, so the
equal-arc path executes the terms it executed before and no golden moved.

| site | assumption | outcome |
|---|---|---|
| `vector/renderer.cpp` scene `dpp_x/dpp_y` | simplification tolerance | **stays** (centre scale) |
| `vector/renderer.cpp` area-pattern seed | lat/lon 0,0 by linear formula | **gated**: `GeoToSurface({0,0})` when not affine, surface centre if it has no image. `PatternAnchor` was already projection-agnostic, so the seed only fixes which lattice the first frame picks |
| `vector/renderer.cpp` label metres/px | uniform ground scale | **`LocalScaleAt`** at the feature's first vertex |
| `canvas/geo_draw.cpp` `DrawLabelAtPixel` / `DrawLabelAlongPath` metres/px | uniform ground scale | **`LocalScaleAt`** at the anchor pixel (inverted through `SurfaceToGeo`); the path form measures at the start of its first run, because a label is one size along its length |
| `SymbolAngleOnChart` (`geo_draw.cpp`, `renderer.cpp`) | only rotation turns a north-up symbol | **convergence at the anchor**: the turn taken out is `rotation - convergence`. In `GeoDraw` this lives in `DrawSymbol`, NOT in `StampSymbol` — `DrawSymbolAtPixel` passes 0 deliberately and a pixel-anchored symbol keeps the caller's own angle |
| `geo/contour.cpp`, `geo/elevation_tiling.cpp` | sampling lattice pitch | **stays** (centre scale) |
| `nav/camera_slew.cpp` | pan distance in px | **stays** |
| `overlay/moving_map_overlay.cpp` heading | dpp aspect for the heading line | **`LocalScaleAt` at the ship**, for both the resolver's aspect and MM2's convergence term |
| `RouteKit/fv_road_graph_overlay.cpp` | pick tolerance box | **stays** |
| `scale_table.cpp` | scale from dpp | **stays** |
| TA-mask builder (`overlay/ta_mask_overlay.cpp`) | three `SurfaceToGeo` calls give per-pixel steps | **gated**: one `SurfaceToGeo` per pixel off an affine projection. Not subdivided — the mask reads a nearest post, so a half-pixel tolerance would land on the same posts |
| `grid_overlay.cpp` graticule | straight lines between grid vertices, and a per-point short-way unwrap | **both fixed** — see D8 |

**D8 — the graticule walks unwrapped and subdivides by deflection.**
`ProjectedGridLine` replaces `LinePoints` + `DrawGeoPolyline`: coarse legs of
at most 15 degrees (a midpoint test alone is fooled by a curve symmetric about
its leg, which a Mercator meridian across the equator is), then bisection while
the chord's midpoint is more than 0.5 px from the projected curve, to depth 6,
skipped where both ends are two surface-sizes off screen. Every sample goes
through `GeoToSurfaceUnwrapped` in the viewport's own unwrapped frame, so a view
wider than the world draws each copy of a line in its own place instead of
folding them onto the first. `EntryPoint` and `DrawTicks` were normalizing the
same way and now do not; a meridian's LABEL is still the wrapped value, so the
line at 190 east reads "W 170". Runs break where the projection refuses a point
and where two samples jump more than a surface diagonal, which is the Azimuthal
Equidistant rim.

**D9 — the moving map's convergence is stored in the sense its consumers add.**
`MovingMapOverlay::convergence_deg_` is MINUS
`MapProjection::LocalScale::convergence_deg`. `LocalScaleAt` reports the angle
from true north to grid north, clockwise, and the screen angle of a true bearing
is the bearing LESS that angle — measured on a Lambert chart, a true 62.3786 at
60N/20E draws at 45.0000 with a convergence of 17.3788. `screen_angle_deg` and
the camera's `point_angle` are both MM2's ported `heading + map_rotation +
convergence` and their agreeing is the invariant, so the sign lives at the fill
site rather than in either of them. Windows could not settle this: its
`get_convergence` returns an absolute value through `GEO_delta_lon`, so there
was no sign to preserve (PJ3's deviation (2)).

`HeadingResolver::SetDegPerPixel` takes DEGREES per pixel and divides raw degree
deltas by them, so the local metres per pixel are converted per axis on the way
in — a degree of longitude is shorter than a degree of latitude by cos(lat), and
handing the metres over unconverted cost twelve degrees of bearing at 60 north.

Vector segments stay straight in screen space between projected vertices, as
in Windows (bit-faithful). If long lines look wrong at small scales, that is
a Windows behaviour, and a change there is a separate decision.

### PJ6 — Bindings and shells
- pyfvw: `MapProjection.set_projection_type` / `.projection_type`,
  `.is_affine`, `.local_scale_at`; `MapEngine.set_projection_type`.
- PythonView: a projection menu. This is the demo surface for PJ2–PJ4.
- Settings: `[display] projection =` in `peregrine.ini.sample`.
- `FvFavorite` already parses `map_proj_params.type`
  (`fvw_core/FvConfigFileServer/FvFavorite.cpp`). Wire it through if the
  favorites importer is ported by then; otherwise record it as a follow-up.
- Pippin: **out of scope** (Chris 2026-09-22). It stays equal-arc.

---

## 3. Testing

- **Oracle.** GEOTRANS 3.3 has `lambert`, `mercator`, `azeq` and
  `orthographic` under `third_party/geotrans-3.3/CCS/src/dtcc/CoordinateSystems`.
  Configure it with a **sphere** of radius `WGS84_a_METERS` (flattening 0) so
  it matches the Windows math, then compare to 1e-9 relative on
  projection-plane metres. It is independent code, so agreement proves the
  formulas and not merely the transcription. GEOTRANS stays frozen: it is
  called only from tests.
- **Round trip.** `SurfaceToGeo(GeoToSurface(p)) == p` to 1e-9° over a grid
  per projection and rotation, excluding `kNotProjectable` points.
- **Identity.** PJ0's bit-identity test runs in every later phase.
- **Goldens.** One raster and one vector golden per projection per phase,
  pinned at rotation 0 and at one turned angle.
- **Mutation check.** As PR2 did: break the gate, and confirm an equal-arc
  golden fails.

---

## 4. Out of scope: the 3D view

`GENERAL_PERSPECTIVE_PROJECTION` is not a projector. In Windows it is
osgEarth. `GeospatialScene` builds a geodetic `MapNode`, and
`MapRenderingEngineTileSource::createImage` renders each globe tile through
the 2D engine in **Equal Arc** over the tile's lat/lon box
(`PrepareForRendering` → `set_fixed_size_map_type_from_geo_bounds` →
`Projector::bind_map_to_surface`). The GPU does the perspective.

The port already has the equivalent of `bind_map_to_surface`:
`MapProjection::SetResolution`, which `store/tile_pack.cpp` uses for
Pippin's data pack. A 3D view would be a globe renderer consuming those
tiles, and it would need nothing from this plan.

---

## 5. Related, not in these phases

- **Polar CADRG** is not wanted (Chris 2026-09-22). The `kUnsupported`
  ledger item stays as it is.
- **Projected imagery residual** (the UTM DOQs, `PORTING.md` PR3 note). PJ1's
  branch could also serve an affine projection with non-affine sources, which
  would retire the residual. Decide after PJ1 measures the cost.

---

## 6. Requirement: TIROS at world scale

Chris 2026-09-22: TIROS must render gracefully at scales where the view spans
most of the world, in every projection. The test data is the full global
pyramid (`testdata/tiros3/topobath/`: World 2, 16km 2, 8km 8, 4km 32,
2km 128, 1km 512, 500m 2048 tiles), so a world view reads only a few coarse
tiles, and the work is geometric rather than a matter of I/O.

Not yet checked, so do this first in the session that starts the work:
- **The view wider than 180°.** `MapEngine::CompositeRow` unwraps every
  frame's longitude to within ±180° of the centre (`UnwrapLonNear`). A frame
  near the antipode of the centre can land on the wrong side, or be drawn
  only once when the view covers it twice. `VmapBounds` returns a `GeoRect`,
  which cannot say "all longitudes". Establish what equal-arc does today at
  WORLD scale before adding projections, and fix it there first.
- **The existing `TIROS tile-seam` item** (`PORTING.md` known issues).
  Establish whether it is the same wrap bug or a sampling edge.
- **Per projection:** Mercator clips at ±80° (`kNotProjectable` above it,
  drawn as empty rather than smeared). A world Lambert is the WORLD_OVERVIEW
  parallel rule. AzEq shows the whole sphere, with the antipode as the rim.
  Orthographic shows one hemisphere as a disc. PJ1's subdivision must stay
  bounded when half the surface is unprojectable (every failed corner splits;
  cap the recursion at the 8×8 exact floor).

**Acceptance, added to PJ1–PJ4:** one world-scale TIROS golden per
projection, centred on the antimeridian and on 0°, with no seams, gaps or
duplicated continents, and a frame time within 2× of the same view in
equal-arc.

## 7. Resolved questions

2026-09-22: `kNotProjectable` approved (D4); Pippin stays equal-arc (PJ6);
polar CADRG is not wanted (§5); phase order is as numbered, Lambert (PJ3)
before AzEq/Orthographic (PJ4).
