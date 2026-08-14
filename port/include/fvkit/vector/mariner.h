// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::MarinerSettings — the depth numbers that belong to the MARINER, not to
// the chart producer, shared by every vector product that shades water.
//
// WHY THIS IS CROSS-PRODUCT AND NOT S-52's. E3a gave ENC an S52MarinerSettings
// on its own engine and the ledger left the other half open: DNC's depth ramp
// was still driven by CECDISValues' default-constructed ssdc/msdc/mssc, so a
// vessel's draft could not be entered at all. The two products turn out to be
// the same four numbers and the same two switches under different names, and
// the DNC side is not a guess — it was read off the delivered GeoSym tables:
//
//   attexp.txt row 2257  idsm = 0 and cvl >= ssdc and cvl <  msdc
//   fullsym.txt row 2257 BE010 area symbol 0820, "depth area (medium deep);
//                        4 shades"
//
// so, over the whole BE010 (depth area) block, rows 2256..2270:
//
//   S-52 name        GeoSym/DNC   what the table does with it
//   ---------------  -----------  ------------------------------------------
//   safety_contour   ssdc         the pivot: 2 shades split here (2260/2261),
//                                 4 shades put the medium band above it
//                                 (2257), and BE020's sounding text goes dark
//                                 at or below it (2318) — which is why DNC has
//                                 no separate safety_depth: ssdc is both.
//   deep_contour     msdc         top of the medium band (2256/2257).
//   shallow_contour  mssc         the very-shallow/medium-shallow split
//                                 (2258/2259).
//   two_shades       idsm         1 = two depth shades, 0 = four. The column
//                                 is named "Interactive Display Shallow Mode"
//                                 and the row comments spell it "2 shades" /
//                                 "4 shades".
//   shallow_pattern  isdm         1 = "shallow display mode on", which adds
//                                 area symbol 0949 over the shallowest bands
//                                 (2265..2270, and BA020/BD120 foreshore and
//                                 reef at 1441/2164) on top of the flat fill.
//
// DEFAULTS DIFFER PER PRODUCT AND MUST. The struct's own defaults are S-52's;
// GeoSym seeds its engine with CECDISValues' (safety 10 m, shallow 2 m, deep
// 30 m, four shades, shallow pattern ON), because those are the numbers the
// DNC goldens were pinned over and the bit-faithful rule keeps them. A caller
// that sets nothing therefore renders exactly what it rendered before this
// header existed, on either product.
//
// UNITS ARE METRES on both. VPF's cvl/hdp columns and S-57's DRVAL1/VALSOU are
// both metric, so no conversion happens anywhere along this path.

#ifndef FVKIT_VECTOR_MARINER_H_
#define FVKIT_VECTOR_MARINER_H_

namespace fv {

struct MarinerSettings {
  // Metres. Depths are compared against these exactly as S-52 §8.4 does.
  double safety_contour = 30.0;   // the bold contour; DEPARE/DEPCNT pivot here
  double shallow_contour = 2.0;   // inner edge of the shallow-water shade
  double deep_contour = 30.0;     // outer edge of the deep-water shade
  double safety_depth = 30.0;     // soundings at or below this print bold/black
  bool two_shades = false;        // two depth shades instead of four
  bool shallow_pattern = false;   // a pattern over the shallowest band

  bool operator==(const MarinerSettings& o) const {
    return safety_contour == o.safety_contour &&
           shallow_contour == o.shallow_contour &&
           deep_contour == o.deep_contour && safety_depth == o.safety_depth &&
           two_shades == o.two_shades && shallow_pattern == o.shallow_pattern;
  }
  bool operator!=(const MarinerSettings& o) const { return !(*this == o); }
};

}  // namespace fv

#endif  // FVKIT_VECTOR_MARINER_H_
