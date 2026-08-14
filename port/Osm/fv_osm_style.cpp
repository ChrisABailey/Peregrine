// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_osm_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "fv_web_mercator.h"
#include "fvkit/proj.h"  // kNativeDisplayMmPerPixel

namespace fv {
namespace {

using Json = nlohmann::json;

// MapLibre paint values are CSS pixels at this nominal resolution.
constexpr double kStyleNominalDpi = 96.0;

// 1 device pixel in the HIMETRIC units a VectorSymbol display list is authored
// in (see fvkit/vector/renderer.h — the renderer's px_per_himetric is fixed at
// 1/25.4, so this is its exact inverse).
constexpr double kHimetricPerPixel = 25.4;

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

unsigned char ClampByte(double v) {
  if (!(v > 0.0)) return 0;
  if (v >= 255.0) return 255;
  return static_cast<unsigned char>(std::lround(v));
}

// h in [0,360), s and l in [0,1] — the CSS definition, which is what a GL
// style's hsl()/hsla() means.
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
  FvColor out;
  out.r = ClampByte((r + m) * 255.0);
  out.g = ClampByte((g + m) * 255.0);
  out.b = ClampByte((b + m) * 255.0);
  out.a = ClampByte(a * 255.0);
  return out;
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return std::string();
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Splits "a, b, c" — commas only, whitespace trimmed. Percent signs are kept
// for the caller, because hsl()'s 2nd/3rd arguments carry them and rgb()'s
// may.
std::vector<std::string> SplitArgs(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    size_t comma = s.find(',', start);
    if (comma == std::string::npos) {
      out.push_back(Trim(s.substr(start)));
      break;
    }
    out.push_back(Trim(s.substr(start, comma - start)));
    start = comma + 1;
  }
  return out;
}

// Accepts a plain number or a percentage; `full` is what 100% means.
bool ParseColorNumber(const std::string& tok, double full, double* out) {
  if (tok.empty()) return false;
  std::string t = tok;
  bool pct = false;
  if (t.back() == '%') { pct = true; t.pop_back(); t = Trim(t); }
  char* end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0') return false;
  *out = pct ? v / 100.0 * full : v;
  return true;
}

// The CSS colour forms a GL style actually uses. Deliberately NOT the full
// named-colour table: three names cover what the reference styles use, and an
// unknown one is a load failure rather than a silent black.
bool ParseColor(const std::string& raw, FvColor* out) {
  const std::string s = Trim(raw);
  if (s.empty()) return false;

  if (s[0] == '#') {
    const std::string h = s.substr(1);
    auto nib = [&h](size_t i) { return HexDigit(h[i]); };
    if (h.size() == 3 || h.size() == 4) {
      for (char c : h) if (HexDigit(c) < 0) return false;
      out->r = static_cast<unsigned char>(nib(0) * 17);
      out->g = static_cast<unsigned char>(nib(1) * 17);
      out->b = static_cast<unsigned char>(nib(2) * 17);
      out->a = h.size() == 4 ? static_cast<unsigned char>(nib(3) * 17) : 255;
      return true;
    }
    if (h.size() == 6 || h.size() == 8) {
      for (char c : h) if (HexDigit(c) < 0) return false;
      out->r = static_cast<unsigned char>(nib(0) * 16 + nib(1));
      out->g = static_cast<unsigned char>(nib(2) * 16 + nib(3));
      out->b = static_cast<unsigned char>(nib(4) * 16 + nib(5));
      out->a = h.size() == 8
                   ? static_cast<unsigned char>(nib(6) * 16 + nib(7))
                   : 255;
      return true;
    }
    return false;
  }

  const size_t open = s.find('(');
  if (open != std::string::npos && s.back() == ')') {
    const std::string fn = Trim(s.substr(0, open));
    const std::vector<std::string> args =
        SplitArgs(s.substr(open + 1, s.size() - open - 2));
    if (fn == "rgb" || fn == "rgba") {
      if (args.size() != (fn == "rgb" ? 3u : 4u)) return false;
      double r, g, b, a = 1.0;
      if (!ParseColorNumber(args[0], 255.0, &r)) return false;
      if (!ParseColorNumber(args[1], 255.0, &g)) return false;
      if (!ParseColorNumber(args[2], 255.0, &b)) return false;
      if (args.size() == 4 && !ParseColorNumber(args[3], 1.0, &a)) return false;
      out->r = ClampByte(r);
      out->g = ClampByte(g);
      out->b = ClampByte(b);
      out->a = ClampByte(a * 255.0);
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

  if (s == "white")       { *out = FvColor{255, 255, 255, 255}; return true; }
  if (s == "black")       { *out = FvColor{0, 0, 0, 255};       return true; }
  if (s == "transparent") { *out = FvColor{0, 0, 0, 0};         return true; }
  return false;
}

// ---------------------------------------------------------------------------
// Zoom functions
// ---------------------------------------------------------------------------

// A number that may vary with zoom. `set` distinguishes "the style said 0"
// from "the style said nothing", which matters for line-width (0 is legal and
// means invisible) and for opacity (absent means 1).
struct NumberFn {
  bool set = false;
  double constant = 0.0;
  double base = 1.0;
  std::vector<std::pair<double, double>> stops;

  double At(double zoom) const {
    if (stops.empty()) return constant;
    if (zoom <= stops.front().first) return stops.front().second;
    if (zoom >= stops.back().first) return stops.back().second;
    size_t i = 0;
    while (i + 1 < stops.size() && stops[i + 1].first <= zoom) ++i;
    const double z0 = stops[i].first, v0 = stops[i].second;
    const double z1 = stops[i + 1].first, v1 = stops[i + 1].second;
    if (!(z1 > z0)) return v0;
    // The GL spec's exponential interpolation: base 1 is linear, base > 1
    // weights the change toward the higher zoom.
    double t = (zoom - z0) / (z1 - z0);
    if (base != 1.0 && base > 0.0) {
      const double d = z1 - z0;
      t = (std::pow(base, zoom - z0) - 1.0) / (std::pow(base, d) - 1.0);
    }
    return v0 + t * (v1 - v0);
  }
};

// A colour that may vary with zoom. STEPPED, not interpolated — see the
// header's declared deviations.
struct ColorFn {
  bool set = false;
  FvColor constant;
  std::vector<std::pair<double, FvColor>> stops;

  FvColor At(double zoom) const {
    if (stops.empty()) return constant;
    size_t i = 0;
    while (i + 1 < stops.size() && stops[i + 1].first <= zoom) ++i;
    return stops[i].second;
  }
};

// ---------------------------------------------------------------------------
// Style layers
// ---------------------------------------------------------------------------

struct StyleLayer {
  OsmStyleLayerInfo info;
  Predicate filter;
  bool has_filter = false;

  ColorFn color;          // fill-color / line-color / circle-color / text-color
  NumberFn opacity;       // fill- / line- / text-opacity
  ColorFn outline_color;  // fill-outline-color
  NumberFn width;         // line-width / circle-radius / text-size
  std::vector<double> dash;  // line-dasharray, in line-width units
  std::string text_field;    // "{name:latin}"-style token template
  std::string icon_image;    // recorded, never drawn
  ColorFn halo_color;        // text-halo-color
  NumberFn halo_width;       // text-halo-width, CSS px
  // symbol-placement / symbol-spacing / text-max-angle / text-offset[1],
  // i.e. everything that makes a road name follow its road. Constants only,
  // like every other layout property here.
  bool along_path = false;
  double spacing = 250.0;    // GL default, in px at the style's own sizes
  double max_angle = 45.0;   // GL default
  double offset_em = 0.0;    // text-offset is in EMs; +y in GL is DOWN
};

// `{name:latin} {ref}` -> the feature's values, with a token that resolves to
// nothing dropping out. Returns false when the template HAS tokens and none of
// them resolved, which is how a blank label is skipped instead of drawn; a
// template of pure literal text (legal, and used for one-off annotations) is
// always kept.
bool ExpandTokens(const std::string& tmpl, const VectorFeature& f,
                  std::string* out) {
  out->clear();
  bool any = false;
  bool has_token = false;
  for (size_t i = 0; i < tmpl.size();) {
    if (tmpl[i] == '{') {
      const size_t close = tmpl.find('}', i + 1);
      if (close == std::string::npos) {  // unbalanced: literal, like MapLibre
        out->append(tmpl.substr(i));
        break;
      }
      has_token = true;
      const std::string key = tmpl.substr(i + 1, close - i - 1);
      const std::string* v = f.Attribute(key);
      if (v != nullptr && !v->empty()) {
        out->append(*v);
        any = true;
      }
      i = close + 1;
    } else {
      out->push_back(tmpl[i]);
      ++i;
    }
  }
  *out = Trim(*out);
  return (any || !has_token) && !out->empty();
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct OsmStyleEngine::Impl {
  std::string name;
  std::vector<StyleLayer> layers;
  std::vector<OsmStyleLayerInfo> infos;

  // Background layers are not features; kept aside for background().
  bool has_background = false;
  ColorFn background_color;
  NumberFn background_opacity;

  double ref_lat = 0.0;
  double mm_per_pixel = kNativeDisplayMmPerPixel;
  double zoom_override = -1.0;
  double scaleless_zoom = 14.0;

  std::map<std::string, size_t> ignored_icons;
  std::vector<bool> layer_drew;
  size_t empty_labels = 0;
  size_t ignored_halo_blur = 0;  // style layers carrying a text-halo-blur

  // source-layer -> indices into `layers`, in style order. The dispatch: a
  // feature only ever visits the style layers aimed at its own MVT layer.
  std::unordered_map<std::string, std::vector<size_t>> by_source_layer;

  void Clear() {
    name.clear();
    layers.clear();
    infos.clear();
    has_background = false;
    background_color = ColorFn();
    background_opacity = NumberFn();
    ignored_icons.clear();
    layer_drew.clear();
    empty_labels = 0;
    ignored_halo_blur = 0;
    by_source_layer.clear();
  }
};

namespace {

// Every load failure funnels through here so the message always names the
// layer and the property — an unsupported style is a data fact a human has to
// act on, and "unsupported expression" alone is useless.
Status Reject(const std::string& layer_id, const std::string& what) {
  return Status::Error(kUnsupported, layer_id.empty()
                                         ? what
                                         : "style layer '" + layer_id +
                                               "': " + what);
}

std::string JsonToString(const Json& v) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
  if (v.is_number_integer()) return std::to_string(v.get<long long>());
  if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
  if (v.is_number_float()) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v.get<double>());
    return std::string(buf);
  }
  if (v.is_null()) return std::string();
  return std::string();
}

// --- filters ---------------------------------------------------------------
//
// LEGACY syntax only. The tell is the first element: a legacy filter's head is
// one of the fixed operator words below and its second element is a plain
// string key. An expression's head is any other word ("get", "match", "case",
// "coalesce", …) or its arguments are themselves arrays — both are rejected
// here rather than half-understood.

bool IsLegacyOp(const std::string& op) {
  static const char* kOps[] = {"==", "!=", "<",  "<=", ">",   ">=",
                               "in", "!in", "has", "!has", "all", "any",
                               "none"};
  for (const char* o : kOps) if (op == o) return true;
  return false;
}

Status ParseFilter(const Json& j, const std::string& layer_id, Predicate* out);

Status ParseFilterList(const Json& j, const std::string& layer_id, size_t from,
                       std::vector<Predicate>* kids) {
  for (size_t i = from; i < j.size(); ++i) {
    Predicate k;
    Status s = ParseFilter(j[i], layer_id, &k);
    if (!s.ok()) return s;
    kids->push_back(std::move(k));
  }
  return Status::Ok();
}

Status ParseFilter(const Json& j, const std::string& layer_id, Predicate* out) {
  if (j.is_boolean()) {
    *out = j.get<bool>() ? Predicate::Always() : Predicate::Never();
    return Status::Ok();
  }
  if (!j.is_array() || j.empty() || !j[0].is_string())
    return Reject(layer_id, "filter is not a legacy filter array");

  const std::string op = j[0].get<std::string>();
  if (!IsLegacyOp(op))
    return Reject(layer_id,
                  "filter operator '" + op +
                      "' is an expression; only the legacy filter syntax is "
                      "supported (==, !=, <, <=, >, >=, in, !in, has, !has, "
                      "all, any, none)");

  if (op == "all" || op == "any" || op == "none") {
    std::vector<Predicate> kids;
    Status s = ParseFilterList(j, layer_id, 1, &kids);
    if (!s.ok()) return s;
    if (kids.empty()) {
      // ["all"] is vacuously true, ["any"] vacuously false, ["none"] true.
      *out = (op == "any") ? Predicate::Never() : Predicate::Always();
      return Status::Ok();
    }
    if (op == "all") *out = Predicate::And(std::move(kids));
    else if (op == "any") *out = Predicate::Or(std::move(kids));
    else *out = Predicate::Not(Predicate::Or(std::move(kids)));
    return Status::Ok();
  }

  if (j.size() < 2 || !j[1].is_string())
    return Reject(layer_id, "filter '" + op + "' needs a string key");
  const std::string key = j[1].get<std::string>();
  if (key == "$id")
    return Reject(layer_id,
                  "filter on $id: the vector seam does not carry a feature id "
                  "a style can compare against");
  if (!key.empty() && key[0] == '$' && key != "$type")
    return Reject(layer_id, "filter on unsupported special key '" + key + "'");

  if (op == "has") { *out = Predicate::Exists(key); return Status::Ok(); }
  if (op == "!has") { *out = Predicate::Missing(key); return Status::Ok(); }

  if (op == "in" || op == "!in") {
    std::vector<std::string> vals;
    for (size_t i = 2; i < j.size(); ++i) {
      if (j[i].is_array() || j[i].is_object())
        return Reject(layer_id, "filter '" + op + "' takes literal values");
      vals.push_back(JsonToString(j[i]));
    }
    if (op == "in") {
      *out = Predicate::In(key, std::move(vals));
    } else {
      // MapLibre's !in is true for a MISSING tag; fvkit's kNotIn requires the
      // attribute to be present (rules.h: "a missing attribute makes every
      // comparison false"). The two are reconciled here, once, rather than by
      // loosening the shared predicate for one product.
      *out = Predicate::Or({Predicate::Missing(key),
                            Predicate::In(key, std::move(vals), true)});
    }
    return Status::Ok();
  }

  if (j.size() != 3)
    return Reject(layer_id, "filter '" + op + "' takes exactly one value");
  if (j[2].is_array() || j[2].is_object())
    return Reject(layer_id, "filter '" + op + "' takes a literal value");
  const std::string val = JsonToString(j[2]);

  if (op == "==") {
    *out = Predicate::Compare(PredicateOp::kEqual, key, val);
  } else if (op == "!=") {
    // Same reconciliation as !in above.
    *out = Predicate::Or(
        {Predicate::Missing(key),
         Predicate::Compare(PredicateOp::kNotEqual, key, val)});
  } else if (op == "<") {
    *out = Predicate::Compare(PredicateOp::kLess, key, val);
  } else if (op == "<=") {
    *out = Predicate::Compare(PredicateOp::kLessEqual, key, val);
  } else if (op == ">") {
    *out = Predicate::Compare(PredicateOp::kGreater, key, val);
  } else {
    *out = Predicate::Compare(PredicateOp::kGreaterEqual, key, val);
  }
  return Status::Ok();
}

// --- paint / layout values -------------------------------------------------

Status ParseNumber(const Json& j, const std::string& layer_id,
                   const std::string& prop, NumberFn* out) {
  out->set = true;
  if (j.is_number()) { out->constant = j.get<double>(); return Status::Ok(); }
  if (j.is_array())
    return Reject(layer_id, prop + " is an expression; only a constant or a "
                                   "{base, stops} zoom function is supported");
  if (!j.is_object() || !j.contains("stops"))
    return Reject(layer_id, prop + " is not a number or a zoom function");
  if (j.contains("property"))
    return Reject(layer_id,
                  prop + " is a data-driven (per-feature) function; only zoom "
                         "functions are supported");
  if (j.contains("base")) {
    if (!j["base"].is_number())
      return Reject(layer_id, prop + ": 'base' is not a number");
    out->base = j["base"].get<double>();
  }
  const Json& st = j["stops"];
  if (!st.is_array() || st.empty())
    return Reject(layer_id, prop + ": 'stops' is not a non-empty array");
  for (const Json& s : st) {
    if (!s.is_array() || s.size() != 2 || !s[0].is_number() ||
        !s[1].is_number())
      return Reject(layer_id, prop + ": a stop is not [zoom, number]");
    out->stops.emplace_back(s[0].get<double>(), s[1].get<double>());
  }
  std::sort(out->stops.begin(), out->stops.end(),
            [](const std::pair<double, double>& a,
               const std::pair<double, double>& b) {
              return a.first < b.first;
            });
  return Status::Ok();
}

Status ParseColorValue(const Json& j, const std::string& layer_id,
                       const std::string& prop, ColorFn* out) {
  out->set = true;
  if (j.is_string()) {
    if (!ParseColor(j.get<std::string>(), &out->constant))
      return Reject(layer_id,
                    prop + ": unrecognized colour '" + j.get<std::string>() +
                        "' (hex, rgb(), rgba(), hsl(), hsla(), white, black "
                        "and transparent are supported)");
    return Status::Ok();
  }
  if (j.is_array())
    return Reject(layer_id, prop + " is an expression; only a constant or a "
                                   "{stops} zoom function is supported");
  if (!j.is_object() || !j.contains("stops"))
    return Reject(layer_id, prop + " is not a colour or a zoom function");
  if (j.contains("property"))
    return Reject(layer_id,
                  prop + " is a data-driven (per-feature) function; only zoom "
                         "functions are supported");
  const Json& st = j["stops"];
  if (!st.is_array() || st.empty())
    return Reject(layer_id, prop + ": 'stops' is not a non-empty array");
  for (const Json& s : st) {
    if (!s.is_array() || s.size() != 2 || !s[0].is_number() ||
        !s[1].is_string())
      return Reject(layer_id, prop + ": a stop is not [zoom, colour]");
    FvColor c;
    if (!ParseColor(s[1].get<std::string>(), &c))
      return Reject(layer_id, prop + ": unrecognized colour '" +
                                  s[1].get<std::string>() + "'");
    out->stops.emplace_back(s[0].get<double>(), c);
  }
  std::sort(out->stops.begin(), out->stops.end(),
            [](const std::pair<double, FvColor>& a,
               const std::pair<double, FvColor>& b) {
              return a.first < b.first;
            });
  return Status::Ok();
}

}  // namespace

// ---------------------------------------------------------------------------

OsmStyleEngine::OsmStyleEngine() : impl_(new Impl) {}
OsmStyleEngine::~OsmStyleEngine() = default;

void OsmStyleEngine::SetReferenceLatitude(double lat) {
  if (impl_->ref_lat != lat) {
    impl_->ref_lat = lat;
    BumpStyleEpoch();
  }
}
double OsmStyleEngine::reference_latitude() const { return impl_->ref_lat; }

void OsmStyleEngine::SetDisplayMmPerPixel(double mm_per_pixel) {
  if (!(mm_per_pixel > 0.0)) return;
  if (impl_->mm_per_pixel != mm_per_pixel) {
    impl_->mm_per_pixel = mm_per_pixel;
    BumpStyleEpoch();
  }
}
double OsmStyleEngine::display_mm_per_pixel() const {
  return impl_->mm_per_pixel;
}

void OsmStyleEngine::SetZoomOverride(double z) {
  if (impl_->zoom_override != z) {
    impl_->zoom_override = z;
    BumpStyleEpoch();
  }
}
double OsmStyleEngine::zoom_override() const { return impl_->zoom_override; }

void OsmStyleEngine::SetScalelessZoom(double z) {
  if (impl_->scaleless_zoom != z) {
    impl_->scaleless_zoom = z;
    BumpStyleEpoch();
  }
}
double OsmStyleEngine::scaleless_zoom() const { return impl_->scaleless_zoom; }

double OsmStyleEngine::ZoomForScale(double scale_denominator) const {
  if (impl_->zoom_override >= 0.0) return impl_->zoom_override;
  if (!(scale_denominator > 0.0)) return impl_->scaleless_zoom;
  const double z = webmerc::ZoomForScaleExact(scale_denominator, impl_->ref_lat,
                                              impl_->mm_per_pixel);
  return std::isfinite(z) ? z : impl_->scaleless_zoom;
}

const std::vector<OsmStyleLayerInfo>& OsmStyleEngine::layers() const {
  return impl_->infos;
}
const std::string& OsmStyleEngine::style_name() const { return impl_->name; }

bool OsmStyleEngine::background(double scale_denominator, FvColor* out) const {
  if (!impl_->has_background || out == nullptr) return false;
  const double zoom = ZoomForScale(scale_denominator);
  FvColor c = impl_->background_color.At(zoom);
  if (impl_->background_opacity.set) {
    c.a = ClampByte(c.a * std::max(0.0, std::min(1.0,
                                                 impl_->background_opacity.At(zoom))));
  }
  *out = c;
  return true;
}

const std::map<std::string, size_t>& OsmStyleEngine::ignored_icons() const {
  return impl_->ignored_icons;
}
size_t OsmStyleEngine::layers_that_drew() const {
  size_t n = 0;
  for (bool b : impl_->layer_drew) if (b) ++n;
  return n;
}
size_t OsmStyleEngine::empty_labels() const { return impl_->empty_labels; }
size_t OsmStyleEngine::ignored_halo_blur() const {
  return impl_->ignored_halo_blur;
}
void OsmStyleEngine::ResetDiagnostics() {
  impl_->ignored_icons.clear();
  impl_->empty_labels = 0;
  std::fill(impl_->layer_drew.begin(), impl_->layer_drew.end(), false);
  ResetUnresolvedSymbols();
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

Status OsmStyleEngine::LoadFile(const std::string& path, std::string* error) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) {
    Status s = Status::Error(kIoError, "cannot open style file: " + path);
    if (error != nullptr) *error = s.message;
    return s;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return LoadText(ss.str(), error);
}

Status OsmStyleEngine::LoadText(const std::string& json_text,
                                std::string* error) {
  // All-or-nothing: build into a fresh Impl and only swap it in on success, so
  // a rejected style leaves the previous one (or the closed engine) intact.
  Impl fresh;
  fresh.ref_lat = impl_->ref_lat;
  fresh.mm_per_pixel = impl_->mm_per_pixel;
  fresh.zoom_override = impl_->zoom_override;
  fresh.scaleless_zoom = impl_->scaleless_zoom;

  auto fail = [&](const Status& s) {
    if (error != nullptr) *error = s.message;
    return s;
  };

  Json root;
  try {
    root = Json::parse(json_text);
  } catch (const std::exception& e) {
    return fail(Status::Error(kInvalidArg,
                              std::string("style is not valid JSON: ") +
                                  e.what()));
  }
  if (!root.is_object())
    return fail(Status::Error(kInvalidArg, "style root is not an object"));
  if (root.contains("version") && root["version"].is_number() &&
      root["version"].get<int>() != 8) {
    return fail(Status::Error(
        kUnsupported, "style version " +
                          std::to_string(root["version"].get<int>()) +
                          " is not the GL style spec version 8"));
  }
  if (root.contains("name") && root["name"].is_string())
    fresh.name = root["name"].get<std::string>();
  if (!root.contains("layers") || !root["layers"].is_array())
    return fail(Status::Error(kInvalidArg, "style has no 'layers' array"));

  const Json& jlayers = root["layers"];
  int priority = 0;
  for (const Json& jl : jlayers) {
    if (!jl.is_object())
      return fail(Status::Error(kInvalidArg, "a style layer is not an object"));
    const std::string id =
        jl.contains("id") && jl["id"].is_string() ? jl["id"].get<std::string>()
                                                  : std::string("<unnamed>");
    if (!jl.contains("type") || !jl["type"].is_string())
      return fail(Reject(id, "layer has no 'type'"));
    const std::string type = jl["type"].get<std::string>();

    StyleLayer L;
    L.info.id = id;
    L.info.priority = priority;

    if (type == "background") L.info.type = OsmStyleLayerType::kBackground;
    else if (type == "fill")  L.info.type = OsmStyleLayerType::kFill;
    else if (type == "line")  L.info.type = OsmStyleLayerType::kLine;
    else if (type == "symbol") L.info.type = OsmStyleLayerType::kSymbol;
    else if (type == "circle") L.info.type = OsmStyleLayerType::kCircle;
    else
      return fail(Reject(id, "layer type '" + type +
                                 "' is outside the supported subset "
                                 "(background, fill, line, symbol, circle)"));

    if (jl.contains("minzoom")) {
      if (!jl["minzoom"].is_number()) return fail(Reject(id, "minzoom is not a number"));
      L.info.minzoom = jl["minzoom"].get<double>();
    }
    if (jl.contains("maxzoom")) {
      if (!jl["maxzoom"].is_number()) return fail(Reject(id, "maxzoom is not a number"));
      L.info.maxzoom = jl["maxzoom"].get<double>();
    }
    if (L.info.maxzoom < L.info.minzoom)
      return fail(Reject(id, "maxzoom is below minzoom"));

    // minzoom/maxzoom -> ScaleBand. Zoom grows as scale gets FINER, so the
    // bounds cross over: the layer's minzoom is its COARSEST scale (largest
    // denominator) and its maxzoom is its finest. Both edges are inclusive
    // here where MapLibre's maxzoom is exclusive — a measure-zero difference
    // at exactly the boundary denominator, called out so nobody rediscovers it.
    if (L.info.minzoom > 0.0) {
      L.info.band.max_denom = webmerc::ScaleForZoomExact(
          L.info.minzoom, fresh.ref_lat, fresh.mm_per_pixel);
    }
    if (L.info.maxzoom < 24.0) {
      L.info.band.min_denom = webmerc::ScaleForZoomExact(
          L.info.maxzoom, fresh.ref_lat, fresh.mm_per_pixel);
    }

    if (jl.contains("source-layer")) {
      if (!jl["source-layer"].is_string())
        return fail(Reject(id, "source-layer is not a string"));
      L.info.source_layer = jl["source-layer"].get<std::string>();
    }
    if (L.info.type != OsmStyleLayerType::kBackground &&
        L.info.source_layer.empty())
      return fail(Reject(id, "layer has no 'source-layer'"));

    if (jl.contains("filter")) {
      Status s = ParseFilter(jl["filter"], id, &L.filter);
      if (!s.ok()) return fail(s);
      L.has_filter = !L.filter.is_always();
      L.info.filter = L.has_filter ? L.filter.ToText() : std::string();
    }

    const Json empty = Json::object();
    const Json& paint = jl.contains("paint") && jl["paint"].is_object()
                            ? jl["paint"] : empty;
    const Json& layout = jl.contains("layout") && jl["layout"].is_object()
                             ? jl["layout"] : empty;

    if (layout.contains("visibility") && layout["visibility"].is_string() &&
        layout["visibility"].get<std::string>() == "none") {
      L.info.visible = false;
    }

    auto num = [&](const Json& obj, const char* key, NumberFn* dst) -> Status {
      if (!obj.contains(key)) return Status::Ok();
      return ParseNumber(obj[key], id, key, dst);
    };
    auto col = [&](const Json& obj, const char* key, ColorFn* dst) -> Status {
      if (!obj.contains(key)) return Status::Ok();
      return ParseColorValue(obj[key], id, key, dst);
    };

    Status s = Status::Ok();
    switch (L.info.type) {
      case OsmStyleLayerType::kBackground:
        if (!(s = col(paint, "background-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "background-opacity", &L.opacity)).ok()) return fail(s);
        if (paint.contains("background-pattern"))
          return fail(Reject(id,
                             "background-pattern needs a sprite sheet, which "
                             "this port does not load"));
        break;
      case OsmStyleLayerType::kFill:
        if (!(s = col(paint, "fill-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "fill-opacity", &L.opacity)).ok()) return fail(s);
        if (!(s = col(paint, "fill-outline-color", &L.outline_color)).ok())
          return fail(s);
        if (paint.contains("fill-pattern"))
          return fail(Reject(id,
                             "fill-pattern needs a sprite sheet, which this "
                             "port does not load"));
        break;
      case OsmStyleLayerType::kLine:
        if (!(s = col(paint, "line-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "line-opacity", &L.opacity)).ok()) return fail(s);
        if (!(s = num(paint, "line-width", &L.width)).ok()) return fail(s);
        if (paint.contains("line-pattern"))
          return fail(Reject(id,
                             "line-pattern needs a sprite sheet, which this "
                             "port does not load"));
        if (paint.contains("line-dasharray")) {
          const Json& d = paint["line-dasharray"];
          if (!d.is_array() || d.empty())
            return fail(Reject(id, "line-dasharray is not a non-empty array"));
          for (const Json& e : d) {
            if (!e.is_number())
              return fail(Reject(id,
                                 "line-dasharray is a function or expression; "
                                 "only a constant array is supported"));
            L.dash.push_back(e.get<double>());
          }
        }
        break;
      case OsmStyleLayerType::kSymbol:
        if (!(s = col(paint, "text-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "text-opacity", &L.opacity)).ok()) return fail(s);
        if (!(s = num(layout, "text-size", &L.width)).ok()) return fail(s);
        if (!(s = col(paint, "text-halo-color", &L.halo_color)).ok())
          return fail(s);
        if (!(s = num(paint, "text-halo-width", &L.halo_width)).ok())
          return fail(s);
        // `text-halo-blur` is the fourth thing this loader ignores by design
        // (see the header): the halo is a stamped dilation, and a blur radius
        // has no meaning without coverage to blur. Counted, not rejected —
        // every OpenMapTiles-derived style carries it next to a width that IS
        // honoured, and rejecting it would fail the whole style over the one
        // property whose absence is least visible.
        if (paint.contains("text-halo-blur")) {
          if (!paint["text-halo-blur"].is_number() &&
              !paint["text-halo-blur"].is_object())
            return fail(Reject(id, "text-halo-blur is not a constant or a "
                                   "zoom function"));
          ++fresh.ignored_halo_blur;
        }
        if (layout.contains("icon-image")) {
          if (!layout["icon-image"].is_string())
            return fail(Reject(id, "icon-image is a function or expression"));
          L.icon_image = layout["icon-image"].get<std::string>();
        }
        if (layout.contains("text-field")) {
          if (!layout["text-field"].is_string())
            return fail(Reject(id,
                               "text-field is an expression; only the {token} "
                               "string form is supported"));
          L.text_field = layout["text-field"].get<std::string>();
        }
        if (layout.contains("symbol-placement")) {
          if (!layout["symbol-placement"].is_string())
            return fail(Reject(id, "symbol-placement is not a constant"));
          const std::string sp = layout["symbol-placement"].get<std::string>();
          // "line-center" is "line" with one run per feature, which is what a
          // zero spacing means to the placer.
          if (sp == "line" || sp == "line-center") {
            L.along_path = true;
            if (sp == "line-center") L.spacing = 0.0;
          } else if (sp != "point") {
            return fail(Reject(id, "unknown symbol-placement: " + sp));
          }
        }
        if (layout.contains("symbol-spacing")) {
          if (!layout["symbol-spacing"].is_number())
            return fail(Reject(id, "symbol-spacing is not a constant"));
          L.spacing = layout["symbol-spacing"].get<double>();
        }
        if (layout.contains("text-max-angle")) {
          if (!layout["text-max-angle"].is_number())
            return fail(Reject(id, "text-max-angle is not a constant"));
          L.max_angle = layout["text-max-angle"].get<double>();
        }
        if (layout.contains("text-offset")) {
          const Json& o = layout["text-offset"];
          if (!o.is_array() || o.size() != 2 || !o[1].is_number())
            return fail(Reject(id, "text-offset is not a constant [x, y]"));
          // Only the perpendicular component survives: the placer offsets
          // across the path, and a GL along-axis offset has no meaning once
          // the run is centred on the geometry. GL's +y is DOWN, the placer's
          // positive offset is LEFT of travel (up on an eastward road), hence
          // the negation.
          L.offset_em = -o[1].get<double>();
        }
        break;
      case OsmStyleLayerType::kCircle:
        if (!(s = col(paint, "circle-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "circle-opacity", &L.opacity)).ok()) return fail(s);
        if (!(s = num(paint, "circle-radius", &L.width)).ok()) return fail(s);
        break;
    }

    if (L.info.type == OsmStyleLayerType::kBackground) {
      // Only the first background layer can win; a style with two is telling
      // us something we do not model, so say so rather than pick one.
      if (fresh.has_background)
        return fail(Reject(id, "a second 'background' layer; the canvas has "
                               "one clear colour"));
      fresh.has_background = true;
      fresh.background_color = L.color;
      fresh.background_opacity = L.opacity;
      fresh.infos.push_back(L.info);
      ++priority;
      continue;
    }

    fresh.by_source_layer[L.info.source_layer].push_back(fresh.layers.size());
    fresh.infos.push_back(L.info);
    fresh.layers.push_back(std::move(L));
    ++priority;
  }

  fresh.layer_drew.assign(fresh.layers.size(), false);
  *impl_ = std::move(fresh);
  ClearSymbolCache();
  set_open(true);  // bumps the style epoch
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------

namespace {

FvColor WithOpacity(FvColor c, const NumberFn& op, double zoom) {
  if (!op.set) return c;
  const double a = std::max(0.0, std::min(1.0, op.At(zoom)));
  c.a = ClampByte(c.a * a);
  return c;
}

const char* GeometryToken(VectorGeometryType t) {
  switch (t) {
    case VectorGeometryType::kPoint: return "Point";
    case VectorGeometryType::kLine:  return "LineString";
    case VectorGeometryType::kArea:  return "Polygon";
  }
  return "Point";
}

// "circle:<radius in himetric>:<rrggbbaa>" — self-describing, so LoadSymbol
// can build the display list from the id alone and the base class's cache
// dedupes every feature that shares a radius and colour.
std::string CircleSymbolId(int radius_himetric, const FvColor& c) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "circle:%d:%02X%02X%02X%02X",
                radius_himetric, c.r, c.g, c.b, c.a);
  return std::string(buf);
}

bool ParseCircleSymbolId(const std::string& id, int* radius, FvColor* c) {
  if (id.compare(0, 7, "circle:") != 0) return false;
  const size_t colon = id.find(':', 7);
  if (colon == std::string::npos) return false;
  const std::string r = id.substr(7, colon - 7);
  const std::string hex = id.substr(colon + 1);
  if (r.empty() || hex.size() != 8) return false;
  char* end = nullptr;
  const long rv = std::strtol(r.c_str(), &end, 10);
  if (end == r.c_str() || *end != '\0' || rv <= 0) return false;
  for (char ch : hex) if (HexDigit(ch) < 0) return false;
  auto byte = [&hex](size_t i) {
    return static_cast<unsigned char>(HexDigit(hex[i]) * 16 + HexDigit(hex[i + 1]));
  };
  *radius = static_cast<int>(rv);
  c->r = byte(0); c->g = byte(2); c->b = byte(4); c->a = byte(6);
  return true;
}

}  // namespace

Status OsmStyleEngine::StyleFeature(const VectorFeature& f,
                                    const StyleContext& ctx,
                                    const StylePass& pass,
                                    std::vector<StyleResult>* out) {
  auto it = impl_->by_source_layer.find(f.layer);
  if (it == impl_->by_source_layer.end()) return Status::Ok();

  const double zoom = ZoomForScale(ctx.scale_denominator);
  // CSS pixels at 96 dpi -> device pixels. Never below 1: a hairline that
  // rounds to zero is a road that vanishes, which is worse than one a pixel
  // too wide.
  const double dpi_scale = ctx.device_dpi > 0.0
                               ? ctx.device_dpi / kStyleNominalDpi : 1.0;
  const std::string geom_token = GeometryToken(f.type);

  const Predicate::Lookup lookup =
      [&f, &geom_token](const std::string& key) -> const std::string* {
    if (key == "$type") return &geom_token;
    return f.Attribute(key);
  };

  for (size_t idx : it->second) {
    const StyleLayer& L = impl_->layers[idx];
    if (!L.info.visible) continue;
    if (!L.info.band.Contains(ctx.scale_denominator)) continue;
    if (L.has_filter && !L.filter.Evaluate(lookup)) continue;

    StyleResultBuilder b(pass.PriorityOr(L.info.priority), out);

    switch (L.info.type) {
      case OsmStyleLayerType::kFill: {
        if (L.color.set) {
          FillStyle& fs = b.Fill();
          fs.valid = true;
          fs.brush.color = WithOpacity(L.color.At(zoom), L.opacity, zoom);
        }
        if (L.outline_color.set) {
          StrokeStyle& ss = b.Stroke();
          ss.valid = true;
          ss.pen.color = WithOpacity(L.outline_color.At(zoom), L.opacity, zoom);
          ss.pen.width = 1;
        }
        break;
      }
      case OsmStyleLayerType::kLine: {
        if (!L.color.set) break;
        // line-width defaults to 1 CSS px when the style omits it.
        const double w_css = L.width.set ? L.width.At(zoom) : 1.0;
        if (!(w_css > 0.0)) break;
        const int w_px = std::max(1, static_cast<int>(std::lround(w_css * dpi_scale)));
        StrokeStyle& ss = b.Stroke();
        ss.valid = true;
        ss.pen.color = WithOpacity(L.color.At(zoom), L.opacity, zoom);
        ss.pen.width = w_px;
        if (!L.dash.empty()) {
          // GL dash runs are in LINE-WIDTH units, not pixels.
          for (double d : L.dash)
            ss.pen.dash.push_back(std::max(1, static_cast<int>(std::lround(d * w_px))));
          if (ss.pen.dash.size() % 2 != 0)  // Pen wants on/off pairs
            ss.pen.dash.push_back(ss.pen.dash.back());
        }
        break;
      }
      case OsmStyleLayerType::kCircle: {
        if (!L.color.set) break;
        const double r_css = L.width.set ? L.width.At(zoom) : 5.0;
        if (!(r_css > 0.0)) break;
        const int r_hm = std::max(
            1, static_cast<int>(std::lround(r_css * dpi_scale * kHimetricPerPixel)));
        PointSymbolStyle& sy = b.Symbol();
        sy.valid = true;
        sy.symbol_id = CircleSymbolId(
            r_hm, WithOpacity(L.color.At(zoom), L.opacity, zoom));
        sy.scale = pass.SymbolScaleOr(1.0);
        break;
      }
      case OsmStyleLayerType::kSymbol: {
        if (!L.icon_image.empty()) ++impl_->ignored_icons[L.icon_image];
        if (L.text_field.empty()) break;
        if (!pass.draw_labels) break;
        std::string text;
        if (!ExpandTokens(L.text_field, f, &text)) {
          ++impl_->empty_labels;
          break;
        }
        LabelStyle& lb = b.Label();
        lb.valid = true;
        lb.text = std::move(text);
        lb.style.size = (L.width.set ? L.width.At(zoom) : 16.0) * dpi_scale;
        lb.style.color = L.color.set
                             ? WithOpacity(L.color.At(zoom), L.opacity, zoom)
                             : FvColor{0, 0, 0, 255};
        // The halo is CSS px like every other GL paint value, so it goes
        // through the same dpi conversion the sizes and widths do. A style
        // that gives a colour and no width gets the GL default of 0, i.e.
        // nothing — the colour alone is not a request to draw an outline.
        if (L.halo_width.set) {
          lb.halo_width = L.halo_width.At(zoom) * dpi_scale;
          lb.halo_color =
              L.halo_color.set
                  ? WithOpacity(L.halo_color.At(zoom), L.opacity, zoom)
                  : FvColor{255, 255, 255, 255};
        }
        // A road name follows its road; a place name does not. The style says
        // which, and a placement of `line` on a point feature is harmless —
        // the renderer falls back to the point path for it.
        if (L.along_path) {
          lb.placement = LabelPlacement::kAlongPath;
          lb.spacing_px = L.spacing * dpi_scale;
          lb.max_angle_deg = L.max_angle;
          lb.offset_px = L.offset_em * lb.style.size;
        }
        break;
      }
      case OsmStyleLayerType::kBackground:
        break;  // not reachable: background layers are not in by_source_layer
    }

    b.Finish();
    if (b.AnythingDrawn()) impl_->layer_drew[idx] = true;
  }
  return Status::Ok();
}

bool OsmStyleEngine::LoadSymbol(const std::string& symbol_id,
                                VectorSymbol* out) {
  int r_hm = 0;
  FvColor c;
  if (!ParseCircleSymbolId(symbol_id, &r_hm, &c)) return false;

  // A filled disc, in HIMETRIC and y-UP like every other display list at this
  // seam (fvkit/vector/style.h). CGM's conjugate-diameter encoding degenerates
  // to the two axes for a circle.
  SymbolPrimitive p;
  p.type = SymbolPrimitiveType::kEllipse;
  p.center = SymbolPoint{0.0, 0.0};
  p.radius1 = SymbolPoint{static_cast<double>(r_hm), 0.0};
  p.radius2 = SymbolPoint{0.0, static_cast<double>(r_hm)};
  p.has_fill = true;
  p.fill_color = c;
  out->primitives.push_back(p);
  out->min_x = out->min_y = -static_cast<double>(r_hm);
  out->max_x = out->max_y = static_cast<double>(r_hm);
  return true;
}

}  // namespace fv
