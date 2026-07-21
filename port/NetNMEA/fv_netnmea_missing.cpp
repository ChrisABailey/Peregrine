// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Definitions declared in fvw_core/NetNMEA/gps.h but implemented outside the
// original fvw_core-only snapshot (the full tree arrived 2026-07-16).
//
// GPS_get_y2k_compliant_year: verified against the original at
// Applications/FalconView/MovingMapOverlay/gps.cpp (GPS epoch is 1980; the
// original keeps a 10-year buffer): two-digit years 71-99 -> 1900s,
// 00-70 -> 2000s (pivot 70, NOT the more common pivot 80 this port first
// guessed). Values >= 100 pass through unchanged (e.g. the 255 "invalid"
// marker, or already-4-digit years).

int GPS_get_y2k_compliant_year(int year)
{
   if (year < 100)
   {
      if (year > 70)
         year += 1900;  // 71-99 -> 1971-1999
      else
         year += 2000;  // 00-70 -> 2000-2070
   }
   return year;
}
