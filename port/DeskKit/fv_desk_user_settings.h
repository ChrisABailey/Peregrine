// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_user_settings.h — the settings the application writes.
///
/// `fv::Settings` is the hand-authored peregrine.ini and is never rewritten
/// (rule S1). Choices made in the app (options dialogs, the last workspace)
/// live here instead: a flat key → string store in the same key spelling
/// (`grid.line_color`), saved as JSON in the user config directory.
/// `ApplyTo` lays these values over a loaded `fv::Settings`, so every
/// existing reader sees them; a value set here wins over peregrine.ini.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
class Settings;

namespace desk {

class UserSettings {
 public:
  /// Reads `path`. A missing file is Ok and leaves the store empty (the
  /// first run). A malformed file is an error and leaves the store as it was.
  Status Load(const std::string& path);
  /// Writes every value to `path` through a temporary file and a rename, so
  /// a crash mid-write keeps the previous file. Creates the directory.
  Status Save(const std::string& path) const;

  bool Has(const std::string& key) const { return values_.count(key) != 0; }
  std::string Get(const std::string& key, const std::string& def = std::string()) const;
  /// Sets a value; a change bumps Generation() and marks the store dirty.
  void Set(const std::string& key, const std::string& value);
  /// False when the key was not present.
  bool Erase(const std::string& key);
  /// Removes every key starting with `prefix`; returns how many.
  size_t ErasePrefix(const std::string& prefix);
  /// Sorted.
  std::vector<std::string> Keys() const;

  /// Copies every value into `settings` with `Settings::Set`.
  void ApplyTo(Settings& settings) const;
  /// Copies the `settings` keys starting with `prefix` in here.
  void CaptureFrom(const Settings& settings, const std::string& prefix);

  bool dirty() const { return dirty_; }
  uint64_t Generation() const { return generation_; }

 private:
  std::map<std::string, std::string> values_;
  mutable bool dirty_ = false;
  uint64_t generation_ = 0;
};

/// Where the user settings file lives: the user config directory of
/// `fv::DefaultSettingsPaths()`, file `user-settings.json`. Empty when no
/// home directory is known.
std::string DefaultUserSettingsPath();

}  // namespace desk
}  // namespace fv
