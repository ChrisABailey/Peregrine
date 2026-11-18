// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// plugin.cpp — registers the peregrine.ais package's one overlay type.
#include <memory>

#include "ais_traffic.h"
#include "peregrine/fv_native.h"

namespace {

/// Sent on every Open Waters request, which asks clients to name themselves.
constexpr char kUserAgent[] = "Peregrine-AIS/1.0.0 (github.com/ChrisABailey/Peregrine)";

}  // namespace

FV_NATIVE_PLUGIN_BUILD_ID()

FV_NATIVE_EXPORT int fv_native_register(fv::desk::NativeRegistrar* r) {
  fv::desk::NativeOverlay traffic;
  traffic.factory = [] { return std::make_shared<peregrine_ais::TrafficOverlay>(kUserAgent); };
  return r->AddOverlay("traffic", std::move(traffic)).ok() ? 0 : 1;
}
