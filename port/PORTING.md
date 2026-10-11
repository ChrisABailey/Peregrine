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
# 2665 in ctest's count (2645 pass, 3 skipped, 17 disabled), 0 failed, 2026-10-11 (2559/2542/17 on 2026-10-10). The
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
| Features | `feature/` (FO1, FO2 `geopackage.h`) | Positions are lat,lon (contracts D2); the swap to x,y happens only in the WKB/GeoJSON codecs. A default `Geometry` is null. `fv_schema` is how our GeoJSON keeps its schema (titles, units, choices, ranges); files without it get inferred types. |

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
  `styles/peregrine-osm.json` base sheet, `peregrine-osm-overlay.json` overlay sheet;
  `atlanta-streets{,-dark}.json` city bike-comfort map, dark sheet generated by `tools/make_atlanta_dark_style.py`,
  staged in Pippin's Atlanta pack by `stage_data.py`; test `OsmReferenceStyle.AtlantaStreetsTiersRoadsAndHoldsBackClutter`).

### 1d. Routing — `port/Routing/` and `port/RouteKit/`

`port/Routing`: graph built offline from `.osm`/`.osm.pbf` (`fvgraph build|info|route|profiles`),
`.fvroad` v2, bidirectional Dijkstra over split states for turn restrictions, profiles with per-mode
access bits on the arc, mid-arc snapping, ordered stops, ferries/tolls, golf-way flags, weights in
`rules/route-weights.json` (live-reloaded). **fvkit does not link Routing**; `port/RouteKit/` links
both: `RouteDoc` (`.fvrte`, byte-compatible with Python's `json.dump`), `RoutePlanner`,
`RouteOverlay`, `RouteEditSession` (the one editor both shells use), `RoadGraphOverlay` (debug view,
coloured by profile weight, with a what-if), route maneuvers.
A second shipped rule file, `rules/route-weights-city.json` ("City Rules"), defines `bicycle`/`foot`/`car` for a US city grid; the Atlanta Pippin pack stages it and sets `routing.rules` to it (`stage_data.py` `ATLANTA_INI`); Kiawah keeps `route-weights.json`.

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
DK1–DK6f done (2026-10-05/09). DK6d/DK6e's mac proof (DNC/ENC/OSM drawing in Peregrine after
Generate Coverage vs PythonView's `--shot`) — Chris confirmed by hand 2026-10-09; needs
`geosym.data_dir`/`enc.data_dir`/`osm.style` set in `peregrine.ini`.
**DK6f** (2026-10-09, K13) — `Desk` holds one `DtedElevationSource` (`elevation_source()`),
opened from the catalog's first directory `dted` source, re-resolved on `CatalogChanged()`;
the StackHook attaches it to `fv.contour`/`fv.tamask` as added, else reports the overlay
"needs DTED elevation data; the catalog has none" — touched only under `StackEdit`.
`HostLabelFont()` (fv_desk_base_map.*) sets a default canvas font (Arial/DejaVuSans/
LiberationSans) in DeskHost's worker and FakeDesk — overlays had no font before this, so grid
labels were silently missing and the TA mask's peak label failed the whole overlay. New launch
option `--run <command-id>` (repeatable) in `ParseLaunchOptions`/Swift `Arguments`; PeregrineGtk
doesn't execute it yet. DK7's GTK window + generated menus were delivered as LX2/LX3; the
remaining DK7 item (same-workspace real-data proof) moves to Chris to run later on a Linux machine.
**Vector labels** (2026-10-09): `VectorBaseMap` turns OSM/ENC/DNC labels on when `HostLabelFont()`
finds a font; DeskHost's worker and FakeDesk give the base-map canvas that font. Checked by
Peregrine `--shot` on Charleston ENC (Coastal), Kiawah OSM and Cape Cod/Nantucket DNC (coa17c;
the DNC17 test data has no Charleston coverage).
**DK8a** (2026-10-09) — RouteKit's `RouteEditor` (an `app::OverlayEditor` over `RouteEditSession`:
Add/Delete Point, Cycle Leg Type, Follow Roads, by Bicycle, Straight Legs, Undo/Redo; no
Elevation profile) registers on `fv.route` via `RegisterRouteEditor`; `Desk` owns one
`RoutePlanner` (rebuilt in `SettingsLoaded()`) and binds it plus the editor to each route as it
joins the stack; new `Desk::Editing()`/`OverlayPointerDown/Drag/Up`/`OverlayKeyDown` gate events
to the active editor; `DeskHost::PointerDown` offers presses to the stack first, `KeyDown` is new;
Peregrine-mac's `MapCanvasView.keyDown` forwards NSEvent to it. Next: see DK8c below.
**DK8c route elevation profile** (2026-10-09) — the last route item of DK8. DeskKit
`fv_desk_profile.{h,cpp}` gives `RouteGeoPath`, `ElevationWindow`, `ProfileChart` (PythonView's
AN6 window in C++); `RouteEditor` gains an Elevation Profile tool; `Desk::Open/Close/UpdateRouteProfile`
re-samples on a hash of path/line-type/elevation-gen/settings; `DeskHost` exposes the layout via
`HostTick.profile_*`/`LayoutRouteProfile`. Peregrine-mac's `ProfileWindow.swift` draws it natively
(AppKit); checked live on Kiawah DTED2. New `[analysis] units`/`profile_samples` settings.
**Decided**: DeskKit computes the chart layout, each shell draws primitives natively — not
DeskKit rasterising onto `CpuCanvas` — so GTK gets the same chart from the same numbers. DK8
remaining: none — the Edit Point dialog, layers panel, search and Edit-menu undo move to FO (FO-D8).
**DK8b** (2026-10-09) — `RouteEditor::Tools()` gives every tool a symbolic icon (add_point,
delete_point, leg_type, follow_roads, follow_roads_bicycle, straight_legs, undo, redo); DeskKit's
toolbar shows the active editor's icon tools, so these now appear; Peregrine-mac `Toolbar.swift`
maps them to SF Symbols (no Elevation Profile tool yet). `RouteOverlay` is now `app::Properties`
(`[route]`: road_color/width/style, casing_color/width, beach_casing_color, show_labels,
show_status) via `OptionsPage` parts keyed by `group_id`; `Desk` registers `RoutingOptions`
(`routing.graph`/`routing.rules`/`routing.profile`) on fv.route's page, applying re-resolves the
Desk's `RoutePlanner`. **Decided**: a shell-level setting belonging on an overlay type's options
page is added as an extra `MapOptionsSource` keyed by that type id, not as an overlay property.
K17 open: Chris's confirmation of the shipped defaults (Info/Debug, 5×5 MB); Windows `ERR_report` not routed through it.
**DK8 add-mode/hover** (2026-10-09) — Chris, after trying DK8b: `RouteEditSession`'s add mode now
stays armed across clicks (off by "a"/the Add Point tool, Escape, or any other RouteEditor tool
button), a **deliberate departure from route.py**, which spends the mode on one click; `Delete` of
the selected waypoint now selects the one before it (the new first when it was first), so repeated
Deletes walk back along the route. Hover hints reach the status bar: `Desk` owns an
`app::PickSession`; new `Desk::OverlayHover`/`OverlayHoverExit`; `ShellTap::ShowHint` sets
`Desk::SetFeedback` (status, else tool tip); `DeskHost::Hover` calls it under a `try_lock` of the
stack mutex (not `StackEdit`, which would interrupt the draw) and skips the hint for a move that
lands while the worker is drawing. Peregrine-mac needed no change.
**DK8a follow-ups** (2026-10-09) — `RouteEditSession::OnKeyDown` now declines any ctrl/alt/meta
chord except Ctrl+Z/Ctrl+Shift+Z, so Cmd+R no longer follows roads; `Desk::BindOverlay` takes
`routing.profile` from settings only for a route joining the stack clean (no waypoints, no
profile, not dirty), so an opened route keeps its own; Peregrine-mac's AppDelegate now runs
`--run`'s `map.group.*` entries before placing the camera so `--center`'s scale settles on the
right ladder, rest of `--run` after (README updated).
**DK8d points editor** (2026-10-10) — DeskKit `fv_desk_point_editor.{h,cpp}`: `fv::desk::PointEditor`
(C++ form of points.py's) with Add Point (A), Delete Point (D), Labels, Undo (U), Redo (Shift+U) and icons
add_point/delete_point/labels/undo/redo; `Desk`'s constructor calls `RegisterPointEditor`, and
`Desk::BindOverlay` gives each `PointOverlay` the stack (`SetManager`) for drag capture and snapping.
Overlay ▸ Edit ▸ Points and the toolbar Points mode button exist. `PointEditSession::OnKeyDown` (fvkit, shared
with Pippin) declines ctrl/alt/meta chords except Ctrl+Z/Ctrl+Shift+Z, so Cmd+A/Cmd+D reach the shell.
Peregrine-mac maps "labels" to SF Symbol "textformat". **Kept deliberately**: a point's add mode is
still spent by one click (fvkit/Pippin), unlike the route's.
**Status bar and keys** (2026-10-09) — `map.pan.north/south/east/west` (Map ▸ Pan, arrow keys)
call `Desk::PanScreens(fx, fy)`, moving the camera half a screen so the edge point becomes centre;
map groups take an optional `shortcut` in map-groups.json (Raster Primary+R, OSM Primary+O);
`overlay.open` moved to Primary+Shift+O to clear Primary+O. `StatusBar.feedback` +
`Desk::SetFeedback()` add a left-hand status slot; `DeskHost::StatusFeedback()`/`StatusElevation()`
("1,234 ft" under the cursor, from a second `DtedElevationSource` Desk opens for the UI thread —
`Desk::ElevationFeetAt` — because the render worker's `elevation_source()` can't be read off-thread).
Peregrine-mac's status bar is feedback/scale+series/position+elevation in three thirds; menu items
don't bind a bare-key equivalent (it would steal the key from text fields), so
`MapCanvasView.keyDown` executes `map.pan.*` for the arrows instead. **Decided**: unmodified
shortcuts are not menu key equivalents in a native shell; the map widget handles them so text
fields keep their keys.

**Feature overlays (FO)** — plan `port/feature-overlay-plan.md`, approved 2026-10-10 (FO-D1–D9
decided by Chris: GeoPackage + GeoJSON, no SpatiaLite; QGIS opens with the right fields; desktop
edits attributes and geometry; Pippin unchanged). DK8 closes at DK8d (FO-D8).
**FO1 done 2026-10-10**: `fvkit/feature/{geometry,schema,layer,geojson}.h` (WKB, schema, memory layer, GeoJSON;
our writer adds an `fv_schema` foreign member, the reader infers types without it). 
**FO2 done 2026-10-10**: `fvkit/feature/geopackage.h` (GeoPackage read/write, R-tree, data-column metadata, one
transaction per file; 12 tests). Decisions in `archive/PORTING-ARCHIVE.md` (2026-10-10). Pending Chris: open
`build/feature-samples.gpkg` in QGIS (written by FeatureGpkgSample). 
**FO3 done 2026-10-10**: `fv.features` overlay (`overlay/feature_overlay.h`, order 940; .gpkg first table + .geojson via
additive `FileTypeDesc::extra_extensions`; single-symbol `feature/style.h`); additive `Overlay::opacity/SetOpacity/SupportsOpacity`;
DeskKit `fv_desk_layers.*` + `overlay.layers` (Primary+L), `ShowLayers()`, `--open <file>`; Peregrine-mac `LayersWindow.swift`.
Decisions in `archive/PORTING-ARCHIVE.md` (FO3). LX: PeregrineGtk needs a layers panel (`linux/*` follow-up). 
**FO4 done 2026-10-10**: fvkit `feature/value_text.h` (FormatFieldValue/ParseFieldValue/FieldActionUrl), `feature/points_layer.h`
(`PointsFeatureLayer`, `.fvpoints` adapter, FO-D6; edits via PointEditSession so undoable); `FeatureOverlay` `selected()/SetSelected` (yellow halo) + `app::SearchProvider`
(title = label field, else "name", else first text field). DeskKit `fv_desk_feature_form.*`, `fv_desk_attribute_table.*`; commands `overlay.attribute_table`, `overlay.edit_feature`;
PointEditor "Edit Point…" (icon edit_point); DeskShell `ShowFeatureForm/ShowAttributeTable`; DeskHost Form*/Table* API, HostTick `form_requested/table_requested/features_changed`.
Peregrine-mac `FeatureFormWindow.swift`, `AttributeTableWindow.swift`. Tests: fvkit +7 (feature_edit_test), DeskKit +11; ctest 2542/2542. Decisions in `archive/PORTING-ARCHIVE.md` (FO4).
LX: PeregrineGtk needs the form and table windows (`linux/*` follow-up). Next: FO5 (style).

**FO5 done 2026-10-10**: fvkit `feature/style.h` renderer model (single/categorised/graduated, `FeatureStyle::SymbolFor`, per-feature `overrides` keyed by id, `ClassifyCategories`/`ClassifyGraduated` equal-interval/quantile, colour-ramp or size); `feature/style_json.cpp` MapLibre-subset JSON (`WriteFeatureStyleJson`/`ReadFeatureStyleJson`). GeoPackage `ReadLayerStyle/WriteLayerStyle` (`layer_styles`, QGIS columns + `fv_style`); GeoJSON `fv_style` member. FeatureOverlay loads/saves the style with the file, per-feature symbols, `DrawFeatureSwatch`. DeskKit `fv_desk_layer_style.*` (LayerStyleModel), commands `overlay.style` "Layer Style…" and `overlay.style_feature` "Style This Feature…", DeskShell::ShowLayerStyle, DeskHost Style* API + HostTick `style_requested`. Peregrine-mac `LayerStyleWindow.swift`. Tests: fvkit +8 (feature_style_test), DeskKit +6 (layer_style_test 5, host_test 1); ctest 2556/2556. Decisions in `archive/PORTING-ARCHIVE.md` (FO5). LX: PeregrineGtk needs the style dialog (`linux/*` follow-up). Next: FO6 (geometry editing).

**FO5B/FO5C added 2026-10-10** (Chris): FO5's `style_json.cpp` grew a private MapLibre expression evaluator while `OsmStyleEngine` rejects expressions, and `FeatureOverlay` draws its own symbols outside `LookupTableStyleEngine`. FO5B: one shared expression evaluator used by OSM and feature styles; colour stops interpolate (O2 deviation dropped). FO5C: feature layers render through the engine (done, below). Next: FO6.

**FO5B done 2026-10-10**: fvkit `style/expression.{h,cpp}` (`StyleValue`, `StyleExpression` Parse/Evaluate/`is_constant`/`reads_zoom`, `StyleValuesEqual`, `ParseCssColor` moved from `fv_osm_style.cpp`); ops: literal get has id geometry-type zoom to-string/number/boolean/color string number boolean == != < <= > >= ! all any in match case coalesce step interpolate(linear, exponential) concat downcase upcase + - * / % min max rgb rgba; `format`, `cubic-bezier` and unknown ops are `kUnsupported`. `OsmStyleEngine` takes expression filters, colour/number paint and layout values, `text-field`/`icon-image`/`fill-pattern`; legacy `{base,stops}` compiles to interpolate, `type:"interval"` now honoured as step; data-driven `{property}` functions still rejected. Colour stops interpolate (O2 colour-step deviation dropped, superseded). `ReadFeatureStyleJson` evaluates through `StyleExpression` (private evaluator removed) and rejects expressions that read zoom. Tests: fvkit +10 (style_expression_test), Osm +6, 2 old rejection tests retired; ctest 2570/2570; OSM render goldens unchanged. Decisions in `archive/PORTING-ARCHIVE.md` (FO5B). LX: nothing GTK-facing. Next: FO6.

**FO5C done 2026-10-10**: engine moved to fvkit `style/maplibre_style.{h,cpp}` (`MapLibreStyleEngine`, `MapLibreLayerInfo/Type`); `Osm/fv_osm_style.h` is a forwarding header (`class OsmStyleEngine : public MapLibreStyleEngine`, a class because DeskKit forward-declares it); `fv_web_mercator.h` moved to `fvkit/geo/web_mercator.h`, forwarder kept. Engine gained circle-stroke, `fv-circle-shape` squares, `fv-line-dash` presets (builtin stamps as `builtin:<rrggbbaa>:<id>` symbols), `SetLabelFont`, `["id"]` (`VectorFeature::has_id/id`), `LabelStyle::anchor` centre. `VectorRenderer` fills every ring even-odd (holes cut; goldens re-pinned: GeoSym harbor 502 px, S-52 Charleston 173, OSM Atlanta 312, all interior rings). New `feature/vector_source.{h,cpp}` (`FeatureVectorSource`, source-layer `features`). `WriteFeatureStyleJson` is the render form: casing line-width is now the whole casing (FO5 files stored earlier today misread casing), labels split `fv-label`/`fv-label-shapes`, optional `highlight_id` halo layers. `FeatureOverlay` draws via engine + `VectorRenderer`; `DrawFeatureSwatch` uses the same path. Deviations: boolean labels "true"/"false" (was yes/no); point labels above the style's largest marker; markers exact size. Tests: fvkit +4; ctest 2574/2574. Decisions in `archive/PORTING-ARCHIVE.md` (FO5C). LX: PeregrineGtk unaffected; the Peregrine sync will see moved files. Next: FO6.

**FO6 done 2026-10-10**: fvkit `app/undo.{h,cpp}` (`app::UndoStack`: entries label/owner/undo/redo, depth 200, generation, `SetOnChange`, `RemoveOwner`; a failing entry drops its owner's history). `overlay/feature_edit.{h,cpp}` `FeatureEditSession` (`FeatureOverlay::edit()`): tools Select/AddPoint/DrawLine/DrawArea/AddHole, `VertexRef`, Add/Delete/MoveFeatures, Move/Insert/DeleteVertex, Add/RemoveHole, UpdateFeature, RecordEdit; snapping via `SnapCandidates` (never the dragged vertex); handles/sketch/box drawn by `DrawChrome`; undo entries hold before/after snapshots, alive token for closed documents. `FeatureOverlay` has multi-selection (`selection()`, `SetSelection`), EditTarget/SnapTo, `WriteFeatureStyleJson` takes a highlight-id vector; `IEditableFeatureLayer::InsertWithId` (default kUnsupported; Memory and GeoPackage override). DeskKit `fv_desk_feature_editor.{h,cpp}` (`RegisterFeatureEditor`), `Desk::undo()`, `edit.undo`/`edit.redo` (Primary+Z / +Shift+Z), new Edit menu, `OverlayPointerDown`/`DeskHost::PointerDown(x,y,shift)` overloads; form Apply records "Edit Attributes". Peregrine-mac: Shift on press, `undo:`/`redo:` on the map view, Edit menu merged in MainMenu.swift, toolbar icons. Tests: fvkit +15, DeskKit +4, 3 layout tests updated; ctest 2593/2593. Decisions in `archive/PORTING-ARCHIVE.md` (FO6). LX: PeregrineGtk needs the editor palette icons, Shift on press, and the Edit menu (`linux/*` follow-up). Next: FO7 (layer fields and New Layer).

**FO7 done 2026-10-10**: fvkit `feature/schema_change.{h,cpp}` (`SchemaChange` {schema, source[] old index or -1} in schema.h with `ValidateSchemaChange`; `ConvertFieldValue`: same type kept, int<->real, bool->1/0, date->datetime at midnight, datetime->date cut (lossy), real->int rounds (lossy), to text as the form shows it with exact numbers (%.15g/%.17g), text parsed as the form parses, enumeration accepts labels, values breaking choices/bounds -> null; `RemapFeature`; `AssessSchemaChange`/`SchemaChangeImpact` dropped/converted/missing_required + Describe). `IEditableFeatureLayer::ChangeSchema` (default kUnsupported; Memory and GeoPackage override; PointsFeatureLayer stays fixed). GeoPackage rebuilds the table inside the edit transaction (new table, rows copied with ids and stored geometry blobs, old dropped, renamed, triggers/indexes re-created from sqlite_master so R-tree and GDAL count triggers survive; an index on a deleted column is dropped with a warning; sqlite_sequence kept; field metadata rewritten, constraints only this table used deleted); a field may not take the id/geometry column name; Revert discards it. `RemapStyleFields` (renames followed; deleted/unsuitable field -> single renderer, labels off). `FeatureOverlay::ChangeSchema` drops that layer's undo history. `feature/templates.{h,cpp}` (blank, Historical monuments, Restaurants, Rivers), `CreateFeatureLayerFile`. DeskKit `fv_desk_layer_fields.{h,cpp}` (`LayerFieldsModel`, `NewLayerModel`, `ParseChoices/FormatChoices`, type/role/geometry titles); commands `overlay.fields` "Layer Fields…" and `overlay.new_layer` "New Feature Layer…" (Overlay menu); Desk Open/Apply/CloseLayerFields, `LayerFieldsLoss()`, Open/Create/CloseNewLayer (refuses replacing a file open in the stack); apply closes the form and style dialog, reloads the table; `AttributeTableModel::Reload` now clamps a sort column past the columns. DeskHost LayerField*/NewLayer* API, HostTick `fields_requested`/`new_layer_requested`. Peregrine-mac `LayerFieldsWindow.swift` (asks before losing values), `NewLayerWindow.swift`; AppDelegate runs `--open` before the non-group `--run` commands (README updated). Tests: fvkit +11 (feature_schema_change_test), DeskKit +8 (layer_fields_test 7, host_test 1); ctest 2609 pass / 3 skip. Not verified: the Peregrine-mac windows were built and their commands run without error from `--run`, not inspected visually. Decisions in `archive/PORTING-ARCHIVE.md` (FO7). LX: PeregrineGtk needs the Layer Fields and New Layer dialogs (`linux/*` follow-up). Next: FO8 (GDAL importer, optional).

**FO7B done 2026-10-10** (Chris, Peregrine UI tweaks): the feature editor palette now carries the layer dialogs. DeskKit `FeatureLayerTool` {label, icon, enabled, run} and `FeatureEditor`/`RegisterFeatureEditor` overloads taking a vector of them; Desk registers Attribute Table…, Edit Feature…, Layer Style…, Style This Feature…, Layer Fields… (icons attribute_table, edit_point, layer_style, style_feature, layer_fields), each running the `overlay.*` command of the same name; those five commands left the Overlay menu layout (still registered; host Execute and tests use them). Toolbar layout drops map.zoom_in / map.zoom_out / map.recenter (still in the Map menu). `MouseEvent::clicks` (2 = second press of a double-click). `FeatureEditSession`: a double-click while drawing a line/area/hole finishes the sketch (the first press placed the vertex); a double-click on a feature (Select tool, or Add Point tool) selects it alone and raises `TakeOpenRequest()`; `Desk::OverlayPointerDown(x,y,shift,clicks)` opens the feature form on that request; `DeskHost::PointerDown(x,y,shift,clicks)`. `DeskHost::PanPointerDown(x,y)`: press always pans the map regardless of editor; Peregrine-mac uses it while the space bar is held (open/closed hand cursor; Space no longer reaches overlays' KeyDown). `Desk::DocumentName()` / `DeskHost::DocumentName()`: file name of the current overlay if it is a file overlay, else the topmost file overlay; never-saved gives "unnamed.<type default extension>" (fallback "fvrte"); empty when no file overlay; the Peregrine-mac window title is that name, else "Peregrine — <catalog>". Tests: fvkit +2 (feature_geometry_edit_test double-click), DeskKit +3 (feature_editor_test), desk_test toolbar/reachability and host_test toolbar updated; ctest 2617/2617 pass. Not verified: the Peregrine-mac app built (xcodebuild succeeded) but was not inspected visually (another Peregrine instance held the computer-use binding). LX follow-up (`linux/*`): PeregrineGtk needs click count on press, space-bar pan via PanPointerDown, window title from DocumentName, and icons for the five new palette tools.

**FO7C done 2026-10-10** (Chris, hover status and tooltips): new capability `app::EditHint` (capabilities.h) with `std::string EditHintAt(PixelPoint)`, reached by `Overlay::AsEditHint()` (default null); it says what a press would do while the overlay is edited, empty when a press would pan. FeatureOverlay implements it via `FeatureEditSession::HintAt` ("Add point", "Start line"/"Start area"/"Start hole", "Add vertex (double-click or Return finishes)", "Close area"/"Close hole", Select tool "Move vertex", "Insert vertex", "Select <point|line|area> “<title>”", and "Move … (double-click to edit)" for a selected feature); PointOverlay via `PointEditSession::HintAt` ("Add point", "Select/Move point “name”"); RouteOverlay via `RouteEditSession::HintAt` ("Add waypoint", "Select/Move waypoint <label>"). FeatureOverlay now also implements `app::HitTest` (topmost feature via `FeatureEditSession::FeatureAt`; tool tip = `FeatureTitle`, status = "<layer name>: <title>"); VectorMapOverlay implements HitTest through an owned `app::VectorHitTest` over its renderer's pick index (set after each draw, cleared on SetSource/SetStyle); `FeatureEditSession::OnSketchStart` factored out of OnMouseDown. Base map: `VectorBaseMap::HintAt(PixelPoint)` / `BaseMapRenderer::HintAt` describe what the last vector render drew (topmost named hit among the first 8: tool tip = name, status "name (class)"; else status = class only; empty for raster). DeskHost guards the base render with `base_mu` and records `base_view`; `Hover` maps the pointer through the current and base viewports and calls `Desk::SetMapHint` (try_lock, skipped while the worker draws; re-read when a new frame lands). Limitation: OpenMapTiles keeps names on label features (transportation_name, water_name), so a road line or lake fill gives its class and the drawn label gives the name. Desk composes hover feedback by precedence: edited overlay's EditHint › overlay hit (PickSession) › base map; `Desk::HoverToolTip()` = overlay hit's tip else base map's; edit hint refreshed on hover, press and release; `DeskHost::HoverToolTip()`; HostTick.status_changed also fires when the tool tip changes; Peregrine-mac sets the map view's `toolTip` from it in refreshStatus. Behaviour change: with Add armed on a route, hovering a waypoint now reads "Add waypoint" (desk_test RouteEditorAddsWaypointsWhereClicked updated: Escape, then the waypoint hint). Tests: fvkit +2 (feature_geometry_edit_test hint, point_edit_test hint), RouteKit +1 (route_edit_test hint), DeskKit +2 (feature_editor_test status/tooltip precedence, vector_map_test base-map hover on Kiawah OSM); ctest 2622/2622. Peregrine-mac built (xcodebuild), not inspected visually. LX follow-up (`linux/*`): PeregrineGtk should show `DeskHost::HoverToolTip()` as the map widget's tooltip.

**FO7D done 2026-10-10** (Chris, the edited file is the top layer): already true before this change (invariant 5): `EditorManager::TakeFocus` raises the edited overlay with `MoveToTopOfWorking`; entering a mode adopts the topmost overlay of its type; making another overlay current moves the edit to it. New: `EditorManager` observes `OverlayOrderChanged` (`OnOrderChanged`): when a user reorder (layers panel MoveAbove/MoveBelow, Reorder) puts a different overlay at `OverlayManager::TopOfWorking()`, the edit moves to it if it is the editor's type (MakeCurrent + TakeFocus), else the mode exits WITHOUT `RestoreDefaultOrder` (`ExitMode(notify, restore_order=false)`) so the user's order stands. It also observes `OverlayAdded` (`OnOverlayAdded`): an overlay added mid-edit that lands above the edited one by display order is put back under it (MoveToTopOfWorking of the edited overlay). New `OverlayManager::TopOfWorking()` (public form of TopWorkingOverlay), `SetDimOthers(focus, percent)` / `dim_focus()` / `dim_percent()`, `SetOffscreenFont(path)`: while a focus is set and percent < 100, DrawAll draws every visible ordinary overlay except the focus into an offscreen CpuCanvas per consecutive run, scales alpha by percent and composites it; focus and top-most band draw at full strength; declutter disables it; Remove clears a removed focus. `EditorManager::SetDimOthers(percent)` (default 100 = off) pushes edited_/percent to the manager on TakeFocus and clears it on ReleaseFocus. Desk sets 50 (`kDimmedOverlayPercent`) and the host label font as the offscreen font. The base map is not dimmed. Tests: fvkit +5 (app_editor_test: raise same type moves the edit, raise other type closes and keeps order, add mid-edit stays under, dim focus follows the edit; overlay_test OverlayManagerDim pixel check); ctest 2627/2627. Peregrine-mac built (xcodebuild), not inspected visually. LX: nothing GTK-specific (all in fvkit/DeskKit).

**City Rules** (2026-10-11): Chris saw Atlanta bike routes using private ways (gate at Coronet Way NW, node 5829097078 on private service w616559239; private Coronet segment w169927714). Cause: the Kiawah bicycle profile's `private_penalty` 0.9 rewards private ways. `rules/route-weights-city.json`: `private_penalty` 25 for all three profiles (priced, not deleted, so a destination behind a gate stays reachable); bike 14 km/h, turn signage obeyed; cycleway 0.6, path 0.7, residential/living_street 0.8, unclassified 1.2, service/track/tertiary 1.5, secondary 2.5, primary 4, trunk 8 (last resort), footway/pedestrian 1.2 with class_kph 5/6 (walking the bike); motorway and steps excluded; golf cart paths excluded. Measured on 120 random 1.5-5 km intown Atlanta trips: mid-route private cut-throughs fell from 31/114 routes (10.3 km) under the Kiawah rules to 4/118 (0.65 km), failed routes 6 to 2; the worst remaining case is unchanged at private x1000, so it has no public alternative. Tests: `route_rules_test` +2 (CityRuleFileParses, CityRulesRideAroundAGatedStreet); `fv_routing_test` 155/155. CR2 built the same day: `.fvroad` v3 (one extra 32-bit attribute word per arc: BikeProvision bits 0-2, lanes bits 3-6, rest reserved; v1/v2 load with it zero), `RoadArc::bike`/`lanes`, flag `kArcSpeedPosted` (bit 13); rule keys `bike_lane_factor` and `speed_factor` (posted speed only, not on a track), set in route-weights-city.json only; atlanta.fvroad rebuilt as v3. Sweep of 150 Atlanta trips: share on lanes/tracks 4.65% -> 8.51%, on 45 mph+ roads 0.62% -> 0.32%. Tests +5; full ctest 2638/2638. Plan for the rest (CR5-CR8: surface/signals, tiles incl. bike_route, contraflow, restage) is in port/pippin-plan.md section "CR". CR3 built 2026-10-11 (gates and barriers on nodes): OSM readers deliver node tags on request (`OsmSink::WantNodeTags`/`NodeTags`; XML and PBF dense + plain); a barrier node on a road sets the existing per-mode access bits on the adjacent edges (no `.fvroad` format change); the gate family is passable unless tagged, bollard/block/chain/cycle_barrier bar motor vehicles; a barrier where one way ends and another passes through applies only to the ending way. Atlanta: 905 restricting barrier nodes (10 branch), 1,741 edges, graph 305,687 nodes / 801,490 arcs, testdata atlanta.fvroad rebuilt. 150-trip sweep CR2 vs CR3: bike through a private gate 1 -> 0, car through bollards/blocks 3 -> 0, distance/time within 0.06%; two car trips became unroutable because their end is inside a block-gated complex (Lloyd Hall Court SE), correct per data. Tests +7 (osm_reader +3, road_graph +3 incl. Atlanta gates, route_rules +1); full ctest 2645/2645. CR4 built 2026-10-11 (signed bike routes, graph and rules only; the tiles half moved to CR6): route=bicycle relations give each arc direction a BikeRoute network (local/regional/national) in bits 7-8 of the v3 attribute word (no version bump); forward/backward roles honoured, super-relations skipped; rule key `bike_route_factor`, City Rules 0.9 for all three; `fvgraph route` prints the route mix. Atlanta: 48 relations, 7,346/2,689/836 arcs, testdata atlanta.fvroad rebuilt. Sweep: signed share 23.85% -> 28.10%, secondary-or-busier 3.32% -> 3.17%, distance/time +1.3%. Tests +3 (road_graph +1, route_rules +2); full ctest 2648/2648. CR5 built 2026-10-11 (surface and traffic signals; no version bump): v3 attribute word bits 9-10 surface (`RoadSurface` unknown/paved/compacted/rough), bit 11 signal at the target, bits 12-13 unsignalled crossing of a secondary/primary/trunk at the target (`RoadCrossing`), bits 14-15 the same at the source (the twin's). Signals are `highway=traffic_signals`, `crossing=traffic_signals`, `crossing:signals=yes` on road nodes, not vertices (graph shape unchanged); an interior signal within `signal_reach_m` (30 m) controls its junction, farther midblock ones are ignored; a short arc (<30 m) between two signalled junctions (dual-carriageway median) gets no bit, one stop not two. Crossing: the busiest non-link secondary/primary/trunk at the junction, excluding the arc's own edge, exceeds the arc's own level. Rule keys `surface_factor` {paved, compacted, rough} (multiplies the class weight), `signal_seconds`, `crossing_seconds` {secondary, primary, trunk}; crossing seconds are charged half arriving, half leaving the busier road (straight over pays whole, turning pays half, riding along nothing). Delays are cost only, Materialize times omit them; distance profiles convert at fixed_kph. New `NodeDelayCost`/`ArrivalDelayCost`/`DepartureDelayCost` in fv_router.h; `ArcCost` unchanged, so the RouteKit overlay's relative cost is too. Build option `traffic_signals` (on). City Rules: bicycle surface 1.15/1.6, signal 20 s, crossing 15/45/90; foot crossing 10/30/60 and no signal wait (a wait sent walkers onto arterials, secondary-or-busier 4.10% -> 6.11%, since mapped sidewalks have a signalled crosswalk at each corner); car neither (signal_seconds 15 made routes +11% distance, rat-running). Kiawah names none of the keys, so unchanged. Decision: the bidirectional search's reverse frontier priced an arc by its own direction, not the one travelled, so per-direction bike provision (CR2) and bike route (CR4) were costed backwards on the reverse half; now priced by the twin arc (test BidirectionalMatchesDijkstraWhenDirectionsCostDifferently fails 332 times without it). Atlanta (atlanta.fvroad rebuilt): 7,157 signal nodes, 7,037 signalled junctions, 18,297 arcs end at one; unsignalled-crossing arcs 11,117 secondary / 4,798 primary / 1,474 trunk; surface 206,620 paved / 376 compacted / 3,896 rough; graph unchanged. Sweep, 150 trips, with vs without the CR5 keys: bicycle (144 routed, 113 changed) unsignalled crossings 235 -> 149, signals passed 1,239 -> 818, secondary-or-busier 3.37% -> 2.91%, distance +2.7%, time +3.1%; foot (142) crossings 203 -> 86, distance +0.05%, busy share unchanged 4.33%; car unchanged. `fvgraph` build/info print signal and surface counts, `route` prints signals passed and unsignalled crossings. Tests: road_graph +1, route_rules +4; fv_routing_test 175/175; full ctest 2653/2653.

**Linux app (LX)** — plan `port/linux-plan.md` (2026-10-07). `port/apps/PeregrineGtk` is built by
a Claude cloud session in the Peregrine repo, delivered as `linux/*` PRs with Ledger notes.
Back-port each merged PR into FVW (`format-patch | git am -3`) **before** the next sync, or the
sync deletes it. Peregrine's `CLAUDE.md` is dest-owned and holds that session's rules.
**LX1–LX3 back-ported 2026-10-09** (PR #3 `linux/ci-baseline` + an unreviewed branch): suite
green on Ubuntu 24.04/GCC 13.3 (2382 tests, 438 skip for no map data, 17 disabled, all 718
targets build); GTK4 skeleton (`application`/`map_window`/`map_canvas`) and generated
menus/toolbar/shortcuts over `DeskHost`; `DeskKit::ParseLaunchOptions` shared by both shells
(Swift still has its own parser — candidate to switch). Mac re-verified clean, 2440/2440, 3
skips, 17 disabled. Next: LX4 (dialogs, DK5) once the sample-testdata run is recorded for Linux.
**LX4 back-ported 2026-10-11** (branch `claude/sharp-bardeen-xfabnm`, not yet merged on Peregrine main): PeregrineGtk
`dialogs.{h,cpp}` (request handler runs a nested main loop; `Gtk::FileDialog` with request filters, `ChoiceDialog`,
`JobDialog` for catalog-build progress + Cancel), `data_sources_window` (Add/Remove/Generate Coverage),
`MapCanvas::SetHeld` (no host calls while a dialog waits), `StartLog` + Help ▸ Show Log; gtest `peregrine_gtk.dialogs`
(own Xvfb). Linux 2441 tests, 185 skipped. Not done: Map ▸ Options and Overlay ▸ Options on Linux.

### 2a. Open threads (each optional; pick on Chris's word)

| Item | What is left |
|---|---|
| **DK8a follow-ups** | PeregrineGtk needs the same key/pointer forwarding to `DeskHost` (the API is ready, GTK side isn't done) and must run `map.group.*` launch commands before placing the camera; also needs the three-part status bar (`StatusFeedback`/`StatusElevation`) and arrow-key panning in its map widget, as widget handling, not global shortcuts; and must call `DeskHost::Hover` for hints too. `StatusElevation` does a DTED lookup on every tick's status-string build; a hover hint is skipped for a move that lands while a frame is drawing. |
| **DK8d follow-ups** | No Edit Point... dialog yet (points.py has one: name, shape, colour swatches, size, icons, category, elevation, remarks, phone, url). Open design question for Chris: `fv_desk_options` writes to Settings, not a live object, so the dialog needs a new object-backed page kind or dedicated `DeskHost` accessors like the route profile's. PeregrineGtk needs the "labels" icon mapped; Peregrine-mac points editor not tried by hand (Chris). |
| **DK8b follow-ups** | PeregrineGtk needs the same icon names mapped for its toolbar; its options dialog shows the Route page with no change needed. Peregrine-mac's toolbar icons not yet checked by hand (Chris). |
| **DK8c follow-ups** | PeregrineGtk needs its own profile window over the same `DeskHost` accessors (cairo) and the `elevation_profile` icon; waypoint markers use the plan's leg ends (a straight leg that failed to route still counts as a leg); no "Segments" box (PythonView had one) — sample count is the `analysis.profile_samples` setting only. |
| **DK6b follow-ups** | No `os_log` sink and no in-memory ring sink yet (§2e lists both); Pippin → `os_log` and pyfvw → `logging` still to do. |
| **D1 route-data audit** | Unstarted tool in `port/tools/`: Monte Carlo reachability sweep, missing-crossing candidates, islands, snap traps; output coordinates for upstream OSM fixes. Oracle: "anywhere on Kiawah by bike, walking ≤25 yd". Worked example: archive snapshot §2a D1. |
| **ViewKit frame cache** | Last-frame only; Pippin's guard band/underlay (`PPBaseCoverage.h`) stays in Pippin until Pippin moves onto ViewKit (DK11 optional track). |
| **ViewKit input** | Editor input routing, picking and hover hints (desktop-plan §2a last bullet) deferred to DK8. |
| **ViewKit product stability** | `MapView` keeps its current product while panning; it re-chooses only on a ladder step, a pinch settle or a map-group change. |
| **DeskKit command registry gaps** | `file.export_image`, `map.goto` have menu layout slots but no registered command yet — each lands with its own session. |
| **DK3 follow-ups** | fvkit's built-ins (grid, points, contour, tamask, scalebar, movingmap) still register in code, not via manifest (deliberate, avoids a drifting JSON copy); no bundled `overlays.json` yet; a static-lib builtin registrar needs an explicit referencing object (dead-stripping). |
| **DK5 follow-ups** | A data source's scan can't be interrupted (cancel waits for it); readers wait up to 10s on the busy timeout during a big scan's commit; progress sheet/Build panel not driven live (out-of-process open panel) — Chris to try by hand. |
| **LX Options dialogs** | Map ▸ Options and Overlay ▸ Options do nothing on Linux yet; the GTK shell needs its own options windows. |
| **LX1–LX3 follow-ups** | DejaVu graticule golden is pinned to `fonts-dejavu-core` 2.37, will need re-pinning on a newer Ubuntu; the cloud session's FetchContent of googletest/pybind11/protozero/vtzero from GitHub `/archive/` is blocked by its proxy (worked around with `FETCHCONTENT_SOURCE_DIR_*`) — a CI runner behind a proxy needs the same; Linux run of the sample testdata still to be recorded here. |
| **Generate Coverage all-or-nothing** | No incremental rescan; a cancelled generation leaves a partial catalog. |
| **DK5b follow-ups** | `Execute`'s `StackEdit` lock is held across a modal request handler, so frames pause during a modal dialog; DK8 editor input routing must take `StackEdit` when a pointer event reaches an editor; overlays have no cancel inside their own `OnDraw` yet. |
| **DK6 follow-ups** | Raster pixel pitch is still a `MapOptionsSource` to add; elevation breaks are a text field (a custom breakpoint editor is a later custom page); a newer options request replaces an open dialog's unapplied edits. |
| **DK6d follow-ups** | a vector frame is not interruptible (`VectorRenderer` has no cancel hook); `[vector]` scene_margin/simplify/label_reference_scale and families_* not read by DeskKit; `DeskHost` logs a warning on every failed render, so an unset geosym dir logs once per frame; overlay-sheet (`osm.overlay_style`) not used. |
| **DK6f follow-ups** | `TAMaskOverlay` propagates a peak-label failure as an overlay failure while the grid overlay swallows label failures, inconsistent — now masked by `HostLabelFont`; PeregrineGtk should execute `LaunchOptions::run` (it already parses it). |
| **Peregrine core deployment target** | `libperegrine_core.a` builds for the host macOS (27) while the app targets 14.0 (ld warns) — set `CMAKE_OSX_DEPLOYMENT_TARGET=14.0` before DK10. |
| **O5 routing** | Via-way restrictions (recognised, not applied); steps and elevation have no cost; ferry timetables unmodelled; "walk the bike" mixed-mode undecided. |
| **City Rules follow-ups** | Lanes, surface, signals, bike routes priced; contraflow CR7; tiles CR6 built 2026-10-11 (`port/tools/bike_tags_check.py`: 210,727 highway ways, 0 tile/graph disagreements, tile side transcribed from the lua; Atlanta Streets light+dark tiers on maxspeed/cycleway, new `bike_route` layer; `atlanta.mbtiles` rebuilt, 424 tiles; ctest 2653 green). **BLOCKER (Chris): `process-peregrine.lua` + `config-peregrine.json` are untracked in ~/Documents/Source/tilemaker, so the CR6 tile change is not in git; the mbtiles cannot be rebuilt from FVW.** Check caveats: tile `cycleway` is strongest across sides while the graph reads per side; `opposite_lane` reads lane in tiles, none in graph (1 way in Atlanta). Open (Chris): whether foot/car want a signal wait at all, needs a turn-aware model (charge only when crossing); cycle_barrier still has no delay (could use the arrival delay). Atlanta's national/regional routes and "Shared Roadway" lcn routes run mostly on secondary/primary roads, so the bike_route discount is kept small. `motorcar=*` is not read on ways or barriers (e.g. Woodsmill Dr SE gate 2754129196). Atlanta pack not restaged or tried on a phone (Chris). |
| **MM5b** | Viterbi map-matching to remove along-track error (seam ready in `RoadCandidate`). |
| **Overlays** | Tier 3 only: `PrintToolOverlay`, `nitf`, `Cov_ovl` — heavy shell coupling, last or never. **Dropped, do not re-queue**: `shp`, `localpnt`, `ar_edit`, `TacticalModel`, `SkyViewOverlay`. No shell turns on `fv.scalebar`; nothing feeds `fv.tamask`'s `SetAltitude` from the moving map. |
| **Data** | Rebuild `kiawah.fvroad` once Chris's `bicycle=designated` OSM edit on the Ocean Course path propagates. |
| **R3d perf** | Only on a fresh profile; last one pointed at `LookupTableStyleEngine` styling. |

### 2b. Rendering and product gaps

- `ICanvas` lacks pattern brush, clip region and layer alpha — one off-screen-layer session fixes
  all three (area patterns bleed past rings; outer ring only; top-most opacity unapplied).
- No label collision/de-duplication in `VectorRenderer` (ENC repeats names per overlapping cell/band). Now visible in Peregrine: ENC Coastal repeats "Cooper River"/"Ashley River" along the river.
- Chart symbols ignore device DPI (would move GeoSym goldens); ENC text size read as pixels; S-52
  `SPACE`/`DISPLAY` skipped; halos have no blur and only OSM sets one; OSM colour stops step (superseded by FO5B: they now interpolate).
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
- [ ] FO2: open `build/feature-samples.gpkg` in QGIS — field aliases, descriptions and the pizza `style` pick-list should show.
- [ ] FO3: open the rivers sample in Peregrine (`--open port/fvkit/test/data/charleston-rivers.geojson`, Overlay ▸ Layers…) and try the panel — hide/fade/reorder (not verified live; screen was locked).
- [ ] FO4: in Peregrine, open the pizza/rivers sample, Overlay ▸ Attribute Table…, double-click a row, edit and Apply; and Edit Point… on a `.fvpoints` point (not verified live).
- [ ] FO5: in Peregrine, open the rivers sample, Overlay ▸ Layer Style…, Graduated by Average flow, Classify, Apply, save as .gpkg and reopen; and Style This Feature… on a selected feature (not verified live).

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
- A `.fvroad` field added after v2 goes in the v3 per-arc attribute word, read as zero from older files; the map tiles carry the same inputs the graph builder reads, and the style tiers on them (pippin-plan CR).
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
