// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::VpfVectorSource — DNC/VPF as an FvKit IVectorSource (plan phases V5a/V5c).
//
// Rides the headless VPF reader ported in V1 (port/VpfMapServer).
//
// SIMPLE feature classes (V5a) are a direct table join and need no topology:
//
//   <coverage>/<CLASS>.LFT   line   rows: (id, f_code, tile_id, edg_id)
//   <coverage>/<CLASS>.PFT   point  rows: (id, f_code, tile_id, end_id/cnd_id)
//         then the primitive's coordinates come from that tile's EDG / END.
//
// AREA feature classes (V5c) do need topology: a .AFT row names a FACE, and a
// face is turned into rings (part[0] outer, part[1..] holes) by walking the
// FAC -> RNG -> EDG winged-edge topology. That walk lived in the Windows-only
// VPFFace/vpfelem (coupled to GDI CRgn/MapProj drawing and the FalconView app
// layer — map.h, graphics.h, param.h, refresh.h); the geometry half is
// extracted into fv_vpf_vector_source.cpp (TraverseRing/RingPoints), none of
// the drawing. Area features are emitted with VectorGeometryType::kArea.
//
// COORDINATE ORDER: VPF stores tuples as (X, Y) = (lon, lat), matching what
// variant.h's coord structs declare, so the member names are trustworthy.
//
// Worth knowing why that sentence needed checking: building this source
// surfaced a latent LP64 bug in the V1 reader (vpfrcset.cpp read a
// variable-length field's 4-byte count with `*(long int*)`, 8 bytes on LP64).
// The count survived truncation, so it looked fine, but the cursor then
// skipped the payload's first float and every tuple came back as
// (lat[i], lon[i+1]) with the last reading past the end. That made latitude
// appear to sit in the `lon` member. Fixed in vpfrcset.cpp; the tests here
// pin real dnc17 coordinates so it cannot regress silently.

#ifndef FV_VPF_VECTOR_SOURCE_H_
#define FV_VPF_VECTOR_SOURCE_H_

#include <memory>
#include <string>
#include <vector>

#include "fvkit/vector/vector.h"

namespace fv {

class VpfVectorSource : public IVectorSource {
 public:
  VpfVectorSource();
  ~VpfVectorSource() override;

  VpfVectorSource(const VpfVectorSource&) = delete;
  VpfVectorSource& operator=(const VpfVectorSource&) = delete;

  // `path` is a DNC LIBRARY directory, e.g. ".../dnc17/h1707300".
  // Backslashes and case are resolved at the file-open boundary as elsewhere.
  Status Open(const std::string& path) override;
  bool IsOpen() const override;

  // Union of every feature returned so far is not known up front, so this is
  // the union of the library's tile extents (see NOTE in the .cpp: computed
  // from feature geometry, since tile bounds need the tileref face).
  GeoRect Bounds() const override;

  std::vector<std::string> Layers() const override;
  Status Query(const VectorQuery& q, std::vector<VectorFeature>* out) override;

  // Identify (plan §5.3): re-reads the row named by `ref` and decodes it for
  // display — column descriptions from the table header, coded values through
  // the coverage's INT.VDT/CHAR.VDT (fv_vpf_vdt.h), the class description from
  // the FCA. `ref` must be one Query() handed out (its `layer` indexes
  // Layers()). The dictionaries load lazily, per coverage, on first use.
  Status Describe(const FeatureRef& ref, FeatureDescription* out) override;

  // --- the parsed-feature cache (R3c) --------------------------------------
  //
  // Before R3c every Query walked every row of every feature table and built
  // every feature, then threw away the ones outside the box: a viewport
  // returning 5 features cost the same 9.4 ms as one returning 5,037. The
  // library is now parsed ONCE, on the first Query, and every later query is
  // a box test over what is already in memory.
  //
  // This is the same arrangement ENC has had since E1 (cells parsed at Open)
  // and OSM since O1 (an LRU of decoded tiles). DNC was the product without
  // it, and the one the R3a/R3b profile was taken on.
  //
  // Off = the pre-R3c behaviour, for a caller that would rather pay the scan
  // than hold the library (roughly 1.2x its size on disk). Must be set before
  // the first Query to have any effect on what is already cached; turning it
  // off releases the cache.
  void SetFeatureCacheEnabled(bool on);
  bool feature_cache_enabled() const;
  // Features held. 0 before the first Query, and always 0 with the cache off.
  size_t cached_features() const;

 private:
  Status ScanAll(std::vector<VectorFeature>* out);
  static void AppendMatching(const std::vector<VectorFeature>& src,
                             const VectorQuery& q,
                             std::vector<VectorFeature>* out);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv

#endif  // FV_VPF_VECTOR_SOURCE_H_
