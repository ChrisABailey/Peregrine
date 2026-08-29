// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::EncVectorSource tests (ENC phase E3a) — the LEFT-hand side of the vector
// seam for ENC, over the real Charleston cells E1 reads.
//
// The counts pinned here are DERIVED from the delivered data (E1's tests pin
// the same cells against the producer's own DSSI/CATALOG numbers). A cell
// dropped alongside moves any total over the directory — which is what
// happened on 2026-07-28 — so as of E6 the exact counts are pinned against ONE
// NAMED CELL and the whole-root tests assert structure and lower bounds. See
// the comment above OpensEveryCellUnderTheRoot.

#include "fv_enc_vector_source.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
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

#define SKIP_WITHOUT_ENC()                \
  const std::string enc_root = EncRoot(); \
  if (enc_root.empty()) GTEST_SKIP() << "no ENC test data"

#define OPEN_SOURCE(var)                            \
  fv::EncVectorSource var;                          \
  {                                                 \
    const fv::Status s = var.Open(enc_root);        \
    ASSERT_TRUE(s.ok()) << s.message;               \
  }

size_t CountOf(const std::vector<fv::VectorFeature>& f, const char* key) {
  size_t n = 0;
  for (const auto& v : f)
    if (v.style_key == key) ++n;
  return n;
}

// Cells present on disk, which is what "every cell under the root" means.
size_t CellsOnDisk(const std::string& root) {
  size_t n = 0;
  for (const auto& e : fs::recursive_directory_iterator(root))
    if (e.is_regular_file() && e.path().extension() == ".000") ++n;
  return n;
}

// The harbour box the four US5CHS* cells cover. Always present; the corpus
// around it is not.
const fv::GeoRect kCharlestonHarbour{{32.70, -80.025}, {32.85, -79.875}};

}  // namespace

// WHAT IS PINNED EXACTLY HERE, AND WHY IT IS NOT A TOTAL. `TestData/enc` grows
// when new cells are dropped into it — four coastal/approach cells arrived on
// 2026-07-28 beside the four Charleston harbour ones — so a count over the
// whole directory is an inventory fact with a short shelf life. (R3b made the
// same repair to a GeoTIFF test for the same reason; this is the next one.)
// The exact numbers therefore live on ONE NAMED CELL, which a sibling arriving
// cannot move, and the whole-root tests assert structure: every cell opened,
// every feature well-formed, the harbour still covered.
TEST(EncVectorSource, OpensEveryCellUnderTheRoot) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  EXPECT_TRUE(src.IsOpen());
  const size_t on_disk = CellsOnDisk(enc_root);
  EXPECT_GE(on_disk, 4u) << "the four Charleston cells are the floor";
  EXPECT_EQ(src.cell_count(), on_disk) << "a cell on disk was not opened";

  // The union of everything opened must contain the Charleston harbour box —
  // the four 0.075-degree cells E1 pinned, the same box its CATALOG.031 test
  // derives. Coarser cells only ever grow it.
  const fv::GeoRect b = src.Bounds();
  EXPECT_LE(b.ll.lat, kCharlestonHarbour.ll.lat + 0.01);
  EXPECT_LE(b.ll.lon, kCharlestonHarbour.ll.lon + 0.01);
  EXPECT_GE(b.ur.lat, kCharlestonHarbour.ur.lat - 0.01);
  EXPECT_GE(b.ur.lon, kCharlestonHarbour.ur.lon - 0.01);
}

// The exact-inventory half, on one cell. US5CHSDC is the harbour cell E1 read
// first and the one with the three update files.
TEST(EncVectorSource, OneNamedCellHasExactlyTheFeaturesItHas) {
  SKIP_WITHOUT_ENC();
  const std::string cell = enc_root + "/US5CHSDC/US5CHSDC.000";
  if (!fs::is_regular_file(cell)) GTEST_SKIP() << "no US5CHSDC";
  fv::EncVectorSource src;
  const fv::Status s = src.Open(cell);
  ASSERT_TRUE(s.ok()) << s.message;

  EXPECT_EQ(src.cell_count(), 1u);
  EXPECT_EQ(src.Layers().size(), 42u);

  std::vector<fv::VectorFeature> f;
  ASSERT_TRUE(src.Query(fv::VectorQuery(), &f).ok());
  EXPECT_EQ(f.size(), 600u);

  size_t points = 0, lines = 0, areas = 0;
  for (const auto& v : f) {
    switch (v.type) {
      case fv::VectorGeometryType::kPoint: ++points; break;
      case fv::VectorGeometryType::kLine: ++lines; break;
      default: ++areas; break;
    }
  }
  EXPECT_EQ(points, 105u);
  EXPECT_EQ(lines, 287u);
  EXPECT_EQ(areas, 208u);

  // The classes that carry the chart.
  EXPECT_EQ(CountOf(f, "DEPARE"), 59u);
  EXPECT_EQ(CountOf(f, "DEPCNT"), 70u);
  EXPECT_EQ(CountOf(f, "COALNE"), 83u);
  EXPECT_EQ(CountOf(f, "SOUNDG"), 11u);

  const fv::GeoRect b = src.Bounds();
  EXPECT_NEAR(b.ll.lat, 32.700, 0.001);
  EXPECT_NEAR(b.ll.lon, -80.025, 0.001);
  EXPECT_NEAR(b.ur.lat, 32.775, 0.001);
  EXPECT_NEAR(b.ur.lon, -79.950, 0.001);
}

// OpenCells is what a caller with a catalog uses, and the reason it exists is
// that a directory cannot express a USAGE BAND. TestData/enc holds bands 2, 3,
// 4 and 5 over the same water, so a source opened on the shared root serves
// all of them at once — which is how PythonView came to draw a 1:1,000,000
// general cell underneath a 1:12,000 harbour chart.
TEST(EncVectorSource, OpenCellsTakesExactlyTheCellsNamed) {
  SKIP_WITHOUT_ENC();
  std::vector<std::string> harbour;
  for (const char* name : {"US5CHSDC", "US5CHSDD", "US5CHSEC", "US5CHSED"}) {
    const std::string p =
        enc_root + "/" + name + "/" + name + ".000";
    if (fs::is_regular_file(p)) harbour.push_back(p);
  }
  ASSERT_FALSE(harbour.empty()) << "no Charleston harbour cells";

  fv::EncVectorSource src;
  const fv::Status s = src.OpenCells(harbour, enc_root);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(src.cell_count(), harbour.size());

  // The band's own extent, not the exchange set's. The band-2 cell in this
  // same directory reaches Bermuda; a harbour source must not.
  const fv::GeoRect b = src.Bounds();
  EXPECT_NEAR(b.ll.lat, kCharlestonHarbour.ll.lat, 0.01);
  EXPECT_NEAR(b.ur.lat, kCharlestonHarbour.ur.lat, 0.01);
  EXPECT_NEAR(b.ll.lon, kCharlestonHarbour.ll.lon, 0.01);
  EXPECT_NEAR(b.ur.lon, kCharlestonHarbour.ur.lon, 0.01);

  // Every cell opened is one that was named.
  for (size_t i = 0; i < src.cell_count(); ++i)
    EXPECT_NE(std::find(harbour.begin(), harbour.end(), src.cell_path(i)),
              harbour.end())
        << src.cell_path(i);

  // And the same viewport carries FEWER features than the whole directory
  // does, which is the defect stated as a number: the extra ones are the
  // coarser bands drawing under the harbour chart.
  fv::VectorQuery q;
  q.area = kCharlestonHarbour;
  std::vector<fv::VectorFeature> band, all;
  ASSERT_TRUE(src.Query(q, &band).ok());

  fv::EncVectorSource everything;
  ASSERT_TRUE(everything.Open(enc_root, enc_root).ok());
  ASSERT_TRUE(everything.Query(q, &all).ok());
  if (everything.cell_count() > src.cell_count())
    EXPECT_LT(band.size(), all.size())
        << "the coarser bands contributed nothing — re-derive this test";
}

TEST(EncVectorSource, OpenCellsRejectsAnEmptyList) {
  fv::EncVectorSource src;
  const fv::Status s = src.OpenCells({}, std::string());
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code, fv::kInvalidArg);
  EXPECT_FALSE(src.IsOpen());
}

TEST(EncVectorSource, ASingleCellOpensOnItsOwn) {
  SKIP_WITHOUT_ENC();
  const std::string cell = enc_root + "/US5CHSDC/US5CHSDC.000";
  if (!fs::is_regular_file(cell)) GTEST_SKIP() << "no US5CHSDC";
  fv::EncVectorSource src;
  const fv::Status s = src.Open(cell);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(src.cell_count(), 1u);
  EXPECT_FALSE(src.Layers().empty());
}

// The catalogue is what turns OBJL 42 into "DEPARE"; without it every feature
// would be unsymbolizable, so Open must refuse rather than serve numbers.
TEST(EncVectorSource, MissingCatalogueIsAnErrorNotAnEmptyOne) {
  SKIP_WITHOUT_ENC();
  fv::EncVectorSource src;
  const fs::path empty_dir =
      fs::temp_directory_path() / "fvw_enc_no_catalog_test";
  fs::create_directories(empty_dir);
  const fv::Status s = src.Open(enc_root, empty_dir.string());
  EXPECT_FALSE(s.ok());
  EXPECT_FALSE(src.IsOpen());
  fs::remove_all(empty_dir);
}

TEST(EncVectorSource, LayersAreObjectClassAcronymsInStableOrder) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  const std::vector<std::string> layers = src.Layers();
  EXPECT_GE(layers.size(), 58u) << "the Charleston cells alone reach 58 classes";
  EXPECT_TRUE(std::is_sorted(layers.begin(), layers.end()));
  for (const char* expected : {"COALNE", "DEPARE", "DEPCNT", "LNDARE",
                               "SOUNDG", "SLCONS"}) {
    EXPECT_NE(std::find(layers.begin(), layers.end(), expected), layers.end())
        << expected << " missing from the layer list";
  }
  // No layer may be a raw code: that would mean the catalogue did not answer.
  for (const std::string& l : layers)
    EXPECT_EQ(l.find("OBJL "), std::string::npos) << l;
}

TEST(EncVectorSource, QueryReturnsEveryDrawableFeature) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  std::vector<fv::VectorFeature> f;
  ASSERT_TRUE(src.Query(fv::VectorQuery(), &f).ok());
  ASSERT_FALSE(f.empty());

  size_t points = 0, lines = 0, areas = 0;
  for (const auto& v : f) {
    switch (v.type) {
      case fv::VectorGeometryType::kPoint: ++points; break;
      case fv::VectorGeometryType::kLine: ++lines; break;
      default: ++areas; break;
    }
    EXPECT_FALSE(v.parts.empty());
    EXPECT_EQ(v.layer, v.style_key);
    EXPECT_GE(v.ref.layer, 0);
  }
  // Every feature is one of the three kinds and nothing was dropped between
  // the query and the count.
  EXPECT_EQ(points + lines + areas, f.size());
  EXPECT_GT(points, 0u);
  EXPECT_GT(lines, 0u);
  EXPECT_GT(areas, 0u);

  // The classes that carry the chart are all present; the harbour cells alone
  // contribute these many, so a corpus that grows can only add.
  EXPECT_GE(CountOf(f, "DEPARE"), 479u);
  EXPECT_GE(CountOf(f, "DEPCNT"), 503u);
  EXPECT_GE(CountOf(f, "COALNE"), 440u);
  EXPECT_GE(CountOf(f, "SOUNDG"), 70u);
}

TEST(EncVectorSource, QueryClipsToTheRequestedArea) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  fv::VectorQuery q;
  q.area = fv::GeoRect{{32.77, -79.95}, {32.79, -79.92}};
  std::vector<fv::VectorFeature> f;
  ASSERT_TRUE(src.Query(q, &f).ok());
  ASSERT_FALSE(f.empty());
  EXPECT_LT(f.size(), 4470u);
  for (const auto& v : f) EXPECT_TRUE(v.bounds.Intersects(q.area));
}

TEST(EncVectorSource, MaxFeaturesCapsTheResult) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  fv::VectorQuery q;
  q.max_features = 25;
  std::vector<fv::VectorFeature> f;
  ASSERT_TRUE(src.Query(q, &f).ok());
  EXPECT_EQ(f.size(), 25u);
}

TEST(EncVectorSource, AttributesArriveUnderTheirAcronyms) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  std::vector<fv::VectorFeature> f;
  ASSERT_TRUE(src.Query(fv::VectorQuery(), &f).ok());

  size_t with_depth_range = 0;
  for (const auto& v : f) {
    if (v.style_key != "DEPARE") continue;
    const std::string* d1 = v.Attribute("DRVAL1");
    const std::string* d2 = v.Attribute("DRVAL2");
    if (d1 == nullptr || d2 == nullptr) continue;
    ++with_depth_range;
    EXPECT_FALSE(d1->empty());
    // Depths are metres as text; the ENC's own SOMF scaling happened in E1.
    EXPECT_NE(d1->find_first_of("0123456789"), std::string::npos) << *d1;
  }
  // EVERY depth area in the corpus declares its range — which is exactly why
  // the style engine's "?" (unknown-value) condition must not match them. The
  // count is the class total, so this is the strong claim: not one DEPARE is
  // missing DRVAL1/DRVAL2.
  EXPECT_EQ(with_depth_range, CountOf(f, "DEPARE"));
  EXPECT_GE(with_depth_range, 479u);
}

// S-57 keeps a sounding's value in the GEOMETRY (3-D vertices), so the source
// republishes it as a pseudo-attribute the style engine can reach. It is named
// DEPTH, not an Appendix A acronym, because Appendix A has no such attribute.
TEST(EncVectorSource, SoundingsPublishTheirDepthAsAnAttribute) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  std::vector<fv::VectorFeature> f;
  ASSERT_TRUE(src.Query(fv::VectorQuery(), &f).ok());

  size_t with_depth = 0;
  for (const auto& v : f) {
    if (v.style_key != "SOUNDG") continue;
    const std::string* d = v.Attribute("DEPTH");
    if (d == nullptr) continue;
    ++with_depth;
    EXPECT_FALSE(d->empty());
  }
  // Same shape: every sounding republishes its depth, whatever the corpus is.
  EXPECT_EQ(with_depth, CountOf(f, "SOUNDG"));
  EXPECT_GE(with_depth, 70u);
}

// SCAMIN is a property of the DATA ("do not show me below this scale"), so it
// is applied by the source. A query with no scale must not lose anything.
TEST(EncVectorSource, ScaminThinsOnlyWhenTheQueryNamesAScale) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);

  std::vector<fv::VectorFeature> all;
  ASSERT_TRUE(src.Query(fv::VectorQuery(), &all).ok());
  EXPECT_EQ(src.last_query_scamin_skipped(), 0u);

  fv::VectorQuery zoomed_in;
  zoomed_in.scale_denominator = 5000.0;
  std::vector<fv::VectorFeature> near;
  ASSERT_TRUE(src.Query(zoomed_in, &near).ok());

  fv::VectorQuery zoomed_out;
  zoomed_out.scale_denominator = 2000000.0;
  std::vector<fv::VectorFeature> far;
  ASSERT_TRUE(src.Query(zoomed_out, &far).ok());

  EXPECT_GT(src.last_query_scamin_skipped(), 0u);
  EXPECT_LT(far.size(), near.size());
  EXPECT_EQ(near.size(), all.size())
      << "a harbour-scale query must not lose harbour features";
}

TEST(EncVectorSource, DescribeDecodesThroughTheAppendixACatalogue) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  std::vector<fv::VectorFeature> f;
  ASSERT_TRUE(src.Query(fv::VectorQuery(), &f).ok());

  // Any depth area: its title is the class's long name, not the acronym.
  const fv::VectorFeature* depare = nullptr;
  for (const auto& v : f)
    if (v.style_key == "DEPARE") { depare = &v; break; }
  ASSERT_NE(depare, nullptr);

  fv::FeatureDescription d;
  const fv::Status s = src.Describe(depare->ref, &d);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(d.class_name, "DEPARE");
  EXPECT_EQ(d.title, "Depth area");
  // The note names the cell the feature came from — whichever that is, since
  // the corpus is not fixed.
  EXPECT_NE(d.source_note.find(".000"), std::string::npos) << d.source_note;
  EXPECT_FALSE(d.attributes.empty());
  for (const auto& a : d.attributes) {
    EXPECT_FALSE(a.code.empty());
    EXPECT_EQ(a.code.find("ATTL "), std::string::npos)
        << "attribute " << a.code << " did not resolve to an acronym";
  }

  // An enumerated attribute must come back as words, not as its value id.
  // QUAPOS/WATLEV/CATCOA are the common ones; take whichever the cells have.
  bool saw_decoded = false;
  for (const auto& v : f) {
    fv::FeatureDescription vd;
    if (!src.Describe(v.ref, &vd).ok()) continue;
    for (const auto& a : vd.attributes) {
      if (a.raw.empty()) continue;
      if (a.display != a.raw && a.display.find_first_not_of("0123456789.,-") !=
                                    std::string::npos) {
        saw_decoded = true;
        break;
      }
    }
    if (saw_decoded) break;
  }
  EXPECT_TRUE(saw_decoded) << "no enumerated value decoded to text";
}

TEST(EncVectorSource, DescribeRejectsARefFromNowhere) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  fv::FeatureRef bogus;
  bogus.layer = 0;
  bogus.tile = 99;
  bogus.feature = 1;
  fv::FeatureDescription d;
  EXPECT_FALSE(src.Describe(bogus, &d).ok());

  bogus.tile = 0;
  bogus.feature = -12345;
  EXPECT_FALSE(src.Describe(bogus, &d).ok());
}

// E1 refuses to hide unapplied updates, and the source must carry that warning
// up: these four cells all ship with update files this phase does not apply.
TEST(EncVectorSource, StalenessWarningNamesTheUnappliedUpdates) {
  SKIP_WITHOUT_ENC();
  OPEN_SOURCE(src);
  const std::string w = src.StalenessWarning();
  EXPECT_NE(w.find("US5CHS"), std::string::npos) << w;
  EXPECT_NE(w.find("BASE EDITION"), std::string::npos) << w;
}
