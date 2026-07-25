# ICD → pyfvw name mapping

Required by `port/fvkit-contracts.md` (D5): one row per bound surface,
mapping the legacy COM ICD (`PFPS400-MapServer-ICD.doc`) / COM interface
names to pyfvw, with semantic deltas. Grows with each binding slice.

Conventions used throughout (stated once, not repeated per row):
- HRESULT / legacy error codes → `pyfvw.FvError` (`.code`, `.message`);
  raised on any non-ok Status, legacy code preserved in the message text,
  `.code` is the FvKit enum (`pyfvw.OUT_OF_COVERAGE`, ...).
- geo types are `geo.GeoPoint` / `geo.GeoRect`: **lat before lon** everywhere;
  lon canonical (-180, +180], ±180 → +180; `ll.lon > ur.lon` = antimeridian
  crossing.
- pixels are interleaved **RGBA8** `PixelBuffer` (zero-copy numpy `(h, w, 4)`
  via buffer protocol); the decode releases the GIL.

## Slice 1 (2026-07-15): geo primitives, DTED elevation, GeoTIFF rasters

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| `IMap::GetElevation` (feet, MISSING/PARTIAL sentinels) | `formats.DtedElevationSource.get_elevation(lat, lon)` | **Meters**, float. Void posts (-32767) → `NaN` return; outside coverage → `FvError(OUT_OF_COVERAGE)` instead of MISSING (-99999). |
| `IDted` (CDted service: caches, level fallback, notify events) | `formats.DtedElevationSource` | Per-tree source; opens cells lazily, prefers highest DTED level per cell. No notify events (pure calls). |
| MDM `IEnumMapElementsBase::FindFirstElement/FindNextElement` | `formats.DtedFrameEnumerator.frames(dir)` / `formats.GeoTiffFrameEnumerator.frames(dir)` | Single call returns the whole sorted list instead of a cursor. DTED bounds are path-derived (never opens files); GeoTIFF reads each header (as Windows GenerateCoverage did). |
| `IGeoTiffFrameFile::raw_GetFrameProperties` | `formats.FrameInfo` fields (`bounds`, `series_key`) | `series` BSTR → UTF-8 `series_key`; unsupported frames are skipped by the enumerator (Windows fell back to ImageLib COM/MrSID). |
| ImageLib `Image::get_rgb_subimage` (RGB, planar variants) | `formats.GeoTiffRasterSource.read_block(x, y, w, h)` | RGBA8 `PixelBuffer` (alpha 255). Rect must lie inside the image (`FvError(INVALID_ARG)` otherwise; callers clip). |
| `CGeoTiff::inv_transform` / `fwd_transform` (int pixels) | `pixel_to_geo(px, py)` / `geo_to_pixel(p)` | Double pixel coords at the API; the legacy transform is whole-pixel, so results round to pixel centers (round-trip within ±1 px). |
| `ISettableMapProj` geo types (`degrees`, lat/lon pairs) | `geo.GeoPoint`, `geo.GeoRect` | (see conventions above) |
| HRESULT / error codes | `pyfvw.FvError` | (see conventions above) |

## Slice 2 (2026-07-15/16): CADRG + TIROS raster adapters

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| `ICadrgMapServer` / RPFRenderer (CadrgMapServer COM, Windows-only) | `formats.CadrgRasterSource` | One RPF frame (1536²). Whole frame VQ-decodes once on the first `read_block`, then crops from cache. Equal-arc frames support `pixel_to_geo`/`geo_to_pixel`; **polar frames raise `FvError`** (unsupported; none in CONUS/TestData). |
| CadrgMapServer frame catalog walk (SQL `tbl_map_series_cadrg`) | `formats.CadrgFrameEnumerator.frames(dir)` | Recursive filename/scale enumeration; **no SQL** — scale comes from a built-in MIL-STD-2411A series→scale table (ground-truthed against each frame's embedded RPF coverage corners). No pixels decoded. |
| CadrgMapServer decoded-frame cache | `formats.CadrgFrameCache(capacity=24)` | Explicit LRU of opened `CadrgRasterSource` keyed by path (`.get(path)`, `.size`, `.capacity`); bounds resident decoded frames (~9.4 MB each) during a pan. |
| `ITirosMapServer` (TirosMapServer COM, Windows-only) | `formats.TirosRasterSource` | One TIROS JPEG tile. Filename-derived equal-arc bounds; reads the real JPEG dims in `open` (don't assume 1350²). Hairline inter-tile seam preserved from the original georef math. |
| TirosMapServer frame enumeration | `formats.TirosFrameEnumerator.frames(dir)` | Recursive `*.wld` listing; bounds from the filename grid (georef in the JPEG marker is only a version tag). |

## Slice 3 (2026-07-15/16): L2 coverage catalog

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| MDM (Map Data Manager) `tblDataSources` / `tblMapSeries` / `tblCoverage` | `catalog.Catalog` (SQLite; `add_data_source` / `scan` / `series` / `select_by_geo_rect` / `best_series_for_scale` / `remove_data_source`) | Schema mirrors the three MDM tables. `scan` drives the FvKit format registry (not per-server GenerateCoverage). `db_path` defaults to `":memory:"`. |
| MDM series/coverage rows | `catalog.SeriesRow`, `catalog.CoverageRow` | Read-only value objects; `SeriesRow.scale_denom` is the normalized 1:N denominator (via ported MapScaleUtil). |
| MDM viewport query | `Catalog.select_by_geo_rect(rect, series_id=0)` | Antimeridian-aware R-tree: crossing coverage stored as two boxes (`id = cov*2 + piece`), queries split + de-dupe per contract D2. `series_id=0` = all series. |
| Per-server `RegisterFormat` (MapServer registration) | `catalog.register_builtin_formats()` | One call registers the built-in adapters (dted, geotiff, cadrg, tiros, vpf, gpkg) with the scan registry. Idempotent; call before `scan`. |

## Slice 4 (2026-07-16): L2.5 canvas

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| `IGraphicsContext2` (GDI-backed primitive set) | `canvas.ICanvas` (base) / `canvas.CpuCanvas` | Primitives take Pen/Brush/TextStyle **value structs per call** instead of GDI factory objects (documented deviation). CpuCanvas is a non-AA scanline rasterizer over an RGBA8 `PixelBuffer`. `DrawRectangle` corners are **inclusive** (GDI `Rectangle` excludes right/bottom) — revisit at GeoSym V5 parity. Text is stb_truetype (host fonts), ASCII-only for now; metrics differ from GDI. |

## Slice 5 (2026-07-16): L4 overlay SPI

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| `IFvOverlay` + renderer + UI-event interfaces (FalconView overlay SPI) | `overlay.Overlay` (Python-subclassable) | The three COM interfaces are folded into **one** class (a single pybind trampoline is what subclassing needs). Override `on_draw(proj, canvas)`, `on_mouse_*`/`on_double_click`/`on_mouse_wheel`, `on_key_down`. Persistence/Editor/IMapView deferred to first consumer. Exceptions are contained per D3 (draw → `FvError` from `draw_all`; event handlers → treated as unhandled). |
| Overlay mouse/keyboard event structs | `overlay.MouseEvent` | Plain value struct (`x, y, button, shift, ctrl`); key events pass an int keycode. |
| FalconView overlay manager (z-order, event routing) | `overlay.OverlayManager` | `add`/`remove`/`move_to_top`, `draw_all` (bottom-up), `route_*` (top-down until handled → True). `add` uses `keep_alive` so a subclassed overlay's Python half survives as long as the manager (trampoline-lifetime fix). |
| Built-in graticule overlay | `overlay.GridOverlay` | Lat/lon graticule; `set_color((r,g,b[,a]))`. |

## Slice 6 (2026-07-16): L3 projection + map engine

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| `ISettableMapProj` (equal-arc projection, dpp) | `engine.MapProjection` | `deg_per_pixel_lat/lon`, `bounds`, `geo_to_surface(p)` → `(sx, sy)`, `surface_to_geo(sx, sy)` → GeoPoint. dpp via ported MapScaleUtil (matches FalconView); lon deltas unwrapped around center so antimeridian viewports need no special-casing. |
| `VPFMapPlugIn_imp` in-process render walk / MapView compositing loop | `engine.MapEngine` | Configure `set_surface`/`set_center`/`set_scale`, then `render(canvas, series_id=0)` composites the catalog's coverage (catalog select → per-frame source via registry+LRU → corner-exact/linear-between resample → CpuCanvas). `get_elevation` via an attachable elevation source (`set_elevation_source`). No COM dispatch — direct C++ calls. |

## Slice 7 (2026-07-16): L5 store (GeoPackage TilePack)

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| *(no COM predecessor)* — new offline pre-render tool | `store.TilePackWriter` | `create(path, table, bounds, tile_size=256)` then `write_level(engine, zoom, series_id=0)` renders pyramid levels into an OGC GeoPackage (EPSG:4326, PNG blobs). Packs are then consumed as raster through the `gpkg` catalog format (pyramid levels become catalog series). |

## Slice 8 (2026-07-20): VPF/DNC coverage enumeration

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| `IVpfMapServer` coverage generation (VpfMapServer COM, Windows-only) | `formats.VpfFrameEnumerator.frames(db_dir)` | One `FrameInfo` per (library, tile): `series_key` = library, `path` = `db\|library\|tile` locator (for V5's future vector source). Guards: requires a `.dht`; skips untiled / empty-name / out-of-Earth-range rows (the DNC `browse` thumbnail library reads an uninitialized extent). Only the coverage catalog is bound here — **feature/vector drawing is not yet ported** (GeoSym V4/V5). |

## Slice 9 (2026-07-24): vector charting — VPF/DNC through GeoSym

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| `IVpfMapServer` / `IGeoSymServer` drawing (both Windows-only, GDI-bound) | `vector.VpfVectorSource`, `vector.GeoSymStyleEngine`, `vector.VectorRenderer` | The three-part V5 seam. CSymLayeredDisplay's N offscreen DCs collapse into one priority-ordered pass onto an `ICanvas`. Two independent knobs replace the single Windows zoom: `MapProjection.set_physical_scale` (map scale, symbols keep their pixel size) and `VectorRenderer.set_symbol_scale` + `set_device_dpi` (feature zoom, map extent fixed). |

## Slice 10 (2026-07-25): identify — pick index + feature description

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| *(no direct COM predecessor)* — FalconView identified by re-querying `fv::VpfDatabase` | `renderer.pick_index.hit_test(x, y, tol)` -> `[PickHit]`, then `IVectorSource.describe(ref)` -> `FeatureDescription` | **The hit test runs over what was DRAWN**, not over source geometry, so it agrees with the screen: strokes are indexed at their pen half-width, symbols at their inked box (9 px floor), labels at their measured text box, and clipped-away features are absent. Valid only for the last `render()`. `describe()` re-reads the row and decodes it through the coverage's `INT.VDT`/`CHAR.VDT` plus the FCA — a surface the Windows build only ever had inside the unported VPFDataLib fork. VPF integer nulls (-32768 / -2147483648) come back with an empty `display` and their sentinel in `raw`. |

## Not yet bound

- **Scale-dependent feature/label rules** (GeoSym's `vgroup`/`txtgroup` are
  parsed but unused, so labels are dense when zoomed out) — plan phase R2.
- **Along-path SAMI symbol placement** (dash elements of type point-symbol
  currently degrade to gaps) — plan phase E2/E3, shared with S-52.
- **FeatureStore** (the feature half of L5; only the raster TilePack store is
  bound).
- OSM (MVT), ENC (S-57/S-52), and the remaining unported map servers
  (NITF, JP2, WMS, GeoPDF, Lidar, …) — future tracks per the port queue.
