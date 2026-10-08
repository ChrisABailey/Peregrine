# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# Builds a catalog from the sample data, runs `peregrine-gtk --catalog
# --center --shot` on the Charleston orthophoto under xvfb-run, and checks
# that the status line names the GeoTIFF series and that the PNG holds a
# picture rather than one flat colour. Prints "SKIPPED:" without the data.
# Inputs: APP, BUILD_CATALOG, DATA (testdata/), GEOTRANS (MSPCCS_DATA),
# XVFB_RUN, WORK (a scratch directory).

if(NOT EXISTS ${DATA}/geotiff/22620e2710n_4ft.tif)
  message("SKIPPED: no GeoTIFF in ${DATA}")
  return()
endif()

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
set(CATALOG ${WORK}/catalog.sqlite)
set(SHOT ${WORK}/shot.png)
set(ENV{XDG_CONFIG_HOME} ${WORK}/config)
set(ENV{XDG_DATA_HOME} ${WORK}/data)
set(ENV{MSPCCS_DATA} ${GEOTRANS})
set(ENV{GSK_RENDERER} cairo)

execute_process(
  COMMAND ${BUILD_CATALOG} ${DATA} ${CATALOG}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  TIMEOUT 120)
message(STATUS "catalog: ${out}${err}")
if(NOT result EQUAL 0)
  message(FATAL_ERROR "building the catalog failed (${result})")
endif()

execute_process(
  COMMAND ${XVFB_RUN} -a ${APP} --catalog ${CATALOG} --center 32.5814,-80.1416,6714
          --shot ${SHOT}
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
if(NOT "${out}${err}" MATCHES "shot\\.png: 1:6,714 \\| geotiff Color")
  message(FATAL_ERROR "the status line does not report the GeoTIFF series at 1:6,714")
endif()
# A flat fill (no frame, or a node the renderer could not draw) compresses to
# a few kilobytes; the orthophoto does not.
file(SIZE ${SHOT} bytes)
if(bytes LESS 100000)
  message(FATAL_ERROR "${SHOT} is ${bytes} bytes; the map was not drawn")
endif()
