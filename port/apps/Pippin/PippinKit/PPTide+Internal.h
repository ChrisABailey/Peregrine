// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPTide+Internal.h — how a PPTide is made. Private to PippinKit: the
// signature carries a C++ type, so it stays out of the umbrella header.

#pragma once

#import <PippinKit/PPTide.h>

#include <memory>

#include "fvkit/nav/tide.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPTide ()

/// Takes a loaded, non-empty table.
- (instancetype)initWithTable:(std::shared_ptr<const fv::nav::TideTable>)table
          rideableBelowMeters:(double)rideableBelow
          walkEasyBelowMeters:(double)walkEasyBelow NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END
