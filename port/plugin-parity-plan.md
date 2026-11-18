<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Plugin overlay parity plan (PX)

Follows `port/extensions-plan.md` (EX1–EX8). Goal: an overlay delivered as a package, of any
of the three kinds (`native`, `library`, `python`), can do what Route, Points and Feature
overlays do — an editor with a toolbar button and its own toolbar, pointer and key gestures,
tool tips, context menus, item info, undo, several open documents — while a package overlay
without an editor stays a plain static or file overlay.

## 1. Where things stand (2026-11-04)

Read from the code: the `As*()` overrides of `RouteOverlay`, `PointOverlay`, `FeatureOverlay`;
`LibraryOverlay` (`DeskKit/fv_desk_library_plugin.cpp`); `PyOverlay`
(`bindings/pyfvw/pyfvw_module.cpp`); `fv_desk_packages.cpp` (which kinds get an
`editor_factory`); `Desk::BindOverlay`, `Desk::OverlayPointerDown`, `Desk::OverlayKeyDown`;
`DefaultToolbarLayout`; the mac and GTK toolbar icon mapping.

Columns updated as phases land (through PX7). Legend: ✓ works in the app · ◐ partly · ✗ no · — not applicable.

| Capability | Route | Points | Feature | native | library (C) | python |
|---|---|---|---|---|---|---|
| Static overlay (toggled from the Overlay menu) | — | — | — | ✓ | ✓ | ✓ |
| File overlay: New, Open, Save, Save As, Close, Recent, workspaces, several open at once | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Several save formats (`save_filters` → format index) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Revert | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Dirty flag set by the overlay | ✓ | ✓ | ✓ | ✓ host service | ✓ `set_dirty` only | ✓ `dirty` |
| Draw | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Hover tool tip and status text (HitTest) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Edit hint (status text while editing) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Context menu section | ✗ | ✓ | ✗ | ✓ | ✓ | ✓ |
| Item Info sheet | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Snap-to | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Search | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Property page | ✓ | ✗ | own dialog | ✓ | ✓ | ✓ |
| **Editor**: toolbar mode button, editor toolbar and menu, cursor, UI constraints | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Pointer presses and drags | ✓ | ✓ | ✓ | ✓ while its editor is active | ✓ while its editor is active | ✓ while its editor is active |
| Keys | ✓ | ✓ | ✓ | ✓ while its editor is active | ✓ while its editor is active | ✓ while its editor is active |
| Edit focus, per-instance undo/redo (EditTarget) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Mouse capture during a drag | ✓ | ✓ | ✓ | ✓ host service | ✓ host service | ✓ host service |
| Edit ▸ Undo (Desk stack, then the edited overlay's EditTarget) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Host services: elevation, label font, display scale, route planner | ✓ | ✓ | ✓ | ✓ | ◐ elevation, status text | ✓ (elevation, label font, display scale; no route planner) |
| Redraw request after background work | — | — | — | ✓ | ✓ | ✓ |
| Selection, Edit Feature form, attribute table, export selection | ✗ | ✓ | ✓ | ✗ | ✗ | ✗ |
| Toolbar or tool icon from the package's `.svg`/`.png` | — | — | — | ✓ mac (GTK: LX-PX) | ✓ mac (GTK: LX-PX) | ✓ mac (GTK: LX-PX) |

What decides most of the table:

1. **Only `native` gets an editor.** `fv_desk_packages.cpp` sets `editor_factory` for
   `builtin` and `native` entries; `library` and `python` have no way to name one. The toolbar
   (`DefaultToolbarLayout`: `editor.mode.*`, then `editor.tool.*`) and the Editor menu follow
   from `editor_factory`, so those two kinds get neither.
2. **Pointer and key events reach overlays only while an editor is active**
   (`Desk::OverlayPointerDown`/`OverlayKeyDown` return early otherwise), or under mouse
   capture. So `on_mouse` in the C ABI and `on_mouse_*`/`on_key_down` in Python never fire
   in the app; the gtests that exercise them call the overlay directly.
3. *(Fixed in PX9.)* **Host services are wired by `dynamic_cast`** in `Desk::BindOverlay` (manager for capture,
   planner, elevation, undo stack, label font, display scale) and in the selection and
   feature-form code. A plugin type, native included, matches none of the casts.
4. **The C ABI is version 1.0**: draw, open/save, hit test, properties, mouse. Nothing for
   menus, keys, editors, edit hints, snap, search, item info, undo or dirty.
5. **The shells map icon names to built-in symbols only** (`Toolbar.swift`
   `image(for:)`, GTK `IconNameFor`); the absolute path a manifest icon resolves to is not
   loaded.

EX-D1 says `native` has "everything a built-in overlay has"; items 3 and 5 meant it did not.
PX9 closed item 3: every core overlay takes its services through `OverlayHost`, and
`BindOverlay` keeps only the route planner and routing settings.

## 2. Decisions

Accepted by Chris 2026-11-04 "for now"; revisit if a phase shows otherwise. Revised the same
day: nothing in the repository has outside users, so no decision keeps backward compatibility
(PX-D3, PX-D4, PX-D7).

| # | Question | Decision |
|---|---|---|
| PX-D1 | How a `python` or `library` overlay declares its editor | An `"editor"` key on the overlay entry, `{"python": "module:Class"}` or `{"library": "name"}`, the same shape as `implementation`. No editor key: a static or file overlay without one, as today. |
| PX-D2 | Do overlays get pointer events with no editor active? | No. Keep FalconView's model: gestures belong to editors; hover, tool tips and context menus already reach every overlay. |
| PX-D3 | How host services reach overlays | A new capability `app::HostAware` with `SetHost(OverlayHost*)` (null unbinds), called by `Desk::BindOverlay` for any overlay that has it. `OverlayHost` offers: request redraw (any thread), capture and release the mouse, the elevation source, label font, display scale, push an undo entry, status message. Core overlays (route, points, feature, contour, TA mask) move onto it too, in PX9, and the `BindOverlay` casts go. |
| PX-D4 | C ABI growth | API 2.0, redesigned rather than extended; 1.x plugins are refused. `create` receives a host-issued overlay handle that every host call takes (no `self` registry); `set_dirty` is the only way a document becomes dirty (no inference from a handled press); `save` takes the format index; a `fv_editor_class` table joins `fv_plugin_api`. `struct_size` stays on every struct so later 2.x minors can append. |
| PX-D5 | Edit ▸ Undo | When the Desk stack is empty, fall back to the edited overlay's `EditTarget` undo/redo, so route, points and every plugin get Edit ▸ Undo, not only features. |
| PX-D6 | Selection, feature form, attribute table | Out of scope for plugins in this plan. A plugin that wants them implements `IEditableFeatureLayer` (native only); revisit after PX6. Chris expects the feature overlay architecture to become extensible later (see §4). |
| PX-D7 | How an editor finds the overlay it edits | `fv::app::OverlayEditor::edited()` (and `editors()`), set by the `EditorManager` that makes the editor. Native editor factories take no context; Python editors subclass `pyfvw.app.OverlayEditor` and read `self.edited`. Replaces `NativeEditorContext` (2026-11-04). |

## 3. Phases

| Phase | Work | Done when |
|---|---|---|
| PX1 ✓ | **Host services** (PX-D3): `app::HostAware`, `OverlayHost` implemented by the Desk, called from `BindOverlay`; thread-safe redraw request through the shell. pyfvw: `Overlay.host` (redraw, capture, elevation, status). C: `fv_host_api` gains `request_redraw(overlay)`, `capture_mouse`, `release_mouse`, `set_dirty`, `elevation_at`. | gtest: a native, a C and a python overlay each request a redraw from a worker thread and capture the mouse through FakeDesk |
| PX2 ✓ | **Python editors** (PX-D1): manifest `editor`, `PythonBridge::ResolveEditor`, a C++ `OverlayEditor` adapter over a `pyfvw.app.OverlayEditor` subclass (`activate`, `deactivate`, `tools()`, `default_cursor`, `ui_constraints`, `auto_enter_on_create`), with `self.edited` answering the edited overlay as its Python object (from `fv::app::OverlayEditor::edited()`, which every editor now has). | gtest: rangerings-py gains an editor; its toolbar button and tools appear in the toolbar model, and FakeDesk clicks place rings as the native example's do |
| PX3 ✓ | **C API 2.0** (PX-D4): overlay handle from `create` replaces the PX1 `self` registry; `fv_editor_class` (create, destroy, activate, deactivate, tool list with enabled/checked/icon, run tool, edited overlay); `fv_overlay_class` gains `on_key`, `context_menu`, `edit_hint`, `item_info`, `snap_to`, `search`, edit focus and undo/redo, `save` with format index, `revert`; dirty only through `set_dirty`. rangerings-c and the test plugins move to 2.0. | gtest: rangerings-c gains the same editor and the three examples draw and edit identically |
| PX4 ✓ | **Python catch-up**: `edit_hint`, `item_info` in the trampoline. | gtest: hover and Item Info on the python example match rangerings-c |
| PX5 ✓ | **Icons**: mac loads absolute `.svg`/`.png`/`.pdf` icon paths with `NSImage(contentsOf:)`; `MenuNode.icon` paths for editor tools too. LX brief for GTK (GdkPixbuf). | an installed package's editor button shows its own art on the mac |
| PX6 ✓ | **Edit ▸ Undo fallback** (PX-D5) | gtest: Edit ▸ Undo undoes a route waypoint drag and a plugin edit |
| PX7 ✓ | **analysis-py on the editor**: toolbar palette (Line, Path, Area, Viewshed, Profile), click-and-drag editing, background viewsheds that request a redraw when done; the context menu stays for the per-item commands. | the analysis example works like PythonView's tool in Peregrine-mac |
| PX8 ◐ | **Docs and Linux** (docs done; Linux PR open): `PACKAGING.md` capability table per kind, `fv_plugin.h` 2.0 notes, LX brief in `port/linux-plan.md`. | the Linux PR, back-ported |
| PX9 ✓ | **Core overlays on the host** (PX-D3): route, points, feature, contour and TA mask become `HostAware` and take manager, undo stack, label font, display scale and elevation from `OverlayHost`; `Desk::BindOverlay`'s casts go (the route planner may stay a Desk call). | gtest: the existing route/points/feature/contour suites pass with `BindOverlay` reduced to `SetHost` |

PX2 and PX3 are independent once PX1 lands; PX4 and PX5 are independent of everything; PX9 follows PX3.

## 3a. Linux brief

Moved to `port/linux-plan.md` § Plugin overlay parity (PX8): package icons from absolute
paths, keys to package editors, the options choice-row check, and the proof.

## 4. Later

- **Extensible feature overlays.** Chris (2026-11-04): the feature overlay architecture may
  later accept extensions. Candidates are the seams Desk now reaches by `dynamic_cast` on
  `FeatureOverlay`/`PointOverlay` (selection, Edit Feature form, attribute table, export
  selection, style and fields dialogs): each becomes a capability a plugin can implement, so
  PX-D6 is lifted rather than worked around. Not scheduled.
