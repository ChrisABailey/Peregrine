<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Osprey (CH) — S-57 ENC moving map, a Pippin sibling

**Status: APPROVED, 2026-10-23.** CH-D1 to CH-D4 decided by Chris the same day.

## Goal

Osprey is a new iOS app beside Pippin that displays NOAA S-57 ENC charts instead of the OSM map, keeps points,
routes and the moving map, and replaces turn-by-turn guidance with a course deviation
indicator (CDI). Charts are installed and updated by downloading NOAA's regional zips (Florida:
one 57 MB zip). No street routing.

## Starting point

- `port/Enc/` reads S-57 (`S57Cell`, `S57ObjectCatalog`) and draws it with S-52
  (`EncVectorSource`, `S52StyleEngine`); Peregrine already renders Charleston ENC with it.
- Base editions only: `.000` is read, update files (`.001`…) are detected but not applied
  (`EncVectorSource` reports them).
- Pippin already ships two variants from one tree (Kiawah, Atlanta: own icon, display name, pack).
- `pippin::RouteStore` with no graph joins waypoints with straight legs (today a fallback).
- Test data: `testdata/enc/` (8 Charleston cells). The ledger lists 815 more in a sibling
  `enc-archive/`, not present in this checkout.

## Decisions

| Id | Question | Decision |
|---|---|---|
| CH-D1 | Separate git fork or a variant in this tree? | A new app, **Osprey**, beside Pippin in this tree: own target, bundle id, icon, pack. Map, GPS and route code stay shared. |
| CH-D2 | Leg geometry | Rhumb line (constant course), drawn and measured as such. |
| CH-D3 | NOAA update files before CH10 | Nothing: base editions are drawn, no warning is shown. |
| CH-D4 | Where the CDI computation lives | RouteKit C++ beside `fv::nav` turn guidance, unit-tested; Swift only draws it. |
| CH-D5 | An update file that cannot be applied | The cell stays at the last update that applied cleanly and is still drawn; the app shows a one-line notice naming how many cells are not fully up to date. Supersedes CH-D3 once CH10 lands. |
| CH-D6 | Apply updates at open or write merged cells | At every open, in memory. The delivered `.000` + updates stay the only copy; no ISO 8211 writer, no merged-cell cache. |
| CH-D7 | Reference implementation for verification | GDAL's S-57 driver (`UPDATES=APPLY`), Homebrew, dev machine only, driven from `port/tools`; never linked or required by CMake. |

## Steps (easiest first)

**CH1 — Charts on screen (small).** When the pack's `pippin.ini` names an ENC directory, `PPMap`
opens `EncVectorSource` + `S52StyleEngine` instead of the OSM source. Link `fv_enc` into
`pippin_core_deps` (`port/apps/Pippin/CMakeLists.txt`). Prove on the simulator with the 8
Charleston cells, then measure open time and memory with a few hundred cells (`enc-archive/`
or an unpacked NOAA state zip).
If that is slow, move CH7 forward.

*CH1 as built (2026-10-23).* `PippinKit/PPEncChart.{h,cpp}` (`pippin::EncChart`): cells
grouped by usage band from the cell name, one `EncVectorSource` + base/underlay
`VectorRenderer` per band opened on first draw, one shared `S52StyleEngine`;
`EncBandForScale` picks the band nearest the display scale in log terms. `PPMap` takes the
chart path when `enc.cells` is set (`enc.data_dir`, `enc.min_zoom`/`max_zoom`); legend, dark
style and chart search are off for a chart pack. Pens are drawn at `96 x pixels-per-point`
dpi (Peregrine's weight, not ECDIS millimetres) and symbols/text at pixels-per-point.
Shared fix: S-52 dash runs scale with device dpi (`ApplyPenStyle`); unchanged at 96 dpi, so
no golden moved. `stage_data.py --region charleston-enc` stages the Charleston fixture.
Tests `EncBandForScale.*`, `EncChart.*` (5); full ctest 2879/2879. Sim (iPhone 18 Pro Max):
Charleston Approach at 1:87,912, base draw 68 ms, `apps/Pippin/docs/ch1-charleston-enc.png`.
Florida on the mac (`EncChart` directly, 1320x2868): open 70 ms; first draw of a band
150-200 ms (bands 1-3), 1.0 s (band 4, 201 cells), 1.4 s (band 5, 306 cells); redraw
4-44 ms; resident memory 870 MB once every band is open, because a band keeps all its cells
parsed. Not done: labels still repeat and overlap across cells; at harbour scale outside
harbour coverage the screen is blank.

**CH2 — The Osprey app (small).** `port/apps/Osprey`, its own target, display name, icon, bundle id. Nautical miles and knots
(`DistanceUnits.swift`). Hide cycling features: beach verdict, avoid hills, slope colours, ride
wording. Keep the tide card. First-launch "not for navigation" notice.

*CH2 as built (2026-10-23).* `Osprey` target in `Pippin.xcodeproj` (shared scheme) compiles the
same 36 Swift sources and links PippinKit. `port/apps/Osprey` holds Info.plist (`PPAppFlavor` =
`osprey`, imports but does not export the .fvrte UTI, no `pippin://` scheme), Assets.xcassets
(icon from `art/make_icon.py`), README.md and a git-ignored `Data/` pack; `OSPREY_DISPLAY_NAME` /
`OSPREY_BUNDLE_ID` (org.peregrine.Osprey) in `Pippin.xcconfig`, version 0.1.0. `AppFlavor.swift`
reads the flavor at run time (absent or unknown = Pippin) and drives offered and default units,
beach/hills visibility, the track noun (also the GPX stem), the export menu title, and the
first-launch "Not for Navigation" alert (`PPNavigationNoticeAcknowledged`).
`DistanceUnits.nauticalMiles`: feet below 0.1 nm, then one-decimal nm; knots one decimal; heights
in feet; countdown yards to 1 nm. A stored unit the flavor does not offer falls back to the
flavor default. Route sheet beach picker/footer, avoid hills and slope colours need flavor and
pack; the tide card drops beach lines and ride/walk bands in Osprey. The pack-staged check moved
from PippinKit to per-app "Check the data pack" phases. `stage_data.py charleston-enc` keeps
`[tides]` (NOAA 8665530, `testdata/tides/8665530.json`) and points `[wind]` at the harbour
(32.7808,-79.9236); staged with `--pack port/apps/Osprey/Data` (29 files, 9.9 MB). Tests:
`pippin_app_flavor` (Swift, mac), 12 nautical cases in `guidance_format_test`; ctest 2884/2884;
both simulator builds pass, screenshots `apps/Pippin/docs/ch2-osprey-*.png`. Not done: Share and
Live Activity extensions are not built for Osprey (the Live Activity is still open after CH5; it needs its own widget target).

**CH3 — Straight-leg routes (small).** No graph in the pack: straight legs are the normal case,
with no "could not follow roads" message. Route sheet lists each leg's course and distance.
Legs per CH-D2.

*CH3 as built (2026-10-24).* `[routing] legs = "rhumb"` (set by `stage_data.py` for both ENC
regions; Pippin's ini documents `"roads"`) puts `pippin::RouteStore` in straight-leg mode
(`SetStraightLegs`): no planner call or graph load, the overlay draws `LineKind::kRhumb`, and
`fv::analysis::GeoPath` (rhumb) measures each leg. `RouteSnapshot::straight`/`legs`
(`PPRoute.isStraight`, `PPRouteLeg` course °T and distance) and `length_m` is their sum; status
is empty; beach/elevation report unavailable; `RoutePath()` is the leg line densified at 250 m,
so the trip computer's distance-to-go works; maneuvers stay empty (CH5 brings the CDI). Sheet:
"N legs, distance" status, a Legs section (Start → Via 1, 047°T, distance), no Mode picker.
Osprey flavor (`AppFlavor.placesStopsByTap`, `appendsStops`): an empty route opens on the stops
form, "Add stop" appends a new End, "Add Stops on Map" dismisses into a tap mode where each tap
appends a stop and is applied to the live route at once (Undo, Done back to the sheet, Cancel
restores the route and draft); a row's "Pick on map" is one tap. A tap on a pin takes the pin's
position and name (`MapModel.tapTarget`). Waypoints stay draggable in tap mode. Pippin keeps the
crosshair. Tests `RouteStoreStraight.*` (4). Sim: `apps/Pippin/docs/ch3-osprey-legs.png`. Not
done: taps injected back-to-back by the simulator can drop one; courses are true, not magnetic.

**CH4 — Moving map for a boat (small).** COG vector ahead of own-ship; SOG and COG in the
trip bar. Following and course-up already exist.

*CH4 as built (2026-10-24).* `PPOwnship.courseDegrees`/`hasCourse` carry the receiver's reported
COG (`PositionFix::true_heading_deg`); a heading derived from movement is a screen angle and is
not used. `Course.swift` (Foundation-only): `Course.text` ("047°T", moved from RouteSheet) and
`Course.vectorEnd` (rhumb-line end point after `vectorSeconds` = 360 s, the usual 6-minute
plotter vector; nil below `vectorMinimumSpeed` = 0.5 kn). `AppFlavor.showsCourseOverGround`
(Osprey only): MapScreen draws the vector under the ship (black 2 pt on a 4 pt white casing,
through the perspective) and adds a COG row ("safari" icon) to the trip bar's left cluster,
en-dash with no reported course or below 0.5 kn. SOG is the existing speed row (knots under
nautical units). Tests `pippin_course` (Swift, mac, 11 checks), 2 new `pippin_app_flavor`
checks. Sim (iPhone 18 Pro Max, Tampa pack, simulated route at 6 m/s): vector leaves the
chevron's nose at 212°T, `apps/Pippin/docs/ch4-osprey-cog-vector.png`; trip bar
`ch4-osprey-trip-bar.png`. Not done: COG is true, not magnetic; vector length is fixed at six
minutes (no setting); at small scales (1:1.9M) the vector is sub-pixel; the vector draws
whenever there is a fix, not only in GPS mode.

**CH5 — Course deviation indicator (medium).**
- C++ in RouteKit, with tests: cross-track error with sign, bearing and distance to the
  active waypoint, time to go and ETA from SOG, arrival radius with automatic leg advance,
  off-course alarm past a set XTE limit.
- Swift: a CDI bar (needle and scale) replaces the turn card. Alert tone, notifications and
  the Live Activity carry CDI values instead of maneuvers.

*CH5 as built (2026-10-24).* `fv::nav::CourseDeviation` lives in `fvkit/nav/course_deviation.{h,cpp}`
beside guidance.h, not in RouteKit: it needs no road graph (CH-D4's intent holds: C++, unit-tested,
Swift only draws). Rhumb legs via `GEO_calc_range_and_bearing`; XTE and along-track come from the
rhumb range and bearing from the leg start resolved against the leg course (signed, + right of
track); bearing and distance to the active waypoint, distance to end, TTG and ETA from SOG (none
below 0.5 kn). A leg advances on the arrival circle (0.05 nm) or a perpendicular crossing; the last
leg counts only when crossed; the first fix skips passed legs silently. Off-course alarm past
0.1 nm held 5 s, cleared below 80% of the limit. Ini keys `course.arrival_m`, `course.off_course_m`.
Tests `CourseDeviation.*` (16). PippinKit: `PPCourseDeviation` on `PPFrame`/`PPCameraStep`
(`courseDeviation`), `currentCourseDeviation`; course events ride on `guidanceEvents` as new
`PPGuidanceEventKind` cases Waypoint/OffCourse/BackOnCourse (Arrived reused) with `crossTrackMeters`
and `nextName`. Swift: the Osprey CDI card replaces the turn banner on a straight-leg route (point
name, n/legs, distance; needle on a +-5 dot scale, full scale = off-course limit, red at full
scale or alarm; XTE "0.04 nm R" and Steer left/right/On track; DTK, BRG, TTG, ETA). `Course.swift`
gains needle/crossTrackText/steerText/timeToGoText (`pippin_course` also compiles
DistanceUnits.swift). `TurnAlerts`: waypoint chime (G6-E7); off-course uses the off-route chime.
`GuidanceNotice`: waypoint notice ("Reached X / Now steering for Y", one id), off-course notice
with side and steer, removed on back-on-course (`pippin_guidance_notice` 30/30). ctest 2905/2905;
Osprey and Pippin simulator builds pass. Sim (Tampa pack): `apps/Pippin/docs/ch5-osprey-cdi.png`,
`ch5-osprey-off-course.png`. Not done: Live Activity for Osprey (no activity extension in the
target; needs a new widget target), so CDI values are not on the lock screen; waypoint advance seen
only in tests, not on the sim; card ETA is wall clock + TTG; no manual skip-to-next-waypoint;
courses true, not magnetic.

**CH6 — Import a chart zip from Files (medium).** Document picker, unzip (small C++ reader on
zlib, which iOS ships), build a cell index (bounds, compilation scale, usage band, edition),
swap the chart set in atomically. Exercises the full Florida set without download code.

*CH6 as built (2026-10-25).* `fv::ZipReader` (`port/Enc/fv_zip_reader.{h,cpp}`, on zlib via
`fv_zlib`) reads the central directory incl. zip64, stored + deflate, with a CRC-32 check;
`IsSafeZipPath` rejects absolute, `..` and backslash names before anything is written; encrypted or
other methods return kUnsupported. It lives in fv_enc, not fvkit, since ENC is its only user.
`fv::ReadS57DatasetInfo` reads only DSID/DSSI/DSPM (edition, CSCL) and `fv::FindS57UpdateFiles`
lists updates; DSID/DSPM parsing is shared with `S57Cell::Open` via a helper. Cell index
`fv_enc_index.{h,cpp}`: `chart-index.tsv` (header `# fv-enc-index 1`), one row per base cell:
relative path, name, band, edition, update count, CSCL, bounds (CATALOG.031 or parsed);
Build / Write (tmp + rename) / Read. `pippin::ChartStore` (`PippinKit/PPChartStore.{h,cpp}`):
each import unpacks into `set-<ms>/`, indexes it, then commits by renaming a `current` pointer
file; failure or cancel removes the new set and leaves the current one; free-space check;
`RemoveStaleSets` runs after the map has opened the new set. The store is
`Library/Application Support/Charts`, excluded from backup. `EncChart::Open` uses
`chart-index.tsv` when present instead of scanning. ObjC `PPChartStore`/`PPChartSet`
(`PPChartImport.{h,mm}`); `PPMap.drawsCharts`, `-openChartsAtURL:error:` (render queue; swaps the
EncChart only if the new one opens); `PPErrorChartImportFailed = 7`. Swift: Osprey menu "Import
Charts" (shown when the pack draws charts) opens `.fileImporter` for `.zip`; the import runs in a
detached task; progress shows as "Importing charts... N%" in the notice capsule; the swap happens on
the render queue; notice "Installed N chart cells." (+ unreadable count); launch opens the installed
set if any. Measured: Florida (698 cells, 65 MB zip of `testdata/enc-regions/FL`) import 863 ms on
the mac, open from index 21 ms (scan was 70 ms), via `ChartStoreBench.DISABLED_ImportRealZip` with
`FVW_CHART_ZIP`. On the simulator (iPhone 18 Pro Max) the Florida zip imported through Files and
drew; screenshots `apps/Pippin/docs/ch6-osprey-importing.png`, `ch6-osprey-florida.png`. Tests (16
new): ZipReaderTest (5), IsSafeZipPath (1), ReadS57DatasetInfo (1), FindS57UpdateFiles (1),
BuildEncIndex (1), EncIndexFile (2), ChartStoreTest (5); zip fixtures from
`port/Enc/test/zip_test_writer.h`. Bounds follow the charts: `EncChart::coverage()` is the union of
the cells in band 3 and finer (all cells when there are none; NOAA state zips carry band-1 cells that
span the whole Gulf). `openChartsAtURL:` makes it `PPMap.homeBounds`, and
`PPViewport.withCenterBounds(_:)` swaps the pan clamp and zoom-out limit; after an import a view
outside the new coverage moves to its home view, and a launch with an installed set opens on it
rather than on `pippin.home_bounds`. Shared places are judged against the same box
(`ChartStoreTest.CoverageLeavesOutOverviewCells`; sim: `ch6-osprey-new-region.png`, Charleston
imported over Florida, then the pan stops at the coastal cell's south edge). Not done: no cancel
button for an import; an import replaces the whole set (no merging of regions); updates still not
applied (CH10); the unreadable-cells list is counted in the notice but not shown; opening on a
whole state draws band 3 at state scale, which took several seconds on the simulator for Florida.

**CH7 — A whole state at speed (medium).** Use the CH6 index to open only cells in view and
pick the usage band for the zoom. Fix two known ENC defects from the ledger: one unreadable
cell aborts the exchange set; labels repeat across overlapping cells.

*CH7 as built (2026-10-23), ahead of CH2–CH6.* No CH6 index yet, so `EncChart::Open` indexes
with `EncFrameEnumerator` (CATALOG.031 bounds; an uncatalogued cell is parsed once).
`EncVectorSource::OpenIndexed` takes `EncCellEntry{path, bounds}` and parses a cell only when a
query meets it; `SetMaxResidentCells` (EncChart: 32 per band) drops least recently used cells
after each query, never ones that query needed; `ReleaseCells` drops them all, and `EncChart`
calls it on every other band when the drawn band changes. `Layers()` of an indexed source is
the whole Appendix A acronym list. `S57Cell` frees its node/edge tables once geometry is built.
Unreadable cells are skipped and listed (`unreadable_cells()`); Open fails only when none
reads. `VectorRenderer::SetLabelRepeatPixels` (off by default, goldens unchanged) drops a label
whose text was drawn within that distance this frame, on-canvas anchors only; `EncChart` uses
120 authored px. Florida, `EncChartBench.DISABLED_FloridaFootprint` (1320x2868, one draw per
band at Miami then 41 draws down the Keys at 1:50,000): peak footprint 1658 MB → 80–120 MB;
first band-4 draw 985 → 29 ms, band 5 1343 → 25 ms; worst Keys frame 42 ms. Charleston
coastal: `apps/Pippin/docs/ch7-charleston-labels.png`; sim `ch7-charleston-sim.png`. Not done:
different names still overlap (light descriptions); no collision placement.

**CH8 — Download from NOAA (medium).** Region picker (state and Coast Guard district zips),
background `URLSession` download with progress and resume, then the CH6 import. Confirm the
current NOAA download URLs when building it.

*CH8 as built (2026-10-25).* `ChartRegions.swift` (Foundation only): 34 states/PR plus 9 Coast Guard
district zips, URLs `https://www.charts.noaa.gov/ENCs/<ID>_ENCs.zip`, checked live 2026-10-25 (byte
ranges supported; 200 with Content-Length). Sizes are as NOAA listed them 2026-10-24, shown as "about
N MB". NOAA territory codes AQ/BS/GT/HT/FM/MH/PO are left out (District 14 covers them; PW and SP zips
are empty). `ChartDownloader.swift`: background `URLSession` (`<bundle id>.charts`), one download at a
time, progress callbacks; the zip is moved to `Library/Application Support/Charts/Downloads`
(ChartStore's RemoveStaleSets only deletes `set-*`). Resume data is saved as `<ID>.resume` when a
download fails and used by the next start; a cancel discards the download. `PippinApp`
`.backgroundTask(.urlSession)` hands back events while suspended; `attach()` at launch reconnects a
running task and claims a zip that finished while nothing was listening. MapModel: `downloadCharts`,
`cancelChartDownload`, `chartDownload`, `resumableChartRegions`; a finished zip goes through the CH6
import (`importCharts(from:removingAfter:)`) and is deleted afterwards. UI: "Download Charts" menu
item (Osprey only), `ChartDownloadSheet` in ChartSheets.swift (states, districts, a Resume mark, a
cancel row), notice "Downloading Alabama charts… N%". Import Charts is hidden while a download runs.
Test `pippin_chart_regions` (Swift, ctest); full ctest 2986/2986. Sim (iPhone 18 Pro Max): Alabama
downloaded and imported, view moved to Mobile Bay (`apps/Pippin/docs/ch8-osprey-download-sheet.png`,
`ch8-osprey-downloading.png`, `ch8-osprey-alabama.png`). Not done: no free-space check before a
download (the import still checks); a dropped connection not tested on the sim; region rows draw in
the tint colour; resume data from an older file is not checked against the server's ETag.

**CH9 — Update check (medium).** Compare NOAA's ENC product catalog (per-cell edition and
update dates) with the installed index; show "updates available" and fetch only changed
regions.

*CH9 as built (2026-10-26).* `fv_enc_catalog.{h,cpp}` in fv_enc: `ReadEncCatalog` parses NOAA's
ENCProdCat.xml (https://www.charts.noaa.gov/ENCs/ENCProdCat.xml, ~10.6 MB, 7,303 cells on 2026-10-26)
with expat: name, status, edtn, updn, states and Coast Guard districts (district n -> "0nCGD");
coverage panels skipped. Checked live: a regional zip holds exactly the active cells naming that
region (Florida: 687 active FL cells + 11 cancelled = the 698 in testdata/enc-regions/FL).
`CompareEncCatalog` sorts installed cells into new editions, new updates (same edition, catalog
updn > installed update files), cancelled by NOAA but still drawn, added (active in the region, not
installed) and unlisted; a cell cancelled locally but active in the catalog counts as a new edition.
Installed update number = count of update files, not applied ones (refetching the same files would
not fix a failed update); FL 698/698 matched updn by file count, so no DSID UPDN of the base is
needed and the index format stays 2. Region: recorded at import from the zip name
(`EncRegionFromZipName`, `<ID>_ENCs.zip` -> `region` file in the set dir, `ChartStore::CurrentRegion`),
else `InferEncRegion` (Jaccard over cells not cancelled; ties to the first region in the catalog).
`ChartStore::CheckForUpdates`; ObjC `PPChartUpdateCheck` + `-[PPChartStore checkForUpdatesWithCatalogAtURL:error:]`.
Swift: `ChartCatalog.fetch` (ChartDownloader.swift) caches the catalog in Library/Caches/ENCProdCat.xml
with its ETag (If-None-Match; 304 reuses the cache); `ChartUpdateText` (ChartRegions.swift) builds the
notice ("Florida charts are up to date." / "NOAA has changed 13 Florida chart cells: 3 new editions,
9 updated, 1 withdrawn. Download Florida again to install them."). MapModel: menu item "Check for
Chart Updates" (shown when an imported set is installed and nothing downloads or imports); launch
check when the last one is over 7 days old, off cellular and Low Data Mode
(allowsExpensive/ConstrainedNetworkAccess false), silent unless there are changes; the region with
changes gets an "Update" mark in Download Charts; an import clears it. "Fetch only changed regions" =
the one region a set is (an import still replaces the whole set).
Measured: live catalog vs the Florida set: up to date, region FL
(`EncCatalogBench.DISABLED_FloridaAgainstLiveCatalog` with FVW_ENC_CATALOG). The Charleston fixture
(2025) is behind the catalog (e.g. US4SC1BO updn 1, none installed). Tests: ReadEncCatalog (2),
InferEncRegion (1), CompareEncCatalog (3), EncRegionFromZipName (1), ChartStoreTest +2;
pippin_chart_regions extended. Full ctest 2995/2995.
Simulator (iPhone 18 Pro Max, Osprey): launch check fetched the catalog and stayed silent; menu check
shows "Alabama charts are up to date." (region inferred: that set predates the region file),
`apps/Pippin/docs/ch9-osprey-up-to-date.png`.
Not done: the "updates available" notice and Update mark not seen on the simulator (tests only); NOAA
reissued bases with DSID UPDN > 0 and no update files would read as new updates (none seen in FL); the
10 MB catalog is fetched whole when it changes (NOAA offers no compressed copy: .xml.zip is 404).

**CH10 — Apply S-57 updates (hardest).** NOAA's regional zips carry update files rather than
merged cells (Florida: 698 cells, 210 update files; New York: 289 cells, 140). Implement ER
update application in `fv_s57` and verify against known-good cells.

*CH10 steps 1–3 as built (2026-10-25).* `S57Cell::Open` reads every VRID/FRID record into an
`S57RecordSet` (`fv_s57_records.{h,cpp}`: unscaled coordinates, ordered VRPT/FSPT/FFPT rows,
features in file order with a deleted flag), applies `<stem>.001…` with `ApplyS57Update`, then
scales and assembles geometry. The applier checks EDTN and UPDN sequence, RUIN insert/delete/modify
with RVER = old + 1, VRPC/SGCC/FSPC/FFPC insert/delete/modify by 1-based index, ATTF/NATF/ATTV by
ATTL (a value of only 0x7F deletes), and refuses OBJL/PRIM/FOID changes. Each file applies whole or
not at all (undo log); a failure stops the chain there (CH-D5) and `unapplied_updates()` /
`StalenessWarning()` name it. EDTN 0 is a cancellation: the cell opens with no features and
`cancelled()`. The cell's issue date becomes the last update's ISDT (NOAA leaves UADT blank in
updates). `S57Updates::kBaseOnly` keeps the old behaviour for tests that pin the base edition.
Florida: all 210 updates apply (165 cells updated, 11 of them cancelled; +1,119 / −564 / 2,367 modified
features), no feature's geometry becomes incomplete, ASan clean; opening all 698 cells takes the
same time with updates as without (2.5 s). Charleston golden `S52Render.CharlestonHarborViewport`
re-pinned: with the update files removed the old hash reproduces, with them 848 px change in 28
clusters inside updated cells. Tests: `S57Update.*` (18, hermetic, `test/iso8211_test_writer.h`),
`S57Cell` +4.

*CH10 step 4 as built (2026-10-25).* `chart-index.tsv` is format 2: each row adds the update
files that apply and a cancelled flag, from `ReadS57UpdateState` (records read and updates
applied, no geometry). `EncChart::Open` rebuilds a format-1 index in place, leaves cancelled
cells out (so they no longer count toward coverage) and counts cells with updates that did not
apply (`outdated_cell_count`). An uncatalogued cell's bounds come from its base edition, so a
cancelled one is still indexed. `chart-names.tsv` is format 2, so a table built from base
editions is rebuilt on first search. Cancelled cells are not passed to the names build.
`ChartImportResult` carries cancelled and out-of-date cells and applied and failed update
counts. The import notice reads "Installed 687 chart cells with 210 updates applied. 11
withdrawn by NOAA are left out."; launch shows "N chart cells are not fully up to date." (CH-D5)
via `PPMap.outdatedChartCellCount`. Florida import 0.86 → 1.76 s (the update pass); open from
index 38 ms. GDAL cross-check (CH-D7, GDAL 3.13.3): `port/tools/enc_update_diff.py` with
`fv_enc_dump` compared FOID, OBJL, attributes and vertex sets for all 172 cells with updates
(Florida 165, Charleston 7): base editions and applied updates agree on all 161 cells that are
not cancelled (148,808 features). The 11 cancelled cells differ only because GDAL ignores
EDTN 0. Raw differences between the two readers: GDAL recodes ISO 8859-1 text to UTF-8 and
drops empty list items, and `fv_enc` passes ATVL through as stored. Pinned without GDAL:
`S57Update.PinnedFloridaCells` (US5JAXEC, US5JAXFF, US4FL1YB, US4FL2DJ, cancelled US1GC09M:
feature and vertex counts, content digest). Not done: the New York set is not on disk, so it is
not cross-checked; text attributes reach labels as Latin-1 bytes, not UTF-8 (to check);
no before/after app screenshots.

## Data layout

- `testdata/enc/` stays the fixed Charleston fixture; the ENC tests open it recursively, so
  nothing else goes under it.
- Regional sets go in `testdata/enc-regions/<REGION>/ENC_ROOT/`, unpacked exactly as NOAA
  ships them (CATALOG.031, README, cell folders with updates). The device chart store uses the
  same layout, so an import or download test can point at either.

**CH11 — Chart settings and feature query (medium; any time after CH1).** Safety depth and
safety contour, display category (Base / Standard / All), day / dusk / night palettes (night
follows dark mode). Tap a feature for its attributes (light characteristics, depths, names).

*CH11 as built (2026-10-25).* `pippin::EncChartSettings` (safety contour, safety depth, shallow and
deep contours in metres; display category Base / Standard / All; day / dusk / night palette) on
`EncChart::SetSettings`, which clamps shallow <= safety <= deep and maps onto the S-52 engine's
`MarinerSettings`, `ViewingGroupSet::SetMaxCategory` and colour table. Day keeps the white ground;
dusk and night clear to the table's NODTA. Defaults are S-52's, so an unset chart draws as before.
`EncChart::FeaturesAt` reads the last base draw's pick index (drawn ink, topmost first, area fills
included) and describes each hit through `EncVectorSource::Describe`; `EncFeatureSummary` writes
Chart No. 1 notation for lights (`Fl(2) W 30s 49.7m 20M`), DEPARE/DRGARE ranges, DEPCNT values and
WRECKS/OBSTRN/UWTROC depths, in metres or feet. PPMap: `chartSafetyContour`, `chartSafetyDepth`,
`chartCategory`, `chartPalette` (Automatic = night while `darkStyle`; `hasDarkStyle` is YES for chart
packs), `chartDepthsInFeet`, `chartFeaturesNear:`. Swift: `ChartSettings.swift` (persisted in user
defaults; depth pickers offer round feet or metres and snap on a unit change; app default feet, a
15 ft safety contour and safety depth), `ChartSheets.swift` (settings sheet from the menu, feature sheet at a
medium detent). In Osprey a tap that hits no pin lists the chart features there. Tests: `EncChart.*`
+2, `EncFeatureSummary.*` (2), `EncChartSettings.*` (1), `pippin_chart_settings`. Sim:
`apps/Pippin/docs/ch11-osprey-light-info.png`, `ch11-osprey-settings.png`, `ch11-osprey-night-base.png`.
Not done: the shallow/deep contours and two-shade option have no UI; the sheets are plain sheets on
iPad too, not the tool card; sector-light legs are still drawn as a flare (LIGHTS05); a tap lists
every stacked feature (up to eight), with no merge of the same object across cells.
Soundings fix (same day): `EncVectorSource` passed one point and the first vertex's depth per
SOUNDG record, so each record of 10-300 soundings drew one; it now emits one point feature per vertex
with its own DEPTH (US5CHSDC: 11 → 165). `kHashCharleston` re-pinned. On the phone soundings still
follow SCAMIN at true scale (harbour cells 1:18,000, approach 1:60,000) and print in metres, as
S-52 has no feet sounding symbols. Sim: `apps/Pippin/docs/ch11-osprey-soundings.png`.

**CH12 — Help screen (small).** An Osprey help sheet from the map menu, plain text, that explains:
- Depth soundings are in metres whatever the depth unit setting is; a smaller subscript digit is
  tenths of a metre (S-52 has no feet sounding symbols).
- The CDI card's abbreviations: XTE (cross-track error, distance and side off the leg), DTK
  (desired track, the leg's course in °T), BRG (bearing to the active waypoint, °T), TTG (time to
  go to the active waypoint at the current SOG) and ETA (clock time of arrival at that waypoint).

*CH12 as built (2026-10-25).* `ChartHelpSheet` (`ChartSheets.swift`) opens from a "Chart Help" item in the
map menu (`MapMenuButton.onChartHelp`), shown only when `model.importsCharts`; Pippin's menu is unchanged.
Sections: Depth Soundings (metres whatever the depth unit; smaller digit is tenths; no feet symbols in
S-52; at or shallower than the safety depth black, deeper grey) and Course Card (XTE, DTK °T, BRG °T, TTG
at current SOG, ETA clock time; TTG/ETA show a dash until under way). Medium and large detents. Swift
only, no new tests. Sim: `apps/Pippin/docs/ch12-osprey-chart-help.png`.

**CH13 — Waypoint labels on the chart (small).** Each route marker carries its label ("Start",
"WP 1" ... "End"; "Via n" in Pippin) beside the diamond, readable at phone scale.

*CH13 as built (2026-10-25).* Osprey labels intermediate stops "WP n" (`AppFlavor.intermediateStopPrefix`;
Pippin keeps "Via n"). `RouteOverlay::DrawMarker` drew its label at a fixed 12 px with a 10 px offset,
about 4 pt on a 3x screen; size and offset now scale with `symbol_dpi_scale` and clear the marker,
as `PointOverlay` names do. Unchanged at scale 1 apart from a 2 px wider offset.

**CH14 — Search the chart (small).** Osprey's search box finds named chart objects (S-57 `OBJNAM`)
as well as the user's pins: aids to navigation by name or number, bridges, anchorages, marinas,
landmarks, named waters and channel reaches.

*CH14 as built (2026-10-25).* `EncChart::search_source()` is an `IVectorSource` that forwards to
the band of the last base draw, so a search finds what the chart shows at that scale; it is
detached when the chart is destroyed. PPMap wraps it in the hidden `VectorMapOverlay` "Chart" with
label tag `OBJNAM` and rebuilds it when an import replaces the chart. ENC cells have no name index,
so the search is always windowed to the viewport (`search.chart_in_view_only` does not apply).
Charleston cells carry 519 distinct names (128 lateral buoys and beacons, 52 special marks, 100
waters and rivers, 107 reaches and fairways, 7 bridges, 4 anchorages). Results are POI rows; the
shared kind vocabulary (Point / Road / POI) is unchanged. Test: `EncChart.SearchFindsNamedObjectsInTheDrawnBand`.
Not done: no symbol of its own for aids to navigation; `NOBJNM` (national-language names) is not
searched.
Whole-set search (same day): the view-only limit made search close to useless, so text queries now
go to a named-object table. `fv_enc_names.{h,cpp}` (fv_enc): `BuildEncNames` reads every cell's
OBJNAM objects and merges the same name and class within 200 m across cells and bands, keeping the
finest band's position; `chart-names.tsv` beside `chart-index.tsv`. `ChartStore::SetCatalogDir`
(PPChartStore `catalogDirectoryURL`, from `PPMap.chartCatalogURL`) builds it at import as the last
part of the progress bar; a set imported earlier, the bundled pack and the fixture build it on first
search (cached only beside an installed set's index). The search source answers `SearchNames` from
it, best match then nearest the last draw. Florida: 40,248 named features → 21,662 rows, 3.9 s to
build on the mac, 2.2 MB, 10 ms to read. Tests: `EncNames.*` (3),
`ChartStoreTest.ImportWritesTheNameTableWhenGivenACatalogue`,
`EncChart.SearchFindsNamedObjectsAnywhereInTheSet` (replaces the drawn-band test).

## Risks

- **S-101 transition.** NOAA is moving from S-57 to S-101; check its S-57 end date before
  investing in CH9–CH10.
- **Navigation claims.** The S-52 rendering is not ECDIS-certified; the app must not present
  itself as a navigation system (App Store review, liability).
- **Storage.** The zip is kept only while unpacking; unpacked cells plus the index are larger.
