# Pippin plan (P1–P20) — cycling/walking navigation for iOS over Peregrine

**Status: P1–P20 ALL BUILT — P1–P5 2026-08-18, P6–P8 + P12 + P13 2026-08-19, P9 and P11
2026-08-20 (points, then the compass, then the pick button saying WHERE), P10 and P14
2026-08-21, P15–P19 2026-08-25. The snap-points half of P9 became P19 and went in as a
capability of the overlay SPI rather than a picker in the route sheet — Chris's call, and
the right one: any overlay can now be snapped to by implementing an interface, and two
already do.** What they delivered
and what they left open is in `port/apps/Pippin/README.md`; the archive rows are the
narrative. P6 departed from the sketch in one named way: **there is no `PPRoutePlanner`
class**, because a public planner object would be a second handle to render-queue state
and P3's threading contract closes exactly that trap — the C++ went into
`PippinKit/PPRouteStore.{h,cpp}` instead, pure `std::` so the mac tests it (15 gtests,
including the relaunch acceptance criterion). P1's two open
items are both CLOSED: the label font is DejaVu Sans, sourced from the build machine rather
than downloaded (Bitstream Vera licence, notice staged beside it), and the pixel question is
`kCGImageAlphaLast | kCGBitmapByteOrder32Big`, measured on the phone by a `PPPixelProbe` that
ships. Requirements:
`port/apps/Pippin_requirements.md`. One session per P-step, the FVW session protocol applies
(ledger first, compiler-driven, commit per step, no subagents).

**Goal**: an iOS app — offline cycling and walking navigation on Kiawah Island — built as the
**third shell** over the Peregrine core, after PythonView and the CLIs. The pitch of this plan is
that almost everything Pippin needs ALREADY EXISTS headless and tested: the moving map is MM1–MM7,
routing with walk/cycle profiles and via points is O4–O5e, OSM rendering is the O-series, the route
document is route.py's `.fvrte`, and the evaluation ride is `testdata/kiawah_cycle.gpx`. What this
plan actually builds is: an iOS build of the core, a small Objective-C++ facade, a SwiftUI skin,
a C++ route overlay (so route logic stops living only in Python), a trip computer, and a GPX
writer. The mac build remains the test bed for every line of C++ — iOS gets only what cannot be
tested anywhere else.

## Decisions taken (2026-08-18, Chris: no strong preference on language/build — so decided here)

- **UI language: Swift + SwiftUI.** The UI is deliberately thin (D-series thinking: the shell is a
  skin over `fv::` seams), and SwiftUI is the least code per screen on current iOS. Everything
  with logic in it is C++17 under `port/`, testable on the mac.
- **Bridge: `PippinKit`, a hand-written Objective-C++ facade — the pyfvw of iOS.** Not
  Swift↔C++ direct interop: fvkit's API is `shared_ptr`/`Status`/`std::function`-heavy, and a
  curated facade (a dozen classes, each mirroring a seam the app plan already named) keeps the
  boundary reviewable, exactly as pyfvw does. Rule: **nothing UIKit/Foundation leaks below
  PippinKit; nothing `std::` leaks above it.**
- **Rendering v1: CpuCanvas → CGImage → SwiftUI.** The retained VectorScene plus a
  Kiawah-sized dataset makes CPU rasterizing plausible on an A-series chip; prove it before
  building a GPU path. If measured too slow, the stated escape is the V7 CoreGraphics `ICanvas`
  backend (vpf-geosym-plan §V7) — a new backend, not a new architecture.
- **Position: CoreLocation.** The nav plan's "CoreLocation stays out until NMEA-over-TCP proves
  insufficient" was a DESKTOP rule (the phone feeding the mac); on the phone itself CoreLocation
  is the only feed. The adapter is one ObjC++ class delivering `PositionFix`es into MM1's
  `FixQueue` — the threading contract is already written ("a source delivers on whatever thread
  it likes; the app pumps the queue").
- **Offline by construction**: the app bundle carries `kiawah.mbtiles` (a bbox cut, P1),
  `kiawah.fvroad`, the peregrine-osm style + sprites, and fonts. No network code in v1 at all;
  the requirements' "could access existing servers" is future work.
- **Route document stays `.fvrte` v1**, byte-compatible with route.py — Pippin must open a route
  PythonView saved and vice versa. The route overlay is ported to C++ (P5) so the logic exists
  once; route.py keeps working untouched.
- **App-layer usage: `OverlayManager` + overlays, NOT `OverlaySession`/editors.** A3's AppShell
  inventory (file dialogs, menus, mode dance) is desktop-shaped; Pippin's flows are SwiftUI
  sheets and its v1 editing is "replace the waypoints from the sheet", not click-drag. Adopt the
  session/editor machinery only if a later feature (P9 point editing) earns it.
- **Deployment target iOS 17**, iPhone only, portrait-first. Development on the simulator with a
  location script; evaluation on Chris's phone via Xcode sideload.
- **Licensing settled for the Swift, still open for the store** (decided 2026-08-18): Peregrine
  is **LGPL-3.0-or-later**, which is what makes Pippin possible — under LGPL-3.0 §4 an
  application that links FvKit is a Combined Work, not a derivative, so **Pippin's Swift can
  stay closed and unreleased**. Had Peregrine stayed GPL-3.0, linking it would have forced
  Pippin's own source open. The obligations that remain are the §4 ones: ship `COPYING` +
  `COPYING.LESSER`, carry the GTRC and Peregrine notices in the about screen, and link FvKit as
  a **dynamic framework** so a user can relink it (§4d) — cheap, and worth doing from P2 rather
  than retrofitting.
  **The App Store is a separate, unsolved problem.** LGPL-3.0 inherits GPL-3.0 §10
  ("no further restrictions"), which the App Store's per-Apple-ID device limit is widely read to
  violate — the VLC precedent. LGPL does not rescue this: the constraint rides on the
  FalconView-derived code, not on the Swift. Chris cannot self-authorize an exception because
  **GTRC**, not Chris, holds the upstream copyright. So: personal sideload and simulator work are
  unaffected and always were; TestFlight is already distribution and inherits the same problem;
  a public ship needs either a non-App-Store channel or a written additional permission from
  GTRC under GPL-3.0 §7. **The obligation triggers on distributing the binary, not on publishing
  source** — a private repo is not a shield.

## Architecture (the whole thing on one page)

```
SwiftUI            MapScreen · RouteSheet · StatsBar · CompassButton      (P2,P6,P8,P9)
   │ Swift
PippinKit          PPMap (engine+stack+render) · PPLocationSource         (P2,P4)
(ObjC++ framework) PPRoutePlanner · PPTrip · PPRecorder                   (P6,P8,P10)
   │ C++17
fvkit              MapEngine+proj(rotation) · OverlayManager · CpuCanvas
                   nav/ (FixQueue·camera·slew·MovingMapOverlay·gpx·trip*)
                   OsmVectorSource · OsmStyleEngine · VectorScene
port/RouteKit      fv::RouteDoc (.fvrte) · fv::RouteOverlay · RoutePlanner
port/Routing       RoadGraph · Router (walk/cycle, via, restrictions) · RoadSnapper
data (bundled)     kiawah.mbtiles · kiawah.fvroad · style+sprites · fonts
data (Documents/)  current.fvrte · recorded trips (*.gpx) · pippin.ini
```
`*` = new in this plan (P8; RouteKit was, and is built as of P5). Everything else exists and
is tested.

**Threading contract** (MM1's, restated for iOS): CoreLocation delivers on its own queue →
`FixQueue`. A `CADisplayLink` tick on the main thread drains the queue, `Tick`s the moving-map
overlay, applies the resulting `CameraTarget` through `CameraSlew`, and marks the frame dirty.
Rendering runs on ONE background render queue (never concurrent with itself); it publishes a
finished `CGImage` to the main thread. The engine and stack are touched from the main thread
only — the render queue works on a snapshot (`VmapBounds` + scene), which is what the retained
scene already makes cheap.

**Dirty discipline**: a frame re-renders only on (a) gesture settle, (b) camera change from a
fix, (c) style/overlay epoch. During a live gesture the LAST rendered image is translated/scaled
as a preview and the real render happens on settle — the standard slippy-map trick, stated here
so nobody "fixes" the blurry mid-pinch frame.

## UI design (so every session builds toward one coherent screen)

One screen, map full-bleed edge to edge (content-first, per HIG); controls float in the safe
area over the map, using system materials so light/dark adapt for free. **The map itself has one
look** — the peregrine-osm style — in v1.

- **Lower-left: round Route button** (SF Symbol `point.topleft.down.curvedto.point.bottomright.up`).
  Tap opens the route sheet; when a route exists the same sheet opens pre-filled for editing
  (requirement: "clicking route when route already exists just allows user to change info").
- **Lower-right: round GPS/Start button** (`location`, filled+tinted while active). Tap enters GPS
  mode; tap again exits (requirement). GPS mode = auto-center + track-up + stats visible.
- **Stats bar** (GPS mode only): a translucent capsule across the top safe area — elapsed,
  current speed, distance gone, distance to go, ETA — monospaced digits so the layout never
  jitters. Hidden outside GPS mode.
- **Route sheet**: a medium-detent SwiftUI sheet. Rows: Start, End, then zero-or-more Via rows
  with an "Add via" button. Each row offers **Current location | Pick on map** (P9 adds
  **Point name**). "Pick on map" dismisses the sheet into a picking state: crosshair at screen
  center, pan the map under it, confirm/cancel buttons — one mechanism serves start, end and via.
  Below the rows: a Walk/Cycle segmented control. OK computes and dismisses; Cancel changes
  nothing; a Clear Route action removes the route.
- **Compass** (P9): top-right, appears when the map is rotated, tap = back to north-up and
  toggles auto-rotate — the platform-standard affordance.
- **Startup**: opens on Kiawah (the bundled data's bounds), north-up, GPS off, ownship drawn if a
  fix arrives (requirement: shown but NOT auto-centered).

## Sessions

Each is one sitting, mac-tests-first where the code allows it, ending with a commit and a ledger
row. P1 and P5 and P8 and P10 are entirely mac-testable; the iOS-only sessions (P2,P3,P4,P6,P7,P9)
end with a simulator screenshot as the acceptance artifact, which is how progress stays evaluable.

### P1 — The data pack and the iOS cross-build (all headless, all mac-provable) — **BUILT 2026-08-18**
The session that makes every later session possible, with no Xcode in it. Built as planned with
four departures, all recorded in `port/apps/Pippin/README.md` and the archive row: the cut is a
new `port/tools/mbtiles_cut.py` rather than an `fvpack cut` (fvpack builds GeoPackage pyramids
over the catalog — the wrong shape entirely); the bbox was widened to `-80.17,-79.97` x
`32.55,32.67` to cover the `map*.osm` extracts as the plan asked; `kiawah.fvroad` needed no
rebuild (it was already the access-honouring one, byte for byte); and P1 went one step past its
own acceptance by linking and RUNNING `pippin_link_check` on the simulator, which draws the
Kiawah frame P2 was going to be the first to see.
- **`fvpack cut`** (or a new small tool in `port/tools/` if fvpack's shape doesn't fit): copy a
  bbox subset of an MBTiles into a new MBTiles — pure SQLite (`tiles` rows in range per zoom +
  metadata rewrite with new bounds). Cut **`testdata/OSM/kiawah.mbtiles`** from
  `us-south.mbtiles` (Kiawah + a margin: lon −80.14..−80.00, lat 32.56..32.66 — verify against
  the map*.osm extracts' bounds, all zooms present in the source). Expect a few MB. Verify with
  `fvrender` on the mac: the same Kiawah frame from the cut and from us-south must be
  **pixel-identical**.
- **Rebuild `kiawah.fvroad` WITHOUT `--ignore-access`** — the 2026-08-17 artifact ignores access,
  but O5b's whole point is that on a gated island `access=private` is priced, never deleted.
  Confirm `fvgraph route` still finds "Ruddy Turnstone to the beachclub" for walk and cycle.
- **Assemble the bundle manifest** `port/apps/Pippin/Data/` (or a script that stages it):
  kiawah.mbtiles, kiawah.fvroad, `peregrine-osm.json` + sprite sheet, the TTFs the style/
  PythonView use (check what `SetDefaultFont` is fed today; bundle an OFL-licensed font), a
  `pippin.ini` (fv::Settings) naming all of it by bundle-relative path.
- **iOS toolchain build**: a CMake preset (`build-ios/`) with `CMAKE_SYSTEM_NAME=iOS`, static
  everything, for `ios-arm64` and the simulator arch. New umbrella target **`pippin_core`**
  (fvkit + fv_osm + fv_routing + deps) behind an `FVW_IOS` option so the default build is
  untouched. FetchContent deps (expat/zlib/vtzero/json) are all static C — expect them to just
  compile; SQLite comes from the iOS SDK's libsqlite3. **Tests do not run on iOS**; acceptance
  is that the libraries link and `ctest` on the mac is still all green.
- Likely snags to budget for: `#include <sys/...>` guards in line_transport (sockets DO exist on
  iOS — leave them in), and any stray `std::filesystem` calls needing the iOS 13+ availability
  floor (we are on 17, fine).

### P2 — The skeleton: Xcode project, PippinKit, a Kiawah map on screen — **BUILT 2026-08-18**
Built as planned with three departures, all recorded in `port/apps/Pippin/README.md` and the
archive row: **`PPMap` owns no `MapEngine`** (the pack has no raster in it, so the engine would
be an empty catalog and a `RenderBaseMap` that draws nothing — PythonView's vector path drives a
bare `MapProjection` for the same reason); the `.pbxproj` is **hand-written with explicit file
references** and Xcode does NOT build the core (a script phase checks for it and prints the
command); and P2 also closed P1's font item, which was supposed to need Chris. The framework is
**dynamic**, which is the LGPL-3.0 §4d obligation being met at the cheap moment rather than the
expensive one.
- **`port/apps/Pippin/`**: `Pippin.xcodeproj` — app target `Pippin` (SwiftUI), framework target
  `PippinKit` (ObjC++), linking P1's static libs (a script phase or XCFramework packaging step,
  whichever is less magic).
- **PippinKit v1 is ONE class**: `PPMap` — init with the Data paths (settings/style/mbtiles/
  fonts), owns MapEngine + OverlayManager + CpuCanvas; `setCenter:scale:size:`,
  `renderFrame` → `CGImage`. **Settle the pixel question here once**: `PixelBuffer`'s byte
  order and alpha vs `CGBitmapInfo` (premultiplication!) — one deliberate test image (a red/
  green/blue/half-alpha quad rendered and screenshotted) so it is never debugged again.
- **SwiftUI**: `MapScreen` shows the rendered frame full-bleed at the display's scale; the two
  round buttons exist and do nothing yet. App opens on Kiawah (requirement).
- Acceptance: simulator screenshot of a styled Kiawah island. Commit the screenshot beside the
  plan's ledger row.

### P3 — A map you can touch: gestures and the render loop — **BUILT 2026-08-18**
Built as planned, with the camera taken OFF `PPMap` and made a value — the one shape decision
of the session, and everything else follows from it. Detail in `port/apps/Pippin/README.md`.
- **`PPViewport` is the camera, immutable**: centre, scale, rotation, surface, and the pack's
  own limits, with every derivation (`resized`, `panned`, `zoomed`, `atHomeView`) returning a
  new one. That is what makes the thread split free — a value goes to the render queue and a
  `PPFrame` (image + the viewport it was drawn at) comes back — and it is why the gesture
  arithmetic is `MapProjection`'s own `SurfaceToGeo`/`GeoToSurface` rather than a second copy
  of equal-arc geometry in Swift. **Every gesture is therefore already rotation-aware**, which
  is P7's turned chart paid for in advance.
- **Drag pans, pinch zooms about the fingers, no two-finger rotate** (as planned). UIKit
  recognizers under a `UIViewRepresentable`, not SwiftUI gestures: a map needs the pinch
  centroid every frame, incremental deltas, and the gesture state.
- **The loop**: `CADisplayLink` + dirty flag + one render in flight + a preview transform
  computed from the two viewports. The link **pauses itself** when nothing is moving. ONE
  deliberate departure: the plan says render on settle, and Pippin also renders DURING a
  gesture whenever the queue is idle and the last frame came in under 40 ms — the preview
  covers the in-flight interval exactly, so a live frame costs nothing in correctness and
  removes the empty margin a long settle-only pan would show. The fallback is a measurement,
  not a device guess.
- **Zoom limits from the data**: in = the pyramid's maxzoom at one tile pixel per POINT plus
  five levels of overzoom (1:1,614, ~100 m across a phone — three levels and 1:6,456 until P13);
  out = two levels past the
  pack-fitting view (1:1,317,288), with the file's minzoom as the outer bound. The **centre is
  clamped to the pack's box**, so there is no dragging into an empty ocean.
- **`symbol_dpi_scale` was NOT wired, because nothing draws through `GeoDraw` yet** — the
  overlay stack is empty until P4. The style engines' side of the same question was already
  right (`SetDeviceDpi` from the pack's `mm_per_pixel`), which is why labels are crisp at 3x.
  P4 is where an overlay first needs the symbol half.
- **The drift measurement ships**: `-PPViewportProbe YES`, a `PPPixelProbe`-shaped screen. It
  caught a real defect — `MapProjection` centres the surface on `((w-1)/2, (h-1)/2)` and the
  first cut of the pan assumed `w/2`, a constant 0.24 pt offset on every pan. Now the pan ASKS
  the projection where its centre is. Residual: east-west exact, north-south 0.038 pt over an
  84 pt drag (the cosine of the new centre latitude, which is inherent to equal-arc).
- Acceptance met: `docs/p3-gestures.png` (dragged and pinched, 1:102,112, 14–43 ms/frame),
  `docs/p3-viewport-probe.png` (all nine checks PASS), mac `ctest` still 1370/1370.

### P4 — The ownship: CoreLocation in, no auto-center — **BUILT 2026-08-18**
Built as planned, with the overlay's TICK put on the render queue — the one shape decision,
and everything else follows from it (MM4 ties the camera to the drawing, so the overlay has
to live on the side of P3's thread split that draws). Detail in `port/apps/Pippin/README.md`.
- **The sentinel table is mac-tested.** `PPLocationFix.h` is pure C++ — a POD of CLLocation's
  fields in, a `PositionFix` out — so P4's only real logic is under ctest (10 tests, and the
  mac build now configures `port/apps/Pippin` for that one target). `<= 0` would be wrong
  everywhere; a negative `courseAccuracy` does NOT invalidate a valid course; and
  `horizontalAccuracy` becomes an HDOP through `RoadSnapSettings::hdop_scale`, so MM5's
  radius comes out as the metres the receiver reported.
- **A frame no longer depends only on the viewport** — P3's "same camera, skip the render"
  shortcut is wrong the moment a ship moves under a still map, so `MapModel` grew a second
  dirty flag. And the two feeds need opposite clocks: a live receiver pushes (event-driven,
  the link still pauses between fixes), a `ScriptedSource` has no thread on purpose and gets
  a 4 Hz timer.
- **`symbol_dpi_scale` is wired** (the ledger's symbol-DPI gap, closed for the first overlay
  that would show it): one iOS point / the viewport's mm-per-pixel, so an authored pixel is a
  point. Which caught that **`fv.north` does not fit the shape box** — it is drawn at twice
  the box along its axis, so `SetSizePx`'s "full width" is true of `fv.ownship` and not of the
  chevron. The pack asks for 14.
- **Both feeds run**: `-PPDemoFeed YES` replays the bundled `kiawah_cycle.gpx` (the GPX has no
  course, so the heading is DERIVED in screen space), and `simctl location start` drives the
  real `PPLocationSource` (CoreLocation reports one, so it is REPORTED) — MM1's asymmetry
  visible in one readout. Acceptance met: over 30 s of demo ride the ship moved and the frame
  stayed at 1:46,987 / 2310 features / 1039 draws, the same numbers twice.
- What P4 leaves for P7: a frame costs its whole vector render (47–65 ms at z14) to move one
  chevron, because the overlay shares the base map's canvas pass; and the tick's camera answer
  is computed and dropped, because auto-centring is off.

Original plan text:
- **`PPLocationSource`** (ObjC++): `CLLocationManager` (WhenInUse authorization + purpose
  string; `desiredAccuracy` best-for-navigation, `activityType` fitness) → `PositionFix` — every
  field with its validity honored: negative `course`/`speed`/accuracy mean INVALID (CoreLocation's
  own sentinel convention meets MM1's rule 1), `horizontalAccuracy` maps to the hdop-shaped
  radius the RoadSnapper floors and caps.
- `MovingMapOverlay` joins the stack with auto-center and auto-rotate OFF (requirement: shown,
  not centered). The `CADisplayLink` drain from the architecture section gets built here.
- **Development feed**: convert `kiawah_cycle.gpx` into an Xcode scheme location script so the
  simulator replays the real ride; the demo path via `ScriptedSource` stays available behind a
  debug setting (the same tuple dispatch idea PythonView uses).
- Acceptance: ship symbol rides around Kiawah on the simulated feed while the map stays put;
  heading comes from the resolver (screen-space rule) and the symbol points along the road.

### P5 — The route, moved to C++ where every shell can reach it (mac session, fully tested) — **BUILT 2026-08-18**
Built as planned, with the departures at the end of this section. New module **`port/RouteKit/`** (fvkit must NOT link `port/Routing` — that invariant is stated
twice in the ledger, so the route overlay lives in a module that links both):
- **`fv::RouteDoc`**: read/write `.fvrte` v1, byte-compatible with route.py (same
  format/version strings, same field names, `indent=2` writing so diffs stay human). The
  existing `Routing/rules/Ruddy Turnstone to the beachclub.fvrte` becomes a test fixture.
- **`fv::RoutePlanner`**: graph + `RouteRulesFile` + profile → legs. Straight legs (great-circle,
  route.py's honest default) or road-following via `RouteVia` (one route through the ordered
  waypoints — already built, O5d). Walk and cycle profiles only surface in Pippin.
- **`fv::RouteOverlay`** (an `fv::Overlay`): draws leg lines + waypoint diamonds + labels with
  `GeoDraw`, matching route.py's look; carries the doc and the planner result; `hit_test_point`
  for later. v1 editing surface is `SetWaypoints(...)` — wholesale replacement, which is exactly
  what a sheet-driven UI produces. route.py is left untouched (it keeps its editor); a later
  non-Pippin session may rebase it over the C++ overlay if that ever pays.
- Tests all on the mac: fvrte round-trip byte-for-byte, the Ruddy Turnstone route pinned for
  walk and cycle over the P1 graph, overlay draw smoke over the golden harness.

**What was built, and the five places it departed from the sketch above.**
48 gtests in `port/RouteKit/test/`, all green, ASan/UBSan clean; the tree went 1380 → 1428.

1. **The `.fvrte` WRITER is hand-rolled, and that is the whole of the byte-compatibility claim.**
   The plan said "same format/version strings, same field names, `indent=2`" as though a JSON
   library would do it. It will not: `json.dump(indent=2)` is a specific serializer — keys in
   INSERTION order, an empty list on one line and a non-empty one on many, `ensure_ascii` with
   **surrogate pairs** above the BMP, and floats as CPython's `repr` (shortest round-trip digits,
   fixed notation when the decimal point falls in (-4, 16] and scientific outside it). nlohmann
   agrees on most of that and not all of it, so reading is nlohmann's and writing is ours.
   `PythonFloatRepr` is that rule, public because it IS the claim, and it was fuzzed against
   CPython over **120,000 doubles** — realistic coordinates, the exponent cut, and uniformly random
   bit patterns — with **zero mismatches**. The fixture test is the direct one: read the `.fvrte`
   route.py wrote and re-emit it, byte for byte.
2. **The plan's "straight legs OR road-following" is really a three-way fallback**, and it had to
   be, because that is what `follow_roads` already does and the two shells must agree what a route
   IS. `RoutePlanner::Plan` tries `RouteVia`, falls back to independent PAIRS when the through route
   cannot be had at all, leaves a pair that will not route as a STRAIGHT leg, and treats
   `kOutOfCoverage` as the only per-leg failure — every other error is about the REQUEST (a profile
   the rule file does not define, above all), would fail identically on every pair, and is reported
   ONCE rather than hidden as a screenful of straight lines.
3. **`RegisterRouteOverlayType` is a function of its own and cannot be otherwise.** The plan did not
   mention registration; without it `fv.route` is not a type any shell can open. It cannot join
   `RegisterBuiltinOverlayTypes`, because that lives in fvkit and fvkit is exactly what may not link
   the router — so a shell calls both. It carries the shell's ONE planner into every overlay the
   factory makes, which is what lets a dozen open routes share one graph.
4. **The two symbol references got told apart here, as P4 predicted.**
   `RouteOverlay::SetSymbolDpiScale` is the second setter after the ownship's, and the argument is
   in the header: a route's diamonds are the APP's furniture and take the app's unit, while chart
   symbology is pinned against paper and takes the ledger's 0.32 mm. The hit test scales with it
   too — a marker drawn twice the size that did not also become twice the target would be a thing a
   finger can see and cannot press.
5. **Labels default ON, which costs a font.** A waypoint's label is its identity in the document, so
   an unlabelled diamond is a marker with its meaning thrown away — the opposite call from
   `PointOverlay`, and pinned as such. The price is that `CpuCanvas` fails a draw with no default
   font, so a shell that sets none gets nothing rather than a nameless route. Pippin's pack carries
   DejaVu; the trap is noted in the ledger for the next shell.

**What P5 leaves on P6's desk.** RouteKit is **bound to nothing** — no `pyfvw.route`, no PippinKit
class — so its only caller today is its own test and P6 writes the first real one. The status line
is drawn on the canvas at **(10, 20)**, route.py's spot on a desktop window and under the notch on a
phone, so P6 either calls `SetShowStatus(false)` and puts the words in SwiftUI or moves it. And
`SetWaypoints` DROPS the plan by design, so a sheet that re-plans on every keystroke will re-route
on every keystroke — the compute belongs behind the sheet's OK, which is where the plan already put
the spinner.

### P6 — The route flow: the sheet, the picker, the drawn route — **BUILT 2026-08-19**
Built as planned, with one departure (no `PPRoutePlanner` — see the status note at the top)
and one thing the plan did not anticipate: `RouteOverlay::FileOpen` drops the plan, so
"reloads at launch" is an open AND a replan, which is what `RouteStore::LoadAtLaunch` is
for and what `RouteStore.ARouteSurvivesARelaunchWITHItsRoads` pins. Acceptance ran on the
simulator exactly as written below. Details in `port/apps/Pippin/README.md`.

- The route sheet and pick-on-map state exactly as the UI design section specifies. Current
  location uses the last valid fix (disabled with an explanation when there is none).
- `PPRoutePlanner` wraps P5; compute runs off-main with a spinner in the sheet (Kiawah-sized
  Dijkstra is milliseconds, but the seam should not assume that forever).
- The computed route persists to `Documents/current.fvrte` on every change and reloads at
  launch; Route button re-opens the sheet pre-filled (requirement); Clear Route deletes it.
- Acceptance: on the simulator — create Start=current location, End=picked on the beach,
  mode=cycle; the drawn path follows roads; kill and relaunch the app; the route is still there.

### P7 — GPS mode: the moving map earns its name — **BUILT 2026-08-19**
Built as planned, with the acceptance run on the simulator exactly as written below and
three things the plan did not say. **The camera's answer travels on `PPFrame`** — the tick
is on the render queue (P4) and the viewport is on the main thread, so the frame is the
value that already crosses; the shell applies the centre and rotation and keeps its OWN
scale. **The zoom-out rule is `PPCameraFit.h`**, pure C++ with mac gtests, and its
constant is derived from MM2's real track-up anchor `(W/2, 5H/6)`: a route start is
normally BEHIND the rider, so the radius that fits is `min(W/2, H/6)` — the first cut
allowed 0.4 of the smaller side and put the start off the bottom of the screen. And
**the snapper indexes the ROUTER's graph** (`fv::RoutePlanner::graph()` now hands out a
`shared_ptr`), with the filter at `kAll` rather than the network's driveable default,
because this rider is on the cycleway the default exists to avoid. Details in
`port/apps/Pippin/README.md`.

- GPS button → auto-center ON + track-up ON through the EXISTING camera + slew (MM2–MM4): apply
  `CameraTarget` per tick, rotation through the projection (PR1–PR3 built all of this; Pippin
  declares `SetRotationSupported` true). Second tap: modes off, slew back to north-up
  (requirement: clicking GPS again exits).
- **The zoom-out rule** (requirement): on entering GPS mode with a route that is not fully
  in view, widen the scale until current position AND the route's start are visible (compute
  the box, pick the enclosing scale, one slew — not a per-frame fit).
- RoadSnapper ON in GPS mode (the graph is bundled; `Applied()` fixes feed everything
  downstream), OFF when idle.
- Acceptance: replaying `kiawah_cycle.gpx`, entering GPS mode centers and rotates the map with
  the ride, smoothly (slew, never a jump); exiting restores north-up and stops following.

### P8 — The trip computer (mac session: the numbers, then the bar) — **BUILT 2026-08-19**
New header **`fvkit/nav/trip.h`** — headless, tested, no UI in it:
- `TripComputer::OnFix(const PositionFix&)` + `SetRoute(polyline)` →
  `TripStats{elapsed_s, odometer_m, speed_mps, remaining_m, eta_epoch_s}`, each with a validity.
- Rules decided now so the tests can pin them: **elapsed** runs from `Start()` (wall clock, not
  first fix); **odometer** sums fix-to-fix geodesic distance over VALID fixes only, gated by a
  minimum-movement floor (GPS jitter at a standstill must not creep the odometer — state the
  floor, ~2 m, as data); **speed** prefers the reported field, else derives (same preference
  order heading.h uses); **remaining** projects the current position onto the route polyline
  (`ProjectOntoSegment`, already inline in road_snap.h) and sums the tail; **ETA** =
  remaining ÷ an EMA-smoothed speed (constant stated in the header, ~30 s time constant),
  invalid below walking pace so the bar shows "—" rather than "arrival next Tuesday".
- Tests over `kiawah_cycle.gpx` — 1705 fixes at 1 Hz, 1704 s, a known ride — pin elapsed,
  total distance, and end-of-ride remaining≈0 against hand-derived values.
- Then the SwiftUI stats bar per the UI design; it consumes `TripStats` via `PPTrip` and shows
  en-dashes for invalid fields. Trip starts/stops with GPS mode (requirement ties them).
- Acceptance: mac tests pinned; on the simulator the bar ticks through the replayed ride.

**BUILT.** `fvkit/nav/trip.h` + `trip.cpp`, 27 gtests, and the bar on screen. Four notes worth
keeping:

- **THE ANCHOR is the one idea in the header.** A minimum-movement floor applied per FIX breaks
  a walker — 1.4 m/s never moves 2 m between two 1 Hz fixes, so the whole walk reads as a
  standstill. So the floor is measured from an ANCHOR that moves only when a step is counted:
  a walker's steps accumulate against it and are counted in pairs (140 m of walking comes out
  140 m exactly), while a parked bike's jitter stays bounded around it and never crosses. The
  derived speed rides on the same anchor, so the two numbers cannot disagree about whether the
  ship is moving — and at a standstill the numerator stays small while dt grows, so the speed
  falls to zero on its own.
- **The real ride is pinned to the metre: 6097.99 m** against the 6100.84 m a naive fix-to-fix
  sum gives. The floor costs 2.85 m over 6.1 km, which is the argument for it in one number.
  Cross-checked against an independent implementation outside the codebase.
- **`kiawah_cycle.gpx` ENDS STOPPED** — 6.0 m in its last 60 s — so the 30 s average decays to
  0.34 m/s and the ETA is correctly withdrawn. That is now a test rather than a loose bound:
  a bar reading "arriving in 4 minutes" at a rider who is off the bike is exactly what
  `min_eta_speed_mps` exists to prevent.
- **THE TRIP IS FED FROM TWO PLACES, and the timestamp is what makes that safe.** A live
  receiver's fixes are counted at `pushFix:`; `MovingMapTick` cannot serve them because `Tick`
  DRAINS ITS QUEUE IN A LOOP and reports only the last fix it consumed, so a delayed frame that
  swallowed two would chord straight across the first. A scripted replay never touches
  `pushFix:` (it is polled inside the tick), so the tick counts that one. Live fixes reach both
  and are deduplicated by their own stamp. Found by the review pass, not by the acceptance run —
  the anchor hides it well enough that the bar looks right either way.
- Measured on the simulator, replaying the ride: **2:00 · 12 km/h · 1.7 km · 2.5 km · 3:19 PM**,
  and 1.7 + 2.5 is the 4.2 km route. `PPRoute` gained nothing; the line the "distance to go" is
  measured along comes from a new `RouteStore::RoutePath()` (road geometry, not the waypoints —
  on Kiawah those are three points and a 6 km ride between them).

### P9 — The points — **BUILT 2026-08-20** (the compass followed the same day)

Chris asked for the POINTS half on its own and said the compass could wait, so this section is
now two: what was built, and what P9 still owes.

**The plan said "point add/edit UI is explicitly OUT". It is IN, because Chris asked for it**,
and the sentence that made it out of scope — "that is where A4's editor machinery would enter"
— turned out not to apply. A4's `OverlayEditor` is a desktop mode object: a tool palette, a
current-tool, a `KeyEvent` stream and a mode that follows the current overlay. A phone has none
of those. What a phone has is a TAP and a SHEET, and the two of them together are the editor —
so nothing from A4 was needed and no mode machinery was built. The ledger's standing item ("the
point overlay has no EDITOR, so a `.fvpoints` document is read-mostly in the app") is closed for
Pippin and still open for PythonView, which is the shell that would want A4's shape.

**Schema 3: `phone` and `url`.** Two TEXT columns on `points`, unvalidated and carried verbatim
— a document is authored by `sqlite3` as often as by an app, and a reader that rejected
"(843) 555 0100" would be enforcing a format nobody agreed to. Whatever DIALS and whatever OPENS
is the shell's question (`PPMapPoint.dialURL` / `.webURL`), and it is the shell that decides not
to offer a link for text that cannot be one. The reader probes for each post-schema-1 column and
SELECTs a literal default for a missing one, so schema 1, 2 and 3 all open through one query and
a document interrupted between two `ALTER`s opens too.

**The symbol-DPI gap is closed for this overlay.** `PointOverlay::SetSymbolDpiScale` — the same
call `RouteOverlay` has had since P5 — and `PPMap` sets it per tick to `pixelsPerPoint`, the P12
number. A `size_px` is therefore an AUTHORED pixel, a 22-px marker is 22 points wide on any
screen, and the CULL BOX, the LABEL's type size and the HIT TEST all scale with it: a target a
finger can see is only useful if it is a target a finger can press. 1.0 is the default and the
identity, so no golden moved.

**`port/apps/Pippin/PippinKit/PPPointStore.{h,cpp}`** is `PPRouteStore` for points and is the
mac-testable half. Three rules, all in its header: the SEED (the pack's `kiawah.fvpoints` is
copied into `Documents/` on the first launch and never read again — two sources of truth for one
map would let an app update revert somebody's edits); EVERY EDIT WRITES, ordered with the
mutation because it is the same call; and A FAILED WRITE IS NOT A REFUSED EDIT — the point is on
screen and correct, and what is lost is the next launch. One place it deliberately differs from
the route: **an empty set is WRITTEN rather than deleted**, because an absent document means
"seed me" and a user who deleted every point would be handed all of them back.

**The hit test answers the NEAREST with no ambiguity question**, which is a phone decision and
not a disagreement with A5: the desktop pick session can ask "which of these did you mean?"
because it has a mouse, a context menu and a status bar, and a rider with one thumb gets the
closest one and taps again. `PointOverlay::HitTestPoint`'s full aggregation is untouched.

**The shell.** A points button beside Route (`mappin.slash` / `mappin.and.ellipse` — the pair is
named outright, because `CircleButton`'s `.fill` convention is a GUESS and this symbol has no
filled twin; the button went blank on the first simulator run, which is now `activeSystemName`'s
reason for existing). **Adding is that button HELD DOWN** (Chris, 2026-08-20 — it was a small `+`
beside it for one day): a hard press is the same gesture on this hardware, so one
`LongPressGesture` serves both, it is armed only while the points are shown, and the tap that
follows a hold is stepped over by a timestamp the gesture leaves behind. A TAP recognizer that
must fail in favour of the pan. `PointInfoSheet` — name, phone, URL, remarks,
each row absent rather than greyed when empty, and the two live ones are `Link`s so the system's
own handler puts up the phone app and Safari, and the long-press menu comes free.
`PointEditSheet` — name, phone, URL, remarks, a six-shape picker drawn as the shapes, an
eight-colour palette, a Location row and a Delete behind the only confirmation in this app.
`PickOverlay` is reused verbatim: P6's "one mechanism serves start, end and via" now serves a
point as well.

**And the Location row IS the route sheet's stop row** (`LocationRow`, 2026-08-20, at Chris's
request): **Current location | Pick on map**, offered-and-disabled when there is no fix, in one
view used by both flows. A new point therefore opens the FORM and takes its place from inside it
— Save is held back by `isSaveable` until it has one, which is what the route's OK already does
with an unset stop — rather than being marched out to the crosshair first. The point of sharing
it is P11 below: a coordinate is spelled for a user in exactly two functions
(`LocationText.describe` and `PickOverlay.readout`), and both are shared, so the street-name
change lands on the route and the points together. **It did, the same day** — one edit to each
function, and both flows name the road.

### P9's COMPASS half — **BUILT 2026-08-20** (and the lag with it)

Chris asked for three things in one sentence: the compass toggles north-up and course-up; in
course-up the map centres and orients CONTINUOUSLY, without the lag it has today; and two
fingers rotate the map when it is not following. All three built.

**The lag had a name and a number, and it was not the slew.** MM2's track-up apron is a
`W/5 x 2H/5` box with the ship anchored at its lower edge, so a rider heading up the screen
crosses **a third of a screen — about a minute at bicycle speed on a riding scale** — before
anything happens. And because `MovingMapCamera::Update` returns early while the ship is inside
the apron, the ROTATION does not change either: a corner taken mid-apron does not turn the
chart at all. That apron is right for a chart being read and wrong for one being ridden, so
course-up switches it off (`CameraModes::continuous`) and **north-up keeps it untouched**,
which is the half Chris asked to keep.

**Continuous centring costs what MM3's header says it costs unless the slew is retuned with
it**, so the follow slew is `kLinear` and lasts **one measured fix interval**. Shorter and the
map lunges then stands still (a stutter once a second); longer and it is still travelling
toward fix N when N+1 retargets it, which is the lag wearing a smaller hat; equal and the chart
slides at the rider's own speed. The interval is MEASURED because no constant serves both a
phone at 1 Hz and the demo feed at `demo_time_scale` — `PPFollowCadence.h` is that measurement
(a running average over the ticks that SAW a fix, which is the honest quantity because `Tick`
drains its queue in a loop; a gap resets rather than averages), and it is twelve gtests on the
mac, because a stutter and a trail are both invisible in a still.

**The one line nobody will re-derive** is in `applyRotationPin:`. North-up follow must pin the
chart at north, because MM2 PRESERVES the current turn when `auto_rotate` is off — a rider
leaving course-up mid-ride would otherwise have the next fix retarget the slew at the angle the
unwind was passing through, freezing the chart half-turned and re-aiming it there forever.
`SetRotationSupported(false)` takes the rotation off the camera, but it ALSO does
`slew_.Reset(center, 0)`, because it is written for a shell that could never rotate and reads
"unsupported" as "already at north". This shell can rotate and the chart is still turned, so
the slew ALONE is told where the map really is (`slew().Reset`, never `ResetMap`, which would
put the angle back into `map_rotation_deg_`). Without that line the chart SNAPS to north on the
next fix instead of unwinding to it. The price, stated at the site: with rotation unsupported
the overlay draws the ship at its true bearing, so for the second the unwind takes the chevron
is off by whatever turn is left.

**The compass is on screen only when it has something to say** — following, or the chart is
turned — and its needle points at NORTH rather than at the heading, which is what lets one
control serve both modes. A tap means the obvious thing in each: following, it is the
auto-rotate switch; not following, it is the way back from a twist, and it leaves the mode
alone, because a twist is not a mode. `northScreenAngleDegrees` is the chart's turn and NOT its
negative — `rotationDegrees` turns the CONTENT clockwise and carries north with it — which was
wrong in the first cut and caught in the simulator by looking at which side the ocean was on.

**Two-finger rotate** is `PPViewport.rotated(by:about:)` (the pinch's shape exactly: remember
what is under the fingers, turn, pan by however far it moved) plus a `UIRotationGestureRecognizer`
with a **twelve-degree dead zone**, which is what preserves P3's reason for leaving it out —
only what is PAST the wall is applied, so it neither turns on an accidental twist nor jumps by
twelve degrees when it arms. Refused in GPS mode, gated in `MapModel.rotate` rather than in the
recognizer. The slew's re-base learned the rotation in the same session (`kRotationEpsilonDeg`,
compared the short way round), because a twist moves the chart behind the slew's back exactly
as a drag moves its centre.

Pack keys added: `movingmap.course_up`, `slew_s`, `follow_slew_s`, `follow_slew_min_s`,
`follow_slew_max_s`.

- **The snap-points half of the original P9 is still open**: the route sheet's rows gaining a **Point
  name** picker, so a chosen point snaps the waypoint exactly (the requirement's own example).
  The document is now in the app and `MapModel.points` is already published, so this is a picker
  and a coordinate — a sitting's worth of Swift with no C++ under it.
- **The embedded ARTWORK went in the same day** (Chris asked whether `symbol_id` could point at
  the maki set). It could not, and that IS the design: `symbol_id` is a foreign key into the
  document's OWN `symbols` table, not a path into an icon directory — schema 2 put the artwork
  IN the file so it opens with its symbology anywhere. So `stage_data.py` now embeds 43 maki
  PNGs (CC0, from `testdata/GeoSymbol/makiPng`), nine of the ten shipped points wear one, and
  the editor gained a picker over the palette. The earlier note here — that a picker would mean
  "rasterising a symbol through `CpuCanvas` into a `UIImage` per cell" — was WRONG: a document
  stores PNG bytes and `UIImage(data:)` takes those directly, so nothing is rasterised and
  UIKit caches the decode.
- What is still not there: **any way to get NEW artwork into a document from the app.**
  `AddSymbolFromPngFile` is bound and only the staging script and `WriteSampleFile` call it, so
  the palette a document ships with is the palette it has.
- **One number now controls two things**, and it is worth knowing before tuning it: a marker's
  `size_px` also sizes its icon, at `kIconFractionOfBadge` = 0.62. 12 gives a 7.4-point glyph,
  which maki's 32-px artwork does not resolve to; 26 gives 16 points, about what maki is drawn
  at on a web map. `stage_data.py`'s `MARKER_SIZE_PX` carries the table.

### P10 — Record and share — **BUILT 2026-08-21**
Everything the plan asked for, plus a share button on a single POINT that Chris asked for in
the same sentence ("so I can send it to the apple maps app or to another user").

**The writer is in `fvkit/nav/gpx.h` beside the reader**, GPX 1.1 only, and its four rules are
in the header. The one worth repeating here is rule 1, because a round-trip test alone does not
catch it: **a field with no validity is not written**. `has_altitude` false means no `<ele>`
element, never `<ele>0</ele>` — a zero written for an absent field reads back as a valid
sea-level fix, and the reader's own "a missing `<time>` yields `has_time` false" only survives a
round trip if the writer honours it. Speed is deliberately not written at all: base GPX has
nowhere to put it, and a Garmin extension carrying a number the reader DERIVES would be a second
source of truth for a quantity that already has one. Tests are the round trip the plan asked for
(`kiawah_cycle.gpx`'s 1705 points, out and back, equal to 1e-7 degrees and 0.05 m) plus the
mechanism ones a round trip cannot see.

**`fvkit/nav/gpx_recorder.h` is the recording half, and it APPENDS.** A writer that keeps the
ride in RAM and saves on Stop loses everything to the one failure a recorder must not have: the
app killed in a jersey pocket at mile 30. So the recorder remembers where the FOOTER starts,
seeks back to it for the next point, writes the point and writes the footer again — the footer's
length never changes, nothing is truncated, and **the file on disk is a complete, valid GPX after
every single fix**. A test reads the file back mid-ride, without closing the recorder, after each
of six fixes. Cost is one seek and one flush per fix, which at 1 Hz is nothing.

**In the app it landed in one line, in `PPMap`'s `consumeRawFix:`** — P8's `feedTrip:`, renamed,
because it was already exactly the right seam: the one place both feeds become one stream, the
RAW fix rather than the snapped one, and a timestamp gate that makes a live fix arriving twice
(once at `pushFix:`, once through the tick that consumed it) one point in the file instead of
two.

Shell: a **record button** above the GPS button, red while writing, whose HELD press opens a
**Rides sheet** — start, stop, share, delete, with the live point count — following the points
button's own idiom (the tap is the common thing, the hold is the occasional one), and the notice
posted when a ride ends is where the hold is taught. Recording deliberately does NOT require GPS
mode: the two answer different questions (*follow me* and *keep this*), and a rider looking at
the whole island while the phone logs the ride is not doing anything strange.

**The point share is `PPMap.writePoint(_:toGpxURL:)` — a CLASS method, since it reads nothing
from the map — plus two rows in the info sheet.** "Open in Maps" is a `maps:` URL and not a share
at all; "Share" is a sheet carrying TWO items, a `.gpx` named after the point and an
`https://maps.apple.com` link, because a file is for somebody with a mapping app and a link is
for somebody with a phone. `.fvpoints` stores elevation in FEET and GPX's `<ele>` is metres; the
conversion is at the format edge. The remarks become the waypoint's `<desc>`, which is the one
place this session touched the reader — `GpxDocument::waypoint_descriptions`, because a point
shared without whatever the user wrote about it is a pin the recipient has to guess at.

**One bug fell out of testing it and it was P9's.** `CircleButton`'s tap-versus-hold guard was a
timestamp honoured for one second, so **a hold lasting longer than a second fell through to the
tap**. Nobody noticed while the fall-through merely toggled the points; the record button made it
*stop the ride the user was holding the button to look at*. The flag is now cleared when a press
BEGINS (a zero-distance `DragGesture` is how a touch-down is observed at all), which closes both
that and the stale-flag leak the timestamp existed to avoid.

### P11 — The pick button says WHERE, not what the numbers are — **BUILT 2026-08-20**
Today the picker's confirm button reads `Use 32.60841, -80.07213`, and the two sheets' rows read
`32.60841, -80.07213`. **Both spellings are now single functions shared by the route and the
points** — `PickOverlay.readout` and `LocationText.describe`, both in `RouteSheet.swift` — which
is Chris's own requirement for this session: the change must land in the route UI and the point
UI at once.
A lat/lon is the one thing a rider on a bike cannot check. Replace it with the name of the
nearest thing they could actually ride on: **"Use Flyaway Drive"**, **"Use cycleway"**,
or — when nothing routable is in range — **"No usable path"**, which is a LABEL AND NOT A
BLOCK: the button stays enabled and the point is still taken. A rider aiming at a beach or a
car park means it.

Nothing new has to be built underneath. `RouteStore::EnsureRoadNetwork()` already hands out a
`RoadGraphNetwork` over the same `kiawah.fvroad` the router plans on ([PPRouteStore.h:180](port/apps/Pippin/PippinKit/PPRouteStore.h:180)),
already at `RoadSnapFilter::kAll` so footways and cycleways are in the index, and
`QueryNear(p, radius, &out)` already returns `RoadCandidate{name, distance_m, ...}` sorted on
nothing — the caller picks the nearest. This session is a query, a fallback ladder and a label.

- **`PPRouteStore` gains one call**, headless and testable on mac:
  `std::string DescribePoint(GeoPoint p, double max_m, const std::string& profile)`.
  It lives beside the network rather than in Swift because the answer depends on the ROUTING
  RULES, and those are C++.
- **"Usable" must mean what the router means, not what the index holds.** The filter is `kAll`,
  so an unfiltered nearest-candidate will happily name a footpath the cycling profile will
  refuse, and the button would promise a road the very next replan cannot use. Test each
  candidate against the profile actually selected in the sheet — its class weight and its
  access bits (`kArcNoBicycle` / `kArcPrivateBicycle` and the foot pair) — and only then let it
  name the button. A `private` arc still counts as usable: `RouteOptions::private_penalty`
  prices it, it does not delete it, and the whole point of that decision (O5b) was that someone
  routing to a house behind a gate has to be allowed down the drive.
- **The fallback ladder**, in order: (1) nearest usable arc with a name → `"Use Flyaway Drive"`;
  (2) nearest usable arc with no name → its class, via `RoadClassName()` prettified —
  `"Use cycleway"`, `"Use footway"`, `"Use track"`, and `living_street` → `"living street"`
  (the raw tag values are underscored and must not reach a button); (3) nothing usable inside
  `max_m` → `"No usable path"`, button still enabled.
- **State the radius as data, not a constant in Swift.** ~50 m is the honest number for a
  crosshair the rider aimed by eye — wider than the snapper's `max_radius_m` (60 m is a GPS
  error budget; this is a thumb) but it wants its own key in `pippin.ini` alongside the other
  routing settings, because the right value differs between an island of cul-de-sacs and a city.
- **Two roads at once is the case worth deciding now**: at a junction, or on a road with a
  cycleway beside it, the two nearest candidates are metres apart and the nearest one flickers
  as the map drifts a pixel. Prefer the candidate that is nearest, but keep the previously
  named one while it stays within a small margin (the snapper's own `stay_bonus_m` idea, ~10 m)
  so the button does not blink between "Flyaway Drive" and "cycleway" while the rider is
  reading it.
- Tests (mac, no UI): over `kiawah.fvroad`, pin a point on a named residential street, a point
  on an unnamed path, a point offshore (→ "No usable path"), and a point equidistant between a
  road and its parallel cycleway. The last one is the hysteresis test and needs two calls.
- Acceptance: on the simulator, dragging the map under the crosshair rolls the button through
  Kiawah's street names and reads "No usable path" over the ocean, and a point confirmed while
  the button said "No usable path" still lands in the route sheet.


**What P11 actually came out as.** The plan above is what was built, with three
decisions the plan left to the session:

- **`DescribePoint` returns a STRUCT, not the plan's `std::string`.** The two
  places that spell a coordinate need two different sentences out of one
  answer — the button reads "Use Flyaway Drive" and the row reads "Flyaway
  Drive" — and a C++ function returning either would have left the other one
  slicing an English string apart. `pippin::PlaceDescription` carries the fact
  (`usable`, `name`, `named`, `distance_m`, `arc`); `LocationText.describe` and
  `LocationText.useButton` carry the wording, beside every other word in the
  app.
- **"Usable" is the ROUTER'S OWN FUNCTION, not a second copy of its rules.**
  `Router::ArcUsable` became the free `fv::routing::ArcUsable` and the member
  now calls it, and the options it is asked with come from
  `RoutePlanner::BuildOptions` (public since this session) rather than being
  rebuilt — so the same rule file, the same profile lookup and the same poll
  answer the label and the replan. The case that proves it is on the fixture:
  standing ON Kiawah Island Parkway, which carries `bicycle=no` for its whole
  length, the button reads "cycleway" (the path 8 m away) for a rider and
  "Kiawah Island Parkway" for a car.
- **A ROW IS NOT A CROSSHAIR, so it does not remember and it does not say "No
  usable path".** `DescribePoint`'s `remember` argument is false for a sheet:
  a row naming a stop the user set ten minutes ago would otherwise hand the
  crosshair a head start from a coordinate somewhere else on the island. And a
  row with no road near it falls back to the NUMBERS rather than to the
  button's label — "No usable path" is the right thing to say about a place
  somebody is still choosing and says nothing at all about a place they have
  already chosen. A stop dropped in the Atlantic reads `32.58707, -80.07005`,
  which is at least a fact.

The radius and the margin are `pippin.ini`'s `routing.pick_radius_m` (50) and
`routing.pick_stay_bonus_m` (10). Ten mac gtests in
`port/apps/Pippin/test/route_store_test.cpp`, every coordinate in them read out
of `kiawah.fvroad` itself rather than picked off a map. Verified in the
simulator: the button rolled through "Use Treeduck Court", "Use cycleway" and
"Use track" as the map moved under the crosshair, read "No usable path" over
the Atlantic, and the stop confirmed while it said so still landed in the sheet.

### P12 — The units session: an authored pixel is a POINT — **BUILT 2026-08-19**
The two things Chris's first real-device run turned up, both of them the shell counting in the
wrong unit, both fixed in `PippinKit` with no core change and therefore no OSM golden moved.

**The crayon.** `PPMap::renderViewport` gave the three display calls the SAME number, which
was the point of the P2 comment above them — but the number was the SURFACE PIXEL's pitch, and
only the projection wants that one. `VectorRenderer::SetDeviceDpi(25.4 / mmPerPixel)` is 489
dpi on a 3x phone, and `OsmStyleEngine` scales every authored width by `dpi / 96` — 5.09x. The
zoom relation had the same unit error in the other direction: derived over the surface pixel, it
styled the map 1.58 levels further in than the view on screen. Measured before anything was
changed, one road at one scale on both shells (`port/apps/Pippin/README.md` carries the table):
a minor road at 1:25,000 drew **36 px = 1.87 mm** on the phone against the desktop shell's
**3 px = 0.75 mm**. Both halves now count in POINTS — `viewport.mmPerPoint` to the source and
the style, `96 * pixelsPerPoint` to the renderer — which is the unit P4 already chose for
symbols, the unit `PPViewport`'s zoom limits were already written in, and the unit MapLibre
itself renders a GL style in on a phone. The same road now draws 11 px = 0.57 mm at z15.05.

**The bands.** `-[PPViewport viewportAtHomeView]` fitted the pack: `home_bounds` is a landscape
box, a phone is portrait, so the app opened with a third of a screen of map between two bands of
style background. It now COVERS — `homeScaleFilling:YES`, `min` where contain takes `max` —
and the island's long axis runs off the sides. The zoom-OUT limit deliberately stays on the
CONTAIN fit: "two levels past the view that fits the pack" is a statement about how much ground
exists, and the opening view moving in does not make the island smaller.

P3's viewport probe asked the old question ("home view fits the pack") and passed all the while
the phone was showing those bands, so it now asks the new one and checks BOTH axes.

### P13 — Five small things, from riding it — **BUILT 2026-08-19**
No new machinery: five changes Chris asked for after the first rides, all of them about what
the phone looks like. Each one is a line or two, and the reasons are the interesting half.

- **Two more zoom levels in.** `kOverzoomLevels` 3 → 5, so the near limit is 1:1,614 (~100 m
  across a phone) instead of 1:6,456. Three levels was the scale a rider READS at; five is the
  scale a rider standing at a junction INSPECTS at. The source still clamps the read zoom at
  the pyramid's maxzoom, so the cost is a coarser-looking map and not a byte of extra I/O —
  measured at the limit: `z14 · 23 features · 12 draws · 104 ms · 1:1,614`.
- **The route line doubled**, 3 px over a 2 px casing → 6 over 4 (`fv_route_overlay.cpp`), and
  the uncalculated great-circle legs 2 → 4. The widths are DEVICE pixels, which is the whole
  finding: 3 px is 3 points on the desktop shell the number was chosen on and ONE point on a
  3x phone. Same class of unit error as P12's, one layer up.
- **The ride bar at 26 pt, in two clusters** — upper LEFT what has happened (elapsed, speed,
  odometer), upper RIGHT what is left (distance to go, arrival), the right one appearing only
  with a route. Five readouts at that size do not fit across a phone in a row; stacking them in
  two corners also decouples the two widths, so a growing number moves nothing but itself.
- **An app icon**: Chris's ruddy turnstone, in `port/apps/Pippin/art/` with `make_icon.py`,
  which squares the artwork's own rounded corners and drops the alpha because iOS masks the
  icon itself and an icon that arrives pre-rounded is masked twice.
- **The new `style.json` committed** (marsh, park, boardwalk-with-casing, landuse reordered).
  `stage_data.py` already copies that exact file into the pack, so committing it IS shipping it;
  it loads clean at 69 layers and the boardwalks show on the phone at 1:12,196.

### P14 — A place shared IN, from Apple Maps or Google Maps — **BUILT 2026-08-21**
Chris, 2026-08-21: "allow a point shared out of apple maps or (google maps etc...) to [be] shared
to pippin ... added to the point overlay with some generic symbol", conditional on the share
carrying a lat/lon, and with the address in the remarks if it has one. **P10 was the way out;
this is the way in.**

**THE CONDITION IS MET, WITH ONE WRINKLE WORTH THE WHOLE SECTION.** An Apple Maps share on iOS 18
— and on the iOS 26 SIMULATOR — is a URL with the coordinate written into it
(`…/place?address=…&coordinate=37.334859,-122.009040&name=Apple%20Park`): offline, exact, and
carrying the postal address as well. **But an iOS 26 share from a real device is often just
`https://maps.apple/p/U8rE9v8n8iVZjr`, with nothing in it at all**, and every Google Maps share is
the same shape (`https://maps.app.goo.gl/…`). The coordinate is still recoverable from those —
the short link redirects THROUGH a full URL that has it before landing on a page that does not —
so `PlaceLink.resolve` follows the chain and reads the hops as they go past. That is a network
round trip and an undocumented shape, and both facts are written where they will be found:
`PlaceLink.allowsNetworkLookup` is one constant that turns it off, after which a short link is an
honest "that link doesn't carry a position" and nothing else changes.

**A SHARE SHEET LISTS EXTENSIONS AND NOTHING ELSE**, which is why this session added a second
target (`PippinShare`, `org.peregrine.Pippin.Share`) rather than a plist key. No URL scheme, no
document type and no entitlement will put an app in Maps' share sheet — only an app extension
will. **The extension is deliberately thin**: it gets a string out of an `NSItemProvider` and
opens a URL, and everything about what a place IS lives in `PlaceLink.swift`, which is compiled
into both targets. An extension is the worst place in the system to debug logic — tens of
megabytes, no console worth the name, and a host that kills it when it likes.

**THE HANDOFF IS A URL AND NOT AN APP GROUP.** `pippin://place?lat=…&lon=…&name=…&address=…`,
registered in the app's `Info.plist`, opened by the extension and read by `MapScreen.onOpenURL`.
The App Group a bigger app would use costs an entitlement on both targets and a group id
registered with Apple before a device build will sign; a place is four short fields and fits in a
URL. What it costs instead is the `open` dance in `ShareViewController.hand(over:)`, and that
dance turned out to be the whole of the session's second half.

**`NSExtensionContext.open` DOES NOT OPEN THE APP FROM A SHARE EXTENSION.** It calls back `false`
and nothing happens — on a device and in the simulator alike — exactly as its documentation has
always implied ("only implemented in the Today extension point"). This shipped believing
otherwise, and it took Chris's phone to find out: *"the screen briefly toggles when i select
pippin ... but no point shows up"*, while the same gesture worked in the simulator. It worked
there because the responder-chain fallback beside it was doing the work, and this file's own
comment then dismissed that fallback as dead code. **It is not dead code; it is the mechanism.**

Two things about it are load-bearing and each was broken once while getting here. It must send
the **modern three-argument `open`** — a version sending the deprecated one-argument `openURL:`
through `perform` logged "sent it" and did nothing, because something else in an extension's
responder chain answers to that selector and swallows it. And the **cast to `UIApplication`** is
what tells the application apart from whatever else claims to know the word; a share extension
does have one in its process, it simply has no `UIApplication.shared`. That is also why the
extension is NOT built `APPLICATION_EXTENSION_API_ONLY`: the flag forbids naming the type, and
the type is the mechanism.

**And the outcome is OBSERVED rather than believed.** Neither call answers "did Pippin come
forward", so `NSExtensionHostWillResignActive` does — the one signal in that process that means
another app took the screen. Without it inside two and a half seconds the card says so. The first
version dismissed the sheet on `open`'s cheerful failure, and what the user got was a share sheet
that flickered shut having done nothing: the worst failure a feature can have, because it is
indistinguishable from a feature that was never built.

**THERE IS A LOG NOW, IN BOTH PROCESSES**, because none of the above was visible from either
side: subsystem `org.peregrine.Pippin`, category `share`, at `.notice` so it persists and with
`privacy: .public` so the URL can be read. It is what found all of it, and it prints the whole
chain — what arrived, what parsed, what the redirects gave up, what `open` claimed, and the point
id the document assigned.

**One more thing this session fixed, which was a data-loss bug and not a share bug.** A share can
arrive with `onOpenURL` racing the `onAppear` that loads the points, and `PointStore::AddPoint`
persists immediately — so an add against an unloaded overlay would WRITE a one-point document
over the user's own, and the load that followed would read that back as the whole set. On a first
launch the pack would never be seeded either. `acceptSharedPlace` now calls the idempotent
`loadPoints()` first; the render queue is serial, so that is the entire fix.

**And two copies of the app break the handoff**, which is worth knowing before anybody debugs
this again: an older install under a different bundle id (`org.peregrine.Pippin.<team-id>`, from
an Xcode install that uniqued the identifier) means two apps claim the `pippin` scheme and which
one iOS launches is undefined — plus two rows called Pippin in every share sheet.

**`PlaceLink` IS FOUNDATION-ONLY SO IT IS TESTED ON THE MAC** — `test/place_link_test.swift`, 56
checks over real share URLs, wired into ctest (`ctest -R place_link`). The single most important
case in it: **a Google `/maps/place/…` URL carries TWO coordinate pairs and they are different
places.** `@32.6051,-80.0777,17z` is where the CAMERA was and `!3d32.60841!4d-80.07213` inside the
`data` blob is the PLACE; reading them in the wrong order is a marker a screenful from where the
user pointed. The parser also takes `geo:` URIs, vCard `GEO`, OSM `mlat`/`mlon`, and a pasted
"32.60841, -80.07213" — and REFUSES `q=0,0`, which is Apple's own spelling of "I have no
coordinate, search for the words" and would otherwise drop a marker in the Gulf of Guinea.

**In the app it is one `onOpenURL` and one model method.** `MapModel.acceptSharedPlace` adds the
point on the render queue, and `mutatePoints` grew a `then:` so the info sheet opens only after
the new selection has been published. The marker is the app's OWN default — the same circle a
hand-added point gets, at the size the set already uses — because a special "imported" look would
mark a shared point as second class forever; where it came from is kept in the `category` column
instead. **The same place shared twice is one point** (15 m, about the width of the building
somebody is pointing at), the address becomes the remarks, and a place OUTSIDE the pack is saved
and said so rather than refused — Kiawah is one pack of many and a rider's hotel in Charleston is
not a mistake. In GPS mode the map is not recentred: a rider following their own position does
not want the screen to leave them because a friend sent a restaurant.

**Tested end to end from a COLD start** (Pippin not running), on the simulator and then on
Chris's iPhone 14 Pro on iOS 26.6: Apple Maps → Share → **Pippin** → the app comes forward with
the point on the overlay, the name in the title and the address in the remarks — by way of a
`maps.apple/p/…` short link resolved through its own redirects, which is the iOS 26 device case
this whole section is about.

### P15 — Auto-Lock: the screen stays up while the phone has a job — **BUILT 2026-08-25, not yet ridden**
Chris, 2026-08-25, off a real ride: "the iphone goes to a lock screen when trying to use the gps."
It did, and the app had never said otherwise — there was no `isIdleTimerDisabled` anywhere in the
tree.

**THE LOCK SCREEN IS DATA LOSS HERE, NOT AN ANNOYANCE**, and that is the whole reason this is a
bug rather than a preference. Pippin holds `NSLocationWhenInUseUsageDescription` and declares no
`UIBackgroundModes`, so when Auto-Lock fires the scene leaves the foreground, iOS suspends the
process, and CoreLocation stops delivering: the moving map freezes at wherever the rider was when
the screen went dark, and a recording in flight simply gets a hole in it with nothing in the file
saying so.

The fix is `updateIdleTimer()` in `MapModel`, setting
`UIApplication.shared.isIdleTimerDisabled = gpsMode || isRecording`. **It hangs off `didSet` on
both flags rather than off the four places that set them**, so a mode added later cannot forget to
call it.

**TWO MODES AND NOT ONE**, for the same reason `startRecording` does not require GPS mode: they
are different jobs. Following needs the screen because the rider is LOOKING at it; recording needs
it because the fixes have to keep arriving whether anybody is looking or not.

**WHAT IT DOES NOT DO, and it was checked case by case rather than assumed.** The flag suppresses
the IDLE countdown only — a deliberate side-button press still locks the phone, and no app can
override that. And the flag binds only while Pippin is frontmost: iOS consults the foreground
app's setting, so the moment Pippin backgrounds its flag stops applying and whatever is in front
uses its own. It therefore cannot hold the screen on for another app and cannot outlive Pippin's
time in front, which is the battery question Chris asked next and the answer is zero in every
backgrounded case — a suspended process draws nothing, and the GPS hardware is released with it.

**So a ride recorded with the screen deliberately switched off is still gapped**, and always will
be until background location is a conversation somebody wants to have. That is listed under "What
is deliberately NOT in v1" already and this session did not move it.

### P16 — Do not render for a ship nobody can see — **BUILT 2026-08-25**
The first of three battery items Chris raised on 2026-08-25 after P15's five-case walkthrough.
Do this one FIRST and on its own: it is the clean one, and it makes P17 easier to judge in
isolation.

**EVERY FIX COSTS A FULL RENDER TODAY, whether or not the fix changes a pixel.**
`MapModel.receive(_ fix:)` calls `setContentDirty()` unconditionally, which wakes the display link
and re-renders — and there is no raster cache under it (`PPMap.mm` opens the MBTiles and
rasterizes vector tiles per frame), so that is a real decode-and-draw for a symbol that is not in
the viewport.

The gate: **when not following, not recording, and the ownship is outside the viewport, skip the
render.** Two details decide whether it works.

- **The test still runs on every fix.** Projecting one point is free next to a render, and it is
  what notices the ownship coming back INTO view and wakes the loop. Skipping the test as well as
  the render is the way to make a ship that never reappears.
- **Test against the viewport grown by the symbol's radius**, not the viewport. A bare rectangle
  pops the symbol in a fix late at the edge.

**NOTHING A NORMAL USER SEES FREEZES, and that was checked rather than hoped.** The trip readout
lives in `rideBar`, gated on `model.gpsMode` (`MapScreen.swift`), which this case excludes by
definition; the only other consumers of `ownship`/`status` are in `statsOverlay`, behind the
`PPShowStats` launch argument. So the frame is allowed to stand still.

**IT IS MEASURABLE BEFORE AND AFTER FOR FREE** — `PPFrame.renderMilliseconds` is already recorded
per frame, so renders-per-minute while idle is the number that settles whether this was worth it.

**KNOW THE CEILING.** This optimises one state: app foreground, not following, not recording,
ownship off-screen, screen still on. Auto-Lock and the rider's own attention bound it — it is the
"browsing the map, planning a route" state and not an hours-long leak. The hours-long cases are
already zero (P15).

**BUILT, and the plan above is what was built.** `RenderGate.swift` is the decision,
`MapModel.renderIsWorthIt(for:)` the two lines of plumbing that give it a projected point, and
`PPMap.ownshipSymbolRadiusInPoints` the margin. `receive(_ fix:)` still forwards EVERY fix
unconditionally — the trip computer, the recorder and the heading resolver all live below that
line and none of them may miss one — and only `setContentDirty()` is gated.

**THE GATE IS A VALUE IN ITS OWN FILE BECAUSE OF ITS ONE BIT OF STATE**, and that bit is a second
detail the plan did not have. A gate that only asks "is the ship visible NOW" leaves **the
chevron pinned to the edge**: the last fix before the ship goes is drawn, and if the next is
simply skipped, the stale symbol sits half on the screen for as long as the rider keeps riding
away from it. So the departure gets one frame too — `visible || wasVisible` — which draws the
ship at the position that puts it off the canvas and clears it honestly. The same bit is what
makes every "cannot answer" case (no fix position, no surface, a non-finite projection) resolve
to *render, and assume visible*: an optimisation that guesses when it does not know is how a
feature stops working in the one case nobody tested.

**IT IS TESTED OFF THE PHONE**, because both of its real failure modes — a ship that never wakes
the loop again, and that pinned chevron — are invisible in a still. `RenderGate` is
CoreGraphics-only for the same reason P14's `PlaceLink` is Foundation-only, and
`test/render_gate_test.swift` runs 40 checks under `ctest -R render_gate` through the same
three-line swiftc harness.

**THE MARGIN IS THE SYMBOL'S REACH AND NOT ITS HALF-WIDTH.** `fv.north` is drawn at twice its
shape box along its own axis (`builtin.cpp`), so a chevron asking for `movingmap.size_px` = 14 is
28 points long and reaches 14 points from its centre whichever way the rider is pointing. Half
the box would clip the nose. `PPMap` asks the overlay (`size_px()`) rather than re-reading the
setting, and no conversion happens on the way out because an authored pixel is a point on this
screen (P12).

**AND RECORDING IS GATED THOUGH THE FILE WOULD SURVIVE IT.** A live receiver's fixes reach the
recorder through `pushFix:`, which is not the render — but a DEMO replay's are polled inside the
tick, so a recording of one would lose points. Two feeds, one rule. (The demo feed is not gated
at all, and cannot be: its fixes never pass through `receive(_ fix:)`, and gating its timer on the
last known position would stop the replay advancing, which is the ship-that-never-returns bug
with no way out of it.)

**MEASURED ON THE SIMULATOR** with `-PPShowStats YES` and `simctl location start`, which is a
real receiver through `CLLocationManager` rather than the demo path. Riding west off the left
edge, the readout froze at 32.61000,−80.10738 — the first fix past the edge plus a symbol — and
stayed there while the fixes ran on to −80.20000, the chevron gone from the screen rather than
stuck to it. Riding back in, the readout resumed at the fix that re-entered. GPS mode over the
top of it still followed and still snapped (Kiawah Island Parkway), which is the `following`
branch doing what it says.

### P17 — A receiver nobody is reading — **BUILT 2026-08-25**
The second battery item, and the finding underneath it is bigger than the change Chris proposed.

**`stopFeed()` HAS NO CALL SITES.** `startFeed()` runs once from `MapScreen.onAppear`, and from
that moment until the app leaves the foreground `CLLocationManager` runs at
`kCLLocationAccuracyBestForNavigation` with `distanceFilter = kCLDistanceFilterNone` and
`pausesLocationUpdatesAutomatically = NO` — the most expensive configuration CoreLocation offers —
whether or not anybody is following, and whether or not anybody is recording. Pippin sitting open
on a table costs about what following costs, minus the screen.

**DROPPING `BestForNavigation` HELPS, BUT IT IS NOT THE LEVER.** Apple documents that level as
extremely high power and suggests it only while plugged in, so shedding it is a genuine saving —
but `desiredAccuracy` is a hint, and the dominant cost is that the GNSS receiver is powered at
all. Every tier from `Best` down through `NearestTenMeters` and `HundredMeters` keeps it running.
**The step change is at `Kilometer`/`ThreeKilometers`**, where iOS can answer from cell and wifi
and largely power the chip down. So the idle state should go coarse — `ThreeKilometers` plus a
large `distanceFilter` — rather than merely one notch down from best.

**WHY A FULL STOP WOULD ALMOST BE SAFE, which is the thing worth not re-deriving**: in this mode
the ownship is off-screen because the user PANNED AWAY, not because they walked away. The
transition that matters is therefore a CAMERA change, which the app already renders for and would
catch there. Coarse-rather-than-stopped only exists to cover the rarer case of physically
travelling back into the visible area, and `ThreeKilometers` is about the right resolution to
notice that.

**TWO HAZARDS THAT WOULD EAT THE WIN.**

- **Flapping.** An ownship parked near the viewport edge flips the mode on every fix, and each
  restart has a warm-up cost that can exceed leaving it alone. This needs TWO thresholds and not
  one — full accuracy on inside viewport+25%, dropped only outside viewport+100%.
- **Re-acquisition latency.** A GNSS warm start is seconds and a cold one can be thirty. So the
  first press of the GPS button after a coarse spell shows a stale or wandering ship unless
  `setGpsMode(true)` and `startRecording` restore full accuracy UP FRONT, before anything else.
  Those are the two places where latency is unacceptable.

**A FREE ADJACENT ONE**: `pausesLocationUpdatesAutomatically = NO` is justified in
`PPLocationSource.mm` by "a navigation app that quietly stops navigating" — true while navigating,
and bought for nothing when not following and not recording.

**BUILT, and the plan above is what was built.** `LocationPolicy.swift` is the decision,
`PPLocationAccuracyMode` on `PPLocationSource` is the pair of configurations it chooses between,
and `MapModel.reconsiderLocationAccuracy(for:)` is the plumbing. Both hazards are handled the way
the plan asked: the two thresholds are `LocationPolicy.returnFraction` (0.25) and `.leaveFraction`
(1.0), fractions of the viewport's own size on each edge so they mean the same thing at every
zoom, and `demandFullAccuracy()` is called by `setGpsMode(true)` and `startRecording()` before
either does anything else that can take time. The free adjacent one went with it: the automatic
pause is `NO` in the navigation configuration and `YES` in the coarse one.

**THE HOOK THAT IS NOT A FIX, and it is the piece the plan implied without naming.** The policy is
re-taken from TWO places, and the second matters more than the first: a fix arriving, and
**`viewport`'s `didSet`** — P11's one hook over every writer of the camera, now with a seventh
caller. It has to be there, because the plan's own reasoning says the ship went off screen by
being PANNED AWAY FROM: with the receiver at three kilometres and a hundred-metre filter, a phone
sitting still delivers nothing at all, so a policy that only ran on fixes would notice the map
being panned back over the ship approximately never. It costs one projection, which is why it can
hang off a gesture, and `shipPosition(for:)` is now the one projection shared by this and P16's
gate.

**THE COARSE FILTER IS A HUNDRED METRES, NOT THREE KILOMETRES**, and getting that wrong would have
been quiet. `distanceFilter` is how far the fix must move before it is delivered, and these fixes
exist to notice a rider travelling back towards what is on screen; at a riding zoom the viewport
is a few hundred metres across, so a filter the size of the accuracy tier would step straight over
the return threshold and land back inside the viewport with nothing in between. A hundred metres
still throws away the standing-still jitter, which is all it is there for.

**AND A PAUSED RUN DOES NOT COME BACK BECAUSE THE SETTINGS CHANGED.** The coarse configuration is
the only one that sets `pausesLocationUpdatesAutomatically`, and once CoreLocation has paused a
run, iOS resumes it when it next sees motion — no use at all to a rider who presses GPS while
stopped. So `PPLocationSource` records the pause (`locationManagerDidPauseLocationUpdates:`) and
the return to navigation restarts updates by hand. Everything else about writing `accuracyMode` is
live and cheap: CoreLocation applies `desiredAccuracy` and `distanceFilter` to a running manager,
and writing the mode it is already in does nothing, which is what makes it safe to drive at
gesture rate.

**THE STATE MACHINE IS TESTED OFF THE PHONE** (`test/location_policy_test.swift`, 44 checks under
`ctest -R location_policy`), and it has the strongest claim of the three Swift test tables: P17's
named hazard IS the state machine. The flap is a case — a ship walked across the return threshold
twenty times, asserting the mode never changes once — and so is every "cannot answer" case
resolving to full accuracy, for `RenderGate`'s reason. **One expectation in the first draft of that
table was wrong and is worth keeping**: `demandNavigation()` does not survive the next fix on its
own, and it must not. What holds the receiver sharp is `following || recording`, which is true a
tenth of a second later; the demand covers only the gap between the press and the flag. If the
press is REFUSED — `setGpsMode` bails on the authorization check — the next fix correctly puts the
receiver back where it was, because nobody is following anything.

**IT IS INVISIBLE, SO IT IS ON THE STATS LINE.** A tier change moves no pixel — that is the point
of it — so `MapModel.receiverIsCoarse` appends `· coarse` to the `-PPShowStats` readout, beside
the status rather than the ownship because it is a fact about the receiver and is worth reading at
a moment when no fix has arrived for minutes.

**MEASURED ON THE SIMULATOR** through a real `CLLocationManager` (`simctl location set`,
`-PPShowStats YES`), all four transitions: a fix on Kiawah reads no tier; the same phone set
27 km west reads `· coarse` (and P16's gate froze the ownship line at 32.61000,−80.10700, which
is the two sessions agreeing); the fix set back on the island clears it; and then, **without the
phone moving at all**, three pans west put the ship a screen off the edge and the readout went
`· coarse` again — the camera hook, which is the half of this that no fix could have done. Pressing
GPS cleared it instantly and GPS mode held it clear.

**WHAT IS LEFT ON THE DESK.** `stopFeed()` still has no call sites; the finding is answered by
coarse-rather-than-stopped, and the method is kept because it is also the demo feed's teardown and
the only honest spelling of "not now". And at a very deep zoom a three-kilometre fix landing
inside a small return band would restore full accuracy for nothing — the safe direction, costing a
warm start rather than a wrong position, and rare because the return band covers a tiny fraction
of that uncertainty disc.

### P18 — The bitmap cache — **BUILT (2026-08-25)**
The third item, and the only one of the three whose payoff is not mainly battery.

**WHAT WAS TRUE BEFORE IT.** `PPMap` opened the MBTiles and rasterized vector tiles on every
frame into ONE canvas, with the overlays composited into the same pass; `liveRenderBudgetMs` and
the settle-only fallback in `MapModel.tick()` existed to paper over how expensive that was during
a gesture. P4 and P7 both wrote the same sentence into the README and left it — "a frame costs its
whole vector render to move one chevron … the way out is an overlay pass over a cached base
image". That is what this is.

**IT WAS NOT REDUNDANT WITH P16.** P16 removed frames in the IDLE case, which is where a cache
would have helped least. The cache pays in the ACTIVE case — panning at display rate — and most
of all in FOLLOW, which neither P16 nor P17 touches.

#### The shape, and why it is neither of the two the plan expected

The plan offered tiles or a guard band, and warned that a band is invalid under rotation, so it
"never hits in the mode that would benefit most". **That warning was true of a band that has to
BLIT, and Pippin has never had to blit.** `MapScreen`'s preview transform (P3) has applied a
scale, a turn and an offset to the last frame since the app's third session, on the GPU, with the
offset asked of the projection so it has no drift. A guard band's blit was therefore already
written; what was missing was pixels for it to eat.

So P18 is **two layers and two of that same transform**:

* the **base map** into a surface LARGER than the screen — the band — kept between frames;
* the **overlay** (route, points, ownship) into its own surface exactly the size of the screen,
  cleared to TRANSPARENT, drawn every frame at the LIVE camera;
* `ZStack { base; overlay }`, each under its own preview transform, clipped to the screen.

There is no blitter anywhere in the app. Rotation and scale cost nothing, so **course-up is
served like any other mode** — the objection that ruled a band out is answered by moving the
composite rather than by making the band bigger. And the OVERLAY needs no band at all, because
it is redrawn every frame and so its own transform is only the milliseconds it took to draw.

#### The pieces

| Piece | What it is |
|---|---|
| `CpuCanvas::BlendPixel` | src-over onto a TRANSLUCENT destination. The old formula weights the source by its own alpha against a destination contributing nothing, which drags every antialiased edge toward the cleared colour — a dark fringe on every symbol and glyph on the overlay layer. The opaque case is its own branch and is byte-identical, which is what keeps every pinned golden in the tree still pinned. 5 tests. |
| `PPBaseCoverage.h` | The hit test, pure C++ and header-only like `PPCameraFit.h`: scale EXACTLY equal, turn within a quality cap the short way round, and the live surface's four corners mapped through both projections landing inside the band. 11 gtests. |
| `PPViewport.grown(byMargin:)` | The band. Deliberately NOT `resized(to:displayScale:)`, which recomputes the zoom limits for a new SCREEN and could clamp the very scale the cache is keyed on. |
| `PPFrame` | Two images and two viewports, plus `baseWasDrawn` and `baseMilliseconds`. |
| `MapModel.tick()` | Chooses the band per frame, and owns the settle. |

#### The four things nobody should re-derive

1. **THE LIVE-RENDER BUDGET HAD TO BE REWRITTEN OR THE WHOLE SESSION CANCELS ITSELF.** P3's rule
   is "if the last frame took longer than 40 ms, let the preview carry the gesture". That was a
   fair guess at the next frame's cost when every frame drew the whole map, and it is not one any
   more. Left alone it is fatal in a way that is invisible in the code: the first frame of a pan
   at a wide view costs 70 ms, the budget then refuses every frame for the rest of the gesture,
   **the band is therefore never built**, and the cache is never asked a question it could have
   answered. Measured on the simulator before it was fixed: **0 hits in 2 frames over a
   two-second drag**. The test now asks the two questions that decide the cost — would the cache
   serve this frame (then it is an overlay pass, whatever the last one cost), and is this the
   gesture's FIRST base draw (then it is the investment that builds the band, and it is allowed
   even at a wide view). Which is why `PPViewport` exposes `covers(_:maxTurnDegrees:)`: the shell
   has to be able to ask the cache's own question before deciding to start a frame.

2. **THE SETTLE IS WHAT MAKES THE CACHE FREE RATHER THAN MERELY CHEAP.** A composited base is
   resampled whenever the transform is not the identity, so a map left at rest under a cache hit
   is permanently, slightly soft — and a map somebody is reading is the one thing on this screen
   that must be exact. So the moment the loop would pause, it draws once more with the cache
   refused **and no band**: no band, because that makes the result provably exact (same size,
   zero offset, `MapScreen` marks it live and turns filtering off) rather than approximately so,
   and because it hands the band's memory back while nothing is moving. It fires only when what
   is on screen is actually resampled — a fix under a camera that has not moved is served under
   the IDENTITY transform and needs no settle, which is the most valuable hit the cache has and
   the reason `edge_inset_px` is not charged in that case.

3. **THE BAND IS A PER-FRAME DECISION, NOT A CONSTANT.** The plan's warning that "too generous a
   band makes the miss case worse than no band at all" is exactly right, and the answer is that a
   band is paid for on every miss and earns its keep only on hits — so it is not asked for where
   every frame is known in advance to miss. That is a PINCH (the scale is part of the cache key
   and has to be, so no cached base can serve a zoom) and the FIRST frame of a launch (nothing to
   serve from, and the cold-start view is the widest and most expensive one the app ever draws).
   The test is "has the scale moved since the last frame", not "is a pinch running", so a zoom
   the camera did by itself is covered too.

4. **CONTENT NEVER INVALIDATES THE BASE, AND THAT IS THE EDITOR'S DOOR.** A route replanning, a
   point being edited and the ownship moving are all overlay changes, and the overlay is redrawn
   every frame regardless. So a dragged route point costs one overlay pass — 8–11 ms measured —
   and not a vector render. That was Chris's reason for asking for the session and it is now
   true; what is left to do is bind the gesture.

#### Measured, iPhone 17 simulator, Debug app over a RelWithDebInfo core

| Case | Before | After |
|---|---|---|
| a two-second drag at 1:96,161 | 2 frames drawn, the rest carried by the preview | **23 frames, 20 of them cache hits at 15 ms**, and the pan renders live rather than previewed |
| the demo ride following, z12, 1:131,716 | every frame a full vector render | **2115 of 2373 frames served, 8 ms each**; base draws 25–33 ms |
| the cold-start view | 74 ms | 44 ms base + 11 ms overlay, no band (by rule 3) |

The hit rate in follow is ~89%, and the frames it does not serve are the ones where the band ran
out or the chart turned past `base_cache_max_turn_deg`.

#### What it costs

Memory, and it is the honest downside: at `base_cache_band_margin = 0.25` the base canvas is
2.25x the screen's pixels (28 MB at 1206x2622) and its CGImage is a second copy, plus a
screen-sized overlay canvas and its copy. The settle drops the band back to screen size while
nothing is moving, and a memory warning drops the cache outright
(`MapModel` -> `PPMap.invalidateBaseLayer`). Both keys are in `pippin.ini` under `[display]`, and
`base_cache_band_margin = 0` turns the band off without turning the cache off — a camera that has
not moved at all is still served, which is every fix that arrives while the map is still.

#### What is NOT in it

Tiles. The plan's other shape was never built, because the band answered the two objections that
made tiles look attractive (rotation, and the blit) at a much better constant factor. If a future
pack makes a miss too expensive to absorb, the band margin is the first knob and tiles are still
the second.

### P19 — The snap: a pick lands ON the thing it is aimed at — **BUILT 2026-08-25**

This is P9's open half, and it is NOT the picker the plan described. The plan said "the route
sheet's rows gaining a **Point name** picker … a picker and a coordinate, with no C++ under it".
Chris rejected that shape on the way in, and the reason is worth keeping because it is the whole
session: **a picker is a points feature; snapping is a property of the overlay interface.** The
requirement's own words are that if the pick location *overlaps an overlay feature*, the pick
takes that feature's exact position — which says nothing about points, and everything about what
any overlay ought to be able to answer.

He was also right that the interface was already there. `Overlay::AsSnapTo()` has been on the
base class since the app layer landed, `fv::app::SnapTo` is declared in `capabilities.h` with
`test_snap_to`/`do_snap_to`'s two-phase shape folded into one call, and
`PickSession::SnapToPoint` already walked the stack and ran FalconView's `snptodlg` flow
verbatim. **Nothing implemented it.** `PointOverlay` implemented four capabilities and not this
one, and `route_overlay_test.cpp` asserted `AsSnapTo() == nullptr` as a fact about the world.

#### What was actually missing, which was three small things and no new architecture

1. **Two implementers.** `PointOverlay::SnapToPoint` returns the point's surveyed coordinate;
   `RouteOverlay::SnapToPoint` returns a waypoint's. Both are written **over their own
   `HitTestPoint`** rather than beside it, so there is ONE reach rule and a snap and a hit can
   never disagree about what the finger is over — the dpi scale is the knob most likely to break
   that, so it is what the test pins. The route one exists because an SPI with one implementer
   has not been shown to be an SPI; it also buys a real thing, which is starting a route where
   the last one ended.
2. **`SnapToItem::distance_px`.** Every existing caller went through the chooser, and a chooser
   ranks nothing — it shows rows and a human picks. A shell with no dialog has to rank instead,
   and there was nothing on the candidate to rank BY.
3. **`fv::app::SnapCandidates(manager, proj, p, tolerance_px)`** — the same walk, shell-free and
   const, ranked nearest-first, stable so a tie keeps stack order. `PickSession::SnapToPoint` is
   now that walk plus the dialog. It is a free function because a phone should not have to
   implement eleven pure virtuals of `AppShell` to ask a question it will never ask.

`PPMap` gained `snapTarget(near:in:tolerance:)`, which asks the **stack** and not the point
store — the method did not change when the second implementer arrived and will not for the third.
`PPSnapTarget` carries the coordinate, the name and the distance in points.

#### On the phone

`MapModel.pickSnap` is published beside `pickPlace` and **answered in the same render-queue
hop**, because the two are one fact about one crosshair: a button naming a road over a coordinate
that had already snapped would be P11's "lie the user cannot see" with a second author. Every
call site now reads `pickCoordinate` (`pickSnap?.coordinate ?? crosshairCoordinate`), which
extends P11's one-definition rule by exactly one line, so the words on the button and the
coordinate the button stores stay the same place.

**The snap is automatic within tolerance and never silent.** The confirm button reads
`Use Ruddy Turnstone`, and the crosshair's ring grows and takes the accent colour. Moving the map
off the feature is the refusal, and no second control was added to spell one.

**The crosshair cannot move to the feature, and that is not a compromise** — it is pinned to the
centre of the screen by construction, the map moves under it, so a crosshair that slid off centre
would be a second and contradictory answer to "where is the pick".

#### Two things the simulator corrected, both of which the mac could not have

* **The tolerance was backwards.** It shipped at 30 points on the reasoning that a pick wants a
  wider catch than a tap — and on screen it snapped to a marker sitting **28 points away, plainly
  beside the crosshair**, which is not what *overlaps* means. It is now 12, and the reasoning is
  inverted: the number is the margin BEYOND the marker's own drawn ink (`HitTestPoint` adds the
  half-width first), so a 26-point marker is caught from about 25 points out — roughly when the
  ring touches it. A tap can afford more because the FINGERTIP is the blunt instrument; here the
  rider steers the map under a crosshair they can place exactly, and eagerness reads as the app
  picking the wrong thing. `pippin.ini` `[pick] snap_tolerance`, 0 to switch it off.
* **The first indicator was invisible.** A small white dot inside the ring is indistinguishable
  from the marker sitting behind the crosshair on a pale chart. Two channels — hue and size —
  survive being drawn over a map; one does not.

#### Proved on the simulator, and this is the acceptance criterion

Start was picked over the point `Bufflehead Dr`. The route document then held
`32.6095000000, -80.0655000000`, **identical to ten decimal places** to that row in
`points.fvpoints`. The End waypoint, picked in the ordinary way, holds `32.5956007934,
-80.1096644372` — the un-projection of a pixel, which is exactly the noise a snap exists to
remove.

#### What is NOT in it

An **overlay that is invisible is not a snap target**, which falls out of the aggregation rather
than being decided anywhere: switch the points off and the crosshair stops snapping to them,
because a coordinate that jumped to something the rider cannot see is indistinguishable from the
app losing the pick. There is **no snapping to a position along the route's drawn line** — a snap
returns a place that already existed and had a name, and the nearest point on a polyline is a
computed coordinate with nothing behind it. And `LocationText.useButton`'s precedence rule is
**not covered by a mac test**: it needs `PPPlace` and `PPSnapTarget`, so testing it off the phone
would mean a stub harness for two ObjC value types; both of its branches were read off the
simulator instead.


### P20 — Search, and the route dialog redesigned around it — **BUILT 2026-08-30**

The requirement is Chris's, given whole: the route dialog opens on a SEARCH BOX; picking a result
puts it in the destination and the rider's own position in the start; from there the dialog is the
form it has always been, vias and all; and the `⋯` that edits one stop gains a search above
*Current location* and *Pick on map*.

**No search engine was written.** S1–S4 built the whole seam a month ago and nothing in Pippin
had ever called it: `fv::app::SearchSession` walks the overlay stack and asks everything that
answers `AsSearch()`, so one query reaches the rider's `.fvpoints`, the chart's named features and
the routing graph, and ranks the union once. What P20 built is a bridge (`PPSearch.{h,mm}`,
`PPMap searchFor:inViewport:`), two decisions of Pippin's own, and the dialog.

**The two search-only overlays.** A shell can only search what is in the stack, and Pippin had two
searchable things that were not overlays: the chart it draws through `VectorRenderer`, and the
`.fvroad` the router plans on. Both are now wrapped and held **NOT VISIBLE** — which is
`search.h`'s own arrangement rather than a dodge — and built LAZILY on the first search, because
the road one reads Kiawah's graph and most launches never search. **The graph is shared, never
loaded twice**: `RoadGraphOverlay::EnsureGraph` would read the file itself, so the planner's own
`shared_ptr` is handed over instead, which is the sharing `PPRouteStore.h` already documents
between the router and the snapper.

**The query carries no area, and that is the whole of "search everything"** — Chris's correction,
same day, and it is the one design mistake this session made. The first cut put the viewport on
the `SearchQuery` so the chart would have something to scan, and **an area on the query is a cut
on every provider**: it quietly shrank the two that never needed one. A `.fvpoints` document is a
few dozen rows, and `RoadGraphOverlay` tests each DISTINCT NAME once against the graph's own name
table — its own comment already said so ("without one every node is visited, which a text query
can afford because of the table above"). Both answer over the whole island for nothing.

So the viewport is **lent to the chart alone**: `VectorMapOverlay::SetSearchFallbackArea`, the one
line of fvkit this session added. A text query with no area of its own runs over that window
instead of over nothing; a query with its own area is untouched, a spatial-only query is
untouched, and **an indexed pack never reaches it**, because tier 2 answers globally first and a
window would be a scope the shell had no business imposing. 7 gtests.

`search.chart_in_view_only` is what that window is switched by — a settings key and not a
checkbox, which is the requirement's own wording, and there is deliberately no key for confining
the points or the roads because there is no reason to want one. **Turning it off does less than it
looks like it should**: with no window and no name index the chart contributes *nothing* and the
answers are the points and the roads. `fvnames build` over the staged pack is the one line in
`stage_data.py` that would make the chart global too, and nothing has needed it yet. **`near` is
still always set**, and after this change it is the only thing making a global answer read as a
local one.

**`PPSearchRules.h` is the fifth pure-C++ header**, and it holds the two decisions that fail
invisibly. **(1) The word**, which since 2026-08-30 is DRAWN rather than spelled — a pin
(`mappin.and.ellipse`, the points button's own glyph), a hand-drawn `RoadSquiggle`, and an upside-
down `drop.fill` for the map marker; the word itself survives as the accessibility label, so
`PPSearchKindWord` is still the one definition and still what the mac test asserts on. The
vocabulary is three wide — **Point** for the rider's own document, **Road** for the graph and the
chart's `transportation*` layers, **POI** for everything else named. The provider's own `detail` ("transportation_name · residential") is
right for a shell whose user is reading a chart schema and wrong for a thumb. Which provider
answered is decided by POINTER IDENTITY against the overlays `PPMap` created, never by parsing
that string. **(2) The duplicate.** The chart and the graph both answer "Ruddy Turnstone" —
PythonView shows both, tagged, and is right to; a phone shows one. **The survivor is whichever
ranked higher, never "the chart's"**, and that is what keeps the rule safe with the scope key off,
where the graph's row is the only row there is. Same name is NOT enough to call two rows one thing
(ten thousand Main Streets), so the test is name AND place — **bounds overlap first**, because the
two recordings of one street are anchored kilometres apart on a long road, with the centre
distance as the fallback for the degenerate boxes a point answers with. 15 gtests.

**Framing is a second fit rule and not the first one with a flag.** `ScaleToFitBounds` joins
`ScaleToShow` in `PPCameraFit.h` and differs in three ways that are all the requirement: it
**narrows as well as widening** (this IS "take me there", where the zoom-out rule fires on a
button press about something else); **a degenerate box only re-centres**, because no scale fits a
dimensionless thing; and it **aims above the middle**, since the sheet that asked is still on
screen. Both fits are discs rather than boxes, for the rotation reason P7 already argued. 7 gtests.

**What the box opens on (Chris, same day).** Both phases now open at the MEDIUM detent — the
search box was large on the reasoning that a keyboard was about to take half the phone, and a
pre-filled box raises no keyboard at all, so large only bought a full-screen sheet over the map
the rider is choosing on. It opens on the last thing successfully searched for, and on the pack's
`search.initial_text` ("Boardwalk", Kiawah's numbered beach boardwalks — in the road graph and
spread along the island) until there has been one: the seed is a PACK key because the right word
is a fact about the data, and the memory is a USER DEFAULT because where the rider has been is a
fact about the install. The keyboard comes up only when the box is empty, and the field selects
all when tapped so the first keystroke replaces rather than appends (SwiftUI has no spelling for
that; it is `UITextField.textDidBeginEditingNotification` plus a `selectAll(nil)` one runloop
later). **And the term is recorded past the cancellation check, which is a bug the simulator
caught**: recording it inside `MapModel.search(for:)` stored whichever prefix query resumed last,
so typing "heron" left the box reading "Hero".

**Four things the simulator (and the first ride through it) corrected.**

0. **An area on the query is a cut on everybody**, which is the paragraph above and the one thing
   here that was a design error rather than a detail: the point document and the road network were
   being limited to what was on screen so that the chart could be. Pinched all the way in to about
   a hundred metres of Gadwall Lane, "bufflehead" now answers *Bufflehead Drive — Road, 386 m* and
   *Bufflehead Dr — Point, 434 m*, and "heron" reaches *Night Heron Park, 3.0 km* at the far end
   of the island.
1. **`.presentationDetents` per branch does not resize a live sheet.** The search box came up at
   the medium detent — a text field with two rows of list under it — because the sheet was
   presented on the *other* branch. A sheet has one presentation, so it now has one set of detents
   and a `selection` binding the phase drives.
2. **A stop chosen by name was being renamed to the road under it.** P11's rule — a row names the
   nearest road the profile could ride on — is exactly right for a coordinate picked off a
   crosshair and exactly wrong for one the rider typed most of: searching *Marsh Observation
   Tower* and being answered `cycleway` is the app forgetting what it was just told. `RouteStop`
   now carries the chosen name, DISPLAY ONLY (the document's waypoint labels stay positional,
   because `RouteDoc` selects by label), and every other way of filling the row clears it. The
   precedence is P19's, one step further: **snap > chosen name > road > numbers.**
3. **The `⋯` menu's own answer to how a search sheet is presented** is a sheet over a sheet, which
   needs nothing to survive a dismissal — unlike "Pick on map", which is why the draft lives in
   `MapScreen`.

**Clear Route is one tap and always on screen.** It was a labelled row below the mode picker from
P6, which is below the fold at the medium detent — one tap in principle and a scroll plus a tap in
a rider's hand. It is now a toolbar item beside Cancel and OK, it appears only when there is a
route, and **it does not dismiss**: clearing is almost always the first half of starting a
different one, so the sheet returns to the search box on an empty draft.

**The route's own waypoints are dropped from the answers.** `fv::RouteOverlay` is a search
provider and answers "Start", "End", "Via 1" — useful in a shell that can select a waypoint, noise
in the dialog that is asking where the next stop should go.

**Measured on the simulator.** "marsh" over the Kiawah view returned *Marsh Edge Lane — Road,
825 m*, *Marsh Observation Tower — POI, 1.3 km*, *Marsh Island Woods — POI, 1.4 km*, *Marsh Elder
Court — Road*, *Marsh Cove Road — Road*, *Marsh Cottage Lane — Road* — nearest first, no road
listed twice, and the chart and the graph agreeing silently. "marsh obs" (token prefix) narrowed
it to the one row. Choosing it framed the map behind the sheet, filled End with the name and Start
from the receiver's own fix, and OK planned — reporting *stops 1 and 2 are not connected*, which
is the honest answer for an observation tower across the marsh.

## What is deliberately NOT in v1
Server/gazetteer lookup, other areas of interest (the manifest is data-driven, so a second
region is a second data pack, not code), point editing, breadcrumb trail rendering, Metal,
CarPlay/watch, background-location recording (v1 records only while foregrounded — background
modes are an entitlement + battery conversation for a later session).
