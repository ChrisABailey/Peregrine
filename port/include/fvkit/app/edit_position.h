// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/app/edit_position.h — what a pixel means to an editor.

#pragma once

#include <string>

#include "fvkit/geo.h"

namespace fv {

/// A position an editor placed, and whether a snap produced it.
///
/// Carried rather than a bare GeoPoint because a shell wants to say so:
/// Pippin's button reads "Use Ruddy Turnstone" and a desktop status bar says
/// the same in one string. A snap that happens silently is indistinguishable
/// from a drag that missed.
struct EditPosition {
  GeoPoint position;
  /// False when nothing has drawn the overlay yet, or the pixel is off the
  /// projection. A caller that gets this holds its position — it does not
  /// guess a coordinate.
  bool valid = false;
  bool snapped = false;
  /// The candidate's own description ("Points: Ruddy Turnstone"), empty when
  /// nothing was snapped to.
  std::string snapped_to;
};

}  // namespace fv
