// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "fv_route_overlay.h"

namespace fv {
namespace desk {
namespace {

/// `value` rounded to `decimals` places with thousands separated: "1,234.5".
std::string Grouped(double value, int decimals) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, std::fabs(value));
  std::string s(buf);
  const size_t dot = s.find('.');
  std::string whole = s.substr(0, dot);
  const std::string frac = dot == std::string::npos ? std::string() : s.substr(dot);
  for (int i = static_cast<int>(whole.size()) - 3; i > 0; i -= 3) whole.insert(i, ",");
  std::string out = whole + frac;
  // "-0" stays "0" when the value rounds to zero.
  if (value < 0 && out.find_first_not_of("0.,") != std::string::npos) out.insert(0, "-");
  return out;
}

}  // namespace

std::optional<analysis::GeoPath> RouteGeoPath(const RouteOverlay& route,
                                              std::vector<size_t>* waypoints) {
  std::vector<GeoPoint> pts;
  std::vector<size_t> marks;
  if (route.has_plan()) {
    // Consecutive legs share their join point.
    for (const std::vector<GeoPoint>& leg : route.plan().legs) {
      if (leg.empty()) continue;
      if (pts.empty()) marks.push_back(0);
      pts.insert(pts.end(), pts.empty() ? leg.begin() : leg.begin() + 1, leg.end());
      marks.push_back(pts.size() - 1);
    }
  }
  if (pts.size() < 2) {
    pts.clear();
    marks.clear();
    for (const RouteWaypoint& w : route.waypoints()) {
      marks.push_back(pts.size());
      pts.push_back(w.position);
    }
  }
  if (pts.size() < 2) return std::nullopt;
  if (waypoints != nullptr) *waypoints = std::move(marks);
  const analysis::LineType type = route.leg_kind() == LineKind::kRhumb
                                      ? analysis::LineType::kRhumb
                                      : analysis::LineType::kGreatCircle;
  return analysis::GeoPath(std::move(pts), type);
}

std::pair<double, double> ElevationWindow(double min_m, double max_m, double total_distance_m,
                                          double per_m) {
  double lo = min_m * per_m, hi = max_m * per_m;
  if (!(hi > lo)) {
    hi = lo + 1.0;
    lo = lo - 1.0;
  }
  const double pad = (hi - lo) * 0.08;
  lo -= pad;
  hi += pad;
  const double floor = std::max(0.0, total_distance_m) * kFullScaleGrade * per_m;
  if (hi - lo < floor) {
    const double mid = (lo + hi) * 0.5;
    lo = mid - floor * 0.5;
    hi = mid + floor * 0.5;
  }
  return {lo, hi};
}

analysis::RangeUnit ParseRangeUnit(const std::string& name) {
  std::string n;
  for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (n == "miles") return analysis::RangeUnit::kMiles;
  if (n == "km") return analysis::RangeUnit::kKilometers;
  if (n == "m") return analysis::RangeUnit::kMeters;
  if (n == "yards") return analysis::RangeUnit::kYards;
  if (n == "ft") return analysis::RangeUnit::kFeet;
  return analysis::RangeUnit::kNauticalMiles;
}

void ProfileChart::Sample(IElevationSource* src, const analysis::GeoPath& path, int samples,
                          const std::vector<size_t>& markers) {
  markers_m_.clear();
  for (size_t i : markers)
    if (i < path.PointCount()) markers_m_.push_back(path.CumulativeAt(i));
  if (src == nullptr) {
    Clear("No elevation data is open — add a DTED source.");
    return;
  }
  analysis::ProfileOptions opts;
  opts.sample_count = static_cast<size_t>(std::max(3, samples));
  analysis::ProfileResult r;
  const Status s = analysis::SampleTerrainProfile(*src, path, opts, &r);
  if (!s.ok()) {
    Clear(s.message);
    return;
  }
  result_ = std::move(r);
  message_.clear();
}

void ProfileChart::Clear(std::string message) {
  result_.reset();
  message_ = std::move(message);
}

std::pair<const char*, double> ProfileChart::ElevationUnit() const {
  const bool metric = style_.units == analysis::RangeUnit::kKilometers ||
                      style_.units == analysis::RangeUnit::kMeters;
  return metric ? std::make_pair("m", 1.0) : std::make_pair("ft", 1.0 / 0.3048);
}

ProfileLayout ProfileChart::Layout(double width_pt, double height_pt) const {
  ProfileLayout L;
  L.x0 = kPadLeft;
  L.y0 = kPadTop;
  L.x1 = std::max(width_pt, 200.0) - kPadRight;
  L.y1 = std::max(height_pt, 140.0) - kPadBottom;
  const double total = result_ ? analysis::ConvertRange(result_->total_distance_m, style_.units) : 0;
  if (!result_ || result_->Empty() || !(total > 0)) {
    L.message = message_.empty() ? "Nothing to profile — the route needs two waypoints."
                                 : message_;
    return L;
  }
  L.has_plot = true;
  const analysis::ProfileResult& r = *result_;
  const std::string eu = ElevationUnit().first;
  const double per_m = ElevationUnit().second;
  const std::pair<double, double> window =
      ElevationWindow(r.min_m, r.max_m, r.total_distance_m, per_m);
  const double lo = window.first, hi = window.second;
  const double w = L.x1 - L.x0, h = L.y1 - L.y0;
  auto sx = [&](double d_m) { return L.x0 + w * (analysis::ConvertRange(d_m, style_.units) / total); };
  auto sy = [&](double e_m) { return L.y1 - h * ((e_m * per_m - lo) / (hi - lo)); };

  const std::string du = analysis::RangeUnitName(style_.units);
  for (int i = 0; i <= 5; ++i) {
    L.y_ticks.push_back({L.y1 - h * i / 5.0, Grouped(lo + (hi - lo) * i / 5.0, 0) + " " + eu});
    L.x_ticks.push_back({L.x0 + w * i / 5.0, Grouped(total * i / 5.0, 1) + " " + du});
  }
  std::vector<ChartPoint> run;
  for (const analysis::ProfilePoint& p : r.points) {
    if (p.has_data) {
      run.push_back({sx(p.distance_m), sy(p.elevation_m)});
    } else if (!run.empty()) {
      L.runs.push_back(std::move(run));
      run.clear();
    }
  }
  if (!run.empty()) L.runs.push_back(std::move(run));
  for (double d : markers_m_)
    if (d > 0.0 && d < r.total_distance_m) L.vertex_x.push_back(sx(d));
  return L;
}

std::string ProfileChart::Stats() const {
  if (!result_) return std::string();
  const analysis::ProfileResult& r = *result_;
  const std::string eu = ElevationUnit().first;
  const double per_m = ElevationUnit().second;
  const std::string sep = "  ·  ";
  auto elev = [&](const char* what, double m) {
    return sep + what + " " + Grouped(m * per_m, 0) + " " + eu;
  };
  std::string s = analysis::FormatRange(r.total_distance_m, style_, 2);
  s += elev("min", r.min_m) + elev("max", r.max_m) + elev("gain", r.gain_m) +
       elev("loss", r.loss_m);
  s += sep + std::to_string(r.points.size()) + " samples";
  if (r.no_data_count > 0) s += sep + std::to_string(r.no_data_count) + " no data";
  if (r.step_was_clamped) s += sep + "step clamped to the posts";
  return s;
}

std::string ProfileChart::ReadoutAt(double x_pt, double width_pt, double height_pt) const {
  const ProfileLayout L = Layout(width_pt, height_pt);
  if (!L.has_plot || x_pt < L.x0 || x_pt > L.x1) return std::string();
  const analysis::ProfileResult& r = *result_;
  const double d_m = r.total_distance_m * (x_pt - L.x0) / std::max(1.0, L.x1 - L.x0);
  // The nearest sample, so the readout matches what was drawn.
  const analysis::ProfilePoint* best = &r.points.front();
  for (const analysis::ProfilePoint& p : r.points)
    if (std::fabs(p.distance_m - d_m) < std::fabs(best->distance_m - d_m)) best = &p;
  const auto [eu, per_m] = ElevationUnit();
  const std::string e =
      best->has_data ? Grouped(best->elevation_m * per_m, 0) + " " + eu : "no data";
  char pos[64];
  std::snprintf(pos, sizeof pos, "%.5f, %.5f", best->at.lat, best->at.lon);
  return analysis::FormatRange(best->distance_m, style_, 2) + "   " + e + "   " + pos;
}

}  // namespace desk
}  // namespace fv
