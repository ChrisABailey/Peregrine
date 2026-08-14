// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/symbol/png_library.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "fvkit/tools/png_io.h"

namespace fv {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

bool LowerEndsWith(const std::string& s, const std::string& suffix) {
  if (s.size() < suffix.size()) return false;
  for (size_t i = 0; i < suffix.size(); ++i) {
    char c = s[s.size() - suffix.size() + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}

// Reads a whole file. Used for the sidecar/index JSON only — the PNGs go
// through png_io.
bool ReadTextFile(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out->assign(std::istreambuf_iterator<char>(in),
              std::istreambuf_iterator<char>());
  return true;
}

double NumberOr(const json& j, const char* key, double fallback) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_number()) return fallback;
  return it->get<double>();
}

}  // namespace

PngSymbolLibrary::PngSymbolLibrary() = default;
PngSymbolLibrary::~PngSymbolLibrary() = default;

// A re-Open REPLACES the library. Without this an id catalogued by the first
// Open still resolves after the second, and — worse for the sheet form — its
// rectangle now indexes a different image.
void PngSymbolLibrary::Reset() {
  entries_.clear();
  cache_.clear();
  sheet_ = PixelBuffer();
}

Status PngSymbolLibrary::OpenDirectory(const std::string& dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return Status::Error(kNotFound, "not a directory: " + dir);
  Reset();

  // Two passes so the @2x preference is decided per ID rather than per file:
  // a directory listing has no guaranteed order, so "the 2x wins" cannot be a
  // property of which one was seen last.
  std::map<std::string, std::string> plain, retina;
  for (const auto& de : fs::directory_iterator(dir, ec)) {
    if (ec) break;
    if (!de.is_regular_file(ec)) continue;
    const std::string name = de.path().filename().string();
    if (!LowerEndsWith(name, ".png")) continue;
    std::string id = name.substr(0, name.size() - 4);
    if (LowerEndsWith(id, "@2x")) {
      retina[id.substr(0, id.size() - 3)] = de.path().string();
    } else {
      plain[id] = de.path().string();
    }
  }

  for (const auto& kv : plain) {
    Entry e;
    e.path = kv.second;
    entries_[kv.first] = e;
  }
  for (const auto& kv : retina) {
    // The 2x file is used when it is asked for, and always when it is the only
    // form there is — a set that ships @2x only would otherwise catalogue as
    // empty, which reads as a missing directory rather than a missing option.
    auto it = entries_.find(kv.first);
    if (it != entries_.end() && !prefer_high_dpi_) continue;
    Entry e;
    e.path = kv.second;
    e.pixel_ratio = 2.0;
    entries_[kv.first] = e;
  }

  // Sidecar pivots, read now because they are tiny and because a pivot that
  // fails to parse should say so at Open rather than at the first draw.
  for (auto& kv : entries_) {
    // An `<id>@2x.json` beside the 2x artwork wins and is read as-authored;
    // a plain `<id>.json` is authored against the 1x artwork, so its numbers
    // are SCALED BY THE RATIO. A pivot lives in TILE pixels (that is the space
    // the sampler subtracts it in), and a 2x tile has twice as many of them —
    // without this a pin's tip lands halfway up the pin on a retina set, and
    // only on a retina set.
    std::string side = (fs::path(dir) / (kv.first + "@2x.json")).string();
    double scale = 1.0;
    std::string text;
    if (kv.second.pixel_ratio == 1.0 || !ReadTextFile(side, &text)) {
      side = (fs::path(dir) / (kv.first + ".json")).string();
      scale = kv.second.pixel_ratio;
      if (!ReadTextFile(side, &text)) continue;
    }
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
      return Status::Error(kIoError, "bad symbol sidecar: " + side);
    if (j.contains("pivot_x") || j.contains("pivot_y")) {
      kv.second.has_pivot = true;
      kv.second.pivot_x = NumberOr(j, "pivot_x", 0.0) * scale;
      kv.second.pivot_y = NumberOr(j, "pivot_y", 0.0) * scale;
    }
  }
  return Status::Ok();
}

Status PngSymbolLibrary::OpenSheet(const std::string& png_path,
                                   const std::string& json_path) {
  std::string index = json_path;
  if (index.empty()) {
    index = png_path;
    if (LowerEndsWith(index, ".png")) index.resize(index.size() - 4);
    index += ".json";
  }
  std::string text;
  if (!ReadTextFile(index, &text))
    return Status::Error(kNotFound, "cannot read sprite index " + index);
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object())
    return Status::Error(kIoError, "bad sprite index " + index);

  // Both failures above happen BEFORE Reset(), so an Open that fails leaves
  // the library exactly as it was rather than half-replaced.
  PixelBuffer sheet;
  Status s = ReadPngFile(png_path, &sheet);
  if (!s.ok()) return s;
  Reset();
  sheet_ = std::move(sheet);

  for (auto it = j.begin(); it != j.end(); ++it) {
    const json& v = it.value();
    if (!v.is_object()) continue;
    Entry e;
    e.x = static_cast<int>(NumberOr(v, "x", 0.0));
    e.y = static_cast<int>(NumberOr(v, "y", 0.0));
    e.w = static_cast<int>(NumberOr(v, "width", 0.0));
    e.h = static_cast<int>(NumberOr(v, "height", 0.0));
    if (e.w <= 0 || e.h <= 0) continue;
    // A sprite whose rectangle is not wholly inside the sheet is a broken
    // index, and slicing it would read another sprite's pixels. Skip it: the
    // other 399 icons are still good, which is the same call the ENC reader's
    // whole-exchange-set abort gets wrong (see §2d).
    if (e.x < 0 || e.y < 0 || e.x + e.w > sheet_.Width() ||
        e.y + e.h > sheet_.Height())
      continue;
    e.pixel_ratio = NumberOr(v, "pixelRatio", 1.0);
    if (!(e.pixel_ratio > 0.0)) e.pixel_ratio = 1.0;
    // NOTE prefer_high_dpi_ does nothing in the SHEET form and that is not an
    // oversight: a sprite index states one rectangle per id, so there are no
    // two variants to choose between — the ratio is a property of the sprite
    // and is honoured through pixel_ratio whatever the caller prefers. The
    // preference only decides anything in the loose form, where `<id>.png` and
    // `<id>@2x.png` are two files claiming one id. (Mapbox ships the two
    // ratios as two SHEETS, `sprite.json` and `sprite@2x.json`, so the caller
    // chooses there by choosing a path.)
    if (v.contains("pivot_x") || v.contains("pivot_y")) {
      e.has_pivot = true;
      e.pivot_x = NumberOr(v, "pivot_x", 0.0);
      e.pivot_y = NumberOr(v, "pivot_y", 0.0);
    }
    entries_[it.key()] = e;
  }
  return Status::Ok();
}

std::vector<std::string> PngSymbolLibrary::ids() const {
  std::vector<std::string> out;
  out.reserve(entries_.size());
  for (const auto& kv : entries_) out.push_back(kv.first);
  return out;  // std::map is already sorted
}

bool PngSymbolLibrary::LoadEntry(const Entry& e, SymbolPixmap* out) const {
  if (e.path.empty()) {
    if (sheet_.Empty()) return false;
    out->tile = PixelBuffer(e.w, e.h);
    for (int y = 0; y < e.h; ++y)
      std::memcpy(out->tile.Row(y), sheet_.Row(e.y + y) + e.x * 4,
                  static_cast<size_t>(e.w) * 4);
  } else {
    if (!ReadPngFile(e.path, &out->tile).ok()) return false;
  }
  out->pixel_ratio = e.pixel_ratio;
  if (e.has_pivot) {
    out->pivot_x = e.pivot_x;
    out->pivot_y = e.pivot_y;
  } else {
    // Centre. In TILE pixels, so a 2x tile's centre is at 2x the coordinate —
    // which is right, because the pivot is subtracted from the anchor in the
    // same tile-pixel space the sampler works in.
    out->pivot_x = out->tile.Width() / 2.0;
    out->pivot_y = out->tile.Height() / 2.0;
  }
  return true;
}

const SymbolPixmap* PngSymbolLibrary::Pixmap(const std::string& symbol_id) {
  auto hit = cache_.find(symbol_id);
  if (hit != cache_.end())
    return hit->second.tile.Empty() ? nullptr : &hit->second;

  auto it = entries_.find(symbol_id);
  if (it == entries_.end()) return nullptr;

  SymbolPixmap sym;
  // A failure is cached as an EMPTY tile rather than not cached at all: a
  // symbol that will not decode is asked for once per stamp otherwise, and an
  // area pattern makes that hundreds of failed file opens per frame.
  LoadEntry(it->second, &sym);
  auto ins = cache_.emplace(symbol_id, std::move(sym));
  return ins.first->second.tile.Empty() ? nullptr : &ins.first->second;
}

}  // namespace fv
