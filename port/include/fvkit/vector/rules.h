// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/vector/rules.h — the CROSS-PRODUCT rule layer (vpf-geosym plan §5.2).
//
//   IVectorSource -> [ rules ] -> IStyleEngine -> VectorRenderer -> ICanvas
//
// Every vector product this port cares about carries the same four knobs on
// top of "which symbol does this feature get" (plan §5.1):
//
//   * an attribute CONDITION      GeoSym ATTEXP | S-52 LUP attribute pairs |
//                                 MapLibre `filter`
//   * a SCALE window              S-52 SCAMIN   | MapLibre min/maxzoom |
//                                 (GeoSym has none in the table)
//   * a VIEWING GROUP toggle      GeoSym vgroup/txtgroup (IHO numbering) |
//                                 S-52 viewing groups | MapLibre layer class
//   * a DISPLAY CATEGORY          GeoSym dispcat | S-52 BASE/STANDARD/OTHER
//
// So they live here once, in product-neutral terms, rather than three times
// inside three style engines. A style engine consults a ResolvedPlan before it
// does any table work; a host application ALSO gets a rule file with one
// syntax across all three products, which is the authoring mechanism the plan
// asks for (product tables load first as the base layer, the user file second
// as the override layer — later rules win).
//
// THE PERFORMANCE POINT, which is the reason this is a plan and not just a
// predicate evaluator: rules are split into key-only rules and attribute
// rules, and a ResolvedPlan is compiled ONCE per (scale, settings epoch) and
// memoized per {layer, style_key}. For the overwhelmingly common case — a key
// whose applicable rules carry no predicate — a feature costs one hash lookup
// and ZERO predicate evaluations. `predicate_evaluations()` is exposed so a
// test can prove that rather than assume it.
//
// NOT bit-faithful by design, and it does not have to be: with an empty
// RuleSet and the default ViewingGroupSet, everything is visible with no
// effects, so a product engine retrofitted onto this renders exactly what it
// rendered before (the DNC golden hashes are unchanged). Thinning is opt-in.

#ifndef FVKIT_VECTOR_RULES_H_
#define FVKIT_VECTOR_RULES_H_

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "fvkit/vector/vector.h"

namespace fv {

// ---------------------------------------------------------------------------
// Predicate AST
// ---------------------------------------------------------------------------

enum class PredicateOp {
  kAlways = 0,  // no condition — the cheap case the plan optimizes for
  kNever,
  kExists,   // attribute present (its value may be empty)
  kMissing,  // attribute absent
  kEqual,
  kNotEqual,
  kLess,
  kLessEqual,
  kGreater,
  kGreaterEqual,
  kIn,     // value is one of values[]
  kNotIn,  // value is present and none of values[]
  kAnd,
  kOr,
  kNot,
};

// How a comparison resolves is worth stating once: if BOTH the stored value
// and the rule's value parse completely as numbers, the comparison is
// numeric; otherwise it is a byte-wise string comparison. That is what makes
// `hdp > 10` behave for VPF integer columns and `nam = "Nantucket"` behave for
// text, without the rule author having to declare types the product tables do
// not agree on anyway. A MISSING attribute makes every comparison false
// (kMissing/kNotExists is how you ask about absence).
struct Predicate {
  PredicateOp op = PredicateOp::kAlways;
  std::string attribute;
  std::vector<std::string> values;
  std::vector<Predicate> children;

  static Predicate Always() { return Predicate{}; }
  static Predicate Never() { Predicate p; p.op = PredicateOp::kNever; return p; }
  static Predicate Exists(std::string a);
  static Predicate Missing(std::string a);
  static Predicate Compare(PredicateOp op, std::string a, std::string v);
  static Predicate In(std::string a, std::vector<std::string> vs, bool negate = false);
  static Predicate And(std::vector<Predicate> kids);
  static Predicate Or(std::vector<Predicate> kids);
  static Predicate Not(Predicate kid);

  bool is_always() const { return op == PredicateOp::kAlways; }

  // Attribute lookup: returns the stored value, or nullptr when absent.
  using Lookup = std::function<const std::string*(const std::string&)>;

  bool Evaluate(const Lookup& get) const;
  bool Evaluate(const VectorFeature& f) const;

  // Human-readable round-trip of the rule-file syntax; used by diagnostics
  // and by the parser tests.
  std::string ToText() const;
};

// The comparison rule documented above, as free functions — so a PRODUCT's own
// table conditions (S-52's lookup ATTC values, a GeoSym expression, an OSM
// filter) resolve a value the same way the rule layer does instead of each
// engine carrying a private copy that can drift from this comment. Extracted in
// E3c for exactly that reason.
//
// RuleValueAsNumber is true only when the WHOLE token parses as a number
// (trailing whitespace allowed, trailing anything else not): "3" and "-2.5"
// yes, "3a" and "" no. Half-parsed numbers are how a string comparison silently
// becomes a wrong numeric one.
bool RuleValueAsNumber(const std::string& s, double* out);
// -1 / 0 / +1, numeric when both sides are numbers, byte-wise otherwise.
int CompareRuleValues(const std::string& a, const std::string& b);
inline bool RuleValuesEqual(const std::string& a, const std::string& b) {
  return CompareRuleValues(a, b) == 0;
}

// ---------------------------------------------------------------------------
// Scale
// ---------------------------------------------------------------------------

// A window on the map scale DENOMINATOR, so a LARGER number is further zoomed
// OUT (1:250000 is smaller-scale than 1:50000 — the usual cartographic trap).
// 0 on either bound means unbounded on that side, so a default-constructed
// band matches every scale.
//
//   min_denom = 25000, max_denom = 0        -> 1:25k and coarser (zoomed out)
//   min_denom = 0,     max_denom = 100000   -> 1:100k and finer  (zoomed in)
//
// A scale_denominator of 0 means "the caller has no scale", and matches every
// band — a headless bulk render must never lose features to thinning it did
// not ask for.
struct ScaleBand {
  double min_denom = 0.0;
  double max_denom = 0.0;

  bool unbounded() const { return min_denom <= 0.0 && max_denom <= 0.0; }
  bool Contains(double denom) const {
    if (denom <= 0.0) return true;
    if (min_denom > 0.0 && denom < min_denom) return false;
    if (max_denom > 0.0 && denom > max_denom) return false;
    return true;
  }
};

// ---------------------------------------------------------------------------
// Viewing groups and display categories
// ---------------------------------------------------------------------------

// IMO/S-52 display categories. GeoSym's `dispcat` column uses exactly these
// three values, which is why this is here and not in the GeoSym engine: it is
// the one declutter control both products already agree on.
enum DisplayCategory {
  kDisplayCategoryNone = 0,  // the table said nothing
  kDisplayBase = 1,
  kDisplayStandard = 2,
  kDisplayOther = 3,
};

// Runtime on/off state for a product's numbered viewing groups (GeoSym's
// 5-digit IHO `vgroup`, its 2-digit `txtgroup`, an S-52 viewing group, an OSM
// layer class). Group 0 means "ungrouped" and is always enabled — a product
// row with no group must never disappear because of a group toggle.
//
// ONE NUMBER SPACE. A product that has several kinds of group shares this one
// set, so its numbering must not collide. GeoSym is safe by construction and
// it was checked, not assumed: across all of fullsym.txt the IHO viewing
// groups run 11050..38010 (always five digits) while the IHO text groups run
// 1..29, so `Set(21, false)` can only mean the text group. A product whose
// spaces DO overlap needs a second ViewingGroupSet, not a renumbering here.
//
// `epoch()` bumps on every change; a ResolvedPlan carries the epoch it was
// compiled against, so a toggle invalidates the memoized decisions without
// anyone having to remember to.
class ViewingGroupSet {
 public:
  void SetDefault(bool on);
  bool default_on() const { return default_on_; }

  void Set(int group, bool on);
  void SetRange(int lo, int hi, bool on);  // inclusive
  void ClearOverrides();

  bool Enabled(int group) const {
    if (group == 0) return true;
    auto it = overrides_.find(group);
    return it == overrides_.end() ? default_on_ : it->second;
  }

  // Categories are a separate axis from groups: everything at or below
  // `max_category` is shown. kDisplayCategoryNone rows always show.
  void SetMaxCategory(int c);
  int max_category() const { return max_category_; }
  bool CategoryEnabled(int c) const {
    return c == kDisplayCategoryNone || c <= max_category_;
  }

  uint64_t epoch() const { return epoch_; }

 private:
  bool default_on_ = true;
  int max_category_ = kDisplayOther;
  std::map<int, bool> overrides_;
  uint64_t epoch_ = 1;
};

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

// What a matching rule does. `kShow` is not a no-op: it re-enables a feature
// an earlier rule hid, which is how "hide everything, then show these" reads.
enum class RuleAction {
  kShow = 0,
  kHide,
  kModify,  // leave visibility alone, apply the effect
};

// The adjustable part. Every field is optional so overrides compose: a later
// rule that only sets `labels` leaves an earlier rule's priority alone.
//
// `restyle` in the plan's sketch (replacing colours/symbols outright) is NOT
// here: it needs the style vocabulary from style.h, and rules.h deliberately
// sits below it so a source-only or test-only caller can use rules without
// pulling in the canvas. Adding it means moving StyleOverride into style.h,
// not growing this struct.
struct RuleEffect {
  bool has_priority = false;
  int priority = 0;

  bool has_labels = false;
  bool labels = true;  // false = draw the feature but not its label

  bool has_symbol_scale = false;
  double symbol_scale = 1.0;  // multiplies the engine's symbol scale

  // Merges `o` on top of *this (later rule wins per-field).
  void Merge(const RuleEffect& o);
  bool empty() const {
    return !has_priority && !has_labels && !has_symbol_scale;
  }
};

// Which features a rule is about. An empty string is a wildcard, so
// {"", "BE010"} is "that FACC in any layer" and {"hydline", ""} is "that whole
// layer". `style_key` is whatever the product puts in VectorFeature::style_key
// (FACC for VPF, an S-57 acronym for ENC, a tag key for OSM).
struct FeatureKey {
  std::string layer;
  std::string style_key;

  bool operator==(const FeatureKey& o) const {
    return layer == o.layer && style_key == o.style_key;
  }
};

struct FeatureKeyHash {
  size_t operator()(const FeatureKey& k) const {
    // FNV-1a over both halves with a separator, so {"ab",""} != {"a","b"}.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const std::string& s) {
      for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
      h ^= 0xFF; h *= 1099511628211ull;
    };
    mix(k.layer);
    mix(k.style_key);
    return static_cast<size_t>(h);
  }
};

struct Rule {
  FeatureKey match;

  // Optional geometry filter (a rule about area features only).
  bool has_geometry = false;
  VectorGeometryType geometry = VectorGeometryType::kLine;

  Predicate cond;  // Always() by default — this is the cheap path
  ScaleBand scale;

  // Product group ids this rule is about, 0 = not group-scoped. A rule with a
  // group is only APPLIED when that group is enabled, which is how a product
  // table's own rows ride the same machinery as user rules.
  int viewing_group = 0;
  int display_category = kDisplayCategoryNone;

  RuleAction action = RuleAction::kShow;
  RuleEffect effect;

  // Assigned by RuleSet::Add in insertion order. Later wins.
  int order = 0;

  bool key_only() const { return cond.is_always(); }
};

// An ordered pile of rules. The product's own table loads first, the user's
// file second; nothing else distinguishes them.
class RuleSet {
 public:
  void Add(Rule r);
  void Clear();

  size_t size() const { return rules_.size(); }
  bool empty() const { return rules_.empty(); }
  const std::vector<Rule>& rules() const { return rules_; }
  uint64_t epoch() const { return epoch_; }

  // --- rule-file syntax ---------------------------------------------------
  //
  //   # comment
  //   hide  layer=hydline
  //   show  key=BE010  scale=..50000
  //   set   key=DA010  priority=3  labels=off
  //   hide  key=BH140  where hdp exists and hdp < 3
  //   hide  group=27070
  //
  // One rule per line: an action (show|hide|set) followed by selector tokens,
  // optionally followed by `where <predicate>`.
  //
  // Selectors: layer=, key= (alias fcode=), geom=point|line|area,
  //            scale=<min>..<max> (either side may be empty),
  //            group=<int>, category=base|standard|other|<int>
  // `set` effects: priority=<int>, labels=on|off, symbolscale=<double>
  //
  // Predicate grammar (recursive descent, `and` binds tighter than `or`):
  //   expr   := term ('or' term)*
  //   term   := factor ('and' factor)*
  //   factor := 'not' factor | '(' expr ')' | comparison
  //   comparison := ident ('='|'=='|'!='|'<'|'<='|'>'|'>=') value
  //               | ident 'exists' | ident 'missing'
  //               | ident ['not'] 'in' '(' value (',' value)* ')'
  // Values are bare words or "quoted strings".
  //
  // Parsing is all-or-nothing: on the first bad line nothing is added and
  // *error names the line number. A rule file is authored by a human and a
  // half-applied one is worse than none.
  //
  // TWO LIMITS OF THE LINE SYNTAX, both fine for the identifiers real
  // products use and both cheap to lift if that changes: a '#' anywhere
  // outside the predicate starts a comment even inside quotes, and a SELECTOR
  // value may not contain whitespace (selectors are split on spaces before
  // the predicate lexer runs). Predicate values have neither restriction —
  // `where nam = "Fort Sumter"` works.
  Status LoadText(const std::string& text, std::string* error = nullptr);
  Status LoadFile(const std::string& path, std::string* error = nullptr);

 private:
  std::vector<Rule> rules_;
  int next_order_ = 0;
  uint64_t epoch_ = 1;
};

// ---------------------------------------------------------------------------
// ResolvedPlan
// ---------------------------------------------------------------------------

// The per-key answer. When `deferred` is empty, `visible`/`effect` are the
// FINAL answer for every feature with this key at this scale — no predicate
// is ever evaluated for it.
//
// ORDERING: rules apply in source order and later ones win, so compilation
// can only fold a rule into `visible`/`effect` while no per-feature rule has
// been seen yet. From the first per-feature rule onward EVERY applicable rule
// lands in `deferred`, unconditional ones included — otherwise a `hide` that
// comes after a conditional `show` would be applied first and lose. The fast
// path is unaffected: a key with no per-feature rules has an empty `deferred`.
struct RuleDecision {
  bool visible = true;
  RuleEffect effect;
  std::vector<const Rule*> deferred;

  bool needs_predicates() const { return !deferred.empty(); }
};

// Compiled once per (RuleSet epoch, ViewingGroupSet epoch, scale); memoizes a
// RuleDecision per {layer, style_key} as keys are met.
//
// The plan is a VIEW over the RuleSet: it stores `const Rule*` into the set's
// storage, so the RuleSet must outlive it and must not be mutated behind it.
// Mutating bumps the epoch, so `Valid()` catches the mistake; Compile() again
// after any change.
class ResolvedPlan {
 public:
  void Compile(const RuleSet& rules, double scale_denominator,
               const ViewingGroupSet& groups);

  bool Valid(const RuleSet& rules, double scale_denominator,
             const ViewingGroupSet& groups) const;

  // True when there is nothing to do at all (no rules survived compilation).
  // A style engine can skip the lookup entirely — this is the state a caller
  // that never touched rules stays in forever.
  bool trivial() const { return trivial_; }

  // Memoized per-key decision. Cheap and safe to call per feature.
  const RuleDecision& Decide(const FeatureKey& key) const;

  // Full per-feature answer: memoized key lookup, then the conditional rules
  // in order. Returns visibility; *effect (may be null) gets the merge.
  bool Evaluate(const VectorFeature& f, RuleEffect* effect) const;

  // Diagnostics. `predicate_evaluations` is what proves the fast path.
  size_t keys_cached() const { return cache_.size(); }
  size_t predicate_evaluations() const { return predicate_evals_; }
  size_t active_rules() const { return active_.size(); }
  void ResetStats() { predicate_evals_ = 0; }

 private:
  const RuleDecision& DecideForKey(const FeatureKey& key) const;

  std::vector<const Rule*> active_;  // scale/group-filtered, in order
  bool trivial_ = true;
  double scale_ = 0.0;
  uint64_t rules_epoch_ = 0;
  uint64_t groups_epoch_ = 0;

  mutable std::unordered_map<FeatureKey, RuleDecision, FeatureKeyHash> cache_;
  mutable size_t predicate_evals_ = 0;
};

}  // namespace fv

#endif  // FVKIT_VECTOR_RULES_H_
