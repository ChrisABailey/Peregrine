<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Layer manager plan (LM) — sources, layers and layer properties

**Status: APPROVED, 2026-10-12** (Chris: LM-D1–LM-D4). LM1 in progress on Linux; the DeskKit
part is written in Peregrine and back-ported to FVW with the plan.

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
| Joins and Relates, Time, HTML Popup | shown disabled until scheduled | LM5+ |

## 5. Decisions

| # | Question | Decision |
|---|---|---|
| LM-D1 | Unit of a layer | **Decided (Chris, 2026-10-12): one overlay per layer**; a source groups overlays |
| LM-D2 | Sources view and drawing order | **Decided (Chris, 2026-10-12): a tree of sources and their layers; a separate Layers view orders layers freely, ungrouped** |
| LM-D3 | Overlays without a file | **Decided (Chris, 2026-10-12): each is its own source** |
| LM-D4 | Spec of one table of a file | **Decided (Chris, 2026-10-12): `path\|layername=table`**, as QGIS writes it |

## 6. Sessions

| # | Scope | Proven by |
|---|---|---|
| LM1 | `source_spec.h`; GeoPackage tables open as one overlay each, sharing a package; `SourcesModel`; source/layer menus; read-only source and layer properties; Zoom to Layer/Source; Sources and Layers views in PeregrineGtk | FakeDesk opens a two-table GeoPackage as two layers of one source, hides the source, moves it, and reads its properties; the workspace restores both tables |
| LM2 | Properties dialog with General, Symbology, Labels and Fields tabs; scale-dependent drawing | a layer hidden outside its scale range; style and fields edited through the tabs as through the old dialogs |
| LM3 | Display and Selection tabs | map tips show the chosen field; a layer's selection colour overrides the default |
| LM4 | Definition Query tab and query builder | a filtered layer draws, searches and hit-tests only matching features; the filter survives save and reopen |
| LM5+ | Joins and Relates, Time, HTML Popup; web-service sources | each as scheduled |

The mac shell follows each session: `LayersWindow.swift` gains the Sources view and the
context menus from the same DeskKit calls.
