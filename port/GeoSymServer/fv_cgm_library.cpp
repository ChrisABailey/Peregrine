// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// stdafx.h first: fv_cgm_to_symbol.h pulls in SymColors.h, which is MFC-shaped.
#include "stdafx.h"  // POSIX branch: fv_compat + CString + MFC containers

#include "fv_cgm_library.h"

#include <filesystem>

#include "fv_cgm_symbol.h"
#include "fv_cgm_to_symbol.h"  // completes CSymColorAdjuster + ToVectorSymbol

namespace fv {
namespace {

namespace fs = std::filesystem;

bool IsCgmName(const std::string& name) {
  if (name.size() <= 4) return false;
  const std::string tail = name.substr(name.size() - 4);
  return tail == ".cgm" || tail == ".CGM";
}

}  // namespace

CgmSymbolLibrary::CgmSymbolLibrary()
    : adjuster_(new CSymColorAdjuster()) {}
CgmSymbolLibrary::~CgmSymbolLibrary() = default;

Status CgmSymbolLibrary::OpenDirectory(const std::string& dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return Status::Error(kNotFound, "not a directory: " + dir);
  files_.clear();
  cache_.clear();
  for (const auto& de : fs::directory_iterator(dir, ec)) {
    if (ec) break;
    if (!de.is_regular_file(ec)) continue;
    const std::string name = de.path().filename().string();
    if (!IsCgmName(name)) continue;
    files_[name.substr(0, name.size() - 4)] = de.path().string();
  }
  return Status::Ok();
}

void CgmSymbolLibrary::SetColorAdjust(int brightness, int contrast) {
  adjuster_->Setup(brightness, contrast);
  // The display lists baked the old adjustment in.
  cache_.clear();
}

std::vector<std::string> CgmSymbolLibrary::ids() const {
  std::vector<std::string> out;
  out.reserve(files_.size());
  for (const auto& kv : files_) out.push_back(kv.first);
  return out;  // std::map is already sorted
}

const VectorSymbol* CgmSymbolLibrary::Symbol(const std::string& symbol_id) {
  const std::string id =
      IsCgmName(symbol_id) ? symbol_id.substr(0, symbol_id.size() - 4)
                           : symbol_id;

  auto hit = cache_.find(id);
  if (hit != cache_.end())
    return hit->second.primitives.empty() ? nullptr : &hit->second;

  auto file = files_.find(id);
  if (file == files_.end()) return nullptr;

  // A parse failure is cached as an EMPTY display list rather than not cached
  // at all — a symbol that will not parse is otherwise re-read once per stamp,
  // and an area pattern makes that hundreds of file opens per frame.
  VectorSymbol sym;
  CgmSymbol cgm;
  if (cgm.LoadFile(file->second).ok()) sym = ToVectorSymbol(cgm, *adjuster_);
  auto ins = cache_.emplace(id, std::move(sym));
  return ins.first->second.primitives.empty() ? nullptr : &ins.first->second;
}

}  // namespace fv
