<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Peregrine for Linux

The desktop map application on GTK4 (gtkmm-4), the Linux counterpart of
`port/apps/Peregrine` (`port/linux-plan.md`). A C++ shell over DeskKit's
`fv::desk::DeskHost`, the same object the macOS app drives. Every decision
lives in C++ under `port/`; this directory holds only the window, event
translation, the frame blit, and the menus and toolbar generated from
DeskKit's model. Dialogs come later.

| This app | macOS app |
|---|---|
| `application.{h,cpp}` | `AppDelegate.swift` |
| `map_window.{h,cpp}` | `MapWindowController.swift` |
| `map_canvas.{h,cpp}` | `MapCanvasView.swift` |
| `main_menu.{h,cpp}` | `MainMenu.swift`, `Toolbar.swift` |

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
installed.

### On the sample data

Map ▸ Build Map Catalog needs a directory dialog, which this app does not have
yet. The test helper `peregrine-gtk-build-catalog` does the same build from
the command line. GeoTIFF needs the GEOTRANS datum tables in `MSPCCS_DATA`:

```sh
export MSPCCS_DATA=$PWD/fvw_core/PdfLib/sdk/lib
build/port/apps/PeregrineGtk/peregrine-gtk-build-catalog $PWD/testdata /tmp/sample.sqlite
build/port/apps/PeregrineGtk/peregrine-gtk --catalog /tmp/sample.sqlite \
    --center 32.5814,-80.1416,6714
```

That opens on the Charleston orthophoto (`geotiff Color`). Give the data
directory as an absolute path: the catalog stores it as given.

### Tests

| Test | What it proves |
|---|---|
| `peregrine_gtk_test` (gtest) | every command in DeskKit's menu bar has one menu item and one action; shortcuts map to GTK accelerators; actions follow the commands' enabled and checked state; the menus rebuild when a catalog with data opens; the frame placement is a transform every GSK renderer draws |
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
| `--shot <png>` | Writes the window's content (map and status bar) once the first frame is drawn, prints the status line to stderr, and quits. |

Reopening the last catalog at the last view when `--catalog` is not given is
not implemented yet.

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
as text when the icon theme has no matching symbolic icon.

Shortcuts are DeskKit's, with Primary as Ctrl (`<Primary>` in GTK accelerator
syntax). Unlike the macOS app there is no Edit, Window or Help menu, and Quit
stays in the File menu, as in GNOME applications with a menu bar.

## Input

Drag pans; a touchpad two-finger scroll pans; a mouse wheel notch, Page Up and
Page Down step the map group's ladder about the cursor; a pinch zooms live and
settles on the nearest step when it ends.

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
