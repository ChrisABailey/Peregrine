// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPWind+Internal.h — how the wind objects are made. Private to PippinKit.

#import <PippinKit/PPWind.h>

#include "fvkit/nav/wind.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPWindSample ()
- (instancetype)initWithSample:(const fv::nav::WindSample &)sample NS_DESIGNATED_INITIALIZER;
@end

@interface PPWindSettings ()
/// Made by the map from the pack's settings.
- (instancetype)initWithLatitude:(double)latitude
                       longitude:(double)longitude
               beachFacesDegrees:(double)faces
      onshoreWarnMetersPerSecond:(double)onshoreWarn
     headwindWarnMetersPerSecond:(double)headwindWarn NS_DESIGNATED_INITIALIZER;
@end

NS_ASSUME_NONNULL_END
