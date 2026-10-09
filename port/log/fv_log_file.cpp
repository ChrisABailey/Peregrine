// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The log's file sink and the platform paths it writes to.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <system_error>
#include <vector>

#include "fvkit/log.h"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#if !defined(_WIN32)
#include <sys/utsname.h>
#endif

namespace fs = std::filesystem;

namespace fv {
namespace {

std::tm LocalTime(std::chrono::system_clock::time_point t) {
  const std::time_t secs = std::chrono::system_clock::to_time_t(t);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &secs);
#else
  localtime_r(&secs, &tm);
#endif
  return tm;
}

std::string Strftime(const char* format, const std::tm& tm) {
  char buf[32];
  std::strftime(buf, sizeof buf, format, &tm);
  return buf;
}

std::string Env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr ? v : "";
}

}  // namespace

std::shared_ptr<FileLogSink> FileLogSink::Open(const std::string& directory,
                                               const std::string& name, Status* status,
                                               uint64_t max_bytes, int keep_files) {
  std::error_code ec;
  fs::create_directories(directory, ec);
  if (ec) {
    if (status != nullptr)
      *status = Status::Error(kIoError, "cannot create log directory " + directory + ": " +
                                            ec.message());
    return nullptr;
  }
  std::shared_ptr<FileLogSink> sink(
      new FileLogSink(directory, name, max_bytes, std::max(keep_files, 1)));
  const Status s = sink->OpenFor(std::chrono::system_clock::now());
  if (status != nullptr) *status = s;
  return s.ok() ? sink : nullptr;
}

FileLogSink::FileLogSink(std::string directory, std::string name, uint64_t max_bytes,
                         int keep_files)
    : directory_(std::move(directory)),
      name_(std::move(name)),
      max_bytes_(max_bytes),
      keep_files_(keep_files) {}

FileLogSink::~FileLogSink() {
  if (file_ != nullptr) std::fclose(file_);
}

std::string FileLogSink::Path() const {
  std::lock_guard<std::mutex> lock(mu_);
  return path_;
}

void FileLogSink::Write(const LogRecord& r) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!OpenFor(r.time).ok()) return;
  const std::string line = FormatLogRecord(r) + "\n";
  std::fwrite(line.data(), 1, line.size(), file_);
  std::fflush(file_);
  size_ += line.size();
}

Status FileLogSink::OpenFor(std::chrono::system_clock::time_point t) {
  const std::tm tm = LocalTime(t);
  const std::string day = Strftime("%Y-%m-%d", tm);
  if (file_ != nullptr && day == day_ && size_ < max_bytes_) return Status::Ok();

  const std::string path = (fs::path(directory_) / (name_ + "-" + day + ".log")).string();
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
    if (day == day_) {
      // Full: keep it under a time-stamped name and start again.
      std::string rolled = name_ + "-" + day + "-" + Strftime("%H%M%S", tm);
      fs::path to = fs::path(directory_) / (rolled + ".log");
      std::error_code ec;
      for (int n = 2; fs::exists(to, ec); ++n)
        to = fs::path(directory_) / (rolled + "-" + std::to_string(n) + ".log");
      fs::rename(path_, to, ec);
    }
  }
  file_ = std::fopen(path.c_str(), "ab");
  if (file_ == nullptr)
    return Status::Error(kIoError, "cannot open log file " + path);
  std::error_code ec;
  const uintmax_t size = fs::file_size(path, ec);
  size_ = ec ? 0 : static_cast<uint64_t>(size);
  path_ = path;
  day_ = day;
  Prune();
  return Status::Ok();
}

void FileLogSink::Prune() {
  const std::string prefix = name_ + "-";
  std::vector<std::string> names;
  std::error_code ec;
  for (fs::directory_iterator it(directory_, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string f = it->path().filename().string();
    if (f.size() > prefix.size() + 4 && f.compare(0, prefix.size(), prefix) == 0 &&
        f.compare(f.size() - 4, 4, ".log") == 0)
      names.push_back(f);
  }
  // Names sort by date; a rolled file sorts before its day's current file.
  std::sort(names.begin(), names.end());
  const std::string current = fs::path(path_).filename().string();
  size_t excess = names.size() > static_cast<size_t>(keep_files_)
                      ? names.size() - static_cast<size_t>(keep_files_)
                      : 0;
  for (const std::string& f : names) {
    if (excess == 0) break;
    if (f == current) continue;
    fs::remove(fs::path(directory_) / f, ec);
    --excess;
  }
}

std::string DefaultLogDirectory(const std::string& app_name) {
#if defined(_WIN32)
  const std::string base = Env("LOCALAPPDATA");
  if (base.empty()) return "";
  return (fs::path(base) / app_name / "Logs").string();
#elif defined(__APPLE__)
  const std::string home = Env("HOME");
  if (home.empty()) return "";
  return (fs::path(home) / "Library" / "Logs" / app_name).string();
#else
  std::string base = Env("XDG_STATE_HOME");
  if (base.empty()) {
    const std::string home = Env("HOME");
    if (home.empty()) return "";
    base = (fs::path(home) / ".local" / "state").string();
  }
  std::string lower;
  for (char c : app_name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return (fs::path(base) / lower).string();
#endif
}

std::string OperatingSystemDescription() {
#if defined(_WIN32)
  return "Windows";
#else
  std::string text;
#if defined(__APPLE__)
  char version[64] = {0};
  size_t len = sizeof version;
  if (sysctlbyname("kern.osproductversion", version, &len, nullptr, 0) == 0)
    text = std::string("macOS ") + version + " ";
#endif
  struct utsname u;
  if (uname(&u) == 0) {
    const std::string kernel = std::string(u.sysname) + " " + u.release + " " + u.machine;
    text += text.empty() ? kernel : "(" + kernel + ")";
  }
  return text.empty() ? "unknown" : text;
#endif
}

}  // namespace fv
