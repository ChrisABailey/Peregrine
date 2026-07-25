// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

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
  EXPECT_TRUE(ln[0].stroke.valid);
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

TEST(GeoSymStyle, UnknownSymbolIdIsNullAndIsNotRetried) {
  SKIP_WITHOUT_ASSETS();
  auto e = OpenDnc();
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->Symbol("9999"), nullptr);
  EXPECT_EQ(e->Symbol("9999"), nullptr);
  EXPECT_EQ(e->Symbol(""), nullptr);
  EXPECT_EQ(e->symbols_loaded(), 0u);
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
// 0 = probe mode (prints hash, no assert).
constexpr uint64_t kHashHarbor = 0x71724acb5cb517aull;

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

}  // namespace
