# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# Runs `peregrine-gtk --shot` with no catalog under xvfb-run and checks that a
# PNG is written and the status line reports the empty catalog.
# Inputs: APP (the binary), XVFB_RUN, WORK (a scratch directory).

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
set(SHOT ${WORK}/shot.png)

# A private config directory keeps the user's settings out of the test.
set(ENV{XDG_CONFIG_HOME} ${WORK}/config)
set(ENV{XDG_DATA_HOME} ${WORK}/data)
set(ENV{GSK_RENDERER} cairo)

execute_process(
  COMMAND ${XVFB_RUN} -a ${APP} --shot ${SHOT}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  TIMEOUT 60)
message(STATUS "stdout: ${out}")
message(STATUS "stderr: ${err}")

if(NOT result EQUAL 0)
  message(FATAL_ERROR "peregrine-gtk --shot exited with ${result}")
endif()
if(NOT EXISTS ${SHOT})
  message(FATAL_ERROR "no PNG at ${SHOT}")
endif()
file(READ ${SHOT} signature LIMIT 8 HEX)
if(NOT signature STREQUAL "89504e470d0a1a0a")
  message(FATAL_ERROR "${SHOT} is not a PNG (${signature})")
endif()
# xvfb-run sends the program's stderr to its stdout.
if(NOT "${out}${err}" MATCHES "shot\\.png: [^\n]*No map data in the catalog")
  message(FATAL_ERROR "the status line does not report the empty catalog")
endif()
