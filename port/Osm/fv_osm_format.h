// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::OsmFrameEnumerator — an OSM vector-tile pyramid as catalog rows (OSM
// phase O3), the analog of EncFrameEnumerator for ENC and VpfFrameEnumerator
// for DNC. It lives here rather than in port/fvkit/formats/ for exactly the
// reason fv_enc_format.h gives: fv_osm links fv_fvkit, so an fvkit that named
// OsmFrameEnumerator would close a dependency cycle. RegisterOsmFormat() is
// called by the consumer next to RegisterBuiltinFormats().
//
// ONE FILE IS ONE FRAME, AND ONE SERIES. Every other enumerator in the port
// walks a tree of many files (CADRG frames, DTED cells, ENC cells) and groups
// them into series; an .mbtiles file is a single self-contained pyramid, so
// the row count is the file count. `path` is the file, which
// OsmVectorSource::Open already takes.
//
// SERIES_KEY IS THE FILE STEM ("us-south"), not the tileset's `name` metadata.
// The key is a user-visible handle — it is what `--series osm/us-south` names
// and what the catalog files rows under — so it has to be stable and free of
// spaces, and a Tilemaker `name` is neither ("OpenMapTiles US South").
// The pretty name is still available: it is on MbtilesFile::name() once the
// source is open, which is where a title bar should read it.
//
// SCALE IS 0 — DELIBERATELY. A pyramid is a scale RANGE (z4..z14 here), not a
// compilation scale, and FrameInfo carries one number. Two other things follow
// from filing it as scale-less, and both are the behaviour we want:
//
//   * PythonView opens a scale-less vector series by FITTING its bounds,
//     exactly as it does a DNC library, instead of jumping to a nominal scale.
//   * The PageUp/PageDown scale ladder ignores series that declare no scale,
//     so a pyramid is never a rung on it. That is right: stepping "one scale"
//     inside a pyramid is what the zoom keys already do, continuously, and the
//     source picks the tile level itself (webmerc::ZoomForScale).
//
// This is the same call ENC's F3 row made in the other direction, and for the
// same reason: a band IS a scale, a pyramid is not.

#ifndef FV_OSM_FORMAT_H_
#define FV_OSM_FORMAT_H_

#include <string>
#include <vector>

#include "fvkit/formats/enumerate.h"

namespace fv {

class OsmFrameEnumerator : public IFrameEnumerator {
 public:
  OsmFrameEnumerator() = default;

  // `dir` is any directory with .mbtiles files below it; `dir` may also BE an
  // .mbtiles file, so that File > Add Map Data can point straight at one.
  //
  // A raster .mbtiles found in the tree is SKIPPED, not rejected: a directory
  // holding both a raster and a vector pyramid is a perfectly ordinary thing,
  // and refusing the whole scan over a file this format does not claim would
  // lose the ones it does. (Raster MBTiles are an IRasterSource's job; nothing
  // reads them yet.)
  Status Begin(const std::string& dir) override;
  bool Next(FrameInfo* info) override;

  // Vector pyramids skipped as raster, and files that would not open at all.
  // Diagnostics for a scan log, not errors.
  size_t skipped_raster() const { return skipped_raster_; }
  size_t skipped_unreadable() const { return skipped_unreadable_; }

 private:
  std::vector<FrameInfo> frames_;  // sorted by path
  size_t next_ = 0;
  size_t skipped_raster_ = 0;
  size_t skipped_unreadable_ = 0;
};

// Registers "osm" (enumerator only — OSM is vector data drawn through
// OsmVectorSource + OsmStyleEngine + VectorRenderer, so there is no raster or
// elevation surface, exactly like "vpf" and "enc"). Idempotent; call it
// alongside RegisterBuiltinFormats().
void RegisterOsmFormat();

}  // namespace fv

#endif  // FV_OSM_FORMAT_H_
