# Feature overlay plan (FO) — user data as GIS feature layers

**Status: APPROVED, 2026-10-10.** FO-D1–FO-D9 decided by Chris. FO1 done 2026-10-10. FO2 done 2026-10-10. FO3 done 2026-10-10. FO4 done 2026-10-10. FO5 done 2026-10-10. FO5B done 2026-10-10. FO5C done 2026-10-10 (Chris: one style engine, no duplicate expression evaluator or renderer). FO6 done 2026-10-10. FO7 done 2026-10-10. FO8 is next; FO9 done 2026-10-21; FO10 done 2026-10-21. FO9 and FO10 added 2026-10-21 (Chris).

## Goal

One overlay type for user data of any shape: points, lines and areas, each with fields the user
defines, styled per class of feature or per feature, and editable on the desktop, attributes
and geometry alike. Examples that must work without code written for them:

| Layer | Geometry | Fields |
|---|---|---|
| Historical monuments | Point | name, date built, date range commemorated |
| Pizza restaurants | Point | name, phone, style of pizza (one of a list) |
| Rivers | Line | name, depth (m), average flow (m³/s) |

The dialogs are generated from the layer's schema. No overlay ships its own form.

## 1. Formats

| Role | Format | Notes |
|---|---|---|
| Native, read/write | **GeoPackage** (`.gpkg`, OGC 12-128r18) | SQLite; already a dependency everywhere. Geometry is the GPKG blob (header + WKB). One file may hold several layers. |
| Interchange, read/write | **GeoJSON** (RFC 7946) | No schema in the file: types are inferred on open and become a real schema on save to GeoPackage. Parsed with nlohmann/json, already in the build. |
| Geometry model | **OGC Simple Features** | Point, LineString, Polygon (with holes) and their Multi- forms, in memory and as WKB. Not a file format. |
| Import only, optional, desktop | GDAL/OGR (FO8) | Shapefile, KML, GPX and the rest. Never a core dependency; the vendored `gdal_1.9.1` is not used. |

SpatiaLite is not supported (FO-D1).

## 2. Model

A **feature layer** is a schema, features and a style.

- **Schema** — ordered fields. Each has a storage type (text, integer, real, boolean, date,
  datetime), an optional semantic type (phone, URL, email, enumeration, measurement with a unit,
  multi-line text), a display name, a description, constraints (range, allowed values,
  required) and a default. A date range is two date fields paired by the schema, so QGIS sees two
  ordinary columns.
- **Features** — an id, one geometry, one value per field (null allowed).
- **Interface** — `IFeatureLayer` (read, query by box, schema) and `IEditableFeatureLayer`
  (insert, update, delete, schema change, transactions). GeoPackage, GeoJSON and the `.fvpoints`
  adapter (FO-D6) implement them.

Schema metadata is stored in GeoPackage's own extension tables, `gpkg_data_columns` and
`gpkg_data_column_constraints`, so QGIS opens the file with the right field names, descriptions
and pick-lists (FO-D2). Semantic types QGIS has no slot for (phone, URL) go in
`gpkg_data_columns.mime_type` or a constraint name; QGIS shows them as plain text.

## 3. Style

The user picks one of three renderers per layer, the model QGIS users know:

- **Single** — every feature drawn alike.
- **Categorised** — one field (pizza style); a symbol per value plus an "Other" bucket.
- **Graduated** — one numeric field (flow rate); value breaks mapped to width, size or a colour
  ramp.

A **per-feature override** ("Style this feature…") sits above the renderer, keyed by feature
id. User data columns are never used for style.

A style is a small JSON document in a subset of the MapLibre GL style spec: categorised is a
`match` expression, graduated a `step`. The vector seam already loads that format
(`OsmStyleEngine` over `LookupTableStyleEngine`), so drawing reuses it. The document lives in
the GeoPackage in a `layer_styles` table, the table name QGIS uses, with our JSON in its own
column. QGIS ignores what it cannot read and does not show the styling (FO-D2). Symbols (icons) are
embedded in the GeoPackage as `.fvpoints` embeds them, so a file carries its own look.

Symbol properties: point (shape, size, fill, outline, icon), line (colour, width, dash, casing),
area (fill colour and opacity, outline). Labels: field, font size, halo.

## 4. Dialogs (DeskKit models, native shells)

Each is plain data with apply/revert, bound to a live layer or feature, not to `Settings`. That
difference from the options model (`fv_desk_options`) is why it is a new model.

- **Feature form** — one control per field, chosen by storage and semantic type: text field,
  number with unit, date picker, two date pickers for a range, pop-up for an enumeration, check
  box, phone and URL as text with an action button. Validation from the constraints.
- **Attribute table** — every feature in a sortable, filterable grid. Selecting a row selects
  the feature on the map.
- **Layer fields** — add, rename, retype, reorder and delete fields. Changes that lose data ask
  first.
- **Layer style** — the renderer choice, the field, the classes or breaks (with "classify"
  from the data), a symbol editor per class and a live legend.
- **New layer** — geometry type, a starting schema (blank, or a template), file and format.

## 5. Editing (desktop)

- **Geometry**: add point; draw line; draw area; move a feature; vertex drag, insert (click on a
  segment), delete; holes added and removed in an area; snapping to vertices of any visible feature
  layer. Gestures follow `RouteEditSession` and `PointEditSession`: capture on press, Escape backs
  out one thing at a time, chords go back to the shell.
- **Selection**: click, shift-click, box. The form edits one feature; the table edits many.
- **Undo**: one stack per workspace (desktop-plan §2b) that every feature edit feeds; Edit ▸
  Undo/Redo drives it. This is DK8's Edit-menu undo item, built here (FO-D8).
- **Save**: GeoPackage edits are written in a transaction on Save; Revert discards. GeoJSON
  saves the whole file.

## 6. Existing overlays

- **`.fvpoints` and `fv.points` stay** (FO-D4, FO-D6). Pippin keeps its own UI and file format.
  On the desktop, a `.fvpoints` adapter exposes the fixed schema (name, category, elevation,
  remarks, phone, url) through `IEditableFeatureLayer`, so the feature form is also the Edit
  Point dialog (closes DK8d follow-up). File ▸ Save As offers GeoPackage.
- **Routes (`.fvrte`)** stay a route document; they gain export to GeoJSON and GeoPackage as a
  line layer.
- **DK8 remaining**: the layers panel is built in FO3 against the feature overlay, and search
  gains a feature-layer provider in FO4 (FO-D8).

## 7. Decisions

| # | Question | Decision | Notes |
|---|---|---|---|
| FO-D1 | Native format | **Decided (Chris, 2026-10-10): GeoPackage; no SpatiaLite** | GeoJSON for interchange. |
| FO-D2 | QGIS compatibility | **Decided (Chris, 2026-10-10): opens with the right fields** | Field names, descriptions and constraints via `gpkg_data_columns`. QGIS styling (SLD/QML) is not written. |
| FO-D3 | Editing scope | **Decided (Chris, 2026-10-10): attributes and geometry, desktop** | Points, lines, areas incl. holes. |
| FO-D4 | Pippin | **Decided (Chris, 2026-10-10): keeps its current UI** | No Pippin work in this plan. Pippin may draw GeoPackage layers read-only later; not scheduled. |
| FO-D5 | Style storage | **Decided (Chris, 2026-10-10): MapLibre-subset JSON in `layer_styles`** | Reuses the existing style engine. |
| FO-D6 | `.fvpoints` on desktop | **Decided (Chris, 2026-10-10): keep, behind an `IEditableFeatureLayer` adapter** | Alternative: one-way import to GeoPackage. Keeping it avoids two point formats diverging between Pippin and the desktop. |
| FO-D7 | GDAL | **Decided (Chris, 2026-10-10): optional desktop importer only (FO8), current GDAL, not vendored** | A core dependency would be the largest build on Linux and unworkable on iOS. |
| FO-D8 | DK8 overlap | **Decided (Chris, 2026-10-10): workspace undo, layers panel and feature search land in FO; DK8 closes with DK8d** | Avoids building them for `fv.points` and rebuilding them. |
| FO-D9 | Coordinate systems | **Decided (Chris, 2026-10-10): write EPSG:4326 only; read any SRS the file declares that GEOTRANS can convert, else refuse with a message** | GeoJSON is 4326 by RFC 7946. |

## 8. Sessions

| # | Scope | Proven by |
|---|---|---|
| FO1 | **Geometry and schema model** — Simple Features types, WKB read/write, envelope, `IFeatureLayer`, schema with semantic types; GeoJSON read/write with type inference | gtests: WKB round-trips every geometry type; the three example layers round-trip through GeoJSON with their types inferred |
| FO2 | **GeoPackage** — read/write layers, GPKG geometry blob, R-tree index, `gpkg_data_columns`/constraints, transactions | gtests: a file written here passes the OGC GeoPackage validator's core checks; a sample file opened in QGIS shows the field names and pick-lists (Chris, by hand) |
| FO3 | **Feature overlay** — `fv.features` type opening `.gpkg`/`.geojson`, drawn through a single style; layers panel view-model (order, visibility, opacity) | FakeDesk renders the rivers sample; Peregrine shows it with a layers panel |
| FO4 | **Feature form and attribute table** — DeskKit models, `.fvpoints` adapter, Peregrine dialogs, search provider | FakeDesk edits a pizza restaurant's phone and style through the model; the Edit Point dialog works on a `.fvpoints` file |
| FO5 | **Style** — renderer model (single/categorised/graduated), per-feature override, `layer_styles` storage, style dialog with legend | the pizza layer categorised by style and the rivers graduated by flow, saved and reopened identical |
| FO5B | **Shared style expressions** — one MapLibre expression evaluator in fvkit (`get`, `has`, `id`, `geometry-type`, `zoom`, `literal`, `to-string`/`to-number`, comparisons, `all`/`any`/`!`, `match`, `case`, `coalesce`, `step`, `interpolate` linear/exponential over numbers and colours). `OsmStyleEngine` accepts expression filters and expression paint/layout values beside the legacy forms; `ReadFeatureStyleJson` evaluates through the same code and its private evaluator goes. Colour stops interpolate (legacy `{stops}` too), dropping the O2 colour-step deviation | gtests: one expression suite shared by both callers; an OSM style using `match`/`interpolate` styles like its legacy twin; FO5 style JSON still round-trips |
| FO5C | **One engine for feature layers** — `FeatureStyle` stays the dialog's model but compiles to MapLibre JSON that a `LookupTableStyleEngine` loader renders, so `FeatureOverlay` stops drawing its own symbols (selection halo and legend swatches go through the engine too). Honours the standing rule: style engines are loaders over `LookupTableStyleEngine` | FakeDesk renders the rivers graduated and the pizza layer categorised identically before and after; no feature-specific draw code left in `feature_overlay.cpp` |
| FO6 | **Geometry editing** — draw/move/vertex edit for lines and areas, holes, snapping, selection, workspace undo stack and Edit ▸ Undo | FakeDesk draws an area with a hole, drags a vertex, undoes both |
| FO7 | **Layer fields and New Layer** — schema editing, templates | adding a field to a saved GeoPackage keeps its data and its QGIS field names |
| FO8 | **GDAL importer** (optional) — shapefile, KML, GPX into GeoPackage, desktop only | a shapefile with a `.prj` imports with its fields |
| FO9 | **Export selection; Save As into a GeoPackage** (Chris, 2026-10-21) — Export Selection… on the layer menu and the feature editor palette writes the selected features, with the layer's schema and style, to a GeoPackage: a new file, or a new table or an append to a matching table in an existing one, through the shared connection when that file is open (so a package can be built up from several layers). Save As onto an existing multi-table GeoPackage asks first and offers the same add-as-table path instead of replacing the file (the data-loss follow-up). Decided 2026-10-21 (Chris): append is offered only when the schemas match (same geometry type, same field names case-insensitively and types, any order; `SchemasMatchForAppend`), otherwise the selection goes in as a new layer; the exported table does not open as a layer (the status bar names it; an already-open target table shows the appended rows). UI uses existing prompts (Chris, 2026-10-22: no save-panel Replace alert for an existing file): a list of New GeoPackage… (save panel) / Existing GeoPackage… (open panel) / each GeoPackage already open, then for an existing file New layer / Add to "<table>" per matching table, asked only when a table matches; no new dialog | FakeDesk exports two selections from different layers into one open .gpkg and both tables reopen with their fields and styles; Save As onto a two-table file keeps the other table |
| FO10 | **Map context menu, Get Info, Edit** (Chris, 2026-10-21) — a right click on the map runs `PickSession::BuildContextMenu` (fvkit A5, built but not wired: `DeskHost::ShowContextMenu` is a no-op and MapCanvasView has no right-click). Every overlay under the point adds "Get Info…", which opens an info dialog whose rows the overlay supplies through a new capability (feature: attributes; point: its fields; route: name, length, legs). Routes and points add "Edit Route…" / "Edit Point…": raise the overlay to the top, enter its editor and select the item. DeskHost flattens the menu as it does `LayerMenu`; GTK by `linux/*` PR | FakeDesk right-clicks a feature, a point and a route: each menu has Get Info…, the info rows come from the overlay, Edit Route… leaves the route on top in route-edit mode |

PeregrineGtk follows each session through `linux/*` PRs, as for DK.
