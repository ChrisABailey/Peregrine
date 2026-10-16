// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/symbol/builtin_icons.h"

namespace fv {
namespace {
#include "maki_sheet.inc"
}  // namespace

std::shared_ptr<PngSymbolLibrary> BuiltinIconSheet() {
  static const std::shared_ptr<PngSymbolLibrary> sheet = [] {
    auto lib = std::make_shared<PngSymbolLibrary>();
    if (lib->OpenSheetData(kMakiSheetPng, sizeof kMakiSheetPng, kMakiSheetJson).ok()) {
      // Pixmap caches on first lookup; filling the cache now leaves no writes.
      for (const std::string& id : lib->ids()) lib->Pixmap(id);
    }
    return lib;
  }();
  return sheet;
}

}  // namespace fv
