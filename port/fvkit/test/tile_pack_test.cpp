// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// L5 TilePack tests: GeoPackage write, level metadata, stitched read-back,
// and the full circle — the engine rendering FROM a pack the engine WROTE,
// byte-identical to the direct render at the same resolution.

#include "fvkit/store/tile_pack.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/engine.h"
#include "fvkit/formats/registry.h"

namespace fs = std::filesystem;

namespace {

std::string TestDataDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  return d ? d : "";
}

class TilePackReal : public ::testing::Test {
 protected:
  void SetUp() override {
    td_ = TestDataDir();
    if (td_.empty() || !fs::exists(td_ + "/rpf")) GTEST_SKIP();
    fv::ClearFormatRegistryForTest();
    fv::RegisterBuiltinFormats();
    catalog_ = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(catalog_->Open(":memory:").ok());
    int64_t id;
    int n = 0;
    ASSERT_TRUE(catalog_->AddDataSource(td_ + "/rpf", "cadrg", 0, &id).ok());
    ASSERT_TRUE(catalog_->Scan(id, &n).ok());
    std::vector<fv::SeriesRow> series;
    ASSERT_TRUE(catalog_->Series(&series).ok());
    for (const auto& s : series)
      if (s.series_key == "LFC") lfc_ = s.id;
    ASSERT_NE(lfc_, 0);
    pack_dir_ = (fs::temp_directory_path() / "fvkit_packs").string();
    fs::create_directories(pack_dir_);
    pack_ = pack_dir_ + "/fvkit_test_pack.gpkg";
  }
  void TearDown() override {
    fv::ClearFormatRegistryForTest();
    std::remove(pack_.c_str());
  }

  std::string td_, pack_, pack_dir_;
  std::shared_ptr<fv::Catalog> catalog_;
  int64_t lfc_ = 0;
};

TEST_F(TilePackReal, WriteReadRoundTrip) {
  // Atlanta-ish 1 x 1.4 deg pack, 3 levels
  fv::GeoRect bounds{{33.2, -85.2}, {34.2, -83.8}};
  fv::MapEngine engine(catalog_);
  fv::TilePackWriter writer;
  ASSERT_TRUE(writer.Create(pack_, "lfc_atl", bounds, 256).ok());
  int n0 = 0, n2 = 0;
  ASSERT_TRUE(writer.WriteLevel(engine, 0, lfc_, &n0).ok());
  ASSERT_TRUE(writer.WriteLevel(engine, 1, lfc_, nullptr).ok());
  ASSERT_TRUE(writer.WriteLevel(engine, 2, lfc_, &n2).ok());
  ASSERT_TRUE(writer.Close().ok());
  EXPECT_GE(n0, 1);
  EXPECT_GT(n2, n0);  // deeper level has more tiles

  // metadata: 3 levels, dpp halves each level
  std::string table;
  fv::GeoRect rb;
  std::vector<fv::TileLevelInfo> levels;
  ASSERT_TRUE(fv::ReadTilePackLevels(pack_, &table, &rb, &levels).ok());
  EXPECT_EQ(table, "lfc_atl");
  ASSERT_EQ(levels.size(), 3u);
  EXPECT_NEAR(levels[0].dpp_lat / levels[1].dpp_lat, 2.0, 1e-12);
  EXPECT_NEAR(levels[1].dpp_lat / levels[2].dpp_lat, 2.0, 1e-12);
  EXPECT_NEAR(rb.ll.lat, 33.2, 1e-9);

  // read one tile-aligned block back and compare EXACTLY to a fresh engine
  // render at the same dpp/center — the pack must be a faithful cache
  fv::TilePackRasterSource pack_src;
  ASSERT_TRUE(pack_src.Open(pack_ + "#z=2").ok());
  const fv::TileLevelInfo& L = levels[2];
  // pick the tile at matrix center (guaranteed rendered: LFC covers it)
  int tc = L.matrix_width / 2, tr = L.matrix_height / 2;
  fv::PixelBuffer from_pack;
  ASSERT_TRUE(
      pack_src.ReadBlock({tc * 256, tr * 256, 256, 256}, &from_pack).ok());

  fv::MapEngine engine2(catalog_);
  ASSERT_TRUE(engine2.SetSurfaceDimensions(256, 256).ok());
  ASSERT_TRUE(engine2.SetResolution(L.dpp_lat, L.dpp_lon).ok());
  double top = rb.ur.lat - tr * 256 * L.dpp_lat;
  double left = rb.ll.lon + tc * 256 * L.dpp_lon;
  ASSERT_TRUE(engine2
                  .SetCenter({top - 128.0 * L.dpp_lat, left + 128.0 * L.dpp_lon})
                  .ok());
  fv::CpuCanvas direct(256, 256);
  direct.Clear(fv::FvColor{0, 0, 0, 0});
  int drawn = 0;
  ASSERT_TRUE(engine2.RenderBaseMap(direct, lfc_, {}, &drawn).ok());
  ASSERT_GE(drawn, 1);

  ASSERT_EQ(from_pack.Width(), 256);
  bool identical = true;
  for (int y = 0; y < 256 && identical; ++y)
    identical = std::memcmp(from_pack.Row(y), direct.Buffer().Row(y), 256 * 4) == 0;
  EXPECT_TRUE(identical) << "pack tile must be byte-identical to direct render";
}

TEST_F(TilePackReal, EnumeratedAndRenderedThroughEngine) {
  // small 2-level pack
  fv::GeoRect bounds{{33.4, -84.9}, {34.0, -84.0}};
  fv::MapEngine build_engine(catalog_);
  fv::TilePackWriter writer;
  ASSERT_TRUE(writer.Create(pack_, "atl", bounds, 256).ok());
  ASSERT_TRUE(writer.WriteLevel(build_engine, 1, lfc_, nullptr).ok());
  ASSERT_TRUE(writer.Close().ok());

  // catalog the pack via the "gpkg" format and render from it
  auto pack_cat = std::make_shared<fv::Catalog>();
  ASSERT_TRUE(pack_cat->Open(":memory:").ok());
  int64_t id;
  int n = 0;
  ASSERT_TRUE(pack_cat
                  ->AddDataSource(pack_dir_, "gpkg", 0, &id)
                  .ok());
  ASSERT_TRUE(pack_cat->Scan(id, &n).ok());
  ASSERT_GE(n, 1);  // one row per level

  fv::MapEngine view_engine(pack_cat);
  ASSERT_TRUE(view_engine.SetSurfaceDimensions(200, 150).ok());
  ASSERT_TRUE(view_engine.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(view_engine.SetScale(1000000.0).ok());
  fv::CpuCanvas canvas(200, 150);
  canvas.Clear(fv::FvColor{9, 9, 9, 255});
  int drawn = 0;
  fv::Status s = view_engine.RenderBaseMap(canvas, 0, {}, &drawn);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_GE(drawn, 1);
  long inked = 0;
  for (int y = 0; y < 150; ++y) {
    const unsigned char* row = canvas.Buffer().Row(y);
    for (int x = 0; x < 200; ++x)
      if (row[4 * x] != 9) ++inked;
  }
  EXPECT_GT(inked, 1000) << "render-from-pack should paint chart pixels";
}

TEST(TilePackErrors, GuardRails) {
  fv::TilePackWriter w;
  EXPECT_EQ(w.Create("/tmp/x.gpkg", "t", {{10, 170}, {12, -178}}).code,
            fv::kUnsupported);  // AM crossing
  EXPECT_EQ(w.Create("/tmp/x.gpkg", "t", {{12, 5}, {10, 6}}).code,
            fv::kInvalidArg);  // empty
  fv::TilePackRasterSource src;
  EXPECT_FALSE(src.Open("/nonexistent.gpkg").ok());
}

}  // namespace
