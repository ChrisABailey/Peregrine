// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// pyfvw.app — the app layer (fv::app, port/fvkit-app-plan-COMPLETE.md) in Python (A6).
//
// A1-A5 were C++-only by design, and this is where every seam gets its first
// non-test consumer. Three shapes are worth reading before the code:
//
// 1. A SHELL IS A PYTHON CLASS. `AppShell` binds with a trampoline, so a tk (or
//    Qt, or curses) application subclasses it and answers the five decisions in
//    its own dialogs. Rule R1 says the core never opens one; from Python that
//    reads as "the core calls you back".
//
// 2. AN EDITOR IS DUCK-TYPED, and it is a PROXY rather than a trampoline. The
//    C++ side wants a `std::unique_ptr<OverlayEditor>` out of the factory, and
//    ownership of a Python-constructed object cannot be handed to C++ that way.
//    So `PyEditorProxy` holds the Python object and forwards the six calls by
//    name; anything with `activate`/`deactivate` is an editor. Where the API
//    hands an editor BACK (EditorManager.current_editor, AppShell.
//    on_editor_changed) the proxy is unwrapped again, so Python always sees the
//    object it created and never a wrapper.
//
// 3. A CAPABILITY IS A METHOD YOU DEFINED. C++ overlays return `this` from an
//    As*() accessor (R2); a Python overlay cannot, so the overlay trampoline
//    (pyfvw_module.cpp) implements every capability and reports the ones whose
//    methods the subclass actually defines. Defining `file_open` MAKES an
//    overlay persistent; defining `hit_test_point` makes it pickable. The
//    discovery is cached per instance, because it is asked on every event.
//
// Naming follows D5: snake_case, parameterless getters become properties, a
// non-ok Status raises pyfvw.FvError. FlowResult is NOT an exception — a cancel
// is the user's answer, not an error, and it has to be readable as a value.

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pyfvw_common.h"

#include "fvkit/app/capabilities.h"
#include "fvkit/app/editor.h"
#include "fvkit/app/pick.h"
#include "fvkit/app/search.h"
#include "fvkit/app/session.h"
#include "fvkit/app/shell.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/proj.h"
#include "fvkit/settings.h"

using namespace pybind11::literals;

namespace pyfvw {
namespace {

using fv::app::AppShell;
using fv::app::EditorManager;
using fv::app::EditorUiConstraints;
using fv::app::FileTypeDesc;
using fv::app::FlowResult;
using fv::app::HitItem;
using fv::app::HintText;
using fv::app::MenuNode;
using fv::app::OverlayEditor;
using fv::app::OverlaySession;
using fv::app::OverlayTypeDesc;
using fv::app::OverlayTypeRegistry;
using fv::app::PickPolicy;
using fv::app::PickSession;
using fv::app::SearchOrder;
using fv::app::SearchProvider;
using fv::app::SearchQuery;
using fv::app::SearchResult;
using fv::app::SearchSession;
using fv::app::SnapToItem;

// ---------------------------------------------------------------------------
// Editors: a proxy over a duck-typed Python object
// ---------------------------------------------------------------------------

class PyEditorProxy : public OverlayEditor {
 public:
  explicit PyEditorProxy(py::object obj) : obj_(std::move(obj)) {}
  ~PyEditorProxy() override {
    py::gil_scoped_acquire gil;
    obj_ = py::object();
  }

  const py::object& target() const { return obj_; }

  fv::Status Activate() override { return Call("activate"); }
  fv::Status Deactivate() override { return Call("deactivate"); }

  fv::app::CursorId DefaultCursor() const override {
    py::gil_scoped_acquire gil;
    if (!py::hasattr(obj_, "default_cursor")) return OverlayEditor::DefaultCursor();
    try {
      return obj_.attr("default_cursor")().cast<fv::app::CursorId>();
    } catch (py::error_already_set&) {
      PyErr_Clear();
      return OverlayEditor::DefaultCursor();
    }
  }

  EditorUiConstraints UiConstraints() const override {
    py::gil_scoped_acquire gil;
    if (!py::hasattr(obj_, "ui_constraints")) return {};
    try {
      return obj_.attr("ui_constraints")().cast<EditorUiConstraints>();
    } catch (py::error_already_set&) {
      PyErr_Clear();
      return {};
    }
  }

  bool AutoEnterOnCreate() const override {
    py::gil_scoped_acquire gil;
    if (!py::hasattr(obj_, "auto_enter_on_create")) return true;
    try {
      return obj_.attr("auto_enter_on_create")().cast<bool>();
    } catch (py::error_already_set&) {
      PyErr_Clear();
      return true;
    }
  }

  std::vector<MenuNode> Tools() const override {
    py::gil_scoped_acquire gil;
    if (!py::hasattr(obj_, "tools")) return {};
    try {
      return obj_.attr("tools")().cast<std::vector<MenuNode>>();
    } catch (py::error_already_set&) {
      PyErr_Clear();
      return {};
    }
  }

 private:
  // A raising activate() is a real failure the mode dance must see: the editor
  // does not enter and the shell is told. That is why this one does NOT
  // swallow the way the optional queries above do.
  fv::Status Call(const char* name) {
    py::gil_scoped_acquire gil;
    if (!py::hasattr(obj_, name)) return fv::Status::Ok();
    try {
      obj_.attr(name)();
      return fv::Status::Ok();
    } catch (py::error_already_set& e) {
      return fv::Status::Error(fv::kInternal,
                               std::string("python editor ") + name + ": " +
                                   e.what());
    }
  }

  py::object obj_;
};

// The Python object behind an editor, or the C++ editor itself when it is one.
py::object EditorToPython(OverlayEditor* editor) {
  if (editor == nullptr) return py::none();
  if (auto* proxy = dynamic_cast<PyEditorProxy*>(editor)) return proxy->target();
  return py::cast(editor, py::return_value_policy::reference);
}

// ---------------------------------------------------------------------------
// The shell
// ---------------------------------------------------------------------------

class PyAppShell : public AppShell {
 public:
  using AppShell::AppShell;

  SaveAnswer AskSave(const std::string& name) override {
    PYBIND11_OVERRIDE_PURE_NAME(SaveAnswer, AppShell, "ask_save", AskSave, name);
  }
  std::vector<std::string> ChooseFilesToOpen(const FileTypeDesc& t) override {
    PYBIND11_OVERRIDE_PURE_NAME(std::vector<std::string>, AppShell,
                                "choose_files_to_open", ChooseFilesToOpen, t);
  }
  std::pair<std::string, int> ChooseSaveSpec(
      const FileTypeDesc& t, const std::string& suggested) override {
    using Pair = std::pair<std::string, int>;
    PYBIND11_OVERRIDE_PURE_NAME(Pair, AppShell, "choose_save_spec",
                                ChooseSaveSpec, t, suggested);
  }
  std::optional<int> ChooseFromList(
      const std::string& title, const std::vector<std::string>& rows) override {
    using Opt = std::optional<int>;
    PYBIND11_OVERRIDE_PURE_NAME(Opt, AppShell, "choose_from_list",
                                ChooseFromList, title, rows);
  }
  bool ConfirmRevert(const std::string& spec) override {
    PYBIND11_OVERRIDE_PURE_NAME(bool, AppShell, "confirm_revert", ConfirmRevert,
                                spec);
  }
  void SetCursor(fv::app::CursorId c) override {
    PYBIND11_OVERRIDE_PURE_NAME(void, AppShell, "set_cursor", SetCursor, c);
  }
  void ShowHint(const HintText& h) override {
    PYBIND11_OVERRIDE_PURE_NAME(void, AppShell, "show_hint", ShowHint, h);
  }
  void ShowContextMenu(fv::PixelPoint at, const MenuNode& root) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "show_context_menu");
    if (!o) return;
    o(at.x, at.y, root);
  }
  void RequestInvalidate() override {
    PYBIND11_OVERRIDE_PURE_NAME(void, AppShell, "request_invalidate",
                                RequestInvalidate);
  }
  void OnEditorChanged(const fv::app::TypeId& id, OverlayEditor* editor) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "on_editor_changed");
    if (!o) return;
    o(id, EditorToPython(editor));
  }
  void ReportError(const fv::Status& s) override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "report_error");
    if (!o) return;
    o(s.code, s.message);
  }
};

// ---------------------------------------------------------------------------
// Stack observers
// ---------------------------------------------------------------------------

class PyStackObserver : public fv::StackObserver {
 public:
  using fv::StackObserver::StackObserver;

  void OverlayAdded(fv::Overlay& ov) override {
    PYBIND11_OVERRIDE_NAME(void, fv::StackObserver, "overlay_added",
                           OverlayAdded, ov);
  }
  void OverlayRemoved(fv::Overlay& ov) override {
    PYBIND11_OVERRIDE_NAME(void, fv::StackObserver, "overlay_removed",
                           OverlayRemoved, ov);
  }
  void OverlayOrderChanged() override {
    PYBIND11_OVERRIDE_NAME(void, fv::StackObserver, "order_changed",
                           OverlayOrderChanged);
  }
  void CurrentChanged(fv::Overlay* now, fv::Overlay* was) override {
    PYBIND11_OVERRIDE_NAME(void, fv::StackObserver, "current_changed",
                           CurrentChanged, now, was);
  }
  void OverlayDirtyChanged(fv::Overlay& ov) override {
    PYBIND11_OVERRIDE_NAME(void, fv::StackObserver, "dirty_changed",
                           OverlayDirtyChanged, ov);
  }
  void OverlayFileSpecChanged(fv::Overlay& ov) override {
    PYBIND11_OVERRIDE_NAME(void, fv::StackObserver, "file_spec_changed",
                           OverlayFileSpecChanged, ov);
  }
};

}  // namespace

// ---------------------------------------------------------------------------

void BindApp(py::module_& m) {
  py::module_ app = m.def_submodule(
      "app",
      "The application layer (fv::app): overlay types as data, the shell seam, "
      "the file flows, editors and picking. Subclass AppShell to be the UI the "
      "core calls back into; build an OverlayTypeDesc per overlay type; drive "
      "OverlaySession for File New/Open/Save/Close.");

  // --- value types --------------------------------------------------------

  py::enum_<FlowResult>(app, "FlowResult",
                        "The outcome of a cancelable flow. CANCELED is the "
                        "USER's answer, not an error: it propagates and aborts "
                        "whatever was being done on their behalf.")
      .value("DONE", FlowResult::kDone)
      .value("CANCELED", FlowResult::kCanceled)
      .value("FAILED", FlowResult::kFailed);
  app.def("flow_result_name",
          static_cast<const char* (*)(FlowResult)>(&fv::app::ToString),
          "result"_a);

  py::enum_<fv::app::CursorId>(app, "CursorId",
                               "What a click here would feel like; the shell "
                               "maps these to its own art.")
      .value("DEFAULT", fv::app::CursorId::kDefault)
      .value("CROSSHAIR", fv::app::CursorId::kCrosshair)
      .value("HAND", fv::app::CursorId::kHand)
      .value("MOVE", fv::app::CursorId::kMove)
      .value("NO", fv::app::CursorId::kNo)
      .value("WAIT", fv::app::CursorId::kWait);

  py::class_<HintText>(app, "HintText",
                       "The two strings a hover produces: a floating tool tip "
                       "and a status-bar line. Either may be empty.")
      .def(py::init([](std::string tip, std::string status) {
             return HintText{std::move(tip), std::move(status)};
           }),
           "tool_tip"_a = "", "status"_a = "")
      .def_readwrite("tool_tip", &HintText::tool_tip)
      .def_readwrite("status", &HintText::status)
      .def_property_readonly("empty", &HintText::empty)
      .def("__repr__", [](const HintText& h) {
        return "<HintText tool_tip='" + h.tool_tip + "' status='" + h.status +
               "'>";
      });

  py::class_<MenuNode>(app, "MenuNode",
                       "One node of a menu tree — data, not a widget. An empty "
                       "label with no children is a separator; `action` is any "
                       "callable, `children` a list of MenuNode.")
      .def(py::init([](std::string label, py::object action, std::string icon,
                       bool enabled, bool checked,
                       std::vector<MenuNode> children) {
             MenuNode n;
             n.label = std::move(label);
             n.icon = std::move(icon);
             n.enabled = enabled;
             n.checked = checked;
             n.children = std::move(children);
             if (!action.is_none()) {
               // The callable is held by the std::function, so a lambda passed
               // inline survives the call that built the menu.
               n.action = [action]() {
                 py::gil_scoped_acquire gil;
                 try {
                   action();
                 } catch (py::error_already_set&) {
                   PyErr_Clear();  // a menu action must not unwind the core
                 }
               };
             }
             return n;
           }),
           "label"_a = "", "action"_a = py::none(), "icon"_a = "",
           "enabled"_a = true, "checked"_a = false,
           "children"_a = std::vector<MenuNode>())
      .def_readwrite("label", &MenuNode::label)
      .def_readwrite("icon", &MenuNode::icon)
      .def_readwrite("enabled", &MenuNode::enabled)
      .def_readwrite("checked", &MenuNode::checked)
      .def_readwrite("children", &MenuNode::children)
      .def_property_readonly("is_separator", &MenuNode::is_separator)
      .def_property_readonly(
          "has_action", [](const MenuNode& n) { return n.action != nullptr; })
      .def("invoke",
           [](const MenuNode& n) {
             if (n.action) n.action();
           },
           "Run this node's action, if it has one.")
      .def("__repr__", [](const MenuNode& n) {
        return "<MenuNode '" + n.label + "' children=" +
               std::to_string(n.children.size()) + ">";
      });

  py::class_<FileTypeDesc>(
      app, "FileTypeDesc",
      "Present => the type is a FILE overlay (many instances, each a "
      "document); absent => STATIC (at most one, toggled). Filters are "
      "(description, pattern) pairs; the index into save_filters is the "
      "format_index a Save As carries through to the overlay.")
      .def(py::init([](std::string ext, std::string dir,
                       std::vector<std::pair<std::string, std::string>> open_f,
                       std::vector<std::pair<std::string, std::string>> save_f) {
             FileTypeDesc d;
             d.default_extension = std::move(ext);
             d.default_directory = std::move(dir);
             d.open_filters = std::move(open_f);
             d.save_filters = std::move(save_f);
             if (d.save_filters.empty()) d.save_filters = d.open_filters;
             return d;
           }),
           "default_extension"_a = "", "default_directory"_a = "",
           "open_filters"_a = std::vector<std::pair<std::string, std::string>>(),
           "save_filters"_a = std::vector<std::pair<std::string, std::string>>())
      .def_readwrite("default_directory", &FileTypeDesc::default_directory)
      .def_readwrite("default_extension", &FileTypeDesc::default_extension)
      .def_readwrite("open_filters", &FileTypeDesc::open_filters)
      .def_readwrite("save_filters", &FileTypeDesc::save_filters);

  py::class_<EditorUiConstraints>(
      app, "EditorUiConstraints",
      "What the frame must stop offering while an editor is active. The core "
      "does not enforce these — it owns no controls; the shell greys its own.")
      .def(py::init([](bool rot, bool proj, bool north, bool reorder) {
             EditorUiConstraints c;
             c.disable_rotation = rot;
             c.disable_projection_change = proj;
             c.requires_north_up = north;
             c.disable_overlay_reorder = reorder;
             return c;
           }),
           "disable_rotation"_a = false, "disable_projection_change"_a = false,
           "requires_north_up"_a = false, "disable_overlay_reorder"_a = false)
      .def_readwrite("disable_rotation", &EditorUiConstraints::disable_rotation)
      .def_readwrite("disable_projection_change",
                     &EditorUiConstraints::disable_projection_change)
      .def_readwrite("requires_north_up", &EditorUiConstraints::requires_north_up)
      .def_readwrite("disable_overlay_reorder",
                     &EditorUiConstraints::disable_overlay_reorder);

  // --- pick items ---------------------------------------------------------

  py::class_<HitItem>(
      app, "HitItem",
      "One pickable thing under a point. An overlay's hit_test_point returns "
      "these WITHOUT setting `overlay` — the binding stamps it, so a Python "
      "overlay cannot mis-attribute its own hits.")
      .def(py::init([](uint64_t feature, double distance_px, HintText hint,
                       fv::app::CursorId cursor) {
             HitItem h;
             h.feature = feature;
             h.distance_px = distance_px;
             h.hint = std::move(hint);
             h.cursor = cursor;
             return h;
           }),
           "feature"_a = 0, "distance_px"_a = 0.0, "hint"_a = HintText{},
           "cursor"_a = fv::app::CursorId::kHand)
      // Borrowed: the stack owns the overlay, and this pointer is only good
      // for as long as the pick that produced it.
      .def_property_readonly(
          "overlay", [](const HitItem& h) { return h.overlay; },
          py::return_value_policy::reference)
      .def_readwrite("feature", &HitItem::feature)
      .def_readwrite("distance_px", &HitItem::distance_px)
      .def_readwrite("hint", &HitItem::hint)
      .def_readwrite("cursor", &HitItem::cursor)
      .def("__repr__", [](const HitItem& h) {
        return "<HitItem feature=" + std::to_string(h.feature) + " d=" +
               std::to_string(h.distance_px) + " '" + h.hint.tool_tip + "'>";
      });

  py::class_<SnapToItem>(app, "SnapToItem",
                         "A point the cursor could snap to. Unlike a hit, "
                         "these are collected from every overlay that answers "
                         "and the user is asked when more than one does.")
      .def(py::init([](fv::GeoPoint point, std::string description) {
             SnapToItem s;
             s.point = point;
             s.description = std::move(description);
             return s;
           }),
           "point"_a, "description"_a = "")
      .def_readwrite("point", &SnapToItem::point)
      .def_readwrite("description", &SnapToItem::description)
      .def_readwrite("distance_px", &SnapToItem::distance_px,
                     "Screen distance to the asked-about pixel, for "
                     "nearest-wins ranking. 0 = the overlay did not say.")
      .def_property_readonly(
          "overlay", [](const SnapToItem& s) { return s.overlay; },
          py::return_value_policy::reference);

  py::enum_<PickPolicy>(app, "PickPolicy",
                        "How several answers under one point become one.")
      .value("TOP_MOST", PickPolicy::kTopMost,
             "Stack order first: the thing drawn on top wins even if something "
             "below is nearer. What a mouse user expects.")
      .value("NEAREST", PickPolicy::kNearest,
             "Distance first, stack order as the tie-break. What a finger "
             "needs.")
      .value("ASK_WHEN_AMBIGUOUS", PickPolicy::kAskWhenAmbiguous,
             "TOP_MOST, except that more than one candidate is a question "
             "(AppShell.choose_from_list) rather than a ranking.");
  app.def("pick_policy_name",
          static_cast<const char* (*)(PickPolicy)>(&fv::app::ToString),
          "policy"_a);
  app.def("pick_row_text", &fv::app::PickRowText, "item"_a,
          "The chooser row for a hit: '<overlay>: <hint>'.");

  // --- the type registry --------------------------------------------------

  py::class_<OverlayTypeDesc>(
      app, "OverlayTypeDesc",
      "An overlay TYPE as data — identity, menu text, stacking, the file "
      "sub-descriptor, the factory and the editor factory. Overlay instances "
      "carry only behaviour; everything user-facing consults the descriptor.\n\n"
      "`factory` is any callable returning an Overlay; `editor_factory` any "
      "callable returning an object with activate()/deactivate() (and "
      "optionally tools(), default_cursor(), ui_constraints(), "
      "auto_enter_on_create()). A type with no `file` is STATIC: at most one "
      "instance, toggled rather than opened.")
      .def(py::init([](std::string id, std::string display_name,
                       py::object factory, py::object editor_factory,
                       std::optional<FileTypeDesc> file,
                       std::string parent_display_name, std::string icon,
                       int default_display_order, bool is_top_most,
                       int default_opacity, bool user_controllable,
                       bool restore_at_startup) {
             OverlayTypeDesc d;
             d.id = std::move(id);
             d.display_name = std::move(display_name);
             d.parent_display_name = std::move(parent_display_name);
             d.icon = std::move(icon);
             d.default_display_order = default_display_order;
             d.is_top_most = is_top_most;
             d.default_opacity = default_opacity;
             d.user_controllable = user_controllable;
             d.restore_at_startup = restore_at_startup;
             d.file = std::move(file);
             if (!factory.is_none()) {
               d.factory = [factory]() -> std::shared_ptr<fv::Overlay> {
                 py::gil_scoped_acquire gil;
                 try {
                   return OverlayFromPython(factory());
                 } catch (py::error_already_set&) {
                   PyErr_Clear();
                   return nullptr;  // the session reports "factory made nothing"
                 }
               };
             }
             if (!editor_factory.is_none()) {
               d.editor_factory =
                   [editor_factory]() -> std::unique_ptr<OverlayEditor> {
                 py::gil_scoped_acquire gil;
                 try {
                   py::object made = editor_factory();
                   if (made.is_none()) return nullptr;
                   return std::unique_ptr<OverlayEditor>(
                       new PyEditorProxy(std::move(made)));
                 } catch (py::error_already_set&) {
                   PyErr_Clear();
                   return nullptr;
                 }
               };
             }
             return d;
           }),
           "id"_a, "display_name"_a = "", "factory"_a = py::none(),
           "editor_factory"_a = py::none(),
           "file"_a = std::optional<FileTypeDesc>(),
           "parent_display_name"_a = "", "icon"_a = "",
           "default_display_order"_a = 0, "is_top_most"_a = false,
           "default_opacity"_a = 100, "user_controllable"_a = true,
           "restore_at_startup"_a = false)
      .def_readonly("id", &OverlayTypeDesc::id)
      .def_readonly("display_name", &OverlayTypeDesc::display_name)
      .def_readonly("parent_display_name", &OverlayTypeDesc::parent_display_name)
      .def_readonly("icon", &OverlayTypeDesc::icon)
      .def_readonly("default_display_order",
                    &OverlayTypeDesc::default_display_order)
      .def_readonly("is_top_most", &OverlayTypeDesc::is_top_most)
      .def_readonly("default_opacity", &OverlayTypeDesc::default_opacity)
      .def_readonly("user_controllable", &OverlayTypeDesc::user_controllable)
      .def_readonly("restore_at_startup", &OverlayTypeDesc::restore_at_startup)
      .def_property_readonly(
          "file",
          [](const OverlayTypeDesc& d) { return d.file; },
          "The FileTypeDesc, or None for a static type.")
      .def_property_readonly(
          "is_file", [](const OverlayTypeDesc& d) { return d.file.has_value(); })
      .def_property_readonly(
          "has_editor",
          [](const OverlayTypeDesc& d) { return d.editor_factory != nullptr; })
      .def("__repr__", [](const OverlayTypeDesc& d) {
        return "<OverlayTypeDesc '" + d.id + "' " +
               (d.file ? "file" : "static") + ">";
      });

  py::class_<OverlayTypeRegistry>(
      app, "OverlayTypeRegistry",
      "What the user can open, and what goes in the menus. Hands out stable "
      "descriptors: a descriptor outlives every overlay made from it.")
      .def(py::init<>())
      .def("register",
           [](OverlayTypeRegistry& r, const OverlayTypeDesc& d) {
             ThrowIfError(r.Register(d));
           },
           "desc"_a,
           "Rejects an empty id, a duplicate id and a missing factory — a type "
           "that cannot be instantiated fails here rather than under the "
           "user's click.")
      .def("find", &OverlayTypeRegistry::Find, "type_id"_a,
           py::return_value_policy::reference_internal)
      .def("find_by_extension", &OverlayTypeRegistry::FindByExtension, "ext"_a,
           py::return_value_policy::reference_internal,
           "File-open dispatch. Case-insensitive, a leading dot is tolerated, "
           "and registration order breaks a tie.")
      .def("all", &OverlayTypeRegistry::All,
           py::return_value_policy::reference_internal)
      .def("with_editors", &OverlayTypeRegistry::WithEditors,
           py::return_value_policy::reference_internal,
           "The Tools-menu source: every type that has an editor.")
      .def("is_static", &OverlayTypeRegistry::IsStatic, "type_id"_a)
      .def("is_file", &OverlayTypeRegistry::IsFile, "type_id"_a)
      .def("__len__", &OverlayTypeRegistry::size);

  app.def("register_builtin_types",
          [](OverlayTypeRegistry& r) {
            ThrowIfError(fv::app::RegisterBuiltinOverlayTypes(r));
          },
          "registry"_a,
          "Registers the types fvkit itself provides: the lat/lon grid "
          "(static) and the point overlay (a file type reading .fvpoints "
          "SQLite documents).");
  // std::string, not the char array: both ids are declared as arrays of
  // UNKNOWN bound (`extern const char kGridTypeId[]`), and binding one to
  // attr()'s forwarding reference casts an incomplete type.
  app.attr("GRID_TYPE_ID") = std::string(fv::app::kGridTypeId);
  app.attr("POINTS_TYPE_ID") = std::string(fv::PointOverlay::kTypeId);

  // --- the shell ----------------------------------------------------------

  py::class_<AppShell, PyAppShell> shell(
      app, "AppShell",
      "The seam to a native UI (rule R1: the core never opens a dialog). "
      "Subclass and implement ask_save, choose_files_to_open, "
      "choose_save_spec, choose_from_list, confirm_revert, set_cursor, "
      "show_hint, show_context_menu(x, y, menu), request_invalidate, "
      "on_editor_changed(type_id, editor) and report_error(code, message).");
  py::enum_<AppShell::SaveAnswer>(shell, "SaveAnswer")
      .value("SAVE", AppShell::SaveAnswer::kSave)
      .value("DISCARD", AppShell::SaveAnswer::kDiscard)
      .value("CANCEL", AppShell::SaveAnswer::kCancel);
  shell.def(py::init<>());

  // --- the flows ----------------------------------------------------------

  py::class_<OverlaySession>(
      app, "OverlaySession",
      "The verb layer: New/Open/Save/Save As/Close/Exit and the saved "
      "configuration. Owns nothing — it composes the registry, the stack, the "
      "shell and Settings. Every verb returns a FlowResult.")
      .def(py::init<OverlayTypeRegistry&, fv::OverlayManager&, AppShell&,
                    fv::Settings&>(),
           "registry"_a, "manager"_a, "shell"_a, "settings"_a,
           py::keep_alive<1, 2>(), py::keep_alive<1, 3>(),
           py::keep_alive<1, 4>(), py::keep_alive<1, 5>())
      .def("set_editor_manager", &OverlaySession::SetEditorManager,
           "editors"_a.none(true), py::keep_alive<1, 2>(),
           "Optional, and must be wired in BOTH directions — the editor "
           "manager also needs set_session(this).")
      .def("toggle_static", &OverlaySession::ToggleStatic, "type_id"_a,
           "Static type only: open it if it is closed, close it if it is open.")
      .def("new_file_overlay", &OverlaySession::NewFileOverlay, "type_id"_a,
           "A fresh untitled document, made current, and (with an editor "
           "manager wired) entering that type's editor.")
      .def("open_file_overlays", &OverlaySession::OpenFileOverlays,
           "type_hint"_a = std::string(),
           "Asks the shell for files, then opens each. An empty hint offers "
           "every registered file type and dispatches by extension.")
      .def("open_file", &OverlaySession::OpenFile, "type_id"_a, "spec"_a,
           "Open one named file. An empty type_id dispatches by extension. "
           "An already-open (type, spec) is made current rather than doubled.")
      .def("save", &OverlaySession::Save, "overlay"_a)
      .def("save_as", &OverlaySession::SaveAs, "overlay"_a)
      .def("save_all", &OverlaySession::SaveAll)
      .def("close", &OverlaySession::Close, "overlay"_a)
      .def("close_all", &OverlaySession::CloseAll)
      .def("exit", &OverlaySession::Exit)
      .def("save_configuration",
           [](OverlaySession& s, const std::string& name) {
             ThrowIfError(s.SaveConfiguration(name));
           },
           "name"_a = "default",
           "Writes the stack into Settings under [session.<name>]. IN MEMORY: "
           "fv.Settings has no writer (rule S1 — the file is the user's).")
      .def("restore_configuration",
           [](OverlaySession& s, const std::string& name) {
             ThrowIfError(s.RestoreConfiguration(name));
           },
           "name"_a = "default")
      .def("restore_startup_overlays",
           [](OverlaySession& s) { ThrowIfError(s.RestoreStartupOverlays()); })
      .def_property_readonly(
          "last_error",
          [](const OverlaySession& s) -> py::object {
            if (s.last_error().ok()) return py::none();
            return py::str(s.last_error().message);
          },
          "The message behind the last FAILED, or None.")
      .def_property_readonly(
          "last_error_code",
          [](const OverlaySession& s) { return s.last_error().code; })
      .def_property_readonly("warnings", &OverlaySession::warnings)
      .def("clear_warnings", &OverlaySession::ClearWarnings);

  // --- editors ------------------------------------------------------------

  py::class_<OverlayEditor>(
      app, "OverlayEditor",
      "The C++ editor interface. Python editors do NOT subclass this — any "
      "object with activate() and deactivate() is an editor (see "
      "OverlayTypeDesc.editor_factory); this class exists so a C++ editor is "
      "still visible from Python.")
      .def("activate", [](OverlayEditor& e) { ThrowIfError(e.Activate()); })
      .def("deactivate", [](OverlayEditor& e) { ThrowIfError(e.Deactivate()); })
      .def_property_readonly("default_cursor", &OverlayEditor::DefaultCursor)
      .def_property_readonly("ui_constraints", &OverlayEditor::UiConstraints)
      .def_property_readonly("auto_enter_on_create",
                             &OverlayEditor::AutoEnterOnCreate)
      .def_property_readonly("tools", &OverlayEditor::Tools);

  py::class_<EditorManager>(
      app, "EditorManager",
      "The mode dance. set_mode(t) makes the current overlay match the mode "
      "(adopting the topmost of that type, or creating one through the "
      "session when the editor auto-enters); making a different overlay "
      "current makes the MODE match the overlay, which is observed rather "
      "than called, so it holds however the change was made.")
      .def(py::init<OverlayTypeRegistry&, fv::OverlayManager&, AppShell&>(),
           "registry"_a, "manager"_a, "shell"_a, py::keep_alive<1, 2>(),
           py::keep_alive<1, 3>(), py::keep_alive<1, 4>())
      .def("set_session", &EditorManager::SetSession, "session"_a.none(true),
           py::keep_alive<1, 2>(),
           "Optional: without a session, entering a mode with nothing of its "
           "type open simply WAITS instead of creating a document.")
      .def("set_mode", &EditorManager::SetMode, "type_id"_a,
           "An empty id leaves edit mode. Entering the mode already current is "
           "a no-op that reports DONE.")
      .def("toggle_editor", &EditorManager::ToggleEditor, "type_id"_a)
      .def_property_readonly("current_mode", &EditorManager::CurrentMode,
                             "Empty string = not editing.")
      .def_property_readonly(
          "current_editor",
          [](const EditorManager& m) { return EditorToPython(m.CurrentEditor()); },
          "The object your editor_factory returned, or None.")
      .def_property_readonly(
          "edited", [](const EditorManager& m) { return m.edited(); },
          py::return_value_policy::reference,
          "The overlay holding edit focus — null while a mode is active with "
          "nothing of its type open.")
      .def_property_readonly("active_constraints",
                             &EditorManager::ActiveConstraints)
      .def_property_readonly(
          "last_error",
          [](const EditorManager& m) -> py::object {
            if (m.last_error().ok()) return py::none();
            return py::str(m.last_error().message);
          });

  // --- picking ------------------------------------------------------------

  py::class_<PickSession>(
      app, "PickSession",
      "Hover, click deconfliction, snap-to and context-menu composition. Who "
      "is asked is the DRAW order reversed, so a tap agrees with the screen. "
      "Picking does not replace routing: route the event first and resolve "
      "here only when no overlay consumed it.")
      .def(py::init<fv::OverlayManager&, AppShell&>(), "manager"_a, "shell"_a,
           py::keep_alive<1, 2>(), py::keep_alive<1, 3>())
      .def("update_hover",
           [](PickSession& s, const fv::MapProjection& proj, int x, int y,
              PickPolicy policy) {
             s.UpdateHover(proj, fv::PixelPoint{x, y}, policy);
           },
           "proj"_a, "x"_a, "y"_a, "policy"_a = PickPolicy::kTopMost,
           "Tells the shell the winner's cursor and hint — ONLY when the hit "
           "changes, so a mouse move along one road does not rebuild a tooltip "
           "sixty times a second.")
      .def_property_readonly(
          "hovered",
          [](const PickSession& s) -> py::object {
            const HitItem* h = s.hovered();
            return h ? py::cast(*h) : py::none();
          })
      .def("clear_hover", &PickSession::ClearHover)
      .def("resolve_click",
           [](PickSession& s, const fv::MapProjection& proj, int x, int y,
              PickPolicy policy) -> py::object {
             auto hit = s.ResolveClick(proj, fv::PixelPoint{x, y}, policy);
             return hit ? py::cast(*hit) : py::none();
           },
           "proj"_a, "x"_a, "y"_a, "policy"_a = PickPolicy::kTopMost,
           "None = nothing there, or the user cancelled the chooser.")
      .def("hit_test_point",
           [](const PickSession& s, const fv::MapProjection& proj, int x, int y,
              PickPolicy policy) {
             return s.HitTestPoint(proj, fv::PixelPoint{x, y}, policy);
           },
           "proj"_a, "x"_a, "y"_a, "policy"_a = PickPolicy::kTopMost,
           "Every candidate, ranked, asking nothing.")
      .def("snap_to_point",
           [](PickSession& s, const fv::MapProjection& proj, int x, int y)
               -> py::object {
             auto item = s.SnapToPoint(proj, fv::PixelPoint{x, y});
             return item ? py::cast(*item) : py::none();
           },
           "proj"_a, "x"_a, "y"_a,
           "0 answers => None, 1 => it with no dialog, n => the chooser.")
      .def_static(
          "snap_candidates",
          [](const fv::OverlayManager& m, const fv::MapProjection& proj, int x,
             int y, double tolerance_px) {
            return fv::app::SnapCandidates(m, proj, fv::PixelPoint{x, y},
                                           tolerance_px);
          },
          "manager"_a, "proj"_a, "x"_a, "y"_a, "tolerance_px"_a = 8.0,
          "The same walk with no shell and no question: every candidate from "
          "every visible overlay that answers, ranked nearest first. What a "
          "shell with no chooser dialog uses.")
      .def("build_context_menu",
           [](const PickSession& s, const fv::MapProjection& proj, int x,
              int y) { return s.BuildContextMenu(proj, fv::PixelPoint{x, y}); },
           "proj"_a, "x"_a, "y"_a)
      .def("show_context_menu",
           [](PickSession& s, const fv::MapProjection& proj, int x, int y) {
             return s.ShowContextMenu(proj, fv::PixelPoint{x, y});
           },
           "proj"_a, "x"_a, "y"_a,
           "False (and the shell is NOT called) when no overlay contributed.")
      .def_readwrite("tolerance_px", &PickSession::tolerance_px,
                     "Device pixels, and the SHELL scales it: the core has no "
                     "business knowing a finger is wider than a pointer.");

  // --- searching ----------------------------------------------------------
  //
  // The second aggregating capability (S1-S4). Everything a caller needs is
  // three types and one call: what you are looking for, what came back, and a
  // session over the stack. There is no provider registry to bind because
  // there is no provider registry — an overlay that answers `as_search` is
  // discovered by the same walk that draws it.

  py::enum_<SearchOrder>(
      app, "SearchOrder",
      "How merged answers are ranked. NOT a query grammar: 'order by X' later "
      "is one more value here, which every UI already knows how to render.")
      .value("AUTO", SearchOrder::kAuto,
             "BEST_MATCH when the query has text, NEAREST when it does not.")
      .value("BEST_MATCH", SearchOrder::kBestMatch,
             "Match quality, then distance from `near`, then stack order.")
      .value("NEAREST", SearchOrder::kNearest,
             "Distance from `near` (or the centre of `area`), then stack "
             "order. With neither there is nothing to measure from and this "
             "degrades to stack order alone.");
  app.def("search_order_name",
          static_cast<const char* (*)(SearchOrder)>(&fv::app::ToString),
          "order"_a);

  py::class_<SearchQuery>(
      app, "SearchQuery",
      "Two independently optional filters — an area and some text — plus how "
      "to rank what comes back. `near` + `radius_m` is a circle: the SESSION "
      "folds it into a box before any provider sees it and cuts the exact "
      "circle afterwards, so a provider never implements one.")
      .def(py::init([](std::optional<fv::GeoRect> area,
                       std::optional<fv::GeoPoint> near, double radius_m,
                       std::string text, bool visible_only, size_t max_results,
                       SearchOrder order) {
             SearchQuery q;
             q.area = area;
             q.near = near;
             q.radius_m = radius_m;
             q.text = std::move(text);
             q.visible_only = visible_only;
             q.max_results = max_results;
             q.order = order;
             return q;
           }),
           "area"_a = py::none(), "near"_a = py::none(), "radius_m"_a = 0.0,
           "text"_a = std::string(), "visible_only"_a = false,
           "max_results"_a = 50, "order"_a = SearchOrder::kAuto)
      .def_readwrite("area", &SearchQuery::area, "The box, or None.")
      .def_readwrite("near", &SearchQuery::near,
                     "Distance origin. With radius_m it is also a cut; alone "
                     "it only orders.")
      .def_readwrite("radius_m", &SearchQuery::radius_m)
      .def_readwrite("text", &SearchQuery::text,
                     "Empty = spatial only. Case-insensitive token prefix: "
                     "'rud tur' finds Ruddy Turnstone.")
      .def_readwrite("visible_only", &SearchQuery::visible_only,
                     "False — the deliberate opposite of picking. 'Where is X' "
                     "is a fair question about a layer that is switched off.")
      .def_readwrite("max_results", &SearchQuery::max_results,
                     "The cap at BOTH ends: no provider appends more, and the "
                     "session returns at most this many after ranking. "
                     "0 = uncapped.")
      .def_readwrite("order", &SearchQuery::order)
      .def("__repr__", [](const SearchQuery& q) {
        return "<SearchQuery '" + q.text + "'" +
               (q.area ? " in area" : "") + (q.near ? " near" : "") + ">";
      });

  py::class_<SearchResult>(
      app, "SearchResult",
      "One answer. `title` is the provider's own label field — the caller "
      "never learns whether that was `name`, `OBJNAM` or a waypoint's own "
      "text — and `detail` is the one line that tells two rows of the same "
      "name apart. Provenance is flat, exactly like a HitItem's.")
      .def(py::init([](std::string title, std::string detail,
                       fv::GeoPoint position, std::optional<fv::GeoRect> bounds,
                       int match_quality, uint64_t feature) {
             SearchResult r;
             r.title = std::move(title);
             r.detail = std::move(detail);
             r.position = position;
             r.bounds = bounds ? *bounds : fv::GeoRect{position, position};
             r.match_quality = match_quality;
             r.feature = feature;
             return r;
           }),
           "title"_a = std::string(), "detail"_a = std::string(),
           "position"_a = fv::GeoPoint{}, "bounds"_a = py::none(),
           "match_quality"_a = 0, "feature"_a = 0,
           "bounds defaults to the degenerate box at `position`, which is the "
           "honest answer for a point.")
      .def_readwrite("title", &SearchResult::title)
      .def_readwrite("detail", &SearchResult::detail)
      .def_readwrite("position", &SearchResult::position,
                     "Where a label would sit — what distance ordering and "
                     "the radius cut measure to.")
      .def_readwrite("bounds", &SearchResult::bounds,
                     "What 'go there' frames. Degenerate for a point.")
      .def_readwrite("match_quality", &SearchResult::match_quality,
                     "0 exact, 1 whole-string prefix, 2 token match.")
      .def_readwrite("feature", &SearchResult::feature)
      // Borrowed: the stack owns the overlay.
      .def_property_readonly(
          "overlay", [](const SearchResult& r) { return r.overlay; },
          py::return_value_policy::reference)
      .def("__repr__", [](const SearchResult& r) {
        return "<SearchResult '" + r.title + "' (" + r.detail + ")>";
      });

  app.def("text_match_quality", &fv::app::TextMatchQuality, "query"_a,
          "candidate"_a,
          "-1 for no match, else 0 exact / 1 prefix / 2 token prefix. THE "
          "shared rule: a provider decides which string it matches, never "
          "what matching means.");
  app.def("search_distance_meters", &fv::app::SearchDistanceMeters, "a"_a,
          "b"_a,
          "The metre the session orders by, and the same one road snapping "
          "measures in — so a 500 m search and a 500 m snap agree.");

  // A cancel flag a Python thread can raise, because std::atomic<bool> is not
  // a thing Python has. It exists for the one async behaviour an incremental
  // search box needs: the next keystroke cancels the search in flight.
  py::class_<std::atomic<bool>, std::shared_ptr<std::atomic<bool>>>(
      app, "CancelFlag",
      "Pass one to SearchSession.search and set() it from another thread to "
      "cut a long search short. A cancelled search returns what it had, "
      "ranked — a caller that shows a partial answer gets a sensible one.")
      .def(py::init([] { return std::make_shared<std::atomic<bool>>(false); }))
      .def("set", [](std::atomic<bool>& f) { f.store(true); })
      .def("clear", [](std::atomic<bool>& f) { f.store(false); })
      .def_property_readonly("cancelled",
                             [](const std::atomic<bool>& f) { return f.load(); });

  py::class_<SearchSession>(
      app, "SearchSession",
      "The ONLY discovery path: walks the stack, asks everything that answers "
      "`as_search`, ranks the union. It holds no state about the stack — the "
      "walk is re-done per call — and unlike a PickSession it needs no shell, "
      "because a search asks the user nothing.")
      .def(py::init<const fv::OverlayManager&>(), "manager"_a,
           py::keep_alive<1, 2>())
      .def("search",
           [](const SearchSession& s, const SearchQuery& q,
              std::shared_ptr<std::atomic<bool>> cancel) {
             if (!cancel) {
               py::gil_scoped_release unlock;
               return s.Search(q);
             }
             py::gil_scoped_release unlock;
             return s.Search(q, *cancel);
           },
           "query"_a, "cancel"_a = nullptr,
           "Ranked results, best first. The GIL is released for the walk, so "
           "a search running on a worker thread does not freeze the UI — and "
           "a Python overlay's own `search` re-acquires it when its turn "
           "comes.");

  // --- stack observers ----------------------------------------------------

  py::class_<fv::StackObserver, PyStackObserver>(
      app, "StackObserver",
      "Subclass and override any of overlay_added, overlay_removed, "
      "order_changed, current_changed(now, was), dirty_changed, "
      "file_spec_changed. Do not add to or remove from the stack from inside "
      "a callback.")
      .def(py::init<>());
}

}  // namespace pyfvw
