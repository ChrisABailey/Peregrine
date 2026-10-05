// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RideActivityAttributes.swift — the ride's Live Activity type, shared by the
// app (which starts and updates it) and PippinActivity (which draws it).

import ActivityKit

/// A guided ride. Nothing is fixed for the life of the activity; all of it is
/// in the content state.
struct RideActivityAttributes: ActivityAttributes {
    typealias ContentState = RideActivityContent
}
