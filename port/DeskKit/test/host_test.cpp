// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// DeskHost: frames reach the shell, input reaches the view, a keyboard step
// keeps the position under the cursor.

#include "fv_desk_host.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "scratch.h"
#include "stub_formats.h"

using fv::GeoRect;
using fv::desk::DeskHost;
using fv::desk::FramePlacement;
using fv::desk::HostTick;
using fv::desk::test::Frame;
using fv::desk::test::Scratch;
using fv::desk::test::StubFrames;

namespace {

/// Releases the host at scope exit.
struct HostRef {
  DeskHost* host = DeskHost::Create();
  ~HostRef() { fv_desk_host_release(host); }
  DeskHost* operator->() { return host; }
};

/// Ticks until the worker has delivered the frame for the current view.
HostTick Settle(DeskHost* host) {
  HostTick t = host->Tick();
  host->WaitForRender();
  const HostTick u = host->Tick();
  t.new_frame |= u.new_frame;
  t.redraw |= u.redraw;
  t.status_changed |= u.status_changed;
  return t;
}

TEST(DeskHost, DeliversAFrameAtThePixelSize) {
  HostRef h;
  EXPECT_FALSE(h->HasFrame());
  EXPECT_EQ(h->FramePixels(), nullptr);
  // No surface yet: nothing is requested.
  h->Tick();
  h->WaitForRender();
  EXPECT_FALSE(h->Tick().new_frame);

  h->Resize(200, 100, 2.0, 0.25);
  const HostTick t = Settle(h.host);
  EXPECT_TRUE(t.new_frame);
  EXPECT_TRUE(t.redraw);
  ASSERT_TRUE(h->HasFrame());
  EXPECT_EQ(h->FrameWidth(), 400);
  EXPECT_EQ(h->FrameHeight(), 200);
  EXPECT_DOUBLE_EQ(h->FrameDisplayScale(), 2.0);
  ASSERT_NE(h->FramePixels(), nullptr);
  // No catalog: an opaque black ground.
  EXPECT_EQ(h->FramePixels()[0], 0);
  EXPECT_EQ(h->FramePixels()[3], 255);
  std::vector<uint8_t> copy(400 * 200 * 4, 7);
  EXPECT_FALSE(h->CopyFrame(copy.data(), copy.size() - 1));
  ASSERT_TRUE(h->CopyFrame(copy.data(), copy.size()));
  EXPECT_EQ(copy[0], 0);
  EXPECT_EQ(copy[3], 255);

  // The frame was drawn for the live view, so it sits where it is.
  FramePlacement p = h->Placement();
  ASSERT_TRUE(p.valid);
  EXPECT_NEAR(p.a, 1.0, 1e-9);
  EXPECT_NEAR(p.tx, 0.0, 1e-6);

  // Nothing changed: no new request and no new frame.
  const HostTick idle = Settle(h.host);
  EXPECT_FALSE(idle.new_frame);
  EXPECT_FALSE(idle.redraw);

  // A drag moves the placement before the next frame arrives.
  h->PointerDown(100, 50);
  h->PointerDrag(130, 40);
  EXPECT_TRUE(h->Tick().redraw);
  p = h->Placement();
  ASSERT_TRUE(p.valid);
  EXPECT_NEAR(p.tx, 30.0, 0.5);
  EXPECT_NEAR(p.ty, -10.0, 0.5);
  h->PointerUp(130, 40);
  EXPECT_TRUE(Settle(h.host).new_frame);
  EXPECT_NEAR(h->Placement().tx, 0.0, 1e-6);
}

TEST(DeskHost, StatusReportsScaleAndCursor) {
  HostRef h;
  h->Resize(200, 100, 1.0, 0.25);
  EXPECT_EQ(h->StatusMessage(), "No map data in the catalog");
  EXPECT_EQ(h->StatusScale(), "1:10,000,000");
  EXPECT_EQ(h->StatusPosition(), "");
  h->Tick();
  // The view centre is (0, 0); one point left of it is west.
  h->Hover(99.5 - 1, 49.5);
  EXPECT_TRUE(h->Tick().status_changed);
  const std::string pos = h->StatusPosition();
  EXPECT_NE(pos.find("00\xC2\xB0" "00.000' N"), std::string::npos) << pos;
  EXPECT_NE(pos.find("' W"), std::string::npos) << pos;
  h->HoverExit();
  EXPECT_EQ(h->StatusPosition(), "");
}

TEST(DeskHost, OpenCatalogReportsAMissingFile) {
  HostRef h;
  EXPECT_NE(h->OpenCatalog("/nonexistent/catalog.sqlite"), "");
  EXPECT_EQ(h->CatalogPath(), "");
  EXPECT_NE(h->Execute("no.such.command"), "");
}

class DeskHostStub : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    // Three nested raster series. GNC sets the data centre, (32.5, -80); the
    // finer two reach the north-west, where the step tests put the cursor.
    StubFrames()["cadrg"] = {
        Frame("gnc", GeoRect{{28, -84}, {37, -76}}, "GNC", 5e6),
        Frame("jnc", GeoRect{{31, -83}, {35, -78}}, "JNC", 2e6),
        Frame("tpc", GeoRect{{32, -82.5}, {34, -79}}, "TPC", 5e5),
    };
    ASSERT_TRUE(fv::desk::test::RegisterStubFormat("cadrg").ok());
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  std::string MakeCatalog(Scratch& dir) {
    const std::string path = dir.Path("catalog.sqlite");
    fv::Catalog cat;
    EXPECT_TRUE(cat.Open(path).ok());
    int64_t src = 0;
    int added = 0;
    EXPECT_TRUE(cat.AddDataSource("/stub/cadrg", "cadrg", 0, &src).ok());
    EXPECT_TRUE(cat.Scan(src, &added).ok());
    return path;
  }
};

TEST_F(DeskHostStub, OpeningACatalogCentresOnItsData) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  EXPECT_EQ(h->CatalogPath(), path);
  // Recentred on the union of the frames and settled on a series there.
  EXPECT_EQ(h->StatusProduct().rfind("cadrg ", 0), 0u) << h->StatusProduct();
  EXPECT_EQ(h->StatusMessage(), "");
  // The stub format cannot draw, but the frame still arrives.
  EXPECT_TRUE(Settle(h.host).new_frame);
}

TEST_F(DeskHostStub, GoToChoosesTheSeriesNearestTheScale) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  h->GoTo(33, -80, 6e5);
  EXPECT_EQ(h->StatusProduct(), "cadrg TPC");
  EXPECT_EQ(h->StatusScale(), "1:500,000");
  EXPECT_NEAR(h->CenterLat(), 33, 1e-9);
  EXPECT_NEAR(h->CenterLon(), -80, 1e-9);
  EXPECT_DOUBLE_EQ(h->ScaleDenom(), 5e5);
  // Outside TPC and JNC only GNC is left.
  h->GoTo(29, -77, 6e5);
  EXPECT_EQ(h->StatusProduct(), "cadrg GNC");
}

TEST_F(DeskHostStub, PageUpAtACornerStepsTheMapUnderTheCursor) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  ASSERT_EQ(h->Execute("map.zoom_out"), "");
  ASSERT_EQ(h->Execute("map.zoom_out"), "");
  ASSERT_EQ(h->StatusProduct(), "cadrg GNC");

  // Near the upper-left corner, about (33.7, -82.1): inside JNC and TPC.
  h->Hover(40, 30);
  const std::string before = h->StatusPosition();
  h->Step(+1);
  EXPECT_EQ(h->StatusProduct(), "cadrg JNC");
  EXPECT_EQ(h->StatusScale(), "1:2,000,000");
  EXPECT_EQ(h->StatusPosition(), before);
  h->Step(+1);
  EXPECT_EQ(h->StatusProduct(), "cadrg TPC");
  EXPECT_EQ(h->StatusPosition(), before);
  // Past the finest series the raster ladder stops and says so.
  h->Step(+1);
  EXPECT_EQ(h->StatusProduct(), "cadrg TPC");
  EXPECT_EQ(h->StatusMessage(), "No further map at this point");
}

TEST(DeskHostData, DrawsAGeoTiffBaseMap) {
  const char* root = std::getenv("FVW_TESTDATA_DIR");
  const std::string dir = root ? std::string(root) + "/geotiff" : std::string();
  if (dir.empty() || !std::filesystem::exists(dir)) GTEST_SKIP() << "no testdata/geotiff";
  fv::ClearFormatRegistryForTest();
  fv::RegisterBuiltinFormats();
  Scratch scratch;
  const std::string path = scratch.Path("catalog.sqlite");
  {
    fv::Catalog cat;
    ASSERT_TRUE(cat.Open(path).ok());
    int64_t src = 0;
    int added = 0;
    ASSERT_TRUE(cat.AddDataSource(dir, "geotiff", 0, &src).ok());
    ASSERT_TRUE(cat.Scan(src, &added).ok());
    ASSERT_GT(added, 0);
  }
  HostRef h;
  h->Resize(128, 128, 2.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  EXPECT_EQ(h->StatusProduct().rfind("geotiff ", 0), 0u) << h->StatusProduct();
  ASSERT_TRUE(Settle(h.host).new_frame);
  ASSERT_EQ(h->FrameWidth(), 256);
  const uint8_t* px = h->FramePixels();
  int lit = 0;
  for (int i = 0; i < 256 * 256; ++i) lit += (px[4 * i] | px[4 * i + 1] | px[4 * i + 2]) != 0;
  EXPECT_GT(lit, 256 * 256 / 4);
  fv::ClearFormatRegistryForTest();
}

}  // namespace
