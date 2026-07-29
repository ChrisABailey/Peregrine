// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_s52_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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
  S52MarinerSettings mariner;
  bool show_meta = false;

  std::map<std::string, size_t> unhandled_cs;
  size_t placeholders = 0;
  size_t sector_lights_simplified = 0;

  // --- helpers ------------------------------------------------------------

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

  // One lookup row's conditions against one feature. Every condition must
  // hold; S-52 authors the rows most-specific-first, so the first row that
  // passes is the answer (E2's header states the same rule).
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

// DEPCNT02 — depth contours. The safety contour is the one line an ECDIS must
// make unmistakable, so it is drawn wider and in DEPSC; every other contour is
// a thin DEPCN. Reduced: the spec also promotes a DEPARE boundary to a
// contour, which needs the neighbouring area's depth range and therefore the
// retained scene E3b/R3 builds.
std::string DepCnt02(const VectorFeature& f, const S52MarinerSettings& m) {
  const double valdco = AttrNumber(f, "VALDCO", -1.0);
  const bool safety =
      valdco >= 0.0 && std::fabs(valdco - m.safety_contour) < 1e-6;
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

// SOUNDG02 — soundings. DEVIATION, and a visible one: S-52 draws a sounding
// as a row of digit SYMBOLS from the library, and in the delivered library
// those (SOUNDS01 and friends) are RASTER-ONLY definitions with no HPGL, so
// there is no vector display list to stamp. The number is emitted as TEXT
// instead — same information, wrong typography — and it is drawn in CHBLK
// when at or below the mariner's safety depth, CHGRD when deeper, which is
// the distinction the mariner actually reads. The source publishes the depth
// as the pseudo-attribute DEPTH (see fv_enc_vector_source.cpp) because S-57
// keeps it in the geometry, not in an attribute.
std::string Soundg02(const VectorFeature& f, const S52MarinerSettings& m) {
  const std::string* depth = f.Attribute("DEPTH");
  if (depth == nullptr || depth->empty()) return std::string();
  const double v = ToDouble(*depth, 0.0);
  const char* color = v <= m.safety_depth ? "CHBLK" : "CHGRD";
  return std::string("TX(DEPTH,2,1,2,'15110',-1,-1,") + color + ",21)";
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

// The sounding a hazard carries, printed the way SOUNDG02 prints one.
std::string ValsouText(const VectorFeature& f, const S52MarinerSettings& m) {
  const std::string* v = f.Attribute("VALSOU");
  if (v == nullptr || v->empty()) return std::string();
  const char* color = ToDouble(*v, 0.0) <= m.safety_depth ? "CHBLK" : "CHGRD";
  return std::string(";TE('%4.1lf','VALSOU',2,1,2,'15110',-1,-1,") + color +
         ",21)";
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
    // The area takes the depth ramp when it states a depth, and the dotted
    // hazard boundary either way.
    std::string out = has_depth ? Seabed01(depth, depth, m) : "AC(DEPVS)";
    out += ";LS(DOTT,2,CHBLK)";
    if (has_depth && IsDangerDepth(depth, m))
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

// LIGHTS05 — lights (100 features here). The flare colour comes straight from
// COLOUR through the library's own symbol descriptions (LIGHTS11 red, 12
// green, 13 white/yellow, 14 magenta, LITDEF11 the default).
//
// TWO DOCUMENTED REDUCTIONS, both counted rather than hidden:
//  * A SECTOR light (SECTR1/SECTR2) is drawn as a plain flare. The published
//    procedure draws the sector's two legs and the arc between them at the
//    light's nominal range, which is GEOMETRY the style engine cannot express
//    — an instruction string names symbols, not arcs. Emitting it needs either
//    a geometry-producing style op or a light-specific overlay; the count is
//    in sector_lights_simplified().
//  * The light DESCRIPTION string ("Fl(2)R.10s12M") is not composed. That is
//    a text formatter over six attributes, and labels are a separate axis.
std::string Lights05(const VectorFeature& f, const S52MarinerSettings&) {
  // CATLIT 8 = flood light, 11 = strip light: both have their own symbol and
  // neither takes a flare.
  if (AttrIn(f, "CATLIT", {8})) return "SY(LIGHTS82)";
  if (AttrIn(f, "CATLIT", {11})) return "SY(LIGHTS81)";

  // S-57 COLOUR: 1 white, 3 red, 4 green, 6 yellow, 9 amber, 11 orange,
  // 12 magenta. A light with several colours takes the first that matches, in
  // the spec's own order of precedence (red, green, then the white family).
  const char* sym = "LITDEF11";
  if (AttrIn(f, "COLOUR", {3})) sym = "LIGHTS11";
  else if (AttrIn(f, "COLOUR", {4})) sym = "LIGHTS12";
  else if (AttrIn(f, "COLOUR", {1, 6, 9, 11})) sym = "LIGHTS13";
  else if (AttrIn(f, "COLOUR", {12})) sym = "LIGHTS14";
  return std::string("SY(") + sym + ")";
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
  // Areas and lines take the family's complex boundary line. S-52 also puts a
  // CENTRED copy of the symbol inside an area, and that is deliberately NOT
  // emitted: the renderer anchors point symbology at a part's FIRST VERTEX,
  // so the mark would sit on a corner of the boundary rather than in the
  // middle of the area — a misplaced restriction symbol is worse than none.
  // A centroid anchor belongs with the retained scene (R3).
  return "LC(" + name + ")";
}

// RESTRN01 — the restriction symbology of any object that carries RESTRN.
std::string Restrn01(const VectorFeature& f, const S52MarinerSettings&) {
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
    (*m)["TOPMAR01"] = &TopMar01;
    (*m)["WRECKS02"] = &Wrecks02;
    return m;
  }();
  return *kRegistry;
}

// True when this feature is a sector light the procedure simplified — the
// caller counts these so the deviation is measured, not remembered.
bool IsSimplifiedSectorLight(const VectorFeature& f) {
  const std::string* a = f.Attribute("SECTR1");
  const std::string* b = f.Attribute("SECTR2");
  return (a != nullptr && !a->empty()) || (b != nullptr && !b->empty());
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

void S52StyleEngine::SetPointStyle(S52PointStyle s) {
  impl_->point_style = s;
  BumpStyleEpoch();
}
void S52StyleEngine::SetAreaStyle(S52AreaStyle s) {
  impl_->area_style = s;
  BumpStyleEpoch();
}

// The MUTABLE accessor bumps, unconditionally. A caller that reaches for it to
// read (there is a const overload for that) costs one scene rebuild; a caller
// that moves the safety contour and did NOT cause a bump would keep drawing the
// old depth ramp, which is the failure worth ruling out.
S52MarinerSettings& S52StyleEngine::mariner() {
  BumpStyleEpoch();
  return impl_->mariner;
}
const S52MarinerSettings& S52StyleEngine::mariner() const {
  return impl_->mariner;
}
const S52PresentationLibrary& S52StyleEngine::library() const {
  return impl_->lib;
}

const S52Lookup* S52StyleEngine::SelectLookup(const VectorFeature& f) const {
  if (!IsOpen()) return nullptr;
  const std::vector<const S52Lookup*> rows =
      impl_->lib.Lookups(impl_->TableFor(f.type), f.style_key);
  if (rows.empty()) return nullptr;
  for (const S52Lookup* candidate : rows)
    if (impl_->Matches(*candidate, f)) return candidate;
  // S-52 authors a catch-all (no conditions) at the end of each object's rows;
  // when a producer's attribute combination misses even that, the table's last
  // row is its own default, which beats drawing nothing.
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
  const VectorSymbol* s = impl_->lib.Symbol(symbol_id);
  if (s != nullptr) return s;
  // Line-styles and patterns share the symbol namespace on the way out: a
  // caller holding an id from LC()/AP() must be able to fetch its display
  // list without knowing which table it came from.
  s = impl_->lib.LineStyle(symbol_id);
  if (s != nullptr) return s;
  return impl_->lib.Pattern(symbol_id);
}

const SymbolPixmap* S52StyleEngine::Pixmap(const std::string& symbol_id) {
  if (symbol_id.empty()) return nullptr;
  return impl_->lib.SymbolBitmap(symbol_id);
}

// ---------------------------------------------------------------------------
// Instruction execution
// ---------------------------------------------------------------------------

namespace {

// The instruction accumulator is fv::StyleResultBuilder in
// fvkit/vector/lookup_engine.h as of E3c — it names no product's instructions,
// only style.h's slots, so it belongs to the shared core.

// S-52 pen styles. Run lengths are in pixels, matching Pen::dash, and are the
// spec's nominal 3.6 mm / 0.6 mm patterns rounded at a nominal 96 dpi — the
// renderer scales nothing, so a dash stays legible at any zoom.
void ApplyPenStyle(const std::string& style, Pen* pen) {
  const std::string s = Clean(style);
  if (s == "DASH") {
    pen->dash = {12, 6};
  } else if (s == "DOTT") {
    pen->dash = {2, 4};
  } else {
    pen->dash.clear();  // SOLD, and anything a future table adds
  }
}

// Pixels per HIMETRIC unit for SYMBOL geometry — must match what the renderer
// itself uses (renderer.h's kHimetricPerHundredthInch), because the placer's
// run lengths are measured in the same pixels the symbol is drawn at. This is
// NOT the dpi-based conversion pen widths use; see the note on
// kHimetricPerHundredthInch for why symbols are sized at a nominal 100 dpi.
double PxPerHimetric(const StyleContext& ctx) {
  const double s = ctx.symbol_scale > 0.0 ? ctx.symbol_scale : 1.0;
  return s / kHimetricPerHundredthInch;
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

// TE's format string, applied to one attribute value. Only the conversions the
// delivered library actually uses are honoured ('%s' and the numeric forms);
// anything else falls through to the raw value, which is information rather
// than a blank label.
std::string ApplyTextFormat(const std::string& format,
                            const std::string& value) {
  const size_t pct = format.find('%');
  if (pct == std::string::npos) return format.empty() ? value : format;
  const char conv = format.back();
  char buf[128];
  if (conv == 's') {
    snprintf(buf, sizeof(buf), format.c_str(), value.c_str());
  } else if (conv == 'd' || conv == 'i') {
    snprintf(buf, sizeof(buf), format.c_str(), std::atoi(value.c_str()));
  } else if (conv == 'f' || conv == 'g' || conv == 'e') {
    snprintf(buf, sizeof(buf), format.c_str(), ToDouble(value, 0.0));
  } else {
    return value;
  }
  return std::string(buf);
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

  // A CS procedure's output is executed in the same pass, so its instructions
  // land in the same StyleResult sequence. One level deep is enough: no
  // published procedure emits another CS.
  std::vector<S52Instruction> program = row->instructions;
  // No published procedure emits another CS, so expansion is one level deep in
  // practice — but a future procedure that emitted its own name would spin
  // here forever, and a style engine must not be able to hang a render.
  const size_t kMaxProgram = 64;
  for (size_t i = 0; i < program.size() && i < kMaxProgram; ++i) {
    const S52Instruction& ins = program[i];

    if (ins.op == "CS") {
      if (ins.params.empty()) continue;
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
      const std::string emitted = it->second(f, impl_->mariner);
      if (emitted.empty()) continue;
      const std::vector<S52Instruction> sub = ParseS52Instructions(emitted);
      if (program.size() + sub.size() > kMaxProgram) continue;
      // NOTE `ins` is a reference INTO program and is dead after this insert;
      // nothing below the insert may touch it.
      program.insert(program.begin() + static_cast<long>(i) + 1, sub.begin(),
                     sub.end());
      continue;
    }

    if (ins.op == "LS" && ins.params.size() >= 3) {
      StrokeStyle& s = builder.Stroke();
      s.valid = true;
      s.pen.color = impl_->Color(ins.params[2]);
      s.pen.width = PenWidthPx(ToDouble(ins.params[1], 1.0), ctx.device_dpi);
      ApplyPenStyle(ins.params[0], &s.pen);
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
      run.symbol_id = name;
      run.length = period * PxPerHimetric(ctx);
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
      ap.symbol_id = name;
      // Never below 2 px: a sub-pixel pitch is millions of stamps for a solid
      // block of ink, and the placer's own budget would truncate it anyway.
      ap.spacing_x = (std::max)(2.0, sx * scale);
      ap.spacing_y = (std::max)(2.0, sy * scale);
      ap.staggered = def->fill_type == 'S';
      continue;
    }

    if (ins.op == "SY" && !ins.params.empty()) {
      const std::string name = Clean(ins.params[0]);
      const VectorSymbol* sym = impl_->lib.Symbol(name);
      PointSymbolStyle& s = builder.Symbol();
      s.valid = true;
      // The renderer already folds StyleContext::symbol_scale into its pixel
      // conversion, so only a RULE's own multiplier belongs here.
      s.scale = pass.SymbolScaleOr(1.0);
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
      if (ins.params.size() >= 2) {
        const std::string p = Clean(ins.params[1]);
        double deg = 0.0;
        if (RuleValueAsNumber(p, &deg)) {
          s.rotation_deg = deg;
        } else {
          const std::string* v = f.Attribute(p);
          if (v != nullptr) s.rotation_deg = ToDouble(*v, 0.0);
        }
      }
      continue;
    }

    if ((ins.op == "TX" || ins.op == "TE") && draw_labels) {
      const bool is_te = ins.op == "TE";
      const size_t attr_index = is_te ? 1 : 0;
      if (ins.params.size() <= attr_index) continue;
      const std::string* value = f.Attribute(Clean(ins.params[attr_index]));
      if (value == nullptr || value->empty()) continue;

      LabelStyle& label = builder.Label();
      label.valid = true;
      label.text = is_te ? ApplyTextFormat(ins.params[0], *value) : *value;
      // Parameter layout after the attribute: hjust, vjust, space, chars,
      // xoffs, yoffs, colour, display.
      const size_t base = attr_index + 1;
      if (ins.params.size() > base + 3)
        label.style.size = TextSizeFromSpec(ins.params[base + 3]) * symbol_scale;
      if (ins.params.size() > base + 6)
        label.style.color = impl_->Color(ins.params[base + 6]);
      continue;
    }
  }

  // Plan §7: never let a feature vanish because the symbology was not
  // implemented. A CS procedure this engine does not have draws the S-52
  // question mark — but ONLY when nothing else in the row put ink down, since
  // a beacon that already drew its own symbol needs its missing topmark
  // counted, not a question mark stamped on top of it.
  if (unhandled_here > 0 && !builder.AnythingDrawn()) {
    PointSymbolStyle& s = builder.Symbol();
    s.valid = true;
    s.symbol_id = kQuestionMark;
    ++impl_->placeholders;
  }
  builder.Finish();
  return Status::Ok();
}

}  // namespace fv
