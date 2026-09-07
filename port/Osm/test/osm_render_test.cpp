// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

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
#include "fvkit/overlay/vector_map_overlay.h"
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

std::string OverlayStylePath() {
  const char* d = getenv("FVW_OSM_STYLE_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/peregrine-osm-overlay.json";
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
// RE-PINNED 2026-08-17 for the re-cut us-south pyramid, 0x3f49f779d29c7887 ->
// 0x4c8ca75e922f59ff, after diffing the two frames pixel for pixel: 78 pixels
// of 262,144 (0.03%) moved, every one of them a swap between the two greys
// (198,188,178) and (217,208,201), scattered a few pixels at a time over the
// downtown blocks. That is the tile's landuse count going 79 -> 238 changing
// which fill wins along a polygon edge, and nothing else in the frame moved —
// no feature shifted, no colour appeared or vanished, the description above
// still reads true off the PNG.
// 0 = probe mode (prints the hash, asserts nothing).
constexpr uint64_t kHashAtlanta = 0xc53a5352fc5bb38full;

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

// ---------------------------------------------------------------------------
// The Kiawah cut (Pippin P1). `port/tools/mbtiles_cut.py` copies tile blobs
// byte for byte out of us-south into a phone-sized pyramid, so the ONLY thing
// that can go wrong is which tiles it took — a dropped row, an off-by-one in
// the box, or the TMS/XYZ y-flip inverted. Every one of those shows up as a
// frame that differs from the same frame drawn over the source, which is what
// this pins: not a hash (that would need re-pinning with every re-cut of
// us-south) but the CUT AGAINST ITS OWN SOURCE, rendered through the same
// path Pippin will use.
//
// Two viewports deliberately: one at street scale (z14, the deepest level,
// where a missing column is a blank stripe) and one zoomed out past the
// island (z10-ish, where the tiles are so large that a naive "clamp the box
// to the deepest zoom" cut loses them entirely and the map goes empty).
constexpr double kKiawahLat = 32.6045;
constexpr double kKiawahLon = -80.0870;

std::string KiawahCutPath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/OSM/kiawah.mbtiles";
  return fs::is_regular_file(p) ? p : std::string();
}

TEST(OsmRender, KiawahCutMatchesSource) {
  SKIP_WITHOUT_DATA();
  const std::string cut_path = KiawahCutPath();
  if (cut_path.empty()) GTEST_SKIP() << "no OSM/kiawah.mbtiles cut";

  // Every zoom the source has, the cut has, counted over the TILES and not
  // over the metadata the cut tool wrote itself: a phone that can zoom out
  // to the whole coast is the reason the cut is not clamped to the deepest
  // levels, and a cut that dropped z0-z9 would still declare z0..14.
  fv::OsmVectorSource src_probe, cut_probe;
  ASSERT_TRUE(src_probe.Open(mb_path).ok());
  ASSERT_TRUE(cut_probe.Open(cut_path).ok());
  std::vector<int> src_zooms, cut_zooms;
  ASSERT_TRUE(src_probe.file().ZoomLevels(&src_zooms).ok());
  ASSERT_TRUE(cut_probe.file().ZoomLevels(&cut_zooms).ok());
  EXPECT_EQ(src_zooms, cut_zooms) << "the cut lost a zoom level";

  // IS THE PACK STILL A CUT OF THIS SOURCE? Everything below compares two
  // renders pixel for pixel, and that is only a statement about
  // `mbtiles_cut.py` while the pack's tiles ARE the source's tiles — the cut
  // copies blobs byte for byte, which is what makes "no hash, just agreement"
  // a contract that never needs re-pinning (P1).
  //
  // A pack built independently — tilemaker run straight at the Kiawah bbox,
  // which is what 2026-08-27 did, or a cut taken from a DIFFERENT vintage of
  // us-south — is not wrong, it simply is not a cut of THIS file, and holding
  // the two renders to pixel equality would then be pinning that two separate
  // builds of OSM agree, which they never will. Measured that day: of 70
  // shared z14 tiles, 9 were byte-identical. So the test asks first, and says
  // which of the two situations it is in rather than failing as though the
  // renderer had changed.
  {
    fv::MbtilesFile::ZoomExtent cut_z14;
    ASSERT_TRUE(cut_probe.file().ZoomExtentOf(14, &cut_z14).ok());
    ASSERT_FALSE(cut_z14.empty()) << "the pack has no z14 tiles";
    int shared = 0, identical = 0;
    for (int x = cut_z14.min_x; x <= cut_z14.max_x && shared < 16; ++x) {
      for (int y = cut_z14.min_y; y <= cut_z14.max_y && shared < 16; ++y) {
        const fv::webmerc::TileId t{14, x, y};
        if (!cut_probe.file().HasTile(t) || !src_probe.file().HasTile(t)) continue;
        std::string a, b;
        if (!cut_probe.file().ReadTile(t, &a).ok()) continue;
        if (!src_probe.file().ReadTile(t, &b).ok()) continue;
        ++shared;
        if (a == b) ++identical;
      }
    }
    ASSERT_GT(shared, 0) << "the pack and the source share no z14 tile at all";
    if (identical != shared) {
      GTEST_SKIP() << "OSM/kiawah.mbtiles is not a cut of this us-south.mbtiles ("
                   << identical << " of " << shared
                   << " shared z14 tiles are byte-identical). Either it was built "
                      "independently by tilemaker, or the two files are different "
                      "vintages of OSM. Re-cut it with port/tools/mbtiles_cut.py "
                      "from this source to exercise the cut again.";
    }
  }

  // Both viewports stay INSIDE the cut box (0.20 deg by 0.12 deg, ~19 km by
  // 13 km): outside it the cut is legitimately empty, so a wider frame would
  // pin the box's edge rather than the copy. 512 px at 0.25 mm/px is 128 mm
  // of paper, so 1:75,000 is 9.6 km across — the island end to end.
  struct Case { const char* name; double scale; } cases[] = {
      {"osm_kiawah_z14", 25000.0},    // street scale, the deepest tiles
      {"osm_kiawah_wide", 75000.0},   // the whole island, a shallower level
  };
  for (const auto& c : cases) {
    uint64_t hashes[2] = {0, 0};
    size_t draws[2] = {0, 0};
    const std::string* paths[2] = {&mb_path, &cut_path};
    fv::CpuCanvas frames[2] = {fv::CpuCanvas(512, 512), fv::CpuCanvas(512, 512)};
    for (int i = 0; i < 2; ++i) {
      Scene s;
      s.source = std::make_shared<fv::OsmVectorSource>();
      ASSERT_TRUE(s.source->Open(*paths[i]).ok());
      s.style = std::make_shared<fv::OsmStyleEngine>();
      ASSERT_TRUE(s.style->LoadFile(style_path).ok());
      s.style->SetReferenceLatitude(kKiawahLat);
      s.style->SetDisplayMmPerPixel(s.source->display_mm_per_pixel());

      fv::MapProjection proj;
      ASSERT_TRUE(proj.SetSurfaceSize(512, 512).ok());
      ASSERT_TRUE(proj.SetCenter(fv::GeoPoint{kKiawahLat, kKiawahLon}).ok());
      ASSERT_TRUE(proj.SetPhysicalScale(c.scale, 0.25).ok());

      fv::FvColor bg{255, 255, 255, 255};
      s.style->background(proj.Scale(), &bg);
      frames[i].Clear(bg);
      fv::VectorRenderer r(s.source, s.style);
      ASSERT_TRUE(r.Render(proj, &frames[i]).ok());
      hashes[i] = Fnv1a(frames[i].Buffer());
      draws[i] = r.draws_emitted();
    }
    // Two identically BLANK frames would agree, so make the frame prove it
    // has a map in it before the agreement means anything.
    EXPECT_GT(draws[0], 100u) << c.name << ": source frame is nearly empty";
    EXPECT_GT(DistinctColors(frames[1].Buffer()), 4u) << c.name;
    EXPECT_EQ(draws[0], draws[1]) << c.name << ": the cut drew a different map";
    EXPECT_EQ(hashes[0], hashes[1]) << c.name << ": the cut is not pixel-identical";
    WritePng(frames[1].Buffer(), c.name);
  }
}

// ---------------------------------------------------------------------------
// OSM AS AN OVERLAY: the same pyramid, the overlay sheet, drawn over a map
// that is already there.
// ---------------------------------------------------------------------------

namespace {

// A stand-in for whatever is underneath — shaded relief, imagery, a scanned
// chart. Any colour would do; a mid grey makes "still there" easy to count.
constexpr unsigned char kUnderR = 96, kUnderG = 104, kUnderB = 96;

size_t PixelsStillShowingTheMapUnder(const fv::PixelBuffer& b) {
  size_t n = 0;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* p = row + x * 4;
      if (p[0] == kUnderR && p[1] == kUnderG && p[2] == kUnderB) ++n;
    }
  }
  return n;
}

}  // namespace

// The two properties that make a style sheet usable as an overlay, over the
// real Atlanta tiles: the sheet declares no background, and what it does draw
// leaves most of the map underneath visible. The base sheet fails both, which
// is why there are two files.
TEST(OsmOverlay, LeavesTheMapUnderneathShowing) {
  const std::string mb_path = MbtilesPath();
  const std::string overlay_style = OverlayStylePath();
  const std::string base_style = StylePath();
  if (mb_path.empty()) GTEST_SKIP() << "no OSM mbtiles";
  if (overlay_style.empty()) GTEST_SKIP() << "no OSM overlay style";

  Scene s = MakeScene(mb_path, overlay_style);
  ASSERT_TRUE(s.source && s.style);

  fv::FvColor none{};
  EXPECT_FALSE(s.style->background(25000.0, &none))
      << "an overlay sheet must declare no background layer";

  fv::VectorMapOverlay ov("OSM", s.source);
  ov.SetStyle(s.style);

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(512, 512).ok());
  ASSERT_TRUE(proj.SetCenter(fv::GeoPoint{kAtlantaLat, kAtlantaLon}).ok());
  ASSERT_TRUE(proj.SetPhysicalScale(25000.0, 0.25).ok());

  fv::CpuCanvas canvas(512, 512);
  canvas.Clear(fv::FvColor{kUnderR, kUnderG, kUnderB, 255});
  ASSERT_TRUE(ov.OnDraw(proj, canvas).ok());

  const size_t total = 512 * 512;
  const size_t under = PixelsStillShowingTheMapUnder(canvas.Buffer());
  EXPECT_GT(ov.last_draw_features(), 100u);
  // Downtown Atlanta at 1:25k is as dense as this pack gets — a solid block
  // grid, the connector and the rail corridor — and a fifth of the frame is
  // still the map below it. Measured 27%.
  EXPECT_GT(under, total / 5);

  // The base sheet over the same view. Its background is not counted here at
  // all (a `background` layer is a canvas clear, and the renderer does not
  // clear), so what this compares is the FILLS alone: landcover, landuse,
  // park and building close over the frame where the overlay sheet leaves it
  // open. Measured 6% against 27%.
  if (!base_style.empty()) {
    Scene b = MakeScene(mb_path, base_style);
    ASSERT_TRUE(b.source && b.style);
    fv::VectorMapOverlay base("OSM base", b.source);
    base.SetStyle(b.style);
    fv::CpuCanvas c2(512, 512);
    c2.Clear(fv::FvColor{kUnderR, kUnderG, kUnderB, 255});
    ASSERT_TRUE(base.OnDraw(proj, c2).ok());
    EXPECT_LT(PixelsStillShowingTheMapUnder(c2.Buffer()), under / 2)
        << "the base sheet is expected to paint over the map, not beside it";
  }
}
