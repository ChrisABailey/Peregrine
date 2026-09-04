// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// GeoSymStyleEngine tests (vpf-geosym plan phase V5b) — the product-specific
// half of the vector seam, plus the end-to-end DNC render that is the phase's
// milestone ("the VPF pan-viewer moment").
//
// Real data: TestData/GeoSymbol (rule tables + 757 CGM symbols) and
// TestData/vpf/dnc17/h1707300 (the Nantucket Sound harbor library). Both are
// git-ignored, so every real-data case skips when they are absent.
//
// The golden render pins an FNV-1a hash of the canvas AND writes a PNG for
// eyeballing, same convention as canvas_test.cpp. Labels are deliberately OFF
// in the golden scene: label glyphs come from the host font, which would make
// the hash machine-dependent. A separate test asserts labels put ink down.

#include "fv_geosym_style.h"

#include <gtest/gtest.h>
#include <png.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fv_cgm_symbol.h"
#include "fv_vpf_vector_source.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/vector/renderer.h"

namespace fs = std::filesystem;

namespace {

std::string TestDataDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  return d != nullptr ? std::string(d) : std::string();
}

bool HaveSymAssets() {
  const std::string d = TestDataDir();
  return !d.empty() && fs::is_directory(d + "/GeoSymbol/SymAssign") &&
         fs::is_directory(d + "/GeoSymbol/Graphics");
}

std::string HarborLibrary() {
  const std::string d = TestDataDir();
  if (d.empty()) return {};
  const std::string p = d + "/vpf/dnc17/h1707300";
  return fs::is_directory(p) ? p : std::string();
}

#define SKIP_WITHOUT_ASSETS() \
  if (!HaveSymAssets()) GTEST_SKIP() << "no GeoSym assets"

// Opens the engine for DNC or skips.
std::unique_ptr<fv::GeoSymStyleEngine> OpenDnc() {
  std::unique_ptr<fv::GeoSymStyleEngine> e(new fv::GeoSymStyleEngine);
  const fv::Status s = e->Open(TestDataDir(), fv::kGeoSymDnc);
  if (!s.ok()) {
    ADD_FAILURE() << "GeoSym open failed: " << s.message;
    return nullptr;
  }
  return e;
}

fv::VectorFeature LineFeature(const std::string& facc,
                              std::vector<std::pair<std::string, std::string>> attrs = {}) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kLine;
  f.style_key = facc;
  f.layer = "test";
  f.attributes = std::move(attrs);
  f.parts.push_back({{41.6, -70.0}, {41.7, -69.9}});
  return f;
}

fv::VectorFeature PointFeature(const std::string& facc,
                               std::vector<std::pair<std::string, std::string>> attrs = {}) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kPoint;
  f.style_key = facc;
  f.layer = "test";
  f.attributes = std::move(attrs);
  f.parts.push_back({{41.65, -69.95}});
  return f;
}

fv::VectorFeature AreaFeature(const std::string& facc,
                              std::vector<std::pair<std::string, std::string>> attrs = {}) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kArea;
  f.style_key = facc;
  f.layer = "test";
  f.attributes = std::move(attrs);
  f.parts.push_back({{41.6, -70.0}, {41.7, -70.0}, {41.7, -69.9}, {41.6, -69.9},
                     {41.6, -70.0}});
  return f;
}

// The fill of the first result that carries one.
const fv::FillStyle* FirstFill(const std::vector<fv::StyleResult>& rs) {
  for (const fv::StyleResult& r : rs)
    if (r.fill.valid) return &r.fill;
  return nullptr;
}

// Two REAL DNC features whose fullsym.txt rows actually fire, so these tests
// exercise product rows rather than the "no condition matched" fallback:
//   acc=1  -> row 2249 "depth curve - accurate" (vgroup 33020, dispcat 3)
//   cvl=9.1 -> row 2257 "depth area (medium shallow)" (vgroup 13030, dispcat 1)
fv::VectorFeature DepthCurve() { return LineFeature("BE010", {{"acc", "1"}}); }
fv::VectorFeature DepthArea() {
  return AreaFeature("BE010", {{"cvl", "9.1"}});
}

// A DNC bridge, which is the tree's cleanest LABELLED row: fullsym.txt 1110
// carries labatt=nam with txrowid 1 (a TEXT.TXT row that exists) and IHO text
// group 21, while 1108/1109 put two strokes under it. NOTE the depth-curve
// label row 2251 cannot be used for this — its txrowid is 34 and TEXT.TXT
// stops at 33, so GetTextByID misses and no label is ever drawn. That is the
// shipped data, not a port bug; see the "no default styling to fall back on"
// comment in fv_geosym_style.cpp.
fv::VectorFeature LabelledBridge() {
  return LineFeature("AQ040", {{"nam", "Bourne"}, {"bsc", "12"}});
}

fv::StyleContext Ctx() {
  fv::StyleContext c;
  c.scale_denominator = 50000.0;
  c.device_dpi = 96.0;
  c.symbol_scale = 1.0;
  return c;
}

uint64_t Fnv1a(const fv::PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int i = 0; i < b.Width() * 4; ++i) {
      h ^= row[i];
      h *= 1099511628211ull;
    }
  }
  return h;
}

void WritePng(const fv::PixelBuffer& b, const std::string& name) {
  const std::string path = name + ".png";
  FILE* f = fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(png);
  ASSERT_EQ(setjmp(png_jmpbuf(png)), 0);
  png_init_io(png, f);
  png_set_IHDR(png, info, b.Width(), b.Height(), 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  for (int y = 0; y < b.Height(); ++y)
    png_write_row(png, const_cast<unsigned char*>(b.Row(y)));
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  fclose(f);
}

// CpuCanvas has no built-in font (see cpu_canvas.cpp: an empty font_path
// with no SetDefaultFont is an error), so the label test supplies a host one.
// This is exactly why the golden scene keeps labels OFF.
std::string HostFont() {
  const char* candidates[] = {
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Supplemental/Courier New.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  for (const char* f : candidates)
    if (FILE* fp = fopen(f, "rb")) {
      fclose(fp);
      return f;
    }
  return {};
}

size_t NonBackgroundPixels(const fv::PixelBuffer& b, unsigned char bg) {
  size_t n = 0;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* p = row + 4 * x;
      if (p[0] != bg || p[1] != bg || p[2] != bg) ++n;
    }
  }
  return n;
}

// ---------------------------------------------------------------------------
// Table loading
// ---------------------------------------------------------------------------

TEST(GeoSymStyle, OpenLoadsTheDncAssignmentRows) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);
  EXPECT_TRUE(e->IsOpen());
  // fullsym.txt holds 687 rows with pid=5 (DNC). The four hardcoded
  // fallbacks (icon/line/defpt/defln) are added on top by the engine, as
  // CSymProduct::OpenProduct does, and are NOT counted here.
  EXPECT_EQ(e->row_count(), 687u);
}

TEST(GeoSymStyle, OpenFailsCleanlyOnAMissingDataDir) {
  fv::GeoSymStyleEngine e;
  const fv::Status s = e.Open("/nonexistent/geosym/root");
  EXPECT_FALSE(s.ok());
  EXPECT_FALSE(e.IsOpen());
}

TEST(GeoSymStyle, ADifferentProductSelectsDifferentRows) {
  SKIP_WITHOUT_ASSETS();
  fv::GeoSymStyleEngine dnc, vmap1;
  ASSERT_TRUE(dnc.Open(TestDataDir(), fv::kGeoSymDnc).ok());
  ASSERT_TRUE(vmap1.Open(TestDataDir(), fv::kGeoSymVmapLevel1).ok());
  EXPECT_NE(dnc.row_count(), vmap1.row_count());
  EXPECT_GT(vmap1.row_count(), 0u);
}

// ---------------------------------------------------------------------------
// Symbol assignment
// ---------------------------------------------------------------------------

// BA010 (coastline) has three DNC line rows, all at display priority 7,
// distinguished by ATTEXP conditions on the shoreline attributes:
//   1421  acc = 1 AND slt <> 6 AND slt <> 8      (accurate, not mangrove/marsh)
//   1422  acc <> 1 ...                            (approximate)
//   1423  slt = 6 OR slt = 8                      (mangrove, marsh)
TEST(GeoSymStyle, CoastlineGetsAStrokeAtItsTablePriority) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(
      e->Style(LineFeature("BA010", {{"acc", "1"}, {"slt", "1"}}), Ctx(), &out)
          .ok());
  ASSERT_FALSE(out.empty());
  for (const auto& r : out) {
    EXPECT_EQ(r.priority, 7) << "fullsym.txt gives BA010 dispri 7";
    EXPECT_TRUE(r.stroke.valid) << "a line row must produce a stroke";
    EXPECT_GE(r.stroke.pen.width, 1);
  }
}

// The 2nd-chance fallback. BA010's three conditions all test `acc`/`slt`,
// and CAEEntry::Evaluate returns FALSE outright for an attribute that is not
// in the list — so a coastline row stripped of those attributes matches
// nothing and GeoSym answers with "defln", the black default line at
// priority 9. Preserving that is the point: an unstyled DNC feature still
// draws rather than vanishing.
//
// (The real dnc17 coastl features all carry slt=8, which DOES match rule
// 1423 — "slt = 6 OR slt = 8", mangrove/marsh — so they style normally;
// the next test pins that.)
TEST(GeoSymStyle, KnownFaccWithNoMatchingRuleFallsBackToDefln) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(LineFeature("BA010"), Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());
  EXPECT_EQ(out[0].priority, 9) << "defln draws at priority 9";
  EXPECT_TRUE(out[0].stroke.valid);
}

// The real slt=8 coastline takes rule 1423 instead, at the table priority.
TEST(GeoSymStyle, RealCoastlineAttributesSelectTheMangroveRule) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(
      e->Style(LineFeature("BA010", {{"acc", "1"}, {"slt", "8"}}), Ctx(), &out)
          .ok());
  ASSERT_FALSE(out.empty());
  EXPECT_EQ(out[0].priority, 7) << "matched a real BA010 row, not the fallback";
}

// BE010 = depth curve, display priority 5 — i.e. it must draw UNDER the
// coastline. That relative order is what the renderer's priority sort uses.
TEST(GeoSymStyle, DepthCurveSitsBelowCoastline) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  std::vector<fv::StyleResult> curve, coast;
  ASSERT_TRUE(e->Style(LineFeature("BE010", {{"acc", "1"}}), Ctx(), &curve).ok());
  ASSERT_TRUE(
      e->Style(LineFeature("BA010", {{"acc", "1"}, {"slt", "1"}}), Ctx(), &coast)
          .ok());
  ASSERT_FALSE(curve.empty());
  ASSERT_FALSE(coast.empty());
  EXPECT_EQ(curve[0].priority, 5);
  EXPECT_LT(curve[0].priority, coast[0].priority);
}

// --- area fill (the `areasym` column) --------------------------------------
//
// An AREA row's `areasym` names a CGM whose PICTURE fill colour is the brush
// (CCGMSymbol::DrawArea). Until 2026-07-25 Style() parsed the column and never
// read it, so DNC drew as outlines on white.

TEST(GeoSymStyle, LandIsBuffAndOpenWaterIsBlue) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  // BA030 "island" -> area symbol 0822; BA040 "water (except inland)" -> 0821.
  std::vector<fv::StyleResult> land, water;
  ASSERT_TRUE(e->Style(AreaFeature("BA030"), Ctx(), &land).ok());
  ASSERT_TRUE(e->Style(AreaFeature("BA040"), Ctx(), &water).ok());

  const fv::FillStyle* lf = FirstFill(land);
  const fv::FillStyle* wf = FirstFill(water);
  ASSERT_NE(lf, nullptr) << "land area got no fill";
  ASSERT_NE(wf, nullptr) << "water area got no fill";

  // Straight out of 0822.cgm / 0821.cgm — not colours this port invented.
  EXPECT_EQ(lf->brush.color.r, 222);
  EXPECT_EQ(lf->brush.color.g, 205);
  EXPECT_EQ(lf->brush.color.b, 139);
  EXPECT_EQ(wf->brush.color.r, 180);
  EXPECT_EQ(wf->brush.color.g, 218);
  EXPECT_EQ(wf->brush.color.b, 247);
  // Land reads warm, water reads cool.
  EXPECT_GT(lf->brush.color.r, lf->brush.color.b);
  EXPECT_GT(wf->brush.color.b, wf->brush.color.r);
}

// The depth ramp: BE010's rows name a different area symbol per depth band and
// the ATTEXP conditions pick one from the feature's `cvl` (depth curve value)
// against the mariner settings. Deeper water is paler; shallower is bluer.
TEST(GeoSymStyle, DepthAreasShadeFromTheCurveValue) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  auto fill_for = [&](const char* cvl) {
    std::vector<fv::StyleResult> rs;
    EXPECT_TRUE(e->Style(AreaFeature("BE010", {{"cvl", cvl}}), Ctx(), &rs).ok());
    const fv::FillStyle* f = FirstFill(rs);
    return f ? f->brush.color : fv::FvColor{0, 0, 0, 0};
  };

  const fv::FvColor shallow = fill_for("0");     // 0810
  const fv::FvColor mid = fill_for("9.1");       // 0821
  const fv::FvColor deep = fill_for("55");       // 0805
  ASSERT_GT(shallow.a, 0) << "shallow depth area got no fill";
  ASSERT_GT(mid.a, 0);
  ASSERT_GT(deep.a, 0);

  // Distinct bands...
  EXPECT_FALSE(shallow.r == mid.r && shallow.g == mid.g && shallow.b == mid.b);
  EXPECT_FALSE(mid.r == deep.r && mid.g == deep.g && mid.b == deep.b);
  // ...ordered light-with-depth, the chart convention (and all blue-ish).
  EXPECT_LT(shallow.r, mid.r);
  EXPECT_LT(mid.r, deep.r);
  EXPECT_GE(shallow.b, shallow.r);
  EXPECT_GE(deep.b, deep.g - 16);
}

// ---------------------------------------------------------------------------
// Mariner settings (fvkit/vector/mariner.h) — the DNC half
//
// Until these landed, the depth ramp above was driven by CECDISValues'
// default-constructed knobs and a vessel's draft could not be entered at all.
// The mapping is read off the delivered tables, so each test below names the
// fullsym.txt rows it is really about.
// ---------------------------------------------------------------------------

// Every fill in the order the engine emitted them.
std::vector<fv::FvColor> Fills(const std::vector<fv::StyleResult>& rs) {
  std::vector<fv::FvColor> out;
  for (const fv::StyleResult& r : rs)
    if (r.fill.valid) out.push_back(r.fill.brush.color);
  return out;
}

bool SameRgb(const fv::FvColor& a, const fv::FvColor& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

TEST(GeoSymStyle, GeoSymStartsOnCECDISValuesOwnDefaults) {
  fv::GeoSymStyleEngine e;  // no Open needed: the defaults are constructed
  const fv::MarinerSettings& m = e.mariner();
  // NOT the struct's defaults, which are S-52's (30/2/30/30, four shades, no
  // pattern). These are the numbers every DNC golden was pinned over.
  EXPECT_DOUBLE_EQ(m.safety_contour, 10.0);   // ssdc
  EXPECT_DOUBLE_EQ(m.shallow_contour, 2.0);   // mssc
  EXPECT_DOUBLE_EQ(m.deep_contour, 30.0);     // msdc
  EXPECT_FALSE(m.two_shades);                 // idsm = 0
  EXPECT_TRUE(m.shallow_pattern);             // isdm = 1
}

// The whole point of the feature: raising the safety contour to a deeper
// vessel draft moves the bands, so water that was "medium deep" becomes
// "medium shallow" without the chart changing.
TEST(GeoSymStyle, TheSafetyContourMovesTheDepthBands) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  auto first_fill = [&](const char* cvl) {
    std::vector<fv::StyleResult> rs;
    EXPECT_TRUE(e->Style(AreaFeature("BE010", {{"cvl", cvl}}), Ctx(), &rs).ok());
    const fv::FillStyle* f = FirstFill(rs);
    return f != nullptr ? f->brush.color : fv::FvColor{0, 0, 0, 0};
  };

  // Default ssdc = 10: 15 m is medium DEEP (row 2257, symbol 0820) and 5 m is
  // medium SHALLOW (row 2267, symbol 0821).
  const fv::FvColor deep_15 = first_fill("15");
  const fv::FvColor shallow_5 = first_fill("5");
  ASSERT_GT(deep_15.a, 0);
  ASSERT_GT(shallow_5.a, 0);
  ASSERT_FALSE(SameRgb(deep_15, shallow_5)) << "the two bands must differ";

  // A deeper ship: safety contour 20 m. 15 m is now inside it, so the SAME
  // feature takes the medium-shallow shade the 5 m one had.
  fv::MarinerSettings m = e->mariner();
  m.safety_contour = 20.0;
  e->SetMariner(m);
  EXPECT_TRUE(SameRgb(first_fill("15"), shallow_5))
      << "15 m did not cross the safety contour when it moved to 20 m";
  EXPECT_FALSE(SameRgb(first_fill("15"), deep_15));

  // ...and back. The engine must not have baked the old value anywhere.
  m.safety_contour = 10.0;
  e->SetMariner(m);
  EXPECT_TRUE(SameRgb(first_fill("15"), deep_15));
}

// idsm: 1 = two depth shades, 0 = four. With two shades everything at or
// beyond the safety contour is one colour (row 2260, symbol 0805), which is
// the ECDIS "safe / unsafe" display.
TEST(GeoSymStyle, TwoShadesCollapsesTheRampAtTheSafetyContour) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  auto first_fill = [&](const char* cvl) {
    std::vector<fv::StyleResult> rs;
    EXPECT_TRUE(e->Style(AreaFeature("BE010", {{"cvl", cvl}}), Ctx(), &rs).ok());
    const fv::FillStyle* f = FirstFill(rs);
    return f != nullptr ? f->brush.color : fv::FvColor{0, 0, 0, 0};
  };

  // Four shades: 15 m (medium deep) and 55 m (very deep) are different.
  ASSERT_FALSE(SameRgb(first_fill("15"), first_fill("55")));

  fv::MarinerSettings m = e->mariner();
  m.two_shades = true;
  e->SetMariner(m);
  const fv::FvColor safe_15 = first_fill("15");
  const fv::FvColor safe_55 = first_fill("55");
  ASSERT_GT(safe_15.a, 0);
  EXPECT_TRUE(SameRgb(safe_15, safe_55))
      << "two-shade mode still drew two different deep-water colours";
  // ...and the shallow side is still its own shade.
  EXPECT_FALSE(SameRgb(first_fill("1"), safe_15));
}

// isdm: "shallow display mode". ON (the DNC default) adds area symbol 0949 —
// a second, patterned fill — over the shallow bands; OFF leaves the flat fill
// alone. Rows 2267/2268 are the pair.
TEST(GeoSymStyle, ShallowPatternIsASecondFillAndCanBeTurnedOff) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  auto fills = [&](const char* cvl) {
    std::vector<fv::StyleResult> rs;
    EXPECT_TRUE(e->Style(AreaFeature("BE010", {{"cvl", cvl}}), Ctx(), &rs).ok());
    return Fills(rs);
  };

  const std::vector<fv::FvColor> on = fills("5");
  ASSERT_GE(on.size(), 2u) << "shallow mode on should add the 0949 pattern";

  fv::MarinerSettings m = e->mariner();
  m.shallow_pattern = false;
  e->SetMariner(m);
  const std::vector<fv::FvColor> off = fills("5");
  ASSERT_EQ(off.size(), on.size() - 1);
  // The band's own colour is unchanged — only the overlay went away.
  EXPECT_TRUE(SameRgb(off[0], on[0]));
}

// DNC has ONE number where S-52 has two: ssdc is the safety contour AND the
// sounding threshold. BE020 rows 2318 (hdp <= ssdc, dark) and 2319
// (hdp > ssdc, light) are the pair, and they are label rows, so this is also
// the one test here that needs the labels on.
TEST(GeoSymStyle, TheSafetyContourAlsoDarkensSoundings) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);
  e->SetDrawLabels(true);

  // Both BE020 rows also require `hdh = NULL` — a sounding, not a drying
  // height — and a MISSING attribute makes every ATTEXP comparison false, so
  // the null has to be stated for either row to fire at all.
  const std::vector<std::pair<std::string, std::string>> kSounding15m = {
      {"hdp", "15"}, {"hdh", "NULL"}};

  auto label_color = [&]() {
    std::vector<fv::StyleResult> rs;
    EXPECT_TRUE(e->Style(PointFeature("BE020", kSounding15m), Ctx(), &rs).ok());
    for (const fv::StyleResult& r : rs)
      if (r.label.valid) return r.label.style.color;
    return fv::FvColor{0, 0, 0, 0};
  };

  // Default ssdc = 10, so a 15 m sounding is deeper than the ship needs: light.
  const fv::FvColor light = label_color();
  ASSERT_GT(light.a, 0) << "no sounding label drawn at all";

  fv::MarinerSettings m = e->mariner();
  m.safety_contour = 20.0;
  e->SetMariner(m);
  const fv::FvColor dark = label_color();
  ASSERT_GT(dark.a, 0);
  EXPECT_FALSE(SameRgb(light, dark))
      << "the sounding did not change shade when it fell inside the contour";
  // "Dark" is literally that: the shallower row is the darker ink.
  EXPECT_LT(dark.r + dark.g + dark.b, light.r + light.g + light.b);
}

// The retained-scene contract: a real change must bump the style epoch, and a
// no-op set must NOT — a setter an interactive caller pushes every frame would
// otherwise throw the scene away once per redraw (the R3a rule).
TEST(GeoSymStyle, SetMarinerBumpsTheEpochOnlyWhenSomethingMoved) {
  fv::GeoSymStyleEngine e;
  const uint64_t before = e.style_epoch();
  e.SetMariner(e.mariner());  // identical settings
  EXPECT_EQ(e.style_epoch(), before);
  // ...and reading is free, which is why the mutable accessor has its own
  // name: mariner() through a non-const engine must not be a scene rebuild.
  EXPECT_DOUBLE_EQ(e.mariner().safety_contour, 10.0);
  EXPECT_EQ(e.style_epoch(), before);

  fv::MarinerSettings m = e.mariner();
  m.safety_contour += 1.0;
  e.SetMariner(m);
  EXPECT_NE(e.style_epoch(), before);
}

// A hollow/empty fill style means "do not fill" and must not paint.
TEST(GeoSymStyle, AreaRowWithoutAnAreaSymbolGetsNoFill) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);
  // An unknown FACC falls through to the "line" pseudo-entry, which carries no
  // area symbol at all.
  std::vector<fv::StyleResult> rs;
  ASSERT_TRUE(e->Style(AreaFeature("ZZ999"), Ctx(), &rs).ok());
  EXPECT_EQ(FirstFill(rs), nullptr);
}

// CCGMPattern stores its monochrome bits INVERTED (AddMonochromeBit sets a bit
// when the cell is off), and GDI's pattern brush paints the ZERO bits in the
// foreground colour. Reading that backwards turned DNC's sparse shallow-water
// stipple into a near-opaque grey slab over the depth shading.
TEST(GeoSymStyle, StippledAreaFillIsSparseNotOpaque) {
  SKIP_WITHOUT_ASSETS();
  fv::CgmSymbol stipple;
  const std::string path = TestDataDir() + "\\GeoSymbol\\Graphics\\0934.cgm";
  ASSERT_TRUE(stipple.LoadFile(path).ok()) << "0934.cgm missing";
  ASSERT_FALSE(stipple.area_style().patterns.empty())
      << "0934 should carry a pattern";
  EXPECT_FALSE(stipple.area_style().patterns[0].solid);

  size_t ink = 0, total = 0;
  for (unsigned char byte : stipple.area_style().patterns[0].bits)
    for (int bit = 0; bit < 8; ++bit, ++total)
      if ((byte & (1u << bit)) == 0) ++ink;
  ASSERT_GT(total, 0u);
  const double coverage = static_cast<double>(ink) / total;
  EXPECT_GT(coverage, 0.0);
  EXPECT_LT(coverage, 0.25) << "stipple coverage " << coverage
                            << " — bits are probably being read uninverted";
}

// An unknown FACC falls back to the pseudo-entries CSymProduct::OpenProduct
// appends: "icon" (symbol 5000) for points, "line" (5001) for lines.
TEST(GeoSymStyle, UnknownFaccFallsBackToIconAndLine) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  std::vector<fv::StyleResult> pt;
  ASSERT_TRUE(e->Style(PointFeature("ZZ999"), Ctx(), &pt).ok());
  ASSERT_EQ(pt.size(), 1u);
  EXPECT_TRUE(pt[0].symbol.valid);
  EXPECT_EQ(pt[0].symbol.symbol_id, "5000");
  EXPECT_EQ(pt[0].priority, 9);

  std::vector<fv::StyleResult> ln;
  ASSERT_TRUE(e->Style(LineFeature("ZZ999"), Ctx(), &ln).ok());
  ASSERT_FALSE(ln.empty());
  // Symbol 5001 is a SAMI line whose cycle stamps a point symbol, so since
  // E3b it comes back as a line PATTERN for the shared placer rather than as
  // a plain pen. Either way it must put linework down.
  EXPECT_TRUE(ln[0].stroke.valid || ln[0].line_pattern.valid);
  EXPECT_EQ(ln[0].priority, 9);
}

// Direction-of-flow rotates a point symbol, exactly as CCGMSymbol::DrawSymbol
// does (it reads the "dof" attribute; the table's `orient` column is skipped
// there too).
TEST(GeoSymStyle, DofAttributeRotatesThePointSymbol) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  std::vector<fv::StyleResult> a, b;
  ASSERT_TRUE(e->Style(PointFeature("ZZ999"), Ctx(), &a).ok());
  ASSERT_TRUE(e->Style(PointFeature("ZZ999", {{"dof", "90"}}), Ctx(), &b).ok());
  ASSERT_FALSE(a.empty());
  ASSERT_FALSE(b.empty());
  EXPECT_DOUBLE_EQ(a[0].symbol.rotation_deg, 0.0);
  EXPECT_DOUBLE_EQ(b[0].symbol.rotation_deg, 90.0);
}

// Line width comes from the CGM's SAMI/picture line width in HIMETRIC,
// converted with the device DPI (GDI's HIMETRICtoDP). Doubling the DPI must
// not make a line thinner, and the width is always at least one pixel.
TEST(GeoSymStyle, LineWidthScalesWithDeviceDpi) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  fv::StyleContext lo = Ctx();
  fv::StyleContext hi = Ctx();
  hi.device_dpi = 300.0;

  std::vector<fv::StyleResult> a, b;
  const fv::VectorFeature f = LineFeature("BA010", {{"acc", "1"}, {"slt", "1"}});
  ASSERT_TRUE(e->Style(f, lo, &a).ok());
  ASSERT_TRUE(e->Style(f, hi, &b).ok());
  ASSERT_FALSE(a.empty());
  ASSERT_EQ(a.size(), b.size());
  EXPECT_GE(a[0].stroke.pen.width, 1);
  EXPECT_GE(b[0].stroke.pen.width, a[0].stroke.pen.width);
}

// A label attribute can name SEVERAL attributes, whose values are joined with
// commas (CCGMSymbol::DrawLabel's Tokenize branch). BC020 buoy-light rows are
// the DNC case: row 1714 labels "col,per" when col/per are set and eol/lvn/mlr
// are 0 or unknown. TEXT.TXT row 22 gives size 10 and offset 6 mm at 90 deg,
// so the label sits to the RIGHT of the buoy (dx > 0, dy ~ 0).
TEST(GeoSymStyle, MultiAttributeLabelConcatenatesValues) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);
  e->SetDrawLabels(true);

  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(PointFeature("BC020", {{"col", "R"},
                                              {"per", "4"},
                                              {"eol", "0"},
                                              {"lvn", "0"},
                                              {"mlr", ""}}),
                       Ctx(), &out)
                  .ok());
  bool found = false;
  for (const auto& r : out) {
    if (!r.label.valid) continue;
    if (r.label.text != "R,4") continue;
    found = true;
    EXPECT_GT(r.label.dx, 0) << "tdir 90 puts the label east of the feature";
    EXPECT_EQ(r.label.dy, 0);
    // QUIRK: tsize is documented in points but GDI consumed it as device
    // units (CSymFont: nHeight = -m_nSize), so 10 stays 10 px at scale 1.
    EXPECT_DOUBLE_EQ(r.label.style.size, 10.0);
  }
  EXPECT_TRUE(found) << "no concatenated col,per label was produced";
}

// Labels stay off unless asked for, which is what keeps the golden render
// independent of the host font.
TEST(GeoSymStyle, LabelsAreOffByDefault) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(PointFeature("BE020", {{"hdp", "0.3"}, {"hdh", ""}}),
                       Ctx(), &out)
                  .ok());
  for (const auto& r : out) EXPECT_FALSE(r.label.valid);
}

// Below 20% the original bails out of DrawSymbol/DrawSAMILine/DrawText
// ("make sure it is something viewable"); the engine keeps that cutoff.
TEST(GeoSymStyle, TinySymbolScaleProducesNothing) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  fv::StyleContext tiny = Ctx();
  tiny.symbol_scale = 0.1;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(
      e->Style(LineFeature("BA010", {{"acc", "1"}, {"slt", "1"}}), tiny, &out)
          .ok());
  EXPECT_TRUE(out.empty());
}

// ---------------------------------------------------------------------------
// CGM -> VectorSymbol
// ---------------------------------------------------------------------------

TEST(GeoSymStyle, SymbolLoadsACgmDisplayListAndCachesIt) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  const fv::VectorSymbol* s = e->Symbol("0001");
  ASSERT_NE(s, nullptr);
  EXPECT_FALSE(s->primitives.empty());
  EXPECT_GT(s->max_x, s->min_x);
  EXPECT_EQ(e->symbols_loaded(), 1u);

  // Same pointer on the second ask: the cache, not a re-parse.
  EXPECT_EQ(e->Symbol("0001"), s);
  EXPECT_EQ(e->symbols_loaded(), 1u);

  // Every primitive must say how to paint itself.
  for (const auto& p : s->primitives)
    EXPECT_TRUE(p.has_fill || p.has_stroke || !p.text.empty());
}

// Orientation, pinned independently of the golden hash: a golden can be
// re-pinned by hand, a mirrored symbol cannot sneak past this.
//
// 0003.cgm is authored (CGM VDC, y up) with extent ll=(-175,-58),
// ur=(304,628) — nearly all of its ink is ABOVE the anchor. The parser
// applies the picture's y multiplier (-1) as it reads, and ToVectorSymbol
// applies it a second time exactly as CCGMDrawingObject::RotateVDC does on
// Windows, so a VectorSymbol must come back out in the authored y-up frame.
// Without that second application (the state this shipped in until
// 2026-07-27) max_y and min_y swap sign and every DNC symbol renders upside
// down.
TEST(GeoSymStyle, SymbolComesBackInTheAuthoredYUpFrame) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  const fv::VectorSymbol* s = e->Symbol("0003");
  ASSERT_NE(s, nullptr);
  EXPECT_DOUBLE_EQ(s->min_x, -175.0);
  EXPECT_DOUBLE_EQ(s->max_x, 304.0);
  EXPECT_DOUBLE_EQ(s->min_y, -58.0);
  EXPECT_DOUBLE_EQ(s->max_y, 628.0);
  // The point of the four above, stated once more as intent: the body of this
  // symbol sits above its anchor, not below it.
  EXPECT_GT(s->max_y, -s->min_y);

  // And the vertex the V4 parser test pins (m_vertices y = -415, i.e. the
  // once-applied frame) must arrive negated.
  bool saw = false;
  for (const auto& p : s->primitives) {
    for (const auto& pt : p.points) {
      if (pt.y == 415.0) saw = true;
      EXPECT_NE(pt.y, -415.0) << "vertex arrived in the parser's y-down frame";
    }
  }
  EXPECT_TRUE(saw) << "0003.cgm's pinned polyline vertex went missing";
}

TEST(GeoSymStyle, UnknownSymbolIdIsNullAndIsNotRetried) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->Symbol("9999"), nullptr);
  EXPECT_EQ(e->Symbol("9999"), nullptr);
  EXPECT_EQ(e->Symbol(""), nullptr);
  EXPECT_EQ(e->symbols_loaded(), 0u);

  // E3c: the shared core COUNTS the miss, which GeoSym previously only
  // remembered. A product naming a .cgm the delivered Graphics dir does not
  // have is a data fact, and it is now reportable — the same accounting S-52's
  // raster-only symbol names already had.
  ASSERT_EQ(e->unresolved_symbols().count("9999"), 1u);
  EXPECT_EQ(e->unresolved_symbols().at("9999"), 1u)
      << "two requests, one load attempt";
  EXPECT_EQ(e->unresolved_symbols().count(""), 0u)
      << "an empty id is not a missing symbol";
}

// The whole shipped symbol set must convert without a crash or an empty
// display list where the CGM had elements. (The V4 test already proves all
// 757 CGMs parse; this proves the conversion to the product-neutral form.)
TEST(GeoSymStyle, EveryShippedSymbolConverts) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  size_t converted = 0, with_primitives = 0;
  for (const auto& entry : fs::directory_iterator(TestDataDir() + "/GeoSymbol/Graphics")) {
    if (entry.path().extension() != ".cgm") continue;
    const std::string id = entry.path().stem().string();
    const fv::VectorSymbol* s = e->Symbol(id);
    if (s == nullptr) continue;
    ++converted;
    if (!s->primitives.empty()) ++with_primitives;
  }
  EXPECT_EQ(converted, 757u);
  // Line symbols legitimately have no drawable elements (their content is
  // the SAMI stroke description), so this is a floor, not an equality.
  EXPECT_GT(with_primitives, 600u);
}

// ---------------------------------------------------------------------------
// End to end: DNC harbor viewport through the shared renderer
// ---------------------------------------------------------------------------

// Pinned 2026-07-23, re-pinned 2026-07-24 after V5c added AREA features:
// Nantucket Sound comes out as a recognisable harbor chart with FILLED areas:
// buff land, green foreshore/marsh, and water shaded by depth from strong blue
// inshore through pale to near-white offshore, under the V5b coastline, depth
// contours, dredged channels, navaids and bottom-characteristic symbols.
// Re-pinned 2026-07-25 when Style() started reading the `areasym` column (the
// fills and the depth ramp are new; before that areas were outline-only).
// Visually re-checked geosym_harbor.png before updating.
// Re-pinned 2026-07-27: point symbols were being drawn UPSIDE DOWN (the VDC
// direction multipliers were applied once by the parser instead of twice —
// see ApplyVdcDir in fv_geosym_style.cpp). Everything else is unchanged;
// SymbolComesBackInTheAuthoredYUpFrame guards the orientation directly.
// Re-pinned 2026-07-27 (E3b): SAMI line symbols are placed along the path for
// the first time. Where a cable/limit line used to come out as a black dashed
// run — the old code pushed a point-symbol element's LENGTH into the pen's
// dash array, so the symbol became a dash — it is now the magenta chain the
// CGM actually authors. 972 pixels moved; nothing else in the scene did.
// Visually re-checked geosym_harbor.png before updating.
// 0 = probe mode (prints hash, no assert).
constexpr uint64_t kHashHarbor = 0xff2caf6ea91219c4ull;

// E3b: a SAMI line component whose cycle stamps a point symbol becomes a
// LINE PATTERN for the shared placer, not a pen. Before E3b the symbol run's
// LENGTH was pushed into the pen's dash array, so the symbol silently became
// a dash — 89 of the 757 delivered CGM line symbols carry such a run.
TEST(GeoSymStyle, SamiLineWithPointSymbolsBecomesALinePattern) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);

  // Symbol 5001 is the "unknown FACC" line fallback and is one of them.
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(LineFeature("ZZ999"), Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());

  const fv::LinePatternStyle* lp = nullptr;
  for (const fv::StyleResult& r : out)
    if (r.line_pattern.valid) lp = &r.line_pattern;
  ASSERT_NE(lp, nullptr) << "the SAMI cycle came back as a plain pen";

  size_t stamps = 0;
  for (const fv::PathRun& run : lp->runs) {
    if (run.type != fv::PathRunType::kSymbol) continue;
    ++stamps;
    EXPECT_FALSE(run.symbol_id.empty());
    // The CGM names the symbol as a FILE; the id handed to the renderer must
    // be the bare number Symbol() resolves.
    EXPECT_EQ(run.symbol_id.find(".cgm"), std::string::npos);
    EXPECT_NE(e->Symbol(run.symbol_id), nullptr)
        << run.symbol_id << " does not resolve";
  }
  EXPECT_GT(stamps, 0u);

  // A pattern that stamps must not ALSO be drawn as a stroke, or the dashes
  // would be painted twice — once by the pen and once by the placer.
  for (const fv::StyleResult& r : out)
    if (r.line_pattern.valid) EXPECT_FALSE(r.stroke.valid);
}

TEST(GeoSymRender, NantucketSoundHarborViewport) {
  SKIP_WITHOUT_ASSETS();
  const std::string lib = HarborLibrary();
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data";

  auto source = std::make_shared<fv::VpfVectorSource>();
  ASSERT_TRUE(source->Open(lib).ok());

  auto style = std::make_shared<fv::GeoSymStyleEngine>();
  ASSERT_TRUE(style->Open(TestDataDir(), fv::kGeoSymDnc).ok());

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(512, 512).ok());
  ASSERT_TRUE(proj.SetCenter(fv::GeoPoint{41.70, -69.90}).ok());
  // Explicit resolution, not a scale: the golden must not move if
  // MapScaleUtil's latitude-dependent dpp is ever retuned.
  ASSERT_TRUE(proj.SetResolution(0.0004, 0.0004).ok());

  fv::CpuCanvas canvas(512, 512);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});

  fv::VectorRenderer r(source, style);
  ASSERT_TRUE(r.Render(proj, &canvas).ok());

  EXPECT_GT(r.features_queried(), 100u);
  EXPECT_GT(r.draws_emitted(), 100u);
  EXPECT_GT(NonBackgroundPixels(canvas.Buffer(), 255), 1000u)
      << "the chart came out blank";

  const uint64_t h = Fnv1a(canvas.Buffer());
  if (kHashHarbor == 0)
    printf("PROBE harbor hash: 0x%llxull\n", (unsigned long long)h);
  else
    EXPECT_EQ(h, kHashHarbor);
  WritePng(canvas.Buffer(), "geosym_harbor");
}

TEST(GeoSymRender, RenderIsDeterministic) {
  SKIP_WITHOUT_ASSETS();
  const std::string lib = HarborLibrary();
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data";

  uint64_t hashes[2] = {0, 0};
  for (int i = 0; i < 2; ++i) {
    auto source = std::make_shared<fv::VpfVectorSource>();
    ASSERT_TRUE(source->Open(lib).ok());
    auto style = std::make_shared<fv::GeoSymStyleEngine>();
    ASSERT_TRUE(style->Open(TestDataDir(), fv::kGeoSymDnc).ok());

    fv::MapProjection proj;
    proj.SetSurfaceSize(256, 256);
    proj.SetCenter(fv::GeoPoint{41.70, -69.90});
    proj.SetResolution(0.0008, 0.0008);

    fv::CpuCanvas canvas(256, 256);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    fv::VectorRenderer r(source, style);
    ASSERT_TRUE(r.Render(proj, &canvas).ok());
    hashes[i] = Fnv1a(canvas.Buffer());
  }
  EXPECT_EQ(hashes[0], hashes[1]);
}

// --- the retained scene (R3a) ----------------------------------------------
//
// The hermetic scene tests live in port/fvkit/test/vector_scene_test.cpp. What
// can only be checked here, over real data and a real style engine, is the one
// claim that matters: retaining must not change the picture.

TEST(GeoSymRender, AReusedSceneRendersTheHarborIdentically) {
  SKIP_WITHOUT_ASSETS();
  const std::string lib = HarborLibrary();
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data";

  auto MakeSource = [&] {
    auto s = std::make_shared<fv::VpfVectorSource>();
    EXPECT_TRUE(s->Open(lib).ok());
    return s;
  };
  auto MakeStyle = [&] {
    auto s = std::make_shared<fv::GeoSymStyleEngine>();
    EXPECT_TRUE(s->Open(TestDataDir(), fv::kGeoSymDnc).ok());
    return s;
  };
  auto Viewport = [](double lat, double lon) {
    fv::MapProjection p;
    p.SetSurfaceSize(384, 384);
    p.SetCenter(fv::GeoPoint{lat, lon});
    p.SetResolution(0.0006, 0.0006);
    return p;
  };

  // A: no margin, so the target viewport is queried and styled from scratch.
  fv::CpuCanvas fresh(384, 384);
  fresh.Clear(fv::FvColor{255, 255, 255, 255});
  fv::VectorRenderer a(MakeSource(), MakeStyle());
  ASSERT_TRUE(a.Render(Viewport(41.7020, -69.9020), &fresh).ok());
  ASSERT_FALSE(a.scene_reused());

  // B: a margin, warmed at a different centre, then panned onto the same
  // viewport — which must now be a scene HIT and the same pixels.
  fv::CpuCanvas warm(384, 384), reused(384, 384);
  reused.Clear(fv::FvColor{255, 255, 255, 255});
  fv::VectorRenderer b(MakeSource(), MakeStyle());
  b.SetSceneMargin(0.5);
  ASSERT_TRUE(b.Render(Viewport(41.70, -69.90), &warm).ok());
  ASSERT_TRUE(b.Render(Viewport(41.7020, -69.9020), &reused).ok());
  ASSERT_TRUE(b.scene_reused()) << "the pan should have stayed inside the margin";

  EXPECT_EQ(Fnv1a(fresh.Buffer()), Fnv1a(reused.Buffer()));
  EXPECT_GT(NonBackgroundPixels(reused.Buffer(), 255), 1000u);
}

// Simplification is opt-in and render-only. Over the real harbour it should
// drop most vertices (DNC coastlines are far denser than a 900 px viewport can
// show) while leaving the chart recognisably the same amount of ink.
TEST(GeoSymRender, SimplifyingTheSceneKeepsTheChart) {
  SKIP_WITHOUT_ASSETS();
  const std::string lib = HarborLibrary();
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data";

  size_t ink[2] = {0, 0};
  size_t verts[2] = {0, 0};
  for (int simplify = 0; simplify < 2; ++simplify) {
    auto source = std::make_shared<fv::VpfVectorSource>();
    ASSERT_TRUE(source->Open(lib).ok());
    auto style = std::make_shared<fv::GeoSymStyleEngine>();
    ASSERT_TRUE(style->Open(TestDataDir(), fv::kGeoSymDnc).ok());

    fv::MapProjection proj;
    proj.SetSurfaceSize(512, 512);
    proj.SetCenter(fv::GeoPoint{41.70, -69.90});
    proj.SetResolution(0.0004, 0.0004);

    fv::CpuCanvas canvas(512, 512);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    fv::VectorRenderer r(source, style);
    if (simplify) r.SetSimplifyPixels(0.5);
    ASSERT_TRUE(r.Render(proj, &canvas).ok());
    ink[simplify] = NonBackgroundPixels(canvas.Buffer(), 255);
    verts[simplify] = r.scene().vertices_kept();
    EXPECT_EQ(r.scene().vertices_in(), 112330u)
        << "the source's own vertex count moved; re-check the tolerance below";
  }

  EXPECT_EQ(verts[0], 112330u) << "exact must keep every vertex";
  EXPECT_LT(verts[1], verts[0] / 2) << "half a pixel should thin DNC a lot";

  // Same chart: the inked area moves by a sub-pixel jitter on dense linework,
  // not by features appearing or disappearing.
  const double ratio = static_cast<double>(ink[1]) / static_cast<double>(ink[0]);
  EXPECT_GT(ratio, 0.97);
  EXPECT_LT(ratio, 1.03);
}

// Labels are off by default precisely so the golden above stays
// font-independent; this proves the label path works when turned on.
TEST(GeoSymRender, LabelsAddInkWhenEnabled) {
  SKIP_WITHOUT_ASSETS();
  const std::string lib = HarborLibrary();
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data";

  const std::string font = HostFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  size_t ink[2] = {0, 0};
  for (int with_labels = 0; with_labels < 2; ++with_labels) {
    auto source = std::make_shared<fv::VpfVectorSource>();
    ASSERT_TRUE(source->Open(lib).ok());
    auto style = std::make_shared<fv::GeoSymStyleEngine>();
    ASSERT_TRUE(style->Open(TestDataDir(), fv::kGeoSymDnc).ok());
    style->SetDrawLabels(with_labels != 0);

    fv::MapProjection proj;
    proj.SetSurfaceSize(512, 512);
    proj.SetCenter(fv::GeoPoint{41.70, -69.90});
    proj.SetResolution(0.0004, 0.0004);

    fv::CpuCanvas canvas(512, 512);
    ASSERT_TRUE(canvas.SetDefaultFont(font).ok());
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    fv::VectorRenderer r(source, style);
    ASSERT_TRUE(r.Render(proj, &canvas).ok());
    ink[with_labels] = NonBackgroundPixels(canvas.Buffer(), 255);
    if (with_labels) WritePng(canvas.Buffer(), "geosym_harbor_labels");
  }
  EXPECT_GT(ink[1], ink[0]) << "enabling labels put no extra ink on the chart";
}

// Brightness is the CSymColorAdjuster knob the original applied at draw time;
// pushing it to the maximum must visibly change the rendered chart.
TEST(GeoSymRender, ColorAdjustChangesTheOutput) {
  SKIP_WITHOUT_ASSETS();
  const std::string lib = HarborLibrary();
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data";

  uint64_t hashes[2] = {0, 0};
  for (int i = 0; i < 2; ++i) {
    auto source = std::make_shared<fv::VpfVectorSource>();
    ASSERT_TRUE(source->Open(lib).ok());
    auto style = std::make_shared<fv::GeoSymStyleEngine>();
    ASSERT_TRUE(style->Open(TestDataDir(), fv::kGeoSymDnc).ok());
    if (i == 1) style->SetColorAdjust(80, 0);

    fv::MapProjection proj;
    proj.SetSurfaceSize(256, 256);
    proj.SetCenter(fv::GeoPoint{41.70, -69.90});
    proj.SetResolution(0.0008, 0.0008);

    fv::CpuCanvas canvas(256, 256);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    fv::VectorRenderer r(source, style);
    ASSERT_TRUE(r.Render(proj, &canvas).ok());
    hashes[i] = Fnv1a(canvas.Buffer());
  }
  EXPECT_NE(hashes[0], hashes[1]);
}

// ---------------------------------------------------------------------------
// R2: the cross-product rule layer, retrofitted (plan §5.2)
//
// The values pinned below are REAL fullsym.txt rows for DNC (pid 5):
//   BE010 delin 2 (depth curve, line)  vgroup 33020, dispcat 3 (Other)
//   BE010 delin 2 label row            vgroup 33022, txtgroup 20
//   BE010 delin 3 (depth area)         vgroup 13030, dispcat 1 (Base)
//   BH140 delin 2 (river/stream)       vgroup 22010, dispcat 2 (Standard)
// ---------------------------------------------------------------------------

TEST(GeoSymRules, DefaultsChangeNothing) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_TRUE(e);
  EXPECT_EQ(0u, e->rules().size());
  EXPECT_TRUE(e->viewing_groups().default_on());
  EXPECT_EQ(fv::kDisplayOther, e->viewing_groups().max_category());

  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(DepthCurve(), Ctx(), &out).ok());
  EXPECT_FALSE(out.empty()) << "an unconfigured engine must symbolize as before";
  EXPECT_EQ(0u, e->rule_predicate_evaluations());
}

// ViewingGroupSet is ONE number space, so GeoSym's two kinds of group must
// not collide in it. That is a property of the shipped table, so pin it here
// rather than assuming it: viewing groups are 5-digit, text groups are 1..29.
TEST(GeoSymRules, ViewingAndTextGroupNumberingDoNotOverlap) {
  SKIP_WITHOUT_ASSETS();
  const std::string path = TestDataDir() + "/GeoSymbol/SymAssign/fullsym.txt";
  FILE* f = fopen(path.c_str(), "r");
  ASSERT_NE(f, nullptr) << path;
  char line[1024];
  long min_vgroup = 0, max_txtgroup = 0;
  bool past_header = false;
  while (fgets(line, sizeof(line), f) != nullptr) {
    if (!past_header) {
      if (line[0] == ';') past_header = true;
      continue;
    }
    // Fields 13 (vgroup) and 14 (txtgroup), 1-based, '|' delimited.
    int field = 1;
    const char* p = line;
    const char* f13 = nullptr;
    const char* f14 = nullptr;
    for (const char* c = line; *c != '\0'; ++c) {
      if (*c != '|') continue;
      if (field == 13) f13 = p;
      if (field == 14) f14 = p;
      ++field;
      p = c + 1;
    }
    if (f13 != nullptr && *f13 != '|') {
      const long v = strtol(f13, nullptr, 10);
      if (v > 0 && (min_vgroup == 0 || v < min_vgroup)) min_vgroup = v;
    }
    if (f14 != nullptr && *f14 != '|') {
      const long v = strtol(f14, nullptr, 10);
      if (v > max_txtgroup) max_txtgroup = v;
    }
  }
  fclose(f);
  ASSERT_GT(min_vgroup, 0);
  ASSERT_GT(max_txtgroup, 0);
  EXPECT_GT(min_vgroup, max_txtgroup)
      << "vgroup " << min_vgroup << " collides with txtgroup " << max_txtgroup
      << " — the two would need separate ViewingGroupSets";
}

TEST(GeoSymRules, ViewingGroupTogglesTheRowsThatBelongToIt) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_TRUE(e);

  std::vector<fv::StyleResult> before;
  ASSERT_TRUE(e->Style(DepthCurve(), Ctx(), &before).ok());
  ASSERT_FALSE(before.empty());
  ASSERT_EQ(5, before[0].priority) << "row 2249 (dispri 5), not the fallback";

  e->viewing_groups().Set(33020, false);  // depth curves off
  std::vector<fv::StyleResult> after;
  ASSERT_TRUE(e->Style(DepthCurve(), Ctx(), &after).ok());
  EXPECT_TRUE(after.empty()) << "the depth-curve rows should have dropped out";

  // …and it must NOT resurrect the 2nd-chance fallback. That is the failure
  // mode the label fix of V5b already found once: a hidden feature must draw
  // nothing, not a default black line at dispri 9.
  for (const fv::StyleResult& r : after)
    EXPECT_NE(9, r.priority) << "the defln fallback came back";

  // Another FACC's rows are untouched.
  std::vector<fv::StyleResult> other;
  ASSERT_TRUE(e->Style(LineFeature("BH140"), Ctx(), &other).ok());
  EXPECT_FALSE(other.empty());
}

TEST(GeoSymRules, DisplayCategoryIsAThresholdOverTheRealTable) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_TRUE(e);

  // Base only: BE010's depth AREAS are dispcat 1 and survive; BH140 (2) and
  // BE010's depth CURVES (3) do not.
  e->viewing_groups().SetMaxCategory(fv::kDisplayBase);

  std::vector<fv::StyleResult> areas;
  ASSERT_TRUE(e->Style(DepthArea(), Ctx(), &areas).ok());
  EXPECT_FALSE(areas.empty()) << "a Display Base row must survive";

  std::vector<fv::StyleResult> standard;
  ASSERT_TRUE(e->Style(LineFeature("BH140"), Ctx(), &standard).ok());
  EXPECT_TRUE(standard.empty());

  std::vector<fv::StyleResult> other;
  ASSERT_TRUE(e->Style(DepthCurve(), Ctx(), &other).ok());
  EXPECT_TRUE(other.empty());

  e->viewing_groups().SetMaxCategory(fv::kDisplayStandard);
  standard.clear();
  ASSERT_TRUE(e->Style(LineFeature("BH140"), Ctx(), &standard).ok());
  EXPECT_FALSE(standard.empty());
}

// The dense-label problem (Q6c item 3): GeoSym's txtgroup drops the LABEL
// without touching the symbology the row also carries.
TEST(GeoSymRules, TextGroupDropsLabelsOnly) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_TRUE(e);
  e->SetDrawLabels(true);

  const fv::VectorFeature f = LabelledBridge();
  std::vector<fv::StyleResult> with;
  ASSERT_TRUE(e->Style(f, Ctx(), &with).ok());
  size_t labels = 0, strokes = 0;
  for (const fv::StyleResult& r : with) {
    if (r.label.valid) ++labels;
    if (r.stroke.valid) ++strokes;
  }
  ASSERT_GT(labels, 0u) << "the nam label row should have fired";
  ASSERT_GT(strokes, 0u);

  e->viewing_groups().Set(21, false);  // IHO text group 21
  std::vector<fv::StyleResult> without;
  ASSERT_TRUE(e->Style(f, Ctx(), &without).ok());
  size_t labels2 = 0, strokes2 = 0;
  for (const fv::StyleResult& r : without) {
    if (r.label.valid) ++labels2;
    if (r.stroke.valid) ++strokes2;
  }
  EXPECT_EQ(0u, labels2);
  EXPECT_EQ(strokes, strokes2) << "symbology must be untouched";
}

TEST(GeoSymRules, UserRuleFileHidesAndReprioritizes) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_TRUE(e);

  std::string err;
  ASSERT_TRUE(e->rules()
                  .LoadText(
                      "hide key=BE010 geom=line\n"
                      "set  key=BH140 priority=42\n",
                      &err)
                  .ok())
      << err;

  std::vector<fv::StyleResult> hidden;
  ASSERT_TRUE(e->Style(DepthCurve(), Ctx(), &hidden).ok());
  EXPECT_TRUE(hidden.empty());

  // The geometry selector means the AREA rows of the same FACC still draw.
  std::vector<fv::StyleResult> areas;
  ASSERT_TRUE(e->Style(DepthArea(), Ctx(), &areas).ok());
  EXPECT_FALSE(areas.empty());

  std::vector<fv::StyleResult> bumped;
  ASSERT_TRUE(e->Style(LineFeature("BH140"), Ctx(), &bumped).ok());
  ASSERT_FALSE(bumped.empty());
  for (const fv::StyleResult& r : bumped) EXPECT_EQ(42, r.priority);
}

// Scale-banded thinning: the same rule set behaves differently at two
// viewport scales, and the switch costs one recompile, not a per-feature test.
TEST(GeoSymRules, ScaleBandedRuleThinsWhenZoomedOut) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_TRUE(e);
  e->SetDrawLabels(true);
  std::string err;
  ASSERT_TRUE(e->rules().LoadText("set labels=off scale=250000..\n", &err).ok())
      << err;

  const fv::VectorFeature f = LabelledBridge();
  auto label_count = [&](double denom) {
    fv::StyleContext c = Ctx();
    c.scale_denominator = denom;
    std::vector<fv::StyleResult> out;
    EXPECT_TRUE(e->Style(f, c, &out).ok());
    size_t n = 0;
    for (const fv::StyleResult& r : out)
      if (r.label.valid) ++n;
    return n;
  };
  EXPECT_GT(label_count(50000.0), 0u);
  EXPECT_EQ(0u, label_count(1000000.0));
  EXPECT_GT(label_count(50000.0), 0u) << "and back again";
}

// The plan's headline claim, measured on the real harbor render: a key-only
// rule set costs ZERO predicate evaluations across thousands of features.
TEST(GeoSymRules, KeyOnlyRulesCostNoPredicatesOverRealDnc) {
  SKIP_WITHOUT_ASSETS();
  const std::string lib = HarborLibrary();
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data";

  auto source = std::make_shared<fv::VpfVectorSource>();
  ASSERT_TRUE(source->Open(lib).ok());
  auto style = std::make_shared<fv::GeoSymStyleEngine>();
  ASSERT_TRUE(style->Open(TestDataDir(), fv::kGeoSymDnc).ok());
  std::string err;
  ASSERT_TRUE(style->rules()
                  .LoadText("hide key=BE010\nshow key=BH140\n", &err)
                  .ok())
      << err;

  fv::MapProjection proj;
  proj.SetSurfaceSize(256, 256);
  proj.SetCenter(fv::GeoPoint{41.70, -69.90});
  proj.SetResolution(0.0008, 0.0008);
  fv::CpuCanvas canvas(256, 256);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});
  fv::VectorRenderer r(source, style);
  ASSERT_TRUE(r.Render(proj, &canvas).ok());

  EXPECT_GT(r.features_queried(), 100u);
  EXPECT_EQ(0u, style->rule_predicate_evaluations());
}

// The same render with an ATTRIBUTE rule: predicates are evaluated, but only
// for the keys the rule actually names.
TEST(GeoSymRules, AttributeRulesEvaluateOnlyForTheirOwnKeys) {
  SKIP_WITHOUT_ASSETS();
  const std::string lib = HarborLibrary();
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data";

  auto source = std::make_shared<fv::VpfVectorSource>();
  ASSERT_TRUE(source->Open(lib).ok());
  auto style = std::make_shared<fv::GeoSymStyleEngine>();
  ASSERT_TRUE(style->Open(TestDataDir(), fv::kGeoSymDnc).ok());
  std::string err;
  ASSERT_TRUE(
      style->rules().LoadText("hide key=BE010 where cvl > 100\n", &err).ok())
      << err;

  fv::MapProjection proj;
  proj.SetSurfaceSize(256, 256);
  proj.SetCenter(fv::GeoPoint{41.70, -69.90});
  proj.SetResolution(0.0008, 0.0008);
  fv::CpuCanvas canvas(256, 256);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});
  fv::VectorRenderer r(source, style);
  ASSERT_TRUE(r.Render(proj, &canvas).ok());

  const size_t evals = style->rule_predicate_evaluations();
  EXPECT_GT(evals, 0u);
  EXPECT_LT(evals, r.features_queried())
      << "only BE010 features should have been tested, not every feature";
}

}  // namespace

