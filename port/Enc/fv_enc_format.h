// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::EncFrameEnumerator — S-57 ENC as catalog rows (ENC phase E4), the
// analog of VpfFrameEnumerator for DNC. One row per CELL; `path` is the cell
// file itself, which EncVectorSource::Open already takes, so ENC needs no
// locator of VPF's "<db>|<library>|<tile>" kind.
//
// WHY THIS LIVES IN fv_enc AND NOT IN fvkit. Every other adapter's enumerator
// sits in port/fvkit/formats/ and is wired up by RegisterBuiltinFormats().
// ENC cannot: `fv_enc` links `fv_fvkit` (it needs the VectorSymbol seam and
// the rule layer), so an fvkit that referenced EncFrameEnumerator would close
// a dependency cycle. The registry was built multi-slot and public for
// exactly this — RegisterFormat() is callable from any layer — so ENC
// registers itself through RegisterEncFormat() and the consumer (pyfvw,
// PythonView, a future CLI) calls it next to RegisterBuiltinFormats().
//
// SERIES = the usage band. "US5CHSDC" is band 5, harbour; band is S-57's
// scale ladder and plays the role DNC's library plays, which is the same
// mapping EncVectorSource's header already documents. Cells of one band form
// one series, so BestSeriesForScale picks overview vs harbour the way it
// picks between CADRG series.
//
// SCALE is the band's NOMINAL scale, not the cell's own CSCL. The cell
// carries an authoritative compilation scale (S57DatasetInfo::compilation_
// scale) but reading it means fully parsing the cell, and enumeration has to
// stay a directory-and-header scan — a real NOAA exchange set is ~1000 cells.
// The band's conventional scale is the standard answer to "which cells belong
// at this display scale" and is what the catalog needs; a caller that wants
// the exact figure has it after Open().

#ifndef FV_ENC_FORMAT_H_
#define FV_ENC_FORMAT_H_

#include <string>
#include <vector>

#include "fvkit/formats/enumerate.h"

namespace fv {

class EncFrameEnumerator : public IFrameEnumerator {
 public:
  EncFrameEnumerator() = default;

  // `dir` is an exchange-set root, or any directory with base cells (*.000)
  // below it. Cells are found by extension, never by the ENC_ROOT layout —
  // the same rule EnumerateEncCells follows, and for the same reason (the
  // delivered NOAA sets have their cell folders lifted out of ENC_ROOT).
  Status Begin(const std::string& dir) override;
  bool Next(FrameInfo* info) override;

 private:
  std::vector<FrameInfo> frames_;  // sorted by path
  size_t next_ = 0;
};

// IHO usage band -> the series name shown in a menu ("Harbour"), and the
// nominal scale denominator that band is compiled at. Band 0 / unknown gets
// an empty name and scale 0.
std::string EncBandName(int usage_band);
double EncBandNominalScale(int usage_band);

// Registers "enc" (enumerator only — ENC is vector data, so there is no
// raster or elevation surface, exactly like "vpf"). Idempotent; call it
// alongside RegisterBuiltinFormats().
void RegisterEncFormat();

}  // namespace fv

#endif  // FV_ENC_FORMAT_H_
