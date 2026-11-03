<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Extensions plan (EX) — overlay packages, the Tools menu, the Overlay Manager

**Status: APPROVED, 2026-11-02** (EX-D5–EX-D8 accepted; native SDK publishes the full fvkit headers). Extends `port/desktop-plan.md` §3a
(overlay manifests, decision K14). Applies to Peregrine on macOS and Linux; Pippin and Osprey
are out of scope.

## Goal

A user can add and remove overlays without rebuilding Peregrine, and a developer can ship one
as a single file. Peregrine gains a **Tools** menu for things that are not overlays. Its first
two entries are the **Overlay Manager** (the setup tool for packages) and the **ENC Importer**.

What exists already: `OverlayTypeRegistry` is the factory. `fv_desk_overlay_manifest.*` reads
JSON entries from a list of directories, and the `builtin` kind registers. The `library` and
`python` kinds parse but are refused. `pyfvw` already lets Python subclass `fv::Overlay`.

## Decisions

| # | Question | Decision |
|---|---|---|
| EX-D1 | Compiled overlays | **Two kinds.** `library`: a plain C ABI (`fv_plugin.h`, C99), versioned function tables, no fvkit or C++ types across the boundary; it survives Peregrine releases until the major API version changes, and it grows by optional tables (§2). `native`: C++ against fvkit itself, with everything a built-in overlay has, locked to one Peregrine build by a build id and refused, never loaded, on a mismatch (§2b). `library` is for code distributed outside our control; `native` is for overlays rebuilt with each release. Supersedes K14. (Chris, 2026-11-02) |
| EX-D8 | How `native` plugins reach fvkit | **Peregrine's core becomes one shared library**, `libperegrine_core` (fvkit, DeskKit, ViewKit, format and vector libraries), inside the app bundle / under `/usr/lib/peregrine`. Plugins link against it. One copy of every process-wide table (format registry, builtin overlay table, log sinks) is then guaranteed, which exporting symbols from a static executable does not give. Pippin keeps the static closure. (Chris, 2026-11-02) |
| EX-D2 | Python | **Embed the system's Python.** Linux links the distro's libpython3 and the .deb depends on `python3`. The mac app loads a user-installed Python 3 named in settings, so nothing is bundled or signed. Python is optional: without it, python packages list as *unavailable* and everything else works. (Chris, 2026-11-02) |
| EX-D3 | When changes apply | **Install and enable apply live; remove, disable and upgrade apply at the next launch.** The registry only ever grows while running. A loaded library is never unloaded, because an open overlay, editor or render may still hold its code. (Chris, 2026-11-02) |
| EX-D4 | First tools | **Overlay Manager and ENC Importer.** No external-command tool kind in v1. The importer later moved to the ENC options page (EX-D9). (Chris, 2026-11-02) |
| EX-D5 | Package file | **`.pgpkg`, a zip of one package directory.** A directory with the same layout installs too ("Install from Folder…", for developers, linked in place rather than copied). (Chris, 2026-11-02) |
| EX-D6 | Trust | **No package signing in v1.** Before installing, Peregrine shows the publisher, version and kinds of code ("native library", "Python"), and the user confirms that it runs with their permissions. On macOS the app needs the `com.apple.security.cs.disable-library-validation` entitlement. A downloaded dylib must be signed and notarized by its author, or Gatekeeper refuses it, and the Overlay Manager says so. (Chris, 2026-11-02) |
| EX-D7 | Ids | **Reverse-DNS package id; every overlay and tool id in the package starts with `<package id>.`** `fv.` and `peregrine.` are reserved. This settles collisions at install time rather than at click time. (Chris, 2026-11-02) |
| EX-D9 | ENC Importer placement | **Runs in-process, not a Tools menu entry.** Launched by "Import Zip…" / "Import Folder…" buttons on Map ▸ Options ▸ ENC (commands `enc.import_zip`, `enc.import_folder`), via a generic `OptionsAction` on `MapOptionsSource`/`OptionsPage`. No CLI and no out-of-process tool kind; EX-D4 stands. Coverage regeneration is process-agnostic (CatalogBuild uses its own SQLite connection), so an out-of-process importer stays possible later. (Chris, 2026-11-02) |
| EX-D10 | Adding and excluding tools | **A user-only `tools.json` beside user-settings.json** (`DefaultToolConfigPath`): `{"format":1,"exclude":[ids or "prefix.*"],"tools":[entries; id `user.*`, builtin implementation only]}`. Excluded tools' packages still load; only the `tools.run` command is not registered. No system-level file. (Chris, 2026-11-02) |

## 1. The package

### 1a. Layout

```
com.example.rangerings/              ← the directory a .pgpkg holds (its name is the id)
  peregrine-package.json             ← required
  lib/macos/librangerings.dylib      ← universal (arm64 + x86_64), or macos-arm64/…
  lib/linux-x86_64/librangerings.so
  lib/linux-aarch64/librangerings.so
  python/rangerings/__init__.py      ← on sys.path when the package loads
  resources/icons/rangerings.svg     ← paths in the manifest resolve against the package root
  resources/…                        ← symbols, style sheets, data: the package's own business
  README.md  LICENSE
```

### 1b. `peregrine-package.json`

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
      "icon": "resources/icons/rangerings.svg", "kind": "file", "display_order": 40,
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

- An entry in `overlays` is exactly today's overlay manifest entry, so the parser is reused.
  `implementation` is one of `{"builtin": name}`, `{"library": class name}`,
  `{"native": class name}` or `{"python": "module:Class"}`. A `library` or `native` name is
  looked up in the table the library's entry point returns. One package holds either C or
  native libraries, not both, since the two kinds load through different entry points.
- `libraries` keys are `macos`, `macos-arm64`, `macos-x86_64`, `linux-x86_64`,
  `linux-aarch64` and `windows-x86_64`, tried most specific first. A package with `library`
  entries and no library for this machine is *unavailable (platform)*.
- `requires.peregrine` is a minimum version. `requires.plugin_api` must equal the host's major
  version. `requires.python` is the minor version the package was written for. It is a warning
  if newer than the host's Python and a refusal if older than 3.10.
- Unknown top-level keys are ignored, so a newer package format can add keys without breaking
  older readers. A higher `format` is refused with "needs a newer Peregrine".

### 1c. Where packages live

| Location | macOS | Linux | Can the user remove it? |
|---|---|---|---|
| System | `Peregrine.app/Contents/Resources/packages/` | `/usr/share/peregrine/packages/` | No; disable only |
| User | `~/Library/Application Support/Peregrine/packages/` | `$XDG_DATA_HOME/peregrine/packages/` | Yes |
| Linked (developer) | anywhere; path listed in the state file's `linked` | same | Unlink only; files untouched |

The state lives in `<user packages>/.state.json`: `disabled` (ids), `linked` (directories) and
`pending_removal` (directories), applied at the next launch before anything loads. Upgrades
wait in `.pending/<id>`, and an install in progress uses `.staging/`. As built in EX1, the
state file replaced the `user-settings.json` keys first planned: it is written at every change,
whereas user settings are saved only at quit, and a pending removal must survive a crash.
The loose-manifest directory `overlays/` keeps working for `builtin` entries, so DK3's tests and
any existing user files are unaffected.

If one id appears at two locations, the order of precedence is linked, then user, then system.
The one that loses is listed as *shadowed*, not loaded.

### 1d. Install, upgrade, remove

- **Install**: unzip into `packages/.staging/<id>`, validate, then rename into place.
  Validation covers: the manifest parses, its format and API versions are supported, every path
  stays inside the package (no `..`, no absolute paths, no symlinks out), the directory name
  equals the id, every contributed id has the package's id as prefix, and no extension clashes
  with a loaded type (the first registrant wins, as today). After the rename, the package's types
  and tools register live.
- **Upgrade** (same id is already installed): the new version is staged in
  `packages/.pending/<id>` and swapped in at the next launch.
- **Remove**: the package is marked in `packages.pending`. At the next launch, before loading,
  its directory is deleted, but only after checking that it is inside the user packages
  directory and holds a `peregrine-package.json`.
- An overlay of a removed type that is still named in a saved workspace opens as
  "type not installed", the same message a workspace gets today for an unknown type.

## 2. Compiled overlays

Which kind a developer picks:

| | `native` (C++) | `library` (C) | `python` |
|---|---|---|---|
| Access | Everything a built-in overlay has: `ICanvas`, `GeoDraw`, `LabelPlacer`, symbol libraries, every capability accessor (editors, search, context menu, snapping, routing overrides, edit target, item info), keyboard, all of fvkit's data | What the API version offers; API 1 is drawing, mouse, hit test, properties, open/save | pyfvw: full canvas and most of fvkit's data; capability hooks bound in EX5 |
| Survives a Peregrine release | **No**; rebuild against the new SDK | Yes, within a major version | Yes, unless pyfvw changes |
| Compiler | The one Peregrine was built with | Any, or any language with a C FFI | — |

### 2b. The `native` kind

- The plugin is ordinary C++ against the SDK headers (fvkit, DeskKit app layer). It registers
  `BuiltinOverlay`-shaped factories, an `fv::Overlay` and optionally an `app::OverlayEditor`,
  plus tools, under names the manifest refers to.
- Two `extern "C"` symbols. `fv_native_build_id()` returns the build id the plugin was compiled
  against, and the host compares it **before calling anything C++**. Only on a match does it
  call `fv_native_register(fv::desk::NativeRegistrar*)`.
- The build id is generated at Peregrine's configure time from its version, git commit,
  compiler id and version, standard library, and the platform. It is exported in the SDK's
  CMake package (`find_package(Peregrine CONFIG)` → `Peregrine::core`, compile definition
  `FV_SDK_BUILD_ID`). A mismatch makes the package *unavailable (built for Peregrine X; this
  is Y)* in the Overlay Manager.
- The plugin links `libperegrine_core` (EX-D8). Loading is `dlopen(RTLD_NOW | RTLD_LOCAL)`,
  never closed, as for `library`.
- Exceptions do not cross the boundary. The host wraps every call into a native overlay in the
  same `Status` handling it applies to built-ins.

### 2c. The C ABI — `port/sdk/include/peregrine/fv_plugin.h`

C99, UTF-8 strings, no C++ types. Every struct that crosses the boundary starts with
`uint32_t struct_size`, so later minor versions can append fields without breaking older plugins.

```c
#define FV_PLUGIN_API_MAJOR 1
#define FV_PLUGIN_API_MINOR 0

/* The one symbol the host looks up. Returns 0, or a nonzero fv_status if the
   plugin refuses this host (it reports why through host->log). */
FV_PLUGIN_EXPORT int fv_plugin_entry(const fv_host_api* host, fv_plugin_api* out);

typedef struct fv_plugin_api {
  uint32_t struct_size;
  uint32_t api_major, api_minor;           /* what the plugin was built against */
  const fv_overlay_class* overlays; size_t overlay_count;
  const fv_tool_class*    tools;    size_t tool_count;
} fv_plugin_api;

typedef struct fv_overlay_class {
  uint32_t struct_size;
  const char* name;                         /* matches implementation.library */
  void* (*create)(const fv_host_api*, const char* type_id);
  void  (*destroy)(void* self);
  int   (*draw)(void* self, const fv_view* view, const fv_draw_api* draw);
  /* Optional (null = capability absent; the host adapter's As*() returns null): */
  int   (*open)(void* self, const char* path);              /* file overlays */
  int   (*save)(void* self, const char* path);
  int   (*hit_test)(void* self, double x, double y, fv_hit* out);
  const char* (*properties_schema)(void* self);             /* JSON, app::Properties shape */
  const char* (*get_properties)(void* self);                /* JSON values */
  int   (*set_properties)(void* self, const char* json);
  int   (*on_mouse)(void* self, const fv_mouse_event* e);   /* nonzero = handled */
} fv_overlay_class;
```

- **Drawing** goes through `fv_draw_api`, a host function table over `GeoDraw` (G3). It offers
  polyline and polygon on geographic points, circle, ellipse and arc in metres, symbol, label,
  and pixel versions of label and symbol, plus `geo_to_screen` / `screen_to_geo`. `fv_view`
  carries the surface size, bounds, scale denominator (`ScaleDenominatorFor`, never `Scale()`),
  projection type and DPI scale. Styles are small POD structs. A plugin never sees an
  `ICanvas`, so the stable surface is the narrow one.
- **Threading**: `draw` runs on the render worker under the stack lock (K15). The plugin
  checks `draw->cancelled()` between features. Everything else runs on the UI thread. A plugin
  never calls the UI.
- **Memory**: the host owns what it passes, valid for the call. The plugin owns the strings it
  returns, valid until its next call on the same object.
- **Status**: `fv_status` mirrors `fv::Status` kinds. `host->set_error(msg)` attaches the
  message, which reaches the log and the shell's error report.
- **Host side**: a `LibraryOverlay : fv::Overlay` adapter, capabilities by accessor (contracts).
  Libraries load with `dlopen(RTLD_NOW | RTLD_LOCAL)` / `LoadLibraryExW` and are never
  closed (EX-D3).
- **Not in API 1**, in the order proposed for later minor versions, each an optional table:
  keyboard and item info; data services (elevation query, current position fix, settings
  read); context menu; search provider; editor (tool palette, mode, edit hints); snapping;
  routing overrides. A table is added when a plugin needs it, not ahead of demand.

**Tools** in a library: `fv_tool_class { name; int (*run)(const fv_tool_context*) }`. The
context (§4) is a function table as well.

## 3. Python

- **The bridge is a separate shared library**, `libperegrine_python`, linked against libpython.
  The app opens it at the first python package, so Peregrine starts and runs without Python.
  On macOS it is built against one Python minor version, found from `python.home` (settings)
  or the python.org and Homebrew framework locations. A version mismatch makes python packages
  *unavailable (Python 3.x not found)*. pybind11 has no stable-ABI build, so a fixed minor
  version is a real constraint.
- **The bridge embeds pyfvw** (`PYBIND11_EMBEDDED_MODULE`), so `import pyfvw` inside Peregrine
  is the app's own build. A python overlay is a `pyfvw.overlay.Overlay` subclass. pyfvw's
  trampoline already makes it an `fv::Overlay`, so no extra adapter is needed. The GIL is
  taken per call, including on the render worker.
- pyfvw's `Overlay` today overrides only `on_draw`, mouse and keys. EX5 binds the capability
  accessors (hit test, item info, properties, persistence, context menu, then editors) so that
  Python approaches the `native` kind's access.
- A package's `python_path` is prepended to `sys.path`. **v1 packages carry their pure-Python
  dependencies inside that directory**. `pip install --target` from the Overlay Manager is a
  later step.
- Tools: `"python": "module:function"`, called as `function(ctx)` with a `pyfvw.app.ToolContext`.

## 4. Tools and the Tools menu

- **`fv::desk::ToolRegistry`** (DeskKit): `ToolDesc { id, title, icon, description, submenu,
  run }`. Each tool becomes a command `tools.run.<id>`. Builtin tools register from code,
  like builtin overlays. Package tools come from the manifest's `tools` array.
- **Menu**: `DefaultMenuLayout()` gains `{"tools", "Tools", {Cmd("tools.overlay_manager"),
  Sep(), Expand("tools.run.")}}` after Overlay. Commands sharing a `submenu` group as they do
  today. The menu is additive on both shells.
- **`ToolContext`**: the shell vocabulary a tool may use, a subset of `AppShell`. It covers:
  choose file or directory, show a form (an `app::Properties` schema rendered by the existing
  options-page generator, so no tool brings its own widgets), message, progress with cancel on a
  worker, open a file as an overlay, add a data directory to the catalog, and read the current
  view.
- Builtin tools whose windows need more than that (the Overlay Manager) have a DeskKit model,
  with a native window per shell, as the options dialog does.

## 5. The Overlay Manager (Tools ▸ Overlay Manager…)

**Model** (`fv_desk_packages.*`, headless, gtest-covered): `PackageManager` scans the three
locations, applies `packages.pending`, and lists
`PackageInfo { id, name, version, publisher, location, contributions, state }`. The state is
one of: loaded, disabled, pending removal, pending upgrade, shadowed, failed (message), or
unavailable (platform / API / Python). Operations: `Install(path)` (answers whether the change
applied live), `InstallLinked(dir)`, `SetEnabled`, `Remove`, `Reveal`.

**Window**: a table of packages (name, version, location, state badge) and a detail pane
(description, publisher, licence, contributed overlays and tools, failure text). The buttons are
Install…, Install from Folder…, Enable/Disable, Remove and Show in Finder/Files. A
"Restart Peregrine to finish" banner shows while anything is pending. Double-clicking a
`.pgpkg` (a mac UTI, a Linux MIME type) opens the manager on the install confirmation.

## 6. The ENC Importer (Map ▸ Options ▸ ENC: Import Zip… / Import Folder…, EX-D9)

Input: an `ENC_ROOT` directory, a zip (NOAA's downloads), or several. Output: the cells and
their updates are copied into the charts directory (`enc.charts_dir`; as planned, the ENC data directory), `BuildEncIndex` /
`WriteEncIndex` and the names table are rebuilt, and the catalog is rescanned. The work runs
on a worker with progress and cancel. The result reports cells added, updates applied and cells
cancelled (CH10's notices). The model is `fv_desk_enc_import.*` over `port/Enc`
(`ZipReader::ExtractAll`, `fv_enc_index.h`). Whatever of CH10's import lives in PippinKit
moves into `port/Enc` so that Osprey and Peregrine share one importer.

## 7. Sessions

| | Session | Proof |
|---|---|---|
| EX1 | **Packages and tools, headless**: package manifest parser (reusing the overlay entry parser), three locations, pending/disabled state, install from dir and zip with validation, `PackageManager`, `ToolRegistry`, Tools menu, `builtin` tools. `ZipReader` moves from `Enc` to fvkit if DeskKit cannot link `Enc`. | gtests: a test package with a builtin overlay and tool installs from a zip and appears in menus live; path-escape, id-prefix and format-version refusals; removal applies at the next `PackageManager` construction and never leaves the packages directory |
| EX2 | **C ABI**: `fv_plugin.h`, `fv_draw_api` over `GeoDraw`, `LibraryOverlay`/`LibraryTool`, loader; `port/sdk/examples/rangerings-c` built by CMake into a `.pgpkg` | gtest dlopens the example, draws on a `CpuCanvas` and checks ink; hit test and properties round-trip; a plugin built for API 2 is refused with a message |
| EX3 | **Native kind**: `libperegrine_core` shared library (EX-D8) for the mac app and the CMake build, build id, `NativeRegistrar`, SDK CMake package; `port/sdk/examples/rangerings-cpp` with an editor (placing rings by click) to prove parity with built-ins | gtest loads the native example and drives its editor through `FakeDesk`; a plugin stamped with another build id is refused before any C++ call; full ctest still green with the shared core |
| EX4 | **Mac shell**: Tools menu, Overlay Manager window, `.pgpkg` UTI, entitlement (EX-D6) | the C and native examples install from Finder, draw, disable and are removed after a relaunch |
| EX5 | **ENC Importer**: DeskKit model and mac options-page buttons (planned: mac sheet); CH10 importer shared with Osprey | gtest imports the Charleston set from a zip and matches Osprey's index; mac: imported charts draw in the ENC group |
| EX6 | **Python**: `libperegrine_python`, embedded pyfvw, python overlay and tool kinds, capability accessors bound in pyfvw, `examples/rangerings-py` | gtest (skipped without Python) draws the python example identically to the C one and hit-tests it; Peregrine without Python lists it as unavailable |
| EX7 | **Developer guide and tooling**: `port/sdk/README.md` (choosing a kind; writing a C, native and Python overlay; writing a tool), `port/sdk/PACKAGING.md` (the format in §1, per-platform build and signing, rebuilding native plugins per release), `port/tools/pgpkg.py` (`validate`, `build`), `port/sdk/` added to the Peregrine sync | a third party can build all three examples from the public repo with only the SDK directory and an installed Peregrine |
| EX8 | **Linux**: brief in `port/linux-plan.md` for the Linux session — shared core in the build and .deb, GTK Tools menu, Overlay Manager, ENC Importer, `python3` dependency, system packages dir, `.pgpkg` MIME type | the Linux PR, back-ported here before the next sync |

**EX1 as built (2026-11-02):** `fv_desk_packages.*` (manifest, `PackageManager`), `fv_desk_tools.*`
(`ToolRegistry`, builtin tool table), Tools menu with `tools.overlay_manager` (disabled until a
shell overrides `DeskShell::ProvidesOverlayManager`) and `tools.run.<id>`, and `Desk::LoadPackages` /
`InstallPackage` / `SetPackageEnabled` / `RemovePackage`. `ZipReader` stays in `port/Enc`, which
DeskKit already links. The mac shell calls `LoadPackages` (EX4). Format reference:
`port/sdk/PACKAGING.md`.

**EX2 as built (2026-11-02):** `fv_desk_library_plugin.*` loads `library` packages (`sdk/include/peregrine/fv_plugin.h`, example `sdk/examples/rangerings-c`, packed into a .pgpkg by CMake; the example builds standalone with only `sdk/include`). Desk gained `shell()`. Deviations from §2c:
(a) properties cross as per-key text (`get_property`/`set_property`, `PropertyValue::ToString` form), not whole-JSON get/set; the schema stays JSON.
(b) `geo_to_screen`/`screen_to_geo` live on `fv_view`, not `fv_draw_api`; `hit_test` and `on_mouse` take the view.
(c) API 1 symbols are the built-in shapes only (`fv.circle` etc.).
(d) `fv_view.dpi_scale` is always 1 (`Overlay::OnDraw` is not told the display scale).
(e) API 1 tool context is `open_file` and `request_redraw` only; message/progress/forms wait for the ToolContext work.
(f) a handled mouse-down and a successful `set_property` mark a persistent plugin overlay dirty (API 1 has no dirty call).
(g) log level constants are `FV_LEVEL_*` (`FV_LOG_*` are fvkit macros).
(h) a library whose API major differs is refused after its entry point runs, and stays loaded (never closed).
The EX1 test expecting library packages "not supported" now expects a missing library file to fail.

**EX3 as built (2026-11-02):** `peregrine_core` is a shared library (`sdk/CMakeLists.txt`, mac and Linux, not iOS; all of fv_deskkit's static closure linked whole; `GeoTiff_defines.cpp` split into archive `fv_geotiff_names` and left out of the whole pass because its key tables duplicate ImageLib's `geotiff.inc`). Build id from `cmake/PeregrineBuildId.cmake` at configure time. Loader `fv_desk_native_plugin.*`, SDK header `sdk/include/peregrine/fv_native.h`, example `sdk/examples/rangerings-cpp` (also builds out of tree via `-DPeregrine_DIR=build/port/sdk`), test `sdk/test/native_plugin_test.cpp`. Deviations from §2b:
(a) the gate is a file scan for the `fv-native-build-id=` marker before `dlopen`, so a mismatched plugin runs no code at all, not even static initialisers; `fv_native_build_id` is still required and re-checked after load.
(b) native editor factories take a `NativeEditorContext` (the Desk's `EditorManager`), because `BuiltinOverlay`'s editor factory cannot reach the edited overlay; `PackageManager::SetEditorManager` is called by `Desk::LoadPackages`.
(c) the SDK CMake package is build-tree only (`build/port/sdk/PeregrineConfig.cmake`); the install-tree package waits for EX7.
(d) host calls into native overlays are not wrapped in `Status` handling, same as built-ins.
EX8 risk: the Linux shared core and examples were never built or tested on Linux; the dylib exports every symbol including third-party (zlib, expat), which can interpose with GTK's on ELF, so consider a version script. PeregrineGtk still links static.

**EX4 as built (2026-11-02):** `DeskHost` carries the package surface (`LoadPackages(system, user, version)`, `TakeOverlayManagerRequest` / `HostTick.overlay_manager_requested`, `PackageCount/At` with the text struct `HostPackage`, `PackagesRestartPending`, `InspectPackage`, `InstallPackage`/`LinkPackage` returning `HostInstallOutcome`, `SetPackageEnabled`, `RemovePackage`); `Desk::LinkPackage` added; the host shell's `ProvidesOverlayManager` is true once packages load. Mac: `OverlayManagerWindow.swift` (install confirmation names publisher and kinds of code, EX-D6; Gatekeeper hint on signing failures; restart banner); `MapWindowController` loads `Contents/Resources/packages` and the user dir (host version = CFBundleShortVersionString, 0.1); `AppDelegate` opens `.pgpkg` from Finder, queued until launch finishes. `Info.plist` exports UTI `org.peregrine.package`; `Peregrine.entitlements` has disable-library-validation. Test `DeskKit/test/host_packages_test.cpp` (3).
(a) `PackageManager::Remove` forgets the disabled choice when the last copy goes, so a reinstall loads enabled.
(b) both examples now require peregrine 0.1 (1.0 made them unavailable in a 0.1 app).
(c) the rangerings-cpp plugin has an empty install rpath so `@rpath/libperegrine_core` binds to the host's core (vmmap: one core in the process).
Live run: both .pgpkg opened from Finder, confirmed, installed, Tools menu showed Overlay Manager and both submenus, rings drawn, disable survived relaunch, remove deleted at once (not loaded). Loaded-remove-relaunch is covered by gtests only. Untested: hardened runtime and notarization (entitlement and Gatekeeper path), an unsigned downloaded dylib.

**EX5 as built (2026-11-02):** `port/Enc/fv_enc_import.*` `ImportEncSet(source zip or folder, dest_dir, catalog_dir, result, progress)` (unzip or copy, chart-index.tsv, names table) is shared; `PPChartStore::Import` calls it and `ChartImportResult` aliases `fv::EncImportResult`. `fv_desk_enc_import.*`: `EncImportJob` (worker, progress/cancel), `EncSetName`, `DefaultChartsDir()` (`charts/` beside the user packages dir), setting `enc.charts_dir`. Sets go to `<charts>/enc/<set>`, staged in `<charts>/.staging/`; the same name replaces. `Desk::ImportEncCharts` adds the charts dir as a scan root (refused if inside another root); on finish `PollCatalogBuild` rescans only the enc step (no replace_existing). `Desk::Building()` covers both jobs, plus `JobFinished()`; DeskHost `JobFraction/JobText/Tick` handle the import. Options actions: `OptionsActionCount/At` on the host, mac `OptionsWindow` renders the buttons (applies pending changes first). Tool config (EX-D10): `ParseToolConfig/ReadToolConfig/ToolConfig::Excludes` in `fv_desk_packages`, `Desk::LoadToolConfig` / `DeskHost::LoadToolConfig`, called by `MapWindowController` after `LoadPackages`. Deviation from §6: imported charts go to the charts dir, not `enc.data_dir` (the S-52 library dir). Tests (14): Enc `EncImportTest` 4, DeskKit `EncImportTest` 7, PackagesTest tool config 2, DeskHost ENC page button 1; DeskTest `EveryCommandIsReachable` counts options actions as reachable. Not done: no live mac run of the import (charts drawing in the ENC group is covered by gtests only); Linux buttons belong to EX8; the "matches Osprey's index" proof is zip-vs-folder index equality plus the shared code path.

**EX6 as built (2026-11-03):** `libperegrine_python` (`sdk/python`, built only when `pybind11::embed` exists): pyfvw sources compiled with `PYFVW_EMBEDDED` (`pyfvw::DefineModule` shared by `PYBIND11_MODULE` and `PYBIND11_EMBEDDED_MODULE`), links peregrine_core + libpython. C entry `fv_python_bridge_open(python_home, error)`; the interpreter starts once with `install_signal_handlers=0`, the GIL is released for good, never finalised. Binds `pyfvw.app.ToolContext` (package_dir, open_file, request_redraw; invalid after the call).
`fv_desk_python_plugin.*`: `PythonBridge` interface (Version, AddPath, ResolveOverlay, ResolveTool), `SetPythonBridgeLocation(path, python_home)`, `DefaultPythonBridgePath()` (beside the binary holding DeskKit, via dladdr), `LoadPythonBridge` (dlopen RTLD_GLOBAL, once; failures remembered per path). PackageManager: `requires.python` < 3.10 is unavailable, newer than host is a warning; missing bridge is unavailable "Python support is not installed"; import error or non-Overlay class is failed with the Python error. `ToolContext::OpenFile/RequestRedraw` added; the C adapter uses them.
pyfvw: Python overlays gain the Properties capability (`properties_schema()` + `read_property`/`write_property`; C++ get/set_property validate type and range first; a successful set marks a file overlay dirty); `MapProjection.__copy__/__deepcopy__`; OSError from a persistence call maps to kIoError. Example `sdk/examples/rangerings-py` (com.example.pyrings, extension .prng, module `pyrings`) packed by CMake.
Tests: `sdk/test/python_plugin_test.cpp` (12, against the shared core): draws pixel-identical to rangerings-c including labels (host font), also after property changes; hit tests agree; draw on another thread; tool opens sample; without the bridge the package is unavailable while the C one loads; refusals (3.9, raising module, non-Overlay class). pytest `test_pyfvw_overlay_properties.py` (5).
Mac: the "Embed the Peregrine core" Xcode phase copies libperegrine_python into Contents/Frameworks when built, replaces its build rpath with @loader_path, signs it; the staleness check prunes `sdk/python` and `bindings`. Deviations: (a) the C plugin clamps an out-of-range ring_count; the Python trampoline refuses it (kInvalidArg) per the Properties contract. (b) `PyOverlay::FileOpen` does not record file_spec itself; the session flow does (the C adapter does both). (c) the example's labels are skipped when the canvas has no font, as rangerings-c ignores label failures. (d) `python.home` is not yet read from settings by any shell: `SetPythonBridgeLocation`'s home argument exists but `MapWindowController` does not pass it.
Open: no live mac run with a python package; the bridge links this machine's pyenv libpython 3.12 by absolute path, so a distributed app needs the python.org/Homebrew framework lookup from §3; Linux belongs to EX8.

EX1, EX2 and EX5 are headless and do not depend on each other's code. EX3 changes how both
apps link, so it lands before EX4 and before the Linux brief. EX6 needs EX2's adapter
conventions. The guide grows with each session; EX7 finishes it.

## Open questions

- The native SDK publishes the full fvkit header set (Chris, 2026-11-02).
- Windows: the ABI and layout cover it (`windows-x86_64`, `LoadLibraryExW`), but nothing is built
  or tested until there is a Windows shell.
- Whether python packages may later install dependencies with pip (needs network access and a
  per-package `site-packages`).
