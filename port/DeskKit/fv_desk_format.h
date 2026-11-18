// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_format.h — the text the window shows for positions, heights and times.
#pragma once

#include <string>

#include "fvkit/geo.h"

namespace fv {
namespace desk {

/// "1,234 ft", rounded to the foot, thousands separated.
std::string FormatFeet(double feet);

/// "32°48.123' N  079°54.456' W" — degrees and decimal minutes.
std::string FormatPosition(const GeoPoint& g);

/// "2026-11-09 14:03:22 UTC" for seconds since 1970-01-01 UTC, rounded down to the second.
std::string FormatUtc(double time_s);

/// "14:03:22" UTC, the time of day only.
std::string FormatUtcTimeOfDay(double time_s);

}  // namespace desk
}  // namespace fv
