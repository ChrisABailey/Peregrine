// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::FamilySet — named groups of vector features a user can switch off.
//
// THE PROBLEM. Every vector product this port draws already groups its own
// data, and no two of them group it the same way:
//
//   DNC   VPF COVERAGES, a directory each: `nav` holds the buoys, beacons and
//         lights; `hyd` holds the depth areas, contours and the bottom
//         characteristics. Aids to navigation and bottom characteristics are
//         genuinely in different places on disk, which is why "show me the
//         navaids without the seabed" is a natural request on DNC and has no
//         single-attribute answer.
//   ENC   S-57 OBJECT CLASSES, a six-letter acronym each (BOYLAT, DEPARE).
//         S-52 groups them for display with viewing groups, which the
//         delivered chartsymbols.xml does not carry — so the grouping has to
//         be stated somewhere, and this is that somewhere.
//   OSM   MVT SOURCE LAYERS, named by the tile schema (water, transportation,
//         building, poi...).
//
// THE MECHANISM, deliberately not a fourth filter. The R2 rule layer
// (fvkit/vector/rules.h) already hides features by layer, key, geometry,
// scale, group, category and attribute predicate, with one syntax across all
// three products and a compiled plan that costs nothing for the common case. A
// family is therefore just a NAME over a list of rule-file selectors:
//
//   { "name": "navaids", "enabled": true,
//     "select": ["layer=buoybcnp", "layer=lightsp"] }
//
// and switching it off emits `hide layer=buoybcnp` / `hide layer=lightsp` into
// the engine's RuleSet. Nothing new evaluates at draw time, the selectors are
// as expressive as a hand-written rule (including `where hdp > 10`), and a
// product the port has never seen can be grouped without a code change.
//
// WHY JSON when fv::Settings chose INI (S1). A family is a name plus a LIST,
// and a list is exactly what the flat INI key space cannot hold without
// inventing a separator convention. This file is also product data more than
// it is user preference — the shipped ones under port/families/ describe DNC,
// ENC and OSM as they are — so it sits beside port/Routing/rules/*.json rather
// than inside peregrine.ini, and the ini names the path:
//
//   [vector]
//   families_dnc = /path/to/dnc-families.json
//
// ON/OFF LIVES IN THE FILE. `enabled` is the switch; there is no GUI and none
// is needed. SetEnabled() exists for a host that grows one.
//
// ORDER MATTERS AND IT IS THE CALLER'S. Later rules win, so families load
// FIRST and a user rule file second — that way a rule file can `show` back
// something a family hid, which is the override direction that reads naturally
// ("hide the whole port-facilities family, but keep the pier I care about").

#ifndef FVKIT_VECTOR_FAMILIES_H_
#define FVKIT_VECTOR_FAMILIES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "fvkit/vector/rules.h"

namespace fv {

struct FeatureFamily {
  // Stable identifier, lower-case by convention. What a settings key or a
  // future menu refers to.
  std::string name;
  // What a person would call it ("Aids to Navigation").
  std::string title;
  // Where the grouping comes from, for whoever edits the file next
  // ("DNC coverage nav"). Never interpreted.
  std::string note;

  // false = every selector below is hidden.
  bool enabled = true;

  // Rule-file SELECTOR fragments, exactly as RuleSet::LoadText documents them
  // minus the leading action: `layer=hydline`, `key=BE010 geom=area`,
  // `group=27070`, `key=BH140 where hdp exists and hdp < 3`. Validated when
  // the file loads, so a typo is reported then and not when the family is
  // first switched off.
  std::vector<std::string> select;
};

class FamilySet {
 public:
  // All-or-nothing, like every other authored file in the tree (the R2 rule
  // parser, fv::Settings): on the first bad family nothing is loaded and
  // *error says which one and why. Replaces anything previously loaded.
  Status LoadJson(const std::string& text, std::string* error = nullptr);
  Status LoadFile(const std::string& path, std::string* error = nullptr);

  // The `product` string the file declares ("dnc", "enc", "osm"). Advisory:
  // nothing enforces that a DNC file is applied to a DNC engine, because the
  // selectors are product-neutral and a host that mixes them is not
  // necessarily wrong.
  const std::string& product() const { return product_; }
  const std::string& description() const { return description_; }
  // The file this came from; empty for LoadJson.
  const std::string& path() const { return path_; }

  const std::vector<FeatureFamily>& families() const { return families_; }
  const FeatureFamily* Find(const std::string& name) const;

  // An UNKNOWN name is enabled: a family nobody declared cannot be hiding
  // anything, and a host asking about one is not an error.
  bool Enabled(const std::string& name) const;
  // false = no such family (the caller misspelled it); nothing changes.
  bool SetEnabled(const std::string& name, bool on);
  size_t disabled_count() const;

  // Appends one `hide` rule per selector of every DISABLED family. An enabled
  // family emits NOTHING — it is not `show`, because a show would beat a hide
  // that came before it and families are not meant to fight each other.
  //
  // Cheap and repeatable: a host that toggles a family clears the RuleSet,
  // calls this, then reloads its user rule file.
  Status AppendRules(RuleSet* out, std::string* error = nullptr) const;

  // Bumps on every load and every SetEnabled that changed something, so a
  // host can tell whether its RuleSet is still current.
  uint64_t epoch() const { return epoch_; }

 private:
  std::string product_;
  std::string description_;
  std::string path_;
  std::vector<FeatureFamily> families_;
  uint64_t epoch_ = 1;
};

}  // namespace fv

#endif  // FVKIT_VECTOR_FAMILIES_H_
