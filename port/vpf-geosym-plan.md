# Vector charts plan — VPF/GeoSym, plus OSM tiles & ENC (S-57/S-52)

**Status: plan only (2026-07-16, surveyed with Chris's direction). No code yet.**
Companion to `port/fvkit-contracts.md` and the ledger. Scope: VpfMapServer +
its embedded GeoSymServer — FalconView's largest module (~62K lines, 111
files) and the only one that *draws* (GDI) rather than just decodes pixels.

## 1. What's actually there (survey findings)

```
VpfMapServer/
  Vpf/                17.9K  VPF table/topology reader (tables, vpfdb, vpfelem,
                             VPFFace, vpf_poly, indexes, tile) + some GDI (10 files)
  VPFDataLib/          8.2K  DIVERGED FORK of Vpf/ (same filenames tables.cpp,
                             vpfdb.cpp, vpfelem.cpp, tile.cpp — different contents!)
  GeoSymServer/       16.6K  GeoSym symbology engine: attribute-expression rule
                             evaluation, delimited symbology tables, CGM binary
                             symbol parser+renderer, colors, text, layered display
  VPFDataRenderServer/ 5.2K  COM render orchestration (products/databases)
  VPFMapPlugInCOM/     8.7K  plugin COM surface
  VPFLib/              2.6K  misc, no GDI
```

- **The GDI footprint is small and stereotyped.** Complete primitive
  inventory across GeoSymServer+Vpf: Polyline(56) dominates; then pens/
  brushes/fonts + TextOut/DrawText, Rectangle/Polygon/Ellipse, Begin/End/
  StrokePath (styled wide lines), and BitBlt/TransparentBlt/AlphaBlend +
  CreateCompatibleDC (offscreen **layer compositing** in SymLayeredDisplay).
  Only 6 GeoSymServer files touch GDI at all. This maps ~1:1 onto the ICanvas
  already planned for FvKit L2.5 (which mirrors IGraphicsContext2).
- **The render contract is per-feature, screen-space.** GeoSymServerCOM:
  `OpenProduct(id)` → `StartRendering(hDC, viewportRect, layered)` → per
  feature `DrawSymbol(FACC, attrs, x, y, scale, zoom, rot)` /
  `FillRegion(FACC, attrs, points…)` / `DrawSAMILine(…)` →
  `CompleteRendering(useAlpha, alphaValue)`.
  **(Resolved 2026-07-16, full tree arrived):** the feature-walking loop is
  IN-TREE — `VPFMapPlugInCOM/VPFMapPlugIn_imp.cpp` (5,090 lines) holds
  `CGeoSymServer*` directly (in-process C++, not COM dispatch) and does the
  whole walk/priority-sort/draw sequence. Phase V5 is therefore a port-by-
  extraction of that loop, not a reconstruction from the COM contract.
- **GeoSym symbol art is CGM.** Point/line/area symbols are binary CGM
  metafiles interpreted at draw time (CGMFile.cpp, ~1.6K lines of element
  dispatch), selected by rule tables (AttributeExpressions + DelimitedParser)
  keyed on FACC code + feature attributes, with display priorities
  (GetPriority) and ISDM/IDSM/SSDC/… display-parameter knobs.
- **Blocker:** the GeoSym *assets* (CGM files + symbology tables) are not in
  this tree or TestData — they ship with a FalconView install. Needed from
  the Windows machine before phases 3+ can be tested.
- **Test data is excellent for the data layer:** TestData/vpf/dnc17 is a real
  DNC library (8,435 files: harbor/approach/coastal/general coverages).
- **ODR hazard to resolve on day one:** Vpf/ vs VPFDataLib/ same-named,
  diverged sources (tables.cpp: 1329 vs 497 lines). Same disease as the
  CGeoKey fork — fix with the established `namespace` + using-declaration
  pattern, or port only one and leave the other Windows-only.

## 2. Target architecture: three severed layers

Everything follows the contracts doc; COM stays on Windows.

```
L-A  fv::VpfDatabase        headless VPF reader (Vpf/ tables+topology in place)
     features + geometry (lat/lon) + attributes; tile-aware enumeration
        │
L-B  fv::GeoSymEngine       headless symbology: (FACC, attrs, product, display
     params) -> SymbolDecision {CGM id | line style | fill | text | priority}
     + fv::CgmSymbol: CGM binary -> resolution-independent display list
     (polylines/polygons/ellipses/text runs in symbol-local coords)
        │
L-C  fv::VpfRenderer        walks features for a viewport, sorts by GeoSym
     priority, emits ICanvas calls (fvkit/canvas/canvas.h, L2.5)
        │
     ICanvas backends:      CpuCanvas (portable, offline/pre-render)
                            CoreGraphicsCanvas (macOS + iOS interactive)
                            [Metal/Skia only if profiling ever demands]
```

Key decisions:

- **Sever at ICanvas, not at GDI.** Don't shim HDC. The 6 GDI files' calls
  become ICanvas calls; SymLayeredDisplay's offscreen-DC layering becomes
  "render in priority order into one buffer" (one pass, no N full-screen
  layers) with an optional per-layer alpha group only where GeoSym truly
  needs translucent area fills.
- **CGM parses once into a display list** (CGMFileCache already proves the
  caching instinct); the display list is the unit both live rendering and
  symbol pre-rasterization consume.
- **Text:** CpuCanvas uses stb_truetype (as planned); CoreGraphicsCanvas uses
  CoreText. GeoSym label placement logic (SymText/draw_text) is portable
  math — port it; only glyph rasterization is per-backend. Accept
  metrics deltas vs GDI (document, don't chase pixel parity).
- **macOS/iOS performance stance:** CoreGraphics is the right first
  interactive backend — immediate-mode like GDI, hardware-composited,
  available identically on both OSes, and DNC feature densities (a few
  thousand features per harbor view) are well within CG's budget. The real
  wins are architectural, not backend: (1) R-tree culling via the L2 catalog
  (VPF tiles → coverage rows, same as raster frames); (2) **pre-rasterized
  point-symbol atlas** — CGM symbols rendered once per zoom bucket, then
  blitted, instead of re-interpreting CGM per feature per frame (the
  original's biggest per-frame cost); (3) batch features by symbology state
  (pen/brush) to minimize state churn; (4) per-VPF-tile display-list caching
  so panning re-uses geometry already projected.

## 3. The pre-processing (raster) path — recommended iOS default

Chris's instinct is right and the FvKit plan already has the vehicle:
**fvkit/store TilePack (GeoPackage raster tile pyramids)**, planned L5.

- Offline tool (`fvrender-vpf`): CpuCanvas + VpfRenderer render a DNC library
  into a GeoPackage tile pyramid, one layer per scale band (harbor/approach/
  coastal/general map the natural GeoSym display scales). Runs on the Mac,
  output is pure raster.
- Runtime (iOS especially): tiles are just another raster source — the
  existing IRasterSource/catalog/pan machinery consumes them with zero
  vector cost, uniform with CADRG/GeoTIFF/TIROS. This is the lowest-risk
  path to "DNC on an iPhone" and doubles as the golden-image test rig for
  the live renderer.
- Trade-offs to accept: labels/symbols bake in at tile scale (no
  rotation-following text, no tap-to-identify from the raster alone);
  re-render needed when symbology or data updates. Mitigation later:
  hybrid — raster base + live GeoSym point symbols/labels on top (cheap:
  points only, from the same fv::VpfDatabase).
- Feature identify/tap can still work against fv::VpfDatabase directly
  (spatial query, no rendering), independent of how pixels got on screen.

## 4. Phased build-out (session-sized, each independently testable)

| Phase | Deliverable | Test | Depends on |
|---|---|---|---|
| V0 | Resolve Vpf/ vs VPFDataLib fork (namespace or pick-one); survey.sh both | builds unchanged on Windows | — |
| V1 | `fv::VpfDatabase` headless (port Vpf/ reader in place: tables, vpfdb, vpfelem, VPFFace, vpf_poly, tile) | open dnc17; pin table counts, walk a harbor coverage's features, pin known feature geometry/attrs | — (start anytime) |
| V2 | VPF coverage → L2 catalog rows (`IFrameEnumerator`-ish over VPF tiles) | SelectByGeoRect returns known dnc17 tiles | L2 catalog |
| V3 | `fv::GeoSymEngine` headless (AttributeExpressions, tables, priorities, colors) | pinned symbol decisions for known dnc17 features | **GeoSym assets from Windows box** |
| V4 | `fv::CgmSymbol` parser → display list | pin element counts/geometry for known CGM files; golden SVG-ish dump | GeoSym assets |
| V5 | `fv::VpfRenderer` over ICanvas + CpuCanvas. **Extract the generic vector seam here**: `IVectorSource` (features: point/line/ring geometry in WGS-84 + attributes + class key) → `IStyleEngine` (feature + scale → draw ops: fill/pen/symbol-id/label) → one shared `VectorRenderer` (projection, clipping, draw-op batching over ICanvas). VPF+GeoSym is the first source/style pair; OSM and ENC (below) are siblings riding the same renderer. | golden PNG of a dnc17 harbor viewport (the VPF pan-viewer moment) | L2.5 canvas, V1–V4 |
| V6 | `fvrender-vpf` offline tool → GeoPackage TilePack; tiles consumed as raster source | tile pyramid renders; pan viewer over the pack; open .gpkg in QGIS | V5, L5 store |
| V7 | CoreGraphicsCanvas backend (macOS first, same code iOS) | same golden scenes within documented text-metric deltas; interactive pan profiling | V5 |
| V8 | Symbol atlas + batching + tile display-list cache (perf pass) | frame-time budget met on dnc17 harbor pan | V7 |

Sequencing vs the main ledger: **L2 catalog and L2.5 canvas stay next** — both
are prerequisites here (V2, V5) and for everything else. V1 is the only
VPF phase with zero dependencies and can slot in whenever a session wants it.

## 5. Beyond VPF: OpenStreetMap vector tiles (added 2026-07-19)

OSM rides the V5 vector seam with a much simpler data/style side than VPF —
tiles arrive pre-cut and pre-generalized per zoom level.

- **Container: MBTiles** — a SQLite file of Mapbox Vector Tiles in a z/x/y
  pyramid. We already have the SQLite RAII layer (`fvkit/detail/sqlite.h`);
  an `MbtilesSource` is a small class. (PMTiles later if wanted; MBTiles
  first because it's pure SQLite.) Offline generation: Planetiler over a
  Geofabrik extract — e.g. Georgia, to reuse the Atlanta test geography.
- **Decode: MVT** is protobuf-encoded; vendor the header-only, BSD-licensed
  `protozero` + `vtzero` pair in `port/vendor/` (same pattern as
  stb_truetype). Tile-local integer coords → lat/lon per vertex via the
  exact inverse Web-Mercator formula — cheap for vectors, so OSM features
  enter the SAME equal-arc pipeline as everything else; no raster warping,
  no second projection in the engine.
- **Style: a MapLibre-style-spec SUBSET**, not the full spec (it's huge):
  background/fill/line/label per layer with zoom ranges, parsed from a small
  JSON we author (or a trimmed OpenMapTiles style). This is OSM's
  IStyleEngine; GeoSym and S-52 are its siblings.
- **Catalog/zoom mapping**: MBTiles registers as a data source; coverage
  rows per zoom level's bounds; viewport scale → tile zoom via
  z ≈ log2(156543 m/px ÷ viewport m/px), clamped to the file's minzoom/
  maxzoom — the BestSeriesForScale analog.
- **Licensing**: ODbL attribution for OSM data (display requirement);
  protozero/vtzero BSD-2 — vendor headers with license text intact.

Phases (each session-sized; O1 needs only the catalog + sqlite, O3 needs V5's
renderer seam):

| Phase | Deliverable | Test |
|---|---|---|
| O1 | `MbtilesSource` (tile pyramid metadata + tile fetch) + MVT decode to features (vendored vtzero) | pinned tile/feature/layer counts + geometry sanity over a small Georgia mbtiles in TestData |
| O2 | `OsmStyleEngine` (JSON subset: fill/line/label by zoom) | pinned style decisions for known layers (water/roads/buildings) |
| O3 | render via shared VectorRenderer; pan-viewer `osm` mode; TilePack pre-render option | golden hash of an Atlanta OSM viewport; zoom in/out picks correct z |

## 6. Beyond VPF: ENC — S-57 data + S-52 symbology (added 2026-07-19)

ENC is the closest sibling VPF/GeoSym has — it IS the same architecture with
IHO names: S-57 (ISO 8211 container, feature objects like DEPARE/BOYLAT with
attributes) plays VPF's role; the S-52 Presentation Library (lookup tables +
a vector symbol library + day/dusk/night palettes) plays GeoSym's. A DNC
renderer and an ENC renderer should share everything above the reader/rules.

- **Data: own spec-driven ISO 8211 + S-57 reader** (like every other reader
  in this port), with GDAL's S57 driver as a cross-check, not a dependency.
  OpenCPN's code is GPL — reference for behavior only, never copied.
  **Test data is a gift: NOAA ENC cells are free public-domain downloads**
  (pick 2–3 US harbor cells; Savannah keeps the Georgia geography theme).
  S-57 update files (.001…) are a later phase; base cells (.000) first.
- **Symbology: S-52 PresLib** — parse the IHO .dai presentation library:
  lookup tables (object class + attributes → symbology instructions) map
   1:1 onto GeoSym's fullsym.txt role; the symbol library's vector
  definitions parse to display lists exactly like CGM symbols (reuse V4's
  display-list type). Palettes: day first.
- **The hard 20%: conditional symbology procedures (CS)** — S-52 rules that
  are *code*, not table rows (safety-contour selection in DEPARE, LIGHTS
  sectors, WRECKS…). Port the ~6 procedures a base display needs,
  incrementally, behind a registry keyed by CS name; unimplemented ones fall
  back to a visible placeholder style (never silently drop features).
- **Performance: SENC** — parse S-57 once into a binary cache (the industry
  pattern), invalidated by file mtime; same role as CADRG's decoded-frame
  LRU. TilePack pre-render applies unchanged for iOS.
- **Mariner settings** (safety contour depth, shallow/deep shades) are
  IStyleEngine parameters, mirroring GeoSym's ISDM/IDSM knobs.

| Phase | Deliverable | Test | Depends on |
|---|---|---|---|
| E1 | ISO 8211 reader + S-57 feature/attribute/geometry extraction | pinned object counts/attrs/geometry for a NOAA cell | — (start anytime) |
| E2 | S-52 PresLib parse: lookup tables + symbol display lists + day palette | pinned lookups for DEPARE/BOYLAT/LIGHTS; symbol display-list goldens | PresLib file (IHO; also ships with OpenCPN data) |
| E3 | `S52StyleEngine` + first CS procedures (DEPARE/DEPCNT safety contour, LIGHTS) over the shared VectorRenderer | golden PNG of a NOAA harbor cell; visual sanity vs OpenCPN screenshot (not pixel parity) | V5 seam, E1, E2 |
| E4 | SENC binary cache; pan-viewer `enc` mode; TilePack pre-render | reopen-from-SENC identical hash; pan performance budget | E3 |
| E5 | S-57 update files; dusk/night palettes; more CS procedures | update-applied cell matches NOAA re-issued cell | E1–E4, on demand |

Shared sequencing note: V5 lands the seam; O-phases and E-phases are then
independent tracks (E1 and O1 have no V-dependencies at all and can slot in
whenever a session wants a self-contained reader to build). ENC/DNC on one
screen — a Savannah harbor with NOAA ENC over DNC bathymetry — is the
eventual two-track payoff demo.

## 7. Risks / open questions

- **GeoSym asset availability** (CGM + tables): required for V3+. Confirmed
  still absent after the full source tree arrived 2026-07-16 (no *.cgm
  anywhere) — the assets are FalconView *install data*, not source. Get the
  GeoSym data directory from a Windows install into TestData (git-ignored).
  Also capture reference screenshots of dnc17 harbors from the Windows build
  for V5/V7 golden comparisons.
- **CGM element coverage**: CGMFile.cpp handles a large element switch; port
  what GeoSym's own CGM files use (enumerable from the assets), not all of CGM.
- **Fill semantics**: GDI Polygon fill mode (ALTERNATE default), hatch
  brushes, and path-based wide lines need faithful CpuCanvas equivalents —
  decide winding rules at V5, pin with goldens.
- **DrawSAMILine** (patterned linework along a path) is GeoSym's hardest
  primitive — symbols repeated/oriented along polylines; budget it inside V5.
  (S-52 has the same concept — complex linestyles — so the along-path symbol
  placer built for V5 is shared by E3.)
- **OSM projection seam**: MVT is Web-Mercator tile-local; we transform
  per-vertex to lat/lon at decode. Correct for vectors, but label/line
  widths defined in tile pixels need a scale nudge at high latitudes —
  accept and document (charts, not cartographic perfection).
- **S-52 CS procedures** are open-ended; the registry + visible-fallback
  design bounds the risk (features never silently vanish).
- **Text parity**: GDI font metrics vs stb_truetype/CoreText will differ;
  golden tests must tolerate placement deltas (compare geometry, not pixels,
  for labels).
- **Fork resolution (V0) — RESOLVED 2026-07-16** (project files inspected):
  `VpfMapServer.vcxproj` compiles `Vpf/*.cpp`; `VPFDataLib` is a separate
  project consumed by VPFDataRenderServer and VvodAnalysisServer (different
  binaries — no single-binary ODR collision on Windows). Port **Vpf/** only;
  leave VPFDataLib Windows-only and never compile both into one POSIX
  binary. No renaming needed unless that changes.
