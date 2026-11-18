<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Ownship and moving map UI plan (OS)

Follows DK9 (`port/desktop-plan.md`, landed 2026-11-09). Goal: Peregrine has one ownship
state: time, position with altitude, heading and speed. An Ownship window shows it and scrubs
GPX replays through time. The moving map gets an editor whose toolbar carries the follow modes
and the feed. Several moving maps can run at once, and the topmost one drives the ownship.

## 1. Where things stand (DK9)

- `fv.movingmap` is a **static** type, so at most one can be open. It is toggled from the
  Overlay menu.
- `MovingMapDriver` (`DeskKit/fv_desk_moving_map.*`) owns the one feed. That feed is the demo,
  a track replayed by the wall clock (`ScriptedSource`, play-only, no seeking), or NMEA over
  TCP or UDP. Every setting is a window-wide `movingmap.*` key.
- The follow modes, snap and apron are toggled from the Overlay ▸ Moving Map submenu. The feed
  and the rest are set on the Overlay ▸ Options Moving Map page.
- The overlay draws no trail. Nothing outside the overlay knows where the ship is, apart from
  the status-bar text.

## 2. Decisions

Accepted by Chris 2026-11-09: OS-D2 (a GPX always opens as a moving map), OS-D4 (with a
playback rate), OS-D8 and OS-D9. OS-D10–D14 and the changes to OS-D7 and the phase order
come from the review of OS1–OS3 and were accepted by Chris 2026-11-09. The others are proposed.

**OS-D1 Ownship state lives in DeskKit and the window owns it.** `desk::OwnshipState` holds:
- time: UTC epoch seconds
- position, altitude MSL (m), true heading, ground speed, each with its own validity flag
  (a `PositionFix`)
- where the heading came from: reported or derived
- mode: none, live or replay
- the source overlay: a weak pointer and its name
- a generation counter, which DeskHost turns into `HostTick::ownship_changed`

It is plain state, not an overlay. Later consumers read it: the TA mask altitude (ledger §2a),
range and bearing from ownship, and the status bar.

**OS-D2 A moving map becomes a file overlay, one per feed.** This is FalconView's own shape:
`C_gps_trail` was a file overlay per receiver.
- **New ▸ Moving Map** creates a live map on the default feed.
- **Open** of a `.gpx` (or an NMEA log) creates a replay map whose track is the whole file.
- **Save** writes the trail as GPX through `GpxRecorder`. A replay map is already its file.
- The static toggle and the Overlay ▸ Moving Map submenu go away; the editor replaces them
  (OS-D5).
- `Desk::OpenMovingMapTrack` becomes a plain Open.
- Opening a `.gpx` currently has no overlay type at all, so nothing is lost.

**OS-D3 The topmost moving map is the ownship.** Stack order, as for every other "current"
rule.
- Only that map's camera moves the view. The others draw their ship and trail.
- Raising another map, by clicking it or in the layers panel, hands the ownship and the camera
  over. A live map that is no longer on top keeps listening, recording and moving its ship.
  The moving map editor acts on the topmost map.
- The slew is re-based on the hand-over, so the view glides instead of jumping.

**OS-D4 Replay time is one window clock; scrubbing samples the tracks, not a feed.**
- A replay map keeps its fixes in memory and answers `StateAt(t)`. It interpolates position,
  altitude and time between the bracketing fixes. Heading is the fix's own when reported, else
  the bearing of that segment.
- `Desk` has a `ReplayClock` with a time, a playing flag and a rate. While playing, the time
  advances by `dt × rate` each tick.
- Each tick, every replay map is placed at the clock time:
  - before its start: shown at its first fix, dimmed
  - after its end: shown at its last fix
  - its trail up to the clock time is "past"
- A seek resets the heading resolver and the slew (`ResetMap`), so a jump back does not derive
  a heading across the jump.
- `ScriptedSource` stays for Pippin and the tests. Desktop replay stops using it.
- Opening a GPX moves the clock to that file's last fix (OS-D9).
- The slider's range is the union of the open replay maps' [start, end].

**OS-D5 Follow modes belong to the window; snap and trail belong to each map.**
- Course Up / North Up and Continuous / Leave-the-apron describe the view. They are global
  `movingmap.*` user settings, and only the ownship map applies them.
- Snap to Road, Show Trail Dots and the feed are per overlay. They are stored in the overlay's
  properties, and so in workspaces.

**OS-D6 Feed setup: a "Default feed" plus a per-map feed dialog.**
- The Options page's feed fields are renamed **Default Feed** and are used only for New ▸
  Moving Map.
- The editor's **Feed…** button opens a Feed dialog for the edited map, with:
  - kind: Demo, NMEA TCP or NMEA UDP
  - host and port
  - for a replay map: the file (read-only) and the replay rate
- The dialog is a DeskKit model (an `OptionsModel` page built over the overlay's feed
  properties), so mac and GTK each draw it from data. It is the same mechanism as Overlay
  Options, and needs no new dialog type.

**OS-D7 Trail dots.**
- A live map keeps two things, so that the cap never shortens a saved file:
  - the recording: every raw fix received, which Save writes (OS-D2). It is bounded only by
    a hard limit (`movingmap.record_max`, default 500,000 fixes); at the limit the map stops
    recording and says so in its status text.
  - the drawn trail: the latest `movingmap.trail_max` points of the recording (default
    5,000).
- A replay map's trail is its whole file.
- With Show Trail Dots on, it stamps a dot at each trail point. Past points use the map's
  colour, and in replay the future points are faint.
- Dots closer than 3 px to the previous one are skipped, so a zoomed-out 28-minute ride does
  not draw 1,705 overlapping dots.
- A line through the trail is a separate option (`Show Trail Line`), off by default.

**OS-D8 Listen toggle, per map.** A live map can stop listening to its feed without being
closed.
- Off stops the source (a TCP connection is dropped; a UDP socket is closed). The ship and the
  trail stay where they were.
- On restarts the same feed, and the trail continues from where it stopped.
- If an off map is the ownship, the ownship stays at its last fix, and the window says the
  feed is paused.
- The state is stored in the overlay's properties, so a workspace reopens a map not listening.
- A replay map has no feed, so the toggle is disabled for it.

**OS-D9 Opening a GPX shows the whole ride.** Once the file is read, the replay clock moves to
the file's last fix and stops, so the full trail is drawn as past. This also applies when other
replay maps are open. The slider's range widens to take the new file in.

**OS-D10 One NMEA receiver, shared with AIS.** `ais::NmeaReceiver` moves to `fvkit/nav` and
becomes the only NMEA connection class.
- It already has what the moving map's own `NmeaLineSource` path lacks: reconnect with
  backoff, the off/connecting/live/silent/retrying states, HDT attachment and counters.
- The Desk owns the open connections, keyed by protocol, host and port. A moving map and the
  AIS overlay (AIS6) naming the same endpoint share one socket: fixes go to the map, AIS
  lines to the overlay.
- Listen (OS-D8) is the receiver's Start and Stop. The status text and the Ownship window's
  "feed paused" read the receiver's state; the driver's `dynamic_cast` chain goes away.
- The Feed dialog (OS-D6) and the AIS6 connections dialog are one DeskKit model.
- The demo stays a `ScriptedSource`.

**OS-D11 Each map holds its feed as a value.** `FeedConfig`: kind, host, port, demo speed,
demo scatter, snap to road, listening.
- New ▸ Moving Map copies it from the Default Feed settings. After that the driver reads the
  map's config and never the `movingmap.*` feed keys.
- The config is the map's properties, so the editor tools, the Feed dialog and workspaces all
  read and write the same fields.
- Changing one map's config restarts that map's feed only. A change on the Options page
  restarts nothing.

**OS-D12 The shell-neutral types live in `fvkit/nav`.** `OwnshipState`, `ReplayClock`,
`TrackStateAt`, `ReplayableFixes` and `ReadTrackFixes` move out of DeskKit. The Desk still
owns the instances.
- The consumers OS-D1 names are fvkit overlays and cannot include DeskKit headers. They read
  the ownship through a small `IOwnshipProvider` the Desk implements.
- Pippin and pyfvw can use the track sampling.

**OS-D13 One true heading, resolved in the driver.** `MovingMapStep` carries the map's true
heading as a `ResolvedHeading` with its reported-or-derived source.
- `TrackStateAt` returns a derived segment bearing apart from the fix, so it is not taken for
  a reported one.
- The Desk's own `HeadingResolver` and its resets on seek and hand-over go away. A raised map
  has its heading at once.
- The overlay's screen-angle resolver stays; it is what the symbol is drawn at.

**OS-D14 The Ownship window reads one model.** `OwnshipPanel` carries the formatted
read-outs, the clock (time, playing, rate), the slider range and the enabled state of each
control, under one generation.
- `HostTick::ownship_changed` fires when that generation changes: a new ownship fix, a clock
  move, a play or pause, a range change.
- `HostOwnship` is folded into it. The shells format nothing.
- A before-start replay map is dimmed by a draw opacity on the overlay, not by changing its
  colour, so the colour property saved in a workspace is never the dimmed one.

## 3. User interface

**Moving map editor** (registered like `RegisterRouteEditor`): a toolbar mode button with the
`movingmap` icon. It acts on the edited map, which is the current moving map.

| Tool | Kind | Acts on |
|---|---|---|
| Course Up / North Up | toggle | window (OS-D5) |
| Centre on Every Fix / Leave the Apron | toggle | window |
| Snap to Road | toggle | the edited map |
| Trail Dots | toggle | the edited map |
| Listen | toggle | the edited map's feed; disabled for a replay map (OS-D8) |
| Feed… | button | the edited map's Feed dialog (OS-D6) |
| Ownship… | button | the Ownship window |

Auto Centre on/off stays in the editor menu, not on the toolbar. Show the Apron stays a debug
item on the Options page only.

**Ownship window** (View ▸ Ownship…, non-modal, small). DeskKit has an `OwnshipPanel` model; the
shells draw it.
- **Read-outs:**
  - time UTC
  - position, in the app's coordinate format
  - altitude (ft or m, per the existing units setting)
  - true heading, with "derived" in grey when it was not reported
  - ground speed (kt or km/h)
  - source map name
  - mode (Live, Replay or No feed)
- **Time row:** `|◀` start, ▶ / ❚❚ play-pause, `▶|` end, a slider over [start, end] showing
  the clock time, and a playback speed menu (1×, 2×, 5×, 10×, 60×). The speed is a user
  setting, `ownship.playback_rate`.
- **Units:** `ownship.units`, nautical (ft, kt) or metric (m, km/h), on the Options page.
- **Live:** the read-outs track the topmost live map's latest fix. The time row is enabled
  whenever a replay map is open, and scrubs those maps while the ownship stays live; with no
  replay map open it is disabled.
- **Nothing open:** "No moving map", and every control is disabled.

## 4. Phases

| Phase | What | Done when |
|---|---|---|
| OS1 | `OwnshipState` in Desk, fed by the one DK9 map; `HostTick::ownship_changed`; `DeskHost` getters | gtests: a fix updates time, position, altitude and heading; the generation bumps |
| OS2 | Moving map as a file type: New, Open `.gpx` / NMEA, Save GPX, several at once, topmost = ownship (OS-D2, D3); static toggle and submenu removed; driver per overlay | gtests: two maps, raising one moves the camera's owner; a saved trail reopens identical |
| OS3 | Replay clock and `StateAt(t)` seek (OS-D4); opening a GPX moves the clock to its end (OS-D9) | gtests: seek forward and back and the interpolated state; slider range over two files; open lands on the last fix; live maps ignore the clock |
| OS7 | Ownship window on mac: `OwnshipPanel` and the clock's DeskHost API (OS-D14) | gtests: the generation moves on a seek with a live ownship; time row enabled with a live map on top of a GPX. In-app check with `testdata/kiawah_cycle.gpx`: scrub, play at 10×, live UDP from a phone |
| OS3b | Groundwork: `FeedConfig` per map (OS-D11), shared `NmeaReceiver` (OS-D10), heading in the step (OS-D13), move to `fvkit/nav` (OS-D12); a demo map is not dirty | gtests: a TCP feed that drops reconnects; two maps on one endpoint open one socket; a replay heading with no reported course reads derived; an Options change restarts no feed |
| OS4 | Moving map editor and toolbar (§3), Listen (OS-D8), window follow modes, Feed dialog per map and Default Feed rename (OS-D6) | gtests: tools flip the right setting on the right object; Listen off stops fixes and on resumes them; Apply on the dialog restarts only that map's feed; workspace round-trip, including a live map never saved |
| OS6 | Trail dots, recording kept apart from the drawn trail (OS-D7) | gtests: dot count with thinning; a save after the trail cap holds every fix; golden image of past and future dots |
| OS8 | GTK: brief to `port/linux-plan.md` (row LX-OS) | Linux PR merged and back-ported |

OS5 is merged into OS4: with `FeedConfig` in place the dialog is a page over the map's
properties. OS7 comes first because the app has no way to play a replay until it lands.

OS1 done 2026-11-09: `DeskKit/fv_desk_ownship.h` `OwnshipState` and `Desk::ownship()`, fed
by the one DK9 map; `DeskHost::Ownship()` returns a `HostOwnship` of plain values, and
`HostTick::ownship_changed` reports a change. The heading is a true bearing from the Desk's own
`HeadingResolver` with no degrees-per-pixel; the overlay's derived heading is a screen angle.
A fix with no time is stamped on arrival (`time_from_fix` false). A demo feed counts as live,
a track as replay.

OS2 done 2026-11-09: DeskKit `DeskMovingMap` (`MovingMapOverlay` + `app::Persistence`):
FileNew names it "Moving Map N"; FileOpen reads a `.gpx` or `.nmea` into its track (fewer
than two positions is an error); FileSaveAs writes the track, or the live trail of raw
(unsnapped) receiver fixes, as one GPX track. A live map is dirty once it has a trail.
`MovingMapFileType()` replaces fvkit's static `fv.movingmap` through the new
`OverlayTypeRegistry::Replace`; PythonView keeps the static type. One `MovingMapDriver` per
overlay. `Desk::OwnshipDriver` is the topmost (`FirstOfType`). Only it follows the view; the
others re-base their slew every tick. `SyncOwnship` runs on add, close, reorder and each tick,
and re-bases the new owner's slew on the view. The Overlay ▸ Moving Map submenu, its commands,
`OpenMovingMapTrack` and the `track` feed choice are gone (the Options page keeps the default
feed, replay speed and gap, and the follow settings until OS4). Replay still plays through
`ScriptedSource` from the file's start; OS3 replaces it.

OS3 done 2026-11-09: `TrackStateAt` samples a track at a time (linear position, altitude and
speed; heading from the earlier fix, else the segment bearing; phase before/during/after; `past`
count for the OS6 trail); `ReplayableFixes` drops unpositioned and non-increasing-time fixes
(timeless tracks get 1 s spacing). `ReplayClock` (time, playing, rate) lives in the Desk:
`replay_clock()`, `ReplayRange`, `SeekReplay` (clamps, resets heading/snap/slew), `SetReplayPlaying`
(from the end restarts), `SetReplayRate`; `TickMovingMap` advances it by dt×rate and stops at the
end. `ScriptedSource` no longer drives replay; the driver pushes a fix only when the clock time
changes. Opening a GPX seeks to its last fix, paused. A replay map before its start is drawn at its
first fix, alpha 96; live maps ignore the clock. `movingmap.track_time_scale` became
`ownship.playback_rate`; `movingmap.track_max_gap_s` is dropped from Peregrine. No DeskHost API
until OS7.

OS7 done 2026-11-09: DeskKit `OwnshipPanel` and `BuildOwnshipPanel` (`fv_desk_ownship.h`): the
formatted read-outs ("—" when unknown), status, the time row's range, clock text, playing and
rate index, under `Desk::OwnshipPanelGeneration` (ownship generation plus a clock generation
bumped by seek, play/pause, rate, each playing tick and a replay map opening or closing).
`Desk::SetReplayRate` saves `ownship.playback_rate`; new `ownship.units` setting. View ▸
Ownship… (`view.ownship`, Primary+Shift+M) asks the shell through `ShowOwnship`. DeskHost:
`HostOwnship`/`Ownship()` replaced by `HostOwnshipPanel`/`OwnshipPanel()`, plus
`TakeOwnshipRequest`, `OwnshipRateCount`/`Label`, `SetOwnshipRate`, `SeekReplay`,
`SetReplayPlaying` (the last two under the stack lock); `HostTick::ownship_requested`, and
`ownship_changed` now follows the panel generation. A before-start replay map is dimmed by
fvkit `MovingMapOverlay::SetDrawOpacity`, leaving `color()` alone. Formatters moved to
`fv_desk_format.{h,cpp}`. mac: `OwnshipWindow.swift` (floating panel); a file handed to the
app at launch is now opened once the window exists. In-app check with
`testdata/kiawah_cycle.gpx`: opens on its last fix, scrub, play and pause. The rate menu (gtest
only) and live UDP from a phone were not checked in the app.

OS3b done 2026-11-09: OS-D12: `fvkit/nav/track_replay.{h,cpp}` (ReadTrackFixes,
ReplayableFixes, ReplayPhase, TrackState, TrackStateAt, ReplayClock) and `fvkit/nav/ownship.h`
(OwnshipMode, OwnshipState, IOwnshipProvider, implemented by Desk) moved from DeskKit into
namespace fv; `fv_desk_ownship.h` keeps the panel/format bits and UtcNowSeconds. OS-D13:
`TrackStateAt` no longer writes the segment bearing into `has_true_heading`; `TrackState`
carries `heading` (ResolvedHeading) and `segment_from`/`segment_to` (first/last segment stands
in outside the track); `MovingMapStep` carries `heading`; each `MovingMapDriver` keeps its own
true-bearing `HeadingResolver` and `heading()`, so a raised map has its heading at once, and
Desk's `ownship_heading_` is gone. New `HeadingResolver::Prime(point)`: after a seek the driver
primes the overlay's screen-angle resolver one segment back. OS-D10: `ais::NmeaReceiver` is now
`fv::NmeaReceiver` (`fvkit/nav/nmea_receiver.{h,cpp}`; `ReceiverConfig::operator==`,
`SetEmitPerSentence`); PippinKit `PPAis.mm` updated. DeskKit `fv_desk_feeds.{h,cpp}`:
`FeedReceivers` (Desk-owned, keyed by protocol/host/port, one socket per endpoint, closes with
the last `FeedSubscription`) and `FeedSubscription`. The driver's `NmeaLineSource` path is
gone; status text reads the receiver state (off/connecting/live/silent/retrying, fixes, last
error, "paused" when not listening); TCP reconnects with the 2-60 s backoff. AIS lines are not
routed to subscribers yet (AIS6). OS-D11: DeskKit `FeedConfig` (kind, host, port, demo speed,
demo scatter, snap to road, listening) on each `DeskMovingMap`; `DefaultFeedConfig(settings)`
is copied at driver creation; `Desk::SetMovingMapFeed(overlay, config)` restarts that map's
feed only. `MovingMapFeedChanged` became `MovingMapOptionsChanged` (re-applies follow, symbol
and playback settings, restarts no feed). Snap to road is per map. Not exposed as overlay
properties or in workspaces yet (OS4). A demo map is not dirty after its first fix. Tests: new
FeedReceivers x2, TrackReplay x3 (two moved from TrackStateTest), MovingMapTest x7 new; the
trail test uses an unanswered TCP feed; full ctest 3288 passed on macOS; Peregrine.app built,
not checked in the app. LX: DeskKit + fvkit, no DeskHost change; the GTK shell is unaffected
unless it names `desk::OwnshipMode`/`ReplayPhase` qualified (now `fv::`).

OS4 done 2026-11-09 (OS5 merged in): DeskKit `fv_desk_moving_map_editor.{h,cpp}`
`MovingMapEditor` / `RegisterMovingMapEditor` (registered after the `fv.movingmap` Replace).
Tools: Course Up (`movingmap.auto_rotate`), Centre on Every Fix (`movingmap.continuous`), Snap to
Road, Listen and Feed... (edited map; Listen disabled for a replay), Ownship... (`view.ownship`),
and Auto Centre (`movingmap.auto_center`) with no icon, so it stays in the editor menu and off
the toolbar. The toolbar has an `editor.mode.fv.movingmap` button. The edited map is
`Desk::EditedMovingMap()`: the edited moving map, else the topmost; New > Moving Map enters the
editor. Trail Dots is not on the toolbar yet (OS6). Window follow modes: 
`Desk::SetMovingMapFlag(key, on)` writes the setting and the user settings and re-applies to every map. OS-D8: 
`Desk::ToggleMovingMapListening`; `FeedConfig::listening` false stops the feed (demo source dropped,
receiver subscription released); the driver hands the last fix back to the overlay after a
restart (`MovingMapOverlay::SetSource(nullptr)` clears its fix and that fvkit overlay is shared
with Pippin, so it was left alone) and that fix is not recorded twice in the trail; the ownship
keeps its position while not listening. OS-D11: `FeedConfigProperties` (feed, nmea_host,
nmea_port, listening, speed_mps, noise_m, snap_to_road; a replay map declares snap_to_road only)
held by `DeskMovingMap` with `feed_generation()`; `MovingMapDriver::feed_stale()`;
`Desk::TickMovingMap` restarts a stale map (`Desk::RestartMovingMap`), so a change by any route
restarts that map alone. `SetMovingMapFeed` now accepts replay maps (for snapping) and re-places
a replay at the clock. These are deliberately not the overlay's `AsProperties()`: the Options
page instantiates the type and would show them under the `movingmap.` prefix, colliding with the
default feed keys and pushing into every open map on Apply. OS-D6: `Desk::OpenMovingMapFeed`
shows a one-page `OptionsModel` built by new `OptionsPage::ForObject` (a page over an object's
properties that writes no settings; `persisted_` false; new 
`OptionsModel(std::unique_ptr<OptionsPage>)` ctor); new `DeskShell::ShowFeedOptions`, `DeskHost` options kind `kOptionsFeed =
2`, mac `OptionsWindow` titles it "Moving Map Feed"; Apply goes through
`Desk::ApplyOverlayOptions` and restarts only that map. Deviation from OS-D6: a replay map's
dialog shows Snap to road only; the file name is in the page title ("Feed for <name>") and the
replay rate stays in the Ownship window (PropertySpec has no read-only flag). The Options page's
feed section is titled "Default Feed" and Snap to road help says it is the default for a new
map. Workspaces: fvkit `app::Persistence` gains virtual `ConfigurationSpec()` (default
`file_spec()`) and `ConfigurationProperties()` (default null); 
`OverlaySession::BuildConfiguration` writes every configuration property as `<row>.prop.<key>`, plus `<row>.new =
true` when the spec is empty; `RestoreConfiguration` recreates a `new` row with `NewFileOverlay`
and loads `prop.*` after reopening any row. A live map writes no file (its saved GPX is a
recording, not its source) and reopens live on its feed with its name; a replay map reopens from
its file and loads snap_to_road. mac: Toolbar.swift SF Symbols for course_up, continuous,
snap_to_road, listen, feed, ownship. Tests: new `moving_map_editor_test.cpp` x7 (tools flip the
right setting on the right object; Listen off stops fixes and on resumes them; Listen disabled
for a replay; Apply on the Feed dialog restarts only that map and writes no setting; the Options
page is the Default Feed; a workspace keeps a live map never saved; a workspace reopens a replay
from its file); `DeskTest.TheToolbarShowsOnlyCommandsWithIcons` now expects the moving map mode
button; full ctest 3299 passed on macOS; Peregrine.app built, not checked in the app. LX:
DeskKit + fvkit app layer; the GTK shell must handle `ShowFeedOptions` / `kOptionsFeed` as a
third options dialog kind and add icons for the six new tool names (OS8 brief).

OS6 done 2026-11-10: DeskKit `DeskMovingMap` splits the live trail into the recording
(`recording()`: every raw fix, what Save writes, capped by `movingmap.record_max`, default 500000;
at the limit it stops recording, `recording_full()` is true and the driver's status text appends
", recording full at N fixes") and the drawn trail (`trail()` now returns a `FixSpan`: a replay's
whole track, else the newest `movingmap.trail_max` (default 5000) of the recording; tests that
used `trail()` for the vector use `recording()`). `TrailStyle` (dots, line, trail_max, record_max)
is set by `MovingMapDriver::ApplySettings` from `movingmap.show_trail_dots` (default true),
`movingmap.show_trail_line` (default false), trail_max and record_max. `SetReplayTime` (the driver
calls it on each placed replay time; FileOpen sets it to the last fix) splits a replay into past
and future. `DeskMovingMap::OnDraw` draws the trail under the ship, then the fvkit
`MovingMapOverlay::OnDraw` (fvkit overlay unchanged, shared with Pippin). Dots: 6 px x
`symbol_dpi_scale`, map colour with a black edge (alpha 160) for past, alpha 64 and no edge for
future, all scaled by the draw opacity (so dimmed before start); a dot closer than 3 px (x dpi,
1e-6 tolerance) to the last drawn one is skipped; off-canvas dots are not stamped. Line: 2 px x
dpi, past and future runs joined at the split, broken at unprojectable fixes. Editor: "Trail Dots"
tool (icon `trail_dots`, on the toolbar) after Centre on Every Fix; "Trail Line" with no icon
(menu only) after Auto Centre; both through `Desk::SetMovingMapFlag`. Options page: new "Trail"
section with the four keys; `peregrine.ini.sample` [movingmap] documents them. mac `Toolbar.swift`:
`trail_dots` is SF Symbol `point.3.connected.trianglepath.dotted`. Tests: new
`moving_map_trail_test.cpp` x5 (thinning: 100 fixes 1 px apart give 34 dots, 20 at 5 px give 20;
dots off draws none; trail cap 5 with 12 fixes saves all 12; recording stops at the limit; replay
golden: 5 past dots alpha 255 in map colour and 5 future alpha 64, FNV-1a hash
15521372013564688658, checked visually), plus
`MovingMapTest.TrailSettingsReachEveryMapAndAFullRecordingIsReported`; full ctest 3324 passed on
macOS; Peregrine.app not built or checked in the app. LX: DeskKit only; the GTK shell needs an icon
for the new `trail_dots` tool name (OS8 brief).

OS1 → OS2 → OS3 → OS7 → OS3b → OS4 → OS6 are done (OS6 2026-11-10). The OS8 brief is written (`port/linux-plan.md`, LX-OS, 2026-11-10); OS8 closes when the Linux PR is merged and back-ported.

**Known gaps after OS7** (review 2026-11-09), each with the phase that closes it; the four OS3b rows were closed 2026-11-09:

| Gap | Closed by |
|---|---|
| Any Options change restarts every live feed (`MovingMapFeedChanged`) | OS3b (closed) |
| The TCP feed does not reconnect | OS3b (closed) |
| A replay heading always reads as reported: `TrackStateAt` writes the segment bearing into `has_true_heading` | OS3b (closed) |
| A demo map is dirty after its first fix | OS3b (closed) |
| The live trail grows without limit | OS6 (closed) |

## 5. Open questions for Chris

Confirmed by Chris 2026-11-09: with a live feed and GPX files open together, the slider
scrubs the GPX maps and the ownship follows the topmost map, live or replay.

Confirmed by Chris 2026-11-09: raising a moving map, by clicking it or moving it up in the
layers panel, makes it the ownship, and the editor acts on that map (OS-D3 unchanged). A live
map below it keeps listening, recording its trail and moving its ship; only the view stops
following it.

Settled in OS4, not a question: a workspace stores a live map that has never been saved as a
`new` row (`<row>.new = true`, no file) with its feed properties as `<row>.prop.<key>`; it reopens
live on that feed. A replay map reopens from its file.
