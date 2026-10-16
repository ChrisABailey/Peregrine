<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Layer manager plan (LM) — sources, layers and layer properties

**Status: APPROVED, 2026-10-12** (Chris: LM-D1–LM-D4). LM1 and LM2 done 2026-10-12. LM3 and LM4
done 2026-10-12 in fvkit, DeskKit and the mac app (LM-D5, LM-D6 taken in that session, for
Chris to confirm); the Linux pages are still to build. Style rules (§5, LM5–LM8) added
2026-10-13, LM-D7–LM-D10 decided by Chris; LM5 (fvkit rule model) and LM6 (DeskKit rule list model) done 2026-10-13 (LM-D11 taken, for Chris to confirm). LM7 and LM8 done 2026-10-13 (LM-D12 taken, for Chris to confirm). Symbol icons (§5, LM9a–LM9b) added
2026-10-13: the icon set decided by Chris (LM-D13); LM-D14–LM-D17 proposed with it. LM9a (fvkit icons) done 2026-10-14.

## Goal

The Layers panel becomes a layer manager with more than one view of the overlay stack, a
right-click menu on every row, and one Properties dialog per layer whose tabs grow to cover
what a GIS user expects to set on a layer (ArcMap's Layer Properties is the model).

## 1. Sources and layers

- A **source** is where data comes from: a file today (`.gpkg`, `.geojson`, `.fvpoints`,
  `.fvrte`, …), a web service later. A **layer** is one drawable set of data from a source.
- **One overlay per layer** (LM-D1). Drawing, visibility, opacity, editing and undo stay per
  overlay; nothing in `fvkit` learns about sub-layers. A source is a grouping of overlays.
- A GeoPackage with several feature tables opens as one overlay per table. Each overlay's
  file spec names its table in the form QGIS writes, `rivers.gpkg|layername=rivers`
  (`fvkit/app/source_spec.h`); a file with one table keeps its plain path. Workspaces store
  the spec, so a reopened workspace restores each table.
- The overlays of one GeoPackage share one open package (one SQLite connection), so edits to
  two of its tables do not lock each other out. Save commits the package; Revert reverts it.
- An overlay without a file (grid, scale bar, an unsaved layer) is a source with one layer
  (LM-D3).

## 2. Views

| View | Rows | Reorder | Status |
|---|---|---|---|
| **Sources** | a tree: sources, each with its layers | sources (a source's layers move as a block) and layers within a source | LM1 |
| **Layers** (drawing order) | every layer, top first, ungrouped | any layer anywhere (the panel as built in FO3) | LM1 (kept) |

In the Sources view a source sits where its topmost layer draws, and its layers are listed in
drawing order even when the Layers view has interleaved them with other sources' layers
(LM-D2). Moving a source gathers its layers together above or below the neighbouring source.

Check boxes: a layer's shows or hides its overlay; a source's is on when every layer is
shown, off when none is, mixed otherwise, and clicking it shows or hides them all.

## 3. Context menus

The items, their order and when they are enabled come from DeskKit (`Desk::SourceMenu`,
`Desk::LayerMenu`); a shell only shows them and runs the chosen command.

- **Layer**: Properties…, Layer Style…, Layer Fields…, Attribute Table…, Zoom to Layer, Close.
- **Source**: Properties…, Show All Layers, Hide All Layers, Zoom to Source, Save, Close.

Feature-only items are disabled for layers without features. Close goes through the overlay
session, so a dirty layer asks to save first.

## 4. Layer Properties

One tabbed dialog per layer, plain data with apply and revert like the FO dialogs, bound to a
live overlay. Settings a file can carry go in its `layer_styles` JSON (FO-D5) so the file
keeps its own setup; settings of read-only sources go in the workspace.

| Tab | Content | Session |
|---|---|---|
| Source | path or URL, format, file size and date, layer count; per layer: geometry, feature count, fields, extent | LM1 (read-only, as the source/layer Properties) |
| General | name, description, credits, minimum and maximum drawing scale | LM2 |
| Symbology, Labels | the FO5 Layer Style dialog as tabs | LM2 |
| Fields | the FO7 Layer Fields dialog as a tab, plus aliases and number formats | LM2 |
| Source (edit) | change the source of a layer whose file moved | LM2 |
| Display | opacity, map tips (the field shown on hover), hyperlink field | LM3 |
| Selection | selection colour and width, overriding the default | LM3 |
| Definition Query | a filter over the layer's features, built with a query builder; uses the FO5B expression evaluator | LM4 |
| Joins and Relates, Time, HTML Popup | shown disabled until scheduled | LM9+ |

## 5. Style rules

A feature layer's style is an ordered list of **rules**, each one MapLibre style layer with
its own filter and drawing style. The rule list of a layer is the equivalent of one
style.json for that layer's data.

| Rule part | MapLibre | Edited by |
|---|---|---|
| name | `id` (unique within the layer) | renamed in place on the rule's row |
| type | `type`: `fill`, `line`, `circle` or `symbol` | chosen when the rule is made, changed with Change Type…; decides the style pages |
| filter | `filter` | the LM4 query builder (`QueryBuilderModel`, `QueryBuilderPanel`) |
| style | `paint` and `layout` | the page set for the type (below) |
| scale range | `minzoom`, `maxzoom` | minimum and maximum scale, as on the General tab |
| shown | `layout.visibility` | the rule row's check box |
| legend title | `metadata.fv:legend` | the rule's style pages; empty: the name |

- **Drawing order** is list order, first rule at the bottom, as in MapLibre.
- **Filters combine with the definition query**: a rule draws a feature that passes both.
- **Storage** stays the MapLibre document already kept in `layer_styles` (FO-D5): the rules
  are its `layers` array. Every expression `MapLibreStyleEngine` evaluates is allowed; any
  property or expression the editor cannot show is kept as written and survives a save.
  Import Style… and Export Style… read and write a plain style.json for the layer.
- **Single, categorised and graduated stay as generators.** They write a rule list as they do
  now and record the renderer in the root `metadata`. Duplicating, reordering or editing a
  rule by hand turns the style into `rules`; the Symbology tab then shows the rule list and
  offers Regenerate… (which replaces it) instead of the renderer pages. "Categorise into
  rules" writes one rule per value with an `==` filter instead of one `match` rule.
- **Per-feature overrides** (`FeatureStyle::overrides`) stay: each rule's paint values are
  wrapped in an outer `match` on `["id"]`, as today (LM-D9).

### Style pages per type

| Type | Pages |
|---|---|
| fill | colour, opacity, outline colour, pattern (from the sprite) |
| line | colour, width, opacity, dash, cap, join. A casing is a second line rule beneath; Add Casing duplicates the rule below with a wider width |
| circle | radius, fill, stroke colour and width |
| symbol | icon (sprite image or marker shape), size, rotation; text field, size, colour, halo, placement (point or along the line) |

LM7 built the symbol page with text only: a feature layer had no sprite sheet for `icon-image`
to name. Icons are LM9a–LM9b below.

### Symbol icons

A symbol rule gains an Icon page: icon (a picker with previews), size, colour, rotation and
opacity, as `icon-image`, `icon-size`, `icon-color`, `icon-rotate` and `icon-opacity`. The
Text page is unchanged, so one rule can draw an icon, a label or both.

- **The set** (LM-D13): the Maki pictograms (`testdata/GeoSymbol/makiPng`, 202 icons, CC0).
  Maki includes the marker shapes (`circle`, `square`, `triangle`, `diamond`, `cross`, `star`,
  and `-stroked` outlines of circle, square, triangle and star), so the builtin vector shapes
  are not added. A style names an icon by its Maki name (`restaurant`, `circle`).
- **Where it lives** (LM-D14): `port/tools/` packs the set into one sprite sheet (PNG and
  `sprite.json`), and the generated sheet is compiled into fvkit. No resource folder, no
  install step, the same on every platform; roughly 0.5 MB of binary. `NOTICE.md` gains the
  CC0 entry.
- **Who sees it** (LM-D15): feature layers only. `FeatureOverlay`'s engines fall back to the
  builtin sheet when the style names no sprite of its own; the OSM base map does not, since
  OSM-Liberty names Maki icons too and its goldens would change.
- **Colour** (LM-D16): the set is single-colour and is marked SDF in the sheet; the engine draws
  an SDF icon as an alpha mask tinted by `icon-color` (default black), as MapLibre does.
  The 32 px tiles are `pixelRatio` 2, so `icon-size` 1 draws a 16 px icon (Maki's design size is 15).
- **Circle to symbol** (LM-D17): Change Type from circle to symbol starts the rule at
  the `circle` icon (`square` for a square circle) in the circle's fill colour and a size
  matching its radius, so the points stay drawn.
- **Export**: an exported style names icons a viewer without the set lacks. Known gap; the
  ledger carries it. Writing a `sprite` URL for the set is a later choice.

Every property is a constant or **varies by**: a field (`match` for values, `step` for
classes: the categorised and graduated pages moved down to one property) or zoom
(`interpolate`). An Expression box takes any expression by hand, with Verify.

### Rules in the views

Rules are children of their layer in both the Sources and Layers views (LM-D7): a layer row
discloses its rules, top-drawn first. A rule's check box shows or hides it; rules drag within
their layer only. The rule menu (from DeskKit, `Desk::RuleMenu`): Edit Style…, Edit Filter…,
Duplicate, Rename, Change Type…, Delete. The layer menu gains Add Rule… (name and type).
Rule edits go through the overlay's undo and dirty the file.

## 6. Decisions

| # | Question | Decision |
|---|---|---|
| LM-D1 | Unit of a layer | **Decided (Chris, 2026-10-12): one overlay per layer**; a source groups overlays |
| LM-D2 | Sources view and drawing order | **Decided (Chris, 2026-10-12): a tree of sources and their layers; a separate Layers view orders layers freely, ungrouped** |
| LM-D3 | Overlays without a file | **Decided (Chris, 2026-10-12): each is its own source** |
| LM-D4 | Spec of one table of a file | **Decided (Chris, 2026-10-12): `path\|layername=table`**, as QGIS writes it |
| LM-D5 | Form of a query | **Taken 2026-10-12, for Chris to confirm:** a MapLibre filter expression, evaluated by the FO5B evaluator over each field's `FeatureCategoryKey` text, as the renderer sees it (`fv::FeatureFilter`). The builder's clauses (field, comparison, value; all or any) compile to it and read back from it; any other expression is kept as typed. A definition query is stored in `layer_styles` metadata as `fv:definition-query` |
| LM-D6 | What a definition query hides | **Taken 2026-10-12, for Chris to confirm:** drawing, hits, snapping, rubber-band selection, search, Select by Attributes and the attribute table. The layer and its saved file keep every feature; hidden features are deselected |
| LM-D7 | Where a layer's style rules live | **Decided (Chris, 2026-10-13): as children of their layer**, one overlay per layer still (LM-D1). Rules cannot interleave with another source's layers |
| LM-D8 | What a rule's filter hides | **Decided (Chris, 2026-10-13): hits and selection follow the rules.** A feature no shown rule draws (at the current scale) cannot be hit, snapped to or rubber-band selected; the attribute table, search and Select by Attributes still follow the definition query only |
| LM-D9 | Per-feature overrides under rules | **Decided (Chris, 2026-10-13): kept**, as an outer `match` on `["id"]` in each rule's paint values |
| LM-D10 | Legend | **Decided (Chris, 2026-10-13): one entry per shown rule**, titled by `metadata.fv:legend` or else the rule name; rules sharing a legend title form one legend entry |
| LM-D11 | What a rule's filter builder edits | **Taken 2026-10-13, for Chris to confirm:** only the non-geometry part of a filter (`SplitRuleFilter` / `JoinRuleFilter`). The generators' `["geometry-type"]` terms are kept and re-joined on apply, so the builder never shows or loses them |
| LM-D12 | Style export and import | **Taken 2026-10-13, for Chris to confirm:** Export writes the layer's style without per-feature overrides or selection, non-spec `fv-*` properties moved to layer metadata (`fv:paint`/`fv:layout`), the MapLibre demo glyph server as `glyphs`. Import requires a MapLibre style document (version 8, layers array), replaces the style as one undo step, and keeps the layer's own overrides |
| LM-D13 | Icons a symbol rule can name | **Decided (Chris, 2026-10-13): marker shapes plus a bundled pictogram set** (Maki, CC0). Not the user's own PNGs. Maki carries the shapes itself, so the builtin vector shapes are not added |
| LM-D14 | Where the icon set lives | **Proposed 2026-10-13, for Chris to confirm:** one generated sprite sheet compiled into fvkit |
| LM-D15 | Which engines use the builtin sheet | **Proposed 2026-10-13, for Chris to confirm:** feature layers only, as a fallback when the style names no sprite |
| LM-D16 | Icon colour | **Proposed 2026-10-13, for Chris to confirm:** the set is SDF; `icon-color` tints it |
| LM-D17 | Change Type circle → symbol | **Proposed 2026-10-13, for Chris to confirm:** the rule starts at the matching Maki shape, colour and size |

## 7. Sessions

| # | Scope | Proven by |
|---|---|---|
| LM1 | `source_spec.h`; GeoPackage tables open as one overlay each, sharing a package; `SourcesModel`; source/layer menus; read-only source and layer properties; Zoom to Layer/Source; Sources and Layers views in PeregrineGtk | FakeDesk opens a two-table GeoPackage as two layers of one source, hides the source, moves it, and reads its properties; the workspace restores both tables |
| LM2 | Properties dialog with General, Symbology, Labels and Fields tabs; scale-dependent drawing | a layer hidden outside its scale range; style and fields edited through the tabs as through the old dialogs |
| LM3 | Display and Selection tabs; Select by Attributes over the LM4 query builder | map tips show the chosen field; a layer's selection colour overrides the default |
| LM4 | Definition Query tab and query builder | a filtered layer draws, searches and hit-tests only matching features; the filter survives save and reopen |
| LM5 | fvkit rule model: `FeatureStyle` holds a rule list (name, type, filter, paint, layout, scale range, shown, legend title); arbitrary MapLibre layers read and written without loss; the generators write rules; hits follow the rules (LM-D8) | a hand-written style.json with four rule types, shared filters and zoom expressions draws, and reads back from a save with every layer, filter and expression unchanged; a feature outside every rule is not hit |
| LM6 | DeskKit rule list model (add, duplicate, rename, change type, reorder, show/hide, delete, all undoable); rule rows in both views; `Desk::RuleMenu`; DeskHost exposes it | FakeDesk duplicates a rule, renames it, filters it, moves it under the original and undoes each step; the views list the rules in drawing order |
| LM7 | Mac style pages per type, the filter through `QueryBuilderPanel`, "varies by" field or zoom, the Expression box | a roads layer built by hand as casing, line and label rules with a filtered motorway rule, checked on screen |
| LM8 (done 2026-10-13) | Categorise into rules, Regenerate…, the legend (LM-D10), Import Style… and Export Style… | an exported style.json loads in MapLibre and in a second layer unchanged |
| LM9a (done 2026-10-14) | fvkit icons: `port/tools/` sheet generator, the compiled sheet, the builtin-sheet fallback for feature layers (LM-D15), SDF tint by `icon-color`, `icon-opacity` | a Restaurants layer with a `restaurant` icon rule in red draws red pixels at each point; an OSM render golden is unchanged |
| LM9b | DeskKit Icon page properties and the icon list with previews; Change Type circle → symbol (LM-D17); mac icon picker in the rule style window | FakeDesk sets an icon, size and colour on a symbol rule, saves, reopens and reads them back; checked on screen on the mac |
| LM10+ | Joins and Relates, Time, HTML Popup; web-service sources | each as scheduled |

The mac shell follows each session: `LayersWindow.swift` gains the Sources view and the
context menus from the same DeskKit calls. PeregrineGtk follows LM6–LM9 by `linux/*` PR (LM9b: the Icon page and picker); the
rule APIs in DeskKit are additive.
