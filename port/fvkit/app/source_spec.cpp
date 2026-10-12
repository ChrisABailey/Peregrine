// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/app/source_spec.h"

#include <cstring>

namespace fv {
namespace app {

const char kLayerSpecSeparator[] = "|layername=";

SourceSpec SplitSourceSpec(const std::string& spec) {
  SourceSpec out;
  const size_t at = spec.rfind(kLayerSpecSeparator);
  if (at == std::string::npos) {
    out.path = spec;
    return out;
  }
  out.path = spec.substr(0, at);
  out.layer = spec.substr(at + std::strlen(kLayerSpecSeparator));
  return out;
}

std::string JoinSourceSpec(const std::string& path, const std::string& layer) {
  return layer.empty() ? path : path + kLayerSpecSeparator + layer;
}

}  // namespace app
}  // namespace fv
