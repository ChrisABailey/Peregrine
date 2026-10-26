// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_enc_names.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "fv_enc_vector_source.h"
#include "fv_s57.h"

namespace fs = std::filesystem;

namespace fv {

const char kEncNamesFileName[] = "chart-names.tsv";

namespace {

constexpr char kHeader[] = "# fv-enc-names 2";
constexpr double kMetresPerDegree = 111320.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos
                                                              : tab - start));
    if (tab == std::string::npos) break;
    start = tab + 1;
  }
  return out;
}

/** `s` with tabs and line breaks turned into spaces, so it fits one TSV cell. */
std::string OneCell(const std::string& s) {
  std::string out = s;
  for (char& c : out)
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return out;
}

/** `r` grown by `m` metres on every side. */
GeoRect Grown(const GeoRect& r, double m) {
  const double dlat = m / kMetresPerDegree;
  const double mid = (r.ll.lat + r.ur.lat) / 2.0;
  const double dlon = dlat / std::max(std::cos(mid * kDegToRad), 1e-6);
  return GeoRect{{r.ll.lat - dlat, r.ll.lon - dlon}, {r.ur.lat + dlat, r.ur.lon + dlon}};
}

bool Touch(const GeoRect& a, const GeoRect& b) {
  return a.ll.lat <= b.ur.lat && b.ll.lat <= a.ur.lat && a.ll.lon <= b.ur.lon &&
         b.ll.lon <= a.ur.lon;
}

GeoRect BoundsOf(const VectorFeature& f) {
  GeoRect r{{90.0, 180.0}, {-90.0, -180.0}};
  for (const auto& part : f.parts)
    for (const GeoPoint& p : part) {
      r.ll.lat = std::min(r.ll.lat, p.lat);
      r.ll.lon = std::min(r.ll.lon, p.lon);
      r.ur.lat = std::max(r.ur.lat, p.lat);
      r.ur.lon = std::max(r.ur.lon, p.lon);
    }
  return r;
}

}  // namespace

Status BuildEncNames(const std::vector<std::string>& cell_paths,
                     const std::string& catalog_dir, std::vector<EncNameEntry>* out,
                     const std::function<bool(size_t, size_t)>& progress, double merge_m) {
  out->clear();
  // Finest band first, so the row a repeat merges into carries that band's position.
  std::vector<std::pair<int, std::string>> cells;
  for (const std::string& p : cell_paths) cells.emplace_back(ParseCellName(p).usage_band, p);
  std::stable_sort(cells.begin(), cells.end(),
                   [](const auto& a, const auto& b) { return a.first > b.first; });

  std::unordered_map<std::string, std::vector<size_t>> by_key;  // class + '\t' + name
  for (size_t i = 0; i < cells.size(); ++i) {
    if (progress && !progress(i, cells.size()))
      return Status::Error(kInterrupted, "ENC names: cancelled");
    EncVectorSource src;
    if (!src.OpenCells({cells[i].second}, catalog_dir).ok()) continue;
    std::vector<VectorFeature> features;
    if (!src.Query(VectorQuery{}, &features).ok()) continue;
    for (const VectorFeature& f : features) {
      const std::string* name = f.Attribute("OBJNAM");
      if (name == nullptr || name->empty() || f.parts.empty() || f.parts[0].empty())
        continue;
      EncNameEntry e;
      e.name = OneCell(*name);
      e.object_class = f.style_key;
      e.usage_band = cells[i].first;
      e.bounds = BoundsOf(f);
      e.position = f.type == VectorGeometryType::kPoint
                       ? f.parts[0][0]
                       : GeoPoint{(e.bounds.ll.lat + e.bounds.ur.lat) / 2.0,
                                  (e.bounds.ll.lon + e.bounds.ur.lon) / 2.0};

      std::vector<size_t>& same = by_key[e.object_class + '\t' + e.name];
      const GeoRect reach = Grown(e.bounds, merge_m);
      bool merged = false;
      for (size_t k : same) {
        EncNameEntry& kept = (*out)[k];
        if (!Touch(reach, kept.bounds)) continue;
        kept.bounds.ll.lat = std::min(kept.bounds.ll.lat, e.bounds.ll.lat);
        kept.bounds.ll.lon = std::min(kept.bounds.ll.lon, e.bounds.ll.lon);
        kept.bounds.ur.lat = std::max(kept.bounds.ur.lat, e.bounds.ur.lat);
        kept.bounds.ur.lon = std::max(kept.bounds.ur.lon, e.bounds.ur.lon);
        merged = true;
        break;
      }
      if (merged) continue;
      same.push_back(out->size());
      out->push_back(std::move(e));
    }
  }
  if (progress) progress(cells.size(), cells.size());
  return Status::Ok();
}

Status WriteEncNames(const std::string& path, const std::vector<EncNameEntry>& entries) {
  const std::string tmp = path + ".tmp";
  {
    std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
    if (!os) return Status::Error(kIoError, "ENC names: cannot write " + tmp);
    os << kHeader << "\n";
    os << "# name\tclass\tband\tlat\tlon\tsouth\twest\tnorth\teast\n";
    char num[200];
    for (const EncNameEntry& e : entries) {
      snprintf(num, sizeof(num), "%d\t%.7f\t%.7f\t%.7f\t%.7f\t%.7f\t%.7f", e.usage_band,
               e.position.lat, e.position.lon, e.bounds.ll.lat, e.bounds.ll.lon,
               e.bounds.ur.lat, e.bounds.ur.lon);
      os << OneCell(e.name) << '\t' << e.object_class << '\t' << num << '\n';
    }
    if (!os.flush()) return Status::Error(kIoError, "ENC names: write failed " + tmp);
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) return Status::Error(kIoError, "ENC names: cannot rename to " + path);
  return Status::Ok();
}

Status ReadEncNames(const std::string& path, std::vector<EncNameEntry>* out) {
  out->clear();
  std::ifstream is(path, std::ios::binary);
  if (!is) return Status::Error(kNotFound, "ENC names: cannot open " + path);
  std::string line;
  if (!std::getline(is, line) || line != kHeader)
    return Status::Error(kUnsupported, "ENC names: unknown format in " + path);
  size_t line_no = 1;
  while (std::getline(is, line)) {
    ++line_no;
    if (line.empty() || line[0] == '#') continue;
    const std::vector<std::string> c = SplitTabs(line);
    if (c.size() != 9)
      return Status::Error(kIoError, "ENC names: bad row " + std::to_string(line_no) +
                                         " in " + path);
    EncNameEntry e;
    e.name = c[0];
    e.object_class = c[1];
    e.usage_band = atoi(c[2].c_str());
    e.position.lat = strtod(c[3].c_str(), nullptr);
    e.position.lon = strtod(c[4].c_str(), nullptr);
    e.bounds.ll.lat = strtod(c[5].c_str(), nullptr);
    e.bounds.ll.lon = strtod(c[6].c_str(), nullptr);
    e.bounds.ur.lat = strtod(c[7].c_str(), nullptr);
    e.bounds.ur.lon = strtod(c[8].c_str(), nullptr);
    out->push_back(std::move(e));
  }
  return Status::Ok();
}

}  // namespace fv
