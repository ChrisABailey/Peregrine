// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// CgmSymbolLibrary tests (draw plan G2).
//
// The real GeoSym corpus, through the ISymbolLibrary seam instead of through a
// style engine. Skipped without FVW_TESTDATA_DIR, like every other test in
// this directory.
//
// NOTE what these tests deliberately do NOT do: pin a symbol's geometry. The
// conversion is ToVectorSymbol, which G2 only MOVED — cgm_symbol_test.cpp and
// geosym_style_test.cpp already pin it, and re-pinning it here would make the
// extraction look tested by something it is not. What is tested here is the
// library: cataloguing, laziness, the id rule, and that the two paths to a
// symbol agree.

#include "fv_cgm_library.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "fv_geosym_style.h"

namespace fs = std::filesystem;

namespace {

std::string GraphicsDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  std::string p = std::string(d) + "/GeoSymbol/Graphics";
  if (!fs::is_directory(p)) return {};
  return p;
}

size_t CountCgm(const std::string& dir) {
  size_t n = 0;
  for (const auto& de : fs::directory_iterator(dir)) {
    if (!de.is_regular_file()) continue;
    const std::string ext = de.path().extension().string();
    if (ext == ".cgm" || ext == ".CGM") ++n;
  }
  return n;
}

TEST(CgmSymbolLibrary, ADirectoryThatIsNotThereIsAnError) {
  fv::CgmSymbolLibrary lib;
  EXPECT_FALSE(lib.OpenDirectory("/no/such/place").ok());
  EXPECT_EQ(lib.Symbol("0051"), nullptr);
}

TEST(CgmSymbolLibrary, TheUnitIsGeoSymsOwnHundredthInchGrid) {
  fv::CgmSymbolLibrary lib;
  EXPECT_DOUBLE_EQ(lib.himetric_per_symbol_pixel(), 25.4);
}

TEST(CgmSymbolLibrary, CataloguesTheCorpusWithoutParsingIt) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP() << "FVW_TESTDATA_DIR/GeoSymbol/Graphics";

  fv::CgmSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenDirectory(dir).ok());
  // Derived, not a literal — the sample tree grows (standing rule: never pin a
  // total over a whole data directory).
  EXPECT_EQ(lib.size(), CountCgm(dir));
  EXPECT_GT(lib.size(), 100u);
  EXPECT_EQ(lib.loaded(), 0u) << "cataloguing must not parse";

  const std::vector<std::string> ids = lib.ids();
  ASSERT_EQ(ids.size(), lib.size());
  for (const std::string& id : ids)
    EXPECT_EQ(id.find(".cgm"), std::string::npos) << "the id is the stem";
}

TEST(CgmSymbolLibrary, ResolvesASymbolLazilyAndCachesIt) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP() << "FVW_TESTDATA_DIR/GeoSymbol/Graphics";

  fv::CgmSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenDirectory(dir).ok());

  // 0051 is the black dot GeoSym falls back to when nothing matches, so it is
  // in every delivered product.
  const fv::VectorSymbol* dot = lib.Symbol("0051");
  ASSERT_NE(dot, nullptr);
  EXPECT_FALSE(dot->primitives.empty());
  EXPECT_EQ(lib.loaded(), 1u);

  // Same pointer, nothing new parsed — ISymbolLibrary's lifetime promise.
  EXPECT_EQ(lib.Symbol("0051"), dot);
  EXPECT_EQ(lib.loaded(), 1u);

  EXPECT_EQ(lib.Symbol("nosuchsymbol"), nullptr);
}

TEST(CgmSymbolLibrary, AnIdSpelledAsAFilenameResolvesToTheSameSymbol) {
  // A SAMI point-symbol element names its symbol as a FILE ("5010.cgm") while
  // every table names it as a bare number, so the library accepts both — the
  // same allowance fv_geosym_style.cpp's StripCgmExtension makes.
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP() << "FVW_TESTDATA_DIR/GeoSymbol/Graphics";

  fv::CgmSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenDirectory(dir).ok());
  const fv::VectorSymbol* bare = lib.Symbol("0051");
  ASSERT_NE(bare, nullptr);
  EXPECT_EQ(lib.Symbol("0051.cgm"), bare);
  EXPECT_EQ(lib.loaded(), 1u) << "one cache entry, not two";
}

TEST(CgmSymbolLibrary, AgreesWithTheStyleEngineOnTheSameSymbol) {
  // The two paths to a GeoSym symbol — through the style engine's rule tables,
  // and straight out of the library — run the SAME ToVectorSymbol over the
  // SAME file, and this is what says so.
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) GTEST_SKIP() << "FVW_TESTDATA_DIR";
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP() << "FVW_TESTDATA_DIR/GeoSymbol/Graphics";

  fv::GeoSymStyleEngine eng;
  if (!eng.Open(d).ok()) GTEST_SKIP() << "no GeoSym product under " << d;
  const fv::VectorSymbol* from_engine = eng.Symbol("0051");
  ASSERT_NE(from_engine, nullptr);

  fv::CgmSymbolLibrary lib;
  ASSERT_TRUE(lib.OpenDirectory(dir).ok());
  const fv::VectorSymbol* from_lib = lib.Symbol("0051");
  ASSERT_NE(from_lib, nullptr);

  ASSERT_EQ(from_lib->primitives.size(), from_engine->primitives.size());
  EXPECT_DOUBLE_EQ(from_lib->min_x, from_engine->min_x);
  EXPECT_DOUBLE_EQ(from_lib->max_y, from_engine->max_y);
  for (size_t i = 0; i < from_lib->primitives.size(); ++i) {
    const fv::SymbolPrimitive& a = from_lib->primitives[i];
    const fv::SymbolPrimitive& b = from_engine->primitives[i];
    EXPECT_EQ(static_cast<int>(a.type), static_cast<int>(b.type)) << i;
    ASSERT_EQ(a.points.size(), b.points.size()) << i;
    for (size_t j = 0; j < a.points.size(); ++j) {
      EXPECT_DOUBLE_EQ(a.points[j].x, b.points[j].x) << i << "/" << j;
      EXPECT_DOUBLE_EQ(a.points[j].y, b.points[j].y) << i << "/" << j;
    }
    EXPECT_EQ(a.fill_color.r, b.fill_color.r) << i;
    EXPECT_EQ(a.stroke_color.b, b.stroke_color.b) << i;
    EXPECT_DOUBLE_EQ(a.stroke_width, b.stroke_width) << i;
  }
}

}  // namespace
