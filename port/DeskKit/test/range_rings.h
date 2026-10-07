// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// range_rings.h — a test-only file overlay that the app knows only through a
/// manifest: properties in two groups (one of each kind a page shows) and a
/// document that records what it was asked to open.
#pragma once

#include <string>
#include <vector>

#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
#include "fvkit/overlay/overlay.h"

namespace fv {
namespace desk {
namespace test {

class RangeRings : public Overlay, public app::Properties, public app::Persistence {
 public:
  RangeRings() : Overlay("Range Rings") {
    for (const app::PropertySpec& s : Describe()) values_.push_back(s.default_value);
  }

  app::Properties* AsProperties() override { return this; }
  app::Persistence* AsPersistence() override { return this; }

  const std::vector<app::PropertySpec>& Describe() const override {
    static const std::vector<app::PropertySpec> specs = [] {
      std::vector<app::PropertySpec> v;
      app::PropertySpec count;
      count.key = "ring_count";
      count.label = "Rings";
      count.group = "Rings";
      count.type = app::PropertyType::kInt;
      count.default_value = app::PropertyValue::Int(3);
      count.min = 1;
      count.max = 10;
      v.push_back(count);
      app::PropertySpec units;
      units.key = "units";
      units.label = "Units";
      units.group = "Rings";
      units.type = app::PropertyType::kChoice;
      units.choices = {"nm", "km", "mi"};
      units.default_value = app::PropertyValue::Choice(0);
      v.push_back(units);
      app::PropertySpec color;
      color.key = "line_color";
      color.label = "Line colour";
      color.group = "Style";
      color.type = app::PropertyType::kColor;
      color.default_value = app::PropertyValue::Color(FvColor{255, 0, 0, 255});
      v.push_back(color);
      app::PropertySpec dir;
      dir.key = "export_dir";
      dir.label = "Export folder";
      dir.group = "Rings";
      dir.type = app::PropertyType::kPath;
      dir.path_kind = app::PathKind::kDirectory;
      dir.default_value = app::PropertyValue::Path("");
      v.push_back(dir);
      return v;
    }();
    return specs;
  }

  Status GetProperty(const std::string& key, app::PropertyValue* out) const override {
    const int i = Index(key);
    if (i < 0) return Status::Error(kNotFound, "no property " + key);
    *out = values_[i];
    return Status::Ok();
  }

  /// Clamps the ring count rather than refusing it; refuses a wrong type.
  Status SetProperty(const std::string& key, const app::PropertyValue& v) override {
    const int i = Index(key);
    if (i < 0) return Status::Error(kNotFound, "no property " + key);
    const app::PropertySpec& s = Describe()[i];
    if (v.type != s.type) return Status::Error(kInvalidArg, "wrong type for " + key);
    app::PropertyValue next = v;
    if (key == "ring_count") next.i = next.i < 1 ? 1 : next.i > 10 ? 10 : next.i;
    if (key == "units" && (next.i < 0 || next.i >= (long long)s.choices.size()))
      return Status::Error(kInvalidArg, "no such unit");
    values_[i] = next;
    return Status::Ok();
  }

  Status FileNew() override { return Status::Ok(); }
  Status FileOpen(const std::string& spec) override {
    opened.push_back(spec);
    return Status::Ok();
  }
  Status FileSaveAs(const std::string&, int) override { return Status::Ok(); }

  std::vector<std::string> opened;

 private:
  int Index(const std::string& key) const {
    const std::vector<app::PropertySpec>& s = Describe();
    for (size_t i = 0; i < s.size(); ++i)
      if (s[i].key == key) return static_cast<int>(i);
    return -1;
  }
  std::vector<app::PropertyValue> values_;
};

}  // namespace test
}  // namespace desk
}  // namespace fv
