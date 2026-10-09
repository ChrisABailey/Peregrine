// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_view_ladder.h — the zoom steps of one map group (port/desktop-plan.md §1d).
///
/// A step means "the next map", chosen from the products whose coverage holds
/// the point under the cursor:
/// - series ladder (raster): the display scale jumps to the next product's
///   native scale, whatever its format; past the last one the ladder stops.
/// - uniform ladder (elevation, OSM, ENC, DNC): the display scale moves by a
///   fixed factor, and the product drawn is the one whose native scale is
///   nearest the display scale by ratio. Where nothing covers the cursor the
///   current product is kept and magnified.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
class Catalog;

namespace view {

/// One map series a ladder can draw.
struct LadderProduct {
  int64_t series_id = 0;
  std::string format;
  std::string series_key;
  double scale_denom = 0;  // native 1:N, > 0
};

enum class LadderKind { kSeries, kUniform };

enum class LadderOutcome {
  kStepped,     ///< the display scale changed (the product may have too)
  kEndOfLadder, ///< series ladder: no further product under the cursor
  kNoProduct,   ///< nothing to draw at the cursor and no current product
};

/// What a step or settle decided. `display_denom` is the scale to show;
/// `has_product` is false only with kNoProduct and no current product.
struct LadderStep {
  LadderOutcome outcome = LadderOutcome::kNoProduct;
  double display_denom = 0;
  bool has_product = false;
  LadderProduct product;
  bool product_changed = false;
};

/// The candidate products whose coverage holds a position.
using ProductsAt = std::function<std::vector<LadderProduct>(const GeoPoint&)>;

class ScaleLadder {
 public:
  explicit ScaleLadder(LadderKind kind, double uniform_factor = 2.0);

  LadderKind Kind() const { return kind_; }
  double UniformFactor() const { return factor_; }

  /// One step from `display_denom`. `direction` > 0 zooms in (smaller
  /// denominators), < 0 out. `current` may be null.
  LadderStep Step(double display_denom, const LadderProduct* current,
                  int direction,
                  const std::vector<LadderProduct>& candidates) const;

  /// Where a continuous zoom (a pinch) comes to rest. Series: the candidate
  /// scale nearest `display_denom`. Uniform: the scale is kept and the
  /// nearest product chosen for it.
  LadderStep Settle(double display_denom, const LadderProduct* current,
                    const std::vector<LadderProduct>& candidates) const;

 private:
  LadderKind kind_;
  double factor_;
};

/// The product nearest `display_denom` by ratio. Ties go to `current`, then
/// to the finer scale, then the lower series id. Null when `candidates` is
/// empty.
const LadderProduct* NearestProduct(double display_denom,
                                    const LadderProduct* current,
                                    const std::vector<LadderProduct>& candidates);

/// A display 1:N for a series the catalog stores at scale 0, or 0 for none.
using SeriesScale =
    std::function<double(const std::string& format, const std::string& series_key)>;

/// The catalog's series in `formats` with coverage holding `p`, one entry per
/// series, finest first. A series catalogued at scale 0 takes its scale from
/// `nominal`; one with neither is left out.
std::vector<LadderProduct> CatalogProductsAt(const Catalog& catalog,
                                             const GeoPoint& p,
                                             const std::vector<std::string>& formats,
                                             const SeriesScale& nominal = nullptr);

}  // namespace view
}  // namespace fv
