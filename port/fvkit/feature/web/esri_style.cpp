// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Esri renderer, symbol and label class import into `FeatureStyle`.

#include "fvkit/feature/web/esri_style.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <set>

#include "../geojson_detail.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/log.h"

namespace fv {
namespace web {
namespace {

using geojson_detail::json;

constexpr double kPxPerPt = 96.0 / 72.0;

std::string Str(const json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
  return j[key].get<std::string>();
}

double Num(const json& j, const char* key, double fallback) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return fallback;
  return j[key].get<double>();
}

/// Collects notes once each, in the order first raised.
class Notes {
 public:
  void Add(const std::string& note) {
    if (seen_.insert(note).second) list_.push_back(note);
  }
  const std::vector<std::string>& list() const { return list_; }

 private:
  std::set<std::string> seen_;
  std::vector<std::string> list_;
};

/// An Esri [r, g, b, a] colour scaled by the layer's opacity; null or
/// malformed is transparent.
FvColor Color(const json& c, double opacity) {
  FvColor out{0, 0, 0, 0};
  if (!c.is_array() || c.size() < 3) return out;
  for (size_t i = 0; i < 4 && i < c.size(); ++i)
    if (!c[i].is_number()) return out;
  auto channel = [](double v) { return static_cast<unsigned char>(std::clamp(std::lround(v), 0L, 255L)); };
  out.r = channel(c[0].get<double>());
  out.g = channel(c[1].get<double>());
  out.b = channel(c[2].get<double>());
  out.a = channel((c.size() > 3 ? c[3].get<double>() : 255.0) * opacity);
  return out;
}

/// A symbol that draws nothing: what Esri shows for a value with no symbol.
FeatureSymbol Invisible() {
  FeatureSymbol s;
  s.marker_fill.a = s.marker_outline.a = 0;
  s.marker_outline_px = 0;
  s.line_color.a = 0;
  s.line_width_px = 0;
  s.area_fill.a = s.area_outline.a = 0;
  s.area_outline_px = 0;
  return s;
}

/// The nearest `line_preset` to an esriSLS style.
std::string DashFor(const std::string& style) {
  if (style.find("DashDot") != std::string::npos) return line_preset::kDashDot;
  if (style == "esriSLSLongDash") return line_preset::kLongDash;
  if (style.find("Dash") != std::string::npos) return line_preset::kDash;
  if (style.find("Dot") != std::string::npos) return line_preset::kDot;
  return line_preset::kSolid;
}

/// An esriSLS (or an outline object without a type) as colour and width.
void ReadLine(const json& l, double opacity, FvColor* color, double* width_px) {
  if (!l.is_object() || Str(l, "style") == "esriSLSNull") {
    color->a = 0;
    *width_px = 0;
    return;
  }
  *color = Color(l.value("color", json()), opacity);
  *width_px = Num(l, "width", 1.0) * kPxPerPt;
}

/// An Esri symbol as a `FeatureSymbol`; what it cannot read keeps the default
/// symbol and adds a note.
FeatureSymbol ReadSymbol(const json& j, double opacity, Notes* notes) {
  FeatureSymbol s;
  if (j.is_null()) return Invisible();
  const std::string type = Str(j, "type");
  if (type == "esriSMS") {
    const std::string style = Str(j, "style");
    s.marker = style == "esriSMSSquare" ? FeatureMarker::kSquare : FeatureMarker::kCircle;
    if (style != "esriSMSCircle" && style != "esriSMSSquare" && !style.empty())
      notes->Add("Marker style " + style + " is drawn as a circle.");
    s.marker_size_px = Num(j, "size", 8.0) * kPxPerPt;
    s.marker_fill = Color(j.value("color", json()), opacity);
    ReadLine(j.value("outline", json()), opacity, &s.marker_outline, &s.marker_outline_px);
  } else if (type == "esriSLS") {
    ReadLine(j, opacity, &s.line_color, &s.line_width_px);
    s.dash = DashFor(Str(j, "style"));
  } else if (type == "esriSFS") {
    const std::string style = Str(j, "style");
    if (style == "esriSFSNull") {
      s.area_fill.a = 0;
    } else {
      s.area_fill = Color(j.value("color", json()), opacity);
      if (style != "esriSFSSolid" && !style.empty())
        notes->Add("Hatched fill " + style + " is drawn solid.");
    }
    ReadLine(j.value("outline", json()), opacity, &s.area_outline, &s.area_outline_px);
  } else if (type == "esriPMS" || type == "esriPFS") {
    notes->Add("Picture symbols are not imported; the default symbol is used.");
  } else if (type == "CIMSymbolReference") {
    notes->Add("CIM symbols are not imported; the default symbol is used.");
  } else {
    notes->Add("Symbol type \"" + type + "\" is not imported; the default symbol is used.");
  }
  return s;
}

/// The category key of an Esri unique value for a field of `type`: numbers
/// are keyed as `FeatureCategoryKey` keys them, so "1.50" matches 1.5.
std::string CategoryKey(const std::string& value, FieldType type) {
  if (type != FieldType::kInteger && type != FieldType::kReal) return value;
  char* end = nullptr;
  const double d = std::strtod(value.c_str(), &end);
  if (value.empty() || end != value.c_str() + value.size()) return value;
  if (type == FieldType::kInteger && d == std::floor(d) && std::fabs(d) < 9.2e18)
    return FeatureCategoryKey(FieldValue(static_cast<int64_t>(d)));
  return FeatureCategoryKey(FieldValue(d));
}

/// The schema's spelling of field `name`, or empty when the layer lacks it.
std::string SchemaName(const FeatureSchema& schema, const std::string& name) {
  const int i = schema.Find(name);
  return i < 0 ? "" : schema.fields[i].name;
}

void ReadUniqueValues(const json& r, const FeatureSchema& schema, double opacity, FeatureStyle* style,
                      Notes* notes) {
  const json default_symbol = r.value("defaultSymbol", json());
  if (!Str(r, "field2").empty() || !Str(r, "field3").empty()) {
    notes->Add("Unique values over several fields are not imported; one symbol is used.");
    style->symbol = default_symbol.is_null() ? FeatureSymbol() : ReadSymbol(default_symbol, opacity, notes);
    return;
  }
  const std::string field = SchemaName(schema, Str(r, "field1"));
  if (field.empty()) {
    notes->Add("The renderer's field \"" + Str(r, "field1") + "\" is not in the layer; one symbol is used.");
    return;
  }
  const FieldType type = schema.fields[schema.Find(field)].type;
  style->renderer = FeatureRenderer::kCategorised;
  style->field = field;
  style->symbol = ReadSymbol(default_symbol, opacity, notes);
  std::set<std::string> keys;
  auto add = [&](const std::string& value, const json& info) {
    FeatureCategory c;
    c.value = CategoryKey(value, type);
    if (!keys.insert(c.value).second) return;
    c.label = Str(info, "label");
    if (c.label == c.value) c.label.clear();
    c.symbol = ReadSymbol(info.value("symbol", json()), opacity, notes);
    style->categories.push_back(std::move(c));
  };
  if (r.contains("uniqueValueInfos") && r["uniqueValueInfos"].is_array()) {
    for (const json& info : r["uniqueValueInfos"])
      if (info.is_object() && info.contains("value"))
        add(info["value"].is_string() ? info["value"].get<std::string>() : info["value"].dump(), info);
  } else {
    // ArcGIS 10.9+ may give only groups: classes holding lists of value lists.
    for (const json& group : r.value("uniqueValueGroups", json::array()))
      for (const json& cls : group.value("classes", json::array()))
        for (const json& values : cls.value("values", json::array()))
          if (values.is_array() && !values.empty())
            add(values[0].is_string() ? values[0].get<std::string>() : values[0].dump(), cls);
  }
}

void ReadClassBreaks(const json& r, const FeatureSchema& schema, double opacity, FeatureStyle* style,
                     Notes* notes) {
  const json default_symbol = r.value("defaultSymbol", json());
  const std::string normalization = Str(r, "normalizationType");
  const std::string field = SchemaName(schema, Str(r, "field"));
  const json infos = r.value("classBreakInfos", json::array());
  if (!normalization.empty() && normalization != "esriNormalizeNone") {
    notes->Add("Normalised class breaks (" + normalization + ") are not imported; one symbol is used.");
  } else if (field.empty()) {
    notes->Add("The renderer's field \"" + Str(r, "field") + "\" is not in the layer; one symbol is used.");
  } else if (!infos.is_array() || infos.empty()) {
    notes->Add("The class breaks renderer has no classes; one symbol is used.");
  } else {
    style->renderer = FeatureRenderer::kGraduated;
    style->field = field;
    style->symbol = ReadSymbol(default_symbol, opacity, notes);
    std::vector<json> sorted(infos.begin(), infos.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const json& a, const json& b) {
      return Num(a, "classMaxValue", 0) < Num(b, "classMaxValue", 0);
    });
    double previous_max = Num(r, "minValue", std::numeric_limits<double>::lowest());
    for (size_t i = 0; i < sorted.size(); ++i) {
      FeatureClass c;
      // Esri classes run (previous max, max]; the class list here is [lower, next lower).
      const double inclusive_min =
          i == 0 ? previous_max : std::nextafter(previous_max, std::numeric_limits<double>::infinity());
      c.lower = Num(sorted[i], "classMinValue", inclusive_min);
      c.label = Str(sorted[i], "label");
      c.symbol = ReadSymbol(sorted[i].value("symbol", json()), opacity, notes);
      previous_max = Num(sorted[i], "classMaxValue", c.lower);
      style->classes.push_back(std::move(c));
    }
    return;
  }
  style->symbol = default_symbol.is_null() ? FeatureSymbol() : ReadSymbol(default_symbol, opacity, notes);
}

/// The field a label class names, from an Arcade `$feature.NAME` /
/// `$feature["NAME"]` expression or a legacy `[NAME]`; empty for anything else.
std::string LabelField(const json& cls) {
  static const std::regex arcade(R"re(^\s*\$feature(?:\.(\w+)|\[\s*["'](.+?)["']\s*\])\s*;?\s*$)re");
  static const std::regex legacy(R"re(^\s*\[(\w+)\]\s*$)re");
  std::smatch m;
  const std::string expression = Str(cls.value("labelExpressionInfo", json()), "expression");
  if (!expression.empty()) {
    if (std::regex_match(expression, m, arcade)) return m[1].matched ? m[1].str() : m[2].str();
    return "";
  }
  const std::string old = Str(cls, "labelExpression");
  if (std::regex_match(old, m, legacy)) return m[1].str();
  return "";
}

}  // namespace

Status EsriStyleFromDrawingInfo(const std::string& drawing_info, const FeatureSchema& schema,
                                FeatureStyle* style, std::vector<std::string>* notes_out) {
  const json di = json::parse(drawing_info, nullptr, /*allow_exceptions=*/false);
  if (!di.is_object()) return Status::Error(kInvalidArg, "Esri drawingInfo is not a JSON object");
  *style = FeatureStyle();
  Notes notes;
  const double opacity = 1.0 - std::clamp(Num(di, "transparency", 0.0), 0.0, 100.0) / 100.0;

  const json r = di.value("renderer", json());
  const std::string type = Str(r, "type");
  if (type == "simple") {
    style->symbol = ReadSymbol(r.value("symbol", json()), opacity, &notes);
  } else if (type == "uniqueValue") {
    ReadUniqueValues(r, schema, opacity, style, &notes);
  } else if (type == "classBreaks") {
    ReadClassBreaks(r, schema, opacity, style, &notes);
  } else if (!type.empty()) {
    notes.Add("The " + type + " renderer is not imported; the default symbol is used.");
  }
  if (r.is_object() && r.contains("visualVariables") && r["visualVariables"].is_array() &&
      !r["visualVariables"].empty())
    notes.Add("Visual variables (size, colour or rotation by value) are not imported.");
  if (r.is_object() && r.contains("backgroundFillSymbol") && !r["backgroundFillSymbol"].is_null())
    notes.Add("The background fill symbol is not imported.");

  const json labeling = di.value("labelingInfo", json());
  if (labeling.is_array() && !labeling.empty()) {
    const json& cls = labeling[0];
    const std::string field = SchemaName(schema, LabelField(cls));
    if (field.empty()) {
      notes.Add("The label expression is not a single field; labels are off.");
    } else {
      style->label.field = field;
      const json ts = cls.value("symbol", json());
      if (ts.is_object()) {
        if (ts.contains("color")) style->label.color = Color(ts["color"], 1.0);
        const json font = ts.value("font", json());
        style->label.size_px = Num(font, "size", style->label.size_px / kPxPerPt) * kPxPerPt;
        if (ts.contains("haloColor") && !ts["haloColor"].is_null()) {
          style->label.halo_color = Color(ts["haloColor"], 1.0);
          style->label.halo_px = Num(ts, "haloSize", 1.0) * kPxPerPt;
        } else {
          style->label.halo_px = 0;
        }
      }
    }
    if (labeling.size() > 1) notes.Add("Only the first of the layer's label classes is imported.");
  }

  for (const std::string& n : notes.list()) FV_LOG_INFO("Esri style import: " << n);
  if (notes_out) *notes_out = notes.list();
  return Status::Ok();
}

}  // namespace web
}  // namespace fv
