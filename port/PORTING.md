# FalconView Cross-Platform Port — Working Ledger

**Read this first in every session. Do not re-explore the repo.**
This file is **open work, plus the map and the rules a session needs to start**. The build
narrative — how each finished piece came to be, and the "things worth not re-deriving" that came
with it — lives in **`port/PORTING-ARCHIVE.md`**. Go there when a line here names an archive row
or an area; §5 is the index. Condensed 2026-08-16: §1 was 480 lines of finished-work narrative and
is now an inventory, and every struck-through "done" item in §2 moved to the archive's
**"Condensed out of the working ledger — 2026-08-16"** section, verbatim.

Full strategy:
`/Users/chrisbailey/.claude/plans/this-project-is-extreamly-enumerated-marshmallow.md`.

**Plans** (a `-COMPLETE` suffix means: do not read it unless a bug turns up in that code):
| Doc | Covers | State |
|---|---|---|
| `port/vpf-geosym-plan.md` | vector/symbology design — §5 the cross-product middle layer, §7 ENC | products built; V7 CoreGraphics backend, V8 atlas and WVS still open |
| `port/fvkit-app-plan-COMPLETE.md` | overlay types, stack, editors, pick, shell, session | **A1–A6 all built** |
| `port/fvkit-draw-plan-COMPLETE.md` | geographic lines, symbol libraries, GeoDraw, render state | **G1–G4 built**; G5 (SVG) optional and never started, dimming deferred (§2b) |
| `port/fvkit-nav-plan.md` | moving map / navigation | **MM1–MM7 all built** (MM7 is the app side); MM5b (Viterbi) optional and never started |
| `port/fvkit-contracts.md` | D1–D6: ownership, geo, Status, pixel, naming, adapters | standing contract, always binding |

Other docs: `port/bindings/pyfvw/README.md` (Python user guide),
`port/bindings/pyfvw/ICD-MAPPING.md`, `port/peregrine.ini.sample` (every settings key with its
measured effect), `port/families/{dnc,enc,osm}-families.json` (the data-family starters, M1),
`port/Osm/styles/style-readme.md` (the supported GL-style subset).

```sh
cmake -B build && cmake --build build -j && ctest --test-dir build   # 1369 as of 2026-08-18, ALL GREEN.
# This is the number ctest RUNS. `ctest -N` says 1386 because it also lists the 17 disabled
# GeoTrans tests — do not update this line from -N, the two counts are 17 apart forever.
# THIS LINE DRIFTS: trust the archive row of the LAST session if the two disagree.
# The default CMake build type is NOT optimised — configure -DCMAKE_BUILD_TYPE=Release before
# taking any performance measurement (R3c's real finding).
```

---

## 1. What exists today (so you don't go looking)

An inventory: what the layer is, where it lives, and only the invariants that constrain NEW work.
The reasoning behind each is in the archive row named in §5.

**Geo/math** — `port/{geoid,geo3,geo_tool,geotrans,MapScaleUtil,MapSeriesStringConverter}`, all
tested. GEOTRANS 3.3 is **frozen** (pinned bit-faithful results).

**Raster products** — each an `IRasterSource` + enumerator, self-registered in the format registry
(7 builtin: `geotiff`, `cadrg`, `tiros`, `dted`, `dted-shaded`, `gpkg`, plus the vector products):
`port/{ImageLib,ImageLibCore,CadrgDecoder,CadrgMapServer,GeoTIFFMapServer,TirosMapServer,DtedMapServer,DtedShadedRenderer}`.

### 1a. FvKit core (`port/include/fvkit/`, impl `port/fvkit/`)

`geo.h` (incl. `SurfacePoint`), `raster.h`, `engine.h` (MapEngine), `catalog/` (SQLite+R-tree),
`canvas/` (ICanvas + CpuCanvas), `overlay/` (SPI + manager + grid + `KeyEvent`),
`store/tile_pack.h` (GeoPackage), `settings.h` (`fv::Settings`, INI, the registry replacement).

**`proj.h` — the projection, and it ROTATES** (PR1–PR3, 2026-08-15). Equal-arc, plus
`SetPhysicalScale` and `SetRotation` — a clockwise turn about the surface centre, carried by both
transforms and by `VmapBounds`, which returns the box of the TURNED viewport. **Rotation 0 is the
byte-exact identity, because the arithmetic is GATED and not because a matrix happens to be the
identity** — every raster and vector golden depends on that. Both draw paths honour it: the vector
path since PR2, the raster path since PR3 where `MapEngine::CompositeRow` gates on the rotation and
a turned frame is resampled through the turned projection and MASKED to its own edges. Three
numbers to carry forward: a turned viewport's query box grows to `(w cos + h sin)` by
`(w sin + h cos)` — `(w+h)/√2` on both axes at 45° — the retained scene needs **no rotation in its
cache key** because R3a made its ink geographic, and the **only** angle in the drawing stack that
had to be taught the rotation is a point symbol's north-up `PointSymbolStyle::rotation_deg`
(`SymbolAngleOnChart`, two call sites). `DrawSymbolAtPixel` deliberately does NOT apply it: a pixel
anchor's angle is already a screen angle. Bound as `MapProjection.set_rotation`/`.rotation` and
`MapEngine.set_rotation`.

**The vector seam** — `vector/vector.h` (IVectorSource, VectorFeature, FeatureRef/Describe),
`style.h` (IStyleEngine, path/area pattern styles), `rules.h` (predicate AST, ScaleBand,
ViewingGroup), `families.h` (named groups of features over rule selectors, JSON),
`mariner.h` (`MarinerSettings`, shared by DNC and ENC — it belongs to the ENGINE, where the epoch
that invalidates a retained scene already lives; each product keeps its OWN defaults, which are what
its goldens were pinned over), `lookup_engine.h` (`LookupTableStyleEngine` — the shared engine;
GeoSym, S-52 and OSM are *loaders* over it), `renderer.h` (VectorRenderer + the three placers
`PlaceAlongPath`/`PlaceOverArea`/`PlaceTextAlongPath`), `text_draw.h` (halo offsets, glyph
advances, `AlongPathAnchorShift`), `symbol_draw.h` (`DrawSymbolAt`/`ResolveSymbol`/… and the
optional colour tint), `scene.h` (retained VectorScene), `pick.h` (PickIndex — hit-tests the
emitted ink). `LabelStyle` carries placement, spacing, max angle, offset, a size in pixels OR
ground metres, `halign`/`valign` (E8), `halo_width`/`halo_color` (T2, a stamped halo — nothing new
from `ICanvas`) and `along_anchor` (`kBaseline` = 0.0 = the pinned goldens, `kCenter` = half the
cap height, `kCapHeightEm = 0.72` measured over the port's fonts).

**Geographic contours** (G1, `geo/contour.h`) — `IGeoContour` as a pull iterator with
`SimpleGeoLine`, `GreatCircle`, `RhumbLine`, `GeoCircle`, `GeoEllipse`, `GeoArc` and
`PolylineContour`; `MakeGeoLine` is the factory, `BuildGeoPath` projects any contour into surface
sub-paths, `AtBreak()` says a sub-path ended (a clipped-away leg). **The property that matters: it
CLIPS IN GEOGRAPHIC SPACE BEFORE DENSIFYING**, so an intercontinental arc on a harbour map costs a
search, not a walk — do not "simplify" that away. Step size comes from the projection's dpp
(~20-px chords, `/5` above ±70°), so vertex count tracks the SCREEN. **The pull iterator is
deliberately NOT bound**; every contour is one `pyfvw.geo.*_path` call that builds and projects in
a single crossing.

**Symbol libraries** (G2, `symbol/`) — `ISymbolLibrary` is exactly the three methods `IStyleEngine`
already had, so `IStyleEngine : public ISymbolLibrary` and every style engine IS a symbol library.
Four implementations: `PngSymbolLibrary` (loose files with pivot sidecars and `@2x` twins, OR a
sprite sheet + MapLibre `sprite.json`; lazy), `CgmSymbolLibrary` (GeoSym's ~1500 `.cgm`, kept in
`port/GeoSymServer/` because fvkit never links GeoSym), `BuiltinSymbolLibrary` (13 literals — six
PointOverlay shapes, five line decorations authored **+x ALONG the line, +y to its LEFT**, a north
arrow, a crosshair; `kBuiltinSymbolNominalPx` = 9.0) and `CompositeSymbolLibrary` (ordered; symbol
and pixmap resolve INDEPENDENTLY, and its unit is ONE number for the whole composite — a stated
limitation). Two rules: **a pivot lives in TILE pixels** (`SymbolPixmap::pixel_ratio` divides the
scale so a 2x tile comes out the same SIZE as its 1x twin; it defaults to 1.0 and dividing by 1.0
is the identity, which is why no golden moved), and **a library re-`Open` REPLACES**.

**Overlay drawing** (G3, `canvas/geo_draw.h`) — `GeoDraw(proj, canvas, symbols)` is **the surface
an overlay calls**: `DrawGeoLine`/`Polyline`/`Circle`/`Ellipse`/`Arc`/`Contour`/`SurfacePath`,
`DrawSymbol`(`AtPixel`), `DrawLabel`(`AtPixel`/`AlongPath`). It composes G1, G2 and the placers and
needs **no new `ICanvas` op**. `GeoLineStyle` is `{casing, stroke, pattern}` — the casing is the
halo a line wants, drawn first, wider, and **following the PATTERN when there is one**.
`PresetGeoLine` gives ten names (`solid dash long-dash dot dash-dot railroad arrow tick notch
feba`) over `BuiltinSymbolLibrary` — that is how `LineSegmentRenderer.cpp`'s 913 lines and 15
classes collapse — and **solid deliberately returns an INVALID pattern**, as does an unknown name,
which falls back to a plain line rather than to nothing. Picking is **OFF by default here** (the
opposite of `VectorRenderer`). `symbol_dpi_scale` defaults to 1.0 and is the stated way out of the
symbol-DPI defect (§2b). Bound as `pyfvw.draw` + `pyfvw.symbol`.

**Render state** (G4) — `RenderState{kNormal,kHighlighted}` plus `SetState` /
`SetHighlight(colour, width_px)` (default FalconView's selection yellow at 3 px): **what a draw
MEANS, as against how it is styled**, so a highlighted thing is still drawn as ITSELF. The
mechanism is T2's stamped halo reused verbatim (one function, so a highlight and a text halo cannot
drift apart). Four rules: a LINE takes one wider stroke, not eight offset ones (same picture,
an eighth of the cost) and it goes under the casing; a highlight **never enters the pick index and
never counts as a draw** (`AsHighlightPass`), or a selected feature would become a bigger target;
the highlight goes on a marker's **outermost stamp only**, and never on the label; and the stamp's
growth under a highlight is **capped at 2x**.

**Moving map** (`nav/`, MM1–MM4) —
- `position.h`/`scripted_source.h`/`heading.h` (MM1): `PositionFix` where **every field carries its
  own validity** (the -1000.0 sentinels do not port) with `Merge` for two sentences of one epoch;
  `FixQueue` (mutex + drain-on-tick) because a source delivers on whatever thread it likes, and a
  full queue **drops the OLDEST and counts it**; `ScriptedSource` with an **injectable clock** and
  no thread, so every timing assertion is an equality; `BuildScriptedTrack` takes a **polyline**, so
  fvkit still does not link `port/Routing`; `HeadingResolver` derives heading in **SCREEN** space
  (deg-per-pixel scaled) — on an equal-arc map a true bearing of 045 does not draw at 45°.
- `camera.h` (MM2): `MovingMapCamera` + `ComputeApron`/`DeltaXy{Discrete,Continuous,TrackUp}`,
  ported verbatim from `gps_draw.cpp`. **The camera never touches the engine** — `Update` returns a
  `CameraTarget` and the shell applies it. **The apron is built from where the ship was DRAWN and
  tested against where it has just moved to**, so the seam is two calls (`RecomputeApron` per frame,
  `Update` per fix); rebuilding it around the new position freezes the map forever. The track-up
  offset is `d_x·right-of-course + d_y·ahead` in a Y-DOWN surface and **must not be "fixed" into a
  rotation matrix**. `RecomputeApron(0,0,0,0)` is NOT empty (the original's `+ 1` exclusive edge
  yields a 1×1 box) — `ClearApron` is what an overlay with no fix calls.
- `camera_slew.h` (MM3): `CameraSlew` between the camera and the engine. **No Windows original** —
  FalconView jumps — so every rule is a decision: duration 0 IS the jump and the rate caps must not
  resurrect it; the interpolation is in **GEO**, the frame that survives the projection being
  re-centred by this very animation (longitude the short way, pinned across the antimeridian); the
  caps **extend the duration and never clip the motion** (1200 px/s, 120 deg/s); centre and rotation
  share one duration; a new target **retargets in flight and never queues**, which restarts the ease.
- `overlay/moving_map_overlay.h` (MM4): `fv::MovingMapOverlay` holds the feed, the camera and the
  slew; `fv.movingmap` is a **static built-in type**, deliberately not restored at startup. **The
  camera lives here because the overlay is the only object that is both drawn per frame and fed
  fixes.** `Tick` answers and applies NOTHING. **Every queued fix reaches the heading resolver; only
  the last reaches the camera.** Setting the modes FORCES a recentre. The heading is **NEGATED** into
  `PointSymbolStyle::rotation_deg` (that field turns a symbol counter-clockwise; S-52 negates at its
  own seam for the same reason). `screen_angle_deg()` IS the camera's `point_angle` and ADDS the map
  rotation. A shell declares `SetRotationSupported` (PythonView says yes since PR3); `Tick` **adopts
  `proj.Rotation()`** before using it, so the projection is the one place the applied rotation is
  true. Bound whole as `pyfvw.nav`, and `nav.PositionSource` is subclassable from Python.
- `road_snap.h` (MM5): `RoadSnapper` puts the ship on the road it is on. **The road network is behind
  `IRoadNetwork`** — one method, "which roads are near this point, PROJECTED" — because fvkit still
  does not link `port/Routing`; the port's implementation is `fv::routing::RoadGraphNetwork`
  (`port/Routing/fv_road_network.h`), and **no network is a supported state** (the feature is off).
  `ProjectOntoSegment` is **inline in the header** so an adapter needs fvkit's headers and not its
  library. **Every score term is METRES** (distance + a heading-alignment penalty − a stay bonus for
  the previous arc − a smaller one for an arc CONNECTED to it), the radius comes from the fix's own
  HDOP floored and capped, and below `hold_speed_mps` the arc is **HELD — an infinite stay bonus, not
  a branch**, so a ship that has drifted off it still lets go. Output is a decoration: **the raw fix
  is never destroyed**, `Applied()` is the fix to consume. In `MovingMapOverlay` the snapper stands
  **BEFORE the heading resolver** (resolving first would derive every heading from the scatter the
  snap removes; and the road's bearing then arrives as a *reported* heading, which heading.h already
  prefers) and `snap_min_confidence` (0.25) is what the overlay declines a guess with. Measured on a
  noisy Kiawah track: across-track error removed, **along-track error is not** — that residual is why
  MM5b (Viterbi) is still a real option and not a formality.
- `nmea.h` / `line_transport.h` / `gpx.h` (MM6): **a real feed, and two recorded ones**.
  - **`nmea.h`** ports `MovingMapOverlay/nmea.cpp` field index for field index. Three original
    quirks are PRESERVED and labelled Q1–Q3 in the header: a sentence one field short is
    **rejected whole** (RMC 11, GGA 14, GLL 6, VTG 8), GGA **withholds altitude at ≤3 satellites**,
    and RMC's magnetic heading is **derived from the variation** (west adds, east subtracts, one
    wrap). Two things deliberately DIFFER: the −1000.0 sentinels do not port (position.h rule 1),
    and **ANY TALKER is accepted, not just `$GP`** — `strncmp(s,"$GPRMC",6)` rejects every sentence
    a modern phone sends (`$GNRMC`), and MM6 exists so a phone can drive the map. The build side is
    kept for the recorder, with one deviation: the checksum is **zero-padded everywhere**, where the
    original's `build_RMC`/`build_VTG` wrote `"%2hX"` and emitted `"* 5"` below 0x10 (`build_GGA` had
    it right, and the port takes GGA's form). A built position round-trips to **~1 m**, because
    minutes go out with three decimals — that is the wire format, not the parse.
  - **`NmeaFixAssembler`** is where MM1's `Merge` finally pays: sentences sharing a time of day
    become ONE fix, and **the date comes from the last RMC** (or `SetDateHint`) — until one has
    arrived a fix has `has_time` false rather than a 1970 stamp. Closing on the epoch change costs
    **one epoch of latency**, so `SetEmitPerSentence(true)` is the live-feed mode; `pending_emitted_`
    is what stops the two modes from delivering one instant twice.
  - **`line_transport.h` is a SEPARATE seam from the parser**, so nmea.h never learns where bytes
    come from and every transport is testable without a parser. `ReadLine` **never blocks**
    (`kAgain` between sentences), **`kEnd` is not an error**, and **framing is ONE implementation**
    — `LineBuffer`, which holds a partial read, drops `\r`, skips blanks and **caps a line at 512
    bytes** so a hostile stream is bounded. Four transports: `StringLineTransport` (the test
    double), `FileLineTransport` (with `follow`, which **tracks its own offset and seeks** — a bare
    `clear()` does not see appended bytes — and which **never hands over an unterminated tail**,
    since in a growing file that is a line still being written), `TcpLineTransport` (this is what
    "phone GPS" means: GPS2IP-class apps serve NMEA over TCP; **non-blocking BEFORE connect**, or an
    asleep phone hangs the shell's tick) and `UdpLineTransport`. **Windows is guarded, not tested.**
    Serial waits for a device; CoreLocation stays out until NMEA-over-TCP proves insufficient.
  - **`gpx.h` has NO WINDOWS ORIGINAL** (FalconView has none; GPlan has three, all C#) — Chris asked
    for it beside the NMEA work, and it earns its place because **GPX is what a watch, a phone or a
    bike computer actually hands you** while a raw NMEA log is what a receiver hands a program.
    GPX 1.0 and 1.1, tracks/segments/waypoints/routes, over **expat** (a GPX file comes off the open
    internet, so it is never parsed by hand; an entity declaration **stops the parse at the DTD**).
    Three rules: a track point's `<time>` **is** the fix's time; **speed is derived and heading is
    not** (HeadingResolver already derives one, in SCREEN space, and a true bearing written into
    `has_true_heading` would look REPORTED and quietly win — `derive_true_heading` is there for a
    caller who wants it); and `<ele>` **is read as MSL**, the pragmatic reading every GPX consumer
    makes, said out loud rather than silently. A `<trkseg>` boundary is KEPT (a straight line across
    a lunch break is not a track) and `split_gap_s` makes one where a device forgot to.
  - **The whole of MM6 is bound as of MM7** (`pyfvw.nav`), under two module rules: Python never sees
    a `Status`, so `read_gpx_file` / `read_nmea_log` / `LineTransport.open` **raise**; and an
    out-parameter becomes a return or a `None` (`parse_nmea_sentence`, `add_line`, `flush`,
    `next_line`, `parse_iso8601_utc`), with `read_line` answering the tuple `(LineResult, text)`.
    **`ILineTransport` has NO Python trampoline on purpose** — a Python object holding bytes uses
    `StringLineTransport.add_data`, or subclasses `PositionSourceBase` and delivers whole fixes.
  - **BOTH recorded readers arrive at ONE seam**: `ReadGpxFile`/`ReadNmeaLog` → `FlattenGpxFixes` →
    **`BuildScriptedTrackFromFixes`** (scripted_source.h) → `ScriptedSource` → `MovingMapOverlay`.
    The fixes' own stamps are the schedule, so a ride replays at the speed it was ridden and **the
    moving map does not learn a second kind of track**. `NmeaLineSource` is the LIVE path, and it
    has no thread for MM1's reasons; `max_lines_per_poll` (256) is what stops a file transport
    replaying three hours inside one frame.

### 1b. App layer (`fv::app`, `port/include/fvkit/app/` + `port/fvkit/app/`, A1–A6 — plan DONE)

`type_registry.h` (`TypeId` is a STRING; `OverlayTypeDesc` carries the factory as a
`std::function` and an **`std::optional<FileTypeDesc>` — that optional IS the static-vs-file
distinction**; `RegisterBuiltinOverlayTypes`), `capabilities.h` (`Persistence`, `HitTest`, `SnapTo`,
`ContextMenu`, `RoutingOverrides`, `EditTarget`), `shell.h`, `editor.h`, `session.h`, `pick.h`,
`vector_hit_test.h`. The layer is `fv::app` and D5's "no nested namespace" does not apply to it.

- **Capabilities are found by ACCESSOR, never `dynamic_cast`** — `fv::Overlay` has six `As*()`
  returning nullptr by default over forward-declared types, so L4 keeps no app-layer dependency.
- **A2 is the STACK and it is `fv::OverlayManager` grown in place** (`fvkit/overlay/manager.h` IS the
  plan's `stack.h`): `StackObserver`, a current overlay, `MoveAbove`/`Below`/`ToBottom`/`Reorder`
  (a total permutation, rejected whole if it is not one), `FirstOfType`/`OfType`/`FindByFileSpec`,
  declutter, mouse capture. `SetTypeRegistry` is optional and **with no registry every A2 addition
  is inert**. manager.h still includes nothing from `fvkit/app`.
- **A3 is the SHELL SEAM** — `AppShell` is the complete inventory of UI the layer needs (five
  decisions, six presentation calls) and `FlowResult{kDone,kCanceled,kFailed}`, where **a cancel is
  the USER's answer and propagates** while a failure is reported and left on `last_error()`.
  `OverlaySession` holds the flows; open dedups on **(TypeId, file spec)**. R1 pays for itself:
  `app/test/fake_shell.h` makes all 56 tests plain unit tests with no dialog and no pump.
- **A4 is the MODE DANCE**, and it runs in two directions of which **only one is a call**:
  `SetMode` makes the current overlay match the mode, while "the mode follows the current overlay"
  and "closing the edited overlay falls to the next OF THAT TYPE" are **observed** through a private
  `StackObserver`. The editor instance is per TYPE and cached, so tool state survives. The mutual
  dependency with `OverlaySession` is wired after construction on both sides and both are optional.
- **A5 is PICKING, an aggregation rather than FalconView's first-hit-wins veto.** **Who is asked has
  ONE implementation and it is the DRAW order reversed** — `OverlayManager::DrawOrder()`, with
  `DrawAll` written over it. **The hit id is a HANDLE, not a packing** (a `FeatureRef` is 4×int32,
  `HitItem::feature` is one uint64_t); `VectorHitTest` mints it and `RefFor()` translates back.
  Hover notifies only on a CHANGE, and `kAskWhenAmbiguous` degrades to `kTopMost` on a hover.
- **A6 is the ADOPTION**: `pyfvw.app` binds the layer, PythonView IS an `AppShell`, and
  `fv::PointOverlay` is the first C++ FILE overlay. **A capability is a method you DEFINED** (the
  Python trampoline inherits every capability and answers each accessor from what the subclass
  defines, cached per instance); **a Python overlay made by a FACTORY needs an aliasing
  `shared_ptr`** (`OverlayFromPython`) or it draws and silently answers no picks; **an editor is a
  duck-typed PROXY**, unwrapped wherever the API hands one back.
- **`fv::PointOverlay`** reads a `.fvpoints` **SQLite** document — a real schema somebody else can
  write, which is what makes evolving the dataset INSERTs rather than a parser. **Schema 2 put the
  ARTWORK IN THE DOCUMENT**: a `symbols` table of PNG blobs, separate because many points share one
  symbol, so the file opens with its symbology on a machine that has never seen the icon set. A
  point draws as a BADGE (its shape in its colour, tile centred on top) because icon sets are
  black-on-transparent; alpha 0 asks for the bare icon. `EmbeddedSymbolLibrary` is the third form of
  `ISymbolLibrary` after directory and sheet. **A schema-1 document still opens** and is saved
  forward.

### 1c. Vector products, all three on that one seam

- **DNC/VPF** — `port/VpfMapServer/` (reader, vector source incl. areas, VDT identify) +
  `port/GeoSymServer/` (rule tables, CGM symbols, `GeoSymStyleEngine`).
- **ENC/S-57** — `port/Enc/` (ISO 8211, S57Cell, Appendix A catalogue, S-52 PresLib,
  `S52StyleEngine`, raster symbol sheet, enumerator/registration). **Text is its own band** (E8):
  `kS52PrioTextBase + the object's priority`.
- **OSM** — `port/Osm/` (MBTiles + MVT + `OsmVectorSource` + `OsmStyleEngine`, a MapLibre style-JSON
  loader over `LookupTableStyleEngine`; reference style `port/Osm/styles/peregrine-osm.json`,
  documented subset in `styles/style-readme.md`; sprites via `PngSymbolLibrary`'s sheet form, where
  `icon-image` and `fill-pattern` are `{tag}` TOKEN TEMPLATES and a pattern's spacing and its stamp
  scale must carry the SAME factors).

### 1d. Routing (`port/Routing/`, O4–O5e)

A routable road graph built **OFFLINE from a raw `.osm`/`.osm.pbf` extract** — never from the MVT
pyramid, which is simplified, tile-clipped and has no node identity. `fv_osm_reader.h` (expat XML +
protozero/zlib PBF behind one `OsmSink`, nodes/ways/**relations**), `fv_road_graph.h` (noded graph,
`.fvroad` **v2**, grid nearest-node index, turn restrictions), `fv_router.h` (bidirectional
Dijkstra, the unidirectional one kept as the tests' oracle). CLI `fvgraph build|info|route`.

- **Profiles** (O4b) — driving on the posted clock, walking, cycling — gated by **per-mode access
  bits carried ON the arc**, so one general graph answers all three.
- **Turn restrictions** (O5a) — resolved onto `(via_node, from_arc, to_arc)`; the searches label
  **states**, so only the junctions a restriction names are split and an unrestricted graph costs
  what it did in O4.
- **`access=private` is priced, never deleted** (O5b) — on a gated community it IS the street network.
- **Cost rules** (O5c) — every weight, speed and penalty in `rules/route-weights.json`, reread by
  `RouteRulesFile` on an mtime+size poll so weights are tuned with the app running; a bad file is
  rejected whole and the loaded rules stay in force. `RouteRules::Builtin()` reproduces O5b exactly.
- **Ordered stops** (O5d) — `RouteVia` is ONE route through the waypoints: a leg is seeded with the
  arc the previous one arrived along, so the turn machinery binds signage at the stop for free and a
  U-turn out of a stop is expressed as a barred turn.
- **Ferries and tolls** (O5e) — `route=ferry` enters as **`RoadClass::kFerry`**, a class and not a
  flag, because the speed differs (the crossing's own `duration` over its own length, returned by
  `ProfileSeconds` **whatever profile is asking** — you do not walk a ferry). `toll=yes` is the
  opposite shape and is a bit. Both are avoided through `RouteOptions::{toll_penalty,ferry_penalty}`,
  the ONLY profile-backed settings the router reads from the options rather than the profile.

### 1e. Apps and bindings

`port/bindings/pyfvw` (`pyfvw.{vector,catalog,engine,overlay,canvas,routing,draw,symbol,geo,nav,app}`
+ `pyfvw.Settings`), `port/apps/PythonView.py` (the tk application, and an `AppShell` — since MM7 it opens all three
moving-map feeds: File > Open Track for a `.gpx` or an NMEA log, Overlays > Moving Map Modes >
Connect NMEA Feed for a live one, and Use The Demo Feed to come back. The feed is a TUPLE
(`("demo",)`, `("track", path)`, `("tcp", host, port)`, `("udp", port)`) dispatched at one
`set_source`; a track file wins over a live host at startup; **an empty host in the dialog is the
UDP listener**, which is the one field the two shapes of phone app differ by),
`port/apps/route.py` (the route overlay: a document, a pick target and an editor), plus the
`fvrender`, `fvpack` and `fvgraph` CLIs. Overlay types: `app.crosshair` (static, top-most, restored
at startup) and `app.coverage` from the app; `fv.grid`, `fv.points`, `fv.movingmap` and
PythonView's `fv.route` from the port.

### 1f. Test data (`testdata/` on disk, git-ignored, all present — see §2d on the spelling)

dted · geotiff DOQs · rpf CADRG · tiros3 · `vpf/dnc17` · `VPF 2/WVSPLUS` ·
`GeoSymbol/{SymAssign,Graphics}` (DataDir = `TestData`) and `GeoSymbol/makiPng` ·
`OSM/map*.osm` (adjacent, OVERLAPPING Kiawah Island exports — **enumerate them, never name them**;
refreshed 2026-08-17, the third cut) · `OSM/kiawah.fvroad` (a BUILD ARTIFACT, read by the app and by
no test — and the 2026-08-17 one is `--ignore-access`, see §2d) ·
`OSM/us-south-260728.osm.pbf` (4 GB raw extract) ·
`kiawah_cycle.gpx` (MM6's GPX fixture — a real 28-minute ride on Kiawah, **1705 points at 1 Hz**,
1704 s, no gaps and no repeated stamps, exported from a Garmin FIT file; its elevation runs 9.4 m
down to **−6.2 m**, which is what makes an unsigned or clamped `<ele>` show up) ·
`OSM/mbtiles/us-south.mbtiles` (4.4 GB, 4,872,934 tiles, **re-cut 2026-08-17** — ocean merged and now
running east to the Greenwich meridian, 17 layers with `man_made` new; the rebuild recipe is in the
archive and a rebuild without the coastline shapefile silently loses the sea) ·
`enc/` (the **8** Charleston cells bands 2–5 the ENC goldens are pinned over, the other **815** in
the sibling `TestData/enc-archive/` — see §2d — plus `chartsymbols.xml`, `s57objectclasses.csv`,
`s57attributes.csv`, `s57expectedinput.csv`, `rastersymbols-{day,dusk,dark}.png`).

---

## 2. Open work

### 2a. Active track — next sessions, in order

| # | Session | What it is |
|---|---------|-----------|
| **MM5b** | Moving map, the one optional slice left | **MM1–MM7 are ALL built** — the core reads three feeds and since MM7 the app opens all of them (§1a, §1e). What is left is optional and was optional when it was written down: **MM5b**, an HMM/Viterbi match over a sliding window with network-distance transition costs (OSRM's shape), which MM5 measured exactly the value of — the projection removes the across-track error and leaves the along-track error, 9.32 m in and 5.96 m out over 1295 fixes. The seam is ready (`RoadCandidate` carries `along_m` and the arc's ends) and nothing is shaped around its absence. Also still waiting, on things rather than on work: a **serial** transport (for a device to test against) and **CoreLocation** (for NMEA-over-TCP to prove insufficient). No breadcrumb trail — that is a later plan. |
| **O5** | A route the user can steer — what is left | **Via-way restrictions**, which O5a recognises, counts and deliberately does not apply: a search state carries the arc it arrived on and nothing further back, so these need either a longer state or the via arcs edge-expanded at build time. Smaller, now that the bike profile makes it visible: **steps cost nothing extra** beyond being excluded outright, and there is **no elevation term at all**. A ferry's **timetable** is likewise unmodelled — the crossing costs its `duration`, never the wait for the next sailing. |
| **R3d** | Perf, fourth slice — *if anything still needs it* | R3c re-measured the whole profile at **-O2** (the default build type was the real finding): **cold 49 ms = query 29 + style 11 + draw 8; a retained pan 3.4 ms; a pan out of the retained area 11.6 ms = query 0.7 + style 6.9 + draw 3.5**. The cold 29 ms is the one-time parse of a whole DNC library and the 6.9 ms is **GeoSym styling** — so the next target, if a user still feels one, is `LookupTableStyleEngine`, not the query and not the rasterizer. **Do not start this without a fresh profile**: the plan on this line has been wrong about where the time was three times running. The columnar `FeatureBatch` is a **measured non-goal**. |

### 2b. Product gaps

- **A turned chart has no USER-FACING gesture and no better sampling** (the two things PR1–PR3
  deliberately did not do). The moving map is the only thing that writes `self.rotation`, so a user
  who wants a turned chart has to fly one; the shell is one key binding and one assignment away, and
  the rest of the app is already rotation-aware (both draw paths, the pan delta, every pick). The
  turned blit is **nearest-neighbour exactly like the straight one**, so an odd angle is as aliased
  as a straight chart — the difference is the aliasing is on a diagonal, where a reader notices it.
  Bilinear would be one loop in `CompositeRowTurned`; applying it to the straight path too would
  move every pinned raster golden, which is the reason not to do it in the rotation session.
  Two smaller PR3 findings, documented at their sites: **a frame edge on a half-pixel has its tie
  broken differently by the two paths** (pinned to within 1 px rather than removed with an epsilon),
  and **the affine fit is now over a box up to √2 larger**, so PROJECTED imagery (the UTM DOQs)
  carries a slightly larger residual when turned. Equal-arc products are exact either way.
- **The point overlay has no EDITOR, so a `.fvpoints` document is read-mostly in the app** (A6).
  The C++ side has everything one would need — `AddPoint`/`RemovePoint`/`SetSelected`, a dirty flag,
  a save that replaces the table in one transaction — and all of it is bound. What is missing is an
  `OverlayEditor` for `fv.points` and the two gestures behind it (click-to-place, drag-to-move),
  the same shape `RouteEditor` already has in `route.py`. Schema 2 sharpens what it would offer: a
  symbol PICKER over the embedded palette. **What is NOT there is any way to get artwork into a
  document from the app** — `add_symbol_from_png` is bound and only `WriteSampleFile` calls it.
  Related and smaller: the sample document is written to `<catalog dir>/sample.fvpoints` by a
  File-menu item, which is a demo rather than data management; the shapes ignore device DPI (below);
  and point labels are off by default because there is no label collision (below).
- **Snapping removes the ACROSS-track error and leaves the ALONG-track error alone** (MM5), which
  is what a projection can do and the whole of it: a fix 10 m up the road projects onto the road
  10 m up it. Measured over 1295 fixes of a routed Kiawah track with ±12 m of scatter — mean 9.32 m
  in, **5.96 m out**, which is one axis of the noise almost exactly. Removing the rest needs a
  motion model: **MM5b**, an HMM/Viterbi match over a sliding window with network-distance
  transition costs (OSRM's shape), reusing the router for candidate-to-candidate distances. The
  seam is ready for it — `RoadCandidate` already carries `along_m` and the arc's ends — and nothing
  in MM5 is shaped around its absence. Related and smaller: **a candidate's bearing is a TRUE
  bearing while a derived heading is a SCREEN angle** (MM1's split), so the alignment term compares
  two frames that differ by the projection's aspect — 5° off Charleston, 18° at 60°N. It is a soft
  ranker and the distinction that matters (which WAY along the road) is a 180° one, so this is
  documented rather than reconciled; reconciling means handing the snapper the projection.
- **The snapper's road index is built over the WHOLE graph at construction** (MM5), which is right
  for an island, a county or a state and wrong for a continent-sized `.fvroad` — that wants a
  window around the ship, rebuilt as it moves. Same family as the router's per-query scratch below.
- **WVS (WVSPLUS)** — Chris wants it. Blocked in the reader, root cause known: `fv_vpf` builds the
  feature-class list only from **FCA**, and WVS thematic coverages have none → empty. Fix =
  enumerate from **FCS or a directory scan**, then a simple stroke style engine (WVS has no GeoSym
  symbology). Data: `TestData/VPF 2/WVSPLUS/WVS{012,040,120}M`.
- **Symbol size does not honour device DPI while line widths do.** A symbol is sized on its
  product's own nominal pixel — `himetric_per_symbol_pixel()`, 25.4 for GeoSym and 32 for S-52 —
  and a raster tile is blitted 1:1. **None of those three numbers is the device's**, so on a retina
  pitch every symbol is half its physical size while the text beside it is right. The fix is one
  more factor (`dpi/100` on display lists, `0.32 mm / device mm-per-px` on tiles); the reason it has
  not moved is the bit-faithful rule — it would move every GeoSym golden. **G3 built the way out and
  nothing sets it**: `GeoDraw::symbol_dpi_scale` really multiplies the stamp but defaults to 1.0,
  because an overlay does not know the device and no shell passes one down. Same shape as
  `PickSession::tolerance_px` in §2c. G2's `SymbolPixmap::pixel_ratio` is the other half for RASTER
  symbols: it does not fix this, but a high-DPI sprite set is now at least expressible.
- **A session the user builds at RUN time cannot be persisted** (A3). `SaveConfiguration` /
  `RestoreConfiguration` round-trip through the live `fv::Settings` and are bound — but
  **`fv::Settings` has no `Save()` by rule S1** (the file is authored by a human and the application
  never rewrites it, which is what preserves the comments and the unknown keys). So a hand-written
  `peregrine.ini` can carry a startup session and "save my current layout" cannot. **The fix is not
  to relax S1**: it is a second store for application state — window geometry, last position, the
  saved session — which the settings header already says belongs elsewhere. One decision, then a
  writer. A6 made this the app layer's most visible gap.
- **A top-most overlay's opacity is carried and not applied** (A2). `default_opacity` is
  FalconView's blend for the top-most band and `DrawAll` draws that band as a second pass exactly
  where the blend belongs — but `ICanvas` has no layer alpha. The fix is an off-screen layer, which
  is also what a real pattern brush and a clip region want, **so all three are one canvas session**.
- **ICanvas has no pattern brush.** GeoSym stipples and S-52 `AP` fills are approximated by carrying
  ink coverage in the fill **alpha**. `AreaFillFor` is the one place to change.
- **Area patterns bleed past their ring**, because `ICanvas` has no clip region — a pattern can
  spill by up to half a symbol. And **area patterns are outer-ring only** (`p == 0` in the
  renderer's area branch): holes do not punch through a pattern or a fill.
- **No label collision or de-duplication.** Every product that draws text needs it and none has it;
  OSM makes it visible because a road name repeats per tile, and T1's `symbol-spacing` repeats a
  name along a long road as well. Belongs in the renderer/scene, not a style engine: a per-frame
  index of the boxes the renderer is about to emit, rejecting a label that collides. **The boxes
  exist already** (the pick index takes one per label run). **E8 made this ENC's most visible
  defect** — a Charleston harbour view draws "Shutes Folly Island" three times, one per overlapping
  cell. Note the second cause, which de-duplication alone will not fix: the 8 test cells span 4
  usage bands over the same water and all are open at once, where an ECDIS shows one band, so the
  index has to key on more than the string.
- **Text halos have no blur and only OSM sets one** (T2). `text-halo-blur` is ignored and counted —
  a stamped halo has no coverage to soften. S-52 and GeoSym both draw text that would read better
  with a halo and neither authors a colour, so giving them one is a symbology decision (and would
  move their goldens).
- **S-52's `SPACE` and `DISPLAY` text parameters are still skipped** (E8 took the other four).
  `SPACE` is character spacing and needs `CpuCanvas` before it needs the loader; `DISPLAY` is the
  group number and belongs on the viewing-group axis.
- **ENC body size is treated as PIXELS, not points.** `TextSizeFromSpec` reads the `CHARS` body size
  straight into `TextStyle::size`. Same family as the device-DPI item and unmoved for the same
  reason: it would shift every label on the chart.
- **`OsmStyleEngine` colour stops STEP, numeric stops interpolate** — a declared O2 deviation from
  the GL spec. Visible only side by side.
- **OSM Bright is not vendored.** Adding `openmaptiles/osm-bright-gl-style` (BSD-3/CC-BY) as a
  second reference style needs a network fetch and a `NOTICE.md` entry, and would exercise the
  subset check against a style nobody here authored — worth doing for that alone.
- **`CgmSymbolLibrary` still has no consumer** (GeoSym's ~1500 `.cgm` in an overlay). Every CHART
  symbol still goes through `VectorRenderer` exactly as before, which is what kept the goldens
  byte-identical.
- **`VpfVectorSource::Bounds()` costs a full scan** — tile bounds need the tileref face, so
  per-feature bounds are used. Much reduced by R3c, but it still parses the whole library to answer
  "where is this?". The 14m catalog already stores per-tile coverage; use it for tile-level culling.
- **CIB gap**: the CADRG decoder supports CIB but `CadrgRasterSource` hardcodes `is_cib=FALSE`, and
  there is no CIB test data.
- **Ferries and tolls have no REAL-data fixture** (O5e). Kiawah carries **zero** `route=ferry` ways
  and **zero** `toll` tags, so every O5e test runs on the synthetic "bay" fixture — enough to pin the
  mechanism, not enough to catch a tagging shape nobody thought of. One `fvgraph build` over
  `us-south-260728.osm.pbf` (~1 h) would give real counts.
- **A ferry's `duration` is prorated wrongly on a CLIPPED extract** (noted, not fixed): the speed is
  the way's KEPT length over its stated duration, so a crossing cut by the bbox reads as slower than
  it is. The safe direction, and unfixable without the full way length.
- **Router scratch is allocated per query** — six arrays of `state_count` (~34 bytes/state), fine for
  a state-sized graph, ~700 MB per query on a continent. The fix is reusable scratch with an epoch
  stamp, but `Router` is const and shareable today, so it needs a thread-safety decision.
- **`RoadGraph` cannot cross the antimeridian** — `Finalize` hangs the nearest-node grid on a plain
  min/max bounds. The catalog already has the split-at-180 treatment to copy.
- **`FeatureStore`** — the other half of L5 (only `TilePack` was built).
- **ImageLib leftovers**: `fv_imagelib_gif` compiles but has **no test** (no `.gif` sample);
  `Image.cpp` (multi-format dispatcher) and `nitf/` unported (row 9b-4).

### 2c. PythonView / UI follow-ups

- **A6's leftovers, none of them blocking**: no shell calls `OverlayManager::Reorder`, so the
  reorder DIALOG is unwritten and the stack order is whatever insertion-by-display-order produced;
  `SnapToPoint` is bound and tested but **no overlay in the app answers it**, so nothing snaps;
  `EditorUiConstraints` is reported and greys nothing (the app has no rotation or projection
  controls to grey); `RoutingOverrides` is bound and unused; and **`route_double_click` is called by
  no shell**.
- **A GPX track has nowhere to be DRAWN** — the moving map's most visible gap now that MM7 has
  closed the feed one. `GpxSegmentPath` hands back exactly the polyline
  `GeoDraw::DrawGeoPolyline` wants and nothing calls it — showing the ride you are replaying is a
  route-overlay-shaped job (`route.py` already draws a line from a document) and would make the
  replay legible instead of a symbol wandering an empty chart.
- **Snap-to-road has a menu item and no key** (MM5), unlike the three moving-map modes which have
  M/T/S. `[movingmap] snap_to_road` is the startup state and Overlays > Moving Map Modes > Snap To
  Road moves it with the feed running. Also app-level and deliberate: **`[movingmap] noise_m` is a
  DEMO knob** — the scripted feed replays a track that is already exactly on the roads, so without
  scatter the snapping is a no-op nobody can see. A real feed retires it, and since MM7 the app can open
  one — so the knob is now scoped to the DEMO feed rather than to the app.
- **`PickSession::tolerance_px` is 8 device pixels and the shell is supposed to scale it.** Same
  family as the symbol-DPI gap: the core has no business knowing a finger is wider than a mouse
  pointer. PythonView already knows its own `display.mm_per_pixel`, so this is one line whenever
  somebody has a device where it is wrong.
- **The route line's STYLE is derived, not chosen** (G3). `route.py` draws a calculated route dashed
  when the request was a bicycle one and solid otherwise, decided by matching the profile NAME —
  right for the two modes the app has keys for and silent about a third (a walking profile draws
  exactly like a car). The honest fix is a per-profile line style in the rule file beside the
  weights, which is a rules-schema decision rather than a drawing one. Also unstyled:
  `pyfvw.draw.PRESETS` has ten entries and the app reaches two, and there is no UI for a route's
  colour or width.
- **Toll and ferry avoidance have no UI** (O5e). Both are bound and on the CLI, but the app always
  takes the profile's default. Unlike the rest of this list it wants **two checkboxes and nothing
  else** — they are what a driver changes per journey and they already outrank the profile by
  design; `car_no_tolls` / `car_no_ferries` in the shipped rule file exist only because there is no
  UI. Worth doing beside the profile menu.
- **`private_penalty` has no UI and no settings key** (O5b). Bound and on the CLI; the app takes the
  default. The number is a judgement, not a measurement: 5x keeps a route off a gated shortcut while
  leaving an address behind the gate reachable, and nobody has driven it against a reference router.
- **The U-turn-at-a-stop preference has no UI** (O5d). `allow_u_turn_at_stops` is bound and on the
  CLI; the app takes the default (barred). `Route.u_turn_stops` is not drawn either — the app says
  how MANY stops had to turn round, not which, though a marker on the offending waypoint is what
  would tell the user their stop is up a driveway.
- **Route profiles: still no per-leg choice** (O5c). The profile menu, the `[routing]` settings keys
  and the live rule-file reload are all done; the profile is the app's one route-wide setting and a
  per-waypoint or per-leg profile has nowhere to live.
- **Rule layer has no UI.** `pyfvw.vector.RuleSet` / `ViewingGroupSet` and `engine.rules()` /
  `viewing_groups()` are bound and tested but unreachable from the app. Natural shape: an
  Overlays-menu display-category picker (Base/Standard/Other) + a "Load rule file…" item.
  **Half-answered by M1**: the app loads `[vector] families_{dnc,enc,osm}` at engine open and hides
  what the file says to hide. An Overlays-menu checkbox per family is one `SetEnabled` + a RuleSet
  rebuild; `FamilySet::epoch()` is there so a host can tell.
- **Mariner panel in the Options dialog** — safety/shallow/deep contour, safety depth, two-shade and
  the shallow pattern are bound on BOTH products and settable from `[mariner]`, but there is no
  dialog. It is the one setting a mariner actually changes underway (vessel draft plus under-keel
  clearance), so a spinbox is worth more than most of this list. **The per-product defaults must
  survive it**: a panel that wrote all six values on open would give DNC S-52's numbers.
- **ENC data-dir field** beside the GeoSym one in Options (`[enc] data_dir` exists). The GeoSym
  directory and the OSM style sheet are both there; ENC is the one asset still settings-file-only.
- **OSM has no per-source knobs in the UI** — tile budget, tile-cache capacity and `set_clip_to_tile`
  are bound and defaulted sensibly, but only reachable from Python.

### 2d. Known defects / hygiene

- **`TestData/OSM/kiawah.fvroad` as delivered 2026-08-17 was built with `--ignore-access`** — the
  stopgap O5b explicitly deleted. `fvgraph info` on it reports **0** barred and **0** private arcs
  where the same four extracts built honouring access give **70 barred / 408 private** car arcs, and
  the extracts do still carry the tags (156 `k="access"`, 137 `v="private"`). O5b's finding was that
  this file IS the app's graph, so an app reading it drives through the gates. No test reads it (they
  all build their own into a scratch dir), so nothing fails — which is exactly why it needs writing
  down. Fix is one command:
  `fvgraph build -o TestData/OSM/kiawah.fvroad TestData/OSM/map.osm TestData/OSM/map-2.osm TestData/OSM/map-3.osm TestData/OSM/map-4.osm`
  — left for Chris, since a deliberately permissive graph for a nav demo is a plausible reason to
  have made it this way.
- **The test-data directory is `testdata/` and 14 CMakeLists say `TestData`.** The directory on disk
  has been lower case since the project started (Chris, 2026-08-17 — every fixture is in it);
  `FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData` resolves anyway because APFS is case-insensitive.
  **On a case-sensitive checkout the variable points at nothing and EVERY test that reads a fixture
  fails**, not just one module's. Pre-existing and repo-wide, so it is hygiene rather than a defect
  in any one session; the fix is one spelling across those 14 files, and §1f above should be read as
  naming the directory rather than its case. Same family as the Peregrine case-sensitivity
  follow-up, and `.gitignore` already lists all three spellings for the related reason.
- **Windows sockets are guarded and never compiled** (MM6). `line_transport.cpp`'s Winsock branch is
  written — refcounted `WSAStartup`, `ioctlsocket`, `closesocket`, the same non-blocking-before-
  connect rule — and no build in this tree has ever run it. Treat a first Windows build as bring-up,
  not as a regression.
- **Two open ENC decisions left by the 2026-08-11 test-data cut-back**: (a) should ONE unreadable
  cell abort an exchange set, or be skipped with a warning and a count — an ECDIS would not refuse
  the other 822; (b) the render tests still open `$FVW_TESTDATA_DIR/enc` wholesale, so **restoring
  `TestData/enc-archive/` re-breaks them**. Naming a fixed cell list would make the goldens
  independent of what else is on disk. (`EnumerateEncCells` is a bare recursive iterator matching
  `*.000`, which is why the archive is a SIBLING and not a subdirectory.)
- **`PythonView.py --selftest` aborts on this machine before it reaches any UI step**, in
  `RPFRenderer::get_rgb_image` (`fvw_core/ImageLib/cadrg/imgdisp.cpp:386`) — `get_frame_image`
  fails on a CADRG frame and the original's `ASSERT(0)` is a hard abort in this build. Observed
  2026-08-17 and **reproduced with the working tree stashed**, so it is not MM5's; it is a frame in
  the local catalog the decoder will not read, and it makes the scripted walk-through unusable
  until somebody finds which. The `--shot` path and every ctest test are unaffected.
- **Some tests share one scratch `.gpkg`** and so cannot run in parallel. Fix = per-test filename.
- **`pyfvw_pytest` cannot run in the ASan build** — Python is not sanitizer-instrumented and
  `dlopen`s an instrumented `.so`, so ASan aborts with "Interceptors are not working". The C++ tests
  are unaffected. Either run it under `DYLD_INSERT_LIBRARIES=<libclang_rt.asan_osx_dynamic.dylib>`
  or exclude it from `build-san`.
- **Invariant 3d.1's CANCEL branch is unreachable and is pinned through a FAILURE** (A4). "If the
  user cancels creation, the mode falls back to none" shares one branch with "if creation failed",
  and neither creation flow currently asks the user anything. The fallback is genuinely pinned; the
  word "cancel" in it is not. A creation flow that grows a prompt should add the cancel test.
- **VPF reader UBSan alignment** — `vpfrcset`/`tables` do unaligned scalar loads. ASan-clean; this is
  the only UBSan noise in the tree, which is why every VPF session says "only the pre-existing ones".
- **`VPFRecordset` reopen row-undercount** (original bug, preserved bit-faithfully).
- **Subsampled `ReadBlock`** — zoomed out, 9 fully-VQ-decoded CADRG frames ≈ 1.2 s. Decoders read
  full resolution and then downsample.
- **Polar CADRG transforms** return `kUnsupported` (equal-arc only).
- **TIROS tile-seam** artifacts.
- **C++14 pins** still on `fv_jpeg`, `fv_jpeg12`, `fv_imagelib_gif` (`std::auto_ptr` in headers).
- **`CDTEDInstance` stub** in ImageLib's `Util.cpp` — RPC height refinement returns "no DTED"
  headless; wire `fv::DtedCell`.
- **Grid overlay draws wrong under map rotation** (`port/fvkit/overlay/grid_overlay.cpp`,
  `port/include/fvkit/overlay/grid.h`). Reported by Chris 2026-08-18, not yet triaged. Parked here
  so a future session picks it up instead of rediscovering it.

### 2e. Dependency modernization (three ⛔ rows left; full table in the archive)

The port builds against current upstream via `port/third_party/CMakeLists.txt` (FetchContent).
**Done**: googletest 1.17.0, zlib 1.3.2, expat 2.8.2, protozero 1.8.2, vtzero 1.2.0,
nlohmann/json 3.12.0. **Frozen on purpose**: GEOTRANS 3.3 (a newer one invalidates the pinned geo
results).

Each remaining row is a session of its own; none blocks the active track. Do them in this order
(ascending consumer count, so a break localises):

1. **libpng 1.2.7 (2004) → 1.6.58** — not a drop-in: opaque structs, reworked
   `png_get_`/`png_set_`/`png_jmpbuf`.
2. **libtiff 3.9.4 → 4.7.2** — not a drop-in: `toff_t` widened to 64-bit; `CGeoTiff` is ~25K lines
   against the 3.x API.
3. **IJG jpeg 6b → libjpeg-turbo 3.2.0** — hardest: FalconView *transliterated* IJG to C++ and the
   wrapper carries an encryption fork (`m_crypt_pos`/`m_encrypt`, must be shown unused first);
   re-opens the "FalconView's C++ jpeg and GDAL's C jpeg must not meet in one link" rule. GDAL's
   vendored libjpeg goes away with this.

**Gate**: Q12 (WMS) is the first network-facing feature — anything parsing network input must be on
a modern library first. expat already is.

### 2f. Backlog (unstarted, roughly in priority order)

| # | Item | Data | Complexity |
|---|------|------|-----------|
| Q12 | WMS network raster source | public endpoints (USGS, GIBS) | moderate — HTTP client decision: libcurl |
| Q13 | JP2 via OpenJPEG | public samples | moderate (avoids Kakadu; would also unblock ECRG) |
| Q14 | NITF (ImageLib `nitf/`, row 9b-4) | public NITF test sets | moderate-high |
| Q15 | GeoPDF | USGS topo GeoPDFs | high (PDF engine decision) |
| Q16 | Lidar | USGS 3DEP | high, niche |

Also unbuilt from the vector plan: **V7** a CoreGraphics `ICanvas` backend (macOS first, same code
iOS) and **V8** the symbol atlas / batching pass — neither blocks anything, and R3c measured the
atlas away as a non-goal at current frame times.

**Deprioritized** (Chris 2026-07-19 — restricted/proprietary data, not the public-data use case):
ECRG, CIB, MrSID, Hrdted/RDted/ARdted, BlankMapServer.
**Deferred indefinitely**: CoT (row 8), MdsUtilities (row 6 — Windows system plumbing only; pull
individual helpers on demand), Collaborate, NITFSourcesCtrl, *MapOptions property pages,
FvConfigFileServer. Other unported map servers: Ecrg, MrSID, Jp2, WMS, GeoPdf, Lidar, Blank.

**Deferred by decision, not by backlog**: **dimming** as a `RenderState` (Chris 2026-08-13 — the
two decisions already worked out are in the draw plan's §3d, and nothing in G1–G4 is shaped around
its absence) and **G5**, an SVG symbol library, which is gated on wanting somebody else's symbol
sets.

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
O1–O5e, K1, T1, T2, M1, A1–A6, G1–G4, MM1–MM5, PR1–PR3), the dated decision log, and — appended
2026-08-16 — **"Condensed out of the working ledger"**, which is the long-form §1 this file used to
carry plus every resolved §2 item, verbatim. Read that section when you want the build narrative
for something §1 above only names.

Useful entry points:

- **Moving map (`fvkit/nav`)** — 2026-08-18 (**MM7** the app side: the session whose proof is that
  the shell's tick did not change, the binding rules that turn a Status into a raise and an
  out-parameter into a None, the transport seam deliberately left without a Python trampoline
  because `add_data` is the cheaper door, the feed as a tuple with one `set_source`, the one empty
  field that chooses UDP over TCP, emit-per-sentence as the live feed's answer to MM6's measured
  epoch of latency, the status line that shows bytes AND sentences AND fixes because otherwise a
  parse failure looks like silence, the explicit open that re-reads where a feed switch does not,
  and the headless drive over real TCP and UDP sockets),
  2026-08-17 (**MM5** snap-to-road: the seam that lets two libraries
  meet without linking and the one inline function that pays for it, every score term in metres, the
  hold as an infinite stay bonus rather than a branch, snapping BEFORE the resolver and the two-way
  road that cannot say which way along itself the ship is going, the index built over the geometry
  after a dogleg found it built over the vertices, and the residual that is one axis of the noise —
  which is the honest reason MM5b is still open),
  2026-08-15 (**MM4**: the overlay as the one object that is
  both drawn and fed, the tick that answers and applies nothing, every fix to the resolver and
  only the last to the camera, the mode change that forces a recentre, the negated symbol
  rotation that sixteen passing assertions could not see, and the apron that a zero-sized
  window does not clear),
  2026-08-15 (**MM2**: the apron built from the DRAWN position and
  why rebuilding it from the new one freezes the map, the convergence accessor the plan asked for
  and equal-arc does not need, the track-up offset that is two unit vectors rather than a rotation
  with a sign error, "continuous" centring that jumps at its own case boundaries, the once-only
  wrap that only a wild convergence can defeat, and the one place a bit-faithful port had nothing
  to be faithful to because the original's cast is undefined),
  2026-08-15 (**MM1**: validity per field instead of the -1000
  sentinels, the thread rule and the queue that drops the oldest, the clockless scripted source,
  the track builder that takes a polyline so fvkit still does not link Routing, the heading
  derivation kept in screen space, and the original's `atan` fix-up on an `atan2` that points a
  southbound ship backwards — kept in the test as the oracle it is not).

- **Overlay drawing (`fvkit/geo`)** — 2026-08-15 (**G4**: a highlighted thing still drawn as
  ITSELF and the test that could not tell a gained band from a recoloured one, the line that
  takes one wider stroke instead of eight offset ones, the highlight kept out of the pick index
  so a selected feature is not a bigger target, the outermost-stamp-only rule, the tint that
  keeps a tile's ALPHA, and the 2x stamp cap that only a look at the render would have found),
  2026-08-13 (**G1**: the geodesy that was already in the
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
- **Display/projection** — 2026-07-24 (physical scale + the aspect-ratio bug),
  2026-08-15 (**PR1** rotation: the identity guaranteed by gating the arithmetic rather than by
  an identity matrix, the cardinal angles taken off a table because cos(pi/2) is 6.1e-17, the sign
  pinned from both ends, the viewport turned in PIXELS because the geographic frame is anisotropic,
  the sqrt(2) query box as the stated price, and the retained scene that needs no key because R3a
  made its ink geographic),
  2026-08-15 (**PR2** the vector path: twenty lines of code under two hundred of test, the one
  angle the projection cannot see, the geographic-anchor vs pixel-anchor split that left the
  ownship untouched, and the area pattern deliberately not turned — mutation-checked),
  2026-08-15 (**PR3** the raster path and the shell: the gate that keeps rotation 0 byte-identical,
  the MASK that replaces the straight path's clamp, four corners and three samples, the test frame
  that has a north and a south because a footprint proves nothing, the half-pixel tie the two paths
  break differently, the adopted `proj.Rotation()` that retired MM4's duplicate state, and the pan
  delta turned back into the chart's own axes).
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

