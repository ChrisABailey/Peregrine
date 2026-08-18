# Vector charts plan — VPF/GeoSym, plus OSM tiles & ENC (S-57/S-52)

**Status: MOSTLY BUILT — this stays the standing design reference for the vector/symbology
stack, not a to-do list.** Survey 2026-07-16. Built and finished: V1–V6 (the VPF reader, the catalog
rows, GeoSym, CGM symbols, the vector seam + `VectorRenderer`, the offline tile packer), §5's whole
cross-product middle layer (R1 identify, R2 rules, R3a–R3c the retained scene and the perf passes,
M1 mariner settings + data families), §6 OSM (O1–O6) and §7 ENC/S-52 (E1–E8) — see their rows in
`port/PORTING-ARCHIVE.md`. **Still open from this plan, and tracked in the ledger's §2**: V7 a
CoreGraphics `ICanvas` backend, V8 the symbol atlas / batching pass (R3c measured it away as a
non-goal at current frame times), WVS support (§2b, root cause known), and the canvas gaps §5 keeps
running into — no pattern brush, no clip region, no layer alpha, no label collision.
**The sections worth reading are §5 (the cross-product middle layer — why GeoSym, S-52 and MapLibre
are one machine) and §7 (ENC).** Everything phrased below as a future phase has either landed or is
in the ledger; where the two disagree, the ledger and the archive row win.
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

## 5. Cross-product middle layer: rules, identify, retained scenes (added 2026-07-25, per Chris)

Chris's asks for the ENC/OSM tracks — efficient rendering, a robust mechanism
for defining and applying scale-dependent feature rules, official S-52
symbology reusing the GeoSym infrastructure, and click-to-identify feature
metadata — are all **cross-product**, so they belong in the middle of the V5
seam rather than in any one style engine. This section is the design; §6 (OSM)
and §7 (ENC) then become loaders on top of it.

### 5.1 The convergence: GeoSym, S-52 and MapLibre are one machine

S-52 is not a cousin of GeoSym, it is the same system with IHO names. Mapping
all three products onto the concepts already built:

| Concept | GeoSym / DNC (built) | S-52 / ENC | MapLibre / OSM |
|---|---|---|---|
| dispatch key | FACC `f_code` + delineation | object acronym (DEPARE, BOYLAT) | source-layer + tag filter |
| lookup table | `fullsym.txt` rows | `.dai` LUPT / `chartsymbols.xml` `<lookup>` | style `layers[]` |
| attribute condition | `ATTEXP.TXT` expression | LUP attribute-value pairs (ATTC) | `filter` expression |
| draw order | `dispri` | DISP priority (+ radar flag) | layer order |
| visibility group | `vgroup` / `txtgroup` | viewing group + BASE/STANDARD/OTHER | layer id / class |
| scale filter | *(none in table; band = library)* | `SCAMIN` attribute | minzoom / maxzoom |
| symbol library | CGM → `fv::CgmSymbol` (V4) | vector defs (HPGL-ish instruction strings) | sprite sheet (raster) |
| colours | `COLOR.TXT` + brightness/contrast | DAY/DUSK/NIGHT colour tables | literal colours |
| complex lines | SAMI line style | `LC` complex linestyle | dasharray + line-pattern |
| area patterns | GeoSym area patterns | `AP` area pattern | fill-pattern |
| code rules | — | **CS conditional-symbology procedures** | — |
| metadata dict | `INT.VDT`/`CHAR.VDT` + FCA | S-57 App. A object/attribute catalogue | tag conventions |

Exactly one row (CS procedures) is genuinely ENC-only. Two consequences:

1. **`VectorSymbol` (style.h) is already the right currency.** S-52's vector
   symbol definitions reduce to the same polyline/polygon/ellipse/text set
   `fv::CgmSymbol` reduces to. No new symbol type is needed for ENC.
2. **The along-path symbol placer is shared.** GeoSym's SAMI line style
   (pending, Q6c item 2), S-52's `LC`, and OSM's line-pattern are one
   primitive; the area variant covers GeoSym patterns and S-52's `AP`. Build
   it once, in the renderer, not in a style engine.

**Restructure that follows:** extract a shared `LookupTableStyleEngine` core
into fvkit from what currently sits inside `port/GeoSymServer/fv_geosym_style.cpp`
— row table, memoized resolution, palette, symbol cache, placers. GeoSym,
S-52 and OSM become **loaders** that fill it, not three parallel engines.
Do the extraction when the *second* real table exists (see sequencing), not
speculatively against GeoSym alone.

**BUILT 2026-07-27 (E3c)** — `port/include/fvkit/vector/lookup_engine.h` +
`port/fvkit/vector/lookup_engine.cpp`; ledger row E3c has the full note. Cut
after BOTH engines were written and rendering, not merely after the second
table loaded. Three of the five listed pieces moved (memoized resolution =
the R2 rule layer, symbol cache, and the placers, which were already shared in
E3b) plus the label switch, the open flag and an unresolved-symbol counter;
**the row table and the palette deliberately stayed with the products** — the
two dispatch rules are not special cases of each other (GeoSym: every row whose
condition holds contributes; S-52: the first matching row of one of five tables
wins), and a palette reduces to "give me an FvColor", which is an interface
with nothing behind it. OSM now inherits the core rather than reproducing it.
Both goldens unmoved.

### 5.2 `fvkit/vector/rules.h` — the rule layer — **BUILT 2026-07-26 (R2)**

*As built* (ledger row R2 has the full note): `port/include/fvkit/vector/rules.h`
+ `port/fvkit/vector/rules.cpp`, with `GeoSymStyleEngine` retrofitted as its
first client. Four deviations from the sketch below, all recorded in the
ledger's Decisions: the `Action` set is `show|hide|set` — **`restyle` is
absent** because it needs style.h's vocabulary and rules.h sits below it;
`ViewingGroup` became a `ViewingGroupSet` runtime toggle plus a separate IMO
**display-category threshold** (GeoSym's `dispcat` = S-52's BASE/STANDARD/
OTHER, so it is genuinely cross-product); `ResolvedPlan` defers *every* rule
after the first per-feature one, not just the conditional ones, or source
order stops deciding; and the whole layer is **opt-in** — an empty RuleSet
leaves rendering bit-identical, which is what let it land without moving the
DNC golden.



One predicate AST with three front-end parsers (ATTEXP, S-52 ATTC, MapLibre
`filter`). Rule form:

```
Rule { FeatureKey match (layer/style_key, wildcards allowed)
       Predicate  cond          // attribute comparisons, and/or/not, exists
       ScaleBand  {min_denom, max_denom}
       ViewingGroup group
       int        priority_override
       Action     {hide | show | restyle | label_params | thin} }
```

Two design points carry the weight:

- **Split key-only rules from attribute rules.** Compile a `ResolvedPlan` once
  per (scale, settings-epoch), keyed on `{layer, style_key}`. Per-feature work
  then collapses to one hash lookup plus, usually, zero predicate evaluations.
  Today `GeoSymStyleEngine` re-walks its rows for every feature. This is the
  single biggest algorithmic win available in the style path, and it is what
  makes a rich rule set affordable rather than expensive.
- **A user rule file overrides the product tables**, with one syntax across all
  three products. This is the authoring mechanism Chris asked for, decoupled
  from GeoSym's and S-52's own table quirks; the product tables load as the
  base layer and the user file as an override layer.

Viewing groups become runtime toggles (S-52's BASE/STANDARD/OTHER, GeoSym's
`vgroup`/`txtgroup` — parsed since V5b but unused), i.e. real UI controls.

**Three distinct things are currently conflated as "scale"** — keep them apart:

1. *Which data to read* — DNC library band, OSM tile z, ENC cell usage band.
   Already handled by the L2 catalog's `BestSeriesForScale`.
2. *Which features survive at this viewport scale* — SCAMIN, `vgroup` thinning,
   user rules. This is rules.h, and it is what fixes V5b's unreadably dense
   labels (Q6c item 3).
3. *How big symbols/labels are, and what to drop on collision* — a placer with
   greedy collision detection. Also a net perf win: collided labels are never
   rasterized.

### 5.3 Identify: `FeatureRef` + lazy `Describe()`  — **BUILT 2026-07-25 (R1)**

*As built* (see ledger row R1 for the full note): the seam types landed in
`fvkit/vector/vector.h`, the pick index as `fvkit/vector/pick.h`, and the VPF
dictionaries as `port/VpfMapServer/fv_vpf_vdt.{h,cpp}`. Two deviations from the
sketch below, both recorded in the ledger's Decisions: `VectorFeature` still
carries its attributes (the GeoSym engine rules on them at style time — the
split is R3's `FeatureBatch` work), and `Describe()` is a *virtual with a
kUnsupported default* rather than a pure virtual, so ENC/OSM/test sources can
arrive without one.


`VectorFeature` currently carries `vector<pair<string,string>> attributes` on
every feature. Fine for 5,037 DNC features, brutal for OSM at z14. Split it:

- The render path carries a 12-byte `FeatureRef {source, tile, fid, layer}`.
- `IVectorSource` gains `Status Describe(const FeatureRef&, FeatureDescription*)`
  — full attributes fetched on demand and **decoded to human-readable text**,
  which is what makes a tap popup worth having:
  - **VPF**: `INT.VDT` / `CHAR.VDT` are present per coverage in TestData
    (`vpf/dnc17/*/hyd/`). Only the unported `VPFDataLib` fork reads them; they
    are plain VPF tables, so a small reader over the ported table layer covers
    it. Plus FCA class descriptions and the FACC name.
  - **S-57**: the official Appendix A object/attribute catalogue (acronym →
    long name, enumerated values → text), shipped as a generated table.
  - **OSM**: tag passthrough plus a small alias table.
- `FeatureDescription {title, class_name, layer_name, vector<{code, name, raw,
  display}> attributes, source_note}` — what a click popup binds to directly.

**The pick index is built from drawn geometry, not source geometry**, so
hit-testing matches what is visible: stroke width, symbol bounding box, label
box — not hairline centrelines. Return the whole stack ranked by display
priority so the UI can offer "3 features here" rather than guessing.

Note this supersedes §3's remark that identify would query `fv::VpfDatabase`
directly: that works, but it does not agree with what was drawn, and it does
not generalize to three products. Source-side query remains the right answer
for the *pre-rendered raster* iOS path, where no scene exists.

### 5.4 Retained tile display lists + symbol atlas

§2 already lists the atlas and per-tile display-list caching as the real wins,
and §4 parks them at V8 as a perf polish pass. **Move them earlier**: doing
identify properly needs the same retained structure, so it stops being polish
and becomes load-bearing.

- `TileDisplayList`, keyed by (tile, scale band, style epoch): flat fixed-point
  coordinate arrays + part offsets + resolved style handles + a parallel
  `FeatureRef` array, pre-simplified per band (Douglas–Peucker at ~0.5 px of
  the band's finest scale). LRU-bounded, same pattern as `CadrgFrameCache`.
- Render becomes affine transform + clip + rasterize. The equal-arc projection
  is linear in both axes, so pan and zoom *within a band* cost no re-query, no
  re-style, no re-decode. Simplification is render-only — identify goes back to
  the source through the `FeatureRef`, so precision is not lost.
- **Symbol atlas**: rasterize (symbol_id × size bucket × rotation bucket) once,
  instances become blits. Buoy fields and DNC navaid clusters currently
  re-walk the CGM display list per instance.
- **Columnar geometry**: `vector<vector<GeoPoint>>` costs one allocation per
  part per feature. Add a `FeatureBatch` columnar bulk path alongside the
  existing struct rather than churning V5a/V5b's working shape.
- Generalize ENC's planned **SENC** cache into a product-neutral pre-parsed
  source cache — VPF wants it equally.

### 5.5 Sequencing (chosen by Chris 2026-07-25: hybrid)

Deliberately does not build the shared abstraction until two real tables exist:

| # | Session | Why here |
|---|---|---|
| ~~R1~~ | ~~Identify: `FeatureRef`, `Describe()`, VPF VDT decoding, pick index, pan-viewer click→info panel~~ **DONE 2026-07-25** (ledger row R1) | Self-contained, rules-independent, immediately useful; no abstraction guesswork |
| E1 | ISO 8211 / S-57 reader | Zero dependencies, free NOAA cells, yields the second real product |
| ~~R2~~ | ~~`rules.h`~~ + ~~retrofit GeoSym~~ **DONE 2026-07-26** (ledger row R2) | Landed as designed and killed the dense-label problem (Q6c item 3). **The `LookupTableStyleEngine` extraction did NOT happen here and moved to E2/Q10**: §5.1's rule is to extract when the second real TABLE exists, and E1 delivered the S-57 reader (data), not `chartsymbols.xml` (table), which was not in TestData at the time. **It arrived 2026-07-26** (`TestData/enc/`), so E2 has both tables in hand. |
| ~~E2/E3~~ | PresLib parse + `S52StyleEngine` + CS registry + along-path placer **+ the deferred `LookupTableStyleEngine` extraction** — **ALL DONE 2026-07-27** as E2 / E3a / E3b / E3c (ledger rows) | Placer is shared back into GeoSym SAMI (Q6c item 2). **Ungated 2026-07-26**: `TestData/enc/` now has chartsymbols.xml (3057 lookups, 5 colour tables, 1093 symbols, 59 line-styles, 30 patterns), the raster symbol sheets, and the Appendix A catalogue. |
| R3 | Perf pass: tile display lists, symbol atlas, columnar geometry, simplification | Two products to profile against |
| O1–O3 | OSM | Lands on the finished middle layer; ends up small |

### 5.6 S-52 PresLib source (decided 2026-07-25)

**OpenCPN's `chartsymbols.xml`**, per Chris. Notes:

- **Licensing works here**: it is GPL-3.0, and Peregrine is GPL-3.0, so it is
  compatible *as data*. OpenCPN's C++ remains reference-for-behaviour only,
  never copied — the existing rule is unchanged.
- Treat it like the GeoSym assets: **git-ignored under `TestData/`, supplied at
  runtime via a data-dir argument**, not vendored into the repo.
- It is a derived encoding rather than IHO's original `.dai`, and it is
  conveniently complete: lookup tables, colour tables (day/dusk/night), vector
  symbol definitions and raster symbol sheets in one parseable file. XML is
  substantially easier to parse than the `.dai` fixed-format records.
- **Dependency consequence**: this needs an XML parser. **Use expat** —
  surveyed 2026-07-25, and the tree settles the choice:
  - `third_party/Expat 2.0.1/Source/lib` holds the **full expat source**
    (`expat.h` reports 2.1.0; the directory label is stale), MIT, only 3
    translation units — `xmlparse.c`, `xmlrole.c`, `xmltok.c` (the
    `xmltok_impl.c`/`xmltok_ns.c` pair is `#include`d, not compiled
    separately). Highly portable by design (ships amiga/mac/watcom configs).
  - **FalconView already uses it portably**: 7 files
    (`fvw_core/WMTComponents/{GDALXMLReader,WMSCapabilitiesReader,WMTDescriptor}.*`,
    `Components/data_abstraction_layer/.../DataViewConfigReader.cpp`) drive
    expat's SAX callbacks in plain `std::string`/`std::vector`/`std::stringstream`
    — no MFC, no COM, no Windows types. `GDALXMLReader.h` is a ready-made
    template for a portable SAX consumer, and those files are the natural
    starting point for Q12 (WMS) later.
  - The Windows product already links `libexpat.dll` (present in
    `fvw_core/WinDebug`/`WinRel`), so a shared `port/` target using expat adds
    no new Windows-side dependency.
  - **Not libxml2**, although `third_party/libxml2` (2.9.1) is also a complete
    source tree: 75 TUs, needs a generated `config.h`, and **no FalconView
    source uses it** — it is there as a transitive dependency of GDAL /
    libspatialite. Not tinyxml2/pugixml either: no reason to add a fourth
    option. Expat is SAX-only (no DOM/XPath), which for `chartsymbols.xml` is
    arguably the better fit anyway — stream straight into `LookupRow` /
    `VectorSymbol` instead of holding a multi-MB DOM.
  - **Which copy: RESOLVED 2026-07-25 — a current upstream release, already
    integrated.** The in-tree 2.1.0 dates from 2012 and has a substantial CVE
    history since (notably the 2022 `CVE-2022-2523x/2531x` cluster). Rather
    than use it for `chartsymbols.xml` and re-integrate later for Q12's
    network-supplied XML, expat **2.8.2** is now fetched by
    `port/third_party/CMakeLists.txt` and verified by
    `port/third_party/test/expat_smoke_test.cpp` (3 tests: nested
    elements/attributes shaped like PresLib rows, malformed-input error
    reporting, header-vs-library version agreement). E2 links target `expat`
    and needs no further setup. `third_party/Expat 2.0.1` is left untouched
    for the Windows product build.
  - Side benefit: ledger row 8 deferred `CoT` partly because it "would need
    expat/libxml2 from brew". That reason is void — expat is in-tree.

**MSXML is the non-portable bulk** (survey 2026-07-25): 148 files across
`Applications/`, `Components/`, `fvw_core/`, `Plugins/`, `UIControls/` use
MSXML/`IXMLDOMDocument` (Windows COM). None are on any current port path —
they cluster in overlay persistence and config, largely the already-deferred
buckets. If one is ever needed, sever it to an expat SAX reader rather than
shimming the DOM, using `GDALXMLReader` as the model.

### 5.7 Mariner depth settings — vessel draft drives the chart (survey 2026-07-25, per Chris)

Chris recalled that FalconView let the user enter a vessel depth and coloured
the bathymetry from it. **It does, the mechanism is fully in the ported tree,
and it is currently INERT in the port.** Surveyed end to end because the same
knobs are S-52's, so they belong in this middle layer, not in either style
engine.

#### How it works on Windows

The chain, all of it present in the repo:

```
  CDNCAdvancedDialog                 fvw_core/VPFMapOptions/DNCAdvancedDialog.cpp
    "DNC Advanced Options" dialog, group box "Water depth marking":
      [x] Mark regions with shallow areas          -> ISDM
      Depth coloring mode: 1 / 2 / 4 Color         -> IDSM
      Very Shallow [ 2 ] Meters                    -> MSSC
      Shallow      [10 ] Meters                    -> SSDC   <- the vessel knob
      Medium Deep  [30 ] Meters                    -> MSDC
        |
  CVPFPreferences (Get/SetSSDC ...)  fvw_core/.../VPFLib/VPFPreferences.cpp
        |  persisted as <SSDC>/<MSSC>/<MSDC>/<ISDM>/<IDSM> in the DNC overlay
        |  preference XML (Plugins/.../DefaultDncOptions.h)
        |
  IGeoSymServer propput SSDC/...     fvw_core/.../VPFInterfaces/GeoSymServer.idl
        |
  CCGMData::GetECDISValues()  ->  CECDISValues   <-- PORTED (V3)
        |
  CAEAttributeList::GetValue(name) falls through to
  CECDISValues::GetValueForString() when a rule names an attribute the
  FEATURE does not have — so `ssdc`/`msdc`/`mssc`/`isdm`/`idsm` act as
  PSEUDO-ATTRIBUTES in the ATTEXP rule language.                <-- PORTED (V3)
```

**The rule link is `ATTEXP` condition index == `fullsym.txt` ROW ID** (there is
no attribute-expression column in fullsym.txt — this is easy to get wrong).
Worked example, straight out of `TestData/GeoSymbol/SymAssign`:

```
fullsym  2257|5|BE010|3||||0820|0|…|depth area (medium deep); 4 shades
ATTEXP   2257|1|idsm|1|0|2      idsm == 0        AND      (0 = four-shade mode)
         2257|2|cvl|6|ssdc|3    cvl  >= ssdc     and
         2257|3|cvl|3|msdc|0    cvl  <  msdc
```

`cvl`/`cvh` are HYDAREA.AFT's "Depth Curve or Contour Value Low/High". The
BE010 area rows cover the whole ladder — 0805 very deep, 0820 medium deep,
0821 medium shallow, 0810 very shallow, 0949 shallow-mode pattern, plus
0823/0934/0631 for "depth unknown" (`cvl == 99999`). Soundings use the same
knob directly: fullsym 2318/2319 are BE020 "sounding text (dark) — shallower
than ssdc" / "(light) — deeper than ssdc", gated on `hdp <= ssdc` / `hdp > ssdc`.
ATTEXP operators: 1 `=`, 2 `!=`, 3 `<`, 4 `>`, 5 `<=`, 6 `>=`; last column is
the connector (0 none, 1 `or`, 2 `AND`, 3 `and`, 4 `OR` — two precedence
levels, lowercase binds tighter).

#### Where the port actually stands (verified 2026-07-25, not assumed)

1. ✅ **The rule engine works.** `fv::GeoSymStyleEngine` constructs
   `CAEAttributeList` with a `CECDISValues` and calls
   `att_exp.Evaluate(row.id, atts)`, so the depth conditions evaluate.
2. ❌ **The result is then thrown away.** Every depth-shade row's payload is
   in the **`areasym` column, which `Style()` parses into `SymRow::area_sym`
   and never reads** — the engine never sets `StyleResult::fill`, so
   `VectorRenderer` takes its stroke branch and DNC areas are drawn as
   BOUNDARY OUTLINES ONLY. Confirmed against the real harbor render: the
   visible blue/grey fills are point symbols (`hazardp`, `buoybcnp`, …), and
   `hydarea` contributes only thin strokes to the pick index.
3. ❌ **The knobs are not settable.** `CECDISValues ecdis;` is
   default-constructed and nothing can change it. Its defaults happen to be
   FalconView's own — ISDM 1, IDSM 0, SSDC 10 m, MSDC 30 m, MSSC 2 m — so the
   port is nominally "4-shade, 10 m safety contour", it just cannot draw it.

So this is **one missing feature (area fill) plus one missing API**, not a
porting gap in the rule layer.

#### The ENC/S-52 side — same idea, more formal

Chris's assumption is right, and the two map almost 1:1:

| DNC / GeoSym | S-52 / ENC | Meaning |
|---|---|---|
| `MSSC` | shallow contour | very-shallow / shallow boundary |
| `SSDC` | **safety contour** + **safety depth** | the draft-derived value |
| `MSDC` | deep contour | medium / deep boundary |
| `IDSM` (0 = 4-colour, 1 = 2-colour, 2 = off) | two-shades / four-shades | shade count |
| `ISDM` | shallow-water pattern on/off | pattern overlay |

One real modelling difference: **S-52 separates SAFETY DEPTH from SAFETY
CONTOUR** — the depth governs soundings and isolated-danger symbols, the
contour governs the area boundary (and S-52 *snaps* it to the next deeper
contour actually present in the cell, rather than using the typed number).
DNC conflates both into `SSDC`. The shared struct therefore needs both fields,
with DNC setting them equal. In S-52 the work happens in the CS procedures
already on the E3 list — `DEPARE01`/`SEABED01` (shade selection),
`DEPCNT02` (safety contour), `SNDFRM03`/`SOUNDG03` (sounding style above vs
below safety depth), and the danger-symbol procedures.

#### How this should be handled in the port

- **A product-neutral `MarinerSettings` struct in `fvkit/vector/`** —
  `shallow_contour`, `safety_contour`, `safety_depth`, `deep_contour`
  (all metres), `shade_count` (2 or 4), `shallow_pattern` (bool),
  `depth_shading_enabled`. Each style engine maps it to its own vocabulary
  (`GeoSymStyleEngine` -> `CECDISValues`; `S52StyleEngine` -> its CS inputs).
  Metres is the storage unit — DNC's dialog is already metres — with any
  feet/fathoms conversion at the UI edge only.
- **Carry it on `StyleContext`, not as engine state.** It is a per-render
  input like `scale_denominator`, and it must participate in R3's
  retained-scene "style epoch" so changing the draft invalidates cached
  display lists. Engine-held state would silently serve a stale chart, which
  on a safety-of-navigation setting is the worst possible failure.
- **Area fill has to land first** (`StyleResult::fill` from the `areasym`
  column, resolving the area symbol's CGM fill colour/pattern the way
  `LineStrokeFor` already resolves line symbols). Until then the setting has
  nothing to colour. This is the actual unit of work; the knobs are trivial
  once it exists.
- **Do it in R2**, when `LookupTableStyleEngine` is extracted with GeoSym and
  S-52 tables both in hand — the mariner settings are exactly the kind of
  cross-product rule input R2 exists to model, and `vgroup`/`txtgroup`
  thinning is being wired at the same time.
- **Identify should show it**: a depth area's popup ought to say which band it
  fell in and against which contour, since that is the whole point of the
  feature. §5.3's `Describe()` already carries `cvl`/`cvh`.
- **Quirk to preserve, decided when implemented**: the three sources of
  defaults disagree. `CECDISValues`' ctor and `DefaultDncOptions.h` both say
  ISDM 1 / IDSM 0 (shallow marking on, four shades), but
  `VPFPreferences.cpp`'s *fallbacks* say ISDM 0 / IDSM 1 (marking off, two
  shades) when the preference is absent. Match `CECDISValues` (it is what a
  fresh GeoSym server uses) and note the divergence rather than silently
  picking one.

## 6. Beyond VPF: OpenStreetMap vector tiles (added 2026-07-19)

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
  JSON we author (or a trimmed OpenMapTiles style). By the time O2 runs this
  is a **loader for the shared `LookupTableStyleEngine`** (§5.1), not a third
  standalone engine: MapLibre `filter` is a third front-end for the §5.2
  predicate AST, and minzoom/maxzoom are ScaleBands.
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
| O1 | `MbtilesSource` (tile pyramid metadata + tile fetch) + MVT decode to features (vendored vtzero) + `Describe()` (§5.3) | pinned tile/feature/layer counts + geometry sanity over a small Georgia mbtiles in TestData |
| O2 | `OsmStyleLoader` over the shared lookup core (JSON subset: fill/line/label by zoom) | pinned style decisions for known layers (water/roads/buildings) |
| O3 | render via shared VectorRenderer; pan-viewer `osm` mode; TilePack pre-render option | golden hash of an Atlanta OSM viewport; zoom in/out picks correct z |

## 7. Beyond VPF: ENC — S-57 data + S-52 symbology (added 2026-07-19)

ENC is the closest sibling VPF/GeoSym has — it IS the same architecture with
IHO names: S-57 (ISO 8211 container, feature objects like DEPARE/BOYLAT with
attributes) plays VPF's role; the S-52 Presentation Library (lookup tables +
a vector symbol library + day/dusk/night palettes) plays GeoSym's. A DNC
renderer and an ENC renderer should share everything above the reader/rules.

- **Data: own spec-driven ISO 8211 + S-57 reader** (like every other reader
  in this port), with GDAL's S57 driver as a cross-check, not a dependency.
  OpenCPN's code is GPL — reference for behavior only, never copied.
  **ARRIVED 2026-07-25** — see the ENC exchange-set format note below.
  S-57 update files (.001…) are a later phase; base cells (.000) first.

#### ENC exchange-set layout (as delivered by NOAA, in TestData 2026-07-25)

Chris downloaded four Charleston SC cells. This is the on-disk shape an
enumerator/reader has to cope with, recorded here so E1 does not have to
rediscover it:

```
TestData/enc/                      (git-ignored, like every other sample set)
  CATALOG.031                      exchange-set catalogue (see below)
  ENC_ROOT/ ENC_ROOT-2..4/         leftover shells: README.TXT,
                                   USERAGREEMENT.TXT, each set's CATALOG.031
  US5CHSDC/  US5CHSDC.000          base cell  (312 KB)
             US5CHSDC.001..003     UPDATE files, applied in sequence
             US253DC{A,B}.TXT      chart notes referenced by TXTDSC attributes
  US5CHSDD/  US5CHSDD.000 + .001
  US5CHSEC/  US5CHSEC.000 + .001
  US5CHSED/  US5CHSED.000 + .001 + .002
```

- **Four separate downloads, one exchange set each**, unzipped and the cell
  folders lifted OUT of their `ENC_ROOT/`. So **do not hardcode the standard
  `ENC_ROOT/<producer>/<cell>/` path** — enumerate by locating `*.000` files,
  which is also what makes a hand-assembled folder of cells work.
- **Cell naming is structured**, and free metadata: `US 5 CHS DC` =
  producer (US = NOAA) · **usage band** (1 overview, 2 general, 3 coastal,
  4 approach, **5 harbour**, 6 berthing) · chart-region code · cell id. The
  usage band is S-57's scale band — it is what `VectorQuery.scale_denominator`
  and R2's ScaleBand select on, the same role DNC's library plays.
- **The four cells tile a 2×2 grid** of 0.075° squares over Charleston
  Harbor — 32.70–32.85 N, 80.025–79.875 W — so they exercise cell-to-cell
  seams, and they overlap the 2026-07-23 Charleston DTED2/GeoTIFF refresh
  (ENC over DTED shaded relief is a real demo, not a contrived one).
  DC = "AIWW — Ashley River to Stono River", EC = "Ashley River",
  DD/ED = "Charleston Harbor".
- **`CATALOG.031` is itself an ISO 8211 file** — the same encoding as the
  cells, so the E1 reader parses it with the code it already needs. Its CATD
  records carry, per file: `RCNM, RCID, FILE, LFIL, VOLM, IMPL, SLAT, WLON,
  NLAT, ELON, CRCS, COMT` — i.e. **the cell's bounds and long name without
  opening the cell**, plus a CRC. That is exactly a `FrameInfo` row, so the
  catalogue is the cheap enumeration path when it is present (`.000` scan is
  the fallback when it is not, as here at the top level: the root
  `CATALOG.031` describes only the US5CHSEC set).
- **Update files are not optional.** `.000` is the base edition and `.001…`
  are incremental updates that must be applied **in order** to get the current
  chart; reading only the base silently yields a stale one. E1 may start
  base-only (E5 owns updates) but must **say so loudly** — a chart quietly
  showing superseded data is the worst failure mode in this whole product.
  US5CHSDC (three updates) is the test case.
- The `US253*.TXT` files are chart-note text referenced by cells' `TXTDSC`/
  `NTXTDS` attributes — identify (§5.3) resolves them; `Describe()` should
  read the file rather than show the filename.
- A leading-record sanity check for the reader: `US5CHSDC.000` begins
  `01582 3 LE1 09002 01 !` (ISO 8211 DDR) with field tags `DSID DSSI DSPM
  VRID ATTV VRPT SG2D` — S-57 dataset-identification then vector records.
- **Symbology: S-52 PresLib** — source decided 2026-07-25: **OpenCPN's
  `chartsymbols.xml`** (GPL-3.0, compatible with Peregrine as data; see §5.6
  for licensing, sourcing and the vendored-XML-parser consequence). Lookup
  tables (object class + attributes → symbology instructions) map 1:1 onto
  GeoSym's fullsym.txt role; the symbol library's vector definitions parse to
  display lists exactly like CGM symbols (reuse V4's `VectorSymbol`, per §5.1).
  Palettes: day first, but the file carries dusk/night too.
- **The hard 20%: conditional symbology procedures (CS)** — S-52 rules that
  are *code*, not table rows (safety-contour selection in DEPARE, LIGHTS
  sectors, WRECKS…). Port the ~6 procedures a base display needs,
  incrementally, behind a registry keyed by CS name; unimplemented ones fall
  back to a visible placeholder style (never silently drop features).
- **Performance: SENC** — parse S-57 once into a binary cache (the industry
  pattern), invalidated by file mtime; same role as CADRG's decoded-frame
  LRU. Generalized to a product-neutral pre-parsed source cache in §5.4 (VPF
  wants it too). TilePack pre-render applies unchanged for iOS.
- **Mariner settings** (safety contour depth, shallow/deep shades) are
  IStyleEngine parameters, mirroring GeoSym's ISDM/IDSM knobs.
- **Identify**: S-57's Appendix A object/attribute catalogue is the metadata
  dictionary for `Describe()` — see §5.3.

| Phase | Deliverable | Test | Depends on |
|---|---|---|---|
| E1 | ISO 8211 reader + S-57 feature/attribute/geometry extraction — **BUILT 2026-07-25**, `port/Enc/fv_iso8211.{h,cpp}` + `fv_s57.{h,cpp}` (base editions only, loudly; object/attribute codes stay numeric pending the Appendix A catalogue) | pinned object counts/attrs/geometry for a NOAA cell, plus the producer's own DSSI counts and CATALOG.031 bounds as cross-checks | — (**done**) |
| E2 | `chartsymbols.xml` parse: lookup tables + symbol display lists + palettes — **BUILT 2026-07-27**, `port/Enc/fv_s52_preslib.{h,cpp}` (all 5 colour tables, all 5 lookup tables, HPGL→`VectorSymbol`) + `fv_s57_catalog.{h,cpp}` (Appendix A). Deviation from the sketch: expat comes from `port/third_party` (2.8.2), not the vendored 2012 copy §5.6 assumed | pinned lookups for DEPARE/BOYLAT/LIGHTS; symbol display-list goldens — all present, plus 3 corpus sweeps that each found a real data quirk (ledger Decisions 2026-07-27) | — (**done**) |
| E3 | `S52StyleEngine` over the shared `LookupTableStyleEngine` core + first CS procedures (DEPARE/DEPCNT safety contour, LIGHTS) + along-path placer | golden PNG of a NOAA harbor cell; visual sanity vs OpenCPN screenshot (not pixel parity) | V5 seam, E1, E2, R2 |
| E4 | Pre-parsed source cache; pan-viewer `enc` mode; TilePack pre-render | reopen-from-cache identical hash; pan performance budget | E3 |
| E5 | S-57 update files; dusk/night palettes; more CS procedures | update-applied cell matches NOAA re-issued cell | E1–E4, on demand |

Shared sequencing note: V5 landed the seam; §5 lands the middle layer (rules,
identify, retained scenes) that E- and O-phases both consume. E1 and O1 have no
dependencies at all and can slot in whenever a session wants a self-contained
reader to build; the interleaving actually chosen is the hybrid table in §5.5.
ENC/DNC on one screen — a Savannah harbor with NOAA ENC over DNC bathymetry —
is the eventual two-track payoff demo.

## 8. Risks / open questions

- **GeoSym asset availability** (CGM + tables): **RESOLVED 2026-07-21** —
  `TestData/GeoSymbol/{SymAssign/*.txt, Graphics/*.cgm}` (757 CGM symbols;
  Chris flattened the original double-nesting). Reader appends
  `GeoSymbol\...`, so DataDir = `TestData`. V3/V4 unblocked. Still wanted:
  reference screenshots of dnc17 harbors from the Windows build for V5/V7
  golden comparisons.
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
- **Over-abstracting the middle layer** (added 2026-07-25): §5's rules engine
  and lookup core are designed from GeoSym plus a *reading* of S-52 and
  MapLibre, not from two implemented tables. Mitigated by the §5.5 hybrid
  order — R2 (the extraction) runs only after E1, so the shared shape is cut
  against real S-57/S-52 data rather than an anticipated one. If a session
  finds the abstraction fighting either product, prefer two concrete engines
  over a leaky common one.
- **`chartsymbols.xml` fidelity** (added 2026-07-25): it is OpenCPN's derived
  encoding of the PresLib, not IHO's `.dai`. Expect OpenCPN-specific
  extensions and omissions; pin what the file actually contains, and treat
  divergence from the published S-52 spec as a documented deviation rather
  than a bug to chase. Swapping in a real `.dai` later should be an E2 loader
  change only, so keep the parse behind the loader seam.
- **Identify vs pre-rendered rasters** (added 2026-07-25): the §5.3 pick index
  exists only where a live vector scene does. The TilePack/iOS raster path has
  no scene, so it must fall back to a source-side spatial query (§3) — which
  can disagree with the baked pixels at the margins. Accept and document.
- **Text parity**: GDI font metrics vs stb_truetype/CoreText will differ;
  golden tests must tolerate placement deltas (compare geometry, not pixels,
  for labels).
- **Fork resolution (V0) — RESOLVED 2026-07-16** (project files inspected):
  `VpfMapServer.vcxproj` compiles `Vpf/*.cpp`; `VPFDataLib` is a separate
  project consumed by VPFDataRenderServer and VvodAnalysisServer (different
  binaries — no single-binary ODR collision on Windows). Port **Vpf/** only;
  leave VPFDataLib Windows-only and never compile both into one POSIX
  binary. No renaming needed unless that changes.
