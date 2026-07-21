// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Tests for the portable MapSeriesStringConverter. Expected strings follow
// the formats documented in the original source comments
// (e.g. "CADRG 1:5 M (GNC)") plus round-trips through ToMapSeries.

#include "fv_map_series_string_converter.h"

#include <gtest/gtest.h>

namespace {

using fv::MapSeriesStringConverter;

TEST(MapSeriesTest, FormatProductScaleSeries) {
  MapSeriesStringConverter c;
  std::wstring out;
  ASSERT_EQ(S_OK, c.ToString(L"CADRG", 5000000.0, MAP_SCALE_DENOMINATOR,
                             L"GNC", false, FORMAT_PRODUCT_NAME_SCALE_SERIES,
                             &out));
  EXPECT_EQ(out, L"CADRG 1:5 M (GNC)");
}

TEST(MapSeriesTest, FormatVariants) {
  MapSeriesStringConverter c;
  std::wstring out;

  ASSERT_EQ(S_OK, c.ToString(L"CADRG", 5000000.0, MAP_SCALE_DENOMINATOR,
                             L"GNC", false, FORMAT_SERIES_SCALE, &out));
  EXPECT_EQ(out, L"GNC 1:5 M");

  ASSERT_EQ(S_OK, c.ToString(L"CADRG", 5000000.0, MAP_SCALE_DENOMINATOR,
                             L"GNC", false, FORMAT_SCALE_SERIES, &out));
  EXPECT_EQ(out, L"1:5 M (GNC)");

  ASSERT_EQ(S_OK, c.ToString(L"CADRG", 5000000.0, MAP_SCALE_DENOMINATOR,
                             L"GNC", false, FORMAT_SCALE, &out));
  EXPECT_EQ(out, L"1:5 M");
}

TEST(MapSeriesTest, ScaleStringSimplification) {
  // K/M suffixes, comma insertion, invalid scale
  EXPECT_EQ(MapSeriesStringConverter::ToString(5000000.0,
                                               MAP_SCALE_DENOMINATOR),
            L"1:5 M");
  EXPECT_EQ(MapSeriesStringConverter::ToString(250000.0,
                                               MAP_SCALE_DENOMINATOR),
            L"1:250 K");
  EXPECT_EQ(MapSeriesStringConverter::ToString(17500.0,
                                               MAP_SCALE_DENOMINATOR),
            L"1:17,500");
  EXPECT_EQ(MapSeriesStringConverter::ToString(1234567.0,
                                               MAP_SCALE_DENOMINATOR),
            L"1:1,234,567");
  EXPECT_EQ(MapSeriesStringConverter::ToString(0.0, MAP_SCALE_DENOMINATOR),
            L"Invalid Scale");
  EXPECT_EQ(MapSeriesStringConverter::ToString(1.0, MAP_SCALE_WORLD),
            L"World");
  // no-simplify variant: raw denominator
  EXPECT_EQ(MapSeriesStringConverter::ToString(5000000.0,
                                               MAP_SCALE_DENOMINATOR, false),
            L"1:5000000");
}

TEST(MapSeriesTest, ResolutionStrings) {
  // integral -> no decimals; fractional -> three decimals
  EXPECT_EQ(MapSeriesStringConverter::ToString(5.0, MAP_SCALE_NM), L"5 NM");
  EXPECT_EQ(MapSeriesStringConverter::ToString(1.5, MAP_SCALE_METERS),
            L"1.500 meter");
  EXPECT_EQ(MapSeriesStringConverter::ToString(30.0, MAP_SCALE_ARC_SECONDS),
            L"30 arc sec");
}

TEST(MapSeriesTest, DtedSpecialCases) {
  MapSeriesStringConverter c;
  std::wstring out;
  ASSERT_EQ(S_OK, c.ToString(L"DTED", 3.0, MAP_SCALE_ARC_SECONDS, L"Level 1",
                             false, FORMAT_PRODUCT_NAME_SCALE_SERIES, &out));
  EXPECT_EQ(out, L"DTED Level 1");

  std::wstring product, series;
  double scale = 0;
  MapScaleUnitsEnum units = MAP_SCALE_DENOMINATOR;
  bool soft = true;
  ASSERT_EQ(S_OK, c.ToMapSeries(L"DTED Level 1", FORMAT_SCALE_SERIES,
                                &product, &scale, &units, &series, &soft));
  EXPECT_EQ(product, L"DTED");
  EXPECT_EQ(series, L"Level 1");
  EXPECT_DOUBLE_EQ(scale, 3.0);
  EXPECT_EQ(units, MAP_SCALE_ARC_SECONDS);
  EXPECT_FALSE(soft);
}

TEST(MapSeriesTest, RoundTripScaleSeries) {
  MapSeriesStringConverter c;
  std::wstring product, series;
  double scale = 0;
  MapScaleUnitsEnum units = MAP_SCALE_NM;
  bool soft = true;

  ASSERT_EQ(S_OK, c.ToMapSeries(L"1:5 M (GNC)", FORMAT_SCALE_SERIES, &product,
                                &scale, &units, &series, &soft));
  EXPECT_EQ(product, L"");
  EXPECT_EQ(series, L"GNC");
  EXPECT_DOUBLE_EQ(scale, 5000000.0);
  EXPECT_EQ(units, MAP_SCALE_DENOMINATOR);

  ASSERT_EQ(S_OK, c.ToMapSeries(L"1:17,500 (TLM)", FORMAT_SCALE_SERIES,
                                &product, &scale, &units, &series, &soft));
  EXPECT_DOUBLE_EQ(scale, 17500.0);
  EXPECT_EQ(series, L"TLM");
}

TEST(MapSeriesTest, ToMapScaleParsing) {
  double scale = 0;
  MapScaleUnitsEnum units = MAP_SCALE_NM;

  EXPECT_TRUE(MapSeriesStringConverter::ToMapScale(L"World", &scale, &units));
  EXPECT_EQ(units, MAP_SCALE_WORLD);

  EXPECT_TRUE(
      MapSeriesStringConverter::ToMapScale(L"1:250 K", &scale, &units));
  EXPECT_DOUBLE_EQ(scale, 250000.0);

  EXPECT_TRUE(MapSeriesStringConverter::ToMapScale(L"5 NM", &scale, &units));
  EXPECT_DOUBLE_EQ(scale, 5.0);
  EXPECT_EQ(units, MAP_SCALE_NM);

  EXPECT_TRUE(
      MapSeriesStringConverter::ToMapScale(L"30 arc sec", &scale, &units));
  EXPECT_DOUBLE_EQ(scale, 30.0);
  EXPECT_EQ(units, MAP_SCALE_ARC_SECONDS);

  EXPECT_FALSE(MapSeriesStringConverter::ToMapScale(L"", &scale, &units));
}

TEST(MapSeriesTest, UnsupportedToMapSeriesFormats) {
  MapSeriesStringConverter c;
  std::wstring product, series;
  double scale = 0;
  MapScaleUnitsEnum units = MAP_SCALE_NM;
  bool soft = false;

  EXPECT_EQ(E_NOTIMPL,
            c.ToMapSeries(L"CADRG 1:5 M (GNC)",
                          FORMAT_PRODUCT_NAME_SCALE_SERIES, &product, &scale,
                          &units, &series, &soft));
  EXPECT_EQ(E_NOTIMPL, c.ToMapSeries(L"GNC 1:5 M", FORMAT_SERIES_SCALE,
                                     &product, &scale, &units, &series, &soft));
}

TEST(MapSeriesTest, SoftScaleFormatting) {
  MapSeriesStringConverter c;
  std::wstring out;
  // soft scale with a series: product (series), no scale text
  ASSERT_EQ(S_OK, c.ToString(L"ECRG", 5000000.0, MAP_SCALE_DENOMINATOR,
                             L"TLM100", true, FORMAT_PRODUCT_NAME_SCALE_SERIES,
                             &out));
  EXPECT_EQ(out, L"ECRG (TLM100)");
  // soft scale without a series: product + scale
  ASSERT_EQ(S_OK, c.ToString(L"ECRG", 5000000.0, MAP_SCALE_DENOMINATOR, L"",
                             true, FORMAT_PRODUCT_NAME_SCALE_SERIES, &out));
  EXPECT_EQ(out, L"ECRG 1:5 M");
}

}  // namespace
