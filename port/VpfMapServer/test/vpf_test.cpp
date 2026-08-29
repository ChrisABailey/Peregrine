// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// VPF/DNC reader tests (vpf-geosym-plan phase V1).
//
// Real data: TestData/vpf/dnc17, an NGA-public DNC 17 database. Values below
// are pinned from the ported reader after cross-checking them against the
// data itself (tile names encode their own corners, coverage lists match the
// on-disk directories, FACC codes are valid). They are the golden values for
// the phases that build on this reader.

#include "stdafx.h"

#include <sys/stat.h>

#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "lst_iter.h"
#include "variant.h"
#include "vpfdb.h"
#include "vpfrcset.h"

#include <gtest/gtest.h>

namespace {

std::string TestDataDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  return d ? std::string(d) : std::string();
}

// VPF paths are Win32-shaped (backslashes); the port resolves them at the
// file-open boundary, so tests use the same spelling the reader uses.
std::string Dnc17Root() { return TestDataDir() + "\\vpf\\dnc17"; }

// A library path is the database root WITH a trailing separator: VPFLibrary
// composes get_path() as m_path + m_name + '\\'.
CString Dnc17RootWithSlash() { return CString((Dnc17Root() + "\\").c_str()); }

// The build always sets FVW_TESTDATA_DIR, but the dnc17 sample database is
// not distributed with the source, so gate on the data actually being there.
bool HaveTestData() {
  if (TestDataDir().empty()) return false;
  struct stat sb;
  const std::string dnc = TestDataDir() + "/vpf/dnc17";
  return ::stat(dnc.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

std::vector<std::string> ToVector(const CStringList* list) {
  std::vector<std::string> out;
  POSITION pos = list->GetHeadPosition();
  while (pos)
    out.push_back((LPCSTR)const_cast<CStringList*>(list)->GetNext(pos));
  return out;
}

// The harbor library used throughout: DNC 17 Nantucket Sound / Cape Cod.
const char kHarborLibrary[] = "h1707330";

}  // namespace

#define SKIP_WITHOUT_TESTDATA() \
  if (!HaveTestData()) GTEST_SKIP() << "no dnc17 test data"

// ---------------------------------------------------------------------------
// Win32 path resolution (backslashes + case), the layer every open goes through
// ---------------------------------------------------------------------------

TEST(VpfPath, ResolvesBackslashesAndCase) {
  SKIP_WITHOUT_TESTDATA();

  // Mixed case on disk: dnc17/h1707330/ is lowercase, its CAT table is not.
  const std::string spec = Dnc17Root() + "\\H1707330\\cat";
  const std::string resolved = fv::FvResolveWin32Path(spec.c_str());

  EXPECT_EQ(resolved.find('\\'), std::string::npos) << resolved;
  struct stat sb;
  EXPECT_EQ(::stat(resolved.c_str(), &sb), 0) << resolved;
}

TEST(VpfPath, MissingPathComesBackUnchangedNotGuessed) {
  SKIP_WITHOUT_TESTDATA();

  const std::string spec = Dnc17Root() + "\\no_such_library\\cat";
  const std::string resolved = fv::FvResolveWin32Path(spec.c_str());

  struct stat sb;
  EXPECT_NE(::stat(resolved.c_str(), &sb), 0);
}

// Regression for the CString + char trap: without the operator+ overload,
// this silently became pointer arithmetic (see fv_cstring.h).
TEST(VpfPath, CStringPlusCharConcatenates) {
  CString path("dir");
  CString joined = path + '\\';
  EXPECT_STREQ((LPCSTR)joined, "dir\\");
  EXPECT_EQ(joined.GetLength(), 4);
}

// ---------------------------------------------------------------------------
// Database: library discovery
// ---------------------------------------------------------------------------

TEST(VpfDatabase, EnumeratesDnc17Libraries) {
  SKIP_WITHOUT_TESTDATA();

  VPFDatabase db(CString("dnc17"));
  ASSERT_TRUE(db.open(CString(Dnc17Root().c_str())));
  EXPECT_STREQ((LPCSTR)db.get_name(), "dnc17");

  const CStringList* names = db.get_library_names();
  ASSERT_TRUE(names != nullptr);
  const std::vector<std::string> libs = ToVector(names);
  delete names;

  // 11 libraries: browse, one general (gen17a), one coastal (coa17c),
  // three approach (a*) and five harbor (h*).
  EXPECT_EQ(libs.size(), 11u);
  const std::set<std::string> lib_set(libs.begin(), libs.end());
  for (const char* expected :
       {"browse", "gen17a", "coa17c", "a1707300", "a1707330", "a1707420",
        "h1707290", "h1707300", "h1707330", "h1707350", "h1707360"})
    EXPECT_EQ(lib_set.count(expected), 1u) << expected;
}

// NOTE (2026-07-20): VPFDatabase::open(path) discovers library NAMES but never
// populates m_paths_to_root (it computes path_to_root and then drops it), so a
// later open_library() builds a VPFLibrary with an EMPTY path. The library
// object comes back non-null but not open, which no caller checks. The other
// open() overload -- the DataSource/COV one FalconView actually calls -- does
// populate m_paths_to_root, which is why this never bit on Windows.
// Preserved as-is per the bit-faithful rule; headless callers construct
// VPFLibrary directly, as the tests below do.
TEST(VpfDatabase, OpenLibraryAfterPathOpenYieldsUnopenedLibraryPreservedQuirk) {
  SKIP_WITHOUT_TESTDATA();

  VPFDatabase db(CString("dnc17"));
  ASSERT_TRUE(db.open(CString(Dnc17Root().c_str())));
  EXPECT_TRUE(db.library_exists(CString(kHarborLibrary)));

  VPFLibrary* lib = db.open_library(CString(kHarborLibrary));
  ASSERT_TRUE(lib != nullptr);
  EXPECT_FALSE(lib->is_open());
  EXPECT_STREQ((LPCSTR)lib->get_path(), "h1707330\\");  // no root prefix
  delete lib;
}

// ---------------------------------------------------------------------------
// Library: coverages and tiles
// ---------------------------------------------------------------------------

TEST(VpfLibrary, OpensHarborLibraryAndListsCoverages) {
  SKIP_WITHOUT_TESTDATA();

  VPFLibrary lib(Dnc17RootWithSlash(), CString(kHarborLibrary));
  ASSERT_TRUE(lib.is_open());
  EXPECT_STREQ((LPCSTR)lib.name(), kHarborLibrary);

  const CStringList* covs = lib.get_coverage_names();
  ASSERT_TRUE(covs != nullptr);
  const std::vector<std::string> names = ToVector(covs);
  delete covs;

  // Coverage order comes from the coverage attribute table (CAT), not the
  // filesystem, so it is stable and worth pinning in full.
  const std::vector<std::string> expected = {
      "cul", "ecr", "env", "hyd", "iwy",     "lcr",   "lim",
      "nav", "obs", "por", "dqy", "tileref", "libref"};
  EXPECT_EQ(names, expected);
}

TEST(VpfLibrary, TileBoundsCoverNantucketSound) {
  SKIP_WITHOUT_TESTDATA();

  VPFLibrary lib(Dnc17RootWithSlash(), CString(kHarborLibrary));
  ASSERT_TRUE(lib.is_open());

  const std::vector<VPFTileBounds>* tiles = lib.get_tile_boundaries_list();
  ASSERT_TRUE(tiles != nullptr);
  ASSERT_EQ(tiles->size(), 10u);

  // Tile 1 of 10; the whole library is a 15'-tile grid over 41.0-41.75 N,
  // 70.5-69.5 W (Nantucket Sound).
  const VPFTileBounds& first = (*tiles)[0];
  EXPECT_STREQ(first.tile_name, "hjem3000");
  EXPECT_EQ(first.tile_id, 1);
  EXPECT_DOUBLE_EQ(first.m_geo_rect.ll.lat, 41.00);
  EXPECT_DOUBLE_EQ(first.m_geo_rect.ll.lon, -70.50);
  EXPECT_DOUBLE_EQ(first.m_geo_rect.ur.lat, 41.25);
  EXPECT_DOUBLE_EQ(first.m_geo_rect.ur.lon, -70.25);

  const VPFTileBounds& last = (*tiles)[9];
  EXPECT_STREQ(last.tile_name, "hjfm1530");
  EXPECT_EQ(last.tile_id, 10);
  EXPECT_DOUBLE_EQ(last.m_geo_rect.ll.lat, 41.50);
  EXPECT_DOUBLE_EQ(last.m_geo_rect.ll.lon, -69.75);

  // Every tile is a quarter-degree square, and ids run 1..10 in order.
  for (size_t i = 0; i < tiles->size(); ++i) {
    const VPFTileBounds& t = (*tiles)[i];
    EXPECT_EQ(t.tile_id, (short)(i + 1));
    EXPECT_NEAR(t.m_geo_rect.ur.lat - t.m_geo_rect.ll.lat, 0.25, 1e-9);
    EXPECT_NEAR(t.m_geo_rect.ur.lon - t.m_geo_rect.ll.lon, 0.25, 1e-9);
  }
}

// ---------------------------------------------------------------------------
// Coverage: feature classes and tile lookup
// ---------------------------------------------------------------------------

TEST(VpfCoverage, HydrographyFeatureClasses) {
  SKIP_WITHOUT_TESTDATA();

  VPFLibrary lib(Dnc17RootWithSlash(), CString(kHarborLibrary));
  ASSERT_TRUE(lib.is_open());

  VPFCoverage* hyd = lib.open_coverage(CString("hyd"));
  ASSERT_TRUE(hyd != nullptr);
  EXPECT_STREQ((LPCSTR)hyd->name(), "hyd");
  EXPECT_EQ(hyd->get_topology_level(), 3);  // full topology

  const std::vector<VPFFeatureClass*>* fcs = hyd->get_feature_class_list();
  ASSERT_TRUE(fcs != nullptr);
  ASSERT_EQ(fcs->size(), 4u);

  // delineation: 3 = area, 2 = line, 1 = point
  EXPECT_STREQ((LPCSTR)(*fcs)[0]->GetName(), "hydarea");
  EXPECT_EQ((*fcs)[0]->delineation(), 3);
  EXPECT_STREQ((LPCSTR)(*fcs)[1]->GetName(), "hydline");
  EXPECT_EQ((*fcs)[1]->delineation(), 2);
  EXPECT_STREQ((LPCSTR)(*fcs)[2]->GetName(), "botcharp");
  EXPECT_EQ((*fcs)[2]->delineation(), 1);
  EXPECT_STREQ((LPCSTR)(*fcs)[3]->GetName(), "soundp");
  EXPECT_EQ((*fcs)[3]->delineation(), 1);
}

TEST(VpfCoverage, TileLookupByLatLon) {
  SKIP_WITHOUT_TESTDATA();

  VPFLibrary lib(Dnc17RootWithSlash(), CString(kHarborLibrary));
  ASSERT_TRUE(lib.is_open());
  VPFCoverage* hyd = lib.open_coverage(CString("hyd"));
  ASSERT_TRUE(hyd != nullptr);

  // Inside the south-west tile.
  EXPECT_EQ(hyd->get_tile_id(41.01, -70.49), 1);
  EXPECT_STREQ((LPCSTR)hyd->get_tile_path(41.01, -70.49), "hjem3000");

  // Inside the north-east tile.
  EXPECT_EQ(hyd->get_tile_id(41.60, -69.60), 10);
  EXPECT_STREQ((LPCSTR)hyd->get_tile_path(41.60, -69.60), "hjfm1530");

  const d_geo_rect_t bounds = hyd->get_tile_bounds(41.01, -70.49);
  EXPECT_DOUBLE_EQ(bounds.ll.lat, 41.00);
  EXPECT_DOUBLE_EQ(bounds.ll.lon, -70.50);
}

// ---------------------------------------------------------------------------
// Recordset: table parsing (the layer everything above rides on)
// ---------------------------------------------------------------------------

TEST(VpfRecordset, ReadsCoverageAttributeTable) {
  SKIP_WITHOUT_TESTDATA();

  const std::string path = Dnc17Root() + "\\" + kHarborLibrary + "\\";
  VPFRecordset rs(CString(path.c_str()));
  ASSERT_EQ(rs.open(CString("cat")), SUCCESS);
  EXPECT_TRUE(rs.is_open());
  EXPECT_STREQ((LPCSTR)rs.get_name(), "cat");

  EXPECT_EQ(rs.get_record_count(), 13);
  ASSERT_EQ(rs.get_field_count(), 4);
  EXPECT_STREQ((LPCSTR)rs.get_field_info(0)->m_name, "id");
  EXPECT_STREQ((LPCSTR)rs.get_field_info(1)->m_name, "coverage_name");
  EXPECT_STREQ((LPCSTR)rs.get_field_info(2)->m_name, "description");
  EXPECT_STREQ((LPCSTR)rs.get_field_info(3)->m_name, "level");
}

TEST(VpfRecordset, ReadsHydAreaFeatureTable) {
  SKIP_WITHOUT_TESTDATA();

  const std::string path = Dnc17Root() + "\\" + kHarborLibrary + "\\hyd\\";
  VPFRecordset rs(CString(path.c_str()));
  ASSERT_EQ(rs.open(CString("hydarea.aft")), SUCCESS);

  EXPECT_EQ(rs.get_record_count(), 1134);
  ASSERT_EQ(rs.get_field_count(), 7);
  EXPECT_STREQ((LPCSTR)rs.get_field_info(1)->m_name, "f_code");
  EXPECT_STREQ((LPCSTR)rs.get_field_info(5)->m_name, "tile_id");
  EXPECT_STREQ((LPCSTR)rs.get_field_info(6)->m_name, "fac_id");

  // First record: a FACC BE010 (depth contour) area in tile 1, face 2.
  ASSERT_EQ(rs.set_absolute_position(0), SUCCESS);
  EXPECT_EQ(rs.get_field_value(CString("id"))->m_int_long, 1);
  EXPECT_STREQ((LPCSTR)rs.get_field_value(CString("f_code"))->m_text, "BE010");
  EXPECT_EQ(rs.get_field_value(CString("tile_id"))->m_int_short, 1);
  EXPECT_EQ(rs.get_field_value(CString("fac_id"))->m_int_long, 2);

  // Row ids are 1-based and dense, so the last row pins the whole walk.
  ASSERT_EQ(rs.set_absolute_position(rs.get_record_count() - 1), SUCCESS);
  EXPECT_EQ(rs.get_field_value(CString("id"))->m_int_long, 1134);
}

// The header length and every VPF "long integer" are 4 bytes on disk. On LP64
// the original's *(long*) read 8 (regression for the fix in read_in_header).
TEST(VpfRecordset, OnDiskLongsAreFourBytes) {
  SKIP_WITHOUT_TESTDATA();

  const std::string path = Dnc17Root() + "\\" + kHarborLibrary + "\\hyd\\";
  VPFRecordset rs(CString(path.c_str()));
  ASSERT_EQ(rs.open(CString("hydarea.aft")), SUCCESS);

  // Every id fits in 32 bits and increments by one; an 8-byte read would put
  // the next field's bytes in the high word and blow this up immediately.
  for (int i = 0; i < 50; ++i) {
    ASSERT_EQ(rs.set_absolute_position(i), SUCCESS);
    EXPECT_EQ(rs.get_field_value(CString("id"))->m_int_long, i + 1);
  }
}

TEST(VpfRecordset, ReadsTilerefTable) {
  SKIP_WITHOUT_TESTDATA();

  const std::string path = Dnc17Root() + "\\" + kHarborLibrary + "\\tileref\\";
  VPFRecordset rs(CString(path.c_str()));
  ASSERT_EQ(rs.open(CString("tileref.aft")), SUCCESS);

  EXPECT_EQ(rs.get_record_count(), 10);
  ASSERT_EQ(rs.get_field_count(), 3);

  ASSERT_EQ(rs.set_absolute_position(0), SUCCESS);
  EXPECT_STREQ((LPCSTR)rs.get_field_value(CString("tile_name"))->m_text,
               "hjem3000");
}

// close() used to leave the file/mapping handles dangling, so is_open() still
// said true and the destructor closed them again (use-after-free on POSIX,
// a recycled-handle close on Win32).
TEST(VpfRecordset, CloseIsIdempotent) {
  SKIP_WITHOUT_TESTDATA();

  const std::string path = Dnc17Root() + "\\" + kHarborLibrary + "\\";
  VPFRecordset rs(CString(path.c_str()));
  ASSERT_EQ(rs.open(CString("cat")), SUCCESS);
  ASSERT_TRUE(rs.is_open());

  rs.close();
  EXPECT_FALSE(rs.is_open());
  rs.close();  // must be a no-op, not a second free
  EXPECT_FALSE(rs.is_open());
}

// NOTE (2026-07-20): reopening a recordset undercounts rows. close() clears
// m_fields but leaves m_row_length, and setup_field_info_list ACCUMULATES into
// it (m_row_length += field length), so the second open sees a doubled row
// length and reports half the records (13 -> 6). Pre-existing on Windows and
// out of V1 scope -- pinned here so the fix is deliberate. FalconView never
// reopens a recordset, which is why it has not bitten.
TEST(VpfRecordset, ReopenUndercountsRowsPreservedQuirk) {
  SKIP_WITHOUT_TESTDATA();

  const std::string path = Dnc17Root() + "\\" + kHarborLibrary + "\\";
  VPFRecordset rs(CString(path.c_str()));
  ASSERT_EQ(rs.open(CString("cat")), SUCCESS);
  ASSERT_EQ(rs.get_record_count(), 13);

  rs.close();
  ASSERT_EQ(rs.open(CString("cat")), SUCCESS);
  EXPECT_EQ(rs.get_record_count(), 6);  // 13 would be correct
}

TEST(VpfRecordset, OpeningAMissingTableFails) {
  SKIP_WITHOUT_TESTDATA();

  const std::string path = Dnc17Root() + "\\" + kHarborLibrary + "\\";
  VPFRecordset rs(CString(path.c_str()));
  EXPECT_NE(rs.open(CString("no_such_table")), SUCCESS);
  EXPECT_FALSE(rs.is_open());
}
