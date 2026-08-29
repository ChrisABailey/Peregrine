# FalconView Cross-Platform Port — Working Ledger

**Read this first in every session. Do not re-explore the repo.**
This file is **open work, plus the map and the rules a session needs to start**. The build
narrative — how each finished piece came to be, and the "things worth not re-deriving" that came
with it — lives in **`port/PORTING-ARCHIVE.md`**. Go there when a line here names an archive row
or an area; §5 is the index. Condensed 2026-08-16: §1 was 480 lines of finished-work narrative and
is now an inventory, and every struck-through "done" item in §2 moved to the archive's
**"Condensed out of the working ledger — 2026-08-16"** section, verbatim.

Full strategy:
`/Users/chrisbailey/.claude/plans/this-project-is-extreamly-enumerated-marshmallow.md`.

**Plans** (a `-COMPLETE` suffix means: do not read it unless a bug turns up in that code):
| Doc | Covers | State |
|---|---|---|
| `port/vpf-geosym-plan.md` | vector/symbology design — §5 the cross-product middle layer, §7 ENC | products built; V7 CoreGraphics backend, V8 atlas and WVS still open |
| `port/fvkit-app-plan-COMPLETE.md` | overlay types, stack, editors, pick, shell, session | **A1–A6 all built** |
| `port/fvkit-draw-plan-COMPLETE.md` | geographic lines, symbol libraries, GeoDraw, render state | **G1–G4 built**; G5 (SVG) optional and never started, dimming deferred (§2b) |
| `port/fvkit-nav-plan.md` | moving map / navigation | **MM1–MM7 all built** (MM7 is the app side); MM5b (Viterbi) optional and never started |
| `port/fvkit-contracts.md` | D1–D6: ownership, geo, Status, pixel, naming, adapters | standing contract, always binding |
| *(no plan doc — see §1a-bis)* | the shared overlay toolkit + the real graticule | **BUILT 2026-08-29**: `ScaleTable`, `LabelPlacer` and `app::Properties` extracted from `grid_map`, then the lat/lon graticule ported over them. MGRS/UTM and GARS DROPPED on Chris's call, not deferred |
| `port/search-plan-COMPLETE.md` | global search — the `AsSearch()` capability + `SearchSession`, `VectorMapOverlay`, the OSM name index | **S1–S4 ALL BUILT** (2026-08-27 the seam + point/route providers; 2026-08-28 `VectorMapOverlay` tier 1, the staged `search_names` index + `fvnames` tier 2, and the road provider + bindings + PythonView's Find box) |
| `port/pippin-plan.md` | Pippin, the iOS app (SwiftUI + PippinKit ObjC++ facade over the core) | **P1–P6 built** (P1–P5 2026-08-18: data pack + iOS cross-build; Xcode project, PippinKit, Kiawah on screen; gestures and the render loop; the ownship, both feeds; and P5's `port/RouteKit/`, C++ and mac-tested rather than iOS. **P6 2026-08-19**: the route sheet, the crosshair, and `PPRouteStore` — RouteKit's first caller; **P7 2026-08-19**: GPS mode — the camera's answer on the frame, the zoom-out rule in `PPCameraFit.h`, and the snapper over the router's own graph; **P8 2026-08-19**: the trip computer — `fvkit/nav/trip.h`, the anchored odometer, and the ride bar showing the RIDE's numbers; **P13 2026-08-19**: five small changes off Chris's rides — two more zoom levels in (1:1,614), the route line doubled to 6 px over a 4 px casing (device pixels, so 3 was one point on a 3x phone), the ride bar at 26 pt in two corner clusters, the turnstone app icon in `port/apps/Pippin/art/` + `Assets.xcassets`, and Chris's new `style.json` committed); **P9's POINTS half 2026-08-20**: `.fvpoints` on the phone, drawn, tapped, edited, added and deleted — schema 3 (`phone`, `url`), `PPPointStore`, the points button beside Route, and the info/edit sheets (**same day, two changes off Chris's first use**: the `+` became the points button HELD DOWN, and the editor's Location row became the route sheet's own `LocationRow` — Current location | Pick on map — so P11's street-name change has two shared functions to land in, `LocationText.describe` and `PickOverlay.readout`); **P9's COMPASS half the SAME DAY**: the compass toggles north-up/course-up, course-up centres and orients CONTINUOUSLY (the apron off, and a `kLinear` follow slew one MEASURED fix interval long — `PPFollowCadence.h`, 12 mac tests), north-up keeps the apron Chris asked to keep, and two fingers turn the chart outside GPS mode behind a 12-degree dead zone; **P11 the SAME DAY**: the pick button and both Location rows name the nearest road the selected profile could actually use — "Use Treeduck Court" / "Use cycleway" / "No usable path" — where the difficulty is that the snap index is `kAll` and the nearest candidate is often a road the profile refuses (Kiawah Island Parkway carries `bicycle=no` for its whole length), so `Router::ArcUsable` became the free `fv::routing::ArcUsable` and `RoutePlanner::BuildOptions` went public: the label and the next replan are now one function over one rule file; **P10 2026-08-21**: record and share — `WriteGpx` beside the reader, `fvkit/nav/gpx_recorder.h` which APPENDS so the file is valid after every fix, one line into P8's `feedTrip:` (now `consumeRawFix:`), a red record button whose HELD press opens a Rides sheet, and a share button on a single point that sends a `.gpx` plus an Apple Maps link; **P14 2026-08-21**: a place shared IN — Apple Maps -> Share -> **Pippin** -> a point on the overlay with the address in its remarks, which needs a SECOND TARGET (`PippinShare`, an app extension) because a share sheet lists extensions and nothing else, plus `PlaceLink.swift` (Foundation-only, in both targets, 56 mac checks in ctest) and a `pippin://place?…` handoff rather than an App Group; **the coordinate is in the URL on iOS 18 and in the SIMULATOR, and an iOS 26 device share is often a bare `https://maps.apple/p/…` whose coordinate exists only in an intermediate REDIRECT** — the one network call in the app, and one constant (`PlaceLink.allowsNetworkLookup`) removes it; **P15 2026-08-25**: Auto-Lock — the phone locked mid-ride because nothing ever set `isIdleTimerDisabled`, and with WhenInUse and no `UIBackgroundModes` a lock SUSPENDS the app, so the moving map freezes and a recording in flight is silently gapped; the flag is now `gpsMode \|\| isRecording` off a `didSet` on both, and it suppresses only the IDLE timer — a side-button press still locks, and the flag binds only while Pippin is frontmost (so every backgrounded case draws nothing at all); **P16 IS BUILT (2026-08-25)**: every fix called `setContentDirty()` and with no raster cache under it that is a real decode-and-draw for a chevron nowhere near the screen, so the frame now stands still when nothing is following, nothing is recording and the ship is outside the viewport GROWN BY THE SYMBOL'S REACH — `RenderGate.swift` (CoreGraphics-only, 40 checks under `ctest -R render_gate`), `MapModel.renderIsWorthIt(for:)` and `PPMap.ownshipSymbolRadiusInPoints`; the FIX is never thinned (the trip computer, the recorder and the heading resolver are all below `renderer.push`), only the picture is, and the two things worth not re-deriving are that **the test must run on every fix even when the render does not** (it is the only thing that notices the ship coming back) and that **the departure needs a frame of its own** (`visible || wasVisible`, or the last drawn frame keeps a stale chevron half on the edge for as long as the rider rides away from it); the demo feed is deliberately NOT gated because its fixes never reach `receive(_ fix:)` and gating its timer would stop the replay advancing, which is the ship-that-never-returns bug with no way out; measured on the simulator through a real `CLLocationManager` (`simctl location start`, `-PPShowStats YES`): riding west off the edge the readout froze at −80.10738 and stayed there while the fixes ran on to −80.20000, and riding back in it resumed. **P17 IS BUILT (2026-08-25)**: `stopFeed()` has NO call sites, so `BestForNavigation` with no distance filter ran from launch to background whatever the mode — Pippin on a table cost about what following costs, minus the screen. `desiredAccuracy` is a hint and every tier down to `HundredMeters` keeps the GNSS chip powered, so idle now goes to a NAMED COARSE configuration (`ThreeKilometers`, a 100 m filter, the automatic pause on) rather than a notch off best: `LocationPolicy.swift` is the decision (44 checks under `ctest -R location_policy`), `PPLocationAccuracyMode` the two configurations, `MapModel.reconsiderLocationAccuracy(for:)` the plumbing. **Three things worth not re-deriving**: two thresholds and not one (restore inside viewport+25%, drop only outside viewport+100%, both fractions of the viewport so they mean the same at every zoom) or a ship parked at the edge restarts the receiver on every fix; the policy is re-taken from **`viewport`'s `didSet`** as well as from a fix, and that hook is the important one — a coarse receiver on a still phone delivers nothing, and the ship went off screen by being PANNED away from, so the camera is what notices it coming back; and the coarse `distanceFilter` is 100 m and NOT the size of the accuracy tier, which would step straight over the return threshold. `demandFullAccuracy()` runs first in `setGpsMode(true)` and `startRecording()` against a 30 s cold start, but what HOLDS the tier is `following || recording` — the demand only covers the gap to the flag, and a refused press correctly falls back on the next fix. A paused run is restarted by hand on the way back (iOS otherwise waits for motion). Measured on the simulator through a real `CLLocationManager`, including the pan-away case with **the phone never moving**; `-PPShowStats` appends `· coarse`, because a tier change moves no pixel. **P18 IS BUILT (2026-08-25)** — the raster cache, and it is neither of the two shapes the plan offered. A frame is now TWO LAYERS: the base map into a surface LARGER than the screen (the guard band) and KEPT, the overlay (route, points, ownship) into its own screen-sized TRANSPARENT surface every frame at the live camera, and `MapScreen` stacks them under one preview transform each. **There is no blitter**, which is the whole design: P3's preview transform has scaled, turned and offset the last frame on the GPU since the app's third session, so a band needed pixels rather than code — and because the COMPOSITOR turns the image, course-up (the mode the plan expected a band to fail in) is served like any other. `PPBaseCoverage.h` is the hit test (scale exactly equal, turn the short way within a quality cap, the live surface's four corners inside the band — affine, so four corners settle it exactly), 11 gtests; `CpuCanvas::BlendPixel` learned src-over onto a TRANSLUCENT destination with the opaque case kept as its own byte-identical branch, 5 tests, and that is the ONLY change outside Pippin. Measured on the simulator: a two-second drag went from **2 frames drawn to 23, 20 of them cache hits at 15 ms** (and the pan now renders live rather than previewed), and the demo ride following served **2115 of 2373 frames at 8 ms each**. **Four things worth not re-deriving.** (1) **P3's live-render budget had to be rewritten or the session cancels itself**: "the last frame took 70 ms" refused every frame of a gesture, so the band was never built and the cache was never asked — measured at 0 hits in 2 frames before the fix. It now asks whether the CACHE would serve the frame (then it is an overlay pass whatever the last one cost) and, if not, whether this is the gesture's FIRST base draw (the investment that builds the band); hence `PPViewport.covers(_:maxTurnDegrees:)`, so the shell can ask the cache's own question before starting a frame. (2) **The SETTLE is what makes the cache free rather than cheap** — a composited base is resampled whenever the transform is not the identity, so the moment the loop would pause it draws once more with the cache refused AND NO BAND (provably exact, and it hands the band's memory back); it fires only when what is on screen is really resampled, because **a fix under a camera that has not moved is served under the IDENTITY transform** and that is the cache's most valuable hit. (3) **The band is a PER-FRAME decision** — it is paid for on every miss and earns its keep only on hits, so it is not asked for where every frame is known to miss: a pinch (the scale is part of the key and has to be) and the first frame of a launch. (4) **Content never invalidates the base**, which is the editor's door: a dragged route point costs one overlay pass, 8–11 ms measured. The cost is memory (2.25x the screen's pixels plus a CGImage copy), given back by the settle and by a memory warning; both keys are `pippin.ini` `[display]`. **P19 IS BUILT (2026-08-25)** — the snap, and it is NOT the picker the plan described. Chris rejected that shape going in, with the reason that is the whole session: **a picker is a points feature; snapping is a property of the overlay INTERFACE**, and the requirement's own words are that a pick which *overlaps an overlay feature* takes that feature's exact position. He was also right that the interface was already ported — `Overlay::AsSnapTo()` has been on the base class since the app layer landed, `fv::app::SnapTo` folds `test_snap_to`/`do_snap_to` into one call, and `PickSession::SnapToPoint` already ran FalconView's `snptodlg` flow verbatim. **Nothing implemented it**, and `route_overlay_test.cpp` asserted `AsSnapTo() == nullptr` as a fact about the world. Three things were missing and no new architecture: (1) **two implementers** — `PointOverlay` and `RouteOverlay`, each written OVER its own `HitTestPoint` rather than beside it, so there is one reach rule and a snap and a hit can never disagree about what the finger is over (the dpi scale is what the test pins); the route one exists because an SPI with one implementer has not been shown to be an SPI, and it also buys starting a route where the last one ended. (2) **`SnapToItem::distance_px`** — every existing caller went through the chooser, and a chooser RANKS NOTHING: it shows rows and a human picks, so a shell with no dialog had nothing to rank by. (3) **`fv::app::SnapCandidates`** — the same walk, shell-free and const, nearest-first, stable so a tie keeps stack order; a free function because a phone should not implement eleven pure virtuals of `AppShell` to ask a question it will never ask, and `PickSession::SnapToPoint` is now that walk plus the dialog. On the phone `PPMap snapTarget(near:in:tolerance:)` asks the **stack**, not the point store — it did not change for the second implementer and will not for the third — and `MapModel.pickSnap` is answered in the SAME render-queue hop as the road name, because the two are one fact about one crosshair. Every call site reads `pickCoordinate` (`pickSnap?.coordinate ?? crosshairCoordinate`), extending P11's one-definition rule by one line. The snap is automatic and never silent: the button reads `Use Ruddy Turnstone` and the ring grows and takes the accent colour; dragging off it is the refusal. **Two things only the simulator could correct.** The tolerance was BACKWARDS — 30 points on the reasoning that a pick wants a wider catch than a tap, and on screen it caught a marker **28 points away, plainly beside the crosshair**. It is now 12 and is the margin BEYOND the marker's drawn ink (`HitTestPoint` adds the half-width first), so a 26-point marker is caught from ~25 points — about when the ring touches it; a tap affords more because the FINGERTIP is the blunt instrument, while here the map is steered under a crosshair the rider places exactly. And the first indicator, a white dot inside the ring, is invisible against a pale chart and against the marker behind it — hue and size survive being drawn over a map, one channel does not. **The acceptance criterion**: Start picked over the point `Bufflehead Dr` left `32.6095000000, -80.0655000000` in the route document, identical to ten decimals to that row in `points.fvpoints`, against End's `32.5956007934, -80.1096644372` — the un-projection of a pixel, which is the noise a snap removes. 11 new gtests (3 point, 2 route, 5 aggregation, 1 through a real `PointStore`, plus the flipped capability assertion); `pippin.ini` `[pick] snap_tolerance`, 0 to switch off. **PIPPIN P1–P19 ARE ALL BUILT.** What is left is not a session: the route-point drag gesture P18 made cheap, no way to get NEW artwork into a document from the app, P15 not yet ridden, and store licensing. Requirements in `port/apps/Pippin_requirements.md`, and `port/apps/Pippin/README.md` is the built half |

Other docs: `port/bindings/pyfvw/README.md` (Python user guide),
`port/bindings/pyfvw/ICD-MAPPING.md`, `port/peregrine.ini.sample` (every settings key with its
measured effect), `port/families/{dnc,enc,osm}-families.json` (the data-family starters, M1),
`port/Osm/styles/style-readme.md` (the supported GL-style subset).

```sh
cmake -B build && cmake --build build -j && ctest --test-dir build   # 1799 as of 2026-08-29, ALL GREEN.
# This is the number ctest RUNS. The per-test "n/1532" prefix is 17 higher because it counts
# the 17 disabled GeoTrans tests too — do not update this line from that, the two counts are
# 17 apart forever.
# THIS LINE DRIFTS: trust the archive row of the LAST session if the two disagree.
# The default CMake build type is NOT optimised — configure -DCMAKE_BUILD_TYPE=Release before
# taking any performance measurement (R3c's real finding).
```

---

## 1. What exists today (so you don't go looking)

An inventory: what the layer is, where it lives, and only the invariants that constrain NEW work.
The reasoning behind each is in the archive row named in §5.

**Geo/math** — `port/{geoid,geo3,geo_tool,geotrans,MapScaleUtil,MapSeriesStringConverter}`, all
tested. GEOTRANS 3.3 is **frozen** (pinned bit-faithful results).

**Raster products** — each an `IRasterSource` + enumerator, self-registered in the format registry
(7 builtin: `geotiff`, `cadrg`, `tiros`, `dted`, `dted-shaded`, `gpkg`, plus the vector products):
`port/{ImageLib,ImageLibCore,CadrgDecoder,CadrgMapServer,GeoTIFFMapServer,TirosMapServer,DtedMapServer,DtedShadedRenderer}`.

### 1a. FvKit core (`port/include/fvkit/`, impl `port/fvkit/`)

`geo.h` (incl. `SurfacePoint`), `raster.h`, `engine.h` (MapEngine), `catalog/` (SQLite+R-tree),
`canvas/` (ICanvas + CpuCanvas — **and since P18 a CpuCanvas can be a LAYER**: `BlendPixel` does src-over onto a TRANSLUCENT destination, so a surface cleared to alpha 0 can be drawn on and composited over another; the opaque-destination case is its own branch and is byte-identical, which is why no golden moved; **and since 2026-08-29 CpuCanvas DECODES UTF-8** rather than walking bytes — every glyph loop used to take one byte as one code point, with a comment saying the ASCII subset was all GeoSym needed, and it was until the graticule wanted a degree sign and got `35Â°`. An ASCII string decodes to the code points it always did, so **no pinned golden moved**; the only strings whose rendering changed are the ones that were already wrong), `overlay/` (SPI + manager + the graticule + `KeyEvent`),
`store/tile_pack.h` (GeoPackage), `settings.h` (`fv::Settings`, INI, the registry replacement).

**The catalog is at SCHEMA 2 (2026-08-29): a map series is `(format, series_key, scale,
scale_units)` and NOT the key alone.** Chris reported one GeoTIFF map type at 1:2,014 holding both
`Atlanta SEC.tif` (a 1:500 K sectional) and 1 m orthoimagery, and the Windows code says why:
FalconView keys `tblMapSeries` on **(Scale, ScaleUnits, SeriesName)** —
`CCoverageCache::GetMapSeriesIdentity` looks a frame up with `SelectByScale(scale, units, series)`,
and `GeoTiffMapFileFinder::IsFileValid` **rejects a file outright when no such row exists**. So a 1
metre Color GeoTIFF and a 50 metre Color GeoTIFF have always been two map types over there. The
extraction was never wrong — `TIF_determine_scale_and_series` returns a resolution in METRES for
imagery and a denominator for maps, and `fv::GeoTiffFrame` reproduces both faithfully (TestData's
sheets come out at 0.3, 0.6, 1, 10 and 50 m plus a 1:30 K DRG) — but the catalog filed them by
`series_key`, so `INSERT OR IGNORE` kept whichever frame was scanned first and every other
resolution inherited its scale. **Three places had to agree**, and the middle one is the trap: the
`UNIQUE` constraint, `ResolveSeries`'s `SELECT`, **and `Scan`'s in-memory `series_cache`, which was
keyed on `series_key` alone and would have re-collapsed the rows however the schema was written.**
`SeriesRow` now also carries `display_name` — FalconView's own `FORMAT_SERIES_SCALE` label through
the ported `MapSeriesStringConverter` ("GNC 1:5 M", "Color 1 meter"; the bare key when there is no
scale to name) — because a `series_key` is no longer a unique handle: `fvrender`/`fvpack`/
PythonView take either spelling and **name the candidates rather than silently picking the first**
when a bare key is ambiguous. An older catalog on disk is **rebuilt, not converted** (the per-frame
scales were never stored, so nothing can split the merged rows apart); `Catalog::NeedsRescan()`
says so and PythonView rescans its data sources on open. TestData now yields 7 GeoTIFF map types
where it used to show 3.

**`proj.h` — the projection, and it ROTATES** (PR1–PR3, 2026-08-15). Equal-arc, plus
`SetPhysicalScale` and `SetRotation` — a clockwise turn about the surface centre, carried by both
transforms and by `VmapBounds`, which returns the box of the TURNED viewport. **Rotation 0 is the
byte-exact identity, because the arithmetic is GATED and not because a matrix happens to be the
identity** — every raster and vector golden depends on that. Both draw paths honour it: the vector
path since PR2, the raster path since PR3 where `MapEngine::CompositeRow` gates on the rotation and
a turned frame is resampled through the turned projection and MASKED to its own edges. Three
numbers to carry forward: a turned viewport's query box grows to `(w cos + h sin)` by
`(w sin + h cos)` — `(w+h)/√2` on both axes at 45° — the retained scene needs **no rotation in its
cache key** because R3a made its ink geographic, and the **only** angle in the drawing stack that
had to be taught the rotation is a point symbol's north-up `PointSymbolStyle::rotation_deg`
(`SymbolAngleOnChart`, two call sites). `DrawSymbolAtPixel` deliberately does NOT apply it: a pixel
anchor's angle is already a screen angle. Bound as `MapProjection.set_rotation`/`.rotation` and
`MapEngine.set_rotation`. **The surface CENTRE is `((w-1)/2, (h-1)/2)`, not `(w/2, h/2)`** —
FalconView's pixel-centre convention, and a shell that writes a pan by assuming the other one
gets a constant half-pixel offset on every drag (Pippin P3 measured 0.24 pt of it before the
pan was changed to ASK, via `GeoToSurface` of the current centre, instead of assuming).

**The vector seam** — `vector/vector.h` (IVectorSource, VectorFeature, FeatureRef/Describe),
`style.h` (IStyleEngine, path/area pattern styles), `rules.h` (predicate AST, ScaleBand,
ViewingGroup), `families.h` (named groups of features over rule selectors, JSON),
`mariner.h` (`MarinerSettings`, shared by DNC and ENC — it belongs to the ENGINE, where the epoch
that invalidates a retained scene already lives; each product keeps its OWN defaults, which are what
its goldens were pinned over), `lookup_engine.h` (`LookupTableStyleEngine` — the shared engine;
GeoSym, S-52 and OSM are *loaders* over it), `renderer.h` (VectorRenderer + the three placers
`PlaceAlongPath`/`PlaceOverArea`/`PlaceTextAlongPath`), `text_draw.h` (halo offsets, glyph
advances, `AlongPathAnchorShift`), `symbol_draw.h` (`DrawSymbolAt`/`ResolveSymbol`/… and the
optional colour tint), `scene.h` (retained VectorScene), `pick.h` (PickIndex — hit-tests the
emitted ink). `LabelStyle` carries placement, spacing, max angle, offset, a size in pixels OR
ground metres, `halign`/`valign` (E8), `halo_width`/`halo_color` (T2, a stamped halo — nothing new
from `ICanvas`) and `along_anchor` (`kBaseline` = 0.0 = the pinned goldens, `kCenter` = half the
cap height, `kCapHeightEm = 0.72` measured over the port's fonts).

**Geographic contours** (G1, `geo/contour.h`) — `IGeoContour` as a pull iterator with
`SimpleGeoLine`, `GreatCircle`, `RhumbLine`, `GeoCircle`, `GeoEllipse`, `GeoArc` and
`PolylineContour`; `MakeGeoLine` is the factory, `BuildGeoPath` projects any contour into surface
sub-paths, `AtBreak()` says a sub-path ended (a clipped-away leg). **The property that matters: it
CLIPS IN GEOGRAPHIC SPACE BEFORE DENSIFYING**, so an intercontinental arc on a harbour map costs a
search, not a walk — do not "simplify" that away. Step size comes from the projection's dpp
(~20-px chords, `/5` above ±70°), so vertex count tracks the SCREEN. **The pull iterator is
deliberately NOT bound**; every contour is one `pyfvw.geo.*_path` call that builds and projects in
a single crossing.

**Symbol libraries** (G2, `symbol/`) — `ISymbolLibrary` is exactly the three methods `IStyleEngine`
already had, so `IStyleEngine : public ISymbolLibrary` and every style engine IS a symbol library.
Four implementations: `PngSymbolLibrary` (loose files with pivot sidecars and `@2x` twins, OR a
sprite sheet + MapLibre `sprite.json`; lazy), `CgmSymbolLibrary` (GeoSym's ~1500 `.cgm`, kept in
`port/GeoSymServer/` because fvkit never links GeoSym), `BuiltinSymbolLibrary` (13 literals — six
PointOverlay shapes, five line decorations authored **+x ALONG the line, +y to its LEFT**, a north
arrow, a crosshair; `kBuiltinSymbolNominalPx` = 9.0) and `CompositeSymbolLibrary` (ordered; symbol
and pixmap resolve INDEPENDENTLY, and its unit is ONE number for the whole composite — a stated
limitation). Two rules: **a pivot lives in TILE pixels** (`SymbolPixmap::pixel_ratio` divides the
scale so a 2x tile comes out the same SIZE as its 1x twin; it defaults to 1.0 and dividing by 1.0
is the identity, which is why no golden moved), and **a library re-`Open` REPLACES**.

**Overlay drawing** (G3, `canvas/geo_draw.h`) — `GeoDraw(proj, canvas, symbols)` is **the surface
an overlay calls**: `DrawGeoLine`/`Polyline`/`Circle`/`Ellipse`/`Arc`/`Contour`/`SurfacePath`,
`DrawSymbol`(`AtPixel`), `DrawLabel`(`AtPixel`/`AlongPath`). It composes G1, G2 and the placers and
needs **no new `ICanvas` op**. `GeoLineStyle` is `{casing, stroke, pattern}` — the casing is the
halo a line wants, drawn first, wider, and **following the PATTERN when there is one**.
`PresetGeoLine` gives ten names (`solid dash long-dash dot dash-dot railroad arrow tick notch
feba`) over `BuiltinSymbolLibrary` — that is how `LineSegmentRenderer.cpp`'s 913 lines and 15
classes collapse — and **solid deliberately returns an INVALID pattern**, as does an unknown name,
which falls back to a plain line rather than to nothing. Picking is **OFF by default here** (the
opposite of `VectorRenderer`). `symbol_dpi_scale` defaults to 1.0 and is the stated way out of the
symbol-DPI defect (§2b). Bound as `pyfvw.draw` + `pyfvw.symbol`.

**Render state** (G4) — `RenderState{kNormal,kHighlighted}` plus `SetState` /
`SetHighlight(colour, width_px)` (default FalconView's selection yellow at 3 px): **what a draw
MEANS, as against how it is styled**, so a highlighted thing is still drawn as ITSELF. The
mechanism is T2's stamped halo reused verbatim (one function, so a highlight and a text halo cannot
drift apart). Four rules: a LINE takes one wider stroke, not eight offset ones (same picture,
an eighth of the cost) and it goes under the casing; a highlight **never enters the pick index and
never counts as a draw** (`AsHighlightPass`), or a selected feature would become a bigger target;
the highlight goes on a marker's **outermost stamp only**, and never on the label; and the stamp's
growth under a highlight is **capped at 2x**.

**Moving map** (`nav/`, MM1–MM4) —
- `position.h`/`scripted_source.h`/`heading.h` (MM1): `PositionFix` where **every field carries its
  own validity** (the -1000.0 sentinels do not port) with `Merge` for two sentences of one epoch;
  `FixQueue` (mutex + drain-on-tick) because a source delivers on whatever thread it likes, and a
  full queue **drops the OLDEST and counts it**; `ScriptedSource` with an **injectable clock** and
  no thread, so every timing assertion is an equality; `BuildScriptedTrack` takes a **polyline**, so
  fvkit still does not link `port/Routing`; `HeadingResolver` derives heading in **SCREEN** space
  (deg-per-pixel scaled) — on an equal-arc map a true bearing of 045 does not draw at 45°.
- `camera.h` (MM2): `MovingMapCamera` + `ComputeApron`/`DeltaXy{Discrete,Continuous,TrackUp}`,
  ported verbatim from `gps_draw.cpp`. **The camera never touches the engine** — `Update` returns a
  `CameraTarget` and the shell applies it. **The apron is built from where the ship was DRAWN and
  tested against where it has just moved to**, so the seam is two calls (`RecomputeApron` per frame,
  `Update` per fix); rebuilding it around the new position freezes the map forever. The track-up
  offset is `d_x·right-of-course + d_y·ahead` in a Y-DOWN surface and **must not be "fixed" into a
  rotation matrix**. `RecomputeApron(0,0,0,0)` is NOT empty (the original's `+ 1` exclusive edge
  yields a 1×1 box) — `ClearApron` is what an overlay with no fix calls.
- `camera_slew.h` (MM3): `CameraSlew` between the camera and the engine. **No Windows original** —
  FalconView jumps — so every rule is a decision: duration 0 IS the jump and the rate caps must not
  resurrect it; the interpolation is in **GEO**, the frame that survives the projection being
  re-centred by this very animation (longitude the short way, pinned across the antimeridian); the
  caps **extend the duration and never clip the motion** (1200 px/s, 120 deg/s); centre and rotation
  share one duration; a new target **retargets in flight and never queues**, which restarts the ease.
- `overlay/moving_map_overlay.h` (MM4): `fv::MovingMapOverlay` holds the feed, the camera and the
  slew; `fv.movingmap` is a **static built-in type**, deliberately not restored at startup. **The
  camera lives here because the overlay is the only object that is both drawn per frame and fed
  fixes.** `Tick` answers and applies NOTHING. **Every queued fix reaches the heading resolver; only
  the last reaches the camera.** Setting the modes FORCES a recentre. The heading is **NEGATED** into
  `PointSymbolStyle::rotation_deg` (that field turns a symbol counter-clockwise; S-52 negates at its
  own seam for the same reason). `screen_angle_deg()` IS the camera's `point_angle` and ADDS the map
  rotation. A shell declares `SetRotationSupported` (PythonView says yes since PR3); `Tick` **adopts
  `proj.Rotation()`** before using it, so the projection is the one place the applied rotation is
  true. Bound whole as `pyfvw.nav`, and `nav.PositionSource` is subclassable from Python.
- `road_snap.h` (MM5): `RoadSnapper` puts the ship on the road it is on. **The road network is behind
  `IRoadNetwork`** — one method, "which roads are near this point, PROJECTED" — because fvkit still
  does not link `port/Routing`; the port's implementation is `fv::routing::RoadGraphNetwork`
  (`port/Routing/fv_road_network.h`), and **no network is a supported state** (the feature is off).
  `ProjectOntoSegment` is **inline in the header** so an adapter needs fvkit's headers and not its
  library. **Every score term is METRES** (distance + a heading-alignment penalty − a stay bonus for
  the previous arc − a smaller one for an arc CONNECTED to it), the radius comes from the fix's own
  HDOP floored and capped, and below `hold_speed_mps` the arc is **HELD — an infinite stay bonus, not
  a branch**, so a ship that has drifted off it still lets go. Output is a decoration: **the raw fix
  is never destroyed**, `Applied()` is the fix to consume. In `MovingMapOverlay` the snapper stands
  **BEFORE the heading resolver** (resolving first would derive every heading from the scatter the
  snap removes; and the road's bearing then arrives as a *reported* heading, which heading.h already
  prefers) and `snap_min_confidence` (0.25) is what the overlay declines a guess with. Measured on a
  noisy Kiawah track: across-track error removed, **along-track error is not** — that residual is why
  MM5b (Viterbi) is still a real option and not a formality.
- `nmea.h` / `line_transport.h` / `gpx.h` (MM6): **a real feed, and two recorded ones**.
  - **`nmea.h`** ports `MovingMapOverlay/nmea.cpp` field index for field index. Three original
    quirks are PRESERVED and labelled Q1–Q3 in the header: a sentence one field short is
    **rejected whole** (RMC 11, GGA 14, GLL 6, VTG 8), GGA **withholds altitude at ≤3 satellites**,
    and RMC's magnetic heading is **derived from the variation** (west adds, east subtracts, one
    wrap). Two things deliberately DIFFER: the −1000.0 sentinels do not port (position.h rule 1),
    and **ANY TALKER is accepted, not just `$GP`** — `strncmp(s,"$GPRMC",6)` rejects every sentence
    a modern phone sends (`$GNRMC`), and MM6 exists so a phone can drive the map. The build side is
    kept for the recorder, with one deviation: the checksum is **zero-padded everywhere**, where the
    original's `build_RMC`/`build_VTG` wrote `"%2hX"` and emitted `"* 5"` below 0x10 (`build_GGA` had
    it right, and the port takes GGA's form). A built position round-trips to **~1 m**, because
    minutes go out with three decimals — that is the wire format, not the parse.
  - **`NmeaFixAssembler`** is where MM1's `Merge` finally pays: sentences sharing a time of day
    become ONE fix, and **the date comes from the last RMC** (or `SetDateHint`) — until one has
    arrived a fix has `has_time` false rather than a 1970 stamp. Closing on the epoch change costs
    **one epoch of latency**, so `SetEmitPerSentence(true)` is the live-feed mode; `pending_emitted_`
    is what stops the two modes from delivering one instant twice.
  - **`line_transport.h` is a SEPARATE seam from the parser**, so nmea.h never learns where bytes
    come from and every transport is testable without a parser. `ReadLine` **never blocks**
    (`kAgain` between sentences), **`kEnd` is not an error**, and **framing is ONE implementation**
    — `LineBuffer`, which holds a partial read, drops `\r`, skips blanks and **caps a line at 512
    bytes** so a hostile stream is bounded. Four transports: `StringLineTransport` (the test
    double), `FileLineTransport` (with `follow`, which **tracks its own offset and seeks** — a bare
    `clear()` does not see appended bytes — and which **never hands over an unterminated tail**,
    since in a growing file that is a line still being written), `TcpLineTransport` (this is what
    "phone GPS" means: GPS2IP-class apps serve NMEA over TCP; **non-blocking BEFORE connect**, or an
    asleep phone hangs the shell's tick) and `UdpLineTransport`. **Windows is guarded, not tested.**
    Serial waits for a device; CoreLocation stays out until NMEA-over-TCP proves insufficient.
  - **`gpx.h` has NO WINDOWS ORIGINAL** (FalconView has none; GPlan has three, all C#) — Chris asked
    for it beside the NMEA work, and it earns its place because **GPX is what a watch, a phone or a
    bike computer actually hands you** while a raw NMEA log is what a receiver hands a program.
    GPX 1.0 and 1.1, tracks/segments/waypoints/routes, over **expat** (a GPX file comes off the open
    internet, so it is never parsed by hand; an entity declaration **stops the parse at the DTD**).
    Three rules: a track point's `<time>` **is** the fix's time; **speed is derived and heading is
    not** (HeadingResolver already derives one, in SCREEN space, and a true bearing written into
    `has_true_heading` would look REPORTED and quietly win — `derive_true_heading` is there for a
    caller who wants it); and `<ele>` **is read as MSL**, the pragmatic reading every GPX consumer
    makes, said out loud rather than silently. A `<trkseg>` boundary is KEPT (a straight line across
    a lunch break is not a track) and `split_gap_s` makes one where a device forgot to.
    **It WRITES as well since Pippin P10 (2026-08-21)**: `WriteGpx`/`WriteGpxFile`/`BuildGpxTrack`,
    1.1 only, four rules that mirror the reader's — the one a round-trip test cannot catch is
    **a field with no validity is not written** (`<ele>0</ele>` for an absent altitude reads back
    as a valid sea-level fix), and **speed is not written at all** because base GPX has nowhere to
    put it and a vendor extension would give a DERIVED quantity a second source of truth. Precision
    is the FORMAT's (7 decimals = 11 mm), so round-trip equality is a tolerance and the tests pin
    it there. `FormatIso8601UtcFractional` exists because a 1 Hz recorder whose stamps carry a
    fraction would otherwise write two points with the SAME whole second, which the reader is then
    right to drop. `waypoint_descriptions` is the reader's only change: a `<wpt><desc>` is kept
    (a track point's is still dropped — it has nowhere to live on a `PositionFix`).
  - **`gpx_recorder.h` (P10) APPENDS, and that is its whole reason.** A phone records for three
    hours in a pocket and every bad ending is the same one — killed in the background, flat
    battery, force-quit — so a recorder that saves on Stop loses everything to the one failure it
    must not have. It remembers where the FOOTER starts, seeks back to it for the next point,
    writes the point and writes the footer again; the footer's length never changes, so nothing is
    truncated and **the file on disk is a complete, valid GPX after every fix**. One seek and one
    flush per fix. A test reads the file back mid-ride, after each of six fixes, without closing.
  - **The whole of MM6 is bound as of MM7** (`pyfvw.nav`), under two module rules: Python never sees
    a `Status`, so `read_gpx_file` / `read_nmea_log` / `LineTransport.open` **raise**; and an
    out-parameter becomes a return or a `None` (`parse_nmea_sentence`, `add_line`, `flush`,
    `next_line`, `parse_iso8601_utc`), with `read_line` answering the tuple `(LineResult, text)`.
    **`ILineTransport` has NO Python trampoline on purpose** — a Python object holding bytes uses
    `StringLineTransport.add_data`, or subclasses `PositionSourceBase` and delivers whole fixes.
  - **BOTH recorded readers arrive at ONE seam**: `ReadGpxFile`/`ReadNmeaLog` → `FlattenGpxFixes` →
    **`BuildScriptedTrackFromFixes`** (scripted_source.h) → `ScriptedSource` → `MovingMapOverlay`.
    The fixes' own stamps are the schedule, so a ride replays at the speed it was ridden and **the
    moving map does not learn a second kind of track**. `NmeaLineSource` is the LIVE path, and it
    has no thread for MM1's reasons; `max_lines_per_poll` (256) is what stops a file transport
    replaying three hours inside one frame.


### 1a-bis. THE SHARED OVERLAY TOOLKIT — read this before porting any overlay

Three pieces extracted from FalconView's `grid_map` on 2026-08-29, when the sample grid overlay was
replaced by the real graticule. **They exist so the NEXT overlay does not re-derive them**, and each
one had exactly one copy in the Windows tree per overlay that needed it. If you are porting
`scalebar`, `Contour`, `TAMask`, `shp`, `nitf`, `localpnt` or anything else with a look that changes
with zoom, text on the map, or a property page, start here.

| Piece | Header | Use it for | Replaces |
|---|---|---|---|
| **`ScaleTable<T>`** | `fvkit/scale_table.h` | any value chosen per map scale — line spacings, contour intervals, declutter tiers | `GridSpacing::get_closest_scale` and its per-overlay twins |
| **`LabelPlacer`** | `fvkit/canvas/label_placer.h` | text on the map that must not land on other text | `GridLabel::overlap` + `lat_label_bound_rect_array` |
| **`app::Properties`** | `fvkit/app/properties.h` | everything a user can change about an overlay | the MFC `CPropertyPage` per overlay (`grid_pp.cpp` is 420 lines, and is the SMALLEST one) |

**The things not to re-derive about each.**

- **`ScaleTable` is keyed on a SCALE DENOMINATOR, and you get one with `ScaleDenominatorFor(proj)`
  and never with `proj.Scale()`.** `Scale()` returns **0 in resolution mode**, which is the mode a
  tile pyramid uses and therefore the mode Pippin runs in — an overlay keyed on it works on the
  desktop and silently draws nothing on the phone. The conversion is exact, not approximate: both
  branches of `GetNominalDegreesLatPerPixel` have the same slope (1.3471e-6 deg/px per unit of
  denominator) and meet continuously at 1:5M, so `ResolutionToScale(dpp, MAP_SCALE_ARC_DEGREES)` is
  its inverse over the whole range. `Nearest` clamps off both ends and never interpolates —
  halfway between two authored looks there is no third look.
- **In `LabelPlacer`, CALL ORDER IS PRIORITY.** First to ask keeps the spot; a later overlapping
  label is REFUSED, not moved or shrunk (give it several anchors with `PlaceFirstFit` if you want a
  fallback). Nothing evicts anything, which is what stops text flickering during a pan. A placer is
  **per-frame** — it holds pixel boxes. The box it reserves is the box the draw inks because both go
  through **`MeasureLabelInk`**, which `GeoDraw::DrawLabelAtPixel` now also calls; do not compute a
  label box any other way. An **unmeasurable** label (a canvas with no font — pyfvw's Python
  canvases) is accepted and drawn rather than dropped.
- **`app::Properties` is a DECLARATION, and `LoadFrom`/`SaveTo`/`ResetToDefaults` are written once
  over it.** An overlay implements `Describe`/`GetProperty`/`SetProperty` and gets, for free:
  peregrine.ini persistence under its own prefix, a generic dialog any shell can build, and pyfvw's
  `describe_properties()` / `get_property` / `set_property` — **which are bound on `Overlay`, not on
  any overlay class, so a new overlay needs no new binding code at all**. Enforce the declared range
  in your `SetProperty`: a settings file and a script both reach it and neither is a spinner. One
  unparseable line in an .ini is warned and skipped, never fatal.
  **Nothing in a shell has to load them.** `OverlaySession::Instantiate` — the one place an overlay
  is created in the app layer — applies `LoadFrom(settings, SettingsPrefixForTypeId(type))` to every
  overlay that declares properties, so `fv.grid` reads `[grid]`, `fv.points` reads `[points]` and a
  new overlay reads its own section on the day it is written. **This was missed the first time and
  Chris found it in an hour**: the `[grid]` section was parsed into `Settings` and read by nobody, so
  the graticule drew its compiled-in BLACK casing while `peregrine.ini` asked for green. The
  mechanism was complete; the call site did not exist. Guarded now by four tests in
  `app_session_test.cpp`.
  **The same day, the same bug's other half**: with the hook in, `label_color = "rgba(32, 37, 31,
  0.97)"` was REJECTED (the colour parser took hex only) and the rejection went into
  `session.warnings()`, which PythonView drained in `_ui_flow` and nowhere else — and neither
  `_set_static` nor `restore_startup_overlays` goes through `_ui_flow`, so the message was as good
  as absent. Both ends fixed: `ParseColor` now takes `#RGB`/`#RGBA`/`#RRGGBB`/`#RRGGBBAA`/`rgb()`/
  `rgba()` (**CSS alpha, so `rgba(...,1)` is opaque**) and writes canonical hex back, and
  PythonView's `_drain_session_warnings()` is called from every path that can create an overlay.
  **The lesson for the next overlay: a property nobody can set is a bug, and a rejection nobody
  sees is a worse one.**

### 1b. App layer (`fv::app`, `port/include/fvkit/app/` + `port/fvkit/app/`, A1–A6 — plan DONE)

`type_registry.h` (`TypeId` is a STRING; `OverlayTypeDesc` carries the factory as a
`std::function` and an **`std::optional<FileTypeDesc>` — that optional IS the static-vs-file
distinction**; `RegisterBuiltinOverlayTypes`), `capabilities.h` (`Persistence`, `HitTest`, `SnapTo`,
`ContextMenu`, `RoutingOverrides`, `EditTarget`), `shell.h`, `editor.h`, `session.h`, `pick.h`,
`vector_hit_test.h`, `search.h`. The layer is `fv::app` and D5's "no nested namespace" does not
apply to it.

- **Capabilities are found by ACCESSOR, never `dynamic_cast`** — `fv::Overlay` has six `As*()`
  returning nullptr by default over forward-declared types, so L4 keeps no app-layer dependency.
- **A2 is the STACK and it is `fv::OverlayManager` grown in place** (`fvkit/overlay/manager.h` IS the
  plan's `stack.h`): `StackObserver`, a current overlay, `MoveAbove`/`Below`/`ToBottom`/`Reorder`
  (a total permutation, rejected whole if it is not one), `FirstOfType`/`OfType`/`FindByFileSpec`,
  declutter, mouse capture. `SetTypeRegistry` is optional and **with no registry every A2 addition
  is inert**. manager.h still includes nothing from `fvkit/app`.
- **A3 is the SHELL SEAM** — `AppShell` is the complete inventory of UI the layer needs (five
  decisions, six presentation calls) and `FlowResult{kDone,kCanceled,kFailed}`, where **a cancel is
  the USER's answer and propagates** while a failure is reported and left on `last_error()`.
  `OverlaySession` holds the flows; open dedups on **(TypeId, file spec)**. R1 pays for itself:
  `app/test/fake_shell.h` makes all 56 tests plain unit tests with no dialog and no pump.
- **A4 is the MODE DANCE**, and it runs in two directions of which **only one is a call**:
  `SetMode` makes the current overlay match the mode, while "the mode follows the current overlay"
  and "closing the edited overlay falls to the next OF THAT TYPE" are **observed** through a private
  `StackObserver`. The editor instance is per TYPE and cached, so tool state survives. The mutual
  dependency with `OverlaySession` is wired after construction on both sides and both are optional.
- **A5 is PICKING, an aggregation rather than FalconView's first-hit-wins veto.** **Who is asked has
  ONE implementation and it is the DRAW order reversed** — `OverlayManager::DrawOrder()`, with
  `DrawAll` written over it. **The hit id is a HANDLE, not a packing** (a `FeatureRef` is 4×int32,
  `HitItem::feature` is one uint64_t); `VectorHitTest` mints it and `RefFor()` translates back.
  Hover notifies only on a CHANGE, and `kAskWhenAmbiguous` degrades to `kTopMost` on a hover.
- **SEARCH (S1, 2026-08-27) is the SECOND aggregating capability and the seventh accessor.**
  `search.h` is `SearchQuery`/`SearchResult`/`SearchProvider`/`SearchSession` + `Overlay::AsSearch()`
  — geo-space, on-demand, **hidden overlays included** (`visible_only` defaults FALSE, the deliberate
  opposite of pick; true narrows the walk to `DrawOrder()` reversed, pick's exact set). A provider
  picks WHICH string it matches; `TextMatchQuality` in the header decides what matching MEANS, so
  "rud tur" cannot mean two things in one stack. The session — not any provider — folds
  `near`+`radius_m` into a box for the coarse filter, cuts the exact circle afterwards, and orders
  (`kAuto` = `kBestMatch` with text, `kNearest` without; stack order is the stable tie-break).
  Distances are `road_snap.h`'s metre, borrowed rather than re-derived. `PointOverlay` and
  `RouteOverlay` are the two providers.
- **S2 (2026-08-28) makes a MAP searchable, through the same one discovery path.**
  `fvkit/overlay/vector_map_overlay.h` — `VectorMapOverlay` holds an `IVectorSource` and answers
  `AsSearch()` and nothing else yet (it does not DRAW; that is the layering feature, and it needs
  nothing from this seam). **It is generic over the source, not over OSM**: the label field is a
  knob (`SetLabelTags`, default `{name, name:latin, name:en}`), so ENC's `OBJNAM` and DNC's `nam`
  are the same class with a different list. **Tier 1 is area-constrained and a query with no area
  finds NOTHING** — not the pack, not an error — because the global path is S3's FTS5 table; the
  session has already folded `near`+`radius_m` into an area, so "near me" is an area query.
  **The overlay passes NO SCALE**, which is how the source's own tile budget stays the ONE number
  that owns how much pyramid a search reads (measured on the Kiawah cut: the island caps z14→z13,
  a fifth of the features for three quarters of the names — the pyramid as a free relevance
  function). **Two behaviours tier 1 does not work without, and neither was in the plan.**
  (1) *Unnamed geometry is never a result*, spatial-only queries included: most of a vector tile
  has no name, and a blank row is not a choice anyone can make. (2) *Pieces of one named thing
  are merged into one row* (`SetMergeGapMeters`, default 100 m, negative disables) — a road is
  cut at every tile seam and at every junction where a tag changed, so the un-merged answer to
  "Ruddy Turnstone" is a dozen identical rows; a bridging piece folds two clusters together so
  the answer cannot depend on tile read order, the merged bounds is the union, and **the anchor
  is the biggest piece's own label point, never the union's centre** (which lands off the road).
  The 128-bit→64-bit mint is a KEPT table (ids from 1, `FeatureRefFor` inverts, `DescribeFeature`
  is the identify path), so the same query twice gives a row the same number. The one honest
  limit: `IVectorSource::Query` has no cancel of its own, so ONE source query is the unit of work
  and a flag raised inside it is honoured when it returns. 26 gtests — 21 in
  `port/fvkit/test/vector_map_overlay_test.cpp` over a fake source (everything decided above the
  source seam), 5 in `port/Osm/test/osm_search_test.cpp` over the real Kiawah pyramid.
- **S3 (2026-08-28) is TIER 2: the pack carries its own gazetteer.** The seam grew
  `IVectorSource::HasNameIndex()` + `SearchNames(VectorNameQuery, vector<VectorNameHit>*)`
  (kUnsupported by default, so ENC and DNC can grow one later without the overlay changing), and
  `VectorMapOverlay` asks it **when the query carries TEXT**; a spatial-only query still reads
  tiles, and an index that is absent or fails falls back to the scan. The store is
  `port/Osm/fv_osm_name_index.h` — `search_names` (name, layer, class, anchor, box, min_zoom,
  z/x/y + feature), an external-content `search_names_fts` mirror and `search_names_meta` —
  **written INSIDE the .mbtiles**, because extra SQLite tables are invisible to every other reader
  and a pack cannot then be separated from its index by a copy. `osm::BuildNameIndex` (CLI:
  `fvnames build|info|search|drop`) is the stage-time builder. **Six things worth not
  re-deriving.** (1) *The builder does not read tiles* — it calls `VectorMapOverlay::ScanRows`
  (tier 1, index off) window by window, so which tag is the label, "unnamed geometry is never a
  row" and the merge are decided ONCE for both tiers; the merge therefore moved to
  `fvkit/vector/feature_rows.h` (`FeatureRow` + `MergeFeatureRows`, with a latitude sweep so a
  whole-pyramid merge is not quadratic — the surviving ref is still the first in INPUT order, which
  is what the pinned tier-1 results say). (2) *`min_zoom` IS the relevance function*: every level
  is scanned, shallowest first, a name keeps the shallowest it survived to, and `ORDER BY min_zoom`
  before the cap is why a one-row "kiawah" is **Kiawah Island** and not a street named after it —
  the same free ranking the tile budget gives tier 1, precomputed. (3) *The index NARROWS, the
  port's rule DECIDES*: every returned row is re-tested with `app::TextMatchQuality`, because
  FTS5's word boundaries and folding are not the port's, so a superset is the only safe direction.
  (4) *FTS5 is an optimisation, not a requirement* — `search_names` is a plain table and a reader
  without the module (or with a dropped mirror) scans it and matches in C++; same answers.
  (5) *A `FeatureRef` is not durable* (`tile` is a per-open index), so the index stores z/x/y + the
  layer NAME and the source interns them back into a live ref — an indexed hit is describable with
  no tile read until identify asks. (6) **A pack being READ cannot be written to**: a stepped
  SQLite statement holds a read transaction, and the write fails with `SQLITE_IOERR`
  ("disk I/O error") rather than anything that looks like a lock — `MbtilesFile::ReadTile` now
  resets once the blob is copied, and the builder resolves every ref and CLOSES the pack before
  writing. Measured on the Kiawah cut: 2283 pieces → 1441 names in 0.3 s, and over the whole island
  the index answers "kiawah island" with 9 rows where the budget-capped z13 scan finds 4 — the
  POIs it could not afford to look for. 29 gtests (10 `feature_rows_test.cpp`, 11
  `vector_map_overlay_test.cpp`, 8 `osm_search_test.cpp` over a real built index).
- **S4 (2026-08-28) is the ROUTABLE answer, the bindings, and a shell that asks.** The road
  provider is **`RoadGraphOverlay::AsSearch()`** and not a class of its own — the debug view
  already holds the `.fvroad`, is already in the stack and already packs an arc index into a
  `uint64` for its own picks, so it answers a search with the id a CLICK carries and
  `ArcInfoFor` serves both. **The graph's NAME TABLE is what makes a global text search
  affordable**: it holds each distinct name once, so the shared text rule runs over thousands of
  strings and the arc pass behind it is an integer lookup; a spatial query has no such shortcut
  and goes through the grid's `NodesInRect` (the box grown by the DRAW's own margin, since an arc
  is found through its endpoints either way), and a query with neither text nor area finds
  nothing, exactly as tier 1 does. **`RoadClass`'s declaration order IS the relevance function** —
  the enum is declared in descending importance and that ordering is part of the file format — so
  rows are ranked before the cap and a capped "main" is the primary road, not the service alley
  behind it. One road is one row through S3's `MergeFeatureRows`, which matters MORE here than in
  the tiles: OSM splits a way at every junction and the graph then stores every edge twice, once
  from each end (the mirror is also de-duplicated directly, so switching merging off does not
  double the answer). The class filter deliberately does NOT narrow a search: it is a rule about
  what is DRAWN. 16 gtests in `port/RouteKit/test/road_graph_search_test.cpp`, the last over the
  real `kiawah.fvroad`. **`kNearest` needed only BINDING** (S1 built it): `pyfvw.app` now has
  `SearchOrder`, `SearchQuery`, `SearchResult`, `SearchSession`, `text_match_quality`,
  `search_distance_meters` and a **`CancelFlag`** — a `std::atomic<bool>` with a Python face,
  since the seam's one async behaviour is a flag another thread raises — and the session releases
  the GIL for the walk. **`Overlay.search` joined the Python trampoline**, so A6's "a capability
  is a method you DEFINED" now covers all seven, and `VectorMapOverlay` is bound as
  `pyfvw.overlay.VectorMapOverlay`. **The shell lesson is that you can only search what is in the
  stack**: PythonView's two searchable non-overlays — the chart it draws through the map engine
  and the routing graph — are now wrapped and held NOT VISIBLE, which is decision 5 used exactly
  as written; the road overlay is created on the first search and the Overlays menu's toggle
  became a visibility flag for the instance a search made. **A search box wants THE VIEW as its
  default scope**, because a pack without S3's index answers a global text query with nothing at
  all and "Nothing found" is a bad way to learn that (the status line says so when it happens).
  Ctrl-F, incremental with a 200 ms debounce and synchronous — every measured query here is
  milliseconds, and the flag is bound and waiting for the shell that needs it. Two things the
  headless drive found: `self.vproj` has no bounds until the first render, so an "in view" search
  asked before one would hand every provider a zero-sized box; and framing a 40 m cul-de-sac is
  arithmetically right and useless, so `frame_result` takes `zoom`'s own 1e3..5e8 clamp. 7 pytest
  cases in `test_pyfvw_app.py`, and a `find_box` step in PythonView's `--selftest` (200 rows for
  the Kiawah view, 4 for "ruddy" — the road from the CHART and from the GRAPH, both true).
- **A6 is the ADOPTION**: `pyfvw.app` binds the layer, PythonView IS an `AppShell`, and
  `fv::PointOverlay` is the first C++ FILE overlay. **A capability is a method you DEFINED** (the
  Python trampoline inherits every capability and answers each accessor from what the subclass
  defines, cached per instance); **a Python overlay made by a FACTORY needs an aliasing
  `shared_ptr`** (`OverlayFromPython`) or it draws and silently answers no picks; **an editor is a
  duck-typed PROXY**, unwrapped wherever the API hands one back.
- **`fv::PointOverlay`** reads a `.fvpoints` **SQLite** document — a real schema somebody else can
  write, which is what makes evolving the dataset INSERTs rather than a parser. **Schema 2 put the
  ARTWORK IN THE DOCUMENT**: a `symbols` table of PNG blobs, separate because many points share one
  symbol, so the file opens with its symbology on a machine that has never seen the icon set. A
  point draws as a BADGE (its shape in its colour, tile centred on top) because icon sets are
  black-on-transparent; alpha 0 asks for the bare icon. `EmbeddedSymbolLibrary` is the third form of
  `ISymbolLibrary` after directory and sheet. **A schema-1 document still opens** and is saved
  forward.

### 1c. Vector products, all three on that one seam

- **DNC/VPF** — `port/VpfMapServer/` (reader, vector source incl. areas, VDT identify) +
  `port/GeoSymServer/` (rule tables, CGM symbols, `GeoSymStyleEngine`).
- **ENC/S-57** — `port/Enc/` (ISO 8211, S57Cell, Appendix A catalogue, S-52 PresLib,
  `S52StyleEngine`, raster symbol sheet, enumerator/registration). **Text is its own band** (E8):
  `kS52PrioTextBase + the object's priority`.
- **OSM** — `port/Osm/` (MBTiles + MVT + `OsmVectorSource` + `OsmStyleEngine`, a MapLibre style-JSON
  loader over `LookupTableStyleEngine`; reference style `port/Osm/styles/peregrine-osm.json`,
  documented subset in `styles/style-readme.md`; sprites via `PngSymbolLibrary`'s sheet form, where
  `icon-image` and `fill-pattern` are `{tag}` TOKEN TEMPLATES and a pattern's spacing and its stamp
  scale must carry the SAME factors).

### 1d. Routing (`port/Routing/`, O4–O5e) and RouteKit (`port/RouteKit/`, P5)

A routable road graph built **OFFLINE from a raw `.osm`/`.osm.pbf` extract** — never from the MVT
pyramid, which is simplified, tile-clipped and has no node identity. `fv_osm_reader.h` (expat XML +
protozero/zlib PBF behind one `OsmSink`, nodes/ways/**relations**), `fv_road_graph.h` (noded graph,
`.fvroad` **v2**, grid nearest-node index, turn restrictions), `fv_router.h` (bidirectional
Dijkstra, the unidirectional one kept as the tests' oracle). CLI `fvgraph build|info|route`.

- **Profiles** (O4b) — driving on the posted clock, walking, cycling — gated by **per-mode access
  bits carried ON the arc**, so one general graph answers all three.
- **`RoadGraph::NodesInRect`** (2026-08-28) — every node in a box, over the grid `NearestNode`
  already built, splitting a crossing rect at the antimeridian so nothing is visited twice. Added
  for `fv::RoadGraphOverlay`, and it is four lines rather than a second index for that reason. It
  uses `floor()` and not a cast: a box west or south of the graph gives a NEGATIVE cell index, and
  truncation-towards-zero would turn -0.4 into cell 0 and silently widen the query to the graph's
  own edge.
- **Profile-aware snapping** (2026-08-27) — `RoadGraph::NearestNode` takes an optional
  `NodeFilter`, and `Router::Route`/`RouteVia` pass one that accepts a node only when at least one
  arc incident to it is `ArcUsable` under this query's profile. **Why it had to change**: graph
  nodes are junctions and endpoints ONLY (shape points are edge geometry), so the nearest node can
  be well away and was chosen on distance alone — on Kiawah that put a bicycle's start on a golf
  cart path 138 m off tagged `bicycle=no` while the cycleway 26 m away went unseen, and the router
  then reported "not connected", which was true of the nodes it picked and false of the map. The
  filter is asked BEFORE the distance is kept, so a refused node neither wins nor tightens the
  ring-stopping bound. It **falls back** to the unfiltered nearest when nothing usable is in range,
  so the caller still gets a snap offset and a "not connected" answer rather than a coverage error
  pointing at the wrong thing. The test that broke was pinning the OLD behaviour: RouteStore's
  "waypoint in the river" point sat 392 m out, INSIDE the 500 m snap radius, and only failed
  because the node it reached was barred — it now uses a point 2.6 km out, which is what its name
  always claimed. 3 new gtests (`RouterSnap`), 162 routing tests green.
- **Mid-road snapping** (2026-08-28) — **the route now begins where you stand.** Two halves, and the
  second is the one with teeth.
  - **`RoadGraph::NearestArcPoint`** — a point-to-segment search returning an **`ArcSnap`** (arc,
    both ends, the projected point, `along_m`/`length_m` and so the fraction), over a **second grid
    built in `Finalize`, over arc GEOMETRY**. It has its own bounds and not `bounds_`: that box is
    the box of the VERTICES, and a road's shape can lie outside its own endpoints' box (a dogleg, a
    bay, and in the degenerate case of one east-west road a node box with no height). One entry per
    undirected road; `ProjectOntoSegment` does the projecting — there is one copy of that, on
    purpose — but the segment LENGTHS are `GreatCircleMeters`, because the fraction is what prices a
    partial arc and has to be taken in the same metre `RoadArc::length_m` was built in.
    `at_node()` carries a **1 cm tolerance and it is load-bearing**: coordinates are stored as
    degrees x 1e7 and read back as `e7 * 1e-7`, so a query that IS a junction projects a picometre
    along the road leaving it, and without the tolerance every such call produced a mid-arc anchor
    with a zero-length first step and a phantom road name in the leg list.
  - **A router that begins and ends mid-arc.** `RouteAnchor` is either a node or a point on a road,
    and every stop is one — the node API and the geographic one are now the same code with a
    different snap. **Mid-arc routing is a SEEDING change, not a search change**: a mid-arc anchor
    offers up to two `Terminal`s, one per end of the road it may set off toward, each carrying the
    price of the part of that road it would use, and both frontiers are still plain Dijkstra started
    from a set. A terminal's cost is a constant on one side's distances — exactly what a super-source
    joined by weighted edges contributes — so the bidirectional stopping rule is the one it always
    was. `Step` grew `t0`/`t1`, fractions in the arc's own direction, which is where `stored_order`
    went: a partial traversal and a reversed one became the same kind of thing instead of two cases.
    **The one answer no search can find is both ends on the SAME road** — the path never reaches a
    junction, so no frontier ever settles anything — and it is taken directly, which is safe because
    it is unconditionally optimal when legal: every alternative leaves the road at one end and comes
    back, paying for more of that same road plus whatever lies between.
  - **A U-turn at a mid-road stop is not a turn**, so it is barred by DROPPING a source (the end the
    previous leg came from) rather than by banning a turn — simpler than the junction case and it
    needs no state space of its own. The escape is the same one O5d already allows: refused on the
    spot, the route drives to the next real junction — or to the end of the cul-de-sac — and turns
    THERE.
  - `Route` gained `start_arc`/`end_arc`, `start_point`/`end_point` and `stop_points`;
    `stop_nodes[i]` is **kNoArc for a stop that landed mid-road**, because naming the nearest
    junction would name a different place from where the route passes. `start_node`/`end_node` are
    now the first and last JUNCTION the route reaches, and a route that never leaves one road
    touches none. `RouteOptions::snap_to_arcs` (on) turns it off as a diagnostic, and `fvgraph route
    --snap-nodes` is that flag: a route that changes when it is cleared is a route the snap was
    moving.
  - **Measured on Kiawah**, 1180 random bicycle queries: median snap distance **122 m -> 77 m**,
    mean 136 -> 100, nearer in 821 of 1180; over 855 connected pairs the route's own length changed
    in 848 of them, by a median of 17 m and by as much as 3.7 km where the nearer road is a
    different road. 14 new gtests (`RoadGraphArcSnap`, `RouterArcSnap`, `RouterViaArcSnap`), 137 in
    fv_routing_test, 1680 green.
  - **`fv::NormalizeHeadingDeg` became inline** in `fvkit/nav/position.h` to pay for this.
    `ProjectOntoSegment` is header-only precisely so `port/Routing` can project onto a road without
    linking the map engine, and it ends by normalising a bearing — that ONE out-of-line symbol was
    enough to fail the `fvgraph` link the moment `fv_road_graph.cpp` used the projection.
  - **STILL OPEN**: the audit tool in §2a's **D1** row, which is what finds the rest of the cases
    this kind of defect hides in.
- **Turn restrictions** (O5a) — resolved onto `(via_node, from_arc, to_arc)`; the searches label
  **states**, so only the junctions a restriction names are split and an unrestricted graph costs
  what it did in O4.
- **`access=private` is priced, never deleted** (O5b) — on a gated community it IS the street network.
- **Cost rules** (O5c) — every weight, speed and penalty in `rules/route-weights.json`, reread by
  `RouteRulesFile` on an mtime+size poll so weights are tuned with the app running; a bad file is
  rejected whole and the loaded rules stay in force. `RouteRules::Builtin()` reproduces O5b exactly.
- **Ordered stops** (O5d) — `RouteVia` is ONE route through the waypoints: a leg is seeded with the
  arc the previous one arrived along, so the turn machinery binds signage at the stop for free and a
  U-turn out of a stop is expressed as a barred turn.
- **Ferries and tolls** (O5e) — `route=ferry` enters as **`RoadClass::kFerry`**, a class and not a
  flag, because the speed differs (the crossing's own `duration` over its own length, returned by
  `ProfileSeconds` **whatever profile is asking** — you do not walk a ferry). `toll=yes` is the
  opposite shape and is a bit. Both are avoided through `RouteOptions::{toll_penalty,ferry_penalty}`,
  the ONLY profile-backed settings the router reads from the options rather than the profile.

**RouteKit** (`port/RouteKit/`, P5) — **the module that exists because of the invariant above it**:
fvkit does not link `port/Routing`, a route overlay needs both, so it lives beside them and links
each. Three pieces, all `fv::` (D5), all mac-tested, and none of them iOS-only.
- **`fv::RouteDoc`** reads and writes `.fvrte`, **byte-compatible with route.py's `json.dump`**.
  That is a stronger claim than "both emit valid JSON" and it is what makes a route saved in one
  shell open in the other. Reading is nlohmann's; **the WRITER is hand-rolled**, because
  `json.dump(indent=2)` is a specific serializer — insertion-ordered keys, `[]` on one line but a
  non-empty list on many, `ensure_ascii` with **surrogate pairs** above the BMP, and floats as
  CPython's `repr`: shortest round-trip digits, fixed notation when the decimal point falls in
  (-4, 16] and scientific outside it. `PythonFloatRepr` is that rule, public and tested; it was
  fuzzed against CPython over 120,000 doubles (random bit patterns included) with zero mismatches.
  **The road geometry is NOT in the document** — it is derived from the graph and the rule file,
  either of which can change, so a saved copy would be a stale answer wearing a document's clothes.
- **`fv::RoutePlanner`** — the graph it loads is **SHARED, not owned outright** (P7): `graph()`
  hands out a `shared_ptr<const RoadGraph>` so `RoadSnapper`'s network can index the same roads the
  router routes over. One `.fvroad` per application, and no way for the two halves to disagree
  about what a road is. It is route.py's `follow_roads` moved verbatim rather than improved, because
  the two shells have to agree what a route IS: one `RouteVia` through the ordered stops, falling
  back to **independent PAIRS** when the through route cannot be had at all, a pair that will not
  route staying a **straight leg**, and `kOutOfCoverage` as the ONLY per-leg failure — anything else
  (above all a profile the rule file does not define) is about the REQUEST and is reported once
  rather than hidden as a screenful of straight lines. The graph loads **lazily and once**; the rule
  file is polled per plan, so an edit lands on the next route with nothing restarted.
- **`fv::RouteOverlay`** is the **second C++ file overlay** and the first that draws something
  COMPUTED: `Persistence` + `HitTest` + `SnapTo` + `EditTarget`, route.py's look (blue over a white
  casing and dashed for a bicycle route when calculated — but see §2d, the dash is NOT VISIBLE at
  the shipped width; the overlay's own red legs when not, in whichever `LineKind` `SetLegKind`
  says), G4 selection as a render state, and `SetWaypoints` as the wholesale surface a sheet-driven
  UI produces, which is what Pippin drives. Replacing
  the waypoints DROPS the plan, because a stale road under a moved marker reads as a bug in the
  router — which is why **`FileOpen` drops it too, and why "reload at launch" is TWO steps**: P6's
  `pippin::RouteStore` opens AND replans, since a shell that only reads the file shows the straight
  red legs of a route nobody has computed. The hit test reads **what was drawn**, so an
  overlay that has never drawn answers no pick. `RegisterRouteOverlayType` is separate from
  `RegisterBuiltinOverlayTypes` and **cannot be part of it** — that function lives in fvkit — and it
  carries the shell's ONE planner to every instance the factory makes.
- **`fv::RouteEditSession`** (`fv_route_edit.h`, 2026-08-27) — **the editor, and the reason it is
  here is that there were two of them.** P5's header said the editor was deliberately left in
  route.py; the cost showed up the moment Pippin drew the same document, because the desktop had a
  drag and the phone had wholesale replacement, and two shells were on their way to two answers for
  "what does dragging a waypoint mean". This is route.py's editor TRANSCRIBED, not redesigned:
  select; the press-move-release drag with its **3-pixel Manhattan threshold** (below it a press is
  a CLICK, so selecting costs no undo entry and does not dirty the document); **one undo snapshot
  per drag, taken at the first movement rather than at the press**, so a whole drag undoes as the
  single thing the user did; a cancel that SPENDS that snapshot, so a cancelled drag leaves no
  history at all; armed add-mode (the key arms, the click supplies the position — a click has to
  keep meaning "select"); delete; undo/redo over whole waypoint lists; and route.py's Escape order,
  which unwinds one thing at a time (drag → mode → computed road → selection) and DECLINES when
  there is nothing left, so the app can quit on it.
  **Two ways in, and both are the same edit**: `OnMouseDown/Move/Up` + `OnKeyDown` for a shell whose
  stack routes events (PythonView), and `BeginDrag`/`DragTo`/`EndDrag` plus the named commands for a
  shell with its own recognizers (Pippin — whose still-open route-point drag is now a wiring job
  rather than a second editor). 23 gtests in `test/route_edit_test.cpp`.
- **Every position the editor produces is SNAPPED first.** `ResolvePixel` asks
  `fv::app::SnapCandidates` (P19) before falling back to un-projecting the pixel, so a waypoint
  dropped over a `.fvpoints` marker takes that marker's SURVEYED coordinate rather than a pixel's
  worth of precision — measured in PythonView against the sample document: a drag onto `Ruddy
  Turnstone` gives `32.6044007, -80.1083007`, identical to the row in the file. **Two things worth
  not re-deriving.** (1) **The overlay excludes ITSELF from its own snap walk, and without that one
  line no drag can move**: `SnapToPoint` answers out of what was DRAWN, the dragged waypoint IS
  drawn under the cursor at distance ~0, and it is therefore first in a nearest-first list on every
  frame — so the marker snaps to itself and sits still. Excluding the whole overlay rather than only
  the dragged label is also right on its own terms (two waypoints of one route collapsing onto each
  other is not an edit anybody asked for), and snapping to ANOTHER overlay's answer — P19's stated
  payoff of starting a route where the last one ended — is untouched. (2) **The overlay keeps a COPY
  of the projection each frame was drawn with**: the input SPI carries no projection, and keeping
  the borrowed reference is wrong because its lifetime is the caller's business (PythonView replaces
  its whole engine, and its projection with it, on any catalog change). A shell sets
  `snap_tolerance_px` — 0 switches snapping off — because the core reads no settings file.
- **`fv::RoadGraphOverlay`** (`fv_road_graph_overlay.h`, 2026-08-28) — **the routing network drawn
  as ITSELF**, and the first debug view of the graph rather than of a route over it. Every arc in
  view as its own geometry (shape points, not a chord) coloured by `RoadClass`, a white dot at
  every graph NODE, a legend of the classes actually on screen with per-class arc counts, and a
  pick that answers what the ROUTER sees at a pixel — class, direction, length, speed, and each of
  bicycle/foot/motor in the graph's own three-way terms (allowed / private / denied). It exists
  because a wrong bicycle route is never a question about the chart: the chart and the graph are
  different objects, and they disagree exactly where the bug is — a cycleway drawn and not in the
  graph, two halves of a path that look joined and share no node, a `bicycle=no` service road
  rendered identically to the one beside it. **Four things worth not re-deriving.**
  (1) **Every edge is two arcs and drawing both paints every road twice**; the canonical one is
  the lower source id, EXCEPT when the other endpoint is outside the query box — nobody else will
  ever visit that edge, and without the exception every road leaving the viewport disappears.
  (2) **The query margin is a FRACTION OF THE VIEW, floored at 500 m and capped at 40 km.** It was
  2000 px first, which fetched 26 screens of Kiawah to draw one: a pixel margin reaches further
  the more you zoom IN, which is backwards. A fixed metric margin is wrong at both ends for the
  mirror reason. The longest arc worth catching is a fraction of what you are looking at.
  (3) **`path` is PINK because it was blue**, and blue is what a chart paints water — on Kiawah a
  light-blue path was invisible against the lagoon it runs beside. A colour is only distinct if it
  is distinct from the MAP, not just from the other twenty-one entries in the table.
  (4) **`HitItem::feature` is PACKED, not minted** — the one case A5's rule allows, because a node
  id and an arc index already ARE the graph's identifiers; the top bit says which kind. A NODE
  outranks an arc rather than winning on distance: every node sits on an arc, so nearest-wins would
  make the dots unclickable. Registered as a STATIC type at display order **900 — under the route
  (1000)**, or a debug view of the roads would hide the answer it was opened to explain.
  `RoadClassColor`/`RoadClassWidth` are free functions so a shell's own list of classes cannot
  drift from the picture. 19 gtests in `test/road_graph_overlay_test.cpp`.

### 1e. Apps and bindings

`port/bindings/pyfvw` (`pyfvw.{vector,catalog,engine,overlay,canvas,routing,draw,symbol,geo,nav,app}`
+ `pyfvw.Settings`), `port/apps/PythonView.py` (the tk application, and an `AppShell` — since MM7 it opens all three
moving-map feeds: File > Open Track for a `.gpx` or an NMEA log, Overlays > Moving Map Modes >
Connect NMEA Feed for a live one, and Use The Demo Feed to come back. The feed is a TUPLE
(`("demo",)`, `("track", path)`, `("tcp", host, port)`, `("udp", port)`) dispatched at one
`set_source`; a track file wins over a live host at startup; **an empty host in the dialog is the
UDP listener**, which is the one field the two shapes of phone app differ by.
**Overlays > Road Graph (debug)** since 2026-08-28 — `pyfvw.route.register_road_graph_overlay_type`
carrying the app's ONE planner, so the picture is of the graph this application actually routes on.
**REGISTERING A TYPE PUTS NOTHING IN THIS APP'S MENU**, and the comment beside the crosshair saying
"the Overlays menu is now the registry" is aspirational rather than true: the menu is hand-built
`add_checkbutton`s over `tk.BooleanVar`s applied by `_ui_apply_overlays`, so a new toggle is four
touches — the var, the entry, the apply line, and a `first_of_type` accessor. Worth knowing before
the next type is added believing otherwise. The toggle is `_set_road_graph` and not a bare
`_set_static` because **the planner loads its graph LAZILY, on the first route**: an overlay
switched on before anybody has routed would draw a blank screen and look broken when in fact
nothing had opened the `.fvroad` yet, so turning it on loads the graph and a graph that will not
load is reported AND unticks the box. Hovering already put the overlay's own one-line summary on
the status bar through the existing `PickSession`; a CLICK adds a full multi-line dump on stdout
(`_dump_road_graph_hit`) — the status line is what you read while moving, the dump is what you
paste into a bug report, and it prints BOTH the arc and the node rather than whichever won the
pick, because the interesting cases are exactly the ones where they disagree),
`port/apps/route.py` (**209 lines on 2026-08-27, down from 963**: the tk tool palette and the
type descriptor, and nothing else — the overlay it used to BE is `pyfvw.route.RouteOverlay`.
`pyfvw.route` is RouteKit bound, which nothing had done before because pyfvw did not link
`fv_routekit`; that unbuilt bridge, and not a design choice, is why there were two route
overlays. What is given up with it: nothing in Python implements `EditTarget` + the mouse SPI
any more, though the Overlay SPI itself is still exercised from Python by `Crosshair`,
`CoverageOverlay`, `TrackPointsOverlay` and the pytest subclasses),
`port/apps/Pippin/` (the iOS shell — as of P7 an app with a ship on it, a route you can make and a
map that follows the ride:
`Pippin.xcodeproj` with a SwiftUI target over the `PippinKit` ObjC++ framework, where `PPMap` hands
Swift a `CGImage`, `PPViewport` is the camera as an immutable value, `PPLocationSource` turns
CoreLocation into `PositionFix`es for a `MovingMapOverlay` in the stack, and `PPRoute`/`PPWaypoint`
carry a route out to a SwiftUI sheet; its README is the door. **The mac build now configures
`port/apps/Pippin` too**, for gtests over the pure-C++ half of PippinKit — the CoreLocation
sentinel table (P4), `pippin::RouteStore`, the route document's whole lifecycle (P6), and
`PPCameraFit.h`, the GPS-mode zoom-out rule (P7) — and returns before any of the iOS targets), plus the `fvrender`, `fvpack` and `fvgraph` CLIs — all three are
**host tools that MAKE the data a phone reads**, and the iOS build skips them for that reason. Overlay types: `app.crosshair` (static, top-most, restored
at startup) and `app.coverage` from the app; `fv.grid`, `fv.points`, `fv.movingmap` and
PythonView's `fv.route` from the port.

### 1f. Test data (`testdata/` on disk, git-ignored, all present — see §2d on the spelling)

dted · geotiff DOQs · **`geotiff/Atlanta SEC.tif`** (a current FAA VFR sectional, dropped
2026-08-06, first read 2026-08-27 — 17951x12354 LZW 8-bit palette on Lambert Conformal
Conic 2SP, the only LZW and the only LCC sample in the tree; its bbox is the whole SHEET,
legend panel and margins included, so it reaches ~1 degree past the charted neatline on
every side) · rpf CADRG · tiros3 · `vpf/dnc17` · `VPF 2/WVSPLUS` ·
`GeoSymbol/{SymAssign,Graphics}` (DataDir = `TestData`) and `GeoSymbol/makiPng` ·
`OSM/map*.osm` (adjacent, OVERLAPPING Kiawah Island exports — **enumerate them, never name them**;
refreshed 2026-08-17, the third cut) · `OSM/kiawah.fvroad` (a BUILD ARTIFACT, read by the app and by
no test — and the 2026-08-17 one is `--ignore-access`, see §2d) ·
`OSM/kiawah.mbtiles` (a BUILD ARTIFACT, 119 tiles z0..z14 over the Pippin bbox. **REBUILT
2026-08-27 and NO LONGER A CUT**: it is now tilemaker run straight at
`--bbox=-80.17,32.55,-79.97,32.67` over a newer `us-south` pbf — 2.49 MB, up from the 1.9 MB
`mbtiles_cut.py` cut — which is a legitimate way to make it and leaves `us-south.mbtiles` in the
tree a different vintage. `OsmRender.KiawahCutMatchesSource` pins the pack pixel-for-pixel against
its source rather than against a hash, so it now ASKS FIRST whether the pack is a cut of this
source (are the shared z14 tile blobs byte-identical? measured that day: 0 of 16) and SKIPS with
that count when it is not, rather than failing as though the renderer had moved. **The open
question is Chris's**: if the pipeline is now tilemaker-with-`--bbox` rather than tilemaker-then-cut,
`mbtiles_cut.py` has no caller and the test has no subject — the repair is then either to re-cut
the pack from this us-south, or to give the test a cut it makes itself at test time (sqlite3 is
already on `fv_osm_test`'s link line, and DATA-1 wrote a four-tile MBTiles with it for exactly this
kind of reason). Do not "fix" it by re-pinning a hash: P1 chose agreement-with-its-source
precisely so the test would never need re-pinning when us-south is re-cut) ·
`OSM/us-south-260728.osm.pbf` (4 GB raw extract) ·
`kiawah_cycle.gpx` (MM6's GPX fixture, and since P4 also **staged into Pippin's pack** as its demo feed — a real 28-minute ride on Kiawah, **1705 points at 1 Hz**,
1704 s, no gaps and no repeated stamps, exported from a Garmin FIT file; its elevation runs 9.4 m
down to **−6.2 m**, which is what makes an unsigned or clamped `<ele>` show up) ·
`OSM/mbtiles/us-south.mbtiles` (4.4 GB, 4,872,934 tiles, **re-cut 2026-08-17** — ocean merged and now
running east to the Greenwich meridian, 17 layers with `man_made` new; the rebuild recipe is in the
archive and a rebuild without the coastline shapefile silently loses the sea) ·
`enc/` (the **8** Charleston cells bands 2–5 the ENC goldens are pinned over, the other **815** in
the sibling `TestData/enc-archive/` — see §2d — plus `chartsymbols.xml`, `s57objectclasses.csv`,
`s57attributes.csv`, `s57expectedinput.csv`, `rastersymbols-{day,dusk,dark}.png`).

---

## 2. Open work

### 2a. Active track — next sessions, in order

| # | Session | What it is |
|---|---------|-----------|
| **P9 (snap points)** | Pippin, the iOS app — everything left is OPTIONAL | **P1–P8, P12, P13 and P9's POINTS half are built**: the data pack, the iOS cross-build, the Xcode project, `PippinKit`, a styled Kiawah, a map that pans and pinches, an ownship riding it off either feed, `port/RouteKit/`, a route the user can make on the phone, a map that follows the ride, and (P8, 2026-08-19) **the trip computer** — `fvkit/nav/trip.h`, 27 tests, and the ride bar now showing the RIDE's numbers rather than the ROUTE's. **Pippin now meets every hard requirement in `port/apps/Pippin_requirements.md`**; P9 (compass + `.fvpoints` snap points), P10 (GPX record + share) and P11 (the pick button naming the nearest road/path) are all listed there as optional, so a Pippin sitting is a CHOICE rather than a queue — **P9, P10 and P11 are now built, and only P9's snap-points picker is left**. Read `port/pippin-plan.md` §P9–§P11 and `port/apps/Pippin/README.md`, in that order. **P9's POINTS half is built (2026-08-20)** and Chris asked for it by name, compass deferred: schema 3 puts `phone` and `url` on a `.fvpoints` row; `PointOverlay` gained `SetSymbolDpiScale` and `UpdatePoint`; `PPPointStore.{h,cpp}` is `PPRouteStore` for points (seed from the pack once, write on every edit, nearest-wins hit test) with 15 mac tests; and the shell gained a points button beside Route, a tap recognizer, an info sheet whose phone and URL are `Link`s, and an editor with a shape/colour picker, a Location row and Delete. **Two shell changes the same day, from Chris using it**: adding is now the points button HELD DOWN rather than a small `+` (a hard press is the same gesture; armed only while the points are shown; the tap that follows a hold is stepped over by a timestamp the gesture leaves), and the editor's Location row IS the route sheet's stop row (`LocationRow`) — so a point can be placed at the phone's own fix, and P11's "say the road, not the numbers" is a change to `LocationText.describe` + `PickOverlay.readout`, both shared by the route and the points. **The plan said the add/edit UI was OUT because "that is where A4's editor machinery would enter" — it did not**: A4's `OverlayEditor` is a desktop MODE object and a phone has a tap and a sheet instead, so nothing from A4 was built. **P9's COMPASS half went in the same day (2026-08-20), and with it THE LAG.** Chris asked for three things in one sentence and all three are built: the compass toggles north-up/course-up, course-up centres and orients continuously, and two fingers rotate the chart when the map is not following. **The lag was the APRON, not the slew**: MM2's track-up apron is a `W/5 x 2H/5` box with the ship anchored at its lower edge, and `MovingMapCamera::Update` returns early while the ship is inside it — so a rider heading up the screen crosses a third of a screen, about a minute of riding, before the map moves OR the chart turns. Course-up switches it off (`CameraModes::continuous`); north-up keeps it untouched, which is the half Chris asked to keep. Continuous centring then costs what MM3's header says it costs unless the animation is retuned with it, so the follow slew is `kLinear` and lasts **one MEASURED fix interval** — shorter stutters once a second, longer trails permanently, equal slides at the rider's own speed — and the measurement is `PPFollowCadence.h` (a running average over the ticks that SAW a fix, because `Tick` drains its queue in a loop; a gap resets rather than averages), 12 gtests, because a stutter and a trail are both invisible in a still. **The one line nobody will re-derive**: north-up follow has to PIN the chart at north, since MM2 preserves the current turn when `auto_rotate` is off — and `SetRotationSupported(false)` also does `slew_.Reset(center, 0)`, so the slew ALONE must then be told where the chart really is (`slew().Reset`, never `ResetMap`) or the chart SNAPS to north instead of unwinding (`PPMap.mm`, `applyRotationPin:`). Two-finger rotate is `PPViewport.rotated(by:about:)` plus a recognizer with a 12-degree dead zone (only what is PAST the wall is applied, so it neither turns on an accidental pinch-twist nor jumps when it arms), refused in GPS mode by `MapModel.rotate`, and the slew's re-base learned the ROTATION in the same session (compared the short way round) because a twist moves the chart behind its back exactly as a drag moves its centre. What P9 still owes is only the snap-points picker in the route sheet (the document is in the app and `MapModel.points` is published, so the picker is Swift with no C++ under it). **P10 IS BUILT (2026-08-21)** and it is three things. (1) **`gpx.h` writes as well as reads** and **`gpx_recorder.h` APPENDS** — both in §1a's nav bullets, and the recorder's footer trick is the only clever thing in it. (2) **It plugged into the app in ONE LINE because P8 had already built the socket**: `feedTrip:` is now `consumeRawFix:` and feeds the recorder beside the trip computer, since it was already the one method both feeds pass through, taking the RAW fix and gating a live fix's double arrival by timestamp. **Recording deliberately does NOT require GPS mode** — *follow me* and *keep this* are different questions — but it does require a feed. (3) **A single point shares too** (Chris asked in the same sentence): `PPMap.writePoint(_:toGpxURL:)` is a CLASS method, so it is the one thing on that class exempt from the render-queue rule; "Open in Maps" is a `maps:` URL and not a share at all, while "Share" carries TWO items — a `.gpx` for a recipient with a mapping app and an `https://maps.apple.com` link for one with only a phone. **Two things worth not re-deriving.** The DEMO feed records what the TICK saw (a scripted replay is polled inside `tickMovingMap`, and `Tick` drains its queue reporting only the last fix), so a recorded demo has gaps where the display link was parked; a LIVE receiver's fixes arrive through `pushFix:` one call each and none are lost. And **the one bug this session found was P9's**: `CircleButton`'s tap-versus-hold guard was a timestamp honoured for one second, so **a hold longer than a second fell through to the tap** — invisible while it merely toggled the points, and *stopping the ride* once the record button had it. The flag is now cleared when a press BEGINS (a zero-distance `DragGesture` is SwiftUI's only spelling of "touch down"), which closes that and the stale-flag leak the timestamp existed to avoid. **P12 (2026-08-19) is built and was not one of them**: the two findings from Chris's first real-device run, both of them the shell counting in the wrong unit — the map was drawn 5.09x heavy and 1.58 zoom levels in because an authored pixel was taken to be a surface pixel rather than a POINT, and the app opened fitting the pack rather than filling the screen. Shell-only, no core change, no golden moved (§2b). **P13 (2026-08-19) is the same shape**: five look-and-feel changes Chris asked for after riding it — the zoom-in limit two levels deeper (1:1,614), the route line doubled (`fv_route_overlay.cpp`, the only change outside `PippinKit` and the same device-pixel-vs-point error P12 found one layer down), the ride bar at 26 pt split into an upper-left "what has happened" and an upper-right "what is left", the app icon (`art/make_icon.py` squares the artwork's rounded corners because iOS masks the icon itself), and Chris's edited `style.json` committed so `stage_data.py` ships it. **P11 IS BUILT (2026-08-20), the same day as both halves of P9**, and it is one paragraph: the pick button and the two sheets' Location rows say WHERE instead of printing a lat/lon. **The whole difficulty is the word "usable".** The snap index is built at `RoadSnapFilter::kAll` — it must be, or the ship would never snap to the cycleway it is riding on — so the NEAREST candidate is very often a road the selected profile refuses, and Kiawah's own case is the proof: **the parkway carries `bicycle=no` for its whole length** and the island put a cycleway beside it, so standing ON the parkway the button must read "cycleway" for a rider and "Kiawah Island Parkway" for a car. Making that true rather than approximate is the one structural change this session made anywhere outside Pippin: **`Router::ArcUsable` became the free `fv::routing::ArcUsable`** (the member calls it; the search reads better as a member) and **`RoutePlanner::BuildOptions` became public**, so the label is priced by the same rule file, the same profile lookup and the same poll as the replan that follows it — not by a second copy of the router's rules that could drift. `private` still counts as usable (O5b prices a private road rather than deleting it; on a gated island they ARE the street network). The rest is `pippin::PlaceDescription` + `RouteStore::DescribePoint(p, max_m, profile, remember)` with 10 mac gtests, `PPPlace` across the bridge, and the English composed in `LocationText` — a STRUCT rather than the plan's `std::string` because the button says "Use Flyaway Drive" and the row says "Flyaway Drive" and neither should have to slice the other's sentence apart. **Three things worth not re-deriving.** (1) **"No usable path" is a LABEL AND NOT A BLOCK** — the button stays enabled and the pick lands, because a rider aiming at a beach means it; a pack with no `.fvroad` says the same thing and is right to. (2) **A ROW IS NOT A CROSSHAIR.** Only the crosshair remembers (`remember` false for a sheet), because only a moving crosshair can blink — at a junction the nearest of two roads metres apart flips as the map drifts a pixel, so the road named last is kept while a different one is within `routing.pick_stay_bonus_m` (10 m); a row looking up a stop a mile away would hand that memory a head start somewhere else. And a row with no road near it falls back to the NUMBERS, not to the button's label: "No usable path" is right about a place somebody is still choosing and says nothing about a place they have already chosen. (3) **`pippin.ini` carries the radius** (`routing.pick_radius_m`, 50 m), deliberately NOT the snapper's `max_radius_m` — 60 m there is a GPS error budget and this is a thumb. What P8 leaves on the desk, none of it blocking: **the odometer's anchor floor is one number (`min_movement_m`, 2 m) serving a walker and a parked bike alike**, measured at 2.85 m of loss over a 6.1 km ride — right for this fixture, unmeasured for a receiver worse than the one that recorded it; **the distance-to-go projection is stateless and takes the NEAREST segment**, which a route doubling back on itself can put on the wrong limb (the fix, if it is ever wanted, is a search window around the last projection and NOT a progress ratchet — `trip.cpp` says why); and **`build-xcode/` is a trap left by an earlier session** — `xcodebuild` with no `-derivedDataPath` writes to DerivedData, so installing from `build-xcode/` runs stale code after a build that said SUCCEEDED (P8 lost time to exactly that; the README now says so where the install command is). **P14 IS BUILT (2026-08-21)**, and it is the only Pippin session so far that added a TARGET: a share sheet lists app extensions and nothing else, so receiving a place from Apple Maps means `PippinShare.appex` embedded in the app, and the pbxproj grew a native target, an Embed App Extensions phase and a second bundle id by hand. **The wrinkle nobody should re-derive**: an Apple Maps share carries `coordinate=` on iOS 18 and in the iOS 26 SIMULATOR, but an iOS 26 DEVICE share is often `https://maps.apple/p/XXXX` with nothing in it, and the coordinate lives only in an intermediate hop of its redirect chain (the LAST hop is a page that says the link is unsupported) — same for every Google `maps.app.goo.gl` link. So `PlaceLink.resolve` sniffs redirects, it is the only network call in Pippin, and `PlaceLink.allowsNetworkLookup` is the one constant that turns it off. **The other thing worth keeping**: a Google `/maps/place/…` URL holds TWO coordinate pairs — `@lat,lng,z` is the camera and `!3d…!4d…` is the place — and reading them in the wrong order puts the marker a screenful from where the user pointed. The parser is Foundation-only on purpose (`Pippin/PlaceLink.swift`, compiled into both targets), so its table of real share URLs is 56 checks under `ctest -R place_link` rather than something to try in the simulator. **P15 IS BUILT (2026-08-25) and is one paragraph**: Chris rode it and the phone hit its lock screen, because no line in the tree had ever touched `isIdleTimerDisabled`. The reason it is a BUG and not a preference is the entitlements — WhenInUse plus no `UIBackgroundModes` means a lock backgrounds and then SUSPENDS the process, CoreLocation stops, and a ride being recorded gets a hole in it with nothing in the GPX saying so. `MapModel.updateIdleTimer()` sets the flag to `gpsMode \|\| isRecording` off a `didSet` on both, so a mode added later cannot forget it. **Two limits worth not re-deriving**: it suppresses the IDLE countdown ONLY (a deliberate side-button press locks the phone and no app overrides that, so a screen-off recording is still gapped — background location remains explicitly out of v1), and iOS consults the FOREGROUND app's flag, so Pippin's stops applying the instant it backgrounds and cannot hold another app's screen on. **P16, P17 AND P18 ARE ALL BUILT (2026-08-25), asked for in separate sessions and in that order** (`port/pippin-plan.md` §P16–§P18). **P16 is one paragraph**: every fix called `setContentDirty()`, so an ownship nobody could see cost a full rasterize; the gate is `RenderGate.swift` — a value with ONE BIT of state, tested off the phone because both of its failure modes are invisible in a still. The bit is the part nobody should re-derive: the test runs on EVERY fix even when the render does not (it is what notices the ship returning), and the DEPARTURE gets a frame of its own (`visible || wasVisible`) or the stale chevron stays pinned half on the edge of the last frame drawn. Everything that cannot be answered — no fix position, no surface, a non-finite projection — renders and assumes visible. The margin is `PPMap.ownshipSymbolRadiusInPoints`, which is the symbol's REACH and not its half-width, because `fv.north` draws at twice its shape box along its axis. Nothing a normal user sees freezes: the trip readout is gated on `gpsMode` and the stats on `PPShowStats`. **P17 IS BUILT (2026-08-25)** and is the same shape as P16 one layer out: `stopFeed()` is dead code, so the most expensive configuration CoreLocation offers ran from `onAppear` to background whatever the mode. `desiredAccuracy` is only a hint and the step change is at `ThreeKilometers` (where cell and wifi can answer), not at `Best`, so idle drops to a named COARSE configuration and `LocationPolicy` picks between the two. The bit nobody should re-derive is that the policy hangs off **`viewport`'s `didSet`** as much as off a fix: coarse means a still phone delivers almost nothing, and the ship is off screen because the user PANNED away from it, so the camera — not a fix — is what notices it come back. Two thresholds (viewport+25% to restore, viewport+100% to drop) answer the flap; a 100 m coarse filter answers the tier stepping over its own return threshold; `demandFullAccuracy()` in `setGpsMode(true)` and `startRecording()` answers the cold start, though what HOLDS the tier afterwards is `following || recording` and not the demand. **P18 IS BUILT (2026-08-25)** and the plan's own objection to a guard band turned out to be an objection to a BLITTER. There is no blitter in this app and never was: `MapScreen`'s preview transform has scaled, turned and offset the last frame on the GPU since P3, so the band needed pixels and not code, and rotation — the thing that was supposed to kill it — is done by the compositor. A frame is two layers (banded base map, kept; screen-sized transparent overlay, redrawn every frame at the live camera), each with its own transform. `PPBaseCoverage.h` decides a hit, `CpuCanvas::BlendPixel` learned to composite onto a translucent destination with the opaque case kept byte-identical, and the loop learned a SETTLE frame so a cache costs no sharpness. The bit that nearly buried it is P3's live-render budget: judged on the last frame's cost it refuses the very frame that would build the band, which measured as 0 hits in 2 frames over a drag. **The prize was not battery and it landed**: content never invalidates the base, so a dragged route point costs one overlay pass. |
| **OVL-1..n** | The remaining FalconView overlays, over the §1a-bis toolkit | **The queue, ranked, and the ranking is the point.** `grid_map` is done (the graticule; MGRS/GARS dropped). Already ported or superseded: points (`fv.points` is a NATIVE `.fvpoints`, not a localpnt port), moving map, route, coverage. **Tier 1 — small, self-contained, and each one exercises a different corner of the toolkit**: `scalebar` (1.3k lines; pure map furniture over `DrawSymbolAtPixel`, and the cheapest proof that the property schema generalises to a second overlay), `Contour` (3.0k; DTED is ported and its interval ladder is a `ScaleTable` verbatim), `TAMask` (3.8k; terrain avoidance mask, DTED again). **Tier 2 — real documents, so they need `Persistence` + `EditTarget` as well**: `shp` (17.9k, but the vector seam and OGR work is done, so most of it is document lifecycle), `nitf` (19.3k; ImageLib is ported), `localpnt` (16.6k — the REAL local-points overlay, dbase-backed; decide first whether to port it or keep `.fvpoints` and write an importer), `ar_edit` (13.6k, niche). **Tier 3 — heavy shell coupling, last or never**: `PrintToolOverlay` (18.6k, page layout), `TacticalModel` (18.1k, COLLADA/3D), `SkyViewOverlay` (6.9k, 3D), `catalog/Cov_ovl` (16k, largely superseded by `app.coverage`). **The rule the grid session established**: read the Windows overlay for its DECISIONS (tables, thresholds, draw order, the shape of its labels) and throw away its machinery — the class hierarchies where a flag would do, the static CLists, the per-frame registry reads, the property page. Every one of those had a `grid_map` twin and none of them came across. |
| **MM5b** | Moving map, the one optional slice left | **MM1–MM7 are ALL built** — the core reads three feeds and since MM7 the app opens all of them (§1a, §1e). What is left is optional and was optional when it was written down: **MM5b**, an HMM/Viterbi match over a sliding window with network-distance transition costs (OSRM's shape), which MM5 measured exactly the value of — the projection removes the across-track error and leaves the along-track error, 9.32 m in and 5.96 m out over 1295 fixes. The seam is ready (`RoadCandidate` carries `along_m` and the arc's ends) and nothing is shaped around its absence. Also still waiting, on things rather than on work: a **serial** transport (for a device to test against). **CoreLocation is no longer on this list** — Pippin P4 built it (`PPLocationSource`), and it never contradicted the desktop rule: "NMEA-over-TCP until it proves insufficient" was about a phone feeding a mac, and on the phone itself CoreLocation is the only feed there is. No breadcrumb trail — that is a later plan. |
| **O5** | A route the user can steer — what is left | **Via-way restrictions**, which O5a recognises, counts and deliberately does not apply: a search state carries the arc it arrived on and nothing further back, so these need either a longer state or the via arcs edge-expanded at build time. Smaller, now that the bike profile makes it visible: **steps cost nothing extra** beyond being excluded outright, and there is **no elevation term at all**. A ferry's **timetable** is likewise unmodelled — the crossing costs its `duration`, never the wait for the next sailing. |
| **D1 (route-data audit)** | A tool that finds the OSM defects a route trips over, and a Monte Carlo connectivity sweep — **asked for by Chris 2026-08-27** | **Unstarted. The motivating case is worked out in full, so start from it rather than from a blank page.** Chris's `wont_route.fvrte` (Ruddy Turnstone -> beach access 12) would not route on the phone or in PythonView, and the investigation found THREE independent causes, only one of which was code. **(1) The profile-aware snap — FIXED 2026-08-27, and the vertex-only snap under it FIXED 2026-08-28 (mid-road snapping); see §1d.** **(2) The file asked for `car` on a trip only a bicycle can make**, which no amount of data will fix and which the app should probably SAY rather than reporting "not connected". **(3) A GENUINELY MISSING CROSSWALK IN OSM, and this is what the tool is for.** Kiawah's Governors Drive is `access=private bicycle=no foot=no`; riding ALONG it is barred and CROSSING it is not, and the router already models that correctly for free — a crossing shares a NODE with the barred road and traverses none of its ARCS, which is exactly how Sweet Gum Lane reaches Snowy Egret Court (both hold node `2532617711`; no crossing tags involved, the shared node IS the crossing). Of the seven ways meeting Governors Drive, four are true crossings (two ways at one node) and three are T-junctions with nothing opposite — and one of those three, **Blue Heron Pond Road (node `2532445137`)**, is the only tie its whole enclave has to the island. Measured: injecting one 2-node crossing way from cycleway node `3216238623` to `2532445137` took Oyster Shell Road from **0/80 to 74/80** bicycle-reachable over a random sample, with car reachability **67/80 unchanged**. **THE INVARIANT TO TEST AGAINST IS CHRIS'S** (2026-08-27): *you can get anywhere on the island by bike, though you may have to walk the bike 25 yards along a dirt path or a wooden bridge.* That is a statement about the ISLAND and not about the data, which is what makes it a test oracle — every bike-unreachable pair is then either a data defect to survey or a stretch the router should be allowed to walk. **What the tool should do**, in the order the investigation wanted it: (a) a **Monte Carlo sweep** — sample node pairs per profile, route them, and report the unreachable fraction and the CLUSTERS it falls into (island-wide from one hub, 250 samples: bicycle 51 unreachable before the Blue Heron crossing, 46 after; car 65 — some of that is legitimately gated and some is ways clipped at the extract boundary, and telling those apart is most of the value); (b) **missing-crossing candidates** — a T-junction on a way this profile may not use, with a way it MAY use passing within N metres and no shared node (Sawgrass at 12.2 m and Spartina at 22.5 m are the two remaining candidates here, both unsurveyed); (c) **islands** — a sub-network this profile can reach only through a barred way, which is what Oyster Shell was; (d) **snap traps** — points whose nearest graph node has no arc this profile can use, now that §1d's filter makes them survivable rather than fatal. Output should be COORDINATES a person can open in an editor, because the deliverable is an upstream OSM fix and not a local workaround. **The "walk the bike" half is a real design question and is NOT settled**: it is either a mixed-mode profile (bicycle, with barred stretches admitted at walking speed and a hard length cap) or a post-pass that stitches two bike components through a short foot-legal path — the first is a rules change and the second is a router change, and the invariant above is what decides which by saying how long those stretches actually are. Nothing about this is on the critical path for Pippin; it exists because the app's REASON FOR BEING is handling data a commercial router silently gets wrong (Chris 2026-08-27), and a defect you cannot find is one you cannot fix upstream. Tool goes in `port/tools/`; the analysis scripts this session threw away did (a) and (c) in about 40 lines of `pyfvw.routing` each. |
| **R3d** | Perf, fourth slice — *if anything still needs it* | R3c re-measured the whole profile at **-O2** (the default build type was the real finding): **cold 49 ms = query 29 + style 11 + draw 8; a retained pan 3.4 ms; a pan out of the retained area 11.6 ms = query 0.7 + style 6.9 + draw 3.5**. The cold 29 ms is the one-time parse of a whole DNC library and the 6.9 ms is **GeoSym styling** — so the next target, if a user still feels one, is `LookupTableStyleEngine`, not the query and not the rasterizer. **Do not start this without a fresh profile**: the plan on this line has been wrong about where the time was three times running. The columnar `FeatureBatch` is a **measured non-goal**. |

### 2b. Product gaps

- **A turned chart has no USER-FACING gesture and no better sampling** (the two things PR1–PR3
  deliberately did not do). The moving map is the only thing that writes `self.rotation`, so a user
  who wants a turned chart has to fly one; the shell is one key binding and one assignment away, and
  the rest of the app is already rotation-aware (both draw paths, the pan delta, every pick). The
  turned blit is **nearest-neighbour exactly like the straight one**, so an odd angle is as aliased
  as a straight chart — the difference is the aliasing is on a diagonal, where a reader notices it.
  Bilinear would be one loop in `CompositeRowTurned`; applying it to the straight path too would
  move every pinned raster golden, which is the reason not to do it in the rotation session.
  Two smaller PR3 findings, documented at their sites: **a frame edge on a half-pixel has its tie
  broken differently by the two paths** (pinned to within 1 px rather than removed with an epsilon),
  and **the affine fit is now over a box up to √2 larger**, so PROJECTED imagery (the UTM DOQs)
  carries a slightly larger residual when turned. Equal-arc products are exact either way.
- **The point overlay has no editor ON THE DESKTOP** (A6). **Pippin now has one** (P9,
  2026-08-20) and it needed nothing from A4: an `OverlayEditor` is a desktop MODE object — a tool
  palette, a current tool, a `KeyEvent` stream, a mode that follows the current overlay — and a
  phone has a TAP and a SHEET instead, which together are the whole editor. So what P9 built is
  `PPPointStore` (seed, write-per-edit, nearest-wins hit test; mac-tested) plus two SwiftUI
  sheets, and PythonView is still the shell that would want A4's shape: an `OverlayEditor` for
  `fv.points` with click-to-place and drag-to-move, as `RouteEditor` has in `route.py`.
  **`MapPoint` gained `phone` and `url` (schema 3)** and the reader now probes PER COLUMN and
  SELECTs a literal default for a missing one, so schema 1, 2 and 3 open through one query —
  including a document interrupted between two of the migration's `ALTER`s. Still open:
  **Pippin's editor picks a SYMBOL too** (2026-08-20, the same day, at Chris's asking):
  `stage_data.py` embeds 43 maki PNGs into the seed's `symbols` table and the editor offers
  them, which works because a palette is NOT a projection of the points — a row nothing
  references is kept, saved and handed back. `symbol_id` is a foreign key into that table and
  never a path into an icon directory, which is the property schema 2 exists for. The claim that
  a picker would be expensive ("a palette picker means rasterising a symbol through `CpuCanvas`
  into a `UIImage` per cell") was WRONG and is retracted: a document stores PNG BYTES and
  `UIImage(data:)` takes exactly those. What IS still open: **no way to get NEW artwork into a
  document from an app** (`add_symbol_from_png` is bound and only `WriteSampleFile` and the
  staging script call it), so the palette a document ships with is the palette it has. Also
  still true: the sample document is written to
  `<catalog dir>/sample.fvpoints` by a File-menu item, which is a demo rather than data
  management, and point labels are off by DEFAULT because there is no label collision (below) —
  Pippin turns them on in `pippin.ini` because a couple of dozen places on one island do not
  collide.
- **Snapping removes the ACROSS-track error and leaves the ALONG-track error alone** (MM5), which
  is what a projection can do and the whole of it: a fix 10 m up the road projects onto the road
  10 m up it. Measured over 1295 fixes of a routed Kiawah track with ±12 m of scatter — mean 9.32 m
  in, **5.96 m out**, which is one axis of the noise almost exactly. Removing the rest needs a
  motion model: **MM5b**, an HMM/Viterbi match over a sliding window with network-distance
  transition costs (OSRM's shape), reusing the router for candidate-to-candidate distances. The
  seam is ready for it — `RoadCandidate` already carries `along_m` and the arc's ends — and nothing
  in MM5 is shaped around its absence. Related and smaller: **a candidate's bearing is a TRUE
  bearing while a derived heading is a SCREEN angle** (MM1's split), so the alignment term compares
  two frames that differ by the projection's aspect — 5° off Charleston, 18° at 60°N. It is a soft
  ranker and the distinction that matters (which WAY along the road) is a 180° one, so this is
  documented rather than reconciled; reconciling means handing the snapper the projection.
- **The snapper's road index is built over the WHOLE graph at construction** (MM5), which is right
  for an island, a county or a state and wrong for a continent-sized `.fvroad` — that wants a
  window around the ship, rebuilt as it moves. Same family as the router's per-query scratch below.
- **WVS (WVSPLUS)** — Chris wants it. Blocked in the reader, root cause known: `fv_vpf` builds the
  feature-class list only from **FCA**, and WVS thematic coverages have none → empty. Fix =
  enumerate from **FCS or a directory scan**, then a simple stroke style engine (WVS has no GeoSym
  symbology). Data: `TestData/VPF 2/WVSPLUS/WVS{012,040,120}M`.
- **LINE WEIGHT AND THE OPENING VIEW: both were the shell counting in the wrong unit, and both
  are FIXED** (Pippin P12, 2026-08-19; raised by Chris off a real iPhone 14 Pro the same day).
  Kept here rather than deleted, because the arithmetic is the part worth not re-deriving.
  **Neither fix touches the core, so no OSM golden moved** — this was `PippinKit` all along.
  * The crayon was NOT `display.mm_per_pixel` (0.0519 assumed against the 14 Pro's true 0.0552
    is 6%). It was `VectorRenderer::SetDeviceDpi(25.4 / mmPerPixel)` = 489 dpi feeding
    `OsmStyleEngine`'s `dpi / kStyleNominalDpi` — **5.09x** — compounded by the zoom relation
    being derived over the same surface pixel, which styled the map **1.58 levels** further in
    than the view being shown. Measured first, as this entry demanded: one road, one scale,
    both shells — a minor road at 1:25,000 drew 36 px = **1.87 mm** on the phone against the
    desktop's 3 px = **0.75 mm**. Now 11 px = 0.57 mm at z15.05.
  * The fix is one sentence: **an authored pixel is one iOS POINT** — for a GL style's
    `line-width` exactly as P4 already had it for a symbol's `SetSizePx`. `PPViewport.mmPerPoint`
    is the single definition; the source and the style derive their zoom over it, the renderer
    gets `96 * pixelsPerPoint`, and the projection keeps the surface pixel because it is the one
    thing that really is placing geometry on a screen. `PPViewport`'s zoom limits had assumed
    exactly this ("one tile pixel per POINT") since P3, so the shell now counts in one unit.
  * **The consequence for `display.mm_per_pixel` as a bisect tool**: it no longer moves line
    weight (the pixels-per-point ratio is the backing scale either way). It moves the PHYSICAL
    size of the result and the zoom, which is what a pitch override should do.
  * The opening view now COVERS the screen rather than containing the pack
    (`-[PPViewport homeScaleFilling:]`, `min` where contain takes `max`, 2% overfill). The
    zoom-OUT limit deliberately stays on the CONTAIN fit — how far out is a fact about the
    pack, not about where the app opens. P3's probe check was asking the old question and
    passing while the phone showed background bands; it asks the new one now, on both axes.
- **Symbol size does not honour device DPI while line widths do.** A symbol is sized on its
  product's own nominal pixel — `himetric_per_symbol_pixel()`, 25.4 for GeoSym and 32 for S-52 —
  and a raster tile is blitted 1:1. **None of those three numbers is the device's**, so on a retina
  pitch every symbol is half its physical size while the text beside it is right. The fix is one
  more factor (`dpi/100` on display lists, `0.32 mm / device mm-per-px` on tiles); the reason it has
  not moved is the bit-faithful rule — it would move every GeoSym golden. **G3 built the way out and
  SOMETHING NOW SETS IT** (Pippin P4, 2026-08-18): `GeoDraw::symbol_dpi_scale` still defaults to
  1.0 for everything else, but Pippin passes `one iOS point / the viewport's mm-per-pixel` down to
  the moving map, so on the phone an authored pixel is a POINT and a 24-px ownship is 24 points on
  any screen. **P12 extended that sentence to the STYLE ENGINE** (the item above): the same
  `pixelsPerPoint` now sets `SetDeviceDpi`, so on this shell a symbol pixel and a style pixel are
  finally the same unit. **The reference is the argument, not the mechanism**: Pippin chose the app's own unit
  because an ownship is furniture, while the 0.32 mm above is the right reference for CHART
  symbology pinned against paper — the two want telling apart at their sites the first time one
  product needs each. **P5 did that telling apart** (2026-08-18): `RouteOverlay::SetSymbolDpiScale`
  is the second setter, its header carries the argument for each reference, and the overlay's HIT
  TEST scales with it too — a marker drawn twice the size that did not also become twice the target
  would be a thing a finger can see and cannot press. **P9 made `fv::PointOverlay` the THIRD**
  (2026-08-20) and it is the first one in `fvkit` itself rather than in an app module: the comment
  over `PointShape` had said since G2 that the decision was deferred because "the overlay does not
  know the device and the shell that does has no way to tell it", and `SetSymbolDpiScale` is that
  way. Everything measured against the drawn marker moves with it — the cull box, the LABEL's type
  size (a 12-px name beside a marker drawn 3x is a caption a third the height of what it names) and
  the hit test. So the mechanism is settled; what is still
  open is unchanged, because every CHART product passes nothing and the 0.32 mm reference still has
  no consumer. **P6 made the app's half of it routine**: `PPMap` computes the factor once per tick
  and hands the SAME number to both overlays, so a shell setting it is now a line of code rather
  than a decision. PythonView still sets nothing, which is the desktop half of this item and is
  where `display.mm_per_pixel` already sits waiting. Same shape as `PickSession::tolerance_px` in
  §2c. G2's `SymbolPixmap::pixel_ratio` is the other half for RASTER symbols: it does not fix this,
  but a high-DPI sprite set is now at least expressible.
  P4 also turned up a smaller thing at the same seam: **`MovingMapOverlay::SetSizePx` is the full
  drawn width only for symbols that fit the shape box.** `fv.ownship` does; `fv.north` is map
  furniture and `builtin.cpp` draws it at TWICE the box along its axis, so a chevron asking for 24
  draws 48. Documented at both sites rather than changed, since the authoring is deliberate.
- **The route now exists TWICE, and that was the deal** (P5). `port/RouteKit/` is route.py's
  overlay in C++ and `route.py` is untouched, exactly as the Pippin plan asked — the Python one
  keeps the editor (armed add-mode, drag, undo, the key bindings), the C++ one has `SetWaypoints`
  and nothing else. Two things are duplicated rather than shared and will drift if nobody watches
  them: the **document format** (pinned against each other by a byte-for-byte test over the fixture
  route.py itself wrote, which is the whole reason that test exists) and the **status sentence and
  fallback tree** in `follow_roads` / `RoutePlanner::Plan`, which are the same words and the same
  branches typed twice with nothing comparing them. The stated way out is to rebase `route.py` onto
  the C++ overlay through pyfvw — **and RouteKit is bound to nothing at all yet**, so that is the
  first step and not a small one: the module would need a `pyfvw.route`, and route.py's editor
  would need an `OverlayEditor` over a C++ document. Worth doing when a third shell appears, not
  before.
- **A route's LABELS are on by default, so a canvas with no default font fails its draw** (P5).
  Deliberate on both halves — a waypoint's label IS its identity in the document, which is why they
  default on here and off in `PointOverlay`, and `GeoDraw` reporting a missing font through Status
  is what every other text-drawing overlay does. But the two together mean a shell that has not
  called `CpuCanvas::SetDefaultFont` gets a route that draws NOTHING rather than a route without
  names. Pippin's pack carries DejaVu and PythonView's canvas has a font, so nothing is broken
  today; it is a trap for the next shell, and the honest fix is a canvas that draws the geometry and
  reports the text failure rather than one that returns on the first bad Status.
  **The other half of this item is CLOSED** (P6, 2026-08-19): the status line P5 left drawn on the
  canvas at (10, 20) is turned OFF in Pippin (`SetShowStatus(false)`) and the words move to the
  route sheet, where `RouteSnapshot::status` carries them. Two reasons, and the second is the one
  worth keeping: (10, 20) is under the Dynamic Island, AND text drawn into the map bitmap rides
  P3's preview transform, so it slides about with the chart under a finger. Anything that is not on
  the earth belongs above the canvas, not in it.
- **A session the user builds at RUN time cannot be persisted** (A3). `SaveConfiguration` /
  `RestoreConfiguration` round-trip through the live `fv::Settings` and are bound — but
  **`fv::Settings` has no `Save()` by rule S1** (the file is authored by a human and the application
  never rewrites it, which is what preserves the comments and the unknown keys). So a hand-written
  `peregrine.ini` can carry a startup session and "save my current layout" cannot. **The fix is not
  to relax S1**: it is a second store for application state — window geometry, last position, the
  saved session — which the settings header already says belongs elsewhere. One decision, then a
  writer. A6 made this the app layer's most visible gap.
- **A top-most overlay's opacity is carried and not applied** (A2). `default_opacity` is
  FalconView's blend for the top-most band and `DrawAll` draws that band as a second pass exactly
  where the blend belongs — but `ICanvas` has no layer alpha. The fix is an off-screen layer, which
  is also what a real pattern brush and a clip region want, **so all three are one canvas session**.
- **ICanvas has no pattern brush.** GeoSym stipples and S-52 `AP` fills are approximated by carrying
  ink coverage in the fill **alpha**. `AreaFillFor` is the one place to change.
- **Area patterns bleed past their ring**, because `ICanvas` has no clip region — a pattern can
  spill by up to half a symbol. And **area patterns are outer-ring only** (`p == 0` in the
  renderer's area branch): holes do not punch through a pattern or a fill.
- **No label collision or de-duplication.** Every product that draws text needs it and none has it;
  OSM makes it visible because a road name repeats per tile, and T1's `symbol-spacing` repeats a
  name along a long road as well. Belongs in the renderer/scene, not a style engine: a per-frame
  index of the boxes the renderer is about to emit, rejecting a label that collides. **The boxes
  exist already** (the pick index takes one per label run). **E8 made this ENC's most visible
  defect** — a Charleston harbour view draws "Shutes Folly Island" three times, one per overlapping
  cell; **Pippin P3 made it the most legible one**, a real pinch to z14 over the Kiawah marsh
  drawing "Abbapoola Creek" FOUR times on one phone screen. Note the second cause, which de-duplication alone will not fix: the 8 test cells span 4
  usage bands over the same water and all are open at once, where an ECDIS shows one band, so the
  index has to key on more than the string.
- **Text halos have no blur and only OSM sets one** (T2). `text-halo-blur` is ignored and counted —
  a stamped halo has no coverage to soften. S-52 and GeoSym both draw text that would read better
  with a halo and neither authors a colour, so giving them one is a symbology decision (and would
  move their goldens).
- **S-52's `SPACE` and `DISPLAY` text parameters are still skipped** (E8 took the other four).
  `SPACE` is character spacing and needs `CpuCanvas` before it needs the loader; `DISPLAY` is the
  group number and belongs on the viewing-group axis.
- **ENC body size is treated as PIXELS, not points.** `TextSizeFromSpec` reads the `CHARS` body size
  straight into `TextStyle::size`. Same family as the device-DPI item and unmoved for the same
  reason: it would shift every label on the chart.
- **`OsmStyleEngine` colour stops STEP, numeric stops interpolate** — a declared O2 deviation from
  the GL spec. Visible only side by side.
- **OSM Bright is not vendored.** Adding `openmaptiles/osm-bright-gl-style` (BSD-3/CC-BY) as a
  second reference style needs a network fetch and a `NOTICE.md` entry, and would exercise the
  subset check against a style nobody here authored — worth doing for that alone.
- **`CgmSymbolLibrary` still has no consumer** (GeoSym's ~1500 `.cgm` in an overlay). Every CHART
  symbol still goes through `VectorRenderer` exactly as before, which is what kept the goldens
  byte-identical.
- **`VpfVectorSource::Bounds()` costs a full scan** — tile bounds need the tileref face, so
  per-feature bounds are used. Much reduced by R3c, but it still parses the whole library to answer
  "where is this?". The 14m catalog already stores per-tile coverage; use it for tile-level culling.
- **CIB gap**: the CADRG decoder supports CIB but `CadrgRasterSource` hardcodes `is_cib=FALSE`, and
  there is no CIB test data.
- **Ferries and tolls have no REAL-data fixture** (O5e). Kiawah carries **zero** `route=ferry` ways
  and **zero** `toll` tags, so every O5e test runs on the synthetic "bay" fixture — enough to pin the
  mechanism, not enough to catch a tagging shape nobody thought of. One `fvgraph build` over
  `us-south-260728.osm.pbf` (~1 h) would give real counts.
- **A ferry's `duration` is prorated wrongly on a CLIPPED extract** (noted, not fixed): the speed is
  the way's KEPT length over its stated duration, so a crossing cut by the bbox reads as slower than
  it is. The safe direction, and unfixable without the full way length.
- **Router scratch is allocated per query** — six arrays of `state_count` (~34 bytes/state), fine for
  a state-sized graph, ~700 MB per query on a continent. The fix is reusable scratch with an epoch
  stamp, but `Router` is const and shareable today, so it needs a thread-safety decision.
- **`RoadGraph` cannot cross the antimeridian** — `Finalize` hangs the nearest-node grid on a plain
  min/max bounds. The catalog already has the split-at-180 treatment to copy.
- **`FeatureStore`** — the other half of L5 (only `TilePack` was built).
- **ImageLib leftovers**: `fv_imagelib_gif` compiles but has **no test** (no `.gif` sample);
  `Image.cpp` (multi-format dispatcher) and `nitf/` unported (row 9b-4).

### 2c. PythonView / UI follow-ups

- **A6's leftovers, none of them blocking**: no shell calls `OverlayManager::Reorder`, so the
  reorder DIALOG is unwritten and the stack order is whatever insertion-by-display-order produced;
  ~~`SnapToPoint` is bound and tested but no overlay in the app answers it, so nothing snaps~~ —
  **RESOLVED 2026-08-27**: `PointOverlay` and `RouteOverlay` both answer it (P19), and PythonView's
  route editor snaps every waypoint it places, through `app.snap_candidates`;
  `EditorUiConstraints` is reported and greys nothing (the app has no rotation or projection
  controls to grey); `RoutingOverrides` is bound and unused; and **`route_double_click` is called by
  no shell**.
- **A GPX track has nowhere to be DRAWN** — the moving map's most visible gap now that MM7 has
  closed the feed one. `GpxSegmentPath` hands back exactly the polyline
  `GeoDraw::DrawGeoPolyline` wants and nothing calls it — showing the ride you are replaying is a
  route-overlay-shaped job (`route.py` already draws a line from a document) and would make the
  replay legible instead of a symbol wandering an empty chart.
- **Snap-to-road has a menu item and no key** (MM5), unlike the three moving-map modes which have
  M/T/S. `[movingmap] snap_to_road` is the startup state and Overlays > Moving Map Modes > Snap To
  Road moves it with the feed running. Also app-level and deliberate: **`[movingmap] noise_m` is a
  DEMO knob** — the scripted feed replays a track that is already exactly on the roads, so without
  scatter the snapping is a no-op nobody can see. A real feed retires it, and since MM7 the app can open
  one — so the knob is now scoped to the DEMO feed rather than to the app.
- **`PickSession::tolerance_px` is 8 device pixels and the shell is supposed to scale it.** Same
  family as the symbol-DPI gap: the core has no business knowing a finger is wider than a mouse
  pointer. PythonView already knows its own `display.mm_per_pixel`, so this is one line whenever
  somebody has a device where it is wrong. **Pippin is the shell that does scale it** (P9): the
  tolerance is `points.hit_tolerance` in the pack (22 points, about a thumb), `PPMap` multiplies
  by the viewport's backing scale on the way down, and it is ADDED to the marker's own drawn
  half-width — so a 22-pt marker is pressable 33 points from its centre, which is Apple's
  44-point target arrived at honestly rather than by padding a rectangle.
- **The route line's STYLE is derived, not chosen** (G3). It is derived in ONE place again as of
  2026-08-27 (route.py's copy went with its overlay), and `fv::RoutePlanner`'s `IsBicycleRequest`
  draws a calculated route dashed when the request was a bicycle one and solid otherwise, decided by
  matching the profile NAME — right for the two modes the apps have keys for and silent about a
  third (a walking profile draws exactly like a car). **And the dash it picks is currently invisible
  — see §2d.** The honest fix is a per-profile line style in the rule file beside the
  weights, which is a rules-schema decision rather than a drawing one. Also unstyled:
  `pyfvw.draw.PRESETS` has ten entries and the app reaches two, and there is no UI for a route's
  colour or width.
- **Toll and ferry avoidance have no UI** (O5e). Both are bound and on the CLI, but the app always
  takes the profile's default. Unlike the rest of this list it wants **two checkboxes and nothing
  else** — they are what a driver changes per journey and they already outrank the profile by
  design; `car_no_tolls` / `car_no_ferries` in the shipped rule file exist only because there is no
  UI. Worth doing beside the profile menu.
- **`private_penalty` has no UI and no settings key** (O5b). Bound and on the CLI; the app takes the
  default. The number is a judgement, not a measurement: 5x keeps a route off a gated shortcut while
  leaving an address behind the gate reachable, and nobody has driven it against a reference router.
- **The U-turn-at-a-stop preference has no UI** (O5d). `allow_u_turn_at_stops` is bound and on the
  CLI; the app takes the default (barred). `Route.u_turn_stops` is not drawn either — the app says
  how MANY stops had to turn round, not which, though a marker on the offending waypoint is what
  would tell the user their stop is up a driveway.
- **Route profiles: still no per-leg choice** (O5c). The profile menu, the `[routing]` settings keys
  and the live rule-file reload are all done; the profile is the app's one route-wide setting and a
  per-waypoint or per-leg profile has nowhere to live.
- **Rule layer has no UI.** `pyfvw.vector.RuleSet` / `ViewingGroupSet` and `engine.rules()` /
  `viewing_groups()` are bound and tested but unreachable from the app. Natural shape: an
  Overlays-menu display-category picker (Base/Standard/Other) + a "Load rule file…" item.
  **Half-answered by M1**: the app loads `[vector] families_{dnc,enc,osm}` at engine open and hides
  what the file says to hide. An Overlays-menu checkbox per family is one `SetEnabled` + a RuleSet
  rebuild; `FamilySet::epoch()` is there so a host can tell.
- **Mariner panel in the Options dialog** — safety/shallow/deep contour, safety depth, two-shade and
  the shallow pattern are bound on BOTH products and settable from `[mariner]`, but there is no
  dialog. It is the one setting a mariner actually changes underway (vessel draft plus under-keel
  clearance), so a spinbox is worth more than most of this list. **The per-product defaults must
  survive it**: a panel that wrote all six values on open would give DNC S-52's numbers.
- **ENC data-dir field** beside the GeoSym one in Options (`[enc] data_dir` exists). The GeoSym
  directory and the OSM style sheet are both there; ENC is the one asset still settings-file-only.
- **OSM has no per-source knobs in the UI** — tile budget, tile-cache capacity and `set_clip_to_tile`
  are bound and defaulted sensibly, but only reachable from Python.

### 2d. Known defects / hygiene

- **THE BICYCLE ROUTE'S DASH IS NOT DRAWN, and has not been since P13** (found 2026-08-27, writing
  the pytest that used to assert it). The whole point of the dash is that the MODE is visible in the
  line rather than only in a line of small text — and on screen a bicycle route is indistinguishable
  from a car one. **The cause is an interaction nobody looked for**: `MakeLinePreset`'s `kDash` is
  `dash(8) gap(6)` in pattern units and does **not scale with the pen**, while P13 doubled the route
  line from 3 device pixels to 6 (3 was ONE point on a 3x phone, which is the correct fix for the
  problem it solved). A 6-wide stroke has 3-pixel round caps at each end of every dash, so two
  neighbouring dashes bridge a 6-unit gap **exactly** and the line closes up. Measured through
  `pyfvw.draw` on identical geometry, counting exact-colour pixels: at width 3, solid 1506 vs dash
  1188 — 21% removed; at width 6, solid 3030 vs dash 3024 — **0.2%**. It bites the phone hardest,
  which is where the widening was done for. Three candidate fixes and the choice is a real one: a
  square cap on a `kDash` run, a wider gap, or a pattern that scales with the pen (which would move
  every dashed golden in the tree). `test_the_route_line_says_which_MODE_it_was_priced_as` carries
  the measurement and deliberately does NOT assert the dash, because asserting it would pin a
  picture nobody is being shown.

- ~~**`TestData/OSM/kiawah.fvroad` was built with `--ignore-access`**~~ — **RESOLVED, and it was
  already resolved when this row was written.** Checked in Pippin P1 (2026-08-18): the file on disk
  reports 70 barred / 408 private car arcs, and rebuilding it with the row's own fix command gives
  a **byte-identical** file. Chris had rerun it (mtime 16:49, after the 16:07 extracts) before the
  row was ever read. One fact worth keeping from the check: **the build is INPUT-ORDER dependent** —
  the same four extracts passed as a shell glob (`map*.osm`, which sorts `map-2` first) produce a
  file that differs in 64,873 of 210,674 bytes while every number `fvgraph info` prints is
  identical. It is the same graph with its nodes numbered differently, so a `cmp` against a
  previously delivered `.fvroad` means nothing unless the input order matches.
- **The test-data directory is `testdata/` and 14 CMakeLists say `TestData`.** The directory on disk
  has been lower case since the project started (Chris, 2026-08-17 — every fixture is in it);
  `FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData` resolves anyway because APFS is case-insensitive.
  **On a case-sensitive checkout the variable points at nothing and EVERY test that reads a fixture
  fails**, not just one module's. Pre-existing and repo-wide, so it is hygiene rather than a defect
  in any one session; the fix is one spelling across those 14 files, and §1f above should be read as
  naming the directory rather than its case. Same family as the Peregrine case-sensitivity
  follow-up, and `.gitignore` already lists all three spellings for the related reason.
- **Windows sockets are guarded and never compiled** (MM6). `line_transport.cpp`'s Winsock branch is
  written — refcounted `WSAStartup`, `ioctlsocket`, `closesocket`, the same non-blocking-before-
  connect rule — and no build in this tree has ever run it. Treat a first Windows build as bring-up,
  not as a regression.
- **Two open ENC decisions left by the 2026-08-11 test-data cut-back**: (a) should ONE unreadable
  cell abort an exchange set, or be skipped with a warning and a count — an ECDIS would not refuse
  the other 822; (b) the render tests still open `$FVW_TESTDATA_DIR/enc` wholesale, so **restoring
  `TestData/enc-archive/` re-breaks them**. Naming a fixed cell list would make the goldens
  independent of what else is on disk. (`EnumerateEncCells` is a bare recursive iterator matching
  `*.000`, which is why the archive is a SIBLING and not a subdirectory.)
- **`PythonView.py --selftest` aborts on this machine before it reaches any UI step**, in
  `RPFRenderer::get_rgb_image` (`fvw_core/ImageLib/cadrg/imgdisp.cpp:386`) — `get_frame_image`
  fails on a CADRG frame and the original's `ASSERT(0)` is a hard abort in this build. Observed
  2026-08-17 and **reproduced with the working tree stashed**, so it is not MM5's; it is a frame in
  the local catalog the decoder will not read, and it makes the scripted walk-through unusable
  until somebody finds which. The `--shot` path and every ctest test are unaffected.
- **Some tests share one scratch file and so cannot run in parallel.** Fix = per-test filename.
  Known cases: one `.gpkg`, and — measured in Pippin P1, 2026-08-18 —
  `port/Routing/test/router_test.cpp:1475`, where `BuildBayFixture` writes
  `<tmp>/fv_routing_test/router-bay.osm` under a FIXED name and every `RouterAvoid` test rewrites
  it. `gtest_discover_tests` makes each of them its own ctest process, so under `-j4` one truncates
  the file while another is parsing it: `RouterAvoid.APenaltyBuysADetourAndAHugeOneStillIsNotADeletion`
  failed on one run of three and passes alone every time. **A SECOND instance, measured in Pippin P5
  (2026-08-18)**: `port/Routing/test/route_rules_test.cpp:80` hangs its scratch on
  `<tmp>/fv_route_rules_test/` and `BuildCycleGraph` rewrites `rules-cycle.osm` under a fixed name,
  so `RouteRules.ClassWeightChangesTheRouteChosen` failed once under `-j4` and then passed alone and
  on three further full runs. Same cause, same fix, same family — which is now large enough to be
  worth one pass over every `fs::temp_directory_path()` in the tree rather than another row here.
  **Intermittent and pre-existing — do not read it as a regression**, and do not chase it in
  whatever session it surfaces in.
- **`pyfvw_pytest` cannot run in the ASan build** — Python is not sanitizer-instrumented and
  `dlopen`s an instrumented `.so`, so ASan aborts with "Interceptors are not working". The C++ tests
  are unaffected. Either run it under `DYLD_INSERT_LIBRARIES=<libclang_rt.asan_osx_dynamic.dylib>`
  or exclude it from `build-san`.
- **Invariant 3d.1's CANCEL branch is unreachable and is pinned through a FAILURE** (A4). "If the
  user cancels creation, the mode falls back to none" shares one branch with "if creation failed",
  and neither creation flow currently asks the user anything. The fallback is genuinely pinned; the
  word "cancel" in it is not. A creation flow that grows a prompt should add the cancel test.
- **LZW is now accepted by both GeoTIFF readers, but only one decode path has a fixture.**
  2026-08-27 wired `GEOTIFF_LZW` into every strip and tile arm of `CGeoTiff` (35 sites) and
  `CGeoTiffFrameFile` (8), all through one new `decompress_strip` that picks the codec and then
  undoes the predictor. Only the **8-bit palette strip** path has a real file behind it (the FAA
  sectional, verified byte-identical against libtiff's own decode at five blocks including the
  last row and both far edges); grayscale, 16-bit, 24-bit RGB and the four tiled readers are
  wired but unexercised. Predictor 2 is implemented for 8- and 16-bit samples and verified for
  8-bit against a `tiffcp -c lzw:2` re-encode; predictor 3 (floating point) is refused at load
  with a message. **The trap this replaced**: `CGeoTiff::decompress_lzw` already existed and was
  a copy of `decompress_packbits` with its error strings renamed — enabling LZW without reading
  it would have produced confident garbage rather than a failure.
- **The GeoTIFF frame bbox is CORNER-based, and a conic sheet bulges past its corners.**
  `Atlanta SEC.tif` reports `ur_lat` 36.5631 from its NE corner while the top edge's midpoint is
  at 36.6204 — 6.4 km of sheet north of the catalogued box. Harmless for a sheet with margins;
  it would clip a conic frame trimmed to its neatline. Pre-existing, unchanged, noted because
  the LCC path now has its first real caller.
- **VPF reader UBSan alignment** — `vpfrcset`/`tables` do unaligned scalar loads. ASan-clean; this is
  the only UBSan noise in the tree, which is why every VPF session says "only the pre-existing ones".
- **`VPFRecordset` reopen row-undercount** (original bug, preserved bit-faithfully).
- **Subsampled `ReadBlock`** — zoomed out, 9 fully-VQ-decoded CADRG frames ≈ 1.2 s. Decoders read
  full resolution and then downsample.
- **Polar CADRG transforms** return `kUnsupported` (equal-arc only).
- **TIROS tile-seam** artifacts.
- **C++14 pins** still on `fv_jpeg`, `fv_jpeg12`, `fv_imagelib_gif` (`std::auto_ptr` in headers).
- **`CDTEDInstance` stub** in ImageLib's `Util.cpp` — RPC height refinement returns "no DTED"
  headless; wire `fv::DtedCell`.

### 2e. Dependency modernization (three ⛔ rows left; full table in the archive)

The port builds against current upstream via `port/third_party/CMakeLists.txt` (FetchContent).
**Done**: googletest 1.17.0, zlib 1.3.2, expat 2.8.2, protozero 1.8.2, vtzero 1.2.0,
nlohmann/json 3.12.0. **Frozen on purpose**: GEOTRANS 3.3 (a newer one invalidates the pinned geo
results).

Each remaining row is a session of its own; none blocks the active track. Do them in this order
(ascending consumer count, so a break localises):

1. **libpng 1.2.7 (2004) → 1.6.58** — not a drop-in: opaque structs, reworked
   `png_get_`/`png_set_`/`png_jmpbuf`.
2. **libtiff 3.9.4 → 4.7.2** — not a drop-in: `toff_t` widened to 64-bit; `CGeoTiff` is ~25K lines
   against the 3.x API.
3. **IJG jpeg 6b → libjpeg-turbo 3.2.0** — hardest: FalconView *transliterated* IJG to C++ and the
   wrapper carries an encryption fork (`m_crypt_pos`/`m_encrypt`, must be shown unused first);
   re-opens the "FalconView's C++ jpeg and GDAL's C jpeg must not meet in one link" rule. GDAL's
   vendored libjpeg goes away with this.

**Gate**: Q12 (WMS) is the first network-facing feature — anything parsing network input must be on
a modern library first. expat already is.

### 2f. Backlog (unstarted, roughly in priority order)

| # | Item | Data | Complexity |
|---|------|------|-----------|
| Q12 | WMS network raster source | public endpoints (USGS, GIBS) | moderate — HTTP client decision: libcurl |
| Q13 | JP2 via OpenJPEG | public samples | moderate (avoids Kakadu; would also unblock ECRG) |
| Q14 | NITF (ImageLib `nitf/`, row 9b-4) | public NITF test sets | moderate-high |
| Q15 | GeoPDF | USGS topo GeoPDFs | high (PDF engine decision) |
| Q16 | Lidar | USGS 3DEP | high, niche |

Also unbuilt from the vector plan: **V7** a CoreGraphics `ICanvas` backend (macOS first, same code
iOS) and **V8** the symbol atlas / batching pass — neither blocks anything, and R3c measured the
atlas away as a non-goal at current frame times.

**Deprioritized** (Chris 2026-07-19 — restricted/proprietary data, not the public-data use case):
ECRG, CIB, MrSID, Hrdted/RDted/ARdted, BlankMapServer.
**Deferred indefinitely**: CoT (row 8), MdsUtilities (row 6 — Windows system plumbing only; pull
individual helpers on demand), Collaborate, NITFSourcesCtrl, *MapOptions property pages,
FvConfigFileServer. Other unported map servers: Ecrg, MrSID, Jp2, WMS, GeoPdf, Lidar, Blank.

**Deferred by decision, not by backlog**: **dimming** as a `RenderState` (Chris 2026-08-13 — the
two decisions already worked out are in the draw plan's §3d, and nothing in G1–G4 is shaped around
its absence) and **G5**, an SVG symbol library, which is gated on wanting somebody else's symbol
sets.

### 2g. Still needed from Chris / the Windows machine

- [ ] **Reference output dumps from the Windows build** (elevations, decoded-pixel checksums) for
      golden-file tests — the port's goldens are all self-pinned after a visual check.
- [ ] **Reference screenshots of dnc17 harbour views** for the DNC goldens (the ENC side now has a
      published chart to check against; DNC does not).

---

## 3. Standing rules — violate one and you ship a silent bug

These are the archive decisions that still constrain new work.

**Porting mechanics**
- **Bit-faithful**: known numeric/behavioural quirks in the original are *preserved and documented*,
  not fixed. Fix only what is a bug *in the port*.
- **Compile in place** from `fvw_core/`; never move a file, split only if hopelessly Windows-bound.
  Sever COM/GDI with `#ifdef _WIN32` (`port/tools/guard_win32_functions.py` does brace-matched wrapping).
- **Never modify** `BuildAll.sln`, `*.vcxproj/proj/idl`, `WinDebug/`, `WinRel/`, `third_party/`.
  Shared edits must stay MSVC-compilable: guards, additive accessors, const-correctness.
- **`LONG` is `long` on Win32 but `int32_t`(=`int`) here, and old sources mix the spellings freely.**
  Expect declaration/definition mismatches in every in-place module.
- **Win32 integer widths are ABI**: `LONG`/`DWORD`/`HRESULT` are exact. Mirrored IDL enums in
  `port/include/` keep COM values — **never renumber**.
- Repeated mechanical transforms → a script in `port/tools/`, never dozens of hand edits.
- Editing Latin-1 shared sources re-encodes the file — use escapes (`'\xB0'`), not raw bytes.
- Compat shims: `port/include/fv_compat.h`, `fv_cstring.h`, `fv_mfc_containers.h`, `fv_oledatetime.h`,
  `fv_filemap.h`, `fv_win32_{filemap,finddata,path}.h`, `fv_sscanf_s.h`. **Prefer rewriting to standard
  C++ over growing them.**
- New code under `port/` is C++17; shared `fvw_core/` edits stay ≤C++17 and modern-MSVC-clean.

**Licensing (settled 2026-08-18 — was GPL-3.0, now LGPL)**
- **Peregrine is `LGPL-3.0-or-later`.** Every new file under `port/` is *born* with
  `// SPDX-License-Identifier: LGPL-3.0-or-later` + the Chris Bailey copyright + the
  `See COPYING.LESSER and NOTICE.md` pointer. `sync_peregrine.py` copies bytes and will not
  graft a header on for you.
- **Never write GPL-3.0 into a header again**, and never relicense: `fvw_core/` and the five
  extracted `port/` files are GTRC's LGPL and that is a floor, not a preference. LGPL is what
  lets Pippin's Swift stay closed (LGPL-3.0 §4) — GPL would have forced it open.
- The license texts live at `port/COPYING` (GPLv3, incorporated by reference) and
  `port/COPYING.LESSER` (LGPLv3). Both ship; `port/NOTICE.md` §6 states the linking terms.

**Tests and goldens**
- A golden hash proves *nothing about orientation, colour or units* — F1 and F2 both survived a visual
  check and a pinned hash. **Asymmetric or unit-bearing behaviour needs its own directional assertion**
  independent of the golden.
- **Never pin a total over a whole data directory.** New TestData has broken whole-directory counts
  four times. Exact counts go on **one named cell/tile**; whole-root tests assert structure and lower
  bounds. **Two corollaries, both collected 2026-08-12**: don't NAME a fixture's input files either
  (`{map.osm … map-4.osm}` became `kNotFound` in 15 tests when the extract was re-exported with
  three) — enumerate them; and **never pin a settings file's contents**, only the mechanism that
  reads it, or editing the settings breaks the build (`port/families/*.json`).
- Re-pin a golden only after looking at the PNG, and record the old → new hash in the ledger row.
- Choose a fixture that can *show* the thing under test (the DTED band bug hid behind a flat cell).

**Vector seam**
- `VectorSymbol` is **y-UP**; `CgmSymbol`'s display list is y-DOWN and `ToVectorSymbol` applies the VDC
  direction multipliers. `bounds().top` is the *smaller* value.
- Style engines are **loaders over `LookupTableStyleEngine`** — do not write a fourth engine.
  Product-specific data properties (SCAMIN, `dispcat`) belong in the **source**, not the style engine.
- `IStyleEngine::Style()` **appends** — one row can paint at several priorities. Sort is stable across
  features by priority.
- The retained scene keys on the whole `StyleContext` and an **exact** scale; a no-op setter must stay a
  no-op or the cache never lands; `style_epoch()` defaults to 0 deliberately.
- The pick index is filled from the primitives the renderer **emits**, so a tap agrees with the screen.
- One comparison rule, one implementation (`rules.h`, since R2).

**Data/product facts worth not re-deriving**
- ENC `.000` is the **base edition only** — `.001…` updates are unapplied and the reader says so loudly.
  Enumerate by finding `*.000`, never the `ENC_ROOT/<producer>/<cell>/` path. `CATALOG.031` is itself
  ISO 8211 and gives bounds + long name for free. A cell's **usage band is its scale band**, not a place.
- S-57 `?` in a lookup condition is the **UNKNOWN-VALUE marker, not a wildcard** (read as a wildcard it
  paints the harbour no-data grey).
- **S-52 gives the display priority to the LOOKUP, so text inherits its object's band unless it is
  lifted out.** The library authors text-bearing rows at every priority there is — `LNDARE` carries
  a `TX` at Group 1 — so text at the object's own band is text under the rest of the chart. E8's
  `kS52PrioTextBase` is the fix; the same trap is waiting in any product whose table states one
  priority per row.
- **S-52 TX/TE placement decodes from the library's own rows, and it is worth re-deriving it that
  way rather than trusting a recollection of the spec.** HJUST/VJUST: 1 = centre, 2 = right/bottom,
  3 = left/top — `SEAARE` centres an area name on its centroid with `(1,2,0,0)`, and `BOYLAT`
  offsets LEFT at HJUST 2. XOFFS/YOFFS are in units of the text's own BODY SIZE, y positive DOWN.
  `CHARS` is style/weight/width/2-digit body size, but the delivered library also carries
  4-character forms (`'1508'`), so take the size from the LAST TWO characters, not a fixed offset.
- 17 S-52 symbol names are **raster-only definitions** — a vector-only path cannot draw them (E6 added
  `SymbolPixmap` at the seam).
- CGM monochrome pattern bits are stored **INVERTED**; ink coverage is the fraction of *clear* bits.
- An MBTiles file's declared `bounds` **cannot be trusted** — derive coverage. A tile carries a buffer of
  its neighbours' geometry. There is no OSM SCAMIN analogue; a scale-less bulk query is capped by a tile
  budget.
- **Zoom↔scale is latitude-dependent and there is exactly ONE implementation of it**:
  `webmerc::ZoomForScaleExact` / `ScaleForZoomExact` (`port/Osm/fv_web_mercator.h`). A Web Mercator
  pyramid holds scale constant per PIXEL, so z12 is 1:270k at the equator and 1:190k off Charleston.
  A source uses the viewport's centre latitude; a style engine is handed a scale and no geography,
  so it must be TOLD one (`OsmStyleEngine::SetReferenceLatitude`) — and the same `mm_per_pixel`
  the source got, or minzoom disagrees with the tile that was read.
- MapLibre's `!=` and `!in` are **true for a MISSING tag**; fvkit's `kNotEqual`/`kNotIn` are false
  (rules.h: a missing attribute makes every comparison false). The OSM loader reconciles the two
  with `Or(Missing(k), …)` — do not loosen the shared predicate for one product.
- **An OSM style's layer ORDER is the style file's business, not the port's.** Checked in E8 after a
  "roads draw over paths" report: `peregrine-osm.json` puts `road-path` at 17, above every road
  (12-16); the untracked `styles/style.json` is **CyclOSM**, where `road_path` is 18 and the roads
  are 20/26/27/28. Both render faithfully. Ask which style is loaded before reading an ordering
  complaint as a bug.
- A GL style's draw order is **style-layer order, not feature order**: one road feature emits both
  its casing pass and its fill pass, and every casing in the viewport must be drawn before any
  fill. `StyleResult::priority` = the style layer's index does this, because `VectorScene`
  stable-sorts by priority ACROSS features.
- DTED: level-3 out of scope; lookups are nearest-neighbour; elevation bands default in **feet** and
  need the converting setter.
- **A turn restriction cannot be expressed by a Dijkstra label per node** — the cheapest approach to
  the junction may be exactly the one the sign forbids. Split only the junctions a restriction names
  (one state per arc it can be entered along, +1 for "arrived along nothing"); everything else stays
  one state per node, which is why an unrestricted graph pays nothing. In a bidirectional search the
  two frontiers label a split junction *differently* — forward by the arc in, reverse by the arc out
  — so a meeting is a PAIR of states joined only if that turn is legal.
- **An OSM mode key falls through to the generic `access`**, so `access=private` alone speaks for
  every mode at once — it is not a motor-vehicle statement. Since O5b `private` is **priced, never
  deleted** (`kArcPrivate*` + `RouteOptions::private_penalty`): a road behind a gate is the only way
  to whatever is behind it, and on a gated community it IS the street network. An outright `no` is
  still a denial. On Kiawah `bicycle=no` is real and strictly enforced (bicycles are expected on the
  private cycleways), so the two must not be conflated — `--ignore-access` clears both and is a
  diagnostic, not a way to soften `private`.
- **"The arc I arrived along" has ONE definition — `ArcBetween(node, previous)`** — even though a
  path step sometimes carries the exact arc and `ArcBetween` returns only the first of a parallel
  pair. Both searches label a state by it, so a seed computed any other way makes the answer depend
  on which search ran (measured, O5d: 50 seconds apart over an identical node sequence).
- **A constraint that must hold in a bidirectional search cannot be a filter on the first
  relaxation.** Rejecting a meeting does not remove the reverse frontier's LABEL, and Dijkstra keeps
  only the best one — so the better route the constraint should have forced is already gone. Express
  it as a barred turn (O5d) or a split state (O5a), both of which every frontier consults.
- OSM PBF `Relation.memids` is **delta-encoded across all members whatever their type**, and
  `roles_sid`/`types` are parallel arrays. An OSM restriction names WAYS; a graph knows ARCS, and the
  join is the via node's own adjacency. `via way` restrictions exist and are not the same problem.
- **There is exactly ONE place an arc's duration is decided — `ProfileSeconds` — and every cost
  path must go through it.** `Router::ArcCost` had a branch that called `RouteProfile::Seconds`
  directly, which was identical for every class until O5e gave the ferry its own clock; then the
  search costed a crossing at the profile's flat walking speed while `Materialize` (which does go
  through `ProfileSeconds`) reported the boat's, and a walker was sent over a bridge that is
  actually slower. Caught in review, and the test that pins it is deliberately one where the wrong
  cost changes the ROUTE CHOSEN — a test that only checked the reported seconds passed under the
  bug, because the bug was never in what was reported.
- **And there is exactly ONE place an arc's USABILITY is decided — `fv::routing::ArcUsable` —
  for the same reason** (Pippin P11, 2026-08-20). It was `Router::ArcUsable`, private, and the
  member still calls it; it came out because something that is not routing needed the answer.
  Pippin's pick button names the nearest road a rider could actually ride on, and the snap index
  it queries is built at `RoadSnapFilter::kAll` — so the nearest candidate is very often a road
  the profile refuses (Kiawah's parkway carries `bicycle=no` for its whole length). A second copy
  of the rules would drift, and the drift is visible: a button naming a road the very next replan
  routes around. The options come from `RoutePlanner::BuildOptions`, public for the same reason —
  same rule file, same profile lookup, same poll.
- **A ferry is not a `highway=*` way.** `route=ferry` with no highway tag at all is the normal
  tagging, which is why "avoid ferries" was a DATA gap and not a preference gap: there was nothing
  in the graph to avoid. `route=ferry` takes first refusal over any `highway` the way also carries
  (a slipway). `duration` is `hh:mm:ss` shortened to `hh:mm` or **a bare count of MINUTES** — "20"
  and "0:20" are the same crossing, which is the one trap in the tag.
- `GEO_east_of_degrees(a, b)` means "**a** is east of **b**".
- The scale ladder gives the **current product first refusal** before crossing to another product.

---

## 4. Per-module recipe

1. Create `port/<module>/CMakeLists.txt` compiling sources **in place** from `fvw_core/<module>/`.
2. Uncomment the module's `add_subdirectory` in the root `CMakeLists.txt`.
3. `cmake -B build && cmake --build build` — fix **compiler-first**, don't read files speculatively.
   Typical: drop `stdafx.h`/afx headers, `CString`→`std::string`, TCHAR removal, `__int64`→`int64_t`,
   case-sensitive `#include` paths, `_stricmp` etc. via `fv_compat.h`.
4. Repeated mechanical transforms → a script in `port/tools/`.
5. gtests pinning known-good values in `port/<module>/test/`; `ctest --test-dir build`.
6. **Update this ledger** (move the item out of §2, add a one-line row to the archive) and commit:
   `port(<module>): compiles+tests on macOS`.

**Session protocol**: one module per session · never re-explore the repo broadly · no subagents ·
commit and update this file before ending.

---

## 5. Where to look in the archive

`port/PORTING-ARCHIVE.md` holds the completed module table (rows 1–14u, R1–R3c, E1–E8, F1–F3, S1,
O1–O5e, K1, T1, T2, M1, A1–A6, G1–G4, MM1–MM5, PR1–PR3, S1–S4 search), the dated decision log,
and — appended
2026-08-16 — **"Condensed out of the working ledger"**, which is the long-form §1 this file used to
carry plus every resolved §2 item, verbatim. Read that section when you want the build narrative
for something §1 above only names.

Useful entry points:

- **Global search (`fvkit/app/search.h`)** — the S1 row (2026-08-27: the capability, the session,
  the shared match rule, and the self-move-assign bug a filter that KEPT its row still emptied a
  title with) and the S2–S4 row (2026-08-28: the map as an overlay, the gazetteer inside the pack,
  and the road graph answering the routable half). The design is
  `port/search-plan-COMPLETE.md`; the built detail is §1b above.
- **Moving map (`fvkit/nav`)** — 2026-08-18 (**MM7** the app side: the session whose proof is that
  the shell's tick did not change, the binding rules that turn a Status into a raise and an
  out-parameter into a None, the transport seam deliberately left without a Python trampoline
  because `add_data` is the cheaper door, the feed as a tuple with one `set_source`, the one empty
  field that chooses UDP over TCP, emit-per-sentence as the live feed's answer to MM6's measured
  epoch of latency, the status line that shows bytes AND sentences AND fixes because otherwise a
  parse failure looks like silence, the explicit open that re-reads where a feed switch does not,
  and the headless drive over real TCP and UDP sockets),
  2026-08-17 (**MM5** snap-to-road: the seam that lets two libraries
  meet without linking and the one inline function that pays for it, every score term in metres, the
  hold as an infinite stay bonus rather than a branch, snapping BEFORE the resolver and the two-way
  road that cannot say which way along itself the ship is going, the index built over the geometry
  after a dogleg found it built over the vertices, and the residual that is one axis of the noise —
  which is the honest reason MM5b is still open),
  2026-08-15 (**MM4**: the overlay as the one object that is
  both drawn and fed, the tick that answers and applies nothing, every fix to the resolver and
  only the last to the camera, the mode change that forces a recentre, the negated symbol
  rotation that sixteen passing assertions could not see, and the apron that a zero-sized
  window does not clear),
  2026-08-15 (**MM2**: the apron built from the DRAWN position and
  why rebuilding it from the new one freezes the map, the convergence accessor the plan asked for
  and equal-arc does not need, the track-up offset that is two unit vectors rather than a rotation
  with a sign error, "continuous" centring that jumps at its own case boundaries, the once-only
  wrap that only a wild convergence can defeat, and the one place a bit-faithful port had nothing
  to be faithful to because the original's cast is undefined),
  2026-08-15 (**MM1**: validity per field instead of the -1000
  sentinels, the thread rule and the queue that drops the oldest, the clockless scripted source,
  the track builder that takes a polyline so fvkit still does not link Routing, the heading
  derivation kept in screen space, and the original's `atan` fix-up on an `atan2` that points a
  southbound ship backwards — kept in the test as the oracle it is not).

- **Overlay drawing (`fvkit/geo`)** — 2026-08-15 (**G4**: a highlighted thing still drawn as
  ITSELF and the test that could not tell a gained band from a recoloured one, the line that
  takes one wider stroke instead of eight offset ones, the highlight kept out of the pick index
  so a selected feature is not a bigger target, the outermost-stamp-only rule, the tint that
  keeps a tile's ALPHA, and the 2x stamp cap that only a look at the render would have found),
  2026-08-13 (**G1**: the geodesy that was already in the
  tree, the geographic clip as the thing worth porting, the screen-derived step size, the two
  bit-faithful quirks in the rhumb clipper, the antimeridian break that replaces FalconView's
  wrapped second segment, and the two test ORACLES that were wrong before the code was —
  great-circle bearing used to check a rhumb line, and "bows poleward" asserted on a segment
  whose vertex lies past its own endpoint).

- **App layer (`fv::app`)** — 2026-08-13 (**A6**: the acceptance test — a capability as a method
  you defined, the aliasing shared_ptr a Python factory needs, the editor proxy that is unwrapped
  on the way back, the first C++ file overlay and why its document is SQLite, the sample's
  ambiguity as a pinned data property, and the shell that had to exist before the constructor
  finished),
  2026-08-13 (**A5**: draw order as the one definition of who is on
  screen and picking as its reverse, the hit id that could not be a packing and became a stable
  handle, hover notified only on a change, and the two verbs the capability class names forced
  to be renamed),
  2026-08-13 (**A4**: the dance as two directions with only one of
  them a call, the observer that makes the other one hold from anywhere, the per-type editor
  instance that is cached so tool state survives, the focus bracket asserted as an ORDER rather
  than a pair of counters, and why the release moved out of `OverlaySession::Close`),
  2026-08-12 (**A1**: the string TypeId, the optional `FileTypeDesc` as
  the whole static-vs-file distinction, capabilities by accessor rather than `dynamic_cast` and why
  the trampoline forces it, and the two headers that had to land early because a `unique_ptr` cannot
  be returned through an incomplete type; **A2**: the stack grown in place and inert without a
  registry, the top-most band as a second draw pass rather than a stack position, capture exclusive
  for the mouse and first-refusal for keys, and the one-delivery-per-event deviation from
  `C_ovl_mgr::select`; **A3**: R1 as the thing that makes the layer testable at all, cancel-is-not-
  failure and why it propagates, the flows that refuse to leave a half-built document in the stack,
  the `save_format_index` the plan did not have, configuration in memory because S1 says the
  settings file is the user's, and `Exit`'s snapshot-before-close bug).

- **Vector seam design** — 2026-07-23 (V5a/V5b), 2026-07-27 (E3b placer, E3c extraction).
- **Rule layer** — 2026-07-26 (R2: predicate AST, ScaleBand, ViewingGroup, `ResolvedPlan`).
- **Perf** — 2026-07-28 (R3a retained scene / R3b "the plan was aimed at the wrong costs" + symbol-atlas deferral),
  2026-08-08 (R3c: the ledger's own build command was -O0; the DNC parsed-feature cache; the
  `FeatureBatch` measured away; the geographic pattern anchor).
- **ENC/S-52** — 2026-07-25 (E1 base-edition), 2026-07-27 (E2 PresLib inventory, E3a/E3b CS procedures
  and their reductions), 2026-07-28 (E5 defects vs a published chart, E6 pixmaps, F3 usage bands),
  2026-08-12 (**E8**: the text band, and the four TX/TE placement parameters decoded off the
  library's own rows),
  2026-08-11 (**E7**: the library's 0.32 mm symbol grid measured out of its own dual-form symbols,
  bearing-vs-screen rotation at the SY seam, SNDFRM02's digit layout read off the bitmap pivots).
- **DNC/GeoSym** — 2026-07-20 (V1 reader + two CString silent-corruption bugs), 2026-07-21 (V3),
  2026-07-24 (areas, map-scale-vs-feature-zoom, WVS root cause), 2026-07-25 (F1 fills), 2026-07-27 (F2 flip).
- **Display/projection** — 2026-07-24 (physical scale + the aspect-ratio bug),
  2026-08-15 (**PR1** rotation: the identity guaranteed by gating the arithmetic rather than by
  an identity matrix, the cardinal angles taken off a table because cos(pi/2) is 6.1e-17, the sign
  pinned from both ends, the viewport turned in PIXELS because the geographic frame is anisotropic,
  the sqrt(2) query box as the stated price, and the retained scene that needs no key because R3a
  made its ink geographic),
  2026-08-15 (**PR2** the vector path: twenty lines of code under two hundred of test, the one
  angle the projection cannot see, the geographic-anchor vs pixel-anchor split that left the
  ownship untouched, and the area pattern deliberately not turned — mutation-checked),
  2026-08-15 (**PR3** the raster path and the shell: the gate that keeps rotation 0 byte-identical,
  the MASK that replaces the straight path's clamp, four corners and three samples, the test frame
  that has a north and a south because a footprint proves nothing, the half-pixel tie the two paths
  break differently, the adopted `proj.Rotation()` that retired MM4's duplicate state, and the pan
  delta turned back into the chart's own axes).
- **Settings** — 2026-07-28 (S1: INI over JSON, read-only, three failure modes),
  2026-08-12 (**M1**: the mariner's depth numbers read off GeoSym's own tables, and data
  families as named groups of rule selectors).
- **OSM** — 2026-08-04 (O1: dependencies, bounds, zoom clamp, tile budget, two geometry deviations),
  2026-08-08 (O2: the one zoom↔scale relation, the declared style subset, style-layer draw order).
- **Input** — 2026-08-04 (K1: VK codes over X11 keysyms, tk binding semantics).
- **Routing** — 2026-08-10 (O4 graph + bidirectional Dijkstra, O4b profiles/access, O5a turn
  restrictions and the split-state search), 2026-08-11 (O5d ordered stops: the seed, the barred
  turn, and the parallel-arc rule the two searches disagreed over),
  2026-08-12 (**O5e**: the ferry as a class rather than a flag, `duration` as the crossing's clock,
  the toll bit, and the one cost path that had been bypassing `ProfileSeconds` since O5c).
  **`O5c` has no archive row** — the session committed the code (`ea3aed90`) and updated §1 but
  never wrote one. §1's "Cost rules (O5c)" paragraph and that commit are the record.
- **COM severing pattern** — 2026-07-11 (GeoTIFF `IDatumConvert` is the worked example).

