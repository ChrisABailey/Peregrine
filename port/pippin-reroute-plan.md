<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Pippin junction turns and re-route plan (JT, RR)

Follows the guidance plan (`port/archive/pippin-guidance-plan.md`, GD and BG). Two goals:
turns are announced at junctions where the road name does not change (JT), and an off-route
rider can re-route with one tap (RR). Status: planned 2026-11-09; JT1 done 2026-11-09 (JT-D1–D4 implemented, awaiting Chris's acceptance); JT2 done 2026-11-10 (not yet seen on the phone); RR1 done 2026-11-10; RR2 done 2026-11-10; RR3 done 2026-11-10 (simulator-checked, not yet ridden); RR4 done 2026-11-10 (Live Activity simulator-checked; notification action and the locked-phone device check outstanding).

## 1. Where things stand

- `nav::BuildManeuvers` (`fvkit/nav/maneuver.cpp`) takes its candidates from leg boundaries
  only. `routing::RouteLeg` is a run of arcs sharing a name and class, so a turn inside a leg
  is never a candidate.
- `fv::ManeuverShapeOf` (`RouteKit/fv_route_maneuvers.cpp`) fills `nav::RouteShape` from the
  route's geometry and legs. `routing::Route::nodes` holds the graph nodes along the route and
  is not passed on.
- Off route, `nav::Guidance` emits `kOffRoute` once (25 m held 5 s) and is silent until the
  rider is within 15 m of the line. Pippin shows the off-route banner, notification and Live
  Activity card. Nothing re-plans.
- The router bars a U-turn at an intermediate stop as a hard ban with a retry
  (`LegSeed::banned_toward`, `banned_arc`). It has no notion of the rider's heading at the
  first stop. `Router::SourcesFor` prices each direction of departure from a mid-road anchor
  as its own `Terminal::cost`.
- Pippin's route is a document (`Documents/current.fvrte`): waypoints, profile, options. Every
  plan is a re-plan of that document.

## 2. Evidence: the 2026-11-08 Atlanta ride

`Route.fvrte` (33.78421,-84.44998 to 33.82273,-84.43141, bicycle, avoid hills) planned on
`atlanta.fvroad` with City Rules is 8.99 km, 18 legs, 15 maneuvers. The return leg of
`ride-2026-11-08-1210.gpx` replayed through `nav::Guidance` gives:

| Along route | Route | Announced | Rider |
|---|---|---|---|
| 2.34 km, Marietta Rd to Pine St | right 87° | 385 m, 51 m, 11 m | straight on, off route about 130 s |
| about 3.4 km, unnamed path | none (rejoining) | no | crossed the line, 100 m past it, looped back |
| 5.13 and 5.19 km, Marietta Rd to Whetstone Creek PATH | left, then right 49° after 66 m | yes | straight on, 140 s to return |
| 6.16 km, La Dawn to Dunseath | left 88° | 138 m, 33 m, 6 m | overshot 50 m |
| 6.66 km, Coronet Way | — | — | cut the corner, probably on purpose |

The phone was locked and on vibrate and the watch was recording a workout (Fitness Focus), so
the announced alerts most likely did not reach the rider. That is a settings matter and is
outside this plan (JT-D5).

Unannounced junction turns on the same route, each with another rideable exit:

| Along route | Turn | Straightest other exit | Leg |
|---|---|---|---|
| 2.70 km (nodes at 2699 m and 2711 m) | right 87° | 9° off straight, service | unnamed service, 600 m |
| 4.80 km | right 90° | 0.4° off straight, unclassified | unnamed unclassified, 2.0 km |

## 3. Decisions

Proposed unless marked. Accepted by Chris 2026-11-09: JT-D5, and the scope (JT and RR only).
Accepted 2026-11-10: RR-D4 as amended.

**JT-D1 Junctions enter `RouteShape`, and fvkit still does not link Routing.**
`RouteShape` gains `junctions`: a geometry index and the bearings of the other usable exits,
measured about 15 m out along each exit. RouteKit fills it from `Route::nodes` and the graph.
"Usable" is `ArcUsable` under the route's own `RouteOptions`, so an exit the router would
refuse is not a way to go wrong.

**JT-D2 A junction is announced when either rule holds.** (Rule 1 tightened in JT1, 2026-11-09: a
56° bend on Ruddy Turnstone fired only because a 25 m stub left 138° sharply back.)
- The route turns `slight_deg` (45°) or more and another usable exit turns less than
  `sharp_deg` (110°) from straight on.
- The route turns `straight_deg` (20°) or more and another exit is straighter than the
  route's.

A bend with no junction is not announced.

**JT-D3 One corner, one maneuver.** A junction within `angle_window_m` (15 m) of a leg-boundary
candidate, or of another junction candidate, merges into it. The leg boundary keeps its road
name. Of two junction candidates the one with the larger turn is kept. As built: only an
announced leg boundary absorbs a junction (one dropped as a name change does not); junction
windows are bounded by leg boundaries only, not by neighbouring junctions.

**JT-D4 Wording says the road continues.** `Maneuver` gains `continues` (true when the road
joined has the name of the road left, or both are unnamed). The banner, notification and Live
Activity read "Left to stay on NAME", or "Left on the path" (or road, by class) when unnamed.

**JT-D5 The silent switch is not overridden (accepted).** No playback-category audio. Alerts
on a locked phone stay notifications, under the rider's ringer and Focus settings.

**RR-D1 The U-turn penalty is soft and applies at the first stop only.** `RouteOptions` gains
`start_heading_deg` (unset by default) and `start_u_turn_seconds`. In `SourcesFor`, a terminal
that sets off more than 90° from the heading costs the penalty. A node anchor applies it to
each first arc the same way. Intermediate stops keep the existing hard ban. Default 45 s,
setting `[routing] reroute_u_turn_seconds`.

**RR-D2 A detour is planned to the remaining route, not back to where the rider left.** Stops
are the rider's position, each waypoint whose `stop_geometry_index` is beyond the last
on-route projection, and the destination. Profile, options and adjustments are the route's
own.

**RR-D3 A detour is guidance state, not a document edit.** It replaces the guided line and the
maneuver list until arrival, cancel, or a new plan of the document. `current.fvrte` and its
waypoints are not rewritten. The original line stays drawn as the ghost.

**RR-D4 The button reports the choice it is making (accepted 2026-11-10, as amended in RR2).**
RouteKit plans the detour with the penalty, and also plans turning round: the best route that
sets off opposite the rider's heading, through the same stops. "Back" is the distance along
it until it is within 15 m of the original line. A route via the point the rider left was
rejected: the router must arrive there in the route's direction, which added about 850 m at
the crossing. The button reads "Re-route: +0.3 mi" when the detour does not start with a U-turn, and
"Turn around: 250 ft back" when it does or when turning round is shorter by a margin
(default 200 m, a setting).

**RR-D5 One tap applies it.** No confirm step. Cancel restores the original line and its
guidance.

**RR-D6 The button appears when off route has held and the rider is still moving away.**
10 s after `kOffRoute` with cross-track not decreasing. It goes when the rider rejoins. The
off-route notification carries a "Re-route" action and the Live Activity shows the same
choice; both are subject to JT-D5.

**RR-D7 Re-routing is never automatic in this plan.** The Coronet Way shortcut is a rider
leaving the line on purpose.

## 4. Phases

One session each. Each ends with tests green on macOS and a ledger row.

**JT1 Junction candidates (fvkit, RouteKit). Done 2026-11-09.** Kiawah fixtures 17 -> 21 maneuvers
(+24%; 22 before the JT-D2 tightening); Atlanta section 2 15 -> 17, new ones at exactly 2699 m and
4800 m, none lost; 8 ManeuverJunction tests plus 2 RouteManeuvers tests; suite 3278 on macOS.
RouteKit overloads `ManeuverShapeOf(route, graph, options)` and `ManeuversOf(..., settings)`.
- `RouteShape::junctions`, `Maneuver::continues`, the rules of JT-D2 and JT-D3 in
  `BuildManeuvers`.
- `ManeuverShapeOf(route, graph, options)`; `RoutePlanner` passes its graph and options.
- Tests: synthetic junctions in `nav_maneuver_test` (turn with and without another exit, a
  fork, two nodes on one bend, a junction on a leg boundary). An Atlanta test plans the
  section 2 route and expects maneuvers at about 2.70 km and 4.80 km and none of the existing
  15 lost. Skips without `testdata/OSM/atlanta.fvroad`.
- Measure: maneuver count before and after on the Kiawah fixtures. If the count grows by more
  than about a quarter, tighten JT-D2 before going on.

**JT2 Wording (Pippin). Done 2026-11-10.** `continues` and the road class through `PPGuidance`
and `PPGuidanceEvent`; `GuidanceNotice.roadLine` is the one phrase for the notification body,
the banner and the Live Activity: "Onto NAME", "To stay on NAME", "On the path" / "On the road"
(unnamed, by class), else nothing. The banner now reads "Onto NAME" where it showed the bare
name. `RideActivityContent.road` became `roadLine`. Tests: `guidance_notice`, `ride_activity`.

**RR1 Start heading in the router (Routing). Done 2026-11-10.** RR-D1, with `fvgraph route --heading DEG
[--u-turn-seconds S]`. Tests: a mid-road start whose shorter way is behind the rider goes ahead
when the way round costs less than the penalty and turns round when it costs more; unset
heading gives the same route as today.
- `RouteOptions::start_heading_deg` (NaN = unset) and `start_u_turn_seconds` (45);
  `Route::start_u_turn` reports a departure more than 90 degrees from the heading, bearing
  measured over the first 15 m of the step. Seconds become cost through the profile's delay
  conversion; without a profile a distance query uses 50 km/h.
- Mid-arc start: the terminal setting off behind pays the penalty in `SourcesFor`. Junction
  start: the start node is split and each first arc pays it in both searches. Same-road
  destination behind: the direct path plus penalty is compared with the search's cost.
- Extension of RR-D1 (for Chris's acceptance): on the first leg a U-turn at a first junction
  (each end of the start road, or the far end of each arc out of a junction start) also pays
  the penalty. Without it the search evaded the charge by riding to the next junction and
  turning there (any junction within about 100 m at bike speed).
- Router fix found on the way: path reconstruction stopped at the first state equal to a
  source terminal, so a path that re-reached a seeded state more cheaply (ahead, round a block,
  back past the start) was read back as the turn-round from that source. Both searches now stop
  only at a terminal still holding its seed (no parent). Latent before: an unpriced seed is
  always the cheapest way into its own state.
- Not done: the `[routing] reroute_u_turn_seconds` setting (belongs with RR2/RR3, where Pippin
  passes options).
- `fvgraph route ... --heading` prints "sets off with/turning round from heading N".
- Atlanta check (fvgraph, bicycle, to 33.8227,-84.4314): three greenway starts with heading
  opposite the route turn round at 45 s and go ahead at 100000 s (33.7837621,-84.4448019
  heading 13: 8.66 km turning round, 9.29 km ahead).
- Tests: `router_heading_test.cpp`, 6 cases x bidirectional/Dijkstra = 12 (mid-road start,
  same-road destination behind, U-turn at first junction, junction start, loop back past the
  start, no heading ignores the penalty). Full ctest 3343 passed on macOS.

**RR2 Detour planning (RouteKit). Done 2026-11-10.** Tests from the section 2 ride.
- `RoutePlanner::PlanDetour(DetourRequest, waypoints, route, options)` returns `DetourPlan`:
  `reroute` (heading penalty, u_turn_seconds 45), `turn_around`, `stops_ahead`, `remaining_m`,
  `extra_m` (can be negative: an avoid-hills plan's detour may be shorter than what was left),
  `back_m`, `reroute_starts_with_u_turn`, `pick` (kNone/kReroute/kTurnAround).
  `RoutePlanOptions` gained start_heading_deg/start_u_turn_seconds (passed through
  BuildOptions); `RoutePlan::start_u_turn`.
- Departure from RR-D4 (accepted by Chris 2026-11-10, RR-D4 amended): turning round is priced as a full plan that
  sets off opposite the heading (penalty `turn_around_force_seconds` 3600 on going ahead), not
  as a route to the point the rider left. Routing via the leave point forced the router to
  arrive there in the route's direction and added about 850 m at the crossing. `back_m` is
  metres along that plan until within `rejoin_m` (15 m) of the original line. The turn-round
  plan is dropped when it still goes ahead, and absent without a heading.
- Pick: turn around when the reroute starts with a U-turn or did not route, or
  `turn_around.length_m` + margin (200 m) < `reroute.length_m`; else reroute.
- Ride results (fixes from the GPX): Pine St (33.7970794,-84.4399671 hdg 333, last along
  2347): reroute ahead along Marietta/Laurel Ave, 6222 m vs 6647 m left, turn-round 6776 m,
  125 m back. Rejoin crossing (33.8013988,-84.4428205 hdg 320, 3420): the reroute itself turns
  round, pick turn around, 5615 m, 177 m back. Whetstone (33.8100760,-84.4539332 hdg 325,
  5210): reroute ahead via Westside Blvd and McArthur Blvd onto the PATH, +58 m, 120 m shorter
  than turning round (3962 m, 143 m back); the plan expected turn around, the router's answer
  is reroute.
- Not done: the `[routing] reroute_u_turn_seconds` and turn-around margin settings (RR3,
  where Pippin passes options).
- Tests: `route_detour_test.cpp`, 6 (3 Atlanta ride cases, waypoints already passed dropped,
  no heading only reroutes, no route is no detour). Full ctest 3353 passed on macOS.

**RR3 Detour state and the button (PippinKit, Pippin). Done 2026-11-10.** RR-D3, RR-D5, RR-D6 on
screen. Simulator-checked on the Atlanta build with the section 2 GPX; not yet ridden.
- `RouteOverlay::SetDetour`/`ClearDetour` (RouteKit, additive): the detour draws as the route
  and the planned line in the ghost colour; `FollowRoads` and `ClearRoads` drop it.
- `RouteStore::PlanDetour(position, heading, last_along_m)` returns a `DetourOffer` (pick,
  length, extra_m, back_m) and keeps the plan; `ApplyDetour` makes it the guided line,
  refused once the route has been planned again; `CancelDetour`. Off a detour, the next offer
  is planned against the detour's own stops and line. `RoutePath` and `RouteManeuvers` follow
  the detour; GPX export, `RouteSpotNear` and the snapshot's beach headings stay on the planned
  line. The document is not written.
- `pippin::DetourTrigger` (`PPDetourTrigger.h`) is RR-D6: ready 10 s after `kOffRoute` when the
  cross-track is not 2 m smaller than 5 s before; stays until rejoin. It keeps the last along
  on the line (before any fix on it, the latest projection) and the heading (receiver course,
  else bearing over 3 m of movement).
- `PPMap` plans the offer on the render queue when the trigger is ready, re-plans it every
  100 m, and re-plans at the tap when the rider is 30 m on. `PPGuidance` carries `detourKind`,
  `detourExtraMeters`, `detourBackMeters`, `detouring`; `applyDetour`/`cancelDetour` re-seat the
  trip and guidance through `routeFromSnapshot`. Settings `[routing] reroute_u_turn_seconds`
  (45) and `reroute_turn_around_margin_m` (200).
- Addition to RR-D3, for Chris's acceptance: leaving GPS mode ends a detour (the ride is over).
- The button ("Re-route: +0.3 mi" / "0.2 mi shorter" / "same distance", "Turn around: 410 ft
  back", then "End detour") is its own view under the banner, fed by `MapModel.detourButton`
  published only on change. Inside the per-frame `LiveMapReader` it was rebuilt with every
  frame and dropped taps while the rider moved (seen in the simulator); moved out, apply and
  end both work at 11 mph.
- Simulator, Pine St: off route then "Re-route: 0.3 mi shorter" (RR2's 425 m); applied, the
  banner read "450 yd, To stay on Marietta Road Northwest", 4.1 → 3.8 mi, the planned line grey
  behind; End detour put back 4.1 mi and the Pine St turn. Screenshots
  `apps/Pippin/docs/rr3-detour-offer.png`, `rr3-detour-guided.png`.
- Seen, not changed: right after applying, the banner is empty until the next fix (the
  guidance restarts); after End detour the first fix is a baseline and can show the planned
  route's corner as "Now" until off route holds again (existing `Guidance` behaviour).
- Tests: `RouteOverlay.ADetourDrawsOverThePlannedLineUntilTheNextPlan`, `DetourTrigger.*` (7),
  `RouteStore` detour cases (5, Atlanta ones skip without the graph), `pippin_detour_text`.
  Full ctest on macOS: 3393 tests, no failures.

**RR4 Lock screen (Pippin). Done 2026-11-10.** Simulator-checked (Live Activity); the notification
action not yet seen (the simulator never authorizes notifications); device check with the phone
locked still to do.
- Notification: the off-route notice (`guidance.offroute`) carries the offer.
  `GuidanceNotice.detourActions(from:to:onRoute:)`: a new offer, or a change between re-route and
  turn around, replaces the notice and alerts; a new distance on the same kind replaces it
  silently (passive, no sound); the offer withdrawn while off route reverts to the plain
  off-route text silently; on rejoin the notice goes. Body is the button's label ("Re-route:
  +0.3 mi"). Categories `guidance.detour.reroute` / `guidance.detour.turnaround`, one action
  `guidance.detour.apply` titled "Re-route" / "Turn around", no options: runs in the background
  without the passcode; mirrored to a paired watch. `TurnNotifications` is now the
  notification-centre delegate (no `willPresent`, so foreground behaviour is unchanged).
  `GuidanceNoticeAction.post` gained `category` and `alerts` (defaulted). Background only, driven
  from the per-fix background path in `MapModel` (`noticeDetour`).
- Live Activity: `RideActivityContent` gained `detour` and `detourSymbol`;
  `RideActivityPolicy.offRoute(detour:detourSymbol:)` replaces the constant; a changed offer is
  sent at once. The lock screen card and the expanded Dynamic Island show the button, a
  `Button(intent: ApplyDetourIntent())`: a `LiveActivityIntent` (`DetourIntent.swift`, compiled
  into the app, Osprey and PippinActivity) that runs in the app's process and calls
  `DetourAction.apply`, set by `MapModel` to `applyDetour()`.
- Change for Chris's acceptance: applying or ending a detour no longer ends the Live Activity
  (`MapModel.routeContinuesRide`). An activity can only be started in the foreground, so ending
  it on a lock-screen apply would lose the card for the rest of the ride.
- Simulator (Atlanta build, section 2 route, ride east past the greenway's turn south at 570 m,
  app in background, locked): the card read "Off route" with "Re-route: +0.3 mi", then "Turn
  around: 0.2 mi back" after the 100 m re-plan; tapping it ran the intent in the app without
  unlocking or foregrounding (linkd: Target App, didForeground NO); the card stayed and moved
  to the detour's first turn, "950 yd 2 min". Screenshots `apps/Pippin/docs/rr4-lock-screen-offer.png`,
  `rr4-lock-screen-applied.png`.
- Tests: `guidance_notice` +detour cases (46 checks), `ride_activity` +5 offer checks (48).
  Pippin ctest group 20/20 green on macOS; Pippin and Osprey simulator builds succeed.

## 5. Open questions

- Driveway or parking-aisle exits in JT-D2: answered by JT1's counts. No exclusion needed; the
  sharp-back rule covered the one spurious case.
- Whether the detour should prefer rejoining the original line when the costs are close. Not
  planned; RR-D2 lets the router choose.
- `Guidance::AtMeters` has no upper clamp. A derived speed spike in a GPX replay fired the
  junction ring 918 m early. Live fixes carry their own speed, so the phone is unlikely to
  show it. Still open (not done in JT1).
