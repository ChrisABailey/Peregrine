// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_datum_convert_geotrans.cpp — fv::IDatumConvert backed by the ported
// CGeoTrans (fv_geo3 / MSP GEOTRANS), replacing the DatumConvertServer COM
// object that CGeoTiffFrameFile CoCreates on Windows. DatumConvertServer's
// ConvertDatum is itself a thin wrapper over CGeoTrans::DLL_convert_datum,
// so results match.

#include "fv_interfaces.h"

#include "fv_compat.h"  // Win32 typedefs used by geotrans.h
#include "geotrans.h"

namespace fv {

namespace {

class GeotransDatumConvert : public IDatumConvert {
 public:
  long ConvertDatum(double lat_in, double lon_in, double* lat_out,
                    double* lon_out, const char* datum_in,
                    const char* datum_out) override {
    return m_geotrans.DLL_convert_datum(lat_in, lon_in, *lat_out, *lon_out,
                                        datum_in, datum_out);
  }

 private:
  CGeoTrans m_geotrans;
};

IDatumConvert* g_datum_convert = nullptr;

}  // namespace

IDatumConvert* GetDatumConverter() { return g_datum_convert; }
void SetDatumConverter(IDatumConvert* dc) { g_datum_convert = dc; }

IDatumConvert* EnsureDefaultDatumConverter() {
  if (g_datum_convert == nullptr) {
    static GeotransDatumConvert s_default;
    g_datum_convert = &s_default;
  }
  return g_datum_convert;
}

}  // namespace fv
