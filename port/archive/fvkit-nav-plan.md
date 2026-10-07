# Moving-map / navigation plan (MM1–MM6)

**Status: MM1–MM7 ALL BUILT — MM1–MM4 2026-08-15, MM5 and MM6 2026-08-17, MM7 2026-08-18 (archive
rows).** What is left is not core: **MM5b** (HMM/Viterbi map matching) is optional and never
started, a **serial** transport waits for a device to test against, and **CoreLocation** waits for
NMEA-over-TCP to prove insufficient. **MM7 closed the app gap this line used to name**: the whole of
MM6 is bound as `pyfvw.nav`, and PythonView opens a recorded ride (GPX or an NMEA log), a live feed
(TCP, or UDP broadcast) and the demo. MM6 also grew a **GPX reader** the plan never named (Chris, 2026-08-17), which
shares MM1's replay path with a recorded NMEA log rather than adding one.
The three plan lines that turned out to be WRONG when built,
so they are not re-derived: MM2 needed **no meridian-convergence accessor** on the projection seam
(equal-arc convergence is identically zero, so it is a defaulted argument); MM3's "surface space
for short moves, geo for long ones" is a distinction equal-arc does not have, so the interpolation
is **geo throughout**; and a compass heading must be **NEGATED** into `PointSymbolStyle::
rotation_deg`. The plan also assumed the shell could not turn the chart — PR1–PR3 built that
afterwards, so **track-up is now a real rotation** and `SetRotationSupported` is true in PythonView.

**Goal**: the functionality of `Applications/FalconView/MovingMapOverlay` as a portable
app-support layer — position feeds behind one seam, an ownship symbol oriented to heading,
FalconView's screen-positioning algorithm (the part Chris called out as working well) ported
faithfully with a smoothing layer on top, and optional snap-to-road over the existing
`port/Routing` graph. No COM: `IGPSFeed`/`IMovingMapFeed` are severed at a plain C++ interface,
exactly the D6 adapter shape.

Decisions taken with Chris, 2026-08-15:
- **First source is SIMULATED/SCRIPTED** (deterministic, no hardware); NMEA + transports come
  later (MM6), phone GPS arrives as NMEA-over-TCP before any CoreLocation adapter is considered.
- **Camera = faithful core + smoothing layer**: the apron / 3×3 placement / track-up anchor math
  ported verbatim as a pure function, then an animation layer that slews instead of jumping —
  "jump" vs "smooth" is animation duration 0 vs >0.
- **Snap-to-road = nearest-edge projection first**, HMM/Viterbi matching only if drift demands it.
- **Current position only** — no breadcrumb trail, no per-point UI. The fix history kept is the
  small ring heading derivation needs. Trail recording/playback is a later plan.

## Where the original logic lives (so nobody re-finds it)

All in `Applications/FalconView/MovingMapOverlay/`:
- **Recenter trigger** — `gps_draw.cpp` `map_update()` (~1703): recenter when the ship leaves the
  apron rect or the view, or continuous mode, or forced.
- **Apron** — `gps_draw.cpp` `auto_center_bounding_box_calc()` (~1556): intersection of an outer
  box (W/10..9W/10 × H/10..9H/10) with an inner box; track-up uses a fixed W/5 × 2H/5 box at
  (2W/5, H/2); north-up sizes the inner box from which third of the screen the ship is in.
- **Placement on recenter** — `gps_draw.cpp` `set_new_map()` (~488) + `get_delta_xy_discrete()`
  (~645) / `get_delta_xy_continuous()` (~763): screen divided into a 3×3 grid, ship placed in the
  perimeter box that maximizes map AHEAD of it, chosen from `point_angle` = heading + map rotation
  + **meridian convergence** (`get_meridian_covergence` — the term everyone forgets).
- **Track-up anchor** — `gps.cpp:114`: ship at screen fraction (+0.0 W, +1/3 H) from center, i.e.
  ~83% down the screen, map rotated so track is up; rotation snapped to 0 when < 0.1°.
- **Derived heading** — `gps_draw.cpp` `get_current_heading()` (~420): when the fix carries no
  heading, atan2 over the last two distinct positions in *surface* pixels (deg/pixel scaled).
- **Symbol orientation** — `gps.cpp` ~4106: ship drawn at `north_up_angle` relative to the map,
  i.e. heading + convergence, and the canvas rotation handles the rest.
- **World-scale escape** — `set_new_map` at WORLD/1:80M just centers; no apron math.
- **NMEA** — `nmea.cpp`/`nmea.h`: RMC/GGA/GLL/VTG parse + build, nearly dependency-free
  (CString/COleDateTime only), ports mechanically.

## Sessions

### MM1 — The fix and the feed seam — **BUILT 2026-08-15** (archive row MM1)
Shipped as three headers rather than one: `fvkit/nav/position.h` (fix + seam + `FixQueue`),
`scripted_source.h`, `heading.h`. Two things the plan did not have: the track builder takes a
POLYLINE and not a `Route` (so fvkit still does not link `port/Routing`), and
`get_current_heading`'s quadrant fix-up is an `atan` correction applied to `atan2` — a southbound
ship comes out 180 degrees opposed, so that one is fixed rather than preserved, with the original
formula kept in the test as the oracle it is not.

`port/include/fvkit/nav/position.h` + `port/fvkit/nav/`:
- `PositionFix`: lat/lon (deg), msl alt (m), speed (m/s), true + magnetic heading (deg),
  timestamp (epoch seconds, double), hdop, satellite count — **each field with an explicit
  validity** (the NMEA sentences genuinely deliver partial fixes; -1000.0 sentinels do not port).
- `IPositionSource`: `Start`/`Stop` + a pushed listener (`std::function<void(const PositionFix&)>`
  or a small interface — decide against D1 ownership rules). Threading rule stated up front:
  **the source delivers on the caller's chosen thread or the app pumps a queue** — PythonView/tk
  will need the queue form, so build `FixQueue` (mutex + drain-on-tick) in MM1, not when it hurts.
- `ScriptedSource`: plays a vector of (t, fix) against an injectable clock; a helper builds that
  vector from a `Route` (the O4 router's polyline at a profile speed) so the demo track drives on
  real Kiawah roads. Time-scale factor for faster-than-real-time tests.
- `HeadingResolver`: the `get_current_heading` fallback — fix heading if valid, else derived from
  the last distinct positions (small internal ring; this is all the "trail" MM keeps).
- Tests: all headless; scripted source + resolver pinned numerically.

### MM2 — Camera controller, the faithful core
`fvkit/nav/camera.h`: `MovingMapCamera`, a PURE function of
`{window w×h, ship surface x/y, resolved heading, map rotation, convergence}` and its mode flags →
`CameraTarget {center lat/lon, rotation, changed}`.
- Modes exactly FalconView's toggles: auto-center on/off, auto-rotate (track-up), continuous
  centering. Track-up anchor fraction (0, +1/3) kept as data (`m_rotation_frac_pos_*`).
- Port verbatim: apron calc, in-apron/in-view recenter trigger, discrete + continuous 3×3
  placement, track-up offset math, rotation snap <0.1°, world-scale escape. Convergence comes from
  the projection seam (`fvkit/proj.h` — check what G-series exposes; add the convergence accessor
  if missing, it is one formula on equal-arc).
- The caller (shell) applies `CameraTarget` to `MapEngine`; the camera never touches the engine —
  that is what makes every geometry case a unit test. Pin the discrete-placement table (heading
  0/45/…/315 → which of the 9 boxes) and the apron rects against hand-computed values from the
  original formulas.

### MM3 — Smoothing layer — **BUILT 2026-08-15** (archive row MM3)
Shipped as `fvkit/nav/camera_slew.h` + `camera_slew.cpp`. Two things the plan did not have:
the interpolation is in GEO throughout (surface and geo are the same lerp on equal-arc, and geo
is the frame that survives the projection being re-centred by the animation), and the rate caps
EXTEND the duration rather than clipping the move — with duration 0 exempt, so the FalconView
jump stays exactly reachable. See the archive row for the five decisions.

`fvkit/nav/camera_slew.h`: `CameraSlew` between the camera's target and the engine.
- Tick-driven (`Advance(dt)`), eased center interpolation in surface space for short moves
  (geo for long ones), rotation along the shortest arc, rate caps so a 180° turn doesn't spin the
  chart violently. Duration 0 = the FalconView jump, so MM2's behavior remains reachable and
  testable.
- Continuous track-up mode already produces per-fix micro-pans; the slew's job is the discrete
  recenters and rotation changes. Interrupt rule: a new target retargets the animation in flight,
  never queues.

### MM4 — Ownship overlay + app wiring — **BUILT 2026-08-15** (archive row MM4)
- `fv.movingmap` static overlay type (A1 registry): draws the ownship symbol through G3 `GeoDraw`
  rotated by heading + convergence (+map rotation handled by the projection), G4 `RenderState` for
  selection for free. `BuiltinSymbolLibrary` grows an **ownship** symbol (aircraft/arrow display
  list; the north arrow shows the pattern).
- pyfvw: `pyfvw.nav` binding (source/camera/slew/fix), PythonView keys for the three toggles,
  `[movingmap]` ini section (anchor fractions, slew duration, toggles' startup state — same keys
  FalconView kept in the registry: AUTO_CENTER, AUTO_ROTATE).
- The acceptance demo: ScriptedSource driving a routed track through Kiawah while the camera
  tracks in each mode.
- **As built**: `fv::MovingMapOverlay` (`fvkit/overlay/moving_map_overlay.h`), `fv.movingmap` as a
  STATIC BUILT-IN type, `builtin_symbol::kOwnship`, `pyfvw.nav` (`pyfvw_nav.cpp`), and PythonView's
  "m" key + M/T/S modes + `[movingmap]` ini. Two corrections to the lines above: **`fv.north` is
  the chevron a non-aircraft platform asks for**, so no second ownship symbol was authored; and
  **the heading is NEGATED into the symbol's rotation** — a symbol rotation turns counter-clockwise
  on screen, exactly as S-52 already knew. See the archive row.

### MM5 — Snap-to-road — **BUILT 2026-08-17** (archive row MM5)
Shipped as `fvkit/nav/road_snap.h` + `road_snap.cpp` (the snapper and the `IRoadNetwork` seam) and
`port/Routing/fv_road_network.{h,cpp}` (`RoadGraphNetwork`, the adapter). **Four things the plan did
not have.** (a) The line below says "over `port/Routing`'s RoadGraph", and taken literally that
makes fvkit link the router — which MM1 deliberately avoided. So the network is an INTERFACE fvkit
declares and the adapter lives on the side that owns the roads; `ProjectOntoSegment` is inline in
the header, which is the whole of what the adapter needs from fvkit and is why `fvgraph` does not
link the map engine. (b) The hold at low speed is expressed as an INFINITE STAY BONUS rather than a
branch, so a held arc that has gone out of range is let go for free. (c) The snapper stands BEFORE
the heading resolver, not beside it — and a two-way road with no heading cannot say which way along
itself the ship is going, so the first fixes snap without a bearing and the derived heading answers
the direction question from the third onwards. (d) The adapter's index had to be built over the arc
GEOMETRY: `RoadGraph::bounds()` is the box of the graph's junctions, and a road's shape points can
lie outside it (a single east-west road gives a box with no height at all).

`fvkit/nav/road_snap.h` over `port/Routing`'s `RoadGraph` (needs a prebuilt `.fvroad`; optional —
no graph, no snapping, feature off):
- Candidate arcs within radius = k·HDOP (floored/capped, k tunable), point projected onto the
  segment; score = perpendicular distance + heading-alignment term + **hysteresis** (stick to the
  previous arc and its connected arcs to stop flip-flopping between parallels/at junctions);
  below a speed threshold, hold the last snap (heading is noise when stopped).
- Output is a decoration: `SnappedFix {raw, snapped position, arc id, along-arc bearing,
  confidence}` — the RAW fix is never destroyed; camera and symbol consume snapped when confident,
  raw otherwise. Symbol heading takes the arc bearing when snapped and moving.
- Tests over the Kiawah `map*.osm` graph with synthetic drift (offset a routed track by 5–30 m of
  noise, assert recovery of the road and no junction flapping).
- **MM5b (only if needed)**: HMM/Viterbi over a sliding window with network-distance transition
  costs (OSRM-style), reusing the router for candidate-to-candidate distances. **MM5 measured what
  it would buy**: over a routed Kiawah track with +/-12 m of scatter the mean error came down from
  9.32 m to 5.96, and the residual is the ALONG-track component, which a projection cannot touch and
  a motion model can. Still "only if needed" — but that number is what "needed" would mean.

### MM6 — NMEA + transports — BUILT 2026-08-17 (plus a GPX reader the plan never named)
Shipped as three headers. The plan's two lines were both built as written; what the plan did NOT
have is the third header and the seam that ties them together.
- `fvkit/nav/nmea.h` — RMC/GGA/GLL/VTG → `PositionFix`, checksum, `NMEA_test`, and the build side
  kept for the recorder. **Three original quirks preserved as Q1–Q3** (short sentence rejected
  whole; GGA altitude withheld at ≤3 satellites; magnetic heading derived from the variation) and
  **one deliberate break: any talker, not just `$GP`** — a phone talks `$GNRMC`, and this session
  exists so a phone can drive the map. `NmeaFixAssembler` is where MM1's `Merge` finally gets used:
  sentences of one epoch become one fix, and the date comes from the last RMC.
- `fvkit/nav/line_transport.h` — a seam SEPARATE from the parser, so nmea.h never learns where
  bytes come from. `LineBuffer` is the one framing implementation; `String`/`File` (with `follow`)/
  `Tcp`/`Udp` are the four transports. Serial still waits for a device; CoreLocation still waits
  for NMEA-over-TCP to prove insufficient. **Windows is guarded and never compiled.**
- `fvkit/nav/gpx.h` — NOT IN THE PLAN. Chris asked for it (2026-08-17) and it is the reader a user
  actually has files for. It does not add a replay path: `ReadGpxFile` and `ReadNmeaLog` both hand
  a `vector<PositionFix>` to **`BuildScriptedTrackFromFixes`**, which schedules from the fixes' own
  timestamps and plays through MM1's `ScriptedSource` — so a recorded ride reaches the camera, the
  heading resolver and the road snapper with no new code in any of them.
- Fixture: `TestData/kiawah_cycle.gpx`, a real 1705-point ride. See the archive row for the eleven
  decisions and the three bugs the tests and the review caught.

### MM7 — the app side — BUILT 2026-08-18 (not in the plan, because the plan stopped at the core)
The plan's own status line named this as "the real gap" and MM6's row named it as the leftover.
`pyfvw.nav` grows the whole of MM6 (the GPX document, the NMEA parser and assembler,
`NmeaLineSource`, `LineBuffer` and the four transports, `FixScriptOptions` +
`BuildScriptedTrackFromFixes`), and PythonView grows a File item, a host/port dialog and a
`[movingmap]` block (`track_file`, `nmea_host`, `nmea_port`, `track_max_gap_s`,
`track_time_scale`). **The shell's tick did not change** — it has polled whatever source it was
given since MM4, which is the whole argument that MM6's one-seam design was right. See the archive
row for the ten decisions and the one review finding.

## Out of scope (recorded so they are choices, not omissions)
Breadcrumb trail + per-point UI + trail files; predictive path (`PredictivePath*.cpp`); CDI,
range/bearing, coast track, bullseye (`cdi.cpp`, `rb*.cpp`, `csttrack.cpp`); multiple simultaneous
feeds; SkyView; playback UI. Each has a clean seat later: trail is a document overlay like
`.fvpoints`, predictive path is a pure function of the fix ring.
