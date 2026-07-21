# ICD → pyfvw name mapping

Required by `port/fvkit-contracts.md` (D5): one row per bound surface,
mapping the legacy COM ICD (`PFPS400-MapServer-ICD.doc`) / COM interface
names to pyfvw, with semantic deltas. Grows with each binding slice.

Slice 1 (2026-07-15): geo primitives, DTED elevation, GeoTIFF rasters.

| ICD / COM surface | pyfvw | Semantic deltas |
|---|---|---|
| `IMap::GetElevation` (feet, MISSING/PARTIAL sentinels) | `formats.DtedElevationSource.get_elevation(lat, lon)` | **Meters**, float. Void posts (-32767) → `NaN` return; outside coverage → `FvError(OUT_OF_COVERAGE)` instead of MISSING (-99999). |
| `IDted` (CDted service: caches, level fallback, notify events) | `formats.DtedElevationSource` | Per-tree source; opens cells lazily, prefers highest DTED level per cell. No notify events (pure calls). |
| MDM `IEnumMapElementsBase::FindFirstElement/FindNextElement` | `formats.DtedFrameEnumerator.frames(dir)` / `formats.GeoTiffFrameEnumerator.frames(dir)` | Single call returns the whole sorted list instead of a cursor. DTED bounds are path-derived (never opens files); GeoTIFF reads each header (as Windows GenerateCoverage did). |
| `IGeoTiffFrameFile::raw_GetFrameProperties` | `formats.FrameInfo` fields (`bounds`, `series_key`) | `series` BSTR → UTF-8 `series_key`; unsupported frames are skipped by the enumerator (Windows fell back to ImageLib COM/MrSID). |
| ImageLib `Image::get_rgb_subimage` (RGB, planar variants) | `formats.GeoTiffRasterSource.read_block(x, y, w, h)` | Interleaved **RGBA8** `PixelBuffer` (alpha 255); zero-copy numpy `(h, w, 4)` via buffer protocol. Rect must lie inside the image (`FvError(INVALID_ARG)` otherwise; callers clip). |
| `CGeoTiff::inv_transform` / `fwd_transform` (int pixels) | `pixel_to_geo(px, py)` / `geo_to_pixel(p)` | Double pixel coords at the API; the legacy transform is whole-pixel, so results round to pixel centers (round-trip within ±1 px). |
| `ISettableMapProj` geo types (`degrees`, lat/lon pairs) | `geo.GeoPoint`, `geo.GeoRect` | lat before lon everywhere; lon canonical (-180, +180], ±180 → +180; `ll.lon > ur.lon` = antimeridian crossing. |
| HRESULT / error codes | `pyfvw.FvError` (`.code`, `.message`) | Raised on any non-ok Status; legacy codes preserved in the message text, `.code` is the FvKit enum (`pyfvw.OUT_OF_COVERAGE`, ...). |

Not yet bound: format registry (`RegisterFormat`), CADRG/TIROS adapters,
catalog/engine/canvas/overlays (later slices, per the build-out sequence).
