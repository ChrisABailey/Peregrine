// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// VPF identify tests (plan §5.3, session R1): the VDT value dictionaries and
// VpfVectorSource::Describe().
//
// Real data: TestData/vpf/dnc17 harbor library h1707300 (Cape Cod / Nantucket
// Sound), the same library the V5a/V5c geometry tests pin. The point of these
// assertions is that a coded DNC row comes back as text a human can read —
// "BE010" -> "Depth Curve", acc 1 -> "Accurate" — which is what makes a tap
// popup worth building.

#include "fv_vpf_vdt.h"
#include "fv_vpf_vector_source.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string HarborLibrary() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  std::string p = std::string(d) + "/vpf/dnc17/h1707300";
  if (!fs::is_directory(p)) return {};
  return p;
}

#define SKIP_WITHOUT_DNC()                 \
  const std::string lib = HarborLibrary(); \
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data"

// First feature of a layer, whatever it is — Describe() has to work off a ref
// Query() handed out, so the tests go through the same path a click does.
bool FirstOfLayer(fv::VpfVectorSource* s, const std::string& layer,
                  fv::VectorFeature* out) {
  std::vector<fv::VectorFeature> all;
  fv::VectorQuery q;
  s->Query(q, &all);
  for (const auto& f : all) {
    if (f.layer == layer) { *out = f; return true; }
  }
  return false;
}

const fv::FeatureAttribute* Attr(const fv::FeatureDescription& d,
                                 const std::string& code) {
  for (const auto& a : d.attributes)
    if (a.code == code) return &a;
  return nullptr;
}

// --- the dictionaries ------------------------------------------------------

TEST(VpfValueDescriptions, DecodesCharAndIntCodesFromACoverage) {
  SKIP_WITHOUT_DNC();
  fv::VpfValueDescriptions vdt;
  vdt.Load(lib + "/hyd/");
  ASSERT_FALSE(vdt.empty());

  // CHAR.VDT: the FACC code, the single most useful thing about a DNC feature.
  EXPECT_EQ(vdt.Lookup("HYDLINE.LFT", "f_code", "BE010"), "Depth Curve");
  // INT.VDT: a coded enumeration.
  EXPECT_EQ(vdt.Lookup("HYDLINE.LFT", "acc", "1"), "Accurate");

  // Table and attribute match case-insensitively: the VDT spells the table
  // "hydline.lft", the rest of the reader carries "HYDLINE.LFT".
  EXPECT_EQ(vdt.Lookup("hydline.lft", "F_CODE", "BE010"), "Depth Curve");

  // Misses are empty, not invented — the caller falls back to the raw value.
  EXPECT_EQ(vdt.Lookup("HYDLINE.LFT", "f_code", "ZZ999"), "");
  EXPECT_EQ(vdt.Lookup("NOSUCH.LFT", "f_code", "BE010"), "");
  EXPECT_EQ(vdt.Lookup("HYDLINE.LFT", "f_code", ""), "");
}

TEST(VpfValueDescriptions, MissingVdtIsNotAnError) {
  SKIP_WITHOUT_DNC();
  fv::VpfValueDescriptions vdt;
  vdt.Load(lib + "/tileref/");  // no INT.VDT / CHAR.VDT there
  EXPECT_TRUE(vdt.empty());
  EXPECT_EQ(vdt.Lookup("TILEREF.AFT", "f_code", "BE010"), "");
}

// --- Describe() ------------------------------------------------------------

TEST(VpfDescribeReal, DecodesALineFeatureIntoReadableText) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  fv::VectorFeature f;
  ASSERT_TRUE(FirstOfLayer(&s, "hydline", &f));
  ASSERT_EQ(f.style_key, "BE010");

  fv::FeatureDescription d;
  ASSERT_TRUE(s.Describe(f.ref, &d).ok());

  EXPECT_EQ(d.ref, f.ref);
  EXPECT_EQ(d.title, "Depth Curve");                    // CHAR.VDT on f_code
  EXPECT_EQ(d.class_name, "Hydrography Line Features")  // FCA descr
      << "class description comes from the coverage FCA";
  EXPECT_EQ(d.layer_name, "hydline");
  EXPECT_NE(d.source_note.find("h1707300"), std::string::npos);
  EXPECT_NE(d.source_note.find("hyd"), std::string::npos);

  // f_code is shown (it IS the feature's identity), decoded.
  const fv::FeatureAttribute* fc = Attr(d, "f_code");
  ASSERT_NE(fc, nullptr);
  EXPECT_EQ(fc->name, "FACC Code");  // straight out of the table header
  EXPECT_EQ(fc->raw, "BE010");
  EXPECT_EQ(fc->display, "Depth Curve");

  // A coded integer attribute decodes through INT.VDT.
  const fv::FeatureAttribute* acc = Attr(d, "acc");
  ASSERT_NE(acc, nullptr);
  EXPECT_EQ(acc->name, "Accuracy Category");
  EXPECT_FALSE(acc->display.empty());

  // Join keys are plumbing and are NOT shown.
  EXPECT_EQ(Attr(d, "id"), nullptr);
  EXPECT_EQ(Attr(d, "tile_id"), nullptr);
  EXPECT_EQ(Attr(d, "edg_id"), nullptr);
}

TEST(VpfDescribeReal, UndecodableValuesFallBackToTheRawText) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  fv::VectorFeature f;
  ASSERT_TRUE(FirstOfLayer(&s, "hydline", &f));
  fv::FeatureDescription d;
  ASSERT_TRUE(s.Describe(f.ref, &d).ok());

  // crv (depth curve value) is a plain float with no dictionary entry: its
  // display is the number itself, never empty just because the decode missed.
  const fv::FeatureAttribute* crv = Attr(d, "crv");
  ASSERT_NE(crv, nullptr);
  EXPECT_EQ(crv->display, crv->raw);
  EXPECT_FALSE(crv->raw.empty());
}

TEST(VpfDescribeReal, WorksForPointAndAreaFeaturesToo) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  for (const char* layer : {"soundp", "hydarea"}) {
    fv::VectorFeature f;
    ASSERT_TRUE(FirstOfLayer(&s, layer, &f)) << layer;
    fv::FeatureDescription d;
    ASSERT_TRUE(s.Describe(f.ref, &d).ok()) << layer;
    EXPECT_EQ(d.layer_name, layer);
    EXPECT_FALSE(d.title.empty()) << layer << ": a popup always needs a heading";
    EXPECT_FALSE(d.attributes.empty()) << layer;
  }
}

TEST(VpfDescribeReal, EveryQueriedFeatureCanBeDescribed) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  std::vector<fv::VectorFeature> all;
  fv::VectorQuery q;
  ASSERT_TRUE(s.Query(q, &all).ok());
  ASSERT_GT(all.size(), 5000u) << "the harbor library's full feature set";

  // The render path only carries a FeatureRef; if any of them cannot be
  // resolved back to a row, identify is broken for that layer.
  size_t titled = 0;
  for (const auto& f : all) {
    fv::FeatureDescription d;
    ASSERT_TRUE(s.Describe(f.ref, &d).ok())
        << f.layer << " id " << f.ref.feature;
    ASSERT_EQ(d.ref.feature, f.ref.feature);
    // The row Describe found must be the row Query read.
    const fv::FeatureAttribute* fc = Attr(d, "f_code");
    ASSERT_NE(fc, nullptr) << f.layer;
    EXPECT_EQ(fc->raw, f.style_key) << f.layer << " id " << f.ref.feature;
    if (!d.title.empty()) ++titled;
  }
  EXPECT_EQ(titled, all.size());
}

TEST(VpfDescribeReal, RejectsRefsItDidNotIssue) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  fv::FeatureDescription d;
  fv::FeatureRef bad;                       // layer = -1
  EXPECT_EQ(s.Describe(bad, &d).code, fv::kInvalidArg);
  bad.layer = 9999;
  EXPECT_EQ(s.Describe(bad, &d).code, fv::kInvalidArg);
  bad.layer = 0;
  bad.feature = 1 << 24;                    // no such row
  EXPECT_EQ(s.Describe(bad, &d).code, fv::kNotFound);

  fv::VectorFeature f;
  ASSERT_TRUE(FirstOfLayer(&s, "hydline", &f));
  EXPECT_EQ(s.Describe(f.ref, nullptr).code, fv::kInvalidArg);
}

TEST(VpfDescribe, UnopenedSourceSaysSo) {
  fv::VpfVectorSource s;
  fv::FeatureDescription d;
  fv::FeatureRef r;
  r.layer = 0;
  EXPECT_EQ(s.Describe(r, &d).code, fv::kNotFound);
}

TEST(VpfDescribeReal, NullIntegersDisplayAsNothingButKeepTheirRawValue) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  // por/PIERL.LFT carries a null Vertical Reference Category on its first
  // rows: VPF's null short int is the sentinel -32768, which is meaningless
  // in a popup. The display blanks; the raw value is still there.
  fv::VectorFeature f;
  ASSERT_TRUE(FirstOfLayer(&s, "pierl", &f));
  fv::FeatureDescription d;
  ASSERT_TRUE(s.Describe(f.ref, &d).ok());

  const fv::FeatureAttribute* vrc = Attr(d, "vrr");
  ASSERT_NE(vrc, nullptr);
  ASSERT_EQ(vrc->raw, "-32768") << "test data changed; pick another null field";
  EXPECT_EQ(vrc->display, "");

  // Nothing anywhere in the library shows a bare sentinel to the user.
  std::vector<fv::VectorFeature> all;
  fv::VectorQuery q;
  ASSERT_TRUE(s.Query(q, &all).ok());
  for (size_t i = 0; i < all.size(); i += 37) {  // sampled: this is ~5000 rows
    fv::FeatureDescription dd;
    ASSERT_TRUE(s.Describe(all[i].ref, &dd).ok());
    for (const auto& a : dd.attributes) {
      EXPECT_NE(a.display, "-32768") << dd.layer_name << "." << a.code;
      EXPECT_NE(a.display, "-2147483648") << dd.layer_name << "." << a.code;
    }
  }
}

}  // namespace
