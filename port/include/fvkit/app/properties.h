// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/app/properties.h — an overlay DECLARES what it can be told, and every
// shell builds the same dialog from that declaration.
//
// SHARED OVERLAY TOOLKIT (3 of 3, with scale_table.h and canvas/label_placer.h).
// If you are porting an overlay that has a property page in the Windows
// product, this is what you port it to. You do NOT port the property page.
//
// THE PROBLEM THIS SOLVES, stated once so nobody re-derives it. Every
// FalconView overlay carries an MFC CPropertyPage: grid_map's is 420 lines,
// and it is the SMALLEST one in the tree. Those pages are ~90% identical
// plumbing -- DDX a control to a member, write it to the registry, tell the
// overlay to invalidate -- around ~10% that is actually about the overlay. We
// have four shells already (PythonView, Pippin, fvrender, the tests) and none
// of them is MFC, so porting a page means writing the plumbing a fifth time
// per overlay per shell. Declaring the properties instead means:
//
//   * a shell walks Describe() and builds controls generically -- one dialog
//     builder, every overlay, forever;
//   * settings persistence is written ONCE (LoadFrom/SaveTo below) instead of
//     once per overlay, and the keys land in peregrine.ini rather than in a
//     Windows registry hive;
//   * a binding gets get/set by name for free, so pyfvw and a future scripting
//     layer need no per-overlay glue;
//   * a test can round-trip every property of every overlay without knowing
//     what any of them mean, which is the only way this stays honest.
//
// WHAT IS DELIBERATELY NOT HERE. Layout (tabs, order beyond `group`, control
// widths), validation beyond a range, and inter-property rules ("tick length
// is meaningless while ticks are off"). Those belong to the shell and to the
// overlay's own SetProperty respectively -- a schema that tried to express
// them would become a UI toolkit, which is the thing being deleted.

#ifndef FVKIT_APP_PROPERTIES_H_
#define FVKIT_APP_PROPERTIES_H_

#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"  // FvColor
#include "fvkit/geo.h"            // Status

namespace fv {

class Settings;

namespace app {

enum class PropertyType {
  kBool = 0,
  kInt,
  kDouble,
  kString,
  kColor,   // FvColor; a shell shows a colour well, a settings file "#RRGGBBAA"
  kChoice,  // one of PropertySpec::choices, carried as its INDEX
};

// One property's value. A tagged struct rather than std::variant: it crosses
// to pybind11 and to an ObjC++ facade, and both are simpler over a plain
// struct. Only the field matching `type` is meaningful.
struct PropertyValue {
  PropertyType type = PropertyType::kBool;
  bool b = false;
  long long i = 0;  // kInt and kChoice (the index)
  double d = 0.0;
  std::string s;
  FvColor color;

  static PropertyValue Bool(bool v);
  static PropertyValue Int(long long v);
  static PropertyValue Double(double v);
  static PropertyValue String(std::string v);
  static PropertyValue Color(FvColor v);
  static PropertyValue Choice(long long index);

  // "#RRGGBBAA" for kColor, "true"/"false" for kBool, the plain number or
  // string otherwise. This is the settings-file spelling, and it is stable:
  // an .ini written by one build must be readable by the next.
  std::string ToString() const;
  // Parses ToString()'s output back into `type`. Returns false and leaves the
  // value untouched on garbage, so a typo in a settings file keeps the
  // default rather than zeroing the property.
  bool FromString(const std::string& text);
};

struct PropertySpec {
  // Stable, lowercase, underscore-separated. It is an .ini key and a binding
  // name, so renaming one breaks a user's settings file -- treat it the way
  // fv_map_enums.h treats an ABI enum.
  std::string key;
  std::string label;  // what a dialog shows: "Line colour"
  std::string group;  // a tab or box: "Lines", "Labels". May be empty.
  PropertyType type = PropertyType::kBool;
  PropertyValue default_value;

  // kInt / kDouble only. min == max means unbounded. A shell uses these for a
  // spinner's limits; SetProperty is still responsible for its own clamping,
  // because a caller that is not a dialog can reach it.
  double min = 0.0;
  double max = 0.0;

  // kChoice only, in display order. The VALUE is the index into this.
  std::vector<std::string> choices;

  std::string help;  // tooltip; may be empty
};

// Where an overlay type's properties live in a settings file.
//
// The LAST dotted component of the TypeId, plus a dot: "fv.grid" -> "grid.",
// so its keys are the `[grid]` section of peregrine.ini. That rule was not
// invented here -- it is already what the shipped sample uses for `[points]`
// (fv.points) and `[movingmap]` (fv.movingmap), so an overlay's settings
// section is its type's own short name and always has been.
//
// OverlaySession applies this to EVERY overlay it creates (see Instantiate),
// which is what makes a declared property page settings-backed with no
// per-overlay wiring in any shell.
std::string SettingsPrefixForTypeId(const std::string& type_id);

// The capability. An overlay implements it and returns `this` from
// Overlay::AsProperties(), the same shape as every other capability (R2).
class Properties {
 public:
  virtual ~Properties() = default;

  // The schema. Must be stable for the lifetime of the overlay -- a shell
  // holds pointers into it while a dialog is open.
  virtual const std::vector<PropertySpec>& Describe() const = 0;

  // An unknown key is kNotFound, never a silent default: a shell asking for a
  // property that does not exist has a bug, and so does a settings file.
  virtual Status GetProperty(const std::string& key,
                             PropertyValue* out) const = 0;

  // A wrong-typed or out-of-range value is kInvalidArg and changes nothing.
  // An overlay that needs to repaint on a change does it here.
  virtual Status SetProperty(const std::string& key,
                             const PropertyValue& value) = 0;

  // --- written once, for everybody -----------------------------------------
  //
  // These are NOT virtual and should not be overridden: they are the payoff.
  // Each is defined over Describe/Get/Set alone, so an overlay gets settings
  // persistence and a reset button by declaring its properties and nothing
  // else.

  // Reads "<prefix><key>" out of `settings` for every declared property that
  // is present, leaving absent ones alone. A value that will not parse is
  // skipped and named in `warnings` (when non-null) rather than failing the
  // load -- one bad line in an .ini must not cost the user the other nine.
  Status LoadFrom(const Settings& settings, const std::string& prefix,
                  std::vector<std::string>* warnings = nullptr);

  // Writes every property to "<prefix><key>". `only_changed` skips properties
  // still at their default, which is what keeps a written peregrine.ini short
  // and diffable.
  Status SaveTo(Settings* settings, const std::string& prefix,
                bool only_changed = true) const;

  // Every property back to its declared default.
  Status ResetToDefaults();

  // Convenience for a caller that knows the type. A missing key or a type
  // mismatch yields `def` -- these are for call sites like a draw loop, where
  // a Status has nowhere to go.
  bool GetBool(const std::string& key, bool def) const;
  long long GetInt(const std::string& key, long long def) const;
  double GetDouble(const std::string& key, double def) const;
  std::string GetString(const std::string& key,
                        const std::string& def = std::string()) const;
  FvColor GetColor(const std::string& key, FvColor def) const;

  // The spec for one key, or nullptr. Used by the helpers above and by a
  // shell that wants one control rather than the whole page.
  const PropertySpec* FindSpec(const std::string& key) const;
};

}  // namespace app
}  // namespace fv

#endif  // FVKIT_APP_PROPERTIES_H_
