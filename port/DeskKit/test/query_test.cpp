// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_query.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "fvkit/overlay/feature_overlay.h"

namespace {

using fv::desk::FeatureQuery;
using fv::desk::QueryBuilderModel;
using fv::desk::QueryClause;
using fv::desk::QueryOp;

const std::string kRivers = FV_FEATURE_RIVERS;

fv::FeatureSchema Schema() {
  fv::FeatureSchema s;
  auto add = [&s](const char* name, fv::FieldType type) {
    fv::FieldDef f;
    f.name = name;
    f.type = type;
    s.fields.push_back(f);
  };
  add("name", fv::FieldType::kText);
  add("depth", fv::FieldType::kReal);
  add("navigable", fv::FieldType::kBoolean);
  return s;
}

TEST(FeatureQuery, EveryOperatorCompilesAndReadsBack) {
  const fv::FeatureSchema schema = Schema();
  for (int k = 0; k < fv::desk::kQueryOpCount; ++k) {
    const QueryOp op = static_cast<QueryOp>(k);
    FeatureQuery q;
    q.clauses.push_back({op == QueryOp::kContains ? "name" : "depth", op,
                         fv::desk::QueryOpTakesValue(op) ? (op == QueryOp::kContains ? "Ash" : "7.5") : ""});
    std::string json;
    ASSERT_TRUE(fv::desk::CompileFeatureQuery(q, schema, &json).ok()) << k;
    FeatureQuery back;
    ASSERT_TRUE(fv::desk::ParseFeatureQuery(json, &back).ok()) << json;
    EXPECT_EQ(back, q) << json;
  }
  FeatureQuery two;
  two.match_any = true;
  two.clauses = {{"depth", QueryOp::kGreater, "7"}, {"navigable", QueryOp::kEqual, "yes"}};
  std::string json;
  ASSERT_TRUE(fv::desk::CompileFeatureQuery(two, schema, &json).ok());
  EXPECT_EQ(json, R"(["any",[">",["get","depth"],7.0],["==",["get","navigable"],"true"]])");
  FeatureQuery back;
  ASSERT_TRUE(fv::desk::ParseFeatureQuery(json, &back).ok());
  EXPECT_TRUE(back.match_any);
  EXPECT_EQ(back.clauses[1].value, "true");  // "yes" is stored as the boolean's key
  EXPECT_EQ(fv::desk::FeatureQueryText(two), "depth > 7 or navigable = “yes”");
  ASSERT_TRUE(fv::desk::CompileFeatureQuery(FeatureQuery{}, schema, &json).ok());
  EXPECT_EQ(json, "");
}

TEST(FeatureQuery, IncompleteClausesAreNamed) {
  const fv::FeatureSchema schema = Schema();
  std::string json;
  auto error = [&](QueryClause c) {
    FeatureQuery q;
    q.clauses = {{"name", QueryOp::kIsNotEmpty, ""}, c};
    const fv::Status s = fv::desk::CompileFeatureQuery(q, schema, &json);
    EXPECT_EQ(s.code, fv::kInvalidArg);
    return s.message;
  };
  EXPECT_EQ(error({"gone", QueryOp::kEqual, "x"}).rfind("clause 2: ", 0), 0u);
  EXPECT_NE(error({"depth", QueryOp::kLess, "deep"}).find("not a number"), std::string::npos);
  EXPECT_NE(error({"navigable", QueryOp::kEqual, "maybe"}).find("yes or no"), std::string::npos);
  EXPECT_NE(error({"name", QueryOp::kEqual, " "}).find("needs a value"), std::string::npos);
}

TEST(FeatureQuery, OtherExpressionsAreNotClauses) {
  FeatureQuery q;
  EXPECT_EQ(fv::desk::ParseFeatureQuery(R"(["==", ["geometry-type"], "Point"])", &q).code, fv::kUnsupported);
  EXPECT_EQ(fv::desk::ParseFeatureQuery(R"(["all", ["any", ["has", "a"]]])", &q).code, fv::kUnsupported);
  EXPECT_EQ(fv::desk::ParseFeatureQuery("[nope", &q).code, fv::kInvalidArg);
}

class QueryBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rivers_ = std::make_shared<fv::FeatureOverlay>();
    ASSERT_TRUE(rivers_->FileOpen(kRivers).ok());
    ASSERT_TRUE(QueryBuilderModel::Open(rivers_, "", &q_).ok());
  }

  std::vector<int64_t> Matches(bool within = false) {
    std::vector<int64_t> ids;
    size_t tested = 0;
    const fv::Status s = q_->Matching(within, &ids, &tested);
    EXPECT_TRUE(s.ok()) << s.message;
    return ids;
  }

  std::shared_ptr<fv::FeatureOverlay> rivers_;
  std::shared_ptr<QueryBuilderModel> q_;
};

TEST_F(QueryBuilderTest, ClausesSelectFeatures) {
  EXPECT_EQ(q_->fields().size(), 3u);
  EXPECT_FALSE(q_->dirty());
  EXPECT_EQ(Matches(), (std::vector<int64_t>{1, 2, 3}));  // no clauses: everything

  const size_t i = q_->AddClause();
  EXPECT_EQ(q_->query().clauses[i].field, "name");
  ASSERT_TRUE(q_->SetClause(i, {"depth", QueryOp::kGreaterEqual, "9"}).ok());
  EXPECT_EQ(q_->SetClause(i, {"gone", QueryOp::kEqual, "1"}).code, fv::kInvalidArg);
  EXPECT_TRUE(q_->dirty());
  EXPECT_EQ(Matches(), (std::vector<int64_t>{1, 3}));
  const size_t j = q_->AddClause();
  ASSERT_TRUE(q_->SetClause(j, {"name", QueryOp::kContains, "cOOp"}).ok());
  EXPECT_EQ(Matches(), (std::vector<int64_t>{1}));
  q_->SetMatchAny(true);
  EXPECT_EQ(Matches(), (std::vector<int64_t>{1, 3}));
  EXPECT_EQ(q_->Text(), "depth ≥ 9 or name contains “cOOp”");

  // Within the definition query, only the features the layer shows are tested.
  ASSERT_TRUE(rivers_->SetDefinitionQuery(R"(["<", ["get", "depth"], 10])").ok());
  EXPECT_EQ(Matches(true), (std::vector<int64_t>{3}));

  q_->Revert();
  EXPECT_FALSE(q_->dirty());
  EXPECT_TRUE(q_->query().clauses.empty());
}

TEST_F(QueryBuilderTest, AnExpressionTypedByHandIsKeptAsCustom) {
  ASSERT_TRUE(q_->SetExpression(R"([">", ["get", "flow"], 30])").ok());
  EXPECT_FALSE(q_->custom());  // the builder's own shape reads back as a clause
  ASSERT_EQ(q_->query().clauses.size(), 1u);
  EXPECT_EQ(q_->query().clauses[0].value, "30");

  const std::string custom = R"([">", ["*", ["get", "depth"], 10], ["get", "flow"]])";
  ASSERT_TRUE(q_->SetExpression(custom).ok());
  EXPECT_TRUE(q_->custom());
  EXPECT_EQ(q_->Text(), custom);
  EXPECT_EQ(Matches(), (std::vector<int64_t>{2, 3}));  // 60 > 25 and 90 > 40, not 120 > 130
  EXPECT_FALSE(q_->SetExpression("[\"nope\"]").ok());
  EXPECT_TRUE(q_->custom());

  // Adding a clause leaves the custom text for the builder.
  q_->AddClause();
  EXPECT_FALSE(q_->custom());
  EXPECT_EQ(q_->query().clauses.size(), 1u);

  std::vector<std::string> values;
  ASSERT_TRUE(q_->SampleValues("depth", 10, &values).ok());
  EXPECT_EQ(values, (std::vector<std::string>{"6", "9", "12"}));
  ASSERT_TRUE(q_->SampleValues("name", 2, &values).ok());
  EXPECT_EQ(values, (std::vector<std::string>{"Ashley River", "Cooper River"}));
}

TEST_F(QueryBuilderTest, OpensOnAStoredQuery) {
  std::shared_ptr<QueryBuilderModel> q;
  ASSERT_TRUE(QueryBuilderModel::Open(rivers_, R"(["all", ["has", "name"], ["<", ["get", "flow"], 50]])", &q).ok());
  EXPECT_FALSE(q->dirty());
  ASSERT_EQ(q->query().clauses.size(), 2u);
  EXPECT_EQ(q->query().clauses[1].op, QueryOp::kLess);
  EXPECT_FALSE(QueryBuilderModel::Open(rivers_, "[\"nope\"]", &q).ok());
  EXPECT_EQ(QueryBuilderModel::Open(std::make_shared<fv::FeatureOverlay>(), "", &q).code, fv::kNotFound);
}

}  // namespace
