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

How to write the code inside a package (C library, native C++, Python) is covered in
`port/sdk/README.md` as each kind becomes available.

| Implementation kind | Status in this build |
|---|---|
| `builtin` (code compiled into Peregrine, named by the manifest) | loads |
| `library` (C ABI, `fv_plugin.h`) | loads (API 1); header `port/sdk/include/peregrine/fv_plugin.h`, example `port/sdk/examples/rangerings-c` |
| `native` (C++ against the Peregrine SDK, locked to one build) | loads when the build id matches; header `port/sdk/include/peregrine/fv_native.h`, example `port/sdk/examples/rangerings-cpp` |
| `python` (`pyfvw` subclass or function) | loads when the Python bridge (`libperegrine_python`) opens; otherwise *unavailable*; example `port/sdk/examples/rangerings-py` |

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
  "requires": { "peregrine": "1.1", "plugin_api": 1, "python": "3.12" },
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

### Implementation

Each overlay and tool entry has `"implementation"` with exactly one key:

| Key | Value |
|---|---|
| `builtin` | The name code inside Peregrine registered (`RegisterBuiltinOverlay` / `RegisterBuiltinTool`). |
| `library` | A class or tool name in the table the C library's `fv_plugin_entry` returns. |
| `native` | A class or tool name the native library registers. |
| `python` | `module:Class` for an overlay, `module:function` for a tool. |

A package holds C libraries or native libraries, not both.

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
- A tool function is called as `function(ctx)`. `ctx` is a `pyfvw.app.ToolContext`
  (`package_dir`, `open_file(path)`, `request_redraw()`), valid only during the call.
- Importing the module happens at load time: an import that raises, or a name that is not an
  `Overlay` subclass or a callable, makes the package *failed* with the Python error.
- Every call into Python takes the GIL, including draws on the render worker.

## Rules Peregrine enforces

A package that breaks any of these is refused at install, or listed as *failed* when it is found
on disk.

- Every overlay and tool id starts with the package id and a dot (`com.example.rangerings.rings`).
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
