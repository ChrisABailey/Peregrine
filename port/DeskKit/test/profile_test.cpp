// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the route elevation profile chart and Desk's profile window flow.
#include "fv_desk_profile.h"

#include <gtest/gtest.h>

#include <memory>

#include "fv_desk_fake.h"
#include "fv_route_edit.h"
#include "fv_route_overlay.h"

namespace {

using fv::GeoPoint;
using fv::desk::ElevationWindow;
using fv::desk::FakeDesk;
using fv::desk::ProfileChart;
using fv::desk::ProfileLayout;

/// Rises 10 m per 0.01° of longitude east of -80.1; no data in (-80.06, -80.04).
class RampSource : public fv::IElevationSource {
 public:
  RampSource() = default;
  fv::GeoRect Bounds() const override { return fv::GeoRect{{32, -81}, {33, -79}}; }
  fv::Status GetElevation(const GeoPoint& p, float* m) override {
    if (p.lon > -80.06 && p.lon < -80.04) return fv::Status::Error(fv::kNotFound, "void");
    *m = static_cast<float>((p.lon + 80.1) * 1000.0);
    return fv::Status::Ok();
  }
};

fv::analysis::GeoPath EastWest() {
  return fv::analysis::GeoPath({GeoPoint{32.6, -80.1}, GeoPoint{32.6, -80.0}},
                               fv::analysis::LineType::kGreatCircle);
}

TEST(ProfileChart, ElevationWindowWidensFlatGroundToTheFullScaleGrade) {
  // 10 m flat over 1 km: the window spans 2% of 1 km, centred on the data.
  auto w = ElevationWindow(10, 10, 1000, 1.0);
  EXPECT_DOUBLE_EQ(w.first, 0.0);
  EXPECT_DOUBLE_EQ(w.second, 20.0);
  // Steeper than 2%: the data range plus 8% each side.
  w = ElevationWindow(0, 100, 1000, 1.0);
  EXPECT_DOUBLE_EQ(w.first, -8.0);
  EXPECT_DOUBLE_EQ(w.second, 108.0);
}

TEST(ProfileChart, AHoleInTheDataSplitsTheTerrainIntoRuns) {
  RampSource src;
  ProfileChart chart;
  chart.SetUnits(fv::analysis::RangeUnit::kKilometers);
  chart.Sample(&src, EastWest(), 101);
  ASSERT_TRUE(chart.HasResult());
  const ProfileLayout L = chart.Layout(720, 340);
  ASSERT_TRUE(L.has_plot);
  EXPECT_DOUBLE_EQ(L.x0, ProfileChart::kPadLeft);
  EXPECT_DOUBLE_EQ(L.y1, 340 - ProfileChart::kPadBottom);
  ASSERT_EQ(L.runs.size(), 2u);
  // Rising terrain: the first point is lower on screen (larger y) than the last.
  EXPECT_GT(L.runs[0].front().y, L.runs[1].back().y);
  EXPECT_NEAR(L.runs[0].front().x, L.x0, 1e-9);
  EXPECT_NEAR(L.runs[1].back().x, L.x1, 1e-9);
  ASSERT_EQ(L.x_ticks.size(), 6u);
  ASSERT_EQ(L.y_ticks.size(), 6u);
  EXPECT_EQ(L.x_ticks[0].label, "0.0 km");
  EXPECT_EQ(L.x_ticks[5].label.substr(L.x_ticks[5].label.size() - 3), " km");
  EXPECT_EQ(L.y_ticks[0].label.substr(L.y_ticks[0].label.size() - 2), " m");
  EXPECT_TRUE(L.vertex_x.empty());  // no markers given

  const std::string stats = chart.Stats();
  EXPECT_NE(stats.find("max 100 m"), std::string::npos) << stats;
  EXPECT_NE(stats.find("101 samples"), std::string::npos) << stats;
  EXPECT_NE(stats.find(" no data"), std::string::npos) << stats;
}

TEST(ProfileChart, ReadoutIsTheNearestSampleUnderTheCursor) {
  RampSource src;
  ProfileChart chart;
  chart.Sample(&src, EastWest(), 101);
  const ProfileLayout L = chart.Layout(720, 340);
  EXPECT_EQ(chart.ReadoutAt(L.x0 - 1, 720, 340), "");
  const std::string end = chart.ReadoutAt(L.x1, 720, 340);
  // 100 m is 328 ft; feet because the default units are nautical miles.
  EXPECT_NE(end.find("328 ft"), std::string::npos) << end;
  EXPECT_NE(end.find("32.60000, -80.00000"), std::string::npos) << end;
  const std::string hole = chart.ReadoutAt((L.x0 + L.x1) / 2, 720, 340);
  EXPECT_NE(hole.find("no data"), std::string::npos) << hole;
}

TEST(ProfileChart, WithoutElevationDataTheChartSaysSo) {
  ProfileChart chart;
  chart.Sample(nullptr, EastWest(), 50);
  const ProfileLayout L = chart.Layout(400, 300);
  EXPECT_FALSE(L.has_plot);
  EXPECT_NE(L.message.find("No elevation data"), std::string::npos);
  EXPECT_EQ(chart.Stats(), "");
}

TEST(ProfileChart, RoutePathUsesTheWaypointsAndTheLegKind) {
  fv::RouteOverlay route;
  EXPECT_FALSE(fv::desk::RouteGeoPath(route).has_value());
  route.edit().AddAt(GeoPoint{32.6, -80.0});
  route.edit().AddAt(GeoPoint{32.7, -80.1});
  route.edit().AddAt(GeoPoint{32.8, -80.1});
  std::vector<size_t> waypoints;
  auto path = fv::desk::RouteGeoPath(route, &waypoints);
  ASSERT_TRUE(path.has_value());
  EXPECT_EQ(path->points().size(), 3u);
  EXPECT_EQ(waypoints, (std::vector<size_t>{0, 1, 2}));

  // The middle waypoint is marked; the ends are not.
  RampSource src;
  ProfileChart chart;
  chart.Sample(&src, *path, 50, waypoints);
  const ProfileLayout L = chart.Layout(720, 340);
  ASSERT_EQ(L.vertex_x.size(), 1u);
  const double frac = path->CumulativeAt(1) / path->TotalLength();
  EXPECT_NEAR(L.vertex_x[0], L.x0 + (L.x1 - L.x0) * frac, 1e-6);
  EXPECT_EQ(path->line_type(), fv::analysis::LineType::kGreatCircle);
  route.SetLegKind(fv::LineKind::kRhumb);
  EXPECT_EQ(fv::desk::RouteGeoPath(route)->line_type(), fv::analysis::LineType::kRhumb);
}

TEST(DeskProfile, TheProfileFollowsTheEditedRoute) {
  FakeDesk fd(400, 300);
  fv::desk::Desk& desk = fd.desk();
  // No route: refused and reported.
  EXPECT_FALSE(desk.OpenRouteProfile().ok());
  EXPECT_EQ(fd.shell().errors.size(), 1u);
  EXPECT_EQ(fd.shell().route_profile_shown, 0);

  ASSERT_TRUE(fd.Run({std::string("editor.mode.") + fv::RouteOverlay::kTypeId}).ok());
  auto route = std::dynamic_pointer_cast<fv::RouteOverlay>(
      desk.overlays().FirstOfType(fv::RouteOverlay::kTypeId));
  ASSERT_NE(route, nullptr);
  route->edit().AddAt(GeoPoint{32.6, -80.0});
  route->edit().AddAt(GeoPoint{32.7, -80.1});

  // The Route menu's Elevation Profile tool opens the window.
  const fv::desk::Command* tool = nullptr;
  for (const std::string& id : desk.commands().IdsWithPrefix("editor.tool."))
    if (desk.commands().Find(id)->icon == "elevation_profile") tool = desk.commands().Find(id);
  ASSERT_NE(tool, nullptr);
  ASSERT_TRUE(desk.Execute(tool->id).ok());
  EXPECT_EQ(fd.shell().route_profile_shown, 1);
  ASSERT_TRUE(desk.RouteProfileOpen());
  EXPECT_EQ(desk.route_profile_title(), "Terrain Profile — " + route->Name());
  // The catalog has no DTED.
  EXPECT_NE(desk.route_profile().Layout(400, 300).message.find("No elevation data"),
            std::string::npos);

  // Unchanged: nothing to re-sample. A moved waypoint or a units change is.
  EXPECT_FALSE(desk.UpdateRouteProfile());
  route->edit().AddAt(GeoPoint{32.8, -80.1});
  EXPECT_TRUE(desk.UpdateRouteProfile());
  EXPECT_FALSE(desk.UpdateRouteProfile());
  fd.settings().Set("analysis.units", "km");
  EXPECT_TRUE(desk.UpdateRouteProfile());
  EXPECT_EQ(desk.route_profile().units(), fv::analysis::RangeUnit::kKilometers);

  // Closing the route leaves a message in the open window.
  ASSERT_TRUE(fd.Run({std::string("editor.mode.") + fv::RouteOverlay::kTypeId}).ok());
  ASSERT_TRUE(desk.overlays().Remove(desk.overlays().FirstOfType(fv::RouteOverlay::kTypeId)).ok());
  route.reset();
  EXPECT_TRUE(desk.UpdateRouteProfile());
  EXPECT_EQ(desk.route_profile().Layout(400, 300).message, "The route was closed.");

  desk.CloseRouteProfile();
  EXPECT_FALSE(desk.RouteProfileOpen());
  EXPECT_FALSE(desk.UpdateRouteProfile());
}

}  // namespace
