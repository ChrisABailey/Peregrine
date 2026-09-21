// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPGuidance+Internal.h — how a guidance value is MADE. PRIVATE to PippinKit
// for `PPTrip+Internal.h`'s reason: there is a C++ type in the signature, so
// it is not in the umbrella header and Swift never sees it.

#pragma once

#import <PippinKit/PPGuidance.h>

#include "fvkit/nav/guidance.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPGuidanceEvent ()

- (instancetype)initWithEvent:(const fv::nav::GuidanceEvent &)event
    NS_DESIGNATED_INITIALIZER;

@end

@interface PPGuidance ()

- (instancetype)initWithState:(const fv::nav::GuidanceState &)state
                         road:(NSString *)road
    NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END
