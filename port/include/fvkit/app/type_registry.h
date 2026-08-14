// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/app/type_registry.h — overlay types as DATA (fvkit-app-plan.md §3a).
//
// FalconView's central insight, and the one worth keeping: an overlay type is
// a descriptor, not a class. `OverlayTypeDescriptor` carries identity, display
// name, menu grouping, icon, stacking order, the file sub-descriptor, the
// editor and the factory; `COverlayTypeDescriptorList` is the app's whole
// answer to "what can the user open, and what goes in the menus". Overlay
// INSTANCES carry only behaviour. Everything user-facing consults the
// descriptor.
//
// Left behind from the Windows original (shell freight, not model): backing
// store enum, help ids, ribbon icon resources, COM custom initializers,
// HCURSOR, the registry-backed restore.
//
// Two shape changes from the original, both stated in the plan:
//   * R3 -- the factory is a std::function, not an IFvOverlayFactory. There is
//     no interface for a thing with one method and no state.
//   * R4 -- identity is a string TypeId ("fv.grid"), not a GUID: stable,
//     diffable, and readable in a settings file, which matters because the
//     display order and the restored session are written to fv::Settings.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fvkit/geo.h"  // Status
#include "fvkit/overlay/overlay.h"

namespace fv {
namespace app {

class OverlayEditor;  // fvkit/app/editor.h (A4)

// R4. Convention: "<vendor>.<thing>" -- "fv.grid", "fv.route", "user.drawing".
using TypeId = std::string;

// Present <=> this is a FILE overlay type (many instances, each backed by a
// document). Absent <=> STATIC (at most one instance, toggled on and off).
// That one std::optional is the whole static/file distinction (plan §1.2).
struct FileTypeDesc {
  // Empty = the shell's own document directory.
  std::string default_directory;
  // No leading dot: "rte", not ".rte". Matching is case-insensitive and a
  // leading dot is tolerated on lookup, because a file spec has one.
  std::string default_extension;
  // FalconView-style filter pairs: {"Route Files (*.rte)", "*.rte"}.
  // The index into `save_filters` is the `format_index` a Save As carries
  // through to Persistence::FileSaveAs.
  std::vector<std::pair<std::string, std::string>> open_filters;
  std::vector<std::pair<std::string, std::string>> save_filters;
};

struct OverlayTypeDesc {
  TypeId id;
  // Empty display_name => hidden from overlay lists and menus. FalconView used
  // this for the types the user is not supposed to see; keeping the same rule
  // means a hidden type needs no separate flag.
  std::string display_name;
  std::string parent_display_name;  // menu grouping; may be empty
  std::string icon;                 // symbolic name; the shell maps it to art

  // Where a NEW instance lands in the stack -- insertion is by display order,
  // not blindly on top (~ AddOverlayToStack). Larger = nearer the top.
  int default_display_order = 0;
  bool is_top_most = false;      // drawn above everything: crosshair, HUD
  int default_opacity = 100;     // top-most blending only, 0..100
  bool user_controllable = true; // may the user close/hide it
  bool restore_at_startup = false;

  std::optional<FileTypeDesc> file;  // engaged => file overlay; absent => static

  // Required. Makes an instance; the session layer stamps type_id() on it.
  std::function<std::shared_ptr<Overlay>()> factory;
  // Null => this type has no editor and never enters edit mode. One editor
  // instance per TYPE (A4), which is why this is here and not on the overlay.
  std::function<std::unique_ptr<OverlayEditor>()> editor_factory;
};

// ~ COverlayTypeDescriptorList. Owns its descriptors and hands out stable
// pointers: a descriptor outlives every overlay made from it, and menus,
// stack rows and the session file all hold on to one.
class OverlayTypeRegistry {
 public:
  OverlayTypeRegistry() = default;
  OverlayTypeRegistry(const OverlayTypeRegistry&) = delete;
  OverlayTypeRegistry& operator=(const OverlayTypeRegistry&) = delete;

  // Rejects an empty id, a duplicate id, and a null factory. A type with no
  // factory cannot be instantiated and would fail later, at the point where
  // the user clicked something -- so it fails here instead.
  Status Register(OverlayTypeDesc desc);

  const OverlayTypeDesc* Find(const TypeId& id) const;

  // File-open dispatch: given "…/kiawah.rte", which type opens it. Case-
  // insensitive, and a leading dot on `ext` is tolerated. Registration order
  // breaks a tie between two types claiming one extension -- first wins, so a
  // plugin cannot silently steal a built-in type's files.
  const OverlayTypeDesc* FindByExtension(const std::string& ext) const;

  std::vector<const OverlayTypeDesc*> All() const;          // registration order
  std::vector<const OverlayTypeDesc*> WithEditors() const;  // Tools-menu source

  bool IsStatic(const TypeId& id) const;  // registered and !file
  bool IsFile(const TypeId& id) const;    // registered and  file

  size_t size() const { return order_.size(); }

 private:
  // unique_ptr, not vector<OverlayTypeDesc>: growth must not move a descriptor
  // somebody is already pointing at.
  std::vector<std::unique_ptr<OverlayTypeDesc>> order_;
  std::unordered_map<std::string, OverlayTypeDesc*> by_id_;
};

// ---------------------------------------------------------------------------
// Built-in types
// ---------------------------------------------------------------------------

// The lat/lon graticule: the first STATIC type, and the smallest complete
// example of the descriptor model -- no file sub-descriptor, no editor, drawn
// near the top of the stack, toggled rather than opened.
extern const char kGridTypeId[];  // "fv.grid"

// Registers every type fvkit itself provides. A shell calls this once and then
// adds its own. Fails if any built-in is already registered.
Status RegisterBuiltinOverlayTypes(OverlayTypeRegistry& registry);

}  // namespace app
}  // namespace fv
