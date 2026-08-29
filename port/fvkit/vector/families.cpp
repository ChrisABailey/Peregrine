// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/vector/families.h"

#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fv {
namespace {

using json = nlohmann::json;

// A selector is validated by parsing the rule line it will become. That is the
// whole point of building families on the R2 syntax: there is one parser, so
// there is one definition of what a selector may say, and a typo is a load
// error here instead of a family that silently hides nothing later.
bool SelectorParses(const std::string& sel, std::string* why) {
  RuleSet scratch;
  std::string err;
  if (scratch.LoadText("hide " + sel, &err).ok()) return true;
  // The scratch set is one line, so the parser's "line 1: " prefix is noise.
  const std::string kPrefix = "line 1: ";
  if (err.compare(0, kPrefix.size(), kPrefix) == 0) err = err.substr(kPrefix.size());
  *why = err.empty() ? "not a valid rule selector" : err;
  return false;
}

std::string GetString(const json& o, const char* key) {
  auto it = o.find(key);
  if (it == o.end() || !it->is_string()) return std::string();
  return it->get<std::string>();
}

}  // namespace

Status FamilySet::LoadJson(const std::string& text, std::string* error) {
  auto fail = [&](const std::string& msg) {
    if (error != nullptr) *error = msg;
    return Status::Error(kInvalidArg, msg);
  };

  json doc = json::parse(text, nullptr, /*allow_exceptions=*/false,
                         /*ignore_comments=*/true);
  if (doc.is_discarded()) return fail("not valid JSON");
  if (!doc.is_object()) return fail("top level must be an object");

  auto fams = doc.find("families");
  if (fams == doc.end() || !fams->is_array())
    return fail("no \"families\" array");

  std::vector<FeatureFamily> parsed;
  std::set<std::string> seen;
  for (const json& jf : *fams) {
    if (!jf.is_object()) return fail("every family must be an object");

    FeatureFamily f;
    f.name = GetString(jf, "name");
    if (f.name.empty()) return fail("a family has no \"name\"");
    if (!seen.insert(f.name).second)
      return fail("family \"" + f.name + "\" is declared twice");

    f.title = GetString(jf, "title");
    f.note = GetString(jf, "note");

    auto en = jf.find("enabled");
    if (en != jf.end()) {
      if (!en->is_boolean())
        return fail("family \"" + f.name + "\": \"enabled\" must be true or false");
      f.enabled = en->get<bool>();
    }

    auto sel = jf.find("select");
    if (sel == jf.end() || !sel->is_array() || sel->empty())
      return fail("family \"" + f.name + "\" has no \"select\" list");
    for (const json& js : *sel) {
      if (!js.is_string())
        return fail("family \"" + f.name + "\": every selector must be a string");
      const std::string s = js.get<std::string>();
      std::string why;
      if (!SelectorParses(s, &why))
        return fail("family \"" + f.name + "\", selector \"" + s + "\": " + why);
      f.select.push_back(s);
    }
    parsed.push_back(std::move(f));
  }

  // All-or-nothing: only commit once the whole file parsed.
  product_ = GetString(doc, "product");
  description_ = GetString(doc, "description");
  families_ = std::move(parsed);
  ++epoch_;
  if (error != nullptr) error->clear();
  return Status::Ok();
}

Status FamilySet::LoadFile(const std::string& path, std::string* error) {
  std::ifstream f(path);
  if (!f) {
    const std::string msg = "cannot open family file " + path;
    if (error != nullptr) *error = msg;
    return Status::Error(kNotFound, msg);
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  const Status st = LoadJson(ss.str(), error);
  if (st.ok()) {
    path_ = path;
  } else if (error != nullptr) {
    *error = path + ": " + *error;
  }
  return st;
}

const FeatureFamily* FamilySet::Find(const std::string& name) const {
  for (const FeatureFamily& f : families_)
    if (f.name == name) return &f;
  return nullptr;
}

bool FamilySet::Enabled(const std::string& name) const {
  const FeatureFamily* f = Find(name);
  return f == nullptr ? true : f->enabled;
}

bool FamilySet::SetEnabled(const std::string& name, bool on) {
  for (FeatureFamily& f : families_) {
    if (f.name != name) continue;
    if (f.enabled != on) {
      f.enabled = on;
      ++epoch_;
    }
    return true;
  }
  return false;
}

size_t FamilySet::disabled_count() const {
  size_t n = 0;
  for (const FeatureFamily& f : families_)
    if (!f.enabled) ++n;
  return n;
}

Status FamilySet::AppendRules(RuleSet* out, std::string* error) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "null RuleSet");
  std::ostringstream text;
  for (const FeatureFamily& f : families_) {
    if (f.enabled) continue;
    for (const std::string& s : f.select) text << "hide " << s << "\n";
  }
  const std::string lines = text.str();
  if (lines.empty()) {
    if (error != nullptr) error->clear();
    return Status::Ok();
  }
  // Selectors were validated at load, so a failure here is a bug in this file
  // rather than in the user's — but it is still reported rather than asserted.
  return out->LoadText(lines, error);
}

}  // namespace fv
