// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/formats/registry.h — multi-slot format registry (contracts D1),
// generalizing fv_interfaces.h's single-slot pattern. One entry per map
// format ("dted", "geotiff", ...) holding factories for whichever surfaces
// the format supports; absent surfaces are null std::functions.
//
// Kept as its own header (the plan sketched it inside enumerate.h) so the
// interface headers stay dependency-free.
//
// Not thread-safe: registration happens at startup / test SetUp, matching
// the FalconView singletons this replaces.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/formats/enumerate.h"
#include "fvkit/formats/source.h"

namespace fv {

struct FormatFactories {
  std::string format_key;  // lower-case, e.g. "dted"
  std::function<std::shared_ptr<IFrameEnumerator>()> make_enumerator;
  // Rasters open per file; elevation sources index a whole tree.
  std::function<std::shared_ptr<IRasterSource>()> make_raster_source;
  std::function<std::shared_ptr<IElevationSource>(const std::string& root_dir)>
      make_elevation_source;
};

// kInvalidArg on empty key or duplicate registration.
Status RegisterFormat(const FormatFactories& factories);

// nullptr if the key is unknown. The pointer stays valid until
// ClearFormatRegistryForTest().
const FormatFactories* FindFormat(const std::string& format_key);

std::vector<std::string> RegisteredFormatKeys();

// Registers every built-in adapter (dted, geotiff). Idempotent.
void RegisterBuiltinFormats();

void ClearFormatRegistryForTest();

}  // namespace fv
