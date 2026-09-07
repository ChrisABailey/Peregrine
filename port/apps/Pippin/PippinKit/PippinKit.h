// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PippinKit.h — the umbrella header of the bridge.
//
// The boundary rule this directory's shape enforces: nothing UIKit or
// Foundation leaks below PippinKit, and nothing `std::` leaks above it. The
// public headers are Objective-C that Swift can see; everything behind them
// is C++17 the mac test bed exercises. Same arrangement as pyfvw, and for the
// same reason: a curated facade is reviewable, and a mechanical interop layer
// over `shared_ptr`/`Status`/`std::function` is not.

#import <Foundation/Foundation.h>

#import <PippinKit/PPFix.h>
#import <PippinKit/PPGeometry.h>
#import <PippinKit/PPLocationSource.h>
#import <PippinKit/PPMap.h>
#import <PippinKit/PPPixelProbe.h>
#import <PippinKit/PPPoint.h>
#import <PippinKit/PPRoute.h>
#import <PippinKit/PPSearch.h>
#import <PippinKit/PPTrip.h>
#import <PippinKit/PPViewport.h>

FOUNDATION_EXPORT double PippinKitVersionNumber;
FOUNDATION_EXPORT const unsigned char PippinKitVersionString[];
