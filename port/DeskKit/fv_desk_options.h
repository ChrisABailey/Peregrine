// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_options.h — the Map ▸ Options and Overlay ▸ Options dialogs as data
/// (port/desktop-plan.md §1c).
///
/// A page is generated from an `app::Properties` schema; its sections are the
/// specs' `group`s. A page edits settings under a prefix, not one live object:
/// values are validated by a prototype `Properties` and `Apply` writes them to
/// the settings.
///
/// Overlay ▸ Options has one page per registered, visible overlay type whose
/// overlay declares properties; the prototype comes from the type's factory,
/// and applying pushes the values into every open overlay of the type.
/// Map ▸ Options has one page per map group with a `MapOptionsSource`;
/// applying calls the source's `apply`. A native dialog binds to this and
/// holds no state of its own.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/app/properties.h"
#include "fvkit/app/type_registry.h"

namespace fv {
class OverlayManager;
class Settings;

namespace desk {

class MapGroups;
class UserSettings;

/// A property set kept in the settings under `prefix`, and what applying it
/// does. It is either a map group's page or a section of an overlay type's
/// page, for settings the shell rather than each overlay acts on.
struct MapOptionsSource {
  std::string group_id;  ///< the map group, or the overlay type for an overlay page
  std::string prefix;  ///< "dted."
  /// The page title when `group_id` is not a map group (an app-wide page such
  /// as "Data"); empty for a map group's page, which takes the group's title.
  std::string title;
  std::function<std::unique_ptr<app::Properties>()> make;
  /// Pushes the values into process-wide state. Called on Apply (from the UI
  /// thread) and, for a map group, when settings load.
  std::function<void(const app::Properties&)> apply;
};

/// One control: its declaration, the value shown and the value last applied.
struct OptionsField {
  app::PropertySpec spec;
  app::PropertyValue value;
  app::PropertyValue applied;
  bool changed() const;
};

/// A titled run of fields; the title is the specs' `group` (may be empty).
struct OptionsSection {
  std::string title;
  std::vector<std::string> keys;
};

class OptionsPage {
 public:
  /// The page for `type` with values read from `settings`: the overlay's own
  /// properties, then each of `extra` whose `group_id` is the type. Null when
  /// the type is hidden, has no factory, or the page would have no fields.
  static std::unique_ptr<OptionsPage> ForType(const app::OverlayTypeDesc& type,
                                              const Settings& settings,
                                              const std::vector<MapOptionsSource>& extra = {});
  /// The page for a map group, titled with the group's title, or null when
  /// the source makes no properties or they declare none.
  static std::unique_ptr<OptionsPage> ForMapGroup(const std::string& title,
                                                  const MapOptionsSource& source,
                                                  const Settings& settings);

  const app::TypeId& id() const { return id_; }
  const std::string& title() const { return title_; }
  const std::string& icon() const { return icon_; }
  const std::vector<OptionsField>& fields() const { return fields_; }
  /// Sections in first-appearance order of their group.
  const std::vector<OptionsSection>& sections() const { return sections_; }
  const OptionsField* Field(const std::string& key) const;

  /// Sets the shown value. The overlay's own `SetProperty` decides: a rejected
  /// value changes nothing and its Status is returned; an accepted one is
  /// stored as the overlay reads it back (so its clamping shows).
  Status Set(const std::string& key, const app::PropertyValue& value);
  /// Every field back to its declared default (not yet applied).
  Status ResetToDefaults();
  /// Every field back to its applied value.
  void Revert();
  bool dirty() const;

  /// The changed fields as settings entries, `{"grid.line_color", "#FF0000FF"}`,
  /// spelled as `Properties::SaveTo` spells them (a choice by name).
  std::vector<std::pair<std::string, std::string>> PendingSettings() const;

 private:
  friend class OptionsModel;
  /// One property set on the page. A part without `apply` is the overlay's
  /// own: Apply pushes its changed values into the open overlays of the type.
  struct Part {
    std::string prefix;
    std::shared_ptr<app::Properties> prototype;
    std::function<void(const app::Properties&)> apply;
  };

  OptionsPage() = default;
  /// Loads `props` from `settings` under `prefix` and appends its fields. A
  /// key already on the page is skipped. False when `props` declares nothing.
  bool AddPart(std::shared_ptr<app::Properties> props, const std::string& prefix,
               std::function<void(const app::Properties&)> apply, const Settings& settings);
  OptionsField* MutableField(const std::string& key, size_t* part = nullptr);
  bool PartDirty(size_t part) const;
  void MarkApplied();

  app::TypeId id_;
  std::string title_;
  std::string icon_;
  std::vector<Part> parts_;
  std::vector<OptionsField> fields_;
  std::vector<size_t> field_part_;  ///< parallel to `fields_`
  std::vector<OptionsSection> sections_;
};

class OptionsModel {
 public:
  /// Overlay options: pages for every type in `types`, in registration order,
  /// each with the `extra` sources whose `group_id` is its type.
  OptionsModel(const app::OverlayTypeRegistry& types, const Settings& settings,
               const std::vector<MapOptionsSource>& extra = {});
  /// Map options: pages for every group in `groups` that has a source, in
  /// table order.
  OptionsModel(const MapGroups& groups, const std::vector<MapOptionsSource>& sources,
               const Settings& settings);

  const std::vector<std::unique_ptr<OptionsPage>>& pages() const { return pages_; }
  OptionsPage* Page(const app::TypeId& id);
  /// The page a dialog opens on, or -1 for no preference (the first page, or
  /// the one an already open dialog shows).
  int initial_page() const { return initial_page_; }
  /// Opens the dialog on the page of `id`; false when there is none.
  bool SelectPage(const app::TypeId& id);
  bool dirty() const;
  void Revert();

  /// Writes every changed value to `settings` (and to `user`, when non-null,
  /// so it persists), then marks the pages applied. An overlay page sets the
  /// values on each open overlay of its type; an overlay that rejects one
  /// keeps its own and is named in `warnings`. A source's part (a map page,
  /// or an extra section of an overlay page) calls its source's `apply`.
  Status Apply(Settings& settings, OverlayManager& overlays, UserSettings* user,
               std::vector<std::string>* warnings = nullptr);

 private:
  std::vector<std::unique_ptr<OptionsPage>> pages_;
  int initial_page_ = -1;
};

}  // namespace desk
}  // namespace fv
