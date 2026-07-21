# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# VPF/DNC table reader (vpf-geosym-plan phase V1): the non-GDI subset of the
# sources VpfMapServer.vcxproj compiles — tables, db/library/coverage/
# recordset, indexes, spatial index tree, variants. The GDI-mixed topology/
# render files (vpfelem, VPFFace, vpf_poly, tile, VPFText) belong to the
# render plugin and wait for phase V5. vpf_tree.cpp is excluded too: despite
# the name it is not the spatial tree but VPF_copy_elements/VPF_delete_elements
# (copying coverages to removable media via MdsUtilities + the Windows shell) —
# no reader logic. VPFDataLib is a DIVERGED FORK used by other Windows
# projects — never compile it here (V0).

set(VPF ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/VpfMapServer/Vpf)

add_library(fv_vpf STATIC
  ${VPF}/indexes.cpp
  ${VPF}/lst_iter.cpp
  ${VPF}/tables.cpp
  ${VPF}/variant.cpp
  ${VPF}/vpfdb.cpp
  ${VPF}/vpfrcset.cpp
)
target_include_directories(fv_vpf PUBLIC
  ${VPF}
  ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/VpfMapServer  # StdAfx.h
)
target_link_libraries(fv_vpf PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_vpf_test test/vpf_test.cpp)
  target_link_libraries(fv_vpf_test PRIVATE fv_vpf GTest::gtest_main)
  gtest_discover_tests(fv_vpf_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
