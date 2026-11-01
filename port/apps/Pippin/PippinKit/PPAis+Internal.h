// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPAis+Internal.h — the parsed C++ frame behind `PPOpenWatersFrame`. Private
// to PippinKit.

#import <PippinKit/PPAis.h>

#include "fvkit/ais/openwaters.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPOpenWatersLimits ()
- (instancetype)initWithLimits:(const fv::ais::OpenWatersLimits &)limits NS_DESIGNATED_INITIALIZER;
@end

@interface PPOpenWatersFrame ()
- (instancetype)initPrivate NS_DESIGNATED_INITIALIZER;
@property(nonatomic, readonly) const fv::ais::OpenWatersFrame &frame;
@end

NS_ASSUME_NONNULL_END
