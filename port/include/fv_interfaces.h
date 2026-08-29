// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_interfaces.h — plain C++ interfaces that replace CoCreateInstance'd
// sibling COM servers in portable code (strategy: plan Phase 3, playbook
// SEVER). On Windows the COM wrappers adapt these to the real servers; on
// POSIX implementations are registered at startup (or by tests).

#pragma once

namespace fv {

// Replaces IDatumConvertPtr (DatumConvertServer). Datum codes are GEOTRANS
// 5-char codes ("NAS-C", "NAR-C", "W72", "W84", ...). Returns 0 on success.
struct IDatumConvert {
  virtual ~IDatumConvert() {}
  virtual long ConvertDatum(double lat_in, double lon_in, double* lat_out,
                            double* lon_out, const char* datum_in,
                            const char* datum_out) = 0;
};

// Process-wide registry (single slot per interface; FalconView's COM
// services were effectively singletons too).
IDatumConvert* GetDatumConverter();
void SetDatumConverter(IDatumConvert* dc);  // does not take ownership

// Returns the default GEOTRANS-backed converter (fv_geo3), registering it
// if nothing is registered yet.
IDatumConvert* EnsureDefaultDatumConverter();

}  // namespace fv
