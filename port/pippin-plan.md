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

**Next, planned 2026-09-25**: the share fix (SH), battery and the night chart (BT), dragging
without blank edges and with a fling (DR), tides (TD), beach routing (BR), elevation over an
Atlanta pack (EL) and the 2.5D view (PV) are in **"After 1.1"** at the end of this file. Screen-off
tracking stays BG1–BG5 in `port/archive/pippin-guidance-plan.md`, refined the same day.

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

- **The route-sheet Point-name picker is dropped** (Chris, 2026-09-24): P19's overlay snap already
  lands a waypoint on a point's surveyed coordinate, and that is the behaviour he wants.
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

## After 1.1 — planned 2026-09-25

Chris, 2026-09-25: beach routing, tide information, elevation in the router, the Google Maps
share failure, background GPS, battery, and (stretch) a 2.5D view. Later the same day: dragging
without the blank edges that show as the map slides and turns, and a fling that coasts to a stop
the way Apple's scroll views do (DR); the night chart approved (BT4); and Atlanta, the next place
to map, as the hilly test pack (EL5). This section records what the
survey found and cuts the work into steps of one session or less. Background GPS was already
planned as BG1–BG5 in `port/archive/pippin-guidance-plan.md`; that section was refined in place the same
day and is only summarised here. The session protocol is unchanged: one step per session, mac
tests first where the code allows, a simulator or phone artifact where it does not, commit per
step, ledger row after.

### Order, size, dependencies

| # | Step | Size | Needs | Proved on |
|---|---|---|---|---|
| 1 | SH1 the share chain made visible, the message made honest | S | — | mac `place_link` + phone |
| 2 | SH2 a named Google place through Apple's search | S–M | SH1 | phone |
| 3 | BT1 measure before optimising | S | — | phone |
| 4 | BT2 a frame budget while following | S | BT1 | sim + phone |
| 5 | DR1 an underlay, so the edge is never blank | M | — | sim |
| 6 | DR2 the band ready before the finger moves, and ahead of it | M | DR1 | sim + phone |
| 7 | DR3 the fling | S–M | DR1, BT2 | sim + phone |
| 8 | BG1–BG5 screen-off tracking (guidance plan) | S–M each | BT1 for BG3's numbers | sim + phone |
| 9 | TD1 the tide table | M | — | mac |
| 10 | TD2 the tide card | S | TD1 | sim |
| 11 | BR1 the beach in the graph | M–L | — | mac |
| 12 | BR2 the beach in the rules | M | BR1 | mac |
| 13 | BR3 the beach in the document and the plan | M | BR2 | mac |
| 14 | TD3 the beach verdict | S | TD1, BR3 | mac |
| 15 | BR4 the beach in the route sheet | M | BR3, TD3 | sim |
| 16 | BR5 riding the beach | M | BR4 | sim + phone |
| 17 | BT3 the ownship leaves the canvas | M–L | BT2's numbers | sim + phone |
| 18 | BT4 the night chart (approved) | M | — | sim + phone |
| 19 | EL1–EL4 elevation | M each | — | mac, then sim |
| 20 | EL5 the Atlanta pack | M | EL1–EL4; Chris's DTED2 | data + sim |
| 21 | PV1–PV5 the 2.5D view | M each | BT4 before PV2, DR1 before PV5, BT3 before PV4 | mac, then sim |
| 22 | DM the dark map | M | — | sim + phone |
| — | TD4, BG6, RG1 | optional | | |

S is well under a session, M is a session, L is a full session with no slack. The beach was the
one feature that would not fit, so it is five steps.

Why this order: SH is a defect in a shipped feature. Measurement comes before BG because BG3 is a
battery trade (a lit screen against a pocket), and before BT3 because that is only worth its cost
if the numbers say so. DR follows BT2 because both change the render loop's pacing, and the fling
only makes blank edges worse until the underlay exists. The tide table lands before the beach
toggle so the toggle never ships without the information that makes it usable. The night chart
comes before the 2.5D view because the sky and the distance fade take their colours from the
active style.

### SH — the Google Maps share (item 4)

**What the survey found.**

- The sentence Chris saw is Pippin's own. `ShareViewController.run()` shows *"Couldn't get a
  position out of that link. Short links have to be looked up, and that needs a signal."*
  whenever `PlaceLink.resolve` returns nil, for any reason. `resolve` catches the `URLSession`
  error and discards it (`catch { body = nil }`), so "no network" and "the chain carried no
  coordinate" print the same sentence. Blaming the signal is a guess. That part is certainly a
  bug.
- The Google path has never seen a real Google link. P14 was proven on the phone with Apple Maps
  only; `test/place_link_test.swift` holds hand-written `google.com/maps/place/…` URLs and one
  `expectNothing("https://maps.app.goo.gl/aBcDeFgHiJkLmN")`, never a captured redirect chain.
- A probe from the mac, 2026-09-25, with `resolve`'s own Safari user agent:
  `https://maps.google.com/?q=Kiawah+Island+Golf+Resort,+1+Sanctuary+Beach+Dr,…` follows two
  redirects to `https://www.google.com/maps?q=…` (200, 210 KB). No hop carries a coordinate. The
  page body does — `center=34.138…%2C-84.236…` and `APP_INITIALIZATION_STATE=[[[…,-84.2367,34.1384]`
  — but it is **the requester's own IP location** (north Georgia), not the place: the search runs
  in JavaScript. Today's regexes happen not to match either shape. SH must not be "fixed" by
  teaching the body scrape them; that would drop the marker in the rider's own town.
- The likely failing case, to be confirmed by SH1's capture rather than assumed: a Google Maps
  share of a *named* place redirects `maps.app.goo.gl/<id>?g_st=…` to
  `maps.google.com/?q=<name>,+<address>&ftid=0x…:0x…&…` — a name and an address, no coordinate.
  A dropped pin shares `q=<lat>,<lon>`, which `place(in:)` already reads. The `ftid` is a Google
  feature id; resolving it needs the Places API and a key, which is not an option.

#### SH1 — the chain made visible, the message made honest (S)

**ACCEPTED on Chris's iPhone, 2026-09-25.**
The real cause of the Google share failure was not the signal: `maps.app.goo.gl` answers a
**desktop** Safari user agent with a 200 "DurableDeepLinkUi" page that redirects in JavaScript, so
`resolve()` saw no hops. Every other agent gets a 302. The fix is `PlaceLink.userAgent`, a mobile
Safari agent; Apple's `maps.apple` short links redirect with either agent. On the phone: a dropped
pin (on a road or on the beach) shares `q=<lat>,<lon>` in the first hop and lands exactly. A place
picked from a Google search shares `q=<name>, <address>&ftid=…` with no coordinate in any of its
three hops, and goes to SH2. A "pin has no address" message was added on the wrong premise (that a
dropped pin could resolve to no coordinate) and reverted once the user-agent fix made that case
moot; the message for a query-less no-coordinate outcome is "That link doesn't carry a position."
`PlaceLink.resolve` now returns `ResolveOutcome { place, offline(URLError.Code),
noCoordinate(lastHop:, query:) }` with the plan's six offline codes; `RedirectSniffer` logs every
hop at `.notice`/`.public` and records the chain. The pure `PlaceLink.classify(start:hops:found:
body:failure:)` holds the decision so a captured chain replays on the mac, and
`PlaceLink.failureMessage(for:)` gives the three sentences — the query one reads *"That link sent
a name, not a position: <query>"* (not "Google", since any host can send `q=`) as SH1's stand-in
until SH2. Decision: the body scrape accepts only Google's `!3d/!4d` place pair, never the
`@lat,lon` camera or `center=`, because those are the requester's own IP location; `q=` values are
form-decoded (`+` → space). `test/place_link_test.swift` is 70 checks green, including a
2026-09-25 mac-captured chain (`maps.google.com/?q=…` → `maps.google.com/maps?q=…` →
`www.google.com/maps?q=…`, no coordinate anywhere), a pinned IP-location body negative, the six
offline codes, a dropped-pin hop, and the three messages. iOS sim build green on iPhone 17. Still
open before SH1 counts as accepted: the three phone shares from Google Maps (named place, dropped
pin, airplane mode) with their captured hops added as fixtures.

- `RedirectSniffer` logs every hop at `.notice`, `privacy: .public`. Today only the start and the
  outcome are logged.
- `resolve` says why it failed: `enum ResolveOutcome { case place(SharedPlace), offline(URLError.Code),
  noCoordinate(lastHop: URL?, query: String?) }`. Offline is `.notConnectedToInternet`,
  `.networkConnectionLost`, `.timedOut`, `.cannotFindHost`, `.dataNotAllowed`,
  `.internationalRoamingOff`. `query` is the `q=` text of the last hop that had one and did not
  parse as a coordinate.
- The extension says three different things: offline → needs a signal; no coordinate and no
  query → "That link doesn't carry a position"; no coordinate with a query → SH2's path (until SH2
  lands: "Google sent a name, not a position: <query>").
- On the phone, from Google Maps: share a named place, a dropped pin, and a named place in
  airplane mode. Read the `org.peregrine.Pippin` / `share` log. Every captured hop URL becomes an
  offline fixture in `test/place_link_test.swift` (the fixtures are URLs; the test never touches
  the network). Add a pinned negative: a body with `center=…%2C…` and `APP_INITIALIZATION_STATE`
  yields no coordinate.
- Acceptance: three shares, three different and true messages; `ctest -R place_link` green.
  Confirmed on the phone 2026-09-25: a dropped pin (road or beach) resolves exactly, a Google
  search share carries a name with no coordinate and hands off to SH2, and offline shows the
  signal message.

#### SH2 — a named place through Apple's search (S–M)

**ACCEPTED on Chris's iPhone, 2026-09-25.**
On the phone, "Kiawah Beachwalker Park Parking Lot, 8 Beachwalker Dr…" gets no POI result from
Apple (Apple has no POI called "…Parking Lot"). The address fallback lands 40 m from the park;
an address-fallback result now takes the query's name part (`PlaceLink.namePart`) rather than its
street line, so the marker keeps the park's name. Shared places now get a yellow badge
(`PointPalette.yellowHex`, "#f5c518", added to the palette) with the document's "marker" teardrop
icon, looked up by name. Known weakness, not fixed: an address fallback whose address has no
street (e.g. "Kiawah Island, SC") lands on the town centroid and still passes the one-shared-word
match; only the "check it" remark warns.
The location usage string is unchanged: it promises the rider's position is never sent, and that still holds (the extension has no location permission and sends only the shared name). Rewording App Store-facing text is Chris's call. MapKit only, no `CLGeocoder`.

- When SH1's outcome is "no coordinate, with a query", the extension asks Apple: `MKLocalSearch`
  with the query as `naturalLanguageQuery` (a POI search, so "Kiawah Island Golf Resort, 1
  Sanctuary Beach Dr" lands on the resort, not a street centreline), falling back to address
  geocoding of the part after the first comma. If the iOS 26 SDK marks `CLGeocoder` deprecated,
  use MapKit's replacement; the deployment target stays 17.
- `PlaceLink.swift` stays Foundation-only (it runs under a bare `swift` on the mac). The search
  lives in a new extension-only `PlaceGeocoder.swift`. Its one decision — accept a result only
  when its name or postal address shares a word with the query — is a pure function tested on
  the mac.
- A searched place is approximate and says so: the callback URL gains `approx=1`, and
  `MapModel.acceptSharedPlace` writes "Position from Apple's search for '<query>' — check it"
  into the remarks. Same default marker as any shared point (P14's reasoning).
- Privacy: this is Pippin's second network use and the first to a company that did not mint the
  link. `allowsNetworkLookup`'s comment and the location usage wording change to say so; review
  `PrivacyInfo.xcprivacy`.
- `place_link` is 82 checks green (mac); the iPhone 17 Pro simulator build is green with
  `PlaceGeocoder.swift` added to the PippinShare target.
- Acceptance on the phone, confirmed 2026-09-25: sharing Beachwalker Park from Google Maps gives
  a yellow-pinned point near the park named for the park (not its street line), remarks marked
  approximate; a dropped pin is exact, no remark; airplane mode says it needs a signal.
- Fixtures in `test/place_link_test.swift`: Chris's dropped pin, beach pin and searched place,
  plus a named-place hop captured from the web; 93 checks, all green.
- Build note: after Xcode moved to the iOS 27.0 SDK, `build-ios` needed `cmake --fresh --preset
  ios` (a stale SQLite3 path pointed at the 26.5 SDK). `build-ios-sim` will need the same.

### BT — battery (item 6)

**Where the energy goes, by state.** Qualitative until BT1 puts numbers on it.

| State | Display | GNSS | CPU | Today |
|---|---|---|---|---|
| Following, screen on | lit, pinned (P15) | navigation (P17) | the display link never pauses: the follow slew retargets on every fix. Each frame is an overlay raster (8–11 ms on the simulator); about one in nine redraws the base (25–33 ms) | the expensive state, and where a ride spends its time |
| Recording, screen on | lit, pinned | navigation | draws only when the ship is visible or followed (P16) | |
| Browsing, ship off screen | lit until Auto-Lock | coarse (P17) | no render per fix (P16) | done |
| Backgrounded, neither following nor recording | off | off (suspended) | none | zero; BG2 must keep it zero |
| Backgrounded, following or recording (after BG) | off | navigation | per-fix work, no drawing (BG1) | the target for a long ride |

Three facts set the priorities:

1. The display is the largest single draw while it is lit, and an OLED panel (Chris's iPhone 14
   Pro) spends power roughly in proportion to pixel luminance. Pippin's chart is a light style.
   The two levers are not lighting the screen at all (BG, BG5, optionally BG6) and a dark chart
   when it is lit (BT4).
2. Nothing sets `preferredFrameRateRange` on the `CADisplayLink`, and the follow slew keeps it
   running. The app runs at 60 Hz only because `Info.plist` does not set
   `CADisableMinimumFrameDurationOnPhone`. A map that slides at a rider's speed does not need 60
   frames a second (BT2).
3. The receiver's cost is roughly fixed while it runs at navigation accuracy; `desiredAccuracy`
   is a hint (P17). The idle saving is built. What remains is a `distanceFilter`, so a stopped
   rider generates no fixes and therefore no frames (BT2).

#### BT1 — measure before optimising (S)

- A ride energy log: once a minute while following or recording, append `time, batteryLevel,
  batteryState, thermalState, frames drawn, base draws, cache hits, fixes` to
  `Documents/energy-<ride>.csv` beside the GPX. Battery monitoring is enabled only while riding.
  Exported with the ride, the way P22 exports a track.
- MetricKit: an `MXMetricManagerSubscriber` saves each payload as JSON under
  `Documents/metrics/`. The fields that matter are cumulative CPU and GPU time, location activity
  time by accuracy tier, and `MXDisplayMetric`'s average pixel luminance (the direct measure of
  what BT4 would save). Payloads arrive about daily, so this is the long-run record; the CSV is
  the per-ride one.
- `BUILDING.md` gains the procedure: Instruments' Power Profiler (Xcode 26) recording on the
  phone for a 20-minute ride; the Xcode energy gauge for a quick look.
- The acceptance artifact is a baseline table at fixed brightness over the same Kiawah loop: 30
  minutes following, 30 minutes recording with the map visible, as percent per hour. BT2–BT4 and
  PV5 each re-run it.

**DONE 2026-09-25** (built, installed on Chris's iPhone 14 Pro, baseline recorded the same day).

- `Pippin/EnergyLog.swift` (Foundation-only): `EnergyMode`, `EnergyCounters` with process CPU via
  `getrusage`, `EnergyMeter` differencing, `EnergySample`'s CSV row. Tested on the mac by
  `test/energy_log_test.swift` via `run_energy_log_test.sh`, ctest `pippin_energy_log`, 18 checks
  green.
- `Pippin/EnergyRecorder.swift`: `EnergyRecorder`, owned by `MapModel`; a session is
  following || recording; writes `Documents/trips/energy-<yyyy-MM-dd-HHmm>.csv` — a zero row at
  start, one row per 60 s, an early row on a mode change and at the end; battery monitoring runs
  only during a session. Columns: `time,mode,battery_level,battery_state,thermal_state,frames,
  base_draws,cache_hits,fixes,cpu_s` — two beyond the plan (`mode`, `cpu_s`) because BT2's
  acceptance is CPU time per minute. `MetricsArchive` (an `MXMetricManagerSubscriber` started in
  `PippinApp.init`) saves metric and diagnostic payloads to
  `Documents/metrics/<kind>-<begin>--<end>.json`, deduplicated by name.
- Energy logs are listed in the Rides sheet beside the GPX files ("Energy log, <date>"), shared
  and deleted the same way. `fixes` counts live-feed fixes only; the demo feed is polled in C++
  and shows 0.
- First numbers, simulator (iPhone 17 Pro sim, following at 8 m/s): ~46 frames/s (2763 and 2966
  frames/min, ~5–7% base redraws), 60 fixes/min, 55–67 CPU-seconds per minute. Simulator only, not
  a phone number, but it confirms BT2's premise that following runs near 60 Hz.
- `BUILDING.md` gains "Measuring energy": the ride log, the baseline-table procedure, MetricKit,
  Instruments' Power Profiler.
- **Baseline, 2026-09-25**, iPhone 14 Pro, Release build, on the bench, unplugged, fixed half
  brightness, Pippin open throughout. The GPS came from `xcrun devicectl device simulate location
  route` over Wi-Fi (no debugger): `kiawah_full_1x` out and back, 12.2 km at 3.58 m/s, fixes at
  1 Hz. Raw log: `port/apps/Pippin/docs/bt1-baseline-2026-09-25.csv`.

  | State | Minutes | Frames | Base draws/min | CPU s/min | Battery | Thermal |
  |---|---|---|---|---|---|---|
  | Following (GPS mode) | 39 | 58 /s | 31 | 43.5 (35–66) | 95→75 % in 25 min, **~48 %/h** | serious from minute 12 |
  | Recording, ship on screen | 27 | 1.5 /s | 60 | 5.4 | 75→70 %, **~10 %/h** (5 % steps) | back to nominal in 8 min |

  The screen was lit in both, so the ~5x difference is the 60 Hz follow loop, not the display.
  Following also ran into `serious` thermal state, and its CPU rose as it did. Recording shows
  one base draw per fix: 60 of its 90 frames a minute redraw the base, although the camera does
  not move. BT2 re-runs this table; the recording row's base draws are a BT2/BT3 question.

#### BT2 — a frame budget while following (S)

- `FramePolicy.swift`, a CoreGraphics-only value with a ctest table like `RenderGate`: a gesture →
  up to 60 Hz; following with no gesture → `[display] follow_fps` (start at 20, range 15–30);
  idle → paused, as today. Applied through `link.preferredFrameRateRange` when the state changes,
  not every frame.
- Skip a frame whose camera moved less than `[display] min_move_pt` (0.25 pt) with no content
  change. A rider stopped at a junction otherwise redraws the same picture while the slew
  converges.
- `distanceFilter = 2 m` in the navigation configuration (`PPLocationSource.mm`). Check that
  P17's paused-run restart still fires.
- The slew's duration is the measured fix interval (`PPFollowCadence.h`); a lower frame rate
  samples the same motion less often, so it stays continuous. If the ship visibly steps at 20 Hz
  on the phone, use 30.
- Acceptance: BT1's table re-run, CPU time per minute of following at least halved (the
  prediction is a third at 20 Hz), no judder on the phone.

**BUILT 2026-09-25** (simulator; phone acceptance pending).

- `Pippin/FramePolicy.swift` (CoreGraphics-only): gesture or not-following → system rate;
  following, no gesture → `[display] follow_fps` (default 20, clamped 15–30) via
  `CADisplayLink.preferredFrameRateRange`, applied on a mode change only (gesture began/ended,
  riding change, link creation). ctest `pippin_frame_policy`, 28 checks.
- The min-move skip is a camera STEP, not a dropped frame: the slew is advanced inside the
  render, so skipping a render would freeze it. New `PPMap stepCameraAtViewport:` →
  `PPCameraStep` ticks the moving map (feed poll, fixes, trip, guidance, slew) without drawing;
  `MapModel.tick()` steps when following, no gesture, no content change, same scale/surface, and
  the picture would move < `[display] min_move_pt` (0.25) since the last drawn frame (centre
  shift + rotation arc at the half-diagonal). MapScreen's preview transform carries the
  sub-point move; a step that consumed a fix marks content dirty; one exact frame is drawn
  before the loop pauses after steps. `-PPShowStats` shows "N stepped".
- `distanceFilter = 2 m` in navigation mode (`PPLocationSource.mm`). P17's paused-run restart is
  keyed on the mode change back to navigation and on `pausesLocationUpdatesAutomatically`, which
  is NO in navigation, so it is unaffected.
- Simulator numbers (iPhone 17 Pro sim, simctl location route at 8 m/s, zoomed out to
  1:96,161): 496 and 408 frames drawn/min (vs BT1's ~2,900), ~20 Hz total ticks (947 drawn +
  1403 stepped in ~2 min), 19.3 and 20.6 CPU s/min (vs BT1 sim 55–67) — about a third. With the
  location cleared: 0 frames and 0.003 CPU s the next minute.
- **ACCEPTED on the phone 2026-09-26.** BT1's procedure: iPhone 14 Pro, bench, unplugged,
  half brightness, `devicectl simulate location route` over Wi-Fi (`kiawah_full_1x` out, back
  and out, 3.58 m/s, 1 Hz). Raw log: `port/apps/Pippin/docs/bt2-phone-2026-09-26.csv`.

  | State | Minutes | Frames | Base draws/min | CPU s/min | Battery | Thermal |
  |---|---|---|---|---|---|---|
  | Following, BT2 | 32 | 4.7 /s | 23 | **14.4** (9.5–24) vs BT1 43.5 | 97→95 % | nominal throughout |
  | Recording, BT2 | 40 | 0.9 /s | 3.8 | **1.5** vs BT1 5.4 | 95→90 % at minute 12 | nominal |

  Whole session: 97 % → 90–94 % in 72 minutes, roughly 3–6 %/h across both states, against
  BT1's ~48 %/h following and ~10 %/h recording. The 5 % battery steps are too coarse to split
  the two halves. Following CPU rose to 16–24 s/min in its last minutes because Chris zoomed in
  (more ticks cross `min_move_pt` and draw). Fixes ran 30–40/min against BT1's 60, probably
  the 2 m distance filter on the simulated track. No judder at 20 Hz; `follow_fps` stays 20.
  Recording's base draws fell from 60/min to ~0 after the first minute, which answers BT1's
  one-base-draw-per-fix question. BT3 is optional on these numbers.

#### BT3 — the ownship leaves the canvas (M–L; required for PV4)

- Every follow frame re-rasterises the whole overlay (route, points, ship) because the ship
  moved. Split P18's overlay layer in two: a static overlay (route, points, waypoint diamonds)
  drawn with the base's band and camera and cached like the base — invalidated by the content
  epoch, never by the camera — and the ship as a SwiftUI/`CALayer` sprite placed by
  `viewport.point(forGeo:)` and turned by the resolved heading. A follow frame with no content
  change becomes composite-only.
- The ship's artwork is still the core's (`fv.north`/`fv.ownship`), rendered once per size and
  colour into a `CGImage` by `PPMap`, likewise its halo.
- P18's settle rule applies to the static overlay too: at rest it must be exact.
- This sprite is PV4's billboard layer, which is why BT3 comes first.

**Built 2026-10-04, phone-accepted 2026-10-05.** Required since 2026-10-04 (PV4's
upright symbols), no longer optional.

- fvkit `MovingMapOverlay`: `SetDrawSymbol(false)` places the ship and rebuilds the apron without
  stamping; `DrawSymbolAt(proj, canvas, x, y, angle)` stamps edge + body anywhere. 1 new gtest.
- `PPMap`: the moving map is hidden from `DrawAll` and placed by a direct `OnDraw` into a 1×1
  scratch canvas at the live camera, on every frame **and every step**, so MM2's apron is always
  built from where the sprite is shown. The route and points are drawn at `_baseViewport` into a
  band-sized transparent image, kept until the base moves on or `_contentEpoch` moves (bumped by
  every route/point/visibility/selection/drag/tide/departure/GPS-mode method and by a symbol-scale
  change). `PPFrame.overlayViewport` is that band. The ship sprite is rendered once per symbol
  scale; `PPOwnship` carries `symbolImage`, `symbolPixelsPerPoint`, `viewRotationDegrees`.
- Route waypoint hit test and drag read the overlay's last-drawn projection, now the band's, so
  `routePixel:inViewport:` maps a screen point through the ground into band pixels.
- `MapModel`: `shipDirty` (fix, demo poll) is served by a camera step, not a frame;
  `PPCameraStep.sawNewFix` removed. `MapScreen` places the sprite by offset from the stack's
  centre (the stack is band-sized) and turns it by `screenAngle + live.rot − viewRotation`.
- Memory: the overlay is band-sized now (2.25× the screen, ~28 MB on a 3x phone) instead of
  screen-sized; dropped with the base on a memory warning.
- Sim (iPhone 18 Pro, Atlanta pack, `simctl location` at 8 m/s): ship on the fix and on the
  follow anchor in six consecutive captures; course-up and north-up angles right; route drawn in
  register; an End-waypoint drag moved it 2.5 km to the release point and the cached overlay
  redrew. GPS follow: 313/391 frames a base hit, 149 fixes served as steps.
- **ACCEPTED on the phone 2026-10-05.** Procedure: iPhone 14 Pro, bench, unplugged, half
  brightness, `devicectl simulate location route` over Wi-Fi, `kiawah_full_1x` one pass per
  test (~29 min at 3.58 m/s, 1 Hz). Build: Release archive with
  `SWIFT_ACTIVE_COMPILATION_CONDITIONS='$(inherited) DEBUG'` so the energy log (Debug-only
  since 1.2) is compiled in while Swift stays optimised — comparable to BT1/BT2, which were
  Release builds before the log became Debug-only. Kiawah pack, includes PV1–PV5.

  Three tests, raw logs in `port/apps/Pippin/docs/`: `bt3-phone-1-following-screen-on-2026-10-05.csv`,
  `bt3-phone-2-following-locked-2026-10-05.csv`, `bt3-phone-3-recording-locked-2026-10-05.csv`.

  | State | Minutes | Frames | Base draws/min | CPU s/min | Fixes/min | Thermal |
  |---|---|---|---|---|---|---|
  | Following, screen on, no route | 25 | 7.7 /s | 20 | **9.2** vs BT2 14.4, BT1 43.5 | 39 | nominal |
  | Following, screen on, route shown | 3 | 8.9 /s | 21 | 9.7 | 49 | nominal |
  | Following, route open, screen locked (Live Activity) | 28 | 0 | 0 | **0.04** | ~40 | nominal |
  | Recording only, screen locked | 28 | 0 | 0 | **0.026** vs BT2 screen-on recording 1.5 | 31 | nominal |

  Following CPU fell about a third from BT2 while frames rose (7.7/s vs 4.7/s) — follow frames
  are now mostly composite-only, which is BT3's premise. Showing the route costs ~0.5 CPU s/min
  (cached static overlay). The locked rows are the app process only; the Live Activity is
  rendered by the system and is not in `cpu_s`. Battery (5 % steps): 97 % at start (first row
  already 0.95) → 90 % at minute 24 of test 2 (~56 min, screen-on following plus the start of
  locked following), then 90 % held through the rest of test 2 and all of the 35-minute
  recording (~75 min screen-locked without a 5 % step, so under ~4 %/h locked). Fixes ran
  ~30/min on the simulated track in recording, as BT2 saw. The first attempt at test 3 stopped
  42 s in (one fix in the GPX, a mode-change row, no crash); most likely a second tap on record
  while the first fix took ~40 s to arrive; the rerun from 90 % ran clean.

  The screen-off numbers are the first phone baseline for BG (release 1.3).

#### BT4 — the night chart (M; approved by Chris 2026-09-25)

Chris wants it for its own sake and for the 2.5D view, so it is a feature rather than a
battery experiment, and it does not wait on BT1's numbers.

- A dark variant of the pack style (`style-dark.json` beside `style.json`, the CyclOSM-derived
  sheet the pack draws with: same sources and layers; dark land and water, light roads and
  text, halos inverted), inside the supported GL subset (`port/Osm/styles/style-readme.md`).
  Sprites that are dark-on-transparent get light twins or a tint; `ignored_icons()` must stay as
  it is for the day sheet.
- The pack names both (`[osm] style` and `style_dark`); `stage_data.py` stages and checks both.
- It follows the system appearance, with a map-menu override: Auto / Light / Dark. A switch
  invalidates the base cache and DR1's underlay. The route red, the ship, the pick ring and the
  guidance banner are checked against both; the SwiftUI controls already adapt (system
  materials).
- Each style also names the colours the 2.5D view needs (sky, horizon fade), read from the
  style's `background` layer or a small `metadata` block, so PV2 draws a night sky over a night
  chart without a second source of truth.
- It relaxes this plan's "the map itself has one look in v1", which was a v1 scope rule.
- Acceptance: screenshots of the same view in both; MetricKit's pixel luminance (BT1) falls on a
  night ride.

Screen-off riding is not a BT step: it is BG1–BG5 (optionally BG6), and it is the largest saving
on this list.

### DR — dragging without blank edges, and a fling (added 2026-09-25)

Chris, 2026-09-25: the blank edges that show as the map slides and turns, and no inertia — a
fast drag and release should keep scrolling and slow to a stop, as Apple's own apps do.

**What the code does today** (read 2026-09-25, not yet measured on the phone):

- Nothing sits behind the two map layers (`MapScreen.mapLayer`: base, overlay, clipped), so
  whatever the base does not cover shows the window background.
- **At rest there is no band.** When the map stops under a resampled frame, `MapModel.settle`
  redraws the base with `bandMargin: 0` — pixel-exact, and it hands the band's memory back
  (P18). So every drag starts with a base exactly the size of the screen, and the first point of
  movement uncovers an edge until the gesture's first band draw returns (25–33 ms at riding
  zooms, 44–74 ms at wide views).
- **During a drag the band is thin and stops being rebuilt.** A 0.25 margin is about 98 pt
  left and right and 213 pt top and bottom on a 393×852 pt screen; a flick crosses 98 pt in
  about 50 ms. And `tick()`'s live-render budget refuses every base draw after the gesture's
  first when the last one took over `liveRenderBudgetMs` (40 ms), so at a wide view a long drag
  never rebuilds the band and slides into blank until the finger lifts.
- **Turning uncovers the corners.** The band is the screen grown on each side, and a turned
  screen's corners reach out to the half-diagonal (about 469 pt). With this band they come out
  after about 14° of turn. Both the two-finger rotate (after its 12° dead zone) and course-up
  follow turn the chart, and `base_cache_max_turn_deg = 2.5` forces redraws the gesture budget
  then refuses.
- **No inertia.** `MapGestureView.handlePan` forwards deltas while `.changed` and ignores the
  recognizer's velocity at `.ended`.

#### DR1 — an underlay, so the edge is never blank (M, sim)

- The style's background colour (land) is painted behind everything, so even the worst case is
  land-coloured rather than white or black.
- A third layer under the base: the map drawn two zoom levels out (scale ×4) at screen size —
  one screen of pixels covering four screens of ground in each axis — under its own preview
  transform, exactly as the base and overlay layers are (`MapScreen.preview(of:in:)`). Where it
  shows it is soft, but it is the map, correctly placed. It covers any rotation (four screen
  widths is more than the diagonal) and about a screen and a half of pan in any direction.
- Built at low priority when the loop is idle (after the settle), never ahead of a live frame:
  the render queue is serial and a 50–70 ms wide render must not delay a gesture's first frame.
  Rebuilt when the camera leaves its middle half or the scale moves more than 2× from its key;
  invalidated by a style switch (BT4) or a symbol-size change (P22); dropped on a memory
  warning. Memory: one screen (about 12 MB at 1206×2622).
- The coverage rule joins `PPBaseCoverage.h` (mac-tested): which of sharp base, underlay or
  background colour covers each corner of the live screen.
- `-PPShowStats` gains a "soft frames" count: frames where some of the screen was served by the
  underlay rather than the sharp base. It is the number DR2 drives down.
- Acceptance: a screen recording of a fast drag at a wide view and a two-finger turn shows no
  blank; the soft-frame count is recorded as DR2's baseline.
- This underlay is also the two-tier far field PV5 needs for the 2.5D view.

**Built 2026-09-25** (simulator; phone acceptance pending).

- `PPBaseCoverage.h`: `kUnderlayZoomOut = 4`, `LayerReaches`, `ScreenCover
  {kSharp,kUnderlay,kBackground}`, `ScreenCoverage(base, underlay*, live)` (corner test, worst of
  four; scale/turn not refused, unlike `BaseCovers`), `UnderlayServes(underlay, live)` (stale when
  the live centre leaves the underlay's middle half, the scale moves >2x from its key, or the
  surface or pitch changes; rotation never stales it). 9 new mac gtests in
  `test/base_coverage_test.cpp`; `fv_pippin_test` 128/128 green.
- `PPViewport`: `viewportForUnderlay` (scale x4, NOT clamped to zoom limits), `underlayServes(_:)`,
  `coverage(of:underlay:)` returning `PPScreenCover`.
- `PPMap`: `PPUnderlay` (image, viewport, background colour at the live scale, render
  milliseconds); `renderUnderlay(for:)` draws through a second `VectorRenderer`
  (`_underlayRenderer`, scene margin 0) over the shared source and style so the base renderer's
  retained scene is not evicted; a local `CpuCanvas` per build so only one screen (~12 MB image)
  is held at rest; symbology drawn at DPI / 4 so after the x4 magnification lines and labels match
  the sharp map's size (first sim run at full DPI showed labels 4x oversized).
- `MapModel`: `underlay` + `mapBackground` published; `pauseOrBuildUnderlay` replaces the two
  `link.isPaused = true` sites in `tick()` — builds only where the loop would otherwise pause,
  never during a gesture, one-at-a-time; `underlayKey` stops a failed build retrying every tick;
  `underlayGeneration` discards a build that finishes after a drop; dropped on memory warning and
  on a symbol-size change. `-PPShowStats` gains "N soft M blank · under X ms".
- `MapScreen`: the style background colour behind all layers, replacing the grey placeholder;
  underlay layer under the base with the same `preview(of:in:)` transform.
- Sim results (iPhone 17 Pro sim, Kiawah pack, at 1:96,161, near the widest view): underlay draw
  21–31 ms; fast drags gave 1–5 soft frames and 0 blank; the sea beyond the pack's data edge is
  the style background, drawn identically by the sharp base, so it is not a blank. Known trade:
  the render queue is serial, so a gesture that starts during an underlay build waits up to that
  build's time (~20–30 ms sim) for its first frame.
- Still open: the acceptance screen recording of a fast wide drag and a two-finger turn (the
  simulator's synthesized two-finger path registered as a pinch, not a rotate), a phone check, and
  DR2's soft-frame baseline to be recorded on the phone.

#### DR2 — the band ready before the finger moves, and ahead of it (M, sim + phone)

- **Keep the band at rest.** A band whose margin is a whole number of device pixels, drawn at
  the live camera, should composite pixel-exact at zero offset. P18 assumed it could not ("its
  pixels land between the screen's"); measure it with `PPPixelProbe` and `-PPViewportProbe`. If
  it holds, the settle draws with the band, `isLive` accepts a band at an integral offset, and
  every drag starts covered. The price is the band's memory held at rest (2.25× the screen),
  which reverses P18's choice deliberately; the memory-warning drop stays.
- **Lead the finger.** During a pan, draw the band for where the camera will be when the draw
  lands (the current centre plus the pan velocity times the last base-draw time), not where it
  is now. `covers` is unchanged: it is still a band.
- **Replace the gesture budget.** Instead of refusing base draws after the first, start one
  whenever the screen, predicted one draw-time ahead, comes within `[display]
  band_refresh_fraction` of the band's edge. Still at most one in flight.
- **A rotation-safe band** while the chart is turned or turning (the rotate gesture, course-up
  follow): a square whose side is the screen's diagonal plus the margin — about 2.6× the
  screen's pixels against 2.25× today — so a turn up to `base_cache_max_turn_deg` never shows a
  corner.
- Acceptance: DR1's soft-frame count reaches zero for drags and turns at riding zooms and is
  brief at the widest view; BT1's energy table shows no regression.

**Built 2026-09-25** (simulator; phone acceptance pending).

- `PPBaseCoverage.h`: `GrownSurfaceSize` replaced by `GrownSurfacePixels(w,h,margin,rotation_safe)`
  — grows by whole, equal device pixels per side (the old band was 1769 px wide, 1179×1.5
  rounded, a half-pixel off-centre, which is why P18 believed a band could not be shown
  unfiltered). `PixelAligned(base, live)` (0.1 px tolerance: the projection's x scale follows the
  centre latitude, so a N-S pan drifts ~0.002 px per 40 px at riding scale; a 600 px N-S pan at
  1:96,161 exceeds it and is refused, so the settle redraws). `BandHeadroomPx(base, live)`.
  `BandLead(v, seconds, margin_pt, 0.75)`. 8 new mac gtests; `fv_pippin_test` 135/135.
- `PPViewport`: `grown(byMargin:rotationSafe:)`, `isPixelAligned(to:)`, `bandHeadroom(for:)`, C
  function `PPBandLead`.
- `PPMap`: `render(_:bandMargin:reuseBase:)` replaced by `render(_:band:reuseBase:)` — the shell
  passes the band viewport (possibly led, possibly rotation-safe, nil = screen); the style is
  prepared for the band's own centre. New `baseCacheRefreshFraction` from `[display]
  band_refresh_fraction` (0.5, added to `pippin.ini`).
- `MapModel`: settle now draws the resting band at the live camera (not `bandMargin` 0);
  `settleOwed` (replacing `baseIsResampled`) = base not pixel-aligned OR not the resting band's
  size (covers the first frame, after a pinch, led and rotation-safe bands). During a same-scale
  gesture a base redraw starts when the cached band no longer covers the live camera, or the
  camera predicted one base-draw ahead (pan velocity × `lastBaseDrawMs`) has headroom less than
  `fraction × margin × short side px`; the redraw is centred ahead by `BandLead`. The 40 ms
  live-render budget now applies to pinches only. Pan velocity comes from
  `UIPanGestureRecognizer.velocity(in:)` via `MapGestureView.onPan`.
- `MapScreen`: a layer is shown unfiltered when `isPixelAligned(to:)` (was: same size and zero
  offset).
- Decisions, deviating from the plan text above: (a) the rotation-safe band is each axis reaching
  the diagonal plus HALF the short-side margin — about 3.2x screen pixels, not the plan's 2.6x,
  because the diagonal alone leaves 43 pt of vertical headroom, under the 49 pt refresh threshold;
  (b) it is used only during a two-finger turn, not in course-up follow — follow redraws at the
  live rotation every 2.5°, so its relative turn stays a few degrees and the normal band covers
  it, and a 3.2x band on every follow redraw would regress BT1's energy.
- Sim measurements (iPhone 17 Pro sim, Kiawah): resting frame vs. the pre-DR2 build at the home
  view — 4,636 of 3,162,132 px differ (0.15%), all at the screen's outer edge (the screen-sized
  canvas used to clip these, now whole from the band) plus the status-bar clock; the interior is
  bit-identical, so the band composites pixel-exact at rest. A 500 pt/s 580 ms drag at 1:28,538: 0
  soft, 0 blank. Flicks at ~1,500–5,000 pt/s: soft (underlay) frames, 0 blank (base draw on sim
  97–112 ms at 1:28,538, 47 ms at 1:96,161). A ~80° two-finger turn in 0.5 s (the sim registered
  it as a rotate this time): 0 blank, 7 soft ticks; the rotation-safe band costs 232 ms per draw
  on the sim.
- Still open: phone acceptance (soft-frame count to zero at riding zooms, BT1 energy table no
  regression, the rotation-safe draw time on the phone); a pinch out from 1:1,614 to 1:28,538
  produced 4 blank frames — the underlay (DR1) is stale during a fast pinch-out, DR1's layer, not
  the band; the held band is still retained by the displayed `PPFrame` after a memory-warning
  drop (pre-existing from P18).

#### DR3 — the fling (S–M, sim + phone)

- On pan `.ended`, read `velocity(in:)`. Above `[display] fling_min_speed_pt` (about 200 pt/s),
  start a coast that decays the way `UIScrollView` does: `v(t) = v0·e^(−t/τ)`, with `τ` from
  `[display] fling_deceleration` given in UIKit's own unit (0.998 per millisecond,
  `DecelerationRate.normal`, so τ ≈ 0.5 s and a fling travels about `v0 × 0.5 s`).
- The position is the closed form `v0·τ·(1 − e^(−t/τ))`, and each display-link frame pans by
  the difference since the last one — frame-rate independent, so BT2's pacing cannot change how
  far a fling goes. The coast ends when the speed drops below 10 pt/s, or when the centre clamp
  at the pack's edge bites (it stops; there is no rubber band).
- Pans go through `MapModel.pan(by:)` as a drag does, so they are rotation-aware for free.
- A touch stops the coast dead, and that touch is spent on stopping it: it does not also select
  a marker or start a hold. This is what Apple's scroll views do, and a stopping tap that
  opened a point sheet would be a bug.
- The coast is part of the gesture: `gestureActive` stays true until it ends, so DR2's band
  rules and BT2's gesture frame rate apply, and the settle waits for it.
- **No fling while following.** In GPS mode a drag is honoured and the next fix pulls the map
  back (the slew re-bases; see "The map is asked where it is" in the README), so a coast would
  only fight the follow camera for a second.
- Optional in the same step, off by default: a short zoom coast after a pinch and a rotation
  coast after a turn, each with its own key.
- The math is `Momentum.swift`, CoreGraphics-only, with `test/momentum_test.swift` under the
  swiftc harness: the total distance is `v0·τ`; 60 Hz, 20 Hz and jittered frames reach the same
  position at the same time; the stop threshold; the clamp.
- Acceptance on the phone: a flick coasts and eases to a stop like Maps; a tap stops it without
  selecting anything; the edges stay filled during a coast at riding zoom.

**Built 2026-09-25** (simulator; phone acceptance pending).

- `Momentum.swift` (CoreGraphics-only): `Momentum(velocity:decelerationRate:minSpeed:)` — nil
  below `minSpeed` (never below the 10 pt/s stop speed); τ = −0.001/ln(rate) (0.998/ms → τ ≈
  0.4995 s); a rate outside (0,1) falls back to 0.998; `offset(at:)` = v0·τ·(1−e^(−t/τ)),
  `velocity(at:)`, `duration` = τ·ln(v0/10), `limit` = v0·τ, `clampBit(requested:applied:)` (>0.5 pt
  short = the pack-edge centre clamp bit). `Coast` steps it per display frame from the closed
  form, so the sum of the steps equals the curve at any frame rate.
- `test/momentum_test.swift` + `run_momentum_test.sh`, ctest `pippin_momentum`: 27 checks —
  threshold, curve (v0·τ total, 1−1/e at τ), UIKit `.fast` rate, stop at 10 pt/s, 60 Hz / 20 Hz /
  jittered frames reach the same position (1e-6 pt), repeated timestamp, clamp.
- `MapGestureView`: `onEnded` now carries the pan's release velocity (`velocity(in:)` at
  `.ended`, only when the pan is the last recognizer to end); `CoastStopRecognizer` recognizes at
  touch-down during a coast and fails at once otherwise; the tap and the long press
  `require(toFail:)` it, so a stopping touch neither selects a marker nor starts a hold; the pan
  is left free so the stopping finger can drag.
- `MapModel`: `gestureEnded(releaseVelocity:)` starts a `Coast` (not in GPS mode) and keeps
  `gestureActive` true, so DR2's band rules/lead and BT2's gesture frame rate apply and the
  settle waits; `advanceCoast()` at the top of `tick()` pans by each step with the decaying
  velocity as the band's lead; stops on finish, clamp bite, GPS mode, `stopCoast()` from a touch,
  a new gesture, a shared-place move, or a search framing.
- `pippin.ini [display]`: `fling_min_speed_pt = 200`, `fling_deceleration = 0.998`; PPMap
  `flingMinSpeedPoints`, `flingDecelerationRate`.
- Not built: the optional zoom coast after a pinch and rotation coast after a turn (plan said
  optional, off by default) — left for later.
- Sim results (iPhone 17 Pro): after a flick the map kept moving (1.26 M px changed in the 0.4 s
  after release) and was still afterwards; a tap during a coast stopped it (0 px change over 0.5 s
  after the tap, where an unstopped coast would still be at ~140 pt/s); a tap on a marker at rest
  still opens its sheet (Retts Bluff); three coasts at 1:96,161 counted 0 soft 0 blank.
- Still open: phone acceptance — the flick eases to a stop like Maps; a tap stops it without
  selecting anything (the tap-on-a-marker-during-a-coast case was not exercised on the sim); edges
  stay filled during a coast at riding zoom.

### TD — tides (item 2)

**What exists.**

- Every NOAA tide-prediction station within 14 km of Kiawah is a *subordinate* station of
  Charleston, Cooper River Entrance (8665530): Kiawah River Bridge 8667062 (5.8 km; high +14 min
  ×1.07, low +6 min ×0.89), Snake Island 8666767, the Bohicket Creek stations, Folly River Bridge
  and others. A subordinate station has high and low predictions only, made by applying those
  offsets to the reference station's extremes. Charleston's 37 harmonic constituents are
  published (metres, GMT phases) by the CO-OPS metadata API.
- Libraries: XTide is GPL-3.0, so linking it into the LGPL framework would make the combined work
  GPL, and it is far more than this needs. pytides and UTide are MIT but Python — good test
  oracles, not shippable. Neither CoreLocation nor MapKit publishes tides.
- The harmonic method itself is public domain (Schureman, *Manual of Harmonic Analysis and
  Prediction of Tides*, USC&GS SP-98), about 600 lines with node factors. A US pack does not
  need it.

**Decided here: the pack carries NOAA's own predictions**, fetched when the pack is staged. The
network is used at build time only and the app stays offline. A semidiurnal station has about
1,400 extremes a year; five years is about 7,000 rows. Heights between extremes use NOAA's cosine
interpolation, which is how NOAA draws a subordinate station's curve. There is no astronomy code,
and the numbers are NOAA's by construction. The cost is that the table ends: the pack records
`valid_until`, and the app says so rather than extrapolating. A harmonic engine is TD5, optional,
for a pack NOAA does not cover.

Predictions are astronomical only; onshore wind and storms raise the water above them. The card
says so once, in small type.

#### TD1 — the table (M, mac)

- `port/tools/fetch_tides.py <station> <years> <out>`: CO-OPS `datagetter` with
  `product=predictions&interval=hilo&datum=MLLW&time_zone=gmt&units=metric`, one request per
  year; station metadata (name, position, reference station, offsets) from `mdapi`. Writes
  `tides.json`: a station block, `datum`, `units`, `valid_from`/`valid_until`, and
  `extremes: [[unix_s, height_m, "H"|"L"], …]`. `stage_data.py` stages it; `pippin.ini` gains
  `[tides] file` and `station`, and `[beach] rideable_below_m`.
- `port/include/fvkit/nav/tide.h` + `port/fvkit/nav/tide.cpp`: `fv::nav::TideTable` — `Load`,
  `HeightAt(t)`, `Trend(t)` (rising or falling, and the rate), `Extremes(t0, t1)`,
  `WindowsBelow(threshold, t0, t1)` solved on each half-cycle rather than sampled, `ValidUntil()`.
  Times are Unix seconds; the C++ knows no time zones (the shell formats).
- Tests: the curve passes exactly through every extreme; `WindowsBelow` agrees with 1-second
  sampling; a time outside the table is `kOutOfRange`, never a clamp; the interpolation error
  against NOAA's 6-minute Charleston predictions over one month (fetched once, committed as a
  fixture) is measured and pinned — expect about 0.1 m at mid-tide.

**BUILT 2026-09-25.** `fetch_tides.py`, `fvkit/nav/tide.h`/`.cpp`, and the `pippin.ini`/
`stage_data.py` wiring built as specified above, with two departures: outside
`[valid_from, valid_until)` the table returns the existing `kOutOfCoverage` rather than a new
`kOutOfRange` (no status code was added), and the measured interpolation error against the
committed Charleston March-2026 fixture came in at max 0.137 m / RMS 0.051 m (pinned in
`nav_tide_test.cpp`) against the ~0.1 m expected above. `testdata/tides/8667062.json`
(git-ignored, Kiawah River Bridge, 2026–2030, 7072 extremes) is the working table. 13 new
gtests, all green. Full detail in `port/PORTING.md`'s pippin-plan row.

#### TD2 — the tide card (S, sim)

- `PPTide` over `TideTable`, and `TideCard.swift`: a Swift Charts curve from three hours back to
  a day ahead with a "now" mark, the next two extremes as text ("Low 2:14 pm, 0.2 ft"), the
  rideable band shaded, units from `DisplayUnits`, times in the device's zone. Opened from the
  map menu ("Tides") and, after BR4, from the route sheet's beach row.
- Within 60 days of `valid_until` the card says when the table ends; past it, the card says the
  table has ended and TD3's verdict is "unknown", never a guess.
- Acceptance: a screenshot whose times match NOAA's published table for the day.

**BUILT 2026-09-26.** `PPTide`/`PPTide+Internal.h`/`PPTide.mm` (station id/name, datum,
validFrom/validUntil, rideableBelowMeters, `height(at:)` NaN outside coverage, `extremesFrom:to:`,
`windowsBelow:from:to:` clipped to the valid span) over `TideTable`; `PPMap.tide` (nullable)
loaded at init from `[tides] file` and `[beach] rideable_below_m` — a named table that fails to
load is logged and leaves `tide` nil rather than failing the pack. `TideText.swift` (Foundation-
only formatting: heights ft to 0.1 / m to 0.01, clock in the device zone with weekday for another
day, "Low 2:14 pm, 0.2 ft", the beach line, and the coverage notice within 60 days of
`valid_until` and after it) and `TideCard.swift` (Swift Charts curve three hours back to a day
ahead, now mark, rideable band shaded, next two extremes, beach line, NOAA/astronomical-only
footnote). Menu gains "Tides" (hidden when the pack has no table); sheet opens from MapScreen.
Departure from plan: the route-sheet entry point still waits on BR4 as planned, but TD3's verdict
does not exist yet, so the "ended" notice says the beach verdict is unknown rather than naming it.
Station name shows as NOAA publishes it (upper case). one new mac ctest (`pippin_tide_text`, 18 checks),
green; full ctest green serially — `OsmNameIndex.*` (6 tests) fail only under `-j8`, a pre-existing
parallel-fixture clash unrelated to TD2. Acceptance screenshot
`port/apps/Pippin/docs/td2-tide-card.png` matches NOAA's table for 8667062. Next is TD3, the
beach verdict.

#### TD3 — the beach verdict (S, mac)

- Beside TD1: `BeachTideVerdict(stretches, table, depart_s, threshold)` → for each beach stretch,
  the highest water over [depart + enter, depart + exit] and a verdict — good, marginal (within
  `[beach] marginal_band_m`), poor, or unknown — plus the next window that would make it good.
  The stretches come from BR3.
- The gate (revised 2026-09-26, see BR "Decided here"): `RoutePlanner::Plan` takes the table and
  a departure time; a stretch that is poor or unknown triggers the second, beach-excluded plan.
  `RoutePlan` gains `beach_dropped`: `none`, `high` (water above the threshold when the rider
  would reach the stretch), `rising` (below it on arrival, above it before the stretch ends plus
  `exit_margin_s`), or `no_table` (outside the table's coverage). With it, the time the beach next
  becomes passable, from `WindowsBelow`.
- Tests on synthetic tables and on the real one: the same two stops at low water keep the beach,
  at high water drop it with `high`, and just before the flood with `rising`; Never never runs
  the second plan.

**BUILT 2026-09-26.** `fv::nav::BeachTideVerdicts(stretches, table, depart_s, limits)` in
`fvkit/nav/beach.h`/`.cpp`, beside `tide.h` but not in it — fvkit does not link RouteKit, so it
takes plain `BeachStretchTiming`. Verdict good/marginal/poor/unknown over
`[enter, exit + exit_margin_s]`; each carries enter/exit times, `enter_height_m`,
`peak_m`/`peak_at_s` (the peak falls at an endpoint or a high water, since each half-cycle is
monotonic), `covered_at_s` (rising case), and `passable_from_s`/`good_from_s` (earliest entry at
or after arrival with a long enough window, searched within `search_horizon_s`, 48 h default; NaN
means none). `BeachTideLimits` defaults `rideable_below_m` 0.5, `marginal_band_m` 0.15,
`exit_margin_s` 600; `pippin.ini`'s `[beach]` gains `marginal_band_m` and `exit_margin_s` as
placeholder numbers for Chris to set — PPMap doesn't read them yet, that's BR4.
`RoutePlanOptions::tide` (`BeachTideGate {enabled, table, depart_s, limits}`) is off by default;
`Plan` is `PlanOnce` plus the gate, and a poor or unknown stretch triggers a second plan with
`BeachUse::kNever`. `RoutePlan` gains `beach_verdicts` (parallel to `beach`), `beach_dropped`
(`kNone`/`kHigh`/`kRising`/`kNoTable`), and `beach_dropped_verdict`. Gate enabled with a null
table drops the beach as `kNoTable`; gate disabled skips the check, leaving fvgraph and older
callers unchanged — BR4 still has to decide whether a pack with a beach but no tide table turns
the gate on (decided in BR4: see below). A marginal stretch is kept; the status line is unchanged.
8 new gtests in `nav_beach_test.cpp` (a synthetic closed-form table plus the Kiawah table), 6 new in
`route_planner_test.cpp` (low water keeps the beach, high water gives `kHigh`, just before the
flood gives `kRising` at 7200 s, no table and past the table's end give `kNoTable`, Never isn't
gated, the Kiawah table at low and high), all green.

#### Optional

- **TD4, "leave at"**: a departure time in the route sheet that re-runs TD3, and "best times
  today" in the card.
- **TD5, the harmonic engine — SKIPPED 2026-09-26 (Chris).** Kiawah River Bridge is a
  subordinate station: NOAA publishes only its highs and lows and draws the curve between them
  with the same cosine interpolation TD1 uses, so a harmonic engine would reproduce the table's
  extremes at best (±3 cm, ±3 min) and still interpolate between them. The error it could remove
  is TD1's measured cosine-vs-harmonic gap at Charleston, max 0.137 m / RMS 0.051 m, which at the
  ~0.5 m/h mid-tide rate moves a ride-window edge ~6 min typically and ~16 min at worst — inside
  `exit_margin_s` (600 s) and below wind set-up (0.1–0.3 m). Worth building only for a pack NOAA
  does not predict.
- **SUN, sunrise and sunset on the tide card — BUILT 2026-09-26.** `fvkit/nav/sun.{h,cpp}`:
  `SunEvents(lat, lon, t0, t1)`, NOAA's solar-calculator method refined once at each event, zenith
  90.833°, nothing on a polar day or night. Offline, so it never expires and works for any pack.
  `nav_sun_test.cpp` (5 tests) pins it against the US Naval Observatory's rise/set API (fetched
  2026-09-26): Kiawah at both equinoxes and solstices, Fairbanks at midsummer, Sydney at
  midsummer — every case within 44 s of USNO's minute-rounded times. `PPTide.sunEvents(from:to:)`
  answers at the tide station's position (5.8 km from the island centre: seconds of difference)
  and is not clipped to the table's span. The card gains "Sunset 7:11 pm · Sunrise Sun 7:12 am"
  under the next high and low, and the chart shades each night grey
  (`docs/sun-tide-card.png`); the wording and night spans are `TideText.sun`/`TideText.nights`,
  covered in `tide_text_test.swift`. Not done: darkness in the beach verdict or the route sheet
  (a rideable window that falls entirely at night reads the same as a daytime one).
- **WX, wind — BUILT 2026-09-26** (Chris chose api.weather.gov, and the card line plus the
  route-sheet warning; the wind does not change the plan). `fvkit/nav/wind.{h,cpp}`:
  `WindForecast::Parse` reads a gridpoint document's `windSpeed`/`windGust`/`windDirection`
  series (km/h or m/s, ISO 8601 intervals, null hours as gaps), `At(t)`, and the components
  `TailwindComponent`, `OnshoreComponent`, `SeawardOf` (the perpendicular nearer the beach's
  facing, so a curving beach keeps the sea on the right side) and `TrueBearingDeg`. 8 gtests on
  a committed, trimmed real response (`nav/test/data/nws-chs-82-68-2026-09-26.json`).
  `pippin::RouteSnapshot::beach_headings_deg` gives each stretch's net heading start to end;
  `PPBeachStretch.headingDegrees`, `PPWindForecast`/`PPWindSample`/`PPWindSettings` in
  `PippinKit/PPWind.{h,mm}`. `pippin.ini`: `[beach] faces_deg = 167` (the graph keeps no
  coastline side), `[wind] lat/lon` — a fixed point on the beach, never the rider's position —
  `onshore_warn_mps = 7`, `headwind_warn_mps = 5`. `WindFeed` (Swift) fetches `/points` once
  (the grid URL is remembered), then the grid, with the User-Agent NWS requires; kept an hour,
  cached on disk and used offline, on demand only when the tide card or the route sheet of a
  beach pack opens. Tide card: "Wind 7 mph, gusts 12, from the NW, across the beach" (within 60°
  of square to the beach is "across", else "a tailwind heading east/west"), the onshore warning,
  and "Wind: National Weather Service forecast, updated …" in the footnote. Route sheet: the
  worst headwind at each stretch's entry time if it reaches the limit, else the best tailwind,
  plus the onshore warning. `WindText` wording covered by `wind_text_test.swift`
  (`pippin_wind_text`). Sim, live NWS: card and cache verified against the raw response
  (11.1 km/h from 320° = 7 mph across); Boardwalk 29→41 departing 2026-09-27 15:02 EDT read
  "Tailwind on the beach, 12 mph, gusts 17." — 250° at 12 mph, 5.11 m/s along 077
  (`docs/wx-route-sheet.png`). Not done: the onshore warning has not been seen live (unit-tested
  only); wind does not change the ETA or the tide verdict.

### BR — the beach as a route (item 1)

**What the data says** (the four `testdata/OSM/map*.osm` extracts, measured 2026-09-25):

- The beach is seven `natural=beach` closed ways (1087613906, 945874155, 1087613640–46, 114127800
  "Kiawah Beachwalker Park", …) plus several `natural=sand` closed ways with no `golf` tag. About
  170 other `natural=sand` ways are golf bunkers (`golf=bunker`) and must be left out. No beach is
  a multipolygon relation.
- The water line is `natural=coastline` (128740584, 23.5 km; 191636480, 5.1 km), which OSM maps
  at mean high water. The beach polygons share 88 nodes with it: the seaward edge of the beach is
  the coastline way.
- Access: 14 highway ways share a node with a beach polygon, all `highway=path` and mostly the
  numbered boardwalks ("Boardwalk 29", "30", "31", "33", "35", "39", "40", "41"); about 40 more
  path and footway ends lie within 20 m. Some carry `bicycle=yes`; Boardwalk 38 is
  `access=private`.
- The graph admits `highway=*` ways and `route=ferry`. `natural=*` never reaches it, and the
  router has no notion of an area.

**Decided here.**

- **Synthesise ways; don't route areas.** General area routing (a visibility graph over the
  polygon, as OpenTripPlanner does for plazas) solves a harder problem than this one. The ride
  runs along the water's edge, and that line is already in the data as the coastline. The
  builder turns "coastline where it borders a beach" into arcs, and joins each boardwalk end to
  it with a short straight arc across the dry sand.
- **Two classes, appended** to `RoadClass` after `kFerry`, so older files read unchanged:
  `kBeach`, the firm sand along the coastline, and `kBeachAccess`, the dry-sand crossing from
  where a path ends. They differ in the two things a class decides: speed (firm sand at low tide
  rides like a path; dry sand is walking the bike) and whether the tide matters (only the run
  along the water is tide-gated).
- **Excluded unless asked for**, in the ferry's shape: `RouteProfile::beach_penalty` defaults to
  exclude, `RouteOptions::beach_penalty` is the per-query override, `SelectProfile` mirrors it.
  The default has to live on the penalty and not in the class lists: `foot` has
  `"unlisted_classes": 1.0`, so as soon as beach arcs exist every walking route would take the
  sand if exclusion were left to the class table.
- **Revised 2026-09-26 (Chris): a three-position preference, not a toggle.** "Use beach:" is
  **Never** / **To Save Time** / **Whenever Possible**. The preference is a reluctance on the beach
  arcs' time cost: Never excludes them (the default), To Save Time costs them at their true
  seconds (×1.0, so the beach is taken only when it is faster), Whenever Possible discounts them
  (×`beach_prefer_factor`, starting at 0.5, so a detour of up to ~2× stays on the sand). Chris
  sets the numbers from scenarios; they live in `route-weights.json`, not in code. The tide is
  never part of the preference: it decides whether a beach stretch is passable (a hard gate no
  setting overrides) and, later, how fast it rides. Wind, if WX ever lands, is a speed term, not a
  second control. Precedents: Valhalla's `use_ferry`/`use_trails` (0–1 preferences) and
  OpenTripPlanner's `bikeReluctance`.
- **The tide gate is a second plan, not a time-dependent search.** Plan with the beach admitted;
  TD3 times each beach stretch from the route's own per-arc seconds; if any stretch is not
  passable (above `rideable_below_m`, or rising through it before the stretch ends, with
  `[beach] exit_margin_s` of slack), plan again with the beach excluded and keep that route. The
  result carries *why* the beach was dropped, and the sheet says so (BR4). A time-dependent
  Dijkstra (arc cost evaluated at arrival time) would find a beach route that a different
  approach makes passable; that is optional and only worth it if riding shows the two-plan answer
  missing good routes.
- **The choice is saved in the document.** `.fvrte` gains `"options": {"beach": "time"}`
  (`"time"` or `"prefer"`; absent means never), written
  only when set, so every existing document stays byte-identical to route.py's output (P5's
  claim); route.py learns to read and write it in the same step. The rejected alternative is
  profile variants (`bicycle_beach` via `extends`): no format change, but every later option
  (EL4's "avoid hills") doubles the profile list, and the sheet would be mapping a grid of
  toggles onto names.

#### BR1 — the beach in the graph (M–L, mac)

- `RoadGraphBuildOptions::beaches`, off by default; `fvgraph build --beach`. Collect closed ways
  tagged `natural=beach`, or `natural=sand` with no `golf=*`, and every `natural=coastline` way.
  Check what the builder's passes retain today: these ways' node coordinates must survive to the
  synthesis step, and they are not road nodes.
- Runs: walk each coastline way. A node is on the beach when it is shared with a beach polygon or
  within `beach_line_snap_m` (15 m) of one's boundary; consecutive on-beach nodes form a run.
  Bridge a gap between runs on the same coastline shorter than `beach_gap_m` (60 m): mappers split
  a beach into pieces, and a real inlet is wider.
- Access: a highway way *end* inside a beach polygon or within `beach_access_snap_m` (25 m) of its
  boundary gets a `kBeachAccess` arc to the nearest point on a run, splitting the run there with
  a new graph node. The arc inherits the access bits of the way it extends, so Boardwalk 38 stays
  private. A path that crosses the beach attaches at the node it shares with the polygon.
- Names: a run takes the `name` of the polygon it borders, else "Beach"; access arcs are unnamed.
  GD1 then says "right onto Beach", and P20's road search lists it, which is correct.
- Synthetic nodes get negative `osm_id`s. Multipolygon beaches are counted
  (`beach_relations_skipped`) and not built; Kiawah has none.
- `IsCycleable` true and `IsDriveable` false for both classes; `RoadClassName` "beach" and
  "beach_access". `fvgraph` prints beach polygons, coastline ways, runs, run length, access arcs.
- Tests: a synthetic fixture — one polygon, a coastline sharing its seaward edge, a path ending
  10 m inside and one ending 30 m outside — gives exactly one run and one access arc. The Kiawah
  extracts give an access count close to the boardwalk count above, a run length within a few
  percent of the coastline inside the beach, and a route Boardwalk 29 → Boardwalk 41 that exists
  when beaches are allowed and not otherwise. `road_graph_test.cpp`, `router_test.cpp`.
- Rebuild `testdata/OSM/kiawah.fvroad` with `--beach` and update the README's data-pack row. Every
  pinned route (the Ruddy Turnstone fixtures above all) must be unchanged, because the beach is
  excluded by default.

**BUILT 2026-09-26.** `RoadClass::kBeach`/`kBeachAccess` (10 and 4 km/h class defaults, cycleable,
not driveable, reachable by name only); `RoadGraphBuildOptions::beaches` plus the four distances;
`port/Routing/fv_road_graph_beach.{h,cpp}` does the synthesis; `fvgraph build --beach`, a beach
line in `fvgraph info`, and `pyfvw.RoadGraph.build(beaches=)`. On the Kiawah extracts: 15 beach
polygons, 2 coastline ways, 2 runs, 19.03 km along the water, 79 access arcs (median 46 m, longest
195 m), none out of reach. Walking Boardwalk 29 → Boardwalk 41 is 3.99 km on the sand against
5.16 km by road. 7 new gtests. Departures:
- Adjacent on-beach coastline nodes always join. Kiawah's coastline has ~200 m between nodes, so
  `beach_gap_m` applies only across off-beach nodes.
- A run vertex at a coastline node keeps that node's OSM id; only split points are negative.
- Access candidates are way ends near a polygon *and* any way node the polygon shares. A node
  reached by several ways takes the most open of their access bits. `beach_access_max_m`
  (200 m) caps an access arc, and the ones it refuses are counted.
- A run takes the name most of it borders, with unnamed polygons voting, so the 19 km run is
  "Beach" and not "Kiawah Beachwalker Park".
- **The `kiawah.fvroad` rebuild moves to BR2.** Until `beach_penalty` excludes the beach by
  default, `foot`'s `"unlisted_classes": 1.0` would send walking routes onto the sand.

#### BR2 — the beach in the rules (M, mac)

- `beach_penalty` in the rule-file grammar (a number, or `false`/"exclude"), on `RouteProfile`
  and `RouteOptions`, mirrored by `SelectProfile`; `AvoidFactor`/`ArcUsable` bar both classes
  when it excludes.
- Per-class speed, because dry sand at 4 km/h is physics rather than preference: an optional
  `"class_kph": {"beach": 10, "beach_access": 4}` per profile, applied in `ProfileSeconds` (where
  the ferry's own clock already lives), so the cost and the reported seconds agree.
- `route-weights.json`: `bicycle` and `foot` get weights and speeds for both classes and
  `"beach_penalty": "exclude"`, with comments saying what the numbers mean. The pack uses the
  same file (`pippin.ini` `rules`).
- The preference: `RouteOptions::beach` is `kNever | kToSaveTime | kWheneverPossible`; the
  profile carries `beach_prefer_factor` (default 0.5, must be in (0, 1]) applied as a multiplier
  on `kBeach` arc cost only when `kWheneverPossible` — access arcs are not discounted, so the
  preference never pulls a route across dry sand for its own sake. `beach_penalty` stays the
  profile-level default (exclude).
- Tests: excluded by default for every profile, `foot` included; the override admits them;
  on a fixture where the beach is slightly slower than the road, To Save Time takes the road and
  Whenever Possible takes the beach; where it is faster, both take it;
  reported seconds use the class speeds; a misspelt key is still an error.

**BUILT 2026-09-26.** `RouteOptions::beach` is `BeachUse { kProfileDefault, kNever, kToSaveTime,
kWheneverPossible }`; `kProfileDefault` defers to `RouteOptions::beach_penalty`, a multiplier on
both beach classes or `kAvoidExcluded`, defaulting to excluded so a graph built with beaches
routes like one without until asked. `kNever` excludes; `kToSaveTime` costs both classes ×1.0;
`kWheneverPossible` multiplies `kBeach` arcs only by `beach_prefer_factor` — access arcs are never
discounted. `BeachFactor()` sits public beside `ArcUsable()`; `AvoidFactor`/`ArcUsable` both read
it. Rule-file grammar: `beach_penalty` (same shape as toll/ferry, but ABSENT means exclude rather
than 1.0 — deliberate, since `foot`'s `unlisted_classes: 1.0` would otherwise walk people onto the
sand), `beach_prefer_factor` in (0, 1], and per-profile `class_kph` overlaying key by key, applied
in `RouteProfile::Seconds` via `ProfileSeconds` so cost and reported seconds agree. Builtin rules
and the shipped `route-weights.json`: bicycle beach 0.65/0.75, beach_access 1.5/1.1, class_kph
beach 10 / access 4 km/h; foot class_kph beach 5 / access 3 km/h; both profiles exclude by default,
prefer factor 0.5. `fvgraph route --beach never|time|prefer`; `fvgraph profiles` prints the beach
default. `testdata/OSM/kiawah.fvroad` rebuilt with `--beach` from `~/Downloads/kiawah-260906.osm.pbf`
(418 KB, 3433 nodes / 8132 arcs, 4 runs, 25.53 km along the water, 80 access arcs — a rebuild from
the same source is byte-identical); the README's data-pack row was wrong about the source (claimed
the `map*.osm` extracts) and is corrected. Full ctest green on the rebuilt graph, so every pinned
route is unchanged. By bike, Boardwalk 29 → Boardwalk 41 is 4.23 km / 21.2 min by road at Never
and To Save Time (the sand would be 24.4 min) and 3.99 km, 3.69 km of it beach, at Whenever
Possible. 7 new gtests in `route_rules_test.cpp`; `router_test`'s Kiawah boardwalk test
now also checks that unasked (with and without the `foot` profile) it routes like the plain graph.
No departures of substance: `RouteOptions::beach_penalty` is kept as the per-query override
alongside the enum, with `kProfileDefault` meaning "use `beach_penalty`". `pyfvw` did not gain a
beach argument — BR3 wires `route.py`. Next is TD3, the beach verdict.

#### BR3 — the beach in the document and the plan (M, mac)

- `RouteDoc` reads and writes `options.beach` (`"time"`/`"prefer"`; absent means never; the key
  position decided once and matched in both writers); `apps/route.py` does the same and passes it
  to the router. The existing round-trip fixtures stay byte-exact; a new fixture carries the key.
- `RoutePlanner::Plan` applies it. `RoutePlan` gains `beach` stretches — `{geometry_begin,
  geometry_end, length_m, enter_s, exit_s}` over contiguous `kBeach` arcs (access arcs are not
  tide-gated), timed from the route's own per-arc seconds.
- `pippin::RouteStore` saves the option and replans with it at launch; the relaunch test
  (`ARouteSurvivesARelaunchWITHItsRoads`) gets a beach twin.

**BUILT 2026-09-26.** `fv::RouteBeach { kNever, kToSaveTime, kWheneverPossible }` in
`fv_route_doc.h` (Routing-free); `.fvrte` gains an `options` object `{"beach": "time"|"prefer"}`,
written after `profile` and only when not never, so existing documents stay byte-identical.
Version stays 1 — a reader that ignores the key routes without the beach. Absent key reads as
never and RESETS on write (unlike name/profile, which keep their prior value when absent); an
unknown spelling also reads as never. Departure: the plan named `apps/route.py` as the writer,
but that module has had no reader/writer since the RouteKit move — the Python side is
`pyfvw.route`: `RouteOverlay.beach` and `RoutePlanOptions.beach` as strings
`'never'/'time'/'prefer'` (`''` on options means the route's own), `RoutePlan.beach` a list of
`RouteBeachStretch`. `RoutePlanOptions::beach` (`routing::BeachUse`, default `kProfileDefault`) is
passed to the router in `BuildOptions`; `RouteOverlay::FollowRoads` fills it from the document
when `kProfileDefault`, like the profile; `ToBeachUse()` maps the doc enum. `RoutePlan::beach` is
`RouteBeachStretch { geometry_begin, geometry_end, start_m, length_m, enter_s, exit_s }`, derived
in the planner from `Route::legs` of class "beach" (no router change), merged across adjacent
beach legs since legs split on name too; through routes only, like maneuvers. Departure: added
`start_m`, the index-free position, because `RoutePath`'s duplicate-dropping could in principle
drift indices. `pippin::RouteStore::SetWaypoints(waypoints, profile, std::optional<RouteBeach>
beach = nullopt)` (`nullopt` keeps the document's), `RouteSnapshot::beach` and `beach_stretches`.
On the Boardwalk 29 → 41 fixture by bike, Whenever Possible gives one beach stretch of 3.69 km
(3695 m), entered 52 s in, left at 1382 s; To Save Time and Never take no beach. New fixture
`port/Routing/rules/Boardwalk 29 to Boardwalk 41 by the beach.fvrte`
(`FV_ROUTE_BEACH_FIXTURE_FILE`), written by Python `json.dump`. 6 new gtests (3 RouteDoc, 2
RoutePlanner, 1 RouteStore — `ARouteSurvivesARelaunchWITHItsBeach`); all route tests green. Next
is BR4, the beach in the route sheet.

#### BR4 — the beach in the route sheet (M, sim)

- `PPMap.beachAvailable` (the graph has any `kBeach` arc), `setRouteWaypoints:profile:options:`,
  `PPRoute.beachStretches`.
- Under the Walk/Cycle picker, a three-position slider "Use beach:" — Never / To Save Time /
  Whenever Possible — shown only when the pack has a beach, where Apple Maps puts "Avoid tolls".
- When the setting admits the beach but TD3's gate dropped it, the sheet says why, in one line
  under the slider: "Beach not used — the tide is high (4.8 ft). Rideable again from 1:40 pm."
  or "Beach not used — the tide is rising and covers the beach by 3:10 pm." or "Beach not used —
  the tide table has ended." Required for Whenever Possible, where the rider expects sand; shown
  for To Save Time too, because the rider would otherwise not know the tide was the reason. No
  message when the beach was simply slower. Tapping it opens TD2's card.
- Its footnote is the tide ("Low 2:14 pm · good until 4:05 pm")
  and, once the route uses the beach, TD3's verdict on it ("The beach stretch is at 3.1 ft at
  5:20 pm — soft sand likely"). Tapping the footnote opens TD2's card.
- A new route starts with the last value chosen (a user default); an existing route shows its
  own.
- The route summary says how much is beach ("includes 4.2 km of beach").
- Acceptance: simulator screenshots of the same two stops at Never and Whenever Possible, the
  second taking the beach with the verdict visible, and a third at high water showing the
  "Beach not used" line.

**BUILT 2026-09-26.** Decided the open question from TD3: with no tide table, or a departure past
the table's end, the beach is **allowed, not blocked** — a route stays usable if the app outlives
its table, and the sheet warns instead. `BeachTideGate::keep_unknown` (default true) in
`fv_route_planner.h`; `false` gives TD3's old behaviour (drop as `kNoTable`), which is what TD3's
own tests pass. New test `RoutePlanner.TheTideGateKeepsAnUnknownStretchByDefault`.
`pippin::RouteStore` gains `SetTide(table, limits)`, `set_clock()`, `BeachAvailable()`; every plan
runs the gate with departure = now; `RouteSnapshot` gains `beach_verdicts`, `beach_dropped`,
`beach_dropped_verdict`, `depart_s`. New test `RouteStore.TheTideGatesTheBeachAndNoTableKeepsIt`.
PippinKit: `PPBeachUse`/`PPBeachVerdict`/`PPBeachDropped` enums, `PPBeachStretch`;
`PPRoute.beachUse/beachStretches/beachMeters/beachDropped/droppedStretch/departTime`; `PPMap
setRouteWaypoints:profile:beachUse:`, `beachAvailable`, `routeDepartureOverride`. `PPMap` now reads
`[beach] marginal_band_m`/`exit_margin_s` from `pippin.ini` and hands the table to the store in
`loadTides`. Departure from the plan: the three-position control is a **menu picker, not a
slider/segmented control** — "Whenever Possible" truncates in a segmented control at phone width.
The "Use beach" row sits under Walk/Cycle, shown only when the graph has beach arcs. Footer: the
"Beach not used — …" line (high/rising/table ended) when it applies, else the verdict on the
worst stretch ("The beach stretch is at 0.5 ft at 3:35 pm — firm sand."/"— soft sand likely.", or
unknown: "No tide table for this area. Check the water…" / "The tide table has ended…"), then the
tide now ("Low 3:14 pm, 0.4 ft · Beach rideable until 5:02 pm"); tapping the footer opens TD2's
tide card over the sheet. Status line appends "includes 2.3 mi of beach". A new route starts with
the last chosen value (`UserDefaults` `routeBeachUse`); an existing route shows its own. The tide
card's "ended" notice now says beach routes are still offered. Wording lives in `TideText`
(Foundation-only), 9 new checks in `test/tide_text_test.swift`. A DEBUG-only `-PPDepartAt <epoch>`
launch argument (`TideClock`) judges the tide at a chosen time, used for the acceptance shots.
Acceptance screenshots: `docs/br4-beach-low-water.png` (Boardwalk 29→41 by bike, Whenever
Possible, departing 2026-09-27 15:02: 2.5 mi, 24 min, includes 2.3 mi of beach, firm sand),
`br4-beach-never.png` (2.6 mi, 21 min, no beach), `br4-beach-high-water.png` (departing 08:45,
"Beach not used — the tide is high (6.8 ft). Rideable again from 1:25 pm."). Full ctest green
(2201). `RouteRules.WheneverPossibleDoesNotDiscountTheDrySand` failed once under `-j8` and passed
on rerun and at HEAD — a flake noted, not investigated. Next is BR5, riding the beach.

#### BR5 — riding the beach (M, sim + phone)

- The follow-mode snap: the snap network's `kCycleable` filter (`IsCycleable` plus the access
  bit) admits beach arcs after BR1, which is right — the snap says where the rider is, not what
  the route prefers. Verify a ride along the water snaps to the run rather than a dune-side path,
  and a rider on Beachwalker Drive is not pulled onto the sand.
- Guidance: turns onto and off the beach read sensibly ("right onto Beach", "left onto Boardwalk
  29"), and `end_margin_m` does not swallow a short access arc.
- The route overlay draws beach stretches with a sand-coloured casing (`RouteOverlay` gains
  per-range styling fed from `RoutePlan.beach`), so the rider can see which part is sand.
- A simulated ride along the water with `port/tools/make_sim_gpx.py` (Boardwalk 29 → the beach →
  Boardwalk 41): guidance, snap and trip computer end to end on the simulator, then ridden.

**BUILT 2026-09-26.** Snap: verified, no code change — Pippin's follow-mode snap uses
`RoadSnapFilter::kAll` (`PPRouteStore::EnsureRoadNetwork`), not `kCycleable` as this plan said;
beach arcs are admitted either way. A ride along the Boardwalk 29→41 beach stretch (924 fixes at
4 m spacing, ±8 m noise, 4 m/s) snaps 924/924 to `kBeach` arcs; the same resampling along
Beachwalker Drive (273 fixes) always snaps to residential/tertiary/service, never beach or
beach_access. Guidance: the synthesized `kBeachAccess` arcs are unnamed, so the turn list read
"slight right" onto an unnamed connector then "left onto Beach" 37 m later, and "left" with no
name on exit. Fixed in `port/RouteKit/fv_route_maneuvers.cpp` `ManeuverShapeOf`: each
`beach_access` leg is folded into a neighbour, preferring a non-beach one, earlier first — the
route now reads depart Boardwalk 29 · left onto Beach at 101 m · left onto Boardwalk 41 at 3796 m
· arrive. `end_margin_m`: Kiawah access arcs run 10.5–194.5 m (5 of 80 under the 20 m margin);
`fvkit/nav/maneuver.cpp`'s `kDepart` now names the last road joined inside `end_margin_m` (the
road the rider sets off on) rather than the first leg, so a start beside the sand departs "onto
Beach" instead of silently dropping the turn — documented on `ManeuverSettings::end_margin_m`.
Overlay: `RouteOverlay` draws the plan's beach stretches with `kBeachCasingColor` {200,155,70}
(ochre, darker than the style's pale `landcover_sand` fill) instead of the white casing, legs
split at stretch boundaries (`SplitAtBeach`). New `port/RouteKit/test/route_beach_ride_test.cpp`,
7 tests (turn list on the real route, two synthetic fold cases, start-beside-the-sand depart,
snap along the water, Beachwalker Drive, sand casing pixel count 5907 vs 0 with Never);
`nav_maneuver_test`'s `CornersOnTopOfEitherEnd` re-pinned to depart road "Long Rd". Full ctest
green: 2226 registered, 2209 passed, 0 failed, 17 disabled, 3 skipped. Simulator: iOS-sim core +
Pippin built, `Boardwalk 29 to Boardwalk 41 by the beach.fvrte` as current.fvrte, `-PPDepartAt`
2026-09-27 15:02 EDT, `simctl location start --speed=5` along the plan's 34 points. Banner on
Boardwalk 29 showed "left · 90 yd · Beach", snap "Boardwalk 29". On the sand the snap read "Beach" and the banner "left · 2.3 mi · Boardwalk 41" (`docs/br5-on-the-beach.png`, the ochre casing visible); 150 yd before the exit, still "Beach" (`docs/br5-leaving-the-beach.png`); past it the snap read "Boardwalk 41" and the banner the arrival. The rider was jumped from the beach entry to 280 m before the exit, so the trip computer was not checked over one continuous ride. `make_sim_gpx.py` converts a recorded track, so the ride was
driven from the plan's own points instead.
Still open: the ride on the phone (Chris).

### EL — elevation in the router (item 3)

**What exists.**

- `IElevationSource` (`fvkit/formats/source.h`), the DTED reader, and
  `analysis::SampleTerrainProfile` (AN2): sampling a path at post spacing is built and tested.
- DTED in `testdata/dted/`: level 2 over Kiawah (`w081/n32.dt2`, `w080/n32.dt2`), a flat-island
  sanity check, and level 1 over Atlanta (`w085/n33.dt1` covers the city, 85–84 W;
  `W084/n33.dt1` the eastern suburbs). **Atlanta is the hilly test area** (Chris, 2026-09-25: the
  next place he wants to map), and Chris is downloading DTED level 2 over it before EL starts.
  Note the directory case: `W084` is upper case and every other directory lower, which macOS
  forgives and a case-sensitive Linux checkout will not (an open Peregrine follow-up).
- Everything else an Atlanta pack needs is on disk: `testdata/OSM/us-south-260728.osm.pbf`
  (Geofabrik's US South, which includes Georgia) and `testdata/OSM/mbtiles/us-south.mbtiles`
  (bounds reach 40.6 N; Kiawah's tiles were cut from it). The router reads `.osm.pbf` directly,
  and `osmium` is installed for the cut.
- An undirected edge is two mirrored arcs, one in each endpoint's adjacency, so each stored arc
  has a direction and can carry its own climb and descent.
- `fv_routing` links nothing from fvkit (the invariant RouteKit exists for). The `.fvroad` header
  has a reserved `flags` word and is at version 2.
- **The reverse frontier costs the wrong direction once costs are asymmetric.** In the
  bidirectional search (`fv_router.cpp`, the expansion loop around line 676) the backward frontier
  standing at `u` relaxes arc `a` of `u`, stored `u → target`, while the travel it represents is
  `target → u`. With today's symmetric costs that is exact. With climb it prices every backward
  step with the wrong sign of slope. EL2 fixes it first, and the plain-Dijkstra reference
  (`bidirectional = false`) is the test that proves it.

#### EL1 — elevation in the graph (M, mac)

- A D6-style seam on the builder: `RoadGraphBuildOptions::elevation =
  std::function<bool(double lat, double lon, float* metres)>`. `fvgraph build --dem <dir>` wires
  fvkit's DTED source into it; the tool links fvkit, `fv_routing` still does not.
- Per arc: sample the geometry at the DEM's post spacing, clamped to 10–30 m; apply a hysteresis of
  `elevation_hysteresis_m` (3 m, the rule GPS apps use for total ascent, which removes DEM noise);
  sum climb and descent in the arc's own direction. The mirror gets the swapped pair.
- Bridges and tunnels mislead a DEM. The OSM reader keeps `bridge`, `tunnel` and `layer`, and
  those arcs interpolate linearly between their end nodes (the Kiawah causeway is the local
  case).
- Per node: an elevation, for EL3's chart.
- Format: version 3, header `flags` bit 0 = elevation present, then a trailing section of
  per-arc `uint16 climb_dm, descent_dm` and per-node `int16 elev_dm`. Versions 1 and 2 still load,
  without elevation. `RoadGraph::has_elevation()`.
- Tests: a synthetic DEM — a plane of known grade, and a sinusoid with ±2 m noise — gives the
  exact climb on the plane and rejects the noise; a bridge over a synthetic valley counts no
  climb; the Kiawah graph over the dt2 cells has a total climb near zero (pinned); a small
  Atlanta cut over the Atlanta cells (dt2 once Chris has them, dt1 until then) has climbs
  plausible for its streets, spot-checked against two known hills.

#### EL2 — elevation in the cost (M, mac)

- The direction fix first: `ArcCost(arc, options, reversed)`, the reverse frontier passes `true`
  and the climb/descent pair swaps; `Materialize` uses the traversal direction. Test: on a hilly
  synthetic graph, bidirectional and plain Dijkstra agree on route and cost for 100 random pairs.
  That test must fail before the fix.
- The model, per profile in the rule file; the defaults are the textbook rules and every number
  is a setting:
  - `climb_s_per_m`: seconds added per metre climbed. 6.0 on foot (Naismith: an hour per 600 m);
    6.0 on a bike (about the extra time ~150 W takes to lift 90 kg one metre).
  - `descent_s_per_m`: seconds saved per metre descended on gentle grades, 2.0 for both
    (Langmuir's correction), never faster than `max_kph`.
  - `steep_grade` / `steep_descent_s_per_m`: above 12% a descent costs time instead (+2.0):
    steep descents are slow on foot and braked on a bike.
  - `hill_penalty`: a preference multiplier on the climb seconds in the cost only, never in the
    reported clock. 1.0 is neutral; EL4's "avoid hills" sets it high.
- The physics goes in `ProfileSeconds` (costed and reported alike) and `hill_penalty` in
  `ArcCost` (costed only) — the same split as speeds against class weights.
- An arc's cost is floored at its length over `max_kph`. A descent credit that drove a cost to
  zero or below would break Dijkstra.
- A graph with no elevation behaves exactly as today: every pinned route unchanged.

#### EL3 — the route's profile (S–M, mac then sim)

- `RoutePlan` gains `climb_m`, `descent_m` and an elevation series (distance along, elevation)
  from the node elevations.
- The route sheet shows "↑ 120 m ↓ 85 m" and a small Swift Charts profile under the summary when
  the graph has elevation. On a flat pack it shows nothing, not a flat line.

#### EL4 — "Avoid hills" (S, sim)

- `options.hills: "avoid"` in `.fvrte` (BR3's object; absent is neutral) sets `hill_penalty` from
  `[routing] avoid_hills_penalty`. A toggle under "Use the beach", shown only when the graph has
  elevation.

#### EL5 — the Atlanta pack (M, data + sim)

Atlanta is where Pippin goes next, so this is the second region as well as the test of EL2's
numbers.

- The box is Chris's to name (the BeltLine and intown neighbourhoods are the obvious first cut;
  the metro is far larger than Kiawah and the graph and tiles grow with it).
- The graph: `osmium extract -b <box>` from `us-south-260728.osm.pbf`, then `fvgraph build
  --dem testdata/dted` (no `--beach`: Atlanta has none).
- The tiles: `port/tools/mbtiles_cut.py` from `us-south.mbtiles`, the way P1 cut Kiawah. No
  tilemaker run is needed.
- The DEM: Chris's DTED level 2 cells over the box.
- The pack: `stage_data.py` takes a region (`--region kiawah|atlanta`); the rows that differ
  (graph, tiles, points seed, `pippin.ini`'s home view, `search.initial_text`) move into a
  per-region table, and the Kiawah rows stay byte-for-byte what they are. One pack per build, as
  today.
- An Atlanta build exercises every "shown only when the pack has it" rule in this plan: no beach
  row, no tide card, the hills toggle and elevation profile present.
- Acceptance: a bike route across intown Atlanta that takes a longer, flatter line with "Avoid
  hills" on, and an ETA that tracks a real or simulated ride.

**Pack half BUILT 2026-10-04; elevation still open (EL1–EL4), so acceptance above is not met.**
Chris's box, ±10 miles: `-84.61,33.67,-84.25,33.97` (W,S,E,N). `osmium extract -b <box>` from
`us-south-260728.osm.pbf` gave `testdata/OSM/atlanta.osm.pbf` (31 MB); `fvgraph build` over it
(access honoured, no `--beach`, no `--dem` — no DEM yet) gave `atlanta.fvroad` (29 MB, 305,222
nodes / 800,560 arcs). The tiles did **not** come from `mbtiles_cut.py` as planned: tilemaker
with `config-peregrine.json`/`process-peregrine.lua` and `--bbox` over the osmium cut gave
`atlanta.mbtiles` (23 MB, 424 tiles z0–z14), because the Kiawah Trails style needs the peregrine
profile's tags, not a tile-level cut — the same pipeline as `kiawah.mbtiles` since 2026-08-27.
(`/usr/local/bin/tilemaker` is broken — a dyld error, `boost_program_options` missing after
Homebrew's boost 1.92 — the build at `~/Documents/Source/tilemaker/tilemaker` works.)
`stage_data.py` gained `--region kiawah|atlanta` (default kiawah): COMMON rows plus a REGIONS
table; Atlanta's `pippin.ini` is the tracked one with its keys reset for Atlanta and no
`[tides]`/`[beach]`/demo_track; wind at downtown; `pick_radius_m` 30; `search.initial_text`
"BeltLine". The Atlanta seed `atlanta.fvpoints` is the icon palette with 0 points. Switching
regions drops the other region's copied rows but never a `.fvpoints` seed (`REGION_SEEDS`, kept
by `--release` pruning too). Kiawah's pack verified byte-identical to before; Atlanta's pack is
about 52 MB. `Pippin.xcconfig` gained `PP_BUNDLE_ID` (default `org.peregrine.Pippin`); the app
and share-extension targets use `$(PP_BUNDLE_ID)`/`$(PP_BUNDLE_ID).Share`, so
`PP_BUNDLE_ID=org.peregrine.Pippin.Atlanta PP_DISPLAY_NAME=Pippin` on the `xcodebuild` line
installs beside the "Bike Kiawah" store build — verified with `-showBuildSettings` on both
targets. README gained "Another region" under "The data pack"; `BUILDING.md` has the Atlanta recipe
under Step 2, "Another region". Still open: both apps claim the
`pippin://` URL scheme, so share-from-Maps is ambiguous with both installed;
`MapModel.swift:1134` still says "outside the Kiawah map"; the app has not been built or run on
the Atlanta pack. Chris cleared the Atlanta box for the public Peregrine repo 2026-10-04.

#### RG1 — more than one region in one app (optional; needs Chris)

Once Atlanta exists, a phone that goes to both places wants both packs. Sketch only: the bundle
(or `Documents/`, for a pack added later) holds several packs; the app opens the one whose bounds
contain the last fix, with a map-menu picker to override; `PPMap` is rebuilt on a switch, and the
route, points and rides documents are kept per pack. Not planned in detail until Chris wants it.

### PV — the 2.5D view (item 7, stretch)

**Why it is cheaper than it sounds.**

- A flat map seen in perspective is a projective transform (a homography) of the top-down
  picture. Pippin already draws that picture and composites it on the GPU under a 2D transform
  (P18: `MapScreen.layer`, scale, turn and offset, no blitter). A tilt is a 3D transform on the
  same layer — `.projectionEffect(ProjectionTransform(CATransform3D))` with a perspective term.
  No 3D renderer, no Metal, no new raster path.
- `PPViewport` is the camera, and every geo↔screen conversion goes through it (P3's rule): picks,
  the crosshair, snap, `RenderGate`, `LocationPolicy`. Teaching it the homography teaches all of
  them.
- The cost is area. At a 40° vertical field of view, the top-down picture that covers the visible
  ground is about 1.5× the screen at 30° pitch, 2.6× at 45°, and 7.5–9× at 60° (computed for the
  iPhone 14 Pro screen, 2026-09-25). P18's band is already 2.25×. So **the pitch is capped at
  45°**; past that a CPU raster pays for ground nobody can read.

**Decided 2026-10-04 (Chris):** follow-only 2.5D for 1.3 — the view pitches only while following
and goes flat when a drag leaves follow — capped at 45°. Upright points of interest are wanted, so
BT3 is no longer optional.

**Order, revised 2026-10-04:** PV1 → BT3 → PV2 → PV4 → PV5; PV3 reduced to the follow toggle;
BT4 does not block PV2 (at 45° + 20° the view never reaches the horizon, so there is no sky, only a
fade, which takes the style's background colour until BT4 exists). BT3 moves ahead of PV2 because
the per-frame overlay must cover the tilted footprint too: at 4.7 frames/s while following, a
2.9–4× overlay raster per frame is the real cost, while the base redraws only ~23 times a minute.

#### PV1 — the math (M, mac)

- `PPPerspective.h`, pure C++ like `PPCameraFit.h`: from surface size, pitch, field of view and
  the anchor (where the ship sits, about 70% down the screen in follow), the homography H
  (top-down surface → screen) and its inverse; the visible ground trapezoid clipped at
  `max_depth` screens; the horizon line when the view reaches it; the top-down rectangle to render,
  in the rotated frame so course-up costs nothing extra.
- `PPBaseCoverage` generalised: coverage maps the trapezoid's corners, not the screen's.
- Tests: inverse round trips; pitch 0 reduces exactly to today's transform; footprint ratios
  match the table above.

**Built 2026-10-04** (mac).

- `PippinKit/PPPerspective.h`: `Homography` (apply, adjugate inverse), `Perspective(params)` with
  `FlatToScreen`/`ScreenToFlat`, `GroundQuad`, `RenderBounds(pad)`, `DepthScale(row)` for
  billboard sizing, `HorizonY`. "Flat" is today's viewport surface, rotation included. The tilt
  is pinned at the anchor (it maps to itself, horizontal scale 1), so the ship keeps its pitch-0
  detail; pitch 0 is the identity bit for bit. No `max_depth`: at the 45° cap the quad is bounded.
- `PPBaseCoverage.h`: `BaseCovers` and `ScreenCoverage` take an optional ground quad
  (`LiveCorners`); `BandHeadroomPx` still uses the screen rectangle (PV2).
- **The footprint table above was centre-anchored.** Holding the detail at the follow anchor
  (5/6 down, `kTrackUpAheadFraction`) costs more, on the 1179×2556 screen at 40° fov:

  | Pitch | Bounding box | Ground quad | Road ahead of the ship vs flat |
  |---|---|---|---|
  | 30° | 1.99× | 1.64× | 1.46× |
  | 45° | 3.96× | 2.90× | 2.22× |

  Levers for PV2, to measure on the phone: clip the base draw to the quad (bounding box →
  quad), a narrower fov, the ship higher than 5/6 once pitch supplies the look-ahead, or the
  far part from the DR1 underlay (PV5).
- 8 gtests in `test/perspective_test.cpp`; `fv_pippin_test` 145/145.

#### PV2 — the tilted composite (M, sim)

- `PPViewport.pitchDegrees` (0 leaves every path as it is today); the render request uses PV1's
  rectangle; `MapScreen` applies the projection to the base, overlay and underlay (DR1) layers; a
  sky gradient above the horizon and a fade over the last screen of depth, both in the active
  style's colours (BT4), so a night chart gets a night sky.
- Behind a launch argument (`-PPPitch 45`) until PV3 gives it a gesture.
- Acceptance: screenshots at 0, 30 and 45° following the demo feed; the cache hit rate in follow
  reported against P18's ~89%.

**Built 2026-10-04** (simulator; phone acceptance pending).

- `PPViewport.pitchDegrees` + `pitched(_:)` (clamped 0–45; every derivation keeps it; bands and
  the underlay are flat, pitch 0). Anchor is the follow anchor: x centre, y = (h−1)(0.5 +
  kTrackUpAheadFraction), i.e. 5/6 down. New: `perspectiveTransform` (CATransform3D in points, H
  conjugated by the backing scale; identity built by hand because PippinKit does not link
  QuartzCore), `screenPoint(forFlat:)`, `flatPoint(forScreen:)`, `depthScale(atScreen:)`,
  `farFade`, `flatBounds`; internal `perspective` and `groundQuadX:y:`.
- A tilted `grown(byMargin:)` returns a flat band cut to `Perspective::BandRect(pad)` — the
  ground quad's bounds padded by margin × the short side, united with the screen so it stays
  pixel-aligned (settle logic unchanged) — off-centre, centre set after derivation so the
  pack-box clamp can't move it. `rotationSafe` not applied when tilted. PPMap's unbanded draw of
  a tilted viewport uses `grown(byMargin: 0)`.
- `covers`, `coverage(of:underlay:)`, `bandHeadroom(for:)` and PPMap's cache test pass the ground
  quad when tilted; `BandHeadroomPx` now takes the optional quad (the PV1 note "BandHeadroomPx
  still uses the screen rectangle (PV2)" is now resolved).
- `PPPerspective.h`: `BandRect(pad)` and `FarFade()` — a gradient in the style's background
  colour over the last screen height of flat depth, capped at a third of the screen, top opacity
  2×(1 − DepthScale(row 0)) clamped to 1 (≈0.98 at 45°, end row 798 of 2556 on the 14 Pro). No
  sky: at 45° + 20° half-fov the horizon is never on screen; BT4 will supply the colours.
- `MapScreen`: underlay, base and overlay sit in one flat ZStack under a `Tilt` modifier;
  `projectionEffect` draws only inside the view's frame, so the frame is grown about the centre
  to `flatBounds` and the transform conjugated by the offset (first try clipped the far ground
  at the flat screen's top edge). Background, fade and ship are screen space. Ship placed
  through H, heading mapped through H, scaled by depth (1 at the anchor). Tilted layers always
  filtered (`.medium`).
- `MapModel`: `-PPPitch <deg>` launch argument (`followPitchDegrees`), applied by `applyPitch()`
  only in GPS mode and not during a pick (flattens for pick, restores after). Taps, waypoint
  grab/drag/drop and pinch anchors convert screen → flat. A follow step now also requires the
  cached base to cover the live (tilted) view (`covers(vp, maxTurnDegrees: 180)`), else it draws:
  a few degrees of course-up turn swept the far corners past the band and 38 blank frames showed
  in under a minute before this.
- Sim acceptance (iPhone 18 Pro, Atlanta pack, `simctl location` 8 m/s north up Peachtree, 1:7,233,
  ~50 s following each): base hits 0° 728/775 (94%), 30° 697/745 (94%), 45° 393/421 (93%), vs
  P18's ~89%. Blank/soft: 0° 0/6, 30° 6/19, 45° 0/8 (soft frames come from the mode switch).
  Screenshots `docs/pv2-pitch-0.png`, `pv2-pitch-30.png`, `pv2-pitch-45.png`.
- gtests: 2 new in `test/perspective_test.cpp` (BandRect holds quad + screen; far fade) plus
  headroom-over-quad assertions; `fv_pippin_test` 147/147.
- Still open / found: (a) the underlay is north-up at 4 screen widths, so with the chart turned
  near 90° the tilted far corners pass its short axis and show background — PV5's far-field work,
  recorded under PV5 too; (b) pre-existing, not PV2: a pinch while in GPS mode leaves the camera
  off the ship (reproduced flat, 19 blank frames) — needs its own item; (c) pre-existing: a saved
  Kiawah `current.fvrte` on the Atlanta build makes GPS mode's zoom-out rule widen to 1:2,338,555
  (the sim's copy was set aside as `current.fvrte.kiawah-aside`); (d) PV3 still owns
  flatten-on-drag; pans in tilted GPS mode are applied as flat deltas.

#### PV3 — gestures and picks through the homography (M, sim)

- Pan keeps the ground under the finger; pinch zooms about the ground point under the centroid;
  a two-finger vertical drag changes pitch (Apple Maps' gesture); the compass button also resets
  pitch. DR3's fling coasts in screen space and each frame's delta goes through H⁻¹ like a drag,
  so a coast slows naturally as it runs towards the horizon.
- Pick-on-map (P6/P19) goes flat while picking: a crosshair over a foreshortened map is
  imprecise.
- `-PPViewportProbe` gains pitched checks against P3's drift budget.
- GPS mode pitches to `[display] follow_pitch_deg` (default 0, off), with a map-menu toggle "3D
  while following".
- **Reduced 2026-10-04:** for 1.3 only the toggle and the flatten-on-drag; free 3D pan, pinch and
  the pitch gesture wait until asked for.

**Built 2026-10-04** (simulator; phone acceptance pending).

- `[display] follow_pitch_deg` pack key (default 45, the cap) in `pippin.ini`, read by
  `PPMap.followPitchDegrees` and snapshotted on the Swift `Renderer`. Decision: the key is the
  angle; whether 3D is on is the rider's choice, not the pack's (the plan's "default 0, off"
  became angle 45 in the pack, toggle off by default).
- Map menu toggle "3D While Following" (`view.3d` symbol) → `MapModel.follow3D`, persisted in
  user defaults `PPFollow3D`, off by default. `-PPPitch <deg>` still works: turns it on at that
  angle (overrides the pack key).
  **Removed 2026-10-05 (Chris): following always tilts.** The map menu has no 3D option and
  `PPFollow3D` is no longer read; GPS mode always tilts to `display.follow_pitch_deg`
  (`-PPPitch` still overrides the angle).
  **Changed same day (Chris): course-up only.** The tilt applies only in GPS mode with
  course-up; north-up following is flat. `MapModel.applyPitch()` requires `courseUp`, and
  `setCourseUp(_:)` re-applies the pitch, so the compass toggle tilts/flattens at once.
  `pippin.ini`'s `follow_pitch_deg` comment says so.
- Flatten-on-drag: the first `pan` of a gesture while tilted sets `flattenedForDrag` and
  flattens; `endGesture` clears it and re-tilts; the next fix brings the camera back to the ship.
  Pinch and picks unchanged (pick already flat since PV2). Resolves PV2's open item (d).
- Sim acceptance (iPhone 18 Pro, Atlanta pack, `simctl location` 8 m/s north up -84.387): toggle
  shows a checkmark when on; GPS mode tilts to 45°; a screenshot mid-drag shows the chart flat
  and panned; after release it re-tilts with the ship back at the anchor; relaunch comes up
  tilted (persisted).
- Found: at the launch scale 1:240,401 (the zoom-out rule never narrows on Atlanta), tilted
  follow shows many blank frames — 51 blank/32 soft in 38 s with no touch at all, base dropping
  z11→z10 — so it is not the drag; PV2's 0/8 was measured at 1:7,233. For PV5's list.
- Not done (deferred by the reduction): free tilted pan through H⁻¹, pinch about the ground
  point, the pitch gesture, the compass resetting pitch, `-PPViewportProbe` pitched checks.
- No new gtests (Swift/ObjC++ only; no C++ core change).

#### PV4 — upright symbols (M, sim; after BT3)

- On a tilted plane the route line is right and everything else is wrong: the ship, point
  markers, waypoint diamonds and turn arrow lie flat and foreshorten. The overlay splits: ground
  (route lines) stays in the tilted layer; billboards (ship, markers, diamonds) become sprites
  placed at H(project(geo)) in screen space — BT3's ship sprite, generalised. `RouteOverlay` and
  `PointOverlay` gain a lines-only / markers-only draw switch, or the shell draws markers from the
  overlays' data.

**Built 2026-10-04** (simulator; phone acceptance pending).

- Decision: the plan's first option, a draw switch on the overlays, plus a per-marker stamp
  (BT3's `DrawSymbolAt` pattern). fvkit `PointOverlay` and RouteKit `RouteOverlay` gain
  `SetDrawMarkers(bool)` (off: `OnDraw` records its projection / places waypoints for the hit
  test but stamps no markers; route still draws its line and status) and
  `DrawMarkerAt(proj, canvas, index, x, y)`; each `OnDraw`'s per-marker body moved into a private
  `DrawMarker` so `OnDraw` and the stamp share one code path (output unchanged — existing goldens
  untouched). `PointOverlay::Ink` replaces the local lambda; `RouteOverlay::MarkerColor` the
  inline ternary.
- `PippinKit/PPSprite.h` (pure C++): `InkBounds` + `Crop` to trim a stamped marker to its ink.
- `PPMap`: when the live viewport is tilted the cached overlay is drawn with markers off
  (`_overlayUpright` joins the cache key, so tilting/flattening redraws it once); `PPFrame.billboards`
  (new `PPBillboard`: coordinate, trimmed CGImage, pixelsPerPoint, centerOffset from marker centre
  to image centre in points) carries route waypoints then points, rebuilt only when `_contentEpoch`
  moves; a hidden overlay contributes none. Each sprite is stamped at the centre of a canvas sized
  for symbol + estimated label width, then trimmed. Flat frames carry no billboards and are
  unchanged.
- `MapScreen`: billboards placed between the tilted layers and the far fade, at H(project(geo)),
  scaled by `depthScale`, offset by `centerOffset × depth`, off-screen ones skipped, no hit testing
  (picks still go screen→flat→overlay; markers are centred on their points so the pick agrees).
- Sim acceptance (iPhone 18 Pro, Atlanta pack, test route of 3 waypoints + 3 test points on
  Peachtree, `simctl location` 8 m/s north, 45°): flat view unchanged; tilted at 1:19,025
  course-up the diamonds, square, circle and names are upright, sit on their ground points (via
  diamond on the route line) and shrink with depth. Screenshot `docs/pv4-pitch-45.png`. Sim data
  restored afterwards.
- Tests: 4 new core gtests (PointOverlay ×2, RouteOverlay ×2: markers-off draws nothing/only the
  line and the pick still works; `DrawMarkerAt` is byte-identical to `OnDraw`'s stamp incl.
  selection, dimming, dpi scale, label) and 3 in `test/sprite_test.cpp`; `fv_pippin_test` 150/150.
- Found again (not PV4): a pinch in GPS mode leaves follow with the camera off the ship (PV2's
  item b); 15–22 blank frames at the launch scale 1:240,401 (PV3's finding, already under PV5).

#### PV5 — polish (M, sim + phone)

- Far-field quality: minified ground shimmers. Try `.interpolation(.high)` first. The two-tier
  answer is already built by then: DR1's underlay is the map two zoom levels out, so the far
  part of the footprint can come from it and the sharp band need only reach the middle
  distance, which also cuts PV's 2.6× area cost.
- Base-map labels lie on the plane and stay readable to about 40°, which is part of why the cap
  is 45°. Upright base-map labels would need the style engine to emit labels as billboards; out
  of scope unless asked for.
- BT1's table for 2.5D follow against flat follow. If the difference is large, 2.5D stays a
  manual choice rather than GPS mode's default.
- Found in PV2: the north-up underlay (4 screen widths) doesn't reach the tilted ground quad at
  all rotations — near 90° the far corners pass its short axis and show background. Needs
  `UnderlayServes`'s equal-size check relaxed to a square surface of the long side, or
  equivalent.
- Found in PV3: tilted follow at the Atlanta launch scale (1:240,401) shows blank frames with no
  touch at all (51 blank, 32 soft in 38 s; base z11 → z10). PV2's 0/8 was at 1:7,233.

**Built 2026-10-04, partly** (simulator; the far-field and BT1 items wait for the phone).

- Two causes behind both blank-frame findings. (1) The screen-sized underlay is too narrow for the
  tilted quad: at 45° the far corners sit 3,884 px ahead of the centre (3.44 screen widths) and
  the underlay reaches 2 widths along its short axis. (2) The underlay is rebuilt only where the
  loop would pause, and following never pauses, so after about a minute it went stale and stayed
  stale; PV2's 0/8 run was 50 s long.
- `PPBaseCoverage.h`: `UnderlaySurfacePixels` — a tilted screen's underlay is a square of the
  long side (2556², about 26 MB, twice the flat one), grown further if a quad would sit more than
  0.4 of the side out. `UnderlayServes` takes the optional quad: the surface need only be at
  least the live one (so a square still serves the screen flattened by a drag), and each quad
  corner must stay `kUnderlayQuadInset` (1/16 of the side) inside the edge, so the rebuild starts
  while the quad is still covered. `PPViewport` `forUnderlay()` / `underlayServes(_:)` pass the quad.
- `MapModel`: `buildUnderlayIfStale` split from `pauseOrBuildUnderlay`; a follow step with a
  stale underlay builds the underlay instead and steps on the next tick. Applies flat too, where
  the underlay had the same never-rebuilt problem.
- Sim acceptance (iPhone 18 Pro, Atlanta pack, 45°, `simctl location` 8 m/s, counters from the
  stats line):
  | Run | Before | After |
  |---|---|---|
  | east from Midtown at 1:240,401, ~25–30 s | 16 soft, 23 blank; base hits 63/114 | 1 soft, 1 blank (the mode switch); 201/221 |
  | north then east (a 90° turn) at ~1:15,000, ~2.5 min | +43 soft, +83 blank | +113 soft, +12 blank |
  Underlay draw on the sim: 41 ms at 1:240,401, 150 ms at 1:15,706 (was 16–94 ms
  screen-sized). The empty band at the top of the east run is the pack's edge (-84.25), not a
  coverage miss.
- Not done: `.interpolation(.high)` was not tried — it sets how an `Image` is scaled into its
  frame, which is 1:1 here; the tilt is resampled by the compositor's `projectionEffect`, which
  it does not reach. Shimmer has to be judged on the phone; the remedy then is the two-tier far
  field (the underlay is pre-minified). The two-tier band and BT1's 2.5D-vs-flat table both wait
  for phone numbers. Upright base-map labels stay out of scope.
- Still open: the 12 remaining blank frames on the riding-scale run (where in the run is not
  known); PV2's item (b), a pinch in GPS mode leaving follow off the ship.
- gtests: 4 new in `test/base_coverage_test.cpp` (square size; the screen-sized underlay misses
  a sideways tilted screen and the square does not; the square serves at every 5° turn and
  flattened; running ahead stales it while still covered). `fv_pippin_test` 154/154.

### DM — the dark map — **BUILT 2026-10-05**, release 1.3

Chris asked for a dark-mode base map modelled on Apple Maps' dark look — dark blue water, dark
green vegetation, dark brown sand — explicitly not a night palette: every feature class of
Kiawah Trails stays.

- `port/Osm/styles/kiawah-trails-dark.json` is GENERATED by `port/tools/make_dark_style.py` from
  `kiawah-trails.json`: same 65 layers, filters, widths and zoom ranges, colours from the
  script's PALETTE table. The script exits naming any colour property with no PALETTE entry.
  Edit colours in the script, not the JSON — Chris's hand edits (ground #30342c, beach #54442e,
  minor-road label halo #4d3e2e) were folded into the script.
- Palette highlights: ground #30342c, water #33467a, beach #54442e, buildings #26221d fill with
  grey outline (#5a5a5a z14, #8a8a8a z15+) so they stand out from the ground, road names
  near-white (#f1ede6 minor / #f8f5ef major) on a brown halo, bike network still the strongest
  line.
- The marsh `fill-pattern` tufts were too bright on dark, and `fill-opacity` does not reach
  pattern stamps (`ICanvas::DrawPixmap` has no alpha path; honouring it would be an ICanvas
  change, not done here). So the dark style has its own sprite sheet
  `symbols/kiawah-dark.{png,json}` — the shared osm-liberty-topo sheet with the marsh_pattern
  tile recoloured to a muted olive (#6a8442 at 60% alpha), written by the same script. ~100 KB
  in the pack.
- Pack key `[osm] style_dark` in `pippin.ini`; removing it keeps the map light. `stage_data.py`
  stages the dark style and sheet, lists `osm.style_dark` in PATH_KEYS and runs the sprite-pair
  check for both styles.
- `PPMap.darkStyle` (render queue only) reloads the sheet in place via `OsmStyleEngine::LoadFile`
  (all-or-nothing; keeps ref latitude, mm/px and the label switch, bumps the style epoch so the
  retained scene invalidates), drops the cached base, and bumps the overlay content epoch.
  `PPMap.hasDarkStyle` says whether the pack names one. `MapModel.setDarkMap` is driven by
  `MapScreen`'s `@Environment(\.colorScheme)` (`onChange(initial: true)`), so the map follows the
  system appearance live.
- Point names: fvkit `PointOverlay::SetLabelColors(text, halo)` added (default black on white, so
  goldens unchanged); PPMap sets #f1ede6 on #4d3e2e with the dark sheet, black on white otherwise.
- Same session: the "3D While Following" toggle was removed — see the PV section's note.
- Known, left as is by Chris's choice: the About screen's style name is read once at launch;
  there is no manual Light/Dark choice in the menu.
- Proved: light/dark renders compared with pyfvw; simulator switched light→dark→light live;
  macOS ctest 2299/2299; Release 1.2.0 (2) archive installed on Chris's iPhone and accepted
  2026-10-05.

### Open for Chris

1. **Which tide station stands for the beach.** The default is 8667062 Kiawah River Bridge —
   NOAA's "Kiawah" station, but it sits in the river behind the island. The alternatives are
   Charleston 8665530 itself or another subordinate station whose timing matches the ocean front
   better. It is a pack key, so riding can change it.
2. **The rideable threshold.** `[beach] rideable_below_m` starts as a guess — 0.8 m (2.5 ft) above
   MLLW, the lower half of Charleston's ~1.6 m mean range — to be set from riding.
3. **More network.** SH2 sends a place name to Apple; WX would fetch wind. Pippin's only network
   use today is following a short link. Both are optional, and both need a yes.
4. ~~A night chart~~ — **answered 2026-09-25: yes**, for its own sake and for the 2.5D view (BT4).
5. **Where the beach choice lives** — decided here as an `options` object in `.fvrte` (a three-way value since 2026-09-26) that
   route.py learns, rather than profile variants. Override if the format should not move.
6. ~~The hilly pack~~ — **answered 2026-09-25: Atlanta**, the next place to map; Chris downloads
   the DTED level 2 before EL starts. Still open: **the Atlanta box** (EL5), and whether one app
   should carry both regions (RG1).
7. ~~2.5D~~ — **answered 2026-10-04:** follow-only, capped at 45°, base-map labels lying on the
   plane, points of interest upright (PV4).
8. ~~Keeping the band at rest~~ (DR2) — **answered by the build, 2026-09-25**: the band is held at
   rest (2.25× the screen), reversing P18's choice; the memory-warning drop is unchanged.

## What is deliberately NOT in v1
Server/gazetteer lookup, other areas of interest (the manifest is data-driven, so a second
region is a second data pack, not code), point editing, breadcrumb trail rendering, Metal,
CarPlay/watch, background-location recording (v1 records only while foregrounded — background
modes are an entitlement + battery conversation for a later session).
