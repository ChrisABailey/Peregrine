# Desktop plan (DK) — a native FalconView-class app for macOS, then Linux and Windows

**Status: APPROVED, 2026-10-05.** K1–K15 decided (Chris accepted every recommendation); K16 made in DK5. DK1
(ViewKit core), DK2 (DeskKit core) done 2026-10-05, DK3 (Overlay manifests) done 2026-10-06,
DK4 (mac skeleton), DK5 (menus, toolbar, dialogs, catalog build), DK5b (overlay drawing) and
DK6 (mac options dialogs, generated pages) done 2026-10-07. Next is DK7 (Linux spike, §5).

## Goal

A first-class desktop map application over Peregrine: native menus, windows, keyboard, trackpad,
file dialogs and look on each OS. macOS ships first; the architecture is judged by how little
code the Linux and Windows apps need.

**PythonView is not the template.** It stays what it is — a tk test harness that exercises every
seam (`--selftest`, `--shot`). Its menus are a record of what the core can do, not a UI design.
Logic that lives in its Python today (the scale ladder, analysis.py's flows, points.py's editor
wiring, the moving-map menu) moves into C++ when the desktop app needs it, so it exists once
for three shells.

## The lesson from Pippin

Pippin works because the shell is thin. But its thin layer is in the wrong language for this
plan: **PPMap.mm is 2,500 lines of Objective-C++** holding the render loop, frame cache,
underlay, camera stepping and gesture-to-camera arithmetic, and `PPBaseCoverage.h`,
`PPCameraFit.h`, `PPPerspective.h`, `PPFollowCadence.h` are portable C++ that happens to live in
an iOS framework. None of that can reach Linux or Windows.

So the rule for this plan: **the per-OS layer contains only what the OS forces to be per-OS** —
widgets, event translation, the pixel blit, file dialogs, menus as rendered objects, icon art,
location and serial APIs. Everything with a decision in it is C++17 under `port/`, unit-tested
on the mac.

---

## 1. The user interface

### 1a. Menu bar

Top level: **File · Map · Overlay · ‹Editor›**, plus the menus each OS requires (on macOS the
application menu, Edit, Window and Help, per the HIG). Every item is a DeskKit command (§2b), so
all three OS menu bars are generated from one definition.

**File** — the workspace and the current file overlay (K9): Save, Save As,
Close (the active file overlay, FalconView's behaviour), Save/Restore Workspace, Export Image,
Quit (on macOS, Quit lives in the application menu).

**Map** — the base map.
- **Map family**, a radio group: **Raster** (CADRG, GeoTIFF, TIROS; later GeoPDF, JPEG 2000 and
  any other format stored as an image), **Elevation** (DTED, drawn shaded as PythonView does),
  **OpenStreetMap**, **ENC**, **DNC**. A family appears only
  when the current catalog holds data for it. Which formats belong to which family is data
  (§3b), not code, so a new raster format joins "Raster" by one line in a JSON file.
- Projection ▸, Zoom In / Out / Reset, Go To Location…, Recenter on Data.
- **Build / Refresh Map Catalog…** and **Map Data Sources…** (the directories it scans). A build
  runs in the background with progress and can be cancelled.
- **Options…** — the unified map options dialog (§1c), one page per family.

**Overlay** — everything drawn over the base map.
- **Static overlays** as check items (Grid, Crosshair, Scale Bar, Coverage, Contour Lines,
  Terrain Avoidance Mask, …). A static type has at most one instance, so its menu item is an
  on/off toggle. Contour Lines and the TA Mask read DTED from the catalog whatever the base map
  is, so the Elevation map, contours and the mask can all be on at once, or either overlay can sit
  over a chart.
- **New ▸** (one entry per file type: Points, Route, …) and **Open…** for file overlays. Many
  instances of one file type can be open at once; each is listed in the menu and the layers panel.
- **Options…** — the unified overlay options dialog (§1c), one page per overlay type.

This is the distinction fvkit already makes: an `OverlayTypeDesc` with a `FileTypeDesc` is a file
type, without one it is static (`fvkit/app/type_registry.h`).

**‹Editor›** — a dynamic menu that exists only while an editor is active, titled with the
editor's name ("Route", "Points"). Its items are the editor's `OverlayEditor::Tools()`, the same
`MenuNode` tree the toolbar shows.

### 1b. Toolbar and editors

Editors behave as in PythonView (the mode dance, one editor instance per type, `EditorManager`).
The difference is presentation: **toolbar buttons are icons with tooltips, not text.**
`MenuNode::icon` and `OverlayTypeDesc::icon` are already symbolic names; each shell maps a name to
its own art (SF Symbols on macOS where one fits, a bundled icon set elsewhere). An overlay that
ships outside the app supplies its own icon file through its manifest (§3a).

### 1c. Options dialogs

Two dialogs of the same shape — **Map ▸ Options…** and **Overlay ▸ Options…** — each a list of
pages on the left and the selected page on the right (on macOS, a settings-style window).

- One page per overlay type (Grid: colours, tick marks; Route: routing rules, line style; Points;
  Moving Map; Contour Lines: interval, colours; TA Mask: altitude, clearance bands; …) and one per map family (Raster: display pixel pitch; Elevation: colour breaks, units, shading; OSM: style sheets; ENC:
  mariner settings, data directory; DNC: GeoSym assets, data families).
- **A page is generated from the type's `app::Properties` schema** — typed, grouped fields
  (`PropertySpec`: bool, int, double, string, colour, choice; `group` becomes a section). A new
  overlay gets an options page with no UI code on any platform. A type may register a custom page
  where a generated form is not enough; that is the exception.
- Gaps to fill in fvkit: a **path** property type (file or directory, with a filter), and a
  **writable user-settings store** beside the read-only `fv::Settings` — the options dialog is the
  first thing that has to write settings back (the §2b app-state gap in the ledger).

### 1d. Zooming and scale

Different from PythonView. **Zoom is anchored at the mouse, not the screen centre**, and a zoom
step means "the next map", not "the same map bigger".

The inputs: Page Up / Page Down, each mouse-wheel notch, and the trackpad pinch. A pinch is
continuous, so the preview scales live and on release the view settles on the nearest step
(K10). Two-finger trackpad scroll pans, as every macOS map does.

A step depends on the family's ladder kind (declared per family in §3b):

- **Series ladder — Raster.** Products mix freely, ordered by scale alone: the next step is the
  raster series with the next larger (or smaller) scale **that covers the point under the mouse**,
  whatever its format. Zooming in over one spot might go 1 km TIROS → 1:5M CADRG → 1:2M GeoTIFF →
  1:1M CADRG. The point under the mouse stays fixed on screen. Each map is drawn at its native
  scale.
- **Uniform ladder — Elevation, OpenStreetMap, ENC, DNC.** Every step changes the display scale
  by the same factor (×2, one OSM tile level). After each step the product drawn is the one whose
  native scale is **nearest the display scale** among those with data under the mouse — an OSM
  zoom level, an ENC band, a DNC library (general, coastal, approach, harbour), a DTED level. With
  only coastal and harbour charts, zooming in stays on coastal for several steps, magnified past
  its native scale, until the display scale is closer to harbour's native scale; then it switches
  to harbour. Where no finer product covers the mouse, the current one keeps magnifying.

"Nearest" is measured as a ratio (log scale), so 1:30,000 is nearer 1:22,000 than 1:50,000.

The ladder is a C++ component in ViewKit (§2a), replacing PythonView's `step_scale`: the anchor
is the cursor, candidates are limited to the current family, and the two ladder kinds replace
PythonView's product-first-refusal rule.

**Status bar**: the current map scale (`1:50,000`), the series or band being drawn, and the
cursor position in the user's chosen coordinate format. While an editor is active its hint text
(`AppShell::ShowHint`) takes the left side.

---

## 2. Architecture

```
 macOS app (Swift/AppKit)    Linux app (C++/GTK4)     Windows app (C++/WinUI 3)
   window · menus · panels · dialogs · map widget · input events · blit · icons · OS services
 ───────────────────────────────── per-OS line ─────────────────────────────────────
 port/DeskKit   fv::desk   the desktop application model (new)
                Commands · menu model · Workspace · options model · panel view-models ·
                overlay & map-family manifests · Undo · user settings · DeskShell
 port/ViewKit   fv::view   the interactive map view (new; extracted, shared with Pippin)
                MapView · scale ladder · render scheduler · frame cache · input→camera
 ───────────────────────────────────────────────────────────────────────────────
 fvkit · fv::app · RouteKit · Routing        (exists, tested — additions only)
```

### 2a. ViewKit — one map view for every shell

A platform-free controller behind every map widget. A shell feeds it surface size and scale,
input events in a neutral vocabulary (pointer down/move/up, scroll with a precise-or-notched
flag, magnify, rotate, key), and a "frame wanted" tick; it hands back finished RGBA frames and a
transform for previewing the last frame mid-gesture. It owns:

- the viewport and camera (projection, scale, rotation; zoom about a point, fling);
- **the scale ladder** of §1d, per family;
- the render thread and dirty discipline (Pippin's contract: engine touched on one thread,
  render on one worker against a snapshot, live gesture = transformed last frame);
- the frame/base-layer cache and underlay (P18, DR1, lifted out of PPMap.mm);
- routing input to the active `fv::app` editor, picking and hover hints.

Pippin moves onto it later, file by file, as an optional track (not a precondition).

### 2b. DeskKit — the application, minus the widgets

- **Command registry.** Every user action is a `Command`: id, label, default shortcut, icon
  name, `enabled()` / `checked()` predicates, action. The menu bar of §1a, the toolbar, context
  menus and a command palette are *generated* from it (`fv::app::MenuNode` already exists for
  this). Shortcuts are written once and mapped per OS (⌘ vs Ctrl).
- **Menu model.** Builds File / Map / Overlay / ‹Editor› from the registry, the catalog's
  families, the overlay type registry and the active editor, and signals when it changes (a
  catalog rebuild adds a family; an editor activates).
- **Workspace.** The window's state: catalog, map family and series, view, overlay stack, active
  editor. Saved and restored.
- **Options model.** The pages of §1c, generated from `app::Properties`, with apply/revert.
- **Panel view-models.** Layers (order, visibility, opacity), search results, data sources,
  route editor, moving-map status, analysis results. Each is plain data plus a change signal; a
  native panel binds to it and holds no state of its own.
- **Undo.** One stack per workspace, fed by editors.
- **DeskShell : fv::app::AppShell** plus the desktop extras (progress for long jobs, status
  bar, notifications). Each OS implements it once.
- **Headless driver.** `FakeDesk` runs any command sequence and renders the view to PNG, so a
  feature is tested before any shell shows it, and a shell bug can be told apart from a core bug.

### 2c. Rendering

v1 is `CpuCanvas` → RGBA → platform image (CGImage / cairo surface / D2D bitmap), as on Pippin,
where it is measured adequate. The escape hatch is the same: V7 native `ICanvas` backends, a
new backend rather than a new architecture.

### 2d. Build

The core stays the root CMake build. Linux and Windows apps are CMake targets. The mac app is an
Xcode project over a CMake-built core, as Pippin does it (`cmake --preset`, then Xcode), so
signing and notarization use Apple's tools.

---

## 3. Extending the app without editing it

FalconView did this with COM components. The replacement keeps the idea — a registry of types
that the app discovers, not a list the app hard-codes — without COM.

### 3a. Overlay manifests

`fv::app::OverlayTypeRegistry` already *is* the factory: an `OverlayTypeDesc` carries the id,
display name, icon, static-vs-file, extensions, default stack position, overlay factory and editor
factory. What is missing is a way to fill it without code in the app. So:

**One JSON entry per overlay type**, read at startup from the app bundle and from a user
directory (`~/Library/Application Support/Peregrine/overlays/` and the equivalents):

```json
{
  "id": "user.rangerings",
  "display_name": "Range Rings",
  "icon": "rangerings.svg",
  "kind": "static",
  "display_order": 40,
  "implementation": { "builtin": "user.rangerings" }
}
```

A file type adds `"file": { "extension": "rng", "filters": [["Range Rings", "*.rng"]] }`. The
menus, options page, layers panel, file-open dispatch and toolbar all follow from the entry — the
app's own code names no overlay type.

`implementation` says where the factory comes from. Three kinds, in the order they would be built:

| Kind | What the author writes | Cost and limits |
|---|---|---|
| `builtin` | A C++ overlay in the source tree that registers its factory under a name; the build links every such file in automatically. | No app code edited; needs a rebuild. Every built-in overlay uses this, so it is proved from day one. |
| `library` | A C++ shared library (`.dylib` / `.so` / `.dll`) exporting one `extern "C"` registration function with an ABI version check. | No rebuild of the app. Must be built against the same Peregrine headers with a compatible compiler (a C++ ABI, not a COM one). On macOS the library must be signed. |
| `python` | A `pyfvw.overlay.Overlay` subclass (as PythonView's `Crosshair` already is), named as `module:Class`. | Embeds CPython in the app — a real cost on macOS (bundling and signing a Python framework). Later, if wanted. |

### 3b. Map-family manifest

The same idea for §1a's families: a JSON file maps catalog formats to families.

```json
{ "families": [
  { "id": "raster",    "title": "Raster",        "formats": ["cadrg", "geotiff", "tiros"], "ladder": "series" },
  { "id": "elevation", "title": "Elevation",     "formats": ["dted-shaded"], "ladder": "uniform" },
  { "id": "osm",       "title": "OpenStreetMap", "formats": ["osm"],  "ladder": "uniform" },
  { "id": "enc",       "title": "ENC",           "formats": ["enc"],  "ladder": "uniform" },
  { "id": "dnc",       "title": "DNC",           "formats": ["dnc"],  "ladder": "uniform" }
] }
```

A new raster format (GeoPDF, JPEG 2000) still needs its `IRasterSource` in the format registry;
joining the Raster family is then one word in this file.

**Naming clash:** fvkit already uses "family" for a group of *features within one product*
(`fvkit/vector/families.h`, `port/families/*.json` — DNC's depths, bottom, …). In code the new
concept is `MapGroup` (`map-groups.json`); the UI still says "Map family".

---

## 4. Decisions

| # | Question | Decision | Why |
|---|---|---|---|
| K1 | macOS UI | **AppKit for the frame (windows, menus, toolbar, map view), SwiftUI inside panels, sheets and option pages** | Menus, key handling and window restore are smoother in AppKit; SwiftUI is the least code per generated form. |
| K2 | Swift ↔ core bridge | **Swift's C++ interop directly against DeskKit/ViewKit**, not an ObjC++ facade | We design this surface (value types, narrow handles), so it is written once in C++ and Linux/Windows call it natively. Proven at DK4: the app imports `fv_desk_host.h` directly; no ObjC++ layer. |
| K3 | Linux toolkit | **GTK4 via gtkmm-4** (confirm at DK7) | Native on GNOME, LGPL, C++. Qt6 if KDE look or reuse matters more. |
| K4 | Windows toolkit | **WinUI 3 with C++/WinRT** (confirm before the Windows app) | Current native toolkit; plain Win32 if packaging is too heavy. Not MFC. |
| K5 | First-release scope | §1 in full; points and routes with editing; search; moving map (NMEA/GPX); Range & Bearing | The FalconView core loop. Other analysis, printing, multi-window later. |
| K6 | Name | **Peregrine** | Matches the public repo; Pippin stays the phone app. |
| K7 | Distribution (mac) | Developer ID + notarized DMG first | No sandbox fight over arbitrary data directories in v1. |
| K8 | Minimum OS | macOS 14 | C++ interop and SwiftUI-in-AppKit hosting. |
| K9 | Save/Close of a file overlay | **File menu** (as FalconView); New and Open in Overlay | Keeps the familiar FalconView place for Save while Open sits with the overlay list. |
| K10 | Pinch | **Live preview, settle on the nearest step** | A pinch is continuous; the ladder is discrete. |
| K11 | Raster ladder across products | **Decided (Chris, 2026-10-05): mix by scale alone** — the next scale under the mouse, any raster format | §1d series ladder. |
| K11b | Vector ladder | **Decided (Chris, 2026-10-05): uniform steps; the product nearest the display scale is drawn** | §1d uniform ladder. Step factor ×2. |
| K12 | Raster past the end of the ladder | **Stop, and say so in the status bar** | Alternative: magnify the last series, as the uniform ladder does. |
| K13 | DTED | **Decided (Chris, 2026-10-05): an Elevation map family, plus Contour Lines and TA Mask as static overlays**, all three usable together | The overlays exist (`fv.contour`, `fv.tamask`). Open: nothing feeds the TA mask's altitude from the moving map yet (ledger §2a). |
| K15 | Overlay drawing threading (Chris, 2026-10-07) | **Worker under a stack lock, with FalconView-style cooperative cancel** between base frames and between overlays; a drag moves the last frame | UI-thread compositing would stall on a heavy overlay; a plain lock would make a click wait for a whole render. DK5b. Refined in DK5b: an overlay-only change does not interrupt the base pass (it holds no lock and its result stays valid), so the UI waits on at most one overlay, never a base frame. |
| K16 | Where editors are listed | **Overlay ▸ Edit ▸**, one check item per editor, as well as the toolbar | An editor whose type has no icon would otherwise be unreachable (the toolbar shows only commands with icons). |
| K14 | Plug-in kinds in v1 | **`builtin` only; `library` next; `python` on request** | `builtin` proves the manifest; the other two add only a loader. |

## 5. Sessions

Order is chosen so the second platform is proved early: a seam only one shell has used is not
yet a seam.

| | Session | Proof |
|---|---|---|
| DK1 | **ViewKit core** — viewport, input→camera, render scheduler, frame cache, **scale ladder per family**; extracted from PPMap.mm and PythonView, headless | gtests: cursor-anchored zoom keeps the point fixed; raster step picks the next-scale series under the cursor across formats (TIROS → CADRG → GeoTIFF → CADRG); uniform ladder magnifies coastal until harbour is nearer, then switches, and only where harbour covers the cursor |
| DK2 | **DeskKit core** — command registry, menu model, workspace save/restore, user-settings store, `map-groups.json`, FakeDesk | FakeDesk: families appear only with data; menu model rebuilds on a catalog change; workspace round-trips |
| DK3 | **Overlay manifests** — `overlays.json` loader over `OverlayTypeRegistry`, `builtin` registration, options model generated from `app::Properties`, the path property type | a test-only overlay registered by JSON alone shows up in menus, options and file-open dispatch |
| DK4 | **mac skeleton** — AppKit window, ViewKit map widget, pan/zoom/pinch/wheel, status bar with scale, open a catalog | runs; screenshot; Page Up at a corner steps the map under the cursor |
| DK5 | **mac menus, toolbar, dialogs** — generated menu bar incl. ‹Editor› menu, icon toolbar, DeskShell on NSOpenPanel/NSAlert, catalog build with progress | every command reachable; shortcuts per HIG |
| DK5b | **Overlay drawing** — one worker draws base then overlays; base cached per view, overlay pass over a copy; a cancel token checked between base frames and between overlays, set by a new view or any UI-thread overlay change; the UI takes the stack lock after setting it (waits for at most one frame or overlay); fvkit engine gains a per-frame cancel hook, later the overlay draw context too | Grid toggled from the menu draws; a held Page Up renders only the last step; an overlay change does not re-render the base |
| DK6 | **mac options dialogs** — Map and Overlay options, generated pages | Grid page changes colour and ticks with no Grid-specific UI code |
| DK7 | **Linux spike** — GTK4 window, same map widget contract, generated menus | the same workspace renders on Linux; any Swift-shaped leak in DeskKit fixed here |
| DK8 | **Panels and editing** — layers panel, points and route editors, search, undo | editor flows tested through FakeDesk first |
| DK9 | **Moving map** — NMEA/GPX/demo feed, follow modes | GPX replay end to end |
| DK10 | **mac release** — about/licensing, notarized DMG | installs on a clean mac |
| DK11+ | `library` plug-ins; Linux and Windows to parity; Pippin onto ViewKit (optional) | |

## 6. What this plan does not do

- No change to fvkit's contracts (D1–D6) or the `AppShell` inventory except additions.
- No web or Electron shell; no cross-platform widget toolkit on macOS.
- PythonView is not ported, deleted or frozen; it gains features only when a test needs them.
