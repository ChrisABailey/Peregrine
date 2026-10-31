// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ENC named-object table, against the Charleston fixture.

#include "fv_enc_names.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string EncRoot() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc";
  return fs::is_directory(p) ? p : std::string();
}

#define REQUIRE_ENC(root)                                     \
  const std::string root = EncRoot();                         \
  if (root.empty()) GTEST_SKIP() << "testdata/enc not present"

std::vector<std::string> Cells(const std::string& root) {
  std::vector<std::string> out;
  for (const auto& e : fs::recursive_directory_iterator(root))
    if (e.path().extension() == ".000") out.push_back(e.path().string());
  return out;
}

std::vector<const fv::EncNameEntry*> Named(const std::vector<fv::EncNameEntry>& v,
                                           const std::string& name) {
  std::vector<const fv::EncNameEntry*> out;
  for (const fv::EncNameEntry& e : v)
    if (e.name == name) out.push_back(&e);
  return out;
}

}  // namespace

TEST(EncNames, OneRowPerObjectAcrossCellsAndBands) {
  REQUIRE_ENC(root);
  std::vector<fv::EncNameEntry> names;
  ASSERT_TRUE(fv::BuildEncNames(Cells(root), root, &names).ok());
  EXPECT_GT(names.size(), 400u);

  const auto marina = Named(names, "Municipal Marina");
  ASSERT_EQ(1u, marina.size());
  EXPECT_EQ("HRBFAC", marina[0]->object_class);

  // Charted in the harbour cells and the approach cell; kept from the harbour band.
  const auto buoy = Named(names, "Charleston Harbor North Channel Lighted Buoy 31");
  ASSERT_EQ(1u, buoy.size());
  EXPECT_EQ(5, buoy[0]->usage_band);
  EXPECT_NEAR(32.77, buoy[0]->position.lat, 0.05);
}

TEST(EncNames, RoundTripsThroughTheFile) {
  REQUIRE_ENC(root);
  std::vector<fv::EncNameEntry> names;
  ASSERT_TRUE(fv::BuildEncNames(Cells(root), root, &names).ok());
  const fs::path file = fs::temp_directory_path() / "fv_enc_names_test.tsv";
  ASSERT_TRUE(fv::WriteEncNames(file.string(), names).ok());
  std::vector<fv::EncNameEntry> back;
  ASSERT_TRUE(fv::ReadEncNames(file.string(), &back).ok());
  fs::remove(file);
  ASSERT_EQ(names.size(), back.size());
  EXPECT_EQ(names[0].name, back[0].name);
  EXPECT_EQ(names[0].object_class, back[0].object_class);
  EXPECT_NEAR(names[0].position.lon, back[0].position.lon, 1e-6);
}

TEST(EncNames, CancelStopsTheBuild) {
  REQUIRE_ENC(root);
  std::vector<fv::EncNameEntry> names;
  const fv::Status s = fv::BuildEncNames(Cells(root), root, &names,
                                         [](size_t done, size_t) { return done < 2; });
  EXPECT_EQ(fv::kInterrupted, s.code);
}
