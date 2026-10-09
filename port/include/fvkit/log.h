// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/log.h — the application log (port/desktop-plan.md §2e, K17).
///
/// Libraries write through the `FV_LOG_*` macros, which record file and line
/// and skip formatting when no sink wants the level. Sinks are registered by
/// the application; a library registers none. With no sink registered, errors
/// and warnings go to stderr, so command-line tools and tests still see them.
/// Every function here is thread-safe.
///
/// This is the port's counterpart of FalconView's `ERR_report` /
/// `INFO_report` (Applications/FalconView/include/err.h). The Windows product
/// keeps its own `err.lib`; C code reaches this log through `fv_log_c.h`.
#ifndef FVKIT_LOG_H_
#define FVKIT_LOG_H_

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "fvkit/geo.h"  // Status

namespace fv {

/// Severity, most severe first: a sink at kInfo also receives kWarning and kError.
enum class LogLevel : int { kError = 0, kWarning = 1, kInfo = 2, kDebug = 3 };

/// "error", "warning", "info", "debug".
const char* LogLevelName(LogLevel level);
/// Parses a LogLevelName spelling, case-insensitively. False for anything else.
bool ParseLogLevel(const std::string& text, LogLevel* level);

/// One message as a sink receives it.
struct LogRecord {
  LogLevel level = LogLevel::kInfo;
  std::chrono::system_clock::time_point time;
  const char* file = "";  ///< base name of the source file; "" when unknown
  int line = 0;
  std::string thread;     ///< SetLogThreadName's name, else "t<n>"
  std::string message;
};

/// "2026-10-08 14:03:12.345 W render fv_desk_host.cpp:352  message", local time,
/// without a trailing newline.
std::string FormatLogRecord(const LogRecord& r);

/// Receives records. Write is called with the log's lock held, one record at
/// a time, from whichever thread logged; a sink must not log from Write.
class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void Write(const LogRecord& r) = 0;
};

/// Registers `sink` for records at `max_level` and more severe. Returns an id
/// for RemoveLogSink.
int AddLogSink(std::shared_ptr<LogSink> sink, LogLevel max_level);
void RemoveLogSink(int id);

/// True when some sink (or the stderr fallback) wants `level`. Lock-free.
bool LogEnabled(LogLevel level);
/// Sends a record to every sink that wants `level`. `file` may be a full path.
void LogWrite(LogLevel level, const char* file, int line, std::string message);
/// Sends an Info record to every sink whatever its level: a launch's first line.
void LogBanner(const char* file, int line, std::string message);
/// Names the calling thread in its records ("render", "catalog").
void SetLogThreadName(const std::string& name);

/// Writes each record as one line on stderr.
class StderrLogSink : public LogSink {
 public:
  void Write(const LogRecord& r) override;
};

/// Appends to `<name>-YYYY-MM-DD.log` in a directory, one file per local day.
/// A file over `max_bytes` is renamed `<name>-YYYY-MM-DD-HHMMSS.log` and a new
/// one started; only the newest `keep_files` files of the name are kept. Each
/// line is flushed as it is written.
class FileLogSink : public LogSink {
 public:
  /// Creates `directory` if needed and opens today's file. Null on failure,
  /// with the reason in `*status`.
  static std::shared_ptr<FileLogSink> Open(const std::string& directory, const std::string& name,
                                           Status* status, uint64_t max_bytes = 5u << 20,
                                           int keep_files = 5);
  ~FileLogSink() override;

  /// The file being written now.
  std::string Path() const;
  void Write(const LogRecord& r) override;

 private:
  FileLogSink(std::string directory, std::string name, uint64_t max_bytes, int keep_files);
  /// Opens the file for the local day of `t`, rolling or pruning as needed.
  Status OpenFor(std::chrono::system_clock::time_point t);
  void Prune();

  const std::string directory_;
  const std::string name_;
  const uint64_t max_bytes_;
  const int keep_files_;
  mutable std::mutex mu_;
  std::FILE* file_ = nullptr;
  std::string path_;
  std::string day_;  ///< "YYYY-MM-DD" of the open file
  uint64_t size_ = 0;
};

/// The platform's per-user log directory for `app_name`: macOS
/// ~/Library/Logs/<app>, Linux $XDG_STATE_HOME/<app lower-case> (default
/// ~/.local/state), Windows %LOCALAPPDATA%\<app>\Logs. "" when the home
/// directory is unknown.
std::string DefaultLogDirectory(const std::string& app_name);
/// "macOS 27.0 (Darwin 27.0.0 arm64)" or the nearest the platform reports.
std::string OperatingSystemDescription();

}  // namespace fv

/// Logs a message built with `<<` at `level`; the operands are not evaluated
/// when no sink wants the level.
#define FV_LOG(level, ...)                                           \
  do {                                                               \
    if (::fv::LogEnabled(level)) {                                   \
      std::ostringstream fv_log_os_;                                 \
      fv_log_os_ << __VA_ARGS__;                                     \
      ::fv::LogWrite(level, __FILE__, __LINE__, fv_log_os_.str());   \
    }                                                                \
  } while (0)

#define FV_LOG_ERROR(...) FV_LOG(::fv::LogLevel::kError, __VA_ARGS__)
#define FV_LOG_WARNING(...) FV_LOG(::fv::LogLevel::kWarning, __VA_ARGS__)
#define FV_LOG_INFO(...) FV_LOG(::fv::LogLevel::kInfo, __VA_ARGS__)
#define FV_LOG_DEBUG(...) FV_LOG(::fv::LogLevel::kDebug, __VA_ARGS__)

#endif  // FVKIT_LOG_H_
