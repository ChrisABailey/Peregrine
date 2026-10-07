# The Contour Lines overlay — plan (C1–C6)

Ported from `Applications/FalconView/Contour` (3.0k lines: `contour.cpp`,
`ContourLists.cpp`, `ContourTile.cpp`, `ContourPoint.h`, `contour_pp.cpp`).
Tier 1 of the ledger's **OVL-1..n** queue. Read `port/PORTING.md` §1a-bis (the shared overlay
toolkit) first; this plan only records what is specific to contours.

The rule the graticule session set applies here: **read the Windows overlay for its DECISIONS and
throw away its machinery.** What follows separates the two explicitly, because for this overlay the
split is unusually stark — several of the "decisions" turn out to be dead code.

---

## 0. What the Windows overlay actually does (read once, so nobody re-reads it)

`C_contour_ovl::draw` →
1. refuse to draw at all when the map is smaller-scale than `DisplayThreshold` (default 1:250 K);
2. carve the viewport into a fixed geographic lattice of tiles (0.2° / 0.1° / 0.05° for DTED
   level 1 / 2 / 3) and load each tile's elevation posts through COM `IDted::GetBlockDTED`;
3. sample at `max(4 screen pixels, the DTED post spacing)`, re-sampling only when the
   degrees-per-pixel has moved by more than a third (hysteresis);
4. trace contours per tile at `Interval = MajorInterval / Divisions` (default 304.8 m / 5 =
   60.96 m, i.e. 1000 ft and 200 ft), keying each level as `int(level_m * 1000)` — millimetres,
   because the key must be an integer;
5. draw minor levels 1 px, major levels 2 px, one colour (default `RGB(192,0,64)`), and label
   **major levels only**, once per line, at the halfway vertex, with the line **broken** around
   the text.

### Decisions worth keeping

* **The scale threshold.** Contours over a 1:5 M chart are a smear; FalconView refuses, and so
  should we. Same for a second, independent **label** threshold.
* **The fixed geographic tile lattice**, not a screen-shaped block. It is what makes a pan reuse
  the last frame's work, and it is why contours do not writhe as the map moves.
* **Sampling at `max(post spacing, 4 px)`**, with the one-third hysteresis. Sampling finer than
  the posts invents terrain; finer than 4 px pays for ink nobody can see.
* **One post of N/E overlap per tile**, so a contour crossing a tile edge meets its neighbour.
* **Major/minor as line weight, one colour**; labels on major only, line broken for the text.
* The interval is expressed as **major + divisions**, not as a bare interval — that is how a
  cartographer says it, and it is what makes "major" well defined.

### Machinery to throw away

* **`CContourPoint` + `CellID` + the `multimap<CellID,·>` neighbour chase** (`ContourLists.cpp`,
  755 lines). It stores every crossing as a heap object keyed by two cell ids, then rebuilds
  polylines by searching eight neighbouring cells per point and closing loops by scanning a
  stack. Marching squares with a segment-join pass is the same output, linear, and deterministic.
  The original also relies on unsigned wraparound (`CellID(row-1, …)` at `row == 0`) and shadows
  `pCurrent` inside `SortPoints`'s inner loop; neither is worth transliterating.
* **`CContourTile::prepare_for_draw` and `set_edge_thinning`.** `prepare_for_draw` has a body of
  `if (force_redraw) force_redraw = false; return;`, and `m_ThinningLevel` is stored and never
  read. **The thinning level is dead code in the shipped product** — the registry key exists, the
  property page sets it, and nothing has ever thinned anything. C5 gives it a meaning.
* **The interval LADDER `port/PORTING.md` credits this overlay with does not exist.**
  `contour_pp.cpp:358` fills its listbox with `"1:1M\t200\t1000"` three times — a placeholder for a
  per-scale table that was never written. So there is no `ScaleTable` to port *verbatim*; C4 writes
  one, as new work, and says so.
* Registry reads inside the draw (six per frame), `CMap` tile dictionaries, the hourglass cursor,
  the `MAXLONG → -32767` void sentinel that is then traced through as if it were terrain, and the
  `DataSource` radio group (a DTED level picked by hand — our elevation source already prefers the
  finest level it has).

---

## STATUS (2026-08-29)

**C1, C2, C3, C5 and C6 are BUILT.** C4 is unstarted and optional. What the sessions changed
about the plan below, and why, is in the archive's OVL-C row; the two that matter here are that
the TILE SIZE is now chosen from the viewport rather than from the post spacing alone (a
0.2-degree tile at 1:24 K is sixty times the area on screen), and that the label anchor is a
fraction of the line's VISIBLE run rather than of the whole line.

## C1 — the tracer (`fvkit/geo/terrain_contour.h`) — BUILT

Pure, headless, no projection and no canvas: elevations in, polylines out.

```c++
struct ElevationGrid {        // row 0 is the SOUTH row; metres; NaN = void
  GeoRect bounds; int width, height; std::vector<float> meters;
};
Status SampleElevationGrid(IElevationSource&, const GeoRect&, int w, int h, ElevationGrid*);

struct ContourLine { double level_m; int level_index; bool closed;
                     std::vector<GeoPoint> points; };
std::vector<ContourLine> TraceElevationContours(const ElevationGrid&, double interval_m);
```

* **Marching squares**, one pass over the cells; per cell only the levels between its own min and
  max are marched, so a flat cell costs a comparison. Saddles are disambiguated by the cell's mean
  (the standard rule) — FalconView resolved them by whichever neighbour the multimap happened to
  return, which is not a rule at all.
* Crossing positions are the same linear interpolation along the cell edge FalconView used, so a
  contour's *geometry* is faithful even though its assembly is not.
* **A cell with any NaN corner emits nothing.** The original filled voids with `-32767` and traced
  it, hanging a fan of bogus contours off every hole in the data.
* Joining is by edge id (a horizontal or vertical grid edge is an integer), open chains first from
  their free ends, then the remaining rings, marked `closed` — which C3 and C5 both need.
* `level_index` is the integer `level_m / interval` rounded, so "is this major?" is
  `level_index % divisions == 0` — exact, where the original tested
  `((Level + error/2) % MajInterval) > error` with a 5 % fudge because it had thrown the index away.
* **Interface addition, additive with a default**: `IElevationSource::PostSpacingDeg()` returning
  0 for "unknown", answered by `DtedElevationSource` from the open cell. C2's sampler needs the
  post spacing and there is currently no way to ask for it.

Tests: a plane (no closed rings, N parallel lines), a cone (N concentric rings, all closed, radii
pinned), a saddle (the mean rule pinned), a void hole (nothing emitted around it), a real
`testdata/dted` cell (counts and total vertex count pinned).

## C2 — the overlay (`fvkit/overlay/contour_overlay.h`, `fv.contour`) — BUILT

`ContourOverlay : Overlay, app::Properties`. Holds a `shared_ptr<IElevationSource>` which **may be
null** (then it draws nothing) — `VectorMapOverlay`'s precedent, and necessary because a type
registry factory takes no arguments. The shell sets it where it already attaches elevation.

The tile cache is the overlay's only state: keyed on the geographic lattice cell, holding the
traced lines and the sample spacing they were traced at. Dropped when the interval changes, when
the sampling moves by more than a third, and for tiles far outside the viewport.

Properties (`[contour]` in `peregrine.ini`, defaults = FalconView's):

| key | type | default | note |
|---|---|---|---|
| `display_threshold` | double | 250000 | draw only at 1:N or larger scale |
| `label_threshold` | double | 250000 | |
| `major_interval` | double | 1000 | in `interval_unit`s |
| `interval_unit` | choice | feet | feet, meters |
| `divisions` | int 1–10 | 5 | minor interval = major / divisions |
| `line_color` | color | `#C00040` | FalconView's `RGB(192,0,64)` |
| `major_width_px` / `minor_width_px` | int | 2 / 1 | |
| `show_labels` | bool | false | |
| `smoothing` | choice | none | C5 |
| `thinning_px` | double | 0.5 | C5 |

**Storage deviation, deliberate:** the registry kept `MajorInterval` in metres always and converted
for display. Here `major_interval` is in `interval_unit`s, because a human editing
`peregrine.ini` writes `major_interval = 1000` and `interval_unit = feet`. Behaviour and defaults
are unchanged; only the spelling is.

## C3 — labels — BUILT

Major levels only, one per line, at the halfway point, with the line broken for the text — through
`GeoDraw::DrawLabelAlongPath` for the label span and `DrawSurfacePath` for the two remaining
sub-paths, and `LabelPlacer` so two contours' labels cannot land on each other (the original had
no such check and stacked them at every tile edge).

## C4 — the interval ladder FalconView left as a stub — NOT STARTED

A `ScaleTable<ContourInterval>` behind `interval_mode = manual | auto`, default **manual** so the
port's out-of-the-box look is the product's. Auto is what makes contours legible over a street map
without the user reaching for a spinner at every zoom.

## C5 — SMOOTHING — BUILT (Chris, 2026-08-29: the old overlay never did this, and on large-scale
maps the contours look blocky)

Its own step for the reason Chris gave: it is the one thing here that can cost real frame time.

**Where it happens is the decision.** Smoothing runs in SURFACE space, on the projected path, just
before stroking — not on the cached geographic geometry. Three consequences, all of them the reason:

* "Blocky" is a statement about *pixels*, so the pixel is where the error bound belongs;
* zoomed out there are few on-screen vertices, so the cost scales with what is visible rather than
  with the data;
* changing the option, or the zoom, re-smooths without re-tracing anything.

Two passes, in this order:

1. **Decimate** — Douglas–Peucker at `thinning_px` (0.5 px default). At small scale a traced line
   has many sub-pixel vertices; dropping them is free speed, and it is also what finally gives the
   dead `ThinningLevel` key a meaning.
2. **Smooth** — `smoothing = none | chaikin | spline`.
   * `chaikin` (the default once measured): corner cutting, iterated until the segments are under
     ~4 px or 3 iterations, whichever first; periodic on closed rings so the ring has no corner at
     its seam. Approximating, convex-hull bounded, cannot overshoot into a neighbouring contour —
     which matters, because two contours crossing each other is a *wrong map*, not an ugly one.
   * `spline`: centripetal Catmull-Rom, interpolating, for a caller who wants the curve to pass
     through the traced crossings exactly. Centripetal specifically — the uniform parameterisation
     cusps and self-intersects on exactly the tight staircase corners this is here to fix.

**MEASURED** (Release, 1:50 K, 1024x768, warm cache): **0.72 ms/frame with neither, 1.13 with
Chaikin, 1.16 with the spline**, and the thinning is what pays for it — 21,516 traced vertices
became 4,972 stroked at 0.5 px, then 38,760 after Chaikin. A cold frame is the sampling and the
tracing, not the shaping: 14,884 posts and 6 ms at 1:24 K over real DTED.
`ctest -R BenchSmoothing` with `FV_CONTOUR_BENCH=1` re-runs it. If Chaikin at 2 iterations
is not visibly enough, the answer is NOT more iterations — it is that the *terrain* sample is what
is coarse, and the fix is at generation time: refine the elevation grid bicubically by 2–4× before
tracing (cached, no per-frame cost, more vertices to stroke). That fork is stated here so it does
not have to be re-derived at the time.

## C6 — bindings and shells — BUILT

`pyfvw`: nothing new on `Overlay` (properties are already bound there), so this is the class, its
constructor and `set_elevation_source`. PythonView sets the source in `_attach_elevation`, which
already runs on every catalog change. Pippin is optional and not in this plan.
