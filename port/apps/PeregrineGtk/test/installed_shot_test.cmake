# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# Installs the `peregrine` component into a scratch prefix and runs the
# shot_testdata check against the installed binary with MSPCCS_DATA unset, so
# the GeoTIFF datum shift works only if the app finds the installed tables.
# Inputs: BUILD_DIR, BUILD_CATALOG, DATA (testdata/), XVFB_RUN, WORK.

set(STAGE ${WORK}/stage)
file(REMOVE_RECURSE ${STAGE})
execute_process(
  COMMAND ${CMAKE_COMMAND} --install ${BUILD_DIR} --component peregrine --prefix ${STAGE}
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE err)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "installing the peregrine component failed (${result}): ${err}")
endif()

set(APP ${STAGE}/bin/peregrine-gtk)
set(GEOTRANS "")
set(WORK ${WORK}/run)
include(${CMAKE_CURRENT_LIST_DIR}/shot_testdata.cmake)
