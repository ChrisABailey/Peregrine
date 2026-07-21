// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// FvKit L0 geo primitives — the wrap-case tests the contracts doc (D2)
// requires to exist before any consumer does.

#include "fvkit/geo.h"

#include <gtest/gtest.h>

namespace {

TEST(StatusTest, DefaultIsOk) {
  fv::Status s;
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(s.code, fv::kOk);
  EXPECT_TRUE(fv::Status::Ok().ok());
  fv::Status e = fv::Status::Error(fv::kIoError, "boom");
  EXPECT_FALSE(e.ok());
  EXPECT_EQ(e.code, fv::kIoError);
  EXPECT_EQ(e.message, "boom");
}

TEST(NormalizeLonTest, CanonicalRange) {
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(0.0), 0.0);
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(179.5), 179.5);
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(-179.5), -179.5);
  // +/-180 both normalize to +180 (D2)
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(180.0), 180.0);
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(-180.0), 180.0);
  // wraps
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(190.0), -170.0);
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(-190.0), 170.0);
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(540.0), 180.0);
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(-540.0), 180.0);
  EXPECT_DOUBLE_EQ(fv::NormalizeLon(360.0), 0.0);
}

TEST(GeoPointTest, NormalizeClampsLatWrapsLon) {
  fv::GeoPoint p{95.0, 190.0};
  p.Normalize();
  EXPECT_DOUBLE_EQ(p.lat, 90.0);
  EXPECT_DOUBLE_EQ(p.lon, -170.0);
}

TEST(GeoRectTest, SimpleContains) {
  fv::GeoRect r{{10.0, 20.0}, {20.0, 40.0}};
  EXPECT_FALSE(r.CrossesAntimeridian());
  EXPECT_TRUE(r.Contains({15.0, 30.0}));
  EXPECT_TRUE(r.Contains({10.0, 20.0}));  // edges inclusive
  EXPECT_TRUE(r.Contains({20.0, 40.0}));
  EXPECT_FALSE(r.Contains({15.0, 41.0}));
  EXPECT_FALSE(r.Contains({21.0, 30.0}));
}

TEST(GeoRectTest, AntimeridianContains) {
  // Bering-strait style rect: 170E across to 170W
  fv::GeoRect r{{10.0, 170.0}, {20.0, -170.0}};
  EXPECT_TRUE(r.CrossesAntimeridian());
  EXPECT_TRUE(r.Contains({15.0, 175.0}));
  EXPECT_TRUE(r.Contains({15.0, 180.0}));
  EXPECT_TRUE(r.Contains({15.0, -175.0}));
  EXPECT_TRUE(r.Contains({15.0, 170.0}));   // edges inclusive
  EXPECT_TRUE(r.Contains({15.0, -170.0}));
  EXPECT_FALSE(r.Contains({15.0, 0.0}));
  EXPECT_FALSE(r.Contains({15.0, 169.0}));
  EXPECT_FALSE(r.Contains({25.0, 180.0}));  // lat still checked
}

TEST(GeoRectTest, SplitAtAntimeridian) {
  fv::GeoRect r{{10.0, 170.0}, {20.0, -170.0}};
  auto parts = r.SplitAtAntimeridian();
  ASSERT_EQ(parts.size(), 2u);
  EXPECT_DOUBLE_EQ(parts[0].ll.lon, 170.0);
  EXPECT_DOUBLE_EQ(parts[0].ur.lon, 180.0);
  EXPECT_DOUBLE_EQ(parts[1].ll.lon, -180.0);
  EXPECT_DOUBLE_EQ(parts[1].ur.lon, -170.0);
  EXPECT_DOUBLE_EQ(parts[0].ll.lat, 10.0);
  EXPECT_DOUBLE_EQ(parts[1].ur.lat, 20.0);
  EXPECT_FALSE(parts[0].CrossesAntimeridian());
  EXPECT_FALSE(parts[1].CrossesAntimeridian());

  fv::GeoRect plain{{10.0, 20.0}, {20.0, 40.0}};
  EXPECT_EQ(plain.SplitAtAntimeridian().size(), 1u);
}

TEST(GeoRectTest, IntersectsAcrossAntimeridian) {
  fv::GeoRect crossing{{10.0, 170.0}, {20.0, -170.0}};
  // overlaps the eastern piece only
  EXPECT_TRUE(crossing.Intersects({{12.0, -179.0}, {18.0, -160.0}}));
  // overlaps the western piece only
  EXPECT_TRUE(crossing.Intersects({{12.0, 160.0}, {18.0, 175.0}}));
  // both crossing
  EXPECT_TRUE(crossing.Intersects({{12.0, 175.0}, {18.0, -175.0}}));
  // outside in lon
  EXPECT_FALSE(crossing.Intersects({{12.0, -150.0}, {18.0, 0.0}}));
  // outside in lat
  EXPECT_FALSE(crossing.Intersects({{30.0, 175.0}, {40.0, -175.0}}));
  // touching edge counts (inclusive, documented)
  EXPECT_TRUE(crossing.Intersects({{20.0, -170.0}, {30.0, -160.0}}));
  // symmetric
  EXPECT_TRUE(fv::GeoRect({{12.0, 160.0}, {18.0, 175.0}}).Intersects(crossing));
}

TEST(GeoRectTest, WorldContainsEverything) {
  fv::GeoRect w = fv::GeoRect::World();
  EXPECT_FALSE(w.CrossesAntimeridian());
  EXPECT_TRUE(w.Contains({90.0, 180.0}));
  EXPECT_TRUE(w.Contains({-90.0, -180.0}));
  EXPECT_TRUE(w.Contains({0.0, 0.0}));
  EXPECT_TRUE(w.Intersects({{10.0, 170.0}, {20.0, -170.0}}));
}

}  // namespace
