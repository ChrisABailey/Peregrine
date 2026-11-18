<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Packaging a Peregrine overlay or tool

A **package** adds overlays and Tools-menu entries to Peregrine without rebuilding it. This
page is the package format: what goes in the directory, what the manifest says, and what
Peregrine checks when it installs and loads one. The format is read by
`port/DeskKit/fv_desk_packages.cpp`, and the tests in
`port/DeskKit/test/packages_test.cpp` are its executable reference.

The code inside a package is written against `port/sdk/include/peregrine/fv_plugin.h` (C),
`fv_native.h` (C++) or `pyfvw` (Python); the examples in `port/sdk/examples/` show each kind.
[What an overlay can do](#what-an-overlay-can-do) lists every capability and how each kind
supplies it.

| Implementation kind | Status in this build |
|---|---|
| `builtin` (code compiled into Peregrine, named by the manifest) | loads |
| `library` (C ABI, `fv_plugin.h`) | loads (API 2; API 1 libraries are refused); header `port/sdk/include/peregrine/fv_plugin.h`, example `port/sdk/examples/rangerings-c` |
| `native` (C++ against the Peregrine SDK, locked to one build) | loads when the build id matches; header `port/sdk/include/peregrine/fv_native.h`, example `port/sdk/examples/rangerings-cpp` |
| `python` (`pyfvw` subclass or function) | loads when the Python bridge (`libperegrine_python`) opens; otherwise *unavailable*; examples `port/sdk/examples/rangerings-py` and `port/sdk/examples/analysis-py` |

## Layout

```
com.example.rangerings/              the directory name is the package id
  peregrine-package.json             required
  lib/macos/librangerings.dylib      compiled code, one entry per platform
  lib/linux-x86_64/librangerings.so
  python/rangerings/__init__.py      Python code; this directory goes on sys.path
  resources/icons/rangerings.svg     icons, symbols, style sheets, data
  README.md  LICENSE
```

Only `peregrine-package.json` is required, and only paths the manifest names are read.
Everything else in the directory belongs to the package.

## The manifest

```json
{
  "format": 1,
  "id": "com.example.rangerings",
  "name": "Range Rings",
  "version": "1.2.0",
  "publisher": "Example Corp",
  "license": "LGPL-3.0-or-later",
  "description": "Concentric range rings around any point.",
  "requires": { "peregrine": "1.1", "plugin_api": 2, "python": "3.12" },
  "libraries": {
    "macos": "lib/macos/librangerings.dylib",
    "linux-x86_64": "lib/linux-x86_64/librangerings.so"
  },
  "python_path": "python",
  "overlays": [
    { "id": "com.example.rangerings.rings", "display_name": "Range Rings",
      "icon": "resources/icons/rangerings.svg", "display_order": 40,
      "file": { "extension": "rng", "filters": [["Range Rings", "*.rng"]] },
      "implementation": { "library": "rings" } }
  ],
  "tools": [
    { "id": "com.example.rangerings.wizard", "title": "Ring Wizard…",
      "icon": "resources/icons/wizard.svg", "submenu": "Range Rings",
      "implementation": { "python": "rangerings.tools:wizard" } }
  ]
}
```

| Key | Required | Meaning |
|---|---|---|
| `format` | yes | Manifest format, currently `1`. A higher number is refused with "needs a newer Peregrine". |
| `id` | yes | Reverse-DNS, two or more dot-separated segments of letters, digits, `-` and `_`. `fv.` and `peregrine.` are reserved for packages shipped with Peregrine. |
| `name`, `version` | yes | Display name; version as dotted numbers (`1.2.0`), compared numerically. |
| `publisher`, `license`, `description` | no | Shown in the Overlay Manager and the install confirmation. |
| `requires.peregrine` | no | Minimum Peregrine version. |
| `requires.plugin_api` | for `library` entries | The C plugin API major version the libraries were built against. |
| `requires.python` | no | The Python minor version the code was written for. Older than 3.10 is *unavailable*; newer than the host's Python loads with a warning. |
| `libraries` | for `library`/`native` entries | Platform key → library path. Keys: `macos`, `macos-arm64`, `macos-x86_64`, `linux-x86_64`, `linux-aarch64`, `windows-x86_64`. |
| `python_path` | for `python` entries | Directory prepended to `sys.path`. |
| `overlays` | one of these | Overlay types; each entry is described below. |
| `tools` | one of these | Tools-menu entries; each entry is described below. |

Keys Peregrine does not know are ignored, so later formats can add keys.

### Overlay entries

The same fields as a standalone overlay manifest (`port/DeskKit/fv_desk_overlay_manifest.h`):
`id`, `display_name`, `icon`, `kind` (`static` or `file`; implied by `file`),
`display_order`, `parent_display_name`, `top_most`, `opacity` (0–100),
`user_controllable`, `restore_at_startup`. A file type adds
`"file": {"extension", "filters", "save_filters", "directory"}`.

### Tool entries

`id`, `title` (required), `icon`, `description`, and `submenu`. Tools that share a submenu
title are grouped under it in Tools.

### Feature source entries

`"feature_sources"` lists sources of feature layers for specs that are not files, such as a web
service URL: `id`, optional `display_name`, and `"implementation": {"native": "name"}`, the name
the library passed to `NativeRegistrar::AddFeatureSource`. Feature sources are native only.
When the package loads, each is registered under its `id`
(`fvkit/feature/feature_source.h`), and a spec it claims opens in the Feature Layer overlay
read-only. `peregrine.web` (`port/packages/web-features`) is the example.

### Implementation

Each overlay and tool entry has `"implementation"` with exactly one key:

| Key | Value |
|---|---|
| `builtin` | The name code inside Peregrine registered (`RegisterBuiltinOverlay` / `RegisterBuiltinTool`). |
| `library` | A class or tool name in the table the C library's `fv_plugin_entry` returns. |
| `native` | A class or tool name the native library registers. |
| `python` | `module:Class` for an overlay, `module:function` for a tool. |

A package holds C libraries or native libraries, not both.

### Editors

A `python` or `library` overlay may add `"editor": {"python": "module:Class"}` or
`"editor": {"library": "name"}`, in the same kind as its implementation; a library editor
names an `fv_editor_class` in the table `fv_plugin_entry` returns. An overlay with an editor gets a toolbar mode button (from its `icon`), an
Editor menu and toolbar tools from the editor, and pointer and key events while its editor is
active; without one it is a plain static or file overlay. A `native` overlay registers its
editor in code.

## What an overlay can do

An overlay has a capability when it supplies the function or method in its kind's column;
absent ones are simply not offered (no Save for an overlay without a save function, no menu
section without a context menu). A C function is a member of `fv_overlay_class` (or
`fv_editor_class`, `fv_host_api`); a Python method is defined on the `pyfvw.overlay.Overlay`
subclass (editor rows: on the `pyfvw.app.OverlayEditor` subclass); a native overlay returns the
`fv::app` interface from the matching `As*()` override, and reaches host services through
`AsHostAware` (`fv::app::HostAware::host()`).

| Capability | `library` (C) | `python` | `native` (C++) |
|---|---|---|---|
| Draw | `draw` (required) | `on_draw(proj, canvas)` | `Overlay::OnDraw` |
| File overlay: New, Open, Save, Save As, Close, Recent, workspaces | `open`, `save(path, format_index)` | `file_new`, `file_open`, `file_save_as(spec, format_index)` | `AsPersistence` |
| Several save formats | manifest `save_filters`; `format_index` picks one | same | same |
| Revert | `revert` | `revert` | `Persistence::SupportsRevert`, `Revert` |
| Mark the document edited | host `set_dirty` (only way) | `self.dirty = True` | `Persistence::set_dirty` |
| Hover tool tip and status text | `hit_test` | `hit_test_point` | `AsHitTest` |
| Status text while editing | `edit_hint` | `edit_hint(x, y)` | `AsEditHint` |
| Context menu section | `context_menu` + `menu_command` | `menu_items` | `AsContextMenu` |
| Get Info sheet | `item_info` | `item_info(proj, x, y, tolerance_px)` → `pyfvw.app.ItemInfoSheet` | `AsItemInfo` |
| Snap-to candidates | `snap_to` | `snap_to_point` | `AsSnapTo` |
| Search | `search` | `search` | `AsSearch` |
| Property page | `properties_schema`, `get_property`, `set_property` | `properties_schema`, `read_property`, `write_property` | `AsProperties` |
| Editor: toolbar mode button, Editor menu, palette, cursor, UI constraints | manifest `editor.library` → `fv_editor_class` | manifest `editor.python` → `OverlayEditor` subclass | `editor_factory` in code |
| Pointer presses and drags (editor active, or mouse held) | `on_mouse` | `on_mouse_down`/`_up`/`_move`, `on_double_click`, `on_mouse_wheel` | `Overlay::OnMouse*` |
| Keys (editor active) | `on_key` (virtual-key codes) | `on_key_down` | `Overlay::OnKeyDown` |
| Edit focus and per-overlay undo; Edit ▸ Undo falls back to it | `enter_edit_focus`, `release_edit_focus`, `can_undo`/`undo`/`can_redo`/`redo` | same names | `AsEditTarget` |
| Redraw request from any thread | host `request_redraw` | `host.redraw()` | `OverlayHost::RequestRedraw` |
| Mouse capture during a drag | host `capture_mouse`/`release_mouse` | `host.capture_mouse()`/`release_mouse()` | `OverlayHost::CaptureMouse` |
| Ground elevation | host `elevation_at` | `host.elevation_at`, `host.open_elevation()` (worker threads) | `OverlayHost::ElevationAt`, `OpenElevation` |
| Status bar message | host `show_status` | `host.show_status` | `OverlayHost::ShowStatus` |
| Catalog coverage of a map format (the rows the base map draws from) | — | `host.coverage(format, area=None)` → `catalog.CoverageRow` list | `OverlayHost::Coverage` |
| Ownship fix (moving map, live or replay), from any thread | — | — | `OverlayHost::OwnshipFix` |
| Alarm the user must see (shown as a notice on the UI thread), from any thread | — | — | `OverlayHost::RaiseAlarm` |
| Label font, display scale | — (`fv_view.dpi_scale`) | `host.label_font`, `host.display_scale` | `OverlayHost` |
| Push onto the Desk undo stack | — | — | `OverlayHost::PushUndo` |
| Toolbar and tool icons from the package | manifest `icon`, tool `icon` (`.svg`/`.png`/`.pdf` path or symbolic name) | same | same |

Not available to any package: selection, the Edit Feature form, the attribute table and export
selection belong to the built-in feature and point overlays. A native overlay may implement
`IEditableFeatureLayer` itself.

### Python entries

Python runs inside Peregrine through `libperegrine_python`, which embeds `pyfvw` and links one
libpython. It is opened at the first python package; when it is missing or its Python is not
installed, python packages are *unavailable* and everything else loads.

- `python_path` goes on `sys.path` before the package's entries resolve. Pure-Python
  dependencies ship inside that directory.
- An overlay class subclasses `pyfvw.overlay.Overlay` and takes no constructor arguments; the
  manifest's `display_name` names each instance. Capabilities come from the methods it defines:
  `file_open`/`file_save_as` (a file overlay), `hit_test_point`, `properties_schema` with
  `read_property`/`write_property`, `menu_items`, `search`, and the mouse and key handlers.
- An editor class subclasses `pyfvw.app.OverlayEditor` and takes no constructor arguments.
  `self.edited` is the overlay being edited while the editor is active (else None). It
  overrides whichever of `activate()`, `deactivate()`, `tools()` (a list of
  `pyfvw.app.MenuNode`), `default_cursor()`, `ui_constraints()` and `auto_enter_on_create()`
  it needs.
- `overlay.host` (a `pyfvw.overlay.OverlayHost`, None outside a map window) offers
  `redraw()` from any thread, `capture_mouse()`/`release_mouse()`, `elevation_at(lat, lon)`,
  `open_elevation()` (a new source over the catalog's DTED and lidar, for one worker thread;
  None without any), `show_status(text)`, `label_font` and `display_scale`. An overlay that
  defines `on_host_changed()` is called when it joins a window and when the window's catalog
  or display scale changes.
- A tool function is called as `function(ctx)`. `ctx` is a `pyfvw.app.ToolContext`
  (`package_dir`, `open_file(path)`, `request_redraw()`), valid only during the call.
- Importing the module happens at load time: an import that raises, or a name that is not an
  `Overlay` subclass or a callable, makes the package *failed* with the Python error.
- Every call into Python takes the GIL, including draws on the render worker.
- Pointer presses and keys reach an overlay only while its editor is active (or while it holds
  the mouse), so an interactive Python overlay names an editor. `port/sdk/examples/analysis-py`
  edits through its editor's palette and keeps `menu_items` for per-item commands.
- A `properties_schema()` row of type `path` may add `"path_kind": "directory"` and
  `"path_filters": [["Label", "*.ext"], ...]`.

## Rules Peregrine enforces

A package that breaks any of these is refused at install, or listed as *failed* when it is found
on disk.

- Every overlay, tool and feature source id starts with the package id and a dot (`com.example.rangerings.rings`).
- Every path in the manifest is relative and stays inside the package: no `..`, no leading `/`,
  no backslashes, no drive letters. An icon ending in `.svg`, `.png` or `.pdf` is such a path.
  Any other icon is a symbolic name.
- A package installed from a `.pgpkg` contains no symbolic links.
- In the system and user directories, the package directory is named after its id.
- A file extension that another loaded overlay type already opens is refused. The first one
  loaded keeps it.
- A package loads whole or not at all. If one entry cannot be resolved, none of its overlays
  or tools are registered.

## The .pgpkg file

A `.pgpkg` is a zip holding the package directory:

```sh
cd build-output && zip -r ../com.example.rangerings-1.2.0.pgpkg com.example.rangerings
```

A zip of the directory's *contents* (the manifest at the root) also installs. Finder's
`__MACOSX/` entries are ignored.

## Where packages live

| Location | macOS | Linux | Removable |
|---|---|---|---|
| System | `Peregrine.app/Contents/Resources/packages/` | `/usr/share/peregrine/packages/` | no, disable only |
| User | `~/Library/Application Support/Peregrine/packages/` | `$XDG_DATA_HOME/peregrine/packages/` (`~/.local/share/…`) | yes |
| Linked | any directory, used in place | same | unlink only; files untouched |

When the same id is in more than one place, a linked copy wins over a user copy, and a user copy
wins over a system copy. The others are listed as *shadowed*.

**Linking is for development.** Install from Folder… points Peregrine at your build output
instead of copying it, so a rebuild followed by a relaunch picks up the change.

## When changes apply

| Action | Package not loaded | Package loaded |
|---|---|---|
| Install / link | immediately | the new version is staged and replaces it at the next launch |
| Enable | immediately | — |
| Disable | immediately | at the next launch |
| Remove | immediately | at the next launch |

A loaded package's code is never unloaded while Peregrine runs, because an open overlay may still
be using it. Installed packages and their state live in the user directory:
`.state.json` (disabled ids, linked directories, pending removals), `.pending/<id>` (staged
upgrades), and `.staging/` (installs in progress, cleared at launch). Remove only ever deletes
a directory inside the user packages directory that holds a `peregrine-package.json`.
