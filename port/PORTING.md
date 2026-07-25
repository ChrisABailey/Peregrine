# FalconView Cross-Platform Port — Ledger

**Read this first in every session. Do not re-explore the repo.**
Full strategy: `/Users/chrisbailey/.claude/plans/this-project-is-extreamly-enumerated-marshmallow.md`
(key facts repeated here so this file is self-sufficient).

## Status legend
`—` not started · `C` compiles on macOS · `T` tests pass · `P` exposed in Python

## Module ledger (porting order)

| # | Module | Files | Status | Notes |
|---|--------|-------|--------|-------|
| 1 | fvw_core/geoid | 6 | T | Ported as `port/geoid/fv_geoid.{h,cpp}` (class `fv::GeoidCalculator`); COM wrapper left Windows-only; registry lookup replaced by data-directory ctor arg. 7 gtests, synthetic grids. |
| 2 | fvw_core/geo3 | 10 | T | Compiled in place (`port/geo3/`) over new `port/geotrans/` target (msp_ccs = third_party GEOTRANS 3.3, unmodified, POSIX-ready). Registry prefs → in-memory map; MessageBox/OutputDebugString → stderr stubs; upstream pinned suite runs via GeoTransPinnedSuite. MSPCCS_DATA points at fvw_core/PdfLib/sdk/lib. |
| 3 | fvw_core/geo_tool (+GeoTool=COM twin, Windows-only) | 36 | T | Compiled in place (`port/geo_tool/`), links fv_geo3. WMM geodata dir via `FVW_GEODATA_DIR` env var on POSIX (falls back to in-memory WMM-2000 table). `GEO_get_formatted_location_string` (returns CString) is Windows-only. 9 gtests incl. pinned JFK–LHR great-circle/rhumb values. |
| 4 | fvw_core/MapScaleUtil | 6 | T | Extracted as `port/MapScaleUtil/fv_map_scale_util.{h,cpp}` (`fv::MapScaleUtil`); COM wrapper Windows-only. Enum mirrored in `port/include/fv_map_enums.h` (values = COM ABI, never renumber). 9 gtests, pinned scale/dpp values. |
| 5 | fvw_core/MapSeriesStringConverter | 6 | T | Extracted as `port/MapSeriesStringConverter/` (`fv::MapSeriesStringConverter`, std::wstring API; BSTR/VARIANT COM wrapper Windows-only). Format enum added to `fv_map_enums.h`. 9 gtests. Quirks: ToMapSeries leaves out-params untouched for unhandled formats (still S_OK); MSVC wide `%s` → standard `%ls`. |
| 6 | fvw_core/MdsUtilities | 13 | DEFERRED | 2026-07-11 survey: Windows system plumbing (SQL Server service ctl, registry ACLs/Sddl, USB DeviceIOCtl, volume serials, shell API, message pump, SEH filter). No portable map logic worth extracting now; pull individual helpers (HashString, date utils) only if a Phase-3 reader needs them. |
| 7 | fvw_core/NetNMEA | 20 | T | `nmea.cpp` (NMEA_sentence parser) compiled in place + upstream unit tests. Windows-only: gps.cpp/comm.cpp/poller.cpp/INetNMEA.cpp (COM feed, serial I/O, threads); gps.h COM classes guarded. New `port/include/fv_oledatetime.h` (COleDateTime emulation, ~52 files repo-wide will reuse). 11 gtests. |
| 8 | fvw_core/CoT | 32 | DEFERRED | 2026-07-11 per Chris: not needed for the functionality being ported. Revisit only on demand (would need expat/libxml2 from brew). |
| 9a | ImageLib codecs (zlib/png/jpeg/jpeg12/tiff) | ~160 | T | `port/ImageLib/`: fv_z, fv_png, fv_jpeg(+8/12-bit), fv_tiff (+`fv_tif_posix.c` glue — tree only shipped tif_win32.c), fv_gdal_jpeg (tiff's C jpeg per tiff.vcxproj). 5 round-trip gtests, synthetic images. Excluded from libs: cjpeg/djpeg tool files, pngtest/PngFile, zlib example. |
| 9b-1 | ImageLib core: CJpeg wrapper (jpeg/jpeg.cpp) | 1 | T | `port/ImageLibCore/`. Decodes real TIROS .WLD JPEGs. CString varargs sites cast `(LPCSTR)` (15, compiler-enumerated). C++14 target (std::auto_ptr in jpeg.h). 4 gtests. |
| 9b-2 | ImageLib core: CGeoTiff reader + support (geotiff/geotiff2/fid/image_d/transfrm/Util/MetadataStructs + SVD) | ~15 | T | `fv_imagelib_geotiff`: 25K-line CGeoTiff decodes real DOQs (load/pixels/geo round-trip, 3 gtests). Util.cpp compiled with ~30 Windows methods guarded (registry/GDI/MSXML/COM callbacks/HANDLE I/O) — POSIX policies in `fv_imagelib_util_posix.cpp`. New `fv_mfc_containers.h` (CList/CArray, POSITION idiom). RPC height refinement returns "no DTED" headless (CDTEDInstance stub; TODO wire fv::DtedCell). |
| 9b-3 | ImageLib gif | 4 | C | `fv_imagelib_gif` compiles (CGif + pov_io). No test yet — no .gif sample data; add when a consumer needs it. |
| 9b-4 | ImageLib rest (Image.cpp COM object, nitf/, Dib.cpp) | ~150 | — | Image.cpp = multi-format dispatcher (sever); nitf/ next when NITF imagery matters. |
| 10 | MapDataServer/DtedMapServer | — | T | `port/DtedMapServer/fv_dted_cell.{h,cpp}` (`fv::DtedCell` ← CDtedMemoryMappedFile) over new `port/include/fv_filemap.h` (mmap RAII). COM service (CDted caches/block fills/level fallback) Windows-only. 11 gtests: synthetic cells + real .dt1 plausibility and pinned elevations. |
| 11 | ImageLib/cadrg decoder (CadrgMapServer COM layer still Windows-only) | 24 | T | `port/CadrgDecoder/`: full RPF/VQ frame decode (RPFRenderer::get_rgb_image) in place. New `port/include/fv_cstring.h` (minimal CString, grow test-first). 3 real-data tests: GNC frame decodes to plausible chart pixels, deterministic, pinned checksum; one frame per map type/zone across cgnc/cjnc/clfc/ctlm50. |
| 12 | MapDataServer/GeoTIFFMapServer | — | T | GeoTiffFrameFile.cpp + projsp.cpp + codes.cpp compiled in place (`port/GeoTIFFMapServer/`); COM surface guarded, portable twin `fv::GeoTiffFrame`. **First COM severing**: DatumConvertServer → `fv::IDatumConvert` (port/include/fv_interfaces.h) backed by ported GEOTRANS. 3 real-data tests vs TestData DOQ quarter-quads (NAD→WGS84 shift, pixel/geo round-trip). |
| 13 | FvKit L0 geo primitives + L1 DTED adapter | — | T | First FvKit layer per `port/fvkit-contracts.md` (approved 2026-07-14; D1–D6 pin ownership/geo/Status/pixel/naming/adapters). `port/include/fvkit/{geo.h,formats/{source.h,enumerate.h,dted.h}}` + `port/fvkit/formats/dted.cpp`. GeoRect antimeridian wrap tests; DtedFrameEnumerator (path-derived bounds, case-insensitive, 22 TestData cells) + DtedElevationSource (meters/NaN per D4, prototype .DT3 → kIoError). 15 gtests (99 total). |
| 14a | FvKit L1 GeoTIFF adapter + format registry | — | T | `raster.h` (PixelBuffer RGBA8, ImageInfo — no stored affine, exact transforms are IRasterSource virtuals) + `IRasterSource` + `registry.h` multi-slot RegisterFormat (own header, not enumerate.h). `GeoTiffRasterSource` (pimpl over CGeoTiff, TU pinned C++14 — auto_ptr in ImageLib headers) + `GeoTiffFrameEnumerator` (header-read enumeration via GeoTiffFrame; unsupported frames skipped+logged). 10 gtests (106 total), 14 TestData quads. |
| 14b | pyfvw slice 1 (pybind11) + pan-viewer demo | — | P | Resequenced before CADRG per Chris 2026-07-15. `port/bindings/pyfvw/`: geo types, FvError(.code/.message), PixelBuffer (buffer protocol → zero-copy numpy (h,w,4)), FrameInfo, enumerators, DtedElevationSource, GeoTiffRasterSource; GIL released around decode; ICD-MAPPING.md started. 8 pytest cases run via ctest (107 total). `demo/pan_viewer.py`: tkinter continuous-map arrow-key pan over the 14 DOQ quads, ~100 ms/redraw, seamless at 4-quad corners. |
| 14c | CADRG frame georeferencing (fv::CadrgFrame) | — | T | Half A of the CADRG adapter (per Chris, split 2026-07-15). `port/CadrgMapServer/fv_cadrg_frame.{h,cpp}`: extracts base-34 frame-number decode + equal-arc/polar corner math from the Windows-only CRpfFrameFile, reuses CArcZone/PolarUtil from fv_cadrg, adds a MIL-STD-2411A series→scale table (gn/jn/lf/tl cross-checked). `ReadCadrgFileCoverage` reads the frame's embedded RPF coverage section. 7 gtests; computed bounds match embedded coverage EXACTLY across GNC/JNC/LFC/TLM (114 total). |
| 14d | CADRG Half B: IRasterSource + LRU cache + pyfvw + chart demo | — | P | `CadrgRasterSource` (IRasterSource over RPFRenderer + fv::CadrgFrame; decodes full 1536² frame once, crops from cache; equal-arc pixel↔geo, polar→kUnsupported), `CadrgFrameEnumerator`, and `CadrgFrameCache` (LRU decoded-frame cache — plan risk #1 — keyed by path, cap 24). Registered as "cadrg". Bound to pyfvw (BindRasterSource<T> factored from GeoTIFF). pan_viewer.py gains `cadrg` mode. 6 gtests + 3 pytests (120 total). Real LFC/GNC frames VQ-decode + tile seamlessly. |
| 14e | TIROS adapter (fv::TirosFrame + IRasterSource) + auto_ptr sweep | — | P | Extracted `port/TirosMapServer/fv_tiros_frame.{h,cpp}` (fv::TirosFrame: filename→series/col/row→equal-arc bounds, verbatim from CTirosFrameFile + tirostab). `TirosRasterSource` (CJpeg decode, whole-tile-once cache, equal-arc transform; reads real JPEG dims in Open) + `TirosFrameEnumerator`; registered "tiros"; bound to pyfvw; pan_viewer `tiros` mode (TopoBath over the Mediterranean). 8 georef + 4 adapter gtests + 1 pytest (128 total). **Also the auto_ptr sweep** (commit before this): std::auto_ptr→C++17 across the FvKit path (unique_ptr<T[]> for verified arrays, fvw_auto_ptr shim for shared transfer-on-copy typedefs); unpinned fv_cadrg/fv_cadrg_frame/jpeg_wrapper/geotiff to C++17 — so this TIROS source is C++17 from the start. |
| 14f | FvKit L2 catalog (SQLite + R-tree) | — | P | `fvkit/detail/sqlite.h` (RAII Db/Stmt, system SQLite) + `fvkit/catalog/catalog.{h,cpp}`: schema mirrors MDM tblDataSources/tblMapSeries/tblCoverage; Scan drives the format registry (FrameInfo gained scale/scale_units — carried as int: decoder mapscales.h and fv_map_enums.h both define MapScaleUnitsEnum and must never meet in a TU); antimeridian coverage stored as 2 R-tree boxes (id=cov*2+piece), queries split+de-dupe per D2; BestSeriesForScale normalizes via ported MapScaleUtil (meters/km→denominator). Bound to pyfvw (`pyfvw.catalog`); demo frame selection retired to the catalog (0.05 ms viewport query). 7 gtests + 1 pytest (135 total). |
| 14g | FvKit L2.5 canvas (ICanvas + CpuCanvas) | — | P | `fvkit/canvas/canvas.h` (ICanvas mirroring IGraphicsContext2's primitive set — but with Pen/Brush/TextStyle VALUE structs per call instead of GDI factory objects, documented deviation) + `cpu_canvas.{h,cpp}` (non-AA scanline even-odd fill = GDI ALTERNATE, Bresenham lines w/ square nib + dash, ellipse-as-64-gon, src-over blit, stb_truetype text — vendored `port/vendor/stb_truetype.h`). Golden tests: FNV-1a pinned hashes (visually verified PNGs first) + libpng round-trip via fv_png; text asserts ink/extents only (host fonts). Bound as `pyfvw.canvas.CpuCanvas`. 7 gtests + 1 pytest (142 total). NOTE: DrawRectangle corners are INCLUSIVE (GDI Rectangle excludes right/bottom) — revisit at GeoSym parity (V5 goldens). Text is ASCII-only for now. |
| 14h | FvKit L3a projection + L3b engine + fvrender CLI | — | P | `fvkit/proj.h` (equal-arc MapProjection; dpp via ported MapScaleUtil so it matches FalconView; lon deltas unwrapped around center → antimeridian viewports need no caller special-casing) + `fvkit/engine.h` (MapEngine: catalog SelectByGeoRect → per-frame source via registry+LRU → corner-exact/linear-between resample → CpuCanvas; interrupted-callback per plan; GetElevation via attachable IElevationSource). CoverageRow gained `format`. **`fvrender` CLI = the plan's L3b milestone**: `fvrender --center "33 44 55.7 N 84 23 17.5 W" --scale 500000 --series LFC --out map.png` works headless (DMS/MGRS via geo_tool). Engine golden hash pinned after visual check; synthetic dateline-frame test (caught+fixed a +360 width bug in the crossing-frame overlap math). Bound as `pyfvw.engine`. 9 gtests + 1 pytest (151 total). NOTE: per-frame resample is corner-exact, linear between (affine approx for projected GeoTIFF over a viewport — documented in engine.h). |
| 14i | FvKit L4 overlay SPI + manager + grid + Python trampoline | — | P | `fvkit/overlay/{overlay.h,manager.h,grid.h}`: Overlay base (IFvOverlay+Renderer+UIEvents folded into ONE class — documented deviation; a single trampoline is what pybind subclassing needs; Persistence/Editor/IMapView deferred to first consumer), OverlayManager (bottom-up draw, top-down routing until handled, stack ops, failures name the overlay), built-in GridOverlay (graticule; golden hash pinned after visual check). pyfvw: ICanvas base now carries the draw methods; `pyfvw.overlay.Overlay` is Python-subclassable (on_draw/on_mouse_*/on_key_down; exceptions contained → FvError per D3). **Resolved the plan's trampoline-lifetime risk**: a temporary Python overlay silently lost its overrides (Python half GC'd behind the shared_ptr); fixed with keep_alive(manager, overlay) + regression pytest. 6 gtests + 1 pytest (157 total). |
| 14j | Pan-viewer over MapEngine + overlays (queue Q1) | — | P | Demo's Python compositing loop retired: pan_viewer.py is now a thin tk shell over engine.render + OverlayManager (grid + Python Crosshair overlay). New: +/- zoom (engine resamples), g grid toggle, click re-centers via surface_to_geo with DTED elevation readout through engine.get_elevation, --shot PNG mode for headless capture. Verified at 1:500k (2 frames) and 1:2M (9 frames). Zoom-out perf note: 9 fully-VQ-decoded frames ≈ 1.2 s — the pending "subsampled ReadBlock" item is exactly this cost. |
| 14k | L5 store: GeoPackage TilePack (queue Q2) | — | P | `fvkit/store/tile_pack.{h,cpp}`: TilePackWriter (OGC GeoPackage 1.2 tiles profile, EPSG:4326 matrix, PNG blobs via fv_png in-memory codecs; level 0 = whole pack in one tile, each level halves dpp; empty tiles skipped, transparent background) + TilePackRasterSource (one level as a flat IRasterSource, stitched ReadBlock, exact linear transforms) + TilePackEnumerator (one FrameInfo per level, path "<file>#z=k", so **pyramid levels are catalog series** and BestSeriesForScale picks the level). Registered as "gpkg" (5 builtin formats now). MapProjection/MapEngine gained SetResolution (exact latitude-independent dpp — tile grids can't ride MapScaleUtil's lat-dependent dpp). `fvpack` CLI builds packs offline (Atlanta LFC 4-level = 6 MB). **Key test: pack tile is byte-identical to the direct engine render**, and the engine renders from a pack it wrote via the registry (full circle). detail/sqlite.h gained blob bind/col. 6 gtests + 1 pytest (160 total). QGIS external validation: open scratchpad/atlanta_lfc.gpkg (plan's suggestion) — pending Chris. |
| 14l | VPF V1: headless DNC reader (queue Q3) | — | T | `port/VpfMapServer/` compiles the non-GDI half of VpfMapServer.vcxproj's Vpf/ sources **in place** (indexes, lst_iter, tables, variant, vpfdb, vpfrcset). Reads real TestData/vpf/dnc17: 11 libraries, coverages from the CAT, tile grids, feature classes, and feature-table rows. Excluded: vpf_tree.cpp (VPF_copy_elements — copy-to-removable-media, not the spatial tree, needs MdsUtilities + shell) and the GDI/topology files (vpfelem/VPFFace/vpf_poly/tile/VPFText — phase V5). VPFDataLib stays untouched (diverged fork, V0). Three new Win32-idiom headers: `fv_win32_filemap.h` (CreateFile/CreateFileMapping/MapViewOfFile → mmap), `fv_win32_finddata.h` (FindFirstFile/FindNextFile → readdir), `fv_win32_path.h` (backslashes + case-insensitive resolution at the file-open boundary, so hardcoded `\\tileref\\` literals and mixed-case DNC names work unmodified — and Linux works, not just case-insensitive macOS). fv_mfc_containers gained CMap + CStringList + CList::Find; fv_cstring gained FindOneOf/SpanExcluding/SpanIncluding/Find(ch,start) and **operator+(CString,char) + the ordering operators** (see Decisions — both were silent-corruption bugs). 16 gtests, clean under ASan+UBSan (176 total). |
| 14m | VPF V2: DNC coverage → catalog rows (queue Q4) | — | P | `fvkit/formats/vpf.{h}` + `vpf_enum.cpp`: `VpfFrameEnumerator` walks a DNC database's libraries → tiles into FrameInfo rows over the fv_vpf reader (bridges the CString/MFC reader to std::string FrameInfo at the boundary, like geotiff_enum). Granularity: one row per (library, tile); `series_key` = library; `path` = `db\|library\|tile` locator (MakeVpfLocator/ParseVpfLocator for V5's future vector source). Registered "vpf" (6 formats). Guards: dht-file check (DNC has no root `lat`); **skips untiled + empty-name + out-of-Earth-range rows** — the DNC `browse` thumbnail library's `bounds()`/tile list read an uninitialized extent (non-deterministic garbage), so only named tiled coverage is catalogued (deterministic; verified 4× under ASan). Bound to pyfvw. 5 gtests + 1 pytest (180 total): Nantucket Sound viewport → harbor libs, Atlanta → empty. |
| 14n | GeoSym V3 (of Q5): headless rule-table layer | — | T | `port/GeoSymServer/` compiles the **non-GDI foundation** of GeoSymServer in place: `DelimitedParser` (`|`/`\t` table tokenizer over a new portable `CStdioFile`), `AttributeExpressions` (ATTEXP.TXT rule engine — CVPFFormatHandler/CAEValue/CAEAttributeList/CAEEntry/CAttributeExpression(s), CECDISValues mariner ISDM/IDSM/SSDC knobs), `SymColors` (COLOR.TXT → COLORREF table + brightness/contrast adjuster). Reads real `TestData/GeoSymbol/SymAssign` (DataDir=TestData; backslash locator resolved case-insensitively at CStdioFile::Open). **Left Windows-only (V4/V5)**: SanSymbol (symbol assignment + drawing), SymText/draw_text (GDI text), SymLayeredDisplay (offscreen-DC compositing), CGMFile/CGMData/CGMFileCache (CGM interpreter), GeoSymServer.cpp (StartRendering/DrawSymbol over CDC), SimpleLoc — all GDI/CGM-coupled. New shims: fv_compat CStdioFile + strtok_s/strnlen_s/_tcstol/_tcstod/_tcschr/_tcsstr/IsCharAlpha/IsCharLower; fv_cstring CString(ptr,len) + TrimLeft/Right(char); fv_mfc_containers CList::RemoveTail + const GetNext. 5 gtests (pinned COLOR.TXT entries, brightness math, ATTEXP rule eval for exs/nam conditions). Fixed 1 UB (CAEValue::operator= had no return). **185 total.** |
| 14o | GeoSym V4 (rest of Q5): CgmSymbol parser → display list | 2 | T | `CGMFile.cpp` (3.2K binary CGM element switch) compiled **in place**; its GDI half severed with `#ifdef _WIN32` (per-class `Draw(HDC)`, `CreatePen`/`CreateBrush`, the `FontDesc` typeface table, `CCGMPolygon::m_bitmap_pattern`). New portable seam `port/GeoSymServer/fv_cgm_symbol.{h,cpp}`: **`fv::CgmSymbol`** flattens the MFC `CCGMDrawingObject` graph into a C++17 display list (`CgmElement`: polyline/polygon/polygon-set/ellipse/elliptical-arc/text) in **symbol-local VDC coords, unrotated and unscaled** — V5 supplies the transform. Reads `m_vertices`, NOT `m_disp_vertices` (the latter is the GDI-rotated copy). New shims: fv_compat `CPoint`/`CSize`/`CRect` + `Interlocked*`/`UnionRect`/`SetRectEmpty`/`OPAQUE`/`TRANSPARENT`/`_ASSERTE`; fv_mfc_containers `CObject`, `CTypedPtrList`/`CTypedPtrArray` (+ `CPtrList`/`CPtrArray` tag types, owning nothing like MFC), `CArray::SetAtGrow` and MFC's 2-arg `SetSize`. New tool `port/tools/guard_win32_functions.py` (brace-matched `#ifdef _WIN32` wrapping; V5 will reuse it). 9 gtests: 0003.cgm pinned element-by-element (all 4 geometry kinds), 0001.cgm polygons/colors, reload-replaces, determinism, and a corpus sweep — **757/757 TestData symbols parse, 5339 elements, clean under ASan+UBSan**. **196 total.** **Quirks preserved & documented**: (1) unknown CGM opcode = `ASSERT(false); break;` — MFC compiles ASSERT out of Release so Windows SKIPS the opcode, but our `<cassert>` ABORTS a debug build, so never hand this parser untrusted bytes headless (truncation, the realistic mode, correctly returns `E_CGM_UNEXPECTED_EOF` — pinned). (2) **`LONG` is `long` on Win32 but `int32_t`(=`int`) here, and these sources mix the spellings freely** — 4 declaration/definition pairs (`ReadScaledInteger`, `ReadAttribute`, `UsesLineOrFillColor`, `CCGMText::Initialize`) were hard errors until aligned to the declaration's spelling; expect this in every future in-place module. Also fixed 2 MSVC-only constructs (a `CCGMFile::` qualifier inside the class body; a temporary bound to `POINT&`). **Not compiled by MSVC from here** — shared edits are guards + additive accessors + const-correctness MSVC already accepts. Next: V5 = SanSymbol assignment (fullsym.txt) + VpfRenderer over ICanvas; CGMData/CGMFileCache go WITH V5 (CGMData.h includes SanSymbol.h + SymText.h), not with the parser. Pending (unchanged): **VPF reader UBSan alignment** (vpfrcset/tables unaligned scalar loads; ASan-clean); VPFRecordset reopen row-undercount; subsampled ReadBlock; polar CADRG transforms; TIROS tile-seam; C++14 pins fv_jpeg/fv_jpeg12/fv_imagelib_gif; CIB is_cib gap; FeatureStore (other half of L5). (Resolved 2026-07-21: **DTED test-count drift** — TestData/dted grew to 24 cells (W084/n35, w085/n33, w086/n33 added); the hardcoded `== 22` in dted_adapter_test/catalog_test/test_pyfvw bumped to 24 (23 .dt1 + 1 .DT3).) |
| 14p | VPF V5a (of Q6): FvKit vector seam + `fv::VpfVectorSource` | 3 | T | First half of V5, split per Chris 2026-07-23 (V5 as written = renderer + style engine + symbol assignment + golden PNG, several sessions). **New generic seam** `port/include/fvkit/vector/vector.h`: `IVectorSource` + `VectorFeature{type, style_key, layer, parts, bounds, attributes, tile_id, feature_id}` + `VectorQuery{area, scale_denominator, max_features}` — product-neutral on purpose, so OSM (Q7/Q9) and ENC (Q8/Q11) ride the same middle+right side rather than forking. `style_key` is a string (VPF puts the FACC code there). **`port/VpfMapServer/fv_vpf_vector_source.{h,cpp}`** implements it over the V1 reader: SIMPLE feature classes are a pure table join, no topology — `<COV>/<CLASS>.LFT` rows give `(f_code, tile_id, edg_id)`, then the tile's `EDG`/`END`/`CND` gives coordinates; `tileref/TILEREF.AFT` maps tile_id→directory; primitive recordsets are cached (incl. negative) since a feature table reopens them per row. Reads real dnc17 h1707300: **4,535 features (1,699 line + 2,836 point) across 23 populated layers, 32,473 vertices**, ASan-clean. 13 gtests (**209 total**), pinning hydline/coastl/soundp geometry. **NOT here (V5b)**: AREA features — a `.AFT` row points at a FACE, and face→ring→edge needs the topology in Windows-only VPFFace/vpf_poly, which pull in the FalconView APP layer (map.h, mapx.h, graphics.h, param.h registry, refresh.h) — that coupling is exactly why the plan says "sever at ICanvas, not at GDI". Also V5b: `IStyleEngine` (GeoSym/SanSymbol) + `VectorRenderer` + golden PNG. **Found and fixed a latent V1 bug** — see Decisions 2026-07-23. Tile-level culling still a TODO (`Bounds()` currently costs a full scan; tile bounds need the tileref face, so per-feature bounds are used instead — the 14m catalog already stores per-tile coverage for this). |
| 14q | VPF V5b (of Q6): style seam + GeoSym style engine + VectorRenderer + golden chart | 5 | T | Second half of V5 as split 2026-07-23, minus areas (now V5c). **The middle and right of the vector seam.** New `port/include/fvkit/vector/style.h`: `IStyleEngine` + `StyleResult{priority, stroke, fill, symbol, label}` (Style() APPENDS several passes — a GeoSym row can paint at more than one priority) + **`VectorSymbol`**, a product-neutral point-symbol display list (polyline/polygon/ellipse/text, HIMETRIC units, y-up) so S-52 and OSM sprites ride the same renderer. New `port/include/fvkit/vector/renderer.h` + `port/fvkit/vector/renderer.cpp`: **`fv::VectorRenderer`** — query viewport → style → stable-sort by priority ACROSS features → project → clip → ICanvas. Replaces CSymLayeredDisplay's N offscreen DCs with one pass into one buffer (plan's call). Exposed `ClipPolyline` (Cohen–Sutherland, emits runs) and `ClipPolygon` (Sutherland–Hodgman) as testable free functions. New `port/GeoSymServer/fv_geosym_style.{h,cpp}`: **`fv::GeoSymStyleEngine`** = the portable replacement for Windows-only SanSymbol.cpp — reads fullsym.txt (687 DNC rows) through the V3 `CDelimitedParser`, evaluates ATTEXP conditions per row, resolves COLOR.TXT/TEXT.TXT, loads Graphics/*.cgm through V4's `fv::CgmSymbol` (cached), and reproduces the **two-chance fallback** verbatim (unknown FACC → `icon`/`line` 5000/5001; known FACC with no true condition → `defpt`/`defln` 0051/3113). `fv_cgm_symbol` gained the **SAMI/APS line style** (`CgmLineStyle`: picture width/colour + components + dash elements) and the picture's VDC extent — line symbology is a stroke description, not a stamped glyph. V5a's `VpfVectorSource` now populates `attributes` (every non-structural column). **Golden PNG pinned after visual check**: 512² Nantucket Sound renders as a real harbor chart (coastline, depth contours, dredged channels, magenta navaids, bottom-characteristic symbols). 13 renderer gtests (synthetic — no VPF/GeoSym, so a seam leak fails the build) + 16 GeoSym gtests + 4 render gtests. ASan-clean; UBSan reports only the pre-existing VPF unaligned loads. **242 total.** Deviations/quirks documented in Decisions below. **NOT here (V5c)**: AREA features (face topology), along-path SAMI symbol placement, scale-based label thinning (labels are dense at small scales), pyfvw bindings, pan-viewer mode. |
| 14t | VPF V5c-demo (of Q6c): pyfvw vector bindings + pan-viewer `--vpf` mode | — | P | Second slice of Q6c per Chris 2026-07-24 (the "see DNC on screen + scale/zoom" ask). **New `pyfvw.vector` submodule**: `VpfVectorSource`, `GeoSymStyleEngine` (+ `GEOSYM_DNC` etc. product consts), `VectorRenderer` (`set_symbol_scale`/`set_device_dpi`/`render`), `IVectorSource`/`IStyleEngine` bases; GIL released around Open/Render. `MapProjection` gained Python **setters** (`set_surface_size`/`set_center`/`set_scale`/`set_resolution`/`set_physical_scale` + a ctor) so a vector viewer drives the projection directly (raster path still gets one from the engine). pyfvw links `fv_vpf fv_geosym`. **pan_viewer.py `--vpf <lib> [--geosym <dir>] [--scale N]`** renders a DNC library through GeoSym + VectorRenderer with **two independent knobs, exactly the ones Chris asked for**: `-`/`=` = **map scale** (1:N via `SetPhysicalScale`, correct lat-dependent aspect; GeoSym symbols + line widths keep their PIXEL size), `[`/`]` = **feature zoom** (`symbol_scale` + device DPI together; symbology grows, map extent fixed). `--shot` headless PNG; labels toggle (`l`, needs host TTF). Full Nantucket harbor at 900×650 ≈ 78 ms/render. **Also quieted the VPF reader's per-row `[log]` spam** (thousands of lines/render → 0): probe `get_field_info` (silent) before `get_field_value` (logs on miss) for the point-primitive coordinate field and for complex-feature area/line tables (lim/LIMBNDYA/L, MARITIMA/L) that lack simple tile_id/fac_id/edg_id refs — those are skipped up front. 3 new vector pytests (open/layers/bounds; end-to-end render + aspect; scale-vs-feature-zoom independence). 250 gtest + pyfvw_pytest green. **WVS deferred with root cause** (see Decisions): TestData/`VPF 2`/WVSPLUS opens as a library but its thematic coverages have **no FCA**, and the reader builds the feature-class list only from FCA → empty; needs FCS-based/dir-scan enumeration + a simple (non-GeoSym) stroke style. |
| 14s | VPF V5c-areas (of Q6c): DNC AREA features via extracted face topology | 1 | T | First slice of V5c (Q6c) per Chris 2026-07-24 (scoped to AREA features; SAMI along-path placement, label thinning, pyfvw bindings + pan-viewer `dnc` mode are the remaining Q6c slices). **`fv::VpfVectorSource` now serves AREA feature classes.** A `.AFT` row names a FACE; the geometry half of the Windows-only VPFFace/vpfelem winged-edge walk is **extracted** into `fv_vpf_vector_source.cpp` (`Edge`/`TileTopology`, `TraverseRing` = VPFFace::TraverseRing verbatim, `RingPoints` = GetPointList geometry-only) — **none** of the GDI CRgn/MapProj drawing or the FalconView app-layer includes (Map.h/param.h/refresh.h) it dragged in. Pipeline: AFT.`fac_id` → FAC.`ring_ptr` → RNG(`face_id`,`start_edge`) → winged edges over an in-memory EDG array (winged links are `K`/triplet fields; `.id` only, DNC faces are in-tile), `part[0]`=outer ring, `part[1..]`=holes; CND supplies VPFEdge's add-last-vertex correction. Reads real dnc17 h1707300: **502 areas across 9 classes** (ecrarea/embanka/lakea/foreshoa/hydarea/rivera/ruinsa/dangera/reefa) on top of the 1699 line + 2836 point features (**5,037 total**); all 502 outer rings close, 0 ran-off-array zeros, 0 out-of-region, 0 holes outside bounds. **dqyarea excluded** (data-quality metadata, no f_code/FACC — a drawable area class must carry a FACC). V5b golden harbor PNG **re-pinned** after visual check (area geometry now drawn under the coastline+contours+navaids). **CORRECTION 2026-07-25**: this row originally said area *fills*. They are not filled — `GeoSymStyleEngine` never sets `StyleResult::fill`, so areas render as boundary OUTLINES and the golden hash moved because the outlines appeared. The `areasym` column is parsed into `SymRow::area_sym` and never read; wiring it is what unlocks DNC depth shading (see the 2026-07-25 mariner-settings decision). **FIXED 2026-07-25 in row F1** — areas now fill, and the depth ramp works off the ATTEXP `cvl` rules that were already being evaluated. 6 new/updated gtests (3 new area tests: pinned single face, multi-ring holes, per-class counts). ASan-clean; UBSan reports only the pre-existing VPF unaligned loads. **250 total.** Quirks preserved: 1500-edge overly-large-face skip (drops the feature = drew nothing on Windows); outer ring kept body-or-not, inner rings need body; duplicate vertices at shared nodes (as GetPointList). Added bounds-guards on winged links + a 2N-edge cap (turn malformed/cross-tile links into an abandoned ring, not a hang — the GDI original would have indexed garbage). |
| 14u | DTED shaded-relief renderer (Q6b): `fv::DtedShadedRenderer` + `"dted-shaded"` IRasterSource | 2 | T | Queue Q6b per Chris (2026-07-23 survey). The engine — **`CDtedReader`** (fvw_core/MapDataServer/DtedMapServer/renderer/DtedReader.cpp, ~2900 lines) — is compiled **in place** as `port/DtedShadedRenderer/` (`fv_dted_shaded`); it was already std::string/std::vector/FILE*-based with no COM/GDI/MFC/registry, needing only fv_compat's shims (COLORREF/RGB/BYTE/BOOL, sprintf_s/strncpy_s, THROW_ERROR_MSG/GetLastError). Its `stdafx.h`/`ComErrorObject.h` includes are `#ifdef _WIN32`-guarded (DtedReader.h gets a matching POSIX guard adding `<string>/<vector>/fv_compat.h`) so the MSVC product build is byte-unchanged. **`fv::DtedShadedRenderer`** (`fv_dted_shaded_renderer.{h,cpp}`) is the portable facade replacing the COM/GDI-bound **DTEDRenderer** (disp.cpp — left Windows-only): same knobs (display mode, light dir / sun az-alt / time-of-day, contour, elevation/slope/colour bands, flat-is-black, exaggeration) but renders one cell into a top-down RGBA8 `PixelBuffer` (get_subimage palette-index image + 216-entry table → expand) instead of `IGraphicsContext::PutPixmap`. disp.cpp's non-COM lighting (`convert_to_cartesian`, default NW sun az315/alt45) moved into the facade verbatim; **`fv_solar_position.{h,cpp}`** = a license-clean NOAA solar-position fn replacing the GOV-only SLAC astronomy for time-of-day sun. **`DtedShadedRasterSource`** (`port/fvkit/formats/dted_shaded.h` + `dted_shaded_source.cpp`) wraps it as an IRasterSource (decode-whole-cell-once, crop; exact equal-arc transforms), registered **"dted-shaded"** (7 builtin formats now) reusing `DtedFrameEnumerator` for cell enumeration. `DtedDisplayModeEnum` mirrored to `port/include/fv_dted_enums.h` (COM ABI values). **Visually verified**: w082/n31 renders as coastal Georgia — sea-level-blue ocean correctly east, tidal rivers, NW-lit relief, N-up. **CORRECTION 2026-07-25 (row F1)**: the facade never applied FalconView's default ELEVATION BANDS, and CDtedReader's constructor defaults skip the feet→metres conversion its setter does — so every CONUS elevation fell in band 0 and relief rendered as one flat green hue. The verification cell above is too flat to show it; fixed and re-tested over w084/n35. 11 renderer/solar gtests (real-cell geometry/round-trip/determinism/mode+lighting diffs + pinned FNV render checksum; hermetic NOAA pins grounded in solstice/equinox astronomy) + 3 adapter gtests. ASan+UBSan-clean. **264 total.** |
| 14r | Physical-display scale (native-scale + correct aspect) in MapProjection/engine/pyfvw/demo | — | P | Not a port step — a display feature Chris asked for 2026-07-24: maps drawn at their **native physical scale** on a known display, with **correct aspect**. New `MapProjection::SetPhysicalScale(scale_denominator, mm_per_pixel)`: ground m/px = `mm_per_pixel × denom / 1000`, then dpp from a **WGS84 meters-per-degree series** (NOT MapScaleUtil — see the aspect bug below), so a 1:1M chart puts ~10 km under 1 cm at 0.25 mm/px and lat/lon pixels cover equal ground. `mm_per_pixel` is the zoom knob. `MapEngine::SetPhysicalScale(series_scale, series_scale_units, mm_per_pixel)` turns a SeriesRow's (scale, units) into that call: a cartographic denominator passes through; a ground-resolution series (imagery, `MAP_SCALE_METERS/_KILOMETER`) is shown at **100%** (1 source px = 1 screen px) at the reference pitch `kNativeDisplayMmPerPixel = 0.25` and scaled from there. Bound to pyfvw (`engine.set_physical_scale`, `proj.scale/.mm_per_pixel`, `engine.NATIVE_DISPLAY_MM_PER_PIXEL`); `pan_viewer.py` now renders through it with `-`/`=`/`0` zoom + `--mm` and a mm/px status readout. 8 gtests (4 proj + 1 engine, reused file) + 1 pytest. **247 total.** See the aspect-bug decision below. |
| R1 | Identify: `FeatureRef` + `Describe()` + VPF VDT decoding + pick index + click→info panel | 4 | P | First session of the ACTIVE TRACK (plan §5.3), 2026-07-25. **The seam**: `fvkit/vector/vector.h` gains `FeatureRef{source,layer,tile,feature}` (4×int32 — replaces `VectorFeature`'s loose `tile_id`/`feature_id`; `layer` indexes `Layers()`), `FeatureAttribute{code,name,raw,display}`, `FeatureDescription{ref,title,class_name,layer_name,attributes,source_note}`, and a **virtual `IVectorSource::Describe()` defaulting to kUnsupported** — so ENC/OSM/test sources need not implement it and a caller can tell "no such feature" from "this product cannot describe". **VPF**: new `port/VpfMapServer/fv_vpf_vdt.{h,cpp}` (`fv::VpfValueDescriptions`) reads a coverage's `INT.VDT`/`CHAR.VDT` through the V1 recordset layer — only the unported VPFDataLib fork read these — and `VpfVectorSource::Describe` joins them with the column's own header description (`VPFFieldInfo::m_desc`) and the FCA class description: `BE010` → "Depth Curve", `acc` 1 → "Accurate", `dat` → "Information as of ____". Dictionaries load lazily per coverage; the render path never touches them. **The pick index** (`port/include/fvkit/vector/pick.h` + `fvkit/vector/pick.cpp`, `fv::PickIndex`) is filled by `VectorRenderer` from the primitives it EMITS — strokes at their pen half-width, clipped fill rings, symbol ink boxes, label boxes — so a tap agrees with what is on screen (a hairline styled 5 px wide is 5 px wide to the cursor; a clipped-away feature is not there to hit). `HitTest` returns the whole stack topmost-first, one entry per feature. On by default; `SetPickEnabled(false)` for bulk/offline. **pyfvw**: `FeatureRef`/`FeatureAttribute`/`FeatureDescription`/`PickHit`/`PickIndex`, `IVectorSource.describe()`, `VectorRenderer.pick_index`. **pan_viewer `--vpf`**: click identifies (info panel lists the stack, decoded); shift+click re-centers. 9 PickIndex + 6 renderer-pick + 9 VPF identify gtests + 3 pytests. ASan-clean; UBSan reports only the pre-existing VPF unaligned loads. **291 total.** New helper `fv_vpf_detail.h` (ToStd/Trim/Upper/Lower/VariantInt/VariantText shared by the module's TUs). Quirk/guard notes in Decisions below. |
| E1 | ENC E1 (Q8): ISO 8211 container + S-57 cell reader | 4 | T | Second session of the ACTIVE TRACK, 2026-07-25 — **the port's second real vector product**. Port-native code, not an in-place compile: FalconView has no S-57 reader, and `Applications/GeoRect/adrg/iso.cpp` (the only ISO 8211 in the tree) is the ADRG-era C library with ASCII subfields only, while S-57 is almost entirely binary — so plan §7's "spec-driven reader of our own" stands. **`port/Enc/fv_iso8211.{h,cpp}`** = a chart-agnostic ISO 8211 reader: DDR field definitions, format-control expansion (`(b11,b14,2b11,3A,2A(8),R(4),A)` → typed subfield specs), LSB-first binary ints/floats per S-57 Part 3, `B(nn)` bit strings kept verbatim, repeating fields as rows, and S-57's **all-bits-set null** carried on every value so "absent" ≠ "zero". **`port/Enc/fv_s57.{h,cpp}`** = `fv::S57Cell` (DSID/DSSI/DSPM, vector records, features with ATTF/NATF attributes + FFPT relations, chain-node geometry assembly: point/sounding-array/line-run/area-rings-with-holes), plus `ReadS57Catalog` (CATALOG.031 is *also* ISO 8211 — its CATD rows give a cell's box and long name without opening the cell, and it exercises the reader's ASCII `I`/`R` path), `EnumerateEncCells` (**`*.000` scan, never the `ENC_ROOT/<producer>/<cell>/` path** — the delivered sets have their cell dirs lifted out) and `ParseCellName` (`US5CHSDC` → producer/usage-band/region/id; the band is S-52's scale band, DNC's library role). Reads all four real Charleston cells: **4,487 features, 205,678 vertices, and every one of the 1,850 rings across 1,525 area features closes — 0 incomplete geometries.** 43 gtests (19 hermetic container tests over byte-built files + 24 real-data), ASan+UBSan-clean. **334 total.** **BASE EDITION ONLY and loud about it** — see Decisions. **NOT here**: `.001…` updates (E5), the `IVectorSource`/`FrameInfo` adapters and catalog rows (E3/E4), S-52 styling (E2/E3), and **object-class/attribute acronyms** — S-57 Appendix A is not in the data and was not guessed at (new blocker), which also costs DSSI's meta/geo record split. |
| F1 | Two rendering-fidelity fixes found through PythonView (2026-07-25, per Chris): DTED hill-shading + DNC area fill | 5 | T | Both were **unit/convention bugs in ported code, not missing features**, and both were invisible to the existing tests. (1) **DTED shaded relief rendered flat green.** `CDtedReader`'s constructor seeds `m_elev_breakpts` with `{2500,5000,7500,10000,12500}` but skips the **feet→metres conversion its own `set_elevation_bands()` setter performs** (`FEET_TO_METERS`, DtedReader.cpp:2415), so the raw defaults act as METRES — band 0 runs to 2500 m (8200 ft) and every CONUS elevation lands in it, collapsing the 6-colour ramp to one hue. On Windows `CDtedRenderOptions::InitElevationBands` always pushes the feet array in, so the bad defaults never reach the screen; headless, nothing did. `fv::DtedShadedRenderer::Render` now applies FalconView's own defaults through the converting setter when the caller sets none. **The hill-shading itself was always correct** — verified by reading back the light vector (NW az315/alt45 → (0.577,−0.577,−0.577), flat ground at shade 18/32) and the palette histogram; the Appalachians now render as lit ridge-and-valley terrain. Exaggeration (1.0, 3.0 for time-shading) already matched disp.cpp and was left alone. **Why the tests missed it**: the fixture cell is w082/n31, tidal Georgia, flat enough that every elevation sits in band 0 *whatever* the breakpoints are — so a visual check AND a pinned FNV hash both passed. Added a second fixture on **w084/n35** (southern Appalachians, ~300–1700 m) with 3 tests that assert band behaviour over real relief: default == FalconView's feet bands, breakpoints move the ramp, and luminance spread > 60. Band counting is by **15° HUE buckets** (a band varies only brightness, so hue identifies it; a per-channel chromaticity ratio fails on 8-bit rounding — measured 19 spurious "bands" before switching). (2) **DNC areas drew as outlines** — the `areasym` column was parsed into `SymRow::area_sym` and never read (row 14s correction, Q6c item 5). `fv::CgmSymbol` gained `area_style()` (`CgmAreaStyle{fill_color, fill_style, patterns}` — the picture-level brush `CCGMSymbol::DrawArea` actually uses, plus `CgmPattern` with its bits) and `GeoSymStyleEngine::Style` now sets `StyleResult::fill` from it, cached per symbol number like `LineStrokeFor`. **The depth shading came along for free**: BE010's rows name a different area symbol per depth band and the ATTEXP conditions (`cvl` vs `ssdc`/`msdc`/`mssc`) were already being evaluated — 500 of 502 harbor areas now fill, land buff `0822`, water `0821`, and BE010 ramping deep→shallow 0805/0820/0821/0810 by the feature's own `cvl`. **Second bug inside the first**: `CCGMPattern::AddMonochromeBit` stores bits **INVERTED** ("add new bit. Invert pattern.") and GDI's monochrome brush paints the **ZERO** bits in the foreground colour, so ink coverage is the fraction of CLEAR bits — reading it uninverted turned DNC's ~5%-ink shallow-water stipple into a 95% grey slab covering the depth shading (that grey was the first render's most obvious defect). **DEVIATION**: `ICanvas` has only a solid `Brush`, so a non-solid pattern is approximated by carrying its ink coverage in the fill ALPHA (5.5% stipple → alpha 14); a real pattern brush is an ICanvas addition and `AreaFillFor` is the one place to change. 5 new GeoSym tests (land-vs-water colours pinned from the CGMs, depth ramp ordering, no-areasym → no fill, stipple coverage < 25%) + the harbor golden **re-pinned after visual check** (`0x5f2407aab641322f` → `0x71724acb5cb517a`). 341 gtests green; ASan clean, UBSan shows only the pre-existing VPF unaligned loads. **Still open** (unchanged): the `MarinerSettings` API on `StyleContext` — the depth ramp currently uses `CECDISValues`' default-constructed ssdc/msdc/mssc, so a vessel draft still cannot be set. That is R2, as planned. |
| — | PythonView: pan_viewer upgraded to an application (2026-07-25, per Chris) | — | P | Not a port step. `demo/pan_viewer.py` → **`port/apps/PythonView.py`** (git mv; the demo dir is gone): a tkinter/ttk app (stdlib-only on top of pyfvw+numpy — deliberately no Qt, nothing to version-match against the built `.so`) with a native menu bar over every family the port renders. **Map menu** groups catalog series into 4 families: Raster Charts (cadrg, gpkg) / Imagery (geotiff, tiros) / Elevation (dted-shaded — its FIRST UI exposure; series carry scale 0 so -/= drives an explicit display 1:N through `set_physical_scale(denom, 0, mm)`, defaults per level) / Vector Charts (vpf; one family on purpose — ENC/OSM slot in beside DNC when their engines land, and the menu shows them as disabled "Planned" entries). **Coverage overlay** (`c`): per-viewport `select_by_geo_rect` footprints, one color per format + on-canvas legend with in-view counts, antimeridian rows drawn as 2 boxes, per-family toggles in the Overlays menu. **Catalog UI**: first-run scan prompt, Add Map Data (auto-detect incl. a dht-walk for VPF databases), Manage Data Sources dialog (reads the catalog SQLite read-only for listing — no new C++ binding needed), New/Open catalog. **Window resizes freely** (debounced re-render at the new surface size), drag-pan + wheel zoom, Go To Location (DMS/MGRS via parse_location), Options dialog (pixel pitch, GeoSym dir, vector brightness/contrast via SetColorAdjust), Tools→fvpack pointer, `--shot`/`--series`/`--at` headless CLI, and **`--selftest`** (scripted UI walk: every family + coverage + resize, snapshots to build/pythonview_selftest; steps are CHAINED with idle gaps — absolute after() schedules starve the event loop behind slow renders and the wm Configure round-trip never lands). Sparse-coverage centering fixed: series centroid snaps to the nearest frame when it lands in a gap (CADRG samples). **Findings for the pending list**: (1) **big-endian ('MM') GeoTIFFs fail to decode** — 18 of 29 TestData tiffs; enumeration reads their headers fine so they catalog, then `CGeoTiff` load fails ("Error in byte order string: M") and (2) **one bad frame aborts `MapEngine::RenderBaseMap`'s whole composite** — the app catches the FvError and shows it in the status bar, but the engine should skip-and-log per frame. (3) WVSPLUS scans into the catalog (216 tiles) but its libraries open empty (no FCA — known 14t deferral), so those menu entries render nothing until FCS-based enumeration lands. |
| — | Dependency modernization wave 1 (2026-07-25) | — | T | Not a port step — per Chris, "move to modern versions where available". New `port/third_party/CMakeLists.txt` is the single place library versions live (FetchContent, gtest pattern). **gtest 1.14→1.17.0, zlib 1.2.5→1.3.2 (wired in: `fv_z` is now an INTERFACE onto `fv_zlib`; libpng/libtiff consume it unchanged), expat 2.1.0→2.8.2 (new; first consumer is the E2 PresLib loader).** 3 expat smoke tests added. `codecs_test`'s `EXPECT_STREQ(zlibVersion(), "1.2.5")` replaced with `zlibVersion() == ZLIB_VERSION` + a `ZLIB_VERNUM >= 0x1300` floor — a literal would just drift on every bump (same lesson as the 2026-07-23 TestData refresh), whereas header-vs-library agreement catches the real bug class. **267 total.** libpng/libtiff/jpeg deferred as module-sized API breaks and GEOTRANS deliberately frozen — see the dependency-modernization table above. `third_party/` and `fvw_core/ImageLib/*` untouched; the Windows product build is byte-unchanged. |
| — | TestData refresh 2026-07-23 (Charleston SC set) | — | T | Not a port step — Chris dropped new sample data before Q5/V4. **+2 DTED2 cells** (w080/n32.dt2 = new coverage, w081/n32.dt2 = overlaps the existing .dt1) and **+14 GeoTIFFs** (10 `22…e…n` DOQQ tiles + 4 `C3208…` sheets, all ~32.5N 80.1W), so dted 24→26 frames and geotiff 15→29. 5 tests failed on hardcoded inventory; **fixed by deriving the expectations from the tree instead of bumping the literals a third time** — `CellsOnDisk`/`TiffsOnDisk`/`_count_by_ext` walk the sample dir, `CatalogReal` compares Scan's row count to the format's own enumerator, and the DTED union box is compared to the union of the enumerator's per-cell boxes (w080 pushed ur.lon −80→−79). geotiff_adapter_test's two-branch if/else became a `BlockFor()` region table (Chesapeake/Charleston/Choctawhatchee) that **fails on an unclassified name** rather than silently bounds-checking new data against the wrong region. **New coverage the data enabled**: w081/n32 is the first real cell present at two levels, so `RealDtedSource.FinerLevelWinsInOverlappingCell` now pins dted.cpp's finest-level-first ordering (DTED1 10 m / 3 m vs DTED2 17 m / 21 m at two points, each level isolated in its own tree so the assertion can't pass by coincidence) + `Dted2OnlyCellProvidesCoverage`. 187 total. |

## Dependency modernization (2026-07-25, per Chris: "move to modern versions
## where available")

The Windows product build consumes 1998–2012-era vendored forks. Those trees
are frozen by the hard rules, so **the port does not edit them** — it builds
against current upstream releases fetched in `port/third_party/CMakeLists.txt`
(FetchContent, matching the gtest pattern; one place for every version, no
multi-MB source dumps in a repo that already has a 155 MB blob problem, and
nothing added to the Peregrine public subset). Windows keeps its forks; the
port moves forward. Offline builds: `FETCHCONTENT_FULLY_DISCONNECTED=ON` with
a populated cache, or `FETCHCONTENT_SOURCE_DIR_<NAME>`.

| Library | In tree (age) | Upstream | Status | Notes |
|---|---|---|---|---|
| googletest | 1.14.0 | **1.17.0** | ✅ **DONE** | Root CMakeLists, `FVW_GTEST_VERSION`. Bumping headers without a clean rebuild leaves stale `.a`s — `MakeAndRegisterTestInfo` took `const char*` in ≤1.14 and `std::string` in ≥1.15, so a dirty build dir link-errors. `rm -rf build` on any gtest bump. |
| zlib | 1.2.5 (2010) | **1.3.2** | ✅ **DONE** | True drop-in; libpng/libtiff consume it unchanged. `fv_z` is now an INTERFACE forwarding to `fv_zlib`, so the swap was one edit. Also retired the MSVC-CRT remap (`_open=open` …) and `-include unistd.h` the 2010 fork needed. |
| expat | 2.1.0 (2012) | **2.8.2** | ✅ **DONE** | New integration, no port consumers displaced. 3 smoke tests. First consumer = E2 PresLib loader; Q12 WMS (network XML) is why it is current, not the in-tree copy. |
| libpng | 1.2.7 (**2004**) | 1.6.58 | ⛔ **module-sized** | NOT a drop-in: 1.2→1.4→1.5→1.6 made structs opaque and reworked `png_get_`/`png_set_`/`png_jmpbuf`. FalconView's png consumers need porting. Also drops the `-include math.h` hack (1.2's `TARGET_OS_MAC` sniffing picked the Mac OS 9 `<fp.h>` branch). |
| libtiff | 3.9.4 (2010) | 4.7.2 | ⛔ **module-sized** | NOT a drop-in: 3.x→4.x widened `toff_t` to 64-bit and changed many types; `CGeoTiff` is ~25K lines against the 3.x API. Would retire the `__int64=long long` define and the hand-written `fv_tif_posix.c`. |
| IJG jpeg 8/12-bit | **6b (1998)** | libjpeg-turbo 3.2.0 | ⛔ **module-sized** | Hardest of the three. FalconView did not vendor IJG 6b — it **transliterated it to C++** (`J*.cpp`, not `.c`) and the C++ wrapper carries an encryption fork (`jpeg.h`'s `m_crypt_pos`/`m_encrypt`). turbo keeps the 6b C API and has native 12-bit in 3.x, so the swap is feasible, but it re-opens the symbol-collision rule (FalconView's C++ jpeg vs GDAL's C jpeg must not meet in one link) and the crypt feature must be shown unused before it is dropped. |
| GDAL's libjpeg (for tiff) | 6b (1998) | — | ⛔ tied to libtiff | `third_party/gdal_1.9.1/frmts/jpeg/libjpeg`, mirrors `tiff.vcxproj`. Goes away with the libtiff/turbo work, not before. |
| GEOTRANS | 3.3 | ~3.9 (NGA MSP) | 🚫 **do not upgrade** | Deliberate. Geo results are pinned bit-faithfully against the Windows build (rows 2/3, `GeoTransPinnedSuite`); a newer GEOTRANS changes numeric output and invalidates exactly the tests that give the port its meaning. Revisit only with Windows-side reference dumps in hand. |
| SQLite | system | system | ✅ n/a | Already the OS copy. |
| stb_truetype | current | — | ✅ n/a | Vendored current at `port/vendor/`. |

**Sequencing for the three ⛔ rows**: each is a session of its own, and none
blocks the ENC/OSM track. Do them **libpng → libtiff → jpeg** (ascending
consumer count, so a break is easy to localise), and only when the ENC/OSM
queue permits — the 2010-era codecs are a security concern for *untrusted*
input, and every current consumer reads local, operator-supplied map data.
Q12 (WMS) is the first port feature to touch the network; anything it parses
must be on a modern library first — expat already is.

## Port queue (2026-07-19, per Chris: public data only; ordered by data
## availability first, implementation complexity second)

**Tier 1 — data in hand (or none needed), cheapest first:**
| # | Item | Data | Complexity |
|---|---|---|---|
| Q1 | 14j: pan-viewer over MapEngine + overlays (zoom) | none needed | trivial |
| Q2 | L5 store: GeoPackage TilePack writer (+ FeatureStore stub) | self-generated | moderate |
| ~~Q3~~ | ~~VPF V1: headless VPF/DNC reader~~ **done 2026-07-20 (row 14l)** | dnc17 in TestData ✓ | moderate |
| Q4 | VPF V2: DNC coverage → catalog rows | same | low |
| ~~Q5~~ | ~~GeoSym V3/V4: rule engine + CGM display lists~~ **done: V3 2026-07-21 (row 14n), V4 2026-07-23 (row 14o)** | assets present: TestData/GeoSymbol/{SymAssign,Graphics} (757 CGM), DataDir=TestData | moderate |
| Q6 | VPF V5: VectorRenderer seam + DNC drawing | above | high (the big one) — **split 2026-07-23: V5a DONE (row 14p, IVectorSource + VpfVectorSource); V5b DONE (row 14q, IStyleEngine + GeoSymStyleEngine + VectorRenderer + golden chart PNG); V5c = the remainder, see below**|
| Q6c | VPF V5c: DNC areas + renderer polish | dnc17 ✓ | moderate — the leftovers of V5, each independently useful: (1) ~~**AREA features**~~ **DONE 2026-07-24 (row 14s)**: `.AFT`→FACE→ring→edge topology walk extracted from Windows-only VPFFace/vpfelem (geometry only, no GDI/app-layer); 502 DNC areas served. (2) **Along-path SAMI symbol placement** (dash elements of type kPointSymbol currently degrade to gaps). (3) **Scale-based label/feature thinning** — GeoSym's vgroup/txtgroup columns are parsed but unused, so labels are unreadably dense zoomed out. (4) ~~pyfvw bindings + a pan-viewer `dnc` mode~~ **DONE 2026-07-24 (row 14t)**: `pyfvw.vector` submodule + `--vpf` demo with map-scale / feature-zoom knobs. (5) ~~**AREA FILL from the `areasym` column**~~ **DONE 2026-07-25 (row F1)**: `CgmSymbol::area_style()` + `StyleResult::fill`; DNC now draws buff land and depth-shaded water (BE010's per-band area symbols were already being selected by the ATTEXP `cvl` rules). Stipple patterns are approximated by ink coverage in the fill alpha — the bits are stored INVERTED, see row F1. **Remaining half**: the `MarinerSettings` API on `StyleContext` so a vessel draft can actually be entered (the ramp uses `CECDISValues`' defaults today) — still R2. (6) **WVS support** (Chris wants WVSPLUS too): reader builds feature classes only from FCA, but WVS thematic coverages have none → enumerate from FCS or by directory scan; then a simple stroke style engine (WVS has no GeoSym symbology). Data present: `TestData/VPF 2/WVSPLUS/WVS{012,040,120}M`. |
| ~~Q6b~~ | ~~DTED shaded-relief renderer~~ **DONE 2026-07-24 (row 14u)**: `fv::DtedShadedRenderer` + `"dted-shaded"` IRasterSource; CDtedReader engine compiled in place, DTEDRenderer/disp.cpp severed, NOAA solar-position fn replaced SLAC. Hill-shade/elevation-bands/slope/contour all served; visually verified on TestData. | dted in TestData ✓ | low-moderate — done |

**ACTIVE TRACK (set 2026-07-25, per Chris): ENC + OSM, with a cross-product
middle layer.** Chris's requirements — efficient rendering, a robust mechanism
for defining and applying scale-dependent feature rules, official S-52
symbology reusing the DNC/GeoSym infrastructure, and click-to-identify feature
metadata — are all cross-product, so they land in the middle of the V5 seam.
Design: **`port/vpf-geosym-plan.md` §5** (new). Key findings: S-52 and GeoSym
are structurally the same system (§5.1 mapping table — only CS procedures are
ENC-only), so `VectorSymbol` and the along-path placer are shared, and the
three style engines collapse into one `LookupTableStyleEngine` plus per-product
loaders. Chosen order (§5.5 hybrid — the abstraction is cut only after a second
real table exists):

| # | Session | Notes |
|---|---|---|
| ~~R1~~ | ~~Identify: `FeatureRef` + lazy `IVectorSource::Describe()` + VPF `INT.VDT`/`CHAR.VDT` decoding + pick index + pan-viewer click→info panel~~ **DONE 2026-07-25 (row R1 above)** | Landed as designed; the VDT reader over the ported table layer was ~60 lines |
| ~~Q8~~ | ~~ENC E1: ISO 8211 / S-57 reader~~ **DONE 2026-07-25 (row E1 above)**: `port/Enc/` reads all four Charleston cells base-edition-only, with the ISO 8211 container reader that also parses CATALOG.031. **Next session = R2** (the rule layer), whose second real table is now available. |
| R2 | `fvkit/vector/rules.h` (predicate AST + ScaleBand + ViewingGroup + memoized `ResolvedPlan`) + extract `LookupTableStyleEngine` from GeoSym; retrofit GeoSym | Subsumes Q6c item 3 (scale-based label/feature thinning — `vgroup`/`txtgroup` finally used) |
| Q10/Q11 | ENC E2/E3: PresLib parse + `S52StyleEngine` + CS registry + along-path placer | Placer shared back into GeoSym SAMI = Q6c item 2 |
| R3 | Perf: retained `TileDisplayList` cache, symbol atlas, columnar `FeatureBatch`, per-band simplification | Pulls plan phase V8 forward — identify needs the same retained structure |
| Q7/Q9 | OSM O1–O3 | Lands on the finished middle layer; ends up small |

**S-52 PresLib source decided 2026-07-25**: OpenCPN's `chartsymbols.xml`
(GPL-3.0, compatible with Peregrine **as data**; OpenCPN C++ stays
reference-only). Git-ignored under `TestData/`, supplied via a data-dir arg
like the GeoSym assets. **XML parser = expat, already in-tree** (survey
2026-07-25, see plan §5.6): `third_party/Expat 2.0.1/Source/lib` is full MIT
source, 3 TUs, `expat.h` says 2.1.0; **FalconView already drives it portably**
in 7 files (`fvw_core/WMTComponents/*`, a data_abstraction_layer config
reader) in plain std::string/std::vector — `GDALXMLReader.h` is a ready-made
portable SAX-consumer template. Compile from third_party unmodified (GEOTRANS
precedent, row 2) with our own `expat_config.h` in `port/include/`. NOT
libxml2 (75 TUs, needs generated config, no FalconView source uses it — it is
a GDAL/libspatialite transitive dep). Before **Q12 (WMS = network XML)** swap
to a current expat release under `port/vendor/expat/` — the vendored 2.1.0 is
from 2012 with a real CVE history; fine for trusted local assets like
chartsymbols.xml, not for network input. Side benefit: this voids the stated
reason row 8 deferred CoT. **MSXML is the non-portable bulk**: 148 files
across the app trees use `IXMLDOMDocument`, none on any current port path.

**Tier 2 — freely downloadable/generatable public data:**
| # | Item | Data | Complexity |
|---|---|---|---|
| Q7 | OSM O1: MBTiles + MVT reader | Planetiler over Geofabrik extract (generate once) | low-moderate; dependency-free, good filler session anytime |
| ~~Q8~~ | ~~ENC E1: ISO 8211 / S-57 reader~~ **done 2026-07-25 (row E1)** | NOAA ENC cells in TestData ✓ | moderate; dependency-free — done |
| Q9 | OSM O2+O3: style loader + render via V5 seam | same as Q7 | low (after V5 + R2) |
| Q10 | ENC E2: S-52 PresLib (lookups + symbol display lists) | OpenCPN `chartsymbols.xml` — **needed in TestData** | moderate |
| Q11 | ENC E3+E4: S52StyleEngine + CS procedures + source cache, render | above | high (after V5 seam + R2; shares V4 display lists + the shared along-path placer) |
| Q12 | WMS: network raster source | public endpoints (USGS, GIBS) | moderate (HTTP client decision: libcurl) |
| Q13 | JP2 via OpenJPEG | public samples | moderate (avoids Kakadu license; would also unblock ECRG if it ever returns) |
| Q14 | NITF (9b-4 nitf/) | public NITF test sets | moderate-high |
| Q15 | GeoPDF | USGS topo GeoPDFs (free) | high (PDF engine decision) |
| Q16 | Lidar | USGS 3DEP (free) | high, niche |

**Deprioritized (per Chris 2026-07-19, corrected same day: ECRG not ENC —
restricted/proprietary data, not the public-data use case):**
ECRG (NGA successor to RPF; restricted distribution + JP2/Kakadu heritage),
CIB (restricted imagery; is_cib adapter gap stays noted), MrSID (proprietary
codec; TestData exists but licensing), Hrdted/RDted/ARdted variants (legacy),
BlankMapServer + misc rasters (filler only).

Deferred indefinitely: Collaborate, NITFSourcesCtrl, *MapOptions property pages,
FvConfigFileServer (registry-backed).

VpfMapServer/GeoSym (largest, ~62K lines): **planned 2026-07-16** — see
`port/vpf-geosym-plan.md` (phases V0–V8: headless VPF reader → GeoSym rule
engine → CGM display lists → ICanvas rendering → offline pre-render to
GeoPackage TilePack for iOS; CoreGraphics interactive backend). V1 (VPF
reader) has no dependencies; V5+ need L2 catalog + L2.5 canvas; V3+ need the
GeoSym asset directory (ARRIVED 2026-07-21: TestData/GeoSymbol; see blocker below).
**Extended 2026-07-19 (per Chris)** with two new vector tracks on the same
V5 renderer seam: **OSM vector tiles** (MBTiles/MVT via vendored
protozero+vtzero, MapLibre-subset style engine; phases O1–O3) and **ENC**
(own ISO 8211/S-57 reader + S-52 PresLib symbology, CS-procedure registry,
SENC cache; phases E1–E5; NOAA cells = free public-domain test data).
E1 and O1 are dependency-free reader sessions available anytime.

Other unported map servers, for the record (beyond VPF): EcrgMapServer,
MrSIDMapServer (TestData/mrsid now present), Jp2MapServer (Kakadu-licensed
on Windows — POSIX would use OpenJPEG), WMSMapServer, GeoPdfMapServer,
LidarMapServer, Hrdted/RDted/ARdted variants, NITF (9b-4), BlankMapServer.
Also: CIB (RPF imagery) — decoder supports it but CadrgRasterSource
hardcodes is_cib=FALSE and there's no CIB test data (small gap, listed in
Pending).

## Per-module recipe

1. Create `port/<module>/CMakeLists.txt` compiling sources **in place** from `fvw_core/<module>/`.
   Don't move files; split a file only if it is hopelessly Windows-bound.
2. Uncomment the module's `add_subdirectory` in the root `CMakeLists.txt`.
3. `cmake -B build && cmake --build build` — fix errors compiler-first, don't read files speculatively.
   Typical fixes: drop `#include "stdafx.h"`/afx headers, `CString`→`std::string`, TCHAR removal,
   `__int64`→`int64_t`, case-sensitive `#include` paths, `_stricmp` etc. via `port/include/fv_compat.h`.
   Shared sources must stay standard C++ compilable by modern MSVC/GCC/clang (guard
   platform-specific code with `#ifdef _WIN32`).
4. Repeated mechanical transforms → script in `port/tools/`, not hand edits.
5. Add gtest cases pinning known-good values in `port/<module>/test/`; `ctest --test-dir build`.
6. Update the ledger row above, commit: `port(<module>): compiles+tests on macOS`.

## Decisions made

- 2026-07-10: **Bit-faithful porting**: known numeric quirks in the original are preserved, not
  fixed, so outputs match the Windows build (golden tests stay meaningful). Found in geoid:
  CGeoid's bilinear blend inverts latitude within each grid cell (sub-meter error) — preserved,
  documented in `port/geoid/fv_geoid.cpp`. Fix later on both platforms together if desired.

- 2026-07-10: C++17 for new `port/` code.
- 2026-07-11: **VS2010 compatibility dropped** (per Chris): the Windows build will move to modern
  MS Build tools or GCC; shared fvw_core edits may use standard C++ (C++11/17) freely. Earlier
  sessions' C++03-conservative shared edits remain valid, just no longer required.
- 2026-07-10: gtest v1.14 via FetchContent (vendored gtest 1.5/1.6 abandoned).
- 2026-07-10: Prebuilt Windows binaries git-ignored; full Windows tree lives on separate machine.
- 2026-07-10: COM severing = plain C++ abstract interfaces + factory registry; COM wrappers stay
  Windows-only files. `fv_compat.h` HRESULT shim only for code not worth rewriting yet.

- 2026-07-11: `sscanf_s` implemented in `port/include/fv_sscanf_s.h` (~100 call sites across
  fvw_core; sizes are runtime args so call sites can't be rewritten mechanically). Unit-tested in
  `port/geo3/test/geo3_test.cpp`. LP64 little-endian only — see ABI note in the header.
- 2026-07-11: POSIX policy for Windows UI/persistence in shared files: `MessageBox`/
  `OutputDebugString` → stderr (fv_compat.h); HKCU display-format prefs → in-memory session map
  (pattern in geo3.cpp). Revisit with a real config store when FvConfigFileServer is tackled.
- 2026-07-11: geo3.cpp contained two raw Latin-1 0xB0 (degree-sign) bytes; replaced with `'\xB0'`
  escapes — byte-identical semantics, now pure ASCII.
- 2026-07-11: GEOTRANS datum/ellipsoid data for tests reuses `fvw_core/PdfLib/sdk/lib/*.dat`
  (ellips.dat there spells ellipsoid names differently than a FalconView install, so the upstream
  `datum_ellipsoid_name` test is excluded from GeoTransPinnedSuite).

- 2026-07-11: geo_tool's Sodano-based geodesics differ from modern references (JFK–LHR: ~200 m /
  0.17° vs GeographicLib). Pinned as-is per bit-faithful rule; values in
  `port/geo_tool/test/geo_tool_test.cpp` should be cross-checked on the Windows build someday.
- 2026-07-11: `GEO_east_of_degrees(a, b)` means "a is east of b" — easy to misread.

- 2026-07-11: **Win32 integer widths are part of the ABI**: `LONG`/`DWORD`/`HRESULT` are exactly
  32 bits; typedefing them as `long` broke `FAILED()` on LP64 (0x80070057 came out positive).
  fv_compat now uses int32_t/uint32_t. Corollary: `LONG` and `long` are distinct types on LP64 —
  mixed declarations/definitions that compiled on Windows need aligning (done in GEOTRANS.CPP).
- 2026-07-11: MapScaleUtil preserved quirks: statute miles converted with the FEET factor
  (~5280x error, ships that way on Windows); low-precision PI/RAD constants in its private
  Vincenty copy (differs from geo_tool's); scale truncated via int cast; MAP_SCALE_WORLD pinned
  to 2147483647.0 (Windows LONG_MAX), not LP64 LONG_MAX.
- 2026-07-11: COleDateTime → dedicated `port/include/fv_oledatetime.h` (OLE DATE double,
  MFC half-second rounding, MFC arithmetic-in-days quirk preserved). ~52 files repo-wide use it.
- 2026-07-11: Backslash `#include` paths → `port/tools/fix_include_slashes.py` (forward slashes
  are MSVC-valid, so shared files can be fixed unguarded).
- 2026-07-11: IDL enums used by portable code get mirrored headers in `port/include/`
  (`fv_map_enums.h` ← CustomInterfaces/CommonMap.idl). Values are COM ABI — never renumber.

- 2026-07-12: MFC containers → `port/include/fv_mfc_containers.h` (CList/CArray with POSITION
  idiom; upgraded from per-site REWRITE given 106-file prevalence). CFile/CException/COleDateTime-
  style minimal emulations continue to live in fv_compat.h as they stay small.
- 2026-07-12: Util.cpp method-guard pattern: Windows-bound CUtil methods (registry, GDI, MSXML,
  COM callbacks, HANDLE I/O, GetDiskFreeSpaceEx) wrapped _WIN32 individually; POSIX equivalents
  in port/ImageLibCore/fv_imagelib_util_posix.cpp (env-var data paths, no-op progress callbacks,
  stat-based file date/size, "assume plenty" free space). Deltas documented inline.
- 2026-07-11: **CString varargs**: MFC code passes CString by value to printf-style Format("%s")
  — works on MSVC (bit-copy = pointer) but is indirect-passed on Itanium ABI, so it can NOT be
  made to work via layout tricks on clang. Policy: cast to `(LPCSTR)` at each site clang flags
  (-Wnon-pod-varargs is a hard error, so the compiler enumerates them; harmless on Windows).
  fv_cstring.h is pointer-sized anyway (MFC-like) with a static_assert documenting the contract.
- 2026-07-11: JINCLUDE.H's non-Windows JFSETPOS macro had a latent `pos`/`ppos` typo — never
  compiled anywhere before this port; fixed (dead-code repair, not a behavior change).
- 2026-07-11: cadrg decoder quirks: archaic `delete[n] ptr` syntax rewritten to `delete[]`
  (identical semantics, VS2010-fine); resize loop sets FP rounding mode via _controlfp_s —
  emulated with fesetround (RC bits only). NitfFileHeader.cpp excluded (separate _bstr_t utility,
  not in the decode path).
- 2026-07-11: **COM severing pattern established** (GeoTIFF): plain interface in
  `port/include/fv_interfaces.h` + single-slot registry + default impl; COM wrapper adapts on
  Windows. ComErrorHandler macros (TRY_BLOCK/CATCH_BLOCK_RET/THROW_ERROR_MSG) have POSIX
  equivalents in fv_compat preserving the `hr | 0x80000000` convention. projsp.cpp's file-scope
  `minor`/`major` arrays renamed (`sp_minor_axis`/`sp_major_axis`) — macOS SDK declares
  inline functions with those names. TestData geotiff samples are USGS DOQ quarter-quads
  (3.75', 1 m/px), not DRGs.
- 2026-07-11: **DTED level 3 is out of scope** (per Chris: NGA made only a few prototype .DT3
  cells ever; only DTED 1 and 2 matter). The TestData n40.DT3 uses a nonstandard prototype UHL —
  the speculative variant-header fallback was removed from fv_dted_cell.cpp; such files are
  rejected, matching the Windows reader in this snapshot.
- 2026-07-11: DTED lookups are nearest-neighbor (no interpolation); elevations are big-endian
  signed-magnitude; PARTIAL (-32767) passes through feet conversion unconverted; cell geo bounds
  come from the tile path, not the file header. `fv_filemap.h` maps whole files (Windows' 4-32MB
  sliding windows were a 32-bit-era optimization; bytes identical).
- 2026-07-11: ImageLib codec notes: vendored zlib omits compress.c/uncompr.c (use deflate/inflate
  API); FalconView's jpeg fork adds a crypt()/jpeg_encrypt_table feature and a memory-mapped-file
  jpeg_fread (POSIX version in `port/ImageLib/fv_jpeg_posix.cpp`); tiff links GDAL's C libjpeg on
  Windows (mirrored as fv_gdal_jpeg) — FalconView's C++ jpeg and GDAL's C jpeg export colliding
  unmangled symbols, so they must never be linked into one binary (separate DLLs on Windows,
  separate test executables here). libpng's TARGET_OS_MAC sniffing needs `-include math.h`.
  `__int64` is now a macro (long long) in fv_compat because `unsigned __int64` must parse.

- 2026-07-14: **FvKit contracts approved** (`port/fvkit-contracts.md`): shared_ptr interface
  ownership (pulled forward from plan step 7), lat,lon order + lon (-180,180] with ±180→+180,
  ll.lon>ur.lon = antimeridian crossing (split into 2 boxes for R-tree), Status{code,message}
  + out-params (bindings raise pyfvw.FvError), RGBA8 PixelBuffer currency, FvKit elevations
  meters/float with void→NaN (feet enum + -32767 sentinel stay below the adapters). No
  subagents for any of this work (per Chris — full per-step visibility).
- 2026-07-14: DTED adapter details: cell bounds always path-derived (enumerator parses
  w082/n31.dt1 names, case-insensitive — TestData has uppercase W084); scan-only enumeration
  (edition stays empty, MDM-style); level preference = highest that opens; coverage north/east
  outer edge posts resolve kOutOfCoverage by floor() (documented in dted.h).

- 2026-07-15: **Second colliding-fork fix pattern**: GeoTiffFrameFile.h's helper classes
  (CGeoData/CTiffTag/CGeoKey/CTiepointTransform/median_cut) are a diverged fork of ImageLib
  geotiff.h's same-named classes (separate DLLs on Windows, ODR collision in one binary).
  Fixed by wrapping the MapServer copy in `namespace gtff` + using-declarations in the header
  (all unqualified uses keep compiling on both platforms, incl. Windows-only GeoTiffMapHandler);
  method definitions qualified `gtff::` (mechanical sed). Prefer this over the jpeg/gdal-jpeg
  "separate binaries" rule when the classes must coexist in one executable.
- 2026-07-15: GeoTIFF FvKit notes: enumeration reads each file's header (GetFrameProperties) —
  unlike DTED there is no path-derived georeferencing; that matches Windows GenerateCoverage.
  ImageInfo deliberately has no geotransform affine (projected DOQs aren't affine in degree
  space); exact per-source transforms are PixelToGeo/GeoToPixel virtuals. f-prefixed TestData
  quads are full 7.5' DOQs (wider than quarter-quads — bounds envelope in tests reflects it).

- 2026-07-15: CADRG frame georeferencing extracted (fv::CadrgFrame). A CADRG frame carries no
  georef in its filename alone: bounds = f(arc zone from last filename char, base-34 frame index,
  fixed series scale). The Windows CRpfFrameFile (COM) computed this; scale came from a SQL
  catalog (tbl_map_series_cadrg). Portable version uses a built-in MIL-STD-2411A series→scale
  table. **Authoritatively validated**: each RPF frame ALSO embeds its own coverage-section
  corners (nw/sw/ne/se), read via cadrg_cib_base::read (handles the NITF wrapper); computed
  bounds match those embedded corners exactly for GNC/JNC/LFC/TLM — so both the scale table and
  the arc math are ground-truthed by the data. (The mosaic.db `maxps` is the mosaic's coarsest
  LOD, NOT native dpp — do not use it as a resolution check.) Only gn/jn/lf/tl are in TestData;
  on/tp/jg/tf/tc scales in the table are standard but unverified here.

- 2026-07-15: `pyfvw.geo.parse_location(text, datum="WGE")` binds geo_tool's GEO_string_to_lat_lon
  (→ GEOTRANS) — parses decimal/DMS lat-lon and MGRS/milgrid to a WGS-84 GeoPoint (needs
  MSPCCS_DATA; MGRS/UTM specifically require it, decimal/DMS don't). pan_viewer gained `--at
  <location>` and centers CADRG on Atlanta by default. pyfvw now links fv_geo_tool. Also fixed a
  latent ctest bug: the pyfvw_pytest ENVIRONMENT used gtest-style `\;` which collapsed all three
  vars into one PYTHONPATH value — MSPCCS_DATA/FVW_TESTDATA_DIR were never set, so every real-data
  pytest had been silently skipping. Plain `;` (set_tests_properties takes a real list) fixes it;
  all 12 pytests now run.
- 2026-07-15: CADRG raster adapter (Half B). RPFRenderer::get_rgb_image always decodes the whole
  1536² frame then crops, so CadrgRasterSource decodes once on first ReadBlock and caches the RGBA
  (crops subsequent reads itself). The LRU (CadrgFrameCache) bounds resident decoded frames
  (~9.4MB each) during a pan — the plan's #1 risk, now a concrete seam. Equal-arc pixel↔geo is
  linear from the frame's WGS-84 corners + dpp (pixel (0,0)=NW); polar frames return kUnsupported
  (none in CONUS/TestData). pan_viewer centers on a frame (not bbox center) because CADRG sample
  coverage is disjoint clusters (CA + GA). Decoder headers force the source TU to C++14 (now
  fv_fvkit_legacy14 bundles geotiff+cadrg sources); the enumerator stays C++17 (clean header).

- 2026-07-16: TIROS georef is filename-based (like CADRG): tiros3 tiles are 1350² JPEGs named
  `TopoBath_<scale>_<ccrr>.wld`; series (`500M`/`001K`) → scale+units → equal-arc dpp, col/row →
  global-grid position. Bounds formulas carry half-pixel (dpp/2) insets + a +dpp_lat/−dpp_lon
  correction, preserved verbatim — this leaves a hairline seam between adjacent tiles (visible in
  the demo, cosmetic). The georef ("FalconView GeoRect v3.3") in the JPEG COM marker is only a
  version tag — no lat/lon; all georef comes from the filename. TirosRasterSource reads real JPEG
  dims via CJpeg::load in Open (don't assume 1350² for the ReadBlock stride).
- 2026-07-16: **Editing Latin-1 shared sources**: the Read/Edit tools re-encode a file's bytes as
  UTF-8 on save, corrupting raw high bytes (e.g. Transfrm.cpp's 0x92 apostrophes → U+FFFD). For
  fvw_core files containing non-ASCII bytes, edit via a binary-safe path (python `open(...,'rb')`
  replace) and verify the diff is only the intended lines. CRLF is preserved by the Edit tool;
  only high bytes are at risk.

- 2026-07-20: **VPF V1 (Vpf/ reader) decisions.**
  - The Vpf sources' prefix header is `Vpf/StdAfx.h`, NOT `VpfMapServer/StdAfx.h`: a quoted
    `#include "stdafx.h"` resolves against the *including file's* directory first, on MSVC and
    clang alike. The POSIX stand-ins go there. (An earlier session edited the wrong one, to no
    effect.) Note the tree spells it `StdAfx.h` while sources include `"stdafx.h"` — fine on
    Windows/macOS, and clang warns (-Wnonportable-include-path); a case-sensitive Linux FS will
    need a fix at that point.
  - Three Win32 idioms got dedicated headers rather than per-site rewrites, because they recur
    across the VPF/GeoSym tree and are exact 1:1 emulations: `fv_win32_filemap.h` (the
    CreateFile → CreateFileMapping → MapViewOfFile whole-file read-only mapping, asserted to
    that one idiom), `fv_win32_finddata.h` (FindFirstFile/FindNextFile/FindClose over readdir;
    HIDDEN = leading dot, which correctly skips macOS .DS_Store), and `fv_win32_path.h`.
  - **`fv_win32_path.h` is the file-open boundary**: it turns backslashes into '/' and resolves
    each component case-insensitively when the literal path misses. This is what lets the reader
    keep its hardcoded `m_path + "\\tileref"` literals AND read DNC's mixed-case layout
    (lowercase dirs, uppercase CAT/LHT tables) unmodified. Wired into CreateFile, CFile::Open
    and `_taccess`. macOS is case-insensitive so it looks unnecessary here — it is what makes
    Linux work.
  - **VPF "long integer" is 4 bytes on disk, but LP64 `long` is 8.** `read_in_header` read the
    header size with `*(long*)` (8 bytes, garbage size + misaligned cursor — an instant
    allocation-size-too-big abort under ASan); same for VPF_INT_LONG field reads and the
    variable-length index count. Fixed with `int32_t` (identical on Win32), and the on-disk
    mirror members in indexes.h retyped to match. This is the same class as the 2026-07-11
    LONG/DWORD finding — expect it in every remaining binary-format reader.

- 2026-07-20: **Two silent-corruption bugs in the CString shim**, both found via this module and
  both now regression-tested. They would have been mis-attributed to the ported code, and they
  affect every module already using fv_cstring.h:
  - `str + '\\'` did NOT concatenate. CString converts implicitly to `const char*`, so with no
    `operator+(const CString&, char)` the compiler silently chose built-in POINTER ARITHMETIC
    and produced a pointer 92 bytes past the buffer (caught by ASan in VPFLibrary::get_path).
    MFC has that overload, which is why the original is correct on Windows.
  - `std::map<CString, T>` (i.e. an emulated CMap) ordered by POINTER VALUE, again via the
    implicit conversion, because CString had no `operator<`. Inserts looked fine and Lookup
    missed at random (VPFDatabase::library_exists). Full ordering + content `operator==` added.
  - Lesson for the shim generally: an implicit `operator const char*` makes missing operators
    compile into wrong code instead of errors. When adding a type to fv_cstring/fv_compat,
    provide the comparison and concatenation operators up front.

- 2026-07-20: VPF bugs found in the original. Per the bit-faithful rule, numeric/behavioral
  quirks are preserved and pinned; **undefined behavior is fixed**, since it has no defined
  behavior to be faithful to.
  - FIXED (UB): `VPFRecordset::VPFRecordset(VPFLibrary*)` did `data.reserve(5)` then
    `data[i] = new VPFVariant()` — writing past the end of an empty vector. (The commented-out
    MFC original, `CArray::SetSize(5,5)`, did size it; the sibling ctor already used push_back.)
  - FIXED (UB): `VPFRecordset::close()` left the file/mapping handles dangling, so `is_open()`
    still returned true and the destructor closed them a second time — a use-after-free on
    POSIX, a recycled-handle close on Win32. Now reset to INVALID_HANDLE_VALUE/NULL.
  - PRESERVED: `VPFDatabase::open(path)` discovers library *names* but never populates
    m_paths_to_root (it computes path_to_root and drops it), so `open_library()` returns a
    non-null but *unopened* library with an empty path. The DataSource/COV `open()` overload
    that FalconView actually calls does populate it, which is why this never bit. Headless
    callers construct VPFLibrary directly.
  - PRESERVED (V2 to fix): reopening a recordset undercounts rows — `close()` clears m_fields
    but leaves m_row_length, and setup_field_info_list accumulates into it, so a second open
    sees a doubled row length and reports half the records (13 → 6).
  - Also: `lst_iter.h` had an MSVC-only extra qualification on a member declaration, and
    `vpfdb.h` another on `VPFFeatureClass::GetName` — both plain removals.

- 2026-07-21: **GeoSym V3 (rule-table layer) decisions.**
  - **Sever line for GeoSym is between rules and drawing**, not at GDI. The
    three rule-table classes (DelimitedParser, AttributeExpressions,
    SymColors) are 100% GDI-free and parse the SymAssign tables into
    decisions; everything that *draws* (SanSymbol's symbol assignment +
    CCGMSymbol, SymText/draw_text, SymLayeredDisplay, the CGMFile interpreter,
    GeoSymServer.cpp's StartRendering/DrawSymbol) is coupled to CDC/CCGMFile
    and stays Windows-only until V4 (CGM→display list) and V5 (VpfRenderer
    over ICanvas). Do not try to port SanSymbol before the CGM display list
    exists — its `.h` pulls in CGMFile/CGMFileCache/SymLayeredDisplay.
  - **CStdioFile added to fv_compat.h** (text-mode line reader). Its
    ReadString mirrors MFC's LPTSTR/UINT overload = fgets: reads through and
    INCLUDING the newline, NULL at EOF. It translates CRLF→LF (strips the
    trailing `\r`) so buffers are byte-identical to the Windows text-mode
    build on the CRLF GeoSym tables — without it, macOS/Linux binary reads
    would leave `\r` in the last token. The GeoSym tables are pipe-delimited
    data rows after a `;`-only separator line, with a comma/colon field-format
    header block skipped before it.
  - **`strtok_s` shim = `strtok_r`** (the MSVC 3-arg form, NOT C11 Annex K's
    4-arg). GeoSym's ParseString relies on strtok_s returning the context
    pointer so it can detect adjacent delimiters (empty fields); strtok_r has
    the same contract.
  - **FIXED (UB)**: `CAEValue::operator=` had no `return *this;` — every
    assignment read an indeterminate reference. UB has no faithful semantics
    (same policy as the 2026-07-20 VPF UB fixes); added the return.
  - Reused the existing `strncpy_s` for `_tcsncpy_s` (identical 4-arg
    contract) rather than a second bounded-copy; `min`/`max` Win32 macros are
    scoped to the GeoSym StdAfx POSIX block so they never shadow std::min/max
    in other TUs.

- 2026-07-23: **V5a found a latent LP64 bug in the V1 VPF reader — coordinates
  were silently shifted by one vertex.** `VPFRecordset::read_field` read a
  variable-length field's on-disk 4-byte count with `*(long int *)` and then
  advanced the cursor by `sizeof(long int)` — 4 bytes on Win32, **8 on LP64**.
  The count itself still came out right (it is the low half of the
  little-endian 64-bit read, and `length` is an `int`), which is precisely why
  V1 never noticed: record counts, field counts and every non-coordinate test
  passed. The damage was to the cursor — it skipped the payload's first float,
  so every coordinate tuple came back as **(lat[i], lon[i+1])** and the last
  tuple read past the end of the array (showing up as an exactly-zero
  component from fresh malloc pages).
  - The symptom was nasty because the output looked *plausible*: real
    coordinates, each one vertex out of step. It first presented as "the
    `coord2_float_t` member names are inverted" — latitude appeared to sit in
    the member spelled `lon`. **That conclusion was wrong.** VPF stores tuples
    as (X, Y) = (lon, lat), exactly as variant.h declares, and vpf_poly.cpp's
    Windows usage of `.lat`/`.lon` is correct.
  - Fixed with `int32_t` (identical on Win32), the same remedy as the
    2026-07-20 `read_in_header` / `VPF_INT_LONG` fixes — this variable-length
    path was simply missed then, because V1 never read a coordinate field.
  - Lesson worth carrying: **an in-range-looking result is not a correct one.**
    The check that actually settled it was a round-trip — feed a parsed
    coordinate back through `VPFCoverage::get_tile_path` and confirm it names
    the very tile the primitive was read from. `CoordinatesAreNotShifted` in
    vpf_vector_source_test.cpp is the regression guard (zero-component,
    outside-extent, and hemisphere checks).

- 2026-07-23: **DTED shaded-relief renderer surveyed (queued Q6b).** The
  hill-shade / elevation-tint / slope / contour engine Chris remembered is in
  `fvw_core/MapDataServer/DtedMapServer/renderer/`:
  - **`DtedReader.cpp/.h` (`CDtedReader`, ~2,900 lines) is the engine** and is
    remarkably portable: **no COM/GDI/MFC/registry** — already std::string/
    std::vector/FILE*-based. The only Win32-isms are COLORREF/BOOL/BYTE/RGB/
    GetRValue (all shimmed in fv_compat.h) and a single `THROW_ERROR_MSG`/
    `GetLastError` in `open()` (POSIX equivalents already exist). Reads a DTED
    cell → **palettized index image (`get_subimage`) + 216-entry RGB table
    (`get_color_table`)** → expand to RGBA8 PixelBuffer.
  - Features confirmed present: shaded relief (`calc_dot_prod` normal·light →
    `get_shaded_color`; `set_light_direction(x,y,z)`, default **az 315°/alt
    45° = NW sun**, exaggeration factor); elevation color bands
    (`set_elevation_bands` + `set_color_bands` → `get_elevation_color`,
    breakpoint interpolation = red/yellow/green); slope shading
    (`get_slope_color`); contour lines (`CheckContour`); color/mono; flat-is-
    black. Display modes 0/1/2 = rendered/elevation/slope.
  - **`disp.cpp/.h` (`DTEDRenderer`) is the only COM/GDI-bound file** (leave
    Windows-only): IActiveMapProj/IGraphicsContext/SAFEARRAY, `GC->PutPixmap`.
    Its role → a thin `fv::DtedShadedRenderer` facade taking light-direction/
    display-mode/bands as params. Mirror the DtedDisplayModeEnum into
    port/include (COM ABI values).
  - **Time-of-day sun is only half in the open tree**: the hook
    (`SetTimeShading` → `set_light_direction(lat,lon)` →
    `convert_to_cartesian(SolarAzimuth,SolarAltitude,…)`) and the az/alt→vector
    math are present, but the date+lat/lon→solar-az/alt astronomy is a **SLAC**
    COM component behind `#ifdef GOV_RELEASE` (NOT shipped). Plan: add a
    standard NOAA solar-position function (~60 lines, license-clean) to replace
    SLAC. Default-NW and user-set az/alt work without it.
  - Coexists with the already-ported `fv::DtedCell` (elevation-query path,
    `port/DtedMapServer`) — different class, different job (this is the
    *rendering* path). Expose as a `"dted-shaded"` IRasterSource so it drops
    into the catalog/engine/pan-viewer (Page-Up/Down scale nav + cursor readout
    for free).

- 2026-07-23: **V5b decisions (style seam, GeoSym engine, renderer).**
  - **Where the seam sits.** `IStyleEngine` returns pixel-valued Pen/Brush/
    TextStyle, never the product's own units: only the engine knows whether
    its tables mean HIMETRIC, points, or millimetres. The renderer hands it a
    `StyleContext{scale, device_dpi, symbol_scale}` once per frame. The one
    thing that stays in authoring units is `VectorSymbol` (HIMETRIC, y-up),
    because a symbol has to stay resolution-independent to be cached.
  - **Style() appends a LIST of passes.** GeoSym rows carry their own display
    priority, and several rows of one FACC can be true at once (a compound
    SAMI line is a wide casing plus a narrow core, at the same priority); a
    single StyleResult could not express that. The renderer stable-sorts all
    passes from all features by priority, LOWER FIRST — which is exactly the
    order CSymLayeredDisplay composited its per-priority bitmaps in.
  - **The 2nd-chance fallback keys off the CONDITION, not the draw.**
    `CSymFACCEntry::DrawSymbol` sets RetVal from `m_attExp.Evaluate(...)`,
    before it knows whether the row produced any ink. Implementing it as
    "did anything get emitted" looked equivalent and was not: DNC soundings
    are label-only rows, so with labels off, 2,564 `defpt` black dots
    appeared that the Windows renderer never draws. Caught by the
    labels-on/labels-off ink comparison.
  - **VPF null floats must reach the rule engine as the EMPTY STRING.** VPF
    encodes a null float as NaN; `%g` renders that as "nan"; GeoSym's ATTEXP
    tables test `att = NULL`, and CAEValue turns the literal NULL into an
    empty string. So "nan" silently failed every sounding/light label rule.
    Related and equally load-bearing: an attribute that is ABSENT makes
    `CAEEntry::Evaluate` return false outright, so "present and null" and
    "not present" are different answers — `VpfVectorSource` therefore keeps
    empty-valued attributes in the list instead of dropping them.
  - **Symbol scaling is FalconView's, quirks included.** `CCGMSymbol::
    DrawSymbol` sizes a symbol as `vdc_extent * zoom / 25.4`
    (s_dblConversionFactor) and maps it through a viewport extent of
    `(+k, -k)` anchored at the symbol's own VDC (0,0) — i.e. 1/100 inch is
    one pixel for symbols no matter what the device is, while LINE WIDTHS go
    through the real `HIMETRICtoDP` and do honour the DPI. Both preserved.
  - **Two more preserved quirks.** (1) SAMI dash run lengths are NOT
    HIMETRIC: CGMFile.cpp scales LineWidth and VerticalDisplacement by 100 on
    read but leaves ElementLength alone (bar the `fabs(len*2)` end-cap
    kludge), and DrawLine then compares it directly against device pixel
    distances — so a dash length is "raw CGM units used as pixels". (2)
    TEXT.TXT documents `tsize` in POINTS, but CSymFont builds the font with
    `nHeight = -m_nSize`, and a negative CreateFont height is character
    height in DEVICE units — so the column behaves as pixels. Converting it
    at 72 dpi would have made every DNC label 33% too big.
  - **Labels are off by default** (`SetDrawLabels`). Not a style choice: label
    glyphs come from the host font (CpuCanvas has no built-in one), so a
    golden hash taken with labels on would be machine-dependent. The golden
    scene renders geometry + symbols only; a separate test turns labels on
    with a host TTF and asserts the ink strictly increases.
  - The renderer's clip helpers are exported (`ClipPolyline`/`ClipPolygon`)
    and tested directly against a stub source and stub style — if that test
    file ever needs VPF or GeoSym to compile, the seam has leaked.

- 2026-07-24: **Physical-display scale, and a real aspect-ratio bug in
  MapScaleUtil it exposed.**
  - The demo drove the projection with `SetScale`, which reproduces each
    map's *baked native pixel density* (CADRG's ~150 m/pixel at 1:1M, etc.),
    NOT the physical scale on the actual screen. `SetPhysicalScale(denom,
    mm_per_pixel)` instead computes ground metres/pixel = `mm_per_pixel ×
    denom / 1000` and converts to dpp — so a 1:1M chart is 1:1M on a
    0.25 mm/px display (1 cm ≈ 10 km). Imagery has no cartographic scale, so
    the engine treats a ground-resolution series as 100% (1 source px = 1
    screen px) at the reference pitch `kNativeDisplayMmPerPixel` and derives
    an equivalent denominator (`metres × 1000 / 0.25`); one knob, `mm_per_pixel`,
    zooms both.
  - **Why the physical path does NOT use MapScaleUtil.** Chris's real
    complaint was aspect ratio that *varied between maps*. Cause: the two
    branches of `GetDegreesPerPixel` — below 1:10M it applies a lon/lat ratio
    from `ResolutionToDegrees`, above it uses a square `dpp_lon = dpp_lat`.
    So the same scene had a different aspect at different scales. Worse,
    `ResolutionToDegrees` is itself **numerically wrong** for the ~1 km
    east-west lines it samples: it reports lon/lat ≈ 2.14 at 32.6° where the
    true value is 1/cos ≈ 1.19 (and ≈ 1.02 at the equator, should be 1.00).
    `CalcRangeAndBearing` (Vincenty inverse) loses precision on tiny
    near-east-west geodesics; the error grows non-monotonically with
    latitude, which is exactly the "varies map to map" symptom. This is
    **preserved** in the bit-faithful `SetScale` path (Windows renders with
    it too), so it is NOT touched. The NEW physical path computes dpp from
    the standard WGS84 metres-per-degree series (`MetersPerDegree` in
    map_projection.cpp), giving square ground cells (ratio = 1/cos(lat)) at
    every latitude and scale. Tests pin the ratio against 1/cos and assert it
    is nowhere near the old ~2.1.
  - `ResolutionToDegrees` was briefly promoted to public and then reverted —
    the correct fix was to not use it at all, keeping the MapScaleUtil header
    surface unchanged.


- 2026-07-24: **VPF V5c AREA features — extract the winged-edge walk, don't
  port the drawing.** A `.AFT` (area feature table) row carries `fac_id`, a
  foreign key into the tile's FAC (face) table; FAC gives `ring_ptr` → a row in
  RNG (ring) → `(face_id, start_edge)`; the boundary is then a loop of directed
  EDG edges linked by the winged-edge fields `right_edge`/`left_edge`/
  `right_face`/`left_face`. In DNC these link fields are `K` (triplet) type, and
  only the local `.id` component matters because faces are self-contained within
  a tile (the original indexes its edge array by id alone).
  - The walk lived entirely in the Windows-only `VPFFace`/`vpfelem`, but those
    files are welded to GDI (`CRgn`, `CPoint` screen points, `CDC` draw) and,
    through their includes (`Map.h`, `param.h`, `refresh.h`, `graphics.h`), to
    the FalconView app layer — which is exactly the coupling the plan says to
    sever at ICanvas, not GDI. So the GEOMETRY half is reproduced in
    `fv_vpf_vector_source.cpp`: `TraverseRing` is `VPFFace::TraverseRing`
    verbatim (same winged-edge decisions, same "stop when we re-cross the start
    edge in the same direction, not merely the start node" termination), and
    `RingPoints` is `GetPointList` with the screen-point conversion removed. The
    edges of a tile are read once into an in-memory `Edge` array applying
    `VPFEdge`'s exact 1-based→0-based conversions and its add-last-vertex
    correction (CND supplies node coords). None of the CRgn/region math is
    ported — a headless vector source emits rings; a face that "drew nothing" on
    Windows is simply not emitted.
  - **`part[0]` = outer ring, `part[1..]` = holes.** Outer rings are kept
    whether or not they have "body" (matching VPFFace, whose body ASSERT is
    commented out); inner rings are kept only if they have body. Rings are
    closed loops (verified: all 502 dnc17 areas close, first==last) and carry
    duplicate vertices at shared nodes exactly as `GetPointList` did (coincident
    points are harmless to a scanline fill).
  - **dqyarea is not a feature layer.** DNC's data-quality coverage area table
    (DQYAREA.AFT) has no `f_code`/FACC — it is chart-source metadata, not a
    symbolized class. Area layers whose `.AFT` lacks an `f_code` column are
    skipped, preserving the "every emitted feature has a style key" invariant
    the point/line classes already satisfy.
  - **Bit-faithful + two UB/hang guards.** Preserved: the 1500-edge
    overly-large-face skip (a data-problem heuristic tied to Windows' 64K CRgn
    limit). Added (the GDI original lacked them, having no defined behavior to
    be faithful to): bounds-checks on every winged link and a `2N+16`-edge
    iteration cap, so a malformed or cross-tile link abandons the ring instead
    of indexing garbage or hanging headless. Same policy as the earlier VPF UB
    fixes.
  - The V5b golden harbor PNG moved (areas now fill) and was re-pinned after a
    visual check — land/earth-cover gray, water light-blue, danger magenta,
    under the existing coastline/contours/navaids.

- 2026-07-24: **DNC on screen — map scale vs feature zoom are two knobs, not
  one** (pyfvw vector bindings + pan-viewer `--vpf`). DNC has no single ideal
  display scale, and Chris wanted both behaviors available and distinct:
  - **Map scale** is the projection: `MapProjection::SetPhysicalScale(denom,
    mm_per_pixel)` sets how much ground fills the window (1:N), with the
    correct latitude-dependent aspect (the row-14r WGS84 metres-per-degree
    path, ratio = 1/cos(lat)). Point symbols and line widths are sized in
    DEVICE pixels (HIMETRICtoDP / the CCGMSymbol 1/100-inch rule), independent
    of the projection, so they keep their pixel size as N changes — exactly a
    cartographer's scale change.
  - **Feature zoom** is the renderer: `SetSymbolScale(s)` scales point
    symbology and `SetDeviceDpi(96*s)` scales line widths, together, WITHOUT
    touching the projection — so symbology grows/shrinks while the map extent
    holds. Verified with a 3-panel montage (1:150k×1 vs 1:150k×2.5 vs
    1:400k×1) and a pytest that pins the independence (feature zoom adds ink at
    fixed ground; scale change keeps symbol pixel-size).
  - The demo holds `mm_per_pixel` at the native pitch and moves `scale_denom`;
    a `MapProjection` ctor + setters were bound so a vector viewer configures
    the projection directly (the raster demo still receives one from the
    engine).
  - **Reader log hygiene**: `VPFRecordset::get_field_value(name)` logs a line
    on every miss; `get_field_info(name)` is silent. Probing existence with
    the latter before the former turned thousands of `[log]` lines per render
    (point tables miss "coordinates" on every row; lim/maritime complex
    features miss tile_id/fac_id/edg_id on every row) into zero. Complex-
    feature area/line tables with no simple primitive reference are skipped up
    front — they already produced no geometry, just noise.

- 2026-07-24: **WVS (World Vector Shoreline Plus) blocked on FCA-less coverage
  enumeration** (TestData/`VPF 2`/WVSPLUS/WVS{012,040,120}M — a VPF product,
  tiled with single-letter multi-level tile paths like `COC/N/L/AA/`). The
  library opens, but `VpfVectorSource` reports "no point/line feature
  classes": `VPFCoverage::create_feature_class_list` builds the class list
  ONLY from the FCA (feature-class attribute table), and WVS's thematic
  coverages (bat/coc/gazette) ship no FCA — only an FCS. So the list is empty
  for every coverage. (DNC's thematic coverages all have an FCA, which is why
  this never surfaced.) To enable WVS: enumerate feature classes from the FCS
  (or by scanning the coverage dir for *.LFT/*.PFT/*.AFT) when FCA is absent,
  and add a **simple non-GeoSym stroke style engine** (WVS is shoreline/
  bathymetry lines + country/ocean areas + gazette points, with no GeoSym
  symbology tables; its FACC column is spelled `facc`, not DNC's `f_code`).
  Deferred from row 14t to keep the working DNC path stable.

- 2026-07-24: **DTED shaded-relief renderer (Q6b) — sever at the pixmap, keep
  the engine whole.** The 2026-07-23 survey held up exactly: `CDtedReader` is a
  clean, portable engine and only `DTEDRenderer` (disp.cpp) is COM/GDI-bound.
  Decisions and preserved quirks:
  - **Rendered image is (image_width-1) x (image_height-1).** disp.cpp trims the
    north post-row (`vpix` min clamped to 1) and the east post-column (`hpix`
    max clamped to width-2) because they overlap the neighbouring cell; the
    facade renders exactly that extent (`get_subimage(0, 1, W-2, H-1, …)`) so
    cells tile seamlessly. `RenderedWidth/Height` and `Info().size` report the
    trimmed dims, not the raw post counts. **Bit-faithful, not a fix.**
  - **The index image is bottom-up (south row first).** get_subimage fills
    `image[row*w+col]` with output row 0 = the southern edge (it reads elevation
    columns south→north and Windows' DIB pixmap consumes bottom-up). The RGBA
    `PixelBuffer` contract is top-down (row 0 = north), so `Render` flips
    vertically while expanding the 216-entry palette. Verified visually: w082/n31
    renders as coastal Georgia with the ocean correctly to the east, N-up.
  - **Equal-arc transforms match disp.cpp's georef exactly.** Pixel (px,py)
    centre = (`image_nw_lon + px*dpp_lon`, `image_nw_lat - (1+py)*dpp_lat`); the
    +1 row offset is disp.cpp's `subimage_ul_lat = image_nw_lat -
    subimage_min_vpix*dpp` with min_vpix=1. dpp = the UHL post interval; image
    bounds = data bounds (open() sets `m_image_* = m_data_*`, despite the header
    comment claiming otherwise — preserved).
  - **Display-mode enum split.** The 6-value `DtedDisplayModeEnum` (mirrored to
    fv_dted_enums.h, COM ABI values) maps onto CDtedReader's internal
    `display_mode` 0/1/2 (rendered/elevation/slope) via `mode>>1`, and its
    color/mono flag via `mode&1` — verbatim from DTEDRenderer::SetDisplayMode.
  - **SLAC → NOAA.** SLAC (ISlac, GOV_RELEASE-only) is not in the tree; the
    date+lat/lon→solar-az/alt astronomy is replaced by the standard NOAA Solar
    Calculator equations (fv_solar_position.cpp, ~90 lines, no refraction term —
    documented, sub-degree vs SLAC, far finer than shading needs). The az/alt →
    light-vector step (`convert_to_cartesian`) and the default NW sun (az 315 /
    alt 45) were already open in disp.cpp and moved into the facade unchanged.
    Time-shading forces exaggeration 3.0 exactly as disp.cpp did.
  - **Missing-data / sea-level stay opaque** (alpha 255 everywhere): Windows drew
    DTED with PutPixmap `bTransparent = VARIANT_FALSE`; index 215 = black
    (void), 214 = blue (sea level). Not made transparent, to stay faithful; a
    consumer that wants voids to drop out can revisit.
  - **Latent uninitialised `m_dted_mono`** in CDtedReader's default ctor (only
    `set_color_mode` writes it) is sidestepped: the facade calls
    `set_color_mode` on every Render. Same for the slope-mode side effect
    (case 2 clobbers `m_light_dir_*`) — lighting is reapplied each Render.

- 2026-07-25: **Mariner depth settings (vessel draft → bathymetry colouring) —
  the rules are ported, the payload is not.** Chris recalled FalconView letting
  the user enter a vessel depth and colouring the bathymetry from it. Surveyed
  end to end; **full write-up in `port/vpf-geosym-plan.md` §5.7** (Windows
  chain, the ATTEXP worked example, the S-52 mapping, the recommended design).
  The three findings that matter:
  - **It is real and the knobs are already ported.** `CECDISValues`
    (ISDM shallow-marking on/off, IDSM shade count, **SSDC ship's safety depth
    contour**, MSDC deep, MSSC shallow — all metres) came across with GeoSym V3
    and IS consulted: `CAEAttributeList::GetValue` falls through to
    `CECDISValues::GetValueForString`, so `ssdc`/`msdc`/`mssc`/`isdm`/`idsm`
    are **pseudo-attributes in the ATTEXP rule language**, and
    `fv::GeoSymStyleEngine` evaluates those rules today. On Windows the UI is
    `CDNCAdvancedDialog` ("DNC Advanced Options" → "Water depth marking"),
    persisted through `CVPFPreferences` and pushed in over
    `IGeoSymServer::SSDC`. **The rule link is ATTEXP condition index ==
    fullsym.txt ROW ID** — fullsym.txt has no attribute-expression column.
  - **The port cannot draw the result.** Every depth-shade row's payload sits
    in the `areasym` column, which `Style()` parses into `SymRow::area_sym`
    and **never reads**; the engine never sets `StyleResult::fill`, so
    `VectorRenderer` strokes areas instead of filling them. Verified against
    the real harbor render, not assumed: the visible blue/grey "fills" are
    point symbols (hazardp/buoybcnp/…), and hydarea contributes only thin
    strokes to the R1 pick index. **DNC areas are outline-only today** — see
    the correction added to row 14s.
  - **Nothing can set the knobs**: `CECDISValues ecdis;` is default-constructed
    (ISDM 1, IDSM 0, SSDC 10, MSDC 30, MSSC 2 — which are FalconView's own
    defaults) and never written.
  So it is one missing feature (area fill) plus one missing API, not a gap in
  the rule layer. **Plan: land area fill, then a product-neutral
  `MarinerSettings` on `StyleContext`** (shallow/safety-contour/safety-depth/
  deep, shade count, shallow pattern) — carried per render, NOT held as engine
  state, so it participates in R3's style epoch and a draft change can never
  serve a cached chart. **Do it in R2**, when GeoSym and S-52 tables are both
  in hand: S-52 has the same knobs (its CS procedures DEPARE01/SEABED01,
  DEPCNT02, SNDFRM03) and they map 1:1 except that **S-52 separates SAFETY
  DEPTH from SAFETY CONTOUR** where DNC conflates them into SSDC — so the
  shared struct carries both, with DNC setting them equal. Divergent default
  to decide when implementing: `VPFPreferences.cpp`'s fallbacks (ISDM 0/IDSM 1)
  contradict `CECDISValues`' ctor and `DefaultDncOptions.h` (ISDM 1/IDSM 0).

- 2026-07-25: **Identify (R1) — hit-test the INK, describe from the SOURCE.**
  The two halves are deliberately asymmetric, and that asymmetry is the design:
  - **Picking is a scene structure, not a spatial query.** `PickIndex` is filled
    by `VectorRenderer` from the primitives it emits, in canvas pixels. A
    source-side query would hit features that were styled invisible, fell
    outside the scale band, or lie off-canvas — none of which the user can see.
    So a stroke is indexed at its pen half-width (not its centreline), a fill at
    its clipped ring, a point symbol at the extent it actually inked, and a
    label at its measured text box. A symbol whose display list drew nothing
    (unknown symbol id) leaves NO hit box, pinned by a test.
  - **Consequence to remember**: the index is valid only for the last
    `Render()`, and every `Render()` clears it. A UI that pans and then hit-tests
    a stale index would be lying; there is nothing to keep in sync because the
    index simply does not outlive the frame.
  - Point symbols get a `kMinPickBox` = 9 px floor per axis, grown about the
    ink's own centre. A 2-px navaid dot is a real thing to aim at, and hit
    targets are the one place where matching the drawn pixels exactly is the
    wrong answer.
  - **Describing goes back to the source**, by `FeatureRef`, so precision and
    completeness survive the render path's simplifications (which R3 will add).
    `Describe()` is virtual with a kUnsupported default — a product that has no
    dictionary yet does not have to fake one, and a caller can distinguish
    "no such feature" from "this product cannot describe".
  - **VPF decoding**: `INT.VDT`/`CHAR.VDT` are ordinary VPF tables, so
    `fv::VpfValueDescriptions` is a pass over the V1 recordset layer, not new
    parsing. Table/attribute keys match case-insensitively — the VDT spells the
    table `hydline.lft`, the rest of the reader carries `HYDLINE.LFT`. Column
    *names* come free from `VPFFieldInfo::m_desc` (the table header already
    carries "Accuracy Category"), and the class description from the FCA. A
    missing VDT is normal, not an error.
  - **VPF integer nulls are blanked in `display` only.** MIL-STD-2407 nulls a
    short int with −32768 and a long with −2147483648; `VariantText` renders
    them verbatim, which is REQUIRED on the style path (GeoSym's ATTEXP tables
    must see what Windows saw) but is noise in a popup. Identify blanks the
    display and keeps `raw`, so nothing is lost. Floats needed no equivalent —
    VPF nulls those as NaN and `VariantText` already yields "".
  - **New guard, same policy as 14s**: `VPFRecordset::set_absolute_position`
    returns FAILURE for an out-of-range row but only AFTER an `ASSERT` that MFC
    compiles out of Release and `<cassert>` does not — so an id read out of the
    data aborts a headless debug build where Windows merely failed. All four
    call sites now go through a range-checking `SeekRow()`. Same quirk class as
    the CGM parser's unknown-opcode ASSERT (14o).
  - `VectorFeature`'s loose `tile_id`/`feature_id` became one `FeatureRef ref`
    — the only part of a feature the render path needs to keep. Attributes stay
    on the feature for now because the GeoSym engine rules on them at style
    time; dropping them from the render path is R3's `FeatureBatch` work.

- 2026-07-25: **155 MB DTED blob purged from git history** (per Chris — the
  cell is never referenced from git). `testdata/dted/w106/n40.DT3` had been
  committed in `b4030c06` (port(geo3)) and carried through every commit since:
  154.6 MB raw, ~66 MB of the pack. Removed from all 66 commits with
  `git filter-branch --index-filter`; `.git` went **151 MB → 85 MB**, and the
  largest remaining blob is 4.9 MB (`third_party/sqlite3.c`). All 66 commits
  survive, but **every SHA from b4030c06 onward changed** — harmless here, as
  the repo has no remotes and nothing was ever pushed. This clears the standing
  "never push FVW without rewriting history first" blocker for the Peregrine
  public-repo work.
  - **Gotcha for any future rewrite**: `filter-branch` resets the working tree
    at the end, so it *deletes the purged file from disk* even though
    `--index-filter` only touches the index. The cell had to be restored from
    the pre-rewrite `.git` backup (verified byte-identical by sha256). It must
    stay on disk: `dted_adapter_test.cpp`'s
    `GetElevation({40.5,-105.5}) == kIoError` asserts the prototype `.DT3` is
    *indexed but unreadable*, which needs the file present.
  - `.gitignore` now lists `test_data/`, `TestData/` **and** `testdata/`. The
    blob got in because git tracked the lowercase path while the ignore said
    `TestData/`; that only masks on macOS (`core.ignorecase=true`) and would
    not on a case-sensitive Linux checkout.
  - Pre-rewrite backup: `../FVW-git-backup-20260725` (a full copy of the old
    `.git`, 151 MB). Delete once satisfied — it still contains the blob.

- 2026-07-25: **ENC E1 — a base cell is a STALE chart, and the reader says so.**
  `.000` is the base edition; `.001…` are mandatory incremental updates. E1 does
  not apply them (E5 does), so `S57Cell::Open` scans for update files next to the
  cell, exposes them as `unapplied_updates()`, offers `StalenessWarning()`, and
  **prints that warning to stderr on every open**. All four delivered NOAA cells
  have updates (US5CHSDC has three), so there is no "base is fine" case in this
  data — a pinned test asserts exactly that, and the E3 render path must refuse
  or badge a stale cell rather than draw it silently. Related decisions:
  - **Object classes and attributes stay NUMERIC** (`OBJL 42`, `ATTL 87`). The
    acronyms (DEPARE, DRVAL1) live in S-57 Appendix A, which is not in the cells;
    writing them from memory would put invented metadata in front of a mariner,
    so E1 ships codes and E2 brings the catalogue with the PresLib (new blocker
    below). The same gap costs DSSI's meta/cartographic/geo record split, which
    is a property of the object CLASS, not of anything in the record — so
    `S57ParseCounts` verifies the feature-record TOTAL plus the collection count
    (primitive-less features = NOLR exactly, in all four cells) and leaves the
    rest to E2.
  - **Geometry is assembled, never inferred.** Chain-node (DSSI DSTR=2) area
    rings are chained by exact node identity — S-57 coordinates are COMF-scaled
    integers, so shared nodes compare exactly and an epsilon would merge distinct
    vertices. A ring that will not close, an unresolvable edge, or a missing end
    node sets `geometry_complete = false` and keeps what was read; the tests
    assert 0 incomplete across all four cells, so that flag is a guard, not an
    excuse. `FSPT.MASK` is deliberately ignored: masking says which parts of a
    boundary S-52 draws, not which parts exist.
  - **Two container traps worth knowing before the next 8211 format.** (1) A data
    record's leader leaves the field-control length blank (it applies to the DDR
    only) — a digit-strict leader parse rejects every NOAA cell. (2) The
    directory entry-map widths (field length / position) are **per record**, not
    per file: these cells' DDR uses 3/4 while short data records use 3/2, so
    reading the DDR's widths for every record shifts every field. Both have
    hermetic regression tests.
  - **ASCII `I`/`R` subfields keep their raw characters**: STED is `R(4)` "03.1",
    and reformatting it as a double prints "3.1". `Value::AsText` returns what
    was stored whenever the value arrived as characters.

## Blockers / needs from Windows machine

- [x] Standard .dt1 cells arrived with the completed copy (w082-w083/n30-n34, 1201x1201 posts);
      real-data DTED tests run and pin known elevations from n31.dt1.
- [x] **GPS_get_y2k_compliant_year verified** (2026-07-16, full tree arrived): the original
      (Applications/FalconView/MovingMapOverlay/gps.cpp) uses pivot **70** (71-99→19xx,
      00-70→20xx; GPS epoch 1980 + 10-year buffer), not the pivot 80 this port guessed.
      fv_netnmea_missing.cpp corrected to match — years 71-79 now map to 1971-1979.
- [x] Sample map data in `TestData/` (git-ignored), complete 2026-07-11: dted/ (w106/n40.DT3),
      vpf/dnc17, geotiff/ (9 USGS DOQ quarter-quads), rpf/ (530 CADRG frames: cgnc/cjnc/clfc/
      ctlm50, NITF 2.0-wrapped, layout rpf/<type>/<zone>/<frame>), tiros3/ (per Chris: TIROS
      "*.WLD" files are actually GEOJPEGs — Readable by the geotiff libraries with a FalconView comment marker — plus lowercase
      .wld world files; reader = jpeg decode + sidecar georef).
- [ ] Reference output dumps from the Windows build (elevations, decoded-pixel checksums) for
      golden-file tests, once Phase 2/3 modules near completion.
- [x] **ENC test data ARRIVED 2026-07-25** (Chris): four NOAA cells around **Charleston SC** in
      `TestData/enc/` (git-ignored) — `US5CHSDC/`, `US5CHSDD/`, `US5CHSEC/`, `US5CHSED/`, each a
      `.000` base cell plus its `.001…` update files and `US253*.TXT` chart notes, with the
      leftover `ENC_ROOT*/` shells and a `CATALOG.031` alongside. **Q8/E1 is unblocked.**
      Full format note — naming, usage bands, the 2×2 grid they cover, what `CATALOG.031`
      contains and why update files are not optional — is in `port/vpf-geosym-plan.md` §7
      ("ENC exchange-set layout"). Three things that change E1's design, repeated here because
      they are easy to get wrong:
      (1) the cell dirs were lifted OUT of `ENC_ROOT/`, so **enumerate by finding `*.000`**,
      never by the standard `ENC_ROOT/<producer>/<cell>/` path;
      (2) **`CATALOG.031` is itself ISO 8211** and its CATD records carry each cell's bounds and
      long name — a free `FrameInfo` row without opening the cell;
      (3) `.000` is only the base edition — `.001…` must be applied in order or the chart is
      silently stale (US5CHSDC has three). E1 may be base-only, but must say so loudly.
      Geography: 32.70–32.85 N, 80.025–79.875 W (Charleston Harbor / Ashley River), which
      **overlaps the 2026-07-23 Charleston DTED2 + GeoTIFF refresh** — so "ENC over shaded
      relief" is available as a real demo.
- [ ] **S-52 PresLib (Q10/E2)**: OpenCPN's `chartsymbols.xml` plus its `rastersymbols-*.png`
      sheets, from an OpenCPN install or its source tree (GPL-3.0 — see the queue note on why
      that is fine as data). Drop under `TestData/s52/` (git-ignored), supplied at runtime by
      data-dir arg like `TestData/GeoSymbol`.
- [ ] **S-57 Appendix A object/attribute catalogue (E2, needed with the PresLib)**: the
      object-class and attribute code tables, i.e. GDAL's / OpenCPN's
      `s57objectclasses.csv` + `s57attributes.csv` (both ship with either project;
      the codes are IHO spec data). Drop beside the PresLib under `TestData/s52/`.
      Until then E1's reader reports numeric `OBJL`/`ATTL` codes only — see the
      2026-07-25 decision on why they are not written from memory. This also
      unblocks DSSI's meta/cartographic/geo record split and gives `Describe()`
      real names for ENC, the way `INT.VDT`/`CHAR.VDT` did for VPF in R1.
- [x] **GeoSym asset directory ARRIVED 2026-07-21**: `TestData/GeoSymbol/` (git-ignored) —
      `SymAssign/` (rule tables: fullsym.txt, simpsym.txt, …) + `Graphics/*.cgm` (757 CGM
      symbol files). Chris flattened an original double-nesting (`GeoSymbol/GeoSymbol/`) on
      2026-07-21. The reader composes `<DataDir>\GeoSymbol\SymAssign\…` and
      `\GeoSymbol\Graphics\`, so **DataDir = `TestData`**; wire it via the ctor/env data-dir
      arg (registry → env), subpaths stay relative. Ignore any `.svn/` metadata dirs.
      Historical (how it was found 2026-07-16): `HKLM\SOFTWARE\[WOW6432Node\]MissionPlanning`
      →`JMPS` value `DataDir`, typical JMPS layout `C:\data\Local\JMPS\Data\GeoSymbol\`.
      Still wanted: reference screenshots of dnc17 harbor views (V5/V7 goldens).
      ~~V0 fork question~~ resolved from project files: VpfMapServer compiles Vpf/;
      VPFDataLib is a separate project (VPFDataRenderServer, VvodAnalysisServer).

- 2026-07-16: **Full Windows tree copied into the repo top level** (Applications/ incl. the
  FalconView app, Components/, Plugins/, UIControls/, unit_tests/, props/, Custom Actions/).
  Untracked in git so far (Chris to decide on committing). Immediate yields: y2k pivot
  verified+fixed; VPF render loop located in-tree (VPFMapPlugIn_imp.cpp — see
  vpf-geosym-plan.md); V0 fork question answered. App trees fall under the hard rules
  (never modify Windows build files).
