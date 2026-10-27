<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Peregrine for Linux

The desktop map application on GTK4 (gtkmm-4), the Linux counterpart of
`port/apps/Peregrine` (`port/linux-plan.md`). A C++ shell over DeskKit's
`fv::desk::DeskHost`, the same object the macOS app drives. Every decision
lives in C++ under `port/`; this directory holds only the window, event
translation, the frame blit, the menus and toolbar generated from
DeskKit's model, and the windows that show DeskKit's dialog models: the
questions the core asks, the options, Map Data Sources, Go To / Find, the
layers panel, the feature-layer dialogs and the route profile.

| This app | macOS app |
|---|---|
| `application.{h,cpp}` | `AppDelegate.swift` |
| `map_window.{h,cpp}` | `MapWindowController.swift` |
| `map_canvas.{h,cpp}` | `MapCanvasView.swift` |
| `main_menu.{h,cpp}` | `MainMenu.swift`, `Toolbar.swift` |
| `dialogs.{h,cpp}` | `Dialogs.swift` |
| `data_sources_window.{h,cpp}` | `DataSourcesWindow.swift` |
| `options_window.{h,cpp}` | `OptionsWindow.swift` |
| `layers_window.{h,cpp}` | `LayersWindow.swift` |
| `properties_window.{h,cpp}` | `PropertiesWindow.swift` |
| `query_builder_panel.{h,cpp}` | `QueryBuilderPanel.swift` |
| `find_window.{h,cpp}` | `FindWindow.swift` |
| `feature_form_window.{h,cpp}` | `FeatureFormWindow.swift` |
| `attribute_table_window.{h,cpp}` | `AttributeTableWindow.swift` |
| `layer_style_window.{h,cpp}` | `LayerStyleWindow.swift` |
| `layer_fields_panel.{h,cpp}` | `LayerFieldsWindow.swift` |
| `new_layer_window.{h,cpp}` | `NewLayerWindow.swift` |
| `profile_window.{h,cpp}` | `ProfileWindow.swift` |

## Build and run

Ubuntu 24.04 or later, from the repository root. The packages and the
whole-tree build are in the root `README.md` (*Build ▸ Linux*); the app needs
`libgtkmm-4.0-dev` (gtkmm 4.10 or later), and its screenshot tests `xvfb`.

```sh
cmake -B build -G Ninja && cmake --build build -j
build/port/apps/PeregrineGtk/peregrine-gtk --catalog <catalog.sqlite>
xvfb-run -a build/port/apps/PeregrineGtk/peregrine-gtk --shot /tmp/shot.png
```

The target is skipped, with a configure message, when gtkmm-4.0 is not
installed. Under a Wayland session GTK prefers the desktop to Xvfb, so a
`--shot` run there needs `GDK_BACKEND=x11` (the tests set it).

### On the sample data

Map ▸ Map Data Sources… asks for a new catalog file, then lists the folders
to scan: Add… the `testdata` directory and Generate Coverage. The test helper
`peregrine-gtk-build-catalog` does the same build from the command line.
GeoTIFF needs the GEOTRANS datum tables in `MSPCCS_DATA`:

```sh
export MSPCCS_DATA=$PWD/fvw_core/PdfLib/sdk/lib
build/port/apps/PeregrineGtk/peregrine-gtk-build-catalog $PWD/testdata /tmp/sample.sqlite
build/port/apps/PeregrineGtk/peregrine-gtk --catalog /tmp/sample.sqlite \
    --center 32.5814,-80.1416,6714
```

That opens on the Charleston orthophoto (`geotiff Color`). Give the data
directory as an absolute path: the catalog stores it as given.

A feature layer to try the layer windows on is
`port/fvkit/test/data/charleston-rivers.geojson` (copy it first; saving
writes it): `--open rivers.geojson --run overlay.layers`. Overlay ▸ Edit ▸ Feature
Layer puts its palette on the toolbar (Attribute Table, Layer Style,
Layer Fields) and adds the Feature Layer menu.

### Tests

| Test | What it proves |
|---|---|
| `peregrine_gtk_test` (gtest) | every command in DeskKit's menu bar has one menu item and one action; shortcuts map to GTK accelerators, and a bare key (an arrow) is not one; actions follow the commands' enabled and checked state; the menus rebuild when a catalog with data opens; every icon the macOS toolbar draws has a symbolic icon; GDK keys map to the core's key codes; the frame placement is a transform every GSK renderer draws |
| `peregrine_gtk.dialogs` (gtest under its own Xvfb) | a choice dialog returns its button from a nested main loop, and closing it is the cancel; the save prompt's buttons map to the host's answers; a file request's filters reach the file dialog; Map Data Sources on `testdata/` creates the catalog, lists the folder, generates coverage behind the progress dialog and draws. On the rivers sample: the title names the file; the layers panel lists the stack and hides a layer; the Sources view groups a layer under its file, collapses, hides a source and opens the layer's Properties from its menu; the table filters, selects and opens the form, which refuses a bad value and applies a good one; Properties opens at Symbology for Layer Style…, classifies graduated, sets a label on the Labels tab and applies; at Fields for Layer Fields…, adds a field and asks before dropping one; the General tab refuses a scale range that never draws, then renames the layer and sets its range; the Display tab sets opacity and a hyperlink field and the Selection tab refuses a bad width, then applies its own colour; the Definition Query tab builds a clause, verifies it, and splits a typed expression into clauses; Style This Feature… takes the style tabs away and Layer Style… takes them back; Go To / Find lists results under group headings, steps the selection over a heading and goes to a coordinate on Enter; New Layer creates and opens a GeoPackage; the options window marks its model dirty and discards it on close; the profile window shows its message without a route; every mapped icon is in the icon theme |
| `peregrine_gtk.shot_empty_catalog` | `--shot` writes a PNG and the status line reports the empty catalog |
| `peregrine_gtk.shot_testdata` | a catalog built from `testdata/` opens at `--center` on the orthophoto, the status line names it, and the PNG holds the picture |

The screenshot tests run under `xvfb-run` with GSK's cairo renderer, the
fallback GTK uses without GL.

## Options

The same options as the macOS app, parsed by DeskKit (`fv_desk_launch.h`):

| Option | Effect |
|---|---|
| `--catalog <path>` | Opens a catalog. |
| `--center lat,lon[,scale]` | Moves the camera there and chooses the map at that scale. |
| `--open <file>` | Opens an overlay file (repeatable), after the camera is placed. |
| `--run <command-id>` | Executes a command (repeatable): `map.group.*` before the camera is placed, so `--center`'s scale settles on that group's ladder; the rest after `--open`. |
| `--shot <png>` | Writes the window's content (map and status bar) once the first frame is drawn, prints the status line to stderr, and quits. |

Reopening the last catalog at the last view when `--catalog` is not given is
not implemented yet. The macOS app's Edit ▸ Cut/Copy/Paste and Window menu
have no counterpart.

## Menus, toolbar and shortcuts

The menu bar (File, Map, Overlay and the active editor's menu) is a
`Gtk::PopoverMenuBar` over a `Gio::Menu` generated from DeskKit's menu model;
the model's separators become sections. Each command is one action in the
window's `cmd` group, so a menu item, a toolbar button and a shortcut all run
`DeskHost::Execute` with the same id. A toggle or radio command's action has a
boolean state that shows as a check mark. Enabled and checked state is copied
from the host whenever a frame, the status, the catalog or a job changes, and
after every command. The menus are regenerated when the host reports
`menus_changed`.

The toolbar sits in the header bar: one icon button per toolbar command
(a toggle button for a checkable one), with the label as tooltip and the label
as text when the icon theme has no matching symbolic icon. DeskKit's icon
names map to the nearest freedesktop/Adwaita symbolic icon (`IconNameFor` in
`main_menu.cpp`, the counterpart of `Toolbar.swift`'s SF Symbols table); the
layers panel uses the same map.

Shortcuts are DeskKit's, with Primary as Ctrl (`<Primary>` in GTK accelerator
syntax). A shortcut without a modifier (the arrows of Map ▸ Pan) is not
installed as an accelerator, so text fields keep their keys; the map widget
handles it instead, as on macOS. The Edit menu is DeskKit's (Undo and Redo of
the workspace history); unlike the macOS app there is no Window menu, and Quit
stays in the File menu, as in GNOME applications with a menu bar. Help ▸ Show Log,
which the macOS shell also adds itself, opens the application log in the
default viewer.

## Dialogs

The host asks its questions (save changes, choose files, a folder or a list
row, revert) from inside the call that raised them, through
`SetRequestHandler`, and expects the answer before that call returns. GTK's
dialogs are asynchronous, so the handler shows a modal dialog and runs a
nested main loop until it is answered. Meanwhile the map widget makes no host
calls (`MapCanvas::SetHeld`) and keeps painting the last frame.

- Files and folders use `Gtk::FileDialog` (the portal where there is one),
  with the request's filters. It does not report the filter chosen for a
  save, so the answer is always the first, as on macOS.
- Save, revert and list questions use `ChoiceDialog`, a modal window with the
  macOS app's wording. Escape and the close button are Cancel.
- A catalog build shows `JobDialog`, a modal progress dialog with Cancel,
  while `JobActive()`.
- Map ▸ Map Data Sources… opens `DataSourcesWindow`: the folders Generate
  Coverage scans, with Add…, Remove and Generate Coverage. It closes when the
  build starts and the progress dialog takes over.

## Windows over DeskKit's models

Each window is shown when the host's tick asks for it (`options_requested`,
`layers_requested`, `form_requested`, `table_requested`, `style_requested`,
`fields_requested`, `new_layer_requested`, `profile_requested`,
`properties_requested`, `find_requested`), is created
the first time and reloaded after, and hides rather than closes. Closing one
closes its model in the host (`CloseFeatureForm`, `CloseOptions`…), so the
next request starts afresh; `features_changed` hides a dialog whose model the
core closed and reloads the table. The windows hold no values of their own:
a control re-reads the model after each edit.

- **Map ▸ Options, Overlay ▸ Options** (`OptionsWindow`): pages in a sidebar,
  fields generated by type (check box, colour button, drop-down, path with
  Choose…, spin button for a bounded integer, entry otherwise); Restore
  Defaults, Cancel, Apply, OK. Closing discards what was not applied.
- **Overlay ▸ Layers…** (`LayersWindow`): two views. *Sources* lists each
  data source (a file; a built-in overlay is its own source) with its layers
  under it, a source's check box mixed while only some layers show; Move
  Up/Down and dragging move a source, or a layer within its source. *Layers*
  is the stack top first, any layer moving anywhere. Rows have visibility,
  icon, name and opacity; selecting one makes it current. A right click in
  Sources selects the row and shows DeskKit's layer or source menu
  (`LayerMenuEntryAt`, `SourceMenuEntryAt`) as a popover whose actions run
  the commands.
- **Properties…** (`PropertiesWindow`): one page per tab DeskKit lists. A
  source has the read-only *Source* sheet alone (a heading per section,
  values selectable). A layer adds *General* (name, description, credits and
  the scale range it draws in, Current filling a scale from the map) and, for
  a feature layer, *Display* (opacity, the map tip and hyperlink fields),
  *Symbology*, *Labels*, *Fields*, *Selection* (the default halo or a colour
  and width of the layer's own) and *Definition Query*. Layer Style… and
  Layer Fields… open it at their tab (`SetPropertiesTabs`). One Apply writes
  every tab, asking first when a change of fields loses values.
- **Attribute Table, Edit Feature** (`AttributeTableWindow`,
  `FeatureFormWindow`): the table sorts by its headers through the host,
  filters, selects on the map and opens the form on a double-click; the form
  has one control per field kind with the model's errors under each.
- **Style panel, Style This Feature** (`LayerStylePanel`,
  `LayerStyleWindow`): renderer and field, the legend with swatches the host
  draws and the selected class's symbol (the Symbology tab), and the labels
  (the Labels tab); the Style This Feature… dialog shows both over one
  feature's symbol. An edit rebuilds the panel once its signal has returned,
  since the rebuild replaces the control that sent it.
- **Query builder** (`QueryBuilderPanel`): clauses of field, comparison and
  value joined by all or any, the query as a sentence, the filter expression
  (editable by hand) and Verify, over one of the host's query models. The
  value's menu button lists the field's sample values (GTK 4.10 deprecates
  the entry-with-list widgets).
- **Fields panel, New Feature Layer** (`LayerFieldsPanel`, `NewLayerWindow`):
  the field list and editor of the Fields tab; New Layer's template, geometry
  and file.
- **Map ▸ Go To / Find…** (`FindWindow`): one field for a coordinate or a
  name, results under a heading per group (Location, Overlays, Roads, Map)
  with the distance DeskKit formats, and "Search the map in this view only"
  (on by default, kept for the session). Typing searches after 200 ms; Enter
  searches and goes to a single answer; Down moves into the list; activating
  a row or Go To moves the map there.
- **Route elevation profile** (`ProfileWindow`): the chart the host lays out,
  drawn with cairo, with a cursor readout and the summary line.

The application log starts after the settings load, in
`$XDG_STATE_HOME/peregrine` (`~/.local/state/peregrine` by default, from
`fv::DefaultLogDirectory`).

## Input

Drag pans; a touchpad two-finger scroll pans; a mouse wheel notch, Page Up and
Page Down step the map group's ladder about the cursor; the arrow keys pan
half a screen (`map.pan.*`); a pinch zooms live and settles on the nearest
step when it ends.

While an overlay is edited, a press goes to its editor with Shift (extend the
selection) and the click count (2 for the second press of a double-click,
within the desktop's double-click time and distance); a key goes to the
editor first, as a Win32 virtual-key code (`KeyCode`). Holding the space bar
makes a press pan the map whatever is edited, with a grab cursor.

The status bar is in three thirds: feedback from the map and the edited
overlay on the left, scale and series in the centre, position and terrain
height under the cursor on the right. The map's tooltip is
`DeskHost::HoverToolTip()`. The window title is the open overlay file's name
(`DocumentName`), else "Peregrine — <catalog>".

## Settings

At launch the app reads peregrine.ini (`fv::Settings::LoadDefault`) and lays
the user settings over it: `$XDG_CONFIG_HOME/peregrine/user-settings.json` (`~/.config/peregrine/` by default,
from `fv::desk::DefaultUserSettingsPath()`).

## GTK notes

- The frame is premultiplied RGBA8 and goes to the GPU as a
  `GdkMemoryTexture` in `GDK_MEMORY_R8G8B8A8_PREMULTIPLIED`; no swizzle.
- The core links a static libpng 1.2 (`fv_png`). The app links with
  `--exclude-libs,ALL` so those symbols are not exported and do not interpose
  the system libpng16 GTK uses.
- The frame is placed with a `GskTransform` built as translate · rotate ·
  skew · scale. GSK's cairo renderer draws a transform made from a general
  `graphene_matrix_t` as a pink fill.
- `--shot` draws the content through the window (`gtk_widget_snapshot_child`)
  and renders it with the window's renderer. A `GtkWidgetPaintable` would hold
  only the last painted frame, which is empty on the first one.
