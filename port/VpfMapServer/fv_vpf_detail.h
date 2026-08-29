// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Internal helpers shared by the VPF port's own translation units
// (fv_vpf_vector_source.cpp, fv_vpf_vdt.cpp). Not part of the FvKit seam —
// everything here is about crossing the reader's MFC/CString boundary into
// std::string, which the seam types are written in.

#ifndef FV_VPF_DETAIL_H_
#define FV_VPF_DETAIL_H_

#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

#include "stdafx.h"

#include "variant.h"

namespace fv {
namespace vpf_detail {

inline std::string ToStd(const CString& cs) {
  return std::string(static_cast<const char*>(cs), cs.GetLength());
}

inline std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

inline std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return s;
}

inline std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

// VPF integer fields arrive as short or long depending on the table's
// declared width; callers only ever want the number.
inline int VariantInt(const VPFVariant& v) {
  switch (v.m_type) {
    case VPF_INT_SHORT: return v.m_int_short;
    case VPF_INT_LONG: return v.m_int_long;
    default: return v.m_int_long;
  }
}

// Renders a field value the way GeoSym's rule engine expects to see it: bare
// text for text fields, a plain integer/float otherwise.
//
// A NULL numeric field must come out as the EMPTY STRING, not "nan". VPF
// encodes a null float as a NaN, and GeoSym's ATTEXP tables test for it with
// `att = NULL` — CAEValue turns the literal NULL into an empty string, so an
// empty value compares equal and a "nan" one does not. (This is also why the
// caller keeps empty-valued attributes in the list instead of dropping them:
// CAEEntry::Evaluate returns false outright for an attribute that is ABSENT,
// so "present and null" and "not there" are different answers.) Without this,
// every DNC sounding label rule silently evaluated false.
inline std::string VariantText(const VPFVariant& v) {
  char buf[64];
  switch (v.m_type) {
    case VPF_TEXT:
    case VPF_LATIN1_TEXT:
    case VPF_FULL_LATIN_TEXT:
    case VPF_MULTI_LINGUAL_TEXT:
      return Trim(ToStd(v.m_text));
    case VPF_INT_SHORT:
      snprintf(buf, sizeof(buf), "%d", static_cast<int>(v.m_int_short));
      return buf;
    case VPF_INT_LONG:
      snprintf(buf, sizeof(buf), "%d", static_cast<int>(v.m_int_long));
      return buf;
    case VPF_FLOAT_SHORT: {
      const double d = static_cast<double>(v.m_float_short);
      if (std::isnan(d)) return std::string();  // VPF null
      snprintf(buf, sizeof(buf), "%g", d);
      return buf;
    }
    case VPF_FLOAT_LONG: {
      if (std::isnan(v.m_float_long)) return std::string();  // VPF null
      snprintf(buf, sizeof(buf), "%g", v.m_float_long);
      return buf;
    }
    default:
      return std::string();
  }
}

}  // namespace vpf_detail
}  // namespace fv

#endif  // FV_VPF_DETAIL_H_
