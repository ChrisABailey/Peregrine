cmake_minimum_required(VERSION 3.21)
project(Peregrine LANGUAGES C CXX)

# Peregrine: a cross-platform (macOS/Linux) port of FalconView, built as
# headless core libraries. Modules opt in one at a time as they are ported
# (see port/PORTING.md). The upstream Windows product build is separate and
# is never modified by this tree.

# DEFAULT BUILD TYPE. `cmake -B build` names no configuration, and a
# single-config generator with an empty CMAKE_BUILD_TYPE passes NO optimizer
# flag at all — so the default build would be -O0. That is not a small
# difference for a renderer: re-measuring the port's own profile at -O2 more
# than halves it (a DNC harbour frame 90 -> 49 ms, a retained pan 21 -> 3.4).
#
# RelWithDebInfo rather than Release: the port is debugged as often as it is
# profiled, and -O2 -g costs nothing but disk. Any explicit
# -DCMAKE_BUILD_TYPE=<name> wins (Debug for an unoptimized build), and
# multi-config generators (Visual Studio, Ninja Multi-Config) choose per
# build and are left alone.
get_property(_peregrine_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _peregrine_multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  message(STATUS "peregrine: no CMAKE_BUILD_TYPE given, defaulting to RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FVW_BUILD_TESTS "Build unit tests" ON)

# --- iOS cross-build ---------------------------------------------------------
#
# `FVW_IOS` is what every module asks. It is SET rather than asked for when the
# toolchain already says iOS, so an iOS build cannot be configured with the flag
# forgotten. It buys three things and no more:
#
#   * NOTHING WITH A HOST IN IT gets built — no test binaries (a device runs no
#     ctest, and gtest would come down as a fourth FetchContent), no CLI tools
#     (fvrender/fvpack/fvgraph are mac tools that MAKE the data a phone reads),
#     no pyfvw (there is no Python in the bundle).
#   * `pippin_core`, the umbrella static library the app links (see
#     port/apps/Pippin/CMakeLists.txt).
#   * static libraries throughout, which is already how this tree builds.
#
# The mac build sees none of it: every guard is `if(FVW_IOS)`, and with the
# option off this file is what it was.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(FVW_IOS ON CACHE BOOL "Cross-build the Pippin core for iOS" FORCE)
else()
  option(FVW_IOS "Cross-build the Pippin core for iOS" OFF)
endif()

if(FVW_IOS)
  # A cross-built test binary cannot be run by the machine that built it, so
  # there is nothing to gain by building one. The mac build is the test bed for
  # every line of C++ in the pack.
  set(FVW_BUILD_TESTS OFF CACHE BOOL "Build unit tests" FORCE)
  message(STATUS "peregrine: iOS cross-build — tests, CLI tools and pyfvw are off")
endif()

if(FVW_BUILD_TESTS)
  include(FetchContent)
  set(FVW_GTEST_VERSION "1.17.0" CACHE STRING "googletest release to build against")
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v${FVW_GTEST_VERSION}.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)
  enable_testing()
endif()

# Current upstream releases of the third-party libraries the port builds
# against (expat, zlib, ...). See port/third_party/CMakeLists.txt for the
# policy; the 2010-era forks under third_party/ and fvw_core/ImageLib stay
# untouched for the Windows product build.
add_subdirectory(port/third_party)

# Shared portability shim (header-only for now)
add_library(fv_compat INTERFACE)
target_include_directories(fv_compat INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/port/include)

# --- Ported modules, in porting order. Uncomment as each lands. ---
add_subdirectory(port/geoid)
add_subdirectory(port/geotrans)
add_subdirectory(port/geo3)
add_subdirectory(port/geo_tool)
add_subdirectory(port/MapScaleUtil)
add_subdirectory(port/MapSeriesStringConverter)
add_subdirectory(port/NetNMEA)
add_subdirectory(port/ImageLib)
add_subdirectory(port/DtedMapServer)
add_subdirectory(port/DtedShadedRenderer)
add_subdirectory(port/GeoTIFFMapServer)
add_subdirectory(port/CadrgDecoder)
add_subdirectory(port/ImageLibCore)
add_subdirectory(port/CadrgMapServer)
add_subdirectory(port/TirosMapServer)
add_subdirectory(port/VpfMapServer)
add_subdirectory(port/GeoSymServer)
add_subdirectory(port/Enc)
add_subdirectory(port/Osm)
add_subdirectory(port/Routing)
add_subdirectory(port/fvkit)
# RouteKit links BOTH fvkit and Routing, which is the one thing fvkit may not
# do — hence a module of its own, and hence after both of them.
add_subdirectory(port/RouteKit)
# pyfvw embeds CPython; there is none on a phone.
if(NOT FVW_IOS)
  add_subdirectory(port/bindings/pyfvw)
endif()

# Pippin, the iOS app. On iOS this builds the umbrella library the app links
# (`cmake --preset ios`); on the mac it builds one thing — the gtest over the
# pure-C++ half of PippinKit — and returns.
add_subdirectory(port/apps/Pippin)
