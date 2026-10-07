# The Analysis tools — plan (AN1–AN7)

Ported from the Windows **Range & Bearing / Analysis Tool** overlay
(`Plugins/LegacyOverlays/RangeBearing`, ~20k lines of MFC/ATL) and the
**Intervisibility** component it calls (`fvw_core/Intervisibility/`,
`Viewshed.cs` 1134 + `GeoPoint.cs` 353 — the one piece of FalconView written
in C#).

Read `port/PORTING.md` §1a-bis (the shared overlay toolkit) and
`port/tamask-plan.md` §0 before AN6; everything before that is pure
computation over `IElevationSource` and needs neither.

**The split Chris asked for**: the *analysis* is C++ objects in FvKit, the
*UI* is Python over `pyfvw`. So AN1–AN4 are headless and unit-tested, AN5 is
the binding seam, and AN6/AN7 are the tk windows and the overlay. Nothing in
AN1–AN4 knows what a chart or a map looks like.

---

## 0. What the Windows overlay actually does (read once, so nobody re-reads it)

Seven object types hang off one abstract `CAnalysisObj` and one overlay
(`CRangeBearing`, `main.cpp` 4225 lines). Everything below the drawing is
arithmetic over two things: `GEO_calc_range_and_bearing` (already ported —
`port/geo_tool`) and an elevation lookup.

| Windows class | file | what it computes |
|---|---|---|
| `CRangeBearingObject` | `RangeBearingObject.cpp` | range + bearing between two points, great-circle or rhumb |
| `CMultiPointRBObj` | `MultiPointRBObj.cpp` | a polyline: per-leg range/bearing and a total |
| `CTotalDistanceObj` | `TotalDistanceObj.cpp` | the sum of the visible R/B objects' ranges |
| `AreaToolObj` | `Area_tool.cpp` | polygon area, by projecting every vertex into a local ENU plane about vertex 0 and running the shoelace |
| `Elevation_Chart` | `Elevation_Chart.cpp` | the terrain profile along a line or polyline |
| `CTerrainMaskObj` | `TerrainMaskObject.cpp` | a full-circle viewshed about a point |
| `CTerrainMaskSectorObj` | `TerrainMaskSectorObject.cpp` | the same, cropped to a bearing ± half a sector angle |

### The terrain profile, and the bug in it

`Elevation_Chart::set_elevation_RB` asks the map server for an **N×N block**
of DTED over the *bounding box* of the two endpoints
(`FVW_IMap::GetBlockDTEDinFeet`) and then reads a **diagonal** out of that
block — four hand-written index expressions, one per quadrant of travel
(`data[i*(n+1)]` for NW→SE and SE→NW, `data[i*(n-1)]` for NE→SW and SW→NE).

That is only the line itself when the line is at 45°, and the stride is wrong
in two of the four quadrants regardless. It also costs N² elevation reads to
produce N samples, and it caps N at 500 for that reason.

**We do not port that.** The profile here walks the *path* and samples
elevation at points on it, which is N reads for N samples, correct at every
bearing, and works unchanged on a polyline or a route. Nothing observable is
lost: the Windows chart's axis, units and segment count all survive.

The multi-point case (`set_range_elevation_multi_point_line`) samples only the
**turning points** — no intermediate terrain at all — so a 200 nm leg over a
ridge charts as a straight line between its ends. Same replacement covers it.

### The viewshed (`Viewshed.cs`)

XDraw, from Franklin's *Geometric Algorithms for Siting of Air Defense Missile
Batteries* (1994). A square lattice centred on the observer, grown one ring
(`boxRadius`) at a time; each new post inherits a **line-of-sight slope** from
the one or two already-computed posts between it and the centre, and stores
back either "visible" (0) or the **height something would have to be here to
be seen** — which is the value the mask actually draws.

Worth keeping, all of it:

* **Three height-calc methods on every post, computed together**: `min` (the
  more pessimistic of the two inherited slopes), `max` (the more optimistic),
  and `interpolated` — a linear blend between the two neighbours weighted by
  where the ray actually crosses the ring. FalconView always asks for
  `interpolated` (`INTERPOLATION_HEIGHT_CALCULATION = 2` in
  `RangeBearing/main.cpp`); the other two are kept because they cost nothing
  once the ring walk exists and they bracket the answer.
* **Earth curvature as a parabolic drop**, `d² / 2R`, `R` = 6 378 135 m
  (`GeoPoint.getElevationDrop`). No refraction term — FalconView models none,
  and adding 4/3-earth here would silently change every answer.
* **Rings walk cardinals, then corners, then the eight wall octants**, and the
  wall posts are the ones that take two parents. The order is not decorative:
  a post's parents must already hold their slopes.
* **The sector crop is applied at two granularities** — `includeDirection` for
  the four cardinals and four corners (± 45° slop) and `includeOctant` for the
  walls (± 22.5°) — and a crop of ≥ 270° is treated as no crop at all.
* **Step size comes from the map scale**, `GetDegreesPerPixel` at the
  observer's latitude (`port/MapScaleUtil` is ported), and the span is capped
  by a registry value `MaxDTEDPoints`; over the cap the step is *widened*
  rather than the range being cut.
* **The output array is row-major, north-west first**, and the caller
  (`main.cpp` `OnViewshedComplete`) hands it straight to an alpha-blended
  pixmap with the bounds the call returns.

Not worth keeping: the COM-per-post elevation lookup (the file's own header
comment lists it as a defect), the `c:\intervis.txt` debug dump, `SAFEARRAY`,
and the `Registry.LocalMachine` read.

### Decisions worth keeping from the overlay shell

* **Great-circle vs rhumb is a per-object property**, not a global. Every
  `GEO_calc_range_and_bearing` call in the tree passes it through.
* **The area tool's local-plane projection**: bearings and ranges from vertex
  0, `x = r sin θ`, `y = r cos θ`, shoelace, halve, absolute value. It is
  accurate for the sizes a user drags out and it is what the numbers in
  existing saved overlays were produced with.
* **Range limits**: the overlay refuses a viewshed whose range is 0 or whose
  centre has no elevation data (`IsElevationDataAvailable`), and reports out
  of memory as "reduce the range" rather than as a crash.
* **Progress + cancel**, `IViewshedCallback::StatusUpdate(percent, &cancel)`,
  fired only when the integer percent *changes*. A viewshed is the one
  computation in this overlay that takes long enough to need it.

---

## 1. The steps

Each step is one session. AN1 and AN2 can share one if AN1 stays small.

### AN1 — `fvkit/analysis/path.h`: the geographic path — **BUILT 2026-09-02**

The spine everything else stands on, and the thing that makes "the elevation
profile of a route" a two-line call rather than a port of the chart dialog.

* `GeoPath`: an ordered `std::vector<GeoPoint>` plus a `HeadingType`
  (`kGreatCircle` / `kRhumb`).
* Per-leg `range_m` / `bearing_deg` (through `GEO_calc_range_and_bearing`),
  cumulative distance, `TotalLength()`.
* `PointAtDistance(double m)` — interpolates *along the leg's own line type*,
  via `GEO_calc_end_point`, so a great-circle leg's midpoint is on the great
  circle.
* `Densify(double step_m)` and `Resample(int n)` — the two ways the profile
  asks for samples.
* `AreaSquareMeters()` — the shoelace above, ported exactly.

17 gtests (`port/fvkit/analysis/test/analysis_path_test.cpp`): a meridian leg
against 60 nm to the degree, a great-circle midpoint against the rhumb
midpoint at 60 N (60.0 vs past 62, which is the whole reason the line type
lives on the path), an antimeridian leg that is 1 degree wide and not 359,
`Densify` keeping every vertex exactly, `Resample` cutting the corner on a
polyline, and the area of a 1°×1° box at the equator against the closed form.

**Two things the session settled.** `cumulative_` keeps one entry per VERTEX
even when a leg is refused — otherwise every index into it is off by however
many legs failed. And `Resample`'s samples are evenly spaced along the PATH,
which on a polyline is not evenly in a straight line; the test that said
otherwise was the wrong test.

### AN2 — `fvkit/analysis/profile.h`: the terrain profile — **BUILT 2026-09-02**

* `TerrainProfile::Sample(IElevationSource&, const GeoPath&, options)` →
  `ProfileResult { std::vector<ProfilePoint>, min_m, max_m, gain_m, loss_m,
  size_t no_data_count }`, where `ProfilePoint` is
  `{ GeoPoint at; double distance_m; float elevation_m; bool has_data; }`.
* Options: either `sample_count` (the Windows "segments" box, 3–500) or
  `step_m`; plus "clamp the step to the source's native `PostSpacing`", the
  same rule `ElevationTiling` already applies — sampling a profile finer than
  the posts invents terrain.
* **Void posts are NaN and `has_data == false`, and the profile still
  returns.** The Windows chart pops "No data available." and refuses to draw
  anything, which is the wrong behaviour for a 300 nm route with one hole in
  it. The count comes back so a UI can say so.
* Turning points are *always* samples, whatever the step — the polyline
  profile has to show the vertices.

13 gtests (`analysis_profile_test.cpp`) over four synthetic sources — a ramp
(elevation is a closed-form function of latitude), a ridge, a source with a
void band, and one with no coverage at all. Two of them exist for the bug
above: an EAST-WEST line has a degenerate bounding box and so no diagonal to
read, and a ridge between two turning points is invisible to a profile that
samples only the vertices. Pinned: **11 samples cost 11 elevation reads.**

The step clamp takes the FINER of the source's two post spacings, because the
spacing along an arbitrary bearing lies between them and a DTED cell thins its
east-west posts toward the poles.

### AN3 — `fvkit/analysis/viewshed.h`: XDraw intervisibility — **BUILT 2026-09-02**

A faithful port of `Viewshed.cs` + the parts of `GeoPoint.cs` it uses, with
COM, the registry and the debug dump taken out.

* `ViewshedRequest { GeoPoint observer; double observer_height_m; double
  range_m; double step_deg; HeightMethod method; int max_posts; std::optional<Sector> crop; }`
  where `Sector { double bearing_deg, angle_deg; }`.
* `ViewshedResult { std::vector<float> visible_height_m; int span; GeoRect
  bounds; }` — row-major, north-west first, `NaN` for a post whose elevation
  was unknown, `0` for "visible".
* `IViewshedProgress { virtual bool OnProgress(int percent) = 0; }` — return
  false to cancel; called only when the integer percent moves, as upstream.
* Curvature drop, the ring walk, the two `setupPoint` overloads, the three
  `determineVisibility*` and the sector predicates all port straight across.
  `calculateCoordinate`'s sign-flipped longitude convention (the Aviation
  Formulary one) is a trap — port it *with* its flip rather than "fixing" it.

15 gtests (`analysis_viewshed_test.cpp`), and they can be exact because **the
due-north column of the lattice is a 1-D chain**: post k is stepped straight
up the meridian, its only parent is post k-1, and `d_k = k · step · 60 · 1852`
metres to the last digit. So the whole recurrence is written out in the test
file and checked term by term.

* **The horizon.** Over flat ground the recurrence reduces to *post k is
  visible ⟺ k(k−1)·drop(one step) < observer height*, which puts the last
  visible post at d ≈ √(2Rh). Pinned exactly, and cross-checked against the
  textbook 5.05 km for a 2 m eye.
* **The shadow behind a wall**, against the closed form
  `los·d + drop(d) + h` with `los = (W − drop(d_wall) − h)/d_wall`, to 1
  part in 10 000 at every post behind it.
* **A void post passes the ray through.** The shadow behind a wall is the
  same shadow whether or not a hole in the coverage lies in it — see below.
* `min ≤ interpolated ≤ max` at every post, plus a flat-ground case where all
  three agree (nothing occludes, so there is nothing to bracket).
* A 90° sector leaves due south unanswered **and saves the reads**; 270° is
  no crop; progress only ever moves forward; cancel returns `kInterrupted`
  with an EMPTY result; the post budget widens the step and keeps the range.

**Four things worth not re-deriving.**

1. **A VOID POST PASSES THE PARENT'S SLOPE THROUGH UNCHANGED.** In
   `DetermineVisibility` the test `losRise < elevationChange` is *false*
   against a NaN, so the branch that would re-derive a slope from ground that
   is not there is never taken. The post gets a NaN height of its own and the
   ray carries on as if the hole were not there — which is the right answer,
   is what the C# does by way of a sentinel elevation, and is very easy to
   break by "tidying" the NaN handling. There is a test whose only job is to
   hold it.
2. **The three methods really are bracketed, and here is why.** The
   interpolated slope is `base + (base − nonBase)·theta`, which reads like an
   extrapolation; but for a wall post at ring b and wall step s, theta works
   out to **−s/b**, so the two signs cancel and it is a convex combination
   moving from base toward nonBase. Verified off-line across ~74 000 posts on
   six terrains (gentle to violent, with and without a sector crop and
   voids): zero violations.
3. **The cost is the ARITHMETIC, not the terrain.** A million posts of real
   Georgia DTED and a synthetic constant source both took 171 ms — so no
   elevation cache would have helped. It was four haversines per post: the
   C# recomputes the distance to the observer inside each of the three
   `determineVisibility*` and again inside `setLineOfSight*From`. Hoisting it
   is common-subexpression elimination and nothing else, **checked
   byte-for-byte over a million posts across all three methods and a sector
   crop**, and it took the million-post case from 171 ms to **105 ms**
   (Release; 5 nm 4.1 ms, 10 nm 19.9 ms).
4. **The post budget is what stands where FalconView's out-of-memory dialog
   stood.** Over the cap the span shrinks and the step WIDENS — the range
   asked for is always the range delivered, more coarsely. The default is a
   million posts, about 80 MB.

**One thing deliberately NOT ported**: `MaxDTEDPoints` from
`Registry.LocalMachine`. It is `ViewshedRequest::max_posts`.

### AN4 — `fvkit/analysis/measure.h`: the measurement objects — **BUILT 2026-09-02**

The small ones, together, because they are the same three lines each:
`RangeBearing`, `MultiPointRangeBearing` (per-leg + total), `TotalDistance`,
`Area`. All of them are `GeoPath` + a unit, so this step is mostly the unit
conversion table (`rb::units_t` → feet/yards/metres/km/NM/miles) and the
formatting rules from `RangeBearing/utils.cpp` (`format_range`,
`format_bearing`, magnetic vs true through `GEO_magnetic_variation`).

Built as **one `Measurement` class over a `MeasurementKind`**, because the four
Windows classes differ only in which label they draw — the geodesy under them
is AN1's and was already written five times over there. 16 gtests
(`port/fvkit/analysis/test/analysis_measure_test.cpp`), and they are tests
about **strings**, since the strings are the whole of what this step adds.

**Five things the transcription pinned, each of which reads like a bug and is
not:**

1. **The range decimal ladder is on the MAGNITUDE, not the unit**
   (`get_num_decimal_places`: < 100 → 2, < 1000 → 1, else 0), so one leg is
   `12.34 NM` and `74977 ft`. **But the total-distance and multi-point labels
   bypass the ladder entirely** and are always two places — `74977.10 ft`.
   Both are FalconView's; `FormatRange`'s `decimals` argument is which one.
2. **Mils beat the display format.** `angle_units == ANGLE_MILS` renders mils
   whatever `display_format` says, `%06.1f`, and the **trailing space is
   inside the string** — so the label really is
   ` 0800.0Mils T / 12.34 NM `.
3. **Degrees/minutes and D/M/S truncate at every field.** `(int)degrees`, then
   `modf` on the remainder, so 44.99999° is `044° 59' 59"` and never `045`.
4. **The multi-point object draws a RUNNING TOTAL at each turning point** —
   not the leg's own range and not a bearing (`redraw_dist_str`) — and a leg
   the geodesy refuses draws **no label at all**, because the whole body sits
   inside `if (GEO_calc_range_and_bearing(...) == SUCCESS)`.
5. **The area is FalconView's in metres.** `AreaToolObj::caculate_Area`
   converts every leg into display units *before* the shoelace, so its area
   comes out in unit²; the shoelace is homogeneous of degree two, so
   `area_m² / (metres per unit)²` is the same number and the geometry no
   longer depends on a UI setting. A test pins the equivalence.

**Three deliberate departures**, all small: the degree sign is UTF-8 rather
than the lone CP-1252 `0xB0` byte in the Windows literals; the magnetic epoch
is an argument rather than a `GetSystemTime()` call inside the formatter (a
default-constructed `MagneticEpoch` still means "now"); and the area is
computed in metres as above.

**The one thing to know about magnetic bearings on this side**: `geomag.cpp`
looks for `wmm.dat` under `$FVW_GEODATA_DIR`, **there is no `wmm.dat` anywhere
in the tree**, and the fallback is the built-in **WMM-95** coefficient table.
So every magnetic bearing here is a 1995-epoch declination — a degree or so
out over CONUS today, more elsewhere. The measurement code is right; the model
behind it wants a data file before anyone quotes a magnetic bearing in anger.
The tests are model-independent (they compare against a direct
`GEO_magnetic_variation` call in the same process), so they pin the sign, the
wrap and the fact that the variation is taken at the **start** of the leg,
which is the part that belongs to this file.

### AN5 — `pyfvw.analysis` — **BUILT 2026-09-02**

Bind AN1–AN4. `GeoPath`, `TerrainProfile`, `Viewshed` (with the progress
callback released round the GIL — it is the first pyfvw call long enough to
need it), and the measurement objects. Follow `pyfvw.nav`'s shape.

`port/bindings/pyfvw/pyfvw_analysis.cpp`, its own TU as `pyfvw.nav` is, plus
20 pytests (`test/test_pyfvw_analysis.py`) and README §6. The tests are
**seam** tests on purpose — AN1–AN4's 61 gtests already pin the geodesy, the
sampling, the XDraw recurrence and every label, and repeating them through
pybind11 would only test pybind11.

**Four things the binding had to decide, three of them about the viewshed:**

1. **The GIL is released for the computation and re-taken for the callback.**
   A million posts is ~105 ms of arithmetic; a UI thread that cannot repaint
   for that long is why FalconView grew a progress dialog. The test that pins
   it spins a second Python thread and counts its ticks **with no progress
   callback** — a callback re-acquires the GIL a hundred times and would let
   the other thread run even if the outer release were missing, so it would
   test nothing. Measured: 98 000 ticks during a 28 ms viewshed.
2. **A raising callback cancels; it does not unwind through C++.** Letting a
   `py::error_already_set` propagate out of `OnProgress` would tear through
   the ring walk with the GIL in an unclear state. The adapter catches it,
   returns `false` — the cancel the C++ interface already means something by —
   and the binding re-raises the **original** exception once the stack is
   back. So a `KeyboardInterrupt` stays a `KeyboardInterrupt` rather than
   being laundered into `FvError(INTERRUPTED)`. A callback returning `None` is
   NOT a cancel; requiring `return True` from a one-line progress bar would be
   a trap.
3. **The result is a buffer, not a list.** A million posts as Python floats is
   ~32 MB of boxed doubles; `ViewshedResult` exposes the buffer protocol, so
   `np.asarray(result)` is a zero-copy `(span, span)` float32 view laid out as
   AN3 documents it (row 0 north, column 0 west). The profile needs none of
   this — its cap is 20 000 samples — so it gets `distances_m` /
   `elevations_m`, the two parallel lists AN6's chart actually wants, NaN
   where there is no data.
4. **`GeoPath.points` returns a COPY.** The C++ accessor hands out a const
   reference into the path; a Python list aliasing it would go stale the
   moment anyone assigned `points`. A test holds it.

Also: `pyfvw.INTERRUPTED` and `pyfvw.INTERNAL` are now module constants.
`INTERRUPTED` reaches Python for the first time with this step, because a
cancelled viewshed is the only thing that raises it.

**One thing the binding tests found in AN3 and did not change**: `max_posts` is
an APPROXIMATE budget. The span is `floor(sqrt(max_posts))` rounded **up** to
odd (there must be a centre post to stand on), so a cap of 2500 delivers span
51 and 2601 posts — 4% over. Rounding down would deliver less range than
asked, which is the one thing the budget exists not to do. At the million-post
default the overshoot is 0.2%. The test asserts the honest property rather
than `<= max_posts`.

### AN6 — the profile window in PythonView — **BUILT 2026-09-02**

A tk chart over `pyfvw.analysis.TerrainProfile`: distance across, elevation
up, the segment-count box and the value labels from the Windows dialog, and
units off the app's own settings. Two ways in:

* a two-point or multi-point measurement drawn on the map, and
* **`Route > Elevation profile`** — the thing Chris actually wants — taking
  the path straight off a `pyfvw.route.RouteOverlay`.

Because AN2 samples a `GeoPath`, those are the same call with a different
path, and the route case needs no C++ work at all.

Built as `ProfileWindow` in `port/apps/analysis.py`, both ways in, with
`port/apps/test/test_analysis_ui.py` — the first tests over `port/apps`, run
by ctest as `pythonview_pytest`. Everything in the window that is not tk was
pushed out of it so a test can reach it: `sample_profile`, `elevation_unit`,
`elevation_window`, `_profile_runs`, `route_geo_path`.

ONE BEHAVIOUR IS DELIBERATELY NOT THE WINDOWS CHART: the vertical scale has a
ceiling. `2DLineGraph` stretched the data to the frame however flat it was,
which draws Kiawah — half a per cent — as a mountain range, and a chart that
says "hilly" about level ground is worse than no chart. `elevation_window`
never spans less than `FULL_SCALE_GRADE` (2%) of the path's own length, so
what a rise fills is its grade over 2% — a quarter of the frame at 0.5%, three
per cent of it at Kiawah's 0.06% — and only a 2% climb fills the window. Above
that it fits the data again, because a mountain SHOULD fill the chart. The
rule is a grade, so it is the same in feet and reads the same at any length.

### AN7 — the viewshed overlay — **BUILT 2026-09-02**

The picture: a per-post colour raster alpha-blended over the base map, which
is exactly `ta_mask_overlay.cpp`'s existing path (`port/tamask-plan.md` TA4).
Read that before starting; the differences are that the lattice is the
viewshed's own square about the observer rather than `ElevationTiling`'s, and
that the computation is slow enough to belong on a worker with the progress
callback wired to the overlay's status line.

Plus the editor: drop an observer, drag its range, and for the sector object
drag the two bearing handles.

Built as `AnalysisOverlay` and `ViewshedObserver` in `port/apps/analysis.py`:
the computation on a worker with its progress on the overlay's status line and
its failure reported there rather than raised, and the picture drawn under a
RING BUDGET (`_ring_budget`, `_runs2d`) at a stride taken from the SCREEN
rather than the post count — which is what keeps a redraw in single-digit
milliseconds when the observer's square is a million posts. The tool palette
and its button bar came out of it as `port/apps/toolbar.py`, rendered from
data so a changed state does not rebuild the row.

---

## 2. What this plan does NOT do

* **The `.rbo`/overlay file format.** `save`/`load` in every `CAnalysisObj`
  is a hand-rolled byte blob with a version enum. Nothing reads those files
  on this side yet, and the format is worth revisiting rather than porting.
  If it turns out Chris has saved overlays, it is its own step.
* **`RDAPI32`** (`RDAPI32.h`/`.lib`, linked by the vcxproj) — a binary-only
  radar-propagation library with no source in the tree. Nothing in the two
  tools Chris asked for touches it.
* **The MFC dialogs** (`TerrainMaskPropDlg` 1187 lines, `ViewshedPropDialog`,
  `edit_dlg`, `property`) and the `2DLineGraph`/`GraphObject` chart control
  (1795 lines) — replaced by AN6, not ported.
