# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# MSP GEOTRANS 3.3 Coordinate Conversion Service, built unmodified from
# third_party/geotrans-3.3 (NGA source; already has POSIX/pthread support).
# GLOB is acceptable here: the third_party tree is frozen.

set(GEOTRANS_SRC ${CMAKE_SOURCE_DIR}/third_party/geotrans-3.3/CCS/src)

file(GLOB_RECURSE GEOTRANS_SOURCES ${GEOTRANS_SRC}/*.cpp)

add_library(msp_ccs STATIC ${GEOTRANS_SOURCES})

# Sources include headers both as "dtcc/..."-rooted paths and bare names from
# sibling directories, so expose every source subdirectory.
file(GLOB_RECURSE GEOTRANS_HEADERS ${GEOTRANS_SRC}/*.h)
set(GEOTRANS_INCLUDE_DIRS "")
foreach(hdr ${GEOTRANS_HEADERS})
  get_filename_component(dir ${hdr} DIRECTORY)
  list(APPEND GEOTRANS_INCLUDE_DIRS ${dir})
endforeach()
list(REMOVE_DUPLICATES GEOTRANS_INCLUDE_DIRS)

target_include_directories(msp_ccs PUBLIC ${GEOTRANS_SRC} ${GEOTRANS_INCLUDE_DIRS})

find_package(Threads REQUIRED)
target_link_libraries(msp_ccs PUBLIC Threads::Threads)

# Third-party code, 2012 vintage: keep the build quiet but unmodified.
target_compile_options(msp_ccs PRIVATE -w)
