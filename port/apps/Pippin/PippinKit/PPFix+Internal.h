// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPFix+Internal.h — how a fix is MADE, and the C++ it is made of. PRIVATE to
// PippinKit for `PPViewport+Internal.h`'s reason: there is a C++ type in the
// signature, so it is not in the umbrella header and Swift never sees it.
//
// A `PPFix` IS a `PPLocationSample` — the CLLocation fields as plain numbers —
// and every public property reads one of them. That keeps the sentinel rules
// in exactly one place (`PPLocationFix.h`, tested on the mac) instead of one
// copy in the ObjC properties and another in the conversion.

#pragma once

#import <PippinKit/PPFix.h>

#include "PPLocationFix.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPFix ()

- (instancetype)initWithSample:(const PPLocationSample &)sample
    NS_DESIGNATED_INITIALIZER;

/// The sample this fix is, for the one consumer that turns it into an
/// `fv::PositionFix` (`PPMap`, on the render queue). A method rather than a
/// property: a C++ reference is not something `@synthesize` should be asked
/// to store.
- (const PPLocationSample &)sample;

@end

NS_ASSUME_NONNULL_END
