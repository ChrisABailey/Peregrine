// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::CgmSymbol tests (vpf-geosym plan phase V4).
//
// Two layers:
//   1. Error paths — always run, no data needed.
//   2. Real data via FVW_TESTDATA_DIR: the GeoSym symbol graphics under
//      TestData/GeoSymbol/Graphics. Two symbols are pinned element-by-element
//      (0003.cgm is the useful one: it carries polygon sets, polylines,
//      elliptical arcs AND a circle), then the whole corpus is swept for
//      structural invariants.
//
// The sweep counts .cgm files on disk instead of asserting a literal — the
// sample tree grows, and a hardcoded corpus size only produces spurious
// failures (same reason the DTED/GeoTIFF inventory assertions are derived).
//
// NOTE: the GeoSym corpus contains NO text elements — GeoSym symbols are pure
// geometry, and symbol text comes from the SymText/draw_text layer, which is
// GDI-coupled and stays Windows-only until phase V5. CgmSymbol still flattens
// ELEMTYPE_TEXT (exercised by the parser, not by this data).

#include "fv_cgm_symbol.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string GraphicsDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  std::string p = std::string(d) + "/GeoSymbol/Graphics";
  if (!fs::is_directory(p)) return {};
  return p;
}

std::vector<std::string> CgmFilesOnDisk(const std::string& root) {
  std::vector<std::string> out;
  for (const auto& ent : fs::directory_iterator(root)) {
    if (!ent.is_regular_file()) continue;
    std::string ext = ent.path().extension().string();
    for (char& c : ext) c = static_cast<char>(tolower(c));
    if (ext == ".cgm") out.push_back(ent.path().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// ---------------------------------------------------------------------------
// Error paths (no data required)
// ---------------------------------------------------------------------------

TEST(CgmSymbolErrors, MissingFile) {
  fv::CgmSymbol s;
  fv::Status st = s.LoadFile("/nonexistent/nope.cgm");
  EXPECT_EQ(st.code, fv::kNotFound) << st.message;
  EXPECT_TRUE(s.elements().empty());
}

TEST(CgmSymbolErrors, EmptyBuffer) {
  fv::CgmSymbol s;
  EXPECT_EQ(s.LoadBuffer(nullptr, 0).code, fv::kInvalidArg);
  const char byte = 0;
  EXPECT_EQ(s.LoadBuffer(&byte, 0).code, fv::kInvalidArg);
}

// LIMITATION (documented, not fixed — bit-faithful rule): CCGMFile answers an
// UNRECOGNIZED CGM opcode with ASSERT(false) then `break`. Under MFC, ASSERT
// compiles out of the shipping Release build, so Windows skips the opcode and
// parses on; our ASSERT maps to <cassert>, so a debug build ABORTS. Feeding
// arbitrary bytes therefore aborts here but not on shipping Windows. Do not
// hand this parser untrusted input in a debug build.
//
// Truncation — the realistic corruption mode, and the one the parser actually
// has an error code for — IS handled: the decoder runs out of buffer and
// returns E_CGM_UNEXPECTED_EOF rather than reading past the end.
TEST(CgmSymbolErrors, TruncatedStreamReportsEofWithoutOverrun) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP();

  std::vector<char> full;
  {
    fs::path p = fs::path(dir) / "0003.cgm";
    const size_t n = static_cast<size_t>(fs::file_size(p));
    full.resize(n);
    FILE* f = fopen(p.string().c_str(), "rb");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(fread(full.data(), 1, n, f), n);
    fclose(f);
  }
  ASSERT_GT(full.size(), 64u);

  for (size_t cut : {full.size() * 3 / 4, full.size() / 2, full.size() / 4,
                     size_t(32), size_t(8)}) {
    fv::CgmSymbol s;
    const fv::Status st = s.LoadBuffer(full.data(), cut);
    EXPECT_FALSE(st.ok()) << "truncation to " << cut << " should not succeed";
    EXPECT_EQ(st.code, fv::kIoError) << "cut=" << cut << ": " << st.message;
    EXPECT_TRUE(s.elements().empty()) << "cut=" << cut;
  }
}

// ---------------------------------------------------------------------------
// Pinned symbols
// ---------------------------------------------------------------------------

// 0003.cgm is the richest small symbol in the corpus: 2 polygon sets, 3
// polylines, 9 elliptical arcs and 1 circle, in that file order.
TEST(CgmSymbolReal, PinnedSymbol0003AllGeometryKinds) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP();

  fv::CgmSymbol s;
  ASSERT_TRUE(s.LoadFile(dir + "/0003.cgm").ok());
  ASSERT_EQ(s.elements().size(), 15u);
  EXPECT_EQ(s.name(), "0003.cgm");

  // VDC extent accumulated by the parser.
  EXPECT_EQ(s.bounds().left, -175);
  EXPECT_EQ(s.bounds().top, -628);
  EXPECT_EQ(s.bounds().right, 304);
  EXPECT_EQ(s.bounds().bottom, 58);

  using T = fv::CgmElementType;
  const T want[15] = {T::kPolygonSet,     T::kPolygonSet,     T::kPolyline,
                      T::kEllipticalArc,  T::kEllipticalArc,  T::kEllipticalArc,
                      T::kEllipticalArc,  T::kEllipticalArc,  T::kEllipticalArc,
                      T::kEllipticalArc,  T::kEllipticalArc,  T::kEllipticalArc,
                      T::kEllipse,        T::kPolyline,       T::kPolyline};
  for (size_t i = 0; i < 15; ++i)
    EXPECT_EQ(s.elements()[i].type, want[i]) << "element " << i;

  // [0] polygon set: 4 vertices, one edge-out flag each.
  const fv::CgmElement& set0 = s.elements()[0];
  ASSERT_EQ(set0.vertices.size(), 4u);
  EXPECT_EQ(set0.vertex_flags.size(), set0.vertices.size());
  EXPECT_EQ(set0.vertices[0].x, -47);
  EXPECT_EQ(set0.vertices[0].y, 16);
  EXPECT_EQ(set0.fill_style, 1);  // CGM_INTSTYLE_SOLID

  // [2] polyline: 2 points, width 18.
  const fv::CgmElement& line = s.elements()[2];
  ASSERT_EQ(line.vertices.size(), 2u);
  EXPECT_EQ(line.vertices[0].x, -75);
  EXPECT_EQ(line.vertices[0].y, -415);
  EXPECT_EQ(line.line_width, 18);
  EXPECT_EQ(line.line_type, 1);  // CGM_LINE_TYPE_SOLID

  // [3] elliptical arc: center + conjugate radii, reduced major/minor.
  const fv::CgmElement& arc = s.elements()[3];
  EXPECT_EQ(arc.center.x, 0);
  EXPECT_EQ(arc.center.y, -474);
  EXPECT_EQ(arc.radius1.x, 0);
  EXPECT_EQ(arc.radius1.y, -72);
  EXPECT_EQ(arc.radius2.x, -114);
  EXPECT_EQ(arc.radius2.y, 0);
  EXPECT_EQ(arc.major_radius, 114);
  EXPECT_EQ(arc.minor_radius, 72);
  EXPECT_EQ(arc.fill_style, 0);  // CGM_INTSTYLE_HOLLOW — arcs are stroked
  EXPECT_TRUE(arc.vertices.empty());

  // [12] the circle: equal radii, axis-aligned conjugate diameters.
  const fv::CgmElement& circle = s.elements()[12];
  EXPECT_EQ(circle.center.x, 0);
  EXPECT_EQ(circle.center.y, 0);
  EXPECT_EQ(circle.radius1.x, 49);
  EXPECT_EQ(circle.radius1.y, 0);
  EXPECT_EQ(circle.radius2.x, 0);
  EXPECT_EQ(circle.radius2.y, 49);
  EXPECT_EQ(circle.major_radius, 49);
  EXPECT_EQ(circle.minor_radius, 49);
}

// 0001.cgm is the simplest useful case: two filled polygons, no strokes.
TEST(CgmSymbolReal, PinnedSymbol0001Polygons) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP();

  fv::CgmSymbol s;
  ASSERT_TRUE(s.LoadFile(dir + "/0001.cgm").ok());
  ASSERT_EQ(s.elements().size(), 2u);
  EXPECT_EQ(s.elements()[0].type, fv::CgmElementType::kPolygon);
  EXPECT_EQ(s.elements()[1].type, fv::CgmElementType::kPolygon);
  EXPECT_EQ(s.elements()[0].vertices.size(), 37u);
  EXPECT_EQ(s.elements()[1].vertices.size(), 24u);
  EXPECT_EQ(s.elements()[0].vertices[0].x, -148);
  EXPECT_EQ(s.elements()[0].vertices[0].y, 130);

  // COLORREF is 0x00BBGGRR, so this fill is RGB(0xA3, 0x7B, 0x3A).
  EXPECT_EQ(s.elements()[0].fill_color, 0x3A7BA3u);
  EXPECT_EQ(s.elements()[0].line_color, 0x000000u);

  EXPECT_EQ(s.bounds().left, -177);
  EXPECT_EQ(s.bounds().right, 197);
}

// Plain polygons carry no edge-out flags; polygon SETS carry one per vertex.
TEST(CgmSymbolReal, OnlyPolygonSetsCarryVertexFlags) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP();

  fv::CgmSymbol s;
  ASSERT_TRUE(s.LoadFile(dir + "/0001.cgm").ok());
  for (const auto& e : s.elements()) EXPECT_TRUE(e.vertex_flags.empty());
}

TEST(CgmSymbolReal, ParseIsDeterministic) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP();

  fv::CgmSymbol a, b;
  ASSERT_TRUE(a.LoadFile(dir + "/0003.cgm").ok());
  ASSERT_TRUE(b.LoadFile(dir + "/0003.cgm").ok());
  ASSERT_EQ(a.elements().size(), b.elements().size());
  for (size_t i = 0; i < a.elements().size(); ++i) {
    EXPECT_EQ(a.elements()[i].type, b.elements()[i].type) << i;
    EXPECT_EQ(a.elements()[i].vertices.size(), b.elements()[i].vertices.size())
        << i;
    EXPECT_EQ(a.elements()[i].line_color, b.elements()[i].line_color) << i;
  }
  EXPECT_EQ(a.bounds().left, b.bounds().left);
  EXPECT_EQ(a.bounds().right, b.bounds().right);
}

// Reloading into the SAME object must replace, not append.
TEST(CgmSymbolReal, ReloadReplacesPreviousContent) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP();

  fv::CgmSymbol s;
  ASSERT_TRUE(s.LoadFile(dir + "/0003.cgm").ok());
  ASSERT_EQ(s.elements().size(), 15u);
  ASSERT_TRUE(s.LoadFile(dir + "/0001.cgm").ok());
  EXPECT_EQ(s.elements().size(), 2u);
  EXPECT_EQ(s.name(), "0001.cgm");

  // A failed load must also leave nothing behind.
  EXPECT_FALSE(s.LoadFile(dir + "/definitely-not-here.cgm").ok());
  EXPECT_TRUE(s.elements().empty());
}

// ---------------------------------------------------------------------------
// Whole-corpus structural sweep
// ---------------------------------------------------------------------------

TEST(CgmSymbolReal, EverySymbolParsesWithSaneGeometry) {
  const std::string dir = GraphicsDir();
  if (dir.empty()) GTEST_SKIP();

  const std::vector<std::string> files = CgmFilesOnDisk(dir);
  ASSERT_GT(files.size(), 0u) << "no .cgm files under " << dir;

  size_t total_elements = 0;
  for (const std::string& path : files) {
    fv::CgmSymbol s;
    const fv::Status st = s.LoadFile(path);
    ASSERT_TRUE(st.ok()) << path << ": " << st.message;
    EXPECT_FALSE(s.elements().empty()) << path << " parsed to nothing";
    EXPECT_FALSE(s.bounds().Empty()) << path << " has a degenerate extent";

    for (const fv::CgmElement& e : s.elements()) {
      EXPECT_NE(e.type, fv::CgmElementType::kUnknown) << path;
      switch (e.type) {
        case fv::CgmElementType::kPolyline:
        case fv::CgmElementType::kPolygon:
          // A stroke or a face needs at least two points.
          EXPECT_GE(e.vertices.size(), 2u) << path;
          EXPECT_TRUE(e.vertex_flags.empty()) << path;
          break;
        case fv::CgmElementType::kPolygonSet:
          EXPECT_GE(e.vertices.size(), 2u) << path;
          EXPECT_EQ(e.vertex_flags.size(), e.vertices.size()) << path;
          break;
        case fv::CgmElementType::kEllipse:
        case fv::CgmElementType::kEllipticalArc:
          // The parser reduces conjugate diameters to a major/minor pair.
          EXPECT_GE(e.major_radius, e.minor_radius) << path;
          EXPECT_GT(e.major_radius, 0) << path;
          EXPECT_TRUE(e.vertices.empty()) << path;
          break;
        case fv::CgmElementType::kText:
          EXPECT_GT(e.char_height, 0) << path;
          break;
        default:
          break;
      }
      total_elements += 1;
    }
  }
  // Sanity floor: the corpus is geometry-dense, so every symbol averages
  // several elements. Catches a regression that parses headers but drops
  // element bodies (which would still satisfy every per-element check).
  EXPECT_GT(total_elements, files.size() * 2);
}

}  // namespace
