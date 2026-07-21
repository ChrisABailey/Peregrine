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
| 14m | Next (queue Q4): VPF V2 — DNC coverage → catalog rows | — | — | Then Q5+ per the port queue. Pending: VPFRecordset reopen undercounts rows (m_row_length accumulates — pinned as a quirk test in vpf_test.cpp, fix deliberately in V2); subsampled ReadBlock (packs now cover the zoom-out path, so lower urgency); polar CADRG transforms; TIROS tile-seam; C++14 pins fv_jpeg/fv_jpeg12/fv_imagelib_gif; CIB is_cib gap; FeatureStore (other half of L5) with overlay persistence. |

## Port queue (2026-07-19, per Chris: public data only; ordered by data
## availability first, implementation complexity second)

**Tier 1 — data in hand (or none needed), cheapest first:**
| # | Item | Data | Complexity |
|---|---|---|---|
| Q1 | 14j: pan-viewer over MapEngine + overlays (zoom) | none needed | trivial |
| Q2 | L5 store: GeoPackage TilePack writer (+ FeatureStore stub) | self-generated | moderate |
| ~~Q3~~ | ~~VPF V1: headless VPF/DNC reader~~ **done 2026-07-20 (row 14l)** | dnc17 in TestData ✓ | moderate |
| Q4 | VPF V2: DNC coverage → catalog rows | same | low |
| Q5 | GeoSym V3/V4: rule engine + CGM display lists | assets from Chris's DNC machine (imminent) | moderate |
| Q6 | VPF V5: VectorRenderer seam + DNC drawing | above | high (the big one) |

**Tier 2 — freely downloadable/generatable public data:**
| # | Item | Data | Complexity |
|---|---|---|---|
| Q7 | OSM O1: MBTiles + MVT reader | Planetiler over Geofabrik extract (generate once) | low-moderate; dependency-free, good filler session anytime |
| Q8 | ENC E1: ISO 8211 / S-57 reader | NOAA ENC cells (free public domain — download, no generation) | moderate; dependency-free |
| Q9 | OSM O2+O3: style subset + render via V5 seam | same as Q7 | low (after V5) |
| Q10 | ENC E2: S-52 PresLib (lookups + symbol display lists) | IHO PresLib (public; also ships with OpenCPN data) | moderate |
| Q11 | ENC E3+E4: S52StyleEngine + CS procedures + SENC, render | above | high (after V5 seam; shares V4 display lists + V5 along-path placer) |
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
GeoSym asset directory from the Windows machine (blocker below).
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
- [ ] **GeoSym asset directory** (confirmed install data, not source) → TestData/geosym
      (git-ignored); needed for vpf-geosym-plan phases V3+. **Located 2026-07-16**: on the
      Windows machine, `HKLM\SOFTWARE\[WOW6432Node\]MissionPlanning` (fallback `...\JMPS`)
      value `DataDir`; assets are `<DataDir>\GeoSymbol\SymAssign\` (rule tables:
      fullsym.txt/simpsym.txt/...) + `<DataDir>\GeoSymbol\Graphics\` (CGM symbol files).
      Typical JMPS layout: `C:\data\Local\JMPS\Data\GeoSymbol\`. Copy the whole GeoSymbol
      folder. Port note: registry → ctor/env data dir, subpaths stay relative.
      Also still wanted: reference screenshots of dnc17 harbor views (V5/V7 goldens).
      ~~V0 fork question~~ resolved from project files: VpfMapServer compiles Vpf/;
      VPFDataLib is a separate project (VPFDataRenderServer, VvodAnalysisServer).

- 2026-07-16: **Full Windows tree copied into the repo top level** (Applications/ incl. the
  FalconView app, Components/, Plugins/, UIControls/, unit_tests/, props/, Custom Actions/).
  Untracked in git so far (Chris to decide on committing). Immediate yields: y2k pivot
  verified+fixed; VPF render loop located in-tree (VPFMapPlugIn_imp.cpp — see
  vpf-geosym-plan.md); V0 fork question answered. App trees fall under the hard rules
  (never modify Windows build files).
