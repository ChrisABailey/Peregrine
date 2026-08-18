// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Symbol library tests (draw plan G2).
//
// Everything here is synthetic — the fixtures are PNGs this file writes into a
// temp directory. G2's OTHER acceptance test, that the extraction moved no
// pixels, is not here and cannot be: it is every pinned golden in the tree
// still passing, which is what `ctest` reports.

#include "fvkit/symbol/library.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/symbol/builtin.h"
#include "fvkit/symbol/png_library.h"
#include "fvkit/tools/png_write.h"
#include "fvkit/vector/symbol_draw.h"

namespace {

namespace fs = std::filesystem;

// A scratch directory of this test's own. Per-test rather than shared: the
// ledger's "never pin a total over a whole data directory" rule has a sibling
// here — two tests writing one directory make the counts depend on order.
class ScratchDir {
 public:
  explicit ScratchDir(const std::string& name)
      : path_(fs::temp_directory_path() / ("fv_symbols_" + name)) {
    std::error_code ec;
    fs::remove_all(path_, ec);
    fs::create_directories(path_, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  std::string str() const { return path_.string(); }
  std::string file(const std::string& n) const { return (path_ / n).string(); }

 private:
  fs::path path_;
};

// A solid RGBA tile, so a sliced sprite can be told from its neighbour.
fv::PixelBuffer Solid(int w, int h, unsigned char r, unsigned char g,
                      unsigned char b) {
  fv::PixelBuffer buf(w, h);
  for (int y = 0; y < h; ++y) {
    unsigned char* row = buf.Row(y);
    for (int x = 0; x < w; ++x) {
      row[x * 4 + 0] = r;
      row[x * 4 + 1] = g;
      row[x * 4 + 2] = b;
      row[x * 4 + 3] = 255;
    }
  }
  return buf;
}

void WriteText(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
}

const unsigned char* Px(const fv::PixelBuffer& b, int x, int y) {
  return b.Row(y) + x * 4;
}

// --- BuiltinSymbolLibrary ---------------------------------------------------

TEST(BuiltinSymbols, EveryAdvertisedIdResolvesToADisplayList) {
  fv::BuiltinSymbolLibrary lib;
  size_t n = 0;
  for (const char* const* id = fv::builtin_symbol::kAll; *id != nullptr; ++id) {
    const fv::VectorSymbol* s = lib.Symbol(*id);
    ASSERT_NE(s, nullptr) << *id;
    EXPECT_FALSE(s->primitives.empty()) << *id;
    // Bounds are informational at this seam but must not be left unset.
    // ONE axis, not both: fv.tick, fv.crosstie and fv.notch are a single
    // stroke ACROSS the path and are genuinely zero-width — they are stamped
    // at an interval the PathRun states, so they have no along-path extent to
    // have.
    EXPECT_GT((s->max_x - s->min_x) + (s->max_y - s->min_y), 0.0) << *id;
    ++n;
  }
  // 13 as authored in G2, +1 for MM4's ownship. The count is here so that an
  // id added to kAll without a display list behind it fails LOUDLY rather
  // than silently drawing nothing.
  EXPECT_EQ(n, 14u);
  EXPECT_EQ(lib.Symbol("no.such.symbol"), nullptr);
}

TEST(BuiltinSymbols, TheGridIsTheDefaultHundredthInchOne) {
  fv::BuiltinSymbolLibrary lib;
  EXPECT_DOUBLE_EQ(lib.himetric_per_symbol_pixel(), 25.4);
  // The shapes are authored 9 nominal pixels across, which is what makes a
  // builtin at scale 1 exactly a PointOverlay marker.
  const fv::VectorSymbol* sq = lib.Symbol(fv::builtin_symbol::kSquare);
  ASSERT_NE(sq, nullptr);
  EXPECT_NEAR(sq->max_x - sq->min_x, 9.0 * 25.4, 1e-9);
}

TEST(BuiltinSymbols, TheShapesShareOneCircumscribedCircle) {
  // point_overlay.cpp's own rule: a triangle and a diamond of one size_px must
  // read as the same size, so they sit on the same circle rather than filling
  // the same box.
  fv::BuiltinSymbolLibrary lib;
  const double r = 9.0 * 25.4 / 2.0;
  for (const char* id : {fv::builtin_symbol::kTriangle,
                         fv::builtin_symbol::kDiamond,
                         fv::builtin_symbol::kStar}) {
    const fv::VectorSymbol* s = lib.Symbol(id);
    ASSERT_NE(s, nullptr) << id;
    double far_out = 0.0;
    for (const fv::SymbolPrimitive& p : s->primitives)
      for (const fv::SymbolPoint& pt : p.points)
        far_out = std::max(far_out, std::hypot(pt.x, pt.y));
    EXPECT_NEAR(far_out, r, 1e-6) << id;
  }
}

TEST(BuiltinSymbols, NorthPointsAtPlusYBecauseSymbolSpaceIsYUp) {
  // A directional assertion the standing rule asks for: a hash of the arrow
  // would pass just as happily upside down.
  fv::BuiltinSymbolLibrary lib;
  const fv::VectorSymbol* n = lib.Symbol(fv::builtin_symbol::kNorthArrow);
  ASSERT_NE(n, nullptr);
  ASSERT_EQ(n->primitives.size(), 1u);
  const std::vector<fv::SymbolPoint>& pts = n->primitives[0].points;
  ASSERT_FALSE(pts.empty());
  // The tip is the single most-extreme point in y, and it is unique — the
  // shape narrows to it. The two tails are the extreme in -y.
  const fv::SymbolPoint* tip = &pts[0];
  for (const fv::SymbolPoint& p : pts)
    if (p.y > tip->y) tip = &p;
  EXPECT_NEAR(tip->x, 0.0, 1e-9);
  EXPECT_GT(tip->y, 0.0);
  EXPECT_EQ(n->max_y, tip->y);
}

TEST(BuiltinSymbols, TheCrosshairLeavesItsCentreOpen) {
  // The pixel the user is aiming at is the one thing it must not cover.
  fv::BuiltinSymbolLibrary lib;
  const fv::VectorSymbol* c = lib.Symbol(fv::builtin_symbol::kCrosshair);
  ASSERT_NE(c, nullptr);
  for (const fv::SymbolPrimitive& p : c->primitives)
    for (const fv::SymbolPoint& pt : p.points)
      EXPECT_GT(std::hypot(pt.x, pt.y), 1.0);
}

TEST(BuiltinSymbols, RecolouringRebakesEveryPrimitive) {
  // Colour is a property of the display list, so a recolour is a rebuild —
  // and the pointers handed out before it are invalidated by that rebuild,
  // which is why SetColor is documented as a construction-time knob.
  fv::BuiltinSymbolLibrary lib;
  lib.SetColor(fv::FvColor{10, 20, 30, 255});
  const fv::VectorSymbol* s = lib.Symbol(fv::builtin_symbol::kSquare);
  ASSERT_NE(s, nullptr);
  ASSERT_FALSE(s->primitives.empty());
  EXPECT_EQ(s->primitives[0].fill_color.g, 20);
  EXPECT_EQ(s->primitives[0].stroke_color.b, 30);
}

// --- PngSymbolLibrary, loose files ------------------------------------------

TEST(PngSymbolLibrary, ReadsADirectoryLazilyAndCentresThePivot) {
  ScratchDir dir("loose");
  ASSERT_TRUE(fv::WritePng(Solid(8, 4, 255, 0, 0), dir.file("red.png")).ok());
  ASSERT_TRUE(fv::WritePng(Solid(2, 2, 0, 255, 0), dir.file("green.png")).ok());
  // A non-PNG must not be catalogued.
  WriteText(dir.file("notes.txt"), "ignore me");

  fv::PngSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenDirectory(dir.str()).ok());
  EXPECT_EQ(lib.size(), 2u);
  EXPECT_EQ(lib.loaded(), 0u) << "cataloguing must not decode";
  EXPECT_EQ(lib.ids(), (std::vector<std::string>{"green", "red"}));

  const fv::SymbolPixmap* red = lib.Pixmap("red");
  ASSERT_NE(red, nullptr);
  EXPECT_EQ(red->tile.Width(), 8);
  EXPECT_EQ(red->tile.Height(), 4);
  EXPECT_DOUBLE_EQ(red->pivot_x, 4.0);
  EXPECT_DOUBLE_EQ(red->pivot_y, 2.0);
  EXPECT_DOUBLE_EQ(red->pixel_ratio, 1.0);
  EXPECT_EQ(lib.loaded(), 1u);

  // Cached: the same pointer, and nothing new decoded.
  EXPECT_EQ(lib.Pixmap("red"), red);
  EXPECT_EQ(lib.loaded(), 1u);

  EXPECT_EQ(lib.Pixmap("missing"), nullptr);
  EXPECT_EQ(lib.Symbol("red"), nullptr) << "a PNG library has no display lists";
}

TEST(PngSymbolLibrary, ASidecarOverridesTheCentredPivot) {
  ScratchDir dir("pivot");
  ASSERT_TRUE(fv::WritePng(Solid(9, 9, 0, 0, 255), dir.file("pin.png")).ok());
  WriteText(dir.file("pin.json"), R"({"pivot_x": 4, "pivot_y": 8})");

  fv::PngSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenDirectory(dir.str()).ok());
  const fv::SymbolPixmap* p = lib.Pixmap("pin");
  ASSERT_NE(p, nullptr);
  // y is DOWN in tile pixels, so 8 is the BOTTOM of a 9-px tile — a map pin's
  // point, which is the case the sidecar exists for.
  EXPECT_DOUBLE_EQ(p->pivot_x, 4.0);
  EXPECT_DOUBLE_EQ(p->pivot_y, 8.0);
}

TEST(PngSymbolLibrary, ASidecarThatWillNotParseFailsTheOpen) {
  ScratchDir dir("badsidecar");
  ASSERT_TRUE(fv::WritePng(Solid(4, 4, 1, 2, 3), dir.file("x.png")).ok());
  WriteText(dir.file("x.json"), "{ this is not json");
  fv::PngSymbolLibrary lib;
  EXPECT_FALSE(lib.OpenDirectory(dir.str()).ok());
}

TEST(PngSymbolLibrary, TheHighDpiTwinIsTakenOnlyWhenAskedFor) {
  ScratchDir dir("retina");
  ASSERT_TRUE(fv::WritePng(Solid(4, 4, 255, 0, 0), dir.file("dot.png")).ok());
  ASSERT_TRUE(
      fv::WritePng(Solid(8, 8, 0, 255, 0), dir.file("dot@2x.png")).ok());

  {
    fv::PngSymbolLibrary lib;  // default: 1x
    ASSERT_TRUE(lib.OpenDirectory(dir.str()).ok());
    EXPECT_EQ(lib.size(), 1u) << "@2x is a variant of dot, not a second id";
    const fv::SymbolPixmap* p = lib.Pixmap("dot");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->tile.Width(), 4);
    EXPECT_DOUBLE_EQ(p->pixel_ratio, 1.0);
  }
  {
    fv::PngSymbolLibrary lib;
    lib.SetPreferHighDpi(true);
    ASSERT_TRUE(lib.OpenDirectory(dir.str()).ok());
    EXPECT_EQ(lib.size(), 1u);
    const fv::SymbolPixmap* p = lib.Pixmap("dot");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->tile.Width(), 8);
    EXPECT_DOUBLE_EQ(p->pixel_ratio, 2.0);
    // Centre in TILE pixels, which is 4 for an 8-px tile — twice the 1x
    // twin's, and right, because the pivot is subtracted in tile space.
    EXPECT_DOUBLE_EQ(p->pivot_x, 4.0);
  }
}

TEST(PngSymbolLibrary, AnAtTwoXOnlySetIsStillReadable) {
  // A set that ships only @2x would otherwise catalogue as empty, which reads
  // as a missing directory rather than a missing option.
  ScratchDir dir("retinaonly");
  ASSERT_TRUE(
      fv::WritePng(Solid(6, 6, 9, 9, 9), dir.file("only@2x.png")).ok());
  fv::PngSymbolLibrary lib;  // NOT asking for high DPI
  ASSERT_TRUE(lib.OpenDirectory(dir.str()).ok());
  ASSERT_EQ(lib.size(), 1u);
  const fv::SymbolPixmap* p = lib.Pixmap("only");
  ASSERT_NE(p, nullptr);
  EXPECT_DOUBLE_EQ(p->pixel_ratio, 2.0);
}

TEST(PngSymbolLibrary, AReOpenReplacesTheLibraryRatherThanAddingToIt) {
  ScratchDir day("day"), night("night");
  ASSERT_TRUE(fv::WritePng(Solid(4, 4, 255, 0, 0), day.file("sun.png")).ok());
  ASSERT_TRUE(fv::WritePng(Solid(4, 4, 255, 0, 0), day.file("both.png")).ok());
  ASSERT_TRUE(
      fv::WritePng(Solid(4, 4, 0, 0, 255), night.file("both.png")).ok());

  fv::PngSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenDirectory(day.str()).ok());
  ASSERT_NE(lib.Pixmap("both"), nullptr);  // decoded, so it is in the cache

  ASSERT_TRUE(lib.OpenDirectory(night.str()).ok());
  EXPECT_EQ(lib.size(), 1u);
  EXPECT_EQ(lib.Pixmap("sun"), nullptr) << "the day set is gone";
  const fv::SymbolPixmap* both = lib.Pixmap("both");
  ASSERT_NE(both, nullptr);
  EXPECT_EQ(Px(both->tile, 0, 0)[2], 255) << "the cache was dropped too";
}

TEST(PngSymbolLibrary, AnAtTwoXSidecarIsAuthoredInTileSpaceAndAPlainOneIsNot) {
  // A pivot lives in TILE pixels, so a `<id>.json` written against the 1x
  // artwork has to be scaled up when the 2x file is the one that got used —
  // otherwise a pin's tip lands halfway up the pin, and only on a retina set.
  ScratchDir dir("retinapivot");
  ASSERT_TRUE(fv::WritePng(Solid(9, 9, 1, 1, 1), dir.file("a.png")).ok());
  ASSERT_TRUE(fv::WritePng(Solid(18, 18, 1, 1, 1), dir.file("a@2x.png")).ok());
  WriteText(dir.file("a.json"), R"({"pivot_x": 4, "pivot_y": 8})");
  // `b` states its own pivot in 2x space and must be taken as authored.
  ASSERT_TRUE(fv::WritePng(Solid(18, 18, 2, 2, 2), dir.file("b@2x.png")).ok());
  WriteText(dir.file("b@2x.json"), R"({"pivot_x": 9, "pivot_y": 17})");

  fv::PngSymbolLibrary lib;
  lib.SetPreferHighDpi(true);
  ASSERT_TRUE(lib.OpenDirectory(dir.str()).ok());

  const fv::SymbolPixmap* a = lib.Pixmap("a");
  ASSERT_NE(a, nullptr);
  EXPECT_DOUBLE_EQ(a->pivot_x, 8.0);
  EXPECT_DOUBLE_EQ(a->pivot_y, 16.0);

  const fv::SymbolPixmap* b = lib.Pixmap("b");
  ASSERT_NE(b, nullptr);
  EXPECT_DOUBLE_EQ(b->pivot_x, 9.0);
  EXPECT_DOUBLE_EQ(b->pivot_y, 17.0);

  // And the 1x binding takes the plain sidecar unscaled.
  fv::PngSymbolLibrary one_x;
  ASSERT_TRUE(one_x.OpenDirectory(dir.str()).ok());
  const fv::SymbolPixmap* a1 = one_x.Pixmap("a");
  ASSERT_NE(a1, nullptr);
  EXPECT_DOUBLE_EQ(a1->pivot_x, 4.0);
  EXPECT_DOUBLE_EQ(a1->pivot_y, 8.0);
}

// --- PngSymbolLibrary, sprite sheet -----------------------------------------

// A 16x8 sheet: red 0..7, green 8..15, both full height.
fv::PixelBuffer TwoSpriteSheet() {
  fv::PixelBuffer sheet(16, 8);
  for (int y = 0; y < 8; ++y) {
    unsigned char* row = sheet.Row(y);
    for (int x = 0; x < 16; ++x) {
      const bool left = x < 8;
      row[x * 4 + 0] = left ? 255 : 0;
      row[x * 4 + 1] = left ? 0 : 255;
      row[x * 4 + 2] = 0;
      row[x * 4 + 3] = 255;
    }
  }
  return sheet;
}

TEST(PngSpriteSheet, SlicesByTheIndexAndFindsItByConvention) {
  ScratchDir dir("sheet");
  ASSERT_TRUE(fv::WritePng(TwoSpriteSheet(), dir.file("sprite.png")).ok());
  WriteText(dir.file("sprite.json"), R"({
    "left":  {"x": 0, "y": 0, "width": 8, "height": 8, "pixelRatio": 1},
    "right": {"x": 8, "y": 0, "width": 8, "height": 8, "pixelRatio": 2}
  })");

  fv::PngSymbolLibrary lib;
  // No json path: the loader swaps .png for .json, which is how sprite sets
  // ship.
  ASSERT_TRUE(lib.OpenSheet(dir.file("sprite.png")).ok());
  EXPECT_EQ(lib.size(), 2u);

  const fv::SymbolPixmap* l = lib.Pixmap("left");
  ASSERT_NE(l, nullptr);
  EXPECT_EQ(l->tile.Width(), 8);
  EXPECT_EQ(Px(l->tile, 0, 0)[0], 255);
  EXPECT_EQ(Px(l->tile, 7, 7)[1], 0) << "must not bleed into the next sprite";

  const fv::SymbolPixmap* r = lib.Pixmap("right");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(Px(r->tile, 0, 0)[1], 255);
  EXPECT_DOUBLE_EQ(r->pixel_ratio, 2.0);
}

TEST(PngSpriteSheet, ASpriteOutsideTheSheetIsSkippedNotFatal) {
  // The other sprites are still good — the opposite of the ENC reader's
  // whole-exchange-set abort (ledger §2d).
  ScratchDir dir("sheetbad");
  ASSERT_TRUE(fv::WritePng(TwoSpriteSheet(), dir.file("s.png")).ok());
  WriteText(dir.file("s.json"), R"({
    "good": {"x": 0, "y": 0, "width": 8, "height": 8},
    "past": {"x": 12, "y": 0, "width": 8, "height": 8},
    "zero": {"x": 0, "y": 0, "width": 0, "height": 8}
  })");
  fv::PngSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenSheet(dir.file("s.png")).ok());
  EXPECT_EQ(lib.size(), 1u);
  EXPECT_NE(lib.Pixmap("good"), nullptr);
  EXPECT_EQ(lib.Pixmap("past"), nullptr);
}

TEST(PngSpriteSheet, AMissingOrUnparseableIndexIsAnError) {
  ScratchDir dir("sheetmissing");
  ASSERT_TRUE(fv::WritePng(TwoSpriteSheet(), dir.file("s.png")).ok());
  fv::PngSymbolLibrary lib;
  EXPECT_FALSE(lib.OpenSheet(dir.file("s.png")).ok());
  WriteText(dir.file("s.json"), "[1, 2, 3]");
  EXPECT_FALSE(lib.OpenSheet(dir.file("s.png")).ok());
}

TEST(PngSpriteSheet, AFailedReOpenLeavesTheLibraryAsItWas) {
  ScratchDir dir("sheetkeep");
  ASSERT_TRUE(fv::WritePng(TwoSpriteSheet(), dir.file("s.png")).ok());
  WriteText(dir.file("s.json"), R"({"a": {"x":0,"y":0,"width":8,"height":8}})");
  fv::PngSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenSheet(dir.file("s.png")).ok());
  ASSERT_NE(lib.Pixmap("a"), nullptr);

  // A parseable index over an image that will not read: the entries must not
  // land, or they would index a sheet that was never loaded.
  WriteText(dir.file("bad.json"), R"({"b": {"x":0,"y":0,"width":8,"height":8}})");
  EXPECT_FALSE(lib.OpenSheet(dir.file("nosuch.png"), dir.file("bad.json")).ok());
  EXPECT_EQ(lib.size(), 1u);
  EXPECT_NE(lib.Pixmap("a"), nullptr);
  EXPECT_EQ(lib.Pixmap("b"), nullptr);
}

TEST(BuiltinSymbols, ARecolourKeepsThePointersItHandedOutValid) {
  // ISymbolLibrary promises the pointer stays valid until the library dies,
  // and the documented usage is resolve-once-stamp-many. So a re-bake rewrites
  // the display lists in place; it must not drop them.
  fv::BuiltinSymbolLibrary lib;
  const fv::VectorSymbol* s = lib.Symbol(fv::builtin_symbol::kSquare);
  ASSERT_NE(s, nullptr);
  lib.SetColor(fv::FvColor{7, 8, 9, 255});
  lib.SetStrokeWidth(3.0);
  EXPECT_EQ(lib.Symbol(fv::builtin_symbol::kSquare), s) << "same node";
  ASSERT_FALSE(s->primitives.empty());
  EXPECT_EQ(s->primitives[0].fill_color.r, 7) << "and it followed the recolour";
  EXPECT_DOUBLE_EQ(s->primitives[0].stroke_width, 3.0 * 25.4);
}

// --- CompositeSymbolLibrary -------------------------------------------------

// Answers one id, in one form. Enough to pin the ordering rules.
class OneSymbol : public fv::ISymbolLibrary {
 public:
  OneSymbol(std::string id, bool as_vector, double himetric)
      : id_(std::move(id)), himetric_(himetric) {
    if (as_vector) {
      fv::SymbolPrimitive p;
      p.points = {{0, 0}, {1, 1}};
      vec_.primitives.push_back(p);
    } else {
      pix_.tile = Solid(2, 2, 1, 1, 1);
    }
  }
  const fv::VectorSymbol* Symbol(const std::string& id) override {
    ++asked;
    return id == id_ && !vec_.primitives.empty() ? &vec_ : nullptr;
  }
  const fv::SymbolPixmap* Pixmap(const std::string& id) override {
    return id == id_ && !pix_.tile.Empty() ? &pix_ : nullptr;
  }
  double himetric_per_symbol_pixel() const override { return himetric_; }
  int asked = 0;

 private:
  std::string id_;
  double himetric_;
  fv::VectorSymbol vec_;
  fv::SymbolPixmap pix_;
};

TEST(CompositeSymbolLibrary, FirstMemberThatAnswersWins) {
  OneSymbol mine("dot", true, 25.4), theirs("dot", true, 25.4);
  fv::CompositeSymbolLibrary comp;
  comp.Add(&mine);
  comp.Add(&theirs);
  EXPECT_EQ(comp.size(), 2u);
  EXPECT_EQ(comp.Symbol("dot"), mine.Symbol("dot"));
  EXPECT_EQ(theirs.asked, 0) << "the second member is not consulted on a hit";
  EXPECT_EQ(comp.Symbol("other"), nullptr);
}

TEST(CompositeSymbolLibrary, APixmapOnlyMemberDoesNotShadowALaterDisplayList) {
  // The two accessors resolve INDEPENDENTLY. A library that has only the
  // pixmap form of an id must not hide a later library's vector form, or
  // adding a sprite sheet in front would silently derasterize the chart.
  OneSymbol raster("buoy", /*as_vector=*/false, 25.4);
  OneSymbol vector("buoy", /*as_vector=*/true, 25.4);
  fv::CompositeSymbolLibrary comp;
  comp.Add(&raster);
  comp.Add(&vector);
  EXPECT_EQ(comp.Symbol("buoy"), vector.Symbol("buoy"));
  EXPECT_NE(comp.Pixmap("buoy"), nullptr);
  // And ResolveSymbol, which is what the drawer actually calls, takes the
  // display list — vector first is the seam's rule.
  fv::ResolvedSymbol r = fv::ResolveSymbol(&comp, "buoy");
  EXPECT_NE(r.vec, nullptr);
  EXPECT_EQ(r.pix, nullptr);
}

TEST(CompositeSymbolLibrary, TheUnitComesFromTheFirstMemberUnlessOverridden) {
  OneSymbol geosym("a", true, 25.4), s52("b", true, 32.0);
  fv::CompositeSymbolLibrary comp;
  EXPECT_DOUBLE_EQ(comp.himetric_per_symbol_pixel(), 25.4) << "empty default";
  comp.Add(&s52);
  comp.Add(&geosym);
  EXPECT_DOUBLE_EQ(comp.himetric_per_symbol_pixel(), 32.0);
  comp.set_himetric_per_symbol_pixel(10.0);
  EXPECT_DOUBLE_EQ(comp.himetric_per_symbol_pixel(), 10.0);
  comp.set_himetric_per_symbol_pixel(0.0);  // back to the inherited one
  EXPECT_DOUBLE_EQ(comp.himetric_per_symbol_pixel(), 32.0);
}

// --- the extracted drawing --------------------------------------------------

TEST(SymbolDraw, ABuiltinReachesTheCanvasThroughTheExtractedDrawer) {
  // The point of G2: an overlay can now stamp a symbol with no renderer, no
  // style engine and no vector source in sight.
  fv::BuiltinSymbolLibrary lib;
  lib.SetColor(fv::FvColor{0, 0, 255, 255});
  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});

  fv::ResolvedSymbol sym = fv::ResolveSymbol(&lib, fv::builtin_symbol::kSquare);
  ASSERT_TRUE(sym.drawable());
  fv::InkBox ink;
  ASSERT_TRUE(fv::DrawResolvedSymbol(&canvas, sym, 32.0, 32.0,
                                     1.0 / lib.himetric_per_symbol_pixel(),
                                     1.0, 0.0, &ink));
  EXPECT_EQ(Px(canvas.Buffer(), 32, 32)[2], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 32, 10)[2], 0);
  // The ink box is the square it drew, not the anchor pixel. 11 rather than 9
  // for a 9-nominal-pixel symbol, and that is InkBox::ToRect's pre-existing
  // arithmetic rather than a G2 change: the corners land on 27.5 and 36.5, and
  // ToRect floors the low edge, ceils the high one and then adds an inclusive
  // pixel — so a box is up to two pixels more generous than the ink. It errs
  // the way a pick box should.
  const fv::PixelRect box = ink.ToRect(0.0);
  EXPECT_EQ(box.width, 11);
  EXPECT_EQ(box.height, 11);
  EXPECT_EQ(box.x, 27);
  EXPECT_EQ(box.y, 27);
}

TEST(SymbolDraw, PixelRatioMakesATwoXTileComeOutTheSameSize) {
  // The one behaviour G2 adds. A 2x tile blits at half scale, so its footprint
  // matches its 1x twin's — otherwise a retina sprite sheet would double every
  // icon on the map.
  fv::SymbolPixmap one;
  one.tile = Solid(8, 8, 255, 0, 0);
  one.pivot_x = one.pivot_y = 4.0;

  fv::SymbolPixmap two;
  two.tile = Solid(16, 16, 255, 0, 0);
  two.pivot_x = two.pivot_y = 8.0;
  two.pixel_ratio = 2.0;

  auto footprint = [](const fv::SymbolPixmap& p) {
    fv::CpuCanvas canvas(64, 64);
    canvas.Clear(fv::FvColor{0, 0, 0, 255});
    fv::ResolvedSymbol r;
    r.pix = &p;
    fv::InkBox ink;
    EXPECT_TRUE(fv::DrawResolvedSymbol(&canvas, r, 32.0, 32.0, 1.0 / 25.4, 1.0,
                                       0.0, &ink));
    // Count what actually inked rather than trusting the box: the 2x path goes
    // through the resampler and the 1x path does not.
    int lit = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x)
        if (Px(canvas.Buffer(), x, y)[0] == 255) ++lit;
    return lit;
  };
  EXPECT_EQ(footprint(one), 64);
  EXPECT_EQ(footprint(two), 64);
}

TEST(SymbolDraw, ADefaultRatioIsTheIdentityAndTouchesNothing) {
  // Why the goldens did not move: pixel_ratio defaults to 1.0 and the division
  // is exact, so a pre-G2 pixmap takes the same `plain` blit it always did.
  fv::SymbolPixmap p;
  p.tile = Solid(5, 5, 0, 0, 255);
  p.pivot_x = p.pivot_y = 2.0;
  EXPECT_DOUBLE_EQ(p.pixel_ratio, 1.0);

  fv::CpuCanvas canvas(32, 32);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::ResolvedSymbol r;
  r.pix = &p;
  ASSERT_TRUE(
      fv::DrawResolvedSymbol(&canvas, r, 16.0, 16.0, 1.0 / 25.4, 1.0, 0.0,
                             nullptr));
  // A straight blit lands the pivot exactly on the anchor and the tile's own
  // edges reach the canvas untouched.
  EXPECT_EQ(Px(canvas.Buffer(), 16, 16)[2], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 14, 14)[2], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 18, 18)[2], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 13, 16)[2], 0);
  EXPECT_EQ(Px(canvas.Buffer(), 19, 16)[2], 0);
}

TEST(SymbolDraw, ResolveTakesTheDisplayListOverTheTileAndSkipsEmptyOnes) {
  // An empty display list is not an answer — a style engine that caches misses
  // hands one back, and treating it as a hit loses the pixmap form.
  class Both : public fv::ISymbolLibrary {
   public:
    const fv::VectorSymbol* Symbol(const std::string&) override {
      return &empty_;  // present but with no primitives
    }
    const fv::SymbolPixmap* Pixmap(const std::string&) override {
      return &pix_;
    }
    fv::VectorSymbol empty_;
    fv::SymbolPixmap pix_{Solid(2, 2, 1, 1, 1), 1.0, 1.0, 1.0};
  } lib;
  fv::ResolvedSymbol r = fv::ResolveSymbol(&lib, "x");
  EXPECT_EQ(r.vec, nullptr);
  EXPECT_NE(r.pix, nullptr);
  EXPECT_TRUE(r.drawable());

  EXPECT_FALSE(fv::ResolveSymbol(nullptr, "x").drawable());
}

}  // namespace
