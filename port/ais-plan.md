<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# AIS — vessel traffic overlay for Osprey and Peregrine

**Status: APPROVED, 2026-10-26.** AIS-D1 to AIS-D7 decided by Chris the same day.

## Goal

Live AIS traffic drawn on the chart in Osprey (iOS) and Peregrine (mac, Linux): ship symbols by
type, heading and course vector, trails, ageing, tap for details, and closest-approach warnings.
Two sources from the start: the Open Waters AIS network over the internet, and a local AIS
receiver or boat network over Wi-Fi (NMEA 0183 over TCP or UDP). The first version uses the free
anonymous Open Waters tier only. One C++ decoder and overlay in
fvkit serve both apps; the shells only open the network connection and draw options pages.

## Starting point

**The Windows overlay.** `Plugins/LegacyOverlays/Maritime/Maritime_RTT` (C#, ~27.7k lines, a
RealTimeTracker COM client). Pipeline Feed → Parser → DataManager/Handler → Layer:

- Feeds (`NMEA/Feed`): TCP client, serial, file playback, XML.
- Parsers registered by sentence type; AIS in `NMEA/Parser/AISParser.cs` decodes types 1–3, 5,
  18, 19 (bit unpacking in `NMEA/Object/BinaryStringConvertor.cs`). Ownship sentences RMC, GGA,
  GLL, HDT, VHW, DPT, MWV, MWD, RPM, ZDA, GSV, ALM, RTE, WPL, BWC. Radar: RSD only.
- Handlers hold state in ADO.NET DataSets backed by Jet `.mdb`; `AISLayer`/`RadarLayer` turn
  processed messages into point symbols.
- Behaviour worth keeping: symbol rotated to true heading plus a separate COG arrow, trails by
  point count or by time (`Settings.TrailType/TrailTime/TrailPoints`), records marked expired
  after `RecordAgeTimeout`, knots or km/h, true or magnetic heading.
- Not ported: the Coast Guard law-enforcement layer (MMSI/MISLE/LE-intel databases, lookouts,
  sighting and boarding reports, arrival notices), the Iridium report queue, the NMEA re-publish
  TCP listener, Cursor-on-Target export, the WinForms UI.
- Defects not to repeat: multipart reassembly is not keyed by sequential message id or channel,
  so interleaved two-part type 5 messages corrupt each other; types 24 (Class B static, where a
  yacht's name comes from), 4, 9, 21 and 27 and `!AIVDO` (own transponder) are ignored.

**Ship symbols.** `AISSymbolFactory.cs` maps a display code to a PNG in the installed
`HD_Data\RTT\AIS\` directory through `Symbollist.txt` there, and records the MIL-STD-2525 SIDC
each image stood for (sailing `SNSPXR`, fishing `SNSPXF`, towing `SNSPXMTO`, law enforcement
`SNSPXL`). Chris found the installed set (FalconView 5.1.1 data,
`~/Downloads/fvw511_data/FalconView_files/CommonAppData/FalconView/RTT/AIS/`, not in this
repository):
- 18 hull outlines, bow up, coloured, with alpha, about 117×287 px: `orange` (unknown, the
  default), `grey`, `Cargo`, `Green` (passenger, WIG), `Blue` (pleasure, high-speed), `Sail`,
  `fish`, `Tug`, `Tow`, `dredge`, `Diving`, `military`, `LE_red`, `SAR-red`, `RedCross`,
  `environment`, `Air`, plus `lookout` (decorator). `Vessels.png` is a contact sheet, not used.
- `Symbollist.txt` keys two-digit entries by ITU ship-and-cargo type (20–24 Green, 30 fish,
  31–32 Tow, 33 dredge, 34 Diving, 35 military, 36 Sail, 37 Blue, 38/51 SAR-red, 40–44 Blue,
  50/52/53 Tug, 54 environment, 55 LE_red, 58 RedCross, 60/69 Green, 70–79 Cargo, 80–84 grey,
  85 Cargo, 90 orange); codes not listed fall back to `orange`. The three- and four-digit keys
  are MISLE display codes and are not used.
- File names in the list differ in case from the files (`Grey.png`/`grey.png`,
  `blue.png`/`Blue.png`, `SAR-Red.png`/`SAR-red.png`, `LE_Red.png`/`LE_red.png`,
  `diving.png`/`Diving.png`); Windows ignored this, so the port matches names case-insensitively.
- The `.gif` files (binocular, cellphone) belong to the sighting and Iridium features and are not
  used.

**In fvkit already.**
- `nav/line_transport.h`: `ILineTransport` with `TcpLineTransport`, `UdpLineTransport`,
  `FileLineTransport`, `StringLineTransport`, `LineBuffer`.
- `nav/nmea.h`: position sentences from any talker (`NmeaFixAssembler`, `NmeaLineSource`).
- `overlay/moving_map_overlay.h` (`fv.movingmap`) as the model for a live overlay; `GeoDraw`,
  `app::Properties`, pick and search providers from the shared overlay toolkit (PORTING.md).
- A JSON library (used by `style/`).

**Open Waters AIS** (`https://openwaters.io/api/ais/`, read 2026-10-26).
- SSE: `GET https://ais.openwaters.io/v1/stream?bbox=minLat,minLon,maxLat,maxLon&snapshot=1&key=…`,
  one `data:` JSON line per frame, `welcome` first (with the limits in effect), then `event`.
  Each event carries `mmsi`, `msg_type`, `lat`/`lon` (last known, also on static messages),
  `time`, `source`, `station`, the raw sentences in `nmea` (TAG block + `!AIVDM`), and decoded
  `message` fields. `synthesized: true` frames from a snapshot rebuild have no `nmea`.
- WebSocket `wss://…/v1/stream` (same events, subscription changeable in place);
  `wss://…/v1/nmea` raw NMEA needs the contributor tier.
- `GET /v1/vessels/{mmsi}` details (destination, ETA, particulars); `/v1/vessels/{mmsi}/track`.
- Limits: anonymous ~10°×10°, 20 msg/s, 2 connections per address; free personal token
  ~20°×20°, 50 msg/s. Over the rate the stream thins rather than closes.
- Licensing is per source and must be shown: every event names `source`; credit lines for
  Kystverket and BarentsWatch (NLOD 2.0), Digitraffic (CC BY 4.0), AISHub (link to aishub.net).

**Local receivers.** NMEA 0183 text (`!AIVDM`, own ship `!AIVDO`) over TCP or UDP is the de facto
standard. Port 10110 is the IANA NMEA-0183 port; many Wi-Fi AIS units and multiplexers
(Quark-elec, Digital Yacht, Vesper, Yacht Devices) default to TCP or UDP 2000. NMEA 2000
networks reach a phone through Wi-Fi gateways that emit the same 0183 text. Signal K (JSON over
WebSocket, Bonjour `_signalk-ws._tcp`) is the other common route. 0183 has no discovery.
iOS: local network access needs `NSLocalNetworkUsageDescription`; receiving broadcast or
multicast UDP needs the `com.apple.developer.networking.multicast` entitlement (requested from
Apple); TCP and unicast UDP need neither.

## Decisions

| Id | Question | Decision |
|---|---|---|
| AIS-D1 | One decoder or one per source? | One: every source is reduced to `!AIVDM`/`!AIVDO` lines on an `ILineTransport`. Open Waters events contribute their `nmea` sentences; only `synthesized` frames without `nmea` take a JSON fallback (position, COG/SOG, heading, name, type from `message`). |
| AIS-D2 | Where HTTPS and TLS live | In the shell, not fvkit: `URLSession` on Apple platforms, libcurl in PeregrineGtk. The shell pushes lines into a thread-safe queue transport. Core stays socket-only, as `line_transport` is now. |
| AIS-D3 | SSE or WebSocket for Open Waters | SSE first: plain HTTP lines, no WebSocket library. A view change that leaves the subscribed box reconnects with a new `bbox` and `snapshot=1`. WebSocket only if reconnect churn proves a problem. |
| AIS-D4 | Ship symbols | The original FalconView PNGs, keyed by ITU ship type through the `Symbollist.txt` table, drawn rotated to heading. AtoN, base station and SAR aircraft get vector symbols (the set has none). Chris, 2026-10-26. |
| AIS-D5 | Open Waters token | Personal token per install, minted by the app (no pasted token, no account). Revised from anonymous-only. Chris + Open Waters developer, 2026-10-26. |
| AIS-D6 | Contribute local receptions to Open Waters | No. Not built: there is no receiver to test with. Chris, 2026-10-26. |
| AIS-D7 | Ownship from AIVDO | AIVDO is decoded and the table marks the record `own_ship`; the overlay does not draw it as a target and CPA skips it. Revised 2026-10-27: under "Use Receiver Position", a VDO position report is a fallback fix when the receiver has sent no RMC/GGA fix for 10 s (a transponder with no GPS sentences on the network). Chris, 2026-10-26 / 2026-10-27. |

## Steps (easiest first)

**AIS1 — Decoder (C++, no I/O).** `fvkit/ais/decoder.{h,cpp}`: parses `!xxVDM`/`!xxVDO` with
optional NMEA 4.10 TAG block (`s:`, `c:`, `t:` kept), checks the checksum, reassembles multipart
messages keyed by (talker, sequential id, channel, part count) with a timeout, and unpacks 6-bit
armour. Decodes types 1–3, 4, 5, 9, 18, 19, 21, 24A/24B, 27 into one `AisMessage`; every field
carries validity (AIS sentinels — heading 511, COG 360, SOG 102.3, lat 91, lon 181 — become
invalid, not values). Unknown types are reported, not errors. Tests: published sample sentences
per type, multipart interleaving, bad checksum, fill bits, the Open Waters example sentences.

*AIS1 as built (2026-10-26).* `fvkit/ais/decoder.{h,cpp}`, namespace `fv::ais`:
`ParseAisSentence`, `UnarmorPayload`, `DecodeAisBits`, `AisDecoder::Feed` →
`FeedResult` {kMessage, kPending, kNotAis, kError, kUnsupported} with `DecoderStats`.
- Checksum required on the sentence (a corrupt binary payload is otherwise undetectable); a
  TAG block is kept only when its own checksum matches, and the sentence still decodes when it
  does not. A 13-digit `c:` is read as milliseconds.
- Reassembly key is (talker + VDM/VDO, sequence id, channel, fragment count). A new first part
  replaces an unfinished message under the same key; an orphan later part is an error; both
  count in `fragments_dropped`, as does a timeout (default 10 s). Repeated fragments are ignored.
- Bits past the end read as zero, so a short trailing field is "not available"; each type has a
  minimum length for its fixed fields (type 5 needs 422 bits, not 424).
- Text fields end at the first '@' and are trimmed at both ends. Ship type is kept as sent:
  undefined codes (1–19, 100+) stay with `has_ship_type`, where pyais reports 0; the symbol
  table sends them to the default hull. SOG is m/s; type 9 and 27 speeds are whole knots.
- The Open Waters API page's `!BSVDM` example decodes to MMSI 258857000, not the 257090090 the
  page prints beside it; the test pins the decoded value.
- Cross-check: 10,406 sentences captured from the anonymous Open Waters stream (Norway and
  Charleston boxes, 2026-10-26; not committed, the sources' terms vary) decode to 7,784
  messages, 0 errors, and agree with pyais 3.3.1 on every compared field except the cases above.
- Tests: `AisDecoder.*` (22).

**AIS2 — Vessel table.** `fvkit/ais/vessel_table.{h,cpp}`: one record per MMSI merging static
and dynamic data, class A/B, kind (vessel, AtoN, base, SAR, beacon by MMSI prefix 970/972/974),
trail ring (by count or age), last-heard time, source attribution, stale and drop ages, change
notifications. CPA/TCPA against an ownship fix. Bounded size with oldest-first eviction.
Deterministic: time comes from the caller. Tests with scripted message sequences.

*AIS2 as built (2026-10-26).* `fvkit/ais/vessel_table.{h,cpp}`: `VesselTable`, `Vessel`,
`TargetKind`, `KindFromMmsi`, `ComputeCpa` → `CpaResult`.
- One record per MMSI; times are on the caller's clock; a revision counter per table and per
  record. Optional listener (added/updated after the change, removed before); it must not
  modify the table.
- Kind: beacon MMSI prefixes 970/972/974 (SART, MOB, EPIRB) always win; else message type
  4/9/21 sets base station / SAR aircraft / AtoN; else MMSI numbering (111 SAR aircraft, 99
  AtoN, 00 base station). Class A from types 1-3 and 5; class B from 18, 19, 24.
- A position report replaces the motion fields it carries, including "not available" ones (no
  stale SOG kept). A type 27 long-range position does not replace a full-resolution position
  younger than the stale age.
- Static fields merge when present; type 5 replaces voyage data (destination, ETA, draught)
  whole, since the crew may clear it.
- Trails only for moving kinds (not base stations or AtoNs): a point when the position changed
  and at least `trail_min_interval_s` (default 10 s) has passed; bounded by count (120) and age
  (1800 s). Defaults: stale 360 s, drop 1800 s, max 10,000 records, evicting the oldest-heard
  non-own-ship record. VDO marks `own_ship` and `own_ship_mmsi` (AIS-D7: not offered
  as a position source).
- CPA/TCPA: local flat earth, mean radius 6,371,008.8 m; the target is dead-reckoned to now;
  the ownship fix is taken as current, stationary when it lacks speed or course; base stations
  and AtoNs are stationary; a moving target without SOG/COG gives range and bearing but
  `valid` = false; TCPA is negative once passed.
- Tests: `AisVesselTable.*` (12), `AisCpa.*` (5); full ctest 3035/3035.

**AIS3 — `fv.ais` overlay.** Modelled on `fv.movingmap`: reads a `VesselTable`, draws the
AIS-D4 symbols rotated to heading (COG when no heading), COG/SOG vector of a chosen minutes ahead, trails, name
labels by zoom, stale symbols faded. Pick and search providers (name, MMSI); a details record
for the shells (MMSI, name, call sign, type, dimensions, destination, ETA, nav status, SOG/COG,
CPA/TCPA, age, source). Options in `app::Properties`: trails, vector minutes, labels, stale/drop
ages, units, kinds shown. Proven with a recorded NMEA file through `FileLineTransport`; golden
render test.
Symbols: `port/tools/make_ais_sheet.py` (after `make_maki_sheet.py`) copies the PNGs into the
tree downscaled (hull about 64 px long at 2×), writes a sprite sheet compiled into fvkit, and
generates the ship-type table from `Symbollist.txt` with names case-folded. Not SDF: the colours
are the symbol. Drawn hull length in points is an option (default about 24 pt), with Class B
smaller; stale vessels faded. The symbols shipped with the LGPL open-source release of FalconView
(GTRI; confirmed by Chris 2026-10-26), so they may go to the public Peregrine repo; `NOTICE.md`
gets a row for them in AIS3.

*AIS3 as built (2026-10-26).*
- `fv::AisOverlay` (`kTypeId` "fv.ais") in `fvkit/overlay/ais_overlay.{h,cpp}`: Overlay + HitTest
  (feature = MMSI) + ItemInfo + SearchProvider (name, MMSI, call sign) + Properties. Lines in by
  `PushLine` (any thread, mutex queue) or a non-blocking `ILineTransport` polled in `Tick(now_s)`
  (at most 5000 lines per tick); `FeedLine` for the UI thread. `Tick` returns true when the table
  revision or the count of stale records changed. `SetOwnship` feeds CPA/TCPA rows in the details
  sheet (`Details(mmsi)`, also what `InfoAt` returns).
- Draw order: trails, COG vectors (distance run in `vector_minutes`), symbols, names. Hull rotated
  to heading, else COG, else north; drawn through GeoDraw::DrawSymbol, so a turned chart turns it,
  and rotated tiles resample nearest-neighbour (the shared pixmap path). Class B hulls are 3/4 size.
- Symbols (`fvkit/ais/presentation.{h,cpp}`, `port/tools/make_ais_sheet.py`): 17 FalconView hulls
  copied into `port/fvkit/ais/symbols/` (lower-case names, 64 px long, pixelRatio 2, 32 px
  nominal) with `Symbollist.txt`; each also baked as a `-stale` twin (60% toward grey, half alpha),
  34 sprites. SAR aircraft use FalconView's `Air.png` (the plan's "the set has none" was wrong for
  aircraft). AtoN, base stations and beacons are drawn as shapes: magenta diamond (dashed when
  virtual), blue square, red crossed circle for SART/MOB/EPIRB.
- Faithful to Windows: ship types `Symbollist.txt` does not list (61-68, 75-78, 86-89, 91-99, and
  all undefined codes) use the default orange hull, though the plan text said 70-79 cargo.
- Own transponder (AIS-D7): not drawn, picked, searched, or given CPA.
- AIS2 change made here: `VesselTable::IsStale` now measures the age of the position (last-heard
  only when there is no position), because a vessel sending only static messages kept looking
  fresh. Drop still uses last heard.
- Properties: show_trails, trail_minutes (30), vector_minutes (6), show_labels, label_max_scale
  (1:100,000), symbol_size_pt (24), show_aton (on), show_base_stations (off), stale_minutes (6),
  drop_minutes (30), speed_unit (knots / km/h / m/s). ITU texts: ShipTypeText, NavStatusText,
  AidTypeText.
- Test data: `port/fvkit/ais/test/data/charleston-synthetic.nmea`, 10 minutes / 364 messages of
  made-up Charleston traffic written by `port/tools/make_ais_sample.py` (its own AIS encoder; live
  captures stay uncommitted). Replayed through FileLineTransport at TAG times: 10 records, 0
  decode errors.
- Golden render (labels off, 800x600, 1:50,000) FNV hash 11680377109443694018;
  `FV_AIS_RENDER_DIR=dir` writes it (and a labelled one) as PNG. A NE-SW ink test pins the
  rotation sense.
- Tests: `AisPresentation.*` (1), `AisOverlay.*` (9); full ctest 3045/3045.
- `NOTICE.md` section 3 now names the symbols.

**AIS4 — Open Waters in Osprey.** Swift SSE client on `URLSession` feeding the queue transport;
bbox from the visible chart, padded and clamped to the tier's area from `welcome`; reconnect on
leaving the box or on drop, with `snapshot=1`. Source credit shown on the map and in the details
sheet. Optional details fetch from `/v1/vessels/{mmsi}`.
Built for the anonymous tier (AIS-D5 then; tokens since OB7): box at most 100 square degrees, 20 msg/s, so the box follows the view and
zoomed-out views show only the centre box.

*AIS4 as built (2026-10-26).*
- fvkit `ais/openwaters.{h,cpp}`: `OpenWatersSsePayload`, `ParseOpenWatersFrame` -> `OpenWatersFrame`
  (welcome limits, event with nmea/source/license/attribution/station/time, error, other),
  `OpenWatersBox`, `OpenWatersBoxServes`, `OpenWatersStreamUrl`. Shared so PeregrineGtk/mac (AIS6) reuse it.
- Live stream facts found 2026-10-26 that differ from the plan's reading: `welcome.limits` gives
  `area` (100 square degrees for anonymous), `rate` 20, `conns` 2, `connects_per_min` 20 - not a
  10x10 degree box; the box is the view padded 25% per side, shrunk uniformly about its centre to 98%
  of the area. Events carry no TAG block; credit is the JSON `attribution` field, which the overlay
  stores as the vessel's TAG t: text (details row renamed "Licence" -> "Credit"), `station` as s:.
  Snapshot frames are `synthesized: true` and normally do carry `nmea`; only some type-5 static
  frames lack it (36 of 244 in a Charleston capture), which is when the JSON fallback (aisstream.io
  field names, sentinels left invalid) is used.
- `AisOverlay::Input`/`Push`/`PushFrame`: queued inputs carry reception time, station and credit; a
  report ages from its reception time (snapshot reports hours old are dropped at once by the 30-min
  drop age; a time ahead of the device clock counts as now).
- A view larger than the tier's area keeps its stream while its centre stays in the middle half of the box.
- PippinKit `PPAis.{h,mm}` (`PPOpenWatersFrame`, `PPOpenWatersLimits`, `openWatersStreamURL`);
  `PPMap.aisVisible`, `pushOpenWatersFrames:`, `aisTargetCount`; the `fv.ais` overlay sits between
  points and ownship, ticked each render with the moving map's last fix as ownship for CPA; drawn
  into the cached overlay band.
- Swift `AisFeed` (URLSession `bytes(for:)` SSE, frames batched to the render queue every 0.5 s, view
  checked against the box once a second, reconnect backoff 2-60 s, connects spaced by the tier's
  connects_per_min, stream closed in the background and reopened on resume). Map menu toggle "AIS
  Traffic" (Osprey only, `AppFlavor.offersAisTraffic`, persisted `aisTraffic`). Credit line over the
  chart: sources heard in the last 30 min, split into sentences without repeats. A tap lists AIS
  targets ahead of ENC features in the chart feature sheet (acronym "AIS", title "AIS Target" when
  all entries are targets).
- Verified on the iOS 27 simulator (iPhone 18 Pro Max), Gulf coast near Orange Beach: 22 targets from
  the snapshot, hulls drawn, live update, tap shows the details sheet.
- Not done: the optional `/v1/vessels/{mmsi}` details fetch; no redraw is scheduled for fading when
  the stream is silent (the next frame batch or camera move redraws).
- Tests: `AisOpenWaters.*` (11); full ctest 3056/3056.

**AIS5 — Local receiver in Osprey.** Connection settings: protocol (TCP/UDP), host, port,
presets 10110 and 2000; the C++ `TcpLineTransport`/`UdpLineTransport` run on the existing feed
thread. Reconnect with backoff; connection state shown. Position sentences on the same feed
(RMC, GGA, HDT) offered to the moving map. Info.plist local-network string. Unicast UDP and
TCP only; broadcast UDP waits on the entitlement.
Tested without a radio: `port/tools/ais_replay.py` serves a recorded NMEA file over TCP and
unicast UDP from the Mac at the receiver's real rate (e.g. a capture saved from the AIS4 stream).

*AIS5 as built (2026-10-27).* Decided by Chris the same day: receiver position behind a setting
(off by default); settings in a sheet from the Map menu.
- fvkit `ais/receiver.{h,cpp}`: `NmeaReceiver` over a `TransportFactory` (`FactoryFor(ReceiverConfig)`
  gives TCP client or UDP listener). `Poll(now)` → `ReceiverBatch` {ais_lines, fixes, state_changed};
  states off / connecting / live / silent (15 s without a line) / retrying. A TCP stream that ends,
  fails, or is silent 60 s closes and reopens with backoff 2 s doubling to 60 s, reset by a line; UDP
  never reconnects. `IsAisSentence` (`!xxVDM`/`!xxVDO`, TAG block allowed). Other lines go through
  `NmeaFixAssembler` (date hint from the clock at start, so GGA-only feeds are stamped).
- VDO fallback (AIS-D7 revised, 2026-10-27): the receiver decodes `!xxVDO` types 1–3, 18 and 19
  (not 27, too coarse) into a fix when no RMC/GGA fix came in the last 10 s
  (`kVdoFallbackAfterS`); SOG and COG carried, transponder heading used as HDT is, stamped on
  arrival (AIS gives only the second of the minute). `vdo_fixes()` counts them. Verified on the
  simulator with `ais_replay.py` without `--ownship`. Tests `AisReceiver.Vdo*`, `GpsSentencesTakePrecedenceOverVdo`.
- fvkit `nav/nmea`: `ParseNmeaTrueHeading` (HDT, any talker). The receiver attaches the last HDT
  (at most 5 s old) to a fix with no course or slower than 0.5 m/s; a moving fix keeps its COG.
- PippinKit `PPAisReceiver` / `PPAisReceiverBatch` (in `PPAis.{h,mm}`): fixes become `PPFix` with
  accuracy from HDOP (5 m per unit, 1 unit when absent) and the receiver's own timestamp;
  `PPMap.pushAisLines:heardAt:` queues lines into the `fv.ais` overlay with reception time.
- Swift `AisReceiver.swift`: `AisReceiverSettings` (enabled, TCP/UDP, host, port, usePosition;
  JSON in defaults key `aisReceiver`); `AisReceiver` polls on its own serial queue every 250 ms
  and hops to the main actor only with data or a change. With "Use Receiver Position" on, phone
  fixes are dropped while a receiver fix is under 10 s old. Stopped in the background like the
  Open Waters stream.
- Menu and sheet (Chris, 2026-10-27): "AIS Traffic" in the Map menu shows or hides the overlay and
  runs or stops both sources. "AIS Settings…" (Osprey only) opens `AisSettingsSheet`: "Open Waters
  Network" on top (defaults key `aisOpenWaters`, on by default) with its status, then "Local
  Receiver" with status, sentence and fix counts, protocol, address, port (presets 10110 and
  2000) and "Use Receiver Position". The two sources are independent and feed one table; turning
  Open Waters off clears the table (it does not track sources) and the receiver's targets return
  with their next reports. With AIS Traffic off the receiver still runs when it supplies the
  position, and its AIS lines are discarded.
- Osprey `Info.plist`: `NSLocalNetworkUsageDescription`. No multicast entitlement: unicast UDP and
  TCP only.
- `port/tools/ais_replay.py`: serves an NMEA file over TCP and/or unicast UDP, paced by TAG `c:`
  times (`--speed`) or `--rate`, `--loop`; strips TAG blocks unless `--keep-tags`; `--ownship`
  adds `$GPRMC`/`$HEHDT` built from the file's own `!AIVDO` reports, stamped now.
- Verified on the iOS 27 simulator (iPhone 18 Pro Max) against `ais_replay.py --tcp 10110 --loop
  --ownship` on the Charleston sample: status Receiving, the synthetic targets drawn round the
  ownship with Open Waters off, ownship SOG/COG from the replayed RMC/HDT.
- Not done: no real receiver tested; the receiver stops in the background, so a locked-screen
  recording falls back to the phone's GPS; broadcast UDP waits on the multicast entitlement.
- Tests: `AisReceiver.*` (10, incl. TCP and UDP loopback); full ctest 3071 run, all passed.

**AIS6 — Peregrine (mac, then Linux).** DeskKit options page from the overlay's properties,
an AIS connections dialog (local receiver, Open Waters), the details panel. mac uses
`URLSession`; PeregrineGtk gets libcurl SSE in its own `linux/*` PR. CPA alarm (threshold
distance and time) in both apps.

**Later, not planned:** Signal K client with Bonjour discovery; ARPA radar targets (TTM/TLL);
contributing receptions (AIS-D6, declined for now); vessel track history from `/v1/vessels/{mmsi}/track`.

**Open Waters token, as the developer suggests (email to Chris, 2026-10-26; backlog OB7).** No
pasted token: each install makes its own, as the Open Waters Signal K plugin does. On first start
generate an Ed25519 key, ask the API for a personal token signed with it (request documented at
https://openwaters.io/api/ais/; reference code
https://github.com/openwatersio/aiscast/blob/main/signalk-plugin/src/identity.ts), and keep both
(Keychain on Apple platforms). No account, nothing for the user to copy; each install gets its own
limits and, if it ever sends data back (AIS-D6), its own station. A personal token is free, never
expires, and covers about 20x20 degrees or up to 50 vessels by MMSI. Every request also sends a
User-Agent with the app's name and version.

*OB7 as built (2026-10-26).*
- fvkit `ais/openwaters.{h,cpp}`: `kOpenWatersKeysUrl`, `OpenWatersMintMessage` (six lines, no trailing newline), `OpenWatersMintBody` (signed, names no station), `OpenWatersToken` + `ParseOpenWatersToken` (token, signed, sub, role, exp, name_error, limits from claims); `ReadLimits` shared with the welcome frame.
- PippinKit `PPOpenWatersToken`, `openWatersKeysURL()`, `openWatersMintMessage`, `openWatersMintBody`.
- Swift actor `OpenWatersIdentity`: CryptoKit Curve25519 key made on first use; key and mint response in the Keychain (service org.peregrine.openwaters, AfterFirstUnlockThisDeviceOnly); token for another key ignored; unsigned token reminted once per launch; failed mint retried after 5 min (keys endpoint allows 10/min); concurrent callers share one mint. User-Agent `<AppName without spaces>/<version> (<build>; github.com/ChrisABailey/Peregrine)` on the mint and the stream.
- `AisFeed`: token fetched before each connect, sent as `Authorization: Bearer` (never in the URL); box sized for the token's tier; HTTP 401/403 with a token drops it and falls back to anonymous limits, next connect mints again; welcome logs the area.
- Verified on iOS 27 simulator (iPhone 18 Pro Max): first launch logs "Open Waters token minted, signed=true"; relaunch reuses it, welcome area 400 sq deg; targets drawn Tampa Bay to Cumberland Island.
- Tests: AisOpenWaters.* 14 (MintMessage, MintBody, ParsesToken added).
