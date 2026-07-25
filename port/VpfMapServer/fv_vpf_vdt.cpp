// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_vpf_vdt.h"

#include "fv_vpf_detail.h"

#include "vpfrcset.h"

namespace fv {

using vpf_detail::Lower;
using vpf_detail::Trim;
using vpf_detail::VariantText;

namespace {

std::string Key(const std::string& table, const std::string& attribute,
                const std::string& value) {
  return Lower(Trim(table)) + "|" + Lower(Trim(attribute)) + "|" + Trim(value);
}

}  // namespace

void VpfValueDescriptions::LoadOne(const std::string& coverage_dir,
                                   const char* table_name) {
  VPFRecordset rs(CString(coverage_dir.c_str()));
  if (rs.open(CString(table_name)) != SUCCESS) return;  // absent VDT is normal

  // Probe the schema once (get_field_info is quiet; get_field_value logs a
  // line per miss, and these tables run to thousands of rows).
  for (const char* col : {"table", "attribute", "value", "description"})
    if (rs.get_field_info(CString(col)) == nullptr) return;

  rs.move_first();
  while (!rs.is_eof()) {
    VPFVariant* t = rs.get_field_value(CString("table"));
    VPFVariant* a = rs.get_field_value(CString("attribute"));
    VPFVariant* v = rs.get_field_value(CString("value"));
    VPFVariant* d = rs.get_field_value(CString("description"));
    if (t != nullptr && a != nullptr && v != nullptr && d != nullptr) {
      const std::string desc = Trim(VariantText(*d));
      if (!desc.empty()) {
        // First entry wins: a coverage's VDT is authored per table, so a
        // duplicate key means duplicated rows, not a refinement.
        map_.emplace(Key(VariantText(*t), VariantText(*a), VariantText(*v)),
                     desc);
      }
    }
    rs.move_next();
  }
}

void VpfValueDescriptions::Load(const std::string& coverage_dir) {
  LoadOne(coverage_dir, "INT.VDT");
  LoadOne(coverage_dir, "CHAR.VDT");
}

std::string VpfValueDescriptions::Lookup(const std::string& table,
                                         const std::string& attribute,
                                         const std::string& value) const {
  if (value.empty()) return std::string();
  auto it = map_.find(Key(table, attribute, value));
  return it == map_.end() ? std::string() : it->second;
}

}  // namespace fv
