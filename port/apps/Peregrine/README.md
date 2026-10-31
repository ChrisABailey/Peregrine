<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Peregrine for macOS

The desktop map application (`port/desktop-plan.md`). An AppKit shell in Swift
over DeskKit's `fv::desk::DeskHost`, which Swift imports directly through C++
interop (`Core/module.modulemap`). Every decision lives in C++; this directory
holds only windows, event translation, the frame blit and menus.

## Build and run

From the repository root:

```sh
cmake -B build && cmake --build build -j --target peregrine_core


xcodebuild -project port/apps/Peregrine/Peregrine.xcodeproj -target Peregrine \
  -configuration Debug SYMROOT=$PWD/build-xcode-peregrine build


build-xcode-peregrine/Debug/Peregrine.app/Contents/MacOS/Peregrine --catalog <catalog.sqlite>
```

Xcode never builds the core. Its "Check the Peregrine core" phase fails, with
the command to run, when `build/port/apps/Peregrine/libperegrine_core.a` is
missing or older than the sources under `port/`.

A catalog whose paths are relative (PythonView's `build/pythonview.sqlite`)
resolves them against the working directory; start the binary from the
directory the catalog was built in (`port/apps` for that one).

## Options

| Option | Effect |
|---|---|
| `--catalog <path>` | Opens a catalog. Without it the last catalog reopens at the last view. |
| `--center lat,lon[,scale]` | Moves the camera there and chooses the map at that scale. |
| `--run <command-id>` | Executes a command (`overlay.toggle.fv.contour`, `map.group.elevation`, …) after the catalog, camera and `--open` files are set; repeatable, in order. A `map.group.*` runs before the camera is placed, so `--center`'s scale settles on that group's ladder. |
| `--open <file>` | Opens an overlay file (`.gpkg`, `.geojson`, `.fvpoints`, `.rte`) before the `--run` commands other than `map.group.*`; repeatable. Finder's Open With does the same for those extensions. |
| `--shot <png>` | Writes the window (map and status bar) once the first frame is drawn, prints the status line to stderr, and quits. |

## Input

Drag pans; a trackpad two-finger scroll pans; a mouse wheel notch, Page Up and
Page Down step the map group's ladder about the cursor; a pinch zooms live and
settles on the nearest step when it ends. Map ▸ Zoom In / Zoom Out are ⌘= / ⌘-.
The arrow keys pan half a screen (Map ▸ Pan); ⌘R and ⌘O choose the Raster and
OpenStreetMap groups (Overlay ▸ Open… is ⌘⇧O). Holding Space makes a drag pan
the map whatever overlay is being edited.

While an editor is active, the file it edits is the top layer and the other
overlays draw at half opacity. Raising another file of the same type in
Layers… moves the edit to it; raising a different type closes the editor. In
the feature layer editor a double-click on a feature opens its form, and a
double-click while drawing a line or area places the last vertex and finishes
the shape. The attribute table, layer style, feature style and layer fields
are on the feature editor's toolbar.

The status bar holds feedback on the left: with an editor active, what a click
would do ("Add point", "Select area “Lake”"); otherwise what is under the
pointer, from an overlay or the vector base map, whose name is also the map's
tool tip. The centre shows the scale and series, and the right the position and
terrain height (feet, from the catalog's DTED) under the cursor. The window
title is the current overlay file's name, or the topmost one's.

## Menus, toolbar and dialogs

The File, Map, Overlay and editor menus and the toolbar are generated from
DeskKit's menu model (`DeskHost::MenuEntryAt`, `ToolbarEntryAt`) and rebuilt
when it changes; `MainMenu.swift` adds only what macOS requires (the
application menu, Edit, Window, Help) and moves `app.quit` into the
application menu. The toolbar shows commands with an icon; `Toolbar.swift` maps
DeskKit icon names to SF Symbols. Questions from the core (save changes?, open,
save as, choose from a list, choose a directory) arrive through
`DeskHost::SetRequestHandler` inside the call that asked, and `Dialogs.swift`
answers them with modal panels.

Map ▸ Map Data Sources… (`DataSourcesWindow.swift`) lists the folders the
catalog scans, stored in the catalog as absolute paths; Add… and Remove edit the
list. Each folder is searched for every format in its conventional subdirectory
(`rpf`, `tiros3`, `dted`, `geotiff`, …) and for VPF databases. Generate Coverage
removes all of the catalog's coverage and rescans every folder in the
background, with a progress sheet and a Cancel button that stops after the
current data source. It refuses while a listed folder is unreachable.

Overlay ▸ Layers… (`LayersWindow.swift`) is the layer manager
(`port/layer-manager-plan.md`) in two views. *Sources* is an outline of each
data source with its layers (each table of a GeoPackage is a layer); a
source's check box is mixed while only some of its layers show, and Move
Up/Down or a drag moves a source, or a layer within its source. *Layers* is
the drawing order, ungrouped. A right click on a row makes it current and shows
DeskKit's layer or source menu (`LayerMenuEntryAt`, `SourceMenuEntryAt`).

Properties… (`PropertiesWindow.swift`) has one tab per tab DeskKit lists. A
source has the read-only *Source* sheet alone. A layer adds *General* (name,
description, credits and the scale range it draws in, Current filling a scale
from the map) and, for a feature layer, *Symbology* and *Labels*
(`LayerStylePanel`) and *Fields* (`LayerFieldsPanel`). The app declares the
tabs (`SetPropertiesTabs`), so Layer Style… and Layer Fields… open Properties
at their tab. One Apply writes every tab, asking first when a change of fields
loses values. Style This Feature… keeps its own dialog over the same style
panel. *Display* sets opacity and, for a feature layer, the map tip and
hyperlink fields; *Selection* the layer's own halo colour and width;
*Definition Query* the features the layer shows, in the query builder.

The query builder (`QueryBuilderPanel.swift`) edits clauses of field,
comparison and value joined by all or any, or the filter expression typed by
hand, with Verify. Select by Attributes… on the layer menu
(`SelectByAttributesWindow.swift`) uses it to make a new selection, add to,
remove from or select within the current one.

## Options and settings

Map ▸ Options… and Overlay ▸ Options… open a window with one page per map
group or overlay type that declares `app::Properties` (`DeskHost::Options*`).
`OptionsWindow.swift` builds every page from the field list — a checkbox, a
number field (with a stepper for a bounded integer), a text field, a colour
well, a pop-up or a path with a Choose… button — so a new overlay's options
need no Swift. Apply writes the values to the settings, to the open overlays
or the map renderer, and to the user settings file; Cancel or closing the
window discards what was not applied.

At launch the app reads peregrine.ini (`fv::Settings::LoadDefault`) and lays
the user settings over it: `~/Library/Application Support/Peregrine/user-settings.json`,
which only the options window writes.

## AppKit notes

- Views do not clip drawing to their bounds (macOS 14 SDK and later), and the
  `dirtyRect` passed to `draw(_:)` can extend past them. Fill `bounds`, never
  `dirtyRect`: a status bar that filled `dirtyRect` painted over the whole map.
- The map canvas is layer-hosting: it owns a black root layer and one sublayer
  holding the frame image, moved by `DeskHost::Placement()`. AppKit draws
  nothing in it.
