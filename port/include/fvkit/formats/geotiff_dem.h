// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fvkit/formats/geotiff_dem.h — GeoTIFF elevation grids (format key "lidar"),
 * the counterpart of FalconView's LidarMapServer, read through libtiff 4.
 *
 * A DEM file is a single-band GeoTIFF of 16/32-bit integers or 32/64-bit
 * floats, stripped or tiled, uncompressed or LZW/Deflate/PackBits, with any
 * reduced-resolution IFDs (COG overviews) used for coarse grid sampling. A
 * file over 1024 posts with no overviews gets 4/16/64-post box-mean levels,
 * built in memory in one pass on first coarse use.
 * Supported georeferencing: geographic WGS-84 and NAD83 (including its
 * realizations), and UTM on WGS-84 or NAD83, by EPSG code or by a "UTM zone"
 * citation. NAD83 is treated as WGS-84 (about 1 m in CONUS).
 *
 * Elevations come back in metres (VerticalUnitsGeoKey feet are converted);
 * GDAL_NODATA posts and non-finite values are NaN. Interpolation is bilinear
 * between post centres; a cell with a void corner answers its nearest post.
 *
 * The Windows server read only uncompressed float strips, one strip read per
 * point, and took units and the void value from a global LidarConfig.dat;
 * none of that is reproduced.
 */

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/formats/enumerate.h"
#include "fvkit/formats/source.h"

namespace fv {

/// One GeoTIFF DEM file. Not thread-safe; holds an open libtiff handle.
class GeoTiffDem {
 public:
  ~GeoTiffDem();
  GeoTiffDem(const GeoTiffDem&) = delete;
  GeoTiffDem& operator=(const GeoTiffDem&) = delete;

  /// Opens `path` and reads its georeferencing. kUnsupported when the file is
  /// a TIFF but not an elevation grid this reader understands.
  static Status Open(const std::string& path, std::unique_ptr<GeoTiffDem>* out);

  /// WGS-84 box around the full pixel extent.
  GeoRect Bounds() const { return bounds_; }
  int Width() const;
  int Height() const;
  /// Ground size of one full-resolution post, in metres (east-west).
  double PostSizeMeters() const { return post_m_; }
  const std::string& Path() const { return path_; }

  /// Bilinear elevation at `p` from the full-resolution grid.
  /// kOutOfCoverage outside the grid; NaN with Ok on a void.
  Status GetElevation(const GeoPoint& p, float* meters);

  /// Same, from the coarsest level whose posts are no larger than
  /// `max_post_m` metres (level 0 when none is).
  Status GetElevationAtResolution(const GeoPoint& p, double max_post_m, float* meters);

  /// Native post spacing at `p` in degrees.
  bool PostSpacing(const GeoPoint& p, double* lat_deg, double* lon_deg) const;

  /// Bytes held by the decoded-block cache.
  size_t CachedBytes() const;
  /// Caps the decoded-block cache (default kDefaultCacheBytes); shrinks it now if needed.
  void SetCacheLimit(size_t bytes);

  static constexpr size_t kDefaultCacheBytes = size_t{64} << 20;

 private:
  struct Impl;
  explicit GeoTiffDem(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
  std::string path_;
  GeoRect bounds_;
  double post_m_ = 0.0;
};

/// Enumerates the DEM GeoTIFFs directly inside one directory (no recursion,
/// like FalconView's `*.tif` search). Imagery TIFFs are skipped silently.
/// Series "Lidar", scale = post size in whole metres (MAP_SCALE_METERS).
class GeoTiffDemEnumerator : public IFrameEnumerator {
 public:
  Status Begin(const std::string& dir) override;
  bool Next(FrameInfo* info) override;

 private:
  std::vector<FrameInfo> frames_;
  size_t next_ = 0;
};

/// A set of DEM GeoTIFFs as one elevation source. Where files overlap, the
/// finest posts win. Files open lazily and at most `kMaxOpenFiles` stay open.
/// Not thread-safe.
class GeoTiffDemSource : public IElevationSource {
 public:
  static constexpr size_t kMaxOpenFiles = 8;

  /// Indexes the DEM files directly inside `dir` (each header is read once).
  explicit GeoTiffDemSource(const std::string& dir);
  /// Indexes each path: a file is taken as is, a directory as above.
  explicit GeoTiffDemSource(const std::vector<std::string>& paths);
  ~GeoTiffDemSource() override;

  /// Number of DEM files indexed.
  size_t FileCount() const { return files_.size(); }
  /// Decoded-block cache per open file (GeoTiffDem::kDefaultCacheBytes by
  /// default). A batch job sampling a whole file in no spatial order wants
  /// it large enough to hold the file.
  void SetCacheLimitPerFile(size_t bytes);

  GeoRect Bounds() const override { return bounds_; }
  Status GetElevation(const GeoPoint& p, float* elevation_meters) override;
  bool PostSpacing(const GeoPoint& p, double* lat_deg, double* lon_deg) override;
  bool SampleGrid(const GeoRect& bounds, int width, int height, float* meters,
                  unsigned char* covered) override;

 private:
  struct FileRef {
    std::string path;
    GeoRect bounds;
    double post_m = 0.0;
  };

  void Index(const std::vector<std::string>& paths);
  /// The open file for files_[i], opening it (and closing the least recently
  /// used one) as needed. nullptr when it fails to open.
  GeoTiffDem* File(size_t i);
  Status Sample(const GeoPoint& p, double max_post_m, float* meters);

  std::vector<FileRef> files_;  // finest posts first
  std::list<std::pair<size_t, std::unique_ptr<GeoTiffDem>>> open_;  // MRU first
  GeoRect bounds_{};
  size_t cache_limit_ = GeoTiffDem::kDefaultCacheBytes;
};

}  // namespace fv
