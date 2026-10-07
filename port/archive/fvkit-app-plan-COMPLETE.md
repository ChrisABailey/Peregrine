# FvKit App Layer — platform-agnostic overlay/application framework (plan)

**Status: COMPLETE. A1–A6 all built (2026-08-12 → 2026-08-13); see their rows in
`port/PORTING-ARCHIVE.md`.** Nothing here is a to-do. Read this file only if a bug turns up in
`fv::app` and you want the design intent behind a component; the ledger's §1b is the summary and
the archive rows are the record of what was actually built and why.

Where the built code differs from what is written below, **the archive row wins**. The three that
matter: **there is no `stack.h`** — A2 grew `fvkit/overlay/manager.h` in place, as §3c says to, so
that file is the stack and `StackObserver` lives in it in namespace `fv`; `EditorManager`'s
dependency on `OverlaySession` is **MUTUAL**, so it is wired through `SetSession`/`SetEditorManager`
after construction and both sides are optional; and invariants §3d.2/§3d.3 are **observed** through
a private `StackObserver` rather than called, so they hold for a `MakeCurrent` or a `Remove` from
anywhere. A5's hit id is a **handle**, not the packing §3e assumes (a `FeatureRef` does not fit in
a uint64_t).

What this plan deliberately never covered, and is tracked in the ledger's §2b/§2c instead: the
reorder DIALOG, an editor for `fv.points`, session persistence (blocked on rule S1), and the
top-most band's opacity (blocked on `ICanvas`).

Companion docs: `port/fvkit-contracts.md` (D1–D6 still bind), `port/vpf-geosym-plan.md`,
ledger `port/PORTING.md`.

## 0. What this is

FVCore (fvkit L1–L5) can project, query, style, render and pick map data. What it cannot do is
*be an application*: there is no notion of an overlay **type**, no distinction between a static
overlay and a file overlay, no editor lifecycle, no save/close flow, no reorder model, and no
pick deconfliction when a click lands on three overlays at once. Every one of those exists in
`Applications/FalconView` — spread across `C_ovl_mgr` (6k lines), `C_overlay`'s interface set,
`OverlayTypeDescriptor`/`COverlayTypeDescriptorList`, `OvlFctry.h`'s editor interfaces and the
`OverlayEventRouter` — all welded to MFC, COM and the ribbon.

This plan extracts the **shape** of that machinery into a set of C++17 components under
`port/include/fvkit/app/` + `port/fvkit/app/`. It is explicitly **not a port**: no FalconView
file is transliterated. The goal is that someone who knows FalconView's overlay system feels at
home (same roles, same flows, recognizable names), and that a thin native shell — tk today,
AppKit/SwiftUI or Qt tomorrow, iOS eventually — can be written against it without the core
knowing which one it is talking to.

What already exists and is kept: `fv::Overlay` + `fv::OverlayManager`
(`fvkit/overlay/{overlay,manager}.h`) — the L4 SPI with draw bottom-up / route top-down,
`MouseEvent`/`KeyEvent`, shared_ptr ownership (D1), no exceptions across the SPI (D3), and the
pybind trampoline constraint that drove the single-base-class decision. The app layer **grows
around** these; it does not replace them.

## 1. The FalconView model, distilled

What the Windows app actually does, reduced to the decisions worth keeping:

1. **An overlay type is data, not code** (`OverlayTypeDescriptor`): identity GUID, display name,
   parent display name (menu hierarchy), icon, default stacking order, top-most flag + opacity,
   user-controllable flag, restore-at-startup flag, an optional *file* sub-descriptor (default
   directory, extension, open/save filters), an optional *editor*, and a factory. The manager
   consults descriptors for everything user-facing; overlay instances only carry behavior.
2. **Two kinds of overlay.** *Static* overlays are at-most-one-per-type and are **toggled**
   (`toggle_static_overlay`: exists → close, doesn't → create). *File* overlays are
   many-per-type, carry `IFvOverlayPersistence` (file spec, dirty, has-been-saved, read-only,
   FileNew/FileOpen/FileSaveAs, revert), and get dedup on open (opening an already-open file
   makes it current; a dirty duplicate offers revert).
3. **Capabilities are optional interfaces** discovered at the call site — render, UI events,
   routing overrides, context menu, persistence, snap-to, playback. An overlay implements what
   it wants; the manager `dynamic_cast`s and skips the rest.
4. **One current overlay, one active editor, and they chase each other.** `set_mode(guid)`
   activates the editor for a type; the manager then makes the topmost overlay of that type
   current (creating one if the editor demands it), or drops to no-edit. Making a different
   overlay current while an editor is on switches the editor (`SwitchToEditor` /
   `make_mode_match_current_overlay`). The overlay losing edit gets `release_edit_focus()`, the
   one gaining it `enter_edit_focus()`. Editors declare UI constraints (disable rotation,
   require north-up, disable reorder…) that the frame honors while they're active.
5. **Events route top-down with two escape hatches**: a pre-pass for overlays that claim
   *direct routing* (selection lock — a mid-gesture overlay must see the mouse first even if it
   isn't topmost), and a *declutter* mode where only the current overlay receives anything.
   First overlay to report handled stops the walk. Draw is the mirror image: bottom-up, then
   top-most-flagged overlays last.
6. **Pick has a "what would happen" half and a "do it" half.** `test_select` runs on every
   mouse-move and yields a cursor + hint (tooltip/status text) describing what a click would do;
   `select` performs it. Deconfliction between overlapping candidates is by stack order for
   clicks (first hit wins), and **by asking the user** when the semantics demand one of several
   (snap-to collects `SnapToInfo` from *all* overlays and pops a chooser when there's more than
   one). Reorder is user-visible and total (`reorder_overlay_list` takes the full permutation;
   `move_above`/`move_below`/`move_to_bottom` for increments).
7. **Lifecycle is cancelable.** Close asks the overlay first (`pre_close(&cancel)`), then
   prompts per dirty overlay (yes/no/cancel) through a dialog, and any cancel aborts the whole
   operation (including app exit). Save-all, save-as with format index, dirty-changed
   notifications to observers.
8. **The stack broadcasts.** `OverlayStackChangedObserver_Interface`: added, removed, order
   changed, file-spec changed, dirty changed. The overlay list UI, title bars, and clients all
   hang off this.

Everything else in `C_ovl_mgr` — ribbon building, HCURSOR, registry restore, CList plumbing,
COM marshaling, 3D catch-up timers — is Windows shell or era noise and is deliberately left
behind.

## 2. Design rules

- **R1 — The core never opens a dialog.** Every point where FalconView calls
  `AfxMessageBox`/`CFileDialog`/a chooser dialog becomes a call on an abstract **shell**
  interface the app supplies. The core states *what it needs decided*; the shell decides how to
  ask (modal dialog, sheet, toast, test fake).
- **R2 — One overlay base class, capabilities by virtual accessor.** FalconView's
  `dynamic_cast` discovery pattern is kept in spirit, but `dynamic_cast` across a pybind11
  trampoline is unreliable, and the L4 header already committed to a single base. So:
  `Overlay` grows `virtual Persistence* AsPersistence() { return nullptr; }` (etc.), and a
  capable overlay overrides to return `this`. Same optionality, one class hierarchy, binding-safe.
- **R3 — Descriptors are plain data + two std::functions** (factory, editor factory). No
  interface for "factory" alone; `std::function<std::shared_ptr<Overlay>()>` is the modern
  spelling of `IFvOverlayFactory`.
- **R4 — Identity is a string TypeId** (`"fv.grid"`, `"fv.route"`, `"user.drawing"`), not a
  GUID. Stable, diffable, readable in settings files. (D5 naming rules apply.)
- **R5 — Status, not HRESULT; no exceptions across the SPI** (D3). Cancelable flows return
  `FlowResult { kDone, kCanceled, kFailed }` rather than a bool-out-param.
- **R6 — The shell owns the event loop and all widgets.** The core is pure calls (same stance
  as `manager.h` today). Anything periodic (autosave) is a method the shell calls, not a timer.
- **R7 — Keep L4 source-compatible.** `PythonView.py` and existing pyfvw overlay users keep
  working; the app layer is additive (new headers, new bindings), with `OverlayManager`
  extended in place.

## 3. Components

File layout (all new unless noted):

```
port/include/fvkit/app/
  type_registry.h     OverlayTypeDesc, FileTypeDesc, OverlayTypeRegistry
  capabilities.h      Persistence, HitTest, SnapTo, ContextMenu, RoutingOverrides, EditTarget
  stack.h             OverlayStack (evolves fvkit/overlay/manager.h), StackObserver
  editor.h            OverlayEditor, EditorUiConstraints, EditorManager
  pick.h              HitItem, PickSession, deconfliction policies
  shell.h             AppShell (the UI delegate seam), MenuNode, HintText, CursorId
  session.h           open/close/save/create flows + configuration save/restore
port/fvkit/app/       implementations + tests in port/fvkit/app/test/
```

### 3a. `type_registry.h` — overlay types as data

```cpp
namespace fv::app {

using TypeId = std::string;                       // R4: "fv.grid", "user.drawing", ...

struct FileTypeDesc {                             // present ⇔ this is a file overlay type
  std::string default_directory;                  // may be empty = shell's document dir
  std::string default_extension;                  // "rte", no leading dot
  // Filter pairs, FalconView-style: {"Route Files (*.rte)", "*.rte"}, ...
  std::vector<std::pair<std::string, std::string>> open_filters, save_filters;
};

struct OverlayTypeDesc {
  TypeId id;
  std::string display_name;                       // empty ⇒ hidden from overlay lists/menus
  std::string parent_display_name;                // menu grouping, may be empty
  std::string icon;                               // symbolic name; the shell maps it to art
  int default_display_order = 0;                  // where a new instance lands in the stack
  bool is_top_most = false;                       // drawn above everything, e.g. crosshair/HUD
  int default_opacity = 100;                      // top-most blending only
  bool user_controllable = true;                  // user may close/hide it
  bool restore_at_startup = false;
  std::optional<FileTypeDesc> file;               // engaged ⇔ file overlay; absent ⇔ static
  std::function<std::shared_ptr<Overlay>()> factory;            // required
  std::function<std::unique_ptr<OverlayEditor>()> editor_factory; // null ⇒ no editor
};

class OverlayTypeRegistry {
 public:
  Status Register(OverlayTypeDesc desc);          // rejects duplicate id
  const OverlayTypeDesc* Find(const TypeId&) const;
  const OverlayTypeDesc* FindByExtension(const std::string& ext) const;  // file-open dispatch
  std::vector<const OverlayTypeDesc*> All() const;         // registration order
  std::vector<const OverlayTypeDesc*> WithEditors() const; // Tools menu source
  bool IsStatic(const TypeId& id) const;          // !file
  bool IsFile(const TypeId& id) const;            //  file
};

}  // namespace fv::app
```

Mirrors `OverlayTypeDescriptor` + `COverlayTypeDescriptorList` minus the Windows freight
(backing store enum, help ids, ribbon icons, COM custom initializers). Display-order
persistence goes through `fv::Settings` (`[overlay-order]` section), matching FalconView's
`Load/SaveDefaultDisplayOrder`.

`Overlay` gains a `TypeId type_id()` (set at creation by the session layer, like
`InternalInitialize(guid)` did) so the stack can answer `FirstOfType`/`OfType` queries.

### 3b. `capabilities.h` — optional interfaces, accessor-discovered (R2)

```cpp
namespace fv::app {

class Persistence {                       // ~ IFvOverlayPersistence
 public:
  virtual ~Persistence() = default;
  const std::string& file_spec() const;   // full path, empty until saved/opened
  bool is_dirty() const;  bool has_been_saved() const;  bool is_read_only() const;
  void set_dirty(bool);                   // fires StackObserver::OverlayDirtyChanged

  virtual Status FileNew() = 0;                          // fresh document; set default spec
  virtual Status FileOpen(const std::string& spec) = 0;
  virtual Status FileSaveAs(const std::string& spec, int format_index) = 0;
  virtual bool   SupportsRevert() const { return false; }
  virtual Status Revert(const std::string& spec) { return Status::Unimplemented(); }
};

struct HitItem {                          // one pickable thing under the cursor
  Overlay* overlay = nullptr;
  uint64_t feature = 0;                   // overlay-scoped id (PickIndex ids fit here)
  double distance_px = 0;                 // screen distance, for nearest-wins policies
  HintText hint;                          // tooltip + status line (see shell.h)
  CursorId cursor = CursorId::kDefault;   // what a click would feel like
};

class HitTest {                           // ~ test_select/selected, made explicit
 public:
  virtual ~HitTest() = default;
  // Everything within tolerance_px of p, best first. Called on hover AND click.
  virtual void HitTestPoint(const MapProjection&, Point p, double tolerance_px,
                            std::vector<HitItem>& out) = 0;
};

struct SnapToItem { GeoPoint point; std::string description; Overlay* overlay; };
class SnapTo {                            // ~ test_snap_to/do_snap_to
 public:
  virtual ~SnapTo() = default;
  virtual void SnapToPoint(const MapProjection&, Point p, double tolerance_px,
                           std::vector<SnapToItem>& out) = 0;
};

class ContextMenu {                       // ~ OverlayContextMenu_Interface
 public:
  virtual ~ContextMenu() = default;
  virtual void AppendMenuItems(const MapProjection&, Point p, MenuNode& menu) = 0;
};

class RoutingOverrides {                  // ~ OverlayUIEventRoutingOverrides (selection lock)
 public:
  virtual ~RoutingOverrides() = default;
  virtual bool WantsDirectRouting() const = 0;   // true while mid-gesture / region select
};

class EditTarget {                        // the overlay half of the editor contract
 public:
  virtual ~EditTarget() = default;
  virtual void EnterEditFocus() {}
  virtual void ReleaseEditFocus() {}
  virtual bool CanUndo() const { return false; }  virtual void Undo() {}
  virtual bool CanRedo() const { return false; }  virtual void Redo() {}
};

}  // namespace fv::app
```

And on `fv::Overlay` (L4 header, additive):

```cpp
virtual app::Persistence*      AsPersistence()      { return nullptr; }
virtual app::HitTest*          AsHitTest()          { return nullptr; }
virtual app::SnapTo*           AsSnapTo()           { return nullptr; }
virtual app::ContextMenu*      AsContextMenu()      { return nullptr; }
virtual app::RoutingOverrides* AsRoutingOverrides() { return nullptr; }
virtual app::EditTarget*       AsEditTarget()       { return nullptr; }
```

Playback/time (`PlaybackEventsObserver`, time segments), OLE drag-drop, the tabular editor and
the vertical view are real FalconView capabilities that are **out of scope here**; each is one
more accessor later, and the pattern shows exactly where it goes.

### 3c. `stack.h` — the stack, grown from today's `OverlayManager`

`fv::OverlayManager` keeps its name and file, and gains:

```cpp
class StackObserver {                     // ~ OverlayStackChangedObserver_Interface
 public:
  virtual void OverlayAdded(Overlay&) {}
  virtual void OverlayRemoved(Overlay&) {}        // fired after removal, before destruction
  virtual void OverlayOrderChanged() {}
  virtual void CurrentChanged(Overlay* now, Overlay* was) {}   // FalconView had this implicit
  virtual void OverlayDirtyChanged(Overlay&) {}
  virtual void OverlayFileSpecChanged(Overlay&) {}
  virtual ~StackObserver() = default;
};

// added to OverlayManager:
Overlay* current() const;                          // topmost by default; explicit make-current
Status MakeCurrent(const std::shared_ptr<Overlay>&);   // triggers editor-follow (EditorManager)
Status MoveAbove(const std::shared_ptr<Overlay>& move, const std::shared_ptr<Overlay>& anchor);
Status MoveBelow(...);  Status MoveToBottom(...);
Status Reorder(const std::vector<std::shared_ptr<Overlay>>& full_order);  // total permutation
std::shared_ptr<Overlay> FirstOfType(const TypeId&) const;
std::vector<std::shared_ptr<Overlay>> OfType(const TypeId&) const;
std::shared_ptr<Overlay> FindByFileSpec(const TypeId&, const std::string& spec) const;
void SetDeclutter(bool current_only);              // ~ show_other_overlays(FALSE)
void AddObserver(StackObserver*);  void RemoveObserver(StackObserver*);
```

Semantics carried over exactly:

- **Insertion by display order**: `Add` without an explicit position inserts where the type's
  `default_display_order` says, not blindly on top (`AddOverlayToStack`'s behavior).
- **Draw**: bottom-up over visible overlays, then the `is_top_most` set last with their
  opacity. Stops at first failing Status (existing rule).
- **Routing**: three-phase per event — (1) direct-routing pre-pass over overlays whose
  `RoutingOverrides::WantsDirectRouting()` is true, top-down; (2) if declutter is on, current
  overlay only; else (3) top-down over visible overlays until handled. This is `C_ovl_mgr::
  select`/`on_left_mouse_button_up` verbatim, minus the re-entry `active` flag (kept: a plain
  reentrancy guard) and the dynamic_casts (now accessor checks).
  **Built (A2), with one deviation from "verbatim"**: FalconView's second walk skips nobody, so a
  direct-routing overlay that *declined* the event was offered it again. Here an event reaches an
  overlay at most once. The pre-pass does ignore declutter, as the original does.
- **Drag capture**: when an overlay handles MouseDown it may call `manager.CaptureMouse(this)`;
  subsequent move/up events go only to it until `ReleaseMouse()` — this replaces FalconView's
  `m_drag`/`drag()`/`drop()`/`cancel_drag()` triad with the mechanism every modern toolkit uses,
  while keeping the guarantee those calls existed for (a gesture cannot be stolen mid-flight).
  Escape (routed as KeyDown) is the conventional cancel; the capturing overlay sees it first.

### 3d. `editor.h` — editors and the mode dance

```cpp
struct EditorUiConstraints {              // ~ IFvOverlayLimitUserInterface
  bool disable_rotation = false;
  bool disable_projection_change = false;
  bool requires_north_up = false;
  bool disable_overlay_reorder = false;
};

class OverlayEditor {                     // ~ IFvOverlayEditor, one instance per type
 public:
  virtual ~OverlayEditor() = default;
  virtual Status Activate() = 0;          // build tool state; shell shows its palette
  virtual Status Deactivate() = 0;
  virtual CursorId DefaultCursor() const { return CursorId::kCrosshair; }
  virtual EditorUiConstraints UiConstraints() const { return {}; }
  virtual bool AutoEnterOnCreate() const { return true; }  // ~ m_bAutoEnterOverlayEditor
  // Editor-owned tools, as data; the shell renders a toolbar/palette from these.
  virtual std::vector<MenuNode> Tools() const { return {}; }
};

class EditorManager {
 public:
  EditorManager(OverlayTypeRegistry&, OverlayManager&, AppShell&);
  // ~ C_ovl_mgr::set_mode / toggle_editor. Empty id ⇒ leave edit mode.
  FlowResult SetMode(const TypeId& id);
  FlowResult ToggleEditor(const TypeId& id);       // Tools-menu behavior
  const TypeId& CurrentMode() const;               // empty = no edit
  OverlayEditor* CurrentEditor() const;            // null = no edit
  EditorUiConstraints ActiveConstraints() const;   // union over current editor
};
```

The invariants FalconView enforces, kept as the `EditorManager`'s whole job:

1. `SetMode(t)`: deactivate old editor (after `ReleaseEditFocus` on the current overlay),
   activate new; then **make the current overlay match the mode** — topmost overlay of type `t`
   becomes current; if none exists and the editor auto-enters, create one (file type ⇒ the
   `FileNew` flow, static ⇒ toggle on); if the user cancels creation, mode falls back to none.
2. `MakeCurrent(o)` while an editor is active: **make the mode match the overlay** — switch to
   `o`'s type's editor if it has one, else leave edit mode. (`SwitchToEditor`.)
3. Closing the overlay being edited: current falls to the next of that type, or mode exits.
4. Focus bracketing is exact: `ReleaseEditFocus` fires before the switch, `EnterEditFocus`
   after, on the overlays that lose/gain being *the edited overlay* (not merely current).

The shell listens (`AppShell::OnEditorChanged(TypeId, OverlayEditor*)`) and shows/hides the
palette, applies `UiConstraints` to its map controls, checks/unchecks Tools-menu items.

### 3e. `pick.h` — hover, click and deconfliction

The one place this plan *reorganizes* FalconView rather than mirroring it. FalconView's
`test_select`/`selected` pair gives each overlay a veto in stack order — first hit wins, and
richer deconfliction (the snap-to chooser) is bolted on per feature. Here picking is a
first-class aggregation over `HitTest`, and *event routing remains available* for overlays that
want raw events (an editor mid-gesture). Both coexist: routing runs first (capture, direct
routing, editor gestures); if no overlay handles the click, the pick session does.

```cpp
enum class PickPolicy {
  kTopMost,       // stack order, then distance — FalconView click behavior
  kNearest,       // distance, then stack order — touch-friendly
  kAskWhenAmbiguous,  // if >1 within tolerance: shell chooser (snap-to dialog behavior)
};

class PickSession {
 public:
  PickSession(OverlayManager&, AppShell&);

  // Hover: aggregate HitTestPoint over visible overlays top-down, take the best item,
  // give the shell its cursor + hint. Cheap; called on every mouse move.
  //  ⇒ this is test_select: "what would a click here do"
  void UpdateHover(const MapProjection&, Point p);

  // Click resolution when routing didn't consume the event. Applies policy;
  // kAskWhenAmbiguous calls shell.ChooseFromList with the hints as rows.
  // Returns the winning item (or none if empty/canceled).
  std::optional<HitItem> ResolveClick(const MapProjection&, Point p, PickPolicy);

  // Snap-to: aggregate SnapTo over all overlays; 0 ⇒ nullopt, 1 ⇒ it,
  // n ⇒ shell chooser (exactly FalconView's snptodlg flow).
  std::optional<SnapToItem> SnapTo(const MapProjection&, Point p);

  double tolerance_px = 8.0;   // one knob; DPI-scaled by the shell
};
```

`HitItem.feature` deliberately fits the ids `fvkit/pick.h`'s `PickIndex` already emits, so a
vector overlay's `HitTestPoint` is a thin adapter over the L4 pick index it already builds.

Context menu composition follows the same aggregation shape: `BuildContextMenu(proj, p)` walks
visible overlays top-down, each `ContextMenu` capability appends into a `MenuNode` tree
(sections per overlay, FalconView-style), shell displays it and invokes the chosen node's
callback.

### 3f. `shell.h` — the seam to native UI (R1)

```cpp
enum class CursorId { kDefault, kCrosshair, kHand, kMove, kNo, kWait, /* grows */ };
struct HintText { std::string tool_tip, status; };   // ~ HintText (getobjpr.h)

struct MenuNode {                          // context menus, editor palettes, Tools menu
  std::string label;                       // empty ⇒ separator
  std::string icon;                        // symbolic, may be empty
  bool enabled = true, checked = false;
  std::function<void()> action;            // null ⇒ submenu-only node
  std::vector<MenuNode> children;
};

class AppShell {                           // implemented by the native shell (or a test fake)
 public:
  virtual ~AppShell() = default;

  // --- decisions (every former modal dialog) ---
  enum class SaveAnswer { kSave, kDiscard, kCancel };
  virtual SaveAnswer AskSave(const std::string& overlay_display_name) = 0;
  virtual std::vector<std::string> ChooseFilesToOpen(const FileTypeDesc&) = 0;   // multi-select
  // Returns path + which save_filters entry; empty path = cancel.
  virtual std::pair<std::string, int> ChooseSaveSpec(const FileTypeDesc&,
                                                     const std::string& suggested) = 0;
  virtual std::optional<int> ChooseFromList(const std::string& title,
                                            const std::vector<std::string>& rows) = 0;
  virtual bool ConfirmRevert(const std::string& file_spec) = 0;

  // --- presentation ---
  virtual void SetCursor(CursorId) = 0;
  virtual void ShowHint(const HintText&) = 0;      // tooltip + status bar
  virtual void ShowContextMenu(Point at, const MenuNode& root) = 0;
  virtual void RequestInvalidate() = 0;            // redraw scheduling stays shell-owned
  virtual void OnEditorChanged(const TypeId&, OverlayEditor*) = 0;
  virtual void ReportError(const Status&) = 0;
};
```

This is the complete inventory of UI the session flows need — nothing else in the app layer
touches a user. A `FakeShell` with scripted answers makes every flow unit-testable
(`FakeShell.next_save_answer = kCancel; expect close aborted`).

### 3g. `session.h` — the flows

`OverlaySession` composes registry + stack + editors + shell and implements the verbs FalconView
scatters across `C_ovl_mgr::{open,create,close,save,save_as,save_all,toggle_static_overlay,
OpenFileOverlay,exit}`:

```cpp
class OverlaySession {
 public:
  OverlaySession(OverlayTypeRegistry&, OverlayManager&, EditorManager&, AppShell&,
                 fv::Settings&);

  FlowResult ToggleStatic(const TypeId&);          // open⇄close, exact FalconView semantics
  FlowResult NewFileOverlay(const TypeId&);        // create + FileNew + auto-enter editor
  // Open with dedup: already open ⇒ MakeCurrent (+ ConfirmRevert if dirty on disk-reopen);
  // extension dispatch through registry when type not given.
  FlowResult OpenFileOverlays(const TypeId& hint /*may be empty*/);
  FlowResult OpenFile(const TypeId&, const std::string& spec);

  FlowResult Save(Overlay&);                       // unsaved ⇒ Save As flow
  FlowResult SaveAs(Overlay&);
  FlowResult SaveAll();
  FlowResult Close(Overlay&);      // EditTarget focus release → prompt if dirty → remove
  FlowResult CloseAll();           // one prompt per dirty overlay; any cancel aborts the rest
  FlowResult Exit();               // CloseAll + optional auto-save of configuration

  // ~ save_overlay_configuration/RestoreStartupOverlays, on fv::Settings:
  // [session] open overlay type ids + file specs + order + current + declutter.
  Status SaveConfiguration(const std::string& name);
  Status RestoreConfiguration(const std::string& name);
};
```

Flow details pinned now so they don't drift later:

- **Close** order: if overlay is being edited → `ReleaseEditFocus` + editor-follow (3d.3);
  `Persistence` + dirty → `AskSave` (kSave runs Save flow; a failed save aborts the close);
  then `Remove` (observers fire), then the shared_ptr drops.
- **Open dedup key** is `(TypeId, canonical file spec)` — `FindByFileSpec`. Case-handling of
  the canonical form is a per-platform detail the shell supplies (macOS/Linux differ; the
  Peregrine ledger already tracks Linux case-sensitivity as an open item).
- **`FileSaveAs` format index** = index into `save_filters`, 0 = default format. Same contract
  as FalconView's `nSaveFormat`, minus the Windows filter-string encoding.

## 4. What is deliberately not here

| FalconView feature | Why deferred | Seam when wanted |
|---|---|---|
| Playback / view time (gantt, time segments) | needs a clock model first | one more capability accessor + a `PlaybackClock` the shell drives |
| Vertical view, 3D draped draw | 2D canvas only today | second draw method on `Overlay`, as `OnDraw` is |
| Tabular editor | big, separable | `AsTabularData()` capability later |
| OLE drag-drop / clipboard | platform clipboard needed | shell methods + one capability |
| Overlay handles (int) for automation | pyfvw uses shared_ptr identity | a handle table in the bindings if a C ABI ever needs it |
| Ribbon/menu construction | shell's job by rule R1 | descriptors + `WithEditors()` are the data source |
| Multi-threaded overlay rendering / backing stores | the port renders through MapEngine; revisit with perf data, not speculatively | draw already returns Status; a per-overlay surface cache can slot into `DrawAll` |

## 5. Milestones (one session each, ledger-style)

| # | Session | Deliverable | Proof |
|---|---------|-------------|-------|
| **A1** ✅ | Types & capabilities | `type_registry.h`, `capabilities.h`, accessor hooks on `Overlay`; registry unit tests; grid overlay registered as the first *static* type | tests: register/dup-reject/by-extension; toggle grid via descriptor in a smoke test |
| **A2** ✅ | Stack v2 | observers, make-current, reorder ops, insertion-by-display-order, declutter, capture; routing three-phase | port `OverlayEventRouter_UnitTests` *ideas* (not code) as gtest; capture + direct-routing tests |
| **A3** ✅ | Shell seam + session flows | `shell.h`, `session.h`, `FakeShell`; save/close/open/dedup/revert/exit flows | every FlowResult path unit-tested against scripted FakeShell |
| **A4** ✅ | Editors | `editor.h`, `EditorManager`, the mode dance, focus bracketing, UI constraints | tests for invariants 3d.1–4; a trivial "points editor" test double |
| **A5** ✅ | Pick | `pick.h`, hover/hint, click policies, ambiguous chooser, snap-to; vector-overlay `HitTest` adapter over `PickIndex` | ambiguity tests (2 overlays, overlapping items, each policy); hover hint text |
| **A6** ✅ | PythonView adoption | pyfvw bindings for app layer (`pyfvw.app`); PythonView becomes an `AppShell` impl; route overlay becomes a file overlay with an editor; drawing types menu built from the registry | the app: File New/Open/Save on a route, editor toggle, right-click menu aggregated, reorder dialog, session restore |

A6 is the acceptance test for the whole plan: if PythonView's ad-hoc overlay code gets
*smaller* while gaining File-Overlay behavior, the abstractions are right. If it needs
scaffolding to satisfy the framework, they're wrong — revisit before writing a second shell.

**Verdict (2026-08-13, A6 landed).** It got smaller: the crosshair, the coverage box and the
graticule stopped being hand-constructed instances and became registered STATIC types (which is
also how the crosshair finally got into the top-most band), the route's own hit-test loop was
replaced by the `HitTest` capability that now also serves the hover and the right-click menu, and
the app gained New/Open/Save/Save As/Close, a save prompt on quit, an editor palette and an
aggregated context menu without hand-rolling any of them. **The scaffolding it DID need is worth
naming, because all of it is at the language seam and none of it is in the model**: a Python
overlay cannot return a C++ capability pointer (so the trampoline discovers capabilities from the
methods the subclass defines), a `unique_ptr` cannot be taken out of Python (so an editor is a
proxy over a duck-typed object), and a factory-made overlay needs an aliasing `shared_ptr` to
keep its Python half alive. A C++ shell needs none of the three. See the ledger's A6 archive row.

## 6. Risks / open questions

- **Editor palettes as `MenuNode`s** may be too weak for a real drawing editor (color wells,
  live coordinate readouts). Acceptable for A4–A6; a richer `ToolPanel` description or a
  shell-side custom panel keyed by TypeId are both compatible escapes.
- **`MakeCurrent`-triggers-editor-switch** surprised FalconView users occasionally; keeping it
  because familiarity is the brief, but it's one boolean (`follow_current`) if Chris wants it
  tamer.
- **Redraw granularity**: `RequestInvalidate()` is whole-view; FalconView's rect/region
  invalidation was central to its perf on 2005 hardware. MapEngine + retained VectorScene make
  full-frame redraw cheap today (R3c numbers), so region invalidation is *deliberately* omitted
  until a profile demands it — the ledger's own rule about perf work.
- **Reentrancy**: FalconView guarded `select` with a static `active` flag for a reason (dialogs
  pumped messages mid-flow). Shell dialogs here are synchronous calls from the core's
  perspective; the same guard goes on `OverlaySession`'s flows, and nested flow entry returns
  `kFailed` loudly rather than deadlocking quietly.
