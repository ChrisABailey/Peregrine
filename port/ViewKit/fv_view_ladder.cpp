// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_view_ladder.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "fvkit/catalog/catalog.h"

namespace fv {
namespace view {

namespace {

// Relative tolerance for "the same scale": a series stored as 1:250,000 and a
// display scale computed back from it may differ in the last bits.
constexpr double kSameScale = 1e-9;

bool SameProduct(const LadderProduct& a, const LadderProduct* b) {
  return b != nullptr && a.series_id == b->series_id && a.format == b->format &&
         a.series_key == b->series_key;
}

// Among products at one scale, prefer the current format, then the lower id.
bool PreferAtSameScale(const LadderProduct& a, const LadderProduct& b,
                       const LadderProduct* current) {
  const bool a_fmt = current != nullptr && a.format == current->format;
  const bool b_fmt = current != nullptr && b.format == current->format;
  if (a_fmt != b_fmt) return a_fmt;
  return a.series_id < b.series_id;
}

LadderStep Result(LadderOutcome outcome, double denom, const LadderProduct* p,
                  const LadderProduct* current) {
  LadderStep s;
  s.outcome = outcome;
  s.display_denom = denom;
  if (p != nullptr) {
    s.has_product = true;
    s.product = *p;
    s.product_changed = !SameProduct(*p, current);
  }
  return s;
}

}  // namespace

ScaleLadder::ScaleLadder(LadderKind kind, double uniform_factor)
    : kind_(kind),
      factor_(uniform_factor > 1.0 && std::isfinite(uniform_factor) ? uniform_factor
                                                                     : 2.0) {}

const LadderProduct* NearestProduct(double display_denom,
                                    const LadderProduct* current,
                                    const std::vector<LadderProduct>& candidates) {
  const LadderProduct* best = nullptr;
  double best_d = 0;
  for (const LadderProduct& c : candidates) {
    if (!(c.scale_denom > 0)) continue;
    const double d = std::fabs(std::log(c.scale_denom / display_denom));
    if (best == nullptr || d < best_d - kSameScale) {
      best = &c;
      best_d = d;
      continue;
    }
    if (d > best_d + kSameScale) continue;
    // A tie: the current product, then the finer scale, then the lower id.
    const bool c_cur = SameProduct(c, current);
    const bool b_cur = SameProduct(*best, current);
    if (c_cur != b_cur) {
      if (c_cur) best = &c;
      continue;
    }
    if (c.scale_denom != best->scale_denom) {
      if (c.scale_denom < best->scale_denom) best = &c;
      continue;
    }
    if (c.series_id < best->series_id) best = &c;
  }
  return best;
}

LadderStep ScaleLadder::Step(double display_denom, const LadderProduct* current,
                             int direction,
                             const std::vector<LadderProduct>& candidates) const {
  if (direction == 0 || !(display_denom > 0))
    return Settle(display_denom, current, candidates);

  if (kind_ == LadderKind::kUniform) {
    const double next =
        direction > 0 ? display_denom / factor_ : display_denom * factor_;
    const LadderProduct* p = NearestProduct(next, current, candidates);
    if (p == nullptr) p = current;  // nothing here: keep magnifying
    return Result(p != nullptr ? LadderOutcome::kStepped : LadderOutcome::kNoProduct,
                  next, p, current);
  }

  // Series ladder: the next native scale past the display scale.
  const LadderProduct* best = nullptr;
  for (const LadderProduct& c : candidates) {
    if (!(c.scale_denom > 0)) continue;
    const bool beyond = direction > 0
                            ? c.scale_denom < display_denom * (1.0 - kSameScale)
                            : c.scale_denom > display_denom * (1.0 + kSameScale);
    if (!beyond) continue;
    if (best == nullptr) {
      best = &c;
      continue;
    }
    const bool closer = direction > 0 ? c.scale_denom > best->scale_denom
                                      : c.scale_denom < best->scale_denom;
    if (closer || (c.scale_denom == best->scale_denom &&
                   PreferAtSameScale(c, *best, current)))
      best = &c;
  }
  if (best == nullptr)
    return Result(LadderOutcome::kEndOfLadder, display_denom, current, current);
  return Result(LadderOutcome::kStepped, best->scale_denom, best, current);
}

LadderStep ScaleLadder::Settle(double display_denom, const LadderProduct* current,
                               const std::vector<LadderProduct>& candidates) const {
  const LadderProduct* p = NearestProduct(display_denom, current, candidates);
  if (p == nullptr)
    return Result(LadderOutcome::kNoProduct, display_denom, current, current);
  const double denom =
      kind_ == LadderKind::kSeries ? p->scale_denom : display_denom;
  return Result(LadderOutcome::kStepped, denom, p, current);
}

std::vector<LadderProduct> CatalogProductsAt(const Catalog& catalog,
                                             const GeoPoint& p,
                                             const std::vector<std::string>& formats,
                                             const SeriesScale& nominal) {
  std::vector<LadderProduct> out;
  std::vector<CoverageRow> rows;
  if (!catalog.SelectByGeoRect(GeoRect{p, p}, &rows).ok() || rows.empty())
    return out;
  std::vector<SeriesRow> series;
  if (!catalog.Series(&series).ok()) return out;
  std::map<int64_t, const SeriesRow*> by_id;
  for (const SeriesRow& s : series) by_id[s.id] = &s;
  const std::set<std::string> wanted(formats.begin(), formats.end());

  std::set<int64_t> seen;
  for (const CoverageRow& r : rows) {
    if (!wanted.count(r.format) || !seen.insert(r.series_id).second) continue;
    auto it = by_id.find(r.series_id);
    if (it == by_id.end()) continue;
    const SeriesRow& sr = *it->second;
    double denom = sr.scale_denom;
    if (!(denom > 0) && nominal) denom = nominal(sr.format, sr.series_key);
    if (!(denom > 0)) continue;
    LadderProduct lp;
    lp.series_id = r.series_id;
    lp.format = sr.format;
    lp.series_key = sr.series_key;
    lp.scale_denom = denom;
    out.push_back(lp);
  }
  std::sort(out.begin(), out.end(), [](const LadderProduct& a, const LadderProduct& b) {
    if (a.scale_denom != b.scale_denom) return a.scale_denom < b.scale_denom;
    return a.series_id < b.series_id;
  });
  return out;
}

}  // namespace view
}  // namespace fv
