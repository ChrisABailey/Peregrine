# The Terrain Avoidance Mask overlay — plan (TA1–TA6)

Ported from `Applications/FalconView/TAMask` (3.8k lines: `TAMask.cpp` 2394,
`TAMaskStatus.cpp` 571, `MaskClipRgn.cpp` 294, `factory.cpp`, plus
`Applications/FalconView/include/TAMask.h`) and the half of
`Applications/FalconView/Contour/ContourLists.cpp` it shares —
`CContourLists::TraceClearanceContours`.

Tier 1 of the ledger's **OVL-1..n** queue, and the second overlay over the
§1a-bis toolkit after `Contour`. Read `port/PORTING.md` §1a-bis first, then
`port/contour-plan.md`; this plan records only what is specific to the mask.

**The ledger was right about the shape of it**: `TraceClearanceContours` *is*
`TraceElevationContours` with three named levels instead of a ladder, and the
tile lattice, the sampling rule and the hysteresis are the contour overlay's,
already built. What is genuinely new here is the **raster** — a per-post
colour mask alpha-blended over the base map — and the **altitude**, which is
the first overlay in the tree whose picture changes because an *aircraft*
moved rather than because the map did.

---

## 0. What the Windows overlay actually does (read once, so nobody re-reads it)

An aircraft is at some altitude MSL. The overlay colours the ground by how
much room is left under it:

| band | condition | default | colour |
|---|---|---|---|
| warning | `elev >= alt - 100 ft` | 100 ft | red |
| caution | `elev >= alt - 300 ft` | 300 ft | yellow |
| OK | `elev >= alt - 500 ft` | 500 ft | green |
| — | below that | | nothing |
| no data | post is void | | magenta `RGB(255,0,128)` |

`C_TAMask_ovl::draw_to_base_map` builds it: load the DTED tiles under the
viewport, classify every post into a palette index (240/241/242/243), assemble
every tile's byte mask into one screen-sized pixmap aligned to a 0.2° lattice,
shift the on-screen sub-rectangle to the pixmap's upper left, and hand it to
`IGraphicsContext::PutPixmapAlphaBlend` with a 5-entry palette and **alpha
128** (`Shading`, 50%). `C_TAMask_ovl::draw` then optionally traces contour
lines at those same three levels (`TraceClearanceContours`) and draws the
highest on-screen post with a pennant icon and its elevation.

Altitude arrives one of two ways: `TestAlt` (a number the user types, default
2500 ft) or `raw_UpdateAltitude` from another overlay — in practice the moving
map, which also hands over a bullseye so the mask can be clipped to a wedge.

### Decisions worth keeping

* **The three named clearances, and their defaults** (100 / 300 / 500 ft,
  red / yellow / green). This is the overlay's entire vocabulary.
* **`>=` at every band, tested from the top down**, so a post exactly on the
  warning level is a warning. `ConvertAltitudeToColor` and the mask loop agree
  on this and so does the tracer.
* **Alpha 128 over the base map.** The mask is an overlay on terrain the
  pilot still needs to read, not a replacement for it.
* **Nearest post, not interpolated** — the mask is blocky on purpose, and
  FalconView goes out of its way (`screen_ll.lon -= half_dted_lonpix`) to
  align the blocks with the posts so a mask square agrees with the cursor
  elevation readout. Ours lands on the same posts by rounding.
* **A separate no-data colour**, and it is drawn whether or not the mask is.
  A hole in the coverage under an aircraft is the most important thing on the
  screen, and `m_MissingData` is checked independently of `m_DrawMask` for
  exactly that reason.
* **Contours OFF by default, mask ON** (`DrawContours` FALSE, `DrawMask`
  TRUE). The lines are the same three levels as the fill.
* **The peak marker**: the highest post *in view*, one only, with its
  elevation. FalconView's own comment says multiple-equal-peak marking was
  removed because flat ground "cause[s] a system near lockup"; the disabled
  code is still there and stays disabled here.
* **Two scale thresholds**, 1:2 M for the mask and 1:500 K for the labels —
  and note they are *coarser* than the contour overlay's 1:250 K, because a
  mask reads at a scale at which contour lines are a smear.
* **The sensitivity dead band** (25 ft): a live altitude that jitters must not
  repaint the map continuously.

### Machinery to throw away

* **The whole screen-mask assembly.** `draw_to_base_map` spends ~200 lines
  concatenating per-tile byte masks into a 0.2°-aligned pixmap, computing a
  sub-rectangle offset, `memcpy`ing that sub-rectangle over the top of the
  same buffer, and handing the result to a stretching blitter. It exists
  because the mask was built in DTED space and had to reach screen space. We
  build it in **screen space directly** — the projection is affine
  (`fvkit/proj.h`), so a scanline walk over the surface steps lat/lon by
  constants and reads the post under each pixel. One buffer, one `DrawPixmap`,
  no alignment arithmetic, and it is correct under rotation for free.
* **The cached per-tile classification** (`m_ContourMask`). Caching the
  *answer* means every altitude change throws every tile away —
  `m_ContoursValid = false` on any of nine `static` old-value comparisons. We
  cache the **elevation** and classify per frame: three comparisons per pixel,
  and a live altitude feed then re-colours at no cost at all. This is why the
  port's `sensitivity` damps a *repaint request* and nothing more.
* **Those `static` locals.** `old_WarnClearance`, `old_Altitude` and seven
  more are function-level statics, so two TA mask overlays in one process
  would invalidate each other's tiles. Not a bug anybody hit, and not one to
  transliterate.
* **`CMaskClipRgn` / `CMaskCircularClipRgn`** (294 lines) — a circular-wedge
  clip driven by the moving map's bullseye, which exists to keep the raster
  path affordable by making it smaller. The raster path here is a scanline
  over pixels that are on the screen anyway. Dropped, and said so out loud
  because it is the one deliberate feature omission in this port.
* **`DrawToVerticalDisplay`**, whose body begins `return SUCCESS;` above
  fifteen lines of unreachable code.
* **`CTAMaskStatus`** (571 lines of MFC dialog), the property page, the
  `IXMLPrefMgr` reads, and the "KLUDGE" block that writes six label settings
  into the registry on construction so that the *contour* code can read them
  back out — `app::Properties` is the whole of that.
* **`DataSource`** (a hand-picked DTED level), for the reason C2 gives.
* **`-32767` as a number.** The original compares against it in five places
  and traces through it in a sixth. A void is NaN here (D4).

---

## STATUS (2026-09-01)

**TA1–TA6 are all BUILT**, in one session, and the plan above survived
contact except in two places worth recording.

**What the plan got wrong, and it was found by DRAWING the thing rather than
by a test.** TA4 said a tile with no data at all paints nothing, and a
per-tile `has_data` flag was enough for that. It is not: **a lattice tile is
not a DTED cell.** The tile size comes off TA1's ladder and is routinely a
whole degree, so a tile straddling the edge of the coverage is PART covered —
the flag says yes, and the uncovered half then takes the no-data colour. The
first real frame (the Georgia test cell at 1:500 K) came out with **85,000
magenta pixels, a quarter of the screen**. The fix is that the coverage mask
is per POST: `SampleElevationGrid` gained an optional `covered` out-parameter
recording whether the source ANSWERED (a void inside coverage is `ok` with
NaN; ground it does not reach is a `kOutOfCoverage` Status, and the grid alone
throws that distinction away). `TAMaskOverlay::TheEdgeOfACoverageIsNotAHoleInIt`
is the regression.

**What the plan under-specified**: the outlines are cut where a ring crosses a
lattice tile, so "three rings" is not a count of anything — four tiles under a
cone give twelve arcs. `DrawStats::contour_levels` (distinct bands that got an
outline) is the number that means something and the tests use it.

**Measured** (Release, the Georgia DTED cell, 700x500, altitude 4000 ft,
outlines on): **cold 20.6 ms, warm 3.9 ms**. The cold frame is one 1-degree
tile at 372 posts a side; the warm one is the raster — 350,000 pixels of
scanline, two adds and three comparisons each. Nothing here is close to
needing work, and the scanline is what makes it so: the affine step means the
warm frame never calls `SurfaceToGeo` at all after the first three corners.

---

## TA1 — the lattice, extracted (`fvkit/geo/elevation_tiling.h`)

The **fifth piece of the §1a-bis toolkit**, and it is an extraction rather
than new work: the geographic tile lattice, the `max(4 px, post spacing)`
sampling rule, the one-third hysteresis, the tile-size ladder, the
antimeridian folding and the post-count clamp all lived in
`contour_overlay.cpp` and are needed here verbatim. `ContourOverlay` is
refactored onto it in the same commit — an extraction nobody re-uses is a
guess, and the contour tests are what prove the move changed nothing.

The payload stays with the caller: contours cache traced lines, the mask
caches elevation posts, and the lattice knows about neither.

## TA2 — three named levels (`TraceElevationContoursAtLevels`)

`TraceElevationContours(grid, interval)` gains a sibling that takes an
explicit sorted level list, with `level_index` the index into it. Marching and
joining are the same functions; only the choice of levels per cell differs
(a binary search over the list instead of `floor(lo/interval) .. ceil(hi/interval)`).
The interval version is untouched, because every pinned contour golden runs
through it.

## TA3 — the overlay (`fvkit/overlay/ta_mask_overlay.h`, `fv.tamask`)

`TAMaskOverlay : Overlay, app::Properties`, holding a `shared_ptr<IElevationSource>`
that may be null, exactly as `ContourOverlay` does and for the same reason.

Properties (`[tamask]` in `peregrine.ini`, defaults = FalconView's):

| key | type | default |
|---|---|---|
| `display_threshold` | double | 2000000 |
| `label_threshold` | double | 500000 |
| `altitude` | double | 2500 (`unit`s MSL) |
| `unit` | choice | feet |
| `warn_clearance` / `caution_clearance` / `ok_clearance` | double | 100 / 300 / 500 |
| `show_warn_level` / `show_caution_level` / `show_ok_level` | bool | true |
| `warn_color` / `caution_color` / `ok_color` | color | red / yellow / green |
| `show_no_data_mask` | bool | true |
| `no_data_color` | color | `#FF0080` |
| `shading` | int 0–100 | 50 (mask alpha) |
| `draw_mask` | bool | true |
| `draw_contours` | bool | false |
| `contour_width_px` | int | 2 |
| `show_peak` | bool | true |
| `show_labels` | bool | true |
| `sensitivity` | double | 25 |
| `max_samples_per_draw` | int | 1000000 |

`SetAltitude(double feet_or_metres)` is the automation seam — the moving map's
`raw_UpdateAltitude`, minus the COM and minus the bullseye. It applies the
sensitivity dead band and reports whether the picture changed.

## TA4 — the raster

Screen-sized RGBA `PixelBuffer`, one `ICanvas::DrawPixmap`. Per scanline the
geographic step is constant (the projection is affine, rotation included), so
the inner loop is two adds, a tile lookup that only misses at a tile boundary,
a `lround` per axis, and three comparisons.

**A tile with no data at all paints nothing.** This is the one place the port
must differ: FalconView only ever held tiles its coverage manager listed, so
"void" always meant a hole *inside* coverage. Our sampler answers NaN both for
a void and for a cell that does not exist, and painting the second one magenta
would flood the screen the moment the aircraft left the DTED. A tile is
therefore remembered with a `has_data` flag and only a tile that has some data
can show a no-data hole.

## TA5 — the peak

FalconView's algorithm, kept, because it is the one piece of its bookkeeping
that pays for itself: each tile remembers its own maximum post and where it
is, tiles are visited highest-max first, and the search stops at the first
tile whose maximum is actually on screen. Only when a tile's own maximum is
off screen is its in-view part scanned. Drawn as `fv.triangle` with the
elevation beside it, in the display unit, subject to `label_threshold`.

## TA6 — registration and bindings

`fv.tamask` in the type registry as a STATIC overlay at display order 890 —
above the contours (880), which are lines about the same ground, and below the
graticule. Not restored at startup, for C2's reason. `pyfvw` gets the class,
`set_elevation_source`, `set_altitude` and `last_draw`; properties are already
bound on `Overlay`.
