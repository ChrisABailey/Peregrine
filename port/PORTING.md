# FalconView Cross-Platform Port — Working Ledger

**Read this first in every session. Do not re-explore the repo.**
This file is **open work only**. Finished modules, the session-by-session narrative and every
dated decision live in **`port/PORTING-ARCHIVE.md`** — go there only when a line below names a
specific entry. Full strategy:
`/Users/chrisbailey/.claude/plans/this-project-is-extreamly-enumerated-marshmallow.md`.
Vector/symbology design: `port/vpf-geosym-plan.md` (§5 = the cross-product middle layer, §7 = ENC).

```sh
cmake -B build && cmake --build build -j && ctest --test-dir build   # 689 tests green as of 2026-08-08
```

Other docs: `port/fvkit-contracts.md` (D1–D6 — ownership/geo/Status/pixel/naming/adapters),
`port/bindings/pyfvw/README.md` (Python user guide), `port/bindings/pyfvw/ICD-MAPPING.md`,
`port/peregrine.ini.sample` (every settings key with its measured effect).

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
`rules.h` (predicate AST, ScaleBand, ViewingGroup), `lookup_engine.h` (`LookupTableStyleEngine` — the
shared engine; GeoSym and S-52 are *loaders* over it), `renderer.h` (VectorRenderer + the three
placers: `PlaceAlongPath` / `PlaceOverArea` / `PlaceTextAlongPath`), `scene.h` (retained VectorScene),
`pick.h` (PickIndex — hit-tests the emitted ink). Labels: `LabelStyle` carries placement
(point or along-path), spacing, max angle, offset, and a size in pixels OR ground metres;
`ICanvas::DrawRotatedTextString` is what draws them (T1).

**Vector products, all three on that one seam**:
- DNC/VPF — `port/VpfMapServer/` (reader, vector source incl. areas, VDT identify) +
  `port/GeoSymServer/` (rule tables, CGM symbols, `GeoSymStyleEngine`).
- ENC/S-57 — `port/Enc/` (ISO 8211, S57Cell, Appendix A catalogue, S-52 PresLib, `S52StyleEngine`,
  raster symbol sheet, enumerator/format registration).
- OSM — `port/Osm/` (MBTiles + MVT + `OsmVectorSource` + `OsmStyleEngine`, a MapLibre
  style-JSON loader over `LookupTableStyleEngine`; reference style
  `port/Osm/styles/peregrine-osm.json`; `OsmFrameEnumerator` + `RegisterOsmFormat`).

**Apps/bindings**: `port/bindings/pyfvw` (full binding surface incl. `pyfvw.vector`, `pyfvw.catalog`,
`pyfvw.engine`, `pyfvw.overlay`, `pyfvw.canvas`, `pyfvw.Settings`), `port/apps/PythonView.py` (the tk
application), `fvrender` and `fvpack` CLIs.

**Test data** (`TestData/`, git-ignored, all present): dted, geotiff DOQs, rpf CADRG, tiros3,
`vpf/dnc17`, `VPF 2/WVSPLUS`, `GeoSymbol/{SymAssign,Graphics}` (DataDir = `TestData`),
`enc/` (8 NOAA Charleston cells bands 2–5 + `chartsymbols.xml` + `s57objectclasses.csv` +
`s57attributes.csv` + `s57expectedinput.csv` + `rastersymbols-{day,dusk,dark}.png`;
S-52 data-dir arg = `TestData/enc`), `OSM/mbtiles/us-south.mbtiles`.

---

## 2. Open work

### 2a. Active track — next sessions, in order

| # | Session | What it is |
|---|---------|-----------|
| **R3d** | Perf, fourth slice — *if anything still needs it* | R3c re-measured the whole profile **at -O2** (see the row in the archive: the default build type was the real finding) and the frame it was aimed at is now **cold 49 ms = query 29 + style 11 + draw 8; a retained pan 3.4 ms; a pan out of the retained area 11.6 ms = query 0.7 + style 6.9 + draw 3.5**. The remaining shape: the cold 29 ms is the **one-time** parse of a whole DNC library and the 6.9 ms is **GeoSym styling** — so the next target, if a user still feels one, is `LookupTableStyleEngine`, not the query and not the rasterizer. **Do not start this without a fresh profile**: this is the third time in a row the plan on this line has been wrong about where the time was (R3b, R3c). The columnar `FeatureBatch` is now a **measured non-goal** — see the archive. |
| **O4** | Routable road graph + A* | Dynamic route creation on the OSM road network. **Not from the mbtiles** — MVT is a rendering product: simplified, tile-clipped, not topologically noded; there is no graph in it. Shape: (1) an offline tool (the `fvpack` pattern) reading the RAW `.osm.pbf` extract (same input Tilemaker had) → compact routable graph (noded intersections, edges with class/oneway/length, class-based speeds); (2) a bidirectional Dijkstra/A* module in core over that graph — no contraction hierarchies needed at region scale; (3) output is a polyline onto the existing route overlay (`port/apps/route.py`, which since O3 can add and delete waypoints interactively — "a" then a click inserts after the selected one). **Use `OsmVectorSource::SetClipToTile(false)`** if the graph tool ever reads the pyramid for anything: clipping is on by default for rendering, and a routable edge cut at a tile seam is two edges. Record when planned: turn restrictions + oneway honored for driveable routes; cross-region routing = rerun the tool on a bigger extract. |

### 2b. Product gaps

- **MarinerSettings on `StyleContext`** (the last open half of R2/Q6c-5). ENC's side is done
  (`S52MarinerSettings`: safety/shallow/deep contour, safety depth, two-shade). **DNC's is not** — the
  GeoSym depth ramp still uses `CECDISValues`' default-constructed `ssdc`/`msdc`/`mssc`, so a vessel
  draft cannot be entered. Needs the settings on the shared `StyleContext`, plus UI.
- **WVS (WVSPLUS)** — Chris wants it. Blocked in the reader, root cause known: `fv_vpf` builds the
  feature-class list only from **FCA**, and WVS thematic coverages have none → empty. Fix =
  enumerate from **FCS or a directory scan**, then a simple stroke style engine (WVS has no GeoSym
  symbology). Data: `TestData/VPF 2/WVSPLUS/WVS{012,040,120}M`.
- **Symbol size does not honour device DPI while line widths do.** `VectorRenderer`'s
  `px_per_himetric` is a fixed `1/25.4` = exactly 100 dpi — bit-faithful for GeoSym *by rule*, but
  arbitrary for S-52, a 2× mismatch on a retina pitch, and it now also applies to raster symbol tiles
  (authored at one nominal pitch, blitted 1:1).
- **ICanvas has no pattern brush.** GeoSym stipples and S-52 `AP` fills are approximated by carrying
  ink coverage in the fill **alpha**. `AreaFillFor` is the one place to change when a real pattern
  brush lands. (Related: **area patterns are not clipped to their area** — archive 2026-07-27 E3b.)
- **ICanvas has no outlined (halo) text.** Tolerable over paper-style charts; OSM labels over
  imagery/dense fills are unreadable without it — which is now visible in the app (O3 wired
  OSM in with labels available on the `l` key), not hypothetical. Sharper since **T1** put road
  names ON their roads: a name now lies over the linework it belongs to, which is exactly the
  case a halo exists for. The canvas-side work is one pass in `CpuCanvas::DrawRotatedTextString`
  (it already walks glyph coverage) plus a colour/width on `TextStyle`.
- **No sprite sheet, so OSM has no icons** (O2/O3 — the app draws none). `icon-image` is counted in
  `OsmStyleEngine::ignored_icons()` and never drawn; `fill-pattern`/`line-pattern`/
  `background-pattern` are load-time rejections. The seam is ready for it — `SymbolPixmap` (E6)
  is exactly a sprite tile — so this is a loader (parse `sprite.json` + the PNG), not a design.
- **No label collision or de-duplication.** Every product that draws text needs it and none has
  it; OSM makes it visible because a road name repeats per tile — and **T1's `symbol-spacing`
  now repeats a name along a long road as well**, which is correct and also multiplies the
  overlaps. Belongs in the renderer/scene, not in a style engine: a per-frame index of the boxes
  the renderer is about to emit, rejecting a label that collides with one already placed. The
  boxes exist already (the pick index takes one per label run).
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
- **`FeatureStore`** — the other half of L5 (only `TilePack` was built).
- **ImageLib leftovers**: `fv_imagelib_gif` compiles but has **no test** (no `.gif` sample);
  `Image.cpp` (multi-format dispatcher) and `nitf/` unported (row 9b-4).

### 2c. PythonView / UI follow-ups

- **Rule layer has no UI.** `pyfvw.vector.RuleSet` / `ViewingGroupSet` and `engine.rules()` /
  `viewing_groups()` are bound and tested but unreachable from the app. Natural shape: an Overlays-menu
  display-category picker (Base/Standard/Other) + a "Load rule file…" item.
- **Mariner panel in the Options dialog** — safety/shallow/deep contour, safety depth, two-shade are
  all bound, none exposed.
- **ENC data-dir field** beside the GeoSym one in Options (the `[enc] data_dir` settings key exists).
  The GeoSym directory and the OSM style sheet are both there; ENC is the one asset still
  settings-file-only.
- **OSM has no per-source knobs in the UI** — tile budget, tile-cache capacity and
  `set_clip_to_tile` are bound and defaulted sensibly, but only reachable from Python.

### 2d. Known defects / hygiene

- **`TilePackReal.WriteReadRoundTrip` and `.EnumeratedAndRenderedThroughEngine` fail under `ctest -j`**,
  pass serially. They write the same scratch `.gpkg`. Fix = per-test filename.
- **`pyfvw_pytest` cannot run in the ASan build** (found R3c, and it is the toolchain, not the code):
  Python is not sanitizer-instrumented and `dlopen`s an instrumented `.so`, so ASan aborts with
  "Interceptors are not working … loaded too late". The 666 C++ tests are unaffected. Either run it
  under `DYLD_INSERT_LIBRARIES=<libclang_rt.asan_osx_dynamic.dylib>` or exclude it from `build-san`.
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
  bounds.
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
- A GL style's draw order is **style-layer order, not feature order**: one road feature emits both
  its casing pass and its fill pass, and every casing in the viewport must be drawn before any
  fill. `StyleResult::priority` = the style layer's index does this, because `VectorScene`
  stable-sorts by priority ACROSS features.
- DTED: level-3 out of scope; lookups are nearest-neighbour; elevation bands default in **feet** and
  need the converting setter.
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

`port/PORTING-ARCHIVE.md` holds the completed module table (rows 1–14u, R1–R3c, E1–E6, F1–F3, S1, O1–O3, K1)
and the dated decision log. Useful entry points:

- **Vector seam design** — 2026-07-23 (V5a/V5b), 2026-07-27 (E3b placer, E3c extraction).
- **Rule layer** — 2026-07-26 (R2: predicate AST, ScaleBand, ViewingGroup, `ResolvedPlan`).
- **Perf** — 2026-07-28 (R3a retained scene / R3b "the plan was aimed at the wrong costs" + symbol-atlas deferral),
  2026-08-08 (R3c: the ledger's own build command was -O0; the DNC parsed-feature cache; the
  `FeatureBatch` measured away; the geographic pattern anchor).
- **ENC/S-52** — 2026-07-25 (E1 base-edition), 2026-07-27 (E2 PresLib inventory, E3a/E3b CS procedures
  and their reductions), 2026-07-28 (E5 defects vs a published chart, E6 pixmaps, F3 usage bands).
- **DNC/GeoSym** — 2026-07-20 (V1 reader + two CString silent-corruption bugs), 2026-07-21 (V3),
  2026-07-24 (areas, map-scale-vs-feature-zoom, WVS root cause), 2026-07-25 (F1 fills), 2026-07-27 (F2 flip).
- **Display/projection** — 2026-07-24 (physical scale + the aspect-ratio bug).
- **Settings** — 2026-07-28 (S1: INI over JSON, read-only, three failure modes).
- **OSM** — 2026-08-04 (O1: dependencies, bounds, zoom clamp, tile budget, two geometry deviations),
  2026-08-08 (O2: the one zoom↔scale relation, the declared style subset, style-layer draw order).
- **Input** — 2026-08-04 (K1: VK codes over X11 keysyms, tk binding semantics).
- **COM severing pattern** — 2026-07-11 (GeoTIFF `IDatumConvert` is the worked example).
