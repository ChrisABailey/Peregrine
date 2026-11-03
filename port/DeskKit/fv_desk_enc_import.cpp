// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_enc_import.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "fv_desk_packages.h"
#include "fvkit/log.h"

namespace fs = std::filesystem;

namespace fv {
namespace desk {

namespace {

constexpr char kStagingDir[] = ".staging";

bool IEquals(const std::string& a, const char* b) {
  size_t i = 0;
  for (; i < a.size() && b[i] != '\0'; ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return i == a.size() && b[i] == '\0';
}

std::string Plural(size_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

}  // namespace

std::string DefaultChartsDir() {
  const std::string packages = DefaultUserPackageDir();
  if (packages.empty()) return std::string();
  return (fs::path(packages).parent_path() / "charts").string();
}

std::string EncSetName(const std::string& source) {
  fs::path p = fs::path(source).lexically_normal();
  if (!p.has_filename()) p = p.parent_path();  // a trailing separator
  std::error_code ec;
  std::string name;
  if (fs::is_directory(p, ec)) {
    name = p.filename().string();
    if (IEquals(name, "ENC_ROOT") && p.has_parent_path()) name = p.parent_path().filename().string();
  } else {
    name = p.stem().string();
  }
  for (char& c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (!std::isalnum(u) && c != '-' && c != '_' && c != '.') c = '_';
  }
  while (!name.empty() && name.front() == '.') name.erase(0, 1);
  return name.empty() ? std::string("ENC") : name;
}

EncImportJob::EncImportJob(std::vector<std::string> sources, std::string charts_dir,
                           std::string catalog_dir)
    : sources_(std::move(sources)),
      charts_dir_(std::move(charts_dir)),
      catalog_dir_(std::move(catalog_dir)) {
  progress_.total = static_cast<int>(sources_.size());
  worker_ = std::thread([this] { Run(); });
}

EncImportJob::~EncImportJob() {
  Cancel();
  Wait();
}

EncImportProgress EncImportJob::Progress() const {
  std::lock_guard<std::mutex> lock(mu_);
  return progress_;
}

void EncImportJob::Cancel() { cancel_ = true; }

void EncImportJob::Wait() {
  if (worker_.joinable()) worker_.join();
}

std::vector<EncImportOutcome> EncImportJob::Outcomes() const {
  std::lock_guard<std::mutex> lock(mu_);
  return outcomes_;
}

bool EncImportJob::AnyInstalled() const {
  std::lock_guard<std::mutex> lock(mu_);
  for (const EncImportOutcome& o : outcomes_)
    if (o.status.ok()) return true;
  return false;
}

std::string EncImportJob::Summary() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::string out;
  for (const EncImportOutcome& o : outcomes_) {
    if (!out.empty()) out += "\n";
    out += o.set_name + ": ";
    if (o.status.code == kInterrupted) {
      out += "cancelled";
      continue;
    }
    if (!o.status.ok()) {
      out += o.status.message;
      continue;
    }
    const EncImportResult& r = o.result;
    out += Plural(r.cell_count, "cell", "cells") + ", " +
           Plural(r.updates_applied, "update", "updates") + " applied";
    if (!r.outdated.empty())
      out += ", " + Plural(r.outdated.size(), "cell", "cells") + " not fully up to date";
    if (!r.cancelled.empty())
      out += ", " + Plural(r.cancelled.size(), "cell", "cells") + " cancelled";
    if (!r.unreadable.empty())
      out += ", " + Plural(r.unreadable.size(), "cell", "cells") + " unreadable";
    if (o.replaced) out += " (replaced the earlier set)";
  }
  return out;
}

void EncImportJob::Run() {
  SetLogThreadName("enc-import");
  const fs::path staging = fs::path(charts_dir_) / kStagingDir;
  std::error_code ec;
  fs::remove_all(staging, ec);  // left over from an import that did not finish
  for (size_t i = 0; i < sources_.size() && !cancel_; ++i) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      progress_.current = EncSetName(sources_[i]);
    }
    EncImportOutcome o = ImportOne(sources_[i]);
    if (o.status.ok()) {
      FV_LOG_INFO("enc import: " << o.set_name << ": " << o.result.cell_count << " cells from "
                                 << o.source);
    } else if (o.status.code != kInterrupted) {
      FV_LOG_WARNING("enc import: " << o.source << ": " << o.status.message);
    }
    std::lock_guard<std::mutex> lock(mu_);
    outcomes_.push_back(std::move(o));
    progress_.done = static_cast<int>(i + 1);
    progress_.fraction = static_cast<double>(progress_.done) / progress_.total;
  }
  fs::remove_all(staging, ec);
  std::lock_guard<std::mutex> lock(mu_);
  progress_.current.clear();
  progress_.finished = true;
  progress_.cancelled = cancel_;
}

EncImportOutcome EncImportJob::ImportOne(const std::string& source) {
  EncImportOutcome o;
  o.source = source;
  o.set_name = EncSetName(source);
  const fs::path enc_dir = fs::path(charts_dir_) / "enc";
  const fs::path staged = fs::path(charts_dir_) / kStagingDir / o.set_name;
  const fs::path dest = enc_dir / o.set_name;
  const double base = static_cast<double>(progress_.done) / std::max(progress_.total, 1);
  o.status = ImportEncSet(source, staged.string(), catalog_dir_, &o.result, [&](double f) {
    std::lock_guard<std::mutex> lock(mu_);
    progress_.fraction = base + f / std::max(progress_.total, 1);
    return !cancel_.load();
  });
  if (!o.status.ok()) return o;

  // Replace by rename, so the catalog never lists a half-written set.
  std::error_code ec;
  fs::create_directories(enc_dir, ec);
  const fs::path old = fs::path(charts_dir_) / kStagingDir / (o.set_name + ".old");
  o.replaced = fs::exists(dest, ec);
  if (o.replaced) fs::rename(dest, old, ec);
  if (!ec) fs::rename(staged, dest, ec);
  if (ec) {
    o.status = Status::Error(kIoError, "cannot install into " + dest.string() + ": " + ec.message());
    std::error_code rm;
    if (o.replaced && !fs::exists(dest, rm)) fs::rename(old, dest, rm);
    fs::remove_all(staged, rm);
    return o;
  }
  fs::remove_all(old, ec);
  o.result.set_dir = dest.string();
  return o;
}

}  // namespace desk
}  // namespace fv
