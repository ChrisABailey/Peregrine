// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// S-57 update application tests — hermetic.
//
// Each test writes a tiny base cell and its update files with the NOAA DDR
// formats, opens it through S57Cell, and checks the result. The base cell is a
// triangle of three connected nodes and three edges, an area on all three
// edges, a line on the first edge and a point on an isolated node.

#include "fv_s57.h"
#include "fv_enc_index.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "iso8211_test_writer.h"

namespace fs = std::filesystem;

namespace {

using fv_test::BuildFile;
using fv_test::BuildRecord;
using fv_test::FieldDef;
using fv_test::LE;

using Fields = std::vector<std::pair<std::string, std::string>>;

constexpr char kUt = 0x1f;
constexpr int kInsert = 1, kDelete = 2, kModify = 3;
constexpr int kVI = 110, kVC = 120, kVE = 130;

const std::vector<FieldDef>& Defs() {
  static const std::vector<FieldDef> defs = {
      {"DSID", "Data set identification field",
       "RCNM!RCID!EXPP!INTU!DSNM!EDTN!UPDN!UADT!ISDT!STED!PRSP!PSDN!PRED!PROF!AGEN!COMT",
       "(b11,b14,2b11,3A,2A(8),R(4),b11,2A,b11,b12,A)"},
      {"DSPM", "Data set parameter field",
       "RCNM!RCID!HDAT!VDAT!SDAT!CSCL!DUNI!HUNI!PUNI!COUN!COMF!SOMF!COMT",
       "(b11,b14,3b11,b14,4b11,2b14,A)"},
      {"VRID", "Vector record identifier field", "RCNM!RCID!RVER!RUIN", "(b11,b14,b12,b11)"},
      {"ATTV", "Vector record attribute field", "*ATTL!ATVL", "(b12,A)"},
      {"VRPC", "Vector record pointer control field", "VPUI!VPIX!NVPT", "(b11,2b12)"},
      {"VRPT", "Vector record pointer field", "*NAME!ORNT!USAG!TOPI!MASK", "(B(40),4b11)"},
      {"SGCC", "Coordinate control field", "CCUI!CCIX!CCNC", "(b11,2b12)"},
      {"SG2D", "2-D coordinate field", "*YCOO!XCOO", "(2b24)"},
      {"FRID", "Feature record identifier field", "RCNM!RCID!PRIM!GRUP!OBJL!RVER!RUIN",
       "(b11,b14,2b11,2b12,b11)"},
      {"FOID", "Feature object identifier field", "AGEN!FIDN!FIDS", "(b12,b14,b12)"},
      {"ATTF", "Feature record attribute field", "*ATTL!ATVL", "(b12,A)"},
      {"FFPC", "Feature record to feature object pointer control field", "FFUI!FFIX!NFPT",
       "(b11,2b12)"},
      {"FFPT", "Feature record to feature object pointer field", "*LNAM!RIND!COMT",
       "(B(64),b11,A)"},
      {"FSPC", "Feature record to spatial record pointer control field", "FSUI!FSIX!NSPT",
       "(b11,2b12)"},
      {"FSPT", "Feature record to spatial record pointer field", "*NAME!ORNT!USAG!MASK",
       "(B(40),3b11)"},
  };
  return defs;
}

// --- field bodies -------------------------------------------------------

std::string Dsid(const std::string& edition, const std::string& update) {
  return LE(10, 1) + LE(1, 4) + LE(1, 1) + LE(5, 1) + "US5TEST1.000" + kUt + edition + kUt +
         update + kUt + "20260101" + "20260101" + "03.1" + LE(1, 1) + "2.0" + kUt + "2.0" +
         kUt + LE(1, 1) + LE(550, 2) + "test";
}

std::string Dspm() {
  return LE(20, 1) + LE(1, 4) + LE(2, 1) + LE(17, 1) + LE(23, 1) + LE(12000, 4) +
         LE(1, 1) + LE(1, 1) + LE(1, 1) + LE(1, 1) + LE(10000000, 4) + LE(10, 4);
}

// A position in hundredths of a degree, scaled by COMF 1e7.
struct Pt {
  int lat100, lon100;
};
std::string Coords(const std::vector<Pt>& pts) {
  std::string s;
  for (const Pt& p : pts)
    s += LE(static_cast<uint32_t>(p.lat100 * 100000), 4) +
         LE(static_cast<uint32_t>(p.lon100 * 100000), 4);
  return s;
}

std::string Name(int rcnm, uint32_t rcid) { return LE(rcnm, 1) + LE(rcid, 4); }
std::string Control(int ui, int index, int count) {
  return LE(ui, 1) + LE(index, 2) + LE(count, 2);
}
std::string EdgeEnds(uint32_t begin, uint32_t end) {
  return Name(kVC, begin) + LE(255, 1) + LE(255, 1) + LE(1, 1) + LE(255, 1) + Name(kVC, end) +
         LE(255, 1) + LE(255, 1) + LE(2, 1) + LE(255, 1);
}
std::string Fspt(const std::vector<std::pair<int, uint32_t>>& targets) {
  std::string s;
  for (const auto& [rcnm, rcid] : targets) s += Name(rcnm, rcid) + LE(1, 1) + LE(1, 1) + LE(2, 1);
  return s;
}
std::string Attf(const std::vector<std::pair<int, std::string>>& attrs) {
  std::string s;
  for (size_t i = 0; i < attrs.size(); ++i) {
    if (i > 0) s += kUt;
    s += LE(attrs[i].first, 2) + attrs[i].second;
  }
  return s;
}
std::string Frid(uint32_t rcid, int prim, int objl, int rver, int ruin) {
  return LE(100, 1) + LE(rcid, 4) + LE(prim, 1) + LE(2, 1) + LE(objl, 2) + LE(rver, 2) +
         LE(ruin, 1);
}
std::string Foid(uint32_t fidn) { return LE(550, 2) + LE(fidn, 4) + LE(1, 2); }
std::string Vrid(int rcnm, uint32_t rcid, int rver, int ruin) {
  return LE(rcnm, 1) + LE(rcid, 4) + LE(rver, 2) + LE(ruin, 1);
}

// --- the fixture --------------------------------------------------------

// Feature RCIDs and their FOID FIDNs in the base cell.
constexpr uint32_t kArea = 1, kLight = 2, kLine = 3;

std::vector<Fields> BaseRecords() {
  return {
      {{"VRID", Vrid(kVC, 1, 1, kInsert)}, {"SG2D", Coords({{3200, -8000}})}},
      {{"VRID", Vrid(kVC, 2, 1, kInsert)}, {"SG2D", Coords({{3200, -7990}})}},
      {{"VRID", Vrid(kVC, 3, 1, kInsert)}, {"SG2D", Coords({{3210, -7990}})}},
      {{"VRID", Vrid(kVI, 20, 1, kInsert)}, {"SG2D", Coords({{3205, -7995}})}},
      {{"VRID", Vrid(kVE, 10, 1, kInsert)},
       {"VRPT", EdgeEnds(1, 2)},
       {"SG2D", Coords({{3200, -7995}})}},
      {{"VRID", Vrid(kVE, 11, 1, kInsert)}, {"VRPT", EdgeEnds(2, 3)}},
      {{"VRID", Vrid(kVE, 12, 1, kInsert)}, {"VRPT", EdgeEnds(3, 1)}},
      {{"FRID", Frid(kArea, 3, 42, 1, kInsert)},
       {"FOID", Foid(1001)},
       {"ATTF", Attf({{87, "5"}, {88, "10"}})},
       {"FSPT", Fspt({{kVE, 10}, {kVE, 11}, {kVE, 12}})}},
      {{"FRID", Frid(kLight, 1, 75, 1, kInsert)},
       {"FOID", Foid(1002)},
       {"ATTF", Attf({{116, "Old light"}})},
       {"FSPT", Fspt({{kVI, 20}})}},
      {{"FRID", Frid(kLine, 2, 30, 1, kInsert)},
       {"FOID", Foid(1003)},
       {"FSPT", Fspt({{kVE, 10}})}},
  };
}

// Writes US5TEST1.000 and its updates into a fresh directory per test.
class CellFiles {
 public:
  CellFiles() {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = fs::temp_directory_path() / (std::string("fv_s57_update_") + info->name());
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    Write(".000", Dsid("1", "0"), BaseRecords(), true);
  }
  ~CellFiles() { fs::remove_all(dir_); }

  std::string base() const { return (dir_ / "US5TEST1.000").string(); }
  std::string dir() const { return dir_.string(); }

  void Update(int n, const std::vector<Fields>& records, const std::string& edition = "1") {
    char ext[8];
    snprintf(ext, sizeof(ext), ".%03d", n);
    Write(ext, Dsid(edition, std::to_string(n)), records, false);
  }

 private:
  void Write(const std::string& ext, const std::string& dsid,
             const std::vector<Fields>& records, bool with_dspm) {
    std::vector<std::string> recs;
    Fields head = {{"DSID", dsid}};
    recs.push_back(BuildRecord(false, head));
    if (with_dspm) recs.push_back(BuildRecord(false, {{"DSPM", Dspm()}}));
    for (const Fields& r : records) recs.push_back(BuildRecord(false, r));
    const std::vector<uint8_t> bytes = BuildFile(Defs(), recs);
    std::ofstream out(dir_ / ("US5TEST1" + ext), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }

  fs::path dir_;
};

const fv::S57Feature* ById(const fv::S57Cell& cell, int32_t rcid) {
  for (const fv::S57Feature& f : cell.features())
    if (f.record_id == rcid) return &f;
  return nullptr;
}

std::string AttributeValue(const fv::S57Feature& f, int code) {
  const fv::S57Attribute* a = f.FindAttribute(code);
  return a == nullptr ? std::string("<absent>") : a->value;
}

// --- base and wiring ------------------------------------------------------

TEST(S57Update, BaseCellOpensAsWritten) {
  CellFiles files;
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok()) << cell.Open(files.base()).message;
  EXPECT_EQ(3u, cell.features().size());
  EXPECT_EQ("0", cell.info().update_number);
  const fv::S57Feature* area = ById(cell, kArea);
  ASSERT_NE(nullptr, area);
  ASSERT_EQ(1u, area->geometry.parts.size());
  EXPECT_EQ(5u, area->geometry.parts[0].size()) << "closed triangle with one mid-edge vertex";
  EXPECT_TRUE(area->geometry_complete);
  EXPECT_TRUE(cell.StalenessWarning().empty());
}

TEST(S57Update, BaseOnlyListsEveryUpdateAsUnapplied) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLine, 2, 30, 2, kDelete)}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base(), fv::S57Updates::kBaseOnly).ok());
  EXPECT_EQ(3u, cell.features().size());
  ASSERT_EQ(1u, cell.unapplied_updates().size());
  EXPECT_NE(std::string::npos, cell.StalenessWarning().find("US5TEST1.001"));
  EXPECT_NE(std::string::npos, cell.StalenessWarning().find("out of date"));
}

// --- record instructions --------------------------------------------------

TEST(S57Update, DeletesAndInsertsFeatures) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLine, 2, 30, 2, kDelete)}},
                   {{"FRID", Frid(4, 1, 75, 1, kInsert)},
                    {"FOID", Foid(1004)},
                    {"ATTF", Attf({{116, "New light"}})},
                    {"FSPT", Fspt({{kVI, 20}})}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_EQ("1", cell.info().update_number);
  EXPECT_TRUE(cell.unapplied_updates().empty()) << cell.StalenessWarning();
  EXPECT_EQ(nullptr, ById(cell, kLine));
  ASSERT_EQ(3u, cell.features().size());
  EXPECT_EQ(4, cell.features().back().record_id) << "inserts follow the base records";
  EXPECT_EQ("New light", AttributeValue(cell.features().back(), 116));
  EXPECT_EQ(1u, cell.features().back().geometry.parts.size());
}

TEST(S57Update, UpdatesApplyInSequence) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLight, 1, 75, 2, kModify)},
                    {"ATTF", Attf({{116, "Second"}})}}});
  files.Update(2, {{{"FRID", Frid(kLight, 1, 75, 3, kModify)},
                    {"ATTF", Attf({{116, "Third"}})}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_EQ("2", cell.info().update_number);
  const fv::S57Feature* light = ById(cell, kLight);
  ASSERT_NE(nullptr, light);
  EXPECT_EQ("Third", AttributeValue(*light, 116));
  EXPECT_EQ(3, light->version);
}

// --- attributes -----------------------------------------------------------

TEST(S57Update, AttributesAreReplacedAddedAndDeleted) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kArea, 3, 42, 2, kModify)},
                    {"ATTF", Attf({{87, "2"}, {88, "\x7f"}, {116, "Shoal"}})}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  const fv::S57Feature* area = ById(cell, kArea);
  ASSERT_NE(nullptr, area);
  EXPECT_EQ("2", AttributeValue(*area, 87));
  EXPECT_EQ("<absent>", AttributeValue(*area, 88)) << "0x7F deletes the attribute";
  EXPECT_EQ("Shoal", AttributeValue(*area, 116));
}

// --- coordinates and vector pointers --------------------------------------

TEST(S57Update, MovingASharedNodeMovesEveryFeatureOnIt) {
  CellFiles files;
  files.Update(1, {{{"VRID", Vrid(kVC, 2, 2, kModify)},
                    {"SGCC", Control(kModify, 1, 1)},
                    {"SG2D", Coords({{3200, -7980}})}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  const fv::S57Feature* line = ById(cell, kLine);
  const fv::S57Feature* area = ById(cell, kArea);
  ASSERT_NE(nullptr, line);
  ASSERT_NE(nullptr, area);
  EXPECT_DOUBLE_EQ(-79.80, line->geometry.parts[0].back().lon);
  EXPECT_DOUBLE_EQ(-79.80, area->geometry.parts[0][2].lon);
  EXPECT_TRUE(area->geometry_complete) << "the ring still closes";
}

TEST(S57Update, EdgeVerticesAreInsertedAndDeletedByIndex) {
  CellFiles files;
  // Insert two vertices before the existing one, then delete the original.
  files.Update(1, {{{"VRID", Vrid(kVE, 10, 2, kModify)},
                    {"SGCC", Control(kInsert, 1, 2)},
                    {"SG2D", Coords({{3199, -7998}, {3198, -7996}})}}});
  files.Update(2, {{{"VRID", Vrid(kVE, 10, 3, kModify)}, {"SGCC", Control(kDelete, 3, 1)}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_EQ("2", cell.info().update_number) << cell.StalenessWarning();
  const fv::S57Feature* line = ById(cell, kLine);
  ASSERT_NE(nullptr, line);
  ASSERT_EQ(4u, line->geometry.parts[0].size());
  EXPECT_DOUBLE_EQ(31.99, line->geometry.parts[0][1].lat);
  EXPECT_DOUBLE_EQ(31.98, line->geometry.parts[0][2].lat);
}

TEST(S57Update, EdgeEndNodeIsRepointed) {
  CellFiles files;
  // Edge 10 now runs VC1 -> VC3; VRPT row 2 is the end node.
  files.Update(1, {{{"VRID", Vrid(kVE, 10, 2, kModify)},
                    {"VRPC", Control(kModify, 2, 1)},
                    {"VRPT", Name(kVC, 3) + LE(255, 1) + LE(255, 1) + LE(2, 1) + LE(255, 1)}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  const fv::S57Feature* line = ById(cell, kLine);
  ASSERT_NE(nullptr, line);
  EXPECT_DOUBLE_EQ(32.10, line->geometry.parts[0].back().lat);
}

// --- feature pointers -------------------------------------------------------

TEST(S57Update, FeatureSpatialPointersAreEdited) {
  CellFiles files;
  // The line moves from edge 10 to edges 11 and 12.
  files.Update(1, {{{"FRID", Frid(kLine, 2, 30, 2, kModify)},
                    {"FSPC", Control(kModify, 1, 1)},
                    {"FSPT", Fspt({{kVE, 11}})},
                    {"FSPC", Control(kInsert, 2, 1)},
                    {"FSPT", Fspt({{kVE, 12}})}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok()) << cell.StalenessWarning();
  EXPECT_TRUE(cell.unapplied_updates().empty()) << cell.StalenessWarning();
  const fv::S57Feature* line = ById(cell, kLine);
  ASSERT_NE(nullptr, line);
  ASSERT_EQ(1u, line->geometry.parts.size());
  ASSERT_EQ(3u, line->geometry.parts[0].size()) << "VC2 -> VC3 -> VC1";
  EXPECT_DOUBLE_EQ(-79.90, line->geometry.parts[0][0].lon);
  EXPECT_DOUBLE_EQ(-80.00, line->geometry.parts[0][2].lon);
}

TEST(S57Update, DeletingARingEdgeLeavesTheAreaIncomplete) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kArea, 3, 42, 2, kModify)}, {"FSPC", Control(kDelete, 3, 1)}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  const fv::S57Feature* area = ById(cell, kArea);
  ASSERT_NE(nullptr, area);
  EXPECT_FALSE(area->geometry_complete);
}

TEST(S57Update, FeatureRelationsAreInserted) {
  CellFiles files;
  const std::string lnam = LE(550, 2) + LE(1001, 4) + LE(1, 2);
  files.Update(1, {{{"FRID", Frid(kLight, 1, 75, 2, kModify)},
                    {"FFPC", Control(kInsert, 1, 1)},
                    {"FFPT", lnam + LE(2, 1)}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  const fv::S57Feature* light = ById(cell, kLight);
  ASSERT_NE(nullptr, light);
  ASSERT_EQ(1u, light->relations.size());
  EXPECT_EQ(1001u, light->relations[0].target.feature_id);
  EXPECT_EQ(2, light->relations[0].relation);
}

// --- refusals: the cell stays at the last good update -----------------------

TEST(S57Update, VersionMismatchRollsBackTheWholeFile) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLight, 1, 75, 2, kModify)},
                    {"ATTF", Attf({{116, "Changed"}})}},
                   {{"FRID", Frid(kLine, 2, 30, 5, kDelete)}}});
  files.Update(2, {{{"FRID", Frid(kArea, 3, 42, 2, kDelete)}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_EQ("0", cell.info().update_number);
  EXPECT_EQ("Old light", AttributeValue(*ById(cell, kLight), 116))
      << "the record before the bad one is rolled back too";
  EXPECT_NE(nullptr, ById(cell, kLine));
  EXPECT_NE(nullptr, ById(cell, kArea)) << "updates after a failed one are not applied";
  EXPECT_EQ(2u, cell.unapplied_updates().size());
  EXPECT_NE(std::string::npos, cell.StalenessWarning().find("version mismatch"));
}

TEST(S57Update, LaterFailureKeepsEarlierUpdates) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLine, 2, 30, 2, kDelete)}}});
  files.Update(2, {{{"FRID", Frid(kLine, 2, 30, 3, kDelete)}}});  // already gone
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_EQ("1", cell.info().update_number);
  EXPECT_EQ(nullptr, ById(cell, kLine));
  ASSERT_EQ(1u, cell.unapplied_updates().size());
  EXPECT_NE(std::string::npos, cell.unapplied_updates()[0].find("US5TEST1.002"));
}

TEST(S57Update, WrongEditionIsRefused) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLine, 2, 30, 2, kDelete)}}}, "2");
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_NE(nullptr, ById(cell, kLine));
  EXPECT_NE(std::string::npos, cell.StalenessWarning().find("edition"));
}

TEST(S57Update, InsertingAnExistingRecordIsRefused) {
  CellFiles files;
  files.Update(1, {{{"VRID", Vrid(kVC, 1, 1, kInsert)}, {"SG2D", Coords({{3000, -8000}})}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_EQ(1u, cell.unapplied_updates().size());
  EXPECT_DOUBLE_EQ(32.0, ById(cell, kLine)->geometry.parts[0].front().lat);
}

TEST(S57Update, IndexOutOfRangeIsRefused) {
  CellFiles files;
  files.Update(1, {{{"VRID", Vrid(kVE, 10, 2, kModify)},
                    {"SGCC", Control(kModify, 5, 1)},
                    {"SG2D", Coords({{3000, -8000}})}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_EQ(1u, cell.unapplied_updates().size());
  EXPECT_NE(std::string::npos, cell.StalenessWarning().find("bad modify"));
}

TEST(S57Update, DataFieldWithoutItsControlFieldIsRefused) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLine, 2, 30, 2, kModify)}, {"FSPT", Fspt({{kVE, 11}})}}});
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_EQ(1u, cell.unapplied_updates().size());
}

// --- cancellation -----------------------------------------------------------

TEST(S57Update, EditionZeroCancelsTheCell) {
  CellFiles files;
  files.Update(1, {}, "0");
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(files.base()).ok());
  EXPECT_TRUE(cell.cancelled());
  EXPECT_TRUE(cell.features().empty());
  EXPECT_NE(std::string::npos, cell.StalenessWarning().find("cancelled"));
}

// --- update state without geometry, and the index --------------------------

TEST(S57Update, UpdateStateMatchesOpen) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLine, 2, 30, 2, kDelete)}}});
  files.Update(2, {{{"FRID", Frid(kLine, 2, 30, 3, kDelete)}}});  // already gone
  fv::S57UpdateState state;
  ASSERT_TRUE(fv::ReadS57UpdateState(files.base(), &state).ok());
  EXPECT_EQ(2, state.update_files);
  EXPECT_EQ(1, state.applied);
  EXPECT_EQ("1", state.info.update_number);
  EXPECT_FALSE(state.error.empty());
  EXPECT_FALSE(state.info.cancelled);
}

TEST(S57Update, IndexRecordsAppliedAndPendingUpdates) {
  CellFiles files;
  files.Update(1, {{{"FRID", Frid(kLine, 2, 30, 2, kDelete)}}});
  files.Update(2, {{{"FRID", Frid(kLine, 2, 30, 3, kDelete)}}});
  std::vector<fv::EncIndexEntry> idx;
  ASSERT_TRUE(fv::BuildEncIndex(files.dir(), &idx).ok());
  ASSERT_EQ(1u, idx.size());
  EXPECT_EQ(2, idx[0].update_count);
  EXPECT_EQ(1, idx[0].applied_updates);
  EXPECT_EQ(1, idx[0].pending_updates());
  EXPECT_FALSE(idx[0].cancelled);
}

TEST(S57Update, IndexRecordsCancellation) {
  CellFiles files;
  files.Update(1, {}, "0");
  std::vector<fv::EncIndexEntry> idx;
  ASSERT_TRUE(fv::BuildEncIndex(files.dir(), &idx).ok());
  ASSERT_EQ(1u, idx.size());
  EXPECT_TRUE(idx[0].cancelled);
  EXPECT_EQ(1, idx[0].applied_updates);
  EXPECT_EQ(0, idx[0].pending_updates());

  const std::string path = files.dir() + "/" + fv::kEncIndexFileName;
  ASSERT_TRUE(fv::WriteEncIndex(path, idx).ok());
  std::vector<fv::EncIndexEntry> back;
  ASSERT_TRUE(fv::ReadEncIndex(path, &back).ok());
  ASSERT_EQ(1u, back.size());
  EXPECT_TRUE(back[0].cancelled);
  EXPECT_EQ(1, back[0].applied_updates);
}

}  // namespace
