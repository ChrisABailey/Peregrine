// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// stub_formats.h — catalog formats whose frames are listed in memory. A scan
/// opens no file. `RegisterStubFormat` cannot draw; `RegisterDrawableStubFormat`
/// draws each frame in one colour, and its reads can be counted and held.
#pragma once

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fv_map_enums.h"
#include "fvkit/formats/enumerate.h"
#include "fvkit/formats/registry.h"

namespace fv {
namespace desk {
namespace test {

/// The frames each stub format's enumerator yields, by format key.
inline std::map<std::string, std::vector<FrameInfo>>& StubFrames() {
  static std::map<std::string, std::vector<FrameInfo>> frames;
  return frames;
}

class StubEnumerator : public IFrameEnumerator {
 public:
  explicit StubEnumerator(std::string key) : key_(std::move(key)) {}
  Status Begin(const std::string&) override {
    next_ = 0;
    return Status::Ok();
  }
  bool Next(FrameInfo* info) override {
    const auto& f = StubFrames()[key_];
    if (next_ >= f.size()) return false;
    *info = f[next_++];
    return true;
  }

 private:
  std::string key_;
  size_t next_ = 0;
};

/// A frame of a 1:`scale` series.
inline FrameInfo Frame(const char* path, GeoRect r, const char* key, double scale) {
  FrameInfo f;
  f.path = path;
  f.bounds = r;
  f.series_key = key;
  f.scale = scale;
  f.scale_units = MAP_SCALE_DENOMINATOR;
  f.size_bytes = 1;
  return f;
}

/// Registers a stub enumerator under `key`.
inline Status RegisterStubFormat(const std::string& key) {
  FormatFactories f;
  f.format_key = key;
  f.make_enumerator = [key] { return std::make_shared<StubEnumerator>(key); };
  return RegisterFormat(f);
}

/// Counts the drawable stub's block reads and can hold them, so a test can
/// act while the render worker is inside a base-map frame.
class StubReads {
 public:
  /// Called by each read; waits while held.
  void Read() {
    std::unique_lock<std::mutex> lock(mu_);
    ++reads_;
    if (!hold_) return;
    waiting_ = true;
    cv_.notify_all();
    cv_.wait(lock, [this] { return !hold_; });
    waiting_ = false;
  }
  void Hold() {
    std::lock_guard<std::mutex> lock(mu_);
    hold_ = true;
  }
  void Release() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      hold_ = false;
    }
    cv_.notify_all();
  }
  /// Blocks until a read is waiting on the hold.
  void WaitUntilHeld() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return waiting_; });
  }
  int Count() {
    std::lock_guard<std::mutex> lock(mu_);
    return reads_;
  }
  void Reset() {
    std::lock_guard<std::mutex> lock(mu_);
    reads_ = 0;
    hold_ = false;
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  int reads_ = 0;
  bool hold_ = false;
  bool waiting_ = false;
};

inline StubReads& StubRasterReads() {
  static StubReads reads;
  return reads;
}

/// An equal-arc frame of `kSize` square pixels, one opaque colour, whose
/// bounds come from the `StubFrames()` entry with the same path.
class StubRasterSource : public IRasterSource {
 public:
  static constexpr int kSize = 64;

  Status Open(const std::string& path) override {
    for (const auto& format : StubFrames())
      for (const FrameInfo& f : format.second) {
        const std::string tail = "/" + f.path;
        if (path == f.path || (path.size() > tail.size() &&
                               path.compare(path.size() - tail.size(), tail.size(), tail) == 0)) {
          bounds_ = f.bounds;
          return Status::Ok();
        }
      }
    return Status::Error(kNotFound, "no stub frame " + path);
  }
  GeoRect Bounds() const override { return bounds_; }
  Status Info(ImageInfo* info) const override {
    info->size = PixelSize{kSize, kSize};
    info->bounds = bounds_;
    return Status::Ok();
  }
  Status ReadBlock(const PixelRect& r, PixelBuffer* out) override {
    StubRasterReads().Read();
    *out = PixelBuffer(r.width, r.height);
    for (int y = 0; y < r.height; ++y) {
      unsigned char* row = out->Row(y);
      for (int x = 0; x < r.width; ++x) {
        row[4 * x + 0] = 40;
        row[4 * x + 1] = 120;
        row[4 * x + 2] = 200;
        row[4 * x + 3] = 255;
      }
    }
    return Status::Ok();
  }
  Status PixelToGeo(double px, double py, GeoPoint* p) const override {
    p->lon = bounds_.ll.lon + (px + 0.5) * (bounds_.ur.lon - bounds_.ll.lon) / kSize;
    p->lat = bounds_.ur.lat - (py + 0.5) * (bounds_.ur.lat - bounds_.ll.lat) / kSize;
    return Status::Ok();
  }
  Status GeoToPixel(const GeoPoint& p, double* px, double* py) const override {
    *px = (p.lon - bounds_.ll.lon) * kSize / (bounds_.ur.lon - bounds_.ll.lon) - 0.5;
    *py = (bounds_.ur.lat - p.lat) * kSize / (bounds_.ur.lat - bounds_.ll.lat) - 0.5;
    return Status::Ok();
  }

 private:
  GeoRect bounds_;
};

/// Registers a stub enumerator under `key` whose frames draw.
inline Status RegisterDrawableStubFormat(const std::string& key) {
  FormatFactories f;
  f.format_key = key;
  f.make_enumerator = [key] { return std::make_shared<StubEnumerator>(key); };
  f.make_raster_source = [] { return std::make_shared<StubRasterSource>(); };
  return RegisterFormat(f);
}

}  // namespace test
}  // namespace desk
}  // namespace fv
