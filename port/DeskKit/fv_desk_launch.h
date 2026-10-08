// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_launch.h — the desktop app's command-line options.
///
/// Every shell reads the same options with the same meaning
/// (port/apps/Peregrine/README.md): `--catalog <path>`,
/// `--center lat,lon[,scale]` and `--shot <png>`. Anything else is ignored,
/// so toolkit options pass through untouched.
#pragma once

#include <string>
#include <vector>

namespace fv {
namespace desk {

/// The launch options a shell acts on.
struct LaunchOptions {
  std::string catalog;  ///< "" when not given
  std::string shot;     ///< "" when not given
  bool has_center = false;
  double center_lat = 0;
  double center_lon = 0;
  /// The scale denominator of `--center`, or 0 to keep the current scale.
  double center_scale = 0;
};

/// Parses `argv` (argv[0] is the program and is skipped). A later option
/// replaces an earlier one; a `--center` that does not hold two or three
/// numbers is ignored.
LaunchOptions ParseLaunchOptions(const std::vector<std::string>& argv);

}  // namespace desk
}  // namespace fv
