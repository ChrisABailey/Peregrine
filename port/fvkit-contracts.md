# FvKit interface contracts

**Status: approved by Chris 2026-07-14.** Changes to D1–D6 need owner sign-off.

This doc pins the cross-cutting decisions that every FvKit layer (L0–L6, see the plan) and
every pyfvw binding must follow, so interfaces built in separate sessions stay coherent.
It sharpens the plan's prose into signature-level rules; where the two disagree, this doc
wins and the plan gets updated. The 2005 COM ICD (`PFPS400-MapServer-ICD.doc`) is the
*semantic* reference (decomposition, method meanings) — never a type/style reference.

Scope note: FvKit is a **new API**. The bit-faithful rule still governs the ported leaf
modules (their outputs must match Windows), but FvKit adapters *may* convert units and
representations on top of them, provided the conversion is documented here or in the
adapter header.

---

## D1 — Ownership & lifetime

- Every FvKit **interface** (`IRasterSource`, `IElevationSource`, `IFrameEnumerator`,
  `ICanvas`, `IOverlay*`, `IMapView`) is held and passed as `std::shared_ptr<I…>`.
  Rationale: pybind11 trampolines (Python-implemented overlays) require shared ownership;
  choosing per-interface later guarantees incoherence. pybind holder type =
  `std::shared_ptr<>` for these, always.
- Interfaces: virtual dtor, no copy/assign, pure virtual methods only, no data members.
- **Concrete value types** (`GeoPoint`, `GeoRect`, `MapScale`, `Status`, `FrameInfo`,
  `PixelBuffer`, `CoverageRow`) are plain movable structs/classes — never behind
  shared_ptr, bound by value in Python (`PixelBuffer` via buffer protocol, see D4).
- **Engines/managers** (`Catalog`, `MapEngine`, `OverlayManager`, `MapProjection`,
  `CpuCanvas`) are concrete classes, non-copyable, movable where cheap; created by value
  or `std::shared_ptr` at the app's choice; pybind holder `std::shared_ptr<>`.
- The legacy ported classes (`fv::DtedCell`, `RPFRenderer`, `fv::GeoTiffFrame`, `CJpeg`)
  are **implementation details owned by their adapter**; they never appear in FvKit
  signatures and are never bound to Python.
- Format registry: `RegisterFormat` (multi-slot, generalizing `fv_interfaces.h`'s
  single-slot pattern) stores `std::function` factories returning
  `std::shared_ptr<IRasterSource>` / `std::shared_ptr<IFrameEnumerator>`; registration
  does not transfer ownership of anything.

## D2 — Geographic conventions (GeoRect / antimeridian)

- All FvKit coordinates are **WGS-84 geodetic decimal degrees**. Datum conversion happens
  inside adapters (via `fv::IDatumConvert`), never at the FvKit API surface.
- Argument/field order is **lat, lon** — always, in every signature, struct, and Python
  tuple. (Matches every ported module and the ICD; never lon,lat even where GIS habit
  suggests it.)
- `GeoPoint{double lat, lon}`. Canonical ranges: lat ∈ [-90, +90], lon ∈ (-180, +180].
  `Normalize()` maps arbitrary lon into range; ±180 normalizes to **+180**.
- `GeoRect{GeoPoint ll, ur}` stores edges as-is. **`ll.lon > ur.lon` means the rect
  crosses the antimeridian** (FalconView's own convention). No rect spans ≥ 360° except
  the explicit `GeoRect::World()`. Poles do not wrap: `ll.lat <= ur.lat` is an invariant.
- `Intersects` / `Contains` / `Intersection` must handle the crossing case internally.
  Anything feeding an R-tree (catalog) **splits a crossing rect into two boxes** at ±180;
  query results are de-duplicated by row id. This is the plan's named silent-failure risk:
  L0 ships wrap-case tests before any consumer exists.

## D3 — Error model

- `fv::Status{ int code; std::string message; }` with `ok() == (code == 0)`. Defined in
  `fvkit/geo.h`. Success is code 0, always.
- FvKit-native error codes are a small negative enum (`kInvalidArg = -1, kNotFound = -2,
  kIoError = -3, kUnsupported = -4, kOutOfCoverage = -5, kInterrupted = -6, kInternal =
  -100`). When wrapping a legacy call, the legacy code (HRESULT, GeoidError, decoder int)
  is preserved **in the message text**, not in `code`.
- Signature shape: fallible operations **return `Status`**; data comes back through
  pointer out-params (matches the ported modules' existing style, trivially bindable).
  No exceptions thrown across any FvKit interface, in either direction (a Python overlay
  raising inside a trampoline is caught at the trampoline and converted to a failed
  Status; pybind does this for us — we log `message`).
- **Python translation is mandatory at the binding layer**: a non-ok `Status` raises
  `pyfvw.FvError` (carries `.code` and `.message`); out-params become return values
  (single value, or tuple in declaration order). Python callers never see Status objects.

## D4 — Pixel & elevation data contracts

- `PixelBuffer`: **owning, interleaved RGBA8**, row-major, **top-down** (row 0 = north/top),
  `stride_bytes >= width*4`, alpha 255 = opaque. The only pixel currency above the
  adapters. Defined in `fvkit/raster.h`.
- `IRasterSource::ReadBlock(px_rect, PixelBuffer&)` — block reads only, never per-pixel.
  Pixel coords: x = column (rightward), y = row (downward), origin at the image's
  top-left. Palette/planar/BGR legacy output is converted to interleaved RGBA8 inside
  the adapter.
- Python: `PixelBuffer` exposes the buffer protocol as numpy `uint8` shape `(h, w, 4)`
  (zero-copy view; buffer keeps itself alive via the holder). Convenience
  `read_block(...) -> numpy.ndarray` on the Python side.
- **GIL released** (`py::gil_scoped_release`) around every I/O- or decode-bound call:
  `Open`, `ReadBlock`, `GetElevation`, `Catalog::Scan`, `RenderBaseMap`.
- Elevations at the FvKit surface (`IElevationSource::GetElevation(GeoPoint, float*)`,
  `MapEngine::GetElevation`) are **meters, float**. Void/partial posts (DTED -32767)
  come back as **NaN with Status ok**; outside coverage = `kOutOfCoverage`. The legacy
  feet/meters enum and raw sentinel passthrough stay below the adapter (bit-faithful
  layer). GeoidCalculator's feet-based delta, if ever exposed, keeps feet and says so in
  its name (`geoid_delta_feet`).

## D5 — Naming, layout, language

- Namespace `fv` for everything (FvKit adds no nested namespace). Headers under
  `port/include/fvkit/` exactly as laid out in the plan; implementations under
  `port/fvkit/<layer>/`.
- C++17. Style follows existing `port/` code (Google-ish, `PascalCase` methods,
  `snake_case` members with `m_` only in legacy-derived code — new FvKit code uses
  trailing-underscore privates).
- Python: module `pyfvw`, submodules mirroring layers (`pyfvw.geo`, `pyfvw.formats`,
  `pyfvw.catalog`, `pyfvw.canvas`, `pyfvw.engine`, `pyfvw.overlay`). **snake_case**
  methods; parameterless getters become **properties**; enums are `enum.IntEnum`-style
  pybind enums keeping COM ABI values (from `fv_map_enums.h` — never renumbered).
- `port/bindings/pyfvw/ICD-MAPPING.md`: one table row per bound method — ICD/COM name →
  pyfvw name → semantic deltas (unit conversions, NaN sentinel, etc.). Grows with the
  bindings; starts as a stub. This is the required ICD→FvKit name-mapping doc.
- pybind11 via CMake FetchContent, pinned version (v2.12+); pytest suite in
  `port/bindings/pyfvw/test/`, run from ctest.

## D6 — Adapter map (green modules → FvKit interfaces)

| Ported class (stays hidden) | FvKit surface | Conversion notes |
|---|---|---|
| `fv::DtedCell` (`Open(file, sw_lat, sw_lon)`, nearest-post, feet/meters enum) | `IElevationSource` via `fvkit/formats/dted.h`; cell-level `IFrameEnumerator` walks `dted/<lon>/<lat>.dt[012]` | SW corner comes from the tile path → the **enumerator** parses it, not the source. Meters only; -32767 → NaN (D4). Multi-cell fallback/caching lives in the adapter, not the engine. |
| `RPFRenderer::get_rgb_image(file, is_cib, startx, starty, w, h, BYTE*)` (interleaved RGB, 1536² frames) | `IRasterSource` via `fvkit/formats/cadrg.h` | RGB → RGBA8 (alpha 255). **LRU decoded-subframe cache is designed into this adapter now** (plan risk #1); `is_cib` inferred from RPF product code. Frame bounds/zone from the CADRG TOC/frame name via the enumerator. |
| `fv::GeoTiffFrame` (`GetFrameProperties` → WGS-84 bounds/scale/series) + `CGeoTiffFrameFile` pixels | `IRasterSource` + `IFrameEnumerator` via `fvkit/formats/geotiff.h` | `supported=false` (NOT_SUPPORTED fell back to ImageLib COM on Windows) → enumerator skips the file with a logged note; wide `series` → UTF-8 `series_key`. |
| `CJpeg` + `.wld` sidecar (TIROS) | `IRasterSource` via `fvkit/formats/tiros.h` | World-file affine → `ImageInfo.geotransform`; decode is whole-image, adapter crops to block. |
| `fv::MapScaleUtil`, `fv_map_enums.h` | consumed by `fvkit/scale.h` (`MapScale`, `MapType`) | HRESULT-shaped returns wrapped to `Status`; quirks (statute-mile FEET factor, etc.) pass through untouched — FvKit does not correct them. |
| `fv::GeoidCalculator`, geo_tool geodesics, `fv::IDatumConvert` | utility bindings under `pyfvw.geo` (direct, no interface) | Feet delta keeps feet + explicit name; Sodano geodesic quirk documented in docstring. |

## Binding sequencing (what "start pyfvw" means)

Bindings wrap **FvKit surfaces, not raw legacy classes** (raw signatures would freeze a
third API style we'd break at L6). Minimum path to "call it from Python on a Mac":

1. **L0** `geo.h`/`scale.h`/`raster.h` + wrap-case and Status tests (one session).
2. **L1** one adapter at a time — DTED first (smallest), then GeoTIFF, CADRG, TIROS —
   each with enumerate/checksum tests against TestData (one session each).
3. **pyfvw slice 1**: bind L0 types + the L1 adapters + `pyfvw.geo` utilities; pytest
   mirrors the gtest pinned values (one session).

Catalog/canvas/proj/engine/overlays (L2–L5) follow the plan afterwards; each later layer
extends the bindings in the same session it lands.

## Open questions (decide when the layer lands, not before)

- `MapProjection` virtual-surface variants: stub vs omit until Skia canvas (L3a).
- Catalog schema versioning/migration story (L2).
- Text rendering scope in CpuCanvas (stb_truetype subset) (L2.5).
