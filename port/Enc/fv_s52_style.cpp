// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_s52_style.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "fv_enc_vector_source.h"  // kEncShoalerContourAttribute
#include "fvkit/vector/renderer.h"  // kHimetricPerHundredthInch, the placer

namespace fv {
namespace {

// S-52's nominal pen unit: LS/LC widths and the HPGL's SWn are both counted in
// 0.32 mm steps, so a chart keeps its authored line weights on any device.
constexpr double kS52PenMm = 0.32;
constexpr double kMmPerInch = 25.4;

// The library's "unknown symbol" mark. S-52 has one for exactly the situation
// this engine hits when a CS procedure is not implemented yet or a lookup
// names a symbol the delivered library does not define.
constexpr char kQuestionMark[] = "QUESMRK1";

// A trailing ')' survives on the last parameter of a handful of lookup rows
// whose instruction text is malformed in the delivered XML — the one E2
// documented, `SY(LNDARE01);CS(QUAPOS01;TX(OBJNAM,...,26))`, loses a paren and
// leaves "26)". Numbers and colour tokens are read through this so a data bug
// in one row cannot mis-colour a feature.
std::string Clean(const std::string& s) {
  size_t end = s.size();
  while (end > 0 && (s[end - 1] == ')' || s[end - 1] == ' ')) --end;
  size_t begin = 0;
  while (begin < end && s[begin] == ' ') ++begin;
  return s.substr(begin, end - begin);
}

double ToDouble(const std::string& s, double fallback) {
  const std::string t = Clean(s);
  if (t.empty()) return fallback;
  char* endp = nullptr;
  const double v = std::strtod(t.c_str(), &endp);
  if (endp == t.c_str()) return fallback;
  return v;
}

// Attribute-condition comparison is the SHARED rule now (E3c): rules.h's
// RuleValueAsNumber / RuleValuesEqual, so a lookup row's ATTC condition and a
// user rule's `where` clause resolve a value identically instead of through two
// copies of the same three lines. Numeric when BOTH sides parse whole, byte-wise
// otherwise — so "3" matches "3.0" but "3,1" (a list attribute's value) stays an
// exact string match, which is what S-52's COLOUR conditions depend on.

double AttrNumber(const VectorFeature& f, const char* acronym,
                  double fallback) {
  const std::string* v = f.Attribute(acronym);
  if (v == nullptr || v->empty()) return fallback;
  return ToDouble(*v, fallback);
}

// True when any member of a (possibly list-valued) attribute is one of
// `values`. S-57 list attributes arrive comma-separated, and S-52 conditions
// on them are "contains", not "equals".
bool AttrIn(const VectorFeature& f, const char* acronym,
            const std::vector<int>& values) {
  const std::string* v = f.Attribute(acronym);
  if (v == nullptr || v->empty()) return false;
  size_t pos = 0;
  while (pos <= v->size()) {
    const size_t comma = v->find(',', pos);
    const std::string one = v->substr(
        pos, comma == std::string::npos ? std::string::npos : comma - pos);
    const int n = std::atoi(one.c_str());
    if (std::find(values.begin(), values.end(), n) != values.end()) return true;
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return false;
}

// MARK: Light sectors

/// Prefix of the generated light-sector symbol ids. The id carries the whole
/// geometry, `LTSECT_<sectr1>_<sectr2>_<radius mm>_<colour token>`, so it
/// passes through SY() and Symbol() with no per-feature state.
constexpr char kLightSectorPrefix[] = "LTSECT_";

// LIGHTS05's screen distances, in mm.
constexpr double kSectorArcRadiusMm = 20.0;
constexpr double kAllRoundArcRadiusMm = 25.0;
constexpr double kSectorLegLengthMm = 25.0;
constexpr double kSectorLegDashMm = 3.6;  // LS(DASH): 3.6 mm on, 1.8 mm off
constexpr double kSectorLegGapMm = 1.8;
constexpr double kHimetricPerMm = 100.0;
constexpr double kPenHimetric = kS52PenMm * kHimetricPerMm;

bool IsLightSectorId(const std::string& id) {
  return id.compare(0, sizeof(kLightSectorPrefix) - 1, kLightSectorPrefix) == 0;
}

std::string LightSectorId(double sectr1, double sectr2, double radius_mm,
                          const char* colour) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%s%g_%g_%g_%s", kLightSectorPrefix, sectr1,
                sectr2, radius_mm, colour);
  return buf;
}

/// Point at `mm` along compass bearing `deg`, in HIMETRIC with y up.
SymbolPoint AlongBearing(double deg, double mm) {
  const double r = deg * 3.14159265358979323846 / 180.0;
  return SymbolPoint{std::sin(r) * mm * kHimetricPerMm,
                     std::cos(r) * mm * kHimetricPerMm};
}

SymbolPrimitive Stroke(std::vector<SymbolPoint> pts, FvColor color,
                       double width) {
  SymbolPrimitive p;
  p.type = SymbolPrimitiveType::kPolyline;
  p.points = std::move(pts);
  p.has_stroke = true;
  p.stroke_color = color;
  p.stroke_width = width;
  return p;
}

/// Builds the display list a light-sector id names: dashed black legs along
/// both sector limits and an arc between them, the light colour over a wider
/// OUTLW pen. Sector limits are bearings from seaward, so the drawing lies on
/// the reciprocals. A 360-degree span is a full circle without legs. Colours
/// come from `color` (the active scheme). False when the id is malformed.
bool BuildLightSector(const std::string& id,
                      const std::function<FvColor(const std::string&)>& color,
                      VectorSymbol* out) {
  if (!IsLightSectorId(id)) return false;
  std::vector<std::string> fields;
  size_t pos = sizeof(kLightSectorPrefix) - 1;
  while (true) {
    const size_t sep = id.find('_', pos);
    fields.push_back(id.substr(pos, sep == std::string::npos ? sep : sep - pos));
    if (sep == std::string::npos) break;
    pos = sep + 1;
  }
  if (fields.size() != 4) return false;
  const double s1 = ToDouble(fields[0], NAN);
  const double s2 = ToDouble(fields[1], NAN);
  const double radius = ToDouble(fields[2], NAN);
  if (!std::isfinite(s1) || !std::isfinite(s2) || !(radius > 0.0)) return false;

  double span = std::fmod(s2 - s1 + 720.0, 360.0);
  const bool all_round = span < 1e-6 || std::fabs(s2 - s1) >= 360.0;
  if (all_round) span = 360.0;
  const double start = s1 + 180.0;

  out->primitives.clear();
  if (!all_round) {
    const FvColor black = color("CHBLK");
    for (double leg : {start, s2 + 180.0}) {
      for (double d = 0.0; d < kSectorLegLengthMm;
           d += kSectorLegDashMm + kSectorLegGapMm) {
        const double e = (std::min)(d + kSectorLegDashMm, kSectorLegLengthMm);
        out->primitives.push_back(Stroke(
            {AlongBearing(leg, d), AlongBearing(leg, e)}, black, kPenHimetric));
      }
    }
  }
  std::vector<SymbolPoint> arc;
  const int steps = (std::max)(2, static_cast<int>(std::ceil(span / 2.0)));
  for (int i = 0; i <= steps; ++i)
    arc.push_back(AlongBearing(start + span * i / steps, radius));
  out->primitives.push_back(Stroke(arc, color("OUTLW"), 4 * kPenHimetric));
  out->primitives.push_back(
      Stroke(std::move(arc), color(fields[3]), 2 * kPenHimetric));

  const double reach =
      ((std::max)(radius, all_round ? 0.0 : kSectorLegLengthMm) +
       kS52PenMm * 2) * kHimetricPerMm;
  out->min_x = out->min_y = -reach;
  out->max_x = out->max_y = reach;
  return true;
}

int PriorityOf(const S52Lookup& l) { return l.display_priority; }

// S-52's display category onto the cross-product axis in fvkit/vector/rules.h.
// Mariners' additions ride with OTHER: they are switchable, which is the only
// property the threshold cares about.
int CategoryOf(S52DisplayCategory c) {
  switch (c) {
    case S52DisplayCategory::kDisplayBase: return kDisplayBase;
    case S52DisplayCategory::kStandard: return kDisplayStandard;
    default: return kDisplayOther;
  }
}

}  // namespace

// ---------------------------------------------------------------------------

struct S52StyleEngine::Impl {
  S52PresentationLibrary lib;
  S52ColorScheme scheme = S52ColorScheme::kDay;
  S52PointStyle point_style = S52PointStyle::kPaperChart;
  S52AreaStyle area_style = S52AreaStyle::kPlainBoundaries;
  // NOTE the mariner settings are NOT here: they moved onto the shared
  // LookupTableStyleEngine core when the DNC half of the same API landed
  // (fvkit/vector/mariner.h). S-52's defaults ARE the struct's defaults, so
  // nothing this engine draws moved with them.
  bool show_meta = false;
  bool show_buoy_names = true;
  bool show_beacon_names = true;
  bool show_light_descriptions = true;

  std::map<std::string, size_t> unhandled_cs;
  size_t placeholders = 0;
  size_t sector_lights_simplified = 0;

  // Generated light-sector symbols, keyed by colour scheme and id. Entries
  // are never erased, so handed-out pointers stay valid for the engine's life.
  std::mutex light_mu;
  std::map<std::string, std::unique_ptr<VectorSymbol>> light_symbols;

  // --- helpers ------------------------------------------------------------

  /// The display list for a light-sector id in the active colour scheme, or
  /// nullptr when the id is malformed.
  const VectorSymbol* LightSector(const std::string& id) {
    const std::string key = std::to_string(static_cast<int>(scheme)) + id;
    std::lock_guard<std::mutex> lock(light_mu);
    auto it = light_symbols.find(key);
    if (it != light_symbols.end()) return it->second.get();
    auto sym = std::make_unique<VectorSymbol>();
    if (!BuildLightSector(
            id, [this](const std::string& t) { return Color(t); }, sym.get()))
      sym.reset();
    return (light_symbols[key] = std::move(sym)).get();
  }

  FvColor Color(const std::string& token) const {
    FvColor c;
    if (!lib.Color(Clean(token), &c)) {
      // An unresolvable colour draws black rather than dropping the primitive,
      // the same call ParseS52Hpgl makes: a chart that loses a colour should
      // look wrong, not lose the feature.
      c = FvColor{0, 0, 0, 255};
    }
    return c;
  }

  S52LookupTable TableFor(VectorGeometryType t) const {
    switch (t) {
      case VectorGeometryType::kPoint:
        return point_style == S52PointStyle::kPaperChart
                   ? S52LookupTable::kPaperChart
                   : S52LookupTable::kSimplified;
      case VectorGeometryType::kArea:
        return area_style == S52AreaStyle::kPlainBoundaries
                   ? S52LookupTable::kPlainBoundaries
                   : S52LookupTable::kSymbolizedBoundaries;
      default:
        return S52LookupTable::kLines;
    }
  }

  /// True when every attribute condition of lookup row `l` holds for `f`.
  /// SelectLookup decides between several matching rows.
  bool Matches(const S52Lookup& l, const VectorFeature& f) const {
    for (const S52LookupAttribute& cond : l.attributes) {
      const std::string* have = f.Attribute(cond.acronym);
      if (have == nullptr) return false;
      const std::string want = cond.value;
      // "?" is S-57's UNKNOWN-VALUE marker, not a wildcard: the attribute must
      // be present with a null (empty) value. Getting this backwards is not a
      // subtle error — DEPARE's first row is
      //   [DRVAL1=?][DRVAL2=?] -> AC(NODTA);AP(PRTSUR01);LS(SOLD,2,CHGRD)
      // i.e. "unsurveyed", and read as a wildcard it matches EVERY depth area,
      // paints the whole harbour no-data grey and the depth ramp never runs.
      // All 479 DEPARE areas in the Charleston cells carry real DRVAL1/DRVAL2
      // values and none carries an empty one, which is what settles the
      // reading from the data rather than from the spec alone.
      if (want == "?") {
        if (!have->empty()) return false;
        continue;
      }
      // The blank forms are the wildcard: the row states no value, so presence
      // (established above) is the whole condition.
      if (want.empty() || want == " ") continue;
      if (!RuleValuesEqual(*have, want)) return false;
    }
    return true;
  }

};

// ---------------------------------------------------------------------------
// Conditional symbology
// ---------------------------------------------------------------------------
//
// A CS procedure returns an INSTRUCTION STRING, which is what the S-52 spec
// says a procedure produces, and the caller runs it through the same executor
// as a table row's own instructions. Building the string costs a few dozen
// bytes per feature and buys procedures that read like the published
// pseudo-code they implement.
//
// PROVENANCE: unlike everything else in this engine, these are NOT read out of
// the delivered data — chartsymbols.xml carries lookup rows and symbols, not
// procedure code. They are written from the published S-52 Presentation
// Library procedures, using attribute values whose MEANINGS are grounded in
// the Appendix A catalogue E2 loaded (s57expectedinput.csv), and each one says
// which parts it reduces. Anything not implemented is registered nowhere, gets
// counted, and draws the question mark.

namespace {

using CsProc = std::function<std::string(const VectorFeature&,
                                         const S52MarinerSettings&)>;

// SEABED01 — the depth ramp, and the reason an ENC looks like a chart. Picks
// one of the DEP* shades for a depth range [drval1, drval2] against the
// mariner's contours. Reduced in one documented way: the shallow-water
// pattern is emitted only when the mariner asks for it.
std::string Seabed01(double drval1, double drval2,
                     const S52MarinerSettings& m) {
  const char* color = "DEPIT";  // intertidal, the shallowest band
  bool shallow = true;
  if (drval1 >= 0.0 && drval2 > 0.0) color = "DEPVS";
  if (m.two_shades) {
    if (drval1 >= m.safety_contour && drval2 > m.safety_contour) {
      color = "DEPDW";
      shallow = false;
    }
  } else {
    if (drval1 >= m.shallow_contour && drval2 > m.shallow_contour)
      color = "DEPMS";
    if (drval1 >= m.safety_contour && drval2 > m.safety_contour)
      color = "DEPMD";
    if (drval1 >= m.deep_contour && drval2 > m.deep_contour) {
      color = "DEPDW";
      shallow = false;
    }
  }
  std::string out = std::string("AC(") + color + ")";
  if (m.shallow_pattern && shallow) out += ";AP(DIAMOND1)";
  return out;
}

/// DEPCNT02 — depth contours. The safety contour (wide, DEPSC) is the cell's
/// shoalest contour at or deeper than the mariner's setting: VALDCO is not
/// shoaler than the setting, and the next shoaler contour in the cell
/// (kEncShoalerContourAttribute) is shoaler than it or absent. Every other
/// contour is a thin DEPCN. Reduced: DEPARE boundaries are not promoted
/// to contours, which needs the neighbouring area's depth range.
std::string DepCnt02(const VectorFeature& f, const S52MarinerSettings& m) {
  constexpr double kEps = 1e-6;
  const double valdco = AttrNumber(f, "VALDCO", -1.0);
  bool safety = valdco >= 0.0 && valdco >= m.safety_contour - kEps;
  if (safety) {
    const double shoaler = AttrNumber(f, kEncShoalerContourAttribute, -1.0);
    const bool has_shoaler = f.Attribute(kEncShoalerContourAttribute) != nullptr;
    safety = !has_shoaler || shoaler < m.safety_contour - kEps;
  }
  return safety ? "LS(SOLD,2,DEPSC)" : "LS(SOLD,1,DEPCN)";
}

// DEPARE01 — depth areas get the ramp; a DEPARE that arrives as a LINE is a
// contour and goes to DEPCNT02, which is what the Lines table's own
// CS(DEPCNT02) row does for DEPARE too.
std::string DepAre01(const VectorFeature& f, const S52MarinerSettings& m) {
  if (f.type == VectorGeometryType::kLine) return DepCnt02(f, m);
  // "Missing DRVAL1 is treated as -1 and missing DRVAL2 as DRVAL1" — the
  // spec's own defaults, which is what puts an unsurveyed area in the
  // shallowest shade rather than the deepest.
  const double drval1 = AttrNumber(f, "DRVAL1", -1.0);
  const double drval2 = AttrNumber(f, "DRVAL2", drval1);
  return Seabed01(drval1, drval2, m);
}

// QUAPOS01 — position quality. QUAPOS 2..9 are the "inaccurate" values in
// Appendix A; they get the low-accuracy symbology, everything else gets the
// plain coastline. Reduced: the spec dispatches the accurate branch on the
// CALLING object class (COALNE and SLCONS differ); this uses the coastline
// pen for lines, which is what both of the callers in the Charleston cells
// (COALNE, LNDARE) resolve to anyway.
std::string QuaPos01(const VectorFeature& f, const S52MarinerSettings&) {
  const bool low = AttrIn(f, "QUAPOS", {2, 3, 4, 5, 6, 7, 8, 9});
  if (f.type == VectorGeometryType::kPoint)
    return low ? "SY(LOWACC01)" : std::string();
  return low ? "LC(LOWACC21)" : "LS(SOLD,1,CSTLN)";
}

// SLCONS03 — shoreline construction. Reduced to the condition/water-level
// branches: ruined or under-construction work is dashed, everything else is
// the solid coastline pen, and a point instance takes the land symbol.
// Not here: the spec's treatment of a SLCONS that bounds a floating dock or
// carries a different pen per CATSLC, which needs the neighbouring area.
std::string SlCons03(const VectorFeature& f, const S52MarinerSettings&) {
  if (f.type == VectorGeometryType::kPoint) return "SY(LNDARE01)";
  // CONDTN 1 = under construction, 2 = ruined (Appendix A).
  if (AttrIn(f, "CONDTN", {1, 2})) return "LS(DASH,1,CSTLN)";
  // WATLEV 3 = always dry, 4 = covers and uncovers, 5 = awash.
  if (AttrIn(f, "WATLEV", {4, 5})) return "LS(DASH,2,CSTLN)";
  return "LS(SOLD,2,CSTLN)";
}

/// SNDFRM03: the sounding figure, as the engine-internal instruction SN(depth
/// in metres), preceded by the frame symbols the feature's attributes call for.
/// The executor sets the figure as text in the mariner's depth unit
/// (SoundingLabels). The library's own digit symbols (SOUNDS/SOUNDG positions
/// 0-5) are 6x10 raster tiles with a baked-in shadow, which turn blocky when
/// scaled; the text keeps their layout and colours.
///
/// Frame symbols, SOUNDS at or shallower than the safety depth, SOUNDG deeper:
/// B1 swept (TECSOU 6), C2 low accuracy (QUASOU 3, 4, 5, 8, 9), A1 the
/// drying-height bar under a negative depth. Not read: QUAPOS, which S-57
/// keeps on the spatial record rather than the feature.
std::string SoundingInstruction(const VectorFeature& f, double depth,
                                const S52MarinerSettings& m) {
  const std::string prefix = depth <= m.safety_depth ? "SOUNDS" : "SOUNDG";
  std::string out;
  if (AttrIn(f, "TECSOU", {6})) out += "SY(" + prefix + "B1);";
  if (AttrIn(f, "QUASOU", {3, 4, 5, 8, 9})) out += "SY(" + prefix + "C2);";
  if (depth < 0.0) out += "SY(" + prefix + "A1);";
  char buf[48];
  std::snprintf(buf, sizeof(buf), "SN(%.4f)", depth);
  return out + buf;
}

// SOUNDG02 — soundings. The source publishes the depth as the pseudo-attribute
// DEPTH (see fv_enc_vector_source.cpp) because S-57 keeps it in the geometry,
// not in an attribute.
std::string Soundg02(const VectorFeature& f, const S52MarinerSettings& m) {
  const std::string* depth = f.Attribute("DEPTH");
  if (depth == nullptr || depth->empty()) return std::string();
  return SoundingInstruction(f, ToDouble(*depth, 0.0), m);
}

// UDWHAZ03 — "is this underwater feature a danger to the mariner?" The one
// question OBSTRN04 and WRECKS02 both turn on. REDUCED, and the reduction is
// the interesting part: the published procedure also asks whether the feature
// is surrounded by water deeper than the safety contour (an isolated danger in
// safe water gets the loud ISODGR symbol, one inside a shoal does not), and
// that needs the neighbouring DEPARE — i.e. the retained scene R3 builds. Here
// the depth test alone decides, so an isolated-danger mark can appear inside
// shoal water where a full ECDIS would leave it plain. That errs toward
// warning the mariner, which is the right direction to err.
bool IsDangerDepth(double valsou, const S52MarinerSettings& m) {
  return valsou <= m.safety_contour;
}

// The symbol for an underwater feature whose depth IS known.
const char* DangerSymbol(double valsou, const S52MarinerSettings& m) {
  // ISODGR01, not the ISODGR51 the spec names: the delivered library defines
  // 51 as RASTER-ONLY (no HPGL) and 01 as the vector twin with the same
  // description, "isolated danger of depth less than the safety contour". A
  // vector-only path can only draw the one that has geometry — the same
  // constraint E3a's 17 raster-only names document. Same substitution below
  // for OBSTRN11 -> OBSTRN18.
  if (IsDangerDepth(valsou, m)) return "ISODGR01";
  // DANGER01 is the defined-depth hazard, DANGER02 the one deeper than 20 m —
  // the library's own descriptions.
  return valsou > 20.0 ? "DANGER02" : "DANGER01";
}

// The sounding a hazard carries, set the way SOUNDG02 sets one — the same
// SNDFRM03 call the published procedures make, so a wreck's depth and the
// sounding beside it are the same typography instead of two different ones.
std::string ValsouText(const VectorFeature& f, const S52MarinerSettings& m) {
  const std::string* v = f.Attribute("VALSOU");
  if (v == nullptr || v->empty()) return std::string();
  return ";" + SoundingInstruction(f, ToDouble(*v, 0.0), m);
}

// OBSTRN04 — obstructions: the single heaviest procedure in these cells (105
// features). Reduced in two stated ways: UDWHAZ03's neighbouring-depth test
// (above), and the spec's separate handling of a foul area, which needs the
// FOULAR object class the Charleston cells do not carry.
std::string Obstrn04(const VectorFeature& f, const S52MarinerSettings& m) {
  const std::string* valsou = f.Attribute("VALSOU");
  const bool has_depth = valsou != nullptr && !valsou->empty();
  const double depth = has_depth ? ToDouble(*valsou, 0.0) : 0.0;

  if (f.type == VectorGeometryType::kLine) {
    // A line obstruction is a dashed danger line at either weight.
    return has_depth && IsDangerDepth(depth, m) ? "LS(DASH,2,CHBLK)"
                                                : "LS(DASH,2,CHGRD)";
  }
  if (f.type == VectorGeometryType::kArea) {
    if (!has_depth) {
      // No depth: a foul area is patterned, otherwise WATLEV picks the fill —
      // dry ground, intertidal, or very shallow water.
      if (AttrIn(f, "CATOBS", {6})) return "AP(FOULAR01);LS(DOTT,2,CHBLK)";
      if (AttrIn(f, "WATLEV", {1, 2})) return "AC(CHBRN);LS(SOLD,2,CSTLN)";
      if (AttrIn(f, "WATLEV", {4})) return "AC(DEPIT);LS(DASH,2,CSTLN)";
      return "AC(DEPVS);LS(DOTT,2,CHBLK)";
    }
    // A stated depth takes the depth ramp and the dotted hazard boundary.
    std::string out = Seabed01(depth, depth, m) + ";LS(DOTT,2,CHBLK)";
    if (IsDangerDepth(depth, m))
      out += std::string(";SY(") + DangerSymbol(depth, m) + ")";
    return out + ValsouText(f, m);
  }

  if (has_depth)
    return std::string("SY(") + DangerSymbol(depth, m) + ")" +
           ValsouText(f, m);

  // No depth: WATLEV decides, using the library's own OBSTRN descriptions.
  // 1 = partly submerged at high water, 2 = always dry, 3 = always under
  // water, 4 = covers and uncovers, 5 = awash.
  if (AttrIn(f, "WATLEV", {1, 2})) return "SY(OBSTRN18)";
  if (AttrIn(f, "WATLEV", {4, 5})) return "SY(OBSTRN03)";
  return "SY(OBSTRN01)";  // "obstruction, depth not stated"
}

// WRECKS02 — wrecks. Same danger machinery as OBSTRN04 plus CATWRK, and the
// QUASOU=7 ("least depth unknown") over-line the library ships as WRECKS07.
std::string Wrecks02(const VectorFeature& f, const S52MarinerSettings& m) {
  if (f.type != VectorGeometryType::kPoint) return Obstrn04(f, m);

  const std::string* valsou = f.Attribute("VALSOU");
  const bool has_depth = valsou != nullptr && !valsou->empty();
  std::string out;
  if (has_depth) {
    out = std::string("SY(") + DangerSymbol(ToDouble(*valsou, 0.0), m) + ")";
  } else if (AttrIn(f, "CATWRK", {4, 5})) {
    out = "SY(WRECKS01)";  // hull or superstructure showing
  } else if (AttrIn(f, "CATWRK", {1})) {
    out = "SY(WRECKS04)";  // non-dangerous, depth unknown
  } else {
    // CATWRK 2/3 and an unstated category both take the DANGEROUS mark: a
    // wreck of unknown category is not a safe wreck.
    out = "SY(WRECKS05)";
  }
  // QUASOU 7 = least depth unknown. WRECKS07 ("line above WRECKS with
  // QUASOU=7") is raster-only in the delivered library, so this deliberately
  // draws the question mark beside the wreck rather than dropping the fact:
  // "the depth over this wreck is unknown and this library cannot draw the
  // mark for it" is information, and plan section 7 says never to lose it
  // silently.
  if (AttrIn(f, "QUASOU", {7})) out += ";SY(WRECKS07)";
  return out + ValsouText(f, m);
}

// Which way a light flare points, as a bearing (clockwise from up, the sense
// the renderer rotates a symbol in and the sense S-52's ORIENT is written in).
//
// The HPGL flare rises straight UP from its pivot, and drawn unrotated it grew
// out of the top of the buoy it belongs to. The delivered library says where
// it really goes, in its own RASTER copy of the same symbol: LIGHTS11/12/13's
// tile puts the light position at the tile corner (bitmap pivot 0,0) with the
// bulb down and to the right — ink centroid at bearing 133.5 degrees, its
// farthest lit pixel at 131.6, both 135 to within a 22x22 tile's rounding.
// So the two forms of one symbol agree once the display list is turned 135.
constexpr int kFlareBearingDeg = 135;

/// Flare symbol for a light's COLOUR: the first match in LIGHTS05's order of
/// precedence (red, green, then the white family), LITDEF11 otherwise.
const char* LightFlare(const VectorFeature& f) {
  // S-57 COLOUR: 1 white, 3 red, 4 green, 6 yellow, 9 amber, 11 orange,
  // 12 magenta.
  if (AttrIn(f, "COLOUR", {3})) return "LIGHTS11";
  if (AttrIn(f, "COLOUR", {4})) return "LIGHTS12";
  if (AttrIn(f, "COLOUR", {1, 6, 9, 11})) return "LIGHTS13";
  if (AttrIn(f, "COLOUR", {12})) return "LIGHTS14";
  return "LITDEF11";
}

/// Arc colour token for a light, with the same precedence as LightFlare.
const char* LightArcColour(const VectorFeature& f) {
  if (AttrIn(f, "COLOUR", {3})) return "LITRD";
  if (AttrIn(f, "COLOUR", {4})) return "LITGN";
  if (AttrIn(f, "COLOUR", {1, 6, 9, 11})) return "LITYW";
  return "CHMGD";
}

/// Reads SECTR1/SECTR2. False when either is missing or the pair spans the
/// full circle, which S-52 treats as an all-round light.
bool LightSectors(const VectorFeature& f, double* s1, double* s2) {
  *s1 = AttrNumber(f, "SECTR1", NAN);
  *s2 = AttrNumber(f, "SECTR2", NAN);
  if (!std::isfinite(*s1) || !std::isfinite(*s2)) return false;
  const double span = std::fmod(*s2 - *s1 + 720.0, 360.0);
  return span > 1e-6 && std::fabs(*s2 - *s1) < 360.0;
}

// A light with a nominal range (VALNMR) of at least this many nautical miles
// is a major light; all-round, it is ringed in its colour.
constexpr double kMajorLightRangeNm = 10.0;

/// Formats a number the way a light description prints it: no trailing zeros.
std::string LightNumber(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", v);
  return buf;
}

/** LITDSN01: the light description, e.g. "Fl(2) R 6s 12m 5M" — character
 *  and signal group, colours, period, height and nominal range, each omitted
 *  when its attribute is missing. Components are space-separated as on US
 *  charts. Empty when the light states none of them. */
std::string LightDescription(const VectorFeature& f) {
  // S-57 LITCHR 1-29; 21-24 are unassigned.
  static const char* const kCharacter[] = {
      "",      "F",      "Fl",     "LFl",   "Q",       "VQ",      "UQ",
      "Iso",   "Oc",     "IQ",     "IVQ",   "IUQ",     "Mo",      "FFl",
      "Fl+LFl", "Oc+Fl", "FLFl",   "Al.Oc", "Al.LFl",  "Al.Fl",   "Al.Gr",
      "",      "",       "",       "",      "Q+LFl",   "VQ+LFl",  "UQ+LFl",
      "Al",    "Al.FFl"};
  // S-57 COLOUR 1-13.
  static const char* const kColour[] = {"",   "W",  "B",  "R",  "G",
                                        "Bu", "Y",  "Gy", "Br", "Am",
                                        "Vi", "Or", "M",  "P"};
  std::string out;
  const int chr = static_cast<int>(AttrNumber(f, "LITCHR", 0.0));
  if (chr > 0 && chr < static_cast<int>(sizeof(kCharacter) / sizeof(*kCharacter)))
    out += kCharacter[chr];
  // A single flash, "(1)", or an empty group is not printed.
  const std::string* group = f.Attribute("SIGGRP");
  if (group != nullptr && *group != "()" && *group != "(1)") out += *group;
  auto append = [&out](const std::string& part) {
    if (part.empty()) return;
    if (!out.empty()) out += ' ';
    out += part;
  };
  if (const std::string* c = f.Attribute("COLOUR")) {
    std::string colours;
    size_t pos = 0;
    while (pos < c->size()) {
      size_t comma = c->find(',', pos);
      if (comma == std::string::npos) comma = c->size();
      const int n = std::atoi(c->substr(pos, comma - pos).c_str());
      if (n > 0 && n < static_cast<int>(sizeof(kColour) / sizeof(*kColour)))
        colours += kColour[n];
      pos = comma + 1;
    }
    append(colours);
  }
  auto number = [](double v, const char* unit) {
    return std::isfinite(v) ? LightNumber(v) + unit : std::string();
  };
  append(number(AttrNumber(f, "SIGPER", NAN), "s"));
  append(number(AttrNumber(f, "HEIGHT", NAN), "m"));
  append(number(AttrNumber(f, "VALNMR", NAN), "M"));
  return out;
}

/// S-52 text group of a light's description.
constexpr const char* kLightDescriptionTextGroup = "23";

/// The TX instruction that sets a light's description to the right of it,
/// text group 23. `yoffs` is in body sizes, negative up.
std::string LightDescriptionText(const VectorFeature& f, int yoffs) {
  const std::string d = LightDescription(f);
  if (d.empty()) return std::string();
  return ";TX('" + d + "',3,2,3,'15110',2," + std::to_string(yoffs) +
         ",CHBLK," +
         kLightDescriptionTextGroup + ")";
}

/// LIGHTS05 — lights. A sector light draws its legs and arc instead of a
/// flare; an all-round light draws a flare at kFlareBearingDeg, and a major
/// one also a full circle. Both carry the description (LIGHTS05's TX at
/// XOFFS 2, YOFFS -1 beside a flare, 0 beside a sector). Not implemented: the
/// 25 mm arc for overlapping sectors at one position, full-range legs, and
/// directional (ORIENT) lights.
std::string Lights05(const VectorFeature& f, const S52MarinerSettings&) {
  // CATLIT 8 = flood light, 11 = strip light: both have their own symbol and
  // neither takes a flare.
  if (AttrIn(f, "CATLIT", {8})) return "SY(LIGHTS82)";
  if (AttrIn(f, "CATLIT", {11})) return "SY(LIGHTS81)";

  double s1 = 0.0, s2 = 0.0;
  if (LightSectors(f, &s1, &s2)) {
    return "SY(" +
           LightSectorId(s1, s2, kSectorArcRadiusMm, LightArcColour(f)) + ")" +
           LightDescriptionText(f, 0);
  }
  std::string out = std::string("SY(") + LightFlare(f) + "," +
                    std::to_string(kFlareBearingDeg) + ")";
  if (AttrNumber(f, "VALNMR", 0.0) >= kMajorLightRangeNm) {
    out += ";SY(" +
           LightSectorId(0.0, 360.0, kAllRoundArcRadiusMm, LightArcColour(f)) +
           ")";
  }
  return out + LightDescriptionText(f, -1);
}

/// SYMINS01 — a NEWOBJ (in US cells, a virtual AIS aid to navigation) draws
/// the instructions its own SYMINS attribute carries. Without them it takes the
/// unknown-object mark; the library has no NEWOBJ01 symbol.
std::string SymIns01(const VectorFeature& f, const S52MarinerSettings&) {
  const std::string* ins = f.Attribute("SYMINS");
  if (ins != nullptr && !ins->empty()) return *ins;
  if (f.type == VectorGeometryType::kPoint) return "SY(QUESMRK1)";
  return "LS(DASH,2,CHMGD)";
}

// TOPMAR01 — a topmark on a buoy or beacon. TOPSHP names the shape; the
// library carries two families of the same shapes, 02..65 for BUOYS and 22..89
// for BEACONS.
//
// DOCUMENTED REDUCTION: which family applies depends on the topmark's PARENT
// object, and the vector seam serves features standalone — S-57's master/slave
// relation is read by E1 but not published through IVectorSource. The BUOY
// family is used, so a beacon topmark draws the buoy variant of the right
// shape. Only 2 features in these cells reach it; exposing the relation is the
// fix, and it belongs with the source, not here.
std::string TopMar01(const VectorFeature& f, const S52MarinerSettings&) {
  const std::string* shape = f.Attribute("TOPSHP");
  // TOPMAR01 is the library's own "topmark not defined" entry and is
  // raster-only, so it reaches the canvas as the question mark — which is
  // exactly what an undefined topmark should look like.
  if (shape == nullptr || shape->empty()) return "SY(TOPMAR01)";
  switch (std::atoi(shape->c_str())) {
    case 1: return "SY(TOPMAR02)";   // cone point up
    case 2: return "SY(TOPMAR04)";   // cone point down
    case 3: return "SY(TOPMAR10)";   // sphere
    case 4: return "SY(TOPMAR12)";   // 2 spheres
    case 5: return "SY(TOPMAR13)";   // cylinder
    case 6: return "SY(TOPMAR14)";   // board
    case 7: return "SY(TOPMAR65)";   // x-shape
    case 8: return "SY(TOPMAR86)";   // upright cross (beacon family only)
    case 9: return "SY(TOPMAR16)";   // cube point up
    case 10: return "SY(TOPMAR08)";  // 2 cones point to point
    case 11: return "SY(TOPMAR07)";  // 2 cones base to base
    case 13: return "SY(TOPMAR05)";  // 2 cones points upward
    case 14: return "SY(TOPMAR06)";  // 2 cones points downward
    case 17: return "SY(TOPMAR17)";  // flag
    default: return "SY(TOPMAR01)";  // the library's own "not defined" mark
  }
}

// The restriction family shared by RESTRN01 and RESARE02. S-52 keeps three
// escalating symbols per family (51 plain, 61 "with other cautions", 71 "with
// other information"); the library defines the 51 forms as both a LINE-STYLE
// (the area's boundary) and a SYMBOL (its centre).
const char* RestrictionFamily(const VectorFeature& f) {
  // RESTRN, Appendix A: 1 anchoring prohibited, 2 anchoring restricted,
  // 3..6 + 24 fishing/trawling prohibited or restricted, 7 entry prohibited,
  // 8 entry restricted, 14 area to be avoided.
  if (AttrIn(f, "RESTRN", {7, 8, 14})) return "ENTRES";
  if (AttrIn(f, "RESTRN", {1, 2})) return "ACHRES";
  if (AttrIn(f, "RESTRN", {3, 4, 5, 6, 24})) return "CTYARE";
  return nullptr;
}

std::string RestrictionInstructions(const VectorFeature& f) {
  const char* family = RestrictionFamily(f);
  if (family == nullptr) {
    // Restricted in some way the attribute does not state. The library has a
    // mark for exactly this, which is better than drawing nothing.
    return f.type == VectorGeometryType::kPoint ? "SY(RSRDEF51)"
                                                : "SY(RSRDEF51);LS(DASH,2,CHMGD)";
  }
  const std::string name = std::string(family) + "51";
  if (f.type == VectorGeometryType::kPoint) return "SY(" + name + ")";
  // A line takes the family's boundary line; an area also takes the symbol,
  // which the renderer centres in the area.
  if (f.type == VectorGeometryType::kLine) return "LC(" + name + ")";
  return "SY(" + name + ");LC(" + name + ")";
}

/// RESTRN01 — restriction symbology for any object class whose lookup row calls
/// it (anchorages, cable and pipeline areas, fairways, ...). Emits nothing when
/// the object carries no RESTRN; the undefined-restriction mark is only for a
/// RESTRN value outside the three families.
std::string Restrn01(const VectorFeature& f, const S52MarinerSettings&) {
  const std::string* restrn = f.Attribute("RESTRN");
  if (restrn == nullptr || restrn->empty()) return std::string();
  return RestrictionInstructions(f);
}

// RESARE02 — RESARE, the restricted-area class itself. Same family selection;
// CATREA refines it in the published procedure (a nature reserve and a firing
// range read differently), and that refinement is NOT implemented: every
// restricted area whose RESTRN says nothing takes the undefined-restriction
// mark. The distinction is informational, not navigational.
std::string ResAre02(const VectorFeature& f, const S52MarinerSettings&) {
  return RestrictionInstructions(f);
}

// DATCVR01 — the limit of chart data. CATCOV 1 = coverage available, 2 = no
// coverage: the first gets the data-limit line the library ships as HODATA01,
// the second the no-data pattern. Reduced: the published procedure also draws
// the scale-boundary and the "overscale" indication, both of which are
// properties of the DISPLAY (which cells are loaded at what scale), not of one
// feature, so they belong to the engine/catalog level rather than here.
std::string DatCvr01(const VectorFeature& f, const S52MarinerSettings&) {
  if (AttrIn(f, "CATCOV", {2})) return "AP(NODATA03)";
  return f.type == VectorGeometryType::kPoint ? std::string()
                                              : "LC(HODATA01)";
}

const std::unordered_map<std::string, CsProc>& CsRegistry() {
  static const std::unordered_map<std::string, CsProc>* kRegistry = [] {
    auto* m = new std::unordered_map<std::string, CsProc>();
    (*m)["DATCVR01"] = &DatCvr01;
    (*m)["DEPARE01"] = &DepAre01;
    (*m)["DEPCNT02"] = &DepCnt02;
    (*m)["LIGHTS05"] = &Lights05;
    (*m)["OBSTRN04"] = &Obstrn04;
    (*m)["QUAPOS01"] = &QuaPos01;
    (*m)["RESARE02"] = &ResAre02;
    (*m)["RESTRN01"] = &Restrn01;
    (*m)["SLCONS03"] = &SlCons03;
    (*m)["SOUNDG02"] = &Soundg02;
    (*m)["SYMINS01"] = &SymIns01;
    (*m)["TOPMAR01"] = &TopMar01;
    (*m)["WRECKS02"] = &Wrecks02;
    return m;
  }();
  return *kRegistry;
}

/// True for a light with only one of SECTR1/SECTR2, which LIGHTS05 cannot
/// draw as a sector and falls back to a flare for.
bool IsSimplifiedSectorLight(const VectorFeature& f) {
  const std::string* a = f.Attribute("SECTR1");
  const std::string* b = f.Attribute("SECTR2");
  return (a != nullptr && !a->empty()) != (b != nullptr && !b->empty());
}

}  // namespace

// ---------------------------------------------------------------------------

S52StyleEngine::S52StyleEngine() : impl_(new Impl) {}
S52StyleEngine::~S52StyleEngine() = default;

Status S52StyleEngine::Open(const std::string& data_dir) {
  impl_.reset(new Impl);
  set_open(false);
  ResetUnresolvedSymbols();
  Status s = impl_->lib.Open(data_dir);
  if (!s.ok()) return s;
  set_open(true);
  return SetColorScheme(S52ColorScheme::kDay);
}

Status S52StyleEngine::SetColorScheme(S52ColorScheme scheme) {
  // The XML's own table names. DAY_BRIGHT is S-52's day palette; the two other
  // day tables (BLACKBACK/WHITEBACK) are printing variants and are reachable
  // through the library directly.
  const char* name = "DAY_BRIGHT";
  if (scheme == S52ColorScheme::kDusk) name = "DUSK";
  if (scheme == S52ColorScheme::kNight) name = "NIGHT";
  Status s = impl_->lib.SetActiveColorTable(name);
  if (!s.ok()) return s;
  impl_->scheme = scheme;
  BumpStyleEpoch();
  return Status::Ok();
}

S52ColorScheme S52StyleEngine::color_scheme() const { return impl_->scheme; }

void S52StyleEngine::SetShowMetaObjects(bool on) {
  if (impl_->show_meta == on) return;  // a no-op set must not bump the epoch
  impl_->show_meta = on;
  BumpStyleEpoch();
}
bool S52StyleEngine::show_meta_objects() const { return impl_->show_meta; }

void S52StyleEngine::SetShowBuoyNames(bool on) {
  if (impl_->show_buoy_names == on) return;
  impl_->show_buoy_names = on;
  BumpStyleEpoch();
}
bool S52StyleEngine::show_buoy_names() const { return impl_->show_buoy_names; }

void S52StyleEngine::SetShowBeaconNames(bool on) {
  if (impl_->show_beacon_names == on) return;
  impl_->show_beacon_names = on;
  BumpStyleEpoch();
}
bool S52StyleEngine::show_beacon_names() const { return impl_->show_beacon_names; }

void S52StyleEngine::SetShowLightDescriptions(bool on) {
  if (impl_->show_light_descriptions == on) return;
  impl_->show_light_descriptions = on;
  BumpStyleEpoch();
}
bool S52StyleEngine::show_light_descriptions() const {
  return impl_->show_light_descriptions;
}

void S52StyleEngine::SetPointStyle(S52PointStyle s) {
  impl_->point_style = s;
  BumpStyleEpoch();
}
void S52StyleEngine::SetAreaStyle(S52AreaStyle s) {
  impl_->area_style = s;
  BumpStyleEpoch();
}

const S52PresentationLibrary& S52StyleEngine::library() const {
  return impl_->lib;
}

const S52Lookup* S52StyleEngine::SelectLookup(const VectorFeature& f) const {
  if (!IsOpen()) return nullptr;
  const std::vector<const S52Lookup*>& rows =
      impl_->lib.Lookups(impl_->TableFor(f.type), f.style_key);
  if (rows.empty()) return nullptr;
  // The matching row with the most conditions wins, earliest on a tie. The
  // delivered library does not keep its catch-all rows last (VEGATN's is
  // first, BOYLAT's Paper row 45 of 55), so file order alone would shadow
  // the specific rows behind them.
  const S52Lookup* best = nullptr;
  const S52Lookup* catch_all = nullptr;
  for (const S52Lookup* candidate : rows) {
    if (candidate->attributes.empty()) {
      if (catch_all == nullptr) catch_all = candidate;
      continue;
    }
    if (!impl_->Matches(*candidate, f)) continue;
    if (best == nullptr ||
        candidate->attributes.size() > best->attributes.size())
      best = candidate;
  }
  if (best != nullptr) return best;
  if (catch_all != nullptr) return catch_all;
  // No catch-all and nothing matched: the last row stands in as the default.
  return rows.back();
}

const std::map<std::string, size_t>& S52StyleEngine::unhandled_cs() const {
  return impl_->unhandled_cs;
}
size_t S52StyleEngine::placeholders_drawn() const { return impl_->placeholders; }
size_t S52StyleEngine::sector_lights_simplified() const {
  return impl_->sector_lights_simplified;
}
void S52StyleEngine::ResetDiagnostics() {
  impl_->unhandled_cs.clear();
  ResetUnresolvedSymbols();
  impl_->placeholders = 0;
  impl_->sector_lights_simplified = 0;
}

const VectorSymbol* S52StyleEngine::Symbol(const std::string& symbol_id) {
  if (symbol_id.empty()) return nullptr;
  if (IsLightSectorId(symbol_id)) return impl_->LightSector(symbol_id);
  if (symbol_id.compare(0, 3, "LC:") == 0) return impl_->lib.LineStyle(symbol_id.substr(3));
  if (symbol_id.compare(0, 3, "AP:") == 0) return impl_->lib.Pattern(symbol_id.substr(3));
  const VectorSymbol* s = impl_->lib.Symbol(symbol_id);
  if (s != nullptr) return s;
  // An untagged name still finds a line-style or pattern, for callers that
  // fetch one by its library name.
  s = impl_->lib.LineStyle(symbol_id);
  if (s != nullptr) return s;
  return impl_->lib.Pattern(symbol_id);
}

const SymbolPixmap* S52StyleEngine::Pixmap(const std::string& symbol_id) {
  if (symbol_id.empty()) return nullptr;
  // Line-styles and patterns are vector only; a point symbol's tile under the
  // same name is a different drawing.
  if (symbol_id.compare(0, 3, "LC:") == 0 || symbol_id.compare(0, 3, "AP:") == 0)
    return nullptr;
  return impl_->lib.SymbolBitmap(symbol_id);
}

double S52StyleEngine::himetric_per_symbol_pixel() const {
  return kS52HimetricPerSymbolPixel;
}

// ---------------------------------------------------------------------------
// Instruction execution
// ---------------------------------------------------------------------------

namespace {

// The instruction accumulator is fv::StyleResultBuilder in
// fvkit/vector/lookup_engine.h as of E3c — it names no product's instructions,
// only style.h's slots, so it belongs to the shared core.

/// Sets `pen`'s dash for an S-52 pen style. Run lengths are the spec's
/// patterns rounded at 96 dpi and scaled to `dpi` like the pen width, so a
/// wide pen on a dense screen keeps its gaps; they do not grow with zoom.
void ApplyPenStyle(const std::string& style, double dpi, Pen* pen) {
  const std::string s = Clean(style);
  const double k = dpi > 0.0 ? dpi / 96.0 : 1.0;
  if (s == "DASH") {
    pen->dash = {12 * k, 6 * k};
  } else if (s == "DOTT") {
    pen->dash = {2 * k, 4 * k};
  } else {
    pen->dash.clear();  // SOLD, and anything a future table adds
  }
}

// Pixels per HIMETRIC unit for SYMBOL geometry — must match what the renderer
// itself uses (it asks the engine, via himetric_per_symbol_pixel()), because
// an area pattern's pitch and a complex line's period are measured in the same
// pixels the symbol is drawn at. This is NOT the dpi-based conversion pen
// widths use; symbols still ignore the device DPI (a standing ledger defect) —
// what changed is WHOSE nominal pixel they are sized on, and S-52's is 0.32 mm.
double PxPerHimetric(const StyleContext& ctx) {
  const double s = ctx.symbol_scale > 0.0 ? ctx.symbol_scale : 1.0;
  return s / kS52HimetricPerSymbolPixel;
}

// S-57 META object classes describe the DATASET rather than the world: data
// quality (M_QUAL's zones of confidence), coverage (M_COVR), the navigation
// system in force (M_NSYS), the units and datums (M_UNIT, M_HDAT). Every one
// of them is spelled "M_" + 4, which is exactly what the catalogue's own Class
// column ('M') marks; s52_style_test asserts this prefix rule against all 251
// catalogued classes, so the shortcut cannot drift from the data.
bool IsMetaAcronym(const std::string& acronym) {
  return acronym.size() > 2 && acronym[0] == 'M' && acronym[1] == '_';
}

/// True for the six S-57 buoy classes (BOYCAR, BOYINB, BOYISD, BOYLAT, BOYSAW,
/// BOYSPP).
bool IsBuoyAcronym(const std::string& acronym) {
  return acronym.size() == 6 && acronym.compare(0, 3, "BOY") == 0;
}

bool IsBeaconAcronym(const std::string& acronym) {
  return acronym.size() == 6 && acronym.compare(0, 3, "BCN") == 0;
}

// The first ink colour a display list puts down, for the fallbacks.
FvColor FirstInk(const VectorSymbol& sym) {
  for (const SymbolPrimitive& p : sym.primitives) {
    if (p.has_stroke) return p.stroke_color;
    if (p.has_fill) return p.fill_color;
  }
  return FvColor{0, 0, 0, 255};
}

int PenWidthPx(double s52_units, double dpi) {
  const double px = s52_units * kS52PenMm / kMmPerInch * dpi;
  return (std::max)(1, static_cast<int>(std::lround(px)));
}

// The 5-character text spec of TX/TE ('15110'): S-52 packs style, weight,
// width and body size into it, and the last two digits are the size. Read
// only the size here — the canvas has one font — and fall back to 10 pt when
// the field is not the expected shape.
double TextSizeFromSpec(const std::string& spec) {
  const std::string s = Clean(spec);
  if (s.size() < 2) return 10.0;
  const std::string tail = s.substr(s.size() - 2);
  const double v = ToDouble(tail, 0.0);
  return v > 0.0 ? v : 10.0;
}

// S-52 HJUST/VJUST. The library states them as single digits, and the delivered
// tables decode themselves: SEAARE labels an area with (HJUST 1, VJUST 2) and
// no offset — a sea name centred on its own centroid, which fixes 1 = centre
// and 2 = centre. BOYLAT uses HJUST 2 at XOFFS -1 and the 127-row majority uses
// HJUST 3 at XOFFS +1, so 2 must place the text's RIGHT edge at the offset
// point and 3 its LEFT edge. Anything else (a `$JUSTH=3` variable reference,
// which nine rows carry) keeps the canvas default rather than guessing.
LabelHAlign HAlignFromSpec(const std::string& spec, LabelHAlign fallback) {
  const std::string s = Clean(spec);
  if (s == "1") return LabelHAlign::kCenter;
  if (s == "2") return LabelHAlign::kRight;
  if (s == "3") return LabelHAlign::kLeft;
  return fallback;
}

LabelVAlign VAlignFromSpec(const std::string& spec, LabelVAlign fallback) {
  const std::string s = Clean(spec);
  if (s == "1") return LabelVAlign::kBottom;
  if (s == "2") return LabelVAlign::kCenter;
  if (s == "3") return LabelVAlign::kTop;
  return fallback;
}

// XOFFS/YOFFS are in units of the text's own BODY SIZE, x positive right and
// y positive DOWN — the same sense as the presentation library's bitmap pivot
// points (E7), and the same sense as the screen. The library's two commonest
// pairs are (+1,-1) and (-1,-1): a name up and to one side of its symbol,
// which is what a paper chart does and what these produce.
int OffsetPixels(const std::string& spec, double body_px) {
  return static_cast<int>(std::lround(ToDouble(Clean(spec), 0.0) * body_px));
}

/** Applies TE's printf-style `format` to one attribute value.
 *  The single conversion spec is located and formatted on its own; the text
 *  around it (`"clr "`, `" kn"`, `" (m)"`) is kept verbatim. A format with no
 *  spec, a second spec or an unsupported conversion yields the raw value. */
std::string ApplyTextFormat(const std::string& format,
                            const std::string& value) {
  const size_t pct = format.find('%');
  if (pct == std::string::npos) return format.empty() ? value : format;
  size_t end = pct + 1;
  while (end < format.size() && std::strchr("-+ #0", format[end]) != nullptr) ++end;
  while (end < format.size() && (std::isdigit(static_cast<unsigned char>(format[end])) ||
                                 format[end] == '.'))
    ++end;
  while (end < format.size() && (format[end] == 'l' || format[end] == 'h')) ++end;
  if (end >= format.size()) return value;
  const char conv = format[end];
  const std::string suffix = format.substr(end + 1);
  if (suffix.find('%') != std::string::npos) return value;
  // The length modifiers are dropped: the argument types below are fixed.
  std::string spec = format.substr(pct, end - pct);
  spec.erase(std::remove_if(spec.begin(), spec.end(),
                            [](char c) { return c == 'l' || c == 'h'; }),
             spec.end());
  spec += conv;
  char buf[128];
  if (conv == 's') {
    snprintf(buf, sizeof(buf), spec.c_str(), value.c_str());
  } else if (conv == 'd' || conv == 'i') {
    snprintf(buf, sizeof(buf), spec.c_str(), std::atoi(value.c_str()));
  } else if (conv == 'f' || conv == 'g' || conv == 'e') {
    snprintf(buf, sizeof(buf), spec.c_str(), ToDouble(value, 0.0));
  } else {
    return value;
  }
  return format.substr(0, pct) + buf + suffix;
}

// Sounding text size in S-52 nominal pixels (0.32 mm): digits close to the
// library tiles' 9 px height and 7 px pitch.
constexpr double kSoundingTextPx = 13.0;

/// The labels for one sounding figure, laid out on the digit tiles' grid: the
/// whole part right-aligned ending at the sounding, the subscript to its right
/// and 4 px lower. A whole part longer than the tiles' three slots starts at
/// the leftmost slot instead. Offsets are nominal pixels times `symbol_scale`.
void SoundingLabels(const SoundingFigure& fig, const FvColor& colour,
                    double symbol_scale, std::vector<LabelStyle>* out) {
  auto px = [symbol_scale](double v) {
    return static_cast<int>(std::lround(v * symbol_scale));
  };
  LabelStyle whole;
  whole.valid = true;
  whole.text = fig.whole;
  whole.style.size = kSoundingTextPx * symbol_scale;
  whole.style.color = colour;
  whole.skip_repeat_filter = true;
  whole.dy = px(5);
  if (fig.whole.size() <= 3) {
    whole.halign = LabelHAlign::kRight;
  } else {
    whole.dx = px(-19);
  }
  if (!fig.fraction.empty() && fig.whole.size() <= 3) {
    LabelStyle sub = whole;
    sub.text = fig.fraction;
    sub.halign = LabelHAlign::kLeft;
    sub.dx = px(1);
    sub.dy = px(9);
    out->push_back(std::move(whole));
    out->push_back(std::move(sub));
    return;
  }
  out->push_back(std::move(whole));
}

}  // namespace

Status S52StyleEngine::StyleFeature(const VectorFeature& f,
                                   const StyleContext& ctx,
                                   const StylePass& pass,
                                   std::vector<StyleResult>* out) {
  // META OBJECTS describe the DATA, not the world, and are off by default.
  // They are not a display CATEGORY: M_QUAL is category OTHER, and so are
  // soundings and depth contours, so a category threshold that hides the
  // quality overlay also hides half the chart. They are a separate axis, and
  // an ECDIS gives them their own toggle for that reason.
  if (!impl_->show_meta && IsMetaAcronym(f.style_key)) return Status::Ok();

  const S52Lookup* row = SelectLookup(f);
  if (row == nullptr) return Status::Ok();

  if (!viewing_groups().CategoryEnabled(CategoryOf(row->display_category)))
    return Status::Ok();

  const double symbol_scale = pass.symbol_scale;
  const bool draw_labels = pass.draw_labels;
  const int priority = pass.PriorityOr(PriorityOf(*row));

  StyleResultBuilder builder(priority, out);
  size_t unhandled_here = 0;
  // Held back until the geometry passes are flushed, then appended at the text
  // band. A row may carry more than one TX/TE (a light's characteristic and its
  // name), and each keeps its own result exactly as the builder would have.
  std::vector<LabelStyle> labels;
  // Sounding figures: text, but drawn at the row's own priority.
  std::vector<LabelStyle> soundings;

  // A CS procedure's output runs in place of the CS, so its instructions land
  // in the same StyleResult sequence. It is walked as a second frame over the
  // row's own program; a CS inside that output is skipped, which no published
  // procedure emits and which could otherwise recurse without bound.
  const std::vector<S52Instruction>* frame[2] = {&row->instructions, nullptr};
  size_t next[2] = {0, 0};
  std::vector<S52Instruction> emitted_program;
  int depth = 0;
  for (;;) {
    if (next[depth] >= frame[depth]->size()) {
      if (depth == 0) break;
      depth = 0;
      continue;
    }
    const S52Instruction& ins = (*frame[depth])[next[depth]++];

    if (ins.op == "CS") {
      if (ins.params.empty() || depth > 0) continue;
      const std::string name = Clean(ins.params[0]);
      const auto& registry = CsRegistry();
      auto it = registry.find(name);
      if (it == registry.end()) {
        ++impl_->unhandled_cs[name];
        ++unhandled_here;
        continue;
      }
      if (name == "LIGHTS05" && IsSimplifiedSectorLight(f))
        ++impl_->sector_lights_simplified;
      const std::string emitted = it->second(f, current_mariner());
      if (emitted.empty()) continue;
      emitted_program = ParseS52Instructions(emitted);
      frame[1] = &emitted_program;
      next[1] = 0;
      depth = 1;
      continue;
    }

    if (ins.op == "LS" && ins.params.size() >= 3) {
      StrokeStyle& s = builder.Stroke();
      s.valid = true;
      s.pen.color = impl_->Color(ins.params[2]);
      s.pen.width = PenWidthPx(ToDouble(ins.params[1], 1.0), ctx.device_dpi);
      ApplyPenStyle(ins.params[0], ctx.device_dpi, &s.pen);
      continue;
    }

    if (ins.op == "LC" && !ins.params.empty()) {
      // A complex line: the line-style's own drawing, repeated end to end
      // along the path and turned to its tangent. E3b routes it through the
      // shared placer (fvkit PlaceAlongPath) — the same call GeoSym's SAMI
      // lines make, which is §5.1's claim that these are one primitive.
      const std::string name = Clean(ins.params[0]);
      const VectorSymbol* ls = impl_->lib.LineStyle(name);
      const S52SymbolDef* def = impl_->lib.LineStyleDef(name);
      if (ls == nullptr || def == nullptr) {
        NoteUnresolvedSymbol(name);
        continue;
      }
      // The definition's <vector width> IS the pattern period: the HPGL of
      // ACHARE51 spans 306..3336 with a period of 3030, and the pivot is the
      // point that rides on the line.
      double period = static_cast<double>(def->vector_width);
      if (!(period > 0.0)) period = ls->max_x - ls->min_x;
      if (!(period > 0.0)) period = 100.0;  // a degenerate definition
      PathRun run;
      run.type = PathRunType::kSymbol;
      run.symbol_id = S52LineStyleId(name);
      run.length = period * PxPerHimetric(ctx);
      run.bend = true;
      if (!(run.length > 0.5)) {
        // Below half a pixel per cycle the placer would stamp thousands of
        // overlapping copies for no visible gain; fall back to a plain pen in
        // the line-style's own colour, which is what E3a always did.
        StrokeStyle& s = builder.Stroke();
        s.valid = true;
        s.pen.color = FirstInk(*ls);
        s.pen.width = 1;
        s.pen.dash = {8, 5};
        continue;
      }
      LinePatternStyle& lp = builder.LinePattern();
      lp.valid = true;
      lp.runs.push_back(std::move(run));
      continue;
    }

    if (ins.op == "AC" && !ins.params.empty()) {
      FillStyle& fill = builder.Fill();
      fill.valid = true;
      fill.brush.color = impl_->Color(ins.params[0]);
      // The optional second parameter is S-52's transparency, 0..4 quarters.
      if (ins.params.size() >= 2) {
        const double t = ToDouble(ins.params[1], 0.0);
        const double keep = (std::max)(0.0, 1.0 - t / 4.0);
        fill.brush.color.a =
            static_cast<unsigned char>(std::lround(255.0 * keep));
      }
      continue;
    }

    if (ins.op == "AP" && !ins.params.empty()) {
      // An area pattern: the symbol stamped on a grid over the area, which
      // E3b's PlaceOverArea does for real instead of E3a's colour-tint
      // approximation.
      const std::string name = Clean(ins.params[0]);
      const VectorSymbol* pat = impl_->lib.Pattern(name);
      const S52SymbolDef* def = impl_->lib.PatternDef(name);
      if (pat == nullptr || def == nullptr) {
        NoteUnresolvedSymbol(name);
        continue;
      }
      // <distance min> is the authored spacing; when the library leaves it at
      // 0 (DIAMOND1 does) the symbol's own box is the pitch.
      const double pitch = static_cast<double>(def->distance_min);
      const double sx = pitch > 0.0 ? pitch
                                    : static_cast<double>(def->vector_width);
      const double sy = pitch > 0.0 ? pitch
                                    : static_cast<double>(def->vector_height);
      const double scale = PxPerHimetric(ctx);
      AreaPatternStyle& ap = builder.AreaPattern();
      ap.valid = true;
      ap.symbol_id = S52PatternId(name);
      // Never below 2 px: a sub-pixel pitch is millions of stamps for a solid
      // block of ink, and the placer's own budget would truncate it anyway.
      ap.spacing_x = (std::max)(2.0, sx * scale);
      ap.spacing_y = (std::max)(2.0, sy * scale);
      ap.staggered = def->fill_type == 'S';
      continue;
    }

    if (ins.op == "SY" && !ins.params.empty()) {
      const std::string name = Clean(ins.params[0]);
      const VectorSymbol* sym = IsLightSectorId(name)
                                    ? impl_->LightSector(name)
                                    : impl_->lib.Symbol(name);
      PointSymbolStyle& s = builder.Symbol();
      s.valid = true;
      // The renderer already folds StyleContext::symbol_scale into its pixel
      // conversion, so only a RULE's own multiplier belongs here.
      s.scale = pass.SymbolScaleOr(1.0);
      // S-52 centres an area's or line's symbol; a point ignores the anchor.
      s.anchor = LabelAnchor::kCentre;
      // S-52 symbols stay screen-upright on a rotated chart. Light sectors
      // carry true bearings in their geometry and turn with it; so does an
      // attribute rotation (ORIENT), set below.
      s.turns_with_chart = IsLightSectorId(name);
      if (sym != nullptr) {
        s.symbol_id = name;
      } else if (impl_->lib.SymbolBitmap(name) != nullptr) {
        // A RASTER-ONLY definition: no HPGL, but a tile in the symbol sheet.
        // 679 of the library's 1018 symbols are authored this way and they are
        // not the fringe — the lateral buoys, beacons and daymarks of a
        // harbour are in here. The renderer resolves the id through Pixmap()
        // when Symbol() has no geometry, so the name goes through unchanged
        // and this is NOT a placeholder or an unresolved reference.
        s.symbol_id = name;
      } else {
        // One of E2's 24 genuinely dangling references.
        NoteUnresolvedSymbol(name);
        ++impl_->placeholders;
        s.symbol_id = kQuestionMark;
      }
      // The optional second parameter is a rotation: either a number of
      // degrees or the acronym of the attribute holding one (ORIENT).
      //
      // NEGATED, and this is a unit conversion rather than a fix. S-52 writes
      // every rotation as a COMPASS BEARING — ORIENT is degrees clockwise from
      // north, and so is the flare's 135 — while the renderer's rotation is
      // CCGMSymbol::DrawSymbol's, `x' = x cos r - y sin r` over a y-up symbol
      // space drawn into a y-down device, which turns a symbol
      // COUNTER-clockwise on screen for a positive angle. The shared renderer
      // keeps FalconView's sense under the bit-faithful rule, so the product
      // that authors in bearings converts at its own seam. Without this a
      // recommended track (SY(RECTRC57,ORIENT)) and a traffic lane
      // (SY(TSSLPT51,ORIENT)) point at their own mirror image about north.
      if (ins.params.size() >= 2) {
        const std::string p = Clean(ins.params[1]);
        double deg = 0.0;
        if (RuleValueAsNumber(p, &deg)) {
          s.rotation_deg = -deg;
        } else {
          // An attribute is a true bearing; a literal (the flare's 135) is
          // relative to the top of the screen.
          const std::string* v = f.Attribute(p);
          if (v != nullptr) {
            s.rotation_deg = -ToDouble(*v, 0.0);
            s.turns_with_chart = true;
          }
        }
      }
      continue;
    }

    if (ins.op == "SN" && !ins.params.empty()) {
      // Soundings are chart data, not optional text: not gated by draw_labels.
      const double depth = ToDouble(Clean(ins.params[0]), 0.0);
      const S52MarinerSettings& m = current_mariner();
      const FvColor colour =
          impl_->Color(depth <= m.safety_depth ? "SNDG2" : "CHGRD");
      SoundingLabels(FormatSounding(depth, m.depth_unit), colour, symbol_scale,
                     &soundings);
      continue;
    }

    if ((ins.op == "TX" || ins.op == "TE") && draw_labels) {
      if (!impl_->show_buoy_names && IsBuoyAcronym(f.style_key)) continue;
      if (!impl_->show_beacon_names && IsBeaconAcronym(f.style_key)) continue;
      const bool is_te = ins.op == "TE";
      const size_t attr_index = is_te ? 1 : 0;
      if (ins.params.size() <= attr_index) continue;
      const bool literal = !is_te && attr_index < ins.quoted.size() &&
                           ins.quoted[attr_index];
      const std::string* value =
          literal ? &ins.params[attr_index]
                  : f.Attribute(Clean(ins.params[attr_index]));
      if (value == nullptr || value->empty()) continue;

      // NOT into `builder`: text is drawn above all geometry, which means a
      // priority of its own and therefore a StyleResult of its own. See
      // kS52PrioTextBase.
      LabelStyle label;
      label.valid = true;
      label.text = is_te ? ApplyTextFormat(ins.params[0], *value) : *value;
      label.anchor = LabelAnchor::kCentre;
      // Parameter layout after the attribute: hjust, vjust, space, chars,
      // xoffs, yoffs, colour, display.
      const size_t base = attr_index + 1;
      if (!impl_->show_light_descriptions && ins.params.size() > base + 7 &&
          Clean(ins.params[base + 7]) == kLightDescriptionTextGroup)
        continue;
      if (ins.params.size() > base + 3)
        label.style.size = TextSizeFromSpec(ins.params[base + 3]) * symbol_scale;
      if (ins.params.size() > base + 6)
        label.style.color = impl_->Color(ins.params[base + 6]);
      if (ins.params.size() > base + 0)
        label.halign = HAlignFromSpec(ins.params[base + 0], label.halign);
      if (ins.params.size() > base + 1)
        label.valign = VAlignFromSpec(ins.params[base + 1], label.valign);
      // The offsets are in body sizes, so they need the size that was just
      // resolved — including the symbol scale, or a magnified chart would keep
      // its text the same distance from a symbol that grew.
      if (ins.params.size() > base + 5) {
        label.dx = OffsetPixels(ins.params[base + 4], label.style.size);
        label.dy = OffsetPixels(ins.params[base + 5], label.style.size);
      }
      labels.push_back(std::move(label));
      continue;
    }
  }

  // Plan §7: never let a feature vanish because the symbology was not
  // implemented. A CS procedure this engine does not have draws the S-52
  // question mark — but ONLY when nothing else in the row put ink down, since
  // a beacon that already drew its own symbol needs its missing topmark
  // counted, not a question mark stamped on top of it.
  if (unhandled_here > 0 && !builder.AnythingDrawn() && labels.empty() &&
      soundings.empty()) {
    PointSymbolStyle& s = builder.Symbol();
    s.valid = true;
    s.symbol_id = kQuestionMark;
    ++impl_->placeholders;
  }
  builder.Finish();
  for (LabelStyle& lb : soundings) {
    StyleResult r;
    r.priority = priority;
    r.label = std::move(lb);
    out->push_back(std::move(r));
  }

  // A large minority of S-52 rows are text ONLY — a sounding IS its number —
  // so this is frequently the row's whole output, not an afterthought on it.
  // A rule that overrode the priority is obeyed literally and keeps its text
  // with its geometry: an override says where the caller wants this object,
  // and silently lifting half of it out of that band would make the override
  // mean something the caller did not ask for.
  const int text_priority =
      pass.has_priority ? priority
                        : kS52PrioTextBase + PriorityOf(*row);
  for (LabelStyle& lb : labels) {
    StyleResult r;
    r.priority = text_priority;
    r.label = std::move(lb);
    out->push_back(std::move(r));
  }
  return Status::Ok();
}

}  // namespace fv
