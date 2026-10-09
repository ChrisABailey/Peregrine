// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The log's sink registry, record formatting, stderr sink and C entry points.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <vector>

#include "fv_log_c.h"
#include "fvkit/log.h"

namespace fv {
namespace {

struct SinkEntry {
  int id;
  std::shared_ptr<LogSink> sink;
  LogLevel level;
};

/// Registered sinks. Never destroyed, so a static destructor that logs during
/// shutdown still finds it.
struct Registry {
  std::mutex mu;
  std::vector<SinkEntry> sinks;
  int next_id = 1;
};

Registry& Reg() {
  static Registry* r = new Registry;
  return *r;
}

/// The most verbose level any sink takes; kWarning for the stderr fallback.
std::atomic<int> g_max_level{static_cast<int>(LogLevel::kWarning)};
std::atomic<int> g_thread_seq{0};
thread_local std::string t_thread_name;
thread_local bool t_writing = false;

void UpdateMaxLevel(const Registry& r) {
  int max = r.sinks.empty() ? static_cast<int>(LogLevel::kWarning) : 0;
  for (const SinkEntry& e : r.sinks) max = std::max(max, static_cast<int>(e.level));
  g_max_level.store(max, std::memory_order_relaxed);
}

const char* BaseName(const char* path) {
  if (path == nullptr) return "";
  const char* base = path;
  for (const char* p = path; *p != '\0'; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;
  return base;
}

const std::string& ThreadName() {
  if (t_thread_name.empty())
    t_thread_name = "t" + std::to_string(g_thread_seq.fetch_add(1) + 1);
  return t_thread_name;
}

/// Dispatches `r`; `all` ignores the sinks' levels.
void Dispatch(LogRecord r, bool all) {
  if (t_writing) return;  // a sink logged from Write
  t_writing = true;
  Registry& reg = Reg();
  {
    std::lock_guard<std::mutex> lock(reg.mu);
    if (reg.sinks.empty()) {
      if (all || r.level <= LogLevel::kWarning)
        std::fprintf(stderr, "%s: %s [%s:%d]\n", LogLevelName(r.level), r.message.c_str(),
                     r.file, r.line);
    } else {
      for (const SinkEntry& e : reg.sinks)
        if (all || r.level <= e.level) e.sink->Write(r);
    }
  }
  t_writing = false;
}

LogRecord MakeRecord(LogLevel level, const char* file, int line, std::string message) {
  LogRecord r;
  r.level = level;
  r.time = std::chrono::system_clock::now();
  r.file = BaseName(file);
  r.line = line;
  r.thread = ThreadName();
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
    message.pop_back();
  r.message = std::move(message);
  return r;
}

}  // namespace

const char* LogLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kError: return "error";
    case LogLevel::kWarning: return "warning";
    case LogLevel::kInfo: return "info";
    case LogLevel::kDebug: return "debug";
  }
  return "info";
}

bool ParseLogLevel(const std::string& text, LogLevel* level) {
  std::string t;
  for (char c : text) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (LogLevel l : {LogLevel::kError, LogLevel::kWarning, LogLevel::kInfo, LogLevel::kDebug}) {
    if (t == LogLevelName(l)) {
      *level = l;
      return true;
    }
  }
  return false;
}

std::string FormatLogRecord(const LogRecord& r) {
  const std::time_t secs = std::chrono::system_clock::to_time_t(r.time);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      r.time.time_since_epoch()).count() % 1000;
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &secs);
#else
  localtime_r(&secs, &tm);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
  static const char kLetter[] = {'E', 'W', 'I', 'D'};
  char head[96];
  std::snprintf(head, sizeof head, "%s.%03d %c ", stamp, static_cast<int>(ms),
                kLetter[static_cast<int>(r.level) & 3]);
  std::string line = head;
  line += r.thread;
  line += ' ';
  if (r.file != nullptr && r.file[0] != '\0') {
    line += r.file;
    line += ':';
    line += std::to_string(r.line);
  }
  line += "  ";
  line += r.message;
  return line;
}

int AddLogSink(std::shared_ptr<LogSink> sink, LogLevel max_level) {
  Registry& r = Reg();
  std::lock_guard<std::mutex> lock(r.mu);
  const int id = r.next_id++;
  r.sinks.push_back(SinkEntry{id, std::move(sink), max_level});
  UpdateMaxLevel(r);
  return id;
}

void RemoveLogSink(int id) {
  Registry& r = Reg();
  std::shared_ptr<LogSink> removed;  // released after the lock
  std::lock_guard<std::mutex> lock(r.mu);
  auto it = std::find_if(r.sinks.begin(), r.sinks.end(),
                         [id](const SinkEntry& e) { return e.id == id; });
  if (it == r.sinks.end()) return;
  removed = std::move(it->sink);
  r.sinks.erase(it);
  UpdateMaxLevel(r);
}

bool LogEnabled(LogLevel level) {
  return static_cast<int>(level) <= g_max_level.load(std::memory_order_relaxed);
}

void LogWrite(LogLevel level, const char* file, int line, std::string message) {
  if (!LogEnabled(level)) return;
  Dispatch(MakeRecord(level, file, line, std::move(message)), false);
}

void LogBanner(const char* file, int line, std::string message) {
  Dispatch(MakeRecord(LogLevel::kInfo, file, line, std::move(message)), true);
}

void SetLogThreadName(const std::string& name) { t_thread_name = name; }

void StderrLogSink::Write(const LogRecord& r) {
  std::fprintf(stderr, "%s\n", FormatLogRecord(r).c_str());
}

}  // namespace fv

extern "C" {

int fv_log_enabled(int level) { return fv::LogEnabled(static_cast<fv::LogLevel>(level)); }

void fv_log_write(int level, const char* file, int line, const char* message) {
  fv::LogWrite(static_cast<fv::LogLevel>(level), file, line, message ? message : "");
}

void fv_log_vprintf(int level, const char* file, int line, const char* fmt, va_list ap) {
  if (!fv_log_enabled(level) || fmt == nullptr) return;
  va_list copy;
  va_copy(copy, ap);
  const int n = std::vsnprintf(nullptr, 0, fmt, copy);
  va_end(copy);
  if (n < 0) return;
  std::string text(static_cast<size_t>(n), '\0');
  std::vsnprintf(&text[0], text.size() + 1, fmt, ap);
  fv::LogWrite(static_cast<fv::LogLevel>(level), file, line, std::move(text));
}

}  // extern "C"
