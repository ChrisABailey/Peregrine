// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/style/maplibre_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "fvkit/geo/web_mercator.h"
#include "fvkit/canvas/geo_draw.h"  // MakeLinePreset
#include "fvkit/proj.h"  // kNativeDisplayMmPerPixel
#include "fvkit/style/expression.h"
#include "fvkit/symbol/builtin.h"
#include "fvkit/symbol/png_library.h"

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

// ---------------------------------------------------------------------------
// Zoom functions
// ---------------------------------------------------------------------------

/// A number that may vary with zoom or per feature. `set` distinguishes "the
/// style said 0" from "the style said nothing": line-width 0 is legal and
/// means invisible, and an absent opacity means 1.
struct NumberFn {
  bool set = false;
  StyleExpression expr;
  bool constant = false;
  double value = 0.0;  ///< when `constant`

  /// The value for one feature; `fallback` when the expression yields no number.
  double At(const StyleExpressionInput& in, double fallback = 0.0) const {
    if (constant) return value;
    double v = fallback;
    return expr.Evaluate(in).AsNumber(&v) ? v : fallback;
  }
};

/// A colour that may vary with zoom or per feature.
struct ColorFn {
  bool set = false;
  StyleExpression expr;
  bool constant = false;
  FvColor value;  ///< when `constant`

  /// The colour for one feature; black when the expression yields no colour,
  /// the style spec's default.
  FvColor At(const StyleExpressionInput& in) const {
    if (constant) return value;
    FvColor c{0, 0, 0, 255};
    return expr.Evaluate(in).AsColor(&c) ? c : FvColor{0, 0, 0, 255};
  }
};

/// A string-valued layout property: a `{token}` template or an expression.
struct TextFn {
  std::string tmpl;
  StyleExpression expr;

  bool empty() const { return tmpl.empty() && expr.empty(); }
};

// ---------------------------------------------------------------------------
// Style layers
// ---------------------------------------------------------------------------

struct StyleLayer {
  MapLibreLayerInfo info;
  Predicate filter;
  bool has_filter = false;
  StyleExpression filter_expr;  // an expression filter; empty for a legacy one

  ColorFn color;          // fill-color / line-color / circle-color / text-color
  NumberFn opacity;       // fill- / line- / text-opacity
  ColorFn outline_color;  // fill-outline-color
  NumberFn width;         // line-width / circle-radius / text-size
  std::vector<double> dash;  // line-dasharray, in line-width units
  TextFn text_field;         // "{name:latin}" template or an expression
  // Sprite names, both the SAME token-template form as text_field: an
  // OpenMapTiles style writes `"icon-image": "{class}"` far more often than it
  // writes a literal, and a pattern is occasionally keyed off a tag too.
  TextFn icon_image;         // symbol layers
  TextFn fill_pattern;       // fill layers
  NumberFn icon_size;        // icon-size, a multiplier on the sprite
  NumberFn icon_rotate;      // icon-rotate, degrees clockwise
  ColorFn icon_color;        // icon-color, the tint of an SDF sprite
  NumberFn icon_opacity;     // icon-opacity
  ColorFn stroke_color;      // circle-stroke-color
  NumberFn stroke_width;     // circle-stroke-width, CSS px
  TextFn shape;              // fv-circle-shape: "circle" or "square"
  TextFn line_preset;        // fv-line-dash: a fvkit line_preset name
  ColorFn halo_color;        // text-halo-color
  NumberFn halo_width;       // text-halo-width, CSS px
  // symbol-placement / symbol-spacing / text-max-angle / text-offset[1],
  // i.e. everything that makes a road name follow its road. Constants only.
  bool along_path = false;
  double spacing = 250.0;    // GL default, in px at the style's own sizes
  double max_angle = 45.0;   // GL default
  bool keep_upright = true;  // text-keep-upright, GL default
  double offset_em = 0.0;    // text-offset is in EMs; +y in GL is DOWN
  // Point placement: text-offset as GL states it (ems, +y down) and
  // text-anchor, whose GL default is "center".
  double point_dx_em = 0.0;
  double point_dy_em = 0.0;
  LabelHAlign point_halign = LabelHAlign::kCenter;
  LabelVAlign point_valign = LabelVAlign::kCenter;
};

/// Maps a GL `text-anchor` keyword to the renderer's box alignment: the
/// keyword names the side of the label box that sits on the anchor point.
/// Returns false for an unknown keyword.
bool ParseTextAnchor(const std::string& a, LabelHAlign* h, LabelVAlign* v) {
  *h = LabelHAlign::kCenter;
  *v = LabelVAlign::kCenter;
  if (a == "center") return true;
  const bool top = a.rfind("top", 0) == 0;
  const bool bottom = a.rfind("bottom", 0) == 0;
  if (top) *v = LabelVAlign::kTop;
  if (bottom) *v = LabelVAlign::kBottom;
  std::string side = a;
  if (top || bottom) {
    side = a.substr(top ? 3 : 6);
    if (side.empty()) return true;
    if (side[0] != '-') return false;
    side = side.substr(1);
  }
  if (side == "left") *h = LabelHAlign::kLeft;
  else if (side == "right") *h = LabelHAlign::kRight;
  else return false;
  return true;
}

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

/// A template's or expression's text for one feature; false when it is empty.
bool ExpandText(const TextFn& t, const VectorFeature& f,
                const StyleExpressionInput& in, std::string* out) {
  if (t.expr.empty()) return ExpandTokens(t.tmpl, f, out);
  *out = Trim(t.expr.Evaluate(in).ToString());
  return !out->empty();
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct MapLibreStyleEngine::Impl {
  std::string name;
  std::vector<StyleLayer> layers;
  std::vector<MapLibreLayerInfo> infos;

  // Background layers are not features; kept aside for background().
  bool has_background = false;
  ColorFn background_color;
  NumberFn background_opacity;

  double ref_lat = 0.0;
  double mm_per_pixel = kNativeDisplayMmPerPixel;
  double zoom_override = -1.0;
  double scaleless_zoom = 14.0;
  std::string label_font;  // empty = the canvas default

  // The sprite sheet. `sprite_url` is what the style said; `sprite_base` is
  // where that resolved on disk, empty when nothing was resolvable. The
  // library outlives every pointer it hands out, per ISymbolLibrary.
  std::string sprite_url;
  std::string sprite_base;
  bool sprite_base_is_explicit = false;  // SetSpriteBase, not derived
  bool prefer_high_dpi = false;
  std::shared_ptr<PngSymbolLibrary> sprites;
  std::shared_ptr<PngSymbolLibrary> fallback_sprites;
  // Tinted or faded copies of sprites, keyed by TintedSpriteId.
  std::map<std::string, SymbolPixmap> tinted;
  // The directory of the file LoadFile is reading, so a relative `sprite`
  // resolves beside its style. Empty for LoadText, which has no location —
  // which is exactly why SetSpriteBase exists.
  std::string load_dir;

  std::map<std::string, size_t> ignored_icons;
  std::vector<bool> layer_drew;
  size_t empty_labels = 0;
  size_t ignored_halo_blur = 0;  // style layers carrying a text-halo-blur

  // source-layer -> indices into `layers`, in style order. The dispatch: a
  // feature only ever visits the style layers aimed at its own MVT layer.
  std::unordered_map<std::string, std::vector<size_t>> by_source_layer;

  std::vector<MapLibreLegendGroup> legend;
  std::string legend_error;
  std::vector<std::string> legend_unlisted;

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
    legend.clear();
    legend_error.clear();
    legend_unlisted.clear();
    sprite_url.clear();
    sprites.reset();
    tinted.clear();
    // An EXPLICIT sprite base survives a reload: the caller set it against
    // this engine, not against the style that happens to be in it. A derived
    // one does not, because the next style resolves its own.
    if (!sprite_base_is_explicit) sprite_base.clear();
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
// A legacy filter compiles to the predicate AST; an expression filter is kept
// as a StyleExpression. IsExpressionFilter is MapLibre's own test for which
// syntax a filter is written in.

bool IsLegacyOp(const std::string& op) {
  static const char* kOps[] = {"==", "!=", "<",  "<=", ">",   ">=",
                               "in", "!in", "has", "!has", "all", "any",
                               "none"};
  for (const char* o : kOps) if (op == o) return true;
  return false;
}

/// MapLibre's isExpressionFilter: true when `j` is written in expression
/// syntax rather than the legacy filter syntax.
bool IsExpressionFilter(const Json& j) {
  if (!j.is_array() || j.empty() || !j[0].is_string()) return false;
  const std::string op = j[0].get<std::string>();
  if (op == "has")
    return j.size() >= 2 && !(j[1].is_string() &&
                              (j[1] == "$id" || j[1] == "$type"));
  if (op == "in")
    return j.size() >= 3 && (!j[1].is_string() || j[2].is_array());
  if (op == "!in" || op == "!has" || op == "none") return false;
  if (op == "==" || op == "!=" || op == ">" || op == ">=" || op == "<" ||
      op == "<=")
    return j.size() != 3 || j[1].is_array() || j[2].is_array();
  if (op == "any" || op == "all") {
    for (size_t i = 1; i < j.size(); ++i)
      if (!IsExpressionFilter(j[i]) && !j[i].is_boolean()) return false;
    return true;
  }
  return true;
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
    return Reject(layer_id, "filter operator '" + op +
                                "' mixes expression and legacy syntax");

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

/// A legacy `{base, stops, type}` zoom function as the equivalent expression:
/// exponential (the default) is `interpolate`, interval is `step`.
Status ZoomFunctionExpression(const Json& j, const std::string& layer_id,
                              const std::string& prop, bool colour,
                              Json* out) {
  if (j.contains("property"))
    return Reject(layer_id,
                  prop + " is a data-driven (per-feature) function; write it "
                         "as an expression");
  double base = 1.0;
  if (j.contains("base")) {
    if (!j["base"].is_number())
      return Reject(layer_id, prop + ": 'base' is not a number");
    base = j["base"].get<double>();
  }
  const std::string type = j.value("type", std::string("exponential"));
  if (type != "exponential" && type != "interval")
    return Reject(layer_id, prop + ": zoom function type '" + type +
                                "' is not exponential or interval");
  const Json& st = j["stops"];
  if (!st.is_array() || st.empty())
    return Reject(layer_id, prop + ": 'stops' is not a non-empty array");
  std::vector<std::pair<double, Json>> stops;
  for (const Json& s : st) {
    const bool ok = s.is_array() && s.size() == 2 && s[0].is_number() &&
                    (colour ? s[1].is_string() : s[1].is_number());
    if (!ok)
      return Reject(layer_id, prop + (colour ? ": a stop is not [zoom, colour]"
                                             : ": a stop is not [zoom, number]"));
    if (colour) {
      FvColor c;
      if (!ParseCssColor(s[1].get<std::string>(), &c))
        return Reject(layer_id, prop + ": unrecognized colour '" +
                                    s[1].get<std::string>() + "'");
    }
    stops.emplace_back(s[0].get<double>(), s[1]);
  }
  std::stable_sort(stops.begin(), stops.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  // Duplicate zooms: the expression forms need strictly ascending stops, and
  // the last stop at a zoom is the one a lookup lands on.
  std::vector<std::pair<double, Json>> unique;
  for (auto& st2 : stops) {
    if (!unique.empty() && unique.back().first == st2.first) unique.back() = st2;
    else unique.push_back(st2);
  }
  if (unique.size() == 1) {
    *out = unique[0].second;
    return Status::Ok();
  }
  Json e = Json::array();
  if (type == "interval") {
    e = Json::array({"step", Json::array({"zoom"}), unique[0].second});
    for (size_t i = 1; i < unique.size(); ++i) {
      e.push_back(unique[i].first);
      e.push_back(unique[i].second);
    }
  } else {
    e = Json::array({"interpolate", Json::array({"exponential", base}),
                     Json::array({"zoom"})});
    for (const auto& st2 : unique) {
      e.push_back(st2.first);
      e.push_back(st2.second);
    }
  }
  *out = e;
  return Status::Ok();
}

/// A paint or layout value as an expression: a constant, a legacy zoom
/// function, or an expression array.
Status ParseValueExpression(const Json& j, const std::string& layer_id,
                            const std::string& prop, bool colour,
                            StyleExpression* out) {
  Json e = j;
  if (j.is_object()) {
    if (!j.contains("stops"))
      return Reject(layer_id, prop + (colour ? " is not a colour or a zoom function"
                                             : " is not a number or a zoom function"));
    Status s = ZoomFunctionExpression(j, layer_id, prop, colour, &e);
    if (!s.ok()) return s;
  } else if (!j.is_array()) {
    if (colour && !j.is_string())
      return Reject(layer_id, prop + " is not a colour or a zoom function");
    if (!colour && !j.is_number())
      return Reject(layer_id, prop + " is not a number or a zoom function");
    FvColor c;
    if (colour && !ParseCssColor(j.get<std::string>(), &c))
      return Reject(layer_id,
                    prop + ": unrecognized colour '" + j.get<std::string>() +
                        "' (hex, rgb(), rgba(), hsl(), hsla(), white, black "
                        "and transparent are supported)");
  }
  Status s = StyleExpression::Parse(e.dump(), out);
  if (!s.ok()) return Status::Error(s.code, Reject(layer_id, prop + ": " + s.message).message);
  return Status::Ok();
}

Status ParseNumber(const Json& j, const std::string& layer_id,
                   const std::string& prop, NumberFn* out) {
  out->set = true;
  Status s = ParseValueExpression(j, layer_id, prop, false, &out->expr);
  if (!s.ok()) return s;
  out->constant = out->expr.is_constant() &&
                  out->expr.Evaluate(StyleExpressionInput{}).AsNumber(&out->value);
  return Status::Ok();
}

Status ParseColorValue(const Json& j, const std::string& layer_id,
                       const std::string& prop, ColorFn* out) {
  out->set = true;
  Status s = ParseValueExpression(j, layer_id, prop, true, &out->expr);
  if (!s.ok()) return s;
  out->constant = out->expr.is_constant() &&
                  out->expr.Evaluate(StyleExpressionInput{}).AsColor(&out->value);
  return Status::Ok();
}

/// A text-field, icon-image or fill-pattern: a `{token}` string or an expression.
Status ParseText(const Json& j, const std::string& layer_id,
                 const std::string& prop, TextFn* out) {
  if (j.is_string()) {
    out->tmpl = j.get<std::string>();
    return Status::Ok();
  }
  if (!j.is_array())
    return Reject(layer_id, prop + " is not a string or an expression");
  Status s = StyleExpression::Parse(j.dump(), &out->expr);
  if (!s.ok()) return Status::Error(s.code, Reject(layer_id, prop + ": " + s.message).message);
  return Status::Ok();
}

}  // namespace

// ---------------------------------------------------------------------------

MapLibreStyleEngine::MapLibreStyleEngine() : impl_(new Impl) {}
MapLibreStyleEngine::~MapLibreStyleEngine() = default;

void MapLibreStyleEngine::SetReferenceLatitude(double lat) {
  if (impl_->ref_lat != lat) {
    impl_->ref_lat = lat;
    BumpStyleEpoch();
  }
}
double MapLibreStyleEngine::reference_latitude() const { return impl_->ref_lat; }

void MapLibreStyleEngine::SetDisplayMmPerPixel(double mm_per_pixel) {
  if (!(mm_per_pixel > 0.0)) return;
  if (impl_->mm_per_pixel != mm_per_pixel) {
    impl_->mm_per_pixel = mm_per_pixel;
    BumpStyleEpoch();
  }
}
double MapLibreStyleEngine::display_mm_per_pixel() const {
  return impl_->mm_per_pixel;
}

void MapLibreStyleEngine::SetZoomOverride(double z) {
  if (impl_->zoom_override != z) {
    impl_->zoom_override = z;
    BumpStyleEpoch();
  }
}
double MapLibreStyleEngine::zoom_override() const { return impl_->zoom_override; }

void MapLibreStyleEngine::SetScalelessZoom(double z) {
  if (impl_->scaleless_zoom != z) {
    impl_->scaleless_zoom = z;
    BumpStyleEpoch();
  }
}
double MapLibreStyleEngine::scaleless_zoom() const { return impl_->scaleless_zoom; }

double MapLibreStyleEngine::ZoomForScale(double scale_denominator) const {
  if (impl_->zoom_override >= 0.0) return impl_->zoom_override;
  if (!(scale_denominator > 0.0)) return impl_->scaleless_zoom;
  const double z = webmerc::ZoomForScaleExact(scale_denominator, impl_->ref_lat,
                                              impl_->mm_per_pixel);
  return std::isfinite(z) ? z : impl_->scaleless_zoom;
}

const std::vector<MapLibreLayerInfo>& MapLibreStyleEngine::layers() const {
  return impl_->infos;
}
const std::string& MapLibreStyleEngine::style_name() const { return impl_->name; }

bool MapLibreStyleEngine::background(double scale_denominator, FvColor* out) const {
  if (!impl_->has_background || out == nullptr) return false;
  StyleExpressionInput in;
  in.zoom = ZoomForScale(scale_denominator);
  FvColor c = impl_->background_color.At(in);
  if (impl_->background_opacity.set) {
    c.a = ClampByte(c.a * std::max(0.0, std::min(1.0,
                                                 impl_->background_opacity.At(in, 1.0))));
  }
  *out = c;
  return true;
}

const std::vector<MapLibreLegendGroup>& MapLibreStyleEngine::legend() const {
  return impl_->legend;
}
const std::string& MapLibreStyleEngine::legend_error() const {
  return impl_->legend_error;
}
const std::vector<std::string>& MapLibreStyleEngine::legend_unlisted_layers() const {
  return impl_->legend_unlisted;
}

const std::map<std::string, size_t>& MapLibreStyleEngine::ignored_icons() const {
  return impl_->ignored_icons;
}
size_t MapLibreStyleEngine::layers_that_drew() const {
  size_t n = 0;
  for (bool b : impl_->layer_drew) if (b) ++n;
  return n;
}
std::vector<std::string> MapLibreStyleEngine::layer_ids_that_drew() const {
  std::vector<std::string> ids;
  for (size_t i = 0; i < impl_->layer_drew.size(); ++i)
    if (impl_->layer_drew[i]) ids.push_back(impl_->layers[i].info.id);
  return ids;
}
size_t MapLibreStyleEngine::empty_labels() const { return impl_->empty_labels; }
size_t MapLibreStyleEngine::ignored_halo_blur() const {
  return impl_->ignored_halo_blur;
}
void MapLibreStyleEngine::ResetDiagnostics() {
  impl_->ignored_icons.clear();
  impl_->empty_labels = 0;
  std::fill(impl_->layer_drew.begin(), impl_->layer_drew.end(), false);
  ResetUnresolvedSymbols();
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

Status MapLibreStyleEngine::LoadFile(const std::string& path, std::string* error) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) {
    Status s = Status::Error(kIoError, "cannot open style file: " + path);
    if (error != nullptr) *error = s.message;
    return s;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  // The one thing LoadText cannot know: where this style is, and therefore
  // what a relative `sprite` is relative to.
  impl_->load_dir = std::filesystem::path(path).parent_path().string();
  const Status s = LoadText(ss.str(), error);
  impl_->load_dir.clear();
  return s;
}

// --- the sprite sheet -------------------------------------------------------

void MapLibreStyleEngine::SetSpriteBase(const std::string& path_without_extension) {
  impl_->sprite_base = path_without_extension;
  impl_->sprite_base_is_explicit = !path_without_extension.empty();
}

const std::string& MapLibreStyleEngine::sprite_base() const {
  return impl_->sprite_base;
}

void MapLibreStyleEngine::SetPreferHighDpiSprites(bool on) {
  impl_->prefer_high_dpi = on;
}

const std::string& MapLibreStyleEngine::sprite_url() const {
  return impl_->sprite_url;
}

std::vector<std::string> MapLibreStyleEngine::sprite_ids() const {
  PngSymbolLibrary* sheet = Sheet();
  return sheet ? sheet->ids() : std::vector<std::string>();
}

namespace {

// GL's `sprite` is a URL without an extension; the sheet is `<it>.json` plus
// `<it>.png`. Only a LOCAL one can be honoured — nothing here fetches — so an
// http(s) or mapbox:// value resolves to nothing and the icons go unresolved.
std::string ResolveSpriteBase(const std::string& url, const std::string& dir) {
  if (url.empty()) return std::string();
  auto starts = [&](const char* p) {
    return url.compare(0, std::strlen(p), p) == 0;
  };
  if (starts("http://") || starts("https://") || starts("mapbox://"))
    return std::string();
  std::string path = starts("file://") ? url.substr(7) : url;
  std::filesystem::path p(path);
  if (p.is_relative() && !dir.empty()) p = std::filesystem::path(dir) / p;
  return p.lexically_normal().string();
}

/// "" when every key of `obj` is in `allowed`, else a message naming the first
/// one that is not. Legend keys are checked so a typo shows as an error rather
/// than as a silently missing row.
std::string UnknownKey(const Json& obj, std::initializer_list<const char*> allowed,
                       const std::string& where) {
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    bool known = false;
    for (const char* a : allowed) known = known || it.key() == a;
    if (!known) return where + ": unknown key '" + it.key() + "'";
  }
  return std::string();
}

/// Reads an object of scalar values into `out`, in key order. Numbers keep
/// the text the engine compares tile attributes against.
std::string ReadTags(const Json& obj, const std::string& where,
                     std::vector<std::pair<std::string, std::string>>* out) {
  if (!obj.is_object()) return where + " is not an object";
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    const Json& v = it.value();
    if (!(v.is_string() || v.is_number() || v.is_boolean()))
      return where + "." + it.key() + " is not a string, number or boolean";
    out->emplace_back(it.key(), JsonToString(v));
  }
  return std::string();
}

/// True when `pattern` names `id`; a trailing `*` matches any suffix.
bool LayerIdMatches(const std::string& pattern, const std::string& id) {
  if (!pattern.empty() && pattern.back() == '*')
    return id.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
  return pattern == id;
}

/// Parses `metadata["peregrine:legend"]`. Returns "" on success, else the
/// first problem found; `infos` are the style's loaded layers.
std::string ParseLegend(const Json& j, const std::vector<MapLibreLayerInfo>& infos,
                        std::vector<MapLibreLegendGroup>* groups,
                        std::vector<std::string>* unlisted) {
  const std::string root = "peregrine:legend";
  if (!j.is_object()) return root + " is not an object";
  std::string err = UnknownKey(j, {"groups", "unlisted_layers"}, root);
  if (!err.empty()) return err;
  if (!j.contains("groups") || !j["groups"].is_array())
    return root + " has no 'groups' array";

  auto reads_source_layer = [&infos](const std::string& layer) {
    for (const MapLibreLayerInfo& i : infos)
      if (i.source_layer == layer) return true;
    return false;
  };

  std::vector<std::string> ids;
  for (const Json& jg : j["groups"]) {
    const std::string gwhere = root + " group " + std::to_string(groups->size());
    if (!jg.is_object()) return gwhere + " is not an object";
    err = UnknownKey(jg, {"title", "subtitle", "entries"}, gwhere);
    if (!err.empty()) return err;
    MapLibreLegendGroup g;
    if (!jg.contains("title") || !jg["title"].is_string())
      return gwhere + " has no 'title' string";
    g.title = jg["title"].get<std::string>();
    if (jg.contains("subtitle")) {
      if (!jg["subtitle"].is_string()) return gwhere + ".subtitle is not a string";
      g.subtitle = jg["subtitle"].get<std::string>();
    }
    if (!jg.contains("entries") || !jg["entries"].is_array())
      return "group '" + g.title + "' has no 'entries' array";

    for (const Json& je : jg["entries"]) {
      const std::string ewhere =
          "group '" + g.title + "' entry " + std::to_string(g.entries.size());
      if (!je.is_object()) return ewhere + " is not an object";
      err = UnknownKey(je, {"id", "label", "detail", "specimen", "osm"}, ewhere);
      if (!err.empty()) return err;
      MapLibreLegendEntry e;
      if (!je.contains("id") || !je["id"].is_string() || je["id"].get<std::string>().empty())
        return ewhere + " has no 'id' string";
      e.id = je["id"].get<std::string>();
      const std::string where = "legend entry '" + e.id + "'";
      if (std::find(ids.begin(), ids.end(), e.id) != ids.end())
        return where + " is listed twice";
      ids.push_back(e.id);
      if (!je.contains("label") || !je["label"].is_string())
        return where + " has no 'label' string";
      e.label = je["label"].get<std::string>();
      if (je.contains("detail")) {
        if (!je["detail"].is_string()) return where + ".detail is not a string";
        e.detail = je["detail"].get<std::string>();
      }
      if (je.contains("osm")) {
        err = ReadTags(je["osm"], where + ".osm", &e.osm);
        if (!err.empty()) return err;
      }
      if (!je.contains("specimen") || !je["specimen"].is_array() ||
          je["specimen"].empty())
        return where + " has no 'specimen' array";
      for (const Json& js : je["specimen"]) {
        const std::string swhere =
            where + " specimen " + std::to_string(e.specimen.size());
        if (!js.is_object()) return swhere + " is not an object";
        err = UnknownKey(js, {"layer", "geometry", "tags"}, swhere);
        if (!err.empty()) return err;
        MapLibreLegendSpecimen sp;
        if (!js.contains("layer") || !js["layer"].is_string())
          return swhere + " has no 'layer' string";
        sp.layer = js["layer"].get<std::string>();
        if (!reads_source_layer(sp.layer))
          return swhere + ": no style layer reads source layer '" + sp.layer + "'";
        if (js.contains("geometry")) {
          const Json& jt = js["geometry"];
          const std::string t = jt.is_string() ? jt.get<std::string>() : "";
          if (t == "line") sp.type = VectorGeometryType::kLine;
          else if (t == "point") sp.type = VectorGeometryType::kPoint;
          else if (t == "area") sp.type = VectorGeometryType::kArea;
          else return swhere + ".geometry is not line, point or area";
        }
        if (js.contains("tags")) {
          err = ReadTags(js["tags"], swhere + ".tags", &sp.tags);
          if (!err.empty()) return err;
        }
        e.specimen.push_back(std::move(sp));
      }
      g.entries.push_back(std::move(e));
    }
    groups->push_back(std::move(g));
  }

  if (j.contains("unlisted_layers")) {
    if (!j["unlisted_layers"].is_array())
      return root + ".unlisted_layers is not an array";
    for (const Json& ju : j["unlisted_layers"]) {
      if (!ju.is_string()) return root + ".unlisted_layers holds a non-string";
      const std::string pattern = ju.get<std::string>();
      const bool names_one = std::any_of(
          infos.begin(), infos.end(),
          [&](const MapLibreLayerInfo& i) { return LayerIdMatches(pattern, i.id); });
      if (!names_one)
        return root + ".unlisted_layers: '" + pattern + "' names no style layer";
      unlisted->push_back(pattern);
    }
  }
  return std::string();
}

}  // namespace

Status MapLibreStyleEngine::LoadText(const std::string& json_text,
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

  // The sprite sheet, before the layers, because a fill-pattern's spacing is
  // its TILE'S OWN SIZE and the layer loop needs the sheet to ask.
  fresh.sprite_base_is_explicit = impl_->sprite_base_is_explicit;
  fresh.prefer_high_dpi = impl_->prefer_high_dpi;
  fresh.fallback_sprites = impl_->fallback_sprites;
  if (root.contains("sprite") && root["sprite"].is_string())
    fresh.sprite_url = root["sprite"].get<std::string>();
  fresh.sprite_base =
      impl_->sprite_base_is_explicit
          ? impl_->sprite_base
          : ResolveSpriteBase(fresh.sprite_url, impl_->load_dir);
  if (!fresh.sprite_base.empty()) {
    auto lib = std::make_shared<PngSymbolLibrary>();
    lib->SetPreferHighDpi(fresh.prefer_high_dpi);
    const Status s =
        lib->OpenSheet(fresh.sprite_base + ".png", fresh.sprite_base + ".json");
    if (s.ok()) {
      fresh.sprites = std::move(lib);
    } else if (impl_->sprite_base_is_explicit) {
      // A base the CALLER named and that does not open is a caller error, and
      // silently drawing an iconless map is the least useful answer to it.
      return fail(Status::Error(
          kIoError, "sprite sheet named by SetSpriteBase does not load: " +
                        fresh.sprite_base + " (" + s.message + ")"));
    }
    // A DERIVED base that does not open is not an error: a style is very often
    // published without the sheet beside it, and MapLibre draws it too — just
    // without icons, which ignored_icons() then counts.
  }

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

    if (type == "background") L.info.type = MapLibreLayerType::kBackground;
    else if (type == "fill")  L.info.type = MapLibreLayerType::kFill;
    else if (type == "line")  L.info.type = MapLibreLayerType::kLine;
    else if (type == "symbol") L.info.type = MapLibreLayerType::kSymbol;
    else if (type == "circle") L.info.type = MapLibreLayerType::kCircle;
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
    if (L.info.type != MapLibreLayerType::kBackground &&
        L.info.source_layer.empty())
      return fail(Reject(id, "layer has no 'source-layer'"));

    if (jl.contains("filter")) {
      const Json& jf = jl["filter"];
      if (IsExpressionFilter(jf)) {
        Status s = StyleExpression::Parse(jf.dump(), &L.filter_expr);
        if (!s.ok())
          return fail(Status::Error(s.code, Reject(id, "filter: " + s.message).message));
        L.has_filter = true;
        L.info.filter = L.filter_expr.text();
      } else {
        Status s = ParseFilter(jf, id, &L.filter);
        if (!s.ok()) return fail(s);
        L.has_filter = !L.filter.is_always();
        L.info.filter = L.has_filter ? L.filter.ToText() : std::string();
      }
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
      case MapLibreLayerType::kBackground:
        if (!(s = col(paint, "background-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "background-opacity", &L.opacity)).ok()) return fail(s);
        // Still rejected with the sheet loading (O6): a background is not a
        // feature here, it is the colour the application CLEARS the canvas
        // with, and there is no geometry to stamp a pattern over.
        if (paint.contains("background-pattern"))
          return fail(Reject(id,
                             "background-pattern has no geometry to repeat "
                             "over: a background is a canvas clear"));
        break;
      case MapLibreLayerType::kFill:
        if (!(s = col(paint, "fill-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "fill-opacity", &L.opacity)).ok()) return fail(s);
        if (!(s = col(paint, "fill-outline-color", &L.outline_color)).ok())
          return fail(s);
        if (paint.contains("fill-pattern") &&
            !(s = ParseText(paint["fill-pattern"], id, "fill-pattern",
                            &L.fill_pattern)).ok())
          return fail(s);
        break;
      case MapLibreLayerType::kLine:
        if (!(s = col(paint, "line-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "line-opacity", &L.opacity)).ok()) return fail(s);
        if (!(s = num(paint, "line-width", &L.width)).ok()) return fail(s);
        if (paint.contains("fv-line-dash") &&
            !(s = ParseText(paint["fv-line-dash"], id, "fv-line-dash",
                            &L.line_preset)).ok())
          return fail(s);
        // O6 loads the sheet, so the artwork is now reachable — but a GL
        // line-pattern STRETCHES a tile along the line, and LinePatternStyle
        // repeats a symbol at a fixed step. Those are different pictures, and
        // guessing which one the style meant is what the declared-subset rule
        // exists to prevent. Rejected until it is built properly.
        if (paint.contains("line-pattern"))
          return fail(Reject(id,
                             "line-pattern stretches a tile along the line; "
                             "this port repeats a symbol at a fixed step, "
                             "which is not the same picture"));
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
      case MapLibreLayerType::kSymbol:
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
        if (layout.contains("icon-image") &&
            !(s = ParseText(layout["icon-image"], id, "icon-image",
                            &L.icon_image)).ok())
          return fail(s);
        if (!(s = num(layout, "icon-size", &L.icon_size)).ok()) return fail(s);
        if (!(s = num(layout, "icon-rotate", &L.icon_rotate)).ok())
          return fail(s);
        if (!(s = col(paint, "icon-color", &L.icon_color)).ok()) return fail(s);
        if (!(s = num(paint, "icon-opacity", &L.icon_opacity)).ok())
          return fail(s);
        if (layout.contains("text-field") &&
            !(s = ParseText(layout["text-field"], id, "text-field",
                            &L.text_field)).ok())
          return fail(s);
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
        if (layout.contains("text-keep-upright")) {
          if (!layout["text-keep-upright"].is_boolean())
            return fail(Reject(id, "text-keep-upright is not a constant"));
          L.keep_upright = layout["text-keep-upright"].get<bool>();
        }
        if (layout.contains("text-offset")) {
          const Json& o = layout["text-offset"];
          if (!o.is_array() || o.size() != 2 || !o[0].is_number() ||
              !o[1].is_number())
            return fail(Reject(id, "text-offset is not a constant [x, y]"));
          L.point_dx_em = o[0].get<double>();
          L.point_dy_em = o[1].get<double>();
          // Only the perpendicular component survives: the placer offsets
          // across the path, and a GL along-axis offset has no meaning once
          // the run is centred on the geometry. GL's +y is DOWN, the placer's
          // positive offset is LEFT of travel (up on an eastward road), hence
          // the negation.
          L.offset_em = -o[1].get<double>();
        }
        if (layout.contains("text-anchor")) {
          if (!layout["text-anchor"].is_string())
            return fail(Reject(id, "text-anchor is not a constant"));
          if (!ParseTextAnchor(layout["text-anchor"].get<std::string>(),
                               &L.point_halign, &L.point_valign))
            return fail(Reject(id, "unknown text-anchor: " +
                                       layout["text-anchor"].get<std::string>()));
        }
        break;
      case MapLibreLayerType::kCircle:
        if (!(s = col(paint, "circle-color", &L.color)).ok()) return fail(s);
        if (!(s = num(paint, "circle-opacity", &L.opacity)).ok()) return fail(s);
        if (!(s = num(paint, "circle-radius", &L.width)).ok()) return fail(s);
        if (!(s = col(paint, "circle-stroke-color", &L.stroke_color)).ok())
          return fail(s);
        if (!(s = num(paint, "circle-stroke-width", &L.stroke_width)).ok())
          return fail(s);
        if (paint.contains("fv-circle-shape") &&
            !(s = ParseText(paint["fv-circle-shape"], id, "fv-circle-shape",
                            &L.shape)).ok())
          return fail(s);
        break;
    }

    if (L.info.type == MapLibreLayerType::kBackground) {
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

  // The legend is read after the layers so its specimens can be checked
  // against them. A bad legend leaves the style loaded with no legend.
  if (root.contains("metadata") && root["metadata"].is_object() &&
      root["metadata"].contains("peregrine:legend")) {
    fresh.legend_error =
        ParseLegend(root["metadata"]["peregrine:legend"], fresh.infos,
                    &fresh.legend, &fresh.legend_unlisted);
    if (!fresh.legend_error.empty()) {
      fresh.legend.clear();
      fresh.legend_unlisted.clear();
    }
  }
  *impl_ = std::move(fresh);
  ClearSymbolCache();
  set_open(true);  // bumps the style epoch
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------

namespace {

FvColor WithOpacity(FvColor c, const NumberFn& op,
                    const StyleExpressionInput& in) {
  if (!op.set) return c;
  const double a = std::max(0.0, std::min(1.0, op.At(in, 1.0)));
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

// Sprite ids are namespaced so they cannot collide with the generated
// "circle:" ones, and so Pixmap() knows an id is its business at a glance.
constexpr char kSpritePrefix[] = "sprite:";

std::string SpriteSymbolId(const std::string& name) {
  return std::string(kSpritePrefix) + name;
}

/// "sprite:<name>#rrggbbaa": the sprite with its colour replaced (an SDF
/// sprite) and its alpha scaled by `tint.a`.
std::string TintedSpriteId(const std::string& name, FvColor tint) {
  char hex[10];
  std::snprintf(hex, sizeof hex, "#%02x%02x%02x%02x", tint.r, tint.g, tint.b, tint.a);
  return SpriteSymbolId(name) + hex;
}

/// A generated circle or square marker, all sizes in HIMETRIC.
struct CircleSymbol {
  int radius = 0;
  FvColor fill;
  FvColor stroke;
  int stroke_width = 0;  // 0 = no outline
  bool square = false;
};

std::string ColorHex(const FvColor& c) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
  return buf;
}

bool ColorFromHex(const std::string& hex, FvColor* c) {
  if (hex.size() != 8) return false;
  for (char ch : hex) if (HexDigit(ch) < 0) return false;
  auto byte = [&hex](size_t i) {
    return static_cast<unsigned char>(HexDigit(hex[i]) * 16 + HexDigit(hex[i + 1]));
  };
  c->r = byte(0); c->g = byte(2); c->b = byte(4); c->a = byte(6);
  return true;
}

std::vector<std::string> SplitColons(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ':') {
      out.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

/// "circle:<r>:<rrggbbaa>" for a plain disc, else
/// "circle:<r>:<fill>:<stroke>:<stroke width>:<c|s>". Self-describing, so
/// LoadSymbol builds the display list from the id and the base class's cache
/// shares it between features.
std::string CircleSymbolId(const CircleSymbol& c) {
  std::string id = "circle:" + std::to_string(c.radius) + ":" + ColorHex(c.fill);
  if (c.stroke_width > 0 || c.square)
    id += ":" + ColorHex(c.stroke) + ":" + std::to_string(c.stroke_width) + ":" +
          (c.square ? "s" : "c");
  return id;
}

bool ParseCircleSymbolId(const std::string& id, CircleSymbol* out) {
  const std::vector<std::string> f = SplitColons(id);
  if (f.size() != 3 && f.size() != 6) return false;
  if (f[0] != "circle") return false;
  char* end = nullptr;
  const long r = std::strtol(f[1].c_str(), &end, 10);
  if (end == f[1].c_str() || *end != '\0' || r <= 0) return false;
  CircleSymbol c;
  c.radius = static_cast<int>(r);
  if (!ColorFromHex(f[2], &c.fill)) return false;
  if (f.size() == 6) {
    if (!ColorFromHex(f[3], &c.stroke)) return false;
    const long w = std::strtol(f[4].c_str(), &end, 10);
    if (end == f[4].c_str() || *end != '\0' || w < 0) return false;
    c.stroke_width = static_cast<int>(w);
    if (f[5] != "c" && f[5] != "s") return false;
    c.square = f[5] == "s";
  }
  *out = c;
  return true;
}

/// "builtin:<rrggbbaa>:<builtin id>" — a fvkit builtin symbol in one colour.
std::string BuiltinSymbolId(const FvColor& color, const std::string& builtin) {
  return "builtin:" + ColorHex(color) + ":" + builtin;
}

}  // namespace

Status MapLibreStyleEngine::StyleFeature(const VectorFeature& f,
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
  StyleExpressionInput in;
  in.get = [&f](const std::string& key, StyleValue* v) {
    const std::string* a = f.Attribute(key);
    if (a == nullptr) return false;
    *v = StyleValue::String(*a);
    return true;
  };
  in.geometry_type = geom_token;
  in.zoom = zoom;
  in.has_id = f.has_id;
  in.id = f.id;

  for (size_t idx : it->second) {
    const StyleLayer& L = impl_->layers[idx];
    if (!L.info.visible) continue;
    if (!L.info.band.Contains(ctx.scale_denominator)) continue;
    if (L.has_filter) {
      if (!L.filter_expr.empty()) {
        const StyleValue v = L.filter_expr.Evaluate(in);
        if (v.type != StyleValue::Type::kBool || !v.boolean) continue;
      } else if (!L.filter.Evaluate(lookup)) {
        continue;
      }
    }

    StyleResultBuilder b(pass.PriorityOr(L.info.priority), out);

    switch (L.info.type) {
      case MapLibreLayerType::kFill: {
        if (L.color.set) {
          FillStyle& fs = b.Fill();
          fs.valid = true;
          fs.brush.color = WithOpacity(L.color.At(in), L.opacity, in);
        }
        if (L.outline_color.set) {
          StrokeStyle& ss = b.Stroke();
          ss.valid = true;
          ss.pen.color = WithOpacity(L.outline_color.At(in), L.opacity, in);
          ss.pen.width = 1;
        }
        if (!L.fill_pattern.empty()) {
          std::string name;
          if (ExpandText(L.fill_pattern, f, in, &name)) {
            const SymbolPixmap* tile = SpriteTile(name);
            if (tile == nullptr) {
              ++impl_->ignored_icons[name];
            } else {
              // SEAMLESS, so the spacing is the tile's OWN drawn size — that
              // is what makes a stamped lattice look like GL's texture tiling
              // instead of a field of stamps with gutters between them.
              //
              // The spacing and the scale therefore carry THE SAME FACTORS or
              // the tiling comes apart: `dpi_scale` because a style engine
              // owns its unit conversion (the renderer's own pixmap_scale has
              // no device DPI in it), and the pass's symbol scale because the
              // stamp obeys it. Scaling only the spacing opens a gutter of
              // exactly the missing factor at every DPI but 96.
              //
              // pixel_ratio is divided out because it is tile pixels per
              // NOMINAL pixel — the same division DrawResolvedSymbol does to
              // the scale, so a 2x sprite tiles at the same spacing as its 1x
              // twin and merely carries more detail.
              const double ratio =
                  tile->pixel_ratio > 0.0 ? tile->pixel_ratio : 1.0;
              const double scale = dpi_scale * pass.SymbolScaleOr(1.0);
              const double w = tile->tile.Width() / ratio * scale;
              const double h = tile->tile.Height() / ratio * scale;
              if (w > 0.0 && h > 0.0) {
                AreaPatternStyle& ap = b.AreaPattern();
                ap.valid = true;
                ap.symbol_id = SpriteSymbolId(name);
                ap.spacing_x = w;
                ap.spacing_y = h;
                ap.symbol_scale = scale;
              }
            }
          }
        }
        break;
      }
      case MapLibreLayerType::kLine: {
        if (!L.color.set) break;
        // line-width defaults to 1 CSS px when the style omits it.
        const double w_css = L.width.set ? L.width.At(in) : 1.0;
        if (!(w_css > 0.0)) break;
        const int w_px = std::max(1, static_cast<int>(std::lround(w_css * dpi_scale)));
        StrokeStyle& ss = b.Stroke();
        ss.valid = true;
        ss.pen.color = WithOpacity(L.color.At(in), L.opacity, in);
        ss.pen.width = w_px;
        std::string preset;
        if (!L.line_preset.empty() && ExpandText(L.line_preset, f, in, &preset) &&
            preset != line_preset::kSolid) {
          LinePatternStyle pattern = MakeLinePreset(preset, ss.pen, dpi_scale);
          if (pattern.valid) {
            // Builtin stamps take the line's colour through their symbol id.
            for (PathRun& run : pattern.runs)
              if (run.type == PathRunType::kSymbol)
                run.symbol_id = BuiltinSymbolId(ss.pen.color, run.symbol_id);
            b.LinePattern() = std::move(pattern);
            ss.valid = false;
            break;
          }
        }
        if (!L.dash.empty()) {
          // GL dash runs are in LINE-WIDTH units, not pixels, and are measured
          // against the width BEFORE it is rounded to a whole pen: rounding
          // twice would step the pattern every time the pen stepped, and two
          // layers meant to case each other would then dash out of register.
          const double w_dash = w_css * dpi_scale;
          for (double d : L.dash)
            ss.pen.dash.push_back(std::max(0.0, d) * w_dash);
          if (ss.pen.dash.size() % 2 != 0)  // Pen wants on/off pairs
            ss.pen.dash.push_back(ss.pen.dash.back());
        }
        break;
      }
      case MapLibreLayerType::kCircle: {
        if (!L.color.set) break;
        const double r_css = L.width.set ? L.width.At(in) : 5.0;
        if (!(r_css > 0.0)) break;
        const int r_hm = std::max(
            1, static_cast<int>(std::lround(r_css * dpi_scale * kHimetricPerPixel)));
        CircleSymbol cs;
        cs.radius = r_hm;
        cs.fill = WithOpacity(L.color.At(in), L.opacity, in);
        if (L.stroke_width.set) {
          const double sw_css = L.stroke_width.At(in);
          if (sw_css > 0.0) {
            cs.stroke_width = std::max(
                1, static_cast<int>(std::lround(sw_css * dpi_scale * kHimetricPerPixel)));
            cs.stroke = L.stroke_color.set
                            ? WithOpacity(L.stroke_color.At(in), L.opacity, in)
                            : FvColor{0, 0, 0, 255};
          }
        }
        std::string shape;
        cs.square = !L.shape.empty() && ExpandText(L.shape, f, in, &shape) &&
                    shape == "square";
        PointSymbolStyle& sy = b.Symbol();
        sy.valid = true;
        sy.symbol_id = CircleSymbolId(cs);
        sy.scale = pass.SymbolScaleOr(1.0);
        break;
      }
      case MapLibreLayerType::kSymbol: {
        // The icon and the text are INDEPENDENT halves of a GL symbol layer:
        // a POI draws both, a shield draws only the icon, a place name only
        // the text. Before O6 the icon half was counted and dropped, so a
        // layer with an icon and no text contributed nothing at all.
        if (!L.icon_image.empty()) {
          std::string name;
          if (ExpandText(L.icon_image, f, in, &name)) {
            if (SpriteTile(name) == nullptr) {
              ++impl_->ignored_icons[name];
            } else {
              PointSymbolStyle& sy = b.Symbol();
              sy.valid = true;
              const double opacity = L.icon_opacity.set ? L.icon_opacity.At(in) : 1.0;
              if (SpriteTile(name)->sdf || opacity < 1.0) {
                // MapLibre tints only SDF sprites; icon-opacity fades any.
                FvColor tint = L.icon_color.set ? L.icon_color.At(in) : FvColor{0, 0, 0, 255};
                const double a = std::clamp(opacity, 0.0, 1.0) * tint.a;
                tint.a = static_cast<unsigned char>(std::lround(a));
                sy.symbol_id = TintedSpriteId(name, tint);
              } else {
                sy.symbol_id = SpriteSymbolId(name);
              }
              // icon-size is a multiplier on the sprite's own size, and
              // dpi_scale is the same CSS-px-to-device conversion every other
              // value here goes through.
              sy.scale = (L.icon_size.set ? L.icon_size.At(in) : 1.0) *
                         dpi_scale * pass.SymbolScaleOr(1.0);
              // GL's icon-rotate is degrees CLOCKWISE, which is what
              // PointSymbolStyle::rotation_deg already means (GeoSym's
              // convention).
              if (L.icon_rotate.set) sy.rotation_deg = L.icon_rotate.At(in);
            }
          }
        }
        if (L.text_field.empty()) break;
        if (!pass.draw_labels) break;
        std::string text;
        if (!ExpandText(L.text_field, f, in, &text)) {
          ++impl_->empty_labels;
          break;
        }
        LabelStyle& lb = b.Label();
        lb.valid = true;
        lb.text = std::move(text);
        lb.style.size = (L.width.set ? L.width.At(in) : 16.0) * dpi_scale;
        lb.style.font_path = impl_->label_font;
        lb.style.color = L.color.set
                             ? WithOpacity(L.color.At(in), L.opacity, in)
                             : FvColor{0, 0, 0, 255};
        // The halo is CSS px like every other GL paint value, so it goes
        // through the same dpi conversion the sizes and widths do. A style
        // that gives a colour and no width gets the GL default of 0, i.e.
        // nothing — the colour alone is not a request to draw an outline.
        if (L.halo_width.set) {
          lb.halo_width = L.halo_width.At(in) * dpi_scale;
          lb.halo_color =
              L.halo_color.set
                  ? WithOpacity(L.halo_color.At(in), L.opacity, in)
                  : FvColor{255, 255, 255, 255};
        }
        // A road name follows its road; a place name does not. The style says
        // which, and a placement of `line` on a point feature is harmless —
        // the renderer falls back to the point path for it.
        if (L.along_path) {
          lb.placement = LabelPlacement::kAlongPath;
          lb.spacing_px = L.spacing * dpi_scale;
          lb.max_angle_deg = L.max_angle;
          lb.keep_upright = L.keep_upright;
          lb.offset_px = L.offset_em * lb.style.size;
          // GL centres a line label ON its line and measures `text-offset`
          // from there; the placer's own anchor is the baseline, which put
          // every street name along the top edge of its street.
          lb.along_anchor = LabelAlongAnchor::kCenter;
        } else {
          lb.dx = static_cast<int>(std::lround(L.point_dx_em * lb.style.size));
          lb.dy = static_cast<int>(std::lround(L.point_dy_em * lb.style.size));
          lb.halign = L.point_halign;
          lb.valign = L.point_valign;
          lb.anchor = LabelAnchor::kCentre;
        }
        break;
      }
      case MapLibreLayerType::kBackground:
        break;  // not reachable: background layers are not in by_source_layer
    }

    b.Finish();
    if (b.AnythingDrawn()) impl_->layer_drew[idx] = true;
  }
  return Status::Ok();
}

// The sheet lookup both emission sites go through, by BARE name (no prefix).
// nullptr = no sheet, or the sheet has no such sprite; the caller counts it.
const SymbolPixmap* MapLibreStyleEngine::SpriteTile(const std::string& name) const {
  PngSymbolLibrary* sheet = Sheet();
  if (sheet == nullptr || name.empty()) return nullptr;
  return sheet->Pixmap(name);
}

PngSymbolLibrary* MapLibreStyleEngine::Sheet() const {
  if (impl_->sprites) return impl_->sprites.get();
  return impl_->sprite_url.empty() ? impl_->fallback_sprites.get() : nullptr;
}

void MapLibreStyleEngine::SetFallbackSprites(std::shared_ptr<PngSymbolLibrary> sheet) {
  impl_->fallback_sprites = std::move(sheet);
  impl_->tinted.clear();
}

const SymbolPixmap* MapLibreStyleEngine::Pixmap(const std::string& symbol_id) {
  const size_t n = std::strlen(kSpritePrefix);
  if (symbol_id.compare(0, n, kSpritePrefix) != 0) return nullptr;
  const size_t hash = symbol_id.rfind('#');
  if (hash == std::string::npos || hash < n) return SpriteTile(symbol_id.substr(n));

  auto hit = impl_->tinted.find(symbol_id);
  if (hit != impl_->tinted.end()) return &hit->second;
  const SymbolPixmap* src = SpriteTile(symbol_id.substr(n, hash - n));
  FvColor tint;
  if (src == nullptr || !ColorFromHex(symbol_id.substr(hash + 1), &tint)) return nullptr;
  SymbolPixmap out = *src;
  for (int y = 0; y < out.tile.Height(); ++y) {
    unsigned char* px = out.tile.Row(y);
    for (int x = 0; x < out.tile.Width(); ++x, px += 4) {
      if (src->sdf) {
        px[0] = tint.r;
        px[1] = tint.g;
        px[2] = tint.b;
      }
      px[3] = static_cast<unsigned char>((px[3] * tint.a + 127) / 255);
    }
  }
  return &impl_->tinted.emplace(symbol_id, std::move(out)).first->second;
}

const VectorSymbol* MapLibreStyleEngine::Symbol(const std::string& symbol_id) {
  // A sprite has no display list, and that is not a failure — it resolves
  // through Pixmap(), which is the next thing ResolveSymbol asks. Answered
  // HERE rather than by letting LoadSymbol return false, because the base
  // class counts a false LoadSymbol as an UNRESOLVED SYMBOL: every icon that
  // drew perfectly would show up in the one diagnostic whose whole job is to
  // list the symbology that did not.
  if (symbol_id.compare(0, std::strlen(kSpritePrefix), kSpritePrefix) == 0)
    return nullptr;
  return LookupTableStyleEngine::Symbol(symbol_id);
}

bool MapLibreStyleEngine::LoadSymbol(const std::string& symbol_id,
                                     VectorSymbol* out) {
  if (symbol_id.compare(0, 8, "builtin:") == 0) {
    const size_t colon = symbol_id.find(':', 8);
    FvColor color;
    if (colon == std::string::npos ||
        !ColorFromHex(symbol_id.substr(8, colon - 8), &color))
      return false;
    BuiltinSymbolLibrary lib;
    lib.SetColor(color);
    const VectorSymbol* sym = lib.Symbol(symbol_id.substr(colon + 1));
    if (sym == nullptr) return false;
    *out = *sym;
    return true;
  }

  CircleSymbol c;
  if (!ParseCircleSymbolId(symbol_id, &c)) return false;
  // HIMETRIC and y-up, like every display list at this seam. The outline
  // lies outside the radius, as MapLibre's circle-stroke does.
  const double r = c.radius + c.stroke_width / 2.0;
  SymbolPrimitive p;
  if (c.square) {
    p.type = SymbolPrimitiveType::kPolygon;
    p.points = {{-r, -r}, {r, -r}, {r, r}, {-r, r}};
  } else {
    p.type = SymbolPrimitiveType::kEllipse;
    p.center = SymbolPoint{0.0, 0.0};
    p.radius1 = SymbolPoint{r, 0.0};
    p.radius2 = SymbolPoint{0.0, r};
  }
  p.has_fill = true;
  p.fill_color = c.fill;
  if (c.stroke_width > 0) {
    p.has_stroke = true;
    p.stroke_color = c.stroke;
    p.stroke_width = c.stroke_width;
  }
  out->primitives.push_back(p);
  const double ext = c.radius + c.stroke_width;
  out->min_x = out->min_y = -ext;
  out->max_x = out->max_y = ext;
  return true;
}

void MapLibreStyleEngine::SetLabelFont(const std::string& path) {
  impl_->label_font = path;
}

bool IsMapLibreExpressionFilter(const std::string& filter_json) {
  const Json j = Json::parse(filter_json, nullptr, /*allow_exceptions=*/false);
  return !j.is_discarded() && IsExpressionFilter(j);
}

}  // namespace fv
