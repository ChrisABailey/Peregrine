// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPSearch+Internal.h — how a search row is MINTED. PRIVATE to PippinKit for
// `PPFix+Internal.h`'s reason: there is a C++ type in the signature, so it is
// not in the umbrella header and Swift never sees it.

#pragma once

#import <PippinKit/PPSearch.h>

#include "PPSearchRules.h"

NS_ASSUME_NONNULL_BEGIN

@interface PPSearchResult ()

/// One row, converted once. The row arrives already CLASSIFIED and already
/// de-duplicated — both are `PPSearchRules.h`'s, so that the two decisions a
/// mac test can check are made where the mac test can reach them, and this
/// initializer is a conversion and nothing more.
- (instancetype)initWithRow:(const pippin::SearchRow &)row
    NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END
