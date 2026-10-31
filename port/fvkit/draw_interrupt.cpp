// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/draw_interrupt.h"

namespace fv {

namespace {
thread_local const std::function<bool()>* current = nullptr;
}  // namespace

DrawInterruptScope::DrawInterruptScope(const std::function<bool()>& interrupted)
    : previous_(current) {
  current = &interrupted;
}

DrawInterruptScope::~DrawInterruptScope() { current = previous_; }

bool DrawInterruptible() { return current != nullptr && *current; }

bool DrawInterrupted() { return current != nullptr && *current && (*current)(); }

}  // namespace fv
