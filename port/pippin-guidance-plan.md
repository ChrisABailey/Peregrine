# Pippin guidance plan (GD1–GD4, BG1–BG5) — turn alerts, then screen-off tracking

**Status: THE GD HALF IS BUILT, 2026-09-09, on Chris's word lifting the 2026-09-04 deferral.
GD1–GD4 are done; BG1–BG5 are open, refined 2026-09-25 with sizes and an optional BG6.**

**The order is Chris's and it is fixed**: guidance first, with the phone on and the screen lit —
an arrow saying left/right/straight with a countdown to the turn — and background location
second. Guidance is the feature; background execution is what later lets the same events reach a
pocket or a wrist. Each half stands alone, and the GD half needs nothing from iOS beyond a view.

Pippin itself is `port/pippin-plan.md` (P1–P20; P21–P22 are written up in
`port/apps/Pippin/README.md`). The FVW session protocol applies: ledger first, one step per
session, compiler-driven, commit per step, no subagents.

## What the survey found (2026-09-04) — do not re-explore for this

- **There are no maneuvers anywhere in the tree.** `fv::Route` (`port/Routing/fv_router.h:174`)
  carries `geometry`, `nodes`, `legs`, `length_m`, `seconds`, and the snap terminals. `RouteLeg`
  (`:167`) is a run of consecutive arcs sharing a road name — "0.8 km on Folly Rd". No turn
  angle, no instruction, no step list, in `Routing`, `RouteKit`, `PPRoute` or the trip computer.
  `fv::TripStats` gives distance-to-end along the route and nothing per junction.
- **`legs` + `nodes` + `geometry` is most of the raw material.** A maneuver is a leg boundary; the
  angle falls out of the geometry either side of the junction node, and the leg already carries
  the name of the road being joined.
- **The fix path never touches Metal.** `MapModel.receive(_:)` hands each fix to the render queue,
  `PPMap.pushFix:` feeds `PPMap.consumeRawFix:` (`PippinKit/PPMap.mm:991`), and that method feeds
  the trip computer and `fv::GpxRecorder`. Drawing is a separate call on the same queue. Logging
  and guidance can run with nothing drawn.
- **The GPX on disk is always valid.** `GpxRecorder` writes the footer after every point and the
  next point overwrites it (`port/fvkit/nav/gpx_recorder.cpp`), so a kill mid-ride leaves a
  well-formed file rather than a truncated one.
- **There is no scene-phase handling in the app at all.** A fix calls `setContentDirty()`, which
  unpauses the `CADisplayLink`. Backgrounded, that is GPU work after the app was told to stop.
- **The screen is currently load-bearing.** `MapModel.updateIdleTimer` (`MapModel.swift:331`) pins
  `isIdleTimerDisabled` while following or recording precisely because there is no background
  mode. Authorization is When-In-Use, there is no `UIBackgroundModes`, and
  `allowsBackgroundLocationUpdates` is never set.
- **`LocationPolicy` asks the wrong question for a dark screen.** It decides accuracy from where
  the ownship projects in the viewport, which means nothing when nobody is looking. It is
  CoreGraphics-only with a `ctest -R location_policy` target, so a new input is cheap to test.

## Decisions taken now, so a session does not relitigate them

- **Maneuvers are derived from the route, in C++, on the mac.** There is no third-party router to
  ask and no instruction text to import. This is the same shape as every other headless piece: the
  logic lives under `port/fvkit/nav/`, the phone gets a facade. The `fv::routing::Route` itself
  stays on RouteKit's side of the seam — see GD1.
- **Authorization stays When-In-Use.** With `UIBackgroundModes = location` and
  `allowsBackgroundLocationUpdates`, fixes continue while the app is backgrounded and the screen
  is off — which is the whole of both use cases. `Always` buys only relaunch-when-not-running, and
  costs a second and scarier prompt plus a harder App Review answer.
- **No watchOS target.** A time-sensitive `UNNotificationRequest` mirrors to a paired Apple Watch
  on its own when the phone is locked. That is the wrist buzz, for the price of a notification. A
  watch app with WatchConnectivity and a complication is a project, not a step, and is out of
  scope for this plan.
- **A Live Activity is noted, not planned.** ActivityKit on the lock screen is arguably the right
  home for "next turn" once the maneuver data exists. It is another target and another review
  surface; revisit after GD3 is ridden.
- **The ringer decides, and iOS applies it for us** (GD4, and Chris's call). `.ambient` audio is
  silenced by the Ring/Silent switch; haptics are not. Sound and haptic together with the ringer
  on, haptic alone when silent, and no setting of our own.

## GD — guidance, phone on (the feature)

### GD1 — the turn list — **BUILT 2026-09-09**
`port/include/fvkit/nav/maneuver.h` + `port/fvkit/nav/maneuver.cpp`, 10 gtests; the
`fv::routing::Route` adapter is `port/RouteKit/fv_route_maneuvers.{h,cpp}`, 5 gtests over the real
Kiawah graph.

**It does NOT take an `fv::Route`, and that is the one change from the plan as written.** fvkit
does not link port/Routing — the invariant RouteKit exists for — so `BuildManeuvers` takes a
`nav::RouteShape` (the drawn line, plus each leg's name, class and index into that line) and
RouteKit's `ManeuversOf` fills one from a route. Same D6 seam as `nav/road_snap.h`, and it is what
lets the fvkit tests build a junction in metres and assert a 90 degree left.

`fv::routing::RouteLeg` gained `geometry_begin`, filled in `Router::Materialize`. Nothing could
place a leg boundary on the line before it: a leg carried a name, a length and a duration, and a
turn happens at a boundary.

Two decisions the plan left open, both settings on `ManeuverSettings`:
- **The angle is measured over a 15 m window either side of the junction, not over the adjacent
  shape points**, which around a surveyed bend are a metre apart and mostly noise. The window is
  clamped to the neighbouring candidates so two corners 20 m apart do not average into one.
- **`end_margin_m = 20`**: a corner within 20 m of either end is dropped. Both ends of a route are
  a snap, often mid-block, and on the Kiawah fixture the last named way is joined **half a metre**
  before the destination — a "turn left" nobody can act on. Turned off, that corner comes back,
  which is what the RouteKit test asserts.

The Kiawah bicycle route (Ruddy Turnstone to the beach club, 2025 m, 9 legs) yields 6 maneuvers:
depart Sea Marsh Drive, right at 239 m onto an unnamed path, a staggered crossing at 950/952 m
(right onto Amaranth, immediately left onto Green Winged Teal — both real and both kept), right
onto Eugenia at 1193 m, arrive. The bands themselves (20/45/110/160 degrees) are defaults nobody
has ridden yet.

### GD2 — the approach state machine — **BUILT 2026-09-09**
`port/include/fvkit/nav/guidance.h` + `port/fvkit/nav/guidance.cpp`, 14 gtests on synthetic fixes;
the real ride replayed against a real planned route is `RouteKit/test/route_guidance_test.cpp`,
4 gtests, same seam as GD1.

**Chris settled all five open questions on 2026-09-09 by taking the defaults**, so these are now
the behaviour rather than a proposal:

- **The rings are times clamped to distances.** Heads-up 30 s clamped to [100 m, 400 m], act-now
  8 s clamped to [20 m, 80 m], and the junction itself at 10 m. A rider with no speed gets the
  floor. Time alone gives a stopped rider an infinite ring; distance alone serves a walker and a
  rider the same corner identically.
- **Only the innermost ring fires**, and crossing several between two fixes spends the outer ones
  rather than queueing them. This is what stops guidance that starts mid-approach — and every
  staggered junction — announcing twice in one second.
- **Off route is sustained (25 m held 5 s), rejoining is immediate (15 m).** The gap between the
  two numbers is the hysteresis. Off route says so ONCE and then falls silent; rejoining re-arms
  every alert still ahead, because a corner passed in silence was never announced.
- **A staggered pair is carried on the event** (`has_then`) whenever the following maneuver falls
  inside the current one's act-now ring. It is a property of the two corners, not of the ring, so
  the heads-up carries it too — "in 200 m, right then immediately left".
- **The projection is windowed**, 150 m either side of the last one, widening to the whole route
  only when the windowed answer is off route anyway. This is the fix
  `TripComputer::RemainingAlongRoute`'s own comment proposes, and GD2 needs it where the trip
  computer does not: a jumpy distance-to-go is tolerable, announcing a turn from the other limb of
  an out-and-back is not.

Two things the real ride found that the synthetic fixes did not:

- **GD1 was measuring in a different metre.** It took its along-route distances from geo_tool's
  ellipsoidal range while Routing, road_snap and the trip computer all use the WGS-84 mean-radius
  haversine — 0.3 percent apart, 6 m over the 2 km fixture, enough for a countdown to run past the
  end of the line. `maneuver.cpp` now measures ranges the port's way and keeps geo_tool for
  bearings only; the GD1 RouteKit test that should have caught it was loose (2 percent) and is now
  1 m.
- **Arrival is decided before the rings and ends them.** The arrive maneuver's own
  at-the-junction ring fired after "you have arrived", about the same point.

The ride (1705 fixes, the router's 4-maneuver route between its ends) yields exactly heads-up,
act-now, at, passed for each of the two corners and then heads-up, act-now, arrived — once each,
in order, with no off-route event. A test moves a 200-fix stretch 80 m sideways to prove the
tolerances do fire, and asserts nothing is announced while the guidance is silent.

### GD3 — the banner — **BUILT 2026-09-09**
`PPGuidance.{h,mm}` + `PPGuidance+Internal.h` ride on `PPFrame` beside `PPTrip`, and the banner is
`MapScreen.guidanceBanner`, below the ride bar. Nil in three cases that mean different things to a
rider and the same thing to the banner: outside GPS mode, with no planned route, and past the
destination. **Off route is deliberately NOT one of them** — the guidance is running and has
stopped naming a corner, so the banner says "Off route" rather than vanishing, which would read as
a bug.

**The turn list had to be carried down to the phone**, and that is the bulk of this step. The
phone plans through `fv::RoutePlanner` and never sees an `fv::routing::Route`, and `RoutePlan`
dropped the leg names, so: `RoutePlan` gained `maneuvers`, filled on the through-route branch
only (a per-pair fallback draws straight legs between stops it could not connect, and instructions
along a line the rider cannot follow are worse than none); `pippin::RouteStore::RouteManeuvers()`
hands them out; and `PPMap` sets the route on `fv::nav::Guidance` in the same two places it sets
the trip computer's, so the two can never disagree about which route is being ridden.

**UNITS — Chris's call on 2026-09-09, and it overrides this plan's "units follow the ride bar".**
A turn countdown is metres under a kilometre and YARDS under a mile; the ride bar's `distance()`
keeps feet and is untouched. `DistanceUnits.countdown(_:)` is the second convention, deliberately,
because a rider is told "in 200 yards" and never "in 600 feet". It also rounds — 10 below a
hundred, 50 above — since the banner redraws at the receiver's rate and an unrounded countdown
flickers through every value on the way down. The unit is chosen from the rounded number AND the
true one, because rounding crosses the boundary both ways: 995 m rounds UP and must not read
"1000 m", an exact mile rounds DOWN to 1750 yd and must not read as yards.

`DistanceUnits` moved out of `MapMenu.swift` into its own Foundation-only `DistanceUnits.swift` so
the formatting is testable on the mac, `run_place_link_test.sh`-style; `DisplayUnits`, the
observable preference, stayed with the menu. 20 assertions in `test/guidance_format_test.swift`,
wired as `ctest -R pippin_guidance_format`.

Ridden in the simulator against the saved Kiawah route: the banner reads **1250 yd / Surfwatch
Drive** in miles and **1.1 km / Surfwatch Drive** in kilometres, same corner, with the slight-left
glyph.

### GD4 — the alert, screen on — **BUILT 2026-09-09**
`Pippin/TurnAlerts.swift` plays them; `Pippin/AlertTone.swift` synthesises the chimes.

**THE RINGER DECIDES, AND iOS DECIDES IT FOR US — Chris's call, 2026-09-09.** There is no public
API for the Ring/Silent switch and none is needed: an `AVAudioSession` in the `.ambient` category
is silenced by that switch, while `UIFeedbackGenerator` is not (it follows the rider's own Sounds
& Haptics setting). So every alert fires its haptic and offers its chime, and the system drops the
chime when the phone is silent. **Ringer on is sound AND haptic together; silent is haptic alone.**
Nothing reads a switch, because nothing can. `.ambient` also mixes rather than interrupts, so a
rider's music keeps playing; ducking is deliberately not asked for.

The earlier note in this file that "phone haptics alone are not the alert" was written when this
plan was drafted and **Chris has overruled it** — it was never his finding. There is no
sound/haptic setting; the ringer is the setting.

**The chimes are synthesised, not shipped.** `AlertTone` builds a 16-bit mono PCM WAV in memory
and `AVAudioPlayer` plays it, so there are no binary assets in the bundle to version by hand, and
the arithmetic is a mac test (`ctest -R pippin_alert_tone`, 25 assertions — the header a player
looks for, the three internal lengths agreeing, an envelope that starts and ends at silence, and
the peak landing on the amplitude asked for).

**Loud enough for moving air.** Each note is its fundamental plus two overtones (0.35 and 0.2),
normalised by the harmonic sum's own peak, at `guidance.alert_amplitude` — 0.9 by default. The
fundamentals sit between 1 and 2.7 kHz, where a phone speaker is efficient and the ear is most
sensitive, and the notes are long enough for the ear's 200 ms loudness integration. A pure sine
at 0.35 around G5 was inaudible on a ride.

What each event does: heads-up is a light impact and a rising two-note chime; act-now a medium
impact and a brighter four-note one; the junction itself is **felt and not heard** (a third sound
for one corner is nagging); arrived is a success haptic and a three-note chime; off route a warning
haptic and the only falling chime, since it is the one alert that is not an instruction; rejoined
is a light impact alone; passed is not an alert at all.

**Events had to reach Swift**, which GD3 had left dropped. `PPGuidanceEvent` rides on `PPFrame`
beside `PPGuidance`, and it is **buffered**: a fix is consumed whenever one arrives and a frame is
drawn when the display link runs, so a frame that swallowed two fixes carries both fixes' events.
Each event is delivered on exactly one frame, and leaving GPS mode drops whatever was not drawn.

**One defect the real ride found and the synthetic fixes could not.** The junction ring was a fixed
10 m, which is smaller than the 8 m a rider covers between two 1 Hz fixes at 29 km/h — so it was
stepped over and the third alert fired only sometimes. It is now a time like the other two,
`at_s = 1.5` with `at_m = 10` as the floor a stopped rider gets. `nav_guidance_test` pins it at
four speeds.

Ridden in the simulator, `kiawah_cycle.gpx` replayed against a route planned between its own ends:
`heads up 239 m`, `act now 62 m`, `at 12 m`, `passed` — the 30 s, 8 s and 1.5 s rings at 8 m/s,
each exactly once and in order.

## BG — background, phone in a pocket (refined 2026-09-25)

Sizes: BG1, BG2, BG3 and BG4 are small; BG5 is a session; BG6 is optional. The order is fixed:
BG1 stands alone, and **BG2 without BG3 is a battery regression** (see BG2). The battery baseline
BG3 is judged against is BT1 in `port/pippin-plan.md` ("After 1.1"), which should land first.

### BG1 — scene-phase gating in `MapModel` (S)
Correct on its own merits and worth landing whether or not the rest of BG happens. Getting it
loosely wrong means an occasional hard-to-reproduce app kill, so it goes first and alone.
- `MapScreen` observes `scenePhase` and forwards it to `MapModel.setSceneActive(_:)`.
- Background: pause the `CADisplayLink`; `tick()` and `draw` refuse; a frame already on the
  render queue may finish, but its `PPFrame` is not published. The ingest path —
  `receive(_ fix:)` → `pushFix:` → `consumeRawFix:` → trip computer, recorder, guidance — keeps
  running.
- `.inactive` (Control Centre, a call banner) is not background: the screen is still visible and
  drawing continues.
- Back to active: one exact frame with no band (P18's settle rule), and the follow camera jumps to
  the current fix rather than animating from where the map was minutes ago.
- The decision is a small CoreGraphics-only value, `ScenePolicy`, with a ctest table like
  `RenderGate`: phase × following × recording → draw, keep the feed, run the link.
- Acceptance on the simulator with `-PPShowStats`: background the app during a demo ride; the
  frame counter freezes while fixes keep arriving; no CoreAnimation or GPU warnings in the log.

### BG2 — the capability (S)
- `UIBackgroundModes = location`. In `PPLocationSource`, `allowsBackgroundLocationUpdates` and
  `showsBackgroundLocationIndicator` are YES only while following or recording.
- **The hazard.** Today a backgrounded Pippin costs nothing: iOS suspends it and the receiver
  stops with it (P15). Once background updates are allowed, a manager left running keeps the app
  alive and the receiver powered. So on entering the background with neither following nor
  recording, stop updates, and restart them on return. P17's `stopFeed()`, which has had no call
  site, gets its first real one.
- The coarse configuration's `pausesLocationUpdatesAutomatically = YES` must never be in force in
  the background: a paused run cannot be resumed by a rider who is not looking. Background with
  following or recording is always the navigation configuration.
- The usage string (`INFOPLIST_KEY_NSLocationWhenInUseUsageDescription` in `project.pbxproj`, not
  `Info.plist`) says recording and turn alerts continue with the screen off.
  `PrivacyInfo.xcprivacy` is reviewed in the same step.
- Stay on `CLLocationManager`'s delegate API. The newer `CLLocationUpdate` /
  `CLBackgroundActivitySession` / `CLServiceSession` family is an alternative, not a requirement,
  and the two models should not be mixed in one step.
- Acceptance on the phone: record, lock, ride five minutes, unlock — the GPX has no gap, and the
  location indicator showed while locked. With GPS off and not recording, backgrounding shows no
  indicator.

### BG3 — `LocationPolicy` learns about the dark (S)
- A third input, `isBackground`. Background with recording or following → navigation accuracy
  whatever the viewport says; background with neither → stopped. BG2's stop rule is then decided
  here, by the tested policy, rather than inline in `MapModel`.
- Relax `updateIdleTimer`: recording alone no longer pins the screen awake; following still does.
  A lit screen in a pocket costs more than the receiver.
- The `location_policy` ctest gains the rows.

### BG4 — the ride that outlived the app (S)
`recordingURL` is main-actor state that does not survive a jetsam kill, and under When-In-Use the
app is not relaunched, so the ride file is left valid but orphaned and the app forgets it was
recording.
- Recommended: `startRecording` writes `Documents/recording.inflight` (the GPX path and start
  time) and `stopRecording` removes it. At launch, a surviving marker means the ride is kept as an
  ordinary ride named "<start> (interrupted)" — `GpxRecorder` has already left a valid file — and
  a one-time banner says so. No resume: appending means seeking behind a closed GPX's footer, and
  a second ride is the honest record.
- The marker logic is a pure function tested on the mac; the acceptance is killing the app from
  Xcode mid-recording on the simulator.

### BG5 — the events reach a pocket or a wrist (M)
- GD2's events become `UNUserNotificationCenter` requests while the app is backgrounded, and are
  suppressed while it is frontmost (GD4 plays those). The mirror to a paired watch is the
  system's.
- The time-sensitive interruption level needs the Time Sensitive Notifications capability
  (`com.apple.developer.usernotifications.time-sensitive`) on the app ID; without it a Focus mode
  can hold the alert.
- Permission is asked the first time GPS mode starts with a route, not at launch.
- One notification per maneuver, replaced as it moves from heads-up to act-now (same identifier)
  rather than stacked; arrival clears the rest.
- Acceptance: the simulator's lock screen during a `simctl location` ride, then a real ride with
  a paired watch.

### BG6 — a Live Activity (optional, M)
The next turn and its countdown on the lock screen, the Always-On display and the Dynamic Island,
so a rider can glance without lighting the whole screen — the battery answer on an iPhone with an
Always-On display. A widget extension target and ActivityKit; `Activity.update` from the app on
each GD2 event and every ~10 s of countdown. Revisit after BG5 has been ridden.

## Deliberately not in this plan

- A watchOS app, a complication, or WatchConnectivity.
- A Live Activity as a required step: it is BG6, optional, after BG5 has been ridden.
- Automatic re-routing when the rider leaves the route. GD2 falls silent and says so; recomputing
  is a separate decision with its own battery and correctness story.
- `Always` authorization, significant-location change, and region monitoring.
- Voice instructions.

## Open for Chris

**Answered 2026-09-09**: the approach thresholds (times clamped to distances), the off-route alert
(one event, then silence), the staggered pair, and the projection — all as built, written up under
GD2. Chris took the defaults on all of them; none has been ridden yet, and the numbers are
settings rather than constants precisely so a ride can change them.

**Answered 2026-09-09**: sound and haptic together, gated by the ringer, no setting. Written up
under GD4.

Nothing in the GD half is waiting on Chris. The BG half has its own decisions, listed with the
steps.
