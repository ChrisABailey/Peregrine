// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_options.h — the Overlay ▸ Options dialog as data
/// (port/desktop-plan.md §1c).
///
/// One page per registered, visible overlay type whose overlay declares
/// `app::Properties`; a page's sections are the specs' `group`s. A page edits
/// the type's settings (`SettingsPrefixForTypeId`), not one instance: values
/// are validated by a prototype overlay made from the type's factory, and
/// `Apply` writes them to the settings and pushes them into every open overlay
/// of the type. A native dialog binds to this and holds no state of its own.
#pragma once

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

class UserSettings;

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
  OptionsField* MutableField(const std::string& key);
  void MarkApplied();

  app::TypeId id_;
  std::string title_;
  std::string icon_;
  std::string prefix_;
  std::shared_ptr<Overlay> prototype_;
  std::vector<OptionsField> fields_;
  std::vector<OptionsSection> sections_;
};

class OptionsModel {
 public:
  /// Pages for every type in `types`, in registration order.
  OptionsModel(const app::OverlayTypeRegistry& types, const Settings& settings);

  const std::vector<std::unique_ptr<OptionsPage>>& pages() const { return pages_; }
  OptionsPage* Page(const app::TypeId& id);
  bool dirty() const;
  void Revert();

  /// Writes every changed value to `settings` (and to `user`, when non-null,
  /// so it persists) and sets it on each open overlay of the page's type,
  /// then marks the pages applied. An overlay that rejects a value keeps its
  /// own and is named in `warnings`.
  Status Apply(Settings& settings, OverlayManager& overlays, UserSettings* user,
               std::vector<std::string>* warnings = nullptr);

 private:
  std::vector<std::unique_ptr<OptionsPage>> pages_;
};

}  // namespace desk
}  // namespace fv
