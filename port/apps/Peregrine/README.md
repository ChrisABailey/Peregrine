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
| `--shot <png>` | Writes the window (map and status bar) once the first frame is drawn, prints the status line to stderr, and quits. |

## Input

Drag pans; a trackpad two-finger scroll pans; a mouse wheel notch, Page Up and
Page Down step the map group's ladder about the cursor; a pinch zooms live and
settles on the nearest step when it ends.

## AppKit notes

- Views do not clip drawing to their bounds (macOS 14 SDK and later), and the
  `dirtyRect` passed to `draw(_:)` can extend past them. Fill `bounds`, never
  `dirtyRect`: a status bar that filled `dirtyRect` painted over the whole map.
- The map canvas is layer-hosting: it owns a black root layer and one sublayer
  holding the frame image, moved by `DeskHost::Placement()`. AppKit draws
  nothing in it.
