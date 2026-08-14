// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/symbol/library.h"

namespace fv {

void CompositeSymbolLibrary::Add(ISymbolLibrary* lib) {
  if (lib != nullptr && lib != this) members_.push_back(lib);
}

const VectorSymbol* CompositeSymbolLibrary::Symbol(const std::string& id) {
  for (ISymbolLibrary* lib : members_) {
    const VectorSymbol* s = lib->Symbol(id);
    // An EMPTY display list is not an answer. A style engine that caches
    // misses can hand one back, and treating it as a hit would shadow a later
    // member that really has the symbol — the same test ResolveSymbol makes
    // before it falls through to the pixmap form.
    if (s != nullptr && !s->primitives.empty()) return s;
  }
  return nullptr;
}

const SymbolPixmap* CompositeSymbolLibrary::Pixmap(const std::string& id) {
  for (ISymbolLibrary* lib : members_) {
    const SymbolPixmap* p = lib->Pixmap(id);
    if (p != nullptr && !p->tile.Empty()) return p;
  }
  return nullptr;
}

double CompositeSymbolLibrary::himetric_per_symbol_pixel() const {
  if (himetric_override_ > 0.0) return himetric_override_;
  if (!members_.empty()) return members_.front()->himetric_per_symbol_pixel();
  return ISymbolLibrary::himetric_per_symbol_pixel();
}

}  // namespace fv
