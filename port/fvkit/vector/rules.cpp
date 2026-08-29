// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/vector/rules.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fv {
namespace {

std::string Quote(const std::string& s) {
  bool needs = s.empty();
  for (char c : s)
    if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' ||
        c == ',' || c == '"')
      needs = true;
  if (!needs) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The comparison rule (rules.h documents it; E3c made it callable so the
// product style engines share this one implementation)
// ---------------------------------------------------------------------------

bool RuleValueAsNumber(const std::string& s, double* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str()) return false;
  while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
  if (*end != '\0') return false;
  *out = v;
  return true;
}

int CompareRuleValues(const std::string& a, const std::string& b) {
  double na = 0.0, nb = 0.0;
  if (RuleValueAsNumber(a, &na) && RuleValueAsNumber(b, &nb))
    return na < nb ? -1 : (na > nb ? 1 : 0);
  const int c = a.compare(b);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

namespace {
// The names the rest of this file was written against.
inline bool AsNumber(const std::string& s, double* out) {
  return RuleValueAsNumber(s, out);
}
inline int Compare3(const std::string& a, const std::string& b) {
  return CompareRuleValues(a, b);
}
}  // namespace

// ---------------------------------------------------------------------------
// Predicate
// ---------------------------------------------------------------------------

Predicate Predicate::Exists(std::string a) {
  Predicate p;
  p.op = PredicateOp::kExists;
  p.attribute = std::move(a);
  return p;
}

Predicate Predicate::Missing(std::string a) {
  Predicate p;
  p.op = PredicateOp::kMissing;
  p.attribute = std::move(a);
  return p;
}

Predicate Predicate::Compare(PredicateOp op, std::string a, std::string v) {
  Predicate p;
  p.op = op;
  p.attribute = std::move(a);
  p.values.push_back(std::move(v));
  return p;
}

Predicate Predicate::In(std::string a, std::vector<std::string> vs,
                        bool negate) {
  Predicate p;
  p.op = negate ? PredicateOp::kNotIn : PredicateOp::kIn;
  p.attribute = std::move(a);
  p.values = std::move(vs);
  return p;
}

Predicate Predicate::And(std::vector<Predicate> kids) {
  Predicate p;
  p.op = PredicateOp::kAnd;
  p.children = std::move(kids);
  return p;
}

Predicate Predicate::Or(std::vector<Predicate> kids) {
  Predicate p;
  p.op = PredicateOp::kOr;
  p.children = std::move(kids);
  return p;
}

Predicate Predicate::Not(Predicate kid) {
  Predicate p;
  p.op = PredicateOp::kNot;
  p.children.push_back(std::move(kid));
  return p;
}

bool Predicate::Evaluate(const Lookup& get) const {
  switch (op) {
    case PredicateOp::kAlways: return true;
    case PredicateOp::kNever: return false;
    case PredicateOp::kExists: return get(attribute) != nullptr;
    case PredicateOp::kMissing: return get(attribute) == nullptr;
    case PredicateOp::kAnd:
      for (const Predicate& c : children)
        if (!c.Evaluate(get)) return false;
      return true;
    case PredicateOp::kOr:
      for (const Predicate& c : children)
        if (c.Evaluate(get)) return true;
      return false;
    case PredicateOp::kNot:
      return children.empty() ? false : !children[0].Evaluate(get);
    default: break;
  }

  // Everything below compares a stored value. An ABSENT attribute makes them
  // all false, kNotIn included: "not in that list" is a claim about a value
  // that is there, and kMissing is how you ask the other question.
  const std::string* v = get(attribute);
  if (v == nullptr) return false;

  if (op == PredicateOp::kIn || op == PredicateOp::kNotIn) {
    bool found = false;
    for (const std::string& candidate : values)
      if (Compare3(*v, candidate) == 0) { found = true; break; }
    return op == PredicateOp::kIn ? found : !found;
  }

  if (values.empty()) return false;
  const int c = Compare3(*v, values[0]);
  switch (op) {
    case PredicateOp::kEqual: return c == 0;
    case PredicateOp::kNotEqual: return c != 0;
    case PredicateOp::kLess: return c < 0;
    case PredicateOp::kLessEqual: return c <= 0;
    case PredicateOp::kGreater: return c > 0;
    case PredicateOp::kGreaterEqual: return c >= 0;
    default: return false;
  }
}

bool Predicate::Evaluate(const VectorFeature& f) const {
  return Evaluate([&f](const std::string& k) { return f.Attribute(k); });
}

std::string Predicate::ToText() const {
  auto join = [this](const char* sep) {
    std::string s;
    for (size_t i = 0; i < children.size(); ++i) {
      if (i != 0) { s += " "; s += sep; s += " "; }
      const bool paren = children[i].op == PredicateOp::kAnd ||
                         children[i].op == PredicateOp::kOr;
      if (paren) s += "(";
      s += children[i].ToText();
      if (paren) s += ")";
    }
    return s;
  };
  switch (op) {
    case PredicateOp::kAlways: return "always";
    case PredicateOp::kNever: return "never";
    case PredicateOp::kExists: return attribute + " exists";
    case PredicateOp::kMissing: return attribute + " missing";
    case PredicateOp::kAnd: return join("and");
    case PredicateOp::kOr: return join("or");
    case PredicateOp::kNot: {
      if (children.empty()) return "never";
      // A compound child must be parenthesized or the text re-parses as
      // `(not a) and b` — `not` binds tighter than `and`.
      const bool paren = children[0].op == PredicateOp::kAnd ||
                         children[0].op == PredicateOp::kOr;
      return paren ? ("not (" + children[0].ToText() + ")")
                   : ("not " + children[0].ToText());
    }
    case PredicateOp::kIn:
    case PredicateOp::kNotIn: {
      std::string s = attribute;
      s += op == PredicateOp::kIn ? " in (" : " not in (";
      for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) s += ", ";
        s += Quote(values[i]);
      }
      s += ")";
      return s;
    }
    default: break;
  }
  const char* o = "=";
  switch (op) {
    case PredicateOp::kNotEqual: o = "!="; break;
    case PredicateOp::kLess: o = "<"; break;
    case PredicateOp::kLessEqual: o = "<="; break;
    case PredicateOp::kGreater: o = ">"; break;
    case PredicateOp::kGreaterEqual: o = ">="; break;
    default: break;
  }
  return attribute + " " + o + " " + Quote(values.empty() ? "" : values[0]);
}

// ---------------------------------------------------------------------------
// ViewingGroupSet
// ---------------------------------------------------------------------------

void ViewingGroupSet::SetDefault(bool on) {
  if (default_on_ == on) return;
  default_on_ = on;
  ++epoch_;
}

void ViewingGroupSet::Set(int group, bool on) {
  if (group == 0) return;  // group 0 is "ungrouped" and never toggles
  auto it = overrides_.find(group);
  if (it != overrides_.end() && it->second == on) return;
  overrides_[group] = on;
  ++epoch_;
}

void ViewingGroupSet::SetRange(int lo, int hi, bool on) {
  for (int g = lo; g <= hi; ++g) Set(g, on);
}

void ViewingGroupSet::ClearOverrides() {
  if (overrides_.empty()) return;
  overrides_.clear();
  ++epoch_;
}

void ViewingGroupSet::SetMaxCategory(int c) {
  if (max_category_ == c) return;
  max_category_ = c;
  ++epoch_;
}

// ---------------------------------------------------------------------------
// RuleEffect / RuleSet
// ---------------------------------------------------------------------------

void RuleEffect::Merge(const RuleEffect& o) {
  if (o.has_priority) { has_priority = true; priority = o.priority; }
  if (o.has_labels) { has_labels = true; labels = o.labels; }
  if (o.has_symbol_scale) {
    has_symbol_scale = true;
    symbol_scale = o.symbol_scale;
  }
}

void RuleSet::Add(Rule r) {
  r.order = next_order_++;
  rules_.push_back(std::move(r));
  ++epoch_;
}

void RuleSet::Clear() {
  if (rules_.empty()) return;
  rules_.clear();
  next_order_ = 0;
  ++epoch_;
}

// ---------------------------------------------------------------------------
// Rule-file parsing
// ---------------------------------------------------------------------------

namespace {

// Tokens are bare words, quoted strings, punctuation, and comparison
// operators. `key=value` selectors are split by the line parser, not here,
// because a value may legitimately contain '=' inside quotes.
struct Lexer {
  const std::string& s;
  size_t i = 0;
  explicit Lexer(const std::string& src) : s(src) {}

  void SkipSpace() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }
  bool Eof() {
    SkipSpace();
    return i >= s.size();
  }
  // Reads one token. Returns false at end of input.
  bool Next(std::string* tok, bool* quoted) {
    *quoted = false;
    tok->clear();
    SkipSpace();
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '"' || c == '\'') {
      const char q = c;
      ++i;
      while (i < s.size() && s[i] != q) {
        if (s[i] == '\\' && i + 1 < s.size()) ++i;
        tok->push_back(s[i++]);
      }
      if (i < s.size()) ++i;  // closing quote
      *quoted = true;
      return true;
    }
    if (c == '(' || c == ')' || c == ',') {
      tok->push_back(s[i++]);
      return true;
    }
    if (c == '<' || c == '>' || c == '!' || c == '=') {
      tok->push_back(s[i++]);
      if (i < s.size() && s[i] == '=') tok->push_back(s[i++]);
      return true;
    }
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) &&
           s[i] != '(' && s[i] != ')' && s[i] != ',' && s[i] != '<' &&
           s[i] != '>' && s[i] != '!' && s[i] != '=')
      tok->push_back(s[i++]);
    if (tok->empty()) tok->push_back(s[i++]);  // never stall
    return true;
  }
  size_t save() const { return i; }
  void restore(size_t p) { i = p; }
};

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Recursive-descent predicate parser. Grammar in rules.h.
class PredParser {
 public:
  PredParser(Lexer& lex, std::string* err) : lex_(lex), err_(err) {}

  bool Parse(Predicate* out) {
    if (!Expr(out)) return false;
    return true;
  }

 private:
  bool Fail(const std::string& msg) {
    if (err_ != nullptr && err_->empty()) *err_ = msg;
    return false;
  }

  bool Peek(std::string* tok) {
    const size_t p = lex_.save();
    bool q = false;
    const bool got = lex_.Next(tok, &q);
    lex_.restore(p);
    return got && !q;
  }

  bool Expr(Predicate* out) {
    Predicate first;
    if (!Term(&first)) return false;
    std::vector<Predicate> kids;
    std::string tok;
    while (Peek(&tok) && Lower(tok) == "or") {
      bool q = false;
      lex_.Next(&tok, &q);
      Predicate next;
      if (!Term(&next)) return false;
      if (kids.empty()) kids.push_back(std::move(first));
      kids.push_back(std::move(next));
    }
    *out = kids.empty() ? std::move(first) : Predicate::Or(std::move(kids));
    return true;
  }

  bool Term(Predicate* out) {
    Predicate first;
    if (!Factor(&first)) return false;
    std::vector<Predicate> kids;
    std::string tok;
    while (Peek(&tok) && Lower(tok) == "and") {
      bool q = false;
      lex_.Next(&tok, &q);
      Predicate next;
      if (!Factor(&next)) return false;
      if (kids.empty()) kids.push_back(std::move(first));
      kids.push_back(std::move(next));
    }
    *out = kids.empty() ? std::move(first) : Predicate::And(std::move(kids));
    return true;
  }

  bool Factor(Predicate* out) {
    std::string tok;
    bool quoted = false;
    if (!lex_.Next(&tok, &quoted)) return Fail("predicate ended early");
    if (!quoted && Lower(tok) == "not") {
      // `not` here is the unary operator; `x not in (...)` is handled in
      // Comparison, which sees the identifier first.
      Predicate kid;
      if (!Factor(&kid)) return false;
      *out = Predicate::Not(std::move(kid));
      return true;
    }
    if (!quoted && tok == "(") {
      if (!Expr(out)) return false;
      std::string close;
      bool q = false;
      if (!lex_.Next(&close, &q) || close != ")") return Fail("expected ')'");
      return true;
    }
    return Comparison(tok, out);
  }

  bool Comparison(const std::string& ident, Predicate* out) {
    std::string op;
    bool quoted = false;
    if (!lex_.Next(&op, &quoted)) return Fail("expected an operator after '" + ident + "'");
    const std::string lop = quoted ? op : Lower(op);
    if (!quoted && lop == "exists") { *out = Predicate::Exists(ident); return true; }
    if (!quoted && lop == "missing") { *out = Predicate::Missing(ident); return true; }

    bool negate = false;
    std::string kw = lop;
    if (!quoted && lop == "not") {
      negate = true;
      std::string nxt;
      bool q = false;
      if (!lex_.Next(&nxt, &q) || Lower(nxt) != "in")
        return Fail("expected 'in' after 'not' in a comparison");
      kw = "in";
    }
    if (!quoted && kw == "in") {
      std::string tok;
      bool q = false;
      if (!lex_.Next(&tok, &q) || tok != "(") return Fail("expected '(' after 'in'");
      std::vector<std::string> vals;
      for (;;) {
        if (!lex_.Next(&tok, &q)) return Fail("unterminated 'in' list");
        if (!q && tok == ")") break;
        if (!q && tok == ",") continue;
        vals.push_back(tok);
      }
      if (vals.empty()) return Fail("empty 'in' list");
      *out = Predicate::In(ident, std::move(vals), negate);
      return true;
    }

    PredicateOp pop;
    if (op == "=" || op == "==") pop = PredicateOp::kEqual;
    else if (op == "!=") pop = PredicateOp::kNotEqual;
    else if (op == "<") pop = PredicateOp::kLess;
    else if (op == "<=") pop = PredicateOp::kLessEqual;
    else if (op == ">") pop = PredicateOp::kGreater;
    else if (op == ">=") pop = PredicateOp::kGreaterEqual;
    else return Fail("unknown operator '" + op + "'");

    std::string val;
    bool q = false;
    if (!lex_.Next(&val, &q)) return Fail("expected a value after '" + op + "'");
    // An unquoted operator or punctuation token is never a value: without
    // this, `hdp <<` lexes as "hdp < '<'" and a typo becomes a silent rule
    // that compares against a literal angle bracket.
    if (!q && (val == "(" || val == ")" || val == "," || val == "<" ||
               val == ">" || val == "=" || val == "==" || val == "!=" ||
               val == "<=" || val == ">="))
      return Fail("expected a value after '" + op + "', got '" + val + "'");
    *out = Predicate::Compare(pop, ident, val);
    return true;
  }

  Lexer& lex_;
  std::string* err_;
};

bool ParseInt(const std::string& s, int* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0') return false;
  *out = static_cast<int>(v);
  return true;
}

bool ParseDouble(const std::string& s, double* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0') return false;
  *out = v;
  return true;
}

bool ParseCategory(const std::string& s, int* out) {
  const std::string l = Lower(s);
  if (l == "base") { *out = kDisplayBase; return true; }
  if (l == "standard") { *out = kDisplayStandard; return true; }
  if (l == "other") { *out = kDisplayOther; return true; }
  return ParseInt(s, out);
}

// `scale=<min>..<max>`; either side may be empty for unbounded.
bool ParseScale(const std::string& s, ScaleBand* out) {
  const size_t dots = s.find("..");
  if (dots == std::string::npos) return false;
  const std::string lo = s.substr(0, dots);
  const std::string hi = s.substr(dots + 2);
  out->min_denom = 0.0;
  out->max_denom = 0.0;
  if (!lo.empty() && !ParseDouble(lo, &out->min_denom)) return false;
  if (!hi.empty() && !ParseDouble(hi, &out->max_denom)) return false;
  return true;
}

// One line -> one Rule. `line` has already had its comment stripped.
bool ParseRuleLine(const std::string& line, Rule* out, std::string* err) {
  // Split off an optional `where <predicate>` first, so a predicate value
  // containing '=' never confuses the selector splitter.
  std::string head = line;
  std::string cond_text;
  {
    // Find a `where` token at top level (selectors have no parentheses).
    Lexer scan(line);
    std::string tok;
    bool q = false;
    while (true) {
      scan.SkipSpace();
      const size_t start = scan.i;
      if (!scan.Next(&tok, &q)) break;
      if (!q && Lower(tok) == "where") {
        head = line.substr(0, start);
        cond_text = line.substr(scan.i);
        break;
      }
    }
  }

  std::istringstream hs(head);
  std::string action;
  if (!(hs >> action)) return false;  // blank line: caller skips
  const std::string la = Lower(action);
  if (la == "show") out->action = RuleAction::kShow;
  else if (la == "hide") out->action = RuleAction::kHide;
  else if (la == "set" || la == "modify") out->action = RuleAction::kModify;
  else { *err = "unknown action '" + action + "' (expected show|hide|set)"; return false; }

  std::string tok;
  while (hs >> tok) {
    const size_t eq = tok.find('=');
    if (eq == std::string::npos) {
      *err = "expected name=value, got '" + tok + "'";
      return false;
    }
    const std::string name = Lower(tok.substr(0, eq));
    std::string value = tok.substr(eq + 1);
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
        value.back() == value.front())
      value = value.substr(1, value.size() - 2);

    if (name == "layer") out->match.layer = value;
    else if (name == "key" || name == "fcode") out->match.style_key = value;
    else if (name == "geom") {
      const std::string v = Lower(value);
      out->has_geometry = true;
      if (v == "point") out->geometry = VectorGeometryType::kPoint;
      else if (v == "line") out->geometry = VectorGeometryType::kLine;
      else if (v == "area") out->geometry = VectorGeometryType::kArea;
      else { *err = "geom must be point|line|area"; return false; }
    } else if (name == "scale") {
      if (!ParseScale(value, &out->scale)) {
        *err = "scale must be <min>..<max>";
        return false;
      }
    } else if (name == "group") {
      if (!ParseInt(value, &out->viewing_group)) { *err = "group must be an integer"; return false; }
    } else if (name == "category") {
      if (!ParseCategory(value, &out->display_category)) {
        *err = "category must be base|standard|other or an integer";
        return false;
      }
    } else if (name == "priority") {
      if (!ParseInt(value, &out->effect.priority)) { *err = "priority must be an integer"; return false; }
      out->effect.has_priority = true;
    } else if (name == "labels") {
      const std::string v = Lower(value);
      if (v != "on" && v != "off" && v != "true" && v != "false") {
        *err = "labels must be on|off";
        return false;
      }
      out->effect.has_labels = true;
      out->effect.labels = (v == "on" || v == "true");
    } else if (name == "symbolscale") {
      if (!ParseDouble(value, &out->effect.symbol_scale)) {
        *err = "symbolscale must be a number";
        return false;
      }
      out->effect.has_symbol_scale = true;
    } else {
      *err = "unknown selector '" + name + "'";
      return false;
    }
  }

  if (!cond_text.empty()) {
    Lexer lex(cond_text);
    PredParser p(lex, err);
    if (!p.Parse(&out->cond)) {
      if (err->empty()) *err = "bad predicate";
      return false;
    }
    if (!lex.Eof()) {
      *err = "trailing text after the predicate";
      return false;
    }
  }
  return true;
}

}  // namespace

Status RuleSet::LoadText(const std::string& text, std::string* error) {
  std::vector<Rule> parsed;
  std::istringstream in(text);
  std::string line;
  int lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    // Blank / comment-only lines.
    bool blank = true;
    for (char c : line)
      if (!std::isspace(static_cast<unsigned char>(c))) { blank = false; break; }
    if (blank) continue;

    Rule r;
    std::string err;
    if (!ParseRuleLine(line, &r, &err)) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "line %d: ", lineno);
      const std::string msg = std::string(buf) + (err.empty() ? "bad rule" : err);
      if (error != nullptr) *error = msg;
      return Status::Error(kInvalidArg, msg);
    }
    parsed.push_back(std::move(r));
  }
  // All-or-nothing: only commit once the whole file parsed.
  for (Rule& r : parsed) Add(std::move(r));
  if (error != nullptr) error->clear();
  return Status::Ok();
}

Status RuleSet::LoadFile(const std::string& path, std::string* error) {
  std::ifstream f(path);
  if (!f) {
    const std::string msg = "cannot open rule file " + path;
    if (error != nullptr) *error = msg;
    return Status::Error(kNotFound, msg);
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return LoadText(ss.str(), error);
}

// ---------------------------------------------------------------------------
// ResolvedPlan
// ---------------------------------------------------------------------------

void ResolvedPlan::Compile(const RuleSet& rules, double scale_denominator,
                           const ViewingGroupSet& groups) {
  active_.clear();
  cache_.clear();
  predicate_evals_ = 0;
  scale_ = scale_denominator;
  rules_epoch_ = rules.epoch();
  groups_epoch_ = groups.epoch();

  // Scale and group filtering happen ONCE here, not per feature. A rule
  // scoped to a disabled viewing group or an out-of-window scale simply is
  // not in the plan.
  for (const Rule& r : rules.rules()) {
    if (!r.scale.Contains(scale_denominator)) continue;
    if (r.viewing_group != 0 && !groups.Enabled(r.viewing_group)) continue;
    if (r.display_category != kDisplayCategoryNone &&
        !groups.CategoryEnabled(r.display_category))
      continue;
    active_.push_back(&r);
  }
  trivial_ = active_.empty();
}

bool ResolvedPlan::Valid(const RuleSet& rules, double scale_denominator,
                         const ViewingGroupSet& groups) const {
  return rules_epoch_ == rules.epoch() && groups_epoch_ == groups.epoch() &&
         scale_ == scale_denominator;
}

namespace {
const RuleDecision& DefaultDecision() {
  static const RuleDecision kDefault;
  return kDefault;
}
}  // namespace

const RuleDecision& ResolvedPlan::DecideForKey(const FeatureKey& key) const {
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;

  RuleDecision d;
  for (const Rule* r : active_) {
    if (!r->match.layer.empty() && r->match.layer != key.layer) continue;
    if (!r->match.style_key.empty() && r->match.style_key != key.style_key)
      continue;
    // Geometry is a per-feature test even without a predicate, so a
    // geometry-scoped rule is per-feature too.
    const bool per_feature = !r->key_only() || r->has_geometry;
    // Once anything is deferred, everything after it must be as well, or
    // source order stops deciding the outcome (see RuleDecision in rules.h).
    if (per_feature || !d.deferred.empty()) {
      d.deferred.push_back(r);
      continue;
    }
    if (r->action == RuleAction::kHide) d.visible = false;
    else if (r->action == RuleAction::kShow) d.visible = true;
    d.effect.Merge(r->effect);
  }
  return cache_.emplace(key, std::move(d)).first->second;
}

const RuleDecision& ResolvedPlan::Decide(const FeatureKey& key) const {
  if (trivial_) return DefaultDecision();
  return DecideForKey(key);
}

bool ResolvedPlan::Evaluate(const VectorFeature& f, RuleEffect* effect) const {
  if (trivial_) {
    if (effect != nullptr) *effect = RuleEffect();
    return true;
  }
  FeatureKey key;
  key.layer = f.layer;
  key.style_key = f.style_key;
  const RuleDecision& d = DecideForKey(key);

  bool visible = d.visible;
  RuleEffect merged = d.effect;
  for (const Rule* r : d.deferred) {
    if (r->has_geometry && r->geometry != f.type) continue;
    if (!r->cond.is_always()) {
      ++predicate_evals_;
      if (!r->cond.Evaluate(f)) continue;
    }
    if (r->action == RuleAction::kHide) visible = false;
    else if (r->action == RuleAction::kShow) visible = true;
    merged.Merge(r->effect);
  }
  if (effect != nullptr) *effect = merged;
  return visible;
}

}  // namespace fv
