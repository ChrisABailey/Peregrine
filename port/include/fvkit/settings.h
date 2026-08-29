// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::Settings — the port's replacement for the Windows registry.
//
// FalconView keeps most of its preferences under HKEY_CURRENT_USER, read
// through FvConfigFileServer and a scattering of direct RegQueryValue calls.
// None of that is portable, and the port has so far severed it one module at a
// time with whatever was nearest: a constructor argument (geoid's data dir), an
// in-memory map (geo3's prefs), or an environment variable (FVW_GEODATA_DIR,
// FVW_FIF_DIR, MSPCCS_DATA). That was right for each module in isolation and is
// wrong as a whole — there is no one place a user can look, and nothing
// survives a restart.
//
// This is that one place: a hand-editable text file, read once at startup.
//
// FORMAT. INI, deliberately, not JSON:
//
//     # Comments start with # or ; and run to the end of the line.
//     [vector]
//     scene_margin    = 0.25
//     simplify_pixels = 0.5
//
//     [geosym]
//     data_dir = "/path/with spaces/and # a hash"   ; quotes keep both
//
// A `[section]` prefixes the keys under it, so the key above is
// `vector.scene_margin` — sections are a spelling convenience, not a tree. The
// whole store is flat `string -> string`, and typing happens at the Get, which
// is what keeps the parser to one screen and the file to something a person can
// edit without a schema in front of them. JSON was the alternative and lost on
// exactly one point: standard JSON has no comments, and a preferences file
// nobody can annotate is a preferences file nobody will edit.
//
// READ-ONLY, ON PURPOSE. There is no Save(). This file is authored by a human
// and the application never rewrites it — which is what preserves the comments,
// the ordering and the keys the app does not know about. Anything an app wants
// to persist that a user would NOT hand-edit (window geometry, last position)
// is application state and belongs somewhere else, not here.
//
// UNKNOWN KEYS ARE KEPT AND IGNORED. A key no code reads is not an error: it is
// how a file survives a downgrade, and how a module can be configured before
// the code that reads it exists. Only a malformed LINE is an error, and it
// fails the whole load with the line number, the same all-or-nothing contract
// the R2 rule-file parser has.

#ifndef FVKIT_SETTINGS_H_
#define FVKIT_SETTINGS_H_

#include <map>
#include <string>
#include <vector>

#include "fvkit/geo.h"  // Status

namespace fv {

class Settings {
 public:
  // Reads `path`. A missing file is kNotFound — the caller named it, so it
  // meant it. Replaces everything previously loaded.
  Status Load(const std::string& path);

  // Reads the first file in DefaultSettingsPaths() that exists. Finding
  // nothing is Ok with an empty path(): running with no settings file at all
  // is the normal case, not a failure.
  Status LoadDefault();

  // Parses `text` directly, for tests and for an embedded default.
  Status LoadFromString(const std::string& text,
                        const std::string& name = "<string>");

  // The file actually loaded; empty when none was.
  const std::string& path() const { return path_; }

  bool Has(const std::string& key) const;

  // Every getter takes the value to use when the key is absent, so a caller
  // states its own default at the point of use and there is no second table of
  // defaults to drift.
  std::string GetString(const std::string& key,
                        const std::string& def = std::string()) const;
  double GetDouble(const std::string& key, double def) const;
  int GetInt(const std::string& key, int def) const;
  // true/false, yes/no, on/off, 1/0 — case-insensitive.
  bool GetBool(const std::string& key, bool def) const;

  // In-memory override, for a command-line flag that should beat the file.
  void Set(const std::string& key, const std::string& value);

  // Every key present, sorted. Includes keys nothing reads.
  std::vector<std::string> Keys() const;

  // Values that were present but could not be read as the type asked for, in
  // the order they were asked for: "vector.scene_margin = wide is not a
  // number, using 0.25". A typo in a settings file must not fail silently and
  // must not abort startup either, so it does neither — the getter returns its
  // default and says so here, and an app surfaces this list once.
  const std::vector<std::string>& warnings() const { return warnings_; }
  void ClearWarnings() { warnings_.clear(); }

 private:
  void Warn(const std::string& key, const std::string& value,
            const char* wanted, const std::string& used) const;

  std::map<std::string, std::string> values_;
  std::string path_;
  mutable std::vector<std::string> warnings_;
};

// Where LoadDefault looks, in order, first existing file winning:
//
//   1. $FVW_SETTINGS                      — explicit override, and what the
//                                           tests and CI use. Named FVW_* to
//                                           match every other env var in the
//                                           tree (FVW_GEODATA_DIR, …).
//   2. ./peregrine.ini                    — the working directory, so a repo
//                                           or a data set can carry its own.
//   3. the user config dir:
//        POSIX   $XDG_CONFIG_HOME/peregrine/settings.ini, else
//                ~/.config/peregrine/settings.ini
//        macOS   also ~/Library/Application Support/Peregrine/settings.ini
//        Windows %APPDATA%\Peregrine\settings.ini
//
// Returned whether or not they exist, so a "where should I put this?" message
// can list them.
std::vector<std::string> DefaultSettingsPaths();

}  // namespace fv

#endif  // FVKIT_SETTINGS_H_
