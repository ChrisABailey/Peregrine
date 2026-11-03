// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// A native plugin stamped with another Peregrine's build id. The host must
/// refuse it without loading it; its static initialiser records a load.
#include <cstdlib>

#include "peregrine/fv_native.h"

#undef FV_SDK_BUILD_ID
#define FV_SDK_BUILD_ID "0.9.0/000000000000/AppleClang-1.0/libc++/Darwin-arm64"

namespace {
const int kLoaded = (setenv("FV_STALE_NATIVE_LOADED", "1", 1), 0);
}

FV_NATIVE_PLUGIN_BUILD_ID()

FV_NATIVE_EXPORT int fv_native_register(fv::desk::NativeRegistrar*) { return kLoaded; }
