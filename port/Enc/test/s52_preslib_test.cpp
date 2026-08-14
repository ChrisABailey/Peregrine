// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::S52PresentationLibrary tests (ENC phase E2).
//
// Two halves, on purpose:
//  * HERMETIC — the instruction splitter and the HPGL flattener run on
//    hand-written strings, so the unit conversions (0.01 mm, pivot origin,
//    y flip, 0.32 mm pen units, ST transparency) are pinned by arithmetic that
//    does not depend on any delivered file.
//  * REAL — TestData/enc/chartsymbols.xml. Inventory counts are DERIVED from
//    the file (count the elements, require the loader to have kept them all);
//    what is pinned as literals is S-52 semantics that a new PresLib release
//    would still have to honour, plus one symbol pinned coordinate by
//    coordinate as the geometry regression.

#include "fv_s52_preslib.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string EncRoot() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc";
  return fs::is_regular_file(p + "/chartsymbols.xml") ? p : std::string();
}

// Occurrences of `needle` in the PresLib file — the derivation the inventory
// assertions use instead of hardcoded totals.
size_t CountInFile(const std::string& path, const std::string& needle) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return 0;
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  size_t n = 0;
  for (size_t p = text.find(needle); p != std::string::npos;
       p = text.find(needle, p + needle.size())) {
    ++n;
  }
  return n;
}

#define OPEN_LIB(var)                                     \
  const std::string root = EncRoot();                     \
  if (root.empty()) GTEST_SKIP() << "no ENC test data";   \
  fv::S52PresentationLibrary var;                         \
  ASSERT_TRUE(var.Open(root).ok())

fv::S52ColorTable TwoColorPalette() {
  fv::S52ColorTable t;
  t.name = "TEST";
  t.colors["CHBLK"] = fv::FvColor{7, 7, 7, 255};
  t.colors["CHMGD"] = fv::FvColor{197, 69, 195, 255};
  return t;
}

// --- instruction splitting, hermetic ---------------------------------------

TEST(S52Instructions, SplitsOpsAndParameters) {
  const auto ins = fv::ParseS52Instructions("SY(ACHARE02);LS(DASH,2,CHMGF);CS(RESTRN01)");
  ASSERT_EQ(ins.size(), 3u);
  EXPECT_EQ(ins[0].op, "SY");
  ASSERT_EQ(ins[0].params.size(), 1u);
  EXPECT_EQ(ins[0].params[0], "ACHARE02");
  EXPECT_EQ(ins[1].op, "LS");
  ASSERT_EQ(ins[1].params.size(), 3u);
  EXPECT_EQ(ins[1].params[1], "2");
  EXPECT_EQ(ins[2].op, "CS");
  EXPECT_EQ(ins[2].params[0], "RESTRN01");
}

// A TE/TX format string carries its own commas inside single quotes; splitting
// on every comma would shred it.
TEST(S52Instructions, QuotedParameterKeepsItsCommas) {
  const auto ins = fv::ParseS52Instructions(
      "TE('%s, %s','OBJNAM,INFORM',2,1,2,'15110',-1,-1,CHBLK,21)");
  ASSERT_EQ(ins.size(), 1u);
  EXPECT_EQ(ins[0].op, "TE");
  ASSERT_EQ(ins[0].params.size(), 10u);
  EXPECT_EQ(ins[0].params[0], "%s, %s");
  EXPECT_EQ(ins[0].params[1], "OBJNAM,INFORM");
  EXPECT_EQ(ins[0].params[8], "CHBLK");
}

TEST(S52Instructions, EmptyAndTrailingSeparatorsAreHarmless) {
  EXPECT_TRUE(fv::ParseS52Instructions("").empty());
  EXPECT_EQ(fv::ParseS52Instructions("AC(DEPIT);").size(), 1u);
  EXPECT_EQ(fv::ParseS52Instructions(";;AC(DEPIT);;").size(), 1u);
}

// --- HPGL flattening, hermetic ---------------------------------------------

// The three conversions the seam depends on, in one square: coordinates are
// 0.01 mm (HIMETRIC, unscaled), the origin moves to the pivot, and y flips.
TEST(S52Hpgl, PivotBecomesTheOriginAndYFlips) {
  fv::VectorSymbol sym;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPA;SW1;PU100,100;PD300,100;PD300,300;",
                               "ACHBLK", TwoColorPalette(), 100, 100, &sym)
                  .ok());
  ASSERT_EQ(sym.primitives.size(), 1u);
  const auto& p = sym.primitives[0];
  EXPECT_EQ(p.type, fv::SymbolPrimitiveType::kPolyline);
  ASSERT_EQ(p.points.size(), 3u);
  EXPECT_DOUBLE_EQ(p.points[0].x, 0.0);
  EXPECT_DOUBLE_EQ(p.points[0].y, 0.0);
  EXPECT_DOUBLE_EQ(p.points[1].x, 200.0);
  EXPECT_DOUBLE_EQ(p.points[1].y, 0.0);
  EXPECT_DOUBLE_EQ(p.points[2].x, 200.0);
  EXPECT_DOUBLE_EQ(p.points[2].y, -200.0);  // y grows DOWN in the source
}

TEST(S52Hpgl, PenSelectsFromTheDefinitionsColourReference) {
  fv::VectorSymbol sym;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPB;SW1;PU0,0;PD10,0;", "ACHBLKBCHMGD",
                               TwoColorPalette(), 0, 0, &sym)
                  .ok());
  ASSERT_EQ(sym.primitives.size(), 1u);
  EXPECT_EQ(sym.primitives[0].stroke_color.r, 197);
  EXPECT_EQ(sym.primitives[0].stroke_color.g, 69);
  EXPECT_EQ(sym.primitives[0].stroke_color.b, 195);
}

// A symbol that loses a colour should look wrong, not vanish.
TEST(S52Hpgl, UnknownPenDrawsBlackRatherThanDroppingGeometry) {
  fv::VectorSymbol sym;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPZ;PU0,0;PD10,0;", "ACHBLK", TwoColorPalette(),
                               0, 0, &sym)
                  .ok());
  ASSERT_EQ(sym.primitives.size(), 1u);
  EXPECT_EQ(sym.primitives[0].stroke_color.r, 0);
  EXPECT_EQ(sym.primitives[0].stroke_color.b, 0);
}

// SW is in S-52 pen units of 0.32 mm; HIMETRIC is 0.01 mm.
TEST(S52Hpgl, PenWidthIsThirtyTwoHimetricPerUnit) {
  fv::VectorSymbol sym;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPA;SW1;PU0,0;PD10,0;SW3;PU0,10;PD10,10;",
                               "ACHBLK", TwoColorPalette(), 0, 0, &sym)
                  .ok());
  ASSERT_EQ(sym.primitives.size(), 2u);
  EXPECT_DOUBLE_EQ(sym.primitives[0].stroke_width, 32.0);
  EXPECT_DOUBLE_EQ(sym.primitives[1].stroke_width, 96.0);
}

TEST(S52Hpgl, PenUpEndsTheRunInsteadOfConnectingIt) {
  fv::VectorSymbol sym;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPA;PU0,0;PD10,0;PU100,0;PD110,0;", "ACHBLK",
                               TwoColorPalette(), 0, 0, &sym)
                  .ok());
  ASSERT_EQ(sym.primitives.size(), 2u);
  EXPECT_EQ(sym.primitives[0].points.size(), 2u);
  EXPECT_DOUBLE_EQ(sym.primitives[1].points[0].x, 100.0);
}

TEST(S52Hpgl, PolygonModeFillsTheRing) {
  fv::VectorSymbol sym;
  ASSERT_TRUE(fv::ParseS52Hpgl(
                  "SPB;SW1;ST0;PU0,0;PM0;PD100,0;PD100,100;PD0,100;PM2;FP;",
                  "ACHBLKBCHMGD", TwoColorPalette(), 0, 0, &sym)
                  .ok());
  ASSERT_EQ(sym.primitives.size(), 1u);
  const auto& p = sym.primitives[0];
  EXPECT_EQ(p.type, fv::SymbolPrimitiveType::kPolygon);
  EXPECT_TRUE(p.has_fill);
  EXPECT_FALSE(p.has_stroke);
  EXPECT_EQ(p.points.size(), 4u);  // the PU point starts the ring
  EXPECT_EQ(p.fill_color.r, 197);
  EXPECT_EQ(p.fill_color.a, 255);  // ST0 = opaque
}

// ST is S-52's transparency STEP: 0/1/2/3 = 0/25/50/75 % transparent.
TEST(S52Hpgl, TransparencyStepBecomesFillAlpha) {
  fv::VectorSymbol sym;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPA;ST2;PU0,0;PM0;PD10,0;PD10,10;PM2;FP;",
                               "ACHBLK", TwoColorPalette(), 0, 0, &sym)
                  .ok());
  ASSERT_EQ(sym.primitives.size(), 1u);
  EXPECT_EQ(sym.primitives[0].fill_color.a, 127);
}

TEST(S52Hpgl, CircleStrokesOutsidePolygonModeAndFillsInside) {
  fv::VectorSymbol open_sym;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPA;SW1;PU50,50;CI20;", "ACHBLK",
                               TwoColorPalette(), 50, 50, &open_sym)
                  .ok());
  ASSERT_EQ(open_sym.primitives.size(), 1u);
  EXPECT_EQ(open_sym.primitives[0].type, fv::SymbolPrimitiveType::kEllipse);
  EXPECT_TRUE(open_sym.primitives[0].has_stroke);
  EXPECT_FALSE(open_sym.primitives[0].has_fill);
  EXPECT_DOUBLE_EQ(open_sym.primitives[0].radius1.x, 20.0);
  EXPECT_DOUBLE_EQ(open_sym.primitives[0].radius2.y, 20.0);

  fv::VectorSymbol filled;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPA;SW1;ST0;PU50,50;PM0;CI20;PM2;FP;", "ACHBLK",
                               TwoColorPalette(), 50, 50, &filled)
                  .ok());
  ASSERT_EQ(filled.primitives.size(), 1u);
  EXPECT_EQ(filled.primitives[0].type, fv::SymbolPrimitiveType::kEllipse);
  EXPECT_TRUE(filled.primitives[0].has_fill);
}

TEST(S52Hpgl, ExtentCoversEveryPrimitiveIncludingCircles) {
  fv::VectorSymbol sym;
  ASSERT_TRUE(fv::ParseS52Hpgl("SPA;PU0,0;PD100,0;PU200,200;CI50;", "ACHBLK",
                               TwoColorPalette(), 0, 0, &sym)
                  .ok());
  EXPECT_DOUBLE_EQ(sym.min_x, 0.0);
  EXPECT_DOUBLE_EQ(sym.max_x, 250.0);
  EXPECT_DOUBLE_EQ(sym.min_y, -250.0);
  EXPECT_DOUBLE_EQ(sym.max_y, 0.0);
}

TEST(S52Hpgl, EmptyInputIsAnEmptySymbolNotAnError) {
  fv::VectorSymbol sym;
  EXPECT_TRUE(fv::ParseS52Hpgl("", "", TwoColorPalette(), 0, 0, &sym).ok());
  EXPECT_TRUE(sym.primitives.empty());
  EXPECT_DOUBLE_EQ(sym.max_x, 0.0);
}

// --- the delivered library --------------------------------------------------

TEST(S52PresLib, OpensFromADirectoryOrTheFileItself) {
  const std::string root = EncRoot();
  if (root.empty()) GTEST_SKIP() << "no ENC test data";
  fv::S52PresentationLibrary from_dir;
  ASSERT_TRUE(from_dir.Open(root).ok());
  EXPECT_TRUE(from_dir.is_open());

  fv::S52PresentationLibrary from_file;
  ASSERT_TRUE(from_file.Open(root + "/chartsymbols.xml").ok());
  EXPECT_EQ(from_dir.lookups().size(), from_file.lookups().size());
}

TEST(S52PresLib, MissingFileIsAnError) {
  fv::S52PresentationLibrary lib;
  EXPECT_FALSE(lib.Open("/nonexistent/enc").ok());
  EXPECT_FALSE(lib.is_open());
}

TEST(S52PresLib, KeepsEveryElementInTheFile) {
  OPEN_LIB(lib);
  const std::string xml = root + "/chartsymbols.xml";
  EXPECT_EQ(lib.lookups().size(), CountInFile(xml, "<lookup "));
  EXPECT_EQ(lib.line_style_count(), CountInFile(xml, "<line-style "));
  EXPECT_EQ(lib.pattern_count(), CountInFile(xml, "<pattern "));
  EXPECT_EQ(lib.color_tables().size(), CountInFile(xml, "<color-table "));
  // Symbols are counted by NAME: 73 of them are defined twice (vector +
  // raster-only), so the element count is higher than the symbol count.
  EXPECT_LT(lib.symbol_count(), CountInFile(xml, "<symbol "));
  EXPECT_GT(lib.symbol_count(), CountInFile(xml, "<symbol ") * 9 / 10);
}

// The duplicate-name rule, on the pair that exposed it: a raster-only row must
// never mask a vector definition, whichever comes first in the file.
TEST(S52PresLib, VectorDefinitionWinsOverARasterOnlyDuplicate) {
  OPEN_LIB(lib);
  for (const char* name : {"ACHARE51", "BCNGEN01"}) {
    const fv::S52SymbolDef* def = lib.SymbolDef(name);
    ASSERT_NE(def, nullptr) << name;
    EXPECT_FALSE(def->hpgl.empty()) << name << " kept the raster-only row";
    const fv::VectorSymbol* sym = lib.Symbol(name);
    ASSERT_NE(sym, nullptr) << name;
    EXPECT_FALSE(sym->primitives.empty()) << name;
  }
}

// All five S-52 lookup tables are loaded, because which pair is in force
// (paper vs simplified points, plain vs symbolized areas) is a mariner
// setting made at style time, not a load-time choice.
TEST(S52PresLib, LoadsAllFiveLookupTables) {
  OPEN_LIB(lib);
  size_t total = 0;
  for (int t = 0; t < static_cast<int>(fv::S52LookupTable::kLookupTableCount); ++t) {
    const size_t n = lib.lookup_count(static_cast<fv::S52LookupTable>(t));
    EXPECT_GT(n, 100u) << "lookup table " << t << " is empty or tiny";
    total += n;
  }
  EXPECT_EQ(total, lib.lookups().size());
}

TEST(S52PresLib, LoadsTheDayDuskAndNightPalettes) {
  OPEN_LIB(lib);
  EXPECT_EQ(lib.active_color_table(), "DAY_BRIGHT");
  for (const char* name : {"DAY_BRIGHT", "DAY_BLACKBACK", "DAY_WHITEBACK",
                           "DUSK", "NIGHT"}) {
    const fv::S52ColorTable* t = lib.ColorTable(name);
    ASSERT_NE(t, nullptr) << name;
    EXPECT_GT(t->colors.size(), 50u) << name;
    EXPECT_FALSE(t->graphics_file.empty()) << name;
  }
  // The day palette's black, pinned: it is the colour every fallback pen uses.
  fv::FvColor c;
  ASSERT_TRUE(lib.Color("CHBLK", &c));
  EXPECT_EQ(c.r, 7);
  EXPECT_EQ(c.g, 7);
  EXPECT_EQ(c.b, 7);
}

TEST(S52PresLib, ActiveTableSelectionIsCheckedAndChangesColours) {
  OPEN_LIB(lib);
  fv::FvColor day;
  ASSERT_TRUE(lib.Color("CHBLK", &day));
  EXPECT_FALSE(lib.SetActiveColorTable("NO_SUCH_TABLE").ok());
  ASSERT_TRUE(lib.SetActiveColorTable("NIGHT").ok());
  fv::FvColor night;
  ASSERT_TRUE(lib.Color("CHBLK", &night));
  EXPECT_NE(day.r + day.g + day.b, night.r + night.g + night.b);
}

// The three lookups the plan names as E2's acceptance pins.
TEST(S52PresLib, PinsTheDepareLookups) {
  OPEN_LIB(lib);
  const auto plain = lib.Lookups(fv::S52LookupTable::kPlainBoundaries, "DEPARE");
  ASSERT_EQ(plain.size(), 2u);
  // Row order is the file's, and S-52 takes the first row that matches: the
  // conditional row must not overtake the attributed one.
  EXPECT_EQ(plain[0]->instruction, "AC(NODTA);AP(PRTSUR01);LS(SOLD,2,CHGRD)");
  ASSERT_EQ(plain[0]->attributes.size(), 2u);
  EXPECT_EQ(plain[0]->attributes[0].acronym, "DRVAL1");
  EXPECT_EQ(plain[0]->attributes[0].value, "?");  // "present, any value"
  EXPECT_EQ(plain[0]->type, fv::S52ObjectType::kArea);
  EXPECT_EQ(plain[0]->display_priority, fv::kS52PrioGroup1);  // skin of the earth
  EXPECT_EQ(plain[0]->display_category, fv::S52DisplayCategory::kDisplayBase);
  EXPECT_FALSE(plain[0]->radar_on_top);

  // The second row is the conditional-symbology one: E3's CS registry keys off
  // exactly this, and DEPARE01 is the safety-contour procedure.
  ASSERT_EQ(plain[1]->instructions.size(), 1u);
  EXPECT_EQ(plain[1]->instructions[0].op, "CS");
  EXPECT_EQ(plain[1]->instructions[0].params[0], "DEPARE01");
}

TEST(S52PresLib, PinsTheBoylatSimplifiedLookup) {
  OPEN_LIB(lib);
  const auto rows = lib.Lookups(fv::S52LookupTable::kSimplified, "BOYLAT");
  ASSERT_FALSE(rows.empty());
  const fv::S52Lookup* r = rows.front();
  EXPECT_EQ(r->type, fv::S52ObjectType::kPoint);
  EXPECT_EQ(r->display_priority, fv::kS52PrioHazards);
  EXPECT_TRUE(r->radar_on_top);
  ASSERT_EQ(r->attributes.size(), 2u);
  EXPECT_EQ(r->attributes[0].acronym, "BOYSHP");
  EXPECT_EQ(r->attributes[0].value, "1");
  // A LIST attribute keeps its commas — this is why the value is a string.
  EXPECT_EQ(r->attributes[1].acronym, "COLOUR");
  EXPECT_EQ(r->attributes[1].value, "3,4,3");
  ASSERT_EQ(r->instructions.size(), 2u);
  EXPECT_EQ(r->instructions[0].op, "SY");
  EXPECT_EQ(r->instructions[1].op, "TE");
}

TEST(S52PresLib, LightsIsPurelyConditional) {
  OPEN_LIB(lib);
  const auto rows = lib.Lookups(fv::S52LookupTable::kSimplified, "LIGHTS");
  ASSERT_FALSE(rows.empty());
  EXPECT_TRUE(rows.back()->attributes.empty());
  ASSERT_EQ(rows.back()->instructions.size(), 1u);
  EXPECT_EQ(rows.back()->instructions[0].op, "CS");
  EXPECT_EQ(rows.back()->instructions[0].params[0], "LIGHTS05");
}

TEST(S52PresLib, UnknownAcronymYieldsNoRowsRatherThanAFallback) {
  OPEN_LIB(lib);
  EXPECT_TRUE(lib.Lookups(fv::S52LookupTable::kPaperChart, "NOSUCH").empty());
  // "######" is the PresLib's OWN unknown-object row, and it is a real lookup.
  EXPECT_FALSE(lib.Lookups(fv::S52LookupTable::kPlainBoundaries, "######").empty());
}

// One symbol pinned coordinate by coordinate: ACHARE02, the anchor. Its HPGL
// is three strokes about pivot (1267,1052) — shank, stock, flukes.
TEST(S52PresLib, PinsTheAnchorSymbolGeometry) {
  OPEN_LIB(lib);
  const fv::S52SymbolDef* def = lib.SymbolDef("ACHARE02");
  ASSERT_NE(def, nullptr);
  EXPECT_TRUE(def->vector_defined);
  EXPECT_EQ(def->pivot_x, 1267);
  EXPECT_EQ(def->pivot_y, 1052);
  EXPECT_EQ(def->color_ref, "ACHMGD");
  EXPECT_TRUE(def->has_bitmap);
  EXPECT_EQ(def->bitmap_width, 13);

  const fv::VectorSymbol* sym = lib.Symbol("ACHARE02");
  ASSERT_NE(sym, nullptr);
  ASSERT_EQ(sym->primitives.size(), 3u);

  // Shank: PU1264,789 PD1264,1291 -> x-3, y +263 .. -239 (y flipped).
  const auto& shank = sym->primitives[0];
  ASSERT_EQ(shank.points.size(), 2u);
  EXPECT_DOUBLE_EQ(shank.points[0].x, -3.0);
  EXPECT_DOUBLE_EQ(shank.points[0].y, 263.0);
  EXPECT_DOUBLE_EQ(shank.points[1].y, -239.0);
  EXPECT_TRUE(shank.has_stroke);
  EXPECT_DOUBLE_EQ(shank.stroke_width, 32.0);
  // Pen A of "ACHMGD" is the day palette's magenta.
  fv::FvColor chmgd;
  ASSERT_TRUE(lib.Color("CHMGD", &chmgd));
  EXPECT_EQ(shank.stroke_color.r, chmgd.r);
  EXPECT_EQ(shank.stroke_color.b, chmgd.b);

  // Stock (crossbar) is horizontal, flukes are the 4-point run at the bottom.
  EXPECT_EQ(sym->primitives[1].points.size(), 2u);
  EXPECT_DOUBLE_EQ(sym->primitives[1].points[0].y, sym->primitives[1].points[1].y);
  EXPECT_EQ(sym->primitives[2].points.size(), 4u);
}

TEST(S52PresLib, SymbolsAreCachedPerColourTable) {
  OPEN_LIB(lib);
  const fv::VectorSymbol* first = lib.Symbol("ACHARE02");
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(lib.Symbol("ACHARE02"), first);  // same cached object

  ASSERT_TRUE(lib.SetActiveColorTable("NIGHT").ok());
  const fv::VectorSymbol* night = lib.Symbol("ACHARE02");
  ASSERT_NE(night, nullptr);
  EXPECT_NE(night, first);  // colours are baked in, so the palette is a key
  EXPECT_EQ(night->primitives.size(), first->primitives.size());
  EXPECT_NE(night->primitives[0].stroke_color.r + night->primitives[0].stroke_color.g +
                night->primitives[0].stroke_color.b,
            first->primitives[0].stroke_color.r + first->primitives[0].stroke_color.g +
                first->primitives[0].stroke_color.b);
}

// A name can be BOTH a symbol and a line-style with different geometry; the
// caches must not collide.
TEST(S52PresLib, SymbolAndLineStyleOfTheSameNameStayApart) {
  OPEN_LIB(lib);
  const fv::S52SymbolDef* line_def = lib.LineStyleDef("ACHARE51");
  ASSERT_NE(line_def, nullptr);
  const fv::VectorSymbol* line = lib.LineStyle("ACHARE51");
  ASSERT_NE(line, nullptr);
  if (lib.SymbolDef("ACHARE51") != nullptr) {
    const fv::VectorSymbol* sym = lib.Symbol("ACHARE51");
    ASSERT_NE(sym, nullptr);
    EXPECT_NE(sym, line);
  }
  // A line style is authored as a repeating run along the path, so it is wider
  // than it is tall and carries several strokes.
  EXPECT_GT(line->primitives.size(), 3u);
  EXPECT_GT(line->max_x - line->min_x, line->max_y - line->min_y);
}

TEST(S52PresLib, PatternsCarryTheirFillTypeAndSpacing) {
  OPEN_LIB(lib);
  const fv::S52SymbolDef* p = lib.PatternDef("DIAMOND1");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->fill_type, 'L');  // linear
  EXPECT_EQ(p->spacing, 'C');    // constant
  EXPECT_NE(lib.Pattern("DIAMOND1"), nullptr);

  const fv::S52SymbolDef* staggered = lib.PatternDef("AIRARE02");
  ASSERT_NE(staggered, nullptr);
  EXPECT_EQ(staggered->fill_type, 'S');
  EXPECT_EQ(staggered->distance_min, 2000);  // 0.01 mm, pattern spacing
  EXPECT_EQ(staggered->distance_max, 10000);
}

// Corpus sweep: every vector-defined definition in the library flattens to
// geometry. This is the assertion that would catch an HPGL opcode a future
// PresLib introduces.
TEST(S52PresLib, EveryVectorDefinitionFlattensToGeometry) {
  OPEN_LIB(lib);
  const std::string xml = root + "/chartsymbols.xml";
  size_t definitions = 0, primitives = 0;
  for (const fv::S52Lookup& l : lib.lookups()) {
    for (const fv::S52Instruction& ins : l.instructions) {
      if (ins.params.empty()) continue;
      const fv::VectorSymbol* s = nullptr;
      if (ins.op == "SY") {
        s = lib.Symbol(ins.params[0]);
      } else if (ins.op == "LC") {
        s = lib.LineStyle(ins.params[0]);
      } else if (ins.op == "AP") {
        s = lib.Pattern(ins.params[0]);
      } else {
        continue;
      }
      if (s == nullptr) continue;  // raster-only definitions have no HPGL
      ++definitions;
      primitives += s->primitives.size();
      EXPECT_FALSE(s->primitives.empty()) << ins.op << "(" << ins.params[0] << ")";
    }
  }
  EXPECT_GT(definitions, 100u);
  EXPECT_GT(primitives, definitions);  // more than one stroke each, on average
}

// The integrity check across the two halves of the file — and it FAILS on the
// delivered library, which is the point: 24 names are referenced by lookups
// and defined nowhere. That is a gap in the source data (verified: the strings
// occur only inside <instruction>), so it is pinned as a known property rather
// than asserted away. E3 must draw a visible placeholder for these, never drop
// the feature (plan section 7).
TEST(S52PresLib, DanglingSymbolReferencesAreTheKnownTwentyFour) {
  OPEN_LIB(lib);
  const std::vector<std::string> missing = lib.UnresolvedSymbolReferences();
  EXPECT_EQ(missing.size(), 24u);
  // Spot-check the three the sweep found first, one of each opcode kind.
  const auto has = [&missing](const std::string& s) {
    return std::find(missing.begin(), missing.end(), s) != missing.end();
  };
  EXPECT_TRUE(has("SY(FLTHAZ02)"));
  EXPECT_TRUE(has("SY(BOYLAT55)"));
  EXPECT_TRUE(has("LC(ARCSLN01)"));

  // 23 of the 24 are absent from ALL three tables; exactly one is defined
  // under a different kind — ESSARE01 is a line-style that a lookup calls with
  // SY(). E3 can recover that one by falling back across kinds; the other 23
  // have nothing to fall back to. Several are plainly authoring typos in the
  // source library ("NEWOBJ 01", "TOWERS74|", "DGPS01DRFSTA01").
  size_t defined_under_another_kind = 0;
  for (const std::string& m : missing) {
    const std::string n = m.substr(3, m.size() - 4);
    if (lib.SymbolDef(n) != nullptr || lib.LineStyleDef(n) != nullptr ||
        lib.PatternDef(n) != nullptr) {
      ++defined_under_another_kind;
      EXPECT_EQ(m, "SY(ESSARE01)");
    }
  }
  EXPECT_EQ(defined_under_another_kind, 1u);
}

// ---------------------------------------------------------------------------
// Raster tiles (E6)
// ---------------------------------------------------------------------------

// Counts non-transparent pixels, i.e. whether a tile has any ink at all.
size_t Ink(const fv::PixelBuffer& b) {
  size_t n = 0;
  for (int y = 0; y < b.Height(); ++y)
    for (int x = 0; x < b.Width(); ++x)
      if (b.Row(y)[x * 4 + 3] != 0) ++n;
  return n;
}

TEST(S52Raster, RasterOnlySymbolCarriesATileAndNoGeometry) {
  OPEN_LIB(lib);
  // TOPMAR01 is the topmark procedure's own fallback and is defined with a
  // <bitmap> and no HPGL — before E6 it reached the canvas as a question mark.
  const fv::S52SymbolDef* def = lib.SymbolDef("TOPMAR01");
  ASSERT_NE(def, nullptr);
  ASSERT_TRUE(def->hpgl.empty()) << "TOPMAR01 is raster-only in this library";
  EXPECT_EQ(lib.Symbol("TOPMAR01"), nullptr);

  const fv::SymbolPixmap* pix = lib.SymbolBitmap("TOPMAR01");
  ASSERT_NE(pix, nullptr) << lib.raster_sheet_error();
  EXPECT_EQ(pix->tile.Width(), def->bitmap_width);
  EXPECT_EQ(pix->tile.Height(), def->bitmap_height);
  EXPECT_GT(Ink(pix->tile), 0u) << "the tile is blank — wrong sheet offset?";
  // The sheet is transparent between tiles, so a tile that is ENTIRELY ink is
  // as suspicious as a blank one for a symbol this small.
  EXPECT_LT(Ink(pix->tile),
            static_cast<size_t>(pix->tile.Width() * pix->tile.Height()));
}

TEST(S52Raster, TilesAreCachedPerColourTableAndDifferBetweenThem) {
  OPEN_LIB(lib);
  const fv::SymbolPixmap* day = lib.SymbolBitmap("TOPMAR01");
  ASSERT_NE(day, nullptr) << lib.raster_sheet_error();
  EXPECT_EQ(lib.SymbolBitmap("TOPMAR01"), day) << "same cached tile";

  ASSERT_TRUE(lib.SetActiveColorTable("NIGHT").ok());
  const fv::SymbolPixmap* night = lib.SymbolBitmap("TOPMAR01");
  ASSERT_NE(night, nullptr) << lib.raster_sheet_error();
  EXPECT_NE(night, day) << "the sheet IS the palette, so it is part of the key";
  EXPECT_EQ(night->tile.Width(), day->tile.Width());
  // The night sheet is a different rendering of the same glyph, so the tiles
  // must not be byte-identical.
  EXPECT_NE(std::memcmp(night->tile.Data(), day->tile.Data(),
                        static_cast<size_t>(day->tile.Width()) *
                            day->tile.Height() * 4),
            0);
}

// The pivot rule, both halves — the same shape as the vector-pivot test in
// s52_style_test.cpp, because it is the same delivered defect.
TEST(S52Raster, AuthoredPivotIsKeptUnlessItIsOffTheTileEntirely) {
  OPEN_LIB(lib);
  // A daymark stands ON its pivot: 17x33 tile, pivot (8,30) near the bottom.
  // That is a legitimate hanger-off and must survive untouched.
  const fv::S52SymbolDef* tri = lib.SymbolDef("DAYTRI52");
  ASSERT_NE(tri, nullptr);
  ASSERT_EQ(tri->bitmap_pivot_x, 8);
  ASSERT_EQ(tri->bitmap_pivot_y, 30);
  const fv::SymbolPixmap* tri_pix = lib.SymbolBitmap("DAYTRI52");
  ASSERT_NE(tri_pix, nullptr) << lib.raster_sheet_error();
  EXPECT_DOUBLE_EQ(tri_pix->pivot_x, 8.0);
  EXPECT_DOUBLE_EQ(tri_pix->pivot_y, 30.0);

  // ARPONE01's bitmap pivot is written -2147483648 in both axes. That is not
  // an anchor, it is a missing value, and 27 more like it are in the file.
  const fv::S52SymbolDef* arp = lib.SymbolDef("ARPONE01");
  ASSERT_NE(arp, nullptr);
  ASSERT_LT(arp->bitmap_pivot_x, -1000);
  const fv::SymbolPixmap* arp_pix = lib.SymbolBitmap("ARPONE01");
  ASSERT_NE(arp_pix, nullptr);
  EXPECT_DOUBLE_EQ(arp_pix->pivot_x, arp->bitmap_width / 2.0);
  EXPECT_DOUBLE_EQ(arp_pix->pivot_y, arp->bitmap_height / 2.0);

  // And the systematic case E5 documented on the vector side: CTNARE51's
  // bitmap pivot is (52,-17) on a 29x29 tile — 1.79 tile-widths clear of its
  // own glyph, the same fraction its vector pivot is off its own ink.
  const fv::SymbolPixmap* ctn = lib.SymbolBitmap("CTNARE51");
  ASSERT_NE(ctn, nullptr);
  EXPECT_DOUBLE_EQ(ctn->pivot_x, 14.5);
  EXPECT_DOUBLE_EQ(ctn->pivot_y, 14.5);
}

// The one family the rule above must not touch: a sounding digit is a 6x10
// glyph whose PIVOT IS ITS SLOT on the 7 px grid S-52 sets a sounding on, so
// nearly every one of them is "off its own tile" by construction. Re-centred,
// a two-digit sounding stacks its digits on top of each other and its
// decimetre stops being a subscript — which is what it did.
TEST(S52Raster, SoundingDigitsKeepTheOffTilePivotThatIsTheirLayout) {
  OPEN_LIB(lib);
  // Positions 3, 2, 1, 0, 4 read left to right on a 7 px pitch...
  const struct { const char* name; double pivot_x; } kSlots[] = {
      {"SOUNDS30", 19.0}, {"SOUNDS20", 12.0}, {"SOUNDS10", 5.0},
      {"SOUNDS00", -2.0}, {"SOUNDS40", -9.0}};
  for (const auto& s : kSlots) {
    const fv::SymbolPixmap* p = lib.SymbolBitmap(s.name);
    ASSERT_NE(p, nullptr) << s.name << ": " << lib.raster_sheet_error();
    EXPECT_DOUBLE_EQ(p->pivot_x, s.pivot_x) << s.name;
    EXPECT_DOUBLE_EQ(p->pivot_y, 4.0) << s.name;
  }
  // ...and position 5 repeats position 0's column with the pivot 4 px higher,
  // which drops the glyph half a line: the decimetre subscript.
  const fv::SymbolPixmap* sub = lib.SymbolBitmap("SOUNDS50");
  ASSERT_NE(sub, nullptr);
  EXPECT_DOUBLE_EQ(sub->pivot_x, -2.0);
  EXPECT_DOUBLE_EQ(sub->pivot_y, 0.0);
}

TEST(S52Raster, UnknownAndVectorOnlyNamesHaveNoTile) {
  OPEN_LIB(lib);
  EXPECT_EQ(lib.SymbolBitmap("NOSUCH99"), nullptr);
  // 10 of the 1093 <symbol> elements carry no <bitmap> at all.
  const fv::S52SymbolDef* nb = lib.SymbolDef("SOUNDSA1");
  if (nb != nullptr && !nb->has_bitmap)
    EXPECT_EQ(lib.SymbolBitmap("SOUNDSA1"), nullptr);
}

// Corpus sweep, the raster twin of EveryVectorDefinitionFlattensToGeometry:
// EVERY raster-only symbol the library defines must produce a tile. This is
// what turns "the buoys draw now" from an observation about one viewport into
// a property of the whole library, and it fails if a future sheet stops
// covering a <graphics-location>.
TEST(S52Raster, EveryRasterOnlyDefinitionYieldsATile) {
  OPEN_LIB(lib);
  size_t raster_only = 0, with_tile = 0, blank = 0;
  for (const fv::S52Lookup& l : lib.lookups()) {
    for (const fv::S52Instruction& ins : l.instructions) {
      if (ins.op != "SY" || ins.params.empty()) continue;
      const std::string name = ins.params[0];
      const fv::S52SymbolDef* def = lib.SymbolDef(name);
      if (def == nullptr || !def->hpgl.empty()) continue;  // vector, or absent
      ++raster_only;
      const fv::SymbolPixmap* pix = lib.SymbolBitmap(name);
      if (pix == nullptr) continue;
      ++with_tile;
      if (Ink(pix->tile) == 0) ++blank;
    }
  }
  EXPECT_GT(raster_only, 400u) << "the raster half of the library is large";
  EXPECT_EQ(with_tile, raster_only) << "a raster-only symbol with no tile "
                                       "cannot be drawn at all";
  EXPECT_EQ(blank, 0u) << "tiles cut from the wrong place in the sheet";
}

}  // namespace
