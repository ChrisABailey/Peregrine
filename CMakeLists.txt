cmake_minimum_required(VERSION 3.21)
project(Peregrine LANGUAGES C CXX)

# Peregrine: a cross-platform (macOS/Linux) port of FalconView, built as
# headless core libraries. Modules opt in one at a time as they are ported
# (see port/PORTING.md). The upstream Windows product build is separate and
# is never modified by this tree.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FVW_BUILD_TESTS "Build unit tests" ON)

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
add_subdirectory(port/fvkit)
add_subdirectory(port/bindings/pyfvw)
