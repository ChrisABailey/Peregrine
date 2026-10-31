// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// main.cpp — entry point of the Linux desktop app.

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "application.h"
#include "fv_desk_launch.h"

namespace {

/// Points MSPCCS_DATA at the GEOTRANS datum tables installed beside the app
/// (`<prefix>/share/peregrine/geotrans`) when the environment does not set it.
/// A build-tree binary has no such directory and is left as it was.
void UseInstalledData() {
#ifdef PEREGRINE_DATA_FROM_BIN
  if (std::getenv("MSPCCS_DATA") != nullptr) return;
  std::error_code ec;
  const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec) return;
  const std::filesystem::path tables =
      exe.parent_path() / PEREGRINE_DATA_FROM_BIN / "geotrans";
  if (std::filesystem::is_directory(tables, ec))
    setenv("MSPCCS_DATA", tables.lexically_normal().c_str(), 0);
#endif
}

}  // namespace

int main(int argc, char** argv) {
  UseInstalledData();
  const std::vector<std::string> args(argv, argv + argc);
  auto app = peregrine::Application::create(fv::desk::ParseLaunchOptions(args));
  // The options are DeskKit's; GApplication would reject them as unknown.
  const int status = app->run(1, argv);
  return status != 0 ? status : app->exit_status();
}
