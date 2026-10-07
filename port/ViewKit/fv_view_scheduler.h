// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_view_scheduler.h — renders map frames on one worker thread.
///
/// The UI thread posts the latest `Viewport` snapshot with its generation;
/// the worker renders the newest request and drops any it has overtaken, so
/// a gesture produces at most one frame in flight and one waiting. A request
/// whose generation equals the last one rendered or queued is ignored. The
/// last delivered frame is kept for the shell's mid-gesture preview
/// (`PreviewTransform`).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "fv_view_viewport.h"

namespace fv {
namespace view {

/// A finished frame: RGBA8, premultiplied as the renderer wrote it, row-major
/// at the viewport's pixel size.
struct Frame {
  Viewport viewport;
  uint64_t generation = 0;
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;
};

class RenderScheduler {
 public:
  /// Draws one frame. Runs on the worker; it alone touches the engine.
  /// Returning null drops the request.
  using RenderFn =
      std::function<std::shared_ptr<const Frame>(const Viewport&, uint64_t generation)>;
  /// Receives each finished frame on the worker; a shell forwards it to its
  /// UI thread.
  using DeliverFn = std::function<void(std::shared_ptr<const Frame>)>;

  RenderScheduler(RenderFn render, DeliverFn deliver);
  /// Finishes the frame in progress, drops any waiting request, joins.
  ~RenderScheduler();
  RenderScheduler(const RenderScheduler&) = delete;
  RenderScheduler& operator=(const RenderScheduler&) = delete;

  /// Asks for a frame of `view`. Replaces any request not yet started.
  void Request(const Viewport& view, uint64_t generation);

  /// Blocks until no request is waiting or rendering.
  void WaitIdle();

  /// The most recently delivered frame, or null.
  std::shared_ptr<const Frame> LastFrame() const;

  /// Frames rendered (non-null) since construction.
  uint64_t FramesRendered() const;

 private:
  void Run();

  RenderFn render_;
  DeliverFn deliver_;

  mutable std::mutex mu_;
  std::condition_variable wake_;
  std::condition_variable idle_;
  bool stop_ = false;
  bool pending_ = false;
  bool busy_ = false;
  bool have_queued_gen_ = false;
  uint64_t queued_gen_ = 0;  // generation of the newest accepted request
  Viewport pending_view_;
  uint64_t pending_gen_ = 0;
  std::shared_ptr<const Frame> last_;
  uint64_t rendered_ = 0;
  std::thread worker_;
};

}  // namespace view
}  // namespace fv
