// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::S52StyleEngine tests (ENC phase E3a) — the MIDDLE of the vector seam for
// ENC, plus the end-to-end Charleston render that is this phase's milestone,
// the ENC twin of V5b's Nantucket harbor golden.
//
// The golden pins an FNV-1a hash of the canvas and writes a PNG for eyeballing,
// the convention canvas_test.cpp set and every renderer golden since has kept.
// Labels are OFF in the golden scene for the same reason as GeoSym's: label
// glyphs come from the host font and would make the hash machine-dependent.

#include "fv_s52_style.h"

#include <gtest/gtest.h>
#include <png.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fv_enc_vector_source.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/vector/renderer.h"

namespace fs = std::filesystem;

namespace {

std::string EncRoot() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc";
  return fs::is_directory(p) ? p : std::string();
}

bool HaveSymbols() {
  const std::string r = EncRoot();
  return !r.empty() && fs::is_regular_file(r + "/chartsymbols.xml");
}

#define SKIP_WITHOUT_PRESLIB()                                  \
  const std::string enc_root = EncRoot();                       \
  if (!HaveSymbols()) GTEST_SKIP() << "no S-52 presentation library"

std::unique_ptr<fv::S52StyleEngine> OpenEngine(const std::string& root) {
  std::unique_ptr<fv::S52StyleEngine> e(new fv::S52StyleEngine);
  if (!e->Open(root).ok()) return nullptr;
  return e;
}

fv::VectorFeature MakeFeature(fv::VectorGeometryType type, const char* acronym) {
  fv::VectorFeature f;
  f.type = type;
  f.style_key = acronym;
  f.layer = acronym;
  f.parts.push_back({fv::GeoPoint{32.78, -79.93}, fv::GeoPoint{32.79, -79.92}});
  f.bounds = fv::GeoRect{{32.78, -79.93}, {32.79, -79.92}};
  f.ref.layer = 0;
  return f;
}

void SetAttr(fv::VectorFeature* f, const char* key, const char* value) {
  f->attributes.emplace_back(key, value);
}

fv::StyleContext Ctx() {
  fv::StyleContext c;
  c.scale_denominator = 12000.0;  // the cells' own compilation scale
  c.device_dpi = 96.0;
  c.symbol_scale = 1.0;
  return c;
}

bool SameColor(const fv::FvColor& a, unsigned char r, unsigned char g,
               unsigned char b) {
  return a.r == r && a.g == g && a.b == b;
}

// The one fill of the first result that has one.
bool FirstFill(const std::vector<fv::StyleResult>& rs, fv::FvColor* out) {
  for (const auto& r : rs)
    if (r.fill.valid) {
      *out = r.fill.brush.color;
      return true;
    }
  return false;
}

bool FirstStroke(const std::vector<fv::StyleResult>& rs, fv::Pen* out) {
  for (const auto& r : rs)
    if (r.stroke.valid) {
      *out = r.stroke.pen;
      return true;
    }
  return false;
}

std::string FirstSymbol(const std::vector<fv::StyleResult>& rs) {
  for (const auto& r : rs)
    if (r.symbol.valid) return r.symbol.symbol_id;
  return {};
}

uint64_t Fnv1a(const fv::PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width() * 4; ++x) {
      h ^= row[x];
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

size_t NonBackgroundPixels(const fv::PixelBuffer& b, unsigned char bg) {
  size_t n = 0;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* p = row + x * 4;
      if (p[0] != bg || p[1] != bg || p[2] != bg) ++n;
    }
  }
  return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lookup dispatch
// ---------------------------------------------------------------------------

TEST(S52Style, OpenLoadsThePresentationLibrary) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  EXPECT_TRUE(e->IsOpen());
  EXPECT_GT(e->library().lookups().size(), 3000u);
  EXPECT_EQ(e->color_scheme(), fv::S52ColorScheme::kDay);
}

TEST(S52Style, LandAreaFillsWithTheLibrarysOwnLandColour) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());

  fv::FvColor fill;
  ASSERT_TRUE(FirstFill(out, &fill));
  // LANDA in the delivered DAY_BRIGHT table. Pinned from the data, not chosen.
  EXPECT_TRUE(SameColor(fill, 201, 185, 122))
      << int(fill.r) << "," << int(fill.g) << "," << int(fill.b);
  // Skin of the earth draws under everything else.
  EXPECT_EQ(out.front().priority, fv::kS52PrioGroup1);
}

// The regression that mattered most while building this: '?' in a lookup row is
// S-57's UNKNOWN-VALUE marker, not a wildcard. DEPARE's first row is
// [DRVAL1=?][DRVAL2=?] -> AC(NODTA) ("unsurveyed"), so a wildcard reading
// swallows every depth area and paints the harbour no-data grey.
TEST(S52Style, UnknownValueConditionDoesNotMatchARealDepth) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature real = MakeFeature(fv::VectorGeometryType::kArea, "DEPARE");
  SetAttr(&real, "DRVAL1", "5.4");
  SetAttr(&real, "DRVAL2", "9.1");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(real, Ctx(), &out).ok());
  fv::FvColor fill;
  ASSERT_TRUE(FirstFill(out, &fill));
  // NODTA is 163,180,183; a surveyed area must NOT get it.
  EXPECT_FALSE(SameColor(fill, 163, 180, 183)) << "surveyed area drew as no-data";

  // ... and an area whose depths ARE unknown must get exactly that row.
  fv::VectorFeature unknown =
      MakeFeature(fv::VectorGeometryType::kArea, "DEPARE");
  SetAttr(&unknown, "DRVAL1", "");
  SetAttr(&unknown, "DRVAL2", "");
  out.clear();
  ASSERT_TRUE(e->Style(unknown, Ctx(), &out).ok());
  ASSERT_TRUE(FirstFill(out, &fill));
  EXPECT_TRUE(SameColor(fill, 163, 180, 183))
      << "unsurveyed area did not draw as no-data";
}

TEST(S52Style, MostSpecificLookupRowWins) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // A conical lateral buoy, green (COLOUR 4) then red (COLOUR 3). The two must
  // dispatch to DIFFERENT rows: the assertion is on the lookup, not on the
  // emitted symbol id, because several BOYLAT symbols are raster-only in the
  // delivered library and both would then come back as the same placeholder —
  // which is exactly the kind of false pass a symbol-name check would give.
  fv::VectorFeature green = MakeFeature(fv::VectorGeometryType::kPoint, "BOYLAT");
  SetAttr(&green, "BOYSHP", "1");
  SetAttr(&green, "COLOUR", "4");
  fv::VectorFeature red = MakeFeature(fv::VectorGeometryType::kPoint, "BOYLAT");
  SetAttr(&red, "BOYSHP", "1");
  SetAttr(&red, "COLOUR", "3");

  const fv::S52Lookup* green_row = e->SelectLookup(green);
  const fv::S52Lookup* red_row = e->SelectLookup(red);
  ASSERT_NE(green_row, nullptr);
  ASSERT_NE(red_row, nullptr);
  EXPECT_NE(green_row->id, red_row->id) << "colour did not select a different row";
  EXPECT_NE(green_row->instruction, red_row->instruction);

  // Both rows must be the SPECIFIC ones, not the object's catch-all: each
  // states the two conditions we set.
  EXPECT_EQ(green_row->attributes.size(), 2u);
  EXPECT_EQ(red_row->attributes.size(), 2u);

  // And a buoy with no attributes at all falls to a row with no conditions.
  fv::VectorFeature bare = MakeFeature(fv::VectorGeometryType::kPoint, "BOYLAT");
  const fv::S52Lookup* bare_row = e->SelectLookup(bare);
  ASSERT_NE(bare_row, nullptr);
  EXPECT_TRUE(bare_row->attributes.empty());
}

TEST(S52Style, ListValuedAttributeConditionsMatchExactly) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // COLOUR="4,3" (a banded buoy) is authored as its own row, and the list is
  // compared as a whole string: the single-colour row must not capture it, and
  // the order within the list is significant.
  fv::VectorFeature banded =
      MakeFeature(fv::VectorGeometryType::kPoint, "BOYLAT");
  SetAttr(&banded, "BOYSHP", "1");
  SetAttr(&banded, "COLOUR", "4,3");
  fv::VectorFeature plain = MakeFeature(fv::VectorGeometryType::kPoint, "BOYLAT");
  SetAttr(&plain, "BOYSHP", "1");
  SetAttr(&plain, "COLOUR", "4");

  const fv::S52Lookup* banded_row = e->SelectLookup(banded);
  const fv::S52Lookup* plain_row = e->SelectLookup(plain);
  ASSERT_NE(banded_row, nullptr);
  ASSERT_NE(plain_row, nullptr);
  EXPECT_NE(banded_row->id, plain_row->id);
  for (const auto& cond : banded_row->attributes)
    if (cond.acronym == "COLOUR") EXPECT_EQ(cond.value, "4,3");
}

// ---------------------------------------------------------------------------
// Conditional symbology
// ---------------------------------------------------------------------------

TEST(S52Style, DepthRampFollowsTheMarinersContours) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  auto shade = [&](double d1, double d2) {
    fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "DEPARE");
    SetAttr(&f, "DRVAL1", std::to_string(d1).c_str());
    SetAttr(&f, "DRVAL2", std::to_string(d2).c_str());
    std::vector<fv::StyleResult> out;
    EXPECT_TRUE(e->Style(f, Ctx(), &out).ok());
    fv::FvColor c{};
    EXPECT_TRUE(FirstFill(out, &c));
    return c;
  };

  // Defaults: shallow 2 m, safety 30 m, deep 30 m. Four distinct shades, and
  // they must get LIGHTER as the water gets deeper — that is the whole point
  // of the ramp, and it is checked as an ordering, not as four literals.
  const fv::FvColor intertidal = shade(-2.0, -1.0);
  const fv::FvColor very_shallow = shade(0.0, 1.8);
  const fv::FvColor medium = shade(3.6, 9.1);
  const fv::FvColor deep = shade(40.0, 50.0);
  EXPECT_TRUE(SameColor(intertidal, 131, 178, 149));  // DEPIT
  EXPECT_TRUE(SameColor(very_shallow, 115, 182, 239));  // DEPVS
  EXPECT_TRUE(SameColor(medium, 152, 197, 242));        // DEPMS
  EXPECT_TRUE(SameColor(deep, 212, 234, 238));          // DEPDW
  EXPECT_LT(very_shallow.r, medium.r);
  EXPECT_LT(medium.r, deep.r);

  // Raising the safety contour moves the boundary: 9.1 m water is no longer
  // "medium", it falls back into the shallow shade.
  e->mutable_mariner().safety_contour = 12.0;
  e->mutable_mariner().deep_contour = 12.0;
  const fv::FvColor was_medium = shade(3.6, 9.1);
  EXPECT_TRUE(SameColor(was_medium, 152, 197, 242));
  const fv::FvColor now_deep = shade(15.0, 20.0);
  EXPECT_TRUE(SameColor(now_deep, 212, 234, 238));
}

TEST(S52Style, TwoShadeModeCollapsesTheRamp) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->mutable_mariner().two_shades = true;

  auto shade = [&](double d1, double d2) {
    fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "DEPARE");
    SetAttr(&f, "DRVAL1", std::to_string(d1).c_str());
    SetAttr(&f, "DRVAL2", std::to_string(d2).c_str());
    std::vector<fv::StyleResult> out;
    EXPECT_TRUE(e->Style(f, Ctx(), &out).ok());
    fv::FvColor c{};
    EXPECT_TRUE(FirstFill(out, &c));
    return c;
  };
  // Everything shoaler than the safety contour is one shade, everything
  // deeper is the other — no medium bands.
  EXPECT_TRUE(SameColor(shade(3.6, 9.1), 115, 182, 239));   // DEPVS
  EXPECT_TRUE(SameColor(shade(40.0, 50.0), 212, 234, 238));  // DEPDW
}

TEST(S52Style, SafetyContourIsDrawnBolderThanTheOthers) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->mutable_mariner().safety_contour = 10.0;

  fv::VectorFeature safety = MakeFeature(fv::VectorGeometryType::kLine, "DEPCNT");
  SetAttr(&safety, "VALDCO", "10");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(safety, Ctx(), &out).ok());
  fv::Pen bold{};
  ASSERT_TRUE(FirstStroke(out, &bold));

  fv::VectorFeature other = MakeFeature(fv::VectorGeometryType::kLine, "DEPCNT");
  SetAttr(&other, "VALDCO", "5");
  out.clear();
  ASSERT_TRUE(e->Style(other, Ctx(), &out).ok());
  fv::Pen thin{};
  ASSERT_TRUE(FirstStroke(out, &thin));

  EXPECT_GT(bold.width, thin.width);
  EXPECT_TRUE(SameColor(bold.color, 82, 90, 92));    // DEPSC
  EXPECT_TRUE(SameColor(thin.color, 125, 137, 140));  // DEPCN
}

TEST(S52Style, LabelsAreOffByDefault) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  SetAttr(&f, "OBJNAM", "Sullivans Island");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  for (const auto& r : out) EXPECT_FALSE(r.label.valid);

  e->SetDrawLabels(true);
  out.clear();
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  bool labelled = false;
  for (const auto& r : out)
    if (r.label.valid) {
      labelled = true;
      EXPECT_EQ(r.label.text, "Sullivans Island");
    }
  EXPECT_TRUE(labelled);
}

// S-52 gives the LOOKUP a display priority and the whole instruction chain
// inherits it, text included — and the delivered library puts text-bearing rows
// at every priority there is. LNDARE's own row is the proof: `AC(LANDA);
// TX(OBJNAM,...)` at Group 1, so the land name drew at priority 1 and every
// band above it painted over the name. Text now goes in a band of its own.
TEST(S52Style, TextDrawsAboveEveryGeometryBand) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->SetDrawLabels(true);

  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  SetAttr(&f, "OBJNAM", "Sullivans Island");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());

  int label_priority = -1;
  int max_geometry_priority = -1;
  for (const auto& r : out) {
    if (r.label.valid) {
      label_priority = r.priority;
      // The label is a pass of its OWN: lifting it out of the object's band
      // means it cannot ride along on the fill's result.
      EXPECT_FALSE(r.fill.valid);
      EXPECT_FALSE(r.stroke.valid);
      EXPECT_FALSE(r.symbol.valid);
    } else {
      max_geometry_priority = (std::max)(max_geometry_priority, r.priority);
    }
  }
  ASSERT_GE(label_priority, 0);
  ASSERT_GE(max_geometry_priority, 0);
  // The land fill is Group 1, which is exactly the band that used to bury it.
  EXPECT_EQ(max_geometry_priority, fv::kS52PrioGroup1);
  EXPECT_EQ(label_priority, fv::kS52PrioTextBase + fv::kS52PrioGroup1);
  EXPECT_GT(label_priority, max_geometry_priority);
}

// Adding the object's own priority rather than flattening every label onto one
// number keeps the library's relative order among labels: a buoy's name (its
// row is Hazards) still sits over a land area's name (Group 1).
TEST(S52Style, TextKeepsTheLibrarysRelativeOrderAmongLabels) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->SetDrawLabels(true);

  fv::VectorFeature land = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  SetAttr(&land, "OBJNAM", "Sullivans Island");
  fv::VectorFeature buoy = MakeFeature(fv::VectorGeometryType::kPoint, "BOYLAT");
  SetAttr(&buoy, "BOYSHP", "2");
  SetAttr(&buoy, "OBJNAM", "R \"6\"");

  int land_text = -1, buoy_text = -1;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(land, Ctx(), &out).ok());
  for (const auto& r : out)
    if (r.label.valid) land_text = r.priority;
  out.clear();
  ASSERT_TRUE(e->Style(buoy, Ctx(), &out).ok());
  for (const auto& r : out)
    if (r.label.valid) buoy_text = r.priority;

  ASSERT_GE(land_text, 0);
  ASSERT_GE(buoy_text, 0);
  EXPECT_EQ(land_text, fv::kS52PrioTextBase + fv::kS52PrioGroup1);
  EXPECT_EQ(buoy_text, fv::kS52PrioTextBase + fv::kS52PrioHazards);
  EXPECT_GT(buoy_text, land_text);
}

// TX/TE carry HJUST, VJUST, XOFFS and YOFFS for every string in the library and
// all four were being skipped, so every ENC label drew baseline-left exactly on
// its anchor — a name sitting on the symbol it belongs beside.
//
// The decode is the library's own, read off rows that can only mean one thing:
// SEAARE labels an area with (1,2) and no offset, so 1 and 2 are "centre";
// BOYLAT offsets LEFT with HJUST 2, so 2 is right-justified.
TEST(S52Style, TextJustificationAndOffsetComeFromTheInstruction) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->SetDrawLabels(true);

  // LNDARE area: TX(OBJNAM,1,2,3,'15118',-1,-1,CHBLK,26) — centred both ways,
  // one body size left and one UP (y is positive downward), body size 18.
  fv::VectorFeature land = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  SetAttr(&land, "OBJNAM", "Sullivans Island");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(land, Ctx(), &out).ok());
  const fv::LabelStyle* lb = nullptr;
  for (const auto& r : out)
    if (r.label.valid) lb = &r.label;
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->halign, fv::LabelHAlign::kCenter);
  EXPECT_EQ(lb->valign, fv::LabelVAlign::kCenter);
  EXPECT_DOUBLE_EQ(lb->style.size, 18.0);
  EXPECT_EQ(lb->dx, -18);
  EXPECT_EQ(lb->dy, -18);

  // BOYLAT point: TE('%s','OBJNAM',2,1,2,'15110',-1,-1,CHBLK,21) — right
  // justified and bottom aligned, so a name offset left of the buoy ends up
  // wholly clear of it. Body size 10, so the offsets are 10 px, not 18.
  fv::VectorFeature buoy = MakeFeature(fv::VectorGeometryType::kPoint, "BOYLAT");
  SetAttr(&buoy, "BOYSHP", "2");
  SetAttr(&buoy, "OBJNAM", "R \"6\"");
  out.clear();
  ASSERT_TRUE(e->Style(buoy, Ctx(), &out).ok());
  lb = nullptr;
  for (const auto& r : out)
    if (r.label.valid) lb = &r.label;
  ASSERT_NE(lb, nullptr);
  EXPECT_EQ(lb->halign, fv::LabelHAlign::kRight);
  EXPECT_EQ(lb->valign, fv::LabelVAlign::kBottom);
  EXPECT_DOUBLE_EQ(lb->style.size, 10.0);
  EXPECT_EQ(lb->dx, -10);
  EXPECT_EQ(lb->dy, -10);
}

// The offsets are stated in BODY SIZES, so magnifying the symbology has to move
// the text as far as it grew the symbol it is standing off from.
TEST(S52Style, TextOffsetsScaleWithTheSymbology) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->SetDrawLabels(true);

  fv::VectorFeature land = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  SetAttr(&land, "OBJNAM", "Sullivans Island");
  fv::StyleContext c = Ctx();
  c.symbol_scale = 2.0;
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(land, c, &out).ok());
  const fv::LabelStyle* lb = nullptr;
  for (const auto& r : out)
    if (r.label.valid) lb = &r.label;
  ASSERT_NE(lb, nullptr);
  EXPECT_DOUBLE_EQ(lb->style.size, 36.0);
  EXPECT_EQ(lb->dx, -36);
  EXPECT_EQ(lb->dy, -36);
}

// A rule that names a priority is an explicit instruction about where this
// object goes. Obey it literally: lifting half the object into the text band
// anyway would make the override mean something the caller did not write.
TEST(S52Style, ARulePriorityOverrideKeepsTextWithItsGeometry) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->SetDrawLabels(true);
  const fv::Status s = e->rules().LoadText("set key=LNDARE priority=3\n");
  ASSERT_TRUE(s.ok()) << s.message;

  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  SetAttr(&f, "OBJNAM", "Sullivans Island");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());
  bool saw_label = false;
  for (const auto& r : out) {
    EXPECT_EQ(r.priority, 3);
    if (r.label.valid) saw_label = true;
  }
  EXPECT_TRUE(saw_label);
}

// Plan §7: never let a feature vanish because the symbology is not written
// yet. An unimplemented CS procedure is counted AND draws the library's own
// question mark — but only when the row put no other ink down.
TEST(S52Style, UnimplementedCsProcedureIsCountedAndPlaceheld) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // E3b implemented every procedure the Charleston cells reach, so the
  // mechanism is exercised through a row that still asks for an unimplemented
  // one: NEWOBJ (the mariner's own inserted object) is CS(SYMINS01) and
  // nothing else, in every table. The remaining unimplemented procedures are
  // all mariner-object ones — SYMINS, OWNSHP, VESSEL, PASTRK, LEGLIN, VRMEBL,
  // CLRLIN — which no ENC cell can contain.
  // NEWOBJ's first row is conditional on the SYMINS attribute being present,
  // so the feature has to carry it to reach CS(SYMINS01); without it the row
  // that matches is the plain SY(NEWOBJ01) one.
  fv::VectorFeature inserted =
      MakeFeature(fv::VectorGeometryType::kPoint, "NEWOBJ");
  SetAttr(&inserted, "SYMINS", "SY(BOYSPP21)");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(inserted, Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());
  EXPECT_EQ(FirstSymbol(out), "QUESMRK1");
  EXPECT_EQ(e->unhandled_cs().count("SYMINS01"), 1u);
  EXPECT_GE(e->placeholders_drawn(), 1u);
}

TEST(S52Style, PlaceholderIsDrawnOnlyWhenTheRowPutNoInkDown) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // The same object class, one attribute apart. WITH SYMINS the row is
  // CS(SYMINS01) alone, so nothing draws and the question mark is right;
  // WITHOUT it the row is SY(NEWOBJ01), which draws, asks for no procedure,
  // and must not be overstamped.
  //
  // E3a asserted this with ACHARE/RESARE02, and E3b implementing RESARE02
  // exposed something worth recording: after E3b there is NO reachable row in
  // the delivered library that both draws AND calls an unimplemented
  // procedure, so the overdraw half of the guard can no longer be provoked
  // from the data. The corpus sweep still asserts it across all 4,470
  // features (no symbolized feature carries QUESMRK1); this pins the
  // condition itself.
  fv::VectorFeature inserted =
      MakeFeature(fv::VectorGeometryType::kPoint, "NEWOBJ");
  SetAttr(&inserted, "SYMINS", "SY(BOYSPP21)");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(inserted, Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());
  EXPECT_EQ(FirstSymbol(out), "QUESMRK1");
  EXPECT_GE(e->placeholders_drawn(), 1u);

  // The other side of the guard: a row that draws takes no placeholder. LNDARE
  // as an area is the cleanest case in the library — a plain AC fill, no CS,
  // no symbol at all. (NEWOBJ's own no-SYMINS row is NOT usable here: it names
  // SY(NEWOBJ01), which is one of the raster-only definitions, so it draws a
  // question mark through the unresolved-symbol path rather than the CS one.)
  e->ResetDiagnostics();
  fv::VectorFeature land =
      MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  out.clear();
  ASSERT_TRUE(e->Style(land, Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());
  EXPECT_EQ(e->placeholders_drawn(), 0u);
  for (const auto& r : out)
    EXPECT_NE(r.symbol.symbol_id, "QUESMRK1")
        << "a symbolized feature was overdrawn with the unknown mark";
}

// A finding, pinned so it cannot quietly change: a block of lookup rows in the
// delivered chartsymbols.xml is named in LOWER CASE — depare, excnst, topmar,
// and the mariner objects ownshp/vessel/clrlin/ebline/leglin/pastrk/vrmark.
// S-57 object acronyms are uppercase by definition and this engine matches
// them exactly, so those rows are UNREACHABLE from ENC data. That is the
// intended outcome for the mariner objects (they are OpenCPN's own overlay
// classes, not chart features) and it is also why CS(DEPARE02) and
// CS(TOPMARI1) never appear in unhandled_cs(). Matching case-insensitively
// would make an alternate `depare` row selectable ahead of the real ones and
// move the whole depth ramp, which is not a change E3b should make.
TEST(S52Style, LowerCaseLookupNamesAreUnreachableAndThatIsDeliberate) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  EXPECT_FALSE(e->library().Lookups(fv::S52LookupTable::kPlainBoundaries,
                                    "depare").empty())
      << "the lower-case rows are gone; re-derive this expectation";
  EXPECT_TRUE(e->library().Lookups(fv::S52LookupTable::kPlainBoundaries,
                                   "DEPARE").size() > 1u);

  // A DEPARE area still dispatches to a real uppercase row, not the lower-case
  // CS(DEPARE02) one.
  fv::VectorFeature depth =
      MakeFeature(fv::VectorGeometryType::kArea, "DEPARE");
  SetAttr(&depth, "DRVAL1", "5");
  SetAttr(&depth, "DRVAL2", "10");
  const fv::S52Lookup* row = e->SelectLookup(depth);
  ASSERT_NE(row, nullptr);
  bool depare02 = false;
  for (const auto& i : row->instructions)
    if (i.op == "CS" && !i.params.empty() && i.params[0] == "DEPARE02")
      depare02 = true;
  EXPECT_FALSE(depare02);
}

// ---------------------------------------------------------------------------
// The seven CS procedures E3b added. Each pins the decision the procedure
// exists to make, against symbol names grounded in the library's own
// descriptions (LIGHTS11 "light flare, red" and so on) rather than recalled
// from the spec.
// ---------------------------------------------------------------------------

// Every symbol id in the results, in order.
std::vector<std::string> AllSymbols(const std::vector<fv::StyleResult>& rs) {
  std::vector<std::string> out;
  for (const auto& r : rs)
    if (r.symbol.valid) out.push_back(r.symbol.symbol_id);
  return out;
}

bool HasSymbol(const std::vector<fv::StyleResult>& rs, const char* id) {
  for (const auto& s : AllSymbols(rs))
    if (s == id) return true;
  return false;
}

std::string LightSymbol(fv::S52StyleEngine* e, const char* colour) {
  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kPoint, "LIGHTS");
  if (colour != nullptr) SetAttr(&f, "COLOUR", colour);
  std::vector<fv::StyleResult> out;
  EXPECT_TRUE(e->Style(f, Ctx(), &out).ok());
  return FirstSymbol(out);
}

TEST(S52Style, LightFlareFollowsTheColourAttribute) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // S-57 COLOUR: 3 red, 4 green, 1 white, 6 yellow, 12 magenta.
  EXPECT_EQ(LightSymbol(e.get(), "3"), "LIGHTS11");   // red
  EXPECT_EQ(LightSymbol(e.get(), "4"), "LIGHTS12");   // green
  EXPECT_EQ(LightSymbol(e.get(), "1"), "LIGHTS13");   // white
  EXPECT_EQ(LightSymbol(e.get(), "6"), "LIGHTS13");   // yellow
  EXPECT_EQ(LightSymbol(e.get(), "12"), "LIGHTS14");  // magenta
  EXPECT_EQ(LightSymbol(e.get(), nullptr), "LITDEF11");
  // A list-valued COLOUR takes red over the rest, the spec's precedence.
  EXPECT_EQ(LightSymbol(e.get(), "1,3"), "LIGHTS11");
}

TEST(S52Style, TheLightFlareLeansSouthEastInsteadOfStandingOnItsLight) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kPoint, "LIGHTS");
  SetAttr(&f, "COLOUR", "1");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());

  // The HPGL flare rises straight up from its pivot, so unrotated it grew out
  // of the top of the buoy it belongs to. 135 degrees of BEARING lays it down
  // and to the right, which is where the delivered library's own RASTER copy
  // of the same symbol puts it (its bitmap pivot is the tile corner and the
  // ink centroid bears 133.5 from it).
  //
  // NEGATIVE because the renderer's rotation is CCGMSymbol::DrawSymbol's — see
  // the SY handler: positive turns a symbol COUNTER-clockwise on screen, so a
  // compass bearing is negated at this seam and nowhere else.
  bool checked = false;
  for (const auto& r : out)
    if (r.symbol.valid && r.symbol.symbol_id == "LIGHTS13") {
      EXPECT_DOUBLE_EQ(r.symbol.rotation_deg, -135.0);
      checked = true;
    }
  EXPECT_TRUE(checked);
}

TEST(S52Style, SymbolsAreSizedOnTheLibrarysOwnGridNotGeoSyms) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // 0.32 mm, the presentation library's nominal pixel. Not a preference: it is
  // what makes a display list come out the same size as the TILE of the same
  // symbol, and the delivered file states both boxes for 316 symbols so the
  // claim is checkable — which is what the loop below does.
  EXPECT_DOUBLE_EQ(e->himetric_per_symbol_pixel(), 32.0);

  // A symbol authored BOTH ways states its own grid twice: the <vector> box in
  // 0.01 mm and the <bitmap> box in tile pixels. Divide the first by the grid
  // and the tile comes back, which is the whole claim — the two forms of one
  // symbol now draw the same size. Half a pixel of slack, because a tile
  // rounds to whole pixels.
  //
  // These four are ordinary navaids with square-ish tiles big enough that the
  // rounding does not dominate; the same holds across all 316 dual-form
  // symbols, whose median ratio is 32.11.
  for (const char* name :
       {"ACHBRT07", "BOYCAR04", "BUAARE02", "DNGHILIT"}) {
    const fv::S52SymbolDef* def = e->library().SymbolDef(name);
    ASSERT_NE(def, nullptr) << name;
    ASSERT_TRUE(def->has_bitmap) << name;
    ASSERT_GT(def->vector_width, 0) << name;
    const double px_per_unit = 1.0 / e->himetric_per_symbol_pixel();
    EXPECT_NEAR(def->vector_width * px_per_unit, def->bitmap_width, 0.5)
        << name;
    EXPECT_NEAR(def->vector_height * px_per_unit, def->bitmap_height, 0.5)
        << name;
  }
}

// A sounding is a ROW OF SYMBOLS, one per digit — `SOUND` + family + position
// + digit — and the position is what puts the decimetre a half-line low. The
// two families are the safety-depth split: SOUNDS* black for at-or-shallower,
// SOUNDG* grey for deeper.
std::vector<std::string> SoundingSymbols(fv::S52StyleEngine* e,
                                         const char* depth) {
  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kPoint, "SOUNDG");
  SetAttr(&f, "DEPTH", depth);
  std::vector<fv::StyleResult> out;
  EXPECT_TRUE(e->Style(f, Ctx(), &out).ok());
  return AllSymbols(out);
}

TEST(S52Style, SoundingsSplitAtTheMarinersSafetyDepth) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->mutable_mariner().safety_depth = 10.0;

  // 4.5 m is at or above the safety depth: the BLACK family.
  EXPECT_EQ(SoundingSymbols(e.get(), "4.5"),
            (std::vector<std::string>{"SOUNDS14", "SOUNDS55"}));
  // 40 m is deeper: the same layout in the GREY family.
  EXPECT_EQ(SoundingSymbols(e.get(), "40.0"),
            (std::vector<std::string>{"SOUNDG24", "SOUNDG10"}));

  // Moving the safety depth moves the split and nothing else.
  e->mutable_mariner().safety_depth = 50.0;
  EXPECT_EQ(SoundingSymbols(e.get(), "40.0"),
            (std::vector<std::string>{"SOUNDS24", "SOUNDS10"}));
}

TEST(S52Style, SoundingDigitsCarryTheDecimetreAsASubscript) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->mutable_mariner().safety_depth = 1000.0;  // one family, so this reads as layout

  // Position 1 is the units digit and position 5 is the subscript slot — the
  // delivered BITMAP PIVOTS say so: 5 puts the tile at position 0's x with the
  // pivot 4 px higher, i.e. the same column dropped half a line. Composing the
  // tiles by those pivots draws "9<sub>4</sub>", which is how this was read
  // off the data rather than out of the spec.
  EXPECT_EQ(SoundingSymbols(e.get(), "9.4"),
            (std::vector<std::string>{"SOUNDS19", "SOUNDS54"}));
  // Two whole digits: position 2 is the tens, right-to-left ending at 1.
  EXPECT_EQ(SoundingSymbols(e.get(), "12.6"),
            (std::vector<std::string>{"SOUNDS21", "SOUNDS12", "SOUNDS56"}));
  // 31 m and deeper is whole metres — no subscript, by S-52's own rule.
  EXPECT_EQ(SoundingSymbols(e.get(), "31.4"),
            (std::vector<std::string>{"SOUNDS23", "SOUNDS11"}));
  EXPECT_EQ(SoundingSymbols(e.get(), "127.0"),
            (std::vector<std::string>{"SOUNDS31", "SOUNDS22", "SOUNDS17"}));
  // A whole number under 31 has nothing to subscript.
  EXPECT_EQ(SoundingSymbols(e.get(), "5.0"),
            (std::vector<std::string>{"SOUNDS15"}));
  // A drying height is authored negative; its magnitude prints (the bar that
  // says "drying" is the one piece of SNDFRM02 still missing).
  EXPECT_EQ(SoundingSymbols(e.get(), "-1.2"),
            (std::vector<std::string>{"SOUNDS11", "SOUNDS52"}));
}

TEST(S52Style, FloodAndStripLightsTakeTheirOwnSymbolNotAFlare) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature flood = MakeFeature(fv::VectorGeometryType::kPoint, "LIGHTS");
  SetAttr(&flood, "CATLIT", "8");
  SetAttr(&flood, "COLOUR", "3");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(flood, Ctx(), &out).ok());
  EXPECT_EQ(FirstSymbol(out), "LIGHTS82");  // "floodlight"

  fv::VectorFeature strip = MakeFeature(fv::VectorGeometryType::kPoint, "LIGHTS");
  SetAttr(&strip, "CATLIT", "11");
  out.clear();
  ASSERT_TRUE(e->Style(strip, Ctx(), &out).ok());
  EXPECT_EQ(FirstSymbol(out), "LIGHTS81");  // "strip light"
}

TEST(S52Style, SectorLightIsDrawnAsAFlareAndTheDeviationIsCounted) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature sector = MakeFeature(fv::VectorGeometryType::kPoint, "LIGHTS");
  SetAttr(&sector, "COLOUR", "3");
  SetAttr(&sector, "SECTR1", "045");
  SetAttr(&sector, "SECTR2", "135");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(sector, Ctx(), &out).ok());
  // It still draws — a sector light must never vanish — but the legs and arc
  // the spec draws are not expressible as an instruction string, so the
  // simplification is counted instead of forgotten.
  EXPECT_EQ(FirstSymbol(out), "LIGHTS11");
  EXPECT_EQ(e->sector_lights_simplified(), 1u);
}

TEST(S52Style, ObstructionDangerFollowsTheMarinersSafetyContour) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  e->mutable_mariner().safety_contour = 10.0;

  // 5 m under a 10 m safety contour is a danger; 15 m is not; over 20 m takes
  // the deep-hazard mark. This is the whole point of the procedure, and it
  // must move when the MARINER moves the contour.
  fv::VectorFeature shoal = MakeFeature(fv::VectorGeometryType::kPoint, "OBSTRN");
  SetAttr(&shoal, "VALSOU", "5.0");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(shoal, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "ISODGR01")) << FirstSymbol(out);

  fv::VectorFeature deeper = MakeFeature(fv::VectorGeometryType::kPoint, "OBSTRN");
  SetAttr(&deeper, "VALSOU", "15.0");
  out.clear();
  ASSERT_TRUE(e->Style(deeper, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "DANGER01")) << FirstSymbol(out);

  fv::VectorFeature deep = MakeFeature(fv::VectorGeometryType::kPoint, "OBSTRN");
  SetAttr(&deep, "VALSOU", "35.0");
  out.clear();
  ASSERT_TRUE(e->Style(deep, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "DANGER02")) << FirstSymbol(out);

  // Raising the contour past the sounding turns the same feature into a danger.
  e->mutable_mariner().safety_contour = 40.0;
  out.clear();
  ASSERT_TRUE(e->Style(deep, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "ISODGR01"));
}

TEST(S52Style, ObstructionWithoutADepthFallsBackToWatlev) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature dry = MakeFeature(fv::VectorGeometryType::kPoint, "OBSTRN");
  SetAttr(&dry, "WATLEV", "2");  // always dry
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(dry, Ctx(), &out).ok());
  // OBSTRN18, not OBSTRN11: same description, and 11 is raster-only.
  EXPECT_TRUE(HasSymbol(out, "OBSTRN18"));

  fv::VectorFeature covers = MakeFeature(fv::VectorGeometryType::kPoint, "OBSTRN");
  SetAttr(&covers, "WATLEV", "4");  // covers and uncovers
  out.clear();
  ASSERT_TRUE(e->Style(covers, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "OBSTRN03"));

  fv::VectorFeature unknown = MakeFeature(fv::VectorGeometryType::kPoint, "OBSTRN");
  out.clear();
  ASSERT_TRUE(e->Style(unknown, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "OBSTRN01"));  // "depth not stated"
}

TEST(S52Style, WreckCategorySelectsTheWreckSymbol) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature hull = MakeFeature(fv::VectorGeometryType::kPoint, "WRECKS");
  SetAttr(&hull, "CATWRK", "4");  // hull showing
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(hull, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "WRECKS01"));

  fv::VectorFeature safe = MakeFeature(fv::VectorGeometryType::kPoint, "WRECKS");
  SetAttr(&safe, "CATWRK", "1");  // non-dangerous
  out.clear();
  ASSERT_TRUE(e->Style(safe, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "WRECKS04"));

  // An unstated category is DANGEROUS, not safe.
  fv::VectorFeature unstated = MakeFeature(fv::VectorGeometryType::kPoint, "WRECKS");
  out.clear();
  ASSERT_TRUE(e->Style(unstated, Ctx(), &out).ok());
  EXPECT_TRUE(HasSymbol(out, "WRECKS05"));

  // QUASOU 7 = least depth unknown: the library's over-line goes on top, so
  // two symbols, not one. WRECKS07 is RASTER-ONLY, and as of E6 that is a
  // symbol like any other — it goes out under its own name and the renderer
  // resolves it through Pixmap(). Before E6 this row reached the canvas as the
  // question mark, which is the defect E6 fixed.
  fv::VectorFeature unsurveyed = MakeFeature(fv::VectorGeometryType::kPoint, "WRECKS");
  SetAttr(&unsurveyed, "QUASOU", "7");
  out.clear();
  ASSERT_TRUE(e->Style(unsurveyed, Ctx(), &out).ok());
  EXPECT_EQ(AllSymbols(out).size(), 2u);
  EXPECT_TRUE(HasSymbol(out, "WRECKS05"));
  EXPECT_TRUE(HasSymbol(out, "WRECKS07"));
  EXPECT_FALSE(HasSymbol(out, "QUESMRK1"));
  EXPECT_EQ(e->Symbol("WRECKS07"), nullptr) << "WRECKS07 has no HPGL";
  EXPECT_NE(e->Pixmap("WRECKS07"), nullptr) << "...but it has a tile";
}

TEST(S52Style, TopmarkShapeSelectsTheTopmarkSymbol) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  struct Case { const char* topshp; const char* symbol; };
  // TOPSHP -> the BUOY family, per the library's own descriptions. Only the
  // shapes whose lookup row is CS(TOPMAR01) are listed: the delivered table
  // symbolizes many others (TOPSHP 6,7,8,15..32) with a direct SY row and
  // never calls the procedure for them.
  const Case cases[] = {
      {"1", "TOPMAR02"},   // cone point up
      {"2", "TOPMAR04"},   // cone point down
      {"3", "TOPMAR10"},   // sphere
      {"10", "TOPMAR08"},  // 2 cones point to point
      {"11", "TOPMAR07"},  // 2 cones base to base
      // TOPMAR01 ("not defined") is the procedure's own fallback for a shape it
      // does not name. It is RASTER-ONLY, and since E6 that no longer means
      // "question mark": it draws its own tile out of the symbol sheet.
      {"33", "TOPMAR01"},
  };
  for (const Case& c : cases) {
    fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kPoint, "TOPMAR");
    SetAttr(&f, "TOPSHP", c.topshp);
    std::vector<fv::StyleResult> out;
    ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
    EXPECT_TRUE(HasSymbol(out, c.symbol))
        << "TOPSHP " << c.topshp << " gave " << FirstSymbol(out);
  }
  EXPECT_NE(e->Pixmap("TOPMAR01"), nullptr);
}

TEST(S52Style, RestrictionFamilyFollowsRestrn) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // RESTRN01 and RESARE02 are called only from the AREA tables in the
  // delivered library — there is no point or line row for either — so the
  // area path is the only one the data can reach, and it is what is asserted.
  // The family's boundary is a COMPLEX line, which since E3b means a line
  // pattern for the shared placer.
  struct Case { const char* restrn; const char* line_style; };
  const Case cases[] = {
      {"7", "ENTRES51"},      // entry prohibited
      {"1", "ACHRES51"},      // anchoring prohibited
      {"3", "CTYARE51"},      // fishing prohibited
      {nullptr, nullptr},     // restricted, but RESTRN does not say how
  };
  for (const Case& c : cases) {
    fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "RESARE");
    if (c.restrn != nullptr) SetAttr(&f, "RESTRN", c.restrn);
    std::vector<fv::StyleResult> out;
    ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
    if (c.line_style == nullptr) {
      // No family: the library's undefined-restriction mark plus a plain pen.
      EXPECT_TRUE(HasSymbol(out, "RSRDEF51"))
          << "RESTRN absent gave " << FirstSymbol(out);
      continue;
    }
    bool boundary = false;
    for (const auto& r : out)
      if (r.line_pattern.valid && !r.line_pattern.runs.empty() &&
          r.line_pattern.runs[0].symbol_id == c.line_style)
        boundary = true;
    EXPECT_TRUE(boundary) << "RESTRN " << c.restrn << " drew no " << c.line_style;
    // ...and deliberately no centred symbol (see the procedure's comment).
    EXPECT_FALSE(HasSymbol(out, c.line_style));
  }
}

TEST(S52Style, AreaPatternBecomesAGridStampNotATint) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // CATCOV 2 = no coverage, which DATCVR01 answers with the no-data pattern.
  // M_COVR is a META object and those are off by default, so this test — which
  // is about the PATTERN, not about the default — turns them on explicitly.
  e->SetShowMetaObjects(true);
  fv::VectorFeature hole = MakeFeature(fv::VectorGeometryType::kArea, "M_COVR");
  SetAttr(&hole, "CATCOV", "2");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(hole, Ctx(), &out).ok());
  bool patterned = false;
  for (const auto& r : out) {
    if (!r.area_pattern.valid) continue;
    patterned = true;
    EXPECT_EQ(r.area_pattern.symbol_id, "NODATA03");
    EXPECT_GE(r.area_pattern.spacing_x, 2.0);
    EXPECT_GE(r.area_pattern.spacing_y, 2.0);
  }
  EXPECT_TRUE(patterned);
}

TEST(S52Style, QuaposLowAccuracyChangesTheLineSymbolization) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature good = MakeFeature(fv::VectorGeometryType::kLine, "COALNE");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(good, Ctx(), &out).ok());
  fv::Pen accurate{};
  ASSERT_TRUE(FirstStroke(out, &accurate));

  fv::VectorFeature poor = MakeFeature(fv::VectorGeometryType::kLine, "COALNE");
  SetAttr(&poor, "QUAPOS", "4");  // "approximate" in Appendix A
  out.clear();
  ASSERT_TRUE(e->Style(poor, Ctx(), &out).ok());

  // The accurate branch is a simple line (LS, solid). Since E3b the
  // low-accuracy branch is what the library actually says it is: the LOWACC21
  // complex line, stamped along the path by the shared placer rather than
  // approximated with a dashed pen. This distinction is the whole point of the
  // procedure — it is how a mariner sees that a coastline is unreliable — so
  // it is asserted on the instruction that reaches the renderer.
  EXPECT_TRUE(accurate.dash.empty());
  bool patterned = false;
  for (const fv::StyleResult& r : out) {
    if (!r.line_pattern.valid) continue;
    ASSERT_EQ(r.line_pattern.runs.size(), 1u);
    EXPECT_EQ(r.line_pattern.runs[0].type, fv::PathRunType::kSymbol);
    EXPECT_EQ(r.line_pattern.runs[0].symbol_id, "LOWACC21");
    EXPECT_GT(r.line_pattern.runs[0].length, 0.5);
    patterned = true;
  }
  EXPECT_TRUE(patterned) << "low-accuracy coastline drew as an accurate one";
}

TEST(S52Style, RuinedShorelineConstructionIsDashed) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature solid = MakeFeature(fv::VectorGeometryType::kLine, "SLCONS");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(solid, Ctx(), &out).ok());
  fv::Pen pen{};
  ASSERT_TRUE(FirstStroke(out, &pen));
  EXPECT_TRUE(pen.dash.empty());

  fv::VectorFeature ruined = MakeFeature(fv::VectorGeometryType::kLine, "SLCONS");
  SetAttr(&ruined, "CONDTN", "2");  // ruined
  out.clear();
  ASSERT_TRUE(e->Style(ruined, Ctx(), &out).ok());
  ASSERT_TRUE(FirstStroke(out, &pen));
  EXPECT_FALSE(pen.dash.empty());
}

// ---------------------------------------------------------------------------
// Display settings
// ---------------------------------------------------------------------------

// --- E5: the two rendering defects a published chart comparison exposed -----

// META objects (M_QUAL, M_COVR, M_NSYS, ...) describe the DATASET, not the
// world. They are OFF by default; the switch is not a display CATEGORY,
// because M_QUAL is category OTHER and so are soundings and depth contours.
TEST(S52Style, MetaObjectsAreOffByDefaultAndSwitchable) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  EXPECT_FALSE(e->show_meta_objects());

  fv::VectorFeature q = MakeFeature(fv::VectorGeometryType::kArea, "M_QUAL");
  SetAttr(&q, "CATZOC", "4");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(q, Ctx(), &out).ok());
  EXPECT_TRUE(out.empty()) << "a meta object drew with the default settings";

  e->SetShowMetaObjects(true);
  out.clear();
  ASSERT_TRUE(e->Style(q, Ctx(), &out).ok());
  EXPECT_FALSE(out.empty()) << "meta objects on, but M_QUAL still drew nothing";

  // ...and a non-meta feature of the SAME display category is unaffected, which
  // is the whole reason this is a separate axis: SOUNDG and DEPCNT are category
  // OTHER too, and a category threshold that hid M_QUAL would hide them.
  e->SetShowMetaObjects(false);
  {
    fv::VectorFeature contour =
        MakeFeature(fv::VectorGeometryType::kLine, "DEPCNT");
    SetAttr(&contour, "VALDCO", "10");
    out.clear();
    ASSERT_TRUE(e->Style(contour, Ctx(), &out).ok());
    EXPECT_FALSE(out.empty()) << "DEPCNT vanished with the meta switch";

    fv::VectorFeature lake =
        MakeFeature(fv::VectorGeometryType::kArea, "LAKARE");
    out.clear();
    ASSERT_TRUE(e->Style(lake, Ctx(), &out).ok());
    EXPECT_FALSE(out.empty()) << "LAKARE vanished with the meta switch";
  }
}

// A symbol's pivot is what lands on the feature. The delivered library has 61
// vector symbols whose pivot sits outside their own ink — CTNARE51's is 1.8
// symbol-widths clear of it — and drawn as authored those marks float well
// away from the area they annotate. They are re-anchored on their geometry.
//
// The two halves of the rule are tested together on purpose: the fix is only
// correct if it leaves the symbols that legitimately hang off their pivot
// exactly where they were.
TEST(S52Style, SymbolsAreAnchoredOnTheirOwnInk) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  auto ink_box = [&](const char* name, double* x0, double* x1, double* y0,
                     double* y1) {
    const fv::VectorSymbol* v = e->Symbol(name);
    if (v == nullptr || v->primitives.empty()) return false;
    *x0 = *y0 = 1e18; *x1 = *y1 = -1e18;
    auto add = [&](double x, double y) {
      *x0 = std::min(*x0, x); *x1 = std::max(*x1, x);
      *y0 = std::min(*y0, y); *y1 = std::max(*y1, y);
    };
    for (const fv::SymbolPrimitive& p : v->primitives) {
      for (const fv::SymbolPoint& q : p.points) add(q.x, q.y);
      if (p.type == fv::SymbolPrimitiveType::kEllipse) {
        const double rx = std::hypot(double(p.radius1.x), double(p.radius1.y));
        const double ry = std::hypot(double(p.radius2.x), double(p.radius2.y));
        add(p.center.x - rx, p.center.y - ry);
        add(p.center.x + rx, p.center.y + ry);
      } else if (p.points.empty()) {
        add(p.center.x, p.center.y);
      }
    }
    return *x0 <= *x1;
  };

  // Was 1.80 x 0.60 symbol-widths off before the fix; must now contain (0,0).
  for (const char* name : {"CTNARE51", "CTYARE51", "INFARE51", "RSRDEF51",
                           "ENTRES51", "TSSCRS51", "RETRFL01"}) {
    double x0, x1, y0, y1;
    if (!ink_box(name, &x0, &x1, &y0, &y1)) continue;  // raster-only build
    const double mx = 0.25 * (x1 - x0), my = 0.25 * (y1 - y0);
    EXPECT_LE(x0 - mx, 0.0) << name << " draws entirely right of its anchor";
    EXPECT_GE(x1 + mx, 0.0) << name << " draws entirely left of its anchor";
    EXPECT_LE(y0 - my, 0.0) << name << " draws entirely above its anchor";
    EXPECT_GE(y1 + my, 0.0) << name << " draws entirely below its anchor";
  }

  // The other half: these are authored to STAND ON their pivot, and the rule
  // must not have recentred them. A light's flare rises from the position, so
  // its ink starts at y = 0 and goes up; recentring would drop it onto the
  // light and lose the direction the flare points.
  for (const char* name : {"LIGHTS11", "LIGHTS12", "LIGHTS13", "BCNSTK02",
                           "NOTBRD11"}) {
    double x0, x1, y0, y1;
    if (!ink_box(name, &x0, &x1, &y0, &y1)) continue;
    EXPECT_NEAR(y0, 0.0, 1.0) << name << " no longer rises from its anchor";
    EXPECT_GT(y1, 0.0) << name;
  }
}

TEST(S52Style, ColourSchemeSwitchRepaintsEverything) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  fv::FvColor day{};
  ASSERT_TRUE(FirstFill(out, &day));

  ASSERT_TRUE(e->SetColorScheme(fv::S52ColorScheme::kNight).ok());
  EXPECT_EQ(e->color_scheme(), fv::S52ColorScheme::kNight);
  out.clear();
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  fv::FvColor night{};
  ASSERT_TRUE(FirstFill(out, &night));

  EXPECT_FALSE(SameColor(night, day.r, day.g, day.b));
  // Night land is darker than day land — the property a mariner cares about.
  EXPECT_LT(night.r + night.g + night.b, day.r + day.g + day.b);
}

TEST(S52Style, PointStyleSelectsADifferentLookupTable) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature buoy = MakeFeature(fv::VectorGeometryType::kPoint, "BOYLAT");
  SetAttr(&buoy, "BOYSHP", "2");
  SetAttr(&buoy, "COLOUR", "3");
  std::vector<fv::StyleResult> paper;
  ASSERT_TRUE(e->Style(buoy, Ctx(), &paper).ok());

  e->SetPointStyle(fv::S52PointStyle::kSimplified);
  std::vector<fv::StyleResult> simplified;
  ASSERT_TRUE(e->Style(buoy, Ctx(), &simplified).ok());

  EXPECT_NE(FirstSymbol(paper), FirstSymbol(simplified));
}

TEST(S52Style, DisplayCategoryThresholdHidesTheOptionalRows) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  // Depth contours are STANDARD; the base display keeps only BASE rows.
  fv::VectorFeature contour = MakeFeature(fv::VectorGeometryType::kLine, "DEPCNT");
  SetAttr(&contour, "VALDCO", "5");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(contour, Ctx(), &out).ok());
  EXPECT_FALSE(out.empty());

  e->viewing_groups().SetMaxCategory(fv::kDisplayBase);
  out.clear();
  ASSERT_TRUE(e->Style(contour, Ctx(), &out).ok());
  EXPECT_TRUE(out.empty()) << "a STANDARD row survived the BASE threshold";

  // Land is DISPLAY BASE and must survive the same threshold.
  fv::VectorFeature land = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  out.clear();
  ASSERT_TRUE(e->Style(land, Ctx(), &out).ok());
  EXPECT_FALSE(out.empty()) << "display base was hidden";
}

TEST(S52Style, TheSharedRuleLayerAppliesToEncToo) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  fv::VectorFeature f = MakeFeature(fv::VectorGeometryType::kArea, "LNDARE");
  std::vector<fv::StyleResult> out;
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  ASSERT_FALSE(out.empty());

  // One line of the cross-product rule syntax, unchanged from GeoSym's.
  const fv::Status s = e->rules().LoadText("hide key=LNDARE\n");
  ASSERT_TRUE(s.ok()) << s.message;
  out.clear();
  ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
  EXPECT_TRUE(out.empty());
}

TEST(S52Style, SymbolReturnsACachedDisplayList) {
  SKIP_WITHOUT_PRESLIB();
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  const fv::VectorSymbol* s = e->Symbol("QUESMRK1");
  ASSERT_NE(s, nullptr);
  EXPECT_FALSE(s->primitives.empty());
  EXPECT_EQ(e->Symbol("QUESMRK1"), s);
  EXPECT_EQ(e->Symbol("NOSUCHSYMBOL"), nullptr);
}

// ---------------------------------------------------------------------------
// End to end
// ---------------------------------------------------------------------------

// Pinned 2026-07-27 (E3a), after looking at the PNG: Charleston Harbor comes
// out as a real ENC — buff land, the depth ramp from DEPVS inshore through
// DEPMS/DEPMD to DEPDW in the channel, black point symbology, magenta
// navigational marks, and the question mark wherever a CS procedure or a
// raster-only symbol is still owed (E3b).
// 0 = probe mode (prints the hash, asserts nothing).
// Re-pinned 2026-07-27 (E3b), for two reasons at once:
//  * LC complex lines now stamp their line-style symbol along the path through
//    the shared placer instead of being approximated by a dashed pen, and AP
//    area patterns stamp their symbol on a grid instead of tinting the fill;
//  * the seven remaining CS procedures landed, so the harbour's 100 lights
//    draw their coloured flares, its 105 obstructions and 14 wrecks their own
//    hazard symbology, and its restricted areas their boundary line, in place
//    of 281 question marks.
// Visually re-checked s52_charleston.png: red/green/white light flares over
// the navaids, patterned magenta linework for the restricted areas and cables,
// and the depth ramp unchanged underneath.
// Re-pinned 2026-07-28 (E5), after comparing the app's render against a
// published ENC of the same water. Two rendering defects, both visible:
//  * META objects were drawn by default. M_QUAL's zones-of-confidence
//    triangles patterned the whole harbour and M_COVR/M_NSYS added magenta
//    linework over it — 640 of the 1234 draws in a typical viewport were
//    metadata about the dataset rather than chart content. Off by default now
//    (SetShowMetaObjects), which is where an ECDIS keeps them.
//  * 61 of the 367 vector symbols carry an authored PIVOT outside their own
//    ink, most by 1.5-2.0 symbol-widths, so caution and restricted-area marks
//    drew 40-70 px clear of the feature they annotate. Those are re-anchored
//    on their own geometry; symbols that legitimately hang off their pivot (a
//    light's flare, a beacon rising from its position) keep it.
// Visually re-checked s52_charleston.png: the harbour is clean water under the
// depth ramp again, and the caution/restricted marks sit on their areas.
// Re-pinned 2026-07-28 (E6), for the largest visible gap E5 left open: 679 of
// the library's 1018 symbols are RASTER-ONLY and were drawing the question
// mark. In this viewport that is 98 draws across 15 names — the beacons
// (BCNSTK60/61, BCNTOW61), the buoys (BOYPIL60/61, BOYCAN63) and their
// topmarks (TOPSHP20/48/90, TOPMAR01) — which now blit their own tile out of
// rastersymbols-day.png. No feature in these cells draws a placeholder any
// more (S52Render.RasterOnlySymbolsAreDrawnFromTheSheet asserts the zero).
// Visually re-checked at three scales (0.00005 / 0.0002 / 0.0008 deg/px) and
// at 2x symbol scale: lateral buoys are the right colour and shape, daymarks
// stand on their pivots, and the resampled tiles stay crisp when zoomed.
// Re-pinned 2026-08-08 (R3c), for the area-pattern anchor: PlaceOverArea's
// stamp grid hung on the CANVAS origin, so an AP fill crawled inside its own
// region whenever the map panned. It now hangs on a fixed geographic point
// projected into the frame, which moves this chart's marsh/foreshore stamps
// by a sub-spacing offset and nothing else.
// 0x5bfb57105171e60e -> 0x48a0cf127f6a2584. Visually re-checked against the
// pre-change render side by side: the tufts sit in different places along the
// left edge and the bottom-left creek, and every other mark on the chart —
// land, the depth ramp, buoys, lights, the magenta linework — is unmoved.
//
// Re-pinned again 2026-08-08, for the two follow-on fixes the viewer forced
// (see the pan tests below): the anchor is now a RETAINED NEARBY ground point
// rather than lat/lon 0,0, and the placer tests the exact projected ring
// rather than the whole-pixel one ClipPolygon returns.
// 0x48a0cf127f6a2584 -> 0xa01ffd5340f22646. Diffed against the pre-change
// render pixel by pixel rather than eyeballed: 135 pixels of 262,144 changed
// (0.05 %), in 9 connected clusters, and every changed pixel is marsh-grass
// ink — tufts that the rounded ring had been excluding near a boundary now
// appear. No other mark on the chart differs by a single pixel.
//
// Re-pinned 2026-08-11 (E7) — the largest move this golden has had, and every
// part of it intended: 0xa01ffd5340f22646 -> 0xe00d1fe6b6907b43. Three
// symbology changes at once. Display lists are now sized on S-52's own 0.32 mm
// grid instead of GeoSym's 1/100 inch, so every vector symbol is 21% smaller
// and finally agrees with the TILE of the same symbol beside it; light flares
// carry their 135-degree bearing, so they lean down and to the right off their
// lights instead of growing out of the top of them; and a sounding is a row of
// library digit symbols with the decimetre subscripted rather than a text run.
// Read rather than hashed: the flares all lean the same way and clear their
// buoys, and the soundings read 10-sub-6, 5-sub-7, 3-sub-9, 2-sub-4.
//
// NOTE this is also the first run of this test since TestData/enc was cut back
// to the 8 Charleston cells (2026-08-11). It had been DARK, not passing: the
// directory had grown to 823 cells and `Open()` failed whole on the two that
// will not parse, so every S52Render test died before it drew. The 8 that
// remain are the ones the goldens were always pinned over — the set is
// reproducible, being every cell whose coverage meets lat 32.60..32.95,
// lon -80.15..-79.75, which is the padded extent of every coordinate these
// tests name.
constexpr uint64_t kHashCharleston = 0xf828df94fa9a5721ull;

TEST(S52Render, CharlestonHarborViewport) {
  SKIP_WITHOUT_PRESLIB();
  auto source = std::make_shared<fv::EncVectorSource>();
  ASSERT_TRUE(source->Open(enc_root).ok());
  auto style = std::make_shared<fv::S52StyleEngine>();
  ASSERT_TRUE(style->Open(enc_root).ok());

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(512, 512).ok());
  ASSERT_TRUE(proj.SetCenter(fv::GeoPoint{32.78, -79.93}).ok());
  // An explicit resolution, not a scale: the golden must not move if
  // MapScaleUtil's latitude-dependent dpp is ever retuned (V5b's rule).
  ASSERT_TRUE(proj.SetResolution(0.0002, 0.0002).ok());

  fv::CpuCanvas canvas(512, 512);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});

  fv::VectorRenderer r(source, style);
  ASSERT_TRUE(r.Render(proj, &canvas).ok());

  EXPECT_GT(r.features_queried(), 500u);
  EXPECT_GT(r.draws_emitted(), 500u);
  EXPECT_GT(NonBackgroundPixels(canvas.Buffer(), 255), 100000u)
      << "the chart came out blank";

  const uint64_t h = Fnv1a(canvas.Buffer());
  if (kHashCharleston == 0)
    printf("PROBE charleston hash: 0x%llxull\n", (unsigned long long)h);
  else
    EXPECT_EQ(h, kHashCharleston);
  WritePng(canvas.Buffer(), "s52_charleston");
}

// R3c. THE TEST THE CRAWLING MARSH GRASS SHOULD HAVE HAD.
//
// A map that pans is drawing the same ground through a moved window, so every
// mark on it must move by exactly the number of pixels the window moved and
// by nothing else. The S-52 area patterns did not.
//
// THREE defects, found in that order. The last two are why this test is
// parameterised over the pan DIRECTION — each shows on an axis the others do
// not, and a test that panned one way passed while the chart was visibly wrong.
//
//  1. The stamp grid hung on the CANVAS origin, so the tufts stood still while
//     the marsh slid out from under them — a visible crawl.
//  2. R3c re-hung it on lat/lon 0,0, which is ~600,000 px off-screen at
//     Charleston. That is ground-fixed only while dpp holds still, and
//     MapProjection derives dpp from the CENTRE LATITUDE in its scale-driven
//     modes, so a north/south pan rescales it by ~1.26e-6 per pixel and the
//     lever arm turns that into 0.75 px of lattice slip PER PIXEL of vertical
//     pan. Tufts stopped crawling and started blinking on and off instead, a
//     whole cell every ~39 px of drag. Chris caught it in the viewer.
//  3. Independently: the placer took the ring ClipPolygon had ALREADY rounded
//     to whole pixels, so the boundary it tested moved by up to half a pixel
//     with the pan and stamps near an edge flipped in and out. On a marsh cut
//     to ribbons by tidal channels, nearly every stamp is near an edge. This
//     one shows on an EAST pan, where the anchor is blameless.
//
// Measured here, in percent of interior pixels that failed to move with the map
// (see HOW TO READ THE NUMBER below for why none of these is zero):
//
//                                          east    north    diagonal
//     0,0 anchor, clipped integer ring     1.073    3.022     2.828
//     retained anchor, clipped ring        1.073    0.210     1.243
//     retained anchor, exact ring          0.148    0.211     0.276
//
// The original of this test panned only in LONGITUDE, where dpp is constant,
// and so was blind to (2) by construction. It also built a fresh renderer per
// frame; the lattice is now renderer state (see VectorRenderer::PatternAnchor)
// exactly as a retained scene is, so a pan test must reuse one renderer, which
// is what an interactive caller does anyway.
//
// SetPhysicalScale, not SetResolution, for the same reason: the viewer runs in
// physical-scale mode and that is the mode whose dpp moves with the centre.
//
// HOW TO READ THE NUMBER. The pan is 37 px — deliberately not a multiple of
// any pattern spacing in the library, since a multiple would hide the very
// defect under test — and the two frames are compared with the second shifted
// back onto the first. They do NOT come out identical, and the reason is not
// the patterns: shifting the CENTRE by 37 pixels' worth of degrees is 37
// pixels in real arithmetic but not in floating-point arithmetic, so a
// projected vertex can land a thousandth of a pixel the other side of a
// rounding boundary, and a long straight line whose endpoint flips redraws one
// pixel over. Allowing a match anywhere in a 1 px neighbourhood absorbs that;
// a pattern on the wrong lattice is out by a fraction of its SPACING, which is
// several pixels, and no neighbourhood hides it.
//
// s52_pan_diff_*.png marks the disagreeing pixels in red, and is worth LOOKING
// at rather than trusting the number — in a failing build the tufts light up
// as red glyphs, which is the defect drawn.
namespace {

// Renders the Charleston viewport at `pan` px east and `pan_down` px south of
// the base centre, through ONE renderer, and returns the fraction of interior
// pixels that did not move with the map.
double PanMismatch(const std::string& enc_root, int pan_x, int pan_y,
                   const char* png_name) {
  // 768 px over the marshes south of the harbour, at the viewer's own kind of
  // scale. The E3a golden's 512 px harbour viewport is nearly all water and
  // holds barely a dozen stamps — it let a 3 % defect through as a pass, which
  // is how the lever arm survived a test written to catch exactly it.
  constexpr int kW = 768, kH = 768;
  constexpr double kLat = 32.72, kLon = -79.93;
  constexpr double kDen = 50000.0;

  auto source = std::make_shared<fv::EncVectorSource>();
  EXPECT_TRUE(source->Open(enc_root).ok());
  auto style = std::make_shared<fv::S52StyleEngine>();
  EXPECT_TRUE(style->Open(enc_root).ok());
  // One renderer for both frames: the stamp lattice is carried across frames.
  fv::VectorRenderer r(source, style);

  auto RenderAt = [&](int dx, int dy, fv::CpuCanvas* canvas) {
    fv::MapProjection proj;
    EXPECT_TRUE(proj.SetSurfaceSize(kW, kH).ok());
    EXPECT_TRUE(proj.SetCenter(fv::GeoPoint{kLat, kLon}).ok());
    EXPECT_TRUE(proj.SetPhysicalScale(kDen, 0.25).ok());
    // Offset in whole pixels of THIS projection. Moving the centre changes dpp
    // (that is the point of the test), so the offset is applied against the
    // dpp the base centre produced and the centre re-set once.
    EXPECT_TRUE(proj.SetCenter(fv::GeoPoint{
                                   kLat - dy * proj.DegPerPixelLat(),
                                   kLon + dx * proj.DegPerPixelLon()})
                    .ok());
    canvas->Clear(fv::FvColor{255, 255, 255, 255});
    EXPECT_TRUE(r.Render(proj, canvas).ok());
  };

  fv::CpuCanvas a(kW, kH), b(kW, kH);
  RenderAt(0, 0, &a);
  // A ground point that was at (x, y) is now at (x - pan_x, y - pan_y).
  RenderAt(pan_x, pan_y, &b);

  // Everything outside a 56 px frame is excluded: near the edge the two frames
  // legitimately disagree, since one of them queried ground the other did not
  // and clips geometry the other draws whole.
  constexpr int kMargin = 56;
  fv::CpuCanvas diff(kW, kH);
  for (int y = 0; y < kH; ++y)
    memcpy(const_cast<unsigned char*>(diff.Buffer().Row(y)),
           b.Buffer().Row(y), static_cast<size_t>(kW) * 4);

  size_t compared = 0, differing = 0;
  for (int y = kMargin; y < kH - kMargin - pan_y; ++y) {
    const unsigned char* rb = b.Buffer().Row(y);
    for (int x = kMargin; x < kW - kMargin - pan_x; ++x) {
      const unsigned char* pb = rb + x * 4;
      ++compared;
      bool found = false;
      for (int dy = -1; dy <= 1 && !found; ++dy)
        for (int dx = -1; dx <= 1 && !found; ++dx) {
          const unsigned char* pa =
              a.Buffer().Row(y + pan_y + dy) + (x + pan_x + dx) * 4;
          if (pa[0] == pb[0] && pa[1] == pb[1] && pa[2] == pb[2]) found = true;
        }
      if (found) continue;
      ++differing;
      unsigned char* pd =
          const_cast<unsigned char*>(diff.Buffer().Row(y)) + x * 4;
      pd[0] = 255; pd[1] = 0; pd[2] = 0; pd[3] = 255;
    }
  }
  WritePng(diff.Buffer(), png_name);
  EXPECT_GT(compared, 90000u);
  return compared == 0 ? 1.0
                       : static_cast<double>(differing) / compared;
}

}  // namespace

TEST(S52Render, PanMovesEveryMarkByTheSameAmountIncludingAreaPatterns) {
  SKIP_WITHOUT_PRESLIB();
  // East/west: dpp does not move, so this axis was already clean before the
  // PatternAnchor fix. Kept because it is the axis that pins the placer.
  const double east = PanMismatch(enc_root, 37, 0, "s52_pan_diff_east");
  EXPECT_LT(east, 0.0035)
      << east * 100.0 << "% of pixels did not move with an east pan — look at "
         "s52_pan_diff_east.png: red glyphs on the marsh mean the pattern "
         "lattice is anchored to the canvas again";
}

TEST(S52Render, PanningNorthDoesNotReshuffleAreaPatterns) {
  SKIP_WITHOUT_PRESLIB();
  // THE ONE THAT CATCHES THE LEVER ARM. A vertical pan is the only pan that
  // changes dpp, and dpp is what the 0,0 anchor was multiplied by: 3.022 %
  // with that anchor, 0.211 % with a retained near-viewport one.
  const double north = PanMismatch(enc_root, 0, 37, "s52_pan_diff_north");
  EXPECT_LT(north, 0.0035)
      << north * 100.0
      << "% of pixels did not move with a north pan — look at "
         "s52_pan_diff_north.png: red glyphs on the marsh mean the stamp "
         "lattice slid sideways when dpp changed, i.e. the anchor has a lever "
         "arm again";
}

TEST(S52Render, PanningDiagonallyDoesNotReshuffleAreaPatterns) {
  SKIP_WITHOUT_PRESLIB();
  // What a drag actually is, and what Chris's two screenshots were.
  const double diag = PanMismatch(enc_root, 37, 29, "s52_pan_diff_diag");
  EXPECT_LT(diag, 0.0035)
      << diag * 100.0 << "% of pixels did not move with a diagonal pan — see "
                         "s52_pan_diff_diag.png";
}

TEST(S52Render, RenderIsDeterministic) {
  SKIP_WITHOUT_PRESLIB();
  uint64_t hashes[2] = {0, 0};
  for (int i = 0; i < 2; ++i) {
    auto source = std::make_shared<fv::EncVectorSource>();
    ASSERT_TRUE(source->Open(enc_root).ok());
    auto style = std::make_shared<fv::S52StyleEngine>();
    ASSERT_TRUE(style->Open(enc_root).ok());

    fv::MapProjection proj;
    proj.SetSurfaceSize(256, 256);
    proj.SetCenter(fv::GeoPoint{32.78, -79.93});
    proj.SetResolution(0.0004, 0.0004);

    fv::CpuCanvas canvas(256, 256);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    fv::VectorRenderer r(source, style);
    ASSERT_TRUE(r.Render(proj, &canvas).ok());
    hashes[i] = Fnv1a(canvas.Buffer());
  }
  EXPECT_EQ(hashes[0], hashes[1]);
}

// What the chart is still owed, measured over the whole delivered data set
// rather than asserted from a plan. This is the E3b worklist, and it fails if
// a procedure regresses OUT of the implemented set.
TEST(S52Render, EveryFeatureInTheCellsIsSymbolizedOrCounted) {
  SKIP_WITHOUT_PRESLIB();
  fv::EncVectorSource source;
  ASSERT_TRUE(source.Open(enc_root).ok());
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);
  // Labels ON: a large minority of S-52 rows are text-only (a sounding IS its
  // number, a sea area IS its name), so with labels off they legitimately draw
  // nothing and the sweep would be measuring the label switch instead.
  e->SetDrawLabels(true);

  std::vector<fv::VectorFeature> features;
  ASSERT_TRUE(source.Query(fv::VectorQuery(), &features).ok());
  ASSERT_FALSE(features.empty());

  // Plan §7's rule: a feature draws nothing ONLY for a reason the data itself
  // gives. THREE are legitimate and all are present in these cells:
  //   * the feature is a META object (M_QUAL, M_COVR, M_NSYS ...) and those are
  //     off by default — they describe the dataset, not the world;
  //   * the lookup row's instruction is empty (the library says draw nothing —
  //     M_NPUB, nautical publication coverage);
  //   * the row is text-only and this feature lacks the named attribute (an
  //     SBDARE point with no NATSUR has no seabed nature to print).
  // Anything else is a hole in the engine and fails here.
  size_t unexplained = 0, blank_row = 0, text_without_value = 0, meta = 0;
  std::vector<fv::StyleResult> out;
  for (const auto& f : features) {
    out.clear();
    ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
    if (!out.empty()) continue;

    if (f.style_key.compare(0, 2, "M_") == 0) {
      ++meta;
      continue;
    }

    const fv::S52Lookup* row = e->SelectLookup(f);
    if (row == nullptr || row->instructions.empty()) {
      ++blank_row;
      continue;
    }
    bool all_text = true, any_value = false;
    for (const auto& ins : row->instructions) {
      if (ins.op != "TX" && ins.op != "TE") { all_text = false; break; }
      const size_t attr = ins.op == "TE" ? 1u : 0u;
      if (ins.params.size() > attr && f.Attribute(ins.params[attr]) != nullptr)
        any_value = true;
    }
    if (all_text && !any_value) {
      ++text_without_value;
      continue;
    }
    ++unexplained;
  }
  EXPECT_EQ(unexplained, 0u) << "features vanished with no reason in the data";
  EXPECT_GT(blank_row + text_without_value, 0u)
      << "the two documented cases disappeared — re-derive this test";
  EXPECT_GT(meta, 0u) << "no meta objects in these cells — re-derive this test";

  // ...and with meta objects turned ON, every one of them draws: the default
  // suppresses them, it does not hide an inability to symbolize them.
  e->SetShowMetaObjects(true);
  size_t meta_drawn = 0, meta_silent = 0;
  for (const auto& f : features) {
    if (f.style_key.compare(0, 2, "M_") != 0) continue;
    out.clear();
    ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
    if (out.empty()) {
      const fv::S52Lookup* row = e->SelectLookup(f);
      // M_NPUB's row is deliberately empty; that is the blank-row case again.
      if (row == nullptr || row->instructions.empty()) ++meta_silent;
      else ++meta_drawn;  // counted as drawn-or-explained below
    } else {
      ++meta_drawn;
    }
  }
  EXPECT_EQ(meta_drawn + meta_silent, meta);
  EXPECT_GT(meta_drawn, 0u) << "meta objects turned on but nothing drew";
  e->SetShowMetaObjects(false);

  // E3b closed the worklist: every CS procedure these cells reach is now
  // implemented, so this is an EMPTY-SET assertion rather than a list of
  // what is owed. Anything that shows up here is either a regression or a
  // procedure a new cell asked for.
  for (const auto& kv : e->unhandled_cs())
    ADD_FAILURE() << "unimplemented CS procedure " << kv.first << " x"
                  << kv.second;
  EXPECT_TRUE(e->unhandled_cs().empty());

  // The named twelve must stay implemented. E3a's five plus E3b's seven.
  for (const char* done : {"DATCVR01", "DEPARE01", "DEPCNT02", "LIGHTS05",
                           "OBSTRN04", "QUAPOS01", "RESARE02", "RESTRN01",
                           "SLCONS03", "SOUNDG02", "TOPMAR01", "WRECKS02"})
    EXPECT_EQ(e->unhandled_cs().count(done), 0u) << done << " regressed";

  // LIGHTS05's one deviation is measured, not remembered: these cells carry
  // sector lights, and each was drawn as a plain flare (see the procedure).
  EXPECT_GT(e->sector_lights_simplified(), 0u)
      << "no sector lights in the cells — re-derive this expectation";

  // E3a's finding was that every unresolved name in this data set is a
  // RASTER-ONLY definition — the library defines it with no HPGL — rather than
  // a dangling reference. E6 drew those, so the assertion tightens: a name
  // that still comes back unresolved must be one a SY() instruction never
  // named, i.e. a line-style (LC) or a pattern (AP), which have no raster path
  // and were not part of E6.
  for (const auto& kv : e->unresolved_symbols()) {
    const bool defined = e->library().SymbolDef(kv.first) != nullptr ||
                         e->library().LineStyleDef(kv.first) != nullptr ||
                         e->library().PatternDef(kv.first) != nullptr;
    EXPECT_TRUE(defined) << kv.first << " is a dangling reference, not raster-only";
    EXPECT_EQ(e->Pixmap(kv.first), nullptr)
        << kv.first << " has a tile and should have been drawn from it";
  }
}

// What E6 is: the raster half of the symbol library reaching the canvas.
// Measured over the whole data set rather than asserted from the plan, and
// over the harbour viewport that made it visible in the first place.
TEST(S52Render, RasterOnlySymbolsAreDrawnFromTheSheet) {
  SKIP_WITHOUT_PRESLIB();
  fv::EncVectorSource source;
  ASSERT_TRUE(source.Open(enc_root).ok());
  auto e = OpenEngine(enc_root);
  ASSERT_NE(e, nullptr);

  std::vector<fv::VectorFeature> features;
  ASSERT_TRUE(source.Query(fv::VectorQuery(), &features).ok());
  ASSERT_FALSE(features.empty());

  size_t vector_symbols = 0, raster_symbols = 0, question_marks = 0;
  std::vector<fv::StyleResult> out;
  for (const auto& f : features) {
    out.clear();
    ASSERT_TRUE(e->Style(f, Ctx(), &out).ok());
    for (const fv::StyleResult& r : out) {
      if (!r.symbol.valid) continue;
      if (r.symbol.symbol_id == "QUESMRK1") {
        ++question_marks;
      } else if (e->Symbol(r.symbol.symbol_id) != nullptr) {
        ++vector_symbols;
      } else if (e->Pixmap(r.symbol.symbol_id) != nullptr) {
        ++raster_symbols;
      } else {
        ADD_FAILURE() << r.symbol.symbol_id << " resolves to nothing";
      }
    }
  }
  // Both halves of the library are load-bearing on this chart: the buoys,
  // beacons and daymarks are raster, most of the rest is vector.
  EXPECT_GT(raster_symbols, 50u) << "the sheet is not reaching the canvas";
  EXPECT_GT(vector_symbols, raster_symbols);
  // Nothing in these cells needs a placeholder any more: every CS procedure
  // they reach is implemented (E3b) and every symbol they name now resolves.
  EXPECT_EQ(question_marks, 0u);
  EXPECT_EQ(e->placeholders_drawn(), 0u);
}

