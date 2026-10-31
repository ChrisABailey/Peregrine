// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// NOAA ENC product catalog: parse a synthetic catalog in NOAA's layout and
// compare it with installed index rows.

#include "fv_enc_catalog.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/** One cell element as NOAA writes it, coverage panel included. */
std::string CellXml(const std::string& name, const std::string& status, int edtn, int updn,
                    const std::vector<std::string>& states,
                    const std::vector<int>& districts) {
  std::string x = "  <cell>\n    <name>" + name + "</name>\n    <lname>Test &amp; Bay</lname>\n" +
                  "    <cscale>20000</cscale>\n    <status>" + status + "</status>\n" +
                  "    <coast_guard_districts>\n";
  for (int d : districts)
    x += "      <coast_guard_district>" + std::to_string(d) + "</coast_guard_district>\n";
  x += "    </coast_guard_districts>\n    <states>\n";
  for (const std::string& s : states) x += "      <state>" + s + "</state>\n";
  x += "    </states>\n    <regions>\n      <region>30</region>\n    </regions>\n"
       "    <zipfile_location>https://www.charts.noaa.gov/ENCs/" + name + ".zip</zipfile_location>\n"
       "    <edtn>" + std::to_string(edtn) + "</edtn>\n"
       "    <updn>" + std::to_string(updn) + "</updn>\n"
       "    <uadt>2025-12-09</uadt>\n    <isdt>2026-08-04</isdt>\n"
       "    <cov>\n      <panel>\n        <panel_no>1</panel_no>\n        <type>E</type>\n"
       "        <vertex><lat>32.7</lat><long>-79.9</long></vertex>\n"
       "      </panel>\n    </cov>\n  </cell>\n";
  return x;
}

std::string CatalogXml(const std::string& cells) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<EncProductCatalog>\n  <Header>\n"
         "    <title>ENC Product Catalog</title>\n"
         "    <dt_valid>2026-10-26T04:55:10Z</dt_valid>\n  </Header>\n" +
         cells + "</EncProductCatalog>\n";
}

std::string WriteTemp(const std::string& name, const std::string& text) {
  const fs::path p = fs::temp_directory_path() / name;
  std::ofstream(p, std::ios::binary) << text;
  return p.string();
}

fv::EncIndexEntry Installed(const std::string& name, const std::string& edition, int updates,
                            bool cancelled = false) {
  fv::EncIndexEntry e;
  e.name = name;
  e.edition = edition;
  e.update_count = updates;
  e.applied_updates = updates;
  e.cancelled = cancelled;
  return e;
}

/** Florida-shaped catalog: three FL cells (one shared with GA), one GA-only
 *  cell, one cancelled FL cell. */
fv::EncCatalog FloridaCatalog() {
  fv::EncCatalog cat;
  cat.cells = {
      {"US5FLAAA", false, 3, 2, {"FL", "07CGD"}},
      {"US5FLBBB", false, 4, 0, {"FL", "07CGD"}},
      {"US4FLGAC", false, 1, 5, {"FL", "GA", "07CGD"}},
      {"US5GADDD", false, 2, 1, {"GA", "07CGD"}},
      {"US5FLEEE", true, 6, 1, {"FL", "07CGD"}},
  };
  return cat;
}

TEST(ReadEncCatalog, ReadsNoaaLayout) {
  const std::string path = WriteTemp(
      "fv_enc_catalog_test.xml",
      CatalogXml(CellXml("US1EEZ1M", "Cancelled", 10, 1, {"AK", "HI"}, {14}) +
                 CellXml("US5CHSDC", "Active", 2, 3, {"SC"}, {7})));
  fv::EncCatalog cat;
  ASSERT_TRUE(fv::ReadEncCatalog(path, &cat).ok());
  EXPECT_EQ(cat.valid, "2026-10-26T04:55:10Z");
  ASSERT_EQ(cat.cells.size(), 2u);
  EXPECT_EQ(cat.cells[0].name, "US1EEZ1M");
  EXPECT_TRUE(cat.cells[0].cancelled);
  EXPECT_EQ(cat.cells[0].edition, 10);
  EXPECT_EQ(cat.cells[0].update, 1);
  EXPECT_EQ(cat.cells[0].regions, (std::vector<std::string>{"14CGD", "AK", "HI"}));
  EXPECT_EQ(cat.cells[1].name, "US5CHSDC");
  EXPECT_FALSE(cat.cells[1].cancelled);
  EXPECT_EQ(cat.cells[1].edition, 2);
  EXPECT_EQ(cat.cells[1].update, 3);
  EXPECT_EQ(cat.cells[1].regions, (std::vector<std::string>{"07CGD", "SC"}));
  fs::remove(path);
}

TEST(ReadEncCatalog, RejectsTruncatedAndEmpty) {
  fv::EncCatalog cat;
  const std::string cut = CatalogXml(CellXml("US5CHSDC", "Active", 2, 3, {"SC"}, {7}));
  const std::string bad = WriteTemp("fv_enc_catalog_cut.xml", cut.substr(0, cut.size() / 2));
  EXPECT_EQ(fv::ReadEncCatalog(bad, &cat).code, fv::kIoError);
  EXPECT_TRUE(cat.cells.empty());
  const std::string empty = WriteTemp("fv_enc_catalog_empty.xml", CatalogXml(""));
  EXPECT_EQ(fv::ReadEncCatalog(empty, &cat).code, fv::kNotFound);
  EXPECT_EQ(fv::ReadEncCatalog("/nonexistent/ENCProdCat.xml", &cat).code, fv::kIoError);
  fs::remove(bad);
  fs::remove(empty);
}

TEST(InferEncRegion, PicksTheStateNotTheDistrict) {
  const fv::EncCatalog cat = FloridaCatalog();
  // A Florida zip as delivered: every active FL cell plus the cancelled one.
  const std::vector<fv::EncIndexEntry> fl = {
      Installed("US5FLAAA", "3", 2), Installed("US5FLBBB", "4", 0),
      Installed("US4FLGAC", "1", 5), Installed("US5FLEEE", "6", 1, true)};
  EXPECT_EQ(fv::InferEncRegion(fl, cat), "FL");
  EXPECT_EQ(fv::InferEncRegion({Installed("US5XXXXX", "1", 0)}, cat), "");
}

TEST(CompareEncCatalog, CurrentSetIsUpToDate) {
  const std::vector<fv::EncIndexEntry> fl = {
      Installed("US5FLAAA", "3", 2), Installed("US5FLBBB", "4", 0),
      Installed("US4FLGAC", "1", 5), Installed("US5FLEEE", "6", 1, true)};
  const fv::EncCatalogComparison c = fv::CompareEncCatalog(fl, FloridaCatalog());
  EXPECT_EQ(c.region, "FL");
  EXPECT_TRUE(c.up_to_date());
  EXPECT_TRUE(c.unlisted.empty());
}

TEST(CompareEncCatalog, SortsEachKindOfChange) {
  const std::vector<fv::EncIndexEntry> fl = {
      Installed("US5FLAAA", "3", 1),        // one update behind
      Installed("US5FLBBB", "3", 7),        // older edition, more updates
      Installed("US5FLEEE", "6", 0),        // still drawn, NOAA cancelled it
      Installed("US5FLOLD", "1", 0)};       // gone from the catalog
  // US4FLGAC is missing locally.
  const fv::EncCatalogComparison c = fv::CompareEncCatalog(fl, FloridaCatalog(), "FL");
  EXPECT_FALSE(c.up_to_date());
  EXPECT_EQ(c.new_updates, (std::vector<std::string>{"US5FLAAA"}));
  EXPECT_EQ(c.new_editions, (std::vector<std::string>{"US5FLBBB"}));
  EXPECT_EQ(c.cancelled, (std::vector<std::string>{"US5FLEEE"}));
  EXPECT_EQ(c.added, (std::vector<std::string>{"US4FLGAC"}));
  EXPECT_EQ(c.unlisted, (std::vector<std::string>{"US5FLOLD"}));
}

TEST(CompareEncCatalog, ReissuedCellIsANewEdition) {
  fv::EncCatalog cat;
  cat.cells = {{"US5FLAAA", false, 1, 0, {"FL"}}};
  const fv::EncCatalogComparison c =
      fv::CompareEncCatalog({Installed("US5FLAAA", "5", 1, true)}, cat, "FL");
  EXPECT_EQ(c.new_editions, (std::vector<std::string>{"US5FLAAA"}));
}

TEST(EncRegionFromZipName, StatesAndDistricts) {
  EXPECT_EQ(fv::EncRegionFromZipName("FL_ENCs.zip"), "FL");
  EXPECT_EQ(fv::EncRegionFromZipName("07CGD_ENCs.zip"), "07CGD");
  EXPECT_EQ(fv::EncRegionFromZipName("fl_ENCs.zip"), "");
  EXPECT_EQ(fv::EncRegionFromZipName("FL_ENCs (1).zip"), "");
  EXPECT_EQ(fv::EncRegionFromZipName("_ENCs.zip"), "");
  EXPECT_EQ(fv::EncRegionFromZipName("ALL_ENCs.zip"), "");
}

/** Live check: FVW_ENC_CATALOG names a downloaded ENCProdCat.xml and
 *  testdata/enc-regions/FL holds the Florida set. */
TEST(EncCatalogBench, DISABLED_FloridaAgainstLiveCatalog) {
  const char* cat_path = getenv("FVW_ENC_CATALOG");
  const char* data = getenv("FVW_TESTDATA_DIR");
  if (cat_path == nullptr || data == nullptr) GTEST_SKIP() << "FVW_ENC_CATALOG not set";
  fv::EncCatalog cat;
  ASSERT_TRUE(fv::ReadEncCatalog(cat_path, &cat).ok());
  std::vector<fv::EncIndexEntry> fl;
  ASSERT_TRUE(fv::BuildEncIndex(std::string(data) + "/enc-regions/FL", &fl).ok());
  const fv::EncCatalogComparison c = fv::CompareEncCatalog(fl, cat);
  printf("catalog %s: %zu cells; region %s; editions %zu updates %zu cancelled %zu "
         "added %zu unlisted %zu\n",
         cat.valid.c_str(), cat.cells.size(), c.region.c_str(), c.new_editions.size(),
         c.new_updates.size(), c.cancelled.size(), c.added.size(), c.unlisted.size());
  for (const std::string& n : c.new_editions) printf("  new edition %s\n", n.c_str());
  for (const std::string& n : c.new_updates) printf("  new updates %s\n", n.c_str());
  EXPECT_EQ(c.region, "FL");
}

}  // namespace
