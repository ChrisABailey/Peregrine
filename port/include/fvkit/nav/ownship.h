// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// The ownship: where the ship is, when, and which way it is heading, as the
/// map window's topmost moving map last reported it.

#ifndef FVKIT_NAV_OWNSHIP_H_
#define FVKIT_NAV_OWNSHIP_H_

#include <cstdint>
#include <memory>
#include <string>

#include "fvkit/nav/heading.h"
#include "fvkit/nav/position.h"

namespace fv {

class Overlay;

/// Whether the ownship follows a live feed or a recorded track.
enum class OwnshipMode { kNone, kLive, kReplay };

/// The ownship state. Each field of `fix` carries its own validity flag.
struct OwnshipState {
  OwnshipMode mode = OwnshipMode::kNone;
  /// The fix the state was taken from: position, altitude, speed, and the
  /// heading the receiver reported. `fix.time_s` is always set (see `time_from_fix`).
  PositionFix fix;
  /// False when the fix carried no time and `fix.time_s` is when it arrived.
  bool time_from_fix = false;
  /// True heading: the reported one, else the bearing of recent movement.
  /// Not a moving map's drawn angle, which a derived heading takes in screen space.
  ResolvedHeading heading;
  /// The moving map the state came from, and its name.
  std::weak_ptr<Overlay> source;
  std::string source_name;
  /// Changes whenever any field does.
  uint64_t generation = 0;

  bool has_position() const { return mode != OwnshipMode::kNone && fix.has_position; }
};

/// Gives overlays the ownship without depending on the window that owns it.
class IOwnshipProvider {
 public:
  virtual ~IOwnshipProvider() = default;
  virtual const OwnshipState& ownship() const = 0;
};

}  // namespace fv

#endif  // FVKIT_NAV_OWNSHIP_H_
