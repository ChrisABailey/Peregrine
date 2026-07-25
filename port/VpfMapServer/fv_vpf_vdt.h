// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::VpfValueDescriptions — VPF Value Description Tables (identify, plan
// §5.3).
//
// A VPF feature row stores coded values: `f_code` = "BE010", `acc` = 1,
// `exs` = 28. Every coverage ships the dictionary that turns those into text:
//
//   <coverage>/INT.VDT   integer-valued attributes
//   <coverage>/CHAR.VDT  text-valued attributes (including f_code = the FACC)
//
// Both are ordinary VPF tables with the same schema:
//   id, table (e.g. "hydline.lft"), attribute ("acc"), value, description
//
// so this reader is a thin pass over the V1 recordset layer — no new parsing.
// Only the unported VPFDataLib fork read these on Windows, which is why they
// arrive here rather than being extracted from something.
//
// Lookups are (table, attribute, value) with table and attribute matched
// case-insensitively: the VDT spells the table lowercase with its suffix
// ("hydline.lft") while the rest of the reader carries "HYDLINE.LFT".

#ifndef FV_VPF_VDT_H_
#define FV_VPF_VDT_H_

#include <map>
#include <string>

namespace fv {

class VpfValueDescriptions {
 public:
  // Loads INT.VDT and CHAR.VDT from a coverage directory (trailing separator
  // included, as elsewhere in this reader). A missing VDT is not an error —
  // some coverages ship only one, and a coverage with neither simply decodes
  // nothing.
  void Load(const std::string& coverage_dir);

  // Description for a coded value, or an empty string when the dictionary has
  // no entry. `value` is the raw text a VPFVariant rendered ("1", "BE010").
  std::string Lookup(const std::string& table, const std::string& attribute,
                     const std::string& value) const;

  bool empty() const { return map_.empty(); }
  size_t size() const { return map_.size(); }

 private:
  void LoadOne(const std::string& coverage_dir, const char* table_name);

  // key = "<table>|<attribute>|<value>", all lowercased except the value,
  // which is compared verbatim after trimming.
  std::map<std::string, std::string> map_;
};

}  // namespace fv

#endif  // FV_VPF_VDT_H_
