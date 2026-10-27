// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/formats/scan_interrupt.h — a cooperative stop for a frame scan.
///
/// A caller that can abandon a scan (a catalog build the user cancels) wraps
/// it in a `ScanInterruptScope`. Frame enumerators poll `ScanInterrupted()`
/// once per directory entry of their walk and return kInterrupted when it is
/// true, so a scan of a slow network tree stops within an entry or two.
/// Outside a scope, and on any other thread, `ScanInterrupted()` is false.
#pragma once

#include <functional>

namespace fv {

/// Installs `interrupted` for scans on the calling thread until destroyed;
/// scopes nest. `interrupted` must outlive the scope; an empty one never fires.
class ScanInterruptScope {
 public:
  explicit ScanInterruptScope(const std::function<bool()>& interrupted);
  ~ScanInterruptScope();
  ScanInterruptScope(const ScanInterruptScope&) = delete;
  ScanInterruptScope& operator=(const ScanInterruptScope&) = delete;

 private:
  const std::function<bool()>* previous_;
};

/// True when the innermost scope on this thread asks the scan to stop.
bool ScanInterrupted();

}  // namespace fv
