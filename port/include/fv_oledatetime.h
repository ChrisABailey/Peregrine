// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_oledatetime.h — minimal POSIX implementation of MFC's COleDateTime for
// the FalconView port (COleDateTime appears in ~52 fvw_core files).
//
// Storage matches OLE automation DATE: a double counting days since
// 1899-12-30 00:00:00, time-of-day in the fractional part. Field getters use
// MFC's half-second rounding (MFC converts via seconds = floor(frac*86400
// + 0.5)).
//
// Implemented subset (grown test-first, see port/NetNMEA/test/):
//   ctors (default = valid, m_dt 0 — matches MFC), (y,mo,d,h,mi,s), (DATE)
//   SetDateTime / SetDate / SetTime / SetStatus / GetStatus
//   GetYear/Month/Day/Hour/Minute/Second, comparison operators, m_dt access
//
// Semantic deltas vs MFC (documented, acceptable for FalconView use):
//   * Dates before 1899-12-30 (negative DATE, which OLE encodes bizarrely)
//     are outside supported range — SetDateTime rejects years < 1900.
//     FalconView date logic bottoms out at 1971 (GPS_VALID_BASE_DATE).
//   * No locale Format(); not used by ported code.

#pragma once

#ifndef _WIN32

typedef double DATE;

class COleDateTime {
 public:
  enum DateTimeStatus { error = -1, valid = 0, invalid = 1, null = 2 };

  COleDateTime() : m_dt(0), m_status(valid) {}
  COleDateTime(DATE dtSrc) : m_dt(dtSrc), m_status(valid) {}
  COleDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin,
               int nSec) {
    SetDateTime(nYear, nMonth, nDay, nHour, nMin, nSec);
  }

  int SetDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin,
                  int nSec) {
    // MFC accepts years 100-9999; dates before the 1899-12-30 epoch encode
    // negatively here (plain negative days, not OLE's odd negative format —
    // FalconView never uses pre-1899 dates).
    if (nYear < 100 || nYear > 9999 || nMonth < 1 || nMonth > 12 || nDay < 1 ||
        nDay > days_in_month(nYear, nMonth) || nHour < 0 || nHour > 23 ||
        nMin < 0 || nMin > 59 || nSec < 0 || nSec > 59) {
      m_status = invalid;
      m_dt = 0;
      return 1;
    }
    long days = days_from_civil(nYear, nMonth, nDay) - kOleEpochDays;
    double frac = (nHour * 3600.0 + nMin * 60.0 + nSec) / 86400.0;
    m_dt = days + frac;
    m_status = valid;
    return 0;
  }
  int SetDate(int nYear, int nMonth, int nDay) {
    return SetDateTime(nYear, nMonth, nDay, 0, 0, 0);
  }
  int SetTime(int nHour, int nMin, int nSec) {
    return SetDateTime(1899, 12, 30, nHour, nMin, nSec) == 0
               ? 0
               : (m_status = invalid, 1);
  }

  void SetStatus(DateTimeStatus status) { m_status = status; }
  DateTimeStatus GetStatus() const { return m_status; }

  int GetYear() const { int y, m, d; civil(&y, &m, &d); return y; }
  int GetMonth() const { int y, m, d; civil(&y, &m, &d); return m; }
  int GetDay() const { int y, m, d; civil(&y, &m, &d); return d; }
  int GetHour() const { return (int)(rounded_day_seconds() / 3600); }
  int GetMinute() const { return (int)((rounded_day_seconds() / 60) % 60); }
  int GetSecond() const { return (int)(rounded_day_seconds() % 60); }

  operator DATE() const { return m_dt; }

  // MFC's arithmetic takes COleDateTimeSpan, implicitly constructed from a
  // double measured in DAYS. Callers passing seconds (NetNMEA nmea.cpp does)
  // get the same wrong-unit behavior as on Windows.
  COleDateTime& operator+=(double days) {
    m_dt += days;
    return *this;
  }
  COleDateTime& operator-=(double days) {
    m_dt -= days;
    return *this;
  }

  bool operator==(const COleDateTime& o) const { return m_dt == o.m_dt; }
  bool operator!=(const COleDateTime& o) const { return m_dt != o.m_dt; }
  bool operator<(const COleDateTime& o) const { return m_dt < o.m_dt; }
  bool operator>(const COleDateTime& o) const { return m_dt > o.m_dt; }
  bool operator<=(const COleDateTime& o) const { return m_dt <= o.m_dt; }
  bool operator>=(const COleDateTime& o) const { return m_dt >= o.m_dt; }

  DATE m_dt;

 private:
  DateTimeStatus m_status;

  // days_from_civil(1899,12,30), the OLE epoch, relative to 1970-01-01
  static const long kOleEpochDays = -25569;

  static bool is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  }
  static int days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && is_leap(y) ? 29 : d[m - 1];
  }
  // Howard Hinnant's days-from-civil (days since 1970-01-01)
  static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
  }
  static void civil_from_days(long z, int* y, int* m, int* d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long yy = (long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp + (mp < 10 ? 3 : -9));
    *y = (int)(yy + (*m <= 2));
  }

  // Whole days / rounded seconds-within-day, MFC-style (+0.5s rounding),
  // carrying rounding overflow into the day.
  long rounded_total_seconds() const {
    return (long)(m_dt * 86400.0 + 0.5);
  }
  long day_number() const { return rounded_total_seconds() / 86400; }
  long rounded_day_seconds() const { return rounded_total_seconds() % 86400; }

  void civil(int* y, int* m, int* d) const {
    // day_number() counts from the OLE epoch (1899-12-30); shift to
    // days-since-1970 for civil_from_days.
    civil_from_days(day_number() + kOleEpochDays, y, m, d);
  }
};

#endif  // !_WIN32
