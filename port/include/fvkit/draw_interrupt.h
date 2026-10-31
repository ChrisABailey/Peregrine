// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/draw_interrupt.h — a cooperative stop for a draw in progress.
///
/// A caller that can abandon a draw wraps it in a `DrawInterruptScope`. The
/// long loops under an overlay's draw (the feature query, the vector scene
/// build, the vector draw loop) poll `DrawInterrupted()` every few hundred
/// items and return kInterrupted when it is true. Outside a scope, and on
/// any other thread, `DrawInterrupted()` is false.
#pragma once

#include <functional>

namespace fv {

/// Installs `interrupted` for draws on the calling thread until destroyed;
/// scopes nest. `interrupted` must outlive the scope; an empty one never fires.
class DrawInterruptScope {
 public:
  explicit DrawInterruptScope(const std::function<bool()>& interrupted);
  ~DrawInterruptScope();
  DrawInterruptScope(const DrawInterruptScope&) = delete;
  DrawInterruptScope& operator=(const DrawInterruptScope&) = delete;

 private:
  const std::function<bool()>* previous_;
};

/// True when the innermost scope on this thread asks the draw to stop.
bool DrawInterrupted();

/// True when a scope that can fire is installed on this thread.
bool DrawInterruptible();

/// Polls `DrawInterrupted()` on every 256th call of `i`; for hot loops.
inline bool DrawInterruptedAt(size_t i) { return (i & 255) == 255 && DrawInterrupted(); }

}  // namespace fv
