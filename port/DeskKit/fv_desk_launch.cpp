// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_launch.h"

#include <cmath>
#include <cstdlib>

namespace fv {
namespace desk {
namespace {

/// Parses all of `text` as a finite number.
bool ParseNumber(const std::string& text, double* out) {
  if (text.empty()) return false;
  const char* begin = text.c_str();
  char* end = nullptr;
  const double v = std::strtod(begin, &end);
  if (end != begin + text.size() || !std::isfinite(v)) return false;
  *out = v;
  return true;
}

/// Parses "lat,lon[,scale]" into `o`; false leaves `o` unchanged.
bool ParseCenter(const std::string& text, LaunchOptions* o) {
  std::vector<double> v;
  size_t start = 0;
  while (true) {
    const size_t comma = text.find(',', start);
    double n = 0;
    if (!ParseNumber(text.substr(start, comma - start), &n)) return false;
    v.push_back(n);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (v.size() < 2 || v.size() > 3) return false;
  o->has_center = true;
  o->center_lat = v[0];
  o->center_lon = v[1];
  o->center_scale = v.size() == 3 ? v[2] : 0;
  return true;
}

}  // namespace

LaunchOptions ParseLaunchOptions(const std::vector<std::string>& argv) {
  LaunchOptions o;
  for (size_t i = 1; i < argv.size(); ++i) {
    const std::string& a = argv[i];
    const bool has_value = i + 1 < argv.size();
    if (a == "--catalog" && has_value) {
      o.catalog = argv[++i];
    } else if (a == "--shot" && has_value) {
      o.shot = argv[++i];
    } else if (a == "--center" && has_value) {
      ParseCenter(argv[++i], &o);
    }
  }
  return o;
}

}  // namespace desk
}  // namespace fv
