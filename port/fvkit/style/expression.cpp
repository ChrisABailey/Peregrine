// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// MapLibre expression parsing and evaluation (fvkit/style/expression.h).
#include "fvkit/style/expression.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <nlohmann/json.hpp>

namespace fv {

namespace {

using Json = nlohmann::json;

constexpr int kMaxDepth = 64;

unsigned char ClampByte(double v) {
  if (!(v > 0.0)) return 0;
  if (v >= 255.0) return 255;
  return static_cast<unsigned char>(std::lround(v));
}

std::string Trim(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return std::string();
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/// h in degrees, s and l in [0,1], as CSS defines hsl().
FvColor FromHsl(double h, double s, double l, double a) {
  h = std::fmod(h, 360.0);
  if (h < 0.0) h += 360.0;
  s = std::max(0.0, std::min(1.0, s));
  l = std::max(0.0, std::min(1.0, l));
  const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
  const double hp = h / 60.0;
  const double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
  double r = 0, g = 0, b = 0;
  if (hp < 1.0)      { r = c; g = x; }
  else if (hp < 2.0) { r = x; g = c; }
  else if (hp < 3.0) { g = c; b = x; }
  else if (hp < 4.0) { g = x; b = c; }
  else if (hp < 5.0) { r = x; b = c; }
  else               { r = c; b = x; }
  const double m = l - c / 2.0;
  return FvColor{ClampByte((r + m) * 255.0), ClampByte((g + m) * 255.0),
                 ClampByte((b + m) * 255.0), ClampByte(a * 255.0)};
}

/// "a, b, c" split on commas with whitespace trimmed; percent signs kept.
std::vector<std::string> SplitArgs(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t comma = s.find(',', start);
    if (comma == std::string::npos) {
      out.push_back(Trim(s.substr(start)));
      break;
    }
    out.push_back(Trim(s.substr(start, comma - start)));
    start = comma + 1;
  }
  return out;
}

/// A plain number or a percentage of `full`.
bool ParseColorNumber(const std::string& tok, double full, double* out) {
  if (tok.empty()) return false;
  std::string t = tok;
  bool pct = false;
  if (t.back() == '%') {
    pct = true;
    t.pop_back();
    t = Trim(t);
  }
  char* end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0') return false;
  *out = pct ? v / 100.0 * full : v;
  return true;
}

bool ParseNumberText(const std::string& s, double* out) {
  const std::string t = Trim(s);
  if (t.empty()) return false;
  char* end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0' || !std::isfinite(v)) return false;
  *out = v;
  return true;
}

/// JavaScript's Number#toString for the values a style produces.
std::string NumberText(double v) {
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
  char buf[40];
  if (v == std::floor(v) && std::fabs(v) < 1e21) {
    std::snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
  }
  for (int p = 1; p <= 17; ++p) {
    std::snprintf(buf, sizeof buf, "%.*g", p, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  return buf;
}

enum class Op {
  kLiteral, kGet, kHas, kId, kGeometryType, kZoom,
  kToString, kToNumber, kToBoolean, kToColor,
  kAssertString, kAssertNumber, kAssertBoolean,
  kEq, kNe, kLt, kLe, kGt, kGe, kNot, kAll, kAny, kIn,
  kMatch, kCase, kCoalesce, kStep, kInterpolate,
  kConcat, kDowncase, kUpcase,
  kAdd, kSub, kMul, kDiv, kMod, kMin, kMax, kRgb, kRgba,
};

struct OpInfo {
  const char* name;
  Op op;
  int min_args;
  int max_args;  ///< -1 = variadic
};

const OpInfo kOps[] = {
    {"literal", Op::kLiteral, 1, 1},       {"get", Op::kGet, 1, 1},
    {"has", Op::kHas, 1, 1},               {"id", Op::kId, 0, 0},
    {"geometry-type", Op::kGeometryType, 0, 0}, {"zoom", Op::kZoom, 0, 0},
    {"to-string", Op::kToString, 1, 1},    {"to-number", Op::kToNumber, 1, -1},
    {"to-boolean", Op::kToBoolean, 1, 1},  {"to-color", Op::kToColor, 1, -1},
    {"string", Op::kAssertString, 1, -1},  {"number", Op::kAssertNumber, 1, -1},
    {"boolean", Op::kAssertBoolean, 1, -1},
    {"==", Op::kEq, 2, 2},  {"!=", Op::kNe, 2, 2}, {"<", Op::kLt, 2, 2},
    {"<=", Op::kLe, 2, 2},  {">", Op::kGt, 2, 2},  {">=", Op::kGe, 2, 2},
    {"!", Op::kNot, 1, 1},  {"all", Op::kAll, 0, -1}, {"any", Op::kAny, 0, -1},
    {"in", Op::kIn, 2, 2},
    {"match", Op::kMatch, 4, -1}, {"case", Op::kCase, 3, -1},
    {"coalesce", Op::kCoalesce, 1, -1}, {"step", Op::kStep, 2, -1},
    {"interpolate", Op::kInterpolate, 4, -1},
    {"concat", Op::kConcat, 1, -1}, {"downcase", Op::kDowncase, 1, 1},
    {"upcase", Op::kUpcase, 1, 1},
    {"+", Op::kAdd, 1, -1}, {"-", Op::kSub, 1, 2}, {"*", Op::kMul, 1, -1},
    {"/", Op::kDiv, 2, 2},  {"%", Op::kMod, 2, 2}, {"min", Op::kMin, 1, -1},
    {"max", Op::kMax, 1, -1}, {"rgb", Op::kRgb, 3, 3}, {"rgba", Op::kRgba, 4, 4},
};

Status Bad(const std::string& why) { return Status::Error(kInvalidArg, "expression: " + why); }

/// A JSON literal as a value; arrays only inside `literal` or match labels.
bool LiteralFrom(const Json& j, bool allow_array, StyleValue* out) {
  if (j.is_null()) { *out = StyleValue::Null(); return true; }
  if (j.is_boolean()) { *out = StyleValue::Bool(j.get<bool>()); return true; }
  if (j.is_number()) { *out = StyleValue::Number(j.get<double>()); return true; }
  if (j.is_string()) { *out = StyleValue::String(j.get<std::string>()); return true; }
  if (j.is_array() && allow_array) {
    StyleValue v;
    v.type = StyleValue::Type::kArray;
    for (const Json& e : j) {
      StyleValue ev;
      if (!LiteralFrom(e, true, &ev)) return false;
      v.array.push_back(std::move(ev));
    }
    *out = std::move(v);
    return true;
  }
  return false;
}

bool IsTrue(const StyleValue& v) { return v.type == StyleValue::Type::kBool && v.boolean; }

}  // namespace

struct StyleExpression::Node {
  Op op = Op::kLiteral;
  StyleValue literal;               ///< kLiteral; the key of kGet/kHas
  std::vector<Node> args;
  std::vector<std::vector<StyleValue>> labels;  ///< kMatch, one list per output
  std::vector<double> stops;        ///< kStep/kInterpolate
  double base = 1.0;                ///< kInterpolate; 1 = linear
};

namespace {

using Node = StyleExpression::Node;

Status ParseNode(const Json& j, int depth, Node* out) {
  if (depth > kMaxDepth) return Bad("nested too deeply");
  if (!j.is_array()) {
    if (j.is_object()) return Bad("an object is not an expression: " + j.dump());
    out->op = Op::kLiteral;
    LiteralFrom(j, false, &out->literal);
    return Status::Ok();
  }
  if (j.empty() || !j[0].is_string())
    return Bad("an expression must start with its operator: " + j.dump());
  const std::string name = j[0].get<std::string>();
  const OpInfo* info = nullptr;
  for (const OpInfo& o : kOps)
    if (name == o.name) info = &o;
  if (info == nullptr)
    return Status::Error(kUnsupported, "expression: unsupported operator \"" + name + "\"");
  const int n = static_cast<int>(j.size()) - 1;
  if (n < info->min_args || (info->max_args >= 0 && n > info->max_args))
    return Bad("\"" + name + "\" has the wrong number of arguments: " + j.dump());
  out->op = info->op;

  auto parse_args = [&](size_t from, size_t to) -> Status {
    for (size_t i = from; i < to; ++i) {
      Node a;
      Status s = ParseNode(j[i], depth + 1, &a);
      if (!s.ok()) return s;
      out->args.push_back(std::move(a));
    }
    return Status::Ok();
  };

  switch (info->op) {
    case Op::kLiteral:
      if (!LiteralFrom(j[1], true, &out->literal)) return Bad("literal holds an object");
      return Status::Ok();
    case Op::kGet:
    case Op::kHas:
      if (!j[1].is_string()) return Bad("\"" + name + "\" takes a property name: " + j.dump());
      out->literal = StyleValue::String(j[1].get<std::string>());
      return Status::Ok();
    case Op::kMatch: {
      // input, label/output pairs, fallback
      if (n % 2 != 0) return Bad("match needs an input, label/output pairs and a fallback");
      Status s = parse_args(1, 2);
      if (!s.ok()) return s;
      for (size_t i = 2; i + 1 < j.size(); i += 2) {
        std::vector<StyleValue> labels;
        const Json& l = j[i];
        if (l.is_array()) {
          for (const Json& e : l) {
            StyleValue v;
            if (!LiteralFrom(e, false, &v) || v.is_null()) return Bad("a match label is not a literal");
            labels.push_back(std::move(v));
          }
        } else {
          StyleValue v;
          if (!LiteralFrom(l, false, &v) || v.is_null()) return Bad("a match label is not a literal");
          labels.push_back(std::move(v));
        }
        out->labels.push_back(std::move(labels));
        if (!(s = parse_args(i + 1, i + 2)).ok()) return s;
      }
      return parse_args(j.size() - 1, j.size());
    }
    case Op::kCase:
      if (n % 2 != 1) return Bad("case needs condition/output pairs and a fallback");
      return parse_args(1, j.size());
    case Op::kStep: {
      // input, base output, stop/output pairs
      if (n % 2 != 0) return Bad("step needs an input, a base output and stop/output pairs");
      Status s = parse_args(1, 3);
      if (!s.ok()) return s;
      for (size_t i = 3; i + 1 < j.size(); i += 2) {
        if (!j[i].is_number()) return Bad("step stops must be numbers");
        const double stop = j[i].get<double>();
        if (!out->stops.empty() && !(stop > out->stops.back())) return Bad("step stops must ascend");
        out->stops.push_back(stop);
        if (!(s = parse_args(i + 1, i + 2)).ok()) return s;
      }
      return Status::Ok();
    }
    case Op::kInterpolate: {
      // type, input, stop/output pairs
      if (n % 2 != 0) return Bad("interpolate needs a type, an input and stop/output pairs");
      const Json& type = j[1];
      if (!type.is_array() || type.empty() || !type[0].is_string())
        return Bad("interpolate type is not [\"linear\"] or [\"exponential\", base]");
      const std::string t = type[0].get<std::string>();
      if (t == "linear" && type.size() == 1) {
        out->base = 1.0;
      } else if (t == "exponential" && type.size() == 2 && type[1].is_number()) {
        out->base = type[1].get<double>();
      } else if (t == "cubic-bezier") {
        return Status::Error(kUnsupported, "expression: cubic-bezier interpolation is not supported");
      } else {
        return Bad("interpolate type is not [\"linear\"] or [\"exponential\", base]");
      }
      Status s = parse_args(2, 3);
      if (!s.ok()) return s;
      for (size_t i = 3; i + 1 < j.size(); i += 2) {
        if (!j[i].is_number()) return Bad("interpolate stops must be numbers");
        const double stop = j[i].get<double>();
        if (!out->stops.empty() && !(stop > out->stops.back())) return Bad("interpolate stops must ascend");
        out->stops.push_back(stop);
        if (!(s = parse_args(i + 1, i + 2)).ok()) return s;
      }
      return Status::Ok();
    }
    default:
      return parse_args(1, j.size());
  }
}

bool NodeIsConstant(const Node& n) {
  switch (n.op) {
    case Op::kGet: case Op::kHas: case Op::kId: case Op::kGeometryType: case Op::kZoom:
      return false;
    default:
      break;
  }
  for (const Node& a : n.args)
    if (!NodeIsConstant(a)) return false;
  return true;
}

bool NodeReadsZoom(const Node& n) {
  if (n.op == Op::kZoom) return true;
  for (const Node& a : n.args)
    if (NodeReadsZoom(a)) return true;
  return false;
}

/// <0, 0, >0, or 2 when the two values cannot be ordered.
int Compare(const StyleValue& a, const StyleValue& b) {
  double x = 0, y = 0;
  if (a.AsNumber(&x) && b.AsNumber(&y)) return x < y ? -1 : (x > y ? 1 : 0);
  if (a.type == StyleValue::Type::kString && b.type == StyleValue::Type::kString)
    return a.string < b.string ? -1 : (a.string > b.string ? 1 : 0);
  return 2;
}

/// The factor between stop `lo` and `hi` at `x`, as MapLibre's exponential
/// interpolation defines it (base 1 is linear).
double InterpolationFactor(double x, double base, double lo, double hi) {
  const double difference = hi - lo;
  const double progress = x - lo;
  if (difference == 0.0) return 0.0;
  if (base == 1.0) return progress / difference;
  return (std::pow(base, progress) - 1.0) / (std::pow(base, difference) - 1.0);
}

StyleValue Eval(const Node& n, const StyleExpressionInput& in);

StyleValue EvalNumbers(const Node& n, const StyleExpressionInput& in) {
  std::vector<double> v;
  for (const Node& a : n.args) {
    double d = 0;
    if (!Eval(a, in).AsNumber(&d)) return StyleValue::Null();
    v.push_back(d);
  }
  double r = 0;
  switch (n.op) {
    case Op::kAdd: for (double d : v) r += d; break;
    case Op::kMul: r = 1; for (double d : v) r *= d; break;
    case Op::kSub: r = v.size() == 1 ? -v[0] : v[0] - v[1]; break;
    case Op::kDiv: r = v[0] / v[1]; break;
    case Op::kMod: r = std::fmod(v[0], v[1]); break;
    case Op::kMin: r = *std::min_element(v.begin(), v.end()); break;
    case Op::kMax: r = *std::max_element(v.begin(), v.end()); break;
    case Op::kRgb:
    case Op::kRgba:
      return StyleValue::Color(FvColor{ClampByte(v[0]), ClampByte(v[1]), ClampByte(v[2]),
                                       ClampByte((v.size() == 4 ? v[3] : 1.0) * 255.0)});
    default: return StyleValue::Null();
  }
  return StyleValue::Number(r);
}

StyleValue Eval(const Node& n, const StyleExpressionInput& in) {
  switch (n.op) {
    case Op::kLiteral:
      return n.literal;
    case Op::kGet: {
      StyleValue v;
      if (in.get && in.get(n.literal.string, &v)) return v;
      return StyleValue::Null();
    }
    case Op::kHas: {
      StyleValue v;
      return StyleValue::Bool(in.get && in.get(n.literal.string, &v));
    }
    case Op::kId:
      return in.has_id ? StyleValue::Number(static_cast<double>(in.id)) : StyleValue::Null();
    case Op::kGeometryType:
      return in.geometry_type.empty() ? StyleValue::Null() : StyleValue::String(in.geometry_type);
    case Op::kZoom:
      return StyleValue::Number(in.zoom);

    case Op::kToString:
      return StyleValue::String(Eval(n.args[0], in).ToString());
    case Op::kToNumber:
      for (const Node& a : n.args) {
        const StyleValue v = Eval(a, in);
        double d = 0;
        if (v.is_null()) return StyleValue::Number(0);
        if (v.type == StyleValue::Type::kBool) return StyleValue::Number(v.boolean ? 1 : 0);
        if (v.AsNumber(&d)) return StyleValue::Number(d);
      }
      return StyleValue::Null();
    case Op::kToBoolean: {
      const StyleValue v = Eval(n.args[0], in);
      switch (v.type) {
        case StyleValue::Type::kNull: return StyleValue::Bool(false);
        case StyleValue::Type::kBool: return v;
        case StyleValue::Type::kNumber: return StyleValue::Bool(v.number != 0 && !std::isnan(v.number));
        case StyleValue::Type::kString: return StyleValue::Bool(!v.string.empty());
        default: return StyleValue::Bool(true);
      }
    }
    case Op::kToColor:
      for (const Node& a : n.args) {
        FvColor c;
        if (Eval(a, in).AsColor(&c)) return StyleValue::Color(c);
      }
      return StyleValue::Null();
    case Op::kAssertString:
    case Op::kAssertNumber:
    case Op::kAssertBoolean: {
      const StyleValue::Type want = n.op == Op::kAssertString   ? StyleValue::Type::kString
                                    : n.op == Op::kAssertNumber ? StyleValue::Type::kNumber
                                                                : StyleValue::Type::kBool;
      for (const Node& a : n.args) {
        const StyleValue v = Eval(a, in);
        if (v.type == want) return v;
      }
      return StyleValue::Null();
    }

    case Op::kEq:
      return StyleValue::Bool(StyleValuesEqual(Eval(n.args[0], in), Eval(n.args[1], in)));
    case Op::kNe:
      return StyleValue::Bool(!StyleValuesEqual(Eval(n.args[0], in), Eval(n.args[1], in)));
    case Op::kLt: case Op::kLe: case Op::kGt: case Op::kGe: {
      const int c = Compare(Eval(n.args[0], in), Eval(n.args[1], in));
      if (c == 2) return StyleValue::Bool(false);
      switch (n.op) {
        case Op::kLt: return StyleValue::Bool(c < 0);
        case Op::kLe: return StyleValue::Bool(c <= 0);
        case Op::kGt: return StyleValue::Bool(c > 0);
        default: return StyleValue::Bool(c >= 0);
      }
    }
    case Op::kNot: {
      const StyleValue v = Eval(n.args[0], in);
      return v.type == StyleValue::Type::kBool ? StyleValue::Bool(!v.boolean) : StyleValue::Null();
    }
    case Op::kAll:
      for (const Node& a : n.args)
        if (!IsTrue(Eval(a, in))) return StyleValue::Bool(false);
      return StyleValue::Bool(true);
    case Op::kAny:
      for (const Node& a : n.args)
        if (IsTrue(Eval(a, in))) return StyleValue::Bool(true);
      return StyleValue::Bool(false);
    case Op::kIn: {
      const StyleValue needle = Eval(n.args[0], in);
      const StyleValue hay = Eval(n.args[1], in);
      if (hay.type == StyleValue::Type::kString)
        return StyleValue::Bool(hay.string.find(needle.ToString()) != std::string::npos);
      if (hay.type == StyleValue::Type::kArray) {
        for (const StyleValue& e : hay.array)
          if (StyleValuesEqual(needle, e)) return StyleValue::Bool(true);
      }
      return StyleValue::Bool(false);
    }

    case Op::kMatch: {
      const StyleValue input = Eval(n.args[0], in);
      if (!input.is_null()) {
        for (size_t i = 0; i < n.labels.size(); ++i)
          for (const StyleValue& l : n.labels[i])
            if (StyleValuesEqual(input, l)) return Eval(n.args[1 + i], in);
      }
      return Eval(n.args.back(), in);
    }
    case Op::kCase:
      for (size_t i = 0; i + 1 < n.args.size(); i += 2)
        if (IsTrue(Eval(n.args[i], in))) return Eval(n.args[i + 1], in);
      return Eval(n.args.back(), in);
    case Op::kCoalesce:
      for (const Node& a : n.args) {
        StyleValue v = Eval(a, in);
        if (!v.is_null()) return v;
      }
      return StyleValue::Null();
    case Op::kStep: {
      // NOTE: a non-numeric input takes the base output where MapLibre
      // errors; a feature with no value then draws with the base, which is
      // what the feature style's "Other" symbol means.
      double x = 0;
      size_t chosen = 1;
      if (Eval(n.args[0], in).AsNumber(&x)) {
        for (size_t i = 0; i < n.stops.size() && x >= n.stops[i]; ++i) chosen = 2 + i;
      }
      return Eval(n.args[chosen], in);
    }
    case Op::kInterpolate: {
      double x = 0;
      if (!Eval(n.args[0], in).AsNumber(&x)) return StyleValue::Null();
      const size_t last = n.stops.size() - 1;
      if (x <= n.stops.front()) return Eval(n.args[1], in);
      if (x >= n.stops[last]) return Eval(n.args[1 + last], in);
      size_t i = 0;
      while (i + 1 < last && n.stops[i + 1] <= x) ++i;
      const double t = InterpolationFactor(x, n.base, n.stops[i], n.stops[i + 1]);
      const StyleValue lo = Eval(n.args[1 + i], in);
      const StyleValue hi = Eval(n.args[2 + i], in);
      double a = 0, b = 0;
      if (lo.AsNumber(&a) && hi.AsNumber(&b)) return StyleValue::Number(a + t * (b - a));
      FvColor ca, cb;
      if (lo.AsColor(&ca) && hi.AsColor(&cb)) {
        auto mix = [t](unsigned char p, unsigned char q) { return ClampByte(p + t * (q - p)); };
        return StyleValue::Color(FvColor{mix(ca.r, cb.r), mix(ca.g, cb.g), mix(ca.b, cb.b), mix(ca.a, cb.a)});
      }
      return StyleValue::Null();
    }

    case Op::kConcat: {
      std::string s;
      for (const Node& a : n.args) s += Eval(a, in).ToString();
      return StyleValue::String(std::move(s));
    }
    case Op::kDowncase:
    case Op::kUpcase: {
      std::string s = Eval(n.args[0], in).ToString();
      for (char& c : s)
        c = static_cast<char>(n.op == Op::kUpcase ? std::toupper(static_cast<unsigned char>(c))
                                                  : std::tolower(static_cast<unsigned char>(c)));
      return StyleValue::String(std::move(s));
    }
    default:
      return EvalNumbers(n, in);
  }
}

}  // namespace

StyleValue StyleValue::Bool(bool v) {
  StyleValue s;
  s.type = Type::kBool;
  s.boolean = v;
  return s;
}

StyleValue StyleValue::Number(double v) {
  StyleValue s;
  s.type = Type::kNumber;
  s.number = v;
  return s;
}

StyleValue StyleValue::String(std::string v) {
  StyleValue s;
  s.type = Type::kString;
  s.string = std::move(v);
  return s;
}

StyleValue StyleValue::Color(const FvColor& v) {
  StyleValue s;
  s.type = Type::kColor;
  s.color = v;
  return s;
}

bool StyleValue::AsNumber(double* out) const {
  if (type == Type::kNumber) {
    *out = number;
    return true;
  }
  return type == Type::kString && ParseNumberText(string, out);
}

bool StyleValue::AsColor(FvColor* out) const {
  if (type == Type::kColor) {
    *out = color;
    return true;
  }
  return type == Type::kString && ParseCssColor(string, out);
}

std::string StyleValue::ToString() const {
  switch (type) {
    case Type::kNull: return std::string();
    case Type::kBool: return boolean ? "true" : "false";
    case Type::kNumber: return NumberText(number);
    case Type::kString: return string;
    case Type::kColor: {
      char buf[64];
      std::snprintf(buf, sizeof buf, "rgba(%d,%d,%d,%s)", color.r, color.g, color.b,
                    NumberText(color.a / 255.0).c_str());
      return buf;
    }
    case Type::kArray: {
      std::string s = "[";
      for (size_t i = 0; i < array.size(); ++i) {
        if (i) s += ",";
        s += array[i].type == Type::kString ? "\"" + array[i].string + "\"" : array[i].ToString();
      }
      return s + "]";
    }
  }
  return std::string();
}

bool StyleValuesEqual(const StyleValue& a, const StyleValue& b) {
  using T = StyleValue::Type;
  if (a.type == b.type) {
    switch (a.type) {
      case T::kNull: return true;
      case T::kBool: return a.boolean == b.boolean;
      case T::kNumber: return a.number == b.number;
      case T::kString: return a.string == b.string;
      case T::kColor:
        return a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b &&
               a.color.a == b.color.a;
      case T::kArray:
        if (a.array.size() != b.array.size()) return false;
        for (size_t i = 0; i < a.array.size(); ++i)
          if (!StyleValuesEqual(a.array[i], b.array[i])) return false;
        return true;
    }
  }
  // Attributes arrive as text; compare them with numbers and booleans by value.
  const StyleValue& s = a.type == T::kString ? a : b;
  const StyleValue& o = a.type == T::kString ? b : a;
  if (s.type != T::kString) return false;
  double x = 0;
  if (o.type == T::kNumber) return ParseNumberText(s.string, &x) && x == o.number;
  if (o.type == T::kBool) return s.string == (o.boolean ? "true" : "false");
  return false;
}

Status StyleExpression::Parse(const std::string& json_text, StyleExpression* out) {
  if (out == nullptr) return Bad("null out");
  Json j;
  try {
    j = Json::parse(json_text);
  } catch (const std::exception& e) {
    return Bad(std::string("not JSON: ") + e.what());
  }
  auto node = std::make_shared<Node>();
  Status s = ParseNode(j, 0, node.get());
  if (!s.ok()) return s;
  out->root_ = std::move(node);
  out->text_ = j.dump();
  return Status::Ok();
}

StyleExpression StyleExpression::Constant(const StyleValue& v) {
  StyleExpression e;
  auto node = std::make_shared<Node>();
  node->op = Op::kLiteral;
  node->literal = v;
  e.root_ = std::move(node);
  e.text_ = v.type == StyleValue::Type::kString ? Json(v.string).dump() : v.ToString();
  return e;
}

bool StyleExpression::is_constant() const { return root_ == nullptr || NodeIsConstant(*root_); }

bool StyleExpression::reads_zoom() const { return root_ != nullptr && NodeReadsZoom(*root_); }

StyleValue StyleExpression::Evaluate(const StyleExpressionInput& in) const {
  if (root_ == nullptr) return StyleValue::Null();
  return Eval(*root_, in);
}

bool ParseCssColor(const std::string& raw, FvColor* out) {
  const std::string s = Trim(raw);
  if (s.empty()) return false;

  if (s[0] == '#') {
    const std::string h = s.substr(1);
    for (char c : h)
      if (HexDigit(c) < 0) return false;
    auto nib = [&h](size_t i) { return HexDigit(h[i]); };
    if (h.size() == 3 || h.size() == 4) {
      out->r = static_cast<unsigned char>(nib(0) * 17);
      out->g = static_cast<unsigned char>(nib(1) * 17);
      out->b = static_cast<unsigned char>(nib(2) * 17);
      out->a = h.size() == 4 ? static_cast<unsigned char>(nib(3) * 17) : 255;
      return true;
    }
    if (h.size() == 6 || h.size() == 8) {
      out->r = static_cast<unsigned char>(nib(0) * 16 + nib(1));
      out->g = static_cast<unsigned char>(nib(2) * 16 + nib(3));
      out->b = static_cast<unsigned char>(nib(4) * 16 + nib(5));
      out->a = h.size() == 8 ? static_cast<unsigned char>(nib(6) * 16 + nib(7)) : 255;
      return true;
    }
    return false;
  }

  const size_t open = s.find('(');
  if (open != std::string::npos && s.back() == ')') {
    const std::string fn = Trim(s.substr(0, open));
    const std::vector<std::string> args = SplitArgs(s.substr(open + 1, s.size() - open - 2));
    if (fn == "rgb" || fn == "rgba") {
      if (args.size() != (fn == "rgb" ? 3u : 4u)) return false;
      double r, g, b, a = 1.0;
      if (!ParseColorNumber(args[0], 255.0, &r)) return false;
      if (!ParseColorNumber(args[1], 255.0, &g)) return false;
      if (!ParseColorNumber(args[2], 255.0, &b)) return false;
      if (args.size() == 4 && !ParseColorNumber(args[3], 1.0, &a)) return false;
      *out = FvColor{ClampByte(r), ClampByte(g), ClampByte(b), ClampByte(a * 255.0)};
      return true;
    }
    if (fn == "hsl" || fn == "hsla") {
      if (args.size() != (fn == "hsl" ? 3u : 4u)) return false;
      double h, sat, l, a = 1.0;
      if (!ParseColorNumber(args[0], 360.0, &h)) return false;
      if (!ParseColorNumber(args[1], 1.0, &sat)) return false;
      if (!ParseColorNumber(args[2], 1.0, &l)) return false;
      if (args.size() == 4 && !ParseColorNumber(args[3], 1.0, &a)) return false;
      *out = FromHsl(h, sat, l, a);
      return true;
    }
    return false;
  }

  // Three names only: an unknown name is a load failure rather than black.
  if (s == "white")       { *out = FvColor{255, 255, 255, 255}; return true; }
  if (s == "black")       { *out = FvColor{0, 0, 0, 255};       return true; }
  if (s == "transparent") { *out = FvColor{0, 0, 0, 0};         return true; }
  return false;
}

}  // namespace fv
