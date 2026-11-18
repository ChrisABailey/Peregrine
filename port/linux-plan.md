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

Safety net: the sync script refuses `--apply` while any file changed in Peregrine since its
last `Sync:` commit differs from FVW, or while anything under `port/apps/PeregrineGtk/` would
change, and lists the paths; back-port first. `--overwrite-downstream` discards that work and
is used only on Chris's word. A change to the
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

## Plugin overlay parity (PX, `port/plugin-parity-plan.md`)

Package overlays of every kind now get editors, keys, Get Info, undo and their own icons
(PX1–PX7, mac proven). Almost all of it lives in DeskKit and fvkit, so the GTK shell already
has it: the editor's mode button and palette come through `DefaultToolbarLayout` and the menu
model; Edit ▸ Undo is a DeskKit command; a plugin's cross-thread redraw is turned into an
invalidate by `Desk::FlushOverlayRedraw()` inside `DeskHost::Tick`; pointer moves reach an
editor only while a button is down (Desk decides). What is left for the shell:

- **Package icons.** DeskKit hands every toolbar `MenuItem.icon`, and the Layers panel's
  icons, either as a symbolic name or as the absolute path of a `.svg`, `.png` or `.pdf`
  inside an installed package (`OverlayTypeDesc::resource_dir` resolves a tool's relative
  path first). The mac (`CommandToolbarItem.image(for:)`) loads a path beginning with `/`
  once, cached by path (failures too), scales it to 18 pt high keeping its aspect, and draws
  SVG and PDF as template images that follow light and dark; PNG keeps its colours. GTK:
  where `IconNameFor` is called (`map_window.cpp` toolbar, `layers_panel.cpp`), an absolute
  path becomes a `Gtk::Image` from `Gdk::Texture::create_from_filename` (SVG through the
  gdk-pixbuf rsvg loader, PNG direct); PDF may fall back to the label. Recolour SVG/PDF art
  to the theme foreground where cheap (`Gtk::IconPaintable` or a symbolic-style CSS filter).
- **Keys to editors.** `MapCanvas` already maps keyvals to the virtual-key codes `fv_key_event`
  documents; check that Return, Escape, Delete and the arrows reach a package editor
  (analysis-py: Return finishes a line, Escape cancels).
- **Options choice rows.** The mac Options window sent "true"/"false" for every choice row
  because it tested `NSButton` before `NSPopUpButton`; `options_window.cpp` builds
  `Gtk::CheckButton` and `Gtk::DropDown` separately, so this is a check, not a fix.
- **Shipped packages.** `port/packages/osm-overlay` (package `peregrine.osm`, a native
  overlay that draws the catalog's OSM coverage with its own style sheet) is staged by CMake
  into `build/packages/peregrine.osm` with `lib/linux-x86_64` or `lib/linux-aarch64`. The mac
  app bundles `build/packages` as `Resources/packages` and passes it to
  `DeskHost::LoadPackages` as the system directory; GTK does not call `LoadPackages` yet, and
  when it does its system directory is `build/packages` in the tree and the installed
  `share/peregrine/packages` (CPack component `peregrine`) otherwise.
  `port/packages/ais-overlay` (package `peregrine.ais`, AIS6: live AIS traffic from Open
  Waters and a local NMEA receiver, collision alarm) builds only when CMake finds libcurl
  (`libcurl4-openssl-dev` or similar); it fetches the Open Waters stream itself, so the GTK
  shell needs no SSE code. Its alarms arrive as `DeskShell::ShowNotice`; nothing else in the
  GTK shell changes. It appears once GTK calls `LoadPackages`.

Proof: with `port/sdk/examples` linked, the rangerings-c editor's mode button shows
`rings.svg` and its Show Labels tool `labels.svg`; analysis-py's palette (Line, Path, Area,
Viewshed, Profile) draws a line with clicks and Return, a vertex drag undoes from Edit ▸ Undo,
and a viewshed computes in the background and appears without moving the mouse. Requires the
Python bridge on Linux for analysis-py; without it the package lists as *unavailable* and
only the C example is proven.

## Ownship and moving maps (OS, `port/ownship-plan.md`)

The DeskKit side is done and mac-proven (OS1–OS4, OS3b, OS6, OS7). Most of it already reaches
GTK through DeskKit:
- the moving map is a file type (File ▸ New ▸ Moving Map; Open on `.gpx`/`.nmea`; Save
  writes GPX);
- its editor tools come through `DefaultToolbarLayout` and the menu model;
- feed restarts and replay play inside `DeskHost::Tick`;
- the trail dots are drawn by the overlay;
- View ▸ Ownship… (Ctrl+Shift+M) is a DeskKit command.

Three things are left for the shell:

- **Ownship window** (mirror `OwnshipWindow.swift` as `ownship_window.{h,cpp}`). A floating,
  non-modal window titled "Ownship", about 420 px wide.
  - `MapWindow::Handle`: on `t.ownship_requested && host_->TakeOwnshipRequest()` show or raise
    it (`ShowPanel`); on `t.ownship_changed` call its `Reload()`.
  - Rows from `HostOwnshipPanel`: Mode (`status`), Source (`source`, "—" when empty), Time,
    Position, Altitude, Heading, Speed. A small dim "derived" label sits beside the heading,
    shown only when `heading_derived`.
  - Time row: Go to start, Play/Pause, Go to end buttons; a `Gtk::Scale` over
    `[start_s, max(end_s, start_s + 1)]` at `clock_s`; the `clock_text` label; and a rate
    `Gtk::DropDown` of `OwnshipRateLabel(0 … OwnshipRateCount()-1)`, selected at `rate_index`.
    When `rate_index` is -1, show `rate_label` with nothing selected.
  - All of the time row is insensitive unless `clock_enabled`.
  - Actions: start → `SeekReplay(start_s)`, end → `SeekReplay(end_s)`, play →
    `SetReplayPlaying(!playing)`, scale `value-changed` → `SeekReplay(value)`, rate →
    `SetOwnshipRate(index)`.
  - Block the scale's and drop-down's signals while `Reload()` sets them, or every reload
    seeks.
- **Feed dialog.** `HostOptionsKind` has a third kind, `kOptionsFeed = 2` (a moving map's
  Feed… tool). `map_window.cpp` handles only kinds 0 and 1 today, so add a third panel slot
  that reuses `OptionsWindow` with kind 2. Its title is "Moving Map Feed"
  (`options_window.cpp` names only Map/Overlay). The fields come from the same options model,
  and Apply restarts only that map.
- **Icons.** Add `main_menu.cpp` `kIcons` entries for `course_up`, `continuous`,
  `snap_to_road`, `trail_dots`, `listen`, `feed` and `ownship`. Pick Adwaita symbolic names;
  a missing one falls back to the label. Trail Line and Auto Centre have no icon and stay in
  the editor menu only.

Proof, with `testdata/kiawah_cycle.gpx` (it ships in Peregrine's `testdata/`):
- Opening it lands on its last fix with the whole ride in trail dots.
- In the Ownship window, scrub back: the dots after the clock turn faint. Play at 10× and
  pause.
- Trail Dots in the editor toggles the dots.
- New ▸ Moving Map on the demo feed draws a growing trail. Feed… switches it to `nmea_udp`
  port 10110, Apply restarts only that map, and a phone app broadcasting NMEA (GPS2IP or
  similar) moves the ship. Listen off pauses it, and the status bar says "paused".
- `--shot` cannot open a file, so the window and dialog are proven by hand.

## Web feature layers (WF, `port/web-features-plan.md`)

WF1–WF6 are done and mac-proven: a feature layer can come from an OGC API – Features service
or an Esri FeatureServer/MapServer, cached per service in a GeoPackage, and Overlay ▸ Add
Web Layer… lists a service's layers and opens the ticked ones. Everything except the dialog
is shared code that already builds on Linux:

- fvkit: the registry (`feature_source.h`, now with `FeatureSourceDesc::list`), the OGC API
  and Esri clients, `RemoteFeatureLayer` and its cache (`fvkit/feature/web/`). Cache files go
  under `desk::DefaultUserCacheDir()`, which is `$XDG_CACHE_HOME/peregrine` on Linux, in
  `web-features/<hash>.gpkg`.
- The `peregrine.web` package (`port/packages/web-features`, libwebfeatures with the libcurl
  `IHttpClient`). Its CMake builds only when it finds libcurl and stages
  `build/packages/peregrine.web` with `lib/linux-x86_64` or `lib/linux-aarch64`.
- DeskKit `fv_desk_web_layers.{h,cpp}`: `WebLayerConnection` (lists on its own worker
  thread; the UI thread polls it inside `DeskHost::Tick`), recent URLs, and the status-line
  text, which the existing status bar already shows. Commands `overlay.add_web_layer`
  (Overlay menu), `layers.refresh` and `layers.reset_service_style` (layer context menu)
  come through the menu model like every other command.

What is left for the Linux session:

- **Prerequisite: load packages.** The web source is registered by the `peregrine.web`
  package, so nothing claims an `https://` spec until the GTK shell calls
  `DeskHost::LoadPackages(system_dir, "", version)` once after `LoadSettings` and
  `StartLog`, as the mac does in `MapWindowController.init`. The system directory is
  `build/packages` when run from the tree and the installed `share/peregrine/packages`
  otherwise (the LX-PX bullet above). Until then Add Web Layer… is disabled, because its
  command is enabled only while a registered source can list (`AnyFeatureSourceLists`).
- **Dependencies.** The root README's apt list names `libcurl4-openssl-dev`; add it to the CI
  matrix's package list in `.github/workflows/linux.yml`. The .deb should install
  `build/packages/peregrine.web` under `share/peregrine/packages` and depend on `libcurl4`
  (or `libcurl4t64` on 24.04).
- **Add Web Layer window** (mirror `AddWebLayerWindow.swift` as
  `add_web_layer_window.{h,cpp}`). A floating, non-modal window titled "Add Web Layer",
  about 640 × 420, minimum 460 × 300.
  - `MapWindow::Handle`: on `t.web_requested && host_->TakeAddWebLayerRequest()` show or
    raise it through `ShowPanel` and put the cursor in the URL field with its text selected;
    on `t.web_changed` call its `Reload()` if it exists.
  - Top row: a URL entry with a drop-down of the remembered services (`WebRecentUrl(0 …
    WebRecentCount()-1)`, most recent first; GTK 4.10+ — a `Gtk::Entry` with a
    `Gtk::MenuButton` popover, as `QueryBuilderPanel` does for sample values). Fill the entry
    with `WebRecentUrl(0)` when it is empty. Placeholder: "OGC API – Features or Esri
    FeatureServer / MapServer URL". Then a small `Gtk::Spinner` and a Connect button.
  - Connect (button or Enter in the entry) calls `WebConnect(text)`. A non-empty return is
    an immediate refusal (blank, or not a URL any source claims); show it in the status
    label. Otherwise `Reload()`; the listing finishes on a later tick (`web_changed`).
  - A list of rows (`Gtk::ColumnView` or a `Gtk::ListBox` of grids): a check button
    (`WebLayerChosen(i)`, toggling calls `SetWebLayerChosen(i, on)`), Layer
    (`WebLayerTitle`), Geometry (`WebLayerGeometry`, may be empty) and Id (`WebLayerId`),
    titles ellipsized at the end. Selecting a row shows `WebLayerDescription(i)` in a dim
    wrapping label under the list, up to three lines. Activating a row (double-click or Enter)
    ticks that row alone and adds it.
  - Bottom row: the status label (`WebStatus()`: "Connecting to …", "3 layers", the error,
    or "The service offers no feature layers"), then Close (Escape) and Add (the default
    widget). Add is sensitive only while some row is ticked.
  - `Reload()`: spinner spinning and Connect insensitive while `WebState() == 1`; status
    label from `WebStatus()`; rebuild the rows; update Add and the description. Block the
    check buttons' signals while it sets them.
  - Add calls `AddWebLayers()`; on "" hide the window. A non-empty return is the error of
    the first layer that failed to open (the ticked layers after it are not opened): show it
    in the status label and refill the recent list, since the URL is remembered anyway.
  - Hiding the window calls `WebCancel()`, so a slow listing stops.
- **Context menu entries.** Refresh and Reset to Service Style appear in the layer menu
  `LayerMenuEntryAt` already returns; the Layers panel's popover needs no change. Both are
  disabled for a layer that is not a web layer.

Proof (by hand; no test touches the network):
- Add Web Layer… with
  `https://mapservices.weather.noaa.gov/eventdriven/rest/services/WWA/watch_warn_adv/FeatureServer`
  lists two layers, both "Multi-area". Ticking WatchesWarnings and Add draws the warnings in
  the service's colours, and the status bar shows "Loading N web tiles" until they arrive.
- `https://api.weather.gc.ca` lists many collections. LTCE – Snowfall opens, and
  Properties ▸ Symbology ▸ Varies by class offers the numeric `RECORD_SNOWFALL`.
- `https://services6.arcgis.com/ssFJjBXIUyZDrSYZ/arcgis/rest/services/Class_Airspace/FeatureServer`
  (FAA) opens one area layer of about 6,000 features.
- A typo'd host gives the error in the status label and Connect is usable again.
- Close during "Connecting to …" stops the listing; reopening the dialog shows the old
  rows gone.
- Workspace ▸ Save, quit, reopen: the web layer comes back from its URL, with the user's
  style if one was changed. Reset to Service Style on it restores the service's colours, and
  Edit ▸ Undo undoes that in one step.

## Sessions

| | Session | Proof |
|---|---|---|
| LX1 | **Linux CI baseline** — clean configure/build/`ctest` on GCC; confirm the geotrans forced include; fix what breaks (rules above) | suite green on Linux; skip count recorded in the PR |
| LX2 | **GTK skeleton** (DK4 on Linux) — `Gtk::Application`, window, map widget on `DeskHost`, status bar, pan/wheel/pinch/Page Up/Down, `--catalog`/`--center`/`--shot` | `--shot` under Xvfb writes a PNG; empty catalog shows the empty-state status |
| LX3 | **Generated menus** (DK7) — `Gio::Menu` from the menu model, actions bound to DeskKit commands, Ctrl shortcuts; any Swift-shaped leak in DeskKit fixed additively | every command reachable; menu rebuilds on catalog change |
| LX-PX | **Plugin parity on GTK** — package icons from absolute paths, editor keys, options choice check, shipped packages directory (section above) | rangerings-c icons in the toolbar; analysis-py edits and undoes; `--shot` with `--run overlay.toggle.peregrine.osm.overlay` draws OSM over the base map |
| LX-OS | **Ownship on GTK** — Ownship window, Feed dialog (`kOptionsFeed`), moving map editor icons (section above) | `kiawah_cycle.gpx` opens on its last fix with its trail; scrub, play at 10×; a live UDP feed moves the ship and Apply restarts only that map |
| LX-WF | **Web feature layers on GTK** — `LoadPackages` (shared with LX-PX), libcurl in apt/CI/.deb, Add Web Layer window (section above) | NWS warnings listed and drawn over Charleston; GeoMet snowfall styled by class; FAA airspace opens; a saved workspace reopens its web layer |
| LX4+ | Track the mac side: dialogs (DK5), options pages (DK6), panels (DK8), moving map (DK9), each after the mac session has landed the DeskKit part | same proof as the DK row |

LX2 and LX3 can start now; LX4 rows wait for the DeskKit side of the matching DK session to
reach Peregrine.
