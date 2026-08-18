// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/nav/position.h"

#include <cmath>
#include <utility>

namespace fv {

void PositionFix::Merge(const PositionFix& other) {
  if (other.has_position) {
    lat = other.lat;
    lon = other.lon;
    has_position = true;
  }
  if (other.has_altitude) {
    altitude_msl_m = other.altitude_msl_m;
    has_altitude = true;
  }
  if (other.has_speed) {
    speed_mps = other.speed_mps;
    has_speed = true;
  }
  if (other.has_true_heading) {
    true_heading_deg = other.true_heading_deg;
    has_true_heading = true;
  }
  if (other.has_magnetic_heading) {
    magnetic_heading_deg = other.magnetic_heading_deg;
    has_magnetic_heading = true;
  }
  if (other.has_time) {
    time_s = other.time_s;
    has_time = true;
  }
  if (other.has_hdop) {
    hdop = other.hdop;
    has_hdop = true;
  }
  if (other.has_satellite_count) {
    satellite_count = other.satellite_count;
    has_satellite_count = true;
  }
}

double NormalizeHeadingDeg(double degrees) {
  if (!std::isfinite(degrees)) return 0.0;
  double d = std::fmod(degrees, 360.0);
  if (d < 0.0) d += 360.0;
  // fmod of a tiny negative can round to exactly 360.0 on the way back.
  if (d >= 360.0) d = 0.0;
  return d;
}

// ---------------------------------------------------------------------------
// FixQueue
// ---------------------------------------------------------------------------

void FixQueue::Push(const PositionFix& fix) {
  std::lock_guard<std::mutex> lock(mutex_);
  while (fixes_.size() >= capacity_) {
    fixes_.pop_front();
    ++dropped_;
  }
  fixes_.push_back(fix);
}

FixCallback FixQueue::Listener() {
  return [this](const PositionFix& fix) { Push(fix); };
}

std::size_t FixQueue::Drain(std::vector<PositionFix>* out) {
  std::deque<PositionFix> taken;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taken.swap(fixes_);
  }
  if (out != nullptr) {
    out->reserve(out->size() + taken.size());
    for (auto& fix : taken) out->push_back(std::move(fix));
  }
  return taken.size();
}

bool FixQueue::DrainLatest(PositionFix* out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fixes_.empty()) return false;
  if (out != nullptr) *out = fixes_.back();
  fixes_.clear();
  return true;
}

void FixQueue::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  fixes_.clear();
}

std::size_t FixQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return fixes_.size();
}

std::size_t FixQueue::dropped() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_;
}

}  // namespace fv
