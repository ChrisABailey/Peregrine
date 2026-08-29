// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for the "dted-shaded" fvkit adapter: DtedShadedRasterSource behind the
// IRasterSource seam, and its registry wiring. Real TestData (skipped if
// absent), same convention as the other adapter suites.

#include "fvkit/formats/dted_shaded.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "fv_dted_shaded_renderer.h"
#include "fvkit/formats/registry.h"

namespace {

namespace fs = std::filesystem;

std::string CellPath() {
  const char* d = std::getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return "";
  fs::path p = fs::path(d) / "dted" / "w082" / "n31.dt1";
  return fs::exists(p) ? p.string() : "";
}

TEST(DtedShadedAdapter, RegisteredWithEnumeratorAndRasterFactory) {
  fv::RegisterBuiltinFormats();
  const fv::FormatFactories* f = fv::FindFormat("dted-shaded");
  ASSERT_NE(f, nullptr);
  EXPECT_TRUE(static_cast<bool>(f->make_raster_source));
  EXPECT_TRUE(static_cast<bool>(f->make_enumerator));
  EXPECT_FALSE(static_cast<bool>(f->make_elevation_source));
  // The elevation-query "dted" format is a distinct registration.
  EXPECT_NE(fv::FindFormat("dted"), nullptr);
}

TEST(DtedShadedAdapter, ReadBlockCropsFromRenderedCell) {
  const std::string path = CellPath();
  if (path.empty()) GTEST_SKIP() << "TestData DTED cell not present";

  auto src = std::make_shared<fv::DtedShadedRasterSource>();
  ASSERT_TRUE(src->Open(path).ok());

  fv::ImageInfo info;
  ASSERT_TRUE(src->Info(&info).ok());
  EXPECT_EQ(info.size.width, 1200);
  EXPECT_EQ(info.size.height, 1200);

  // A sub-block matches the same region read from a full-image block.
  fv::PixelRect whole{0, 0, info.size.width, info.size.height};
  fv::PixelBuffer full;
  ASSERT_TRUE(src->ReadBlock(whole, &full).ok());

  fv::PixelRect sub{100, 200, 64, 48};
  fv::PixelBuffer block;
  ASSERT_TRUE(src->ReadBlock(sub, &block).ok());
  ASSERT_EQ(block.Width(), 64);
  ASSERT_EQ(block.Height(), 48);
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 64 * 4; ++x)
      ASSERT_EQ(block.Row(y)[x], full.Row(200 + y)[(100 * 4) + x]);

  // Out-of-bounds block is rejected, not partially filled.
  fv::PixelRect oob{1190, 0, 64, 10};
  fv::PixelBuffer none;
  EXPECT_FALSE(src->ReadBlock(oob, &none).ok());
}

TEST(DtedShadedAdapter, RendererForConfigRetintsBeforeFirstRead) {
  const std::string path = CellPath();
  if (path.empty()) GTEST_SKIP() << "TestData DTED cell not present";

  auto a = std::make_shared<fv::DtedShadedRasterSource>();
  ASSERT_TRUE(a->Open(path).ok());
  fv::PixelRect roi{0, 0, 200, 200};
  fv::PixelBuffer relief;
  ASSERT_TRUE(a->ReadBlock(roi, &relief).ok());

  auto b = std::make_shared<fv::DtedShadedRasterSource>();
  ASSERT_TRUE(b->Open(path).ok());
  ASSERT_NE(b->RendererForConfig(), nullptr);
  b->RendererForConfig()->SetDisplayMode(fv::kDtedElevationColor);
  fv::PixelBuffer bands;
  ASSERT_TRUE(b->ReadBlock(roi, &bands).ok());

  bool differ = false;
  for (int y = 0; y < 200 && !differ; ++y)
    for (int x = 0; x < 200 * 4; ++x)
      if (relief.Row(y)[x] != bands.Row(y)[x]) {
        differ = true;
        break;
      }
  EXPECT_TRUE(differ) << "display-mode change had no effect";
}

}  // namespace
