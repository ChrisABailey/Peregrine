<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# WF — web feature layers (OGC API – Features, Esri FeatureServer)

**Status: APPROVED, 2026-11-17.** WF-D1 and the scope decided by Chris; WF-D2 to WF-D11 proposed and accepted the same day. WF1–WF6 done (WF1–WF5 2026-11-17, WF6 2026-11-18).

## Goal

A Feature Layer overlay in Peregrine (mac, Linux) whose features come from a web service instead
of a file: an **OGC API – Features** collection or an **Esri FeatureServer / MapServer** layer.
Everything the file-backed overlay does today works unchanged on it — styling, labels, the
attribute table, selection, Item Info, search, export to GeoPackage, workspaces. Esri layers arrive
styled the way their publisher styled them. Public, anonymous services only in the first version.

**In scope:** OGC API – Features Part 1 (Core) with GeoJSON; Esri GeoServices REST query
(`f=geojson`, `f=json` fallback); Esri `drawingInfo` → `FeatureStyle`; a local cache; a dialog to
add a layer from a URL.

**Out of scope:** FlatGeobuf, PMTiles, SLD (Chris, 2026-11-17); WFS 1.x/2.0 and GML; writing back
to the server (OGC API Part 4, Esri `applyEdits`); authentication (Esri tokens, API keys, OAuth);
CQL2 filtering (Part 3); projected-coordinate responses; iOS (Pippin, Osprey).

## Starting point

**fvkit feature stack** (FO, LM phases; `port/feature-overlay-plan.md`, `port/layer-manager-plan.md`):

- `IFeatureLayer` (`fvkit/feature/layer.h`): synchronous `Query(box)`/`QueryFields`, `Get`,
  `Extent`, `Count`, and `Revision()`, which a reader uses to notice new data. `MemoryFeatureLayer`
  and `GeoPackageFeatureLayer` implement it; `IEditableFeatureLayer` adds edits.
- `ReadGeoJson(text, name, ...)` (`fvkit/feature/geojson.h`) parses a FeatureCollection string
  into a `MemoryFeatureLayer` through nlohmann/json.
- `GeoPackage::Create/Open/OpenLayer` with an R-tree; reads WGS 84 only (`IsWgs84Degrees`, FO-D9).
- `FeatureStyle` (`fvkit/feature/style.h`): single / categorised / graduated / rules,
  `FeatureSymbol` with circle/square markers, line colour/width/dash, area fill/outline, and
  `FeatureLabel`. The MapLibre-subset JSON (`style_json.cpp`) is the stored form.
- `FeatureOverlay::FileOpen(spec)` opens `.geojson` or a `.gpkg` table; specs follow
  `fvkit/app/source_spec.h` (`path|layername=x`), which already groups layers of one source.

**Packages.** `peregrine.ais` (`port/packages/ais-overlay`) is the model: a native package built
only when CMake finds libcurl, staged to `build/packages/`, registered through
`fv::desk::NativeRegistrar` (`port/sdk/include/peregrine/fv_native.h`), which offers
`AddOverlay` and `AddTool` only. Its libcurl use (`sse_stream.cpp`) is the transport to copy.

**Services checked 2026-11-17** (all answered with GeoJSON; used as test targets):

| Kind | Service | Notes |
|---|---|---|
| OGC API | `api.waterdata.usgs.gov/ogcapi/v0` | gauges, time series; Charleston bbox returns sites |
| OGC API | `api.weather.gc.ca` | 104 collections, live hurricane tracks/cones |
| OGC API | `api.pdok.nl/brt/top10nl/ogc/v1` | national topo map; large polygons |
| OGC API | `demo.pygeoapi.io/master`, `demo.ldproxy.net` | reference implementations |
| Esri | `mapservices.weather.noaa.gov/eventdriven/.../WWA/watch_warn_adv/FeatureServer` | live NWS warnings |
| Esri | `services2.arcgis.com/FiaPA4ga0iQKduv3/ArcGIS/rest/services` | 228 federal layers (ArcGIS Online, v12) |
| Esri | `carto.nationalmap.gov/arcgis/rest/services` | MapServer only, ArcGIS 11.3 |
| Esri | `encdirect.noaa.gov/arcgis/rest/services` | ArcGIS 10.91; layer `query` rejected both test queries |

## Decisions

| # | Decision |
|---|---|
| WF-D1 | **libcurl stays out of core** (Chris). The HTTP transport lives in a package, `peregrine.web`, built only when CMake finds libcurl, as `peregrine.ais` is. |
| WF-D2 | **Protocol code in fvkit, transport behind an interface.** New `fvkit/feature/web/`: `IHttpClient` (GET a URL with headers and a timeout, return status, headers and body; cancellable), the OGC API and Esri clients, the cache, and the style import. fvkit links no network library; tests drive the clients with a fake `IHttpClient` over recorded responses. An iOS shell could later supply an `NSURLSession` client. |
| WF-D3 | **`FeatureOverlay` stays the one overlay type.** A spec that is an `http(s)` URL opens through a feature-source registry in fvkit (`RegisterFeatureSource(prefix_or_matcher, opener)`); the package registers its opener with a new additive `NativeRegistrar::AddFeatureSource`. The alternative, a package overlay subclassing `FeatureOverlay`, would give web layers a second type id, a second options page and separate workspace entries. |
| WF-D4 | **Spec form.** A web layer's spec is a URL plus the layer: OGC API `https://host/api|layername=<collection-id>`, Esri `https://host/.../FeatureServer|layername=<layer-id>`. The part before the separator is the source, so the Layers panel groups several layers of one service (LM §1). The service kind (`ogcapi`/`esri`) is detected on open from the landing page and stored with the overlay in a workspace, so a reopen does not detect again. |
| WF-D5 | **Read-only.** `RemoteFeatureLayer` is an `IFeatureLayer`, not editable. Edit tools are disabled with the hint "Web layer: export to a GeoPackage to edit". Export Selection / Save As GeoPackage work as for any layer. |
| WF-D6 | **Cache in a GeoPackage per source.** One `.gpkg` per service URL under `<app cache dir>/web-features/<hash>.gpkg`, one table per layer; the tile index sits beside it in `<hash>.gpkg-tiles.sqlite` (`wf_tiles`: state and fetch time per tile; `wf_tile_features`: the ids each tile returned). A feature no tile lists any more is deleted, so a refetch drops what the service removed; a completed tile replaces the tiles inside it. Queries read the cache synchronously; misses are fetched on a worker. Features are keyed by the service's id (OGC `id`, Esri `objectIdField`) so a feature crossing tiles is stored once. The cache also gives offline use. |
| WF-D7 | **Fetch by tile, not by view.** A view's box is covered by Web-Mercator tiles at the zoom where a tile is about half the view's width (clamped to z4–z14); a tile fetched at a lower zoom covers its children. Each tile is one bbox query, paged to completion. A tile still loading draws what the cache already has; `Revision()` moves when a tile lands, and the overlay asks the host for a redraw (`OverlayHost`, PX1). |
| WF-D8 | **Always WGS 84 longitude/latitude.** OGC: no `crs` parameter (CRS84 is the default). Esri: `inSR=4326&outSR=4326`. A server whose responses are not WGS 84 is refused with a message; projected support waits on the GeoPackage projected-SRS work (§2a). |
| WF-D9 | **Freshness.** Per-layer property `refresh_minutes` (0 = never; default 0, set to 5 by the Esri import when the layer's `editingInfo`/`lastEditDate` or a time field marks it live). Tiles older than that are refetched when next drawn. A Refresh command clears the layer's tiles. |
| WF-D10 | **Limits.** Per response: 64 MB body, 30 s timeout, gzip accepted. Per layer: 200,000 cached features; beyond that, fetching stops and the status line says so. A tile with more than 20 pages is split into its four children. JSON is parsed only by nlohmann/json (§2e: network input on a modern library). |
| WF-D11 | **Esri style import on first add.** When an Esri layer is first added, its `drawingInfo.renderer` becomes the overlay's `FeatureStyle` and its first `labelingInfo` class sets the label field. After that the overlay's own style, stored in the workspace, wins; Layer Style… ▸ Reset to Service Style re-imports. OGC API collections get the default single style. |

## Protocol notes

**OGC API – Features (Part 1 Core).**
- Landing page (`/`, `f=json` or `Accept: application/json`) → `links` rel `data` → `/collections`.
- A collection gives `id`, `title`, `extent.spatial.bbox` (CRS84) and `itemType` (`feature` only;
  record and coverage collections are listed but not offered).
- Items: `/collections/{id}/items?bbox=w,s,e,n&limit=N` (`limit` 1000), paged by following the
  `next` link (never by building an offset); a page with no features ends paging.
- Schema: the collection's `schema` document (Part 5), else its `queryables` (Part 3), else
  fields inferred from a 50-item sample with the GeoJSON reader's rules. Properties marked
  `x-ogc-role: primary-geometry` or with a `geometry-*` format are the geometry, not fields.
- Ids: a positive integer `id` is kept; a string id becomes its number when it is one, else a
  63-bit hash (`OgcFeatureId`).
- A box crossing ±180 is sent as two queries.

**Esri GeoServices REST.**
- Service JSON (`?f=json`) → `layers[]` (and `tables[]`, not offered); group layers are flattened.
- Layer JSON gives `fields[]` (esriFieldTypeOID/Integer/SmallInteger/Double/Single/String/Date/
  GlobalID/GUID → `FeatureSchema`; Date is epoch ms → datetime), `geometryType`, `extent`,
  `maxRecordCount`, `advancedQueryCapabilities.supportsPagination`, `supportedQueryFormats`,
  `drawingInfo`, `objectIdField`.
- Query: `query?where=1=1&geometry=w,s,e,n&geometryType=esriGeometryEnvelope&inSR=4326&outSR=4326&spatialRel=esriSpatialRelIntersects&outFields=*&f=geojson`,
  paged with `resultOffset`/`resultRecordCount` while `exceededTransferLimit` is true.
- Fallbacks:
  - A server without `geoJSON` in `supportedQueryFormats` gets `f=json`, and Esri geometry
    (points, `paths`, `rings` with winding-order holes) is converted in fvkit.
  - A server without pagination is queried by object-id ranges (`returnIdsOnly=true`, then
    `objectIds=` in blocks of `maxRecordCount`).
- Same query against MapServer layers (`Query` in `capabilities`).
- As built (WF3): line and area layers are typed MultiLineString / MultiPolygon and every
  feature is wrapped to match, so an export to a typed GeoPackage table accepts them. Offset
  pages add `orderByFields=<objectIdField>` for a stable order; the page cursor is
  `offset:N` or `ids:<remaining ids>`. A geographic extent or a Web Mercator one is read; any
  other projection leaves the layer without an extent. Coded-value domains are not imported.

**Esri renderer → `FeatureStyle` (WF-D11).**
- Renderers: `simple` → single; `uniqueValue` → categorised (first `field1`; `field2`/`field3`
  joined with `fieldDelimiter` are not supported: imported as single with a log line); `classBreaks`
  → graduated (`minValue` + `classBreakInfos[].classMaxValue` → class lower bounds).
- Symbols: `esriSMS` circle/square (others → circle), size in points × 96/72 → px, colour RGBA,
  outline; `esriSLS` colour, width, style (`esriSLSDash`/`Dot`/`DashDot` → the nearest
  `line_preset`); `esriSFS` fill colour + outline. `transparency` (0–100) scales every alpha.
- Not imported (default symbol, logged): `esriPMS`/`esriPFS` picture symbols, CIM symbols, visual
  variables, `heatmap` and `dotDensity` renderers, scale-dependent label classes after the first.
- As built (WF4): Esri classes run (previous max, max], so each class after the first takes
  `nextafter(previous classMaxValue)` as its lower bound (`classMinValue` wins when given). No
  `defaultSymbol` makes the "Other" symbol invisible, as Esri draws nothing for unlisted values.
  Unique values of a numeric field are keyed as `FeatureCategoryKey` keys them ("1.50" → "1.5");
  `uniqueValueGroups` are read when `uniqueValueInfos` is absent. Hatched `esriSFS` fills are
  drawn solid. Labels take an Arcade `$feature.X` / `$feature["X"]` or legacy `[X]` expression of
  one field; the text symbol gives colour, size and halo. Unsupported pieces come back as
  `notes` (one sentence each) for WF6 to show.

## Steps (easiest first)

| Step | What | Done when |
|---|---|---|
| WF1 ✓ | **Remote layer and cache** (WF-D2, D6, D7, D10): `IHttpClient`, `RemoteFeatureLayer` over a `GeoPackage` cache, the tile scheduler and worker, `Revision()` moves on arrival, cancel on close. Clients are a small internal interface (`ListLayers`, `Describe`, `FetchTile`). | gtests with a fake client: a query returns cached features at once and the rest after the fetch; a feature on two tiles is stored once; a lower-zoom tile covers its children; a closed layer cancels its fetches; the feature cap stops fetching and reports it |
| WF2 ✓ | **OGC API client**: landing page, collections, items paging by `next`, queryables/schema, inferred schema, ±180 split | gtests on recorded USGS Water Data and GeoMet responses (stored under `port/fvkit/test/data/web/`): collections listed with extents; a two-page bbox fetch; a collection without queryables gets an inferred schema |
| WF3 ✓ | **Esri client**: service and layer JSON, field mapping, `f=geojson` paging, `f=json` geometry conversion, object-id paging fallback | gtests on recorded NWS WWA and federal-org responses: fields typed; dates as datetimes; paged fetch; an `f=json` polygon with a hole matches the GeoJSON answer for the same feature |
| WF4 ✓ | **Esri style import** (WF-D11) | gtests: the NWS WWA `uniqueValue` renderer becomes a categorised style with its colours; a `classBreaks` renderer becomes graduated with the right lower bounds; a picture marker falls back and logs |
| WF5 ✓ | **Feature-source registry and the package** (WF-D1, D3, D4): `RegisterFeatureSource` in fvkit, `NativeRegistrar::AddFeatureSource` (additive SDK), `peregrine.web` with the libcurl `IHttpClient` (User-Agent `Peregrine/<version>`), `FeatureOverlay::FileOpen` routing URL specs to the registry; workspaces save and reopen web layers | gtest: a workspace with a web layer reopens it through a registered fake opener; package test: the curl client fetches from a local test HTTP server; Peregrine opens the NWS WWA layer by URL with `--open` |
| WF6 ✓ | **Add Web Layer… dialog** (mac): Overlay ▸ Add Web Layer…, a URL field, Connect lists collections/layers with titles and geometry types, Add opens the chosen ones; recent connections in settings (`[web] recent`); Refresh and Reset to Service Style commands; status line shows "loading N tiles" | DeskKit gtests for the dialog model (`DeskHost` accessors, like the Find window); by hand: USGS monitoring locations, GeoMet hurricane cone and NWS warnings over Charleston |
| WF7 | **Linux**: brief in `port/linux-plan.md` (row LX-WF) for the GTK dialog and packaging the `peregrine.web` package (libcurl dev) | Linux PR merged and back-ported |

## WF5 as built

- **Registry** `fvkit/feature/feature_source.h`: `RegisterFeatureSource({name, matches, open})`,
  `IsFeatureSourceSpec`, `OpenFeatureSource`; process-wide, first registered claimant wins.
  `ReadOnlyFeatureLayer` wraps the opened layer for the overlay and refuses edits with
  "Web layer: export to a GeoPackage to edit".
- **Web source** `fvkit/feature/web/web_source.h`: `MakeWebFeatureSource(http, {cache_dir})`
  makes the "peregrine.web" source; one service object per base URL; a spec without a layer
  opens the first listed. The service kind comes from the URL path (`/FeatureServer`,
  `/MapServer` → Esri, else OGC API), so nothing is detected or stored (WF-D4's stored kind is
  not needed). An Esri layer's `drawingInfo` becomes its first style (WF-D11).
- **Overlay**: `FeatureOverlay::FileOpen` routes a claimed spec to the registry. The layer is
  read-only (`Persistence::is_read_only`, so Save goes to Save As; `AsEditTarget()` null; the
  edit hint says why); Save As GeoPackage turns it into an ordinary file layer. Its style
  travels in the workspace as configuration property `style` (`<row>.prop.style`, one-line
  JSON), loaded after the reopen, so the user's style wins over the service's. A tile landing
  asks the host for a redraw. `OverlaySession::OpenFile` with no type and
  `DeskHost::OpensAsOverlay` accept claimed specs, so `--open <url>` works.
- **SDK/packages**: `NativeRegistrar::AddFeatureSource`; manifest `feature_sources`
  (`id`, `display_name`, `implementation.native`), registered under the manifest id when the
  package loads (`port/sdk/PACKAGING.md`). `desk::DefaultUserCacheDir()` (~/Library/Caches/
  Peregrine, `$XDG_CACHE_HOME/peregrine`, `%LOCALAPPDATA%\Peregrine\cache`).
- **Package** `port/packages/web-features` (`peregrine.web`, libwebfeatures): `CurlHttpClient`
  (redirects, gzip, body cap, cancel, `User-Agent: Peregrine/<PEREGRINE_VERSION>`), staged
  like `peregrine.ais` and bundled by the existing Xcode script.
- **Left for WF6**: opening blocks the calling thread on the service and layer documents; a
  style change on a web layer marks it dirty, so closing asks to save; `refresh_minutes`
  (WF-D9) and Refresh not wired. WF6 lists off the UI thread and adds Refresh; the rest
  stays open (see "WF6 as built").

## WF6 as built

- fvkit: `FeatureSourceDesc::list` lists a service's layers as `FeatureSourceLayer` specs; the web
  source shares its per-URL service object between list and open. `FeatureOverlay` keeps the
  source's style (`source_style_json()`) and exposes `remote_layer()`.
- DeskKit `fv_desk_web_layers`: `WebLayerConnection` lists on a worker thread (polled on the UI
  thread), recent URLs in `web.recent.<n>` (8), `WebLayersStatus` in the status bar. Commands
  Add Web Layer…, Refresh, Reset to Service Style (one undo; overrides kept). Peregrine:
  `AddWebLayerWindow.swift`.
- By hand 2026-11-18: NWS watch_warn_adv listed two layers; Add opened WatchesWarnings over
  Charleston in the service colours. Also 2026-11-18: GeoMet LTCE – Snowfall styled by class (after
  the OGC type-name and cache-identifier fixes) and the FAA ADDS ArcGIS services (Class_Airspace,
  Special_Use_Airspace, US_Airport on services6.arcgis.com). GeoMet done; USGS hand check not run.
- **Open**: Add opens on the UI thread (Describe + layer document per layer); `refresh_minutes`
  (WF-D9) not wired; Reset is in the layer context menu, not inside Layer Style…; disabled web
  commands show on file layers' context menu; a style change still marks a web layer dirty.

## Risks

- **Live services change.** Tests run only on recorded responses; a hand check list in WF6 covers
  the live ones. No ctest touches the network.
- **Esri version spread.** ENC Direct (10.91) rejected both test queries; older servers vary in `f=geojson`
  and pagination support. The `f=json` and object-id fallbacks exist for them; WF3 records one
  pre-10.4-style response to test against.
- **Heavy polygons** (PDOK, federal boundaries) at low zoom. Esri `maxAllowableOffset`
  (generalisation in map units) per tile zoom is a cheap follow-up if WF6 shows it is needed; OGC API
  has no standard equivalent.
- **Cache growth.** No eviction in the first version beyond the per-layer cap; a cache size setting
  and LRU by tile time are a follow-up.
