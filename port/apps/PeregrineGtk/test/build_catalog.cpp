// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// build_catalog.cpp — builds a map catalog from a data directory the way
/// Map ▸ Build Map Catalog does, for the tests that open the app on data.
///
/// Usage: peregrine-gtk-build-catalog <data-root> <catalog.sqlite>

#include <iostream>

#include "fv_desk_catalog_build.h"
#include "fvkit/formats/registry.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <data-root> <catalog.sqlite>\n";
    return 2;
  }
  fv::RegisterBuiltinFormats();
  fv::desk::CatalogBuild build(argv[2], fv::desk::PlanScan(argv[1]));
  build.Wait();
  for (const std::string& e : build.Errors()) std::cerr << e << "\n";
  std::cout << build.Summary() << "\n";
  return build.Results().empty() ? 1 : 0;
}
