<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Linux plan (LX) — PeregrineGtk, the Linux desktop shell

**Status: APPROVED, 2026-10-07.** The Linux counterpart of the macOS app in
`port/desktop-plan.md` (the DK plan). That plan's architecture, decisions K1–K14 and §1 user
interface apply here unchanged; this file adds only what is Linux-specific and how the Linux
work runs alongside the mac work.

## Goal

`port/apps/PeregrineGtk` — a GTK4 (gtkmm-4, decision K3) shell over the same DeskKit/ViewKit
core as `port/apps/Peregrine` (AppKit). The two apps are one product on two toolkits: same
commands, menus, shortcuts (Ctrl for ⌘), status bar, settings keys, workspace files and
command-line options. Only the widgets, the look and the build/dev tools differ.

## Who works where

Two sessions run at once on different repositories:

| | Mac session | Linux session |
|---|---|---|
| Repository | FVW (private, the source of truth) | Peregrine on GitHub (a curated subset of FVW) |
| Owns | DeskKit/ViewKit API design, `port/apps/Peregrine`, `port/PORTING.md`, the plans | `port/apps/PeregrineGtk`, Linux-only CMake, Linux fixes in shared code |
| Delivers by | commit to FVW, then `sync_peregrine.py` to Peregrine | branch `linux/<topic>` + pull request on Peregrine |

**The sync overwrites Peregrine.** `port/tools/sync_peregrine.py --apply` copies FVW's bytes
over Peregrine and deletes any file FVW does not have (only the root `README.md`,
`CMakeLists.txt`, `.gitignore`, `PrivacyPolicy.md`, `CLAUDE.md` and `Screenshots/` are
Peregrine-owned). So every merged Linux PR is copied into FVW before the next sync:

```sh
# in FVW, for each merged PR (paths are identical in both repositories)
git -C ../Peregrine fetch
git -C ../Peregrine format-patch <base>..<merge> --stdout -- . ':!CMakeLists.txt' | git am -3
```

Safety net: if a sync dry run proposes removing or changing anything under
`port/apps/PeregrineGtk/`, or reverting a Linux fix, stop and back-port first. A change to the
Peregrine-owned root `CMakeLists.txt` is copied into FVW's root `CMakeLists.txt` by hand.

## Rules for the Linux session

1. **Branch and PR, never push to `main`.** One topic per PR. The PR description ends with a
   **Ledger notes** section (what landed, what is open, any decision the mac side must make);
   the mac session writes it into `port/PORTING.md`.
2. **Do not edit** `port/PORTING.md`, `port/*-plan.md`, `third_party/`, or
   `port/apps/Peregrine/` (the mac app; it cannot be built on Linux). Proposed plan changes go
   in the PR description.
3. **Shared code changes are small, separate commits** with a title naming the module
   (`DeskKit: …`, `ViewKit: …`, `fvkit: …`), so they back-port and review on their own.
4. **Shared APIs change additively.** Add a method or overload; do not rename, remove or change
   the meaning of anything the Swift shell calls (`fv_desk_host.h` and everything it reaches).
   The mac app is not built in the Linux environment, so a break there is invisible to you.
   If an API is wrong for GTK, add the right one and note in the PR that the old one can go.
5. **Logic goes in C++ under `port/`, not in the shell** (DK plan "the per-OS layer contains
   only what the OS forces to be per-OS"). If the GTK shell needs a decision the mac shell also
   makes (a default, a label, a rule), it moves into DeskKit with a gtest, and both shells
   call it. A FakeDesk test comes before the GTK widget that shows the feature.
6. **The whole suite stays green on Linux.** `ctest` before every PR. Tests that need map data
   `GTEST_SKIP()` without it — Peregrine ships none — and a new test that needs data does the
   same.
7. **Comments** follow the style in `CLAUDE.md`.

## `#ifdef` policy

Shared code (`port/` outside `port/apps/*`, and `fvw_core/`) is compiled on macOS, iOS, Linux
and Windows. The order of preference when Linux needs something different:

1. **Write it portably.** Standard C++17 and `<filesystem>` first. Most "Linux fixes" so far
   (include case, `fpos_t`, `min`/`max`, PIC) were portable fixes, not guards.
2. **Move the difference to the shell or to data.** Per-OS behaviour that is a UI or OS
   service (dialogs, paths shown to the user, fonts, clipboard) is a `DeskShell` method or a
   setting the shell supplies, not a guard in DeskKit.
3. **Guard narrowly when 1 and 2 cannot work** (a libc or compiler difference):
   - Test the feature or the platform, not the toolchain: `__has_include(<…>)` where a header
     decides it; else `_WIN32`, `__APPLE__`, `__linux__`. Do not write `#ifndef __APPLE__` to
     mean Linux — Windows takes that branch too.
   - Keep the guarded region to a few lines, with a one-line comment giving the reason.
   - The macOS branch keeps its exact current behaviour (bit-faithful output is tested there).
   - Prefer one guarded helper used everywhere over the same guard repeated at call sites.
   - `fv_desk_host.h` shows the pattern: `#if __has_include(<swift/bridging>)` with no-op
     fallbacks, so the header is plain C++ for GTK.
4. **Do not grow `port/include/fv_compat.h`** and do not guard `third_party/` (it is frozen);
   a `third_party` workaround goes in that module's CMake (e.g. the forced `<stdio.h>` in
   `port/geotrans/CMakeLists.txt`). If a shim seems unavoidable, propose it in the PR.
5. **Never `#ifdef` a test away.** A test that cannot run on a platform calls `GTEST_SKIP()`
   with the reason.

CMake follows the same rule: `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")` around Linux-only
targets, nothing in a shared module's CMake that changes the mac build.

## Keeping the two apps compatible

What must match (a difference here is a bug in one of the two shells):

- **Commands, menus and shortcuts** — generated from DeskKit's command registry and menu model;
  neither shell hand-builds a menu item that is not a DeskKit command. Shortcuts are written
  once (⌘ on mac, Ctrl on Linux) by DeskKit.
- **The map widget contract** — the order the mac shell uses: `Resize` on size/scale change,
  `Tick()` once per frame clock (GTK: `add_tick_callback`), repaint on `redraw`, paint the last
  frame through `Placement()`, status bar from the four `Status*()` strings.
- **Input vocabulary** — drag pans; precise scroll (touchpad, `GDK_SCROLL_UNIT_SURFACE`) pans;
  a wheel notch, Page Up and Page Down step the ladder about the cursor; pinch
  (`Gtk::GestureZoom`) previews live and settles on the nearest step.
- **Files and settings** — the same settings keys, `user-settings.json`, workspace format and
  `overlays.json`/`map-groups.json` manifests. Locations come from `fv::DefaultSettingsPaths()`
  (XDG on Linux); the shell does not invent its own.
- **Command line** — `--catalog <path>`, `--center lat,lon[,scale]`, `--shot <png>` with the
  same meaning as in `port/apps/Peregrine/README.md`.
- **Layout** — mirror the mac app's file split where a counterpart exists
  (`MapCanvasView.swift` → `map_canvas.{h,cpp}`, `MapWindowController.swift` →
  `map_window.{h,cpp}`, `AppDelegate.swift` → `application.{h,cpp}`), so a change to one shell
  points at the file to change in the other.

What may differ:

- Look and conventions: GNOME HIG (header bar, `Gtk::PopoverMenuBar` from a `Gio::Menu` built
  off DeskKit's menu model, `Gtk::FileDialog`) versus the macOS HIG.
- Build and tools: a plain CMake target (`peregrine-gtk`) versus the Xcode project; the Linux
  session's dev tools (Xvfb/Broadway for `--shot`, gdb, sanitizers) versus Xcode.
- Pixel conversion: the frame is premultiplied RGBA8; cairo wants premultiplied ARGB32 in
  native byte order, so the GTK blit swizzles (or uses `Gdk::MemoryTexture` with
  `R8G8B8A8_PREMULTIPLIED`). This stays in the shell.

## Environment

Ubuntu 24.04 or later:

```sh
sudo apt-get install -y build-essential cmake ninja-build pkg-config libgtkmm-4.0-dev xvfb
cmake -B build -G Ninja && cmake --build build -j && ctest --test-dir build -j8
xvfb-run -a build/port/apps/PeregrineGtk/peregrine-gtk --shot /tmp/shot.png
```

No map data ships, so the cloud session proves layout, menus, input and the empty-catalog
render; the same-workspace-renders proof with real data (DK7) is run on a machine with
`testdata/`.

## Sessions

| | Session | Proof |
|---|---|---|
| LX1 | **Linux CI baseline** — clean configure/build/`ctest` on GCC; confirm the geotrans forced include; fix what breaks (rules above) | suite green on Linux; skip count recorded in the PR |
| LX2 | **GTK skeleton** (DK4 on Linux) — `Gtk::Application`, window, map widget on `DeskHost`, status bar, pan/wheel/pinch/Page Up/Down, `--catalog`/`--center`/`--shot` | `--shot` under Xvfb writes a PNG; empty catalog shows the empty-state status |
| LX3 | **Generated menus** (DK7) — `Gio::Menu` from the menu model, actions bound to DeskKit commands, Ctrl shortcuts; any Swift-shaped leak in DeskKit fixed additively | every command reachable; menu rebuilds on catalog change |
| LX4+ | Track the mac side: dialogs (DK5), options pages (DK6), panels (DK8), moving map (DK9), each after the mac session has landed the DeskKit part | same proof as the DK row |

LX2 and LX3 can start now; LX4 rows wait for the DeskKit side of the matching DK session to
reach Peregrine.
