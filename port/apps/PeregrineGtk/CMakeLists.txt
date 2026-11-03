# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# peregrine-gtk — the Linux desktop app, a GTK4 (gtkmm-4) shell over
# DeskKit's DeskHost (port/linux-plan.md). Skipped with a message when
# gtkmm-4.0 is not installed, so the core still builds and tests without it.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GTKMM4 QUIET IMPORTED_TARGET gtkmm-4.0>=4.10)
endif()
if(NOT GTKMM4_FOUND)
  message(STATUS "peregrine-gtk: gtkmm-4.0 >= 4.10 not found; the Linux app is not built")
  return()
endif()

set(PEREGRINE_GTK_WINDOWS
  attribute_table_window.cpp
  data_sources_window.cpp
  dialogs.cpp
  feature_form_window.cpp
  find_window.cpp
  layer_fields_panel.cpp
  layer_style_window.cpp
  layers_panel.cpp
  properties_window.cpp
  query_builder_panel.cpp
  main_menu.cpp
  map_canvas.cpp
  map_window.cpp
  new_layer_window.cpp
  options_window.cpp
  overlay_manager_window.cpp
  profile_window.cpp
)
add_executable(peregrine-gtk application.cpp main.cpp ${PEREGRINE_GTK_WINDOWS})
# The app links the shared core (extensions plan EX-D8), so a `native` overlay
# package binds to the same copy of every process-wide table. The core keeps
# its third-party libraries (libpng 1.2, libjpeg, zlib, expat, libtiff) local,
# so they do not interpose the system copies GTK loads.
if(TARGET peregrine_core)
  target_link_libraries(peregrine-gtk PRIVATE peregrine_core PkgConfig::GTKMM4)
else()
  target_link_libraries(peregrine-gtk PRIVATE fv_deskkit PkgConfig::GTKMM4)
endif()
# Anything still linked statically stays out of the executable's exports, for
# the same reason.
target_link_options(peregrine-gtk PRIVATE "LINKER:--exclude-libs,ALL")
# The release a package's `requires.peregrine` is checked against; 0.0.0 is
# an unversioned build, which checks nothing.
target_compile_definitions(peregrine-gtk PRIVATE PEREGRINE_GTK_VERSION="${PEREGRINE_VERSION}")

# Installation, as the `peregrine` component the root CPack configuration packages:
# the app, its launcher entry and icon, the GEOTRANS datum tables (MSPCCS_DATA) and
# the OpenStreetMap style sheets. main.cpp and map_window.cpp find the data
# directory relative to the executable, so the layout works under any prefix.
include(GNUInstallDirs)
set(PEREGRINE_GTK_DATADIR ${CMAKE_INSTALL_DATADIR}/peregrine)
file(RELATIVE_PATH _peregrine_data_from_bin
  /${CMAKE_INSTALL_BINDIR} /${PEREGRINE_GTK_DATADIR})
target_compile_definitions(peregrine-gtk PRIVATE
  PEREGRINE_DATA_FROM_BIN="${_peregrine_data_from_bin}")

install(TARGETS peregrine-gtk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT peregrine)

# The shared core and the Python bridge live in <libdir>/peregrine. DeskKit
# looks for the bridge beside the core, so the build tree gets a link to it
# there as well; without the bridge, python packages list as unavailable.
if(TARGET peregrine_core)
  set(PEREGRINE_GTK_LIBDIR ${CMAKE_INSTALL_LIBDIR}/peregrine)
  file(RELATIVE_PATH _peregrine_lib_from_bin
    /${CMAKE_INSTALL_BINDIR} /${PEREGRINE_GTK_LIBDIR})
  set_target_properties(peregrine-gtk PROPERTIES
    INSTALL_RPATH "$ORIGIN/${_peregrine_lib_from_bin}")
  install(TARGETS peregrine_core LIBRARY DESTINATION ${PEREGRINE_GTK_LIBDIR}
    COMPONENT peregrine NAMELINK_SKIP)
  if(TARGET peregrine_python)
    add_dependencies(peregrine-gtk peregrine_python)
    add_custom_command(TARGET peregrine-gtk POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:peregrine_python>
        $<TARGET_FILE_DIR:peregrine_core>/$<TARGET_FILE_NAME:peregrine_python>
      VERBATIM)
    install(TARGETS peregrine_python LIBRARY DESTINATION ${PEREGRINE_GTK_LIBDIR}
      COMPONENT peregrine)
  endif()
endif()
# The system packages directory (extensions plan §1c), empty as shipped, and
# the .pgpkg MIME type the launcher entry opens.
install(DIRECTORY DESTINATION ${PEREGRINE_GTK_DATADIR}/packages COMPONENT peregrine)
install(FILES data/org.peregrine.Peregrine.mime.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME org.peregrine.Peregrine.xml
  COMPONENT peregrine)
install(FILES data/org.peregrine.Peregrine.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT peregrine)
install(FILES data/org.peregrine.Peregrine.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT peregrine)
install(FILES data/org.peregrine.Peregrine.metainfo.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo COMPONENT peregrine)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib/
  DESTINATION ${PEREGRINE_GTK_DATADIR}/geotrans COMPONENT peregrine)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/port/Osm/styles/
  DESTINATION ${PEREGRINE_GTK_DATADIR}/osm-styles COMPONENT peregrine)
install(FILES ${CMAKE_SOURCE_DIR}/COPYING ${CMAKE_SOURCE_DIR}/COPYING.LESSER
  ${CMAKE_SOURCE_DIR}/NOTICE.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/peregrine COMPONENT peregrine)

# The menu, action, shortcut, key and frame-placement tests run without a display. The
# window tests and the --shot tests need an X server, so they run under xvfb-run when that
# is present: the dialogs and feature-layer windows, the window with an empty catalog, and
# with a catalog built from testdata/.
if(FVW_BUILD_TESTS)
  add_executable(peregrine_gtk_test test/main_menu_test.cpp main_menu.cpp map_canvas.cpp)
  target_include_directories(peregrine_gtk_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(peregrine_gtk_test PRIVATE fv_deskkit PkgConfig::GTKMM4 GTest::gtest_main)
  target_link_options(peregrine_gtk_test PRIVATE "LINKER:--exclude-libs,ALL")
  gtest_discover_tests(peregrine_gtk_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/testdata")

  add_executable(peregrine-gtk-build-catalog test/build_catalog.cpp)
  target_link_libraries(peregrine-gtk-build-catalog PRIVATE fv_deskkit)

  find_program(XVFB_RUN xvfb-run)
  if(XVFB_RUN)
    # The dialog tests show windows, so the whole binary runs under its own Xvfb.
    add_executable(peregrine_gtk_dialogs_test test/dialogs_test.cpp test/feature_windows_test.cpp
      test/overlay_manager_test.cpp ${PEREGRINE_GTK_WINDOWS})
    target_include_directories(peregrine_gtk_dialogs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(peregrine_gtk_dialogs_test PRIVATE
      FV_FEATURE_RIVERS="${CMAKE_SOURCE_DIR}/port/fvkit/test/data/charleston-rivers.geojson"
      FV_RANGERINGS_PGPKG="${CMAKE_BINARY_DIR}/port/sdk/examples/rangerings-c/com.example.rangerings.pgpkg")
    add_dependencies(peregrine_gtk_dialogs_test rangerings_pgpkg)
    target_link_libraries(peregrine_gtk_dialogs_test PRIVATE
      fv_deskkit PkgConfig::GTKMM4 GTest::gtest_main)
    target_link_options(peregrine_gtk_dialogs_test PRIVATE "LINKER:--exclude-libs,ALL")
    add_test(NAME peregrine_gtk.dialogs
      COMMAND ${XVFB_RUN} -a $<TARGET_FILE:peregrine_gtk_dialogs_test>)
    set_property(TEST peregrine_gtk.dialogs APPEND PROPERTY
      ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/testdata")
    set_property(TEST peregrine_gtk.dialogs APPEND PROPERTY
      ENVIRONMENT "MSPCCS_DATA=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib")
    # Under a Wayland session GTK would open on the desktop rather than on Xvfb.
    set_property(TEST peregrine_gtk.dialogs APPEND PROPERTY ENVIRONMENT "GDK_BACKEND=x11")

    add_test(NAME peregrine_gtk.shot_empty_catalog
      COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:peregrine-gtk>
        -DXVFB_RUN=${XVFB_RUN}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/shot_test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/shot_test.cmake)
    add_test(NAME peregrine_gtk.shot_testdata
      COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:peregrine-gtk>
        -DBUILD_CATALOG=$<TARGET_FILE:peregrine-gtk-build-catalog>
        -DDATA=${CMAKE_SOURCE_DIR}/testdata
        -DGEOTRANS=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib
        -DXVFB_RUN=${XVFB_RUN}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/shot_testdata
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/shot_testdata.cmake)
    set_tests_properties(peregrine_gtk.shot_testdata PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")

    # The same picture from an installed copy with MSPCCS_DATA unset: the app has to
    # find the datum tables the install put beside it.
    add_test(NAME peregrine_gtk.installed_shot
      COMMAND ${CMAKE_COMMAND}
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DBUILD_CATALOG=$<TARGET_FILE:peregrine-gtk-build-catalog>
        -DDATA=${CMAKE_SOURCE_DIR}/testdata
        -DXVFB_RUN=${XVFB_RUN}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/installed_shot
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/installed_shot_test.cmake)
    set_tests_properties(peregrine_gtk.installed_shot PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
  endif()
endif()
