# FalconView Port — ARCHIVE (full historical ledger through 2026-08-04)

**This is the archive.** The working ledger is `port/PORTING.md` — read that first.
Come here only when the working ledger points you at a specific dated decision or a
completed module row. Nothing here is a to-do list; every row is finished work.

**Read this first in every session. Do not re-explore the repo.**
Full strategy: `/Users/chrisbailey/.claude/plans/this-project-is-extreamly-enumerated-marshmallow.md`
(key facts repeated here so this file is self-sufficient).

## Status legend
`—` not started · `C` compiles on macOS · `T` tests pass · `P` exposed in Python

## Docs
- `port/bindings/pyfvw/README.md` — **the Python user guide** (catalog → base map →
  overlays → vector products, with runnable examples). Written 2026-07-29.
- `port/bindings/pyfvw/ICD-MAPPING.md` — legacy COM ICD → pyfvw name/semantic mapping (D5).
- `port/fvkit-contracts.md` — the FvKit + binding contracts (D1–D6).

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
| F2 | DNC point symbols drew UPSIDE DOWN (2026-07-27, found by Chris in PythonView) | 1 | T | A **one-multiplier** fidelity bug in the CGM→VectorSymbol conversion, present since V5b and invisible to every test because the harbor golden was pinned over it. The CGM VDC EXTENT's direction multipliers (`m_iDirX`/`m_iDirY`; for the standard `lly<ury` extent that **every** GeoSym symbol uses, `dirY = -1`) are applied **TWICE** on Windows: once by `CCGMFile::ReadVDCScaledY` as coordinates are read, and again by `CCGMDrawingObject::RotateVDC` (CGMFile.cpp:2246, "Need to do VDC adjustments for reflection even if zero rotation angle") when it builds the `m_disp_vertices` the GDI path actually draws — after which `CSanSymbol`'s DC (`SetViewportExt(k,-k)`, SanSymbol.cpp:280) flips a third time onto the screen. Row 14o deliberately reads `m_vertices`, NOT `m_disp_vertices` (the rotated copy), which is right for geometry but silently dropped that second multiplier — so `CgmSymbol`'s display list is **y-DOWN**, while `fvkit/vector/style.h` documents `VectorSymbol` as y-UP and `VectorRenderer` flips on the way to pixels. Two flips instead of three = every DNC point symbol mirrored. Fix: `CgmSymbol` now exposes `dir_x()`/`dir_y()` and `ToVectorSymbol` applies them (`ApplyVdcDir`) exactly where `RotateVDC` does, extent included. **Rotation was already consistent and needed no change** — Windows rotates *before* the multipliers and we rotate after, but `diag(1,-1)·R(a) == R(-a)·diag(1,-1)`, the same net transform. The `fv_cgm_symbol.h` header comment claiming "Y is VDC-up" was **wrong** and is corrected: `bounds().top` is the SMALLER value (0003.cgm parses to top=-628, bottom=58), which is exactly the evidence the old comment contradicted. New test `GeoSymStyle.SymbolComesBackInTheAuthoredYUpFrame` pins the orientation **independently of the golden hash** — 0003.cgm's authored extent ll=(-175,-58)/ur=(304,628) must come back out sign-for-sign, and the V4-pinned vertex y=-415 must arrive as +415 — so a future re-pin cannot hide a flip again. Harbor golden re-pinned after visual check (`0x71724acb5cb517a` → `0x8f9cc78e521ed986`); nothing else moved. **377 total.** **Lesson for the pending list**: the V5b golden was "visually verified", and upside-down symbology survived it — asymmetric symbols need a directional assertion, not an eyeball. Unrelated flake noticed: `TilePackReal.{WriteReadRoundTrip,EnumeratedAndRenderedThroughEngine}` fail under `ctest -j8` and pass serially (shared output path). |
| E3a | ENC E3a (Q11, first slice): `EncVectorSource` + `S52StyleEngine` + CS registry + golden Charleston chart | 6 | T | Fifth session of the ACTIVE TRACK, 2026-07-27, sliced per Chris (Q11 as written was five items — the placer and the `LookupTableStyleEngine` extraction are E3b/E3c). **ENC now renders.** New `port/Enc/fv_enc_vector_source.{h,cpp}`: `fv::EncVectorSource` over E1's `S57Cell` + E2's `S57ObjectCatalog` — one cell = one `FeatureRef::tile`, `style_key` and `layer` are both the object-class ACRONYM (the catalogue is REQUIRED, not optional: without it every feature would be unsymbolizable, so Open fails instead), attributes arrive under their acronyms, and **SCAMIN** thinning lives here rather than in the style engine because it is a property of the DATA (S-52 §8.4.4) — honoured only when the query names a scale, so a bulk query never loses features. Serves the four Charleston cells: **4,470 features (931 point + 2,014 line + 1,525 area) across 58 object classes**, `Describe()` decoding through Appendix A. New `port/Enc/fv_s52_style.{h,cpp}`: **`fv::S52StyleEngine`**, the SECOND implementation of the `IStyleEngine` seam — and the proof of §5.1, since **nothing in fvkit changed to accept it**: same `VectorSymbol`, same `VectorRenderer`, same R2 `RuleSet`/`ViewingGroupSet` (S-52's BASE/STANDARD/OTHER is the same axis GeoSym's `dispcat` feeds). Five lookup tables → table chosen by geometry + mariner display settings; first matching row wins; instructions SY/LS/AC/TX/TE executed, **LC and AP approximated** (dashed pen / ink-colour-at-alpha — the same deviation GeoSym's stipple fills carry, and both become exact with the shared along-path placer in E3b). **`S52MarinerSettings` closes the ENC half of the MarinerSettings item F1 left open** — safety/shallow/deep contour, safety depth, two-shade mode — and the depth ramp really moves when the mariner moves the contour (pinned). **CS registry**: procedures are registered by name and return an INSTRUCTION STRING (what the spec says a procedure produces), executed by the same executor. Implemented by weight over the real cells: DEPARE01 (SEABED01 ramp), DEPCNT02, SLCONS03, QUAPOS01, SOUNDG02. Unimplemented ones (LIGHTS05, OBSTRN04, TOPMAR01, WRECKS02, RESTRN01, RESARE01/02, DATCVR01) are **counted, not silent**, and draw the library's own QUESMRK1 — but ONLY when the row put no other ink down, so a beacon that drew its own symbol is not overstamped. **Golden PNG pinned after visual check** (`0xc9dc204d07759233`): Charleston Harbor as a real ENC — buff land, the depth ramp deep to shoal, black point symbology, magenta marks. 13 source + 22 style/render gtests. **456 total.** ASan+UBSan clean. **Three findings, all in Decisions below**: the `?` lookup condition is S-57's UNKNOWN-VALUE marker and NOT a wildcard (read as a wildcard it paints the whole harbour no-data grey); 17 symbol names the cells reach are RASTER-ONLY definitions, not dangling references, so a vector-only path cannot draw them; and a feature legitimately draws nothing in exactly two data-given cases, which the corpus sweep asserts rather than tolerates. |
| E3b | ENC E3b (of Q11): the shared along-path/area placer + the 7 remaining CS procedures | 6 | T | Sixth session of the ACTIVE TRACK, 2026-07-27. **Two halves, both of them cross-product.** (1) **The placer §5.1 promised.** `fvkit/vector/style.h` gains `PathRun{kGap/kDash/kSymbol}` + `LinePatternStyle` (a CYCLE of runs measured in pixels, plus the pen its dashes take) + `AreaPatternStyle`, and `fvkit/vector/renderer.h` gains **`PlaceAlongPath`** and **`PlaceOverArea`** — pure geometry over pixels, no product, no canvas, no style engine, which is what lets 10 hermetic gtests pin them. GeoSym's SAMI line style, S-52's `LC` and S-52's `AP` are now three callers of two functions instead of three approximations: **`GeoSymStyleEngine`** routes a component to the placer when its cycle contains a point-symbol element (89 of the 757 delivered CGM line symbols do; a pure dash/gap component keeps the cheaper pen, and draws identically), and **`S52StyleEngine`**'s `LC` stamps the line-style at its definition's own `vector_width` period while `AP` stamps the pattern on a `distance_min`-pitched grid, staggered when `filltype` is `S`. **Found a real bug doing it**: the pre-E3b GeoSym code pushed a point-symbol element's LENGTH into `Pen::dash`, so every SAMI symbol run silently became a DASH — the DNC cable/limit lines were black dashes where the CGM authors a magenta chain of symbols. (2) **The CS worklist is closed**: DATCVR01, LIGHTS05, OBSTRN04, RESARE02, RESTRN01, TOPMAR01, WRECKS02 join E3a's five, and `unhandled_cs()` over the four Charleston cells is now **empty** — the corpus test asserts the empty set rather than listing what is owed. 281 features that drew a question mark now draw their own symbology: 100 lights take a coloured flare, 105 obstructions and 14 wrecks their hazard marks against the mariner's safety contour, 42+14 restricted areas their boundary line. **E3a's stated blocker on TOPMAR01 dissolved** — the TOPSHP→symbol table is not missing, it is the JOIN between `s57expectedinput.csv`'s TOPSHP enumeration ("cone, point up") and the library's own symbol descriptions ("topmark for buoys, cone point up"), both of which E2 already loads. Golden PNGs **re-pinned after visual checks**: Charleston (`0xc9dc204d07759233` → `0xdabc9bc116c24d88`) now shows red/green/white light flares over the navaids; the DNC harbor (`0x8f9cc78e521ed986` → `0x33b43456964b19da`) moved by 972 pixels, all of them the cable line becoming its real symbol chain. 10 placer + 2 renderer-integration + 1 GeoSym + 12 S-52 gtests. **479 total.** ASan+UBSan clean (only the pre-existing VPF unaligned loads and ImageLib's jpeg shifts; `pyfvw_pytest` aborts under ASan at extension-import time, which is the uninstrumented-interpreter failure, not a finding). Six deviations and three data findings in Decisions below. **NOT here**: E3c, the `LookupTableStyleEngine` extraction. |
| E3c | ENC E3c (of Q11): the `LookupTableStyleEngine` extraction | 4 | T | Seventh session of the ACTIVE TRACK, 2026-07-27, and the last item Q11 was carrying. **The abstraction §5.1 asked for, cut third — deliberately.** It was not cut against GeoSym alone (R2's decision), and not cut when only the second TABLE existed (E2's decision); it is cut now that two real engines are written and rendering, so it describes what they share instead of guessing. New `port/include/fvkit/vector/lookup_engine.h` + `port/fvkit/vector/lookup_engine.cpp`: **`fv::LookupTableStyleEngine`**, a template-method base owning the parts that were literally duplicated — the R2 rule layer (`RuleSet`/`ViewingGroupSet`/the memoized `ResolvedPlan` with its recompile-when-it-moved test), the label switch, the open flag, the `VectorSymbol` display-list cache (negative results cached too) and an `unresolved_symbols()` counter. `Style()` is **final** in the base: null/open checks -> `AcceptContext` -> plan -> a **`StylePass`** (the rule layer's whole answer for one feature: `symbol_scale` already multiplied out, `draw_labels` after the rule's veto, `PriorityOr`/`SymbolScaleOr`), then the product's `StyleFeature`. The two engines became LOADERS: `GeoSymStyleEngine` and `S52StyleEngine` are ~110 and ~170 lines shorter and neither mentions `RuleEffect`. **What deliberately did NOT move**: the row table itself, because the two dispatch rules are not special cases of each other — GeoSym matches FACC+delineation and lets EVERY row whose ATTEXP condition holds contribute a pass, S-52 takes the FIRST matching row of one of five tables and executes its instruction list. Also extracted: S-52's `ResultBuilder` -> `fv::StyleResultBuilder` (it names only style.h's slots, never an S-52 instruction), and the ONE comparison rule rules.h documents is now callable (`RuleValueAsNumber`/`CompareRuleValues`/`RuleValuesEqual`) instead of S-52 carrying a private copy that could drift from the comment. `AcceptContext` exists for one reason and says so: GeoSym's 0.20 "make sure it is something viewable" cutoff runs BEFORE the rule plan in SanSymbol, and a base that compiled the plan first would move it. **Both goldens are UNMOVED** (Charleston `0xdabc9bc116c24d88`, DNC harbor `0x33b43456964b19da`) — the refactor is behaviour-preserving, which is the only acceptable outcome for an extraction session. 10 new **hermetic** gtests (`port/fvkit/test/vector_lookup_engine_test.cpp`): the engine under test is a ~40-line third product invented in the file — no VPF, no GeoSym, no ENC, no canvas, so a seam leak fails the build — pinning the error pair, the neutral state (zero predicate evaluations), the rule veto never reaching the product, StylePass arithmetic, the AcceptContext-before-the-plan ordering, the symbol cache's load-once/remember-failures contract, StyleResultBuilder's merge-and-flush, and the comparison rule. Plus 1 GeoSym test for the counter it gained. **489 total.** ASan+UBSan clean (only the two pre-existing knowns: the `TilePackReal` shared-output-path flake under `-j`, and `pyfvw_pytest` aborting at extension-import under an uninstrumented interpreter). **One stated behaviour change**: `S52StyleEngine::Open` used to drop the RuleSet and ViewingGroupSet with its Impl; they now survive a reopen, which is what GeoSym always did and what an application-owned rule set should do. |
| R3a | Perf (of R3, first slice): the retained `VectorScene` + per-band simplification + style epochs | 5 | P | Eighth session of the ACTIVE TRACK, 2026-07-28. **The plan's `TileDisplayList`, cut against a measurement rather than a guess.** New `port/include/fvkit/vector/scene.h` + `port/fvkit/vector/scene.cpp`: **`fv::VectorScene`** is a retained, already-styled, already-sorted snapshot — geometry flattened COLUMNAR (one `points_` array + `part_first_` offsets, replacing the `vector<vector<GeoPoint>>` that cost an allocation per part per feature), items in draw order, one `StyleResult` per pass. `VectorRenderer::Render` now ALWAYS goes through one (no second code path to diverge — E3c's lesson) and reuses it when `CanServe` says the viewport is still inside the built area and nothing the styles baked has moved. **Measured on the real 900x650 DNC harbour**: 133 ms/frame = query 38 + style 17 + draw 78; a retained pan is **76 ms (-43%)**, and with `SetSimplifyPixels(0.5)` — Douglas-Peucker at build time, 112,330 vertices -> 24,284 — **33 ms (-75%)**. `VectorRenderer` gained `query_ms()/style_ms()/draw_ms()`, which is how those numbers were got and what the demo status line reports. Simplification is **render-only and off by default** (identify walks back through the `FeatureRef`); at 0.5 px it moves 1.16% of pixels on the densest chart in the tree, visually re-checked against the exact render. `IStyleEngine` gained **`style_epoch()`**, implemented properly in `LookupTableStyleEngine` (RuleSet epoch + ViewingGroupSet epoch + an own counter, FNV-mixed) so both real products invalidate correctly; `SetColorAdjust`/`SetDrawLabels` learned to ignore a no-op set, and S-52's mariner/colour setters bump. Bound to pyfvw; **PythonView opts into a 0.25 margin**, so a drag-pan re-projects without re-querying VPF or re-running GeoSym. 26 hermetic gtests (`vector_scene_test.cpp` — a ~30-line source and style engine invented in the file, so a seam leak fails the build: DP behaviour incl. the ring-collapse guard, flatten offsets, draw order, every arm of the reuse predicate, and the epoch composition) + 2 real-data GeoSym tests (**a reused scene renders the harbour byte-identically to a rebuilt one**; simplification keeps the chart within 3% of its ink). **Both goldens UNMOVED**; 517 total. ASan clean, UBSan shows only the pre-existing VPF unaligned loads. Four decisions + one found-in-passing UB below. **NOT here (R3b)**: the symbol atlas and the columnar `FeatureBatch` — the 78 ms draw half, now the clear majority of a frame. |
| R3b | Perf (of R3, second slice): the rasterizer, cut where the profiler pointed — edge-table fill + clip fast paths | 4 | T | Ninth session of the ACTIVE TRACK, 2026-07-28. **R3a handed R3b a plan; the profiler said the plan was aimed at the wrong thing, and the measurement won.** R3a left "draw = 78 of 133 ms" with the symbol atlas and the columnar `FeatureBatch` named as the fix. Instrumenting the draw loop on the same real 900x650 DNC harbour split it for the first time: **clip 15.9 + fill 10.9 (after the fill fix below; ~38 before it) + symbols 9.1 + project 6.0 + lines 1.1**. Point symbols were **20%**, and **the two biggest costs were the two nobody had named** — so R3b is the fill and the clipper, and the atlas is deferred with a reason (below). Neither change is an approximation: **every pinned golden is byte-identical and none was re-pinned**, which is the only acceptable outcome for a pure optimization. (1) **`CpuCanvas::FillScanlines` is now an edge table.** It walked EVERY edge of every ring for EVERY scanline in the bounding box — O(scanlines x edges), so a DNC depth area of thousands of vertices over hundreds of scanlines cost millions of crossing tests to produce a few thousand spans. Edges are bucketed by the first scanline they can cross and retired after the last, which is exact because both endpoints are integers: `(a.y <= y+0.5) != (b.y <= y+0.5)` is precisely `min <= y < max`, a half-open span. **x is recomputed from the edge's own endpoints per scanline rather than stepped by a dx/dy increment** — an incremental x accumulates float error and would move pixels, which is the whole reason to say so out loud. The active set arrives in a different ORDER than the old ring-by-ring walk, but the crossings are then sorted and a sorted sequence of doubles does not remember how it arrived. New `BlendSpan` hoists BlendPixel's per-pixel bounds check and `Row()` out of the span loop. (2) **Both clippers get an all-inside fast path.** `ClipPolygon` copied the ring five times to run four Sutherland-Hodgman passes that are each the IDENTITY when every vertex is inside; `ClipPolyline` set up Cohen-Sutherland per segment. When nothing is outside, the polygon goes straight to the pixel conversion and the polyline provably emits exactly one run (each segment survives whole and joins the last end to end, so only the consecutive-duplicate-pixel suppression survives). Slow-path scratch is now reused across calls. **Result on the harbour: draw 73.2 -> 33.8 ms (-54%), a retained pan frame 73.5 -> 34.1, and simplified 33.3 -> 21.4.** The R3a headline number, 133 ms cold, is now 90; the fully warm simplified pan is **21 ms**. New split at 32.2 ms: fill 10.9, symbols 7.5, project 5.9, clip 5.2, lines 1.1. 5 gtests, all **equivalence tests against the algorithm each one replaced** — the pre-R3b naive scanline and the pre-R3b Sutherland-Hodgman are transcribed into the test files as oracles, so an optimization that is merely close fails rather than silently re-pinning a golden (F2's lesson, applied to perf instead of orientation). The fill oracle runs 9 shapes x 2 alphas: horizontal edges (dropped from the table entirely), rings hanging off the top and bottom (clamped into the drawn range), multiple rings, a hole, a self-intersecting star, a one-scanline sliver, and a ring with no crossings at all. The clip oracle covers boundary-exact vertices, points that round to the same pixel, and a sweep that nudges each vertex one unit past each edge in turn to prove the shortcut does NOT fire. **541 total.** ASan+UBSan clean (only the pre-existing VPF unaligned loads). **One regression caught in self-review, not by a test**: the span loop resolves a row pointer per scanline, where the old per-pixel BlendPixel would have bounds-checked its way out of a zero-width buffer — guarded. Two decisions below, including why the symbol atlas is now a deliberate NON-goal. **Also fixed in passing** (it was failing the suite on arrival, unrelated to this work): `GeoTiffEnumerate.AllQuadsRecognized` broke on two GeoTIFFs dropped into TestData the same morning (`choctb.tif`, `eglin1.tif`) — the **third** time a data drop has moved a hardcoded test inventory, so the Choctawhatchee sanity box is now a Florida-panhandle box that both new sheets fall inside, rather than a bay-sized one nudged again next time. |
| E4 | ENC into PythonView: `EncFrameEnumerator` + format registration + bindings + menu (2026-07-28, per Chris) | 5 | P | Tenth session of the ACTIVE TRACK. **The payoff session for E1/E2/E3: ENC has rendered a golden chart since E3a, and until now it could not be reached from the application.** Three things stood in the way and this closes all three. (1) **`port/Enc/fv_enc_format.{h,cpp}` — `fv::EncFrameEnumerator`**, one catalog row per CELL (`path` is the cell file itself, which `EncVectorSource::Open` already takes, so ENC needs no locator of VPF's `db\|library\|tile` kind). **Series = the S-57 usage band** — "US5CHSDC" is band 5, Harbour — because band is S-57's scale ladder and plays exactly the role DNC's library plays, which is the mapping `fv_enc_vector_source.h` already documented. `scale` is the band's NOMINAL denominator (Overview 3M → Berthing 4k) and **not** the cell's own CSCL: the authoritative figure needs a full cell parse, and enumeration has to stay a header scan — a real NOAA set is ~1000 cells, and the measured difference here is 3 ms for the whole tree via catalogues vs 41 ms for a single cell parsed. Bounds come from **every `CATALOG.031` at or below the root**, not just the root one: TestData holds four downloads whose catalogues stayed in their own `ENC_ROOT-N` shells, so reading only the root one would have covered a quarter of the tree; a cell in no catalogue is parsed for its own extent, and the two paths are cross-checked by a test. (2) **`RegisterEncFormat()` rather than a line in `RegisterBuiltinFormats()`** — see the decision below; `fv_enc` links `fv_fvkit`, so fvkit naming ENC would close a dependency cycle. The registry was built multi-slot and public for exactly this. `pyfvw.catalog.register_builtin_formats()` calls both, so **the layering detail does not leak into Python**. (3) **Bindings**: `EncVectorSource` (+ `cell_count`/`cell_path`/`staleness_warning`/`last_query_scamin_skipped`), `S52StyleEngine` (colour scheme, point/area style, `rules()`/`viewing_groups()` off the shared E3c core, `unhandled_cs`, the diagnostics counters) and **`S52MarinerSettings` by reference**, so `engine.mariner().safety_contour = 10` re-styles the chart — F1's ENC half is now reachable from an application for the first time. Plus `registered_format_keys()`. (4) **PythonView**: ENC joins the Vector Charts family, gets its own coverage colour and an `enc` scan probe, and the Map menu's stale disabled entry ("reader ported, styling next" — three sessions out of date) is gone. `_open_vector` is now the ONLY place that forks per product, which is the vector seam's whole claim; it opens the band's containing DIRECTORY rather than the single cell a row names, so panning across a band's cells needs no source swap. New `[enc] data_dir` settings key (chartsymbols.xml + the Appendix A CSVs), documented in `peregrine.ini.sample`. **Verified in the app**: the Charleston band renders at 1:12,000 as a real S-52 chart — buff land, marsh, the depth ramp, magenta cables and traffic lanes, buoys with their flare colours, wrecks — 2311 features in 77 ms, and the coverage overlay lists "enc - 4 in view". 7 gtests + 5 pytests. **548 total.** ASan+UBSan clean over all 139 ENC tests. **Three bugs found and fixed in this session, two of them mine and one pre-existing** — see the decision below on what they have in common. |
| E5 | ENC rendering defects found by comparing the app against a published chart (2026-07-28, per Chris) | 6 | P | Eleventh session of the ACTIVE TRACK, and **the first time the port's output was checked against an independent rendering of the same water** rather than against itself. Chris put PythonView's Charleston render beside the ArcGIS ENC viewer's and called three things: triangles littering the water, channel linework that looked wrong, and "significant scaling problems on a few of the symbols" — with the note that the marsh symbols looked right, which is what made it a symbol-by-symbol question instead of a global scale one. Diagnosed with a throwaway harness that dumped, per viewport: every area pattern with its computed pixel pitch, every line pattern, every stroke's resolved colour/width against the lookup row that produced it, and every symbol's declared box against the extent of its parsed display list. **Two real defects, one non-defect, and two findings left open.** (1) **META objects were drawn by default — 640 of the 1234 draws in a harbour viewport were metadata, not chart.** The triangles are `DQUALA21/B01/C01`, M_QUAL's zones of confidence, stamped on a 55 px grid; M_COVR and M_NSYS added magenta linework over the same water. `S52StyleEngine::SetShowMetaObjects(bool)`, default **false**. **It is deliberately NOT a display category**: M_QUAL is category OTHER and so are soundings and depth contours, so the category threshold that hides the quality overlay also hides half the chart — meta is a separate axis and an ECDIS gives it its own switch, which is now the Overlays menu item and the `[enc] show_meta_objects` key. (2) **61 of the 367 vector symbols are anchored off their own ink.** A symbol's pivot is the point that lands on the feature; for CTNARE51 the authored pivot is **1.80 symbol-widths right and 0.60 below** its glyph, so the caution mark drew ~65 px clear of the area it annotates (also CTYARE51, ENTRES51/61/71, INFARE51, RSRDEF51, TSSCRS51, RETRFL01/02, TIDCUR03, LIGHTS82...). **The delivered file says it twice** — for 47 of the 61 the independent BITMAP pivot carries the same out-of-range fraction of its own tile (CTNARE51: 1.79 vs 1.80), and a raster pivot outside a 29x29 tile cannot be a deliberate anchor — so this is a conversion artefact in OpenCPN's chartsymbols.xml, not our parse. Narrow documented deviation: a POINT symbol whose pivot lies more than a quarter of its size beyond its own ink is re-anchored on that ink's centre. The bound is generous on purpose, and the test asserts **both halves** — the 7 known-bad symbols now contain their anchor, and every symbol authored to stand on its pivot (LIGHTS11/12/13's flare rising from the light, BCNSTK02, NOTBRD11) still starts at y=0 and rises. Line-styles and patterns are untouched: there the placer positions the stamp and an authored offset is part of how the pattern tiles. (3) **The channel linework was NOT a defect** — the dump showed every stroke resolving correctly (CHGRD grey, CHMGD/TRFCD magenta at the authored widths); the magenta belongs to CTNARE/ACHARE/CBLARE/PIPARE, and most of what read as clutter was the meta linework in (1). Recording it because "looks wrong" was a reasonable read and the evidence is what settled it. (4) **The HPGL parser is vindicated**: 335 of 339 symbols parse to exactly their declared `<vector width/height>`, and the 4 that differ have declared boxes looser than their ink (DWRTPT51, DISMAR03, RSRDEF51, NMKRCD02) — no geometry is lost, `CI` circles included. **Charleston golden re-pinned after visual checks at three scales** (`0xdabc9bc116c24d88` → `0xbaccb187be81a803`). Also fixed: PythonView told the projection the display was 0.25 mm/px and the style engine 96 dpi — **the same physical property, set independently and disagreeing by 6%** — so `device_dpi` is now derived from `display.mm_per_pixel`. 2 gtests + the 3 existing tests taught the new default. **550 total.** **Two findings left open, both in the pending list**: the **raster symbol sheet** (679 of 1018 symbols are bitmap-only; in one harbour viewport 13 lateral buoys, beacons and daymarks draw a question mark where the reference draws the navaid — this is now the largest visible gap and it is a listed Q11 leftover; **DONE 2026-07-28, row E6**), and **symbol size does not honour device DPI while line widths do** (the renderer's `px_per_himetric` is a fixed 1/25.4 = exactly 100 dpi, bit-faithful for GeoSym by rule but arbitrary for S-52; harmless at ~100 dpi, a 2x mismatch on a retina pitch). |
| S1 | `fv::Settings` — the registry replacement (2026-07-28, per Chris) | 5 | P | Not a port step in the module sense: **the port had no preferences mechanism at all.** The registry was severed one module at a time with whatever was nearest — a ctor arg (geoid's data dir), an in-memory map (geo3's prefs), or an env var (`FVW_GEODATA_DIR`, `FVW_FIF_DIR`, `FVW_DATA_PATH`, `MSPCCS_DATA`) — and `FvConfigFileServer`, FalconView's registry-backed config COM server, is on the deferred-indefinitely list. Nothing persisted, and PythonView's Options dialog was in-memory only. New `port/include/fvkit/settings.h` + `port/fvkit/settings.cpp`: **`fv::Settings`**, a flat `string -> string` store read from a hand-edited **INI** file (`[section]` prefixes its keys, so `[vector] scene_margin` is `vector.scene_margin`; `#`/`;` comments; `"`/`'` quoting for paths with spaces or a literal `#`; keys and sections case-insensitive, values not). Chosen over JSON by Chris on one point: **standard JSON has no comments, and a preferences file nobody can annotate is one nobody will edit** — and an INI parser is one screen with no new dependency, where JSON needed a hand-written or vendored one (expat is XML-only). **Read-only by design — there is no `Save()`**: the file is authored by a human and the app never rewrites it, which is what preserves comments, ordering, and keys the running build does not know about. Search path (first existing wins, listable for an error message): `$FVW_SETTINGS` → `./peregrine.ini` → `$XDG_CONFIG_HOME`/`~/.config/peregrine/settings.ini` (+ `~/Library/Application Support/Peregrine/` on macOS, `%APPDATA%\Peregrine\` on Windows). **Three failure modes, each deliberate**: an absent key returns the CALLER's default silently (so defaults live at the point of use, not in a second table that drifts); a value of the wrong type — including `0.25px`, which must not read as `0.25` — returns the default and appends to `warnings()`, so a typo neither aborts startup nor is silently wrong; a malformed LINE fails the whole load with the file and line number, and leaves the previously-loaded values untouched (same all-or-nothing contract as the R2 rule-file parser). Unknown keys are kept, not rejected — that is how a file survives a downgrade. Bound as `pyfvw.Settings` (+ `pyfvw.default_settings_paths()`); **PythonView reads it at startup** for `vector.scene_margin`, `vector.simplify_pixels` (the two R3a knobs, previously unreachable — the margin was hardcoded and simplification was never called), `display.mm_per_pixel`, `geosym.data_dir`/`brightness`/`contrast` and `catalog.db`, plus a `--settings` flag; warnings print to stderr and a broken file still starts the app. Documented sample checked in at **`port/peregrine.ini.sample`**, with the measured effect of each knob next to it. 19 hermetic gtests + 5 pytests. **536 total.** ASan+UBSan clean. |
| E6 | ENC raster symbol sheet: `SymbolPixmap` at the vector seam + S-52 tiles (2026-07-28, per Chris) | 6 | P | Twelfth session of the ACTIVE TRACK, and **the largest visible gap E5 left open**. 679 of the S-52 library's 1018 symbols are defined RASTER-ONLY — a `<bitmap>` tile in the colour table's symbol sheet and no HPGL at all — so a vector-only path drew the question mark over exactly the objects a mariner steers by. In the Charleston harbour viewport that was **98 draws across 15 names**: the beacons (BCNSTK60/61, BCNTOW61), the buoys (BOYPIL60/61, BOYCAN63) and their topmarks (TOPSHP20/48/90, TOPMAR01). **The seam gained a second symbol form, not an ENC special case.** `fvkit/vector/style.h` now carries `SymbolPixmap{tile, pivot_x, pivot_y}` and `IStyleEngine::Pixmap(id)`, defaulting to nullptr — GeoSym and every synthetic test engine are untouched — and the renderer resolves an id ONCE into a `ResolvedSymbol{vec, pix}`, display list first, so a product that authors a symbol both ways keeps the form that scales without resampling. OSM sprite sheets land on this, not beside it. **Units are the deviation worth naming**: everything else at this seam is HIMETRIC, a tile is PIXELS, so the two scales travel separately and are never derived from one another — `2.0` must stay `2.0` and not `2.0/25.4*25.4`, because the nearest sampler decides the tile's first row on a boundary that lands exactly on a half-pixel at integer zooms (found by test, not by reasoning). `DrawPixmapSymbolAt` blits straight through at unit scale with no rotation — the sheet's own anti-aliased edges reach the canvas untouched, and that is every point symbol on a default chart — and otherwise inverse-maps NEAREST-NEIGHBOUR: these are 9-46 px hard-edged glyphs and interpolation smears the one-pixel stroke a buoy is drawn with. The anchor is snapped to a whole pixel FIRST in both paths, which is what makes the resampler REPRODUCE the blit at unit scale instead of shifting the glyph as a zoom crosses 1.0 (pinned: `ResamplerAtUnitScaleMatchesTheStraightBlit`). **`S52PresentationLibrary::SymbolBitmap`** cuts the tile from the sheet named by the active colour table's `<graphics-file>`, cached per (name, colour table) exactly as the display lists are — the three sheets ARE the day/dusk/night palettes, already coloured, so a scheme switch re-cuts rather than recolours. A missing sheet is NOT an Open() failure (the vector half stays usable) but is reported by `raster_sheet_error()`. **E5's pivot defect, again, and worse in this half of the file**: 292 of the 1083 bitmap pivots lie more than a quarter of the tile outside the tile, and **28 are written `-2147483648`** — ARPONE01's is INT_MIN in both axes, which is a missing value, not an anchor. Same narrow deviation as E5's, same bound deliberately: those fall back to the tile centre, and a symbol that legitimately hangs off its pivot (DAYTRI52, a daymark standing on its post at y=30 of a 33 px tile) keeps what it was authored with — the test asserts both halves. The SY instruction now reaches for the tile before the question mark, so a raster-only name is neither a placeholder nor an unresolved reference; **no feature in these cells draws a placeholder any more** (`RasterOnlySymbolsAreDrawnFromTheSheet` pins the zero). Charleston golden re-pinned after visual checks at three scales and at 2x symbol scale. **Also factored** (third consumer): the libpng READER moved out of tile_pack.cpp into `fvkit/tools/png_io.h`, hardened on the way (16-bit stripped, interlaced rejected rather than silently mis-decoded, `png_read_update_info` + a rowbytes check instead of assuming the transforms took). 11 gtests + 6 renderer gtests. **563 total.** ASan+UBSan clean. **Fixed in passing**: four ENC cells (US2EC02M, US3SC1CB, US4SC1BO, US4SC1CO — bands 2/3/4 over the same water) arrived in TestData mid-session and broke 8 tests that pinned totals over the whole directory. This is the **fourth** such break, so the repair is R3b's: exact counts moved onto ONE NAMED CELL (US5CHSDC: 600 features, 42 layers, its own box), the whole-root tests assert structure and lower bounds, and `UsageBandBecomesTheSeriesAndTheScale` now checks EVERY band present against the band table instead of asserting "band 5" — which the new data turns from a one-band claim into the real one. |
| F3 | ENC usage bands in PythonView: one band per source, opened at its own scale, stepped with PageUp/PageDown (2026-07-28, found by Chris) | 4 | P | **Two defects reported against the app, both about the same missing idea: an ENC series IS A SCALE BAND.** (1) **Every band drew every other band.** `_open_vector` opened the common ANCESTOR DIRECTORY of the catalog rows in a series, so that panning across a band needed no source swap — right goal, wrong mechanism, because the common ancestor of an exchange set is the exchange set. Choosing Harbour opened all 8 cells and a 1:12,000 chart drew a 1:1,000,000 general cell underneath itself (280 features queried in the harbour viewport where 194 belong). Invisible while TestData held band 5 alone; the four cells that arrived during E6 made it visible the same day. New **`EncVectorSource::OpenCells(cells, catalog_dir)`** takes the cell list instead of a path — `Open(path, ...)` now resolves its directory and delegates — and PythonView hands it exactly the rows the catalog filed under that series: General 1 cell, Coastal 1, Approach 2, Harbour 4. A band is a scale, not a place, and only naming the cells can say which one is wanted. (2) **A band opened zoomed out to its own full extent** (`_fit_scale_rect` over the source bounds), so picking General framed the whole cell — Bermuda to Maryland — instead of a chart. A vector series that DECLARES a scale now opens at it (the S-57 usage band's nominal 1:1,000,000 / 1:300,000 / 1:50,000 / 1:12,000, which is the scale the cell was compiled for and the only one its SCAMIN thinning is authored against); a DNC library declares none and keeps the fit. Reset (`zoom(None)`) goes to the same place. (3) **PageUp/PageDown now step vector series**, which is what Chris asked for and which the existing raster ladder already did: `step_scale` bailed on `mode == "vector"`, and the guard was never needed — `_scales_at` already ignores series with no scale, so a DNC library (a place) is never a candidate while ENC's four bands (scales) are a clean ladder. **One deliberate change to raster stepping came with it**: the ladder now gives the CURRENT PRODUCT first refusal and only crosses to another when its own product has no next step. Charleston is why — ENC, CADRG, the DOQs and DTED all cover that water, so a flat nearest-scale rule walks a mariner out of the chart he chose and into a topo sheet halfway up the band ladder. Verified end to end through the app: General -> Coastal -> Approach -> Harbour on PageUp and back on PageDown, each opening at its own scale with only its own cells, and PageDown past General correctly leaves ENC for the next coarser product. 2 gtests + 1 pytest (**565 total**); PythonView `--selftest` green. |
| O1 | OSM O1 (Q7): MBTiles container + MVT reader + `fv::OsmVectorSource` | 4 | T | Thirteenth session of the ACTIVE TRACK, 2026-08-04, and **the port's third vector product** — unblocked the moment the data arrived (`TestData/OSM/`, 2026-07-28; see the blocker list). Port-native like `port/Enc`, because FalconView has no vector-tile reader. **The point of the session is how little there was to write**: E4 said a new vector product needs an enumerator, a self-registration and one arm in `_open_vector`, and O1 needed even less than that — the seam, the rule layer, the retained scene, the pick index and the renderer were all already there, so this is a source and nothing else. `port/Osm/`: (1) **`fv_web_mercator.h`** — the ONLY place the port speaks Web Mercator, header-only over geo.h alone. Tile/lon-lat both ways, the TMS<->XYZ flip (self-inverse, and the classic MBTiles bug if you get it backwards), and **`ZoomForScale`** = plan §6's `z ~ log2(156543 / viewport m/px)`, **rounded to nearest rather than floored** because a pyramid level is generalized for a scale RANGE centred on its own. Vertices are converted to WGS-84 ONCE, on the way in, so OSM features enter the same equal-arc pipeline as DNC, ENC and every raster product — no second projection in the engine. (2) **`fv_mvt.{h,cpp}`** — one tile, decoded. protozero 1.8.2 + vtzero 1.2.0 do the protobuf wire format (BSD-2, FetchContent — see the dependency note); this file owns the gzip/zlib/raw sniffing, the per-vertex projection, the property-value variant -> text, and turning vtzero's exceptions into `fv::Status`, since nothing else in the port throws. Rings are classified by winding, so a POLYGON that is really a MULTIpolygon stays honest. (3) **`fv_mbtiles.{h,cpp}`** — the container, pure SQLite over the existing `fvkit/detail/sqlite.h` (which gained **`OpenReadOnly`**: a published pyramid is read-only, and `sqlite3_open` would have CREATED an empty database for a typo'd path). (4) **`fv_osm_vector_source.{h,cpp}`** — `IVectorSource` + `Describe()`. Reads TestData's real `us-south.mbtiles` (Tilemaker, OpenMapTiles schema, z0-14, **798,627 tiles, 3.9 GB**): the downtown-Atlanta z14 tile decodes to **12 layers, 7,389 features and 40,780 vertices**. 56 gtests (**621 total**), ASan+UBSan clean. **Three things a tile pyramid does that a DNC library and an ENC exchange set do not, and which is therefore what this row is really about**: scale CHOOSES A ZOOM instead of thinning features (the pyramid is pre-generalized, so there is no SCAMIN analogue to honour); the unit of I/O is a tile RANGE, so a scale-less bulk query would be 594,419 tiles and is instead capped by a tile budget that steps the level coarser (an explicit `SetZoomOverride` is honoured exactly — a caller that names a zoom has said what it wants); and a tile carries a BUFFER of its neighbours' geometry, so a feature lying wholly in another tile's box is dropped here. Deviations, findings and the dependency decision are in Decisions below. **NOT here (O2/O3)**: the MapLibre style subset over `LookupTableStyleEngine`, the frame enumerator + format registration + pyfvw bindings + PythonView mode, and per-tile clipping of straddling features. |
| K1 | `KeyEvent` at the overlay SPI: VK codes + text + modifiers, and a tk adapter (2026-08-04, found by Chris) | 6 | P | **The SPI could not express a key press, so no UI toolkit had a correct value to send.** `Overlay::OnKeyDown(int key)` took a bare int with NO documented numbering and no modifiers — while `MouseEvent` one struct above it already carried `shift`/`ctrl`. Chris hit it wiring a route overlay to tkinter: there is no way to get from a Tk event to that int. **Two independent defects, both measured rather than reasoned about.** (1) **`event.keycode` is unusable on Aqua Tk** — it packs the Mac virtual keycode and the character into one int, so Left arrives as `2063660802`, Escape as `889192475` and space as `822083616`; the portable fields are `keysym` (a stable NAME everywhere), `char` and `state`. (2) **A generic `<Key>` binding never fired for the keys that mattered**: within one bind tag Tk fires only the BEST-MATCHING pattern, and PythonView bound `<Left>`/`<Right>`/`<Up>`/`<Down>`/`<Prior>`/`<Next>`/`<Escape>` specifically — so an overlay hooked to `<Key>` was structurally deaf to every navigation key no matter how good its event was. **The seam**: `fvkit/overlay/overlay.h` gains `KeyEvent{key, text, shift, ctrl, alt, meta}` and `namespace Key` whose values **ARE Win32 virtual-key codes, never renumbered** — the same rule `fv_map_enums.h` states for the COM ABI enums and for the same reason: a Windows shell passes `WM_KEYDOWN`'s wParam straight through, letters and digits are their ASCII UPPERCASE code points (`e.key == 'A'` needs no constant), and kReturn/kTab/kSpace coincide with their ASCII controls. **`key` and `text` answer different questions and both are needed** (the Qt split): `key` is the layout-independent physical intent a shortcut compares against, `text` the character the layout produced — which is why `Shift-A` comes back as key `0x41` + text `'A'` and plain `a` as key `0x41` + text `'a'`. `OverlayManager::RouteKeyDown` and the pybind trampoline follow; `pyfvw.overlay.KeyEvent` + `pyfvw.overlay.key.*` are bound. **`port/apps/tk_keys.py`** is the adapter, a module and not four lines inline for one reason: **the modifier bits differ per platform and the difference is a trap** — Aqua is Shift 0x1 / Control 0x4 / **Option 0x20000** / **Command 0x8**, while X11 has **Alt at 0x8**, so a single hardcoded mask turns every macOS Cmd shortcut into an Alt shortcut. It takes anything with `.keysym`/`.char`/`.state`, so the mapping is tested with no display, no event loop and no window. Punctuation is deliberately left `key = 0`: `VK_OEM_*` is keyboard-layout-specific and would be a lie on a non-US layout, so `text` carries it. The shell-side pattern that fixes defect (2) is **ONE `<Key>` handler with one readable precedence order — overlays first, then the app** — replacing the dozen specific bindings; an editing overlay can then take Delete or the arrows before the map pans out from under it. That rewrite is **in Chris's working tree, not this commit**: it is interleaved in `PythonView.py` with his in-progress route-overlay experiment (`port/apps/route.py`), and lands with it. The committed `PythonView.py` never called `route_key_down`, so it is unaffected by the signature change. 4 gtests + 2 pytests (**625 total**), ASan+UBSan clean. **Verified in the real application** by driving synthetic key presses through the actual binding: `<Left>` reaches the overlay as 0x25 and then pans, `<Prior>` as 0x21, `<Control-z>` as key 0x5A + text 'z' + ctrl, and an overlay returning True stops the app seeing the key at all. **API BREAK, deliberate and stated**: `route_key_down(int)` becomes `route_key_down(KeyEvent)`; the only caller was Chris's uncommitted experiment, updated with it. |
| O2 | OSM O2: MapLibre style JSON as a LOADER over `LookupTableStyleEngine` (`fv::OsmStyleEngine`) | 4 | T | Fourteenth session of the ACTIVE TRACK, 2026-08-08, and **the fourth product to ride the vector seam without changing it**. `port/Osm/fv_osm_style.{h,cpp}` + `port/Osm/styles/peregrine-osm.json`. The standing rule ("style engines are LOADERS over `LookupTableStyleEngine` — do not write a fourth engine") held exactly: GeoSym reads `fullsym.txt`, S-52 reads `chartsymbols.xml`, this reads `style.json`, and all four end up as table rows behind the same rule layer feeding the same renderer. Nothing in `fvkit` changed. **The three mappings §5.2 predicted, all of them real**: MapLibre `filter` is a third front-end for the predicate AST; `minzoom`/`maxzoom` become a `ScaleBand`; layer order becomes `StyleResult::priority`. **(1) ONE zoom<->scale relation, in both directions.** `fv_web_mercator.h` gains `ZoomForScaleExact` (fractional, unclamped) and its exact inverse `ScaleForZoomExact`; O1's `ZoomForScale` is refactored to be the rounded, clamped form of the first, so a style layer's minzoom and the tile level the source read cannot drift apart. Pinned by a round-trip over z0..20 x three latitudes x two pixel pitches, and by a test asserting the source's clamped choice IS the rounded exact one. **Latitude is part of the relation and there is no way around it** (a pyramid holds scale constant per PIXEL: z12 is 1:270k at the equator, 1:190k off Charleston), so a style engine — handed a scale and no geography — has to be TOLD one: `SetReferenceLatitude` + `SetDisplayMmPerPixel`, which must match what the source got. **(2) The supported subset is DECLARED AND ENFORCED**, the ENC-reader philosophy applied to a style sheet: layer types background/fill/line/symbol/circle; LEGACY filters only (`==`, `!=`, `<`, `<=`, `>`, `>=`, `in`, `!in`, `has`, `!has`, `all`, `any`, `none`, over a tag key or `$type`); constants and `{base, stops}` zoom functions; `{token}` text fields. Everything else — `["match",…]`, `["interpolate",…]`, `["get",…]`, data-driven `{property, stops}` functions, sprite-backed `fill-pattern`/`line-pattern`, `hillshade`/`heatmap`/`fill-extrusion` — is a **load-time failure naming the layer id and the property**, never a silent skip. The load is all-or-nothing: a rejected style leaves the previous one intact (same rule as `RuleSet::LoadText` and `fv::Settings`). **(3) Draw order is style-layer order, not feature order.** OpenMapTiles road rendering is casing/fill PAIRS — a wide dark line under a narrow bright one, both from the SAME feature — so per-feature drawing would put a road's own casing over its neighbour's fill and shred every junction. `priority` = the style layer's index, and `VectorScene` already stable-sorts by priority ACROSS features, so the painter's algorithm the style was authored for comes out for free. Asserted on one feature that matches both layers of a casing pair. **Reference style**: `styles/peregrine-osm.json`, 28 layers written here against the OpenMapTiles schema (which is what `us-south.mbtiles` was cut to) and deliberately inside the subset. Vendoring `osm-bright-gl-style` is still open and is listed as a gap — it needs a network fetch and a NOTICE entry, and its value is exercising the subset check against a style nobody here authored. **Circles are generated symbols**: a `circle` layer emits `symbol_id = "circle:<r_himetric>:<rrggbbaa>"` and the base class's `LoadSymbol` cache builds the ellipse display list once per distinct radius+colour — the designed hook, used as designed. 23 gtests (**648 total**), ASan+UBSan clean, styled 100+ real downtown-Atlanta features off the delivered pyramid with zero unresolved symbols. **NOT here (O3)**: the enumerator, format registration, pyfvw bindings, the PythonView arm, per-tile clipping, overzoom, the golden viewport. **Gaps this opened, all in §2b**: no sprite sheet (so `icon-image` is counted, not drawn), no label collision/de-dup, colour stops step rather than interpolate. |
| O3 | OSM into PythonView: `OsmFrameEnumerator` + format registration + pyfvw bindings + the app arm, plus per-tile clipping, overzoom and the Atlanta golden | 4 | T | Fifteenth session of the ACTIVE TRACK, 2026-08-08, and **the payoff session for O1/O2: OSM is now a map you can open, pan and click**. The E4 shape held for the third time — an enumerator, a self-registration, bindings, one arm in `_open_vector` — and the three OSM-specific pieces are what this row is about. **(1) The catalog row for a pyramid.** `port/Osm/fv_osm_format.{h,cpp}`: one .mbtiles file is one frame AND one series, `series_key` is the file STEM (a handle, not the tileset's prose `name`), bounds are the DERIVED coverage (O1: a declared `bounds` cannot be trusted), and **`scale` is 0 on purpose** — a pyramid is a scale RANGE, not a compilation scale, so it opens FITTED like a DNC library and never appears as a rung on the PageUp/PageDown ladder, where the zoom keys already walk it continuously. Registration lives in `fv_osm` for `fv_enc_format.h`'s reason (fv_osm links fv_fvkit, so fvkit naming it would close a cycle); `register_builtin_formats()` calls it, so Python still sees one call. **(2) Per-tile clipping** — O1's one documented geometry deviation, closed. A feature straddling a seam is in BOTH tiles' buffers and was emitted whole by both, drawing the same ink twice; line geometry is now Liang-Barsky clipped (into SEVERAL runs when it leaves and re-enters, since joining them would draw a chord) and rings Sutherland-Hodgman clipped and re-closed, both against the tile's own box, so the halves meet on the seam. Exact here and nowhere near exact in general: a tile box is an axis-aligned lon/lat rectangle by construction and never crosses the antimeridian. Two consequences worth having found: bounds must be RECOMPUTED from the clipped geometry (a pick index built from the old box hits on empty map), and the query-area test must be RE-RUN afterwards (the whole feature met the viewport; this tile's share may not). `SetClipToTile(false)` restores whole geometry — which is what **O4's graph builder** will want, since a routable edge cut at a tile seam is two edges. **(3) Overzoom is the source clamped and the style NOT.** Past the pyramid's maxzoom the read level stops and the styling zoom keeps going, so z14 geometry draws under z15+ rules; clamping both would freeze the map at z14 widths while it kept zooming, and `last_query_overzoom()` reports the gap (only past the BOTTOM of the pyramid — the fractional gap from nearest-level rounding at any other level is ordinary and reporting it would make the number worthless). **The app**: OSM is a Vector Charts family member, coverage colour and all; `_open_vector` grows one arm; the canvas is cleared with the style's own `background` layer (a GL background is not a feature and cannot be a StyleResult); the status bar shows `OSM/us-south z14+4.0`; Options gains a style-sheet picker and `[osm] style` a settings key. **The one piece of real wiring**: the style engine's reference latitude is set from the viewport, but QUANTIZED to half a degree — setting it per frame bumps the style epoch and the R3a retained scene would never be reused, and half a degree is worth ~0.01 of a zoom level here. **Golden**: `OsmRender.AtlantaViewport`, 512x512 at 1:25,000, pinned `0x3f49f779d29c7887` after looking at the PNG (the downtown connector, Centennial Park, the rail corridor, casings under fills). It is set by PHYSICAL SCALE rather than V5b's resolution rule, deliberately: `MapProjection::Scale()` reports 0 in resolution mode and a scale of 0 makes both halves of OSM fall back to a default, so a resolution-pinned golden would have tested nothing about the relation the module is built on. 11 gtests + 5 pytests (**659 total**). **NOT here**: the sprite sheet (so no icons), halo text, label collision — all in §2b. |
| R3c | Perf, third slice: **the default build type**, the DNC parsed-feature cache, and the geographic pattern anchor | 4 | T | Sixteenth session of the ACTIVE TRACK, 2026-08-08. **The plan on this line was wrong for the third time running, and the profiler said so before a line of it was written** (R3b's lesson, applied to R3b's own successor). R3c was booked as "the product-neutral pre-parsed source cache (ENC's SENC, generalized) + the columnar `FeatureBatch`". Measuring first killed both and found something bigger. **(1) THE FINDING: every perf number in this ledger up to R3b was measured at -O0.** The root `CMakeLists.txt` never set a default `CMAKE_BUILD_TYPE`, and a single-config generator with an empty one passes NO optimizer flag at all — so `cmake -B build`, the command at the top of the ledger, built the port unoptimized. The same DNC harbour frame at -O2: **cold 90 -> 49 ms, a retained pan 21 -> 3.4 ms**, which is larger than the wins R3a and R3b each spent a session on. The build now defaults to **RelWithDebInfo** when nothing is named (multi-config generators untouched, any explicit `-DCMAKE_BUILD_TYPE=` wins). **It also cost a real bug to turn on**: `SymColors.BrightnessAdjuster` SIGTRAPs at -O3, which is R3a's found-in-passing UB (`CSymColorAdjuster`'s ctor reads `m_nBrightness`/`m_nContrast` before assigning them) finally being collected — benign on zeroed memory, not benign to an optimizer. Fixed in both copies of the shared header by seeding the members from the ARGUMENTS the line plainly meant. **(2) The columnar `FeatureBatch` is a measured non-goal.** It would remove the per-feature `vector<vector<GeoPoint>>` and string copies a `std::vector<VectorFeature>` costs; copying an entire query result — an upper bound on everything it could save — is **0.5 ms of DNC's 9.2 ms query and 3.3 ms of OSM's 10 ms**. It is not where the time is, and it would change the seam every product and every test engine implements. **(3) The pre-parsed source cache already existed in two of the three products** — ENC parses its cells at `Open` (query 1-2 ms, flat) and OSM keeps an LRU of decoded tiles (cold 57 ms, warm 10) — so there was no generalization to write. **DNC was the one without it, and DNC is the product the R3a/R3b profile was taken on.** `VpfVectorSource::Query` re-opened every feature table and rebuilt every feature on EVERY call, then threw away what missed the box: proof is that a query returning **5** features cost 9.55 ms and one returning 5,037 cost 9.43 — the cost never depended on the answer. The library is now parsed once (`ScanAll`, byte-for-byte the old Query with its two filters lifted out, so scan ORDER and therefore `max_features` semantics are unchanged) and every later query is a box test over memory: **9.4 -> 0.3 ms, and a tiny viewport is 0.02 ms**. `SetFeatureCacheEnabled(false)` restores the scan; the setting survives a re-`Open` and the cached features do not. Held memory is ~1.2x the library on disk (3.1 MB -> 3.7 MB for the harbour), deliberately uncapped — a source holds one library and a cap only adds a re-scan cliff. **The frame this buys**: a pan out of the retained scene's area went **19.4 -> 7.6 ms of query+style**, and styling is now the larger half of it. **(4) The geographic pattern anchor**, deferred from R3a and collected here per Chris: `PlaceOverArea` hung its stamp grid on the CANVAS origin, so ENC/DNC area patterns crawled inside their own regions while the map panned. The lattice now hangs on an anchor the renderer computes as the pixel position of lat/lon 0,0 — written out from the projection's linear relation rather than through `GeoToSurface`, whose longitude unwrap would make the lattice jump when a pan crossed 90 degrees from the centre. Adjacent areas still share one grid; a pan now slides ring and pattern together. **One golden moved**, the only one with AP fills: `S52Render.CharlestonHarborViewport` `0x5bfb57105171e60e -> 0x48a0cf127f6a2584`, re-pinned after rendering the before and after side by side and looking at both — the marsh tufts sit in different places along the left edge and the bottom-left creek, and land, the depth ramp, buoys, lights and the magenta linework are pixel-identical. **Verified end to end afterwards, per Chris, because a synthetic ring is not the marsh**: rendering the real Charleston viewport twice, 37 px apart, and marking every pixel that did not move with the map draws the defect — with the canvas lattice the grass tufts light up as red glyphs (0.63% of the frame), with the geographic anchor they do not (0.18%, and what is left is one-pixel line noise from the pan not being exactly 37 px in floating point). `S52Render.PanMovesEveryMarkByTheSameAmountIncludingAreaPatterns` pins that with both numbers written down and a `s52_pan_diff.png` side channel. **The first attempt at that test measured the wrong ink** — a bright-green mask that turned out not to be the tuft symbol at all, and which therefore reported "nothing moved" for the BROKEN build; the check that caught it was diffing the two renders directly and looking at which pixels the anchor change actually touches (4,616, every one of them on a tuft). 9 gtests (5 VPF cache, all **equivalence tests against the full scan they replaced** — same features, same order, same attributes, across four boxes, three `max_features` limits, a pre-filled `out`, a toggle and a re-open; 3 `AreaPlacer`, which state the pan property both ways round and prove the default arguments still give the old canvas lattice). **668 total**, green at -O0, at -O2 and under ASan+UBSan. **Found in passing**: `pyfvw_pytest` cannot run in `build-san` at all — Python is not instrumented and `dlopen`s an instrumented `.so` — logged in section 2d rather than papered over. |
| T1 | Labels along a path: rotated text on `ICanvas`, `PlaceTextAlongPath`, ground-scaled label sizing, OSM `symbol-placement` | 3 | T | Seventeenth session of the ACTIVE TRACK, 2026-08-08, per Chris ("drawing labels along features such as roads... the labels are not scaled or placed in geographic coordinates"). **Nothing was degraded; the primitive did not exist.** `LabelStyle` was `{text, TextStyle, dx, dy}` and the renderer stamped it horizontally at `proj_part.front()` — the FIRST VERTEX of every part, which on an MVT road is a tile edge — so a road name sat off at the end of its road, level, and repeated once per part per tile. **(1) `ICanvas::DrawRotatedTextString`**, sub-pixel origin + a CCW-on-screen angle (`e_u = (cos a, -sin a)`, matching `PlacedSymbol::rotation_deg`), **NOT pure**: pyfvw's Python-side `ICanvas` subclasses would otherwise stop compiling, and the base falls back to upright text rather than dropping the string. `CpuCanvas` rasterizes each glyph UPRIGHT through stb and resamples it through the inverse rotation — stb has no outline transform, and rasterizing a rotated outline by hand would mean a second rasterizer whose antialiasing did not match the first; the cost is one bilinear filter of softness, invisible at label sizes. **A zero angle takes the upright path EXACTLY** (pinned by a memcmp test), so every existing golden with text in it is untouched. **(2) `PlaceTextAlongPath`**, the third along-path primitive beside `PlaceAlongPath` and `PlaceOverArea` and pure geometry like both — the caller measures the text (the canvas owns the font) and passes per-glyph advances in. Four decisions worth not re-deriving: a run that does not FIT is dropped whole (half a road name is worse than none); the angle of a glyph is the CHORD across its own advance, not the tangent at its origin, which is what keeps glyphs touching on the outside of a bend; reading direction is decided ONCE from the path chord so a wiggle cannot flip a word, with the tie (a due north-south road) reading UPWARD per convention; and a run that turns harder than `max_angle_deg` between two glyphs is rejected rather than straightened. Advances are taken as DIFFERENCES OF PREFIX WIDTHS, because `GetTextExtent` returns whole pixels and summing rounded characters drifts half a pixel per glyph. **(3) Label sizing that scales with the map** — Chris's other half. `LabelSizeUnit::kMeters` + `ground_size_m` lets an engine state a GROUND size, converted per frame from `DegPerPixelLat`; and because every real product authors size in PIXELS (GeoSym in points, a GL style in px), `VectorRenderer::SetLabelReferenceScale(denom)` reinterprets an authored pixel size as the size AT that scale, so text is `size * ref / current`. **Both default OFF** (`kPixels`, ref 0) for the golden rule: turning it on changes every frame that has a label in it. Clamped to `kMinLabelPx`/`kMaxLabelPx`, and below the floor the label is dropped, not drawn as mush. **(4) The OSM loader** learns `symbol-placement` (`line`/`line-center`, an unknown one is a load-time REJECTION per O2's declared-subset rule, not a silent fallback), `symbol-spacing`, `text-max-angle` and `text-offset` — whose `[x, y]` is in EMs with **+y DOWN**, while the placer's offset is pixels positive to the LEFT of travel, hence the negation and the multiply by the text size. Only the perpendicular component survives; an along-axis offset has no meaning once a run is centred on its geometry. `peregrine-osm.json`'s `road-name` layer now carries all four. **(5) The app**: `[vector] label_reference_scale` settings key, an Overlays checkbutton ("Labels Scale With The Map") that pins the CURRENT scale as the reference so the text does not jump when it is ticked, and `set_label_reference_scale` on the pyfvw renderer. **Verified visually, not only by hash**: `OsmRender.RoadNamesRunAlongTheirRoads` writes `osm_atlanta_road_names.png` (512x512, 1:12,000) — Peachtree and the numbered streets read up their own columns, the MARTA West Line runs along its rail corridor, the Downtown Connector runs up the diagonal. **NO HASH on it**, deliberately: label glyphs come from the host font, which is why the Atlanta golden keeps labels off. It asserts structure instead — the label switch ADDS draws, the added count is not implausibly large, and the new ink is spread across the frame rather than piled in one corner. 21 gtests (9 `TextPlacer`, all directional — an angle, a side, an order — because a hash cannot see an angle; 3 canvas incl. the zero-angle identity and the non-overriding-backend fallback; 4 renderer; 2 OSM style; 1 OSM render + the existing goldens unmoved). **689 total**, ASan+UBSan clean. **NOT here, and now more visible than before** (§2b): no halo/outlined text, so a name over a dark fill is still hard to read; and **no collision or de-duplication** — `symbol-spacing` now repeats a name along a long road, which is the right behaviour and also makes the missing collision index easier to see. |
| R2 | Rule layer: `fvkit/vector/rules.h` + GeoSym retrofit (scale/group/category thinning) | 3 | P | Third session of the ACTIVE TRACK, 2026-07-26. **The cross-product MIDDLE of the vector seam**, per plan §5.2. New `port/include/fvkit/vector/rules.h` + `port/fvkit/vector/rules.cpp`: a `Predicate` AST (exists/missing, the six comparisons, in/not-in, and/or/not) with **one documented comparison rule** — numeric when BOTH sides parse *whole*, byte-wise otherwise, and a missing attribute makes every comparison false including `not in`; `ScaleBand` on the map-scale DENOMINATOR (0 = unbounded, and a scale of 0 matches everything so a bulk render never loses features to thinning it did not ask for); `ViewingGroupSet` (numbered groups + the IMO display-category threshold, `epoch()` bumping on real changes); `Rule`/`RuleSet` with a **rule-file syntax shared by all three products** (`hide key=BE010 scale=..50000`, `set key=DA010 priority=3 labels=off`, `hide key=BH140 where hdp exists and hdp < 3`) parsed by a recursive-descent predicate parser, all-or-nothing with the line number in the error. **`ResolvedPlan` is the point of the whole header**: scale + group filtering happen once per (scale, epochs) at compile time, then a `RuleDecision` is memoized per `{layer, style_key}`, so a key whose rules carry no predicate costs one hash lookup and **zero** predicate evaluations — measured on the real harbor render, not asserted. **GeoSym retrofit**: `fullsym.txt`'s `vgroup`/`txtgroup`/`radar`/`dispcat` columns are finally parsed (read non-fatally *after* the required chain, so a short row can't fail the table) and wired up — a viewing group toggles the rows that belong to it, a **text** group drops those rows' LABELS only (Q6c item 3, the dense-label problem), and `dispcat` is S-52's BASE/STANDARD/OTHER under another name, which is exactly why the category threshold lives in fvkit and not in the GeoSym engine. Bound to pyfvw (`RuleSet`, `ViewingGroupSet`, `engine.rules()`/`viewing_groups()` as live references, `DISPLAY_BASE/STANDARD/OTHER`). 26 hermetic rules gtests (no VPF, no GeoSym, no canvas — a seam leak fails the build) + 9 GeoSym gtests over real rows + 2 pytests. **376 total.** ASan+UBSan clean (only the pre-existing VPF unaligned loads). **NOT bit-faithful, and deliberately opt-in**: with an empty RuleSet and a default ViewingGroupSet nothing changes, so the harbor golden hash is UNMOVED — see the decisions below, including why the `LookupTableStyleEngine` extraction did NOT happen here. |
| E2 | ENC E2 (Q10): S-52 Presentation Library parse + S-57 Appendix A catalogue | 4 | T | Fourth session of the ACTIVE TRACK, 2026-07-27, and **the second real style table** §5.1 was waiting for. Port-native, `port/Enc/`, no FalconView source involved (there is none). **`fv_s52_preslib.{h,cpp}`** reads OpenCPN's `chartsymbols.xml` (2.2 MB, `TestData/enc/`, GPL-3.0 *as data*) through **expat 2.8.2 from `port/third_party`** — its first consumer, so the 2026-07-25 fetch is no longer unverified-by-a-real-user. Loads all **5 colour tables** (day/dusk/night is real, not a stub), all **3057 lookups across all five S-52 lookup tables** (paper/simplified points AND plain/symbolized areas — which pair is in force is a mariner setting made at style time, so all five load), **1018 symbols** (from 1093 elements, see below), 59 line-styles, 30 patterns. Lookup rows carry the parsed `S52Instruction` list (`SY`/`LS`/`LC`/`AC`/`AP`/`TX`/`TE`/`CS`), split on `;` and `,` **outside single quotes** so a TE format string keeps its own commas. **`ParseS52Hpgl`** flattens the symbol library's HPGL to the seam's `VectorSymbol`: S-52 authors in 0.01 mm, which IS HIMETRIC, so geometry passes through unscaled — only the pivot-to-origin shift and the **y flip** (PresLib y is DOWN) happen, and `SWn` becomes n×32 HIMETRIC (S-52's 0.32 mm pen unit). Verified visually by rasterising: the anchor stands upright with its flukes down, the conical buoy on its base. **`fv_s57_catalog.{h,cpp}`** closes E1's stated gap — `OBJL 42 → DEPARE`, `ATTL 87 → DRVAL1`, plus `s57expectedinput.csv`'s 1467 enumerated values, which is what gives ENC a real `Describe()` in the R1 sense, and the `Class` column that E1 needed for the DSSI meta/geo/collection split. 44 gtests (10 hermetic HPGL + 3 instruction + 4 CSV + 17 PresLib + 10 catalogue); ASan+UBSan clean over all 87 ENC tests. **420 total.** **Three data facts the corpus sweeps found, all now pinned** — see Decisions. **NOT here (E3/Q11)**: `S52StyleEngine`, the CS-procedure registry, the along-path placer, and the `LookupTableStyleEngine` extraction, which now has both tables in hand. |
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
| protozero | — (new) | **1.8.2** | ✅ **DONE** | New integration for OSM O1, header-only. Zero-copy protobuf reader; the MVT wire format's varints, zigzag and length-delimited fields. |
| vtzero | — (new) | **1.2.0** | ✅ **DONE** | New integration for OSM O1, header-only, over protozero. The MVT 2.1 schema itself — geometry command integers, ring winding, the property-value variant. The plan called for VENDORING both into `port/vendor/` (the stb_truetype pattern); FetchContent is the same decision with the 2026-07-25 mechanism. Both build with `SOURCE_SUBDIR` pointed at a directory that has no CMakeLists, so MakeAvailable populates without adding their tests/examples/doc targets. |
| nlohmann/json | — (new) | **3.12.0** | ✅ **DONE** | New integration for OSM O1. The port had NO JSON reader: preferences are INI by decision (`fv::Settings` — a file a human annotates) and expat is XML-only, so MBTiles' `json` metadata value is the first place JSON is unavoidable; O2's MapLibre style subset is the second. Fetched as the **`include.zip` release asset** (300 KB of headers) rather than the source tarball, whose test corpus dwarfs this port. |
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
| ~~R2~~ | ~~`fvkit/vector/rules.h` (predicate AST + ScaleBand + ViewingGroup + memoized `ResolvedPlan`); retrofit GeoSym~~ **DONE 2026-07-26 (row R2 above)** | Landed minus the `LookupTableStyleEngine` extraction, which **moves to Q10** — see the decision below: the second real table (chartsymbols.xml) is still not in TestData, so extracting now would still be an abstraction over GeoSym alone, which §5.1 explicitly says not to do. Q6c item 3 is solved. (The gate lifted the very next day — the PresLib arrived 2026-07-26, see the blocker list; the extraction stays in Q10 where the table now is.) |
| ~~Q10~~ | ~~ENC E2: PresLib parse + the S-57 Appendix A catalogue~~ **DONE 2026-07-27 (row E2 above)**: `port/Enc/fv_s52_preslib.{h,cpp}` + `fv_s57_catalog.{h,cpp}`, 44 gtests. The second real style table now exists in code. |
| ~~Q11~~ E3a | ~~ENC E3: `S52StyleEngine` + CS registry + along-path placer + the `LookupTableStyleEngine` extraction~~ **SLICED 2026-07-27 per Chris; E3a DONE (row E3a above)** | E3a landed the source adapter (which E1 had left for "E3+" and Q11's line never named), the style engine, the CS registry with 5 procedures, and the golden chart. ~~**E3b** = the shared along-path placer (S-52 `LC`/`AP` + GeoSym SAMI = Q6c item 2, one placer for both) and the remaining CS procedures~~ **DONE 2026-07-27 (row E3b above)**: `PlaceAlongPath`/`PlaceOverArea` in fvkit serve GeoSym SAMI, S-52 LC and S-52 AP, and all seven remaining procedures landed — `unhandled_cs()` over the Charleston cells is empty. **Q6c item 2 is closed with it.** ~~**E3c** = the `LookupTableStyleEngine` extraction~~ **DONE 2026-07-27 (row E3c above)**: `fvkit/vector/lookup_engine.h` holds the rule layer + label switch + symbol cache + unresolved counter + `StyleResultBuilder`, GeoSym and S-52 are loaders over it, both goldens unmoved. **Q11 is closed.** Still open from this line: the raster symbol sheet (rastersymbols-day.png is in TestData) for the 17 raster-only symbol names. |
| ~~R3~~ R3a | ~~Perf: retained `TileDisplayList` cache, symbol atlas, columnar `FeatureBatch`, per-band simplification~~ **SLICED 2026-07-28; R3a DONE (row R3a above)** | R3a landed the retained scene (columnar geometry, pre-sorted, epoch-invalidated) and per-band simplification: a DNC pan went 133 -> 76 ms retained, -> 33 ms simplified. ~~**R3b** = the remaining half: the **symbol atlas** and the **columnar `FeatureBatch`**~~ **R3b DONE 2026-07-28 (row R3b above) — but NOT as written.** Splitting the 78 ms draw for the first time showed the plan was aimed at the wrong costs: point symbols were 20%, while **fill and CLIPPING** (unnamed by anyone) were the top two. R3b did those instead — an edge-table fill and all-inside clip fast paths, both byte-identical, **draw 73 -> 34 ms** — and the **symbol atlas is now a deliberate non-goal** because it cannot be exact (see the two decisions below). ~~**R3c** = the pre-parsed source cache + the columnar `FeatureBatch`~~ **R3c DONE 2026-08-08 (row R3c above) — and NOT as written either, for the third time on this line.** Two of the three products already had the cache; the `FeatureBatch`'s entire ceiling measured 0.5 ms; and the whole profile this line argues over turned out to have been taken at **-O0**, because the build set no default `CMAKE_BUILD_TYPE`. R3c set the default, gave DNC the cache the other two products had (query 9.4 -> 0.3 ms) and landed the geographic pattern anchor. |
| ~~E4~~ | ~~ENC into PythonView~~ **DONE 2026-07-28 (row E4 above)**, added per Chris after R3b: the enumerator + `RegisterEncFormat()` + pyfvw bindings + the Map menu. ENC had rendered a golden chart since E3a and was unreachable from the app; it is now a Vector Charts family beside DNC, and `_open_vector` is the only place the two products differ. | Left for later: the Options-dialog mariner panel (safety/shallow/deep contour, safety depth, two-shade — all bound, none exposed in the UI yet) and the ENC data-dir field beside the GeoSym one. |
| ~~E5~~ | ~~ENC rendering defects vs a published chart~~ **DONE 2026-07-28 (row E5 above)**, added per Chris after E4: meta objects off by default, 61 off-glyph symbol pivots re-anchored, golden re-pinned. | Both follow-ups from this line are now placed: the **raster symbol sheet** is **DONE 2026-07-28 (row E6)**; **symbol size vs device DPI** is still open (the renderer's `px_per_himetric` is a fixed 1/25.4 = exactly 100 dpi, bit-faithful for GeoSym by rule but arbitrary for S-52 — harmless at ~100 dpi, a 2x mismatch on a retina pitch, and it now applies to raster tiles too, which are authored at one nominal pitch and blitted 1:1). |
| ~~E6~~ | ~~ENC raster symbol sheet~~ **DONE 2026-07-28 (row E6 above)**: `SymbolPixmap` + `IStyleEngine::Pixmap()` at the seam, S-52 tiles cut from `rastersymbols-*.png`, no placeholders left in these cells. | The seam is the reusable half: OSM sprite sheets are the same shape, and line-styles/patterns (LC/AP) still have no raster path — nothing in the Charleston data needs one, and a product that does would add it the same way. |
| ~~Q7~~ O1 | ~~OSM O1: MBTiles + MVT reader~~ **DONE 2026-08-04 (row O1)** | The prediction held: the source is all there was to write, because the seam, the rule layer, the retained scene, the pick index and the renderer were already built. **O2** = the MapLibre style subset as a LOADER over `LookupTableStyleEngine` (MapLibre `filter` is a third front-end for the §5.2 predicate AST; minzoom/maxzoom are ScaleBands) — and it is the first consumer of the JSON reader O1 brought in. **O3** = the E4 path exactly as written — an enumerator + a self-registration + one arm in `_open_vector` — plus per-tile clipping of features that straddle a seam (O1's one documented geometry deviation), and a golden Atlanta viewport. |

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
| ~~Q7~~ | ~~OSM O1: MBTiles + MVT reader~~ **DONE 2026-08-04 (row O1)** | `TestData/OSM/mbtiles/us-south.mbtiles` (Tilemaker, arrived 2026-07-28) ✓ | low-moderate — done. NOT dependency-free after all: protozero + vtzero + nlohmann/json, all header-only, all in `port/third_party` |
| ~~Q8~~ | ~~ENC E1: ISO 8211 / S-57 reader~~ **done 2026-07-25 (row E1)** | NOAA ENC cells in TestData ✓ | moderate; dependency-free — done |
| Q9 | OSM O2+O3: style loader + render via V5 seam | same as Q7 | low (after V5 + R2) |
| ~~Q10~~ | ~~ENC E2: S-52 PresLib (lookups + symbol display lists)~~ **done 2026-07-27 (row E2)** | OpenCPN `chartsymbols.xml`, `TestData/enc/` ✓ | moderate — done |
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

- 2026-08-04 (K1): **A seam that takes `int key` is not a keyboard API, and the way to
  find that out is to try to feed it from a real toolkit.** The overlay SPI shipped with
  `OnKeyDown(int)` in 14i and nothing consumed it for eleven sessions, so nobody noticed
  that the int had no documented numbering — not a VK code, not a keysym, not ASCII,
  just an int. It survived because the only tests were C++ ones that passed whatever they
  liked (`RouteKeyDown('x')`) and got it back. The defect surfaced the moment a UI shell
  had to produce the value: tkinter's own `keycode` is a platform-specific composite
  (Left = 2063660802 on Aqua), so there was NO correct thing to pass. **Generalisable**:
  `MouseEvent` was a struct with modifiers from day one and keys were an int, in the same
  header, ten lines apart — an asymmetry like that between two things that are the same
  kind of thing is worth treating as a bug report before a user files one.

- 2026-08-04 (K1): **VK codes over X11 keysyms, and why the cheaper option lost.**
  `keysym_num` was available and would have made the tkinter adapter a ONE-LINER (Tk
  normalises to X11 keysym numbering on every platform), against a ~30-entry table for
  VK. VK won on two counts: it is the numbering the product this is a port OF already
  speaks, so a future Windows shell passes `WM_KEYDOWN`'s wParam through untouched and a
  ported FalconView key handler needs no translation at all; and the port's standing rule
  for a value crossing a seam with a Windows peer is to mirror the Windows values and
  never renumber (`fv_map_enums.h`). The pleasant accident is that VK_A..VK_Z and
  VK_0..VK_9 ARE the ASCII uppercase code points, so the table only needs NAMED keys and
  an overlay writes `e.key == ord('D')` with no constant. `KeyValuesAreWin32AndNeverRenumbered`
  pins all seventeen values on the C++ side and the pytest pins them again on the Python
  side, because that is the surface an overlay author actually writes against.

- 2026-08-04 (K1): **Tk fires only the best-matching binding per tag, which makes a
  generic `<Key>` handler a trap.** PythonView bound `<Left>`, `<Prior>`, `<Escape>` and
  nine more specifically; adding `t.bind("<Key>", ...)` alongside them looks like "and
  also send me everything else", and it is — but "everything else" excludes every key the
  app already claimed, which is exactly the set an editing overlay wants (arrows, Page
  Up/Down, Escape, Delete). Measured, not assumed: generating `<Left>` fires only the
  specific handler and the generic one never runs. The fix is not to add more bindings but
  to have ONE, with the precedence written out in Python where it can be read — overlays
  first, then the app. Any future shell (Qt, a native Cocoa view) inherits the same rule:
  route to the overlay stack before the application's own accelerators.

- 2026-08-08 (R3c): **the build had no default build type, and three perf sessions were
  profiled at -O0.** `cmake -B build` names no configuration; a single-config generator with an
  empty `CMAKE_BUILD_TYPE` passes no optimizer flag at all. Every number R3a and R3b reasoned
  about, and the "query is 37 ms" that booked R3c, came from an unoptimized binary. At -O2 the
  DNC harbour frame is 49 ms cold and 3.4 ms on a retained pan, against 90 and 21. The lesson is
  not about CMake: **a profile is only a fact about the binary it was taken on**, and the first
  question of a perf session is what that binary was built with. The default is now
  RelWithDebInfo, so the ledger's own command line produces the build the numbers describe.

- 2026-08-08 (R3c): **an optimizing build is a UB detector.** Turning the default on immediately
  SIGTRAPped `SymColors.BrightnessAdjuster` — R3a's found-in-passing constructor reading two
  members before assigning them, recorded then as "benign in practice (the bytes are zero)". It
  was benign to -O0 only. Anything sitting on the known-UB list is a latent build-configuration
  bug, not a cosmetic one.

- 2026-08-08 (R3c): **the columnar `FeatureBatch` is a non-goal, and the number that killed it is
  the cost of COPYING the whole query result** — every string, every part vector, which is strictly
  more than a columnar layout could ever save. It is 0.5 ms of DNC's 9.2 ms query and 3.3 ms of
  OSM's 10 ms. Against that it would change `IVectorSource`, `IStyleEngine` and every synthetic
  test engine in the tree. Measure the ceiling of an optimization before designing it: a copy of
  the output is usually a cheap way to get that ceiling.

- 2026-08-08 (R3c): **the generalized "pre-parsed source cache" had two of its three consumers
  already, and the seam is why that is fine.** ENC parses its cells at Open; OSM keeps an LRU of
  decoded tiles; both were written that way without coordination because `IVectorSource` lets a
  source own whatever it needs to answer a query. DNC alone re-read and re-built the whole library
  on every call. The right move was to give the third product what the other two had, in its own
  file, and NOT to hoist a cache into fvkit that would have to be told what a product's cache unit
  is. A shared seam does not imply a shared implementation of everything behind it.

- 2026-08-08 (R3c): **the proof that a cost is a full scan is that it does not depend on the
  answer.** A query returning 5 features cost 9.55 ms; one returning 5,037 cost 9.43. That single
  pair of numbers is worth more than any amount of reading the loop, and it is the cheapest
  measurement in a perf session — vary the size of the answer and see whether the clock notices.

- 2026-08-08 (R3c): **a pattern's lattice must be anchored to the ground, not to the canvas.**
  `PlaceOverArea` floored the stamp grid against the canvas origin, which lines adjacent areas up
  with each other (the reason it was written that way) but ties the whole lattice to the viewport,
  so every fill crawled inside its own region under a pan. Anchoring on the projected position of a
  fixed geographic point keeps both properties. The anchor is computed from the projection's linear
  relation and NOT from `GeoToSurface`: that call unwraps longitude toward the view centre, and the
  unwrap branch flipping mid-pan is exactly the discontinuity a stable anchor exists to avoid.

- 2026-08-08 (R3c follow-up): **"fixed to the ground" is not enough — it has to be fixed to
  ground that is NEARBY.** The anchor above was lat/lon 0,0, ~600,000 px off-screen at Charleston.
  The lattice index of a point is then `lon/dpp_lon / spacing`: a huge lever arm divided by dpp.
  `MapProjection` derives dpp from the CENTRE LATITUDE in its scale and physical-scale modes, so a
  north/south pan moves dpp_lon by 1.26e-6 of itself per pixel, and 600,000 x 1.26e-6 is **0.75 px
  of lattice slip per pixel of vertical pan** — a whole 29.5 px cell every ~39 px of drag. The
  tufts stopped crawling and started blinking on and off, which is what Chris saw in the viewer and
  what the two screenshots showed. The anchor is now a retained GeoPoint walked to within one cell
  of the viewport centre each frame; the walk is a WHOLE NUMBER OF CELLS, so it re-describes the
  lattice without moving it, and the residual is 4e-5 px per pixel of pan. General form: **an
  invariant expressed as a difference of two large numbers is only as good as the units they are
  measured in.** Nothing was wrong with the geometry; the arithmetic had a lever arm.

- 2026-08-08 (R3c follow-up): **a test that pins a property must vary every input the property
  depends on.** The R3c pan test panned only in LONGITUDE — the one axis along which dpp is
  constant — so it was blind by construction to the defect above, and it passed on a chart that was
  visibly broken. It also used `SetResolution`, where dpp never moves at all, while the viewer runs
  in physical-scale mode. Two coordinates and two projection modes: the test exercised one of each.
  It now runs east, north and diagonal, in the viewer's own mode, through ONE renderer.

- 2026-08-08 (R3c follow-up): **the placer was asking about the wrong geometry.** It took the ring
  `ClipPolygon` had already rounded to whole pixels and deduplicated, so the boundary it tested
  moved by up to half a pixel — differently at every pan — and stamps near an edge flipped in and
  out. On a marsh cut to ribbons by tidal channels that is most stamps. This was independent of the
  anchor and showed on an EAST pan, where the anchor was blameless: 1.07 % of the frame, unchanged
  by the anchor fix, which is what made it visible as a separate thing. The placer now takes the
  exact projected ring and bounds its walk to the canvas. **Membership is a question about the
  geometry; do not ask it of a copy that exists for the rasterizer's convenience.**

- 2026-08-08 (R3c follow-up): **pick the test viewport for the feature under test, not for
  familiarity.** The pan test reused the E3a golden's 512 px harbour view, which is nearly all
  water and holds barely a dozen stamps; at 3 % lattice error it still passed. Moved to 768 px over
  the marshes south of the harbour, the same defect reads 3.02 % against a 0.35 % threshold.
  Calibrated both ways round rather than guessed — the ledger row carries the nine numbers.

- 2026-08-08 (O3): **Overzoom is one half clamped and the other half not, and saying which
  half is the whole design.** A pyramid ends at z14; a display does not. The source clamps
  its READ level (there are no deeper tiles to read) and the style engine keeps evaluating at
  the real zoom, so past the bottom the map draws z14 geometry under z15, z16, z17 rules and
  keeps growing — which is what every slippy map does and what a user reading "zoom in" means.
  Clamping BOTH (the obvious symmetry, and what matching the source's `SetZoomOverride` on the
  style would have produced) freezes line widths and symbol sizes at z14 while the geometry
  keeps expanding: the map does not go blank, it goes subtly wrong, roads growing hairline
  against buildings the size of the screen. The diagnostic is reported only past the BOTTOM of
  the pyramid: at any other level the fractional gap is nearest-level rounding, which is
  normal, and a number that is non-zero almost always is a number nobody reads.

- 2026-08-08 (O3): **Clipping a feature changes two things besides its geometry, and both
  are silent if missed.** Per-tile clipping closed O1's doubled-ink deviation, but a clipped
  feature carries (a) a BOUNDS box describing geometry it no longer has — the pick index is
  built from bounds, so a tap on empty map hits a road that was cut away three tiles over —
  and (b) a claim to have met the QUERY area that was true of the whole feature and may be
  false of this tile's share of it. Both were caught by existing tests (the seam's "every
  returned feature overlaps the query" assertion, and the multipolygon ring-bounds one), which
  is the argument for having written those assertions about the CONTRACT rather than about
  what the code did at the time. General rule: geometry, bounds and the predicate that
  selected the feature are one thing, and a transform touches all three or none.

- 2026-08-08 (O3): **A per-frame setter is a cache invalidation.** The OSM style engine needs
  the viewport's centre latitude, which changes on every pan, and every setter on a style
  engine bumps the style epoch by contract — so the honest wiring (set it each frame) would
  have disabled R3a's retained scene entirely, and disabled it INVISIBLY, as a frame-rate
  regression with no failing test. The fix is to quantize the input to a step below the
  precision anyone can observe (half a degree of latitude = ~0.01 of a zoom level), which
  turns a per-frame setter back into a rare one. The ledger's standing rule already said "a
  no-op setter must stay a no-op or the cache never lands"; this is its other half — an input
  that genuinely changes every frame has to be COARSENED before it reaches such a setter.

- 2026-08-08 (O3): **The overlay SPI hands a projection to OnDraw and none to OnMouseDown**,
  so an overlay that turns a click into a position has to keep one. Keeping the borrowed
  reference is the obvious move and is wrong — its lifetime is the caller's business, and
  PythonView replaces its whole engine (and that engine's projection) on any catalog change.
  The route overlay therefore keeps a COPY, reconfigured each frame through the bound
  `set_surface_size`/`set_center`/`set_resolution`: below the mode distinction a projection is
  a centre plus a degrees-per-pixel pair, so the copy reproduces the transform exactly and no
  projection math is duplicated in the app. `MapProjection` gained `center` and
  `surface_size` accessors in pyfvw to make the copy possible. (The alternative — putting a
  geographic position on `MouseEvent` — was rejected: it would make every shell responsible
  for un-projecting, which is precisely the duplication K1 avoided for keys.)

- 2026-08-08 (O2): **A style engine cannot derive its own zoom, because a Web Mercator zoom
  is not a scale — it is a scale AT A LATITUDE.** The relation holds pixels constant, not
  ground metres, so z12 is 1:270k at the equator and 1:190k off Charleston: a 40% spread,
  which is more than a whole zoom level's worth of symbology. `StyleContext` carries a scale
  denominator and no geography (correctly — it is product-neutral), so the latitude has to be
  configured ON the engine and kept equal to the one the source is using. The temptation was
  to pick the equator and call it canonical; that would have made every style band wrong by
  a level and a half at US latitudes, silently, in a way that looks like "the cartography is
  a bit off" rather than like a bug. The mitigation that made it safe is smaller than the
  problem: ONE implementation of the relation (`ZoomForScaleExact` / `ScaleForZoomExact`),
  with O1's existing `ZoomForScale` refactored to call it rather than keeping its own copy of
  the formula, and a round-trip test across latitudes and pixel pitches so neither argument
  can be quietly dropped.

- 2026-08-08 (O2): **MapLibre's `!=` and `!in` are TRUE for a missing tag; fvkit's are FALSE.**
  `rules.h` states the rule plainly — a missing attribute makes every comparison false, and
  `kMissing` is how you ask about absence — and that is the right rule for a rule LAYER. But
  a GL style's `["!=", "brunnel", "tunnel"]` means "not a tunnel", and the overwhelming
  majority of roads carry no `brunnel` tag at all, so reading it fvkit's way hides the entire
  road network and leaves only the tunnels. **The fix is in the LOADER, not the predicate**:
  the loader emits `Or(Missing(k), NotEqual(k, v))`. Loosening the shared comparison for one
  product would have changed what an authored `.rules` file means for DNC and ENC too, which
  is exactly the drift E3c extracted `CompareRuleValues` to prevent. General principle for
  the next product: a foreign condition language is translated at its own front end, and the
  shared AST stays the AST.

- 2026-08-08 (O2): **Draw order is a property of the STYLE, not of the features.** Every
  earlier product here symbolizes a feature more or less independently — GeoSym's `dispri`
  and S-52's display priority are per-row constants and features do not conspire. OpenMapTiles
  roads do: the visual is built from casing/fill PAIRS, a wide dark line under a narrow bright
  one, both emitted by the SAME feature, and correctness requires every casing in the viewport
  to be painted before any fill. Per-feature draw order puts a road's casing on top of its
  neighbour's fill and shreds every junction. This needed no new machinery — `priority` = the
  style layer's index, and `VectorScene`'s stable sort is already ACROSS features (R3a) — but
  it is the first time that cross-feature sort has been load-bearing rather than tidy, and it
  is worth knowing that the retained scene is what makes GL-style rendering possible here at
  all.

- 2026-08-08 (O2): **The declared-subset discipline is cheap to write and is the whole value
  of the loader.** A GL style sheet in the wild is mostly expressions (`["match", ["get",
  "class"], …]`), and a loader that skipped what it did not understand would produce a map
  that is plausibly wrong — roads present but uncased, water the wrong blue, a whole class of
  feature missing — which is the failure mode E5 spent a session discovering the hard way on
  ENC. So every unsupported construct is a **load failure naming the layer id and the
  property**, and the load is all-or-nothing. The cost is that a real-world style will not
  load until the subset grows; that is the correct trade, and the error message is the
  worklist for growing it.

- 2026-08-04 (O1): **Three header-only dependencies, and why writing them by hand would
  have been the worse call.** Every other reader in this port is spec-driven and its own
  (ISO 8211, S-57, the S-52 PresLib, the CGM parser), so taking protozero + vtzero for MVT
  is a departure and is worth stating. The difference is what a bug costs. ISO 8211 had no
  adequate implementation to take — `Applications/GeoRect/adrg/iso.cpp` is ASCII-subfields-
  only and S-57 is binary — so writing one was the only option. Protobuf has a mature,
  tiny, BSD-2, header-only implementation, and a hand-rolled varint/zigzag reader that is
  subtly wrong does not fail loudly: it MISPARSES, and a misparsed tile looks like a chart
  with the wrong roads on it. vtzero also owns the part that is genuinely fiddly — the
  geometry command integers, the cursor deltas, the ring-winding classification — which is
  exactly the code a from-scratch version would get wrong first. **The plan already said to
  vendor both** (§6, the stb_truetype pattern); this session only changed the MECHANISM to
  the 2026-07-25 one, FetchContent in `port/third_party`, so versions live in one file and
  nothing multi-MB lands in a repo that has had a blob problem. nlohmann/json is the same
  argument on a smaller format: the port genuinely had no JSON reader, MBTiles keeps its
  layer inventory in one, and O2's MapLibre styles are JSON through and through.
  **The hermetic tests are what keeps this honest**: `mvt_test.cpp` builds MVT tiles BYTE BY
  BYTE from the 2.1 schema and never touches vtzero's own builder — a test that encoded
  with the library it decodes with would prove the two agree, not that either matches the
  spec — and the real-data expectations come from an independent Python oracle written
  against the wire format, which agreed with the C++ decoder to **40,780 vertices out of
  40,780** on the Atlanta tile.

- 2026-08-04 (O1): **An MBTiles file's declared `bounds` cannot be trusted, so coverage is
  derived from the tiles.** TestData's `us-south.mbtiles` declares
  `-106.649400,24.026720,0.000000,40.646360` — an east edge of EXACTLY 0.000000 degrees,
  the Greenwich meridian, some 2,000 km past its easternmost tile and most of the way
  across the Atlantic. The other three edges are right to within a tile. A source that
  believed it would have told the catalog it covers the mid-Atlantic, and every viewport
  query out there would have read tiles that are not there. `MbtilesFile::Bounds()` measures
  the pyramid instead (an index-only MIN/MAX over the deepest zoom, ~100 ms for 594,419
  tiles, cached), `declared_bounds()` keeps the file's own claim, and
  `declared_bounds_disagree()` reports the discrepancy rather than hiding the repair.
  Generalises past this file: a cutter writes `bounds` from its config, the tiles from the
  data, and only one of those is evidence.

- 2026-08-04 (O1): **`ZoomForScale` had to clamp latitude, and a test at the pole is what
  found it.** `cos(90 degrees)` in doubles is 6.1e-17, not 0 — so the guard for a
  non-finite ratio never fired, and 1:12,000 at the pole asked for **zoom 0** instead of a
  deep level: the answer walked off the BOTTOM of the pyramid through a clamp that looked
  like it was protecting it. Fixed with the same `kMaxLatitude` clamp `LatToTileY` already
  applies, which is the only answer consistent with the grid having no row up there.
  `LatitudeIsClampedBeforeItReachesTheZoomFormula` asserts the pole agrees with the last
  real row AND that the result is neither endpoint of the clamp range, so a future
  "simplification" back to a bare guard fails.

- 2026-08-04 (O1): **A tile pyramid needs a tile BUDGET, because `scale_denominator = 0` is
  a legal query.** The seam documents 0 as "no scale filter", and DNC and ENC read it as
  "do not thin" — cheap, because their unit of I/O is a library or a cell that is already
  open. For a pyramid the same query means the whole coverage at maxzoom, which here is
  594,419 tiles. So an AUTOMATIC zoom is stepped coarser until the range fits
  `SetMaxTilesPerQuery` (default 64) and `last_query_zoom_capped()` says so, while an
  explicit `SetZoomOverride` is honoured EXACTLY and never capped — a caller that names a
  zoom has said what it wants, and silently changing the generalization under it is worse
  than being slow.

- 2026-08-04 (O1): **Two documented geometry deviations at the seam, both from things MVT
  does that no other product here does.** (1) **A POLYGON geometry may be a MULTIpolygon.**
  The seam's contract is `part[0]` outer plus holes, so a multipolygon is split into one
  `VectorFeature` per outer ring, all sharing one `FeatureRef` — identify names the FEATURE,
  not the ring. Bounds are computed PER EMITTED POLYGON: two islands must not each claim a
  box covering the water between them, or the pick index hits on open sea. (**Found by test,
  not by review**: the first version computed bounds only for the last polygon of the loop,
  so every earlier piece of every multipolygon came back with an empty box at 0,0 — which
  then failed the "every returned feature intersects the query" assertion in a different
  test entirely.) (2) **Tiles carry a BUFFER of their neighbours' geometry** so a wide line
  can be drawn across a seam. A feature lying entirely inside another tile's box is dropped
  here — exact for points, and what stops every edge label being drawn twice — but a line or
  area that STRADDLES a seam is present, clipped, in both tiles and is drawn twice in the
  overlap. Harmless for opaque fill, a double-blend for anything with alpha. Making it exact
  needs per-tile clipping where the pixels are, so it is an O3 item and is listed as one.

- 2026-07-28 (E5): **Self-consistency is not correctness: check the output against someone
  else's rendering of the same data.** Every ENC test to this point compared the port against
  itself — pinned hashes, corpus sweeps, "every feature is symbolized or counted" — and all of
  them passed while more than half the ink in a harbour viewport was metadata and 61 symbols
  drew clear of the features they annotate. Neither defect is detectable from inside: the meta
  objects WERE being symbolized correctly, and the misplaced symbols were faithful to the file.
  It took one screenshot beside a published chart. The same check is owed to DNC/GeoSym (against
  a real DNC viewer) and will be owed to OSM. Corollary for how to look: Chris's "the marsh
  symbols look correct" was the most useful sentence in the report, because it ruled out a
  global scale error and turned the question into which symbols, which is answerable by dumping
  per-symbol geometry.

- 2026-07-28 (E5): **Deviating from delivered data needs the data to say so twice.** The 61
  off-glyph pivots are wrong, but "wrong" was only actionable once the file's own second copy
  agreed — the bitmap pivot carries the same out-of-range fraction for 47 of them, and a raster
  pivot outside its own 29x29 tile cannot be intent. That is what distinguishes a conversion
  artefact worth overriding from an authored offset worth honouring, and it is why the override
  is bounded (a quarter of the symbol's size beyond its ink) and why the test pins the symbols
  that must NOT move alongside the ones that must. Where a product has no Windows original, the
  bit-faithful rule has nothing to anchor to and this is the substitute.

- 2026-07-28 (E4): **A format adapter does not have to live in fvkit, and ENC's cannot.**
  `fv_enc` links `fv_fvkit` (it needs the `VectorSymbol` seam and the R2 rule layer), so an
  fvkit that named `EncFrameEnumerator` would close a dependency cycle. Rather than bend the
  layering — hoisting the ENC reader under fvkit, or splitting fv_enc in two — ENC registers
  itself through `RegisterEncFormat()`, which is what `RegisterFormat()` being public and
  multi-slot was for (D1). The rule for every future product: **a format whose reader depends
  on fvkit registers itself from its own library**, and the CONSUMER calls both registrars. The
  Python binding hides that: `register_builtin_formats()` calls both, because a build-layering
  fact is not something an application should have to know. OSM will land the same way.

- 2026-07-28 (E4): **"Is it VPF?" was standing in for "is it vector", in four places.**
  PythonView tested `series.format == "vpf"` to choose the render mode, to decide whether to
  open a vector source, and twice more in `set_series` — so ENC catalogued and appeared in the
  menu, and then rendered a blank raster frame. The same shape of bug appeared twice more the
  same session: the app's `--selftest` walked one series per FAMILY, so with two products in
  Vector Charts it exercised whichever sorted first and would never have caught the ENC path
  (it was also, silently, never testing **tiros** — a pre-existing hole the fix exposed); and
  `_render_vector` reached for `self.style`, the GeoSym engine, by name. All three are the same
  mistake: **the first implementation of a plural thing gets treated as the thing itself.** Now
  `VECTOR_FORMATS` is derived from `FAMILIES`, the selftest walks one series per FORMAT, and the
  active engine is `self.vstyle`. Worth checking for wherever a second product lands next.

- 2026-07-28 (R3b): **A perf plan written before the profiler is a hypothesis, and this one was
  wrong.** R3a named the symbol atlas and the columnar `FeatureBatch` as R3b, on the reasonable
  guess that "DNC navaid clusters re-walk the CGM display list per instance" was where a 78 ms
  draw went. The first actual split of that draw put point symbols at 20% and the two largest
  costs — polygon fill and CLIPPING — on nobody's list; lines, which emit 1582 of the 2527 draw
  calls, are 1.1 ms and were never worth a thought. The rule this sets for R3c and after:
  **instrument the phase before optimizing it**, and keep the instrumentation cheap enough to
  leave in (`query_ms/style_ms/draw_ms` already pay for themselves; a per-primitive split is
  still a temporary patch, which is the next thing worth making permanent).

- 2026-07-28 (R3b): **The symbol atlas is deferred, and not only because it is now 7.5 ms of a
  32 ms draw.** Caching a rasterized symbol per (symbol, size, rotation) and blitting instances
  means stamping at INTEGER pixel offsets, while `DrawSymbolAt` today maps every vertex through
  a fractional anchor — so an atlas cannot be byte-identical, and it would move both pinned
  goldens by up to half a pixel per symbol. Every other R3 change so far has been exactly
  output-preserving, which is what has made "the golden is unmoved" a usable acceptance test.
  Spending that property on a 23% slice of the draw phase is a bad trade while cheaper exact
  wins remain (see R3c). Revisit when a real caller is symbol-bound — a dense VMAP0 world sheet
  would be the honest test, not the harbour.

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

- 2026-07-26: **R2 shipped the rule layer but NOT the `LookupTableStyleEngine`
  extraction — and that is the plan's own instruction, not a shortcut.** §5.1
  says to extract the shared engine core "when the *second* real table exists,
  not speculatively against GeoSym alone", and §5.5 put R2 after E1 on the
  assumption that E1 would supply it. It did not: E1 delivered the S-57
  *reader* (the DATA side); the S-52 *table* is `chartsymbols.xml`, which is
  still not in TestData (checked, 2026-07-26 — no `chartsymbols*`, no `.dai`
  anywhere under it). Extracting today would still be an abstraction shaped by
  one table, so the extraction **moves into Q10/E2**, where the second table
  arrives and its shape is visible. Nothing is lost: rules.h — the part that is
  genuinely cross-product and that E2/O2 will build on — is in place, and the
  GeoSym engine is now a client of it rather than a fork of it.

- 2026-07-26: **Rule-layer design decisions worth not re-deriving.**
  - **The comparison rule is `numeric iff BOTH sides parse whole`.** Half-parsed
    numbers are how a string compare silently becomes a wrong numeric one, so
    `"3a" < "10"` stays a byte comparison while `"9" < "10"` is arithmetic. The
    trap it exists to stop is pinned in a test: as strings, "9" > "10".
  - **A missing attribute makes EVERY comparison false, `not in` included.**
    "Not in that list" is a claim about a value that is present; `missing` is
    the operator for absence. Without this, one typo'd attribute name turns a
    hide-rule into a hide-everything rule.
  - **Rules apply in source order, later wins, and compilation may only fold a
    rule into the cached decision while nothing per-feature has been seen yet.**
    From the first per-feature rule onward every applicable rule is deferred,
    unconditional ones included — otherwise a `hide` written *after* a
    conditional `show` is folded in first and loses. The naive version of this
    fold was written, caught by a test, and the test is still there.
  - **Group/category filtering in GeoSym sits AFTER `any_matched`.** A group the
    operator switched off means "do not draw this", not "the FACC had no
    matching row"; running it earlier fires the 2nd-chance fallback and paints a
    default black dot in place of every hidden feature — the same failure mode
    the V5b label fix already found once.
  - **`ViewingGroupSet` is one number space**, shared by GeoSym's viewing groups
    and text groups. Safe here and *verified* rather than assumed: across
    fullsym.txt the viewing groups run 11050..38010 (always 5 digits) and the
    text groups 1..29, pinned by a test that reads the shipped table. A product
    whose spaces overlap needs a second set, not a renumbering.
  - **Thinning is opt-in.** An empty RuleSet + a default ViewingGroupSet means
    the plan is `trivial()` and every feature is visible with no effects, so the
    retrofit left the DNC golden hash untouched. That is what keeps the
    bit-faithful rule and a rule engine in the same codebase.
  - **`restyle` (replacing colours/symbols from a rule) is deliberately absent.**
    It needs the style vocabulary from style.h, and rules.h sits *below* style.h
    so a source-only or test-only caller can use rules without the canvas.
    Adding it means moving a `StyleOverride` into style.h, not growing
    `RuleEffect`.

- 2026-07-27 (E2): **Three properties of the delivered PresLib/catalogue, each
  found by a corpus sweep that failed, each now pinned by a test.** All three
  are data facts, not parser bugs, and all three would have been silent
  rendering defects in E3.
  - **`PD;` with no coordinates is a DOT**, and whole symbols are built from
    `PUx,y;PD;` stipples (OBSTRN02, SPRING02). Skipping the empty argument list
    — the obvious reading — erased those symbols completely. Emitted as a
    zero-length run, which the canvas stamps as one square nib of the pen
    width, exactly as pen-down-in-place does.
  - **73 symbol names are defined TWICE**, once as vector and once as
    raster-only (`<definition>R`, no HPGL), with different RCIDs — so 1093
    `<symbol>` elements are 1018 symbols. **The vector definition wins
    regardless of file order**: first-wins silently loses the geometry of every
    symbol whose raster row happens to come first. Element counts are therefore
    NOT symbol counts, and the test derives both rather than pinning either.
  - **24 names are referenced by lookups and defined nowhere** (92 references:
    BOYLAT52..56, FLTHAZ02, ARCSLN01 …). 23 are absent from all three tables and
    several are plainly authoring typos in the source library (`NEWOBJ 01`,
    `TOWERS74|`, `DGPS01DRFSTA01`); exactly one, `ESSARE01`, exists under
    another kind (a line-style called with `SY()`), which E3 can recover by
    falling back across kinds. Exposed as
    `S52PresentationLibrary::UnresolvedSymbolReferences()` and pinned at 24, so
    a PresLib swap surfaces the change instead of hiding it. **E3 owes these a
    visible placeholder** — §7's never-silently-drop-a-feature rule.
  - Bonus, in `s57objectclasses.csv`: the `Class` column's **`C` is COLLECTION
    (C_AGGR/C_ASSO/C_STAC) and `$` is CARTOGRAPHIC** ($AREAS/$LINES/$CSYMB/…),
    the opposite of what the letters suggest — and OpenCPN's pseudo-class
    `_texto` **reuses code 135**, which S-57 gives to TESARE. First wins, so the
    real class keeps the code, and 251 rows load as 250 object classes.

- 2026-07-27 (E2): **The loader is not a style engine, deliberately.**
  `S52PresentationLibrary` parses and exposes; matching a feature to a lookup,
  running CS procedures and emitting `StyleResult`s is E3. Two consequences
  worth not re-deriving: (1) **all five lookup tables load**, because paper-vs-
  simplified and plain-vs-symbolized is a mariner display setting, not a
  load-time choice; (2) **display lists are cached per (kind, name, colour
  table)** — colours are baked into the primitives, so the palette is part of
  the key, and `kind` is in there because a name can be both a symbol and a
  line-style with different geometry (ACHARE51 is).

- 2026-07-27 (E3a): **`?` in a lookup condition is S-57's UNKNOWN-VALUE marker,
  not a wildcard.** E2's header listed the four value shapes without settling
  what `?` MEANS, and the wrong reading is not subtle: DEPARE's first row is
  `[DRVAL1=?][DRVAL2=?] -> AC(NODTA);AP(PRTSUR01);LS(SOLD,2,CHGRD)`, i.e.
  "unsurveyed", so read as "present with any value" it matches EVERY depth
  area, paints the whole harbour no-data grey and the depth ramp never runs
  (that is exactly what the first Charleston render looked like). The DATA
  settles it: all 479 DEPARE areas in the cells carry real DRVAL1/DRVAL2
  values and NONE carries an empty one, so the no-data row can only be meant
  for the empty case. Implemented as "present AND empty"; the blank forms
  (`""` / `" "`) are the actual wildcard. Pinned in
  `S52Style.UnknownValueConditionDoesNotMatchARealDepth`.

- 2026-07-28 (F3): **An ENC series is a usage BAND, and a directory cannot
  express one.** `EncVectorSource` opened a path — one cell, or everything
  under a directory — and PythonView asked it for the common ancestor of a
  series' cells so a pan across the band would not need a source swap. The
  common ancestor of an exchange set is the exchange set, so every band opened
  every cell and a 1:12,000 harbour chart drew a 1:1,000,000 general cell
  underneath itself. `OpenCells(cells, catalog_dir)` takes the list instead,
  and the directory forms delegate to it. The general rule this is an instance
  of: when a catalog already knows which files belong to a selection, a reader
  should accept that selection rather than be handed a path and asked to
  re-derive it — the re-derivation is where the band was lost.

- 2026-07-28 (F3): **A vector series that declares a scale opens AT it.**
  Fitting the series' whole extent to the window is right for a DNC library,
  which is a place with no chart scale, and wrong for an ENC band, which is a
  scale: an S-57 cell is compiled for its band's nominal denominator and its
  SCAMIN thinning is authored against that number, so opening anywhere else
  shows a chart the producer never composed. `scale_denom > 0` is the test,
  and it is the same test the raster path already uses to tell a scaled series
  from a scale-less one.

- 2026-07-28 (F3): **The scale ladder gives the current product first refusal.**
  PageUp/PageDown steps to the nearest larger/smaller-scale series AT the
  screen centre, and that used to mean the nearest one in ANY product. Over
  water covered by ENC, CADRG, DOQs and DTED at once — which Charleston is —
  that walks a mariner out of the chart he chose and into a topo sheet halfway
  up the band ladder. Now the current format is searched first and the full
  ladder is the fallback, so a product is paged through to its end before the
  ladder crosses over. This CHANGED raster stepping too, deliberately.

- 2026-07-28 (E6): **A pixmap is a SECOND symbol form at the seam, not a
  rasterized VectorSymbol.** `IStyleEngine` could have been left alone by
  turning each raster tile into a display list of one-pixel polygons, and that
  would have been wrong twice: a tile has no geometry to hand back, so every
  consumer of `Symbol()` — the pick index, the along-path placer, a future GPU
  backend — would be reasoning about a rectangle pretending to be a drawing;
  and 679 symbols x hundreds of pixels is a display list per glyph where a blit
  will do. So `Symbol()` gained a twin, `Pixmap()`, defaulting to nullptr:
  GeoSym, the synthetic test engines, and any future product with no raster
  symbology never mention it. The renderer resolves an id ONCE into
  `ResolvedSymbol{vec, pix}` and prefers the display list, so a symbol authored
  both ways keeps the form that scales and rotates without resampling.

- 2026-07-28 (E6): **A tile's scale must not be derived from the HIMETRIC one.**
  The renderer carries `px_per_himetric = symbol_scale / 25.4` for display
  lists and `pixmap_scale = symbol_scale` for tiles. Passing one and deriving
  the other reads simpler and was tried; it fails, because `2.0 / 25.4 * 25.4`
  is not `2.0` and the nearest sampler's boundary at an integer zoom lands
  exactly on a half-pixel — the first column of every doubled tile disappeared.
  Caught by `SymbolScaleResamplesTheTile`, not by reading the code. The same
  half-pixel is why the sampler rounds with `floor(t + 0.5)` and not `lround`:
  they differ only at exactly -0.5, which is precisely where that first column
  sits.

- 2026-07-28 (E6): **Both symbol paths snap the anchor to a whole pixel first.**
  D4 puts a pixel's centre ON the integer, so a symbol anchored at a half-pixel
  has no sub-pixel placement worth preserving in something authored to be
  blitted. Snapping first is what makes the resampler REPRODUCE the straight
  blit at unit scale (pinned byte-for-byte in
  `ResamplerAtUnitScaleMatchesTheStraightBlit`) instead of shifting the glyph
  by a pixel the moment a zoom crosses 1.0 — the kind of thing nobody reports
  as a bug and everybody sees.

- 2026-07-28 (E6): **The bitmap pivots carry the same delivered defect E5 found
  in the vector ones, and 28 of them are INT_MIN.** 292 of the 1083 `<bitmap>`
  pivots lie more than a quarter of the tile outside the tile itself, and
  ARPONE01's is written `-2147483648` in both axes — a missing value, not an
  anchor, and the file has 28 of them. Same narrow deviation as E5's and
  deliberately the same bound: a pivot that far out is replaced by the tile's
  centre. The test asserts BOTH halves, as E5's does — ARPONE01 and CTNARE51
  (pivot (52,-17) on a 29x29 tile) re-anchor, and DAYTRI52, a daymark standing
  on its post at pivot y=30 of a 33 px tile, keeps what it was authored with.

- 2026-07-28 (E6): **A missing symbol sheet is not an Open() failure.** The
  vector half of the presentation library is fully usable without
  `rastersymbols-*.png`, and refusing to open would turn a missing optional
  asset into "no ENC symbology at all". `SymbolBitmap()` returns nullptr, the
  reason is available from `raster_sheet_error()`, and the failure is
  remembered per colour table so a chart full of raster symbols does not retry
  a missing file once per feature. Tiles are cached per (name, colour table)
  exactly as the display lists are — the three sheets ARE the day/dusk/night
  palettes, already coloured, so a scheme switch re-cuts rather than recolours.

- 2026-07-27 (E3a): **The 17 unresolved symbol names are RASTER-ONLY
  definitions, not dangling references.** E2 counted 24 names that lookup
  instructions refer to and the library does not DEFINE; a corpus sweep over
  everything the Charleston cells actually reach finds a different and larger
  category — names the library defines with no HPGL at all (`<definition>R`),
  which a vector-only path cannot draw however complete the file is. 17 names,
  166 references, and zero true danglers in this data. They draw QUESMRK1 and
  are counted. Fixing them is not a parse problem: it needs the raster symbol
  sheet (`rastersymbols-day.png`, already in TestData) blitted as a pixmap,
  which is a later phase. The test asserts the CATEGORY, so a real dangling
  reference appearing later fails rather than hiding among these.
  **SUPERSEDED 2026-07-28 (E6)**: the sheet is blitted, so these 17 draw their
  own tiles and are no longer counted as unresolved. The test tightened rather
  than disappeared — a name that still comes back unresolved must be one no
  `SY()` named, i.e. a line-style or a pattern, which have no raster path.

- 2026-07-27 (E3a): **A feature drawing nothing is allowed in exactly two
  cases, both given by the data**, and the corpus sweep asserts that rather
  than tolerating a count: (1) the lookup row's instruction is EMPTY — the
  library itself says draw nothing (M_NPUB, nautical-publication coverage);
  (2) the row is text-only and the feature lacks the named attribute (an
  SBDARE point with no NATSUR has no seabed nature to print). Anything else is
  a hole in the engine. NOTE this is also why the sweep runs with labels ON: a
  large minority of S-52 rows are text-only (a sounding IS its number), so with
  labels off 227 features draw nothing and the test would be measuring the
  label switch. Same shape as DNC's label-only sounding rows (row 14q).

- 2026-07-27 (E3a): **CS procedures are the one thing here written from the
  published spec rather than read out of the delivered data**, because
  chartsymbols.xml carries lookup rows and symbols but no procedure code. Two
  things keep that honest: each procedure names the parts it reduces, and the
  attribute VALUES it tests are grounded in the Appendix A catalogue E2 loaded
  (CONDTN 1/2 = under construction/ruined, QUAPOS 2..9 = inaccurate) rather
  than remembered. Where a procedure could not be grounded at all it was NOT
  written: TOPMAR01 needs the TOPSHP -> TOPMARnn mapping table, which is in
  neither the XML nor the CSVs, and guessing it would put wrong topmarks on a
  chart — the exact failure mode this project forbids. It is counted instead.

- 2026-07-27 (E3b): **The along-path placer is ONE primitive, and it lives in
  the renderer.** GeoSym's SAMI line style, S-52's `LC`, S-52's `AP` and
  (eventually) an OSM line pattern all say the same thing: repeat something
  along a path, or over an area. `LinePatternStyle` is therefore a CYCLE of
  `PathRun{kGap|kDash|kSymbol}` measured in PIXELS — the same unit contract
  `Pen` already carries, so each style engine converts its own authoring units
  and the renderer converts nothing. `PlaceAlongPath`/`PlaceOverArea` are free
  functions over pixel geometry with no canvas and no style engine in their
  signatures, which is what makes the 10 hermetic tests possible: if the
  placer ever needs a chart product to be testable, the seam has leaked.
  Rotation convention: the emitted `rotation_deg` is `atan2(-dy, dx)` of the
  local tangent, i.e. the symbol's own +x axis runs along the path, expressed
  in the sense `DrawSymbolAt` already applies (screen y down, symbol y up).

- 2026-07-27 (E3b): **The pattern is laid along the UNCLIPPED projected path
  and clipped afterwards.** Clipping first is cheaper and was the obvious
  implementation, and it is wrong: the cycle would restart at the canvas edge,
  so every dash and every stamped symbol would jump each time the map panned
  by a pixel. `PatternPhaseIsMeasuredFromThePathNotTheCanvasEdge` pins it by
  rendering the same feature into two viewports ten pixels apart and comparing
  the shifted columns. The cost is bounded by `kMaxPatternCycles` (4096
  cycles) and a saturated step budget, so a projection that puts a vertex a
  million pixels away truncates the walk instead of hanging the render.

- 2026-07-27 (E3b): **A GeoSym SAMI symbol run was being drawn as a DASH.**
  Pre-E3b, `fv_geosym_style.cpp` walked a component's elements and pushed
  EVERY element's length into `Pen::dash` regardless of type, with the comment
  "kPointSymbol runs become gaps" — but a length pushed into `dash` is an ON
  run, not a gap, so the symbol became a black dash of its own width. The DNC
  harbor golden had been pinned over this since V5b. 89 of the 757 delivered
  CGM line symbols carry such a run; the fix moved 972 pixels of the golden,
  all of them one cable/limit line turning into the magenta symbol chain the
  CGM authors. A component with NO point-symbol element still takes the pen
  path — same output, less work — so the change is confined to the case that
  was broken.

- 2026-07-27 (E3b): **Two raster-only substitutions, and two deliberate
  question marks.** E3a recorded that 17 symbol names the cells reach are
  raster-only definitions with no HPGL. Three of the seven new procedures ran
  straight into them. Where the library ships a VECTOR twin with the same
  description the twin is used and the substitution is stated at the call site
  — `ISODGR51` -> `ISODGR01` ("isolated danger of depth less than the safety
  contour") and `OBSTRN11` -> `OBSTRN18` ("obstruction in the water which is
  always above water level"). Where it does not, the procedure still emits the
  name and the engine's existing unresolved-symbol path draws QUESMRK1:
  `WRECKS07` (the "least depth unknown" over-line) and `TOPMAR01` ("topmark
  not defined"). That is the right outcome both times — plan section 7 says a
  fact must not be lost silently, and for TOPMAR01 an undefined topmark
  SHOULD look undefined.

- 2026-07-27 (E3b): **E3a's TOPMAR01 blocker was a missing JOIN, not missing
  data.** E3a declined to write the procedure because "the TOPSHP ->
  TOPMARnn mapping table is in neither the XML nor the CSVs, and guessing it
  would put wrong topmarks on a chart". Both halves are in the delivered data
  and E2 already loads them: `s57expectedinput.csv` enumerates TOPSHP by TEXT
  ("1 = cone, point up") and every `<symbol>` carries a `<description>`
  ("topmark for buoys, cone point up"). Matching the two is the table. The
  BUOY family is used throughout, which is the one reduction: choosing between
  the buoy (02..65) and beacon (22..89) families needs the topmark's PARENT
  object, and S-57's master/slave relation — read by E1 — is not published
  through `IVectorSource`. Exposing it belongs with the source, not the style
  engine. Only 2 features in these cells reach the procedure.

- 2026-07-27 (E3b): **UDWHAZ03 is reduced to its depth test, and the reduction
  errs toward warning.** OBSTRN04 and WRECKS02 both turn on "is this a danger
  to the mariner?", and the published procedure also asks whether the feature
  is surrounded by water deeper than the safety contour — an isolated danger
  in safe water gets the loud mark, one already inside a shoal does not. That
  needs the neighbouring DEPARE, i.e. the retained scene R3 builds. The depth
  test alone decides here, so an isolated-danger mark can appear inside shoal
  water where a full ECDIS would leave it plain. Over-warning is the correct
  direction to be wrong on a chart.

- 2026-07-27 (E3b): **LIGHTS05 draws sector lights as plain flares, and
  COUNTS it.** The spec draws a sector light's two legs and the arc between
  them at the light's nominal range. That is GEOMETRY, and a CS procedure
  returns an INSTRUCTION STRING — it can name symbols, not arcs. Rather than
  invent a geometry-producing style op for one procedure, the simplification
  is exposed as `S52StyleEngine::sector_lights_simplified()` and asserted
  non-zero over the real cells, so the deviation is measured rather than
  remembered. The light DESCRIPTION string ("Fl(2)R.10s12M") is likewise not
  composed; it is a six-attribute text formatter on the labels axis.

- 2026-07-27 (E3b): **RESTRN01/RESARE02 emit the boundary line and NOT the
  centred symbol.** S-52 puts a copy of the restriction symbol inside the
  area. The renderer anchors point symbology at a part's FIRST VERTEX, so that
  copy would sit on a corner of the boundary rather than in the middle of the
  area — a misplaced restriction symbol is worse than none, and unlike a
  missing one it looks authoritative. A centroid anchor belongs with the
  retained scene (R3). Also recorded: in the delivered library these two
  procedures are called ONLY from the two AREA tables, so the point and line
  branches of the shared helper are defensive, not reachable from ENC data.

- 2026-07-27 (E3b): **A block of lookup rows is named in LOWER CASE and is
  therefore unreachable — deliberately left that way.** `depare`, `excnst`,
  `topmar` and the mariner objects (`ownshp`, `vessel`, `clrlin`, `ebline`,
  `leglin`, `pastrk`, `vrmark`) appear alongside their uppercase twins in
  chartsymbols.xml. S-57 object acronyms are uppercase by definition and this
  engine matches exactly, so ENC data never selects them. That is why
  `CS(DEPARE02)` and `CS(TOPMARI1)` never show up in `unhandled_cs()` despite
  being in the file. Matching case-insensitively would make an alternate
  `depare` row selectable ahead of the real ones and move the whole depth
  ramp, so the exact match stands and a test pins it.

- 2026-07-27 (E3b): **Area patterns are not clipped to their area.** ICanvas
  has no clip region, so `PlaceOverArea` emits stamp positions whose CENTRES
  are inside the ring (even-odd, the same rule CpuCanvas fills with, so a
  pattern lands exactly where the solid fill would) and a symbol whose ink
  overruns the boundary bleeds by up to half a symbol. The grid is anchored to
  the canvas origin rather than the ring's own corner, so two adjacent areas
  sharing a pattern line up; it still shifts when the map pans, which a
  geographic anchor would fix at the same time as the retained scene. The fix
  for the bleed is an ICanvas clip rect — one addition, one call site.

- 2026-07-27 (E3b): **A patterned area keeps its boundary pen.** Found in
  review, not by a test: routing `AP` to `area_pattern` instead of a fill
  meant an area whose row is `AP(...);LS(...)` (S-52's MARCUL is exactly that)
  entered the renderer's area branch with no fill, drew the pattern, and
  silently dropped the pen the stroke branch would have drawn. The area branch
  now draws the polygon whenever EITHER a fill or a stroke is present.

- 2026-07-27 (E3c): **the `LookupTableStyleEngine` extraction, and what stayed out of it.** §5.1
  called for a shared core "row table, memoized resolution, palette, symbol cache, placers". Three
  of those five landed; two deliberately did not, and the reasons are the point:
  * **The row table stayed with the products.** GeoSym matches on FACC + delineation and lets EVERY
    row whose ATTEXP condition holds contribute a draw pass; S-52 picks the FIRST matching row of
    one of five tables and executes its instruction list. Neither is a special case of the other,
    so a "shared row table" would be a tagged union pretending to be an abstraction.
  * **The palette stayed too.** GeoSym's is an INDEX into COLOR.TXT run through a brightness/
    contrast adjuster; S-52's is a NAME into one of five day/dusk/night tables. Both reduce to
    "give me an FvColor", which is a one-line interface with nothing behind it worth sharing.
  * The placers were already shared in E3b, and the symbol cache, the rule layer and the label
    switch are what actually moved — plus the open flag, so `IsOpen()` and Style()'s "not open"
    error read identically from either engine.
- 2026-07-27 (E3c): **`AcceptContext` is not a generic hook, it is GeoSym's cutoff keeping its
  position.** SanSymbol/SymText bail below `scale * zoom / 100 == 0.20` BEFORE doing anything else.
  A base class that compiled the rule plan first would still draw nothing, but it would move the
  plan's observable side effects (the memo cache and `rule_predicate_evaluations()`) under a
  cutoff that used to precede them. One virtual, documented at both ends, and a hermetic test
  pins the ordering rather than trusting the comment.
- 2026-07-27 (E3c): **one comparison rule, one implementation.** rules.h has documented since R2
  that a value comparison is numeric only when BOTH sides parse whole and byte-wise otherwise —
  and `fv_s52_style.cpp` then carried its own `WholeNumber`/`ValuesEqual` pair implementing it a
  second time, with a subtly stricter reading (no trailing whitespace). The rule is now callable
  (`RuleValueAsNumber`/`CompareRuleValues`/`RuleValuesEqual`) and S-52 uses it, so a lookup row's
  ATTC condition and a user rule's `where` clause can no longer disagree about what "3" means.
  Both goldens confirm the stricter-to-shared move changed nothing on real data.
- 2026-07-27 (E3c): **`S52StyleEngine::Open` no longer drops the rule set.** The RuleSet and
  ViewingGroupSet used to live in `Impl`, which `Open` replaces wholesale, so reopening a chart
  silently reset the application's rules; GeoSym never did that because its `Open` does not
  replace its Impl. In the shared core they are the ENGINE's state, not the data's, so both
  products keep them across a reopen. Stated rather than silent because it is a real change.

- 2026-07-28 (R3a): **the retained scene is keyed on the whole StyleContext and an EXACT scale,
  not on a scale band.** §5.4 says "keyed by (tile, scale band, style epoch)". Band reuse is
  wrong here and the measurement is not what decides it: both real engines style
  scale-dependently, and `EncVectorSource` does SCAMIN thinning in the SOURCE, so serving a
  scene built at 1:50k for a 1:80k viewport would draw features S-52 says are not there. Scale,
  device DPI and symbol scale are all compared exactly — all three feed `IStyleEngine::Style`.
  Pan is the interaction the cache is for; zoom rebuilds. A band-keyed variant becomes possible
  only when a source can report "my answer does not change between these two scales", which
  nothing in the seam says today.
- 2026-07-28 (R3a): **`IStyleEngine::style_epoch()` defaults to 0, and that is a deliberate
  soft failure.** A cache over a mutable engine needs an invalidation signal, and the choices
  were a pure virtual (every future product and every synthetic test engine must implement it)
  or a default. The default is safe for the two things that actually exist — a fixed table, and
  a test engine — and both REAL products inherit a correct one from `LookupTableStyleEngine`,
  which mixes the RuleSet epoch, the ViewingGroupSet epoch and its own counter with FNV rather
  than adding them (1+2 and 2+1 must not collide, or a rule change cancelling a group change
  would be invisible). The obligation this creates is written at both ends: a product setter
  that changes what `Style()` returns must call `BumpStyleEpoch()`. `S52StyleEngine`'s
  colour-scheme/point-style/area-style setters do, and so does its MUTABLE `mariner()`
  accessor — unconditionally, because a caller that moves the safety contour and did not bump
  would keep drawing the old depth ramp, and one wasted rebuild is the cheaper mistake.
- 2026-07-28 (R3a): **a no-op setter must stay a no-op, or the cache never lands.**
  `GeoSymStyleEngine::SetColorAdjust` bumped on every call, and `PythonView` pushes its current
  brightness/contrast down on EVERY frame — so the scene was thrown away once per redraw and
  the measured win was zero until the setter learned to compare first. Same for
  `SetDrawLabels`. This is the general shape of the risk with an epoch: the bug is silent and
  costs only performance, so the pan-hit test asserts the source was not queried again rather
  than asserting a timing.
- 2026-07-28 (R3a): **the scene margin defaults to 0 because a margin CHANGES WHAT IS DRAWN.**
  Retaining more than the viewport means querying features outside it, and symbology anchored
  just off-canvas legitimately inks into the canvas (the renderer's own ±1e3/1e4 px tolerances
  say so). That is the more correct picture — an edge symbol is half-missing today — but it is
  a change, so it is opt-in: the pinned goldens render at margin 0 and are unmoved, and
  PythonView opts into 0.25. Fixing the edge-symbol case properly means querying a margin
  ALWAYS, which is a golden re-pin and belongs in its own session.
- 2026-07-28 (R3a): **found in passing — `CSymColorAdjuster`'s constructor reads two members
  before assigning them.** `m_bAdjust = (m_nBrightness != 0) || (m_nContrast != 0);` runs before
  either member is set (`SymColors.h`, and the near-identical `fvw_core/Common/SymColors.h`); it
  clearly meant the ARGUMENTS. So a default-constructed adjuster decides whether to run every
  GeoSym colour through its conversion table on indeterminate memory. It is benign in practice
  (the bytes are zero, the goldens are stable) and it is UB in shared source the MSVC product
  build also compiles, so it was NOT fixed inside a perf session. What R3a did instead is refuse
  to depend on it: the new equality guard in `SetColorAdjust` seeds its cached brightness/
  contrast to `INT_MIN`, so the first call always reaches the adjuster even when it asks for
  (0,0) — only a repeated identical set is skipped. Fix tracked separately.

- 2026-07-28 (S1): **INI over JSON, and read-only over read/write.** Two choices worth stating
  because they will look arbitrary later. (1) **INI**, chosen by Chris: the file's whole purpose
  is that a person edits it, standard JSON cannot carry a comment, and the parser is one screen
  against a hand-written or vendored JSON one (expat is in-tree but XML-only). Sections are a
  spelling convenience that flattens to a dotted key — there is no tree, and no schema. (2) **No
  `Save()`.** An app that rewrites its own config file destroys the comments, the ordering and
  any key the running build does not recognise. Application STATE that a user would never
  hand-edit (window geometry, last position) is a different thing and belongs somewhere else;
  this file is preferences only. The consequence, accepted: PythonView's Options dialog still
  changes things for the session only, and the settings file is how you make a change stick.
- 2026-07-28 (S1): **the getters take the caller's default, and there is no defaults table.**
  Every `Get*` takes the value to use when the key is absent, so the default for
  `vector.scene_margin` is written where it is read and cannot drift from a second registry of
  defaults — which is exactly the failure mode the old registry code had (a default in the
  registry writer, another in the reader, and they disagreed). The cost is that an absent key
  and a key set to its default are indistinguishable, which is fine for preferences.
- 2026-07-28 (S1): **a settings file must fail in three different ways, not one.** An absent key
  is silent (normal). A value that will not parse — `0.25px` is the realistic one, and it must
  NOT read as 0.25 — returns the default and is reported through `warnings()`, because aborting
  startup over one typo is worse than running slightly wrong and saying so. A malformed LINE
  fails the load entirely with `file:line`, and leaves whatever was loaded before untouched, so a
  bad edit cannot silently blank every setting. The three are pinned separately in the tests.

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
- [x] **Four more ENC cells ARRIVED 2026-07-28** (Chris, mid-session during E6):
      `US2EC02M` (band 2, General), `US3SC1CB` (band 3, Coastal), `US4SC1BO` and
      `US4SC1CO` (band 4, Approach) in `TestData/enc/`, beside the four band-5
      Charleston harbour cells. They cover the same water at coarser usage bands,
      so `TestData/enc` is now a real multi-band exchange set — which is what
      `BestSeriesForScale` and the R2 scale bands are for, and the enumerator test
      now checks every band present rather than asserting "band 5".
      **They broke 8 tests on arrival**, all of them pinning a total over the whole
      directory (cell_count, layer count, feature counts, the first frame's series).
      Fixed in E6 the way R3b fixed the same class of break: exact counts moved onto
      ONE NAMED CELL, whole-root tests assert structure and lower bounds. **The
      Charleston golden moved for two reasons at once** and both numbers are on
      record so E6's effect stays separable: pre-E6 4 cells `0xbaccb187be81a803` →
      post-E6 4 cells `0x63ec660dc8a13dd8` (the raster symbols) → post-E6 8 cells
      `0x5bfb57105171e60e` (the new cells drawing over the same viewport, which is
      what is pinned).

- [x] **S-52 PresLib + S-57 Appendix A ARRIVED 2026-07-26** (Chris, from OpenCPN). Both
      blockers closed at once, and **E2/Q10 is now unblocked**. NOTE THE PATH: they went
      into **`TestData/enc/`**, beside the Charleston cells — not the `TestData/s52/` this
      entry originally proposed. Use `TestData/enc` as the S-52 data-dir arg. Git-ignored
      via the existing `testdata/` rule; supplied at runtime like `TestData/GeoSymbol`.
      Inventory, verified on arrival rather than assumed:
      - **`chartsymbols.xml`** (2.2 MB): **5 colour tables** (DAY_BRIGHT, DAY_BLACKBACK,
        DAY_WHITEBACK, DUSK, NIGHT — so the day/dusk/night axis is real, not a stub),
        **3057 `<lookup>` rows across the five S-52 lookup tables** (Paper 1335,
        Simplified 756, Symbolized 349, Plain 342, Lines 275 — i.e. both the paper-chart
        and simplified point sets AND both area-fill styles), **1093 symbols**,
        **59 line-styles**, **30 patterns**. That maps 1:1 onto the §5.1 table:
        lookups→rules, line-styles→GeoSym's SAMI, patterns→area fill.
      - **`rastersymbols-{day,dusk,dark}.png`** — the raster symbol sheets the colour
        tables name via `<graphics-file>`. The vector symbol defs are in the XML; these
        are the alternative raster path (and a ready-made symbol atlas for R3).
      - **`s57objectclasses.csv`** (251 rows) + **`s57attributes.csv`** (313 rows) — the
        Appendix A catalogue E1 refused to write from memory. Spot-checked against the
        exact two codes the E1 decision cited as unresolvable: `OBJL 42` → `DEPARE`
        "Depth area", `ATTL 87` → `DRVAL1` "Depth range value 1". objectclasses also
        carries the **`Class` column (G geo / M meta / C cartographic / $ collection)**,
        which is precisely the DSSI record split E1 had to leave to E2.
      - **`s57expectedinput.csv`** (1467 rows) — a bonus nobody asked for and the most
        useful of the three: the *enumerated value* table (code, ID, meaning), i.e.
        S-57's `INT.VDT` equivalent. This is what gives ENC a real `Describe()` in the
        R1 sense, decoding attribute values and not just naming the columns.
- [x] **OSM test data ARRIVED 2026-07-28** (Chris), which is what unblocked Q7/O1:
      `TestData/OSM/` (git-ignored) — **`mbtiles/us-south.mbtiles`** (3.9 GB, Tilemaker
      over the OpenMapTiles schema, z0-14, 798,627 tiles, 16 vector layers, covering
      roughly 24-40.6 N by 106.7-74.7 W, so Atlanta AND the Charleston water every other
      product in TestData already covers), plus the raw material it was cut from:
      `us-south-260728.osm.pbf` (4.1 GB Geofabrik extract) and four small `map*.osm` XML
      extracts. The plan proposed generating a Georgia extract with Planetiler; what
      arrived is a whole US-South cut with Tilemaker, which is better — it is a real
      multi-zoom pyramid rather than a toy, and its coverage overlaps the DTED, DOQ and
      ENC sets, so "OSM over shaded relief" and "OSM beside an ENC harbour chart" are both
      available as demos. **Note for whoever writes O2/O3**: the `.osm.pbf` and the `.osm`
      XML files are NOT read by anything in the port and are not planned to be — the
      pyramid is the deliverable, and reading raw OSM would be a different (and much
      larger) reader. The two facts an O-phase needs from this data are in Decisions:
      the declared `bounds` are wrong, and the tile buffer runs to 53% of a tile.

- [ ] **Follow-ups noted in R2 (2026-07-26), neither blocking:**
      (1) **PythonView has no UI for the rule layer yet** — `pyfvw.vector.RuleSet` /
      `ViewingGroupSet` and `engine.rules()`/`viewing_groups()` are bound and tested, but
      `port/apps/PythonView.py` does not surface them. The natural shape is an Overlays-menu
      display-category picker (Base/Standard/Other) plus a "Load rule file…" item, which is
      what makes scale-dependent authoring reachable to an operator.
      (2) **`TilePackReal.WriteReadRoundTrip` and `.EnumeratedAndRenderedThroughEngine` fail
      under `ctest -j`** and pass serially or with `-R TilePackReal` — they write the same
      scratch `.gpkg` path, so a parallel run has them clobbering each other. Pre-existing
      (predates R2); the fix is a per-test filename. Plain `ctest` is green: 376/376.
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
