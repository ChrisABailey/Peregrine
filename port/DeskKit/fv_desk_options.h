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

/// The options of one map group: a property set kept in the settings under
/// `prefix`, and what applying it does to the renderer.
struct MapOptionsSource {
  std::string group_id;
  std::string prefix;  ///< "dted."
  std::function<std::unique_ptr<app::Properties>()> make;
  /// Pushes the values into process-wide renderer state. Called on Apply and
  /// when settings load, from the UI thread.
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
  /// The page for `type` with values read from `settings`, or null when the
  /// type is hidden, has no factory, or its overlay declares no properties.
  static std::unique_ptr<OptionsPage> ForType(const app::OverlayTypeDesc& type,
                                              const Settings& settings);
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
  OptionsPage() = default;
  /// Loads `props` from `settings` under `prefix` and builds the fields.
  /// Null when `props` declares nothing.
  static std::unique_ptr<OptionsPage> Build(std::shared_ptr<app::Properties> props,
                                            const std::string& prefix,
                                            const Settings& settings);
  OptionsField* MutableField(const std::string& key);
  void MarkApplied();

  app::TypeId id_;
  std::string title_;
  std::string icon_;
  std::string prefix_;
  std::shared_ptr<app::Properties> prototype_;
  /// Map pages: what Apply calls. Overlay pages leave it empty.
  std::function<void(const app::Properties&)> apply_;
  std::vector<OptionsField> fields_;
  std::vector<OptionsSection> sections_;
};

class OptionsModel {
 public:
  /// Overlay options: pages for every type in `types`, in registration order.
  OptionsModel(const app::OverlayTypeRegistry& types, const Settings& settings);
  /// Map options: pages for every group in `groups` that has a source, in
  /// table order.
  OptionsModel(const MapGroups& groups, const std::vector<MapOptionsSource>& sources,
               const Settings& settings);

  const std::vector<std::unique_ptr<OptionsPage>>& pages() const { return pages_; }
  OptionsPage* Page(const app::TypeId& id);
  bool dirty() const;
  void Revert();

  /// Writes every changed value to `settings` (and to `user`, when non-null,
  /// so it persists), then marks the pages applied. An overlay page sets the
  /// values on each open overlay of its type; an overlay that rejects one
  /// keeps its own and is named in `warnings`. A map page calls its source's
  /// `apply`.
  Status Apply(Settings& settings, OverlayManager& overlays, UserSettings* user,
               std::vector<std::string>* warnings = nullptr);

 private:
  std::vector<std::unique_ptr<OptionsPage>> pages_;
};

}  // namespace desk
}  // namespace fv
