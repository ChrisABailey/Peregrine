// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "fv_log_c.h"
#include "fvkit/log.h"

namespace fs = std::filesystem;

namespace {

/// Keeps every record it receives.
class MemorySink : public fv::LogSink {
 public:
  void Write(const fv::LogRecord& r) override { records.push_back(r); }
  std::vector<fv::LogRecord> records;
};

/// Registers a MemorySink for the test's lifetime.
struct ScopedSink {
  explicit ScopedSink(fv::LogLevel level) : sink(std::make_shared<MemorySink>()) {
    id = fv::AddLogSink(sink, level);
  }
  ~ScopedSink() { fv::RemoveLogSink(id); }
  std::shared_ptr<MemorySink> sink;
  int id = 0;
};

/// Counts how often it is streamed.
struct Counted {
  int* count;
};
std::ostream& operator<<(std::ostream& os, const Counted& c) {
  ++*c.count;
  return os << "counted";
}

std::vector<std::string> ReadLines(const std::string& path) {
  std::vector<std::string> lines;
  std::ifstream in(path);
  for (std::string line; std::getline(in, line);) lines.push_back(line);
  return lines;
}

fs::path FreshDir(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / ("fv_log_test_" + name);
  fs::remove_all(dir);
  return dir;
}

void C_Log(int level, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fv_log_vprintf(level, "c_source.c", 7, fmt, ap);
  va_end(ap);
}

TEST(Log, RecordsCarryFileLineAndLevel) {
  ScopedSink s(fv::LogLevel::kInfo);
  const int line = __LINE__ + 1;
  FV_LOG_WARNING("frame " << 3 << " unreadable");
  ASSERT_EQ(s.sink->records.size(), 1u);
  const fv::LogRecord& r = s.sink->records[0];
  EXPECT_EQ(r.level, fv::LogLevel::kWarning);
  EXPECT_EQ(r.message, "frame 3 unreadable");
  EXPECT_STREQ(r.file, "log_test.cpp");  // base name, not the full path
  EXPECT_EQ(r.line, line);
  const std::string text = fv::FormatLogRecord(r);
  EXPECT_NE(text.find(" W "), std::string::npos) << text;
  EXPECT_NE(text.find("log_test.cpp:" + std::to_string(line) + "  frame 3"), std::string::npos)
      << text;
}

TEST(Log, FilteredDebugDoesNoFormatting) {
  ScopedSink s(fv::LogLevel::kInfo);
  int count = 0;
  FV_LOG_DEBUG("value " << Counted{&count});
  EXPECT_EQ(count, 0);
  EXPECT_TRUE(s.sink->records.empty());
  FV_LOG_INFO("value " << Counted{&count});
  EXPECT_EQ(count, 1);
  EXPECT_EQ(s.sink->records.size(), 1u);
}

TEST(Log, EachSinkGetsItsOwnLevels) {
  ScopedSink errors(fv::LogLevel::kError);
  ScopedSink all(fv::LogLevel::kDebug);
  FV_LOG_ERROR("e");
  FV_LOG_INFO("i");
  FV_LOG_DEBUG("d");
  EXPECT_EQ(errors.sink->records.size(), 1u);
  EXPECT_EQ(all.sink->records.size(), 3u);
}

TEST(Log, EnabledFollowsTheMostVerboseSink) {
  // No sink: the stderr fallback takes errors and warnings only.
  EXPECT_TRUE(fv::LogEnabled(fv::LogLevel::kWarning));
  EXPECT_FALSE(fv::LogEnabled(fv::LogLevel::kInfo));
  {
    ScopedSink s(fv::LogLevel::kDebug);
    EXPECT_TRUE(fv::LogEnabled(fv::LogLevel::kDebug));
  }
  EXPECT_FALSE(fv::LogEnabled(fv::LogLevel::kDebug));
}

TEST(Log, BannerReachesEverySink) {
  ScopedSink errors(fv::LogLevel::kError);
  fv::LogBanner(__FILE__, __LINE__, "Peregrine 1.0 started");
  ASSERT_EQ(errors.sink->records.size(), 1u);
  EXPECT_EQ(errors.sink->records[0].level, fv::LogLevel::kInfo);
}

TEST(Log, ThreadNamesAppearInRecords) {
  ScopedSink s(fv::LogLevel::kInfo);
  std::thread([] {
    fv::SetLogThreadName("render");
    FV_LOG_INFO("from the worker");
  }).join();
  ASSERT_EQ(s.sink->records.size(), 1u);
  EXPECT_EQ(s.sink->records[0].thread, "render");
}

TEST(Log, CEntryFormatsOnlyWhenEnabled) {
  ScopedSink s(fv::LogLevel::kWarning);
  C_Log(FV_LOG_C_WARNING, "%s: row %d\n", "bsb", 12);
  C_Log(FV_LOG_C_DEBUG, "%s", "dropped");
  ASSERT_EQ(s.sink->records.size(), 1u);
  EXPECT_EQ(s.sink->records[0].message, "bsb: row 12");  // trailing newline dropped
  EXPECT_STREQ(s.sink->records[0].file, "c_source.c");
  EXPECT_EQ(s.sink->records[0].line, 7);
}

TEST(Log, SinkThatLogsDoesNotRecurse) {
  struct Echo : fv::LogSink {
    int writes = 0;
    void Write(const fv::LogRecord&) override {
      ++writes;
      FV_LOG_ERROR("from inside a sink");
    }
  };
  auto echo = std::make_shared<Echo>();
  const int id = fv::AddLogSink(echo, fv::LogLevel::kDebug);
  FV_LOG_ERROR("outer");
  fv::RemoveLogSink(id);
  EXPECT_EQ(echo->writes, 1);
}

TEST(Log, ParseLevel) {
  fv::LogLevel l = fv::LogLevel::kError;
  EXPECT_TRUE(fv::ParseLogLevel("Debug", &l));
  EXPECT_EQ(l, fv::LogLevel::kDebug);
  EXPECT_TRUE(fv::ParseLogLevel("warning", &l));
  EXPECT_EQ(l, fv::LogLevel::kWarning);
  EXPECT_FALSE(fv::ParseLogLevel("verbose", &l));
}

TEST(FileLogSink, WritesTodaysFileFromManyThreads) {
  const fs::path dir = FreshDir("threads");
  fv::Status st;
  auto sink = fv::FileLogSink::Open(dir.string(), "App", &st);
  ASSERT_NE(sink, nullptr) << st.message;
  const int id = fv::AddLogSink(sink, fv::LogLevel::kInfo);
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t)
    threads.emplace_back([t] {
      for (int i = 0; i < 50; ++i) FV_LOG_INFO("thread " << t << " line " << i);
    });
  for (std::thread& t : threads) t.join();
  fv::RemoveLogSink(id);

  const std::string path = sink->Path();
  EXPECT_EQ(fs::path(path).parent_path(), dir);
  EXPECT_EQ(fs::path(path).filename().string().rfind("App-", 0), 0u);
  const std::vector<std::string> lines = ReadLines(path);
  ASSERT_EQ(lines.size(), 200u);
  for (const std::string& line : lines) EXPECT_NE(line.find("log_test.cpp:"), std::string::npos);
  fs::remove_all(dir);
}

TEST(FileLogSink, RollsAFullFileAndKeepsTheNewest) {
  const fs::path dir = FreshDir("roll");
  fs::create_directories(dir);
  // Older days' files, the oldest of which pruning removes.
  for (const char* day : {"2026-01-01", "2026-01-02", "2026-01-03"})
    std::ofstream(dir / (std::string("App-") + day + ".log")) << "old\n";
  std::ofstream(dir / "Other-2026-01-01.log") << "not ours\n";

  fv::Status st;
  auto sink = fv::FileLogSink::Open(dir.string(), "App", &st, /*max_bytes=*/400,
                                    /*keep_files=*/3);
  ASSERT_NE(sink, nullptr) << st.message;
  fv::LogRecord r;
  r.time = std::chrono::system_clock::now();
  r.file = "x.cpp";
  r.message = std::string(100, 'm');
  for (int i = 0; i < 12; ++i) {
    r.line = i;
    r.time += std::chrono::seconds(1);  // distinct rolled names
    sink->Write(r);
  }

  int ours = 0;
  for (const auto& e : fs::directory_iterator(dir))
    if (e.path().filename().string().rfind("App-", 0) == 0) ++ours;
  EXPECT_EQ(ours, 3);
  EXPECT_TRUE(fs::exists(dir / "Other-2026-01-01.log"));
  EXPECT_FALSE(fs::exists(dir / "App-2026-01-01.log"));
  EXPECT_TRUE(fs::exists(sink->Path()));
  EXPECT_LE(fs::file_size(sink->Path()), 400u + 200u);
  fs::remove_all(dir);
}

TEST(FileLogSink, ANewDayStartsANewFile) {
  const fs::path dir = FreshDir("day");
  fv::Status st;
  auto sink = fv::FileLogSink::Open(dir.string(), "App", &st);
  ASSERT_NE(sink, nullptr) << st.message;
  const std::string today = sink->Path();
  fv::LogRecord r;
  r.time = std::chrono::system_clock::now() + std::chrono::hours(48);
  r.message = "later";
  sink->Write(r);
  EXPECT_NE(sink->Path(), today);
  EXPECT_EQ(ReadLines(sink->Path()).size(), 1u);
  fs::remove_all(dir);
}

TEST(FileLogSink, UnwritableDirectoryFails) {
  const fs::path dir = FreshDir("blocked");
  std::ofstream(dir.string()) << "a file, not a directory";
  fv::Status st;
  EXPECT_EQ(fv::FileLogSink::Open((dir / "sub").string(), "App", &st), nullptr);
  EXPECT_FALSE(st.ok());
  fs::remove(dir);
}

TEST(LogPaths, DefaultDirectoryAndOs) {
  const std::string dir = fv::DefaultLogDirectory("Peregrine");
#if defined(__APPLE__)
  EXPECT_NE(dir.find("/Library/Logs/Peregrine"), std::string::npos) << dir;
#elif !defined(_WIN32)
  EXPECT_NE(dir.find("peregrine"), std::string::npos) << dir;
#endif
  EXPECT_FALSE(fv::OperatingSystemDescription().empty());
}

}  // namespace
