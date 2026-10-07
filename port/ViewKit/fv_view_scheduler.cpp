// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_view_scheduler.h"

#include <utility>

namespace fv {
namespace view {

RenderScheduler::RenderScheduler(RenderFn render, DeliverFn deliver)
    : render_(std::move(render)), deliver_(std::move(deliver)) {
  worker_ = std::thread([this] { Run(); });
}

RenderScheduler::~RenderScheduler() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
    pending_ = false;
  }
  wake_.notify_all();
  worker_.join();
}

void RenderScheduler::Request(const Viewport& view, uint64_t generation) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (stop_ || (have_queued_gen_ && generation == queued_gen_)) return;
    have_queued_gen_ = true;
    queued_gen_ = generation;
    pending_view_ = view;
    pending_gen_ = generation;
    pending_ = true;
  }
  wake_.notify_one();
}

void RenderScheduler::WaitIdle() {
  std::unique_lock<std::mutex> lock(mu_);
  idle_.wait(lock, [this] { return stop_ || (!pending_ && !busy_); });
}

std::shared_ptr<const Frame> RenderScheduler::LastFrame() const {
  std::lock_guard<std::mutex> lock(mu_);
  return last_;
}

uint64_t RenderScheduler::FramesRendered() const {
  std::lock_guard<std::mutex> lock(mu_);
  return rendered_;
}

void RenderScheduler::Run() {
  std::unique_lock<std::mutex> lock(mu_);
  for (;;) {
    wake_.wait(lock, [this] { return stop_ || pending_; });
    if (stop_) break;
    const Viewport view = pending_view_;
    const uint64_t gen = pending_gen_;
    pending_ = false;
    busy_ = true;
    lock.unlock();

    std::shared_ptr<const Frame> frame = render_ ? render_(view, gen) : nullptr;
    if (frame) {
      lock.lock();
      last_ = frame;
      ++rendered_;
      lock.unlock();
      // Outside the lock so the callback may call back in.
      if (deliver_) deliver_(frame);
    }

    lock.lock();
    busy_ = false;
    if (!pending_) idle_.notify_all();
  }
  busy_ = false;
  idle_.notify_all();
}

}  // namespace view
}  // namespace fv
