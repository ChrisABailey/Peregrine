// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// peregrine_core.cpp — the shared core's own translation unit. Everything
/// else in libperegrine_core comes whole from the static libraries it links.
#include "peregrine/fv_sdk_build_id.h"

/// The build id this core was configured with, for diagnostics.
extern "C" const char* fv_peregrine_core_build_id() { return FV_SDK_BUILD_ID; }
