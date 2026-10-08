# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# DeskKit (fv::desk) — the desktop application model: commands, the menu
# model, map groups, overlay manifests, the options model, workspaces, user
# settings, the Desk that composes them and FakeDesk, the headless driver.
# port/desktop-plan.md §2b, §3a.

# map-groups.json is compiled in as the built-in table.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/map-groups.json FV_DESK_MAP_GROUPS_JSON)
set(FV_DESK_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(WRITE ${FV_DESK_GEN_DIR}/fv_desk_map_groups_builtin.inc.tmp
  "constexpr char kBuiltinMapGroupsJson[] = R\"fvjson(${FV_DESK_MAP_GROUPS_JSON})fvjson\";\n")
configure_file(${FV_DESK_GEN_DIR}/fv_desk_map_groups_builtin.inc.tmp
  ${FV_DESK_GEN_DIR}/fv_desk_map_groups_builtin.inc COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/map-groups.json)

add_library(fv_deskkit STATIC
  fv_desk_base_map.cpp
  fv_desk_catalog_build.cpp
  fv_desk_commands.cpp
  fv_desk_desk.cpp
  fv_desk_fake.cpp
  fv_desk_host.cpp
  fv_desk_launch.cpp
  fv_desk_map_groups.cpp
  fv_desk_map_options.cpp
  fv_desk_menu_model.cpp
  fv_desk_options.cpp
  fv_desk_overlay_manifest.cpp
  fv_desk_user_settings.cpp
  fv_desk_workspace.cpp
)
target_include_directories(fv_deskkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${FV_DESK_GEN_DIR})
target_link_libraries(fv_deskkit PUBLIC fv_viewkit fv_fvkit PRIVATE fv_json fv_png)
target_compile_features(fv_deskkit PUBLIC cxx_std_17)

if(FVW_BUILD_TESTS)
  add_executable(fv_deskkit_test
    test/catalog_build_test.cpp
    test/commands_test.cpp
    test/desk_test.cpp
    test/host_test.cpp
    test/launch_test.cpp
    test/map_groups_test.cpp
    test/map_options_test.cpp
    test/menu_model_test.cpp
    test/options_test.cpp
    test/overlay_manifest_test.cpp
    test/user_settings_test.cpp
    test/workspace_test.cpp
  )
  target_link_libraries(fv_deskkit_test PRIVATE fv_deskkit GTest::gtest_main)
  target_compile_definitions(fv_deskkit_test PRIVATE
    FV_MAP_GROUPS_JSON="${CMAKE_CURRENT_SOURCE_DIR}/map-groups.json")
  gtest_discover_tests(fv_deskkit_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/testdata")
endif()
