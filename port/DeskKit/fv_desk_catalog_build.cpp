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
#include "fvkit/log.h"

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

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
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

std::string AbsolutePath(const std::string& path) {
  std::error_code ec;
  fs::path p = fs::absolute(fs::path(path), ec);
  if (ec) p = fs::path(path);
  p = p.lexically_normal();
  std::string out = p.string();
  while (out.size() > 1 && (out.back() == '/' || out.back() == '\\') &&
         p.root_path().string() != out)
    out.pop_back();
  return out;
}

bool PathWithin(const std::string& path, const std::string& root) {
  const fs::path r(root);
  std::error_code ec;
  const bool root_exists = fs::exists(r, ec);
  for (fs::path p(path); !p.empty(); p = p.parent_path()) {
    if (p == r) return true;
    if (root_exists && fs::equivalent(p, r, ec)) return true;
    if (p == p.parent_path()) break;
  }
  return false;
}

std::vector<std::string> RootsFromSources(
    const std::vector<std::pair<std::string, std::string>>& path_format) {
  std::vector<std::string> abs;
  for (const auto& [path, format] : path_format) {
    fs::path p(AbsolutePath(path));
    for (const ScanFormat& f : DefaultScanFormats()) {
      if (f.format == format && !f.subdir.empty() && Lower(p.filename().string()) == Lower(f.subdir)) {
        p = p.parent_path();
        break;
      }
    }
    abs.push_back(p.string());
  }
  // Shorter paths first, so a parent is kept before anything inside it.
  std::sort(abs.begin(), abs.end(), [](const std::string& a, const std::string& b) {
    const auto depth = [](const std::string& s) {
      const fs::path p(s);
      return std::distance(p.begin(), p.end());
    };
    const auto da = depth(a), db = depth(b);
    return da != db ? da < db : a < b;
  });
  std::vector<std::string> roots;
  for (const std::string& p : abs) {
    const bool inside = std::any_of(roots.begin(), roots.end(),
                                    [&](const std::string& r) { return PathWithin(p, r); });
    if (!inside) roots.push_back(p);
  }
  std::sort(roots.begin(), roots.end());
  return roots;
}

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

CatalogBuild::CatalogBuild(std::string catalog_path, std::vector<ScanStep> steps,
                           bool replace_existing)
    : catalog_path_(std::move(catalog_path)),
      steps_(std::move(steps)),
      replace_existing_(replace_existing) {
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
  SetLogThreadName("catalog");
  FV_LOG_INFO("catalog build: " << steps_.size() << " steps into " << catalog_path_);
  Catalog cat;
  const Status opened = cat.Open(catalog_path_);
  auto finish = [this] {
    std::lock_guard<std::mutex> lock(mu_);
    progress_.current.clear();
    progress_.removing = false;
    progress_.finished = true;
    progress_.cancelled = cancel_;
  };
  if (!opened.ok()) {
    FV_LOG_ERROR("catalog build: " << opened.message);
    {
      std::lock_guard<std::mutex> lock(mu_);
      errors_.push_back(opened.message);
    }
    finish();
    return;
  }

  if (replace_existing_) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      progress_.removing = true;
    }
    std::vector<DataSourceRow> old;
    Status s = cat.DataSources(&old);
    for (size_t i = 0; s.ok() && i < old.size(); ++i) s = cat.RemoveDataSource(old[i].id);
    if (!s.ok()) {
      FV_LOG_ERROR("catalog build: clearing the old coverage: " << s.message);
      {
        std::lock_guard<std::mutex> lock(mu_);
        errors_.push_back(s.message);
      }
      finish();
      return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    progress_.removing = false;
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
      // ENC and OSM enumerators report a tree with none of their files as
      // kUnsupported; every format probes every root, so that is an empty result.
      if (s.code == kUnsupported && CanScan(step.format)) {
        FV_LOG_DEBUG("catalog build: " << step.format << " " << dir << ": " << s.message);
        s = Status::Ok();
      }
      if (!s.ok()) {
        FV_LOG_WARNING("catalog build: " << step.format << " " << dir << ": " << s.message);
        std::lock_guard<std::mutex> lock(mu_);
        errors_.push_back(step.format + " " + dir + ": " + s.message);
      }
      if (frames > 0) {
        FV_LOG_INFO("catalog build: " << step.format << " " << dir << ": " << frames
                                      << " frames");
        std::lock_guard<std::mutex> lock(mu_);
        results_.push_back(ScanResult{step.format, dir, frames});
        break;
      }
      if (id != 0 && !step.keep_if_empty) cat.RemoveDataSource(id);
    }
    std::lock_guard<std::mutex> lock(mu_);
    ++progress_.done;
  }
  if (cancel_) FV_LOG_INFO("catalog build: cancelled");
  finish();
}

}  // namespace desk
}  // namespace fv
