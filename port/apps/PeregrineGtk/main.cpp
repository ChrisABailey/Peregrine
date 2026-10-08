// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// main.cpp — entry point of the Linux desktop app.

#include <string>
#include <vector>

#include "application.h"
#include "fv_desk_launch.h"

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv, argv + argc);
  auto app = peregrine::Application::create(fv::desk::ParseLaunchOptions(args));
  // The options are DeskKit's; GApplication would reject them as unknown.
  const int status = app->run(1, argv);
  return status != 0 ? status : app->exit_status();
}
