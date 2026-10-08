// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_catalog_build.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"

namespace fv {
namespace desk {
namespace {

namespace fs = std::filesystem;

bool IsDir(const std::string& p) {
  std::error_code ec;
  return fs::is_directory(p, ec);
}

bool CanScan(const std::string& format) {
  const FormatFactories* f = FindFormat(format);
  return f != nullptr && static_cast<bool>(f->make_enumerator);
}

bool HasDht(const fs::path& dir) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::string name = it->path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if ((name == "dht" || name == "dht.") && !it->is_directory(ec)) return true;
  }
  return false;
}

void WalkVpf(const fs::path& dir, int depth, int max_depth, std::vector<std::string>* out) {
  if (HasDht(dir)) {
    out->push_back(dir.string());
    return;
  }
  if (depth >= max_depth) return;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (name.empty() || name[0] == '.' || !it->is_directory(ec)) continue;
    WalkVpf(it->path(), depth + 1, max_depth, out);
  }
}

std::string Thousands(int64_t n) {
  std::string digits = std::to_string(n);
  std::string out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return out;
}

}  // namespace

const std::vector<ScanFormat>& DefaultScanFormats() {
  static const std::vector<ScanFormat> table = {
      {"cadrg", "rpf"},        {"tiros", "tiros3"},     {"dted", "dted"},
      {"dted-shaded", "dted"}, {"geotiff", "geotiff"},  {"gpkg", ""},
      {"enc", "enc"},          {"osm", "OSM"},
  };
  return table;
}

std::vector<std::string> FindVpfDatabases(const std::string& root, int max_depth) {
  std::vector<std::string> out;
  if (IsDir(root)) WalkVpf(fs::path(root), 0, max_depth, &out);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<ScanStep> PlanScan(const std::string& root, const std::vector<ScanFormat>& formats) {
  std::vector<ScanStep> steps;
  if (!IsDir(root)) return steps;
  for (const ScanFormat& f : formats) {
    if (!CanScan(f.format)) continue;
    ScanStep s;
    s.format = f.format;
    s.dirs.push_back(root);
    if (!f.subdir.empty()) {
      const std::string sub = (fs::path(root) / f.subdir).string();
      if (IsDir(sub)) s.dirs.push_back(sub);
    }
    steps.push_back(std::move(s));
  }
  if (CanScan("vpf")) {
    for (const std::string& db : FindVpfDatabases(root)) steps.push_back(ScanStep{"vpf", {db}});
  }
  return steps;
}

CatalogBuild::CatalogBuild(std::string catalog_path, std::vector<ScanStep> steps)
    : catalog_path_(std::move(catalog_path)), steps_(std::move(steps)) {
  progress_.total = static_cast<int>(steps_.size());
  worker_ = std::thread([this] { Run(); });
}

CatalogBuild::~CatalogBuild() {
  Cancel();
  Wait();
}

BuildProgress CatalogBuild::Progress() const {
  std::lock_guard<std::mutex> lock(mu_);
  return progress_;
}

void CatalogBuild::Cancel() { cancel_ = true; }

void CatalogBuild::Wait() {
  if (worker_.joinable()) worker_.join();
}

std::vector<ScanResult> CatalogBuild::Results() const {
  std::lock_guard<std::mutex> lock(mu_);
  return results_;
}

std::vector<std::string> CatalogBuild::Errors() const {
  std::lock_guard<std::mutex> lock(mu_);
  return errors_;
}

std::string CatalogBuild::Summary() const {
  std::lock_guard<std::mutex> lock(mu_);
  int64_t frames = 0;
  for (const ScanResult& r : results_) frames += r.frames;
  std::string line;
  if (results_.empty()) {
    line = "No map data found.";
  } else {
    line = "Catalogued " + Thousands(frames) + (frames == 1 ? " frame" : " frames") + " from " +
           std::to_string(results_.size()) + (results_.size() == 1 ? " source." : " sources.");
  }
  if (progress_.cancelled)
    line += " Cancelled after " + std::to_string(progress_.done) + " of " +
            std::to_string(progress_.total) + " steps.";
  if (!errors_.empty())
    line += " " + std::to_string(errors_.size()) +
            (errors_.size() == 1 ? " source failed." : " sources failed.");
  return line;
}

void CatalogBuild::Run() {
  Catalog cat;
  const Status opened = cat.Open(catalog_path_);
  auto finish = [this] {
    std::lock_guard<std::mutex> lock(mu_);
    progress_.current.clear();
    progress_.finished = true;
    progress_.cancelled = cancel_;
  };
  if (!opened.ok()) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      errors_.push_back(opened.message);
    }
    finish();
    return;
  }

  for (const ScanStep& step : steps_) {
    if (cancel_) break;
    for (const std::string& dir : step.dirs) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.current = step.format + "  " + dir;
      }
      int64_t id = 0;
      Status s = cat.AddDataSource(dir, step.format, 0, &id);
      int frames = 0;
      if (s.ok()) s = cat.Scan(id, &frames);
      if (!s.ok()) {
        std::lock_guard<std::mutex> lock(mu_);
        errors_.push_back(step.format + " " + dir + ": " + s.message);
      }
      if (frames > 0) {
        std::lock_guard<std::mutex> lock(mu_);
        results_.push_back(ScanResult{step.format, dir, frames});
        break;
      }
      if (id != 0 && !step.keep_if_empty) cat.RemoveDataSource(id);
    }
    std::lock_guard<std::mutex> lock(mu_);
    ++progress_.done;
  }
  finish();
}

}  // namespace desk
}  // namespace fv
