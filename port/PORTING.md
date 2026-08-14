# FalconView Cross-Platform Port — Working Ledger

**Read this first in every session. Do not re-explore the repo.**
This file is **open work only**. Finished modules, the session-by-session narrative and every
dated decision live in **`port/PORTING-ARCHIVE.md`** — go there only when a line below names a
specific entry. Full strategy:
`/Users/chrisbailey/.claude/plans/this-project-is-extreamly-enumerated-marshmallow.md`.
Vector/symbology design: `port/vpf-geosym-plan.md` (§5 = the cross-product middle layer, §7 = ENC).
App-framework design (overlay types/editors/pick/session — **A1–A6 built; the plan is DONE**):
`port/fvkit-app-plan.md`.
Overlay drawing design (geographic lines, symbol libraries, highlight — **G1 built 2026-08-13,
G2 and G3 built 2026-08-14; G4–G5 not started**): `port/fvkit-draw-plan.md`.

```sh
cmake -B build && cmake --build build -j && ctest --test-dir build   # 1099 as of 2026-08-14 (points schema 2: +9 C++).
# This is the number ctest RUNS. `ctest -N` says 1116 because it also lists the 17 disabled
# GeoTrans tests — do not update this line from -N, the two counts are 17 apart forever.
# THIS LINE DRIFTS: T2 added 7 tests, wrote 836 in its own archive row and left this line at
# O5e's 829. Trust the archive row of the LAST session, not this line, if the two disagree.
# FIVE Osm tests fail on the ocean-merged us-south.mbtiles — see §2d, it is the data.
```

Other docs: `port/fvkit-contracts.md` (D1–D6 — ownership/geo/Status/pixel/naming/adapters),
`port/bindings/pyfvw/README.md` (Python user guide), `port/bindings/pyfvw/ICD-MAPPING.md`,
`port/peregrine.ini.sample` (every settings key with its measured effect),
`port/families/{dnc,enc,osm}-families.json` (the data-family starters, M1).

---

## 1. What exists today (so you don't go looking)

**Geo/math**: `port/{geoid,geo3,geo_tool,geotrans,MapScaleUtil,MapSeriesStringConverter}` — all tested.
GEOTRANS 3.3 is **frozen** (pinned bit-faithful results).

**Raster products** — each is an `IRasterSource` + enumerator, self-registered in the format registry
(7 builtin: `geotiff`, `cadrg`, `tiros`, `dted`, `dted-shaded`, `gpkg`, plus the vector products):
`port/{ImageLib,ImageLibCore,CadrgDecoder,CadrgMapServer,GeoTIFFMapServer,TirosMapServer,DtedMapServer,DtedShadedRenderer}`.

**FvKit** (`port/include/fvkit/`, impl `port/fvkit/`): `geo.h`, `raster.h`, `proj.h` (equal-arc +
`SetPhysicalScale`), `engine.h` (MapEngine), `catalog/` (SQLite+R-tree), `canvas/` (ICanvas + CpuCanvas),
`overlay/` (SPI + manager + grid + `KeyEvent`), `store/tile_pack.h` (GeoPackage), `settings.h`
(`fv::Settings`, INI, registry replacement), and the **vector seam**: `vector/vector.h` (IVectorSource,
VectorFeature, FeatureRef/Describe), `style.h` (IStyleEngine, VectorSymbol, path/area pattern styles),
`rules.h` (predicate AST, ScaleBand, ViewingGroup), `families.h` (named groups of
features over rule selectors, JSON — `port/families/{dnc,enc,osm}-families.json`),
`mariner.h` (`MarinerSettings`, shared by DNC and ENC — see the M1 row), `lookup_engine.h` (`LookupTableStyleEngine` — the
shared engine; GeoSym and S-52 are *loaders* over it), `renderer.h` (VectorRenderer + the three
placers: `PlaceAlongPath` / `PlaceOverArea` / `PlaceTextAlongPath`), `scene.h` (retained VectorScene),
`pick.h` (PickIndex — hit-tests the emitted ink). Labels: `LabelStyle` carries placement
(point or along-path), spacing, max angle, offset, a size in pixels OR ground metres, and
(E8) `halign`/`valign` for a point label's box — `kLeft`/`kBaseline` are the canvas's own
behaviour and cost no measurement, anything else costs one `GetTextExtent`;
`ICanvas::DrawRotatedTextString` is what draws them (T1).

**Geographic contours** (G1, `port/include/fvkit/geo/contour.h` + `port/fvkit/geo/contour.cpp`) —
`IGeoContour` (a pull iterator: `MoveFirst`/`NextPoint`) with `SimpleGeoLine`,
`GreatCircleContour`, `RhumbLineContour`, `GeoCircleContour`, `GeoEllipseContour`, plus the two
new ones `GeoArcContour` and `PolylineContour`; `MakeGeoLine(proj, a, b, LineKind, clip)` is the
factory and `BuildGeoPath` projects any contour into surface sub-paths. Ported from
`fvw_core/FvMappingGraphics/GeographicContourIterator.cpp`, algorithms intact. **The property
that matters: it CLIPS IN GEOGRAPHIC SPACE BEFORE DENSIFYING**, so an intercontinental arc on a
harbour map costs a search, not a walk — do not "simplify" that away. The step size comes from
the projection's dpp (~20-px chords, `/5` above ±70° lat), so vertex count tracks the SCREEN.
`SurfacePoint` moved here from `fvkit/vector/renderer.h` into `fvkit/geo.h` — same type, same
namespace. `GeoDraw` is still G3, but **G1 has a consumer since 2026-08-13**: it is bound as
`pyfvw.geo.{LineKind, line_path, line_points, polyline_path, circle_path, ellipse_path, arc_path}`
and `RouteOverlay` draws its legs through it, great circle by default ("g" cycles
great-circle/rhumb/straight). **The pull iterator is deliberately NOT bound** — a per-point call
across the binding costs more than the geodesy it invokes, so every contour is exposed as one
`*_path` function that builds and projects in a single crossing; `line_points` is the exception and
exists for asserting geography, not for drawing.

**Symbol libraries** (G2, `port/include/fvkit/symbol/` + `port/fvkit/symbol/`) — `ISymbolLibrary`
(`library.h`) is exactly the three methods `IStyleEngine` already declared, so
**`IStyleEngine : public ISymbolLibrary` made every style engine a symbol library** and the symbol
TYPES (`SymbolPrimitive`/`VectorSymbol`/`SymbolPixmap`) moved here out of `vector/style.h`, which
now includes this header — no consumer changed. Four implementations: `PngSymbolLibrary`
(`png_library.h` — loose `<id>.png` files with an optional pivot sidecar and `@2x` twins, OR a
sprite sheet plus MapLibre `sprite.json`; lazy, so a directory of 400 icons costs 400 filenames),
`CgmSymbolLibrary` (`port/GeoSymServer/fv_cgm_library.h` — GeoSym's ~1500 `.cgm` reachable without
a style engine; it stays in GeoSymServer because fvkit never links GeoSym), `BuiltinSymbolLibrary`
(`builtin.h` — 13 `VectorSymbol` literals: PointOverlay's six shapes, five line decorations
authored for `PlaceAlongPath` with **+x ALONG the line and +y to its LEFT**, a north arrow and an
open-centred crosshair) and `CompositeSymbolLibrary` (ordered; `Symbol` and `Pixmap` resolve
INDEPENDENTLY so a pixmap-only member cannot shadow a later display list — and its unit is ONE
number for the whole composite, inherited from the first member, which is a stated limitation
because `himetric_per_symbol_pixel()` is asked without an id).
**The drawing came OUT of the renderer** (`fvkit/vector/symbol_draw.h`): `DrawSymbolAt`,
`DrawPixmapSymbolAt`, `ResolveSymbol`, `DrawResolvedSymbol` and `InkBox` were in
`renderer.cpp`'s anonymous namespace, and `ToVectorSymbol` was file-local in `fv_geosym_style.cpp`
(now `port/GeoSymServer/fv_cgm_to_symbol.h`). Verbatim moves — **every pinned golden is
byte-identical, which was the session's acceptance test**. The one addition is
`SymbolPixmap::pixel_ratio` (tile pixels per nominal pixel: a sheet's `pixelRatio`, a file's
`@2x`), which `DrawResolvedSymbol` divides the scale by so a 2x tile comes out the same SIZE as
its 1x twin; **it defaults to 1.0 and dividing by 1.0 is the identity**, which is why the S-52
goldens did not see it. Two rules worth not re-deriving: **a pivot lives in TILE pixels**, so a
1x sidecar applied to a 2x tile puts a pin's tip halfway up the pin (`<id>@2x.json` wins, a plain
sidecar is scaled by the ratio); and **a library re-`Open` REPLACES**, so both `Open*` reset and
`OpenSheet` decodes into a local buffer first — stale entries indexing a different sheet is the
worse failure.

**Overlay drawing** (G3, `port/include/fvkit/canvas/geo_draw.h` + `port/fvkit/canvas/geo_draw.cpp`) —
`GeoDraw(proj, canvas, symbols)` is **the surface an overlay calls**: `DrawGeoLine`/`DrawGeoPolyline`/
`DrawGeoCircle`/`DrawGeoEllipse`/`DrawGeoArc`/`DrawContour`/`DrawSurfacePath`, `DrawSymbol`(`AtPixel`),
`DrawLabel`(`AtPixel`/`AlongPath`). It composes G1's contours, G2's libraries and the vector seam's
placers and needs **no new `ICanvas` op**. Styling is the seam's OWN structs (R1): `GeoLineStyle` is
`{casing, stroke, pattern}` and nothing else — **the casing is the addition and it is the "halo" a
line wants**, drawn first, wider, and **following the PATTERN when there is one** (a solid bar under
a dashed line reads as a solid line, so a dashed line gets a dashed casing; `AddCasing` measures off
whichever of stroke/pattern is live). `PresetGeoLine` turns the pattern ON and the plain stroke OFF —
they would otherwise draw the solid line the pattern replaces. **The line presets are the plan's
finding as data**: ten names (`solid dash long-dash dot dash-dot railroad arrow tick notch feba`)
over `BuiltinSymbolLibrary`, which is how `LineSegmentRenderer.cpp`'s 913 lines and 15 classes
collapse — **solid deliberately returns an INVALID pattern** (a one-run cycle would cost a placer
walk to draw what `DrawLines` draws) and so does an unknown name, which then falls back to a plain
line rather than to nothing. `symbol_dpi_scale` defaults to **1.0** and is the ledger's stated way
out of the symbol-DPI defect for work that has no goldens. Picking is **OFF by default** here (the
opposite of `VectorRenderer`, whose caller always wants identify) — an overlay with its own analytic
hit test should not pay for an index nobody reads; when on, `SetFeature(id)` names what the ink
belongs to and the id comes back out of `HitTest`. Bound as `pyfvw.draw` + `pyfvw.symbol`
(`pyfvw_draw.cpp`) — `GeoLineStyle` is bound because a caller holds one; `LabelStyle` and
`PointSymbolStyle` are NOT, because every field of them is a natural keyword.
**Two extractions came with it**, both mechanical, both pinned by the goldens staying byte-identical:
`fvkit/vector/text_draw.h` (`HaloOffsets`/`HaloPixels`/`LabelPixelSize`/`GlyphAdvances`, out of
`renderer.cpp`'s anonymous namespace — copying them would have forked the halo, which is exactly the
thing that drifts into "the overlay's text looks slightly different from the chart's"), and
`kBuiltinSymbolNominalPx` (9.0) out of `builtin.cpp` so a caller can size a marker against it.
**The consumers are both real**: `fv::PointOverlay` draws its six shapes as builtin symbols (one
`BuiltinSymbolLibrary` per colour, cached — colour is a library setting, and the selection edge is
the same symbol one size up stamped underneath, which retires the pen/thicker-pen/rectangle triple
it used to carry) and its labels through `DrawLabel` with a halo; `port/apps/route.py` draws
**dashed blue over a white casing for a bicycle route, solid blue over a white casing for the
default (car) route, and the overlay's own red straight legs when nothing has been calculated** —
the mode is now visible in the LINE and not only in one line of status text.

**App layer** (`fv::app`, `port/include/fvkit/app/` + `port/fvkit/app/`) — the overlay/application
framework of `port/fvkit-app-plan.md`, **A1–A4 so far**: `type_registry.h` (`TypeId` = a STRING id,
`OverlayTypeDesc` with the factory as a `std::function`, and `std::optional<FileTypeDesc>` — that
optional IS the static-vs-file distinction; `RegisterBuiltinOverlayTypes` registers the grid as the
first static type), `capabilities.h` (`Persistence`, `HitTest`, `SnapTo`, `ContextMenu`,
`RoutingOverrides`, `EditTarget`), plus the early slices of `shell.h` (`CursorId`/`HintText`/`MenuNode`
— `AppShell` is A3) and `editor.h` (`OverlayEditor`/`EditorUiConstraints` — `EditorManager` is A4).
**Capabilities are found by ACCESSOR, never `dynamic_cast`**: `fv::Overlay` grew six `As*()` returning
nullptr by default, over forward-declared types, so L4 keeps no app-layer dependency. `Overlay` also
grew `type_id()`, stamped at creation, empty for an overlay made outside the app layer.
The layer is `fv::app` and D5's "no nested namespace" does not apply to it — see the A1 archive row.
**A2 is the STACK, and it is `fv::OverlayManager` grown in place** (`fvkit/overlay/manager.h` IS the
plan's `stack.h`): `fv::StackObserver` (added/removed/order/current/dirty/file-spec), a current
overlay, `MoveAbove`/`MoveBelow`/`MoveToBottom`/`Reorder` (a total permutation, rejected whole if it
is not one), `FirstOfType`/`OfType`/`FindByFileSpec`, declutter, mouse capture, and the three-phase
route (direct-routing pre-pass → declutter → top-down). `SetTypeRegistry` is optional and **with no
registry every A2 addition is inert**: `Add` is the pre-A2 append, `DrawAll` is one pass (R7).
manager.h still includes nothing from `fvkit/app` — the display order, the top-most flag and the
Persistence hook are reached in manager.cpp only.
**A3 is the SHELL SEAM and the FLOWS**: `shell.h` grew `FlowResult` (kDone/kCanceled/kFailed — a
cancel is the USER's answer and propagates; a failure is reported through `AppShell::ReportError`
and left on `session.last_error()`) and `AppShell` itself, the complete inventory of UI the app
layer needs — five decisions (`AskSave`, `ChooseFilesToOpen`, `ChooseSaveSpec`, `ChooseFromList`,
`ConfirmRevert`) and six presentation calls. `session.h`/`session.cpp` is `OverlaySession`:
`ToggleStatic`, `NewFileOverlay`, `OpenFileOverlays`/`OpenFile` (dedup on **(TypeId, file spec)**
through A2's `FindByFileSpec`, extension dispatch when no type is named, revert offered on a dirty
re-open), `Save`/`SaveAs`/`SaveAll`, `Close`/`CloseAll`/`Exit`, plus `SaveConfiguration`/
`RestoreConfiguration`/`RestoreStartupOverlays` over `fv::Settings`. Rule R1 pays for itself
immediately: `port/fvkit/app/test/fake_shell.h` is a scripted `AppShell`, so all 56 tests are
plain unit tests with no dialog and no message pump. See the A3 archive row for the decisions
worth not re-deriving.
**A4 is the MODE DANCE**: `editor.h` grew `EditorManager` (`SetMode`/`ToggleEditor`/
`CurrentMode`/`CurrentEditor`/`edited`/`ActiveConstraints`/`AutoEnterFor`) and the plan's four
invariants. The dance runs in **two directions and only one of them is a call**: `SetMode` makes
the current overlay match the mode (adopting the topmost of the type, or creating one through
A3's `NewFileOverlay`/`ToggleStatic` when the editor auto-enters — a cancel or failure there
drops the mode back to none), while "the mode follows the current overlay" and "closing the
edited overlay falls to the next OF THAT TYPE" are **observed** through a private `StackObserver`,
so they hold for a `MakeCurrent` or a `Remove` from anywhere. One `Transition` flag both
suppresses the notifications the manager causes itself and makes a reentrant `SetMode` (a shell
answering `OnEditorChanged` by switching again) fail loudly. The **editor instance is per TYPE and
cached**, so tool state survives leaving and re-entering; `Activate`/`Deactivate` bracket its use,
and an editor may not refuse to be left. **The mutual dependency with `OverlaySession` is wired
after construction on both sides** (`SetSession` / `SetEditorManager`) and both are optional: with
no session a mode with nothing to edit simply WAITS, with no EditorManager the A3 flows are
unchanged. Two A3 lines changed for it — `Close` releases edit focus only when no EditorManager is
wired (otherwise the overlay hears it twice), and `NewFileOverlay` auto-enters the editor on
CREATE, never on open. See the A4 archive row.
**A5 is PICKING, and it is an aggregation rather than FalconView's first-hit-wins veto**:
`pick.h` (`PickSession` over the `HitTest` capability — `UpdateHover`, `ResolveClick` with the
three `PickPolicy` values, `HitTestPoint` as the ranked list both share, `SnapToPoint`,
`BuildContextMenu`/`ShowContextMenu`) plus `vector_hit_test.h` (`VectorHitTest`, the adapter
over L4's `PickIndex`). **Who is asked has ONE implementation and it is the DRAW order
reversed**: A2's rule moved out of `DrawAll` into `OverlayManager::DrawOrder()` (visible,
bottom-up, top-most band last, declutter honoured) and `DrawAll` is written over it, so a
top-most HUD picks over the chart exactly as it draws over it. **The hit id is a HANDLE, not a
packing** — a `FeatureRef` is 4×int32 and `HitItem::feature` is one uint64_t, so the plan's
"it fits" is wrong; `VectorHitTest` mints a stable per-adapter handle and `RefFor()` translates
back. **Hover notifies only on a CHANGE** (per mouse move otherwise), `kAskWhenAmbiguous`
degrades to `kTopMost` on a hover because a hover cannot ask, and snap-to's "all overlays"
means all that ANSWER, not all that exist. The verbs are `HitTestPoint`/`SnapToPoint`: the
bare names are the capability classes in the same namespace and would hide them. Picking never
consults capture or `RoutingOverrides` — routing runs first, and the shell owns that order.
See the A5 archive row.
**A6 is the ADOPTION, and it is the plan's acceptance test**: `pyfvw.app` binds the whole layer
(`pyfvw_app.cpp`), `fv::PointOverlay` is the first C++ FILE overlay, and PythonView IS an
`AppShell`. Three rules carry it. **A capability is a method you DEFINED**: a Python overlay
cannot return a C++ interface pointer, so the overlay trampoline inherits every capability and
answers each accessor from what the subclass defines (`file_open` ⇒ a document with `.dirty`/
`.file_spec` and the flows, `hit_test_point` ⇒ pickable, `menu_items` ⇒ a context-menu section,
`snap_to_point`, `wants_direct_routing`, `enter_edit_focus`…), cached per instance. **A Python
overlay made by a FACTORY needs an aliasing `shared_ptr`** (`OverlayFromPython` in
`pyfvw_common.h`) — `keep_alive` cannot help a factory called from inside a flow, and without it
the overlay survives, draws, and silently answers no picks. **An editor is a PROXY and is
duck-typed** — `unique_ptr` ownership cannot cross out of Python, so `PyEditorProxy` forwards
`activate`/`deactivate`/`tools`/… by name and is UNWRAPPED wherever the API hands an editor back,
so Python always sees the object it created. `fv::PointOverlay`
(`fvkit/overlay/point_overlay.h`) reads a `.fvpoints` **SQLite** document — a real schema
somebody else can write, which is what makes evolving the dataset INSERTs rather than a parser —
draws six geometric shapes, and reports the ROW's own id as `HitItem::feature` (identity is in
the file, unlike A5's minted vector handles). `WriteSampleFile` plants two PAIRS of points ~3 px
apart at harbour scale and a test pins that they are, because `kAskWhenAmbiguous` has nothing to
work on otherwise. `Overlay` grew `SetName` (a file overlay renames itself to its document).
See the A6 archive row. **SCHEMA 2 (2026-08-14) put the ARTWORK IN THE DOCUMENT**: a `symbols`
table of PNG blobs and a `points.symbol_id` into it, so a `.fvpoints` file is self-contained —
it opens with its symbology on a machine that has never seen the icon set, which a path into
somebody's symbol directory would not. **The table is separate because many points share one
symbol** (Chris's ask): three forts name one `castle` row, so the file carries the artwork once,
decodes it once, and caches one tile. A point draws as a BADGE — its own shape in its own colour
with the tile centred on top (`kIconFractionOfBadge`) — because icon sets are black-on-
transparent and a bare tile would be invisible over a dark chart and would throw the `color`
column away; alpha 0 is how a document asks for the bare icon, and selection stays the edge one
size up. `EmbeddedSymbolLibrary` (private to `point_overlay.cpp`) is the third form of
`ISymbolLibrary` after G2's directory and sheet: a blob already in memory, decoded lazily and at
most once, a failed decode cached as an empty tile. **A schema-1 document still opens** (two
prepares, v2 then v1) and is saved forward (`ALTER TABLE ... ADD COLUMN`, since
`CREATE TABLE IF NOT EXISTS` leaves an existing table alone). The sample document is now 26
points wearing 23 maki icons from `testdata/GeoSymbol/makiPng` — `WriteSampleFile(spec,
symbol_dir)`, `points.symbol_dir` in the ini, empty = the shapes-only document exactly as
before, since the icons are test data and not in the repository.

**Vector products, all three on that one seam**:
- DNC/VPF — `port/VpfMapServer/` (reader, vector source incl. areas, VDT identify) +
  `port/GeoSymServer/` (rule tables, CGM symbols, `GeoSymStyleEngine`).
- ENC/S-57 — `port/Enc/` (ISO 8211, S57Cell, Appendix A catalogue, S-52 PresLib, `S52StyleEngine`,
  raster symbol sheet, enumerator/format registration). **Text is its own band** (E8):
  `kS52PrioTextBase + the object's priority`, above all geometry, because S-52 gives the
  priority to the LOOKUP and the library authors text-bearing rows at every band there is.
- OSM — `port/Osm/` (MBTiles + MVT + `OsmVectorSource` + `OsmStyleEngine`, a MapLibre
  style-JSON loader over `LookupTableStyleEngine`; reference style
  `port/Osm/styles/peregrine-osm.json`; `OsmFrameEnumerator` + `RegisterOsmFormat`).

**Routing** (`port/Routing/`, O4): a routable road graph built OFFLINE from a **raw** `.osm`/`.osm.pbf`
extract — never from the MVT pyramid, which is simplified and tile-clipped and has no node identity.
`fv_osm_reader.h` (expat XML + protozero/zlib PBF behind one `OsmSink`, nodes/ways/**relations**),
`fv_road_graph.h` (noded graph, `.fvroad` file **v2**, grid nearest-node index, turn restrictions),
`fv_router.h` (bidirectional Dijkstra, with the unidirectional one kept as the tests' oracle).
CLI: `fvgraph build|info|route`.
Three profiles (O4b) — driving on the posted clock, walking and cycling at flat speeds — each gated by
per-mode access bits carried ON the arc, so one general graph answers all three and `--cycle-only` on
the build is only a size optimisation, not the filter.
**Ordered stops (O5d)**: `Router::RouteVia(stops, …)` is ONE route through the waypoints, not a
concatenation of pairs. The stops are fixed and ordered, so per-pair search really is optimal — what
is not independent is the STATE at the stop, so a leg is seeded with the arc the previous one arrived
along and the existing turn machinery then binds signage at the stop for free. A U-turn out of a stop
is expressed as the same thing, a barred turn, so both frontiers and the meeting test obey it without
knowing about stops; it is a preference (`allow_u_turn_at_stops`) that yields to a leg being otherwise
impossible, and the stops where it yielded come back in `Route::u_turn_stops`. All or nothing:
`unreachable_leg` names the pair that has no route. The app falls back to per-pair routing when there
is no through route, and says which answer is on screen.
**Ferries and tolls (O5e)**: a `route=ferry` way now enters the graph as **`RoadClass::kFerry`** — a
class, not a flag, because everything a class decides differs on a boat (who may board, and above all
the speed: the crossing's own `duration` tag over its own length, one speed for every edge the way is
cut into, and **`ProfileSeconds` returns it whatever profile is asking** — you do not walk a ferry).
`toll=yes` is the opposite shape and is a bit, `kArcToll` — a tolled motorway is still a motorway and
keeps a motorway's weight. Both are avoided through `RouteOptions::{toll_penalty,ferry_penalty}`:
a multiplier like `private_penalty`, or `kAvoidExcluded` (-1) to bar the arc outright in `ArcUsable`.
These two are the ONLY profile-backed settings the router reads from the **options** rather than the
profile — `SelectProfile` seeds them and the query has the last word, so "this profile but no ferries
today" needs no profile of its own; every caller (CLI, binding) applies its override *after*
`SelectProfile`. Excluding a ferry can leave an island unreachable, and that is the answer.

**Turn restrictions (O5a)**: `type=restriction` relations resolved onto `(via_node, from_arc, to_arc)`
— both arcs belong to the via node. The searches label **states**, not nodes: a restricted junction is
split into one state per arc it can be entered along (+1 for "arrived along nothing"), everything else
stays one state per node, so an unrestricted graph costs exactly what it did in O4. Car-only;
`--ignore-turns` on either the build or the query takes them out.
**Cost rules (O5c)**: every weight, speed and penalty lives in `rules/route-weights.json`
(`fv_route_rules.h`) — profiles of `{mode, speed, metric, turn_restrictions, private_penalty,
per-highway-class weights}`, with `extends` for variants. `RouteRulesFile` polls mtime+size and
rereads, so weights are tuned with the application running; a file that fails to parse is rejected
whole and the loaded rules stay in force. `RouteRules::Builtin()` reproduces the O5b hard-coded
profiles exactly, so `RouteOptions::profile == nullptr` is the pre-O5c router unchanged.

**Apps/bindings**: `port/bindings/pyfvw` (full binding surface incl. `pyfvw.vector`, `pyfvw.catalog`,
`pyfvw.engine`, `pyfvw.overlay`, `pyfvw.canvas`, `pyfvw.routing`, `pyfvw.Settings`, and A6's
`pyfvw.app` — registry/shell/session/editors/pick, in its own TU `pyfvw_app.cpp`),
`port/apps/PythonView.py` (the tk application, and an `AppShell`), `port/apps/route.py` (the
route overlay: a document, a pick target and an editor), `fvrender`, `fvpack` and `fvgraph` CLIs.
The app's own overlay types are `app.crosshair` (static, top-most, restored at startup) and
`app.coverage` (static); the port's own are `fv.grid`, `fv.points` and PythonView's `fv.route`.

**Test data** (`TestData/`, git-ignored, all present): dted, geotiff DOQs, rpf CADRG, tiros3,
`vpf/dnc17`, `VPF 2/WVSPLUS`, `GeoSymbol/{SymAssign,Graphics}` (DataDir = `TestData`),
`OSM/map*.osm` (adjacent Kiawah Island API exports — they OVERLAP, so a way appears in more
than one; **re-exported 2026-08-12**, now THREE files over a wider box, which is why the routing
tests enumerate `map*.osm` rather than naming them — see the refresh row in the archive) and `OSM/us-south-260728.osm.pbf` (4 GB raw extract, 548M nodes before the first way),
`enc/` (the **8** Charleston cells bands 2-5 the ENC goldens are pinned over — the other **815**
were moved to `TestData/enc-archive/` on 2026-08-11, a sibling because the cell scan recurses;
see §2d for how the 8 were chosen and how to put the rest back — plus `chartsymbols.xml` + `s57objectclasses.csv` +
`s57attributes.csv` + `s57expectedinput.csv` + `rastersymbols-{day,dusk,dark}.png`;
S-52 data-dir arg = `TestData/enc`), `OSM/mbtiles/us-south.mbtiles` (**ocean merged in 2026-08-11** — 3.85 GB, 1,246,885 tiles;
see §2b for the tilemaker recipe and why a rebuild without it silently loses the sea).

---

## 2. Open work

### 2a. Active track — next sessions, in order

| # | Session | What it is |
|---|---------|-----------|
| **G4** | Render state — highlighted | **G3 landed 2026-08-14, so this is unblocked.** `RenderState{kNormal,kHighlighted}` over T2's stamped-halo mechanism, so it needs no new `ICanvas` op; the consumer exists already, and G3 SHARPENED it rather than fixing it: `PointOverlay` now stamps the selection as the same builtin symbol one size up in yellow (T2's halo trick, by hand) and `route.py` re-bakes its marker library's colour per waypoint. Both are ONE mechanism now instead of three, which is what makes `RenderState` a replacement rather than a rewrite. **DIMMING IS DEFERRED — Chris 2026-08-13, decide later if and when it is needed.** The two decisions already worked out are parked in §3d of the draw plan so they are not re-derived: the resolution rule (not current AND another open overlay shares the `TypeId` — the second clause is the point) and that dim must be a blend toward the background rather than an alpha reduction. It costs one enum value and one filter to add later, and nothing in G1–G3 is shaped around its absence. |
| **R3d** | Perf, fourth slice — *if anything still needs it* | R3c re-measured the whole profile **at -O2** (see the row in the archive: the default build type was the real finding) and the frame it was aimed at is now **cold 49 ms = query 29 + style 11 + draw 8; a retained pan 3.4 ms; a pan out of the retained area 11.6 ms = query 0.7 + style 6.9 + draw 3.5**. The remaining shape: the cold 29 ms is the **one-time** parse of a whole DNC library and the 6.9 ms is **GeoSym styling** — so the next target, if a user still feels one, is `LookupTableStyleEngine`, not the query and not the rasterizer. **Do not start this without a fresh profile**: this is the third time in a row the plan on this line has been wrong about where the time was (R3b, R3c). The columnar `FeatureBatch` is now a **measured non-goal** — see the archive. |
| **O5** | A route the user can steer | O4 landed the graph, the router and the app's "r" key; **O4b closed the profile half**; **O5a closed the correctness gap** (turn restrictions, and a search that splits exactly the junctions they name) and **O5b made `access=private` a price rather than a deletion**, so a gated community keeps its street network; **O5c moved every cost number out of `Router::ArcCost` into a JSON rule file** (`port/Routing/rules/route-weights.json`, `fv_route_rules.h`) that `RouteRulesFile` rereads on an mtime/size poll, so weights are tuned while the application runs — a bad file is rejected whole and the loaded rules stay in force, and `RouteRules::Builtin()` reproduces the O5b profiles exactly, so a caller that loads no file is unaffected; **O5d made the waypoints ORDERED STOPS the route passes through** rather than independent pairs (`Router::RouteVia`/`RouteNodesVia`, `Route.route_via`, `fvgraph --via`); **O5e closed avoid-ferry/avoid-toll**, and the ferry half turned out to be a data gap rather than a preference — `route=ferry` carries no `highway` tag, so before O5e a ferry was not in the graph and there was nothing to avoid (archive rows O4, O4b, O5a, O5b, O5c, O5d, O5e). What is left: **via-way restrictions**, which O5a recognises, counts and deliberately does not apply — a search state carries the arc it arrived on and nothing further back, so these need either a longer state or the via arcs edge-expanded at build time. Smaller, now that the bike profile exists to make it visible: **steps cost nothing extra** beyond being excluded outright, and there is no elevation term at all. A ferry's **timetable** is likewise unmodelled — the crossing costs its `duration`, never the wait for the next sailing. |

### 2b. Product gaps

- ~~**MarinerSettings on `StyleContext`**~~ **Done (M1), and not on `StyleContext`** — the settings
  belong to the ENGINE, where the epoch that invalidates a retained scene already lives, so
  `fv::MarinerSettings` (`fvkit/vector/mariner.h`) sits on `LookupTableStyleEngine` and both products
  read it. GeoSym maps it onto `CECDISValues`' `ssdc`/`msdc`/`mssc`/`idsm`/`isdm`; `S52MarinerSettings`
  is now an alias. Each product keeps its OWN defaults (DNC 10 m and pattern on, S-52 30 m and off) —
  they are what the goldens were pinned over. Settable from `[mariner]` in the ini; **still no UI**
  (§2c). Note the accessor split: `mariner()` is const and free, `mutable_mariner()` bumps on call.
- **The point overlay has no EDITOR, so a `.fvpoints` document is read-mostly in the app** (A6).
  The C++ side has everything an editor would need — `AddPoint`/`RemovePoint`/`SetSelected`, a
  dirty flag, a save that replaces the table in one transaction — and all of it is bound, so a
  point can be added from Python today. What is missing is an `OverlayEditor` for `fv.points`
  and the two gestures behind it (click-to-place, drag-to-move), which is the same shape
  `RouteEditor` already has in `port/apps/route.py`. Chris's stated next step is to evolve the
  DATASET, and that runs through `sqlite3` and the schema rather than through the UI, so this is
  a gap and not a blocker — **schema 2 was the first step down that road** (embedded symbols,
  above), and it sharpens what an editor would have to offer: a symbol PICKER over the embedded
  palette, which is a list of names and thumbnails and nothing more. What is NOT there is any
  way to get artwork into a document from the app: `add_symbol_from_png` is bound, so it is a
  Python one-liner, but the only thing that calls it is `WriteSampleFile`. Related and smaller:
  the sample document is written to
  `<catalog dir>/sample.fvpoints` by a File-menu item, which is a demo rather than data
  management; the shapes are drawn at their authored pixel size and so ignore device DPI exactly
  as symbols do (§2b above — G3 gave the overlay a `symbol_dpi_scale` to honour and nothing sets
  it); and point LABELS are off by default because there is still no label collision (see below) —
  turning them on over a dense set overlaps them, though since G3 each one at least wears a white
  halo and is legible over whatever it lands on.
- **WVS (WVSPLUS)** — Chris wants it. Blocked in the reader, root cause known: `fv_vpf` builds the
  feature-class list only from **FCA**, and WVS thematic coverages have none → empty. Fix =
  enumerate from **FCS or a directory scan**, then a simple stroke style engine (WVS has no GeoSym
  symbology). Data: `TestData/VPF 2/WVSPLUS/WVS{012,040,120}M`.
- **Symbol size does not honour device DPI while line widths do.** A symbol is sized on its
  product's own nominal pixel — `IStyleEngine::himetric_per_symbol_pixel()`, 25.4 for GeoSym
  (1/100 inch, bit-faithful *by rule*) and 32 for S-52 (0.32 mm, E7) — and a raster tile is
  blitted 1:1. **None of those three numbers is the device's.** On a retina pitch every symbol
  is half its physical size while the line widths and text beside it are right. The fix is one
  more factor (`dpi/100` on the display lists, `0.32 mm / device mm-per-px` on the tiles), and
  the reason it is not already applied is the bit-faithful rule: it would move every GeoSym
  golden. E7 fixed the different defect underneath it — the two symbol FORMS of one product
  disagreeing with each other. **G3 BUILT the way out and nothing sets it yet**:
  `GeoDraw::symbol_dpi_scale` exists, defaults to 1.0 (the identity) and really multiplies the stamp
  — but `PointOverlay` leaves it at 1.0 because an overlay does not know the device, and no shell
  passes one down. That is now one line in whichever shell has a display it can measure, and it is
  the same shape as the `PickSession::tolerance_px` gap in §2c.
  **G2 added the other half of the answer for RASTER symbols**: `SymbolPixmap::pixel_ratio` means
  a tile can now state that it is drawn at 2 tile pixels per nominal pixel, so a high-DPI sprite
  set is expressible. It does not FIX this defect — the nominal pixel is still not the device's —
  but it removes the reason a retina sprite sheet could not be loaded at all.
- **A session the user builds at RUN time cannot be persisted** (A3). `OverlaySession::
  SaveConfiguration` writes `[session.<name>]` into the live `fv::Settings` and
  `RestoreConfiguration` reads it back, so the round trip is real and tested — but **`fv::Settings`
  has no `Save()` by rule S1** (the file is authored by a human and the application never rewrites
  it, which is what preserves the comments and the unknown keys). So a hand-written `peregrine.ini`
  can carry a startup session today and "save my current layout" cannot. The fix is not to relax S1:
  it is a **second** store for application state — window geometry, last position, the saved session
  — which the settings header already says belongs "somewhere else, not here", and which nothing
  has needed until now. One decision, then a writer. **A6 made this the app layer's most visible
  gap**: PythonView now has documents to remember, so "reopen what I had open" is a thing a user
  would expect, and `SaveConfiguration`/`RestoreConfiguration` are bound (`pyfvw.app`) and work —
  into memory only, so the app does not call them.
- **A top-most overlay's opacity is carried and not applied** (A2). `OverlayTypeDesc::default_opacity`
  is FalconView's blend for the top-most band (the crosshair, a HUD) and `DrawAll` draws that band
  as a second pass exactly where the blend belongs — but `ICanvas` has no layer alpha to blend
  WITH, so the number is ignored. Same shape as the pattern-brush item below: the seam is right,
  the canvas is missing one operation. The fix is an off-screen layer (draw the band into its own
  `PixelBuffer`, composite at `opacity/100`), which is also what a real pattern brush and a clip
  region would want, so all three are one canvas session.
- **ICanvas has no pattern brush.** GeoSym stipples and S-52 `AP` fills are approximated by carrying
  ink coverage in the fill **alpha**. `AreaFillFor` is the one place to change when a real pattern
  brush lands. (Related: **area patterns are not clipped to their area** — archive 2026-07-27 E3b.)
- ~~**ICanvas has no outlined (halo) text.**~~ **Closed by T2, 2026-08-12** — and the guess on this
  line about where the work lived was wrong, which is worth keeping. It said "one pass in
  `CpuCanvas::DrawRotatedTextString` plus a colour/width on `TextStyle`". That is a real coverage
  dilation; what Chris asked for is the Windows method — the string stamped 4 times (8 past one
  pixel, or the corners open) a pixel or two off in the halo colour, then the text over it — which
  needs NOTHING from `ICanvas`. So it is `LabelStyle::halo_width`/`halo_color` and a pass in
  `VectorRenderer`, and every backend including pyfvw's Python `ICanvas` subclasses got it without
  growing a virtual. **What is still open**: no blur (`text-halo-blur` is ignored and counted —
  a stamped halo has no coverage to soften), and no product but OSM sets a halo. S-52 and GeoSym
  both draw text that would read better with one, and neither authors a halo colour, so giving
  them one is a symbology decision rather than a port gap — and it would move their goldens.
- ~~**Nothing yet CONSUMES a symbol library**~~ **Closed by G3, 2026-08-14.** `GeoDraw` is the
  consumer the G2 row was waiting for, `BuiltinSymbolLibrary` now draws every `fv.points` marker and
  every `fv.route` waypoint, and the libraries are bound as `pyfvw.symbol`. Still true and still
  fine: no CHART symbol moved — every symbol a style engine draws still goes through
  `VectorRenderer` exactly as before, which is what kept the goldens byte-identical.
  What has no consumer yet is `CgmSymbolLibrary` (GeoSym's ~1500 `.cgm` in an overlay) and
  `PngSymbolLibrary`'s sheet form (still the OSM-icons wiring below).
- **OSM has no icons — but the LOADER now exists** (O2/O3). G2 built `PngSymbolLibrary` sheet-
  capable (`sprite.json` + the PNG, `pixelRatio` honoured through `SymbolPixmap::pixel_ratio`), so
  the half this line was really about is done. What is left is the WIRING, and it is an Osm
  session: `OsmStyleEngine` must load the style's `sprite` URL through a `PngSymbolLibrary`, stop
  counting `icon-image` in `ignored_icons()` and emit a `PointSymbolStyle` for it. Still open and
  untouched by G2: `fill-pattern`/`line-pattern`/`background-pattern` are load-time rejections,
  and a real one needs the pattern brush below.
- **No label collision or de-duplication.** Every product that draws text needs it and none has
  it; OSM makes it visible because a road name repeats per tile — and **T1's `symbol-spacing`
  now repeats a name along a long road as well**, which is correct and also multiplies the
  overlaps. Belongs in the renderer/scene, not in a style engine: a per-frame index of the boxes
  the renderer is about to emit, rejecting a label that collides with one already placed. The
  boxes exist already (the pick index takes one per label run).
  **E8 made this ENC's most visible defect** by taking the text out from under the geometry that
  was hiding it: a Charleston harbour view draws "Shutes Folly Island" three times and
  "James Island" four, one per overlapping cell. Note the second cause, which de-duplication
  alone will not fix — the 8 test cells span 4 usage bands over the same water and all of them
  were opened at once, where an ECDIS shows one band; a NAME is not unique across bands, so the
  index has to key on more than the string.
- ~~**OSM has no OCEAN.**~~ **Fixed in the DATA, 2026-08-11 — never was a port defect.** The
  original `us-south.mbtiles` held **only `class=lake`** (checked across every z5–z7 tile: not one
  `class=ocean` anywhere) and a z12 tile mid-Atlantic carried a `boundary` feature and nothing
  else, so the sea drew as the style's `background` — the same off-white as the land. Cause:
  `config-openmaptiles.json` already declares an `ocean` layer sourced from
  `coastline/water_polygons.shp`, and `ShpProcessor::read` **returns silently** when `SHPOpen`
  fails, so a build without the shapefile loses the ocean and says nothing.
  **The recipe, should the pyramid ever be rebuilt** (`tilemaker` v3.1, `~/Documents/Source/tilemaker`):
  `./get-coastline.sh` (→ `water-polygons-split-4326.zip`, ~800 MB, **WGS-84 only** — the reader
  takes shapefile X as degrees and passes Y through `lat2latp`, with no reprojection and no `.prj`
  read, so a 3857 download fails the bbox test and silently draws nothing), then run tilemaker
  **from the tilemaker directory** with `--merge`, no `--input`, and an explicit `--bbox`. Merging
  is layer-aware — `ProcessLayer` copies every existing feature of a layer into the new tile first —
  so the ocean run can go SECOND, over the finished pyramid, instead of re-running the 4 GB pbf.
  **The trap**: only layers in *that run's* `layerOrder` are copied through, so the merge must use
  `config-openmaptiles.json` (whose 16 layers cover the file exactly), never `config-coastline.json`,
  which would delete transportation/place/poi/building from every tile it rewrote.
  Measured on the real file: 420 ocean polygons over `-106.66,24.02,-74.69,40.65`, **798,627 →
  1,246,885 tiles, 3.64 → 3.85 GB**, ~10 min, and a Charleston coastal tile kept all nine of its
  layers at identical feature counts with `water` going 42 lakes → 42 lakes + 1 ocean.
- **S-52's `SPACE` and `DISPLAY` text parameters are still skipped** (E8 took the other four).
  `SPACE` is character spacing — the library states 1, 2 or 3 and the canvas has no letter-spacing
  control, so this needs `CpuCanvas` before it needs the loader. `DISPLAY` is the group number and
  belongs on the viewing-group axis, not in the label.
- **ENC body size is treated as PIXELS, not points.** `TextSizeFromSpec` reads the `CHARS` body size
  (10, 18, …) straight into `TextStyle::size`, which is documented as pixel height. Same family as
  the device-DPI item above and the same reason it has not moved: it would shift every label on
  the chart, and the conversion wants the device's real pitch rather than another constant.
- **`OsmStyleEngine` colour stops STEP, numeric stops interpolate** — a declared O2 deviation from
  the GL spec, which interpolates colour ramps too. Visible only side by side.
- **OSM Bright is not vendored.** O2 ships `port/Osm/styles/peregrine-osm.json` (ours, written to
  the OpenMapTiles schema) as the reference style. Adding `openmaptiles/osm-bright-gl-style`
  (BSD-3/CC-BY) as a second reference needs a network fetch and a `NOTICE.md` entry, and will
  exercise the subset check against a style nobody here authored — worth doing for that alone.
- **`VpfVectorSource::Bounds()` costs a full scan** — tile bounds need the tileref face, so per-feature
  bounds are used. Much reduced by R3c (the scan happens once per open, and `Bounds()` now rides the
  same parsed cache every Query does), but it still parses the whole library to answer "where is
  this?". The 14m catalog already stores per-tile coverage; use it for tile-level culling.
- **Area patterns bleed past their ring.** `PlaceOverArea` stamps at lattice points inside the ring,
  but `ICanvas` has no clip region, so a symbol whose ink overruns the boundary is not trimmed — a
  pattern can spill by up to half a symbol. Unchanged since E3b; the fix is a clip rect on `ICanvas`.
  Sharper now that the placer tests the exact ring (R3c follow-up) rather than a rounded one.
- **Area patterns are outer-ring only.** `p == 0` in the renderer's area branch: holes do not punch
  through a pattern (or a fill). Waiting on V5c face topology.
- **CIB gap**: the CADRG decoder supports CIB but `CadrgRasterSource` hardcodes `is_cib=FALSE`, and
  there is no CIB test data.
- **Ferries and tolls have no REAL-data fixture** (O5e). `TestData/OSM/map*.osm` is Kiawah, which
  carries **zero** `route=ferry` ways and **zero** `toll` tags, so every O5e test runs on the
  synthetic "bay" fixture (a tolled bridge, a ferry calling at an island, and a long road round —
  duplicated in `road_graph_test.cpp`, `router_test.cpp` and `test_pyfvw.py`). That is enough to
  pin the mechanism and not enough to catch a tagging shape nobody thought of.
  `TestData/OSM/us-south-260728.osm.pbf` has both in quantity; one `fvgraph build` over it (~1 h)
  would give real counts for the `ferry / toll` line `build` and `info` now print.
- **A ferry's `duration` is prorated wrongly on a CLIPPED extract** (O5e, noted not fixed). The
  speed is the way's kept length over its stated duration, and a crossing cut by the extract's
  bbox keeps less length than the duration covers — so the boat reads as slower than it is and the
  ferry is merely less attractive. The safe direction, and unfixable without the full way length,
  which a clipped extract does not have.
- **Router scratch is allocated per query.** Each `Router::Route` allocates six arrays of
  `state_count` (~34 bytes/state; a state is a node except at a restricted junction): fine for a
  state-sized graph, ~700 MB per query on a continent.
  The fix is reusable scratch with an epoch stamp instead of a full reset — but `Router` is const
  and shareable today, so it needs a thread-safety decision, not just a `mutable`.
- **`RoadGraph` cannot cross the antimeridian.** `Finalize` takes a plain min/max bounds and hangs
  the nearest-node grid on it, so a Fiji-shaped extract gets a world-spanning box and useless
  cells. The catalog already has the split-at-180 treatment to copy.
- **`FeatureStore`** — the other half of L5 (only `TilePack` was built).
- **ImageLib leftovers**: `fv_imagelib_gif` compiles but has **no test** (no `.gif` sample);
  `Image.cpp` (multi-format dispatcher) and `nitf/` unported (row 9b-4).

### 2c. PythonView / UI follow-ups

- ~~**Nothing in `fv::app` is reachable from the app**~~ **Closed by A6.** The layer is bound as
  `pyfvw.app`, PythonView is an `AppShell`, and both of the seams this line complained had no
  consumer now have one (`ChooseFromList` is the tk ambiguity chooser; `SnapToPoint` is bound and
  tested from Python). **What A6 left open, and none of it blocks anything:** no shell calls
  `OverlayManager::Reorder`, so the plan's reorder DIALOG is still unwritten and the stack order
  is whatever insertion-by-display-order produced; `SnapToPoint` has a binding and a test but no
  overlay in the app ANSWERS it, so nothing snaps yet; `EditorUiConstraints` is reported and
  nothing greys anything, because the app has no rotation or projection controls to grey; and
  `RoutingOverrides` is bound but unused.
  **`CaptureMouse` got its first consumer 2026-08-13 — drag-and-drop of route waypoints — and the
  finding was that the CORE was complete and the SHELL was the whole gap.** PythonView bound
  `<ButtonPress-1>` straight to a map pan, synthesized a `route_mouse_down` at RELEASE time and only
  for a press that had not moved, never called `route_mouse_up` or `route_double_click` at all, and
  passed `MouseEvent(x, y)` with the button and modifier fields left at their defaults — so no
  overlay could express press-move-release and capture had nothing to capture. The shell now offers
  the press to the stack FIRST and pans only when the stack declines; `RouteOverlay` takes a press on
  a waypoint, captures, drags on move, commits on up. Four rules fell out and are worth not
  re-deriving: a press is a SELECTION until the cursor actually travels (~3 px), so clicking to
  select costs no undo entry and does not dirty the document; the whole drag is ONE undo snapshot,
  taken at the first real movement rather than at the press; Escape mid-drag SPENDS that snapshot
  putting the waypoint back, so a cancelled drag leaves no history at all (reachable because the
  manager gives the capturing overlay the key first, which is exactly what that rule exists for); and
  a drag drops a followed road, because the road line was computed for waypoints that have since
  moved. `release_edit_focus` cancels a drag in flight for the same reason it already cleared
  `adding`. **`route_double_click` is still called by no shell.**
- **The route line's STYLE is derived, not chosen** (G3). `route.py` draws a calculated route
  dashed when the request was a bicycle one and solid otherwise, and it decides that by matching
  the profile NAME (`bike`/`cycle`) or the `cycle_only` flag — `RouteOverlay._is_bicycle_request`.
  That is right for the two modes the app has keys for ("r" and "b") and it says nothing about a
  third: a walking profile draws exactly like a car. The honest fix is a per-profile line style in
  the rule file beside the weights, which is a rules-schema decision rather than a drawing one.
  Also unstyled: `pyfvw.draw.PRESETS` has ten entries and the app reaches two of them, and there is
  no UI for a route's colour or width (the document carries a colour and the Options dialog does
  not offer it).

- **`PickSession::tolerance_px` is 8 device pixels and the shell is supposed to scale it.**
  Same family as the symbol-DPI gap in §2b: the core has no business knowing a finger is wider
  than a mouse pointer, so the number is deliberately not the core's to adjust — but no shell
  adjusts it yet either, which means on a retina pitch the pick radius is physically half what
  it reads as. **Now a real shell exists and still does not** (A6): PythonView leaves the
  default, and it already knows its own `display.mm_per_pixel`, so this is one line whenever
  somebody has a device where it is wrong.

- **`private_penalty` has no UI and no settings key** (O5b). It is bound (`Router.route(...,
  private_penalty=5.0)`) and on the CLI (`fvgraph route --private-penalty X`), but the app always
  takes the default. The number itself is a judgement, not a measurement: 5x keeps a route off a
  gated shortcut while leaving an address behind the gate reachable, and nobody has driven it
  against a reference router.

- ~~**Route profiles have no UI** (O5c).~~ **Done.** `[routing] rules` / `[routing] profile`
  settings keys, an Options-dialog row (path + browse) and a profile menu built from
  `rule_profiles()`, which rereads the file — so a profile added while the app is running appears
  in it. `rules_error()` shows under the menu in full and, clamped, on the route's status line, so
  a bad edit says so while the last good weights keep routing. **Reload is inherent, not a button**:
  `RouteRulesFile` polls mtime+size per `route()` call, so an edited weight lands on the next "r"
  with nothing restarted (measured: same overlay, 2 min → 4 min after a `speed` edit). "Reload
  Rules" in the dialog exists only for the MENU, which is the one thing that would otherwise go
  stale. `follow_roads()` now reports anything that is not `OUT_OF_COVERAGE` — a profile the file
  does not define above all — instead of hiding it as a straight leg. **Still no UI**: the profile
  is the app's one route-wide setting; a per-waypoint or per-leg profile has nowhere to live.

- **Toll and ferry avoidance have no UI** (O5e). Both are bound (`Router.route(...,
  toll_penalty=…, ferry_penalty=…)`, taking a number, `False` or `"exclude"`) and on the CLI
  (`fvgraph route --avoid-toll/--avoid-ferry/--toll-penalty X/--ferry-penalty X`), but the app
  always takes the profile's default. Unlike the rest of §2c this one wants **two checkboxes and
  nothing else** — they are the two settings a driver changes per journey, they already outrank
  the profile by design, and `car_no_tolls` / `car_no_ferries` in the shipped rule file are only
  there because there is no UI. Worth doing beside the profile menu O5c added.
- **The U-turn-at-a-stop preference has no UI** (O5d). `allow_u_turn_at_stops` is bound
  (`Router.route_via(..., allow_u_turn_at_stops=True)`) and on the CLI (`fvgraph route --u-turns`),
  but the app always takes the default (barred). `Route.u_turn_stops` is likewise not drawn: the app
  says how MANY stops had to turn round, not which, though a marker on the offending waypoint is
  what would actually tell the user their stop is up a driveway.
- **Rule layer has no UI.** `pyfvw.vector.RuleSet` / `ViewingGroupSet` and `engine.rules()` /
  `viewing_groups()` are bound and tested but unreachable from the app. Natural shape: an Overlays-menu
  display-category picker (Base/Standard/Other) + a "Load rule file…" item.
  **Half-answered by M1**: `FamilySet` is the file-level form of the same thing — the app loads
  `[vector] families_{dnc,enc,osm}` at engine open and hides what the file says to hide, so a user can
  switch groups off without a menu. An Overlays-menu checkbox per family is the obvious next step and
  is one `SetEnabled` + rebuild of the RuleSet; `FamilySet::epoch()` is there so a host can tell.
- **Mariner panel in the Options dialog** — safety/shallow/deep contour, safety depth, two-shade and
  the shallow pattern are bound on BOTH products since M1 and settable from `[mariner]` in the ini,
  but there is no dialog. It is the one setting a mariner actually changes underway (it is the
  vessel's draft plus under-keel clearance), so a spinbox beside the ENC/GeoSym rows is worth more
  than most of this list. Note the per-product defaults must survive it: a panel that writes all six
  values on open would give DNC S-52's numbers.
- **ENC data-dir field** beside the GeoSym one in Options (the `[enc] data_dir` settings key exists).
  The GeoSym directory and the OSM style sheet are both there; ENC is the one asset still
  settings-file-only.
- **OSM has no per-source knobs in the UI** — tile budget, tile-cache capacity and
  `set_clip_to_tile` are bound and defaulted sensibly, but only reachable from Python.

### 2d. Known defects / hygiene

- ~~**Every `S52Render` test fails at `Open()`.**~~ **Fixed 2026-08-11 by cutting the data back.**
  `TestData/enc` had grown to 823 cells and `EncVectorSource::Open` fails WHOLE on the two that
  will not parse (`US5CT1FV.000`, `US2EC04M.000` — `field 0001 truncated`), so all seven died
  before drawing. **815 cells moved to `TestData/enc-archive/`**, a SIBLING and not a
  subdirectory: `EnumerateEncCells` is a bare `recursive_directory_iterator` matching `*.000`
  with no directory filter (`fv_s57.cpp:499`), so anything under `enc/` is still found.
  The 8 that stayed are reproducible, not hand-picked — every cell whose catalogued coverage
  meets **lat 32.60..32.95, lon -80.15..-79.75**, the padded extent of every coordinate the ENC
  tests name: `US5CHS{DC,DD,EC,ED}` (Harbour), `US4SC1{BO,CO}` (Approach), `US3SC1CB` (Coastal),
  `US2EC02M` (General). That is exactly the "8 cells bands 2-5" the goldens were pinned over.
  Load time for the suite went 8.7 MB / 8 cells instead of 215 MB / 823.
  **The two decisions this raised are still open**, and neither is urgent now: (a) should ONE
  unreadable cell abort an exchange set, or be skipped with a warning and a count — an ECDIS
  would not refuse the other 822; (b) the render tests still open `$FVW_TESTDATA_DIR/enc`
  wholesale, so restoring the archive re-breaks them. Naming a fixed cell list would make the
  goldens independent of what else is on disk.
- **Five Osm tests fail on the ocean-merged `us-south.mbtiles`** (found 2026-08-11 during O5d;
  confirmed **pre-existing** by stashing that session's diff and rerunning — they fail identically,
  and nothing in O5d touches Osm). Four `Mbtiles` tests (`TheLayerInventoryComesFromTheJsonMetadata`,
  `TheDeclaredBoundsAreWrongAndThePyramidIsAuthoritative`, `TilesComeBackByXyzWithTheTmsFlipHandled`,
  `ZoomExtentsReportTheXyzBoxAndTheTileCount`) plus
  `OsmVectorSource.AScalelessQueryIsCappedByTileCountNotAttempted`, which came back **5001 against
  its cap of 5000**. All five pin the pyramid's shape, and the merge changed it — one more layer,
  798,627 → 1,246,885 tiles, and sea where there was nothing (§2b). Not diagnosed further than
  that: the failures are consistent with the merge and nothing else changed, but which assertion
  wants which new number has not been worked out. This is the ledger's own "never pin a total over
  a whole data directory" rule collecting. Re-pinning is a look-at-the-data decision, not a
  mechanical one, so it is left for an Osm session.
  pass serially. They write the same scratch `.gpkg`. Fix = per-test filename.
- **`pyfvw_pytest` cannot run in the ASan build** (found R3c, and it is the toolchain, not the code):
  Python is not sanitizer-instrumented and `dlopen`s an instrumented `.so`, so ASan aborts with
  "Interceptors are not working … loaded too late". The 666 C++ tests are unaffected. Either run it
  under `DYLD_INSERT_LIBRARIES=<libclang_rt.asan_osx_dynamic.dylib>` or exclude it from `build-san`.
- **Invariant 3d.1's CANCEL branch is unreachable and is pinned through a FAILURE** (A4). "If the
  user cancels creation, the mode falls back to none" shares one branch with "if creation failed",
  and the test drives it with a `FileNew` that refuses — because neither creation flow currently
  asks the user anything (`NewFileOverlay` prompts nowhere, and `ToggleStatic` prompts only when
  CLOSING, which adoption never does). The fallback is genuinely pinned; the word "cancel" in it
  is not. A creation flow that grows a prompt (a template chooser, an overwrite warning) should
  add the cancel test at the same time.
- ~~**A clipped-away leg does NOT break a `PolylineContour` run.**~~ **Fixed in G3, 2026-08-14.**
  The code always got the intent right — a leg that emitted nothing keeps the NEXT leg's first
  point — and had nowhere to SAY so, because `IGeoContour::NextPoint` is a flat point stream. G3
  added **`IGeoContour::AtBreak()`**, asked after `NextPoint` and about the point that call
  produced, defaulting to false so every other contour (all of which are one connected run by
  construction) is unchanged; `BuildGeoPathInto` flushes the sub-path on it, BEFORE the
  antimeridian test, since two points either side of a break are not neighbours at all. The test
  that pinned the defect as-it-behaved was written so that fixing it would fail — it did, and it
  is now `test_a_clipped_away_leg_breaks_the_run` asserting the opposite, with a C++ twin in
  `geo_contour_test.cpp`. `route.py` accordingly draws its legs with one `polyline` call instead
  of the leg-by-leg workaround. Still NOT this defect and still correct: a point merely off the
  edge of the SURFACE projects to a coordinate outside `0..w` and is carried through.
- **VPF reader UBSan alignment** — `vpfrcset`/`tables` do unaligned scalar loads. ASan-clean; this is the
  only UBSan noise in the tree, which is why every VPF session says "only the pre-existing ones".
- **`VPFRecordset` reopen row-undercount** (original bug, preserved bit-faithfully; noted, not fixed).
- **Subsampled `ReadBlock`** — zoomed out, 9 fully-VQ-decoded CADRG frames ≈ 1.2 s. Decoders read full
  resolution and then downsample.
- **Polar CADRG transforms** return `kUnsupported` (equal-arc only).
- **TIROS tile-seam** artifacts.
- **C++14 pins** still on `fv_jpeg`, `fv_jpeg12`, `fv_imagelib_gif` (`std::auto_ptr` in headers).
- **`CDTEDInstance` stub** in ImageLib's `Util.cpp` — RPC height refinement returns "no DTED" headless;
  wire `fv::DtedCell`.

### 2e. Dependency modernization (three ⛔ rows left; full table in the archive)

The port builds against current upstream via `port/third_party/CMakeLists.txt` (FetchContent). **Done**:
googletest 1.17.0, zlib 1.3.2, expat 2.8.2, protozero 1.8.2, vtzero 1.2.0, nlohmann/json 3.12.0.
**Frozen on purpose**: GEOTRANS 3.3 (a newer one invalidates the pinned geo results).

Each remaining row is a session of its own; none blocks the active track. Do them in this order
(ascending consumer count, so a break localises):

1. **libpng 1.2.7 (2004) → 1.6.58** — not a drop-in: opaque structs, reworked `png_get_`/`png_set_`/`png_jmpbuf`.
2. **libtiff 3.9.4 → 4.7.2** — not a drop-in: `toff_t` widened to 64-bit; `CGeoTiff` is ~25K lines against the 3.x API.
3. **IJG jpeg 6b → libjpeg-turbo 3.2.0** — hardest: FalconView *transliterated* IJG to C++ and the wrapper carries an
   encryption fork (`m_crypt_pos`/`m_encrypt`, must be shown unused first); re-opens the "FalconView's C++ jpeg and
   GDAL's C jpeg must not meet in one link" rule. GDAL's vendored libjpeg goes away with this.

**Gate**: Q12 (WMS) is the first network-facing feature — anything parsing network input must be on a
modern library first. expat already is.

### 2f. Backlog (unstarted, roughly in priority order)

| # | Item | Data | Complexity |
|---|------|------|-----------|
| Q12 | WMS network raster source | public endpoints (USGS, GIBS) | moderate — HTTP client decision: libcurl |
| Q13 | JP2 via OpenJPEG | public samples | moderate (avoids Kakadu; would also unblock ECRG) |
| Q14 | NITF (ImageLib `nitf/`, row 9b-4) | public NITF test sets | moderate-high |
| Q15 | GeoPDF | USGS topo GeoPDFs | high (PDF engine decision) |
| Q16 | Lidar | USGS 3DEP | high, niche |

**Deprioritized** (Chris 2026-07-19 — restricted/proprietary data, not the public-data use case):
ECRG, CIB, MrSID, Hrdted/RDted/ARdted, BlankMapServer.
**Deferred indefinitely**: CoT (row 8), MdsUtilities (row 6 — Windows system plumbing only; pull
individual helpers on demand), Collaborate, NITFSourcesCtrl, *MapOptions property pages,
FvConfigFileServer. Other unported map servers: Ecrg, MrSID, Jp2, WMS, GeoPdf, Lidar, Blank.

### 2g. Still needed from Chris / the Windows machine

- [ ] **Reference output dumps from the Windows build** (elevations, decoded-pixel checksums) for
      golden-file tests — the port's goldens are all self-pinned after a visual check.
- [ ] **Reference screenshots of dnc17 harbour views** for the DNC goldens (the ENC side now has a
      published chart to check against; DNC does not).

---

## 3. Standing rules — violate one and you ship a silent bug

These are the archive decisions that still constrain new work.

**Porting mechanics**
- **Bit-faithful**: known numeric/behavioural quirks in the original are *preserved and documented*,
  not fixed. Fix only what is a bug *in the port*.
- **Compile in place** from `fvw_core/`; never move a file, split only if hopelessly Windows-bound.
  Sever COM/GDI with `#ifdef _WIN32` (`port/tools/guard_win32_functions.py` does brace-matched wrapping).
- **Never modify** `BuildAll.sln`, `*.vcxproj/proj/idl`, `WinDebug/`, `WinRel/`, `third_party/`.
  Shared edits must stay MSVC-compilable: guards, additive accessors, const-correctness.
- **`LONG` is `long` on Win32 but `int32_t`(=`int`) here, and old sources mix the spellings freely.**
  Expect declaration/definition mismatches in every in-place module.
- **Win32 integer widths are ABI**: `LONG`/`DWORD`/`HRESULT` are exact. Mirrored IDL enums in
  `port/include/` keep COM values — **never renumber**.
- Repeated mechanical transforms → a script in `port/tools/`, never dozens of hand edits.
- Editing Latin-1 shared sources re-encodes the file — use escapes (`'\xB0'`), not raw bytes.
- Compat shims: `port/include/fv_compat.h`, `fv_cstring.h`, `fv_mfc_containers.h`, `fv_oledatetime.h`,
  `fv_filemap.h`, `fv_win32_{filemap,finddata,path}.h`, `fv_sscanf_s.h`. **Prefer rewriting to standard
  C++ over growing them.**
- New code under `port/` is C++17; shared `fvw_core/` edits stay ≤C++17 and modern-MSVC-clean.

**Tests and goldens**
- A golden hash proves *nothing about orientation, colour or units* — F1 and F2 both survived a visual
  check and a pinned hash. **Asymmetric or unit-bearing behaviour needs its own directional assertion**
  independent of the golden.
- **Never pin a total over a whole data directory.** New TestData has broken whole-directory counts
  four times. Exact counts go on **one named cell/tile**; whole-root tests assert structure and lower
  bounds. **Two corollaries, both collected 2026-08-12**: don't NAME a fixture's input files either
  (`{map.osm … map-4.osm}` became `kNotFound` in 15 tests when the extract was re-exported with
  three) — enumerate them; and **never pin a settings file's contents**, only the mechanism that
  reads it, or editing the settings breaks the build (`port/families/*.json`).
- Re-pin a golden only after looking at the PNG, and record the old → new hash in the ledger row.
- Choose a fixture that can *show* the thing under test (the DTED band bug hid behind a flat cell).

**Vector seam**
- `VectorSymbol` is **y-UP**; `CgmSymbol`'s display list is y-DOWN and `ToVectorSymbol` applies the VDC
  direction multipliers. `bounds().top` is the *smaller* value.
- Style engines are **loaders over `LookupTableStyleEngine`** — do not write a fourth engine.
  Product-specific data properties (SCAMIN, `dispcat`) belong in the **source**, not the style engine.
- `IStyleEngine::Style()` **appends** — one row can paint at several priorities. Sort is stable across
  features by priority.
- The retained scene keys on the whole `StyleContext` and an **exact** scale; a no-op setter must stay a
  no-op or the cache never lands; `style_epoch()` defaults to 0 deliberately.
- The pick index is filled from the primitives the renderer **emits**, so a tap agrees with the screen.
- One comparison rule, one implementation (`rules.h`, since R2).

**Data/product facts worth not re-deriving**
- ENC `.000` is the **base edition only** — `.001…` updates are unapplied and the reader says so loudly.
  Enumerate by finding `*.000`, never the `ENC_ROOT/<producer>/<cell>/` path. `CATALOG.031` is itself
  ISO 8211 and gives bounds + long name for free. A cell's **usage band is its scale band**, not a place.
- S-57 `?` in a lookup condition is the **UNKNOWN-VALUE marker, not a wildcard** (read as a wildcard it
  paints the harbour no-data grey).
- **S-52 gives the display priority to the LOOKUP, so text inherits its object's band unless it is
  lifted out.** The library authors text-bearing rows at every priority there is — `LNDARE` carries
  a `TX` at Group 1 — so text at the object's own band is text under the rest of the chart. E8's
  `kS52PrioTextBase` is the fix; the same trap is waiting in any product whose table states one
  priority per row.
- **S-52 TX/TE placement decodes from the library's own rows, and it is worth re-deriving it that
  way rather than trusting a recollection of the spec.** HJUST/VJUST: 1 = centre, 2 = right/bottom,
  3 = left/top — `SEAARE` centres an area name on its centroid with `(1,2,0,0)`, and `BOYLAT`
  offsets LEFT at HJUST 2. XOFFS/YOFFS are in units of the text's own BODY SIZE, y positive DOWN.
  `CHARS` is style/weight/width/2-digit body size, but the delivered library also carries
  4-character forms (`'1508'`), so take the size from the LAST TWO characters, not a fixed offset.
- 17 S-52 symbol names are **raster-only definitions** — a vector-only path cannot draw them (E6 added
  `SymbolPixmap` at the seam).
- CGM monochrome pattern bits are stored **INVERTED**; ink coverage is the fraction of *clear* bits.
- An MBTiles file's declared `bounds` **cannot be trusted** — derive coverage. A tile carries a buffer of
  its neighbours' geometry. There is no OSM SCAMIN analogue; a scale-less bulk query is capped by a tile
  budget.
- **Zoom↔scale is latitude-dependent and there is exactly ONE implementation of it**:
  `webmerc::ZoomForScaleExact` / `ScaleForZoomExact` (`port/Osm/fv_web_mercator.h`). A Web Mercator
  pyramid holds scale constant per PIXEL, so z12 is 1:270k at the equator and 1:190k off Charleston.
  A source uses the viewport's centre latitude; a style engine is handed a scale and no geography,
  so it must be TOLD one (`OsmStyleEngine::SetReferenceLatitude`) — and the same `mm_per_pixel`
  the source got, or minzoom disagrees with the tile that was read.
- MapLibre's `!=` and `!in` are **true for a MISSING tag**; fvkit's `kNotEqual`/`kNotIn` are false
  (rules.h: a missing attribute makes every comparison false). The OSM loader reconciles the two
  with `Or(Missing(k), …)` — do not loosen the shared predicate for one product.
- **An OSM style's layer ORDER is the style file's business, not the port's.** Checked in E8 after a
  "roads draw over paths" report: `peregrine-osm.json` puts `road-path` at 17, above every road
  (12-16); the untracked `styles/style.json` is **CyclOSM**, where `road_path` is 18 and the roads
  are 20/26/27/28. Both render faithfully. Ask which style is loaded before reading an ordering
  complaint as a bug.
- A GL style's draw order is **style-layer order, not feature order**: one road feature emits both
  its casing pass and its fill pass, and every casing in the viewport must be drawn before any
  fill. `StyleResult::priority` = the style layer's index does this, because `VectorScene`
  stable-sorts by priority ACROSS features.
- DTED: level-3 out of scope; lookups are nearest-neighbour; elevation bands default in **feet** and
  need the converting setter.
- **A turn restriction cannot be expressed by a Dijkstra label per node** — the cheapest approach to
  the junction may be exactly the one the sign forbids. Split only the junctions a restriction names
  (one state per arc it can be entered along, +1 for "arrived along nothing"); everything else stays
  one state per node, which is why an unrestricted graph pays nothing. In a bidirectional search the
  two frontiers label a split junction *differently* — forward by the arc in, reverse by the arc out
  — so a meeting is a PAIR of states joined only if that turn is legal.
- **An OSM mode key falls through to the generic `access`**, so `access=private` alone speaks for
  every mode at once — it is not a motor-vehicle statement. Since O5b `private` is **priced, never
  deleted** (`kArcPrivate*` + `RouteOptions::private_penalty`): a road behind a gate is the only way
  to whatever is behind it, and on a gated community it IS the street network. An outright `no` is
  still a denial. On Kiawah `bicycle=no` is real and strictly enforced (bicycles are expected on the
  private cycleways), so the two must not be conflated — `--ignore-access` clears both and is a
  diagnostic, not a way to soften `private`.
- **"The arc I arrived along" has ONE definition — `ArcBetween(node, previous)`** — even though a
  path step sometimes carries the exact arc and `ArcBetween` returns only the first of a parallel
  pair. Both searches label a state by it, so a seed computed any other way makes the answer depend
  on which search ran (measured, O5d: 50 seconds apart over an identical node sequence).
- **A constraint that must hold in a bidirectional search cannot be a filter on the first
  relaxation.** Rejecting a meeting does not remove the reverse frontier's LABEL, and Dijkstra keeps
  only the best one — so the better route the constraint should have forced is already gone. Express
  it as a barred turn (O5d) or a split state (O5a), both of which every frontier consults.
- OSM PBF `Relation.memids` is **delta-encoded across all members whatever their type**, and
  `roles_sid`/`types` are parallel arrays. An OSM restriction names WAYS; a graph knows ARCS, and the
  join is the via node's own adjacency. `via way` restrictions exist and are not the same problem.
- **There is exactly ONE place an arc's duration is decided — `ProfileSeconds` — and every cost
  path must go through it.** `Router::ArcCost` had a branch that called `RouteProfile::Seconds`
  directly, which was identical for every class until O5e gave the ferry its own clock; then the
  search costed a crossing at the profile's flat walking speed while `Materialize` (which does go
  through `ProfileSeconds`) reported the boat's, and a walker was sent over a bridge that is
  actually slower. Caught in review, and the test that pins it is deliberately one where the wrong
  cost changes the ROUTE CHOSEN — a test that only checked the reported seconds passed under the
  bug, because the bug was never in what was reported.
- **A ferry is not a `highway=*` way.** `route=ferry` with no highway tag at all is the normal
  tagging, which is why "avoid ferries" was a DATA gap and not a preference gap: there was nothing
  in the graph to avoid. `route=ferry` takes first refusal over any `highway` the way also carries
  (a slipway). `duration` is `hh:mm:ss` shortened to `hh:mm` or **a bare count of MINUTES** — "20"
  and "0:20" are the same crossing, which is the one trap in the tag.
- `GEO_east_of_degrees(a, b)` means "**a** is east of **b**".
- The scale ladder gives the **current product first refusal** before crossing to another product.

---

## 4. Per-module recipe

1. Create `port/<module>/CMakeLists.txt` compiling sources **in place** from `fvw_core/<module>/`.
2. Uncomment the module's `add_subdirectory` in the root `CMakeLists.txt`.
3. `cmake -B build && cmake --build build` — fix **compiler-first**, don't read files speculatively.
   Typical: drop `stdafx.h`/afx headers, `CString`→`std::string`, TCHAR removal, `__int64`→`int64_t`,
   case-sensitive `#include` paths, `_stricmp` etc. via `fv_compat.h`.
4. Repeated mechanical transforms → a script in `port/tools/`.
5. gtests pinning known-good values in `port/<module>/test/`; `ctest --test-dir build`.
6. **Update this ledger** (move the item out of §2, add a one-line row to the archive) and commit:
   `port(<module>): compiles+tests on macOS`.

**Session protocol**: one module per session · never re-explore the repo broadly · no subagents ·
commit and update this file before ending.

---

## 5. Where to look in the archive

`port/PORTING-ARCHIVE.md` holds the completed module table (rows 1–14u, R1–R3c, E1–E8, F1–F3, S1,
O1–O5e, K1, T1, T2, M1, A1–A3) and the dated decision log. Useful entry points:

- **Overlay drawing (`fvkit/geo`)** — 2026-08-13 (**G1**: the geodesy that was already in the
  tree, the geographic clip as the thing worth porting, the screen-derived step size, the two
  bit-faithful quirks in the rhumb clipper, the antimeridian break that replaces FalconView's
  wrapped second segment, and the two test ORACLES that were wrong before the code was —
  great-circle bearing used to check a rhumb line, and "bows poleward" asserted on a segment
  whose vertex lies past its own endpoint).

- **App layer (`fv::app`)** — 2026-08-13 (**A6**: the acceptance test — a capability as a method
  you defined, the aliasing shared_ptr a Python factory needs, the editor proxy that is unwrapped
  on the way back, the first C++ file overlay and why its document is SQLite, the sample's
  ambiguity as a pinned data property, and the shell that had to exist before the constructor
  finished),
  2026-08-13 (**A5**: draw order as the one definition of who is on
  screen and picking as its reverse, the hit id that could not be a packing and became a stable
  handle, hover notified only on a change, and the two verbs the capability class names forced
  to be renamed),
  2026-08-13 (**A4**: the dance as two directions with only one of
  them a call, the observer that makes the other one hold from anywhere, the per-type editor
  instance that is cached so tool state survives, the focus bracket asserted as an ORDER rather
  than a pair of counters, and why the release moved out of `OverlaySession::Close`),
  2026-08-12 (**A1**: the string TypeId, the optional `FileTypeDesc` as
  the whole static-vs-file distinction, capabilities by accessor rather than `dynamic_cast` and why
  the trampoline forces it, and the two headers that had to land early because a `unique_ptr` cannot
  be returned through an incomplete type; **A2**: the stack grown in place and inert without a
  registry, the top-most band as a second draw pass rather than a stack position, capture exclusive
  for the mouse and first-refusal for keys, and the one-delivery-per-event deviation from
  `C_ovl_mgr::select`; **A3**: R1 as the thing that makes the layer testable at all, cancel-is-not-
  failure and why it propagates, the flows that refuse to leave a half-built document in the stack,
  the `save_format_index` the plan did not have, configuration in memory because S1 says the
  settings file is the user's, and `Exit`'s snapshot-before-close bug).

- **Vector seam design** — 2026-07-23 (V5a/V5b), 2026-07-27 (E3b placer, E3c extraction).
- **Rule layer** — 2026-07-26 (R2: predicate AST, ScaleBand, ViewingGroup, `ResolvedPlan`).
- **Perf** — 2026-07-28 (R3a retained scene / R3b "the plan was aimed at the wrong costs" + symbol-atlas deferral),
  2026-08-08 (R3c: the ledger's own build command was -O0; the DNC parsed-feature cache; the
  `FeatureBatch` measured away; the geographic pattern anchor).
- **ENC/S-52** — 2026-07-25 (E1 base-edition), 2026-07-27 (E2 PresLib inventory, E3a/E3b CS procedures
  and their reductions), 2026-07-28 (E5 defects vs a published chart, E6 pixmaps, F3 usage bands),
  2026-08-12 (**E8**: the text band, and the four TX/TE placement parameters decoded off the
  library's own rows),
  2026-08-11 (**E7**: the library's 0.32 mm symbol grid measured out of its own dual-form symbols,
  bearing-vs-screen rotation at the SY seam, SNDFRM02's digit layout read off the bitmap pivots).
- **DNC/GeoSym** — 2026-07-20 (V1 reader + two CString silent-corruption bugs), 2026-07-21 (V3),
  2026-07-24 (areas, map-scale-vs-feature-zoom, WVS root cause), 2026-07-25 (F1 fills), 2026-07-27 (F2 flip).
- **Display/projection** — 2026-07-24 (physical scale + the aspect-ratio bug).
- **Settings** — 2026-07-28 (S1: INI over JSON, read-only, three failure modes),
  2026-08-12 (**M1**: the mariner's depth numbers read off GeoSym's own tables, and data
  families as named groups of rule selectors).
- **OSM** — 2026-08-04 (O1: dependencies, bounds, zoom clamp, tile budget, two geometry deviations),
  2026-08-08 (O2: the one zoom↔scale relation, the declared style subset, style-layer draw order).
- **Input** — 2026-08-04 (K1: VK codes over X11 keysyms, tk binding semantics).
- **Routing** — 2026-08-10 (O4 graph + bidirectional Dijkstra, O4b profiles/access, O5a turn
  restrictions and the split-state search), 2026-08-11 (O5d ordered stops: the seed, the barred
  turn, and the parallel-arc rule the two searches disagreed over),
  2026-08-12 (**O5e**: the ferry as a class rather than a flag, `duration` as the crossing's clock,
  the toll bit, and the one cost path that had been bypassing `ProfileSeconds` since O5c).
  **`O5c` has no archive row** — the session committed the code (`ea3aed90`) and updated §1 but
  never wrote one. §1's "Cost rules (O5c)" paragraph and that commit are the record.
- **COM severing pattern** — 2026-07-11 (GeoTIFF `IDatumConvert` is the worked example).
