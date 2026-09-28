// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/tide.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fv {
namespace nav {
namespace {

using json = nlohmann::json;

constexpr double kPi = 3.14159265358979323846;

std::string GetString(const json& o, const char* key) {
  auto it = o.find(key);
  if (it == o.end() || !it->is_string()) return std::string();
  return it->get<std::string>();
}

bool GetNumber(const json& o, const char* key, double* out) {
  auto it = o.find(key);
  if (it == o.end() || !it->is_number()) return false;
  *out = it->get<double>();
  return true;
}

/// The half-cycle from `a` to `b` in NOAA's cosine form. Written as a weight
/// on each end, w = (1 + cos(pi * u)) / 2 with u running 0..1, so the curve
/// lands on `a` and `b` exactly rather than to within rounding.
struct HalfCycle {
  double t0, span, ha, hb;
  HalfCycle(const TideExtreme& a, const TideExtreme& b)
      : t0(a.time_s), span(b.time_s - a.time_s), ha(a.height_m), hb(b.height_m) {}
  double Fraction(double t) const { return (t - t0) / span; }
  double Height(double t) const {
    const double w = 0.5 * (1.0 + std::cos(kPi * Fraction(t)));
    return ha * w + hb * (1.0 - w);
  }
  /// Metres per second.
  double Rate(double t) const {
    return -0.5 * (ha - hb) * kPi / span * std::sin(kPi * Fraction(t));
  }
  /// The fraction u at which the weight on `a` is `w`.
  static double FractionAtWeight(double w) { return std::acos(2.0 * w - 1.0) / kPi; }
};

}  // namespace

Status TideTable::Load(const std::string& path) {
  std::ifstream f(path);
  if (!f) return Status::Error(kNotFound, "cannot open tide table " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  Status st = Parse(ss.str());
  if (!st.ok()) st.message = path + ": " + st.message;
  return st;
}

Status TideTable::Parse(const std::string& json_text) {
  *this = TideTable();
  auto fail = [](int code, const std::string& msg) { return Status::Error(code, msg); };

  json doc = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object()) return fail(kInvalidArg, "not a JSON object");

  const std::string units = GetString(doc, "units");
  if (units != "m") return fail(kUnsupported, "units \"" + units + "\" (only \"m\" is read)");

  TideTable t;
  t.datum_ = GetString(doc, "datum");
  if (!GetNumber(doc, "valid_from", &t.valid_from_) ||
      !GetNumber(doc, "valid_until", &t.valid_until_) || t.valid_until_ <= t.valid_from_)
    return fail(kInvalidArg, "no valid_from/valid_until span");

  auto st = doc.find("station");
  if (st != doc.end() && st->is_object()) {
    t.station_.id = GetString(*st, "id");
    t.station_.name = GetString(*st, "name");
    t.station_.reference_id = GetString(*st, "reference_id");
    GetNumber(*st, "lat", &t.station_.lat);
    GetNumber(*st, "lon", &t.station_.lon);
  }

  auto ex = doc.find("extremes");
  if (ex == doc.end() || !ex->is_array() || ex->size() < 2)
    return fail(kInvalidArg, "no \"extremes\" array");
  t.extremes_.reserve(ex->size());
  for (const json& row : *ex) {
    if (!row.is_array() || row.size() != 3 || !row[0].is_number() || !row[1].is_number() ||
        !row[2].is_string())
      return fail(kInvalidArg, "an extreme is not [time, height, \"H\"|\"L\"]");
    const std::string kind = row[2].get<std::string>();
    if (kind != "H" && kind != "L") return fail(kInvalidArg, "extreme type \"" + kind + "\"");
    t.extremes_.push_back({row[0].get<double>(), row[1].get<double>(), kind == "H"});
  }

  // The cosine form assumes each half-cycle runs from a high to a low or back,
  // so a table that breaks the alternation would draw a curve NOAA never did.
  for (size_t i = 1; i < t.extremes_.size(); ++i) {
    const TideExtreme& a = t.extremes_[i - 1];
    const TideExtreme& b = t.extremes_[i];
    if (b.time_s <= a.time_s) return fail(kInvalidArg, "extremes are not in time order");
    if (a.high == b.high) return fail(kInvalidArg, "two consecutive extremes of one type");
    if (a.high != (a.height_m > b.height_m))
      return fail(kInvalidArg, "a high water is not above its neighbouring low");
  }
  if (t.extremes_.front().time_s > t.valid_from_ || t.extremes_.back().time_s < t.valid_until_)
    return fail(kInvalidArg, "the extremes do not cover valid_from..valid_until");

  *this = std::move(t);
  return Status::Ok();
}

Status TideTable::CheckCoverage(double t) const {
  if (empty()) return Status::Error(kNotFound, "no tide table loaded");
  if (!(t >= valid_from_ && t <= valid_until_))
    return Status::Error(kOutOfCoverage, "time is outside the tide table");
  return Status::Ok();
}

size_t TideTable::SegmentAt(double t) const {
  auto it = std::upper_bound(extremes_.begin(), extremes_.end(), t,
                             [](double v, const TideExtreme& e) { return v < e.time_s; });
  size_t i = static_cast<size_t>(it - extremes_.begin());
  i = i == 0 ? 0 : i - 1;
  return std::min(i, extremes_.size() - 2);
}

Status TideTable::HeightAt(double t, double* height_m) const {
  Status s = CheckCoverage(t);
  if (!s.ok()) return s;
  const size_t i = SegmentAt(t);
  *height_m = HalfCycle(extremes_[i], extremes_[i + 1]).Height(t);
  return s;
}

Status TideTable::TrendAt(double t, TideTrend* trend) const {
  Status s = CheckCoverage(t);
  if (!s.ok()) return s;
  const size_t i = SegmentAt(t);
  trend->rising = extremes_[i + 1].high;
  trend->rate_m_per_h = HalfCycle(extremes_[i], extremes_[i + 1]).Rate(t) * 3600.0;
  return s;
}

std::vector<TideExtreme> TideTable::Extremes(double t0, double t1) const {
  std::vector<TideExtreme> out;
  const double lo = std::max(t0, valid_from_);
  const double hi = std::min(t1, valid_until_);
  for (const TideExtreme& e : extremes_)
    if (e.time_s >= lo && e.time_s <= hi) out.push_back(e);
  return out;
}

Status TideTable::WindowsBelow(double threshold_m, double t0, double t1,
                               std::vector<TideWindow>* windows) const {
  windows->clear();
  if (t1 < t0) return Status::Error(kInvalidArg, "window ends before it begins");
  Status s = CheckCoverage(t0);
  if (s.ok()) s = CheckCoverage(t1);
  if (!s.ok()) return s;

  for (size_t i = SegmentAt(t0); i + 1 < extremes_.size(); ++i) {
    const TideExtreme& a = extremes_[i];
    const TideExtreme& b = extremes_[i + 1];
    if (a.time_s > t1) break;
    const HalfCycle hc(a, b);

    // The fraction range [u0, u1] of this half-cycle at or below the
    // threshold, from the weight on `a` at the crossing. A threshold equal to
    // the low water gives k of exactly 0 or 1, so a touch is never a window.
    double u0 = 0.0, u1 = 1.0;
    if (hc.ha > hc.hb) {  // falling: below from the crossing to the low
      const double k = (threshold_m - hc.hb) / (hc.ha - hc.hb);
      if (k <= 0.0) continue;
      if (k < 1.0) u0 = HalfCycle::FractionAtWeight(k);
    } else {  // rising: below from the low to the crossing
      const double k = (hc.hb - threshold_m) / (hc.hb - hc.ha);
      if (k >= 1.0) continue;
      if (k > 0.0) u1 = HalfCycle::FractionAtWeight(k);
    }
    const double begin = std::max(t0, a.time_s + u0 * hc.span);
    const double end = std::min(t1, a.time_s + u1 * hc.span);
    if (end < begin) continue;
    if (!windows->empty() && begin <= windows->back().end_s)
      windows->back().end_s = std::max(windows->back().end_s, end);
    else
      windows->push_back({begin, end});
  }
  // A span clipped to one instant (t0 == t1, or a crossing at t0) is no window.
  windows->erase(std::remove_if(windows->begin(), windows->end(),
                                [](const TideWindow& w) { return w.end_s <= w.begin_s; }),
                 windows->end());
  return s;
}

}  // namespace nav
}  // namespace fv
