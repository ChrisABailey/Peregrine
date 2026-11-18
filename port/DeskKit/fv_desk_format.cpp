// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_format.h"

#include <cmath>
#include <cstdio>

namespace fv {
namespace desk {

std::string FormatFeet(double feet) {
  const long long n = std::llround(feet);
  std::string digits = std::to_string(n < 0 ? -n : n);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(i, ",");
  return (n < 0 ? "-" : "") + digits + " ft";
}

std::string FormatPosition(const GeoPoint& g) {
  auto part = [](double v, int deg_width, char pos, char neg) {
    const char hemi = v < 0 ? neg : pos;
    double a = std::fabs(v);
    int deg = static_cast<int>(a);
    double min = (a - deg) * 60.0;
    // Rounding to the printed precision must carry into the degrees.
    if (min >= 59.9995) {
      min = 0.0;
      ++deg;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*d\xC2\xB0%06.3f' %c", deg_width, deg, min, hemi);
    return std::string(buf);
  };
  return part(g.lat, 2, 'N', 'S') + "  " + part(g.lon, 3, 'E', 'W');
}

namespace {
/// Splits seconds since the epoch into UTC calendar fields without the
/// platform's gmtime (not thread-safe on every target).
void UtcFields(double time_s, int* y, int* mo, int* d, int* h, int* mi, int* s) {
  const long long t = static_cast<long long>(std::floor(time_s));
  long long days = t / 86400;
  long long rem = t % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  *h = static_cast<int>(rem / 3600);
  *mi = static_cast<int>(rem % 3600 / 60);
  *s = static_cast<int>(rem % 60);
  // Howard Hinnant's civil_from_days.
  days += 719468;
  const long long era = (days >= 0 ? days : days - 146096) / 146097;
  const long long doe = days - era * 146097;
  const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const long long mp = (5 * doy + 2) / 153;
  *d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  *mo = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  *y = static_cast<int>(yoe + era * 400 + (*mo <= 2 ? 1 : 0));
}
}  // namespace

std::string FormatUtc(double time_s) {
  int y, mo, d, h, mi, s;
  UtcFields(time_s, &y, &mo, &d, &h, &mi, &s);
  char buf[48];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d UTC", y, mo, d, h, mi, s);
  return buf;
}

std::string FormatUtcTimeOfDay(double time_s) {
  int y, mo, d, h, mi, s;
  UtcFields(time_s, &y, &mo, &d, &h, &mi, &s);
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", h, mi, s);
  return buf;
}

}  // namespace desk
}  // namespace fv
