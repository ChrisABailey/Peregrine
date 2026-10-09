// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_map_options.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>

#include "fv_desk_vector_map.h"
#include "fvkit/formats/dted_shaded.h"

namespace fv {
namespace desk {
namespace {

constexpr size_t kMaxBands = 5;

/// The Elevation page: one string property holding the breakpoint list.
class ElevationOptions : public app::Properties {
 public:
  ElevationOptions() { value_ = Describe().front().default_value; }

  const std::vector<app::PropertySpec>& Describe() const override {
    static const std::vector<app::PropertySpec> specs = [] {
      app::PropertySpec s;
      s.key = "elevation_bands_ft";
      s.label = "Colour breaks (ft)";
      s.group = "Shaded relief";
      s.type = app::PropertyType::kString;
      s.default_value = app::PropertyValue::String(std::string());
      s.help = "Up to five elevations in feet, lowest first, where the shaded relief "
               "changes colour. Empty uses 2500, 5000, 7500, 10000, 12500.";
      return std::vector<app::PropertySpec>{s};
    }();
    return specs;
  }

  Status GetProperty(const std::string& key, app::PropertyValue* out) const override {
    if (key != "elevation_bands_ft") return Status::Error(kNotFound, "no property '" + key + "'");
    *out = value_;
    return Status::Ok();
  }

  Status SetProperty(const std::string& key, const app::PropertyValue& v) override {
    if (key != "elevation_bands_ft") return Status::Error(kNotFound, "no property '" + key + "'");
    if (v.type != app::PropertyType::kString)
      return Status::Error(kInvalidArg, "elevation_bands_ft is a string");
    // Stored canonical, so the dialog shows what will be drawn.
    value_ = app::PropertyValue::String(FormatElevationBands(ParseElevationBands(v.s)));
    return Status::Ok();
  }

 private:
  app::PropertyValue value_;
};

/// A page over a fixed spec list whose values are only type-checked; the
/// meaning is applied when the page is.
class SpecProperties : public app::Properties {
 public:
  explicit SpecProperties(std::vector<app::PropertySpec> specs) : specs_(std::move(specs)) {
    for (const app::PropertySpec& s : specs_) values_[s.key] = s.default_value;
  }

  const std::vector<app::PropertySpec>& Describe() const override { return specs_; }

  Status GetProperty(const std::string& key, app::PropertyValue* out) const override {
    auto it = values_.find(key);
    if (it == values_.end()) return Status::Error(kNotFound, "no property '" + key + "'");
    *out = it->second;
    return Status::Ok();
  }

  Status SetProperty(const std::string& key, const app::PropertyValue& v) override {
    const app::PropertySpec* spec = FindSpec(key);
    if (spec == nullptr) return Status::Error(kNotFound, "no property '" + key + "'");
    if (v.type != spec->type) return Status::Error(kInvalidArg, key + " has the wrong type");
    app::PropertyValue stored = v;
    if (spec->type == app::PropertyType::kInt && spec->min < spec->max)
      stored.i = std::clamp<long long>(v.i, static_cast<long long>(spec->min),
                                       static_cast<long long>(spec->max));
    if (spec->type == app::PropertyType::kString && key.compare(0, 8, "mariner.") == 0) {
      std::optional<double> depth;
      std::optional<bool> flag;
      const bool is_flag = key == "mariner.two_shades" || key == "mariner.shallow_pattern";
      if (is_flag ? !ParseMarinerFlag(v.s, &flag) : !ParseMarinerDepth(v.s, &depth))
        return Status::Error(kInvalidArg, key + ": '" + v.s + "' is not " +
                                              (is_flag ? "on or off" : "a depth in metres"));
    }
    values_[key] = stored;
    return Status::Ok();
  }

 private:
  std::vector<app::PropertySpec> specs_;
  std::map<std::string, app::PropertyValue> values_;
};

app::PropertySpec PathSpec(const std::string& key, const std::string& label,
                           app::PathKind kind, const std::string& help) {
  app::PropertySpec s;
  s.key = key;
  s.label = label;
  s.type = app::PropertyType::kPath;
  s.path_kind = kind;
  s.default_value = app::PropertyValue::Path(std::string());
  s.help = help;
  if (kind == app::PathKind::kFile) s.path_filters = {{"GL Style Sheets (*.json)", "*.json"}};
  return s;
}

/// The [mariner] keys. They also drive DNC, but live on the ENC page alone so
/// that one open dialog cannot hold two different values for them.
void AddMarinerSpecs(std::vector<app::PropertySpec>* specs) {
  const struct {
    const char* key;
    const char* label;
    const char* help;
  } rows[] = {
      {"mariner.safety_contour", "Safety contour (m)",
       "The bold contour, where the depth shading splits. Empty keeps the chart's default."},
      {"mariner.shallow_contour", "Shallow contour (m)", "Inner edge of the shallow shade."},
      {"mariner.deep_contour", "Deep contour (m)", "Outer edge of the deep shade."},
      {"mariner.safety_depth", "Safety depth (m)",
       "ENC soundings at or below this print bold. DNC has no counterpart."},
      {"mariner.two_shades", "Two depth shades", "on or off; empty keeps the chart's default."},
      {"mariner.shallow_pattern", "Shallow pattern", "on or off; empty keeps the chart's default."},
  };
  for (const auto& r : rows) {
    app::PropertySpec s;
    s.key = r.key;
    s.label = r.label;
    s.group = "Mariner (ENC and DNC)";
    s.type = app::PropertyType::kString;
    s.default_value = app::PropertyValue::String(std::string());
    s.help = r.help;
    specs->push_back(std::move(s));
  }
}

MarinerOverrides MarinerFrom(const app::Properties& p) {
  MarinerOverrides m;
  ParseMarinerDepth(p.GetString("mariner.safety_contour"), &m.safety_contour);
  ParseMarinerDepth(p.GetString("mariner.shallow_contour"), &m.shallow_contour);
  ParseMarinerDepth(p.GetString("mariner.deep_contour"), &m.deep_contour);
  ParseMarinerDepth(p.GetString("mariner.safety_depth"), &m.safety_depth);
  ParseMarinerFlag(p.GetString("mariner.two_shades"), &m.two_shades);
  ParseMarinerFlag(p.GetString("mariner.shallow_pattern"), &m.shallow_pattern);
  return m;
}

/// Read-modify-write of the process-wide vector configuration.
template <typename Edit>
void EditVectorConfig(Edit edit) {
  VectorMapConfig c = CurrentVectorMapConfig();
  edit(c);
  SetVectorMapConfig(c);
}

}  // namespace

bool ParseMarinerDepth(const std::string& text, std::optional<double>* out) {
  out->reset();
  const size_t b = text.find_first_not_of(" \t");
  if (b == std::string::npos) return true;
  const std::string t = text.substr(b, text.find_last_not_of(" \t") - b + 1);
  char* stop = nullptr;
  const double d = std::strtod(t.c_str(), &stop);
  if (stop != t.c_str() + t.size() || !std::isfinite(d) || d < 0) return false;
  *out = d;
  return true;
}

bool ParseMarinerFlag(const std::string& text, std::optional<bool>* out) {
  out->reset();
  std::string t;
  for (char c : text)
    if (c != ' ' && c != '\t') t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (t.empty()) return true;
  if (t == "1" || t == "on" || t == "yes" || t == "true") *out = true;
  else if (t == "0" || t == "off" || t == "no" || t == "false") *out = false;
  else return false;
  return true;
}

std::vector<int> ParseElevationBands(const std::string& text) {
  std::vector<int> feet;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find_first_of(",;", start);
    if (end == std::string::npos) end = text.size();
    const std::string part = text.substr(start, end - start);
    const char* begin = part.c_str();
    char* stop = nullptr;
    const double d = std::strtod(begin, &stop);
    // Python's float() takes surrounding whitespace and no hex.
    bool whole = stop != begin && std::isfinite(d) && part.find_first_of("xX") == std::string::npos;
    for (const char* c = stop; whole && *c != '\0'; ++c)
      if (*c != ' ' && *c != '\t') whole = false;
    // nearbyint under the default rounding mode is Python's round().
    if (whole) feet.push_back(static_cast<int>(std::nearbyint(d)));
    start = end + 1;
  }
  std::sort(feet.begin(), feet.end());
  if (feet.size() > kMaxBands) feet.resize(kMaxBands);
  return feet;
}

std::string FormatElevationBands(const std::vector<int>& feet) {
  std::string out;
  for (int f : feet) {
    if (!out.empty()) out += ',';
    out += std::to_string(f);
  }
  return out;
}

MapOptionsSource ElevationMapOptions() {
  MapOptionsSource src;
  src.group_id = "elevation";
  src.prefix = "dted.";
  src.make = [] { return std::make_unique<ElevationOptions>(); };
  src.apply = [](const app::Properties& p) {
    SetDtedShadedElevationBands(ParseElevationBands(p.GetString("elevation_bands_ft")));
  };
  return src;
}

MapOptionsSource DncMapOptions() {
  MapOptionsSource src;
  src.group_id = "dnc";
  src.make = [] {
    std::vector<app::PropertySpec> specs;
    specs.push_back(PathSpec("geosym.data_dir", "GeoSym directory", app::PathKind::kDirectory,
                             "The directory holding GeoSymbol/SymAssign and GeoSymbol/Graphics."));
    for (const char* key : {"geosym.brightness", "geosym.contrast"}) {
      app::PropertySpec s;
      s.key = key;
      s.label = key[7] == 'b' ? "Brightness" : "Contrast";
      s.group = "Symbology";
      s.type = app::PropertyType::kInt;
      s.min = -100;
      s.max = 100;
      s.default_value = app::PropertyValue::Int(0);
      specs.push_back(std::move(s));
    }
    return std::make_unique<SpecProperties>(std::move(specs));
  };
  src.apply = [](const app::Properties& p) {
    EditVectorConfig([&p](VectorMapConfig& c) {
      c.geosym_dir = p.GetString("geosym.data_dir");
      c.geosym_brightness = static_cast<int>(p.GetInt("geosym.brightness", 0));
      c.geosym_contrast = static_cast<int>(p.GetInt("geosym.contrast", 0));
    });
  };
  return src;
}

MapOptionsSource EncMapOptions() {
  MapOptionsSource src;
  src.group_id = "enc";
  src.make = [] {
    std::vector<app::PropertySpec> specs;
    specs.push_back(PathSpec("enc.data_dir", "S-52 library directory", app::PathKind::kDirectory,
                             "The directory holding chartsymbols.xml and the S-57 catalogue "
                             "CSVs (s57objectclasses.csv, s57attributes.csv)."));
    app::PropertySpec meta;
    meta.key = "enc.show_meta_objects";
    meta.label = "Show meta objects";
    meta.type = app::PropertyType::kBool;
    meta.default_value = app::PropertyValue::Bool(false);
    meta.help = "Draw M_QUAL, M_COVR, M_NSYS and M_NPUB, which describe the dataset.";
    specs.push_back(std::move(meta));
    AddMarinerSpecs(&specs);
    return std::make_unique<SpecProperties>(std::move(specs));
  };
  src.apply = [](const app::Properties& p) {
    EditVectorConfig([&p](VectorMapConfig& c) {
      c.enc_dir = p.GetString("enc.data_dir");
      c.enc_show_meta = p.GetBool("enc.show_meta_objects", false);
      c.mariner = MarinerFrom(p);
    });
  };
  return src;
}

MapOptionsSource OsmMapOptions() {
  MapOptionsSource src;
  src.group_id = "osm";
  src.make = [] {
    return std::make_unique<SpecProperties>(std::vector<app::PropertySpec>{
        PathSpec("osm.style", "Style sheet", app::PathKind::kFile,
                 "The MapLibre GL style sheet OpenStreetMap is drawn with.")});
  };
  src.apply = [](const app::Properties& p) {
    EditVectorConfig([&p](VectorMapConfig& c) { c.osm_style = p.GetString("osm.style"); });
  };
  return src;
}

std::vector<MapOptionsSource> BuiltinMapOptions() {
  return {ElevationMapOptions(), DncMapOptions(), EncMapOptions(), OsmMapOptions()};
}

}  // namespace desk
}  // namespace fv
