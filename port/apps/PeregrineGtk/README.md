<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Peregrine for Linux

The desktop map application on GTK4 (gtkmm-4), the Linux counterpart of
`port/apps/Peregrine` (`port/linux-plan.md`). A C++ shell over DeskKit's
`fv::desk::DeskHost`, the same object the macOS app drives. Every decision
lives in C++ under `port/`; this directory holds only the window, event
translation, the frame blit and, later, menus and dialogs.

| This app | macOS app |
|---|---|
| `application.{h,cpp}` | `AppDelegate.swift` |
| `map_window.{h,cpp}` | `MapWindowController.swift` |
| `map_canvas.{h,cpp}` | `MapCanvasView.swift` |

## Build and run

Ubuntu 24.04 or later, from the repository root:

```sh
sudo apt-get install -y build-essential cmake ninja-build pkg-config libgtkmm-4.0-dev xvfb
cmake -B build -G Ninja && cmake --build build -j
build/port/apps/PeregrineGtk/peregrine-gtk --catalog <catalog.sqlite>
xvfb-run -a build/port/apps/PeregrineGtk/peregrine-gtk --shot /tmp/shot.png
```

The target is skipped, with a configure message, when gtkmm-4.0 (4.10 or
later) is not installed. `ctest` runs `peregrine_gtk.shot_empty_catalog` under
`xvfb-run` when it is installed.

## Options

The same options as the macOS app, parsed by DeskKit (`fv_desk_launch.h`):

| Option | Effect |
|---|---|
| `--catalog <path>` | Opens a catalog. |
| `--center lat,lon[,scale]` | Moves the camera there and chooses the map at that scale. |
| `--shot <png>` | Writes the window's content (map and status bar) once the first frame is drawn, prints the status line to stderr, and quits. |

Reopening the last catalog at the last view when `--catalog` is not given is
not implemented yet.

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
- `--shot` draws the content through the window (`gtk_widget_snapshot_child`)
  and renders it with the window's renderer. A `GtkWidgetPaintable` would hold
  only the last painted frame, which is empty on the first one.
