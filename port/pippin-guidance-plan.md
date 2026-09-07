# Pippin guidance plan (GD1–GD4, BG1–BG5) — turn alerts, then screen-off tracking

**Status: NOT STARTED. Deferred by Chris on 2026-09-04 until Pippin v1 is approved.** Nothing
here is to be built before that; the file exists so the survey behind it is not re-derived.

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

- **Maneuvers are derived from `fv::Route`, in C++, on the mac.** There is no third-party router
  to ask and no instruction text to import. This is the same shape as every other headless piece:
  the logic lives under `port/fvkit/nav/`, the phone gets a facade.
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
- **Phone haptics alone are not the alert.** A vibration in a jersey pocket is not reliably felt.
  Sound, or a notification with one, is what carries.

## GD — guidance, phone on (the feature)

### GD1 — `fvkit/nav/maneuver.{h,cpp}`: the turn list
Takes an `fv::Route` and produces an ordered list of maneuvers: the junction coordinate, the
along-route distance from the start, the signed turn angle, a classification (depart, straight,
slight/normal/sharp left and right, u-turn, arrive), and the name of the road being joined. Leg
boundaries are the candidates; a boundary whose angle is below the straight-through threshold and
whose name merely changes is a leg change but not a turn, and must not become one. Mac gtests over
the Kiawah route fixtures already in the tree. No iOS, no Objective-C.

### GD2 — `fvkit/nav/guidance.{h,cpp}`: the approach state machine
Fed the maneuver list and the fixes, it answers: which maneuver is next, how far along the route
to it, and — edge-triggered, once each — the alert events at the approach thresholds. It owns the
awkward parts: passing a maneuver, standing still next to one, going off route (beyond a
cross-track tolerance the guidance falls silent rather than lying), and rejoining. Replaying
`testdata/kiawah_cycle.gpx` against a planned route is the test. Still pure C++ on the mac.

### GD3 — the banner: arrow, road, countdown
`PPGuidance` in PippinKit, riding on the frame beside `PPTrip` and nil for the same reasons —
outside GPS mode, or with no route, there is no guidance rather than an empty one. A SwiftUI
banner in `MapScreen` near `rideBar` (`MapScreen.swift:660`) and `noticeBanner` (`:741`): the
arrow glyph, the road name, and the distance counting down. Units follow the ride bar; do not
invent a second convention. **This is the step Chris asked for.**

### GD4 — the alert, screen on
Haptic plus an optional short sound at GD2's events, through the rider's own audio route. Nothing
here needs background execution: the screen is lit and the app is frontmost. Notifications wait
for BG5, because a notification the app cannot compute while suspended is worse than none.

## BG — background, phone in a pocket

### BG1 — scene-phase gating in `MapModel`
Observe `scenePhase`, pause the `CADisplayLink`, and suppress draws when not active, while the
ingest path keeps running. **Correct on its own merits and worth landing whether or not the rest
of BG happens** — it is also the step where getting it loosely wrong means an occasional
hard-to-reproduce app kill, so it goes first and alone.

### BG2 — the capability
`UIBackgroundModes = location`; `allowsBackgroundLocationUpdates` and
`showsBackgroundLocationIndicator` in `PPLocationSource`, set for recording and following rather
than always. The usage string lives in `project.pbxproj` as
`INFOPLIST_KEY_NSLocationWhenInUseUsageDescription`, not in `Info.plist`, and must say that
recording continues with the screen off. `PrivacyInfo.xcprivacy` gets a look in the same session.
Watch the interaction with coarse mode's `pausesLocationUpdatesAutomatically = YES`: a run paused
in the background cannot be resumed by a user who is not looking at the screen.

### BG3 — `LocationPolicy` learns about the dark
A third input — backgrounded — and the rule that goes with it: recording means navigation accuracy
whatever the viewport says; neither recording nor following means coarse, or a full stop. Then
relax `updateIdleTimer` so recording no longer pins the screen awake (following still should). A
lit screen inside a pocket costs more than the receiver does. The existing `location_policy` test
target takes the new cases.

### BG4 — the ride that outlived the app
`recordingURL` is main-actor state that does not survive a jetsam kill, and under When-In-Use the
app is not relaunched, so the ride file is left valid but orphaned and the app forgets it was
recording. Decide and implement one of: notice the unfinished ride at launch, or let it appear in
the rides list as an ordinary ride. Either is defensible; silence is not.

### BG5 — the events reach a pocket or a wrist
GD2's alert events become `UNUserNotificationCenter` requests with a time-sensitive interruption
level while the app is backgrounded, suppressed while it is frontmost (GD4 has that case). The
mirror to a paired watch is the system's, not ours.

## Deliberately not in this plan

- A watchOS app, a complication, or WatchConnectivity.
- A Live Activity / Dynamic Island presentation (noted above as the likely successor to GD3).
- Automatic re-routing when the rider leaves the route. GD2 falls silent and says so; recomputing
  is a separate decision with its own battery and correctness story.
- `Always` authorization, significant-location change, and region monitoring.
- Voice instructions.

## Open for Chris, before GD1 starts

- The approach thresholds, and whether they scale with speed. A cyclist at 25 km/h and one walking
  want different warnings from the same corner.
- Whether an off-route rider should get a "you have left the route" alert or only silence.
- Sound, haptic, or both — and whether that is a setting or a decision.
