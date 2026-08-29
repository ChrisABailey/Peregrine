// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/formats/vpf.h — VPF/DNC catalog adapter (vpf-geosym-plan phase V2).
// Enumerates a VPF database's tile coverage into catalog rows over the
// ported reader (fv_vpf). Vector data has no IRasterSource yet (rendering is
// phase V5); this is enumeration only, so viewport queries know which DNC
// libraries/tiles intersect a location — the VPF analog of the DTED/CADRG
// frame enumerators.
//
// Granularity: one FrameInfo per (library, tile). series_key = library name;
// path = "<db_root>|<library>|<tile>" (a locator V5's vector source will
// parse — VPF has no single file per tile). Untiled libraries (e.g. browse)
// contribute one row from the library's overall bounds.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/formats/enumerate.h"

namespace fv {

class VpfFrameEnumerator : public IFrameEnumerator {
 public:
  VpfFrameEnumerator() = default;

  // dir is a VPF database root (the directory containing 'dht' and 'lat').
  Status Begin(const std::string& dir) override;
  bool Next(FrameInfo* info) override;

 private:
  std::vector<FrameInfo> frames_;  // sorted by path
  size_t next_ = 0;
};

// Composes / parses the "<db_root>|<library>|<tile>" locator used in
// FrameInfo.path (tile may be empty for untiled libraries).
std::string MakeVpfLocator(const std::string& db_root,
                           const std::string& library, const std::string& tile);
bool ParseVpfLocator(const std::string& locator, std::string* db_root,
                     std::string* library, std::string* tile);

}  // namespace fv
