// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/formats/scan_interrupt.h"

namespace fv {

namespace {
thread_local const std::function<bool()>* current = nullptr;
}  // namespace

ScanInterruptScope::ScanInterruptScope(const std::function<bool()>& interrupted)
    : previous_(current) {
  current = &interrupted;
}

ScanInterruptScope::~ScanInterruptScope() { current = previous_; }

bool ScanInterrupted() { return current != nullptr && *current && (*current)(); }

}  // namespace fv
