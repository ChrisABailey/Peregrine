// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fv_oledatetime.h (COleDateTime emulation). Reference DATE values
// verified against OLE automation documentation: 1899-12-30 00:00 = 0.0,
// 1900-01-01 00:00 = 2.0, 2000-01-01 00:00 = 36526.0.

#include "fv_oledatetime.h"

#include <gtest/gtest.h>

namespace {

TEST(OleDateTimeTest, KnownEpochValues) {
  COleDateTime d;
  ASSERT_EQ(0, d.SetDateTime(1899, 12, 30, 0, 0, 0));
  EXPECT_DOUBLE_EQ(d.m_dt, 0.0);

  ASSERT_EQ(0, d.SetDateTime(1900, 1, 1, 0, 0, 0));
  EXPECT_DOUBLE_EQ(d.m_dt, 2.0);

  ASSERT_EQ(0, d.SetDateTime(2000, 1, 1, 0, 0, 0));
  EXPECT_DOUBLE_EQ(d.m_dt, 36526.0);

  // GPS atomic clock base used by NetNMEA
  ASSERT_EQ(0, d.SetDateTime(1980, 1, 6, 0, 0, 0));
  EXPECT_DOUBLE_EQ(d.m_dt, 29226.0);
}

TEST(OleDateTimeTest, FieldRoundTrip) {
  COleDateTime d(2003, 7, 21, 14, 35, 59);
  ASSERT_EQ(COleDateTime::valid, d.GetStatus());
  EXPECT_EQ(d.GetYear(), 2003);
  EXPECT_EQ(d.GetMonth(), 7);
  EXPECT_EQ(d.GetDay(), 21);
  EXPECT_EQ(d.GetHour(), 14);
  EXPECT_EQ(d.GetMinute(), 35);
  EXPECT_EQ(d.GetSecond(), 59);
}

TEST(OleDateTimeTest, LeapDayAndMidnight) {
  COleDateTime d(2004, 2, 29, 23, 59, 59);
  EXPECT_EQ(d.GetDay(), 29);
  EXPECT_EQ(d.GetHour(), 23);

  // rejected: not a leap year
  COleDateTime bad;
  EXPECT_EQ(1, bad.SetDateTime(2003, 2, 29, 0, 0, 0));
  EXPECT_EQ(COleDateTime::invalid, bad.GetStatus());
}

TEST(OleDateTimeTest, ComparisonsAndStatus) {
  COleDateTime base(1971, 1, 1, 0, 0, 0);
  COleDateTime gps(1980, 1, 6, 0, 0, 0);
  EXPECT_TRUE(gps >= base);
  EXPECT_TRUE(base < gps);
  EXPECT_TRUE(gps == COleDateTime(gps.m_dt));

  COleDateTime n;
  n.SetStatus(COleDateTime::null);
  EXPECT_EQ(COleDateTime::null, n.GetStatus());
}

TEST(OleDateTimeTest, DefaultIsValidZero) {
  // MFC default-constructs to (valid, m_dt = 0)
  COleDateTime d;
  EXPECT_EQ(COleDateTime::valid, d.GetStatus());
  EXPECT_DOUBLE_EQ(d.m_dt, 0.0);
}

}  // namespace

// --- RMC parser tests added by the port (the upstream suite covers GGA
// altitude only). The RMC sentence below is the classic NMEA reference
// example. Also pins the port's reimplemented GPS_get_y2k_compliant_year
// pivot (94 -> 1994, see fv_netnmea_missing.cpp).

#include "nmea.h"

namespace {

TEST(NmeaRmcPort, ReferenceSentenceParses) {
  NMEA_sentence s;
  ASSERT_TRUE(s.process_RMC(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));
  EXPECT_NEAR(s.get_latitude(), 48.0 + 7.038 / 60.0, 1e-4);
  EXPECT_NEAR(s.get_longitude(), 11.0 + 31.000 / 60.0, 1e-4);
  EXPECT_TRUE(s.valid_date());
  EXPECT_EQ(s.get_day(), 23);
  EXPECT_EQ(s.get_month(), 3);
  EXPECT_EQ(s.get_year(), 94);  // two-digit year, 1994 via y2k pivot
  EXPECT_EQ(s.get_hour(), 12);
  EXPECT_EQ(s.get_minute(), 35);
  EXPECT_NEAR(s.get_second(), 19.0f, 1e-3);
}

TEST(NmeaRmcPort, SouthernWesternHemispheres) {
  NMEA_sentence s;
  ASSERT_TRUE(s.process_RMC(
      "$GPRMC,201730,A,3229.3086,S,09339.2410,W,000.0,360.0,110726,004.2,E*4F"));
  EXPECT_NEAR(s.get_latitude(), -(32.0 + 29.3086 / 60.0), 1e-4);
  EXPECT_NEAR(s.get_longitude(), -(93.0 + 39.2410 / 60.0), 1e-4);
}

}  // namespace
