// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/scale_table.h — "look up a value by map scale", once.
//
// SHARED OVERLAY TOOLKIT (1 of 3, with canvas/label_placer.h and
// app/properties.h). Extracted from grid_map/spacing.cpp during the graticule
// port; written to be the thing the NEXT overlay uses instead of hand-rolling
// it. If you are porting an overlay whose look changes with zoom — contour
// intervals, coverage detail tiers, a declutter threshold — this is where that
// table goes.
//
// WHY IT IS A TABLE AND NOT A FORMULA. FalconView's graticule spacings are
// cartographer-chosen per chart scale (1 degree at 1:2M for latitude but 3 for
// longitude, and the ladder is not geometric anywhere), and so are contour
// intervals and every other zoom-dependent constant in the product. A computed
// "nice interval" is a different, worse product. The table IS the decision;
// this class is only the lookup.
//
// THE KEY IS A SCALE DENOMINATOR (the N in 1:N), because that is the unit the
// decisions were made in. `ScaleDenominatorFor` gets one out of any projection,
// in any of its three dpp modes — see the note on its declaration, which is the
// one thing here worth not re-deriving.

#ifndef FVKIT_SCALE_TABLE_H_
#define FVKIT_SCALE_TABLE_H_

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace fv {

class MapProjection;

// The scale denominator a projection is effectively showing, for keying a
// ScaleTable. Never 0 for a ready projection.
//
// THE THING NOT TO RE-DERIVE: `MapProjection::Scale()` returns 0 in resolution
// mode, and resolution mode is what a tile pyramid uses — which is what Pippin
// runs in. An overlay that keyed a table on `proj.Scale()` would work on the
// desktop and silently draw nothing on the phone. So this asks the projection
// for its degrees-per-pixel (always populated, every mode) and converts.
//
// The conversion is exact rather than approximate: FalconView's nominal
// degrees-lat-per-pixel is proportional to the scale denominator over its
// WHOLE range — the two branches of MapScaleUtil::GetNominalDegreesLatPerPixel
// (1:1M CADRG below 1:5M, 1 km TIROS above) have the same slope, 1.3471e-6
// deg/px per unit of denominator, and meet continuously at 1:5M. So
// MapScaleUtil::ResolutionToScale(dpp, MAP_SCALE_ARC_DEGREES) is that
// function's inverse everywhere, not just on one branch.
//
// A projection in kScale or kPhysical mode reports its own denominator
// directly, since that is what the caller asked for.
double ScaleDenominatorFor(const MapProjection& proj);

// A value chosen per map scale. `T` is whatever the overlay needs — a spacing
// struct, an interval, a bool.
//
// Rows may be given in any order; construction sorts them. A table with no
// rows answers nullptr, which is a legal "this overlay draws nothing".
template <typename T>
class ScaleTable {
 public:
  struct Row {
    double scale_denominator;  // the N in 1:N; must be > 0
    T value;
  };

  ScaleTable() = default;
  ScaleTable(std::initializer_list<Row> rows) : rows_(rows) { Sort(); }
  explicit ScaleTable(std::vector<Row> rows) : rows_(std::move(rows)) {
    Sort();
  }

  // The row whose scale is CLOSEST to `scale_denominator`, or nullptr when the
  // table is empty. Never interpolates and never fails: a scale outside the
  // table clamps to its nearest end, which is what FalconView's
  // get_closest_scale did and is the right answer for a table of discrete
  // cartographic decisions — halfway between two authored looks there is no
  // third look, only the nearer of the two.
  //
  // "Closest" is absolute difference in the DENOMINATOR, which by the identity
  // above is the same ordering as FalconView's comparison in
  // degrees-lat-per-pixel. Ties go to the finer scale (the smaller N), so a
  // user zooming in gets the denser look at the midpoint rather than after it.
  const T* Nearest(double scale_denominator) const {
    if (rows_.empty()) return nullptr;
    const Row* best = &rows_.front();
    double best_d = Dist(best->scale_denominator, scale_denominator);
    for (size_t i = 1; i < rows_.size(); ++i) {
      const double d = Dist(rows_[i].scale_denominator, scale_denominator);
      if (d < best_d) {
        best = &rows_[i];
        best_d = d;
      }
    }
    return &best->value;
  }

  // The same, keyed off a projection. The form an OnDraw actually wants.
  const T* Nearest(const MapProjection& proj) const {
    return Nearest(ScaleDenominatorFor(proj));
  }

  // Exact key match, for a test that wants to assert a specific row rather
  // than the nearest one.
  const T* Exact(double scale_denominator) const {
    for (const Row& r : rows_)
      if (r.scale_denominator == scale_denominator) return &r.value;
    return nullptr;
  }

  bool empty() const { return rows_.empty(); }
  size_t size() const { return rows_.size(); }
  // Ascending by scale denominator (finest first).
  const std::vector<Row>& rows() const { return rows_; }

 private:
  static double Dist(double a, double b) { return a > b ? a - b : b - a; }
  void Sort() {
    // Insertion sort: these tables are ~20 rows built once at startup, and
    // pulling in <algorithm> for it would put a header in every consumer.
    for (size_t i = 1; i < rows_.size(); ++i) {
      Row r = rows_[i];
      size_t j = i;
      while (j > 0 && rows_[j - 1].scale_denominator > r.scale_denominator) {
        rows_[j] = rows_[j - 1];
        --j;
      }
      rows_[j] = r;
    }
  }

  std::vector<Row> rows_;
};

}  // namespace fv

#endif  // FVKIT_SCALE_TABLE_H_
