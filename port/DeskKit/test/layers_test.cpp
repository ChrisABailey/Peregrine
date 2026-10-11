// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <string>

#include "fv_desk_base_map.h"
#include "fv_desk_fake.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/raster.h"

namespace {

using fv::FeatureOverlay;
using fv::GeoPoint;
using fv::desk::FakeDesk;
using fv::desk::LayerRow;

const std::string kRivers = FV_FEATURE_RIVERS;

/// Pixels whose colour is the river blue (30, 90, 200) or close to it.
int Blue(const fv::PixelBuffer& b) {
  int n = 0;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* p = row + 4 * x;
      if (p[2] > 120 && p[0] < 80 && p[1] < 130) ++n;
    }
  }
  return n;
}

/// A FakeDesk on Charleston harbour with the rivers sample open.
class LayersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto& mv = fd_.desk().map_view();
    mv.SetViewport(mv.View().WithCamera(GeoPoint{32.86, -79.95}, 250000, 0));
    fd_.shell().files_to_open = {kRivers};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  }
  std::vector<LayerRow> Rows() { return fd_.desk().layers().Rows(); }

  FakeDesk fd_{400, 400};
};

TEST_F(LayersTest, FakeDeskRendersTheRiversSample) {
  auto features = fd_.desk().overlays().OfType(FeatureOverlay::kTypeId);
  ASSERT_EQ(features.size(), 1u);
  EXPECT_EQ(features[0]->Name(), "Charleston rivers");
  EXPECT_EQ(features[0]->AsPersistence()->file_spec(), kRivers);
  fv::PixelBuffer buf;
  ASSERT_TRUE(fd_.Render(&buf).ok());
  EXPECT_GT(Blue(buf), 300);

  // Labelled by name: the halo puts white pixels on the black background.
  if (fv::desk::HostLabelFont().empty()) GTEST_SKIP() << "no label font on this machine";
  auto* overlay = static_cast<FeatureOverlay*>(features[0].get());
  fv::FeatureStyle style = overlay->style();
  style.label.field = "name";
  overlay->SetStyle(style);
  fv::PixelBuffer labelled;
  ASSERT_TRUE(fd_.Render(&labelled).ok());
  int white = 0;
  for (int y = 0; y < labelled.Height(); ++y)
    for (int x = 0; x < labelled.Width(); ++x)
      if (labelled.Row(y)[4 * x] > 160 && labelled.Row(y)[4 * x + 1] > 160) ++white;
  EXPECT_GT(white, 50);
}

TEST_F(LayersTest, RowsListTheStackTopFirst) {
  ASSERT_TRUE(fd_.Run({std::string("overlay.toggle.") + fv::app::kGridTypeId,
                       std::string("overlay.new.") + fv::PointOverlay::kTypeId})
                  .ok());
  const auto rows = Rows();
  ASSERT_EQ(rows.size(), 3u);
  // Display order puts points (950) over feature layers (940) over the grid (900).
  EXPECT_EQ(rows[0].type_name, "Points");
  EXPECT_EQ(rows[1].type_name, "Feature Layer");
  EXPECT_EQ(rows[2].type_name, "Lat/Lon Grid");
  EXPECT_EQ(rows[1].name, "Charleston rivers");
  EXPECT_TRUE(rows[1].has_opacity);
  EXPECT_FALSE(rows[0].has_opacity);
  EXPECT_TRUE(rows[0].current);  // the newest overlay
  EXPECT_EQ(rows[1].icon, "features");
}

TEST_F(LayersTest, HidingAndFadingRedrawTheLayer) {
  fv::PixelBuffer before;
  ASSERT_TRUE(fd_.Render(&before).ok());
  const int full = Blue(before);
  const uint64_t gen = fd_.desk().LayersGeneration();
  const int invalidates = fd_.shell().invalidate_calls;

  ASSERT_TRUE(fd_.desk().SetLayerVisible(0, false).ok());
  EXPECT_GT(fd_.desk().LayersGeneration(), gen);
  EXPECT_GT(fd_.shell().invalidate_calls, invalidates);
  EXPECT_FALSE(Rows()[0].visible);
  fv::PixelBuffer hidden;
  ASSERT_TRUE(fd_.Render(&hidden).ok());
  EXPECT_EQ(Blue(hidden), 0);

  ASSERT_TRUE(fd_.desk().SetLayerVisible(0, true).ok());
  ASSERT_TRUE(fd_.desk().SetLayerOpacity(0, 30).ok());
  EXPECT_EQ(Rows()[0].opacity, 30);
  fv::PixelBuffer faded;
  ASSERT_TRUE(fd_.Render(&faded).ok());
  EXPECT_LT(Blue(faded), full / 4);  // a 30% blue over black is too dark to count

  EXPECT_EQ(fd_.desk().SetLayerVisible(9, false).code, fv::kInvalidArg);
}

TEST_F(LayersTest, OpacityIsRefusedWhereItDoesNothing) {
  ASSERT_TRUE(fd_.Run({std::string("overlay.toggle.") + fv::app::kGridTypeId}).ok());
  const auto rows = Rows();
  ASSERT_EQ(rows[1].type_name, "Lat/Lon Grid");
  EXPECT_EQ(fd_.desk().SetLayerOpacity(1, 50).code, fv::kUnsupported);
}

TEST_F(LayersTest, MovingAndSelectingRows) {
  ASSERT_TRUE(fd_.Run({std::string("overlay.toggle.") + fv::app::kGridTypeId}).ok());
  ASSERT_EQ(Rows()[0].type_name, "Feature Layer");
  const uint64_t gen = fd_.desk().LayersGeneration();
  ASSERT_TRUE(fd_.desk().MoveLayer(1, 0).ok());  // the grid over the rivers
  EXPECT_EQ(Rows()[0].type_name, "Lat/Lon Grid");
  EXPECT_GT(fd_.desk().LayersGeneration(), gen);
  ASSERT_TRUE(fd_.desk().MoveLayer(0, 1).ok());
  EXPECT_EQ(Rows()[0].type_name, "Feature Layer");

  ASSERT_TRUE(fd_.desk().SelectLayer(1).ok());
  EXPECT_TRUE(Rows()[1].current);
  EXPECT_FALSE(Rows()[0].current);
  EXPECT_EQ(fd_.desk().MoveLayer(0, 5).code, fv::kInvalidArg);
}

TEST_F(LayersTest, OpacitySurvivesTheWorkspace) {
  ASSERT_TRUE(fd_.desk().SetLayerOpacity(0, 45).ok());
  const fv::desk::Workspace saved = fd_.desk().CaptureWorkspace();
  EXPECT_EQ(saved.overlays.at("0.opacity"), "45");

  FakeDesk other(400, 400);
  EXPECT_EQ(other.desk().ApplyWorkspace(saved), fv::app::FlowResult::kDone);
  const auto rows = other.desk().layers().Rows();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].opacity, 45);
}

TEST_F(LayersTest, LayersCommandAsksTheShell) {
  ASSERT_TRUE(fd_.Run({"overlay.layers"}).ok());
  EXPECT_EQ(fd_.shell().layers_shown, 1);
}

}  // namespace
