# FalconView Cross-Platform Port — Working Ledger

**Read this first in every session. Do not re-explore the repo.** It holds the map of what
exists, the open work, and the rules that still constrain new code. It is deliberately short.

- **History lives in `port/archive/`** — read it only when a line here sends you, or when a bug
  turns up in code whose design you need. `archive/PORTING-2026-10-05.md` is this ledger as it
  stood before it was cut down (every measurement, design note and resolved defect, verbatim);
  `archive/PORTING-ARCHIVE.md` is the module table and dated decision log before that. §5 indexes both.
- **Pippin (the iOS app) keeps its own ledger**: `port/pippin-plan.md` + `port/apps/Pippin/README.md`
  (build: `port/apps/Pippin/BUILDING.md`). Nothing about Pippin's sessions is tracked here.
- **Keeping it short** (the rule that stops this file regrowing to 285 KB): an open item is at most
  three lines. When work lands, *delete* its row here and put the narrative in the commit message,
  or in a dated entry at the end of `archive/PORTING-ARCHIVE.md` if it is a decision a later session
  would re-litigate. Rules go in §3 only if violating them ships a silent bug.

```sh
cmake -B build && cmake --build build -j && ctest --test-dir build
# 2431 in ctest's count (17 disabled GeoTrans, 3 skipped), all green at -j8, 2026-10-08. The
# default build type is NOT optimised: configure -DCMAKE_BUILD_TYPE=Release before measuring performance.
```

**Live docs** (everything else is in `port/archive/`):
`port/fvkit-contracts.md` (D1–D6: ownership, geo, Status, pixel, naming, adapters — always binding) ·
`port/bindings/pyfvw/README.md` (Python user guide) · `port/bindings/pyfvw/ICD-MAPPING.md` ·
`port/peregrine.ini.sample` (every settings key) · `port/families/*.json` ·
`port/Osm/styles/style-readme.md` (supported GL-style subset) · `port/NOTICE.md` (licensing).

---

## 1. What exists (so you don't go looking)

**Geo/math** — `port/{geoid,geo3,geo_tool,geotrans,MapScaleUtil,MapSeriesStringConverter}`.
GEOTRANS 3.3 is **frozen** (pinned bit-faithful results).

**Raster products** — each an `IRasterSource` + enumerator in the format registry (`geotiff`,
`cadrg`, `tiros`, `dted`, `dted-shaded`, `gpkg`, plus the vector products):
`port/{ImageLib,ImageLibCore,CadrgDecoder,CadrgMapServer,GeoTIFFMapServer,TirosMapServer,DtedMapServer,DtedShadedRenderer}`.

### 1a. FvKit core — `port/include/fvkit/`, impl `port/fvkit/`

| Area | Headers | Invariants that constrain new work |
|---|---|---|
| Engine, catalog, store | `engine.h`, `catalog/`, `store/tile_pack.h`, `settings.h` | Catalog **schema 2**: a series is `(format, series_key, scale, scale_units)`; old catalogs are rebuilt, not converted. `fv::Settings` is read-only (rule S1). **Decided (Chris, 2026-10-08)**: `RenderBaseMap` skips a frame whose source fails to open/`Info`/`ReadBlock` and lists it in an optional `std::vector<SkippedFrame>*` rather than failing the whole render; other failures still stop it. `BaseMapRenderer`/`DeskHost` surface the skip count/path in the status bar; pyfvw does not expose it yet. |
| Canvas | `canvas/` (ICanvas, CpuCanvas) | CpuCanvas decodes UTF-8 and can be a translucent layer. No pattern brush, no clip region, no layer alpha (§2b). |
| Projection | `proj.h` | Five `ProjectionType`s (Equal Arc, Mercator, Lambert, AzEq, Orthographic) + rotation. **Rotation 0 and Equal Arc are byte-exact because the arithmetic is gated**, not because a matrix is identity. Surface centre is `((w-1)/2, (h-1)/2)`. `kNotProjectable = -7`. |
| Vector seam | `vector/`, `style.h`, `rules.h`, `families.h`, `mariner.h`, `lookup_engine.h`, `renderer.h`, `scene.h`, `pick.h` | GeoSym, S-52 and OSM are **loaders over `LookupTableStyleEngine`**. Pick index is filled from emitted ink. |
| Geographic lines | `geo/contour.h` (G1) | **Clips in geographic space before densifying.** Step size from screen dpp. |
| Symbols | `symbol/` (G2) | Every style engine IS an `ISymbolLibrary`. A pivot is in tile pixels; re-`Open` replaces. |
| Overlay drawing | `canvas/geo_draw.h` (G3), render state (G4) | `GeoDraw` is the surface an overlay calls. A highlight never enters the pick index. `symbol_dpi_scale` is the DPI hook. |
| Terrain | `geo/terrain_contour.h`, `geo/elevation_tiling.h` | Marching squares. A lattice tile is NOT a DTED cell — partial coverage is per post. |
| Analysis | `analysis/{path,profile,viewshed,measure}.h` | Range & Bearing + Intervisibility compute; UI is Python (`port/apps/analysis.py`). |
| Moving map | `nav/` (MM1–MM7), `overlay/moving_map_overlay.h` | Every `PositionFix` field carries its own validity. The camera never touches the engine. Snapper stands before the heading resolver. NMEA accepts any talker. GPX writer appends and is valid after every fix. Guidance: `nav/maneuver.h`, `nav/guidance.h`, `nav/trip.h`. |

**The shared overlay toolkit — use it before porting any overlay:**
`ScaleTable<T>` (`scale_table.h`; key it with `ScaleDenominatorFor(proj)`, never `proj.Scale()`,
which is 0 in resolution mode) · `LabelPlacer` (`canvas/label_placer.h`; call order is priority,
boxes via `MeasureLabelInk`) · `app::Properties` (`app/properties.h`; `OverlaySession::Instantiate`
loads them from the type's settings section — nothing in a shell has to) · `path_shaping.h`
(pixels, on the projected path; thin then smooth) · `ElevationTiling` (lattice only; payload stays
with the caller). **Port a Windows overlay for its decisions** (tables, thresholds, draw order) and
discard its machinery (class hierarchies, static CLists, registry reads, property pages).

Built overlays: `fv.grid` (graticule; MGRS/GARS dropped), `fv.points` (`.fvpoints` SQLite, schema 3,
embedded artwork; editor `point_edit.h` + `port/apps/points.py`), `fv.contour`, `fv.tamask`,
`fv.scalebar`, `fv.movingmap`, `VectorMapOverlay` (any vector source drawn as an overlay, and searchable).

### 1b. App layer — `fv::app`, `port/include/fvkit/app/`

Type registry (`TypeId` is a string; an optional `FileTypeDesc` is the static-vs-file
distinction) · capabilities found by **accessor, never `dynamic_cast`** · `OverlayManager` is the
stack · `AppShell` is the whole UI seam (a cancel is the user's answer and propagates) · editors
and the mode dance · picking aggregates in reverse `DrawOrder()` · search (`search.h`,
`SearchSession`; providers: points, routes, `VectorMapOverlay`, `RoadGraphOverlay`; tier 2 is the
FTS5 name index written inside the `.mbtiles` by `fvnames`). `app/test/fake_shell.h` makes the
layer unit-testable.

### 1c. Vector products

- **DNC/VPF** — `port/VpfMapServer/` + `port/GeoSymServer/` (`GeoSymStyleEngine`).
- **ENC/S-57** — `port/Enc/` (ISO 8211, S-52 PresLib, `S52StyleEngine`). Text is its own band (`kS52PrioTextBase`).
- **OSM** — `port/Osm/` (MBTiles + MVT, `OsmVectorSource`, `OsmStyleEngine` loading MapLibre JSON;
  `styles/peregrine-osm.json` base sheet, `peregrine-osm-overlay.json` overlay sheet).

### 1d. Routing — `port/Routing/` and `port/RouteKit/`

`port/Routing`: graph built offline from `.osm`/`.osm.pbf` (`fvgraph build|info|route|profiles`),
`.fvroad` v2, bidirectional Dijkstra over split states for turn restrictions, profiles with per-mode
access bits on the arc, mid-arc snapping, ordered stops, ferries/tolls, golf-way flags, weights in
`rules/route-weights.json` (live-reloaded). **fvkit does not link Routing**; `port/RouteKit/` links
both: `RouteDoc` (`.fvrte`, byte-compatible with Python's `json.dump`), `RoutePlanner`,
`RouteOverlay`, `RouteEditSession` (the one editor both shells use), `RoadGraphOverlay` (debug view,
coloured by profile weight, with a what-if), route maneuvers.

### 1e. Apps, bindings, tools

`port/bindings/pyfvw` (`pyfvw.{vector,catalog,engine,overlay,canvas,routing,route,draw,symbol,geo,nav,app,analysis,formats}`) ·
`port/apps/PythonView.py` (the tk desktop app, an `AppShell`; `--selftest`, `--shot`) + `route.py`,
`points.py`, `analysis.py`, `toolbar.py`, `settings_file.py` · `port/apps/Pippin/` (iOS; own ledger) ·
`port/apps/Peregrine/` (mac desktop app, AppKit over `DeskHost`; own README) ·
host CLIs `fvrender`, `fvpack`, `fvgraph`, `fvnames` · `port/tools/` (scripts, incl.
`sync_peregrine.py`) · `port/cmake/FvwStaticClosure.cmake` (the static-lib closure helper, shared
by Pippin and Peregrine).

### 1f. ViewKit — `port/ViewKit` (`fv::view`)

The interactive map view shared by the desktop shells (viewport, scale ladder, MapView
controller, render scheduler/frame cache). Design and gaps: `port/desktop-plan.md` §2a.

### 1g. DeskKit — `port/DeskKit` (`fv::desk`)

The desktop application model: command registry, menu model, map groups, overlay manifests
(`fv_desk_overlay_manifest.*`; only `builtin` registers), options model
(`fv_desk_options.*`, generated from `app::Properties`, covering both the Overlay and the Map
dialog; built-in map pages in `fv_desk_map_options.*`), workspace, user settings, `Desk`,
`FakeDesk`. `DeskHost` (`fv_desk_host.*`) is the one object a native shell drives — owns
Settings, a queued shell, `Desk` and a render scheduler; `BaseMapRenderer`
(`fv_desk_base_map.*`, raster formats only) renders for both `FakeDesk` and `DeskHost`.
Design and gaps: `port/desktop-plan.md` §2b, §3a.

### 1h. Test data — `testdata/` (git-ignored)

dted · geotiff DOQs + `Atlanta SEC.tif` (LZW, Lambert) · rpf CADRG · tiros3 · `vpf/dnc17` ·
`VPF 2/WVSPLUS` · `GeoSymbol/` · `OSM/map*.osm` (**enumerate, never name them**) ·
`OSM/kiawah.{fvroad,mbtiles}` (build artifacts) · `OSM/mbtiles/us-south.mbtiles` ·
`kiawah_cycle.gpx` · `enc/` (8 Charleston cells; 815 more in the sibling `enc-archive/`).
Rebuild recipes are in the archive.

---

## 2. Open work

**Desktop app (DK)** — plan `port/desktop-plan.md`, approved 2026-10-05 (K1–K15 decided by Chris; K16 made in DK5; K17 implemented in DK6b, defaults awaiting Chris's confirmation).
DK1–DK6e done (2026-10-05/08). Next: **DK7** (Linux spike, desktop-plan §5). DK6d/DK6e's mac
proof (DNC/ENC/OSM drawing in Peregrine after Generate Coverage vs PythonView's `--shot`) is for
Chris by hand — needs `geosym.data_dir`/`enc.data_dir`/`osm.style` set in `peregrine.ini`.
K17 open: Chris's confirmation of the shipped defaults (Info/Debug, 5×5 MB); Windows `ERR_report` not routed through it.

**Linux app (LX)** — plan `port/linux-plan.md` (2026-10-07). `port/apps/PeregrineGtk` is built by
a Claude cloud session in the Peregrine repo, delivered as `linux/*` PRs with Ledger notes.
Back-port each merged PR into FVW (`format-patch | git am -3`) **before** the next sync, or the
sync deletes it. Peregrine's `CLAUDE.md` is dest-owned and holds that session's rules.

### 2a. Open threads (each optional; pick on Chris's word)

| Item | What is left |
|---|---|
| **DK6b follow-ups** | No `os_log` sink and no in-memory ring sink yet (§2e lists both); Pippin → `os_log` and pyfvw → `logging` still to do; LX's GTK shell needs its own `StartLog` call and Help ▸ Show Log. |
| **D1 route-data audit** | Unstarted tool in `port/tools/`: Monte Carlo reachability sweep, missing-crossing candidates, islands, snap traps; output coordinates for upstream OSM fixes. Oracle: "anywhere on Kiawah by bike, walking ≤25 yd". Worked example: archive snapshot §2a D1. |
| **ViewKit frame cache** | Last-frame only; Pippin's guard band/underlay (`PPBaseCoverage.h`) stays in Pippin until Pippin moves onto ViewKit (DK11 optional track). |
| **ViewKit input** | Editor input routing, picking and hover hints (desktop-plan §2a last bullet) deferred to DK8. |
| **ViewKit product stability** | `MapView` keeps its current product while panning; it re-chooses only on a ladder step, a pinch settle or a map-group change. |
| **DeskKit command registry gaps** | `file.export_image`, `map.goto` have menu layout slots but no registered command yet — each lands with its own session. |
| **DK3 follow-ups** | fvkit's built-ins (grid, points, contour, tamask, scalebar, movingmap) still register in code, not via manifest (deliberate, avoids a drifting JSON copy); no bundled `overlays.json` yet; a static-lib builtin registrar needs an explicit referencing object (dead-stripping). |
| **DK5 follow-ups** | A data source's scan can't be interrupted (cancel waits for it); readers wait up to 10s on the busy timeout during a big scan's commit; progress sheet/Build panel not driven live (out-of-process open panel) — Chris to try by hand; GTK has no synchronous modal dialogs, so LX's shell needs a nested main loop for `SetRequestHandler`. |
| **LX Map Data Sources** | GTK shell needs its own dialog over the new `DeskHost` calls; `map.catalog_build` removed, `map.catalog_rescan` → `map.generate_coverage`. |
| **Generate Coverage all-or-nothing** | No incremental rescan; a cancelled generation leaves a partial catalog. |
| **DK5b follow-ups** | `Execute`'s `StackEdit` lock is held across a modal request handler, so frames pause during a modal dialog; DK8 editor input routing must take `StackEdit` when a pointer event reaches an editor; overlays have no cancel inside their own `OnDraw` yet. |
| **DK6 follow-ups** | Raster pixel pitch is still a `MapOptionsSource` to add; elevation breaks are a text field (a custom breakpoint editor is a later custom page); a newer options request replaces an open dialog's unapplied edits. |
| **DK6d follow-ups** | Vector labels off (no desktop default font on the canvas); a vector frame is not interruptible (`VectorRenderer` has no cancel hook); `[vector]` scene_margin/simplify/label_reference_scale and families_* not read by DeskKit; `DeskHost` logs a warning on every failed render, so an unset geosym dir logs once per frame; overlay-sheet (`osm.overlay_style`) not used. |
| **Peregrine core deployment target** | `libperegrine_core.a` builds for the host macOS (27) while the app targets 14.0 (ld warns) — set `CMAKE_OSX_DEPLOYMENT_TARGET=14.0` before DK10. |
| **O5 routing** | Via-way restrictions (recognised, not applied); steps and elevation have no cost; ferry timetables unmodelled; "walk the bike" mixed-mode undecided. |
| **MM5b** | Viterbi map-matching to remove along-track error (seam ready in `RoadCandidate`). |
| **Overlays** | Tier 3 only: `PrintToolOverlay`, `nitf`, `Cov_ovl` — heavy shell coupling, last or never. **Dropped, do not re-queue**: `shp`, `localpnt`, `ar_edit`, `TacticalModel`, `SkyViewOverlay`. No shell turns on `fv.scalebar`; nothing feeds `fv.tamask`'s `SetAltitude` from the moving map. |
| **Data** | Rebuild `kiawah.fvroad` once Chris's `bicycle=designated` OSM edit on the Ocean Course path propagates. |
| **R3d perf** | Only on a fresh profile; last one pointed at `LookupTableStyleEngine` styling. |

### 2b. Rendering and product gaps

- `ICanvas` lacks pattern brush, clip region and layer alpha — one off-screen-layer session fixes
  all three (area patterns bleed past rings; outer ring only; top-most opacity unapplied).
- No label collision/de-duplication in `VectorRenderer` (ENC repeats names per overlapping cell/band).
- Chart symbols ignore device DPI (would move GeoSym goldens); ENC text size read as pixels; S-52
  `SPACE`/`DISPLAY` skipped; halos have no blur and only OSM sets one; OSM colour stops step.
- Rotation: no user gesture in PythonView; turned blit is nearest-neighbour.
- **WVS** blocked: `fv_vpf` builds feature classes only from FCA — enumerate from FCS/dir scan.
- `VpfVectorSource::Bounds()` full scan; CIB hardcoded off; `FeatureStore` unbuilt; GIF untested;
  `Image.cpp`/`nitf/` unported.
- Routing scale: per-query scratch (~700 MB on a continent), snapper index over whole graph, no
  antimeridian in `RoadGraph`; no real-data ferry/toll fixture; clipped ferry duration prorated wrong.
- A route with labels on and no default font draws nothing.

### 2c. PythonView UI gaps

No reorder dialog; `route_double_click` uncalled; GPX track not drawn; snap-to-road has no key;
no UI for toll/ferry avoidance, `private_penalty`, U-turn-at-stops, per-leg profile, rule/family
picker, mariner panel, ENC data dir, OSM source knobs; route line style derived from profile name
(and see the dash defect, §2d).

### 2d. Known defects and hygiene

- **Bicycle route dash is invisible** at the 6-px route width (round caps close the 6-unit gap).
  Choice pending: square cap, wider gap, or pen-scaled pattern (moves dashed goldens).
- `PythonView.py --selftest` aborts in `RPFRenderer::get_rgb_image` on a local CADRG frame.
- `build/pythonview.sqlite` stores paths relative to `port/apps`; start PythonView from there.
- Sweep every `fs::temp_directory_path()` for shared scratch names (parallel-ctest races; several fixed).
- `pyfvw_pytest` cannot run in the ASan build.
- ENC: one unreadable cell aborts the exchange set; render tests open `enc/` wholesale.
- GeoTIFF: LZW wired on all paths but only 8-bit palette strips have a fixture; corner-based bbox
  under-covers conic sheets.
- TIROS tile seams (pixel-centre bounds vs the blit's edge convention) — diagnosed, not fixed;
  Peregrine's Recenter on Data can land on such a seam when a world TIROS is in the catalog.
- Windows sockets in `line_transport.cpp` never compiled. C++14 pins on `fv_jpeg`, `fv_jpeg12`,
  `fv_imagelib_gif`. `CDTEDInstance` stub in ImageLib `Util.cpp`.
- Preserved originals (do not "fix"): VPF unaligned loads (UBSan), `VPFRecordset` reopen
  undercount, `CCGMPattern::IsSolid()` never true. Slow subsampled CADRG `ReadBlock`; polar CADRG
  unsupported.

### 2e. Dependencies

Done: googletest, zlib, expat, protozero, vtzero, nlohmann/json (FetchContent,
`port/third_party/CMakeLists.txt`). Frozen: GEOTRANS 3.3. Left, in order: **libpng 1.2.7→1.6**,
**libtiff 3.9→4.7**, **IJG jpeg 6b→libjpeg-turbo** (encryption fork to clear first). Anything
parsing network input (Q12) must be on a modern library first.

### 2f. Backlog

Q12 WMS (libcurl) · Q13 JP2 via OpenJPEG · Q14 NITF · Q15 GeoPDF · Q16 Lidar · V7 CoreGraphics
`ICanvas` · V8 symbol atlas (measured as a non-goal) · G5 SVG symbols · dimming as a `RenderState`
(deferred by Chris) · contour C4 auto interval ladder · OSM Bright as a second reference style.
**Deprioritized**: ECRG, CIB, MrSID, RDted, BlankMapServer. **Deferred**: CoT, MdsUtilities,
Collaborate, NITFSourcesCtrl, MapOptions pages, FvConfigFileServer.

### 2g. Needed from Chris / the Windows machine

- [ ] Reference output dumps from the Windows build (elevations, decoded-pixel checksums).
- [ ] Reference screenshots of dnc17 harbour views for the DNC goldens.

---

## 3. Standing rules — violate one and you ship a silent bug

**Porting mechanics**
- **Bit-faithful**: preserve and document original quirks; fix only bugs in the port.
  Exception: projection rendering need not match Windows GDI pixels.
- **Compile in place** from `fvw_core/`; sever COM/GDI with `#ifdef _WIN32`
  (`port/tools/guard_win32_functions.py`). Never modify `BuildAll.sln`, `*.vcxproj/proj/idl`,
  `WinDebug/`, `WinRel/`, `third_party/` (force-include from CMake instead).
- `LONG` is `long` on Win32 but `int32_t` here — expect declaration/definition mismatches.
  `LONG`/`DWORD`/`HRESULT` widths are ABI; mirrored IDL enums keep COM values — never renumber.
- Latin-1 shared sources: use escapes (`'\xB0'`), not raw bytes.
- Compat shims (`port/include/fv_*.h`): prefer rewriting to standard C++ over growing them.

**Licensing**
- Every new file under `port/` is born with `// SPDX-License-Identifier: LGPL-3.0-or-later`, the
  Chris Bailey copyright and the `See COPYING.LESSER and NOTICE.md` pointer. Never write GPL into a
  header; never relicense (GTRC's LGPL is the floor).

**Tests and goldens**
- A golden hash proves nothing about orientation, colour or units — add a directional assertion.
- Never pin a total over a data directory, never name a fixture's input files (enumerate
  `map*.osm`), never pin a settings file's contents. Assert structure, not counts.
- Re-pin a golden only after looking at the PNG; record old → new hash in the commit.
- Choose a fixture that can show the thing under test. A test's scratch file is its own.
- Tests needing map data must `GTEST_SKIP()` without it (Peregrine ships no data).

**Vector seam**
- `VectorSymbol` is y-UP; CGM display lists are y-DOWN.
- Product data properties (SCAMIN, `dispcat`) belong in the source, not the style engine.
- `IStyleEngine::Style()` appends; sort is stable by priority. A GL style's draw order is
  style-layer order (`priority` = layer index).
- The retained scene keys on the whole `StyleContext` and an exact scale; a no-op setter must stay a no-op.
- A kChoice property is spelled by NAME in settings and pyfvw.

**Data facts**
- ENC `.000` is base edition only; enumerate `*.000`. S-57 `?` is UNKNOWN, not a wildcard. S-52
  text needs lifting out of its object's priority band. HJUST/VJUST 1 = centre, 2 = right/bottom,
  3 = left/top; take `CHARS` size from the last two characters.
- CGM monochrome pattern bits are stored inverted.
- MBTiles `bounds` cannot be trusted. **Zoom↔scale has ONE implementation**
  (`webmerc::ZoomForScaleExact`, latitude-dependent); a style engine must be told the reference
  latitude and the same `mm_per_pixel` as the source.
- MapLibre `!=`/`!in` are true for a missing tag; fvkit's are false — reconcile in the loader.
- An OSM style's layer order is the style file's business; ask which style is loaded.
- DTED: level 3 out of scope; nearest-neighbour; bands default to feet.
- `GEO_east_of_degrees(a, b)` means "a is east of b". The scale ladder gives the current product first refusal.

**Routing**
- Turn restrictions need split states, not a label per node; a bidirectional constraint must be a
  barred turn or a split state, never a filter on first relaxation.
- "The arc I arrived along" is `RoadGraph::TwinArc(from_node, arc)`; `ArcBetween` only when all you have is a node pair.
- ONE place decides an arc's duration (`ProfileSeconds`) and ONE its usability (`routing::ArcUsable`).
- `access=private` is priced, never deleted; `bicycle=no` on Kiawah is real.
- A ferry is `route=ferry`, often with no `highway`; `duration` may be bare minutes.
- PBF `Relation.memids` are delta-encoded across all member types.
- `.fvroad` build is input-order dependent — `cmp` means nothing unless inputs are ordered the same.

---

## 4. Per-module recipe and session protocol

1. `port/<module>/CMakeLists.txt` compiling sources in place from `fvw_core/<module>/`; add the
   `add_subdirectory` to the root `CMakeLists.txt`.
2. Build compiler-first; don't read files speculatively. Repeated transforms → script in `port/tools/`.
3. gtests pinning known-good values in `port/<module>/test/`; `ctest --test-dir build` green.
4. Update this ledger (delete the finished row; see the size rule at the top) and commit.

One module per session. Mechanical work goes to the Sonnet agents named in `CLAUDE.md`
(`ledger-scribe`, `port-mechanic`, `test-runner`, `log-triage`); decisions and commits stay on the
main thread.

**Publishing to Peregrine** (github.com/ChrisABailey/Peregrine): run the whole FVW suite first,
check `git status` for uncommitted tracked files, then
`python3 port/tools/sync_peregrine.py --dest ../Peregrine [--apply]`. In the clone: diff its
dest-owned root `CMakeLists.txt`, `.gitignore` and `README.md` against FVW, check for untracked
strays, build and run `ctest -j` more than once before committing.

---

## 5. Where to look in the archive (`port/archive/`)

| File | Read it for |
|---|---|
| `PORTING-2026-10-05.md` | The long-form ledger as of 2026-10-05: per-layer design notes and measurements (§1), every open item's full reasoning (§2), and the dated archive index (§5). Search it by symbol name. |
| `PORTING-ARCHIVE.md` | Module table rows 1–14u and every lettered session (R, E, F, S, O, K, T, M, A, G, MM, PR), the dated decision log, and the 2026-08-16 condensation. |
| `vpf-geosym-plan.md` | Vector/symbology design (§5 cross-product middle layer, §7 ENC); V7/V8/WVS design. |
| `fvkit-app-plan-COMPLETE.md`, `fvkit-draw-plan-COMPLETE.md` | App layer A1–A6; geographic drawing G1–G4 (dimming design in §3d). |
| `fvkit-nav-plan.md` | Moving map MM1–MM7; MM5b design. |
| `search-plan-COMPLETE.md` | Global search S1–S4. |
| `projection-plan.md` | Projections PJ0–PJ6 (D7: no GDI pixel compatibility). |
| `contour-plan.md`, `tamask-plan.md`, `analysis-plan.md`, `point-editor-plan.md` | Those overlays and tools. |
| `pippin-guidance-plan.md` | Pippin's turn guidance (GD) and screen-off tracking (BG). |
