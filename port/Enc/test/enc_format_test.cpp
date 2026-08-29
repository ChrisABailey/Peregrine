// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::EncFrameEnumerator tests (ENC phase E4) — ENC as catalog rows, the
// last thing between a rendering S-52 engine and the ENC entry in the app's
// Map menu.
//
// The boxes pinned here for the four Charleston cells are the same four
// s57_test.cpp pins against the producer's own CATALOG.031 — which is the
// point of the cross-check: the enumerator must agree with the reader about
// where a cell is, whether it took the catalogue shortcut or opened the cell.
// The corpus is NOT fixed (four coarser cells arrived 2026-07-28), so
// everything else here is derived from what is on disk.

#include "fv_enc_format.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "fv_s57.h"
#include "fvkit/formats/registry.h"

namespace fs = std::filesystem;

namespace {

std::string EncRoot() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc";
  return fs::is_directory(p) ? p : std::string();
}

#define SKIP_WITHOUT_ENC()                \
  const std::string enc_root = EncRoot(); \
  if (enc_root.empty()) GTEST_SKIP() << "no ENC test data"

std::vector<fv::FrameInfo> EnumerateAll(const std::string& dir,
                                        fv::Status* status = nullptr) {
  fv::EncFrameEnumerator e;
  const fv::Status s = e.Begin(dir);
  if (status != nullptr) *status = s;
  std::vector<fv::FrameInfo> out;
  if (!s.ok()) return out;
  fv::FrameInfo f;
  while (e.Next(&f)) out.push_back(f);
  return out;
}

std::string Stem(const std::string& path) {
  return fs::path(path).stem().string();
}

}  // namespace

TEST(EncEnumerate, FindsEveryCellAsItsOwnRow) {
  SKIP_WITHOUT_ENC();
  const std::vector<fv::FrameInfo> frames = EnumerateAll(enc_root);

  // Counted from the tree, not hardcoded: a cell dropped alongside must not
  // turn into a spurious failure (the 2026-07-23 TestData lesson, which bit
  // the GeoTIFF inventory again on 2026-07-28).
  std::vector<std::string> cells;
  ASSERT_TRUE(fv::EnumerateEncCells(enc_root, &cells).ok());
  ASSERT_FALSE(cells.empty());
  EXPECT_EQ(frames.size(), cells.size());

  std::set<std::string> stems;
  for (const fv::FrameInfo& f : frames) {
    stems.insert(Stem(f.path));
    EXPECT_GT(f.size_bytes, 0) << f.path;
    EXPECT_LT(f.bounds.ll.lat, f.bounds.ur.lat) << f.path;
    EXPECT_LT(f.bounds.ll.lon, f.bounds.ur.lon) << f.path;
    // The path must be openable AS a cell — it is what EncVectorSource gets.
    EXPECT_TRUE(fs::is_regular_file(f.path)) << f.path;
  }
  EXPECT_TRUE(stems.count("US5CHSDC"));
  EXPECT_TRUE(stems.count("US5CHSED"));
}

// Each cell's usage band — the FIFTH character of its name, which is S-57's
// own convention — becomes its series and its nominal scale.
//
// This used to assert "band 5" over the whole directory, because the four
// Charleston cells were all harbour cells. The 2026-07-28 drop added
// US2EC02M, US3SC1CB, US4SC1CO and US4SC1BO — bands 2, 3 and 4 over the same
// water — which turns a one-band assertion into the real one: EVERY band
// present maps to its own series and scale, and the corpus now exercises four
// of the six rather than one.
TEST(EncEnumerate, UsageBandBecomesTheSeriesAndTheScale) {
  SKIP_WITHOUT_ENC();
  std::set<std::string> series_seen;
  for (const fv::FrameInfo& f : EnumerateAll(enc_root)) {
    const std::string stem = Stem(f.path);
    ASSERT_GE(stem.size(), 5u) << f.path;
    const int band = stem[2] - '0';
    EXPECT_EQ(fv::EncBandName(band), f.series_key) << f.path;
    EXPECT_DOUBLE_EQ(fv::EncBandNominalScale(band), f.scale) << f.path;
    EXPECT_EQ(0, f.scale_units) << "scale is a denominator";
    series_seen.insert(f.series_key);
  }
  EXPECT_TRUE(series_seen.count("Harbour")) << "the Charleston cells are band 5";
}

TEST(EncEnumerate, BandTableCoversEveryNavigationalPurpose) {
  const struct {
    int band;
    const char* name;
    double scale;
  } kBands[] = {
      {1, "Overview", 3000000.0}, {2, "General", 1000000.0},
      {3, "Coastal", 300000.0},   {4, "Approach", 50000.0},
      {5, "Harbour", 12000.0},    {6, "Berthing", 4000.0},
  };
  for (const auto& b : kBands) {
    EXPECT_EQ(b.name, fv::EncBandName(b.band));
    EXPECT_DOUBLE_EQ(b.scale, fv::EncBandNominalScale(b.band));
  }
  // Bands run 1..6; anything else is unknown, not silently band 1.
  EXPECT_TRUE(fv::EncBandName(0).empty());
  EXPECT_TRUE(fv::EncBandName(7).empty());
  EXPECT_DOUBLE_EQ(0.0, fv::EncBandNominalScale(0));
  // A coarser band must sort to a LARGER denominator, or BestSeriesForScale
  // would choose backwards.
  for (int b = 1; b < 6; ++b)
    EXPECT_GT(fv::EncBandNominalScale(b), fv::EncBandNominalScale(b + 1));
}

// The catalogue shortcut and the parse-the-cell fallback must produce the
// same box. TestData is the case that proves it: four downloads, but only one
// CATALOG.031 at the root, so a root-level enumeration takes the shortcut for
// that cell and opens the other three — unless it also reads the catalogues
// in the ENC_ROOT-N shells, which it does.
TEST(EncEnumerate, CatalogueShortcutAgreesWithTheCellItself) {
  SKIP_WITHOUT_ENC();
  const std::vector<fv::FrameInfo> frames = EnumerateAll(enc_root);
  ASSERT_FALSE(frames.empty());

  for (const fv::FrameInfo& f : frames) {
    fv::S57Cell cell;
    const fv::Status s = cell.Open(f.path);
    ASSERT_TRUE(s.ok()) << f.path << ": " << s.message;
    const fv::GeoRect& own = cell.bounds();
    // The catalogue declares the cell's authored LIMITS; the geometry inside
    // may not reach them, so the assertion is containment, not equality.
    EXPECT_LE(f.bounds.ll.lat, own.ll.lat + 1e-9) << f.path;
    EXPECT_LE(f.bounds.ll.lon, own.ll.lon + 1e-9) << f.path;
    EXPECT_GE(f.bounds.ur.lat, own.ur.lat - 1e-9) << f.path;
    EXPECT_GE(f.bounds.ur.lon, own.ur.lon - 1e-9) << f.path;
  }
}

// Enumerating one cell's own folder has no catalogue to consult at all, which
// is the fallback path end to end. It must find that cell and agree with the
// root scan about it.
TEST(EncEnumerate, WorksWithoutAnyCatalogue) {
  SKIP_WITHOUT_ENC();
  const std::string dir = enc_root + "/US5CHSDC";
  if (!fs::is_directory(dir)) GTEST_SKIP() << "cell folder not laid out";
  ASSERT_FALSE(fs::is_regular_file(dir + "/CATALOG.031"))
      << "this test needs a folder WITHOUT a catalogue";

  const std::vector<fv::FrameInfo> frames = EnumerateAll(dir);
  ASSERT_EQ(1u, frames.size());
  EXPECT_EQ("US5CHSDC", Stem(frames[0].path));
  EXPECT_EQ("Harbour", frames[0].series_key);

  // Same cell, found from the root, where the catalogue does answer.
  const fv::FrameInfo* from_root = nullptr;
  const std::vector<fv::FrameInfo> all = EnumerateAll(enc_root);
  for (const fv::FrameInfo& f : all)
    if (Stem(f.path) == "US5CHSDC") from_root = &f;
  ASSERT_NE(nullptr, from_root);
  // The catalogue box contains the parsed one (see the test above); both must
  // at least overlap the same harbour.
  EXPECT_LT(frames[0].bounds.ll.lat, from_root->bounds.ur.lat);
  EXPECT_LT(from_root->bounds.ll.lat, frames[0].bounds.ur.lat);
}

TEST(EncEnumerate, MissingAndEmptyDirectoriesFailCleanly) {
  fv::Status s;
  EnumerateAll("/nonexistent/enc", &s);
  EXPECT_EQ(fv::kNotFound, s.code);

  const fs::path tmp =
      fs::temp_directory_path() / "fv_enc_format_test_empty";
  fs::create_directories(tmp);
  EnumerateAll(tmp.string(), &s);
  EXPECT_EQ(fv::kUnsupported, s.code) << "an ENC-less directory is not an ENC";
  fs::remove_all(tmp);
}

TEST(EncEnumerate, RegistersAsAVectorOnlyFormat) {
  fv::ClearFormatRegistryForTest();
  EXPECT_EQ(nullptr, fv::FindFormat("enc"));

  fv::RegisterEncFormat();
  fv::RegisterEncFormat();  // idempotent, no duplicate error

  const fv::FormatFactories* enc = fv::FindFormat("enc");
  ASSERT_NE(nullptr, enc);
  EXPECT_TRUE(enc->make_enumerator != nullptr);
  // ENC draws through the vector seam, so it has no raster or elevation
  // surface — the same shape as "vpf".
  EXPECT_TRUE(enc->make_raster_source == nullptr);
  EXPECT_TRUE(enc->make_elevation_source == nullptr);

  auto e = enc->make_enumerator();
  ASSERT_NE(nullptr, e);
  EXPECT_EQ(fv::kNotFound, e->Begin("/nonexistent").code);

  fv::ClearFormatRegistryForTest();
}
