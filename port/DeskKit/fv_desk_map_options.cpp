// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_map_options.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

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

}  // namespace

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

std::vector<MapOptionsSource> BuiltinMapOptions() { return {ElevationMapOptions()}; }

}  // namespace desk
}  // namespace fv
