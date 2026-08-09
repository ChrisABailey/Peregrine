// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// OSM end-to-end render (phase O3) — the Atlanta golden, the OSM twin of
// V5b's Nantucket harbour and E3a's Charleston.
//
// It is the first test in this module that puts ALL of OSM together: pyramid
// -> OsmVectorSource -> OsmStyleEngine (reference style) -> VectorRenderer ->
// CpuCanvas. Everything before it pinned one half at a time, which is exactly
// how a source and a style engine can each be right about zoom and still
// disagree with each other on screen.
//
// The golden pins an FNV-1a hash and writes a PNG for eyeballing, the
// convention canvas_test.cpp set. Labels are OFF: label glyphs come from the
// host font and would make the hash machine-dependent.
//
// THE VIEWPORT IS SET BY PHYSICAL SCALE, which is the one place this golden
// departs from V5b's "pin a resolution, not a scale" rule — deliberately, and
// because of what OSM is. MapProjection::Scale() reports 0 in resolution mode,
// and a scale of 0 is exactly the input that makes both halves of OSM fall
// back to a default (max_zoom in the source, scaleless_zoom in the style): the
// golden would render, and would test nothing about the zoom<->scale relation
// this module is built on. SetPhysicalScale is also what PythonView uses, so
// this is the app's own path.

#include <gtest/gtest.h>
#include <png.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fv_osm_style.h"
#include "fv_osm_vector_source.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/proj.h"
#include "fvkit/vector/renderer.h"

namespace fs = std::filesystem;

namespace {

std::string MbtilesPath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/OSM/mbtiles/us-south.mbtiles";
  return fs::is_regular_file(p) ? p : std::string();
}

std::string StylePath() {
  const char* d = getenv("FVW_OSM_STYLE_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/peregrine-osm.json";
  return fs::is_regular_file(p) ? p : std::string();
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

size_t DistinctColors(const fv::PixelBuffer& b) {
  std::vector<uint32_t> seen;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* p = row + x * 4;
      const uint32_t c = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
      bool found = false;
      for (uint32_t s : seen)
        if (s == c) { found = true; break; }
      if (!found) {
        seen.push_back(c);
        if (seen.size() > 4096) return seen.size();
      }
    }
  }
  return seen.size();
}

// Downtown Atlanta: the interstate junction where I-75/85 crosses the
// downtown connector, plus Centennial Park, the rail corridor and the river
// of city blocks around them — dense enough that a broken draw order or a
// missing layer is obvious in the picture, not just in the hash.
constexpr double kAtlantaLat = 33.7550;
constexpr double kAtlantaLon = -84.3900;

struct Scene {
  std::shared_ptr<fv::OsmVectorSource> source;
  std::shared_ptr<fv::OsmStyleEngine> style;
};

Scene MakeScene(const std::string& mb_path, const std::string& style_path) {
  Scene s;
  s.source = std::make_shared<fv::OsmVectorSource>();
  if (!s.source->Open(mb_path).ok()) return {};
  s.style = std::make_shared<fv::OsmStyleEngine>();
  if (!s.style->LoadFile(style_path).ok()) return {};
  // The pair that must agree, and the whole reason O2 gave the style engine a
  // reference latitude: the source derives a zoom from (scale, latitude,
  // pitch) and so does the style. Give both the same two numbers or the
  // style's minzoom switches layers on at a different scale than the tiles
  // they style. This is the wiring PythonView does, tested here.
  s.style->SetReferenceLatitude(kAtlantaLat);
  s.style->SetDisplayMmPerPixel(s.source->display_mm_per_pixel());
  return s;
}

}  // namespace

#define SKIP_WITHOUT_DATA()                              \
  const std::string mb_path = MbtilesPath();             \
  const std::string style_path = StylePath();            \
  if (mb_path.empty()) GTEST_SKIP() << "no OSM mbtiles"; \
  if (style_path.empty()) GTEST_SKIP() << "no OSM reference style"

// Pinned 2026-08-08 (O3), after looking at the PNG: downtown Atlanta comes out
// as a street map — the downtown connector in orange running north-east past
// the stadium and Centennial Park's green, the rail corridor cutting across
// it, the block grid drawn as building fills with the dashed footway network
// threading between them, and every road casing below every road fill (the O2
// draw-order rule, here across thousands of features).
// 0 = probe mode (prints the hash, asserts nothing).
constexpr uint64_t kHashAtlanta = 0x3f49f779d29c7887ull;

TEST(OsmRender, AtlantaViewport) {
  SKIP_WITHOUT_DATA();
  Scene s = MakeScene(mb_path, style_path);
  ASSERT_TRUE(s.source && s.style);

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(512, 512).ok());
  ASSERT_TRUE(proj.SetCenter(fv::GeoPoint{kAtlantaLat, kAtlantaLon}).ok());
  // 1:25,000 at the port's reference pitch = 6.25 m/px = z14.3 here.
  ASSERT_TRUE(proj.SetPhysicalScale(25000.0, 0.25).ok());

  fv::CpuCanvas canvas(512, 512);
  // The style's own background layer is the clear colour — a GL style's
  // `background` is not a feature and cannot be a StyleResult, so the
  // application clears with it. Same call PythonView makes.
  fv::FvColor bg{255, 255, 255, 255};
  EXPECT_TRUE(s.style->background(proj.Scale(), &bg))
      << "the reference style has a background layer";
  canvas.Clear(bg);

  fv::VectorRenderer r(s.source, s.style);
  ASSERT_TRUE(r.Render(proj, &canvas).ok());

  EXPECT_EQ(s.source->last_query_zoom(), 14);
  EXPECT_GT(r.features_queried(), 1000u);
  EXPECT_GT(r.draws_emitted(), 1000u);
  // A blank or single-fill frame would still hash stably, so assert that the
  // picture has the variety a street map has.
  EXPECT_GT(DistinctColors(canvas.Buffer()), 8u);

  const uint64_t h = Fnv1a(canvas.Buffer());
  if (kHashAtlanta == 0)
    printf("PROBE atlanta hash: 0x%llxull\n", (unsigned long long)h);
  else
    EXPECT_EQ(h, kHashAtlanta);
  WritePng(canvas.Buffer(), "osm_atlanta");
}

TEST(OsmRender, OverzoomKeepsDrawingPastTheDeepestTiles) {
  SKIP_WITHOUT_DATA();
  Scene s = MakeScene(mb_path, style_path);
  ASSERT_TRUE(s.source && s.style);

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(256, 256).ok());
  ASSERT_TRUE(proj.SetCenter(fv::GeoPoint{kAtlantaLat, kAtlantaLon}).ok());
  // 1:2,000 = 0.5 m/px = z18: four levels past the pyramid's z14.
  ASSERT_TRUE(proj.SetPhysicalScale(2000.0, 0.25).ok());

  fv::CpuCanvas canvas(256, 256);
  fv::FvColor bg{255, 255, 255, 255};
  s.style->background(proj.Scale(), &bg);
  canvas.Clear(bg);

  fv::VectorRenderer r(s.source, s.style);
  ASSERT_TRUE(r.Render(proj, &canvas).ok());

  // The read level is clamped; the STYLE's is not, which is the arrangement
  // that keeps a slippy map growing after it runs out of levels.
  EXPECT_EQ(s.source->last_query_zoom(), s.source->file().max_zoom());
  EXPECT_GT(s.source->last_query_overzoom(), 2.0);
  EXPECT_GT(r.features_queried(), 0u);
  EXPECT_GT(r.draws_emitted(), 0u) << "large scales went blank";
  EXPECT_GT(DistinctColors(canvas.Buffer()), 2u);
  WritePng(canvas.Buffer(), "osm_atlanta_overzoom");
}

// Road names, the thing along-path labels exist for. NO HASH: the glyphs come
// from the host font, which is exactly why the Atlanta golden above keeps
// labels off. What is pinned instead is structure — that the names arrive,
// that they arrive ON their roads, and that turning the label switch on adds
// draws rather than replacing them.
TEST(OsmRender, RoadNamesRunAlongTheirRoads) {
  SKIP_WITHOUT_DATA();
  const char* fonts[] = {"/System/Library/Fonts/Supplemental/Arial.ttf",
                         "/System/Library/Fonts/Supplemental/Courier New.ttf",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
  std::string font;
  for (const char* f : fonts)
    if (fs::is_regular_file(f)) { font = f; break; }
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  auto render = [&](bool labels, fv::CpuCanvas* canvas) {
    Scene s = MakeScene(mb_path, style_path);
    EXPECT_TRUE(s.source && s.style);
    s.style->SetDrawLabels(labels);
    fv::MapProjection proj;
    proj.SetSurfaceSize(512, 512);
    proj.SetCenter(fv::GeoPoint{kAtlantaLat, kAtlantaLon});
    proj.SetPhysicalScale(12000.0, 0.25);  // z15-ish: road-name's minzoom is 14
    EXPECT_TRUE(canvas->SetDefaultFont(font).ok());
    fv::FvColor bg{255, 255, 255, 255};
    s.style->background(proj.Scale(), &bg);
    canvas->Clear(bg);
    fv::VectorRenderer r(s.source, s.style);
    EXPECT_TRUE(r.Render(proj, canvas).ok());
    return r.draws_emitted();
  };

  fv::CpuCanvas plain(512, 512), named(512, 512);
  const size_t without = render(false, &plain);
  const size_t with = render(true, &named);
  EXPECT_GT(with, without) << "the label switch drew nothing extra";

  // Every label draw is one RUN of glyphs, so the count rises modestly; a
  // wild jump would mean a name per tile per part, the de-duplication gap.
  EXPECT_LT(with - without, without) << "implausibly many label runs";

  // The names are ink the plain frame does not have, and they are inside the
  // frame rather than piled at a part's first vertex in one corner.
  long changed = 0;
  int x0 = 512, x1 = -1, y0 = 512, y1 = -1;
  for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x) {
      const unsigned char* a = plain.Buffer().Row(y) + 4 * x;
      const unsigned char* b = named.Buffer().Row(y) + 4 * x;
      if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) continue;
      ++changed;
      x0 = std::min(x0, x); x1 = std::max(x1, x);
      y0 = std::min(y0, y); y1 = std::max(y1, y);
    }
  EXPECT_GT(changed, 100) << "labels left no ink";
  EXPECT_GT(x1 - x0, 200) << "labels are not spread across the map";
  EXPECT_GT(y1 - y0, 200);
  WritePng(named.Buffer(), "osm_atlanta_road_names");
}

TEST(OsmRender, RenderIsDeterministic) {
  SKIP_WITHOUT_DATA();
  uint64_t hashes[2] = {0, 0};
  for (int i = 0; i < 2; ++i) {
    Scene s = MakeScene(mb_path, style_path);
    ASSERT_TRUE(s.source && s.style);
    fv::MapProjection proj;
    proj.SetSurfaceSize(256, 256);
    proj.SetCenter(fv::GeoPoint{kAtlantaLat, kAtlantaLon});
    proj.SetPhysicalScale(50000.0, 0.25);
    fv::CpuCanvas canvas(256, 256);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    fv::VectorRenderer r(s.source, s.style);
    ASSERT_TRUE(r.Render(proj, &canvas).ok());
    hashes[i] = Fnv1a(canvas.Buffer());
  }
  EXPECT_EQ(hashes[0], hashes[1]);
}
