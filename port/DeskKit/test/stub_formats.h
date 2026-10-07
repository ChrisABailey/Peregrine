// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// stub_formats.h — catalog formats whose frames are listed in memory. A scan
/// opens no file and nothing registered here can draw.
#pragma once

#include <map>
#include <memory>
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

}  // namespace test
}  // namespace desk
}  // namespace fv
