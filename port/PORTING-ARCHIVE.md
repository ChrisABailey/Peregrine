# FalconView Port — ARCHIVE (the full historical ledger)

**This is the archive.** The working ledger is `port/PORTING.md` — read that first.
Come here only when the working ledger points you at a specific dated decision, a completed
module row, or a block of the "Condensed out of the working ledger — 2026-08-16" section at the
end of this file (which holds the long-form §1 the ledger used to carry, plus every resolved §2
item, verbatim). Nothing here is a to-do list; every row is finished work.

**Renamed 2026-08-16** — historical rows below still spell the old names: `fvkit-app-plan.md` is
now `port/fvkit-app-plan-COMPLETE.md` and `fvkit-draw-plan.md` is now
`port/fvkit-draw-plan-COMPLETE.md`. Both are finished; read them only for design intent behind a
bug.

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
| O4 | Routable road graph from a RAW OSM extract + bidirectional Dijkstra (`port/Routing/`, `fvgraph`, `pyfvw.routing`, the route overlay's "r") | 4 | T | Eighteenth session of the ACTIVE TRACK, 2026-08-09. **The first thing in the port that ANSWERS A QUESTION about the map instead of drawing it**, and the first module whose input is deliberately NOT the pyramid the rest of OSM reads. `port/Routing/`: `fv_osm_reader.{h,cpp}` (expat `.osm` + protozero/zlib `.osm.pbf`, one `OsmSink`), `fv_road_graph.{h,cpp}` (build + `.fvroad` format + grid index), `fv_router.{h,cpp}`, `tools/fvgraph.cpp`. **(1) MVT IS NOT A GRAPH, and the ledger said so before the session started.** Vector tiles are a rendering product: geometry is simplified, clipped per tile (O3 made the clipping exact, which makes it worse for this) and carries no node identity, so two tiles never agree on where a junction is. The graph is built from the RAW `.osm.pbf`/`.osm`, where a way is a list of node IDs and **a shared node ID is the junction** — no geometric snapping tolerance anywhere in the builder, which is the whole reason the raw extract is worth reading. **(2) Two passes, and that is the memory strategy, not an accident.** Pass 1 reads WAYS ONLY (`OsmSink::WantNodes()` false, so the PBF reader skips the dense-node groups without decoding them) and records which node IDs the kept ways touch, ENDPOINTS COUNTED TWICE so a way end is always a vertex; pass 2 reads NODES ONLY and resolves just those. Never holding every node in the file is what keeps a continent-sized extract inside a laptop — the delivered `us-south-260728.osm.pbf` is 4 GB and **548 million nodes before the first way appears** (PBF sorts all nodes ahead of all ways; measured, not assumed). A node used twice is a junction, a node used once is a shape point, and shape points are KEPT as edge geometry so a route draws along the road rather than as a chord. **(3) One CSR serves both directions of the bidirectional search.** Every undirected edge is stored as two arcs, one in each endpoint's adjacency, and both carry the same two permissions mirrored: at `u` targeting `v`, `kArcForward` = "u->v is legal", `kArcBackward` = "v->u is legal". So the reverse frontier standing at `v` reads `v`'s OWN arcs and tests `kArcBackward` — there is no reverse index to build, keep in sync, or serialize. Geometry is stored once per edge with a `kArcGeomReversed` bit on the twin, and `arc_point()` always yields owner->target order, so concatenating a path never has to know which way an arc was stored. **(4) The de-duplication bug the tests found.** Adjacent OSM API exports OVERLAP — a way crossing the bbox is returned whole, nodes and all, so `TestData/OSM/map*.osm` share ways — and taking a way twice does not just duplicate the edge, it PROMOTES ITS SHAPE POINTS TO JUNCTIONS (they get "used" twice), so the graph silently changed shape with the number of input files. Found by writing "the same file listed twice must produce the same graph" and watching it fail. Fixed with a way-ID set, built only when more than one input is named (a single OSM file cannot contain a way twice). The four Kiawah exports went 4,747 -> 1,831 edges and 223.7 -> 201.2 km of centreline with the route unchanged at 12.64 km. **(5) `access=private` is data, not a defect.** The delivered exports are **Kiawah Island, a gated resort**, so honouring `access` cuts the driving graph into 107 components (largest 74%) and most random pairs are genuinely unroutable; `--ignore-access` gives 83 components and 87%. Both builds are pinned; the router tests use the permissive one and say why. `fvgraph info` reports the component histogram for exactly this reason — a graph whose largest component is a small fraction of it is a build that failed to node something, and now you can tell the two apart. **(6) Bidirectional Dijkstra with the unidirectional one kept as the ORACLE.** Both searches share one relaxation and one cost function; `RouteOptions::bidirectional=false` selects the simple one, and `Router.BidirectionalMatchesDijkstraEverywhere` runs 200 real node pairs through both and requires identical cost, identical node count and identical reachability — plus strictly fewer total expansions, which is the only reason the two-frontier version exists. No contraction hierarchies: at region scale plain bidirectional answers in single-digit milliseconds and a CH would add a preprocessing stage and an ordering heuristic to maintain for nothing visible. Cost is TRAVEL TIME by default (`maxspeed` parsed, `"55 mph"` converted, class defaults otherwise), `metric="distance"` switches; `EachMetricMinimisesItsOwnQuantity` pins that neither can beat the other at its own game. **(7) The `.fvroad` file** is explicit little-endian scalars, never a memcpy'd struct (it is a build artifact that may cross machines and a struct's padding is nobody's contract), coordinates as degrees x 1e7 in int32 — OSM's own precision, exact — and **`Load` validates every index before the router can dereference it**: monotonic CSR, arc targets, geometry ranges, name indices, and header counts against the file size before anything reserves memory on their say-so. **(8) `fvgraph`** (the `fvpack` pattern): `build` / `info` / `route`, the last with `--geojson` so a route can be looked at. **(9) Through to the app.** `pyfvw.routing` (RoadGraph build/load/save/nearest_node, Router.route -> Route with `geometry` as GeoPoints, named legs, snap offsets), and `port/apps/route.py` gains **"r" — follow the roads**: each consecutive waypoint pair is routed SEPARATELY, so one waypoint nowhere near a road leaves that leg straight and says so rather than throwing the whole route away, Esc goes back to straight legs, and `[routing] graph` names the `.fvroad`. **Tests**: 38 gtests + 7 pytests (**727 total**), all green. The PBF wire format is pinned against a fixture the test file **encodes itself with its own varint writer** — a deliberate second implementation, so a shared misunderstanding of zigzag or delta encoding cannot cancel out — parameterised over raw and zlib blobs, plus a bounded read of the real 4 GB extract. **NOT here**: turn restrictions (`type=restriction` relations are not read at all — the ledger's "when planned" note stands), a walking/cycling profile beyond the `driving=False` switch, and reusable router scratch (every query allocates six arrays of `node_count`, which is ~34 bytes/node — fine for a state, not for a continent; noted in section 2b). |
| O4b | Bicycle profile + per-mode access: three access bits on the arc, `IsCycleable`, flat non-driving speeds, `--cycle-only`/`--cycle`, the app's "b" | 2 | T | Nineteenth session of the ACTIVE TRACK, 2026-08-10. The half of O5 that does not need relations, closed on its own. **(1) `access` is a DEFAULT, not the last word, and Kiawah is the fixture that proves it.** O4 read one generic `access` tag and dropped the way outright if it said `no`/`private`; the delivered exports are a gated resort whose whole leisure-trail network is `access=private` + `bicycle=yes` — private to drive, explicitly open to ride — so that reading deleted **every trail on the island** from a bicycle route. Access is now resolved per mode, most-specific key first (`bicycle` → `vehicle` → `access`; `foot` → `access`; `motor_vehicle` → `vehicle` → `access`), and **`vehicle` is bicycle AND motor_vehicle in OSM's hierarchy** — it sits between the mode key and the generic one for both wheeled modes and is absent from foot's chain entirely, which is what lets a `vehicle=no` boardwalk stay walkable. A way barred to *every* mode is still dropped; one barred to only some is KEPT. **(2) The permissions live on the ARC, not in a build-time filter**, as three new `ArcFlags` bits (`kArcNoBicycle`/`kArcNoFoot`/`kArcNoMotorVehicle`, negated so a cleared bit means "nothing said"). That is what lets ONE general graph answer a driving, a walking and a bicycle query, and it needs **no `.fvroad` format bump**: `flags` is the same `uint8_t` it always was, so a graph written by O4 loads with the new bits clear and behaves exactly as it did. Access is a property of the road, so the mirrored twin arc carries the same bits (unlike `kArcForward`/`kArcBackward`, which swap). **(3) A bike and a pair of legs do not care what the street is posted at.** `travel_seconds()` is `length / maxspeed` and is the DRIVING clock; the non-driving profiles are timed at flat speeds (bike 15 km/h, walk 5 km/h) and — the part that was actually wrong before — that flat speed feeds **both the search cost and the duration `Materialize` reports**, so a walking route no longer claims to cover a residential street at 35 km/h. `arc.speed_kph` is left alone; it is the road's property, not the traveller's. **(4) Class preference is a weight on the cost, never on the clock.** `ArcCost` scales bike seconds by ×0.8 (cycleway/path), ×0.85 (residential/living street), ×1.5 (footway/pedestrian — walking the bike), and `Materialize` reports the UNSCALED seconds, so a preference cannot lie about the arrival time. `metric` has no say under `cycle_only`: with a flat speed, distance and time differ by a constant and would rank identically, so the bike query is always this weighted time. **(5) The filter belongs to the QUERY, and `--cycle-only` on the build is only a size optimisation.** `RouteOptions::cycle_only` filters at search time and `IsCycleable` is the single definition both sides use, so a bike route on a general graph and on a cycle-only graph are the same route — pinned by `CycleOnlyRoutesTheSameOnAGeneralAndOnACycleOnlyGraph`, which also asserts the cycle-only build really is smaller. `cycle_only` overrides the driving filter on BOTH sides (a bike graph that then applied `--drive-only` would discard the cycleways it exists to hold). **Tests**: 5 new gtests (**732 total**), green. **NOT here**, and still O5: `type=restriction` relations (neither reader emits relations at all), ordered through-waypoints, avoid-ferry/avoid-toll. |
| O5a | Turn restrictions: relations out of both OSM readers, `type=restriction` resolved onto arcs, and a search that splits the junctions they name | 3 | T | Twentieth session of the ACTIVE TRACK, 2026-08-10. The one correctness gap O4 left in the driving profile: a route could turn left where a sign forbids it. **(1) Neither reader emitted relations at all**, so this starts in the wire format. `OsmRelation`/`OsmMember` + `OsmSink::WantRelations()`/`Relation()`, in the expat reader (a `<tag>` inside a `<relation>` must not land on the way that came before it — the two element bodies are identical to a handler tracking one flag) and in the PBF one (`Relation` = field 4 of a primitive group; `memids` are **delta-encoded across ALL members whatever their type**, and `roles_sid`/`types` are parallel arrays, so the fixture encodes a way→node→way chain by hand to pin exactly that). `WantRelations()` defaults **false**, unlike its two siblings: relations are new, almost no sink wants them, and off is exactly the old behaviour. **(2) A restriction names WAYS; the graph knows ARCS, and the join is the via node's own adjacency.** A `TurnRestriction` is `(via_node, from_arc, to_arc, kind)` where both arcs belong to the via node — `from_arc` is the arc facing back the way the driver came in, which is precisely the identity of "how did I arrive". The builder keeps a build-time-only `arc_way` join (not persisted) to resolve them. A way that runs THROUGH the via node contributes two arcs and the relation does not say which side the sign is on, so every pair is taken; `from_way == to_way` (a U-turn) keeps only the pairs where the two arcs are equal, which is what stops a through-road being severed. **(3) The load-bearing design decision: a Dijkstra label per NODE cannot express a turn restriction.** The cheapest way to a junction may be exactly the approach the sign forbids, and the second-cheapest is the one that gets through — so the router labels **states**, and a restricted via node is split into one state per arc it can be entered along plus one for "arrived along nothing". Every other node stays a single state whose id IS the node id, so a graph with no restrictions (or a walking query on one that has them) searches exactly the space O4 searched, at exactly the O4 cost. The split states are numbered in `Finalize` and hang off two small hash maps, not a node_count-sized array: restricted junctions are a fraction of a percent of any extract. **(4) The two frontiers label the same junction differently** — forward by the arc it came in on, reverse by the arc it will go out on — so a meeting is a PAIR of states and only joins if the turn between those two arcs is legal. Away from a restriction there is one state each and it reduces to O4's `df[v] + dr[v]`. This is the part real data barely exercises (Kiawah carries five restrictions among 1564 nodes), so the test that earns its keep saturates the graph with 50+ synthetic ones and holds bidirectional against the unidirectional oracle over 300 pairs — a mutation removing the turn check from the bidirectional relaxation fails it. **(5) Restrictions bind a CAR only.** They are motor-vehicle signage; the walking and cycling profiles ignore them, and the builder drops any relation whose `except` list covers `motorcar`/`motor_vehicle`/`vehicle`. **(6) `.fvroad` goes to version 2** (a restriction count in the header, a 16-byte record each); **version 1 still loads** and simply has no restrictions — it is a build artifact, but so is the hour it takes to rebuild one from a continent. **Preserved/declared limits**: `via way` restrictions (a few per cent, mostly divided-highway U-turns) are recognised, counted in the build stats and **NOT applied** — applying one needs more history than "the arc I arrived on"; `restriction:hgv` and friends are lorry signage and are not read; with parallel edges between the same pair, `ArcBetween` takes the first, the same arbitrary choice OSM leaves open. Reading relations must not change the road network, and `honor_turn_restrictions=false` on the build is pinned to produce an identical node/arc count. `Route::nodes` can now repeat a node — an `only_straight_on` forces the route back through the junction it just left, and that is a legal route, not a loop. **Tests**: 18 new gtests (**750 total**), green, and ASan/UBSan clean. **NOT here**, and still O5: via-way restrictions, ordered through-waypoints, avoid-ferry/avoid-toll, a cost for steps and any elevation term. |
| O5b | `access=private` priced instead of deleted, and the build's access stats made visible | 2 | T | Twenty-first session of the ACTIVE TRACK, 2026-08-10, and a straight defect fix on the graph O4b/O5a built. **The bug**: `ParseAccess` folded `private` into `no`, so `access=private` denied every mode at once (each mode key chain falls through to the generic `access`), and O4b's "nobody at all may travel it: not worth a node" rule then deleted the way. On Kiawah — a gated island — that was **51 ways / 340 driveable arcs**, i.e. most of the street network, and driving had no connected graph at all. The diagnostic that mattered: **zero** arcs carried `kArcNoMotorVehicle` afterwards, because the roads were not marked unusable, they were absent. **The fix is a third answer, not a third build option.** `Access::kPrivate` is now its own value and gets its own three arc bits (`kArcPrivateBicycle/Foot/MotorVehicle`); only an outright `no` counts toward the all-modes deletion test. The router prices a private arc by `RouteOptions::private_penalty` (default **5x**, per mode — a bike does not pay the car's gate), which is a PREFERENCE not a clock: `Route::seconds`/`length_m` stay unweighted, the same rule the bicycle class weights follow. A very large penalty approaches "only if there is no other way" and is still not deletion — an address behind the gate stays reachable, which is the whole point. **Per-mode `no` keeps biting**: `access=private` + `bicycle=no` still refuses a bike, which is exactly what `--ignore-access` threw away and why it was never the fix. **File format did NOT bump**: `flags` widened to 16 bits and the extra byte went into the arc word's spare top byte, so an O4/O5 file reads the new bits clear = "nothing said", the same read-old-as-silent rule the access bits themselves got. **Visibility** (the other half of the ledger item): `ways_dropped_access` + six per-mode arc counts on the build stats, printed by `fvgraph build` AND recomputed by `fvgraph info` off the finished file — the app's graph is the file, not somebody's build log. Kiawah now measures 899 ways kept / 11 dropped, 256 private car arcs, 60 barred car arcs, 2362 driveable arcs a car may use, largest component 87% of nodes. `TestData/OSM/kiawah.fvroad` **rebuilt honouring access** (the `--ignore-access` stopgap is gone) and `kiawah_cycle.fvroad` deleted as redundant. **Preserved/declared**: the router's own oracle fixture still builds `--ignore-access` on purpose — its invariants ("the distance metric minimises metres", "a restriction never speeds the clock up") are only true where cost IS the reported quantity, and a penalty is a weight; private behaviour is tested on the strict graph instead, including bidirectional-vs-Dijkstra agreement under the penalty. `destination` is still read as plain allowed. **Tests**: 6 new/rewritten gtests (**756 total**), green. |
| O5d | Ordered stops: one route THROUGH the waypoints instead of a string of independent pairs | 2 | T | Twenty-third session of the ACTIVE TRACK, 2026-08-11, and the first item on §2a's "what is left" list. **The stops are fixed and ordered, so per-pair search really is optimal** — routing A→B→C cannot be improved by considering the pairs together, and that is worth saying because it is why there is no new search here. What is NOT independent is the **state at the stop**: what the route arrives along constrains what it may leave along, and two separate searches share nothing. So the whole change is a seed. `Router::RouteVia`/`RouteNodesVia` chain the legs, seeding each with the arc the previous one arrived along (`LegSeed`), and O5a's turn machinery then binds signage **at** a stop for nothing — the crossroads fixture's no-left-turn is obeyed by a route that stops on the junction, where two independent pairs drove straight through it. **(1) The U-turn is expressed as a barred TURN, not as a rule about stops.** `StateSpace` gained one `BannedTurn` consulted inside `Allowed()`, so the forward frontier, the reverse frontier and the meeting test all obey it without any of them being told what a stop is — which matters because a plain filter on the first relaxation is *incorrect* in a bidirectional search: rejecting the meeting does not remove the reverse LABEL at the start node, and Dijkstra keeps only the best one. For the same reason the leg's start node is **force-split** (an extra state block numbered above every other, `extra_base_`) when and only when a leg carries something over — an unrestricted graph, and every two-point route, searches exactly the state space O5c searched. **(2) The preference yields.** A stop at the end of a cul-de-sac has no other way out, so a leg that cannot be routed with the U-turn barred is retried with it allowed and the stop reported in `Route::u_turn_stops` — visible, not silent. `allow_u_turn_at_stops` turns the preference off; signage is not a preference and always binds. Measured on Kiawah: a via point that lands on Treeduck Court costs **110 m** to obey (16.29 vs 16.18 km), which is the shape wanted. **(3) The bug the oracle caught, and it was mine.** `ArrivalArc` first took the last step's own arc when the step ran against stored order (exact, and free) and `ArcBetween` otherwise. On parallel arcs the two differ, so the seed depended on **which search had run** — the bidirectional and unidirectional answers disagreed by 50 seconds over an identical node sequence: same road, the other carriageway. Now it is `ArcBetween` always, matching the rule both searches already label a state by. A parallel-arc pair is therefore approximated, deliberately: one rule for "the arc I arrived along", used in all three places that ask. **(4) All or nothing**, with `unreachable_leg` naming the pair that has no route and a `kOutOfCoverage` that names which stop is off the network — a half-delivered through route is not one. The app keeps its honest degradation by falling back to the old per-pair loop (now `_follow_roads_per_pair`, unchanged) and **saying which answer is on screen**. **Surface**: `Route::{stop_nodes, stop_offsets_m, stop_geometry_index, u_turn_stops, unreachable_leg}`, `Router.route_via` in pyfvw, `fvgraph route --via LAT LON` (repeatable) `--u-turns`. **Tests**: 12 new gtests + 3 pytest (**785 total**), including two that exist to stop the seam drifting — two stops must come back node-for-node identical to `route()`, and both searches must agree on a multi-stop Kiawah route. **A fixture lesson worth keeping**: the first U-turn fixture hung its dead-end spur off the STOP, and the router escaped by driving to the dead end, turning round *there*, and coming back through the stop having made two legal turns — correct behaviour under a per-turn ban, and a useless test. The spur moved to the next junction along. **Not done**: `Route::u_turn_stops` and `allow_u_turn_at_stops` have no UI (§2c). |
| E7 | S-52 symbology fidelity: the library's own 0.32 mm grid, the light flare's bearing, and soundings set as digit symbols with a decimetre subscript (2026-08-11, per Chris) | 4 | T | Twenty-second session of the ACTIVE TRACK, and the second time the port's ENC output was read against the delivered library rather than against itself — E5 was the first. Chris reported three things off one Charleston screenshot: area/line symbols "much bigger than the buoy symbols", light flares at the wrong angle ("rotated by about 120 deg to the right"), and soundings not set with the decimal as a subscript. **All three had one witness: chartsymbols.xml states 316 of its symbols TWICE**, once as a `<vector>` box in 0.01 mm and once as a `<bitmap>` box in tile pixels, and the two forms disagreeing is what every defect here was. **(1) The nominal symbol pixel is a PRODUCT's number, not the renderer's.** `IStyleEngine::himetric_per_symbol_pixel()`, default 25.4 (FalconView's `s_dblConversionFactor`, 1/100 inch, preserved for GeoSym under the bit-faithful rule), **32 for S-52** — 0.32 mm, the presentation library's own display pixel and the unit its HPGL `SWn` widths count in. Measured, not assumed: the median of the 632 vector/bitmap box ratios is **32.11**, the mode exactly 32. At 25.4 a display list drew **26% larger than the TILE of the same symbol**, and since the buoys and beacons are tiles (E6) while the restriction and anchoring marks are display lists, a harbour's furniture came out in two sizes. The renderer now asks the engine; the S-52 engine's `PxPerHimetric` — which also sets an `AP` pattern's pitch and an `LC` line's period, both measured in the same pixels — moves with it. **This does NOT close the DPI item in §2b**: neither form honours the device pitch, and that is still open. **(2) A rotation is a BEARING at the S-52 seam and CCW on screen inside the renderer.** `DrawSymbolAt` is `x' = x cos r - y sin r` over a y-up symbol space drawn into a y-down device, verbatim from `CCGMSymbol::DrawSymbol` (SanSymbol.cpp:890) — so a positive angle turns a symbol COUNTER-clockwise, while S-52 writes every rotation as degrees clockwise from north. The shared renderer keeps FalconView's sense; the SY handler negates. That fixes more than the flare: `SY(RECTRC57,ORIENT)`, `SY(TSSLPT51,ORIENT)`, `SY(CURENT01,ORIENT)` and the rest of the ORIENT family had been pointing at their own mirror image about north. **The flare's own angle is 135 degrees**, and the library says so in its raster copy: LIGHTS11/12/13's bitmap pivot is the tile corner with the bulb down and to the right — ink centroid at bearing **133.5**, farthest lit pixel at **131.6**, both 135 to within a 22x22 tile's rounding. Unrotated it grew out of the top of the buoy it belonged to. **(3) Soundings are SNDFRM02, not a text run.** The old `TX(DEPTH,...)` was E3a's stated deviation ("the digit symbols are raster-only, so there is no display list to stamp") and E6 removed the reason. S-52 names a digit `SOUND` + family + position + digit, and **the delivered BITMAP PIVOTS are the layout**: pivot x 19, 12, 5, -2, -9 for positions 3, 2, 1, 0, 4 is a contiguous left-to-right row on a 7 px grid (a pivot is subtracted from the anchor, so a larger one sits further left), and position 5 repeats position 0's column with the pivot 4 px higher — the same slot dropped half a line, which IS the subscript. Composing the tiles by their own pivots draws `9₄`, `12₆`, `35`, `127`, which is how the reading was settled; the whole-metre rule (decimetre kept under 31 m, whole metres above) is S-52's. The family is the emphasis: `SOUNDS*` black at or below the mariner's safety depth, `SOUNDG*` grey deeper — the same split the old CHBLK/CHGRD text made. `VALSOU` on a wreck or obstruction goes through the same call, so a hazard's depth and the sounding beside it are one typography instead of two. **(4) E5's pivot-sanity rule had to learn one exception, and finding it took the render.** The digits came out stacked on top of each other: a sounding digit is a 6x10 tile whose pivot IS its slot, so four of the six positions lie outside "a quarter of the tile" and E5's clamp re-centred them all — a rule that cannot tell a deliberate 19 px offset on a 6 px tile from the `INT_MIN` garbage it was written for. Exempted by name (`SOUND*`), which is narrow and says why. **Tests**: 5 new/rewritten gtests — the two families and the safety-depth split, the digit layout including the drying-height and >=31 m cases, the flare's -135, the 32 grid checked back against four dual-form symbols' own boxes, and the sounding pivots pinned slot by slot. **NOT here**: the drying-height bar (`SOUNDSA1`), the swept-sounding bar and the low-accuracy mark, all one symbol each from the same family; sector lights are still a plain flare (E3b's standing deviation). **The ENC RENDER goldens did not run** — see §2d: `TestData/enc` grew from 8 cells to 823 on 2026-08-11 and one of them (`US5CT1FV.000`) fails ISO 8211, so every `S52Render` test fails at `Open()` before it draws. That predates this session and is Chris's call; verification here was the S52Style/S52PresLib suites (88 green) plus rendered PNGs read by eye. |
| E8 | ENC labels: text lifted into a band above all geometry, and the four TX/TE placement parameters that were being skipped (2026-08-12, per Chris) | 3 | T | Twenty-fourth session of the ACTIVE TRACK. Chris reported "text covered by land regions" on ENC and asked whether ENC/OSM define label sizes and orientation. Two defects, one non-defect, and the third question is mostly a yes. **(1) S-52 gives the display priority to the LOOKUP, and the whole instruction chain inherits it — text included.** `StyleFeature` emitted TX/TE into the same `StyleResultBuilder` as the row's geometry, and `VectorScene` has exactly one ordering pass (a stable sort across every feature by `StyleResult::priority`), so a label was drawn at its object's band and everything above it painted over the name. The delivered library makes that certain rather than theoretical: counted over `chartsymbols.xml`, text-bearing rows sit at **every priority there is**, and `LNDARE`'s own Plain row is `AC(LANDA);TX(OBJNAM,...)` at **Group 1** — a land name authored into the skin of the earth, under four later bands. Fix: a label is now its own `StyleResult` at `kS52PrioTextBase (16) + the object's priority`, so text is above all geometry while keeping the library's relative order AMONG labels (a buoy's name is Hazards, a land area's is Group 1, and they still stack that way). The base leaves a gap above `kS52PrioMariners` because an own-ship/AIS layer is the one thing that should eventually draw over text. **A rule that overrides the priority is obeyed literally** and keeps its text with its geometry — an override says where the caller wants the object, and quietly lifting half of it out would mean something the caller did not write. **(2) HJUST, VJUST, XOFFS and YOFFS were parsed positions the loader stepped over**, so every ENC label drew baseline-left exactly on its anchor — a name sitting on the symbol it belongs beside. Measured over the library: every TX/TE sets HJUST/VJUST, and only 62 of ~500 have a zero offset (`(+1,-1)` and `(-1,-1)` alone are 261). **The decode is the data's own, not recalled from the spec**: `SEAARE` labels an area with `(1,2)` and no offset — an area name centred on its own centroid, which fixes 1 and 2 as "centre"; `BOYLAT` offsets LEFT at HJUST 2 and the 127-row majority offsets RIGHT at HJUST 3, so 2 is right-justified and 3 is left. Offsets are in units of the text's own BODY SIZE (so they scale with the symbology, or a magnified chart keeps its text the same distance from a symbol that grew) with y positive DOWN, the same sense as the library's bitmap pivots (E7). **New at the seam**: `LabelStyle::halign`/`valign` (`LabelHAlign`/`LabelVAlign` in `style.h`), applied by the renderer in the kPoint branch. `kLeft`/`kBaseline` are the defaults and are exactly what the canvas already did, so **GeoSym and OSM do not move** — both pre-compute a dx/dy and say nothing about alignment. Alignment costs one `GetTextExtent`, and it is not measured unless a product asks for it. The vertical box is modelled as `baseline-height .. baseline`, the approximation the pick box has always made; splitting it properly needs ascent/descent, which `ICanvas` does not expose and every implementor (including pyfvw's Python subclasses) would have to grow. **(3) OSM layer order is NOT a defect.** `priority` is the declared style-layer index, globally stable-sorted, which is the GL contract. The two styles on disk simply disagree: `peregrine-osm.json` puts `road-path` at 17, after every road (12-16), so paths draw OVER roads; the untracked `styles/style.json` is **CyclOSM**, where `road_path` is 18 and the roads are 20/26/27/28, so roads draw over paths. Chris's sighting matches CyclOSM, faithfully reproduced. **Sizes and orientation, since it was asked**: ENC parses the TX/TE `CHARS` body size (verified against all 11 distinct values in the library, including the 4-character `'1508'` that the last-two-digits rule has to get right) and is always horizontal, which is correct for a chart; OSM handles `text-size` with zoom stops, `text-field`, `symbol-placement` point|line, `symbol-spacing`, `text-max-angle`, `text-offset` and `text-color`, with along-path orientation from the tangent (T1). **Tests**: 6 new gtests (**791 total**). A counting trap worth one line, since this session fell into it: `ctest -N` reports **808** because it also lists the 17 disabled GeoTrans tests, and the ledger's running number has always been what ctest RUNS — 785 + 6 = 791, and the "785" looked stale for exactly as long as it took to compare the two commands. Five in `s52_style_test` pin the text band, the relative order among labels, the justification/offset decode on two rows that can only mean one thing, the body-size scaling, and the rule-override carve-out; one in `vector_renderer_test` pins the alignment mechanics directionally (which side of the anchor the ink landed) including that the defaults do not move. **The ENC render golden did not move and could not**: labels are OFF in it deliberately, since glyphs come from the host font and would make the hash machine-dependent. Verification was therefore the suites plus a rendered Charleston harbour PNG at 1:30,000 read by eye, before and after. **What the render then made obvious**: with the text no longer being buried, ENC's share of §2b's "no label collision or de-duplication" is now the most visible thing on the chart — "Shutes Folly Island" draws three times and "James Island" four, one per overlapping cell. That is the existing gap surfacing, not a new one, and part of it is this script opening all 8 cells across 4 usage bands at once where an ECDIS shows one band. **NOT here**: the S-52 `SPACE` parameter (character spacing), the `DISPLAY` parameter, and the body size still being treated as pixels rather than points — the same DPI item E7 left open in §2b. |
| O5e | Avoiding tolls and ferries — and the ferry turning out to be a data gap, not a preference gap (2026-08-12) | 2 | T | Twenty-sixth session of the ACTIVE TRACK, and the last item on §2a's O5 list. **The ferry was not a preference problem, it was a MISSING-DATA problem.** `route=ferry` carries no `highway` tag, so `WayPass::Way` returned on the first line and no ferry had ever been in the graph — "avoid ferries" had nothing to avoid. **(1) A ferry is a CLASS, a toll is a BIT, and the asymmetry is the point.** `RoadClass::kFerry` (appended, so a pre-O5e `.fvroad` holds none and reads as it always did) because everything a class decides differs on a boat: who may board, whether a profile may use it, and above all the speed. `kArcToll` (bit 9, still inside the arc word's spare byte) because a tolled motorway is still a motorway and must keep a motorway's weight — a class would have thrown that away. **(2) The crossing's clock is its own `duration`, and it is resolved late.** A boat has no `maxspeed`; `duration` over the way's length is the speed, and the length is only known after the geometry resolves, so the tag is carried on `KeptWay` and the way's edges are rewritten in one pass once it is cut — one speed for all of them, because the boat does not go faster in the middle. `ProfileSeconds` then returns that speed **whatever profile is asking**: you do not walk a ferry. The tag's trap is that its shortest form is MINUTES, so "20" and "0:20" are the same crossing. **(3) The knob is a number, and it is the first setting the QUERY outranks the profile on.** `RouteOptions::{toll_penalty, ferry_penalty}` follow O5b's price-don't-delete shape, with `kAvoidExcluded` (-1, spelled like `kClassExcluded`) available because "I cannot pay" and "I will not board" are real and a large multiplier is not the same statement. Every other profile-backed field is read from the profile; these two are read from the OPTIONS, so `SelectProfile` seeds them and the caller's own override lands afterwards — "this profile but no ferries today" needs no profile per combination, and both the CLI and the binding apply theirs after the profile deliberately. Excluding a ferry can leave an island unreachable; that is the answer, and a test pins it. **(4) The bug review caught, and it had been latent since O5c.** `Router::ArcCost`'s profile branch called `RouteProfile::Seconds` directly rather than `ProfileSeconds` — identical for every class until O5e gave one its own clock, and then the search costed a crossing at the profile's flat walking speed (10440 s) while `Materialize` reported the boat's (2700 s), sending a walker over a bridge that is actually slower. One call site; the lesson is the standing rule now in §3. The test written for it is deliberately one where the wrong cost changes the ROUTE CHOSEN — the first version only checked the reported seconds and **passed under the bug**, because the bug was never in what was reported. **Surface**: `RoadClassFromName` (a rule file weights "ferry" by name; `RoadClassFromHighwayTag` still never yields it), `RoadGraphBuildOptions::include_ferries`, four new build stats, `toll_penalty`/`ferry_penalty` in the JSON rule file with the same number/`false`/`"exclude"` grammar a class weight has, `car_no_tolls` and `car_no_ferries` replacing the `car_no_tolls_placeholder` that had been waiting for the toll bit since O5c, `fvgraph build --no-ferries`, `fvgraph route --avoid-toll/--avoid-ferry/--toll-penalty X/--ferry-penalty X` (which now REFUSE a zero or unparsable value rather than silently reading it as "not given"), a ferry/toll line on both `build` and `info`, and the two penalties on `Router.route`/`route_via` in pyfvw taking a number, `False` or `"exclude"`. **Tests**: 17 new gtests + 6 pytest (**829 total**). **The fixture is synthetic and had to be**: a bay with a tolled bridge (7 min), a ferry calling at an island (45 min) and a long road round (62 min), so which crossing comes back says exactly what the query refused. Kiawah carries **zero** ferry ways and **zero** toll tags, which is logged in §2b as the one thing this session could not check against real data. **Not done**: no UI (§2c), and a ferry's timetable is unmodelled — the crossing costs its duration, never the wait for the next sailing. |
| M1 | Mariner depth settings shared by DNC and ENC, and a data-family file that switches groups of vector features off (2026-08-12, per Chris) | 2 | T | Twenty-fifth session of the ACTIVE TRACK. Two asks, both settings-file-shaped and neither wanting a GUI. **(1) The DNC half of the mariner API**, the last open piece of R2/Q6c-5. ENC had `S52MarinerSettings` on its own engine since E3a; DNC's depth ramp was still driven by `CECDISValues`' default-constructed knobs, so a vessel's draft could not be entered at all. The two products turn out to be **the same six values under different names**, and the mapping was READ OFF THE DELIVERED TABLES rather than recalled from a spec — `attexp.txt` row 2257 is `idsm = 0 and cvl >= ssdc and cvl < msdc` and `fullsym.txt` 2257 is BE010 area symbol 0820, "depth area (medium deep); 4 shades". Over the whole BE010 block (rows 2256..2270): `safety_contour` = **ssdc**, `deep_contour` = **msdc**, `shallow_contour` = **mssc**, `two_shades` = **idsm** (1 = two shades, 0 = four, spelled out in the row comments), `shallow_pattern` = **isdm** ("shallow display mode on" adds area symbol 0949 over the shallow bands, rows 2265..2270 plus foreshore/reef at 1441/2164). DNC has **no `safety_depth`**: `ssdc` is both the contour and the sounding threshold, which BE020 rows 2318/2319 (`hdp <= ssdc` dark, `hdp > ssdc` light) settle. New `port/include/fvkit/vector/mariner.h` holds `fv::MarinerSettings`; it lives on the shared `LookupTableStyleEngine`, NOT on `StyleContext` as §2b guessed — the settings belong to the engine, the epoch machinery for invalidating a retained scene is already there, and `StyleContext` is rebuilt per frame by the renderer, which knows nothing about depth. `S52MarinerSettings` is now an alias, so no ENC caller moved and every ENC golden is unchanged. **DEFAULTS DIFFER PER PRODUCT AND MUST**: the struct's are S-52's (30/2/30/30, four shades, no pattern) and GeoSym seeds `CECDISValues`' in its constructor (10/2/30, four shades, **pattern ON**), because those are what the DNC goldens were pinned over. GeoSym re-derives its `CECDISValues` when the mariner epoch moves — one integer compare per feature, five assignments per actual change — because a caller holding the mutable reference can move a contour at any time. **One API wart fixed on the way**: the accessor pair was `mariner()` / `const mariner()`, and inside a non-const member the overload set silently picks the BUMPING one, so a product reading its own safety contour per feature would rebuild the retained scene per feature. Split into `mariner()` (const, free) and `mutable_mariner()` (bumps on call, the E3a contract); `SetMariner()` bumps only when something moved. It cost two failing tests to notice, which is why both names now say what they do. **(2) Data families** (`fvkit/vector/families.h`, `FamilySet`): a NAME over a list of **rule-file selectors**, with an `enabled` flag, loaded from JSON. Deliberately not a fourth filter — switching a family off emits `hide <selector>` into the engine's `RuleSet`, so nothing new evaluates at draw time, a selector is as expressive as a hand-written rule (`layer=soundp where hdp > 30` is a legal family), and a product the port has never seen can be grouped without a code change. Selectors are validated at LOAD by parsing them through the rule parser, so a typo is an error when the file is read rather than a family that silently hides nothing; loading is all-or-nothing and comments are enabled, which answers S1's one objection to JSON. Families load FIRST and a user rule file second, so a rule can put one thing back. **Three starter files ship in `port/families/`**, every family ON so loading one changes nothing that is drawn: **DNC by VPF COVERAGE** (the union of feature classes over the dnc17 libraries — and Chris's recollection was right, `nav`'s buoys/beacons/lights are a different coverage from `hyd`'s bottom characteristics, so the file splits `hyd` further into `depths` and `bottom`), **ENC by S-57 object class** (acronyms from the delivered `s57objectclasses.csv`; S-52 carries display categories but not viewing groups, so this file is where that axis lives for ENC), **OSM by MVT source layer** (the 17 in `us-south.mbtiles`). **Wiring, no GUI**: `[mariner]` keys (per-key, so an unset one keeps the PRODUCT's default rather than the other product's) and `[vector] families_{dnc,enc,osm}` paths, applied in `PythonView._apply_mariner` / `_apply_families`; both bound in pyfvw (`vector.MarinerSettings` with `S52MarinerSettings` kept as an alias, `GeoSymStyleEngine.mariner()/set_mariner()`, `vector.FamilySet`, `vector.FeatureFamily`); every key documented in `peregrine.ini.sample`. **Tests**: 20 new (**811 total**) — 6 gtests over the real GeoSym tables (the DNC defaults, the safety contour moving a 15 m depth area into the shallow band and back, two-shade collapse, the 0949 overlay appearing and going, soundings darkening at ssdc, the no-op-setter epoch rule), 14 hermetic family tests including one that loads the three shipped files and asserts they are neutral, plus 4 pyfvw tests. The five failing Osm tests are the pre-existing §2d ones. |
| T2 | Halo (outlined) text on vector labels, the way Windows FalconView drew it (2026-08-12, per Chris) | 2 | T | Twenty-seventh session of the ACTIVE TRACK, and the §2b item T1 and E8 had each made more visible. **Chris named the method**: the Windows code draws the string four times, shifted right/left/up/down by the halo width, then the text over it — "may not be pixel perfect in all cases but it is generally acceptable and fast/easy". Implemented as stated, and the two judgement calls are about WHERE it lives and what happens past one pixel. **(1) It is a `LabelStyle` field, not a `TextStyle` one, so `ICanvas` did not change.** §2b had guessed the work was "one pass in `CpuCanvas::DrawRotatedTextString` plus a colour/width on `TextStyle`" — true of a real coverage dilation, and wrong for a stamped one: a stamped halo needs nothing from the canvas at all, so putting it on the RENDERER gives it to every backend at once, including pyfvw's Python `ICanvas` subclasses, with no new virtual for them to grow and no half of the seam where `TextStyle` carries a field the canvas silently ignores. `LabelStyle::halo_width` (device pixels, 0 = none, the default) + `halo_color`; `VectorRenderer` draws the stamps, in screen space, before the fill pass. **(2) Four stamps at r = 1, EIGHT past it.** Four is a closed ring only while the glyph's own coverage bridges the diagonal; at r >= 2 the corners open and each letter wears a plus sign, so the diagonals are added on the SAME circle of radius r (r/sqrt2 each way) rather than at the square's corner, which would sit r*sqrt2 out and fringe. **(3) The whole along-path RUN's halo goes down before any of its fill.** Per glyph would be wrong at a bend — glyph N's halo lands on glyph N-1's face and eats it from the trailing edge — which is the one way a stamped halo can look broken rather than merely imprecise. **(4) The outline grows with the text it outlines**: `halo_width` is authored against the authored size, so a label resized by `kMeters` or a label reference scale (T1) scales its halo by the same ratio, or a 40 px name wears a one-pixel thread. **(5) `halo_draws()` is its own counter and is NOT folded into `draws_emitted()`** — a haloed label is one label, and folding 4-8 stamps in would move every existing draw-count assertion the moment a style turned halos on. **The OSM loader** learns `text-halo-color` and `text-halo-width` (constants or zoom functions, CSS px through the same dpi conversion as every other paint value; a colour with NO width draws nothing, which is GL's default and matters because styles set a halo colour per zoom band). `text-halo-blur` becomes the THIRD declared deviation in `fv_osm_style.h`: ignored and counted (`ignored_halo_blur()`), not rejected, because a stamped halo has no coverage to soften and every OpenMapTiles-derived style carries a blur beside a width that IS honoured — failing the whole sheet over the least visible property would be the declared-subset rule turned against itself. `peregrine-osm.json`'s five label layers now carry halos (white at 1.5-2 px; the water names take a faintly blue-white so they do not glare on `#a0c8f0`). **Verified by eye as well as by suite**: `osm_atlanta_road_names.png` re-rendered at 1:12,000 — "Walton Street NW" and the numbered streets now read over the building fills they were dissolving into, and the halo stays continuous around the rotated glyphs. **The Atlanta golden did not move and could not**: labels are off in it deliberately (host font). ENC, DNC and every other golden are untouched — the default is 0, and 0 takes exactly the old path. **Tests**: 7 new (**836 total**, of which the same 5 pre-existing §2d Osm data failures). 4 renderer tests, colour-classified with a NEUTRAL BAND rather than a nearest-colour split — the antialiased rim is green-over-red and belongs to neither count, and folding it in would make "the halo ate the face" and "the face has a blended edge" the same measurement; they pin the ring's existence, that the face survives underneath, the 4 -> 8 switch and its reach, that the halo scales with a resized label, and that a rotated run is outlined glyph by glyph. 3 OSM style tests pin the dpi conversion, the 0.75 halo alpha, blur being counted not rejected, colour-without-width drawing nothing, and a halo width taken from a stop table. **NOT here**: no blur, and still no label collision or de-duplication (§2b) — a halo makes an overlap MORE legible as an overlap, not less of one. |
| OVL-C | The **Contour Lines overlay** (`Applications/FalconView/Contour`, 3.0k lines): the tracer, the overlay, its labels, and the SMOOTHING the Windows product never had (2026-08-29) | 5 | T | First of the ledger's **OVL** queue and the first overlay ported over the §1a-bis toolkit rather than alongside it. Plan `port/contour-plan.md` (C1-C6; C4 unstarted and optional). **(1) `fvkit/geo/terrain_contour.h` — MARCHING SQUARES, not the original's assembly.** The crossings are FalconView's own (linear interpolation along a cell edge, so every vertex lands where its vertex landed); what did not come across is `ContourLists.cpp`'s 400 lines of `CContourPoint` heap objects keyed by two cell ids, filed in a `multimap<CellID,·>` per level and re-chained by searching eight neighbouring cells per point, with loop closure found by scanning a `std::list` stack. It leans on unsigned wraparound (`CellID(row - 1, ...)` at row 0), shadows `pCurrent` inside its own inner loop, and its saddle behaviour is whatever the multimap's iteration order happens to be. **Three differences are visible on a map and all three are deliberate**: a cell with any NaN corner emits nothing (FalconView filled a void with -32767 metres and TRACED IT, hanging a fan of contours off every hole in the coverage); saddles split on the cell MEAN, so the same tile splits the same way twice; and a closed ring knows it is one, which the labeller and the smoother both need. `level_index` is the integer multiple, so "is this major?" is `index % divisions == 0` — exact, where the original threw the index away, kept millimetres in an int and asked `((level + err/2) % major) > err` with a 5% fudge to get it back. **(2) `fvkit/overlay/contour_overlay.h` (`fv.contour`)** keeps FalconView's DECISIONS: the 1:250 K display threshold and its independent label threshold, the fixed geographic tile lattice (a pan reuses what the last frame traced), sampling at `max(4 px, the native post spacing)`, the one-third hysteresis before re-sampling, tiles sharing their edge posts so a contour meets itself across a boundary, major/minor as line WEIGHT in one colour, and labels on major lines only with the line BROKEN for the text. It drops the `DataSource` radio group (the ported elevation source already prefers the finest cell it has), the per-frame registry reads, the `set_valid` flag, the hourglass, and `prepare_for_draw`, whose entire body is `if (force_redraw) force_redraw = false;`. **THE TILE SIZE IS THE ONE PLACE THE PORT STOPS COPYING**: FalconView keyed it on the DTED level alone (0.2 deg for level 1), which at 1:24 K is SIXTY TIMES the area on screen — measured, 58,081 posts sampled to draw about a thousand of them. The step is now the smallest ladder entry that still covers the viewport, capped at 512 posts per axis, so a screenful is one to four tiles at every zoom: the same view fell to **14,884 posts and 6 ms**. **(3) `IElevationSource::PostSpacing`** was added (additive, defaulted to "I don't know"), answered by `DtedElevationSource` from the covering cell's own post counts — which is also the honest version of contour.cpp's hard-coded `DTED_Zone_Conversion` table for east-west thinning toward the poles. **(4) C5, THE SMOOTHING, is Chris's ask and is the toolkit's fourth piece** (`path_shaping.h`, §1a-bis) — thin with Douglas-Peucker, then Chaikin or centripetal Catmull-Rom, IN PIXELS on the projected path and never on the cached geometry, so it costs what is on screen and changing it re-draws without re-reading a post. Chaikin is the default because it is convex-hull bounded and therefore cannot bulge one contour across another. **The thinning is what pays for it**, and it is also what finally gives `ThinningLevel` a meaning: over there the key is stored, clamped to 1..10, written to the registry, shown in the property page and **never read**. Release, 1:50 K, 1024x768: 0.72 / 1.13 / 1.16 ms per warm frame for none / chaikin / spline. **(5) Two things only a rendered frame could have found.** The label's straightness test compared consecutive SEGMENT angles, and a contour traced off posts is jagged at the vertex scale even where it runs dead straight over the length of a four-digit number — 2 labels on 109 major lines. It measures deviation from the CHORD now. And the anchor was a fraction of the whole line, but a tile is bigger than the viewport by design, so the middle of a contour is usually off screen; anchors are now fractions of the longest run that is actually inside the canvas, and a line with nothing on screen is counted as `labels_offscreen` rather than as a failure. **(6) The ledger was wrong about this overlay in one respect** and it is recorded in §2a: there is no interval ladder to port. `contour_pp.cpp:358` lists `"1:1M\t200\t1000"` three times — a stub. **(7) A kChoice property is now spelled by NAME** in .ini and in pyfvw; this overlay is the first type to declare one, so nothing older had to keep working. **Tests**: 31 new (9 tracer, 15 overlay, 7 path shaping) — **1830 total, all green**. |
| A1 | App layer, first milestone: overlay types as DATA, and capabilities discovered by accessor (2026-08-12) | 6 | T | Twenty-eighth session of the ACTIVE TRACK, and the first of `port/fvkit-app-plan.md`'s A-series. **New layer `fv::app`** under `port/include/fvkit/app/` + `port/fvkit/app/`. Explicitly not a port: no FalconView file is transliterated, the *shape* of `OverlayTypeDescriptor` / `COverlayTypeDescriptorList` / the `IFvOverlay*` capability set is extracted and the Windows freight (backing-store enum, help ids, ribbon icons, COM custom initializers, HCURSOR, registry restore) is left behind. **(1) `type_registry.h/.cpp`** — `TypeId` is a STRING (`"fv.grid"`), not a GUID (R4), because the display order and the restored session get written to `fv::Settings` and a GUID is neither diffable nor readable there; `FileTypeDesc` is a `std::optional` member and **that one optional IS the static-vs-file distinction** — engaged means many-instances-with-documents, absent means at-most-one-and-toggled; the factory is a `std::function`, not an `IFvOverlayFactory` (R3). `Register` rejects an empty id, a duplicate, **and a null factory** — a type that cannot be instantiated would otherwise fail at the moment the user clicked something. `All()`/`WithEditors()` return REGISTRATION order, not sorted: that is the order the shell asked for its menus in. `FindByExtension` normalises case and one leading dot, and **first claimant keeps the extension**, so a plugin registered later cannot silently steal a built-in's files. Descriptors are held as `unique_ptr`s because menus, stack rows and the session file all keep a descriptor pointer and growth must not move one — pinned by its own test. **(2) `capabilities.h`** — `Persistence`, `HitTest`, `SnapTo`, `ContextMenu`, `RoutingOverrides`, `EditTarget`, plus `HitItem`/`SnapToItem`. **Discovery is by virtual accessor, not `dynamic_cast` (R2)**: `Overlay` grew six `As*()` returning nullptr by default and a capable overlay overrides to return `this`. Not a style choice — a cross-cast through a pybind11 trampoline is unreliable, and D1 already committed L4 to a single overlay base for that same lifetime reason. The accessors are declared over **forward-declared** `fv::app` types, so L4 keeps zero app-layer dependency and `fvkit/overlay` still builds and binds alone. `Persistence` fires its observer **only on an actual change** — "dirty *changed*" is the notification, and a redundant `set_dirty(true)` per edit would repaint the overlay list on every keystroke. **(3) Two headers landed EARLY on purpose, each with the plan's own text and no invented shape**: `shell.h`'s value types (`CursorId`, `HintText`, `MenuNode`) because capabilities speak them, and `editor.h`'s `OverlayEditor` + `EditorUiConstraints` because `editor_factory` returns a `unique_ptr<OverlayEditor>` and **a `unique_ptr` cannot be RETURNED through an incomplete type** — a bare forward declaration would have left the field unpopulatable and `WithEditors()` untestable. `AppShell` (A3) and `EditorManager` (A4) are still theirs. **(4) `Overlay::type_id()`** — stamped at creation by the session layer the way `InternalInitialize(guid)` did, so A2's stack can answer `FirstOfType`/`OfType`. **Empty is legal**: an ad-hoc pyfvw overlay has no registered type, which is R7 (the layer is additive) made testable. **(5) The grid is the first registered static type** — no file descriptor, no editor, `default_display_order` 900 and `is_top_most` false, because a graticule belongs over the map and under a crosshair or a route being edited. **NAMESPACE DEVIATION, deliberate**: D5 says "FvKit adds no nested namespace"; this layer is `fv::app`, because the app plan spells it that way in every code block and A6 binds it as the `pyfvw.app` submodule, which mirrors the C++ nesting. Noted in `shell.h`'s header comment so it is not rediscovered as a mistake. D1–D4 and D6 apply unchanged. **Tests**: 14 new (**850 total** — note 836 → 850, and that §1's ctest line had been left at O5e's 829 through T2's +7; the same 5 pre-existing §2d Osm data failures). Register/duplicate-reject/empty-id/null-factory, pointer stability across 64 later registrations, extension case+dot+first-wins, static-vs-file (**and that an unregistered id is NEITHER**, so a caller cannot infer existence from `IsStatic` being false), `WithEditors` making a live editor, the grid's descriptor, and **the static-overlay toggle driven through the descriptor over the existing L4 stack** — exists ⇒ close, doesn't ⇒ create, twice round. A3's `ToggleStatic` will own that flow; doing it by hand here is what proves the descriptor already carries everything the flow needs. |
| A3 | App layer, the shell seam and the session flows: `AppShell`, `FlowResult`, `OverlaySession`, and a scripted `FakeShell` (2026-08-12) | 3 | T | Thirtieth session of the ACTIVE TRACK, and the third A-session. Plan §3f/§3g. **(1) Rule R1 is not a design preference, it is what makes the layer testable.** `AppShell` is the complete inventory of UI the app layer needs — five DECISIONS (`AskSave`, `ChooseFilesToOpen`, `ChooseSaveSpec`, `ChooseFromList`, `ConfirmRevert`) and six presentation calls — and nothing else in `fv::app` touches a user. Because the core only ever states what needs deciding, `port/fvkit/app/test/fake_shell.h` answers from plain fields and **every one of the 56 tests is a hermetic unit test with no dialog, no message pump and no robot**. On Windows the equivalent coverage did not exist, and this is why. `FakeShell` is a test header, not a shipped one, and A4/A5 pick it up from there. **(2) `FlowResult` lives in `shell.h`, and kCanceled is NOT kFailed.** A cancel is the user's answer: it is never reported through `ReportError`, it leaves `last_error()` alone, and it **propagates** — one Cancel at one Save prompt aborts the whole `CloseAll` and with it the application Exit, which is FalconView's behaviour and the reason a flow cannot return a plain `Status`. A failure is reported once, at the point it happened, and left on `last_error()`. The header is where it belongs because the thing that produces a cancel is always an `AppShell` answer, and because `editor.h` (which includes shell.h) returns one too in A4. **(3) Every flow that can leave the stack half-built refuses to.** A `FileNew` that fails, a `FileOpen` that fails, a file type whose overlay has no `Persistence` — in all three the overlay is never `Add`ed, because a half-built document the user then has to close is worse than the error. Same shape on the other end: a save that FAILED or was CANCELLED aborts the close it was inside, since closing anyway is exactly how a user loses a document. Both have tests, and the failing-save one is reachable only because the test double can be told to fail. **(4) `Persistence` grew `save_format_index`, which the plan did not have.** Save-after-Save-As has to rewrite the document in the format the user PICKED; with nowhere to remember the index, every plain Save would silently rewrite in format 0. Session-owned, like `has_been_saved`. Found by writing the test, not by reading the plan. **(5) Open dedup is `(TypeId, file spec)` and re-opening a DIRTY copy offers a revert** — and declining that revert is **not** a cancel: the file the user asked for is open and about to be current, which is what they asked for, and their edits simply survive. Dedup is per TYPE, so two overlays may legitimately read one path. With no type named, the extension picks it (registry order breaking ties, per A1), and with no type HINT the open chooser is offered the **union** of every file type's filters — FalconView's File>Open against its Open Overlay, one flow with the type decided in advance and one without. In a multi-select, one bad file never stops the others (the aggregate is the worst outcome: failed beats canceled beats done). **(6) The reentrancy guard §6 asks for is on the PUBLIC entries only.** Each public flow takes a named guard and a nested entry returns kFailed loudly, naming both flows; the flows call each other through private `*Impl` bodies, so Close→Save is not a nested entry. Both halves have a test, the first driven by a `FakeShell` that starts a second flow from inside `AskSave` — which is exactly what a real shell that pumps the message loop does. **(7) `ToggleStatic` does not itself make the new overlay current**, and the test says so with a low-display-order type: a graticule is not a document and there is nothing to be current FOR. Whether it ends up current is A2's insertion rule and nothing else. What deliberately makes an editable overlay current is entering its editor's mode, which is A4's. **The first draft of this test asserted the opposite and failed** against A2's `Add`, which was the right outcome — the stack's rule wins and the session does not fight it. **(8) Configuration is IN MEMORY, and that is rule S1 rather than an omission.** `SaveConfiguration` writes `[session.<name>]` (one row per overlay bottom-up: type, file spec, visibility, plus the current index and declutter) into the live `fv::Settings`; `RestoreConfiguration` reads it back through the same flows, so an already-open file is DEDUPED rather than doubled and nothing is closed first. But `fv::Settings` has no `Save()` — the file is authored by a human and the application never rewrites it — so a hand-written `peregrine.ini` can carry a startup session today and "save my current layout" cannot. New §2b item; the fix is a SECOND store for application state, not a relaxation of S1. Restore is best-effort by design (an unregistered type, an unsaved document, a file that no longer opens are each skipped with a line in `warnings()`), and the saved ORDER is reapplied only when the restored overlays are exactly the stack, because weaving them around overlays the configuration never mentioned would be inventing an answer. `RestoreStartupOverlays` finally reads the `restore_at_startup` flag A1 defined and nothing consumed. **(9) `Exit` had a real bug, caught by its own test**: it took the configuration snapshot AFTER `CloseAll`, by which time there is no stack left to describe, so autosave wrote `count = 0`. The snapshot is now built before anything closes and APPLIED only if the close completed — a cancelled exit must leave the saved session exactly as it was. This is why `BuildConfiguration` exists as a separate step from `SaveConfiguration`. **(10) `count` is a hint and the ROWS are the truth** (found in review): the settings file is one a human is invited to edit, and a count left too high after rows were deleted by hand would have spun through a billion lookups. Restore stops at the first missing row and says so. **Editors are deliberately absent.** The plan's constructor takes an `EditorManager`; that is A4. A3 does the per-instance half that needs no manager — the closing overlay's `ReleaseEditFocus` — and leaves two named hooks: `OverlaySession` takes no editor collaborator, and `AppShell::OnEditorChanged` exists with nothing calling it. **Tests**: 56 new in `port/fvkit/app/test/app_session_test.cpp` (**931 total**; the same 5 pre-existing §2d Osm data failures). ASan/UBSan clean. **Two of the first-draft tests read an overlay the close had just destroyed** — `Close` drops the stack's last reference under D1, so a test that inspects the overlay afterwards must hold its own `shared_ptr`. Worth knowing before writing A4's editor tests, which will do the same thing. |
| A4 | App layer, editors and the mode dance: `EditorManager`, the four invariants, and focus bracketed as an ORDER (2026-08-13) | 2 | T | Thirty-first session of the ACTIVE TRACK, and the fourth A-session. Plan §3d. **(1) The mode dance is TWO directions and they must not chase each other.** `SetMode(t)` makes the current overlay match the mode; making an overlay current makes the mode match the overlay. Implemented as one object with one flag: the second direction is **observed, not called** — `EditorManager` attaches its own `StackObserver` (a private `Hook`, kept off the public API, so `CurrentChanged`/`OverlayRemoved` are things that happen TO it rather than verbs a shell can call), which means it follows a `MakeCurrent` from anywhere including an overlay-list row the user clicked. The price is that every manipulation it performs comes straight back at it, so a `Transition` RAII flag does double duty: suppress the self-inflicted notification, and make a genuine reentrant entry (a shell calling `SetMode` from inside `OnEditorChanged`) return kFailed loudly. Both halves tested. **(2) The mutual dependency with `OverlaySession` is wired after construction, on BOTH sides.** §3d.1 says the mode CREATES the overlay it needs, and creation is a flow (`NewFileOverlay` for a file type, `ToggleStatic` for a static one); §3g says the session's `Close` consults the editor. One of the two has to be built first, so the plan's constructor-reference is a `SetSession`/`SetEditorManager` pair instead. **Both are optional and the degradation is real, not a stub**: with no session, entering a mode with nothing of its type open leaves the editor active and WAITING, which is the identical state `AutoEnterOnCreate() == false` produces — a legitimate configuration for a shell that never creates documents. With no EditorManager the A3 flows are byte-for-byte the A3 flows. **(3) The editor instance is per TYPE and is KEPT.** Made on the type's first entry, cached, reused — so tool state survives leaving and re-entering the mode, and `Activate`/`Deactivate` bracket its USE rather than its life. The test pins it by counting on the instance across two entries. **(4) Invariant 3d.4 is an ORDER, so the tests assert on an ORDER.** Every double writes into one shared event log and a mode switch is asserted as the whole sentence — `release:route, deactivate:route-ed, activate:shape-ed, enter:shape`. A pair of counters would have passed on a version that bracketed the focus backwards, which is exactly the ledger's own rule about a golden hash proving nothing about orientation. **(5) A2's `Remove` and A4's rule differ, and the test is built so they disagree.** A2 drops current to the topmost remaining; §3d.3 wants the next OF THAT TYPE. The fixture puts a shape overlay above two routes so the two rules give different answers, and the refinement works by pre-empting rather than overriding — `OverlayManager::Remove` fires `OverlayRemoved` BEFORE applying its own fallback and only applies it if current is still the overlay it removed, so setting current from inside the hook is enough. **(6) `OverlayRemoved` deliberately does NOT check `in_transition_`, and `CurrentChanged` does.** A current-overlay change can be something this object caused and must ignore; a removal never is — the edited overlay stops existing whoever removed it and the focus has to be dropped either way. That is also why the release happens in the hook and not in `OverlaySession::Close`: **A3's own per-instance release had to become conditional** (`editors_ == nullptr &&`), or the overlay hears `ReleaseEditFocus` twice. Its own test. **(7) Auto-enter on CREATE, not on open**, which is where FalconView draws the line too: `NewFileOverlay` calls `AutoEnterFor`, a no-op mid-transition (the only way to reach it during one is invariant 1 creating the very overlay it is about to adopt — adopting twice would re-enter the mode inside itself, and the test counts activations to prove it does not). **An editor that refuses to activate does not undo the document**: the failure is reported and noted in `warnings()`, and the create still returns kDone, because the thing the user asked for exists. **(8) An editor cannot refuse to be LEFT.** A failing `Deactivate` is reported and then ignored — the alternative is a UI stuck in a tool state with no way out. **(9) The kCanceled branch of invariant 1 is pinned through a FAILURE, and that is stated rather than hidden**: neither creation flow currently asks the user anything (`NewFileOverlay` prompts nowhere, and `ToggleStatic` only prompts when closing, which adoption never does), so a cancel is unreachable today; it takes the identical branch as the failing `FileNew` the test uses, and the fallback-to-no-mode is what is actually pinned. **(10) Two review findings, both fixed**: `EnterMode` took its adopt overlay by CONST REFERENCE and the one interesting caller passes `manager_.current_ptr()` — an editor whose `Activate()` moved the current overlay would have silently redirected the adoption, so the parameter is by VALUE and the comment says why; and a failed `Activate` from no mode at all fired a spurious `OnEditorChanged("", nullptr)`, telling a shell to tear down a palette that was never up. **Tests**: 32 new in `port/fvkit/app/test/app_editor_test.cpp` (**963 total**; the same 5 pre-existing §2d Osm data failures). `FakeShell` grew `editor_changes`, because "one settled mode, one notification" is a COUNT assertion as much as a value one. |
| G1 | Overlay drawing, first slice: geographic contours (`fvkit/geo/contour.h`) — great circle / rhumb / simple / circle / ellipse / arc / polyline, plus `BuildGeoPath` (2026-08-13) | 5 | T | Thirty-fourth session of the ACTIVE TRACK and the first of `port/fvkit-draw-plan.md`. **The hole A6 exposed**: an overlay gets `MapProjection` + `ICanvas` and nothing else, so every line it draws is straight in screen space — `route.py` projects each waypoint and calls `draw_lines`, `PointOverlay` hand-rolls six polygons. Ported from `fvw_core/FvMappingGraphics/GeographicContourIterator.cpp` (2037 lines) with `IDrawingToolsProjection` replaced by `fv::MapProjection`. **(1) EVERY GEODESY CALL IT NEEDS WAS ALREADY COMPILED IN THE PORT** — `fv_geo_tool` builds `distance/bound/check/east_of/overlap/mercator`, which is the reference file's complete dependency list. The only build change was adding `fv_geo_tool` to `fv_fvkit`'s PUBLIC link line. **(2) THE CLIP IS THE POINT, and it is why this was ported rather than rederived from a formula.** A contour does not densify a geodesic and let the rasterizer discard it: it searches for where the arc ENTERS the viewport and steps from there. Pinned by a test that draws New York → Tokyo on a Charleston harbour map (0.0002 dpp) and asserts fewer than 100 points come back — the full arc at that resolution is millions. **(3) The step size is the SCREEN's, not the line's**: `delta_angle = 10*(pixel_width_km + pixel_height_km)/EARTH_RADIUS`, roughly 20-pixel chords, with the original's `/5` reduction above ±70° latitude preserved. A test pins the RELATIONSHIP rather than a count (the standing rule about pinning totals): halving dpp roughly doubles the vertex count. **(4) `SurfacePoint` MOVED to `fvkit/geo.h`** from `fvkit/vector/renderer.h`. It is a D4 pixel primitive and nothing about it is vector-specific; a geometry header has no business pulling in the whole vector seam to get one. Same type, same namespace, so every existing consumer compiled unchanged — a note is left where it used to live. **(5) `BuildGeoPath` yields SUB-PATHS, not the original's `(x1,y1,x2,y2)` int segments**, because everything downstream already speaks sub-paths (`PlaceAlongPath`, `ClipPolyline`, `DrawLines`) and a break is naturally a new sub-path rather than a flag riding each segment. **DECLARED DEVIATION**: FalconView's `geoline_to_surface` returns a SECOND wrapped segment on a world-spanning map; `MapProjection` has no world-wrap duplication, so the run is BROKEN at the antimeridian seam instead (detected as a >180° longitude step, which a 20-pixel-chord contour can only reach by unwrapping) and neither stub is drawn. Also unported: the page-space/world-space transform dance, because `MapProjection` has no rotation. **(6) Two bit-faithful quirks kept and labelled**, both in the rhumb clipper: `clip_t` computes its parameter in SINGLE precision (`(float)num/(float)denom`) inside otherwise-double code, and `num_steps` casts `sqrt(...)` to int BEFORE the `/20`. Both are what the Windows product draws. **(7) `Mercator` is COMPOSED, not inherited** — the original derives privately from it, which hides that the projection is re-centred on each line's own midpoint. **(8) Two contours are NEW** because FalconView spells them several places and none is reusable: `GeoArcContour` (a circle with a bearing range; point spacing matches a full circle of the same radius, and it is not closed) and `PolylineContour` (a run of points with one `LineKind` per leg, expanded lazily leg by leg so a great-circle polyline is never materialised whole). A leg that clips away BREAKS the run rather than joining two disjoint stretches with a line that was never there. **(9) A test-oracle finding worth keeping**: the first version of the constant-bearing test used the GREAT-CIRCLE initial bearing and watched it drift monotonically along a perfectly good rhumb line. Over a chord of several degrees of longitude the two bearings differ by ~`dlon*sin(lat)/2` — whole degrees at mid-latitude. The drift was the oracle's. It now asks `GEO_calc_range_and_bearing(..., FALSE)` for the rhumb bearing and holds to 0.1°. A second premise was wrong the same way: "a great circle bows poleward of both endpoints" is only visible when the two ends are at SIMILAR latitudes — on a mostly-northward segment the northernmost point simply is the northern endpoint, because the full circle's vertex lies past it. **Tests**: 28 new (`port/fvkit/test/geo_contour_test.cpp`) — **1038 total**, the same 5 pre-existing §2d Osm data failures. All directional or relational; there is no golden here to hide an orientation flip. |
| A6 | App layer, PythonView adoption: `pyfvw.app`, `PointOverlay` (the first C++ file overlay, SQLite), the route as a document with an editor, and PythonView as an `AppShell` (2026-08-13) | 4 | P | Thirty-third session of the ACTIVE TRACK, the sixth and LAST A-session, and **the plan's stated acceptance test** (§5): if the app's ad-hoc overlay code gets smaller while gaining File-Overlay behaviour, the abstractions are right. It did — the crosshair, the coverage box and the graticule stopped being hand-added instances, the route's own hit-test loop went away, and what arrived is New/Open/Save/Save As/Close/Exit, a right-click menu, hover hints, an editor palette and a save prompt on quit. **(1) A CAPABILITY IS A METHOD YOU DEFINED.** C++ opts in by overriding one accessor and returning `this` (R2), which a Python subclass cannot do — and `dynamic_cast` across a trampoline is the thing R2 exists to avoid. So `PyOverlay` inherits EVERY capability and answers each accessor by asking whether the subclass defined that capability's methods: `file_open` makes an overlay a document, `hit_test_point` makes it pickable, `menu_items` gives it a context-menu section. The state a capability carries (dirty, file spec) then lives in C++ where the stack's broadcast already expects it. Cached per instance, because an accessor is consulted on every mouse move. **(2) THE TRAMPOLINE-LIFETIME TRAP, in its sharpest form.** `OverlayManager.add` has kept the Python half alive with `py::keep_alive` since day one, but a FACTORY is called from inside a flow with no Python object in scope to keep alive — and if the interpreter drops the last reference, the C++ overlay survives, still draws, and silently answers no picks. `OverlayFromPython` returns an ALIASING `shared_ptr`: it points at the C++ overlay and owns a reference to the `py::object`, whose deleter drops it under the GIL. Two tests pin it, one that the overrides survive a `gc.collect()` and one that the Python half is really released on close. **(3) AN EDITOR IS A PROXY, NOT A TRAMPOLINE, and it is duck-typed.** `editor_factory` must yield a `std::unique_ptr<OverlayEditor>` and ownership of a Python-constructed object cannot be handed to C++ that way, so `PyEditorProxy` holds the object and forwards the six calls by name; anything with `activate`/`deactivate` is an editor. Everywhere the API hands an editor BACK (`EditorManager.current_editor`, `AppShell.on_editor_changed`) the proxy is unwrapped, so Python always sees the object it created — asserted with `is`. **(4) The FIRST C++ FILE OVERLAY is `fv::PointOverlay`, and SQLite is why it is worth having.** A `.fvpoints` document IS a SQLite database with a schema (`points` + `meta`), so `sqlite3 x.fvpoints "insert into points ..."` is a legitimate way to author one and EVOLVING the dataset is INSERTs rather than a new parser. It draws six geometric shapes (no symbology, no sprite sheet, none of the open DPI questions), is persistent, pickable and has a context menu — the first thing in C++ to exercise `Persistence`, `HitTest` and `ContextMenu` outside their own unit tests. **`HitItem::feature` is the row's own id**, not a minted handle like A5's vector adapter, because these features have identity in the file. **(5) The sample document's AMBIGUITY is a pinned data property.** Two pairs of sample points sit ~3 px apart at harbour scale, and a test asserts that they do — `kAskWhenAmbiguous` has nothing to be ambiguous about otherwise, and a coordinate edit would have quietly removed the only real fixture for it. **(6) `Overlay` grew `SetName`**: a file overlay renames itself to its document (the point document carries its own name in `meta`, a route in its JSON), which is what FalconView shows in the overlay list. **(7) Two seams A5/A3 built with NO consumer now have one**: `AppShell::ChooseFromList` is the tk ambiguity chooser, and `PickSession::SnapToPoint` is bound and tested from Python. **(8) The route is a document and an EditTarget.** `.fvrte` is deliberately NOT `.rte` — FalconView's own route files are `.rte`, the port cannot read one yet, and claiming the extension for a JSON format of our own would make that reader a migration instead of an addition. The road geometry is NOT saved: it is derived from the graph and the rule file, both of which change, and a saved copy would be a stale answer that looks like a document. Editing keys and the add-point click now require EDIT FOCUS, so two open routes do not both eat one click; an overlay driven with no `EditorManager` never hears either bracket call and starts editable, which keeps every pre-A6 pyfvw script working. **(9) The app's own overlays became STATIC TYPES.** The crosshair is `is_top_most` (drawn over everything however the stack is reordered — an untyped overlay cannot ask for that, and before A6 the route drew over the crosshair) and `restore_at_startup`; the coverage box is a plain static type. For a type with at most one instance, "off" and "not open" are the same state, so the Overlays menu toggles through `ToggleStatic` and there is no visibility flag left to set. **Behaviour change worth knowing**: the graticule now comes up AT STARTUP, because A1's built-in descriptor says `restore_at_startup` and the app honours the descriptor instead of overriding it. **(10) Every shell method has a HEADLESS answer.** The model half runs with no tk (`--shot`, pytest), and a flow that reached a dialog there would hang a test rather than fail one — so each of the five decisions answers "cancel" (or DISCARD, for the save prompt) when there is no window. The same property makes `PythonView` constructible in a test. **(11) `self.tk = None` had to move to the TOP of `__init__`**: creating the demo route runs a flow, which enters an editor, which calls back into `on_editor_changed` before the constructor has finished — the first real demonstration that a shell is re-entered by the core, and it failed loudly rather than subtly. **(12) `pyfvw.app` is its own translation unit** (`pyfvw_app.cpp` + `pyfvw_common.h`), which is also what forced `FvErrorCpp` and `ThrowIfError` out of an anonymous namespace: the translator catches by type and both TUs must throw the SAME one. **Tests**: 14 new C++ (`port/fvkit/test/point_overlay_test.cpp`) and 22 new pytest (`port/bindings/pyfvw/test/test_pyfvw_app.py`) — **1010 total**, the same 5 pre-existing §2d Osm data failures. |
| G4 | Overlay drawing, fourth slice: `RenderState{kNormal,kHighlighted}` — the stamped halo applied to a shape, and both consumers off their hand-swapped colours (2026-08-15) | 8 | T | Thirty-eighth session of the ACTIVE TRACK, and the fourth G-session. `GeoDraw::SetState` + `SetHighlight(colour, width_px)` over T2's halo, with **no new `ICanvas` operation** (R6). **(1) THE POINT IS THAT A HIGHLIGHTED THING IS STILL DRAWN AS ITSELF.** Both consumers swapped a colour by hand before this, which says "selected" by throwing away the one thing that says *which* — `PointOverlay` painted the selected marker's edge yellow and the badge's black outline vanished with it; `route.py` re-baked its marker library per waypoint, so a selected waypoint stopped saying which route it belonged to. Both now KEEP their colour and GAIN a band. The pre-G4 test could not tell those two apart (it looked at the outermost pixel, which is yellow either way), so `ASelectedMarkerGAINSABandRatherThanRecolouringOne` walks in from outside and pins the three bands in ORDER: highlight, edge, fill. **(2) A LINE TAKES ONE WIDER STROKE, NOT EIGHT OFFSET ONES** — a deliberate deviation from the plan's "stamped in N directions". A line has no interior for the offsets to reveal, which is exactly what makes the stamp worth its cost on a GLYPH, so offset-stamping a polyline draws the identical picture for eight times the work. It goes under the CASING as well as the line, because a casing is part of what is being highlighted. **(3) A HIGHLIGHT IS NEVER IN THE PICK INDEX AND NEVER COUNTS AS A DRAW.** A selected feature must not become a bigger target than an unselected one, or a click between two markers would prefer whichever is already selected. One scoped `AsHighlightPass` diverts `pick_enabled_` and moves the pass's draws into `highlight_draws()`, so an early return cannot leave picking switched off. **(4) THE HIGHLIGHT GOES ON A MARKER'S OUTERMOST STAMP AND ON THAT ONE ONLY.** A `fv.points` marker is up to three stamps deep (edge, badge, icon); highlighting each would draw the badge's glow over the edge and the icon's over the badge, and the marker would read as a set of rings rather than as one selected thing — a `next_state()` latch spends it on the first stamp drawn. The NAME is not highlighted either: a yellow-outlined name over a chart is less legible than the white halo it already has, not more. **(5) THE SYMBOL SEAM GREW A TINT, and the raster half is the interesting one.** `DrawSymbolAt` / `DrawPixmapSymbolAt` / `DrawResolvedSymbol` take `const FvColor* tint`, null = the identity, which is what left every pinned golden byte-identical. A display list has colours to replace; **a tile has only pixels**, so the tint replaces RGB and KEEPS ALPHA — an icon set is black-on-transparent, and a tint that ignored alpha would stamp a coloured SQUARE instead of the icon's shape (pinned by `AHighlightedPixmapIsTintedByAlphaAndKeepsItsOwnPixels` over a one-tile library written for it, since no other consumer reaches the raster branch). `PatternPaths` gained the tint for a pattern's STAMPS, which `pen_override` cannot reach — it is the dash pen — and the CASING deliberately does not use it, because its stamps have come out in the library's own colour since G3 and the G3 assertions are pinned over that. **(6) THE STAMP GROWTH IS CAPPED AT 2x under a highlight and is not under a casing**, which is the same arithmetic with a different answer: the ratio is the widened pen over the plain one, so a 3-px highlight on a 2-px railroad asks for 4x crossties — caught by LOOKING at the render, where it read as a coarser railroad drawn underneath rather than as that railroad glowing. **Bound** as `pyfvw.draw.RenderState` + `GeoDraw.state` / `set_highlight` / `highlight_draws`. **Dimming stays DEFERRED** (Chris 2026-08-13); §3d of the draw plan holds the two decisions already worked out, and nothing in G1–G4 is shaped around its absence. **8 new tests** (7 C++ `GeoDrawState` + 1 `PointOverlay` banding, plus 2 python); suite 1124, up from 1116. |
| PR1 | Projection rotation, first slice: the projection itself — `MapProjection::SetRotation`, the two transforms, and `VmapBounds` over the TURNED viewport (2026-08-15) | 12 | T | Forty-fifth session of the ACTIVE TRACK and the first of the PR-series planned the same day, when Chris flew MM4's demo and found that the map slides but never turns. Chris's call was the faithful route — **rotate the PROJECTION, not the rendered image** — so this slice puts rotation where every consumer already goes through: `GeoToSurface`, `SurfaceToGeo` and `VmapBounds`. No consumer, no binding, tests only; PR2 is the vector path. **(1) ROTATION 0 IS THE EXACT IDENTITY, AND IT IS GUARANTEED BY NOT DOING THE ARITHMETIC RATHER THAN BY DOING IT WITH AN IDENTITY MATRIX.** Every pinned golden in the tree — every canvas hash, every S-52 and GeoSym render — was made at rotation 0, so the acceptance test is the same shape G2's was: byte-identical, not close. Multiplying by cos 0 = 1 and adding sin 0 = 0 would in fact be exact here, but only because these particular terms are; the rule that survives a future edit is that `rot_deg_ != 0` GATES the rotate, and the unrotated path executes the same terms in the same order it did before rotation existed. The tests are `EXPECT_EQ` on doubles, deliberately — `EXPECT_DOUBLE_EQ` allows 4 ulp and would have let a reassociated expression through. The same test runs for a projection turned to 137.5 and back to 0, which is what a shell actually produces when track-up goes off. **(2) CARDINAL TURNS COME OFF A TABLE, BECAUSE `cos(pi/2)` IS 6.1e-17.** That is nothing in a pixel and something in a viewport: at 90 degrees the north-south extent of the bounds would pick up a term proportional to the OTHER axis times 6.1e-17, and more to the point 90/180/270 are the angles a track-up map sits at while a ship holds a cardinal course, so they are the ones a reader will check by hand. 0/90/180/270 get exact 0 and +/-1; everything else goes through `<cmath>`. **(3) THE SIGN IS PINNED BY TWO INDEPENDENT TESTS BECAUSE IT IS THE ONE THING THAT CANNOT BE FIXED LATER.** `SetRotation` turns the chart CLOCKWISE on the screen, which with x-right/y-down is the positive-angle matrix (`x' = x cos - y sin`, `y' = x sin + y cos`). One test says a point due north of the centre swings to the RIGHT at 90 — the north arrow turning clockwise. The other says that at 270, the angle MM2's camera already answers for a course of 090, a point due EAST lands above the centre: track-up, from the other end. Neither is derived from the other, and MM4's `screen_angle_deg` — heading + convergence - map rotation — is consistent with both, which is why PR2 expects the ownship to lose its special case rather than gain one. **(4) THE VIEWPORT IS ROTATED IN PIXELS, NEVER IN DEGREES, AND THAT IS NOT AN OPTIMISATION.** The geographic frame is ANISOTROPIC — `dpp_lat != dpp_lon` everywhere off the equator — so a rotation applied to lat/lon offsets is a shear, not a rotation, and would leave the centre-relative distance test failing by the cos(lat) factor. The corners are turned in surface pixels and only then scaled by dpp; the pixel-space AABB maps to the geographic AABB exactly because the surface->geo map is an axis-aligned scaling. The pivot is the same point the transforms already fold in, the PIXEL centre `((w-1)/2, (h-1)/2)`, while `VmapBounds` keeps its own half-pixel-inclusive `+/-w/2` extents about that same point — a discrepancy that predates rotation and is preserved rather than quietly reconciled. **(5) THE COST OF A TURNED CHART IS A NUMBER, AND IT IS IN THE BOUNDS.** The AABB of a turned w-by-h viewport is `(w cos + h sin)` by `(w sin + h cos)`; at 45 degrees both axes become `(w+h)/sqrt(2)`, so an 801x601 window queries a box 1.4x taller and 1.7x wider than the one it draws. Every source is queried with this, which means a turned frame reads more data than a straight one and no amount of care downstream removes that — it is the honest price of the faithful route and it is pinned by a test rather than left to be discovered in a profile. A quarter turn merely swaps the two extents, which is the cheap case. **(6) THE DECISION §2a ASKED FOR — DOES ROTATION ENTER THE RETAINED SCENE'S CACHE KEY? NO, AND R3a IS WHY.** `VectorScene` holds GEOGRAPHIC ink by an explicit 2026-07-28 decision (its header: re-projecting an equal-arc scene is two multiplies per vertex, and caching pixels would tie it to one viewport origin), so a scene built at rotation 0 re-projects correctly through a turned projection with no key, no epoch and no change. Better than neutral, the containment test in `CanServe` does the right thing for free: a shell asks with `VmapBounds`, a turned frame's box is up to sqrt(2) larger, and a scene built for the straight box fails containment and rebuilds — which is exactly what a turned chart needs, since the corners it now shows were never queried. Had R3a cached surface-space ink this slice would have had to add `rotation` to `SceneBuildParams` and `CanServe`, and every pan under track-up would have rebuilt. The one thing PR2 must not assume: `simplify_px` is converted to degrees through dpp, and rotation does not touch dpp, so the tolerance is unaffected. **(7) A SMALL TRAP, CAUGHT WHILE WRITING IT.** Both transforms rotate a pair IN PLACE (`RotateOffset(dx, dy, &dx, &dy)`), so the helpers take their inputs by value and write both outputs at the end; the naive body assigns `*rx` and then reads `dx` to compute `*ry`, which silently gives `y' = x' sin + y cos` and produces a transform that is not even invertible. The round-trip test at 137.25 degrees is what would have caught it. `SetRotation` accepts any finite angle and wraps into [0, 360) — a shell doing `rotation -= point_angle` produces negatives every other frame — rejects NaN and infinity without disturbing the current turn, and is orthogonal to all three dpp modes: it survives a re-centre and a re-scale, and changes where a degree lands, never how big it is. **12 new tests**; suite 1232, up from 1220 (the five Osm/mbtiles failures are §2d's data, unchanged). |
| PR2 | Projection rotation, second slice: the VECTOR path — `SymbolAngleOnChart`, `GeoDraw::DrawSymbol` vs `DrawSymbolAtPixel`, and the Python binding (2026-08-15) | 11 | T | Forty-sixth session of the ACTIVE TRACK, and the shortest diff-to-claim ratio of the PR series: about twenty lines of code under two hundred lines of test and comment. **(1) ALMOST NOTHING NEEDED TEACHING, AND THAT IS PR1'S RESULT RATHER THAN LUCK.** Rotation was put where every consumer already goes — `GeoToSurface` — so geometry turns, an along-path label turns because its glyph angles come from `atan2` over the PROJECTED tangent, a pattern's stamps turn for the same reason, and the pick index agrees with the ink because it is BUILT from the ink. The three rules §2a asked to settle with tests all held with no code behind them. Had the rotation been added to the renderer instead, each of those would have been a separate change and one of them would have been missed. **(2) THE ONE ANGLE THE PROJECTION CANNOT SEE IS A POINT SYMBOL'S.** `PointSymbolStyle::rotation_deg` is authored against the CHART'S NORTH (a buoy's ORIENT, a runway's bearing), not derived from any projected geometry, so it is the only thing in the drawing stack that has to be told. `SymbolAngleOnChart(deg, chart_rot)` in `vector/symbol_draw.h` is the whole of it — one gated subtraction — applied at exactly two sites: `VectorRenderer`'s single point symbol and `GeoDraw::StampSymbol`. **It SUBTRACTS**, because the angle reaching `DrawResolvedSymbol` turns a symbol COUNTER-clockwise (canvas.h's convention, which is why a product authoring in compass bearings negates) while the chart turns clockwise. Gated on `!= 0` for PR1's reason: every pinned golden was made at rotation 0. **(3) THE SPLIT THAT MAKES IT COHERENT IS GEOGRAPHIC ANCHOR vs PIXEL ANCHOR.** `GeoDraw::DrawSymbol` passes `proj_.Rotation()`; `DrawSymbolAtPixel` passes 0. A pixel anchor's angle is ALREADY a screen angle — the caller that chose the pixel chose the angle — so a scale bar stays level and MM4's ownship needed no edit at all. §2a predicted the ownship would 'lose its special case'; it turned out it never had one, because `screen_angle_deg()` is `heading + map_rotation + convergence` and is drawn at a pixel. The line that would have been wrong is the tempting one: applying the rotation inside `StampSymbol` unconditionally would have counter-rotated the ship by the map rotation twice. **(4) AN AREA PATTERN IS DELIBERATELY NOT TURNED.** `PlaceOverArea` lays its stamps on a grid aligned to the SURFACE axes, so turning the stamps while their grid stayed put is half a rotation and reads worse than none. The ring turns because it is projected; the hatch inside it stays upright, which is what a hatch does. Documented at the site as a decision, not left as an oversight. **(5) THE NEGATIVE CLAIM IS THE ONE WORTH A TEST, AND IT WAS MUTATION-CHECKED.** `APatternStampIsNotTurnedTWICE` draws an east-west line on an unturned chart and a north-south line on one turned 90 — the same horizontal screen line, since HarbourView's dpp is equal on both axes — and compares the ink boxes. Applying the rotation to the pattern stamps as well was tried, and the test fails exactly as its message says (the ticks lie down along their own line, height 10 -> collapsed). A comment claiming 'this must not be rotated twice' proves nothing; that test does. **(6) §6 OF PR1 IS NOW BEHAVIOUR AND NOT JUST AN ARGUMENT.** Two tests: with a generous scene margin a turn REUSES the retained scene and the picture still comes out turned (geographic ink, no cache key); with no margin a 45-degree turn grows the box on both axes, containment fails, and `CanServe` rebuilds by itself. Plus the acceptance test the whole PR series is under — a projection turned to 137.5 and back to 0 renders a BYTE-identical buffer. **(7) BOUND, AND HONESTLY LABELLED.** `MapProjection.set_rotation` / `.rotation` are exposed so the vector path is reachable from Python; the docstring says in as many words that the raster path does not honour it until PR3, because a user who turns the map today gets turned vectors over axis-aligned tiles. **11 new tests** (7 renderer, 4 GeoDraw) plus a Python one; suite 1243, up from 1232 (the five Osm/mbtiles failures are §2d's data, unchanged). |
| DATA-1 | The 2026-08-17 test-data re-cut: 13 Osm/Routing pins moved, the "declared bounds are wrong" finding INVERTED, and the MVT oracle kept in the tree (2026-08-17) | 1 | T | Forty-eighth session of the ACTIVE TRACK, and the first that ports nothing — Chris replaced `TestData/OSM/{map*.osm, kiawah.fvroad, mbtiles/us-south.mbtiles}` and asked for a re-pin plus the four (in fact **thirteen**, once the aborting ASSERTs stopped hiding their successors) known failures closed. Suite **1251, all green**, from 1250 with 13 failing. **(1) THE HEADLINE FINDING: the same declared number was a LIE in one cut of a file and the TRUTH in the next, and nothing in the metadata distinguishes them.** O1 pinned `Mbtiles.TheDeclaredBoundsAreWrongAndThePyramidIsAuthoritative` because us-south declared an east edge of exactly 0.000000 degrees while its easternmost tile was at -74.685. The re-cut declares that same 0.000000 and is now CORRECT — its ocean tiles really do reach the Greenwich meridian, z14 columns 3338..8191 where column 8191 east edge IS lon 0 exactly. So the reader deriving from the pyramid rather than believing the metadata is vindicated by the very change that made the metadata honest, which is a better argument for `Bounds()` than the original defect was. The test is renamed `BoundsAreDerivedFromTheTilesNotBelieved` and now pins the AGREEING direction. **(2) A FLAG WHOSE ONLY COVERAGE IS "WHICHEVER WAY TODAY'S DATA FALLS" IS NOT COVERED.** Re-pinning that test left `declared_bounds_disagree() == true` untested anywhere, so `MbtilesDeclaredBounds.ADeclaredBoxWiderThanThePyramidIsReportedAsDisagreeing` writes a four-tile MBTiles with sqlite3 (already on fv_osm_test's link line, PUBLIC from fv_fvkit) and states BOTH directions over it — a lying box and a box that merely rounds out to tile edges. That direction is now independent of what Chris ships. **(3) THE ORACLE DISAGREED WITH THE DECODER BY 1491 VERTICES AND THE DECODER WAS RIGHT.** A hand-written Python MVT walker (no `mapbox_vector_tile` anywhere this has run) confirmed 13 layers / 7552 features / per-layer counts EXACTLY, but counted 41,397 vertices against the port's 42,888. The difference is precisely the tile's 1491 ClosePath commands: the oracle counts parameter PAIRS, while `MvtTile` closes each ring by re-emitting its first point, which a renderer needs and a wire-byte counter has no reason to invent. Written down as an IDENTITY (pairs + closes = the decoder's total) rather than as two numbers, so a future drop of real geometry still breaks it. The oracle is kept as **`port/Osm/test/mvt_oracle.py`** with that convention in its docstring — it was reconstructed from scratch this session because O1 never committed it. **(4) THE GOLDEN MOVED BY 78 PIXELS AND THE DIFF SAID WHICH ONES.** `OsmRender.AtlantaViewport` `0x3f49f779d29c7887 -> 0x4c8ca75e922f59ff`, re-pinned only after decoding both PNGs and diffing them: 78 of 262,144 pixels (0.03%), every one a swap between the two greys (198,188,178) and (217,208,201), scattered a few pixels at a time over the downtown blocks — the tile's landuse count going 79 -> 238 changing which fill wins along a polygon edge. Nothing shifted and no colour appeared or vanished, so the frame description written at O1 still reads true. The old golden survived only in `build-san/` (Aug 8); the first diff attempt compared today's render against today's render and reported zero, which is the trap in a side-channel PNG that every build overwrites. **(5) THE LEDGER'S OWN RULE COLLECTED TWICE MORE.** Two pins were incidental to their test and are gone rather than re-pinned: `AScalelessQueryIsCappedByTileCountNotAttempted` asserted the FEATURE cap was hit, which was only ever true because the pre-re-cut data's coarsest affordable level happened to hold 5000+ features (the contract is the TILE budget, and the feature cap has its own test); and `OsmFormat`'s "the box is the US South" is now stated as the LATITUDE band, since the longitude is the thing that moved. **(6) THE ROUTING EXTRACT WENT BACKWARDS AND THAT IS FINE.** map.osm is 23,261 nodes / 1,236 ways / 348 highway ways over min 32.5591137 / -80.1965704 — which are the numbers this test carried BEFORE the 2026-08-12 refresh pushed them south-west. Both MAXIMA have never moved across three cuts. Confirmed by an independent ElementTree walk, so what moved was the extract and not the reader. **(7) FOUND IN PASSING, and logged in 2d rather than fixed**: the delivered `kiawah.fvroad` was built with `--ignore-access` — the stopgap O5b deleted — and reports 0 barred / 0 private arcs where an access-honouring build of the same four extracts gives 70 barred / 408 private car arcs. No test reads that file (they all build their own into a scratch dir), which is exactly why it needed writing down. **Every re-pin in this row was confirmed against a SECOND implementation** — sqlite3 CLI for the pyramid, the Python oracle for the tile, ElementTree for the XML, a pixel diff for the golden — because a re-pin that only records what the port now says cannot tell a data change from a regression. |
| MM5 | Moving map, fifth slice: snap-to-road — `fvkit/nav/road_snap.h`, the `IRoadNetwork` seam, `fv::routing::RoadGraphNetwork` and the overlay that snaps before it resolves (2026-08-17) | 8 | T | Forty-ninth session of the ACTIVE TRACK, and the first that had to make two libraries meet without letting them link. **(1) THE SEAM IS THE DESIGN DECISION, AND IT IS MM1'S RULE KEPT RATHER THAN A NEW ONE.** The plan says "`road_snap.h` over `port/Routing`'s RoadGraph"; taken literally that makes fvkit link the router, which MM1 explicitly avoided when the scripted track builder took a POLYLINE instead of a `Route`. So fvkit declares `IRoadNetwork` — one method, "which roads are near this point, projected" — and `port/Routing/fv_road_network.h` supplies `RoadGraphNetwork`. The dependency runs one way, port/Routing already includes `fvkit/geo.h`, and **the ONE thing the adapter needed from fvkit is `ProjectOntoSegment`, which is therefore `inline` in the header** — the alternative was either dragging SQLite, libpng and every decoder into `fvgraph`, or a second copy of "what a metre is" in each adapter. Both metres are 6371008.8, the graph's own. **NO NETWORK IS A SUPPORTED STATE**: every fix passes through unsnapped, which is what a shell with no `.fvroad` gets. **(2) EVERY SCORE TERM IS IN METRES**, so a setting reads as a sentence: `heading_penalty_m` is how far out of its way the snapper will look for a road pointing the right way, `stay_bonus_m` is how much closer another road has to be before it will leave the one it is on. Nothing is a dimensionless weight nobody can tune by eye. **(3) HYSTERESIS IS THE POINT, NOT AN OPTIMISATION**, and the Kiawah test is written as a COMPARISON to prove it: the same track, the same seeded noise, snapped once with the mechanism and once with nearest-edge, **109 road changes against 163**. A bare "the error is under N metres" would pass with the hysteresis deleted. The connected bonus is the smaller half — an arc sharing an end with the current one is a TURN, an arc that shares neither is a jump across the block — and it needs nothing from the network but two opaque node ids. **(4) THE STANDSTILL HOLD IS AN INFINITE STAY BONUS RATHER THAN A BRANCH**, which is what keeps it honest: the held arc still has to be IN RANGE (it is only scored at all because `QueryNear` returned it), so a ship that has genuinely drifted off its road lets go, and a held snap takes the distance factor alone for its confidence because holding makes no claim about which road is best. **(5) SNAPPING GOES BEFORE THE HEADING RESOLVER, and the order is the whole point.** What the resolver keeps is a history of POSITIONS; resolving first and snapping afterwards would derive every heading from the scatter the snap exists to remove. Snapping first also means the road's bearing arrives as a REPORTED heading, which heading.h already prefers over anything it derives — no new rule, just the existing one applying. **A two-way road with no heading cannot say which way along itself the ship is going**, so the first fixes snap without a bearing and the derived heading answers the direction question by the third; that chicken-and-egg is documented and tested rather than papered over with a guess. **(6) THE ADAPTER'S INDEX IS OVER THE GEOMETRY AND THE FIRST VERSION WAS NOT.** `RoadGraph::bounds()` is the box of the graph's VERTICES, which is right for a router (it starts and ends at junctions) and wrong here: a road's shape points can lie outside its own endpoints' box, and a single east-west road gives a box with NO HEIGHT AT ALL. The dogleg test — one way with one interior vertex, queried at the vertex — found it as zero candidates, and the fix is to size the grid off every point actually indexed. Also **one candidate per undirected ROAD, not per arc**: the graph mirrors each edge into both endpoints' lists, and offering both would make every two-way street ambiguous with itself and halve the confidence of every snap on one. And the **class filter is a real need rather than a knob** — Kiawah has a cycleway beside almost every road, so a car snapping to the nearest arc of any class spends the drive on the cycle path (`IsDriveable` + the arc's own motor-vehicle bit; `private` is NOT a bar, O5b's rule). **(7) THE MEASURED RESIDUAL IS THE RIGHT NUMBER RATHER THAN A DISAPPOINTING ONE.** Over 1295 fixes of a routed Kiawah track with ±12 m of scatter: all 1295 snapped, mean error 9.32 m → **5.96 m**. Snapping removes the ACROSS-track error and leaves the ALONG-track one untouched — a fix 10 m up the road projects onto the road 10 m up it — so what is left is one axis of the noise, which is exactly what came out. **That is the honest reason MM5b (HMM/Viterbi) stays optional**: removing it needs a motion model, not a projection. **(8) THE APP GOT A DEMO KNOB AND IT IS LABELLED ONE.** The scripted feed replays a track that is already exactly on the roads, so snapping it is a no-op nobody can see; `[movingmap] noise_m` scatters the replayed track (seeded — a demonstration that scatters differently every run is not one), the status bar names the road the ship is on, and the graph is SHARED with the route overlay rather than loaded twice. **40 new tests** (26 fvkit over a fake network, 4 overlay-wiring, 10 over the real Kiawah graph and the adapter); suite **1291**, up from 1251, all green. |
| MM6 | Moving map, sixth slice: NMEA + transports + a GPX reader — `fvkit/nav/{nmea,line_transport,gpx}.h`, `NmeaLineSource`, and `BuildScriptedTrackFromFixes` as the one seam every recorded track arrives at (2026-08-17) | 78 | T | Fiftieth session of the ACTIVE TRACK, and the one where the moving map finally gets a feed that is not a script. **(1) THE PARSER IS A FAITHFUL PORT AND THE THREE QUIRKS ARE LABELLED Q1–Q3 IN THE HEADER RATHER THAN BURIED.** Q1: a sentence one field short is rejected WHOLE (RMC 11, GGA 14, GLL 6, VTG 8) — kept because a "repaired" short sentence is a guess about which field is missing. Q2: GGA withholds the altitude at three satellites or fewer, which is faithful AND defensible (a 3-satellite altitude is a 2D solution's fiction) but does mean an empty satellite field never yields one. Q3: RMC's magnetic heading is DERIVED from the variation, west adding and east subtracting, wrapped by a single add or subtract of 360 and not a modulo — so both directions are asserted in the tests, because a sign error here is perfectly plausible in one direction alone. **(2) THE ONE DELIBERATE BREAK WITH THE ORIGINAL IS THE TALKER, AND IT IS THE WHOLE POINT OF THE SESSION.** `strncmp(s, "$GPRMC", 6)` was right in 1994 and today rejects every sentence a phone sends: a multi-constellation receiver talks `$GNRMC`. MM6 exists so a phone can drive the map, so any talker is accepted and the two characters are REPORTED, leaving the old behaviour one filter away. Bit-faithfulness protects a behaviour somebody depends on; it does not require porting a hardcoded constellation. **(3) THE −1000.0 SENTINELS STAYING DEAD IS WHAT MADE THE ASSEMBLER POSSIBLE.** MM1's rule 1 (every field carries its own validity) looked like tidiness a year ago; here it is load-bearing, because a GLL genuinely has no speed and a VTG genuinely has no position, and merging them is only expressible if "absent" is a bit rather than a magic float. `NmeaFixAssembler` groups sentences by time of day and `PositionFix::Merge` does the rest — MM1's method, called for the first time by the thing it was written for. **(4) TIME IS SPLIT IN TWO BECAUSE NMEA SPLITS IT IN TWO.** Every sentence but VTG carries a time of DAY; only RMC carries the date. So `NmeaReading` reports the halves separately and the assembler resolves them, and **until a date has arrived a fix has `has_time` false rather than a 1970 stamp** — a GGA-only receiver genuinely does not say what day it is. **(5) CLOSING ON THE EPOCH CHANGE COSTS ONE EPOCH OF LATENCY, WHICH IS INHERENT AND SO IS STATED.** A group is only known to be over when the next one starts; at 1 Hz that is a second a live map would feel. `SetEmitPerSentence` is the way out, and the bug that mode introduces — the epoch close re-delivering an instant already sent — was found in review and is now a `pending_emitted_` flag and a test that pins three emissions where a naive version gives five. **(6) THE TRANSPORT IS A SEPARATE SEAM, AND THAT DECISION PAID TWICE.** nmea.h has no idea whether bytes came off a socket, out of a log or out of a string literal, so every parser test is a string and every transport test needs no parser. It also replaces `NetNMEA/comm.cpp` + `poller.cpp` (Win32 overlapped serial I/O and an MFC pump) rather than porting them — D6's adapter seam. **`ReadLine` never blocks** and **`kEnd` is not an error**, so a source flushes on the first and stops on the second. **(7) FRAMING IS ONE IMPLEMENTATION BECAUSE THE SPLIT READ IS THE BUG EVERY HAND-ROLLED READER HAS.** `LineBuffer` holds the partial tail, drops `\r`, skips blanks and caps a line at 512 bytes (six times the longest legal sentence) so a stream sending one endless line is bounded — this is the port's first network-facing reader and that mattered. Two things it got wrong first: **`follow` needs its own offset and an explicit seek** (clearing eof does not make a stream see appended bytes, and the test caught it reading "one" twice), and **a following file must NEVER hand over an unterminated tail** — in a growing file that is a line still being written, and taking it delivers its first half and then the whole thing again. **(8) THE GPX READER HAS NO WINDOWS ORIGINAL AND EARNS ITS PLACE ANYWAY.** Chris asked for it beside the NMEA work; the argument is that GPX is what a watch, a phone or a bike computer actually hands you, while a raw NMEA log is what a receiver hands a program. Its three rules are decisions, not ports: `<time>` IS the fix's time (so a replay runs at the speed it was ridden); **speed is derived and heading is not**, because HeadingResolver already derives one in SCREEN space and a true bearing written into `has_true_heading` would look REPORTED and quietly outrank it; and `<ele>` is read as MSL, the pragmatic reading every GPX consumer makes, said out loud. Segment boundaries are KEPT — a straight line across a lunch break is not a track. It parses with **expat, never by hand**, because a GPX file comes off the open internet, and an entity declaration **stops the parse at the DTD** (the review found the first version merely RECORDING the refusal and letting expat expand the document first, which is the opposite of the intent). **(9) BOTH RECORDED READERS ARRIVE AT ONE SEAM AND THAT IS THE DESIGN.** `ReadGpxFile` and `ReadNmeaLog` produce the same `vector<PositionFix>`, `BuildScriptedTrackFromFixes` turns their own stamps into a schedule, and MM1's `ScriptedSource` plays it — **so the moving map does not learn a second kind of track, and a GPX replay reaches the camera, the heading resolver and the road snapper with no new code in any of them.** The review found that function double-counting time across an unstamped fix (the rest of the replay drifting one interval later per dropout); fixed by charging the fallback interval against the recording's own clock. **(10) THE FIXTURE IS A REAL RIDE AND IT PROVED ITS WORTH.** `testdata/kiawah_cycle.gpx` — 1705 points at 1 Hz over 1704 s, Chris's own ride, on the island every other piece of this work is pinned over. Its elevation runs down to **−6.2 m**, which is exactly the kind of value an unsigned or clamped `<ele>` loses; the derived mean speed comes out at **3.58 m/s** over 6.1 km, and no 1-second step exceeds 15 m/s. **(11) ONE BUILD-SIDE DEVIATION, RECORDED RATHER THAN SILENT.** The original's `build_RMC` and `build_VTG` format the checksum `"*%2hX"` — space-padded — so anything under 0x10 came out `"* 5"`, invalid to every reader but itself; `build_GGA` has it right. The port takes GGA's form everywhere, nothing was pinned over the old output, and a recorder whose files no other tool can read is not a recorder. A built position round-trips to about a metre, which is the wire format's three decimals of minutes and NOT the parse — the test names that tolerance so nobody tightens it. **78 new tests** (23 transports and framing, 34 NMEA, 21 GPX incl. six over the real ride); suite **1369**, up from 1291, all green. |
| P1 | Pippin, first slice: the offline data pack (`port/tools/mbtiles_cut.py`, `port/apps/Pippin/`) and the iOS cross-build of the core — `libpippin_core.a`, and a link check that RUNS on the simulator (2026-08-18) | 1 | T | Fifty-second session of the ACTIVE TRACK, the first of the Pippin plan, and the only one so far whose product is a **build configuration and 2 MB of data** rather than a library. **(1) THE CUT IS A COPY, WHICH IS WHY IT CAN BE PINNED AGAINST ITS SOURCE.** `mbtiles_cut.py` is pure SQLite — tile blobs move byte for byte into a new file — so the only thing that can go wrong is WHICH tiles it took, and every way of getting that wrong (a dropped row, an off-by-one, the TMS/XYZ y-flip inverted) shows up as a frame that differs from the same frame over `us-south`. `OsmRender.KiawahCutMatchesSource` renders both and compares: **no hash**, deliberately, because a hash would need re-pinning every time us-south is re-cut, whereas "the cut agrees with its own source" is true forever. Two viewports (z14 street scale and the whole island), and the frame has to prove it is a MAP first — 100+ draws and 4+ colours — because two identically blank frames agree too. **(2) A TILE IS ATOMIC, AND THAT IS WHAT MAKES A ZOOMED-OUT PHONE WORK.** Every tile intersecting the box is copied whole, so at z0–z8 the pack holds most of a hemisphere in eight tiles and a user who pinches out gets a map instead of a hole. 119 tiles, z0..z14, **1.9 MB out of 4.4 GB**. The corollary bit the first draft of the test: a viewport WIDER than the cut box is legitimately empty outside it, so the wide case is set to 1:75,000 (9.6 km, the island end to end) rather than 1:400,000 — a test pinning the box's edge is a test of nothing. Bounds were widened from the plan's `-80.14..-80.00` to `-80.17..-79.97` because the plan said to verify against the `map*.osm` extracts and the extracts run to `-80.1532..-79.9892`. **(3) §2d's `--ignore-access` DEFECT WAS ALREADY FIXED WHEN IT WAS WRITTEN.** The graph on disk reports 70 barred / 408 private car arcs, and rebuilding it with the row's own command produces a **byte-identical** file — Chris had rerun it before the row was ever read. What the check DID find is worth more: **the build is input-order dependent**. The same four extracts as a shell glob (`map*.osm` sorts `map-2` first) give a file differing in 64,873 of 210,674 bytes while every number `fvgraph info` prints is identical — the same graph with its nodes numbered differently. A `cmp` against a delivered `.fvroad` means nothing unless the input order matches. **(4) THE MANIFEST HAS TEETH OR IT IS A COMMENT.** `stage_data.py` copies five files and then re-reads the staged `pippin.ini` and checks that every path it names EXISTS in the pack — the two would otherwise drift, and a settings key pointing at nothing is the failure mode a phone reports as a blank map at 3 pm on a bike. It is exit-1 today, on purpose: the **label font is not in the tree**, because iOS gives an app no readable path to a system face and there was no OFL-licensed proportional one on the build machine to bundle. Flagged for Chris rather than downloaded. **(5) `FVW_IOS` IS SET, NOT ASKED FOR.** `if(CMAKE_SYSTEM_NAME STREQUAL "iOS")` forces it, so nobody can configure an iOS build and forget the flag. It turns off everything with a host in it — tests (a device runs no ctest, and gtest would be a fourth FetchContent), pyfvw (no CPython in a bundle), and the CLI tools, which the session finally names for what they are: **`fvrender`/`fvpack`/`fvgraph` MAKE the data a phone reads**. The mac build is untouched: every guard is `if(FVW_IOS)` or `if(NOT FVW_IOS)`. **(6) ONE ARCHIVE, BECAUSE THE ALTERNATIVE LIVES OUTSIDE CMAKE.** The core is 21 static libraries with a dependency order; an Xcode target naming them all is a list nothing checks. `fvw_collect_static_libs` walks `LINK_LIBRARIES`/`INTERFACE_LINK_LIBRARIES` transitively (stripping the `$<LINK_ONLY:>` wrapper PRIVATE deps arrive in) and `xcrun libtool -static` merges them into **`libpippin_core.a`, 63 MB, arm64, platform iOS, minos 17.0**. `libtool` and not `ar`: several modules have a `util.cpp` and `ar` drops duplicate member names silently. Xcode 26's libtool no longer takes `-no_warning_for_no_symbol_table`, which is worth knowing before reading the four "has no symbols" warnings as an error. **(7) A MERGED ARCHIVE PROVES NOTHING ABOUT UNRESOLVED SYMBOLS**, so P1 links a real iOS executable, `pippin_link_check`, and calls the seams. On the SIMULATOR it also runs: `xcrun simctl spawn booted` opens the pack, finds the Ruddy Turnstone route on both profiles (**2.16 km — 1553 s on foot, 519 s by bike**) and draws a 390x750 phone-shaped frame of Kiawah at z14 (978 features, 880 draws), PNG and all. That is most of P2's acceptance arriving in P1, and it means the first Xcode session starts from a core already known to work on the device's architecture. **(8) SQLITE IS THE SDK'S** (`find_package(SQLite3)` resolves to `libsqlite3.tbd` on both iOS sysroots) and the FetchContent deps — expat, zlib, vtzero, json — cross-compiled with no edits at all, exactly as the plan predicted. The link-line arguments that are NOT in the archive are written beside it in `pippin_core_link_flags.txt` rather than left to be rediscovered by a link failure. **1 new test** (the cut vs its source); suite **1370**, up from 1369, all green — with the caveat that `RouterAvoid.APenaltyBuysADetourAndAHugeOneStillIsNotADeletion` flakes under `-j4` on a shared scratch filename, which is §2d's pre-existing defect family and was measured here (1 fail in 3 runs, passes alone every time). |
| MM7 | Moving map, the app side: `pyfvw.nav` grows MM6 (GPX, NMEA, the four transports) and PythonView can finally OPEN a feed — a recorded ride, a phone over TCP or UDP, or the demo (2026-08-18) | 12 | P | Fifty-first session of the ACTIVE TRACK, and the one that closes the gap MM6 left: the core read three feeds and the application could open none of them. **(1) THE WHOLE SESSION IS REACH, NOT DESIGN, AND THE PROOF IS THAT `_moving_map_tick` DID NOT CHANGE.** It has polled whatever source it was given since MM4, and `NmeaLineSource` has a `poll()` for exactly MM1's reason (no thread), so a live socket feed drops into the tick the shell already had. Nothing in the camera, the heading resolver or the snapper learns that the ship is a recording or a phone — which is what MM6's one-seam decision was FOR, and this is the session that spends it. **(2) THE BINDING IS THE FEATURE.** None of MM6 was bound, so not even a script could reach it. `pyfvw.nav` now carries the GPX document, the NMEA parser and assembler, `NmeaLineSource`, `LineBuffer` and all four transports, plus `FixScriptOptions` / `build_scripted_track_from_fixes`. Two module rules did the shaping: **Python never sees a `Status`** (so `read_gpx_file` and `read_nmea_log` RAISE and `LineTransport.open()` raises), and **an out-parameter becomes a return or a `None`** — `parse_nmea_sentence`, `add_line`, `flush`, `next_line` and `parse_iso8601_utc` all answer with the thing or with None, and `read_line` answers with the tuple `(LineResult, text)`. **(3) NO PYTHON TRAMPOLINE FOR `ILineTransport`, DELIBERATELY, unlike `IPositionSource`.** A Python object holding bytes has two better doors already: `StringLineTransport.add_data` takes whatever its own socket just read, and `PositionSourceBase` is subclassable when it would rather deliver whole fixes. A third seam would only be a way to re-implement `LineBuffer` in a slower language. **(4) THE FEED IS A TUPLE AND EVERY BRANCH ENDS AT ONE `set_source`.** `("demo",)`, `("track", path)`, `("tcp", host, port)`, `("udp", port)` — a choice with arguments, dispatched in one place, so File > Open Track, the Connect dialog and the settings file are three ways of writing the same field. **A track file wins over a live host at startup**: the deterministic one is the one to start in. **(5) ONE FIELD CHOOSES THE TRANSPORT, BECAUSE ONE FIELD IS WHAT DIFFERS.** A phone comes in two shapes — an app that SERVES NMEA over TCP (you connect) and one that BROADCASTS over UDP (you listen) — so the dialog asks for a host and a port, and an EMPTY host is the UDP listener. The status line then reports the **bound** port and not the asked-for one, because "udp:0" tells a user nothing about where to point their phone. **(6) EMIT-PER-SENTENCE IS ON FOR THE LIVE FEED AND NOWHERE ELSE.** MM6 measured the cost of the assembler's default — one epoch of latency, a whole second at 1 Hz — and said which mode wants which; this is the shell that finally says it out loud. A recording can afford the wait, a live map cannot. **(7) THE STATUS BAR SHOWS BYTES, SENTENCES AND FIXES, and that is not decoration.** "Receiving but nothing parses" looks EXACTLY like silence unless all three are next to each other, which is why `NmeaLineSource` counts all three — the single most common thing to be wrong about an NMEA feed. **(8) AN EXPLICIT OPEN RE-READS AND A FEED SWITCH DOES NOT.** The parsed schedule is cached against its path, so toggling the overlay or coming back from the demo is free; asking for a file BY NAME re-reads it, because that is how a user says "this, from disk, now" (a log being appended to is the obvious case). The review caught the other half of that: when the cached re-read fails the app falls back to the demo, and it was still SAYING the track was the feed — the status bar being the only thing that would have told the user which ship they were watching. **(9) TWO THINGS FOUND IN THE CORE WHILE WIRING IT.** `nmea.h`'s header said `fix.time_s` is filled in "that is RMC, or any sentence once an assembler has seen a date"; a single-sentence parse leaves it alone even for an RMC that carries both halves, because stamping is the assembler's job — comment corrected, and the binding test asserts the split rather than the recollection. And `_configure_moving_map` scheduled its tick unconditionally, so the moving map could not run headless at all; the guard is one `if self.tk is not None`, and it is what let the whole feature be driven end to end with no window. **(10) VERIFIED AGAINST REAL SOCKETS, NOT MOCKS.** The headless drive runs the actual `PythonView` class through all five states: the 1705-point Kiawah GPX (ship on the island), a built-then-read NMEA log, a **real TCP server** on an ephemeral port (285 bytes, 5 lines, 5 fixes, ship at the last one), a **real UDP datagram** to a bound ephemeral port, and back to the demo. **12 new pytest cases** in `pyfvw_pytest`; that target is ONE ctest test, so the suite number does not move — **1369, all green**. |
| P2 | Pippin, second slice: `Pippin.xcodeproj`, the `PippinKit` ObjC++ framework (`PPMap`, `PPPixelProbe`) and a styled Kiawah on a SwiftUI screen (2026-08-18) | 11 | C | Fifty-third session of the ACTIVE TRACK, the second of the Pippin plan, and the first in the whole port whose acceptance is a **photograph of a phone** rather than a test count. **(1) THE PIXEL QUESTION IS SETTLED, AND THE ANSWER IS THE ONE THAT LOOKS UNNECESSARY.** `kCGImageAlphaLast | kCGBitmapByteOrder32Big`, 8 bpc / 32 bpp / sRGB, is the exact twin of `PixelBuffer` (RGBA8, top-down, NON-premultiplied per D4 and both of `cpu_canvas.cpp`'s blend sites) — `32Big` makes a little-endian phone lay the bytes out in component order, and `AlphaLast` is the half that can be got wrong. **An opaque frame is byte-identical under all three candidate formats**, so the wrong choice survives every map frame Pippin will ever draw and first shows up as a too-bright translucent overlay somewhere around P5. Measured over black, source `(255,255,255,128)` reads back `128,128,128` under `AlphaLast` and `255,255,255` under both `PremultipliedLast` and `NoneSkipLast`. Top-down needs NO flip anywhere: a `CGContext`'s bottom-up user space and `CGContextDrawImage`'s own flip cancel exactly — get that wrong and red and blue swap, loudly. The measurement SHIPS as `PPPixelProbe` (`--args -PPPixelProbe YES`) rather than living in a session log, which is what "so it is never debugged again" has to mean. **(2) NO `MapEngine`, AND THE PLAN NAMED ONE.** The engine is a catalog plus a raster compositor; the pack has no raster in it, so it would be an empty catalog and a `RenderBaseMap` that draws nothing. PythonView's own `_render_vector` drives a bare `MapProjection` for exactly this reason and `PPMap` follows it — the day Pippin bundles a chart the engine goes in beside the projection it already owns and no other line changes. **(3) THE FRAMEWORK IS DYNAMIC BECAUSE LGPL-3.0 §4d SAYS SO**, not because it is tidier: a user has to be able to relink the app against their own build of the FalconView-derived core. `COPYING` and `COPYING.LESSER` ship inside the `.app` for the same section. Free at P2, a retrofit at P8. **(4) XCODE DOES NOT BUILD THE CORE.** A "Check the Peregrine core" script phase verifies `libpippin_core.a` and the staged pack exist and prints the command that makes each; a CMake invocation inside an Xcode build is the kind of magic that works until it silently does not. `LIBRARY_SEARCH_PATHS[sdk=iphoneos*]` / `[sdk=iphonesimulator*]` pick the right one of P1's two build trees, and the whole link line is `-lpippin_core -lsqlite3 -lz` plus CoreFoundation, CoreGraphics and Foundation — **the last two are where iOS starts**, and nothing below PippinKit names them. The `.pbxproj` is hand-written with explicit file references (eight of them): a list that can be read beats one that maintains itself at this size. **(5) P1'S OPEN FONT IS CLOSED, WITHOUT A DOWNLOAD.** DejaVu Sans was already on the machine inside matplotlib, with its licence file beside it, so `stage_data.py` now SOURCES it from a short list of places a normal dev machine keeps one and stages `LICENSE_DEJAVU` as a REQUIRED manifest row — the Bitstream Vera licence permits redistribution only with the notice attached, so a pack with the TTF and not the notice is a pack we have no permission to ship. The file keeps its own name: renaming is the one thing that licence has an opinion about. `pippin.ini`'s `font` key moved off the invented `PippinLabel.ttf` for the same reason. **(6) THE PHYSICAL PITCH IS DERIVED FROM THE BACKING SCALE**, `25.4 / (163 x displayScale)` — Apple's baseline point density — so the style engines' millimetre widths come out physically sized on a 3x screen instead of three times too thin, and the tile source and the style engine are handed the SAME number (O2's one failure mode). **(7) WHAT IT MEASURES**: iPhone 17 Pro, 1206x2622, **z12, 1145 features, 23 ms** for the startup frame at 1:329,322, Debug app over a RelWithDebInfo core; labels draw with haloes, crisp at 3x; the frame is full-bleed corner to corner, verified by sampling the screenshot's corners for the style's declared `#f8f4f0` rather than by looking at it. **(8) THE STARTUP VIEW IS TWO-THIRDS EMPTY AND THAT IS CORRECT.** `home_bounds` is a landscape box (0.20 deg lon x 0.12 deg lat) and a phone held upright is the opposite shape, so fitting the box fills the width and about a third of the height; there is no data north of 32.67 or south of 32.55 and `#f8f4f0` is what the style says to draw where there is none. The first gesture fixes it, which is P3. **No new tests** — this session's C++ is a bridge to a UI framework and the mac test bed cannot exercise a `CGImage`; the suite is unchanged at 1370 and still green, and both the simulator and the arm64 device SDK build. |
| PR3 | Projection rotation, third slice: the RASTER path (`MapEngine::CompositeRowTurned`) and the SHELL — PythonView turns the chart (2026-08-15) | 7 | T | Forty-seventh session of the ACTIVE TRACK, and the one that makes the PR series visible: before it, `set_rotation` turned the vectors over image tiles that were still blitted square. **(1) THE GATE IS THE DESIGN, NOT AN OPTIMISATION.** `CompositeRow` branches on `proj_.Rotation() != 0.0` and the unturned path is not touched at all — same arithmetic, same order, so `kHashAtlanta` and every other pinned raster golden is byte-identical, which is PR1's rule and the acceptance test for the whole series. It is stated as a test rather than as a comment: rendering at 137.5 and back to 0 on ONE engine gives the same FNV-1a as one that never turned, which is exactly the sequence a shell produces when track-up goes off. Unifying the two paths would be the tempting later edit and that test is what would stop it. **(2) THE MASK IS THE WHOLE DIFFERENCE IN KIND, AND CLAMPING IS THE BUG IT PREVENTS.** The unturned path can CLAMP a source coordinate into the block it read, because the target region it built IS the overlap; a turned frame's target is the axis-aligned BOX of a parallelogram, whose corners are not in the frame at all, and clamping there fills all four with the frame's edge pixels smeared out — a diamond drawn as a square. So an unmapped target pixel is left at alpha 0, `PixelBuffer` zero-fills, and `DrawPixmap`'s src-over drops it. Nothing new was needed from `ICanvas` for that (`BlendPixel` has returned early on `a == 0` since it was written), so every backend including pyfvw's Python subclasses gets a turned base map. The claim was MUTATION-CHECKED: putting the clamp back fails three tests, and `ATurnedFrameDoesNotSmearIntoItsBoundingBox` fails with the box corners reading 'b' and 'r' instead of background. **(3) FOUR CORNERS, NOT TWO, AND THREE SAMPLES, NOT FOUR.** Geo->surface is affine (a scaling then a rotation), so a geographic rectangle maps to a parallelogram and the box of its four corners contains every pixel that can carry frame data — the unturned path takes two corners because there the quad IS the box. The source-pixel mapping is then fitted from THREE corner samples and stepped incrementally (two adds per pixel), which keeps the header's stated trade exactly: exact for equal-arc, a small-region affine approximation for projected imagery. Three and not four because the fourth is determined, and reading it from the source's own transform would let a projected image's non-affine residual make the mapping disagree with itself in one corner. **(4) THE TEST FRAME HAS A NORTH AND A SOUTH, BECAUSE A FOOTPRINT PROVES NOTHING.** A blit that moved the box but kept the image axis-aligned passes every extent assertion; a frame that is red above and blue below does not, and the four cardinal turns pin the sense as PR1's — north swings to the RIGHT at 90. Area is the complementary assertion (a rotation is area-preserving, so a frame that fits at every angle must ink the same count at every angle: measured within 0.35%), and it is what would catch the opposite failure, a turned blit that drops rows and leaves the chart with holes. **(5) ONE PIXEL WAS LEFT ON THE TABLE ON PURPOSE.** A quarter turn's ink box comes out 100x151 against the straight one's exact 150x100, and it is a half-pixel tie: this frame's edges land exactly between two pixel centres, the straight path breaks the tie on the SURFACE coordinate and the turned one on the SOURCE coordinate its affine walks to, and the affine arrives at -0.4999999999 where the closed form says -0.5. Any rule stated without an epsilon has this. Pinned to within 1 px with the reason written down, rather than removed by choosing an epsilon — which trades a visible half-pixel for an invisible one. **(6) THE SHELL, AND THE HAZARD PR2 FLAGGED, FIXED AT THE SOURCE.** PR2 warned that `MovingMapOverlay` keeps its OWN `map_rotation_deg_` while the projection keeps another and the ship points right only while they agree, with nothing asserting it. The answer is not discipline: a projection can now be ASKED, so `Tick` ADOPTS `proj.Rotation()` before it does anything with it. For a shell that applies every tick that is the identity; for one that lags, or turns the chart by a route of its own, it is a per-frame correction. It also brings the term into [0, 360), which is precisely what MM2's once-only 360 subtraction in `point_angle` assumes and a slew's arithmetic does not guarantee. The cost is that `SetMapRotation` becomes only an initial condition, which moved one MM4 test off the setter and onto the projection — the same claim, now stated over a chart that is really turned. **(7) THE SHELL HAD ONE MORE THING TO LEARN AND IT WAS THE PAN.** Every mover of the centre multiplies a pixel delta by deg-per-pixel, and that is a statement about the CHART's axes; they are the screen's until the chart turns, after which a drag to the right slides the map off at the rotation angle. `_chart_pixels` turns the delta back and returns its arguments unchanged at rotation 0. Everything else in PythonView was already rotation-aware for free, because it goes through `surface_to_geo` — the re-centre click, every pick, the identify. Turning the moving map off straightens the chart, since nothing else writes the rotation and a user would otherwise be stranded on a tilted map with no gesture to fix it. **MEASURED, at 1000x700 over CADRG**: 59 ms straight, 93 ms at 30 degrees, 100 ms at 45, and a quarter turn is FREE at 57 ms because it only swaps the extents — close to the 2.06x the bounds growth predicts at 45. **7 new tests** (5 engine, 2 overlay) plus a Python one; suite 1250, up from 1243 (the five Osm/mbtiles failures are §2d's data, unchanged). Visually verified over Atlanta LFC at 0/30/90 and checked for seams: a turned viewport that draws two adjacent CADRG frames has ZERO background pixels between them, so the masked blit leaves no crack. |
| MM4 | Moving map, fourth slice: the ownship overlay (`fvkit/overlay/moving_map_overlay.h`), `pyfvw.nav`, and PythonView flying the route (2026-08-15) | 30 | T | Forty-fourth session of the ACTIVE TRACK and the fourth of `port/fvkit-nav-plan.md`. MM1 built the feed, MM2 the camera and MM3 the slew, and **none of the three had a consumer**; `fv::MovingMapOverlay` is the object that holds them in the order they belong in, `pyfvw.nav` is the whole layer bound, and PythonView's "m" key replays the route overlay's own followed road while the camera tracks it. **(1) WHY THE CAMERA LIVES ON THE OVERLAY, of all places.** MM2 splits into two calls because the apron is built from where the ship was DRAWN and tested against where it has just moved to; an overlay is the only object in the port that is both drawn per frame and fed fixes, so anywhere else would oblige the shell to keep that ordering right. Here it is structural — `OnDraw` recomputes, `Tick` updates — and the test that pins it is a ship that stays inside the apron its own draw computed. **(2) THE OVERLAY STILL DOES NOT TOUCH THE ENGINE.** `Tick(proj, dt)` returns `MovingMapTick{new_fix, fix, heading, target, slew}` and applies nothing; there is a `self.center = tick.slew.center` in PythonView and nowhere in fvkit. Binding an auto-applying variant would have put that rule on the wrong side of the language boundary, so `pyfvw.nav` does not have one either. **(3) EVERY QUEUED FIX REACHES THE HEADING RESOLVER; ONLY THE LAST REACHES THE CAMERA.** The resolver's whole state is a short history of distinct positions, so skipping fixes would make a derived heading depend on how fast the shell happened to be ticking; the camera wants the present, and catching up through history walks the map over ground the ship has already left. **(4) A MODE CHANGE FORCES A RECENTRE**, which is FalconView's `force_update` reached the way its own toggles reach it: without it, turning auto-centring on does nothing at all until the ship wanders out of an apron computed while it was off, and the map sits still immediately after the user asked for the opposite. The force is spent, not sticky. **(5) THE SYMBOL'S ROTATION IS NEGATED AT THIS SEAM, and a look at the render is what found it.** `PointSymbolStyle::rotation_deg` is `CCGMSymbol::DrawSymbol`'s angle — `x' = x cos r - y sin r` over a y-up symbol space drawn into a y-down device — which turns a symbol COUNTER-clockwise on screen; a heading is a compass bearing. The first draft passed sixteen assertions and drew every ship flying backwards, because "wider than tall at 090" is true of a mirror image too. `S52StyleEngine` already negates for exactly this reason and its comment is the record; the fix is one minus sign and a directional assertion that the ink reaches further ALONG the heading than against it — which is also why the ownship's nose reaches 1.00 of the box and its tail only 0.85. **(6) TWO THINGS ABOUT THE DRAWN HEADING ARE PRESERVED RATHER THAN RECONCILED.** A REPORTED heading is a true bearing and a DERIVED one is a screen angle (MM1), and the symbol is drawn at whichever the resolver returned — that asymmetry is `get_current_heading`'s own, and reconciling it would be a new behaviour rather than a port of one. And the MAP ROTATION IS SUBTRACTED, because the port has no rotating canvas: FalconView draws at heading + convergence and lets the rotated view carry the rest, so here the screen angle is heading + convergence - map rotation, which on a shell that cannot rotate is the same expression. `screen_angle_deg()` is public because it is what the drawing is pinned against and what a compass readout wants. **One small addition to MM2**: `MovingMapCamera::ClearApron`, because `RecomputeApron(0,0,0,0)` does NOT produce an empty apron — `ComputeApron`'s boxes carry the original's `+ 1` exclusive edge, so a zero-sized window yields a 1x1 box containing the origin, and an overlay whose feed has not delivered yet needs to say "no drawn ship" rather than "a one-pixel apron at the corner". **The binding hands the two per-frame centres back BY VALUE** (caught in review): `def_readonly` on a registered class member is `reference_internal`, so `tick.slew.center` was a live view into `CameraSlew::center_` that changed under its holder on the next `Advance`. `fv.movingmap` is registered as a BUILT-IN static type and deliberately NOT restored at startup — an overlay that came back by itself would start asking a receiver for fixes on every run — which also means the app never supplies its factory, so PythonView configures the INSTANCE the toggle created (the shape every shell meets the moment a plugin registers a type). Keys: "m" for the overlay, upper-case M / T / S for auto-centre / track-up / centre-on-every-fix, plus an Overlays sub-menu and a `[movingmap]` ini section carrying the startup state. **30 new tests** (17 C++, 13 pytest); suite 1218, up from 1201.
| MM3 | Moving map, third slice: the slew (`fvkit/nav/camera_slew.h`) — MM2's target reached over a few tenths of a second instead of in one frame (2026-08-15) | 19 | T | Forty-third session of the ACTIVE TRACK and the third of `port/fvkit-nav-plan.md`. **THIS ONE HAS NO WINDOWS ORIGINAL**: FalconView jumps — `map_update` hands a new centre to `change_map_type` and the chart is a third of a window away on the next frame — so there is nothing to be bit-faithful TO, and the row's job is to say why each number and each rule is what it is. `CameraSlew` sits between MM2's `CameraTarget` and the engine, and it never touches the engine either: `Retarget` takes the camera's answer, `Advance(dt)` is called once per frame, and a `SlewState{center, rotation_deg, changed, active}` comes back for the shell to apply exactly as it applied the target before. `dt` is an ARGUMENT, the same trick MM1's injectable clock plays, so all 19 tests are exact equalities with no clock and no pump. **(1) DURATION 0 IS THE FALCONVIEW JUMP AND IT IS NOT A SPECIAL CASE BOLTED ON**: with a zero duration `Retarget` lands on the target and the next `Advance` reports it, so MM2's behaviour stays reachable THROUGH this class rather than around it — and **the rate caps deliberately do not resurrect it**, because a cap that turned an asked-for jump back into an animation would take the jump away from the one caller who wants it. **(2) THE INTERPOLATION IS IN GEO, AND THE PLAN'S "SURFACE FOR SHORT MOVES, GEO FOR LONG ONES" IS A DISTINCTION THIS PROJECTION DOES NOT HAVE.** Equal-arc surface coordinates are an affine function of lat/lon, so a lerp in one frame IS a lerp in the other to within the dpp drift as the interpolated centre changes latitude — a fraction of a pixel over a third of a window. Geo wins the tie for a reason that would still hold on a projection where they differed: it is the frame that SURVIVES, since the projection is being re-centred by this very animation and surface coordinates captured at retarget mean something else by the next tick. Longitude is taken the short way (`UnwrapLonNear`), pinned by an antimeridian test — 179.9 to -179.9 slides across the date line instead of sweeping the whole world backwards. **(3) THE RATE CAPS EXTEND THE DURATION, THEY NEVER CLIP THE MOTION.** A clipped pan would leave the map short of where the camera said it should be, which is a lie the next fix would have to correct; extending means the destination is always right and only the arrival is later. They are in PIXELS per second (1200 default) and degrees per second (120 default), not ground units — what reads as "the chart flew past" is screen speed, and the same ground distance is a jump at harbour scale and nothing at 1:80M. That is the only thing `Retarget` wants the projection for: dpp. **(4) THE CENTRE AND THE ROTATION SHARE ONE DURATION, the longer of the two the caps ask for**, because they are one motion — a track-up turn whose rotation finished before its pan would swing the chart round and then slide it, which reads as two events. Rotation goes the short way (`ShortestRotationDelta`, range (-180, +180], so a half turn resolves clockwise from either side rather than being a coin toss per call). **(5) A NEW TARGET RETARGETS IN FLIGHT AND NEVER QUEUES** (the plan's interrupt rule), restarting FROM WHERE THE MAP NOW IS so that nothing jumps, and the abandoned destination is forgotten whole — a queue would make the map visit places the ship left several fixes ago. **The cost is stated rather than hidden**: restarting also restarts the EASE, so under continuous centring, where a target arrives every fix, an ease-in-out spends every animation in its slow opening and the map lags by about one duration. Continuous mode wants a short duration or `kLinear`; ease-in-out is for the discrete recentres, which are seconds apart — which is the answer to MM2's finding (4), that continuous mode wants the slew as much as the discrete recentres do. Three smaller rules, each with a test: a target the camera did not change is IGNORED rather than treated as an interruption (the ship is inside its apron, which is not a reason to stop half way); retargeting at the place we are already at STOPS rather than restarts, since a stationary ship under continuous centring produces exactly that and an animation between a point and itself would report a change every frame; and `Reset` is how a shell says the map moved behind the slew's back (a user pan, a bookmark), which cancels the animation and reports nothing back. `Finish` lands now, for a print or a screenshot. **No binding and no consumer yet** — `pyfvw.nav` and the ownship overlay are MM4, by plan, and MM4 is where the slew's defaults meet a real frame clock. **19 new tests**, all headless; suite 1201, up from 1182. |
| MM2 | Moving map, second slice: the faithful camera (`fvkit/nav/camera.h`) — apron, 3x3 placement, track-up anchor and the recenter trigger, as a pure function (2026-08-15) | 27 | T | Forty-second session of the ACTIVE TRACK and the second of `port/fvkit-nav-plan.md`. `MovingMapCamera` plus the four pieces it is composed of (`ComputeApron`, `DeltaXyDiscrete`, `DeltaXyContinuous`, `DeltaXyTrackUp`), ported from `gps_draw.cpp`'s `map_update` (~1703) / `auto_center_bounding_box_calc` (~1556) / `set_new_map` (~488) / `get_delta_xy_*` (~645, ~763) and the anchor fractions at `gps.cpp:114`. **THE CAMERA NEVER TOUCHES THE ENGINE**: `Update` answers a `CameraTarget{changed, center, rotation_deg, rotation_changed, delta_x/y, world_escape}` and the shell applies it — which is why all 27 tests are plain unit tests with no window, no view and no message pump, where the original's four functions all reach for the active view and invalidate it themselves. **(1) THE APRON IS BUILT FROM WHERE THE SHIP WAS DRAWN AND TESTED AGAINST WHERE IT HAS JUST MOVED TO**, and that split is load-bearing rather than incidental. FalconView computes it in `draw` off the ship's drawn rectangle and tests it in `map_update` off the newly arrived fix; the north-up apron is a FUNCTION of the ship's position, so a camera that rebuilt it around the new position would be asking "may the ship be here?" of a box drawn around the ship being here — always yes, and the map never moves again. So the seam is two calls, `RecomputeApron` per frame and `Update` per fix, and a camera that has never drawn has an EMPTY apron and recentres immediately, which is the safe direction. Pinned by `TheApronIsBuiltFromTheDrawnPositionNotTheNewOne`, which asserts the very fix that recentred WOULD have been inside an apron built around it. **(2) THERE IS NO MERIDIAN-CONVERGENCE ACCESSOR AND THERE SHOULD NOT BE ONE.** The plan said MM2 would add one to the projection seam — "it is one formula on equal-arc". It is not: `EqualArcProj::get_convergence` (proj/equalarc.cpp:299) sets it to 0.0 and returns, and so does the Mercator branch of `LambertProj::get_convergence`. Only a genuine conic has a number to give and the port has no conic, so an accessor on `MapProjection` could only ever return zero, which would state the opposite of the truth — that the term is real and this projection has none. It is a defaulted `convergence_deg` argument instead, and the test asserts it is CARRIED (a convergence of 45 with heading 0 places the ship exactly where a heading of 45 does). **(3) THE TRACK-UP OFFSET IS NOT A ROTATION MATRIX AND MUST NOT BE "FIXED" INTO ONE.** `delta_x = d_X cos + d_Y sin; delta_y = d_X sin - d_Y cos` reads as a rotation with a sign error in its second row, and this session wrote a test asserting it was a reflection before working out that it is neither: in a Y-DOWN surface, AHEAD along the course is (sin, -cos) and RIGHT of the course is (cos, sin), so the formula is exactly `d_x * right + d_y * ahead` and is correct. Correcting the "error" would put the ownship ABOVE the map centre while it is heading north. Asserted by decomposition against those two unit vectors at 24 headings, per the standing rule that asymmetric behaviour needs a directional assertion of its own — no golden would have caught it. **(4) "CONTINUOUS" CENTRING IS CONTINUOUS ONLY WITHIN ONE OF THE PLACEMENT'S FOUR CASES.** Sweeping the heading a degree at a time slides the ship a few pixels round the perimeter, until the derivation changes case and it jumps most of a box — five such jumps on a square window, and due WEST is not one of them, because `<=` and the `f` sign flip land on different sides of each boundary. It is inherent to the algorithm and not to the `+ 0.5`: the discrete version is CHOOSING between a corner box and an edge box there, and interpolating an index does not make that choice gradual. Found by a test whose oracle ("one degree moves it under 20 px") was wrong, and kept as the finding it is — continuous mode wants MM3's slew as much as the discrete recentres do. **(5) THE ONCE-ONLY 360 WRAP IS A LATENT HAZARD, NOT A LIVE DEFECT**, and the first version of this row said otherwise. `point_angle >= 360.0` subtracts once rather than looping, but a heading in [0, 360) plus a rotation in [0, 360) is under 720, so one subtraction always suffices for the two terms the port supplies — 250 + 200 really does come out as 90. Only the unbounded third term can defeat it, so the pair of tests pins both: the heading/rotation case wraps correctly, and a convergence of 100 on top of 350 + 350 leaves 440 and takes the wrong branch. **(6) ONE THING IS DELIBERATELY NOT BIT-FAITHFUL, BECAUSE THERE IS NOTHING THERE TO BE FAITHFUL TO.** Out of domain `tan` is ~1.6e16 and the original's `(int)(1.0 - tan/w + 0.5)` is an undefined cast, not a stable numeric quirk; the port clamps the box index to the range the original's own ASSERTs claim, which in domain is the identity. Two other quirks ARE preserved and documented at their sites: the track-up apron's left edge is `2*(W/5)` and not `2W/5` (they differ on a width that is not a multiple of 5), and the continuous placement carries the discrete formula's `+ 0.5` rounding term as a permanent half-box offset. The original's `if (point_angle != 90.0 || point_angle != 270.0)` tautology — true for every value, so its else branch is dead — is ported as the live branch alone, with a test pinning that due east and due west come out as the boxes the dead branch names anyway. **27 new tests**, all headless; suite 1182, up from 1155. |
| MM1 | Moving map, first slice: the fix, the feed seam, the scripted source and the heading resolver (`fvkit/nav/`) (2026-08-15) | 31 | T | Forty-first session of the ACTIVE TRACK (G4 was the thirty-eighth; O6 and the along-path text fix, both 2026-08-15, wrote no rows), and the FIRST of `port/fvkit-nav-plan.md`. `fvkit/nav/{position,heading,scripted_source}.h` — the left-hand edge of the moving map, ported in SHAPE from `Applications/FalconView/MovingMapOverlay` with the COM feeds (`IGPSFeed`/`IMovingMapFeed`) severed at a plain C++ interface, D6's adapter shape. **(1) EVERY FIELD OF A FIX CARRIES ITS OWN VALIDITY, and the -1000.0 sentinels do not port.** FalconView spells "no latitude" as -1000, "no heading" as -1.0 and "no altitude" as -32767, and every consumer has to know which sentinel belongs to which field. The partiality is REAL — a GLL has no speed, a VTG has no position at all — so it is expressed once, as a `has_*` per field, and `PositionFix::Merge` is how two sentences of one epoch become one fix (pinned in both directions: an absent field never overwrites a present one). **(2) THE THREADING RULE IS STATED AT THE SEAM AND THE QUEUE IS BUILT NOW, NOT WHEN IT HURTS.** A source delivers on whatever thread it likes and the listener runs THERE, so a toolkit that owns its thread (tk, and every native shell) puts a `FixQueue` in between and drains it on its own tick — `queue.Listener()` is the `FixCallback` to hand the source, which makes the wiring two lines and no locking in the consumer. **A full queue drops the OLDEST fix and counts it**: a position feed is a stream of the present, so the fix the consumer has not read yet is the one that matters least, and `dropped()` lets a shell say so instead of silently animating history. `DrainLatest` is the common case — the camera only wants where the ship is NOW. **(3) THE SCRIPTED SOURCE HAS NO THREAD AND ITS CLOCK IS INJECTABLE**, which is what makes the whole layer testable without sleeping: `Start()` spawns nothing, `Poll()` asks the clock and emits what is due, and every test in the file drives a fake clock by hand — "the third fix arrives at t=2.5s" is an equality, not a tolerance. `time_scale` replays an hour in a minute; looping re-bases the origin by one script duration rather than resetting it to now, so a lap boundary drifts by nothing (pinned). A listener may `Stop()` the source it is being called from. **(4) THE TRACK BUILDER TAKES A POLYLINE, NOT A `Route`.** The plan says "a helper builds that vector from a Route"; `route.geometry` IS a `vector<GeoPoint>`, and taking the polyline keeps fvkit from linking `port/Routing` at all — so a demo track still drives on real Kiawah roads and the dependency stays where it was. Samples at a constant ground speed over geo_tool's great-circle range/bearing, and **the reported heading is the bearing to the NEXT SAMPLE, not to the far end of the leg**: on a long leg the great circle has turned by the time the ship gets there, and the first version of this had the built-in heading and the DERIVED heading disagreeing by 0.054 deg for exactly that reason. They now agree to 1e-3, which is the acceptance test that the derivation is right (`DrivesTheResolverByEitherBranchToTheSameHeading`). **(5) THE ONE DELIBERATE DEVIATION FROM THE ORIGINAL, AND IT IS A SIGN ERROR.** `get_current_heading()` computes `RAD_TO_DEG(atan2(delta_x, delta_y))` and then applies `if (dy<0) +=180 else if (dx<0) +=360` — which is the correct quadrant fix-up for **`atan`** (range -90..+90) applied to **`atan2`**, which has already resolved the quadrant. The two agree exactly on the northern half and on due east/west; where the ship is going SOUTH the +180 lands on a quadrant that was already right and the answer comes out **180 degrees opposed** — a ship tracking southeast is drawn tracking northwest. That is not a numeric quirk with a stable output to preserve under the bit-faithful rule, it is a defect a user sees the moment they turn south, so this port computes the bearing once with `atan2` and no fix-up. The original formula is kept **in the test** as the oracle it is not (`DivergesFromTheOriginalOnTheSouthernHalfOnly` asserts the agreement on the northern half AND the exact reversal on the southern one), so the deviation is asserted rather than described. **(6) THE DERIVATION STAYS IN SCREEN SPACE, which is the part of the original worth keeping.** It scales both deltas by the map's degrees-per-pixel before taking the angle, so the arrow points the way the track LOOKS on the chart — on an equal-arc map a true bearing of 045 does not draw at 45 degrees, and a symbol drawn at the true bearing visibly disagrees with the line of its own travel. `SetDegPerPixel` reproduces FalconView exactly; unset, it falls back to cos(lat) at the midpoint, which is the local tangent plane and so the true bearing — the honest default for a caller with no map. **(7) THE HISTORY IS BOUNDED, and it is the whole of the "trail" the moving map keeps** (the plan's no-breadcrumbs decision). The original walks its entire icon list back to the last DISTINCT point so that a stationary ship still points the way it was going; this port keeps 8 by default, because a ship that has been still for ten minutes should not claim the heading it had ten minutes ago. A fix with no position HOLDS the last heading rather than swinging the symbol back to north. Nothing is bound to Python and no shell consumes it yet — that is MM4 by design; MM2 (the faithful camera) is next. **31 new tests**, all headless; suite 1155, up from 1124. |
| G3 | Overlay drawing, third slice: `GeoDraw` — the surface an overlay calls, the line-preset table, and both consumers (2026-08-14) | 9 | T | Thirty-sixth session of the ACTIVE TRACK, and the third G-session. `fvkit/canvas/geo_draw.{h,cpp}` joins G1's contours to G2's libraries and the vector seam's placers, with **no new `ICanvas` operation** (R6) and **no new styling vocabulary** (R1) — `GeoLineStyle` is `{casing, stroke, pattern}` over the seam's own structs. **(1) THE CASING IS THE ONE THING THE PLAN DID NOT HAVE, and it is what the ask needed.** A route line over a chart is unreadable in one colour; the fix is the line equivalent of T2's text halo — the same geometry, wider, drawn FIRST — and the property worth not re-deriving is that **the casing must follow the PATTERN**, because a solid white bar under a dashed blue line reads as a solid white line with blue dashes painted on it. So a dashed line gets a dashed casing (one casing dash per line dash, which is how the test pins it: the ink is a bad witness because at any usable casing width the square nib at each dash end bridges most of the gap). `AddCasing` measures off whichever of stroke/pattern is live. **(2) A PATTERN REPLACES THE PLAIN STROKE.** `PresetGeoLine` turns `stroke.valid` off — leaving it on would draw the solid line the pattern exists to replace. (`VectorRenderer` keeps both because S-52 legitimately puts an `LC` over an `LS` casing; here the casing slot is where that lives and it is explicit.) **(3) THE PRESET TABLE IS THE PLAN'S FINDING, AND SOLID IS NOT IN IT.** Ten names over `BuiltinSymbolLibrary` — `LineSegmentRenderer.cpp`'s 913 lines and 15 classes are one operation, stamp a shape every N px along the path, which is `PathRun{kSymbol}` and `PlaceAlongPath`. `solid` and any UNKNOWN name return an INVALID `LinePatternStyle`: a one-run cycle would cost a placer walk to draw what `DrawLines` draws, and a style file naming a preset this build lacks should draw a line rather than nothing. A stamp advances nothing of its own — the dash run before it carries the period — so a preset's spacing reads off ONE number. **(4) PICKING IS OFF BY DEFAULT, the opposite of `VectorRenderer`.** That caller always wants identify; an overlay with its own analytic hit test (PointOverlay has one, and its ids are int64 document rows) should not pay for an index nobody reads. `SetFeature(id)` names the ink and the id comes back out of `HitTest`. **(5) THE LEDGER'S CLIPPED-LEG DEFECT IS CLOSED, and it was one virtual.** `IGeoContour::AtBreak()`, asked AFTER `NextPoint` and about the point that call produced, defaulting to false so every single-run contour is unchanged; `BuildGeoPathInto` flushes on it BEFORE the antimeridian test, since two points either side of a break are not neighbours. The python test that pinned the defect as-it-behaved was written so that fixing it would fail — it did, and it now asserts the opposite. **(6) ONE MORE VERBATIM EXTRACTION, same acceptance test as G2's.** `HaloOffsets`/`HaloPixels`/`LabelPixelSize`/`GlyphAdvances` moved out of `renderer.cpp`'s anonymous namespace into `fvkit/vector/text_draw.{h,cpp}`; copying them would have forked the halo, which is precisely what drifts into 'the overlay's text looks slightly different from the chart's'. Every golden byte-identical. **THE TWO CONSUMERS, which are the acceptance test.** `PointOverlay` draws its six shapes as builtin symbols (one `BuiltinSymbolLibrary` per colour, cached — colour is a LIBRARY setting because a display list carries its own colours) and its names through `DrawLabel` with a halo; the selection EDGE is the same symbol one size up stamped underneath, which retires the pen / thicker-pen / rectangle triple the old switch statement carried and leaves G4 one mechanism to replace instead of three. `route.py` draws **dashed blue over a white casing for a bicycle route, solid blue over a white casing for the default car route, and red straight legs when nothing has been calculated** — per Chris — so the MODE is visible in the line and not only in the status text; the bicycle test is on the profile NAME (`bike`/`cycle`) or `cycle_only`, because the rule file owns the profiles and a user may well call theirs `bicycle-winter`. Bound as `pyfvw.draw` + `pyfvw.symbol` in their own TU: `GeoLineStyle` is bound because a caller holds one across frames, `LabelStyle` and `PointSymbolStyle` are NOT because every field of them is a natural keyword. **21 new tests** (16 C++, 5 python); suite 1090, up from 1069. |
| G2 | Overlay drawing, second slice: `ISymbolLibrary` + four implementations, and the symbol-draw extraction (2026-08-14) | 8 | T | Thirty-fifth session of the ACTIVE TRACK, and the second G-session. **The interface was already written; G2 only named it.** `ISymbolLibrary` (`port/include/fvkit/symbol/library.h`) is exactly the three methods `IStyleEngine` already declared — `Symbol` / `Pixmap` / `himetric_per_symbol_pixel` — so `class IStyleEngine : public ISymbolLibrary` is a two-line change and GeoSym, S-52, OSM and `LookupTableStyleEngine` all became symbol libraries with **no other edit anywhere**. The symbol TYPES (`SymbolPrimitive`, `VectorSymbol`, `SymbolPixmap`) moved out of `vector/style.h` into the new header, which style.h then includes, so every existing include still sees all of them and not one consumer changed. **(1) THE EXTRACTION, and its acceptance test.** `DrawSymbolAt` / `DrawPixmapSymbolAt` / `ResolveSymbol` / `DrawResolvedSymbol` / `InkBox` were ~240 lines locked in `vector/renderer.cpp`'s anonymous namespace — everything an overlay wants to stamp a symbol, reachable only by the chart renderer. They moved verbatim to `fvkit/vector/symbol_draw.{h,cpp}`, re-typed to `ISymbolLibrary*`; `ToVectorSymbol` (CGM -> display list) came out of `fv_geosym_style.cpp`'s file-local namespace the same way into `port/GeoSymServer/fv_cgm_to_symbol.{h,cpp}`. **The acceptance test was that every pinned golden stayed byte-identical, and every one did** — the whole suite is green except the five pre-existing Osm failures (§2d). **(2) `SymbolPixmap::pixel_ratio` is the one behaviour added, and it defaults to the identity.** A sprite sheet states `pixelRatio` per sprite and a loose file states it in `@2x`; `DrawResolvedSymbol` divides the requested scale by it, so a 2x tile comes out the same SIZE as its 1x twin and merely carries more detail. Default 1.0, and dividing an IEEE double by 1.0 is the identity — which is exactly why the S-52 pixmap goldens (the only tiles in the tree before G2) did not move. **(3) A PIVOT LIVES IN TILE PIXELS, so a 1x sidecar on a 2x tile has to be SCALED.** Caught in review: `<id>.json` is authored against the 1x artwork, so applying it verbatim to `<id>@2x.png` put a map pin's tip halfway up the pin — visible only on a retina set. `<id>@2x.json` wins and is read as-authored; a plain sidecar is multiplied by the ratio. **(4) A RE-`Open` REPLACES A LIBRARY**, and getting that wrong is worse for a sheet than for a directory: stale entries index a DIFFERENT image. Both `Open*` now `Reset()`, and `OpenSheet` decodes into a local buffer first so a failed re-open is a no-op rather than a half-replacement. **(5) `BuiltinSymbolLibrary` re-bakes IN PLACE.** `SetColor` used to `clear()` the map, dangling every pointer `Symbol()` had handed out — against the base class's own lifetime promise, and against the resolve-once-stamp-many pattern that promise exists for. The id set is fixed, so assigning over the existing `std::map` nodes keeps every address stable. **The four implementations**: `PngSymbolLibrary` (loose files AND sprite sheet, lazy past the sheet image, which **closes §2b's "no sprite sheet, so OSM has no icons" as a LOADER** — one loader, two consumers), `CgmSymbolLibrary` (GeoSym's ~1500 `.cgm` as a library an overlay can reach, and a test pins that it agrees with `GeoSymStyleEngine::Symbol` primitive by primitive), `BuiltinSymbolLibrary` (13 ids the port authors as `VectorSymbol` literals: PointOverlay's six shapes on one circumscribed circle, five line decorations authored for `PlaceAlongPath` with +x along the line, a north arrow and an open-centred crosshair), and `CompositeSymbolLibrary` (ordered, and `Symbol`/`Pixmap` resolve INDEPENDENTLY so a pixmap-only member cannot shadow a later display list). **The composite's unit is ONE number and that is a stated limitation**: `himetric_per_symbol_pixel()` is asked without an id, so there is nowhere to put a per-member answer; it inherits the first member's, which is right because the builtins are authored on GeoSym's own 1/100-inch grid. **SVG stays deferred to G5** as the plan recommends — CGM is written and tested, SVG needs a path parser, a transform stack, an attribute cascade and Bézier flattening that `VectorSymbol` has no primitive for. **33 new tests** (29 fvkit + 4 GeoSym); suite 1069, up from 1036. |
| A5 | App layer, pick: `PickSession` (hover/click policies/snap-to/context menu) + the `VectorHitTest` adapter over the L4 `PickIndex` (2026-08-13) | 2 | T | Thirty-second session of the ACTIVE TRACK, and the fifth A-session. Plan §3e — the one place the plan REORGANISES FalconView rather than mirroring it (test_select's first-hit-wins veto becomes a ranked aggregation). **(1) "Who is on screen and in what order" now has ONE implementation, and picking is it reversed.** A2 kept that rule inside `DrawAll` (visible, bottom-up, then the top-most band); A5 needed the same answer and a second copy would have drifted, so `OverlayManager::DrawOrder()` is public and `DrawAll` is rewritten over it. The payoff is a test that would otherwise never have been written: **a top-most-flagged overlay outranks the stack index for PICKING exactly as it does for DRAWING**, so a click cannot land on the chart under a HUD the user can see. **(2) The plan was WRONG about the hit id and the correction is the design.** §3e says `HitItem::feature` "deliberately fits the ids `PickIndex` already emits" — but a `FeatureRef` is FOUR int32s, 128 bits, and the field is one uint64_t, so no packing keeps all four. `capabilities.h`'s own words are the ones that hold ("overlay-scoped feature id"): `VectorHitTest` mints a small dense handle per distinct ref it has reported and translates back through `RefFor()`. Handles are STABLE for the adapter's life, so a selection held across a redraw still names the same row — which a packed per-frame index could not have promised. The table grows only with what a human has pointed at. **(3) Hover notifies the shell ONLY ON A CHANGE**, same stance as `Persistence::set_dirty`: `UpdateHover` runs per mouse move and a shell rebuilding a tooltip sixty times a second because the cursor slid two pixels along the same road is the defect this call exists to avoid. Sameness is overlay + feature + cursor + both hint strings, deliberately NOT distance. `FakeShell` therefore records hint/cursor CALLS, not just last values — three moves along one road assert ONE hint. **(4) Two verbs are renamed and it is not taste**: `HitTest` and `SnapTo` are capability CLASSES in `fv::app`, so a member function of either name hides the type inside the class body and `o->AsHitTest()` stops compiling (caught by the compiler mid-session, on the first of the two). They are `HitTestPoint`/`SnapToPoint`, after the capability's own methods. **(5) A hover cannot ask a question**, so `kAskWhenAmbiguous` degrades to `kTopMost` for the cursor and hint rather than opening the chooser on a mouse move. **(6) Every ambiguity fixture puts the NEARER item LOWER in the stack** — the only arrangement in which `kTopMost` and `kNearest` disagree. A fixture where the topmost item is also the nearest passes under either policy and under a session with no policy at all. **(7) "Aggregated across ALL overlays" (snap-to) means every overlay that ANSWERS, not every overlay that EXISTS**: visibility and declutter are honoured, because a cursor that snapped to something the user cannot see is a jump with no cause. **(8) The two choosers take their rows from different places, and the capability contract is why**: a snap candidate carries its own `description` by contract ("the north end of runway 15" is not composable), while a hit's row is composed as `"<overlay>: <hint>"` — with several overlays answering, which one is answering is the point of the question. **(9) The section separator belongs to the CONTRIBUTION, not the attempt**: each `ContextMenu` appends into a scratch node, so an overlay asked and silent costs no separator; and an empty menu is never shown at all, because a menu flashing open with nothing in it is worse than no menu. **(10) `Describe` is memoised in the adapter** — it re-reads a row from the product and hover would call it per mouse move (test: twenty hovers, one `Describe`). **(11) An out-of-range answer from a shell chooser is treated as a CANCEL** rather than guessed at. **(12) Nothing here consults capture or `RoutingOverrides`**: routing runs first and picking resolves only what routing declined, and that sequencing is the shell's — by the time a `PickSession` sees a click the stack has already refused it. **Tests**: 33 new in `port/fvkit/app/test/app_pick_test.cpp` (**996 total**; the same 5 pre-existing §2d Osm data failures), ASan/UBSan clean. |
| A2 | App layer, stack v2: observers, a current overlay, the reorder verbs, insertion by display order, declutter, mouse capture and the three-phase route (2026-08-12) | 3 | T | Twenty-ninth session of the ACTIVE TRACK, and the second A-session. **The plan's `stack.h` is `fvkit/overlay/manager.h`** — `fv::OverlayManager` was grown in place rather than replaced, which is §3c's own instruction and also the only way R7 survives: `PythonView.py` and every pyfvw overlay keep working untouched. **(1) The whole of A2 is inert without a registry.** `SetTypeRegistry` is optional; with none, every overlay has display order 0 and no top-most flag, so `Add` is exactly the pre-A2 `push_back` and `DrawAll` is one pass. That is not a fallback bolted on afterwards — it falls out of the insertion rule, because "insert above the topmost overlay whose order is <= mine" over a stack of equal orders IS an append. Pinned by its own test, because it is the property that lets the app layer land under a running application. **(2) Insertion by display order is `AddOverlayToStack` with the list reversed** (FalconView's head is our back), including its stated **top bias**: the `<=` is what makes an equal order stack newest-on-top, and an overlay the user has dragged down does not drag later arrivals down with it. A top-most overlay compares only against other top-most ones and stops at the first overlay that is not flagged, so it lands above the lot **even when its display order is lower** — the flag is a separate band, not a bigger number (t.hud at 500 sits over t.high at 900, and there is a test that says so). **(3) The top-most band is a SECOND DRAW PASS, not a stack position**, mirroring `OnDrawOverlays` / `OnDrawTopMostOverlays`: a HUD dragged to the bottom of the stack still draws over everything. **`default_opacity` is carried and NOT applied** — `ICanvas` has no layer alpha to blend with; new §2b item, and it wants the same off-screen layer a pattern brush and a clip region want. **(4) Declutter affects DRAW as well as routing.** `show_other_overlays(FALSE)` runs through `RouteEventBottomToTop`, which is the draw path too, so this is one flag over both — and nothing is hidden: turning it off restores the stack exactly. **(5) The three-phase route, and the one deliberate deviation.** Phase 0 capture, phase 1 the direct-routing pre-pass, phase 2 declutter, phase 3 top-down. The pre-pass **ignores declutter**, as `C_ovl_mgr::select` does — a gesture in a background overlay cannot be abandoned because the user pressed declutter mid-drag. The deviation: FalconView's second walk starts at the head and skips nobody, so an overlay that *declined* the direct route was offered the same event **twice**; here an event is delivered to an overlay at most once (a small already-offered list), because an overlay counting clicks would count a declined one twice and nothing depends on seeing it again. **(6) Capture is exclusive for the mouse and first-refusal for keys.** Exclusive is the guarantee `m_drag`/`drag()`/`drop()`/`cancel_drag()` existed for — a declining capture holder still swallows the mouse, or the gesture is stealable. A KEY is different: Escape must reach the gesture first, but a menu shortcut it does not want still belongs to the stack, so an unhandled key routes on (and is not re-offered to the holder). A capture whose overlay has been **hidden** is treated as stale rather than a black hole, since routing skips invisible overlays everywhere else; removal drops it outright. **(7) `current` is explicit state, broadcast, and never a HUD.** An overlay that lands on top becomes current (FalconView's "added to the head" rule) unless it is top-most-flagged — the user does not work in a crosshair. Removing the current one drops to the topmost remaining non-top-most overlay; the "next of the same type" refinement is A4's, layered on this. `CurrentChanged` fires on a change only. **This is the session's one behavioural deviation from `C_ovl_mgr`**: the original asks "is this overlay the HEAD of the list", and a top-most overlay always IS the head — so with a crosshair open, nothing added afterwards ever became current again. "Topmost of the overlays the user works in" is what the rule means and is identical whenever no top-most overlay is open; it has its own test. **(8) The Persistence hook A1 left null is now filled**, through a private `PersistenceBridge` defined in manager.cpp rather than by making `OverlayManager` an `app::PersistenceObserver` — that is what keeps **manager.h free of every app-layer include**, so `fvkit/overlay` still builds alone exactly as A1 arranged. The bridge maps `Persistence&` back to its overlay by scanning the stack (tens of entries, on a document edit) instead of keeping a second map in step. The manager's destructor detaches the hook from everything still in the stack, because an overlay outlives the manager under D1. **(9) `Reorder` is all-or-nothing** — wrong length, a repeat, a null or a stranger is rejected whole and the stack is untouched, because a reorder dialog that has gone stale must not half-apply. `OfType` is TOP-DOWN so that `OfType(t).front() == FirstOfType(t)`, and `FirstOfType` is the TOPMOST of the type because that is the one an editor adopts (§3d.1). `FindByFileSpec` treats an **empty spec as no identity**, so two brand-new unsaved routes do not dedup onto each other. **Tests**: 25 new in `port/fvkit/app/test/app_stack_test.cpp` (**875 total**; the same 5 pre-existing §2d Osm data failures). FalconView's `OverlayEventRouter_UnitTests` ideas re-derived, not transliterated — they are MFC-bound. Notable ones: the no-registry append, the display-order and top-most placements, the HUD dragged to the bottom still drawing last, the lock outranking declutter, the declined direct route falling back **once**, capture surviving a decline and dying with a hide or a removal, an observer detaching itself from inside its own callback, and the dirty/file-spec broadcast stopping the moment the overlay leaves the stack. |
| R2 | Rule layer: `fvkit/vector/rules.h` + GeoSym retrofit (scale/group/category thinning) | 3 | P | Third session of the ACTIVE TRACK, 2026-07-26. **The cross-product MIDDLE of the vector seam**, per plan §5.2. New `port/include/fvkit/vector/rules.h` + `port/fvkit/vector/rules.cpp`: a `Predicate` AST (exists/missing, the six comparisons, in/not-in, and/or/not) with **one documented comparison rule** — numeric when BOTH sides parse *whole*, byte-wise otherwise, and a missing attribute makes every comparison false including `not in`; `ScaleBand` on the map-scale DENOMINATOR (0 = unbounded, and a scale of 0 matches everything so a bulk render never loses features to thinning it did not ask for); `ViewingGroupSet` (numbered groups + the IMO display-category threshold, `epoch()` bumping on real changes); `Rule`/`RuleSet` with a **rule-file syntax shared by all three products** (`hide key=BE010 scale=..50000`, `set key=DA010 priority=3 labels=off`, `hide key=BH140 where hdp exists and hdp < 3`) parsed by a recursive-descent predicate parser, all-or-nothing with the line number in the error. **`ResolvedPlan` is the point of the whole header**: scale + group filtering happen once per (scale, epochs) at compile time, then a `RuleDecision` is memoized per `{layer, style_key}`, so a key whose rules carry no predicate costs one hash lookup and **zero** predicate evaluations — measured on the real harbor render, not asserted. **GeoSym retrofit**: `fullsym.txt`'s `vgroup`/`txtgroup`/`radar`/`dispcat` columns are finally parsed (read non-fatally *after* the required chain, so a short row can't fail the table) and wired up — a viewing group toggles the rows that belong to it, a **text** group drops those rows' LABELS only (Q6c item 3, the dense-label problem), and `dispcat` is S-52's BASE/STANDARD/OTHER under another name, which is exactly why the category threshold lives in fvkit and not in the GeoSym engine. Bound to pyfvw (`RuleSet`, `ViewingGroupSet`, `engine.rules()`/`viewing_groups()` as live references, `DISPLAY_BASE/STANDARD/OTHER`). 26 hermetic rules gtests (no VPF, no GeoSym, no canvas — a seam leak fails the build) + 9 GeoSym gtests over real rows + 2 pytests. **376 total.** ASan+UBSan clean (only the pre-existing VPF unaligned loads). **NOT bit-faithful, and deliberately opt-in**: with an empty RuleSet and a default ViewingGroupSet nothing changes, so the harbor golden hash is UNMOVED — see the decisions below, including why the `LookupTableStyleEngine` extraction did NOT happen here. |
| E2 | ENC E2 (Q10): S-52 Presentation Library parse + S-57 Appendix A catalogue | 4 | T | Fourth session of the ACTIVE TRACK, 2026-07-27, and **the second real style table** §5.1 was waiting for. Port-native, `port/Enc/`, no FalconView source involved (there is none). **`fv_s52_preslib.{h,cpp}`** reads OpenCPN's `chartsymbols.xml` (2.2 MB, `TestData/enc/`, GPL-3.0 *as data*) through **expat 2.8.2 from `port/third_party`** — its first consumer, so the 2026-07-25 fetch is no longer unverified-by-a-real-user. Loads all **5 colour tables** (day/dusk/night is real, not a stub), all **3057 lookups across all five S-52 lookup tables** (paper/simplified points AND plain/symbolized areas — which pair is in force is a mariner setting made at style time, so all five load), **1018 symbols** (from 1093 elements, see below), 59 line-styles, 30 patterns. Lookup rows carry the parsed `S52Instruction` list (`SY`/`LS`/`LC`/`AC`/`AP`/`TX`/`TE`/`CS`), split on `;` and `,` **outside single quotes** so a TE format string keeps its own commas. **`ParseS52Hpgl`** flattens the symbol library's HPGL to the seam's `VectorSymbol`: S-52 authors in 0.01 mm, which IS HIMETRIC, so geometry passes through unscaled — only the pivot-to-origin shift and the **y flip** (PresLib y is DOWN) happen, and `SWn` becomes n×32 HIMETRIC (S-52's 0.32 mm pen unit). Verified visually by rasterising: the anchor stands upright with its flukes down, the conical buoy on its base. **`fv_s57_catalog.{h,cpp}`** closes E1's stated gap — `OBJL 42 → DEPARE`, `ATTL 87 → DRVAL1`, plus `s57expectedinput.csv`'s 1467 enumerated values, which is what gives ENC a real `Describe()` in the R1 sense, and the `Class` column that E1 needed for the DSSI meta/geo/collection split. 44 gtests (10 hermetic HPGL + 3 instruction + 4 CSV + 17 PresLib + 10 catalogue); ASan+UBSan clean over all 87 ENC tests. **420 total.** **Three data facts the corpus sweeps found, all now pinned** — see Decisions. **NOT here (E3/Q11)**: `S52StyleEngine`, the CS-procedure registry, the along-path placer, and the `LookupTableStyleEngine` extraction, which now has both tables in hand. |
| S2–S4 (search) | Global search, the rest: `VectorMapOverlay` + tier 1, the staged `search_names` index + tier 2, and the road provider / bindings / shell (2026-08-28, three sessions in one day) | 6 | P | **The SEARCH track is finished** — `port/search-plan-COMPLETE.md` and the S2/S3/S4 bullets in the working ledger's §1b carry the detail, and nothing here repeats them. What is worth knowing from the archive's own vantage is the SHAPE the three sessions settled into, because it is the shape the next capability should copy. **S2 wrapped a MAP in an overlay so that discovery has ONE path** — `VectorMapOverlay` over any `IVectorSource`, generic rather than OSM-specific (the label field is a knob), and the two rules tier 1 does not work without: unnamed geometry is never a row, and the tile-cut pieces of one road merge into one. **S3 put the gazetteer INSIDE the pack** rather than beside it, as extra SQLite tables in the `.mbtiles` that every other reader ignores — and made the builder go through the live scan (`ScanRows`) so an indexed answer and a scanned one cannot drift; the merge became `fvkit/vector/feature_rows.h` for exactly that reason. **S4 found the road graph already was an overlay**, so the routable answer is `RoadGraphOverlay::AsSearch()` and its ids are the ones a click already carried; it bound the seam into `pyfvw.app` (including a `CancelFlag`, since `std::atomic<bool>` has no Python face), gave the Python trampoline its seventh capability, and put a Find box in PythonView — where the finding that generalises is that **a shell can only search what is in the STACK**, so the chart and the graph are now held there NOT VISIBLE, which is what decision 5 was written for. 71 gtests across the three (26 + 29 + 16) plus 7 pytest cases and a `find_box` selftest step. |
| S1 (search) | Global search, first slice: the `AsSearch()` capability, `SearchSession`, the shared match rule, and the point/route providers (2026-08-27) | 6 | T | First session of the SEARCH track (`port/search-plan-COMPLETE.md`), and the second aggregating capability after A5's pick. **It is deliberately not a flavour of `HitTest`**: pick is pixel-space, per-frame and visible-only, and search is geo-space, on-demand and finds what is off screen and switched off — same pattern (opt-in capability, aggregating session, caller-chosen ordering), different contract. `Overlay::AsSearch()` is the SEVENTH accessor (R2's rule, never `dynamic_cast`) and the only one that is not about the cursor. **Six things are worth not re-deriving.** **(1) The provider picks the FIELD, the seam picks the MEANING.** Each source owns its label knowledge — `PointOverlay` matches `name` falling back to `category` (the same chain `SnapToPoint`'s description follows, so a search and a snap call a thing the same words), `RouteOverlay` matches waypoint labels and the route name — but what "rud tur" MEANS is `TextMatchQuality` in the header, shared: 0 exact, 1 whole-string prefix, 2 every query token a prefix of some candidate token, -1 no match, and an empty query matches everything at 0 so a spatial-only search is not a text search everything fails. EVERY query token must land, because typing a second word has to NARROW a search. Folding is ASCII and that is stated rather than accidental: the usual shortcut (`tolower` per byte over UTF-8) corrupts multi-byte sequences, and the correct answer is ICU, which the port does not carry — so bytes above 0x7F compare exactly and S3's FTS5 index will have the same property. **(2) The circle is folded into a BOX exactly once, in the session, and no provider ever implements one** — `near`+`radius_m` becomes `q.area` (intersected with the caller's own area) before the walk, and the EXACT cut runs afterwards on the merged list, because a box corner is 41% further out than its inscribed circle and a "within 500 m" that returned 700 m would be wrong rather than generous. The test proves it from both ends: the provider is asked about a corner it says yes to, and the session is what throws it away. **(3) Ordering is the SESSION's** (only the aggregator sees all the streams, and N providers must not each compute a distance their own way): `kAuto` resolves to `kBestMatch` when `text` is set and `kNearest` when it is not, and stack order is the stable tie-break for free — the walk is topmost-first and the sort is `stable_sort`, exactly `PickSession::Gather`'s trick. **(4) `visible_only` defaults FALSE, the deliberate opposite of pick**: "where is X" is a legitimate question about a hidden layer, so the walk is the whole stack topmost-first, and only `visible_only` narrows it to `DrawOrder()` reversed (declutter included) — pick's exact set. **(5) `max_results` binds at both ends and the second one matters**: a provider is capped as it appends AND the merged list is cut after RANKING, so what survives a chatty overlay sitting on top of a small one is what answered the question rather than what happened to be walked first. 0 is no cap. **(6) The metre is borrowed, not re-derived** — `SearchDistanceMeters` is `ProjectOntoSegment(a, b, b, ...)` on a DEGENERATE segment, which `fvkit/nav/road_snap.h` documents as filling `distance_m` anyway, so a "within 500 m" search and a 500 m road snap are the same 500 m as the road graph's arcs. **The one real bug this session had, and the test found it**: the radius-cut compaction did `results[keep] = std::move(results[i])` with `keep == i` for every row until the first drop — self-move-assigning a `std::string` is UB and in practice EMPTIES it, so a title vanished on the way through a filter that KEPT its row. Provenance is flat `Overlay* + uint64_t`, `HitItem`'s shape (S2 mints a `FeatureRef` down to 64 bits privately); a route's own row is `feature == 0` because minted waypoint ids start at 1, and its `bounds` is the waypoint box with longitudes unwrapped against the first, or a Pacific crossing frames the planet. A route with no waypoints is deliberately not findable — every field that matters is derived from them. 34 gtests (25 `app_search_test.cpp`, 9 `route_search_test.cpp`, the last of which is the plan's acceptance: one query, two overlay types, and a caller that names neither `name` nor `label`). |
| — | PythonView: pan_viewer upgraded to an application (2026-07-25, per Chris) | — | P | Not a port step. `demo/pan_viewer.py` → **`port/apps/PythonView.py`** (git mv; the demo dir is gone): a tkinter/ttk app (stdlib-only on top of pyfvw+numpy — deliberately no Qt, nothing to version-match against the built `.so`) with a native menu bar over every family the port renders. **Map menu** groups catalog series into 4 families: Raster Charts (cadrg, gpkg) / Imagery (geotiff, tiros) / Elevation (dted-shaded — its FIRST UI exposure; series carry scale 0 so -/= drives an explicit display 1:N through `set_physical_scale(denom, 0, mm)`, defaults per level) / Vector Charts (vpf; one family on purpose — ENC/OSM slot in beside DNC when their engines land, and the menu shows them as disabled "Planned" entries). **Coverage overlay** (`c`): per-viewport `select_by_geo_rect` footprints, one color per format + on-canvas legend with in-view counts, antimeridian rows drawn as 2 boxes, per-family toggles in the Overlays menu. **Catalog UI**: first-run scan prompt, Add Map Data (auto-detect incl. a dht-walk for VPF databases), Manage Data Sources dialog (reads the catalog SQLite read-only for listing — no new C++ binding needed), New/Open catalog. **Window resizes freely** (debounced re-render at the new surface size), drag-pan + wheel zoom, Go To Location (DMS/MGRS via parse_location), Options dialog (pixel pitch, GeoSym dir, vector brightness/contrast via SetColorAdjust), Tools→fvpack pointer, `--shot`/`--series`/`--at` headless CLI, and **`--selftest`** (scripted UI walk: every family + coverage + resize, snapshots to build/pythonview_selftest; steps are CHAINED with idle gaps — absolute after() schedules starve the event loop behind slow renders and the wm Configure round-trip never lands). Sparse-coverage centering fixed: series centroid snaps to the nearest frame when it lands in a gap (CADRG samples). **Findings for the pending list**: (1) **big-endian ('MM') GeoTIFFs fail to decode** — 18 of 29 TestData tiffs; enumeration reads their headers fine so they catalog, then `CGeoTiff` load fails ("Error in byte order string: M") and (2) **one bad frame aborts `MapEngine::RenderBaseMap`'s whole composite** — the app catches the FvError and shows it in the status bar, but the engine should skip-and-log per frame. (3) WVSPLUS scans into the catalog (216 tiles) but its libraries open empty (no FCA — known 14t deferral), so those menu entries render nothing until FCS-based enumeration lands. |
| — | Dependency modernization wave 1 (2026-07-25) | — | T | Not a port step — per Chris, "move to modern versions where available". New `port/third_party/CMakeLists.txt` is the single place library versions live (FetchContent, gtest pattern). **gtest 1.14→1.17.0, zlib 1.2.5→1.3.2 (wired in: `fv_z` is now an INTERFACE onto `fv_zlib`; libpng/libtiff consume it unchanged), expat 2.1.0→2.8.2 (new; first consumer is the E2 PresLib loader).** 3 expat smoke tests added. `codecs_test`'s `EXPECT_STREQ(zlibVersion(), "1.2.5")` replaced with `zlibVersion() == ZLIB_VERSION` + a `ZLIB_VERNUM >= 0x1300` floor — a literal would just drift on every bump (same lesson as the 2026-07-23 TestData refresh), whereas header-vs-library agreement catches the real bug class. **267 total.** libpng/libtiff/jpeg deferred as module-sized API breaks and GEOTRANS deliberately frozen — see the dependency-modernization table above. `third_party/` and `fvw_core/ImageLib/*` untouched; the Windows product build is byte-unchanged. |
| — | TestData refresh 2026-08-12 (Kiawah OSM extract re-exported) + the family files became settings | — | T | Not a port step — Chris replaced the `map*.osm` Kiawah exports with a wider one and rebuilt `TestData/OSM/kiawah.fvroad` from it. **17 routing tests and 1 family test broke; all four causes were the fixtures, not the code.** **(1) The input list was NAMED, not enumerated.** `KiawahInputs()` in both routing test files hard-coded `{map.osm, map-2.osm, map-3.osm, map-4.osm}` and the refresh ships THREE files, so 15 tests died with `kNotFound` from `BuildRoadGraph` — a failure that says nothing about routing. Both now enumerate `map*.osm` from the directory and sort, which is the ledger's own "a directory a person refills gets structure, not a count" rule applied to the input list. That alone fixed 15. **(2) Two reader pins re-pinned against the new `map.osm`**, verified independently of the reader before being written down: nodes **14,450 -> 30,799**, ways **747 -> 1,605**, highway ways **245 -> 493** (a raw grep for `k="highway"` now counts 541; the difference is node tags, which is why the count is taken from ways). Bounds moved SOUTH-WEST only — min lat **32.5591137 -> 32.5089149**, min lon **-80.1965704 -> -80.2401359** — while both maxima are unchanged, so the new export is a superset in the same place. `first_node_id` 110069523 unchanged. These are pins on ONE NAMED FILE, which the rule allows; the comment now says out loud that the file is one a person replaces. **(3) `Router.EachMetricMinimisesItsOwnQuantity` lost its sample, not its invariant.** Every assertion in it passed; the guard `compared > 20` failed at 15, because the wider extract adds fringe faster than it connects it and the fixed pair generator now lands ~25%% of pairs in one component instead of ~33%%. The FLOOR is what makes the test mean anything, so the loop went 60 -> 200 (matching its sibling `BidirectionalMatchesDijkstraEverywhere`) and the floor 20 -> 40, measured at 66. Lowering the floor would have been the wrong repair: an invariant nothing exercises always holds. **(4) The family test was pinning the USER'S SETTINGS.** `TheShippedStarterFilesLoadAndAreNeutral` asserted `disabled_count == 0` over `port/families/*.json` — true for one day, until five ENC families were switched off. That is the pinning-a-total trap one level up: the file is settings, and editing settings must not break the build. Now `TheShippedFilesLoadAndAgreeWithTheirOwnFlags`, which pins the MECHANISM — every selector parses (still the main value, since a typo cannot otherwise be noticed until someone switches that family off) and the emitted rule count equals the selector count of whatever is disabled. Same change in the pyfvw test. **811 total, unchanged**; the five failing Osm tests are §2d's pre-existing ones. |
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

- 2026-08-27 (FAA sectionals): **A format the reader "supports" can still be unreachable in
  three independent places, and the port only finds out when someone brings real data.**
  Reported by a Windows user of Peregrine who went looking for aeronautical test data: FAA
  publishes every VFR sectional as a public-domain georeferenced GeoTIFF, and **none of them
  loaded** — the catalog scan just said `skipping unsupported frame`. Three causes, in the order
  a file hits them.
  **(1) LZW was rejected at the compression tag.** Both readers accepted only uncompressed,
  PackBits and JPEG; LZW got an explicit refusal. That is a 1990s decision — the Unisys LZW
  patent kept LZW out of a lot of readers until it expired in **2003** — and it had never been
  revisited, so it now excludes the ordinary encoding for a palettised chart. The reporter also
  noticed the message they got was the `default` arm rather than the LZW arm, and they were
  right and it mattered: **there are two readers**, and the one behind `CGeoTiffFrameFile::load`
  (the catalog's) had no LZW case at all while ImageLib's had a named one. Chasing that
  discrepancy is what surfaced the real trap — **`CGeoTiff::decompress_lzw` already existed and
  was `decompress_packbits` with its error strings renamed.** Nothing had ever reached it,
  because the compression tag refused LZW first. Turning LZW on without reading that function
  would have produced confident garbage instead of a clean failure; **an unreachable
  implementation is not evidence that the feature is half-done.**
  **(2) Spec-correct Lambert Conformal Conic 2SP was rejected.** The geokey check demanded a
  MIXED pair — `ProjNatOriginLong` (3080, which is **1SP's** spelling) together with
  `ProjFalseOriginLat` (3085, which is 2SP's). Per the GeoTIFF spec a 2SP Lambert is
  parameterised by `ProjFalseOrigin{Lat,Long}` throughout, and FAA's files write exactly that.
  The mixed requirement is the signature of a check written against whatever libgeotiff output
  was on the desk rather than against the spec. Both spellings of both axes are now accepted,
  and the transforms take whichever is PRESENT — so every file that loaded before loads with
  the same numbers it always had, which is why no golden moved. The same fallback was extended
  to `ProjFalseOrigin{Easting,Northing}` (3086/3087), 2SP's spelling of the two the transforms
  were reading from the 1SP keys.
  **(3) On Windows neither of these is fatal and on POSIX both are** — `fv_geotiff_frame.h` had
  already written down the mechanism: a `NOT_SUPPORTED` frame falls back to the ImageLib COM
  object on Windows, and that fallback does not exist here. A chart that merely degrades to a
  second reader over there is invisible over here. **A limitation recorded only in the abstract
  had a large, free, public data source sitting behind it.**
  **How it was wired**, both readers the same way: one new `decompress_strip` that dispatches on
  the compression scheme and then undoes the predictor, with every reader's PackBits arm
  relabelled to serve LZW too (35 arms in `CGeoTiff`, 8 in `CGeoTiffFrameFile` — scripted, per
  the mechanical-transform rule). PackBits with predictor 1 still goes through
  `decompress_packbits` and nothing else, byte for byte. The predictor (tag 317) had never been
  read at all; it is now parsed, implemented for 8- and 16-bit horizontal differencing, and
  refused with a message otherwise.
  **How it was verified, and this is the part worth reusing**: correctness of a decoder is not
  "the picture looks right". The decoded RGB was compared **byte for byte against libtiff's own
  decode** of the same file (via Pillow) at five blocks — origin, mid-sheet, the bottom-right
  corner block, a single-pixel-tall last row, and an odd-offset interior block — all zero
  differing bytes, plus a `tiffcp -c lzw:2` re-encode to exercise the predictor. Only then was
  it rendered. One genuine surprise fell out of that comparison and is NOT a bug: the
  uncompressed arm of `get_8bit_palette_as_rgb_subimage` rewrites pure black to `(1,1,1)` for
  the COM layer's transparency, and the compressed arms never have — a 2.45% "difference" that
  is the original's own deliberate asymmetry.
  Fourth item from the same report, unrelated and one line: CMake 4.4 renamed FindSQLite3's
  imported target to `SQLite3::SQLite3` and deprecated `SQLite::SQLite3`, while CMake 4.2 and
  older define only the old name — so `port/fvkit/CMakeLists.txt` asks `if(TARGET ...)` and uses
  whichever this CMake actually provides, rather than picking one and breaking the other.

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

---

# Condensed out of the working ledger — 2026-08-16

The working ledger `port/PORTING.md` had grown to 1230 lines, most of it the build narrative for
work that is finished. Everything below was moved here **verbatim** on 2026-08-16 so the ledger
could go back to being open work plus the invariants a new session needs. Nothing was deleted.
The ledger's condensed §1 names each area and points here.

## A. The pre-condensation §1, "What exists today"

This is the full build narrative for the vector seam, G1–G4 (contours, symbol libraries, GeoDraw,
render state), MM1–MM4 (the moving map), A1–A6 (the app layer), the routing work O4–O5e and the
three vector products. Each block's "N things worth not re-deriving" is repeated in the matching
archive row; this is the version the ledger carried.

## 1. What exists today (so you don't go looking)

**Geo/math**: `port/{geoid,geo3,geo_tool,geotrans,MapScaleUtil,MapSeriesStringConverter}` — all tested.
GEOTRANS 3.3 is **frozen** (pinned bit-faithful results).

**Raster products** — each is an `IRasterSource` + enumerator, self-registered in the format registry
(7 builtin: `geotiff`, `cadrg`, `tiros`, `dted`, `dted-shaded`, `gpkg`, plus the vector products):
`port/{ImageLib,ImageLibCore,CadrgDecoder,CadrgMapServer,GeoTIFFMapServer,TirosMapServer,DtedMapServer,DtedShadedRenderer}`.

**FvKit** (`port/include/fvkit/`, impl `port/fvkit/`): `geo.h`, `raster.h`, `proj.h` (equal-arc +
`SetPhysicalScale` + **PR1's `SetRotation`** — a clockwise turn about the surface centre, carried by
both transforms and by `VmapBounds`, which returns the box of the TURNED viewport; rotation 0 is the
byte-exact identity. **BOTH PATHS DRAW THROUGH IT**: the VECTOR path since PR2 — geometry, along-path
labels and north-up point symbols turn, point labels stay upright, picking follows the ink — and the
RASTER path since PR3, where `MapEngine::CompositeRow` gates on the rotation and a turned frame is
resampled through the turned projection and MASKED to its own edges. Bound as
`MapProjection.set_rotation` / `.rotation` and `MapEngine.set_rotation`), `engine.h` (MapEngine), `catalog/` (SQLite+R-tree), `canvas/` (ICanvas + CpuCanvas),
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
`BuiltinSymbolLibrary` per colour, cached — colour is a library setting, and the badge's black
edge is the same symbol one size up stamped underneath, which retires the pen/thicker-pen/
rectangle triple it used to carry; **selection was that edge painted yellow until G4 made it a
`RenderState`**) and its labels through `DrawLabel` with a halo; `port/apps/route.py` draws
**dashed blue over a white casing for a bicycle route, solid blue over a white casing for the
default (car) route, and the overlay's own red straight legs when nothing has been calculated** —
the mode is now visible in the LINE and not only in one line of status text.

**Render state** (G4, `RenderState{kNormal,kHighlighted}` in `fvkit/canvas/geo_draw.h`) —
**what a draw MEANS, as against how it is styled**, and it is `GeoDraw::SetState` plus
`SetHighlight(colour, width_px)` (default FalconView's selection yellow at 3 px). The mechanism
is **T2's stamped halo, reused verbatim**: `HaloOffsets` — the same function, so a highlight and
a text halo can never drift apart — gives 4-8 offsets, the ink is stamped at each of them in the
highlight colour, and the normal pass goes over the top. **Nothing new from `ICanvas`** (R6), so
every backend including pyfvw's Python subclasses has it. Bound as `pyfvw.draw.RenderState` +
`GeoDraw.state` / `set_highlight` / `highlight_draws`.
**The property that makes it a replacement and not a rewrite: a highlighted thing is still drawn
as ITSELF.** Both consumers used to swap a colour by hand, which said "selected" by throwing away
the one thing that said *which* — `PointOverlay` painted the selected marker's edge yellow
(the black outline vanished with it) and `route.py` re-baked its marker library per waypoint.
Both now keep their own colour and gain a band; `point_overlay_test`'s
`ASelectedMarkerGAINSABandRatherThanRecolouringOne` pins the three bands in order (highlight,
edge, fill) because the pre-G4 test could not tell a gained band from a recoloured one.
Four decisions worth not re-deriving:
1. **A LINE takes one wider stroke, not eight offset ones.** A line has no interior for the
   offsets to reveal — which is exactly what makes the stamp worth its cost on a GLYPH — so
   offset-stamping draws the identical picture for eight times the work. It goes under the
   CASING too, since a casing is part of what is being highlighted.
2. **A highlight never enters the pick index and never counts as a draw.** A selected feature
   must not become a bigger target than an unselected one, or a click between two markers would
   prefer whichever is already selected. One scoped `AsHighlightPass` diverts both.
3. **The highlight goes on a marker's OUTERMOST stamp and on that one only.** A `fv.points`
   marker is up to three stamps deep (edge, badge, icon); highlighting each would draw the
   badge's glow over the edge and the icon's over the badge, and it would read as a set of rings
   rather than as one selected thing. The label is not highlighted either — a yellow-outlined
   name is less legible over a chart than the white halo it already has.
4. **The symbol seam grew a TINT** (`DrawSymbolAt`/`DrawPixmapSymbolAt`/`DrawResolvedSymbol`
   take `const FvColor* tint`, defaulting to null = the identity, which is what left every
   pinned golden byte-identical). A display list has colours to replace; a **tile has only
   pixels**, so the tint replaces RGB and KEEPS ALPHA — an icon set is black-on-transparent and
   a tint that ignored alpha would stamp a coloured square instead of the icon's shape.
   `PatternPaths` also gained the tint for a pattern's STAMPS, which `pen_override` cannot
   reach; the casing deliberately does NOT use it, because its stamps have come out in the
   library's own colour since G3 and the G3 assertions are pinned over that. The stamp's growth
   under a highlight is **capped at 2x** (the casing's is not): the ratio is the widened pen over
   the plain one, and a 3-px highlight on a 2-px railroad would otherwise ask for 4x crossties,
   which is not that railroad glowing but a coarser one drawn underneath.
**DIMMING IS STILL DEFERRED** (Chris 2026-08-13) — §3d of the draw plan holds the two decisions
already worked out, and nothing in G1–G4 is shaped around its absence.

**Moving map, the feed end** (MM1, `port/include/fvkit/nav/` + `port/fvkit/nav/`) —
`position.h` (`PositionFix`, `IPositionSource`/`PositionSourceBase`, `FixQueue`),
`scripted_source.h` (`ScriptedSource`, `BuildScriptedTrack`) and `heading.h`
(`HeadingResolver`, `ScreenBearingDeg`). Ported in SHAPE from
`Applications/FalconView/MovingMapOverlay`, with `IGPSFeed`/`IMovingMapFeed` severed at a plain
C++ interface (D6). Six things worth not re-deriving:
1. **Every field of a fix carries its own validity and the -1000.0 sentinels do not port.** A
   partial fix is what the NMEA sentences genuinely deliver (a GLL has no speed, a VTG has no
   position), so the partiality is stated once as a `has_*` per field; `Merge` is how two
   sentences of one epoch become one fix.
2. **A source delivers on whatever thread it likes**, so `FixQueue` (mutex + drain-on-tick) is
   the crossing and was built in MM1 rather than when it hurts — `queue.Listener()` is the
   callback to hand the source. A full queue **drops the OLDEST and counts it**: a position feed
   is a stream of the present.
3. **`ScriptedSource` has no thread and its clock is injectable** — `Poll()` asks the clock and
   emits what is due, so a whole flight is one loop with no sleeping and every timing assertion
   is an equality. `time_scale` replays faster than real time; looping re-bases by one script
   duration, so a lap boundary does not drift.
4. **`BuildScriptedTrack` takes a POLYLINE, not a `Route`** — `route.geometry` is exactly that
   argument, and fvkit therefore still does not link `port/Routing`. The heading it stamps is the
   bearing to the **next sample**, not to the far end of the leg, which is what makes it agree
   with the heading `HeadingResolver` derives from the same positions.
5. **The heading derivation stays in SCREEN space** (deg-per-pixel scaled) — that is the part of
   `get_current_heading()` worth keeping, because on an equal-arc map a true bearing of 045 does
   not draw at 45 degrees and an ownship symbol at the true bearing disagrees with the line of its
   own travel. `SetDegPerPixel` reproduces FalconView; unset falls back to cos(lat).
6. **One deliberate deviation, and it is a sign error, not a quirk**: the original applies an
   `atan`-shaped quadrant fix-up to `atan2`, so a SOUTHBOUND track comes out 180 degrees opposed
   (southeast is drawn as northwest). This port computes the bearing once with `atan2` and keeps
   the original formula in the test as the oracle it is not. See the MM1 archive row.
**Moving map, the camera** (MM2, `port/include/fvkit/nav/camera.h` + `port/fvkit/nav/camera.cpp`) —
`MovingMapCamera` plus the four pure pieces it is made of (`ComputeApron`, `DeltaXyDiscrete`,
`DeltaXyContinuous`, `DeltaXyTrackUp`) and an `ApronRect`. Ported verbatim from `gps_draw.cpp`'s
`map_update` / `auto_center_bounding_box_calc` / `set_new_map` / `get_delta_xy_*` and the
anchor fractions at `gps.cpp:114`. **The camera never touches the engine**: `Update` returns a
`CameraTarget{changed, center, rotation_deg, rotation_changed, delta_x/y, world_escape}` and the
shell applies it, which is what makes every geometry case a unit test with no window and no pump.
Six things worth not re-deriving:
1. **The apron is built from where the ship was DRAWN and tested against where it has just
   moved to**, and that ordering is load-bearing, not incidental. The north-up apron is a
   function of the ship's position; rebuilding it around the NEW position asks "may the ship be
   here?" of a box drawn around the ship being here, and the map never moves again. So the seam
   is two calls — `RecomputeApron` per frame, `Update` per fix — exactly as FalconView splits it
   between `draw` and `map_update`. An undrawn camera has an EMPTY apron and therefore recentres
   on the first fix, which is the safe direction.
2. **There is no meridian-convergence accessor and there should not be one.** The plan said MM2
   would add one to the projection seam; `EqualArcProj::get_convergence` returns 0.0, and so does
   the Mercator branch of the Lambert one — only a genuine conic has a number, and the port has
   no conic. An accessor that could only ever return zero would state the opposite of the truth,
   so convergence is a defaulted argument that is CARRIED into `point_angle` and pinned as such.
3. **The track-up offset is not a rotation matrix and must not be "fixed" into one.** It reads as
   one with a sign error in the second row; it is `d_x * right-of-course + d_y * ahead` in a
   Y-DOWN surface, where ahead is (sin, -cos). Correcting the sign puts the ownship ABOVE the
   map centre while it is heading north. Asserted directly by decomposition, per the ledger's
   rule that asymmetric behaviour needs its own directional test rather than a golden.
4. **"Continuous" centring is continuous only WITHIN one of the placement's four cases** — a
   one-degree step moves the ship a few pixels until the derivation changes case, where it jumps
   most of a box (five such jumps on a square window; due west does not, the boundaries are not
   symmetric). Inherent to the algorithm, not to the `+ 0.5`: the discrete version is CHOOSING
   between a corner box and an edge box there. It says continuous mode wants MM3's slew as much
   as the discrete recentres do.
5. **The once-only 360 wrap on `point_angle` is a latent hazard, not a live defect.** A heading
   and a rotation are each in [0, 360) and cannot sum past 720, so one subtraction always
   suffices for the terms the port supplies; only an unbounded convergence can leave the angle
   out of domain. Preserved and pinned both ways.
6. **One thing is deliberately NOT bit-faithful, because there is nothing there to be faithful
   to**: out of domain the original's `(int)` cast of a ~1e16 `tan` is undefined behaviour, so
   the 3x3 box index is clamped to the range the original's own ASSERTs claim. In domain the
   clamp is the identity. Two other quirks are preserved as-is and documented at their sites: the
   track-up apron's left edge is `2*(W/5)` and not `2W/5`, and the continuous placement carries
   the discrete formula's `+ 0.5` rounding term as a permanent half-box offset.
**Moving map, the slew** (MM3, `port/include/fvkit/nav/camera_slew.h` + `port/fvkit/nav/camera_slew.cpp`) —
`CameraSlew` between MM2's `CameraTarget` and the engine, plus `SlewSettings`/`SlewState`/
`SlewEasing` and the free `ShortestRotationDelta`. **This one has no Windows original** —
FalconView jumps — so every rule in it is a decision rather than a port. `Retarget` takes the
camera's answer, `Advance(dt)` is one frame, and `dt` is an ARGUMENT, so every case is an exact
equality with no clock. Five things worth not re-deriving:
1. **Duration 0 IS the FalconView jump**, reachable through this class rather than around it —
   `Retarget` lands and the next `Advance` reports it. **The rate caps do not resurrect it**: a
   cap that turned an asked-for jump into an animation would take the jump from its one caller.
2. **The interpolation is in GEO, and the plan's "surface for short moves, geo for long ones" is
   a distinction equal-arc does not have** (surface is affine in lat/lon, so the two lerps are
   the same line to within dpp drift). Geo wins because it is the frame that SURVIVES the
   projection being re-centred by this very animation. Longitude goes the short way
   (`UnwrapLonNear`), pinned across the antimeridian.
3. **The caps EXTEND the duration and never clip the motion** — a clipped pan lands short of
   what the camera said, which is a lie the next fix must correct. They are in PIXELS/s (1200)
   and deg/s (120), because screen speed is what a user perceives; dpp is the only thing
   `Retarget` wants the projection for.
4. **The centre and the rotation share one duration**, the longer of the two, because they are
   one motion; rotation takes the short arc, range (-180, +180].
5. **A new target retargets in flight and never queues**, restarting from where the map now is
   so nothing jumps. The cost is stated: restarting restarts the EASE, so continuous centring
   wants a short duration or `kLinear` — which is the answer to MM2's finding (4).
Three smaller rules, each tested: a target with `changed` false is ignored (not an
interruption); retargeting where we already are STOPS rather than restarts; `Reset` is how a
shell says the map moved behind the slew's back, and it cancels and reports nothing. `Finish`
lands now.
**Moving map, the ship** (MM4, `port/include/fvkit/overlay/moving_map_overlay.h` +
`port/fvkit/overlay/moving_map_overlay.cpp`) — `fv::MovingMapOverlay`, the object that holds
MM1's feed, MM2's camera and MM3's slew in the order they belong in, plus `MovingMapTick`,
`builtin_symbol::kOwnship` (an aircraft in plan view, nose at +y) and
`MovingMapCamera::ClearApron`. It is **`fv.movingmap`, a STATIC built-in type**, registered by
`RegisterBuiltinOverlayTypes` and deliberately NOT restored at startup. Bound whole as
**`pyfvw.nav`** (`pyfvw_nav.cpp` — fix/feed/queue/scripted source/heading/camera/slew/overlay,
and `nav.PositionSource` is subclassable so a Python receiver is a first-class feed), and
PythonView flies it: "m" toggles, upper-case M / T / S are the three modes, `[movingmap]` in the
ini carries the startup state.
Six things worth not re-deriving:
1. **The camera lives HERE because the overlay is the only object that is both drawn per frame
   and fed fixes.** MM2's split (apron from the DRAWN position, tested against the new one) is
   therefore structural — `OnDraw` recomputes, `Tick` updates — rather than an ordering every
   shell has to get right.
2. **`Tick` answers and applies NOTHING.** MM2/MM3's rule survives the language boundary: there
   is a `self.center = tick.slew.center` in PythonView and nowhere in fvkit, and `pyfvw.nav`
   deliberately has no auto-applying variant.
3. **Every queued fix reaches the heading resolver; only the last reaches the camera.** The
   resolver's state IS a history, so skipping fixes would make a derived heading depend on the
   shell's tick rate; the camera wants the present.
4. **Setting the modes FORCES a recentre** (FalconView's `force_update`), or turning
   auto-centring on does nothing until the ship leaves an apron computed while it was off.
5. **The heading is NEGATED into `PointSymbolStyle::rotation_deg`** — that field is
   `CCGMSymbol::DrawSymbol`'s angle, which turns a symbol COUNTER-clockwise on screen, while a
   heading is a compass bearing. S-52 already negates at its own seam for the same reason. The
   first draft passed every assertion and drew the ship flying backwards, because "wider than
   tall at 090" is equally true of the mirror image; the ownship's nose accordingly reaches
   1.00 of its box against the tail's 0.85, so a test can say the ink reaches further ALONG the
   heading than against it.
6. **`screen_angle_deg()` IS the camera's `point_angle` and it ADDS the map rotation.** MM2
   ported `heading + map_rotation + convergence` verbatim and places the ship on the SCREEN with
   it, so the two must be one sum. Track-up fixes the sign: the camera answers rotation 270 for
   a course of 090 (`rotation -= point_angle` means "turn the chart until this is zero", and
   zero is up). **The first version subtracted** and drew a ship steaming east as pointing
   south — found by Chris flying the demo, not by a test.
7. **A shell must declare whether it can rotate the map at all**
   (`SetRotationSupported`, default true = the contract). **PythonView says YES as of PR3**;
   told false, the overlay pins the rotation at 0, so the ship is drawn at its true screen
   bearing and `auto_rotate` degrades to the track-up ANCHOR alone. Left unsaid, the overlay
   counter-rotates the ownship to match a rotation that never happened.
   **PR3 also retired the duplicate state**: `Tick` now ADOPTS `proj.Rotation()` before it does
   anything with it, so `map_rotation_deg_` is a reading rather than a belief. For a shell that
   applies every tick that is the identity; for one that lags, or turns the chart by a route of
   its own, it is a per-frame correction rather than a compass error nobody notices. It also
   brings the term into [0, 360), which is what MM2's once-only 360 subtraction in `point_angle`
   assumes and a slew's arithmetic does not guarantee. `SetMapRotation` is therefore only the
   initial condition on a rotating shell.
   A REPORTED heading being a true bearing while a DERIVED one is a screen angle stays
   preserved rather than reconciled — that asymmetry is `get_current_heading`'s own.
**`RecomputeApron(0,0,0,0)` is NOT an empty apron** — `ComputeApron` carries the original's `+ 1`
exclusive edge, so a zero window yields a 1x1 box at the origin; `ClearApron` is what an overlay
with no fix yet calls.

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
column away; alpha 0 is how a document asks for the bare icon, and selection is G4's
`RenderState` around the outermost stamp. `EmbeddedSymbolLibrary` (private to
`point_overlay.cpp`) is the third form of
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

## B. Resolved items removed from §2 (open work)

### B1. Active-track rows PR2 and PR3, struck through when they were built

| ~~**PR2**~~ | ~~Projection rotation, second slice: the vector path~~ | **BUILT 2026-08-15 — see the archive row.** The vector path turns, and the striking thing is how little of it was code: geometry, along-path labels and the pick index all turn because the projection does, and the ONE angle that needed teaching was `PointSymbolStyle::rotation_deg` (`SymbolAngleOnChart`, one gated subtraction, applied at exactly two sites). The three rules the row below asked for all held. The ownship also came out with no special case — but not the way the row predicted: `DrawSymbolAtPixel` does NOT apply the chart rotation, because a pixel anchor's angle is already a screen angle, and MM4's `screen_angle_deg()` already sums in the map rotation. Original planning text: **PR1 IS BUILT** (archive row): `MapProjection::SetRotation(deg)` turns the chart clockwise about the surface centre, both transforms and `VmapBounds` carry it, rotation 0 is the byte-exact identity and cardinal angles are exact. **The retained scene needs no cache key** — R3a's ink is geographic, so a scene re-projects through a turned projection unchanged, and `CanServe`'s containment rebuilds by itself when the turned box grows past the straight one (decision recorded in the PR1 row, §6). So what is LEFT here: the renderer, the scene, the pick index and `GeoDraw` carry the rotation. Three rules to settle with tests: a SYMBOL drawn at a bearing adds the rotation (it is `point_angle` again, and MM4's `screen_angle_deg` is already that sum — the ownship should end up with no special case at all); a POINT LABEL stays UPRIGHT on the screen while an along-path label follows its rotated path (FalconView's own behaviour, and `DrawRotatedTextString` already exists for it, T1); and the pick index keeps agreeing with the ink, which it does for free if rotation lives in the projection. **MM4's `SetRotationSupported` goes true for any shell that has this**, and track-up becomes an orientation. |
| ~~**PR3**~~ | ~~Projection rotation, third slice: the raster path + the shell~~ | **BUILT 2026-08-15 — see the archive row. THE MAP TURNS, END TO END.** The gate in `CompositeRow` is the whole design: rotation 0 runs the blit it always ran, byte for byte, and a turned chart goes down `CompositeRowTurned`, which is the same affine-through-corner-samples trade with two forced differences — the target region is the BOX of a quad (four corners, not two) and the blit is MASKED rather than clamped, so a turned frame keeps its own straight edges instead of smearing them into the corners. PythonView applies `tick.slew.rotation_deg` to `self.rotation`, sets `rotation_supported = True`, straightens the chart when the moving map goes off, and turns a drag delta back into the chart's own axes (`_chart_pixels`) so a pan still follows the cursor. The wiring hazard the row below flagged was fixed at the source rather than by discipline: `MovingMapOverlay::Tick` ADOPTS `proj.Rotation()`, so the projection is the one place the applied rotation is true. Measured price at 1000x700 over CADRG: 59 ms straight, 100 ms at 45 degrees, and a quarter turn is FREE (57 ms) — it only swaps the extents. Original planning text: |

### B2. §2b product gaps that had been closed (MarinerSettings/M1, projection rotation/PR1-PR3,
halo text/T2, the first symbol-library consumer/G3, OSM icons/O6, the along-path name anchor,
and OSM's missing ocean)

- ~~**MarinerSettings on `StyleContext`**~~ **Done (M1), and not on `StyleContext`** — the settings
  belong to the ENGINE, where the epoch that invalidates a retained scene already lives, so
  `fv::MarinerSettings` (`fvkit/vector/mariner.h`) sits on `LookupTableStyleEngine` and both products
  read it. GeoSym maps it onto `CECDISValues`' `ssdc`/`msdc`/`mssc`/`idsm`/`isdm`; `S52MarinerSettings`
  is now an alias. Each product keeps its OWN defaults (DNC 10 m and pattern on, S-52 30 m and off) —
  they are what the goldens were pinned over. Settable from `[mariner]` in the ini; **still no UI**
  (§2c). Note the accessor split: `mariner()` is const and free, `mutable_mariner()` bumps on call.
- ~~**THE PROJECTION CAN ROTATE AND THE SHELL CANNOT**~~ **CLOSED BY PR1-PR3, all 2026-08-15.
  THE MAP TURNS.** Chris chose the FAITHFUL route — rotate the
  PROJECTION, not the rendered image — and `MapProjection::SetRotation` carries a clockwise turn
  through `GeoToSurface`/`SurfaceToGeo`/`VmapBounds`, with rotation 0 byte-identical to the
  projection that had no rotation at all. It is bound (`set_rotation` / `.rotation`) and PR2 made
  every vector consumer honour it; **PR3 closed the raster path and the shell**, so PythonView
  turns the chart under track-up and the ship points where it is going on a chart that is really
  turned. What is left is smaller and is listed as its own gap below: there is still no
  user-facing "turn the chart" gesture — the moving map is the only thing that writes the
  rotation — and the turned blit is nearest-neighbour like the straight one, so a chart turned to
  an odd angle is as aliased as one that is not. Three numbers worth carrying forward: a turned viewport's query box grows to
  `(w cos + h sin)` by `(w sin + h cos)` — `(w+h)/sqrt(2)` on both axes at 45 degrees — the retained
  scene needs no rotation in its key because its ink is geographic, and the only angle in the whole
  drawing stack that had to be taught the rotation is a point symbol's north-up
  `PointSymbolStyle::rotation_deg`.
  Original finding:
  (found by Chris flying MM4's demo: "the map does not rotate — it slides around but is always
  north up"). `MapProjection` carries a centre, a scale and a surface size and no rotation;
  `MapEngine` has none either; `ICanvas` can rotate a STRING (T1) and nothing else. So MM2's
  `CameraTarget::rotation_deg` and MM3's slewed rotation are computed faithfully, returned
  honestly, and dropped by every consumer — PythonView applies the centre and sets
  `rotation_supported = False`, which is what keeps the ownship pointing where it is actually
  going. The alternative — rotating the RENDERED IMAGE in the shell — was
  offered and **declined**: it is one session's work and gives a rotating chart with sideways
  place names, and it would have to be unpicked again to get upright labels. PR1-PR3 do it
  properly, and all three have.
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
- ~~**OSM has no icons**~~ **Closed by O6, 2026-08-15.** `OsmStyleEngine` loads the style's
  `sprite` through `PngSymbolLibrary`'s sheet form and emits a `PointSymbolStyle` for
  `icon-image` and an `AreaPatternStyle` for `fill-pattern`. The supported style syntax is now
  DOCUMENTED for a style author rather than only in the header: `port/Osm/styles/style-readme.md`.
  Four things worth not re-deriving:
  1. **`icon-image` and `fill-pattern` are TOKEN TEMPLATES**, the same `{tag}` form as
     `text-field` — `"icon-image": "{class}"` is how OpenMapTiles keys a POI icon and is far
     commoner than a literal.
  2. **The icon and the text are independent halves of a symbol layer.** The icon is emitted
     BEFORE the `draw_labels` early-out, so turning labels off leaves the icons drawing; a
     layer with an icon and no text used to contribute nothing at all.
  3. **`Symbol()` is overridden to short-circuit `sprite:` ids to nullptr.** Letting `LoadSymbol`
     answer false instead would be caught by the base class, which counts a false `LoadSymbol`
     as an UNRESOLVED SYMBOL — every icon that drew perfectly would then appear in the one
     diagnostic whose whole job is to list the symbology that did not. `ResolveSymbol` asks
     `Symbol()` then `Pixmap()`, and a sprite is only ever the second.
  4. **A pattern's spacing and its stamp scale must carry the SAME factors** (`dpi_scale` and the
     pass's symbol scale) or the tiling comes apart: spacing is the tile's DRAWN size, so scaling
     one and not the other opens a gutter of exactly the missing factor at every DPI but 96.
     Caught in review, pinned by `APatternsSpacingAndItsStampScaleTogether`.
  Sprite resolution is **local filesystem only** — an explicit `SetSpriteBase` (whose failure to
  open IS a load failure, since the caller asserted it) then the style's own `sprite` resolved
  beside the style file; an `http(s)` URL resolves to nothing and is NOT a load failure, because
  that would reject every style published on the web. Still rejected, each for its own reason
  now: `line-pattern` (GL stretches a tile along the line, `LinePatternStyle` repeats a symbol at
  a fixed step — different pictures, and guessing is what the declared subset prevents) and
  `background-pattern` (a background here is a canvas clear; there is no geometry to repeat over).
- ~~**An along-path name rides the top edge of its line.**~~ **Fixed 2026-08-15.**
  `PlaceTextAlongPath` returns BASELINE origins and put that baseline on the geometry, so every
  glyph body stood on one side of it and a street's name ran along the street's top edge rather
  than down its middle — visible in `osm_atlanta_road_names.png` for as long as that screenshot
  has existed. `LabelStyle::along_anchor` (`LabelAlongAnchor::{kBaseline,kCenter}`) now says
  which, `AlongPathAnchorShift` in `text_draw.h` converts it to a perpendicular shift the two
  callers (`VectorRenderer`, `GeoDraw`) fold into `offset_px`, and `OsmStyleEngine` asks for
  centred — which is what GL means by a line placement, and it measures `text-offset` from there.
  **kBaseline is the default and returns exactly 0.0**, which is what leaves every pinned S-52 and
  GeoSym golden byte-identical. The shift is half the CAP HEIGHT, not half the box the canvas
  measures: `GetTextExtent` returns ascent-descent, so centring on it hangs a name low by half a
  descender. Cap height is `kCapHeightEm = 0.72`, MEASURED over the fonts the port draws with
  (Arial 0.7163, Helvetica 0.7173, Verdana 0.7271, Geneva 0.7578 — OS/2 sCapHeight/unitsPerEm);
  the whole spread is 0.042 em, half a pixel at a 12 px label, which is why ICanvas is NOT being
  grown a cap-height accessor that every backend including pyfvw's Python subclasses would have
  to implement.
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

### B3. §2c UI follow-ups that had been closed (A6's adoption of `fv::app`, the CaptureMouse
finding, and O5c's route-profile UI)

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

### B4. §2d defects that had been fixed (the ENC test-data cut-back, and the clipped-away leg)

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
