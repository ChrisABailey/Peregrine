// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// scratch.h — a per-test scratch directory, removed when the test ends.
#pragma once

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace fv {
namespace desk {
namespace test {

/// A fresh directory named after the running test and the process, so
/// parallel ctest runs never share one.
class Scratch {
 public:
  Scratch() {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = std::filesystem::temp_directory_path() /
           ("fv_deskkit_" + std::string(info->test_suite_name()) + "_" + info->name() + "_" +
            std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
    std::filesystem::create_directories(dir_, ec);
  }
  ~Scratch() {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  std::string Path(const std::string& name) const { return (dir_ / name).string(); }

 private:
  std::filesystem::path dir_;
};

}  // namespace test
}  // namespace desk
}  // namespace fv
