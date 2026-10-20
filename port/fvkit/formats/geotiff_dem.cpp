// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// GeoTIFF DEM reader over libtiff 4 — see fvkit/formats/geotiff_dem.h.

#include "fvkit/formats/geotiff_dem.h"

#include <tiffio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <regex>
#include <system_error>
#include <utility>

#include "fv_map_enums.h"
#include "fvkit/log.h"

namespace fs = std::filesystem;

namespace fv {

namespace {

constexpr ttag_t kTagPixelScale = 33550;
constexpr ttag_t kTagTiepoint = 33922;
constexpr ttag_t kTagTransformation = 34264;
constexpr ttag_t kTagGeoKeyDirectory = 34735;
constexpr ttag_t kTagGeoDoubleParams = 34736;
constexpr ttag_t kTagGeoAsciiParams = 34737;
constexpr ttag_t kTagGdalNodata = 42113;

constexpr int kKeyModelType = 1024;
constexpr int kKeyRasterType = 1025;
constexpr int kKeyCitation = 1026;
constexpr int kKeyGeographicType = 2048;
constexpr int kKeyGeogCitation = 2049;
constexpr int kKeyGeodeticDatum = 2050;
constexpr int kKeyGeogAngularUnits = 2054;
constexpr int kKeyProjectedCSType = 3072;
constexpr int kKeyPcsCitation = 3073;
constexpr int kKeyProjection = 3074;
constexpr int kKeyProjLinearUnits = 3076;
constexpr int kKeyVerticalUnits = 4099;

constexpr int kUserDefined = 32767;

const float kNaN = std::numeric_limits<float>::quiet_NaN();

TIFFExtendProc g_parent_extender = nullptr;

/// Registers the GeoTIFF and GDAL_NODATA tags so libtiff reads them as
/// typed custom fields rather than anonymous ones.
void ExtendTags(TIFF* tif) {
  static const TIFFFieldInfo kFields[] = {
      {kTagPixelScale, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, (char*)"ModelPixelScale"},
      {kTagTiepoint, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, (char*)"ModelTiepoint"},
      {kTagTransformation, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1,
       (char*)"ModelTransformation"},
      {kTagGeoKeyDirectory, -1, -1, TIFF_SHORT, FIELD_CUSTOM, 1, 1, (char*)"GeoKeyDirectory"},
      {kTagGeoDoubleParams, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, (char*)"GeoDoubleParams"},
      {kTagGeoAsciiParams, -1, -1, TIFF_ASCII, FIELD_CUSTOM, 1, 0, (char*)"GeoASCIIParams"},
      {kTagGdalNodata, -1, -1, TIFF_ASCII, FIELD_CUSTOM, 1, 0, (char*)"GDALNoDataValue"},
  };
  TIFFMergeFieldInfo(tif, kFields, sizeof(kFields) / sizeof(kFields[0]));
  if (g_parent_extender) g_parent_extender(tif);
}

void InstallTagExtender() {
  static std::once_flag once;
  std::call_once(once, [] { g_parent_extender = TIFFSetTagExtender(ExtendTags); });
}

int QuietWarning(TIFF*, void*, const char*, const char*, va_list) { return 1; }

int LogError(TIFF*, void* user, const char* module, const char* fmt, va_list ap) {
  char msg[512];
  vsnprintf(msg, sizeof msg, fmt, ap);
  FV_LOG_WARNING("libtiff: " << static_cast<const char*>(user) << ": "
                             << (module ? module : "") << ": " << msg);
  return 1;
}

/// Opens read-only with warnings suppressed (GDAL metadata tags and the
/// like are not errors) and errors routed to the log. `path` is the error
/// handler's context and must outlive the handle.
TIFF* OpenTiff(const std::string& path) {
  InstallTagExtender();
  TIFFOpenOptions* opts = TIFFOpenOptionsAlloc();
  TIFFOpenOptionsSetWarningHandlerExtR(opts, QuietWarning, nullptr);
  TIFFOpenOptionsSetErrorHandlerExtR(opts, LogError, const_cast<char*>(path.c_str()));
  TIFF* tif = TIFFOpenExt(path.c_str(), "r", opts);
  TIFFOpenOptionsFree(opts);
  return tif;
}

// MARK: Transverse Mercator (Krueger series, third order; sub-millimetre in a UTM zone)

struct Utm {
  int zone = 0;
  bool south = false;
};

constexpr double kA = 6378137.0;
constexpr double kF = 1.0 / 298.257223563;
constexpr double kK0 = 0.9996;
constexpr double kDeg = M_PI / 180.0;

struct TmSeries {
  double n, big_a, alpha[3], beta[3], delta[3], e;
  TmSeries() {
    n = kF / (2.0 - kF);
    const double n2 = n * n, n3 = n2 * n;
    big_a = kA / (1.0 + n) * (1.0 + n2 / 4.0 + n2 * n2 / 64.0);
    alpha[0] = n / 2.0 - 2.0 * n2 / 3.0 + 5.0 * n3 / 16.0;
    alpha[1] = 13.0 * n2 / 48.0 - 3.0 * n3 / 5.0;
    alpha[2] = 61.0 * n3 / 240.0;
    beta[0] = n / 2.0 - 2.0 * n2 / 3.0 + 37.0 * n3 / 96.0;
    beta[1] = n2 / 48.0 + n3 / 15.0;
    beta[2] = 17.0 * n3 / 480.0;
    delta[0] = 2.0 * n - 2.0 * n2 / 3.0 - 2.0 * n3;
    delta[1] = 7.0 * n2 / 3.0 - 8.0 * n3 / 5.0;
    delta[2] = 56.0 * n3 / 15.0;
    e = 2.0 * std::sqrt(n) / (1.0 + n);
  }
};

const TmSeries& Tm() {
  static const TmSeries s;
  return s;
}

double CentralMeridian(const Utm& u) { return (u.zone * 6 - 183) * kDeg; }

void GeoToUtm(const Utm& u, double lat, double lon, double* e, double* n) {
  const TmSeries& s = Tm();
  const double phi = lat * kDeg;
  double dl = lon * kDeg - CentralMeridian(u);
  dl = std::remainder(dl, 2.0 * M_PI);
  const double sp = std::sin(phi);
  const double t = std::sinh(std::atanh(sp) - s.e * std::atanh(s.e * sp));
  const double xi = std::atan2(t, std::cos(dl));
  const double eta = std::atanh(std::sin(dl) / std::sqrt(1.0 + t * t));
  double x = eta, y = xi;
  for (int j = 1; j <= 3; ++j) {
    x += s.alpha[j - 1] * std::cos(2 * j * xi) * std::sinh(2 * j * eta);
    y += s.alpha[j - 1] * std::sin(2 * j * xi) * std::cosh(2 * j * eta);
  }
  *e = 500000.0 + kK0 * s.big_a * x;
  *n = (u.south ? 10000000.0 : 0.0) + kK0 * s.big_a * y;
}

void UtmToGeo(const Utm& u, double e, double n, double* lat, double* lon) {
  const TmSeries& s = Tm();
  const double xi = (n - (u.south ? 10000000.0 : 0.0)) / (kK0 * s.big_a);
  const double eta = (e - 500000.0) / (kK0 * s.big_a);
  double xp = xi, ep = eta;
  for (int j = 1; j <= 3; ++j) {
    xp -= s.beta[j - 1] * std::sin(2 * j * xi) * std::cosh(2 * j * eta);
    ep -= s.beta[j - 1] * std::cos(2 * j * xi) * std::sinh(2 * j * eta);
  }
  const double chi = std::asin(std::sin(xp) / std::cosh(ep));
  double phi = chi;
  for (int j = 1; j <= 3; ++j) phi += s.delta[j - 1] * std::sin(2 * j * chi);
  *lat = phi / kDeg;
  double lon_deg = (CentralMeridian(u) + std::atan2(std::sinh(ep), std::cos(xp))) / kDeg;
  if (lon_deg > 180.0) lon_deg -= 360.0;
  if (lon_deg < -180.0) lon_deg += 360.0;
  *lon = lon_deg;
}

// MARK: GeoKeys

struct GeoKeys {
  std::map<int, int> shorts;
  std::map<int, std::string> ascii;
};

GeoKeys ReadGeoKeys(TIFF* tif) {
  GeoKeys k;
  uint16_t count = 0;
  uint16_t* dir = nullptr;
  if (!TIFFGetField(tif, kTagGeoKeyDirectory, &count, &dir) || count < 4) return k;
  const char* ascii = nullptr;
  TIFFGetField(tif, kTagGeoAsciiParams, &ascii);
  const size_t ascii_len = ascii ? std::strlen(ascii) : 0;
  const int keys = dir[3];
  for (int i = 0; i < keys && 4 + 4 * i + 3 < count; ++i) {
    const uint16_t* e = dir + 4 + 4 * i;
    if (e[1] == 0) {
      k.shorts[e[0]] = e[3];
    } else if (e[1] == kTagGeoAsciiParams && ascii && e[3] < ascii_len) {
      std::string v(ascii + e[3], std::min<size_t>(e[2], ascii_len - e[3]));
      while (!v.empty() && (v.back() == '|' || v.back() == '\0')) v.pop_back();
      k.ascii[e[0]] = v;
    }
  }
  return k;
}

int KeyOr(const GeoKeys& k, int key, int fallback) {
  auto it = k.shorts.find(key);
  return it == k.shorts.end() ? fallback : it->second;
}

/// Geographic CRS codes whose datum is within a couple of metres of WGS-84.
bool IsWgs84LikeGeographic(int code) {
  switch (code) {
    case 4326:  // WGS 84
    case 4269:  // NAD83
    case 4152:  // NAD83(HARN)
    case 4759:  // NAD83(NSRS2007)
    case 6318:  // NAD83(2011)
    case 4617:  // NAD83(CSRS)
    case 4258:  // ETRS89
    case 4283:  // GDA94
    case 7844:  // GDA2020
      return true;
    default:
      return false;
  }
}

bool IsWgs84LikeDatum(int code) { return code == 6326 || code == 6269; }

/// UTM zone from an EPSG projected CRS code on a WGS-84-like datum.
bool UtmFromPcs(int code, Utm* u) {
  if (code >= 32601 && code <= 32660) *u = {code - 32600, false};
  else if (code >= 32701 && code <= 32760) *u = {code - 32700, true};
  else if (code >= 26901 && code <= 26923) *u = {code - 26900, false};  // NAD83
  else if (code >= 6330 && code <= 6348) *u = {code - 6329, false};     // NAD83(2011)
  else if (code == 6328) *u = {59, false};
  else if (code == 6329) *u = {60, false};
  else if (code >= 25828 && code <= 25838) *u = {code - 25800, false};  // ETRS89
  else return false;
  return true;
}

/// UTM zone from an EPSG projection code (ProjectionGeoKey, user-defined PCS).
bool UtmFromProjection(int code, Utm* u) {
  if (code >= 16001 && code <= 16060) *u = {code - 16000, false};
  else if (code >= 16101 && code <= 16160) *u = {code - 16100, true};
  else return false;
  return true;
}

/// "... UTM zone 17N" in a citation, on a WGS 84 or NAD83 datum.
bool UtmFromCitation(const std::string& text, Utm* u) {
  static const std::regex re(R"(UTM[ _]?zone[ _]?(\d{1,2})\s*([NSns])?)",
                             std::regex::icase);
  std::smatch m;
  if (!std::regex_search(text, m, re)) return false;
  if (text.find("WGS") == std::string::npos && text.find("NAD83") == std::string::npos &&
      text.find("NAD 83") == std::string::npos)
    return false;
  const int zone = std::atoi(m[1].str().c_str());
  if (zone < 1 || zone > 60) return false;
  *u = {zone, m[2].matched && std::toupper(m[2].str()[0]) == 'S'};
  return true;
}

double UnitToMeters(int code) {
  switch (code) {
    case 9002: return 0.3048;
    case 9003: return 1200.0 / 3937.0;
    default: return 1.0;
  }
}

}  // namespace

// MARK: GeoTiffDem::Impl

struct GeoTiffDem::Impl {
  struct Level {
    tdir_t dir = 0;
    int width = 0, height = 0;
    int block_w = 0, block_h = 0, blocks_across = 0;
    bool tiled = false;
    double scale_x = 1.0, scale_y = 1.0;  // level-0 posts per post of this level
    // A reduced level built in memory (box means of level 0) for a file with
    // no overviews of its own; `posts` is empty until first use.
    bool synthetic = false;
    std::vector<float> posts;
  };
  using BlockKey = std::pair<int, int>;  // (level, block)

  std::string path;  // libtiff's error-handler context; lives as long as `tif`
  TIFF* tif = nullptr;
  tdir_t current_dir = 0;
  std::vector<Level> levels;
  uint16_t sample_format = SAMPLEFORMAT_UINT;
  uint16_t bits = 0;

  bool geographic = true;
  Utm utm;
  double linear_m = 1.0;    // model unit -> metres (projected)
  double vertical_m = 1.0;  // stored value -> metres
  bool has_nodata = false;
  double nodata = 0.0;
  // Level-0 post index (c, r) -> model: x = a0 + a1 c + a2 r, y = b0 + b1 c + b2 r.
  double a[3] = {0, 1, 0}, b[3] = {0, 0, 1};
  double inv_a[3] = {0, 1, 0}, inv_b[3] = {0, 0, 1};

  std::map<BlockKey, std::pair<std::vector<float>, std::list<BlockKey>::iterator>> cache;
  std::list<BlockKey> lru;  // most recent first
  size_t cached_bytes = 0;
  size_t cache_limit = GeoTiffDem::kDefaultCacheBytes;
  bool synthetic_failed = false;

  ~Impl() {
    if (tif) TIFFClose(tif);
  }

  void ModelToGeo(double x, double y, double* lat, double* lon) const {
    if (geographic) {
      *lat = y;
      *lon = x;
    } else {
      UtmToGeo(utm, x * linear_m, y * linear_m, lat, lon);
    }
  }
  void GeoToModel(double lat, double lon, double* x, double* y) const {
    if (geographic) {
      *x = lon;
      *y = lat;
      return;
    }
    GeoToUtm(utm, lat, lon, x, y);
    *x /= linear_m;
    *y /= linear_m;
  }
  void IndexToGeo(double c, double r, double* lat, double* lon) const {
    ModelToGeo(a[0] + a[1] * c + a[2] * r, b[0] + b[1] * c + b[2] * r, lat, lon);
  }
  void GeoToIndex(const GeoPoint& p, double* c, double* r) const {
    double x, y;
    GeoToModel(p.lat, p.lon, &x, &y);
    *c = inv_a[0] + inv_a[1] * x + inv_a[2] * y;
    *r = inv_b[0] + inv_b[1] * x + inv_b[2] * y;
  }

  Status ReadLayout(tdir_t dir, Level* level) {
    uint32_t w = 0, h = 0;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
    if (w == 0 || h == 0) return Status::Error(kUnsupported, "empty image");
    level->dir = dir;
    level->width = static_cast<int>(w);
    level->height = static_cast<int>(h);
    level->tiled = TIFFIsTiled(tif);
    if (level->tiled) {
      uint32_t tw = 0, th = 0;
      TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tw);
      TIFFGetField(tif, TIFFTAG_TILELENGTH, &th);
      if (tw == 0 || th == 0) return Status::Error(kUnsupported, "bad tile size");
      level->block_w = static_cast<int>(tw);
      level->block_h = static_cast<int>(th);
    } else {
      uint32_t rps = h;
      TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rps);
      level->block_w = static_cast<int>(w);
      level->block_h = static_cast<int>(std::min<uint32_t>(rps, h));
    }
    level->blocks_across = (level->width + level->block_w - 1) / level->block_w;
    return Status::Ok();
  }

  /// Checks the current directory holds one band of a sample type read here.
  bool SameSampleLayout() {
    uint16_t spp = 1, fmt = SAMPLEFORMAT_UINT, bps = 0, planar = PLANARCONFIG_CONTIG;
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &fmt);
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
    return spp == 1 && fmt == sample_format && bps == bits;
  }

  Status ReadGeoreferencing(const std::string& path) {
    uint16_t n = 0;
    double* scale = nullptr;
    double* tie = nullptr;
    double* matrix = nullptr;
    uint16_t n_scale = 0, n_tie = 0;
    const bool has_matrix = TIFFGetField(tif, kTagTransformation, &n, &matrix) && n >= 16;
    const bool has_scale = TIFFGetField(tif, kTagPixelScale, &n_scale, &scale) && n_scale >= 2;
    const bool has_tie = TIFFGetField(tif, kTagTiepoint, &n_tie, &tie) && n_tie >= 6;
    if (!has_matrix && !(has_scale && has_tie))
      return Status::Error(kUnsupported, "no georeferencing: " + path);

    const GeoKeys keys = ReadGeoKeys(tif);
    // PixelIsArea (the default) puts the model coordinate on the pixel's
    // corner; posts are read at pixel centres.
    const double off = KeyOr(keys, kKeyRasterType, 1) == 2 ? 0.0 : 0.5;
    if (has_matrix) {
      a[1] = matrix[0]; a[2] = matrix[1]; a[0] = matrix[3] + off * (matrix[0] + matrix[1]);
      b[1] = matrix[4]; b[2] = matrix[5]; b[0] = matrix[7] + off * (matrix[4] + matrix[5]);
    } else {
      a[1] = scale[0]; a[2] = 0.0; a[0] = tie[3] + (off - tie[0]) * scale[0];
      b[1] = 0.0; b[2] = -scale[1]; b[0] = tie[4] - (off - tie[1]) * scale[1];
    }
    const double det = a[1] * b[2] - a[2] * b[1];
    if (det == 0.0 || !std::isfinite(det))
      return Status::Error(kUnsupported, "degenerate georeferencing: " + path);
    inv_a[1] = b[2] / det; inv_a[2] = -a[2] / det;
    inv_b[1] = -b[1] / det; inv_b[2] = a[1] / det;
    inv_a[0] = -(inv_a[1] * a[0] + inv_a[2] * b[0]);
    inv_b[0] = -(inv_b[1] * a[0] + inv_b[2] * b[0]);

    const int model = KeyOr(keys, kKeyModelType, 0);
    if (model == 2) {
      const int gcs = KeyOr(keys, kKeyGeographicType, 0);
      const int datum = KeyOr(keys, kKeyGeodeticDatum, 0);
      const bool ok = IsWgs84LikeGeographic(gcs) ||
                      ((gcs == 0 || gcs == kUserDefined) && IsWgs84LikeDatum(datum));
      if (!ok)
        return Status::Error(kUnsupported, "geographic CRS " + std::to_string(gcs) +
                                               " is not WGS-84 or NAD83: " + path);
      if (KeyOr(keys, kKeyGeogAngularUnits, 9102) != 9102)
        return Status::Error(kUnsupported, "angular units are not degrees: " + path);
      geographic = true;
    } else if (model == 1) {
      const int pcs = KeyOr(keys, kKeyProjectedCSType, 0);
      bool ok = UtmFromPcs(pcs, &utm);
      if (!ok && (pcs == 0 || pcs == kUserDefined)) {
        const int gcs = KeyOr(keys, kKeyGeographicType, 0);
        const bool datum_ok = IsWgs84LikeGeographic(gcs) ||
                              IsWgs84LikeDatum(KeyOr(keys, kKeyGeodeticDatum, 0));
        ok = datum_ok && UtmFromProjection(KeyOr(keys, kKeyProjection, 0), &utm);
        for (int key : {kKeyPcsCitation, kKeyCitation, kKeyGeogCitation}) {
          if (ok) break;
          auto it = keys.ascii.find(key);
          if (it != keys.ascii.end()) ok = UtmFromCitation(it->second, &utm);
        }
      }
      if (!ok)
        return Status::Error(kUnsupported, "projected CRS " + std::to_string(pcs) +
                                               " is not UTM on WGS-84 or NAD83: " + path);
      geographic = false;
      linear_m = UnitToMeters(KeyOr(keys, kKeyProjLinearUnits, 9001));
    } else {
      return Status::Error(kUnsupported, "no model type GeoKey: " + path);
    }
    vertical_m = UnitToMeters(KeyOr(keys, kKeyVerticalUnits, 9001));

    char* nodata_text = nullptr;
    if (TIFFGetField(tif, kTagGdalNodata, &nodata_text) && nodata_text) {
      char* end = nullptr;
      const double v = std::strtod(nodata_text, &end);
      if (end != nodata_text) {
        has_nodata = true;
        nodata = v;
      }
    }
    return Status::Ok();
  }

  /// Converts one decoded sample at `p` to metres (NaN for voids).
  float ToMeters(const unsigned char* p) const {
    double v;
    switch (sample_format) {
      case SAMPLEFORMAT_IEEEFP:
        v = bits == 32 ? *reinterpret_cast<const float*>(p) : *reinterpret_cast<const double*>(p);
        break;
      case SAMPLEFORMAT_INT:
        v = bits == 16 ? *reinterpret_cast<const int16_t*>(p) : *reinterpret_cast<const int32_t*>(p);
        break;
      default:
        v = bits == 16 ? *reinterpret_cast<const uint16_t*>(p) : *reinterpret_cast<const uint32_t*>(p);
        break;
    }
    if (!std::isfinite(v)) return kNaN;
    if (has_nodata) {
      // A float32 file's nodata string is a double; compare at the stored precision.
      const bool hit = (sample_format == SAMPLEFORMAT_IEEEFP && bits == 32)
                           ? static_cast<float>(v) == static_cast<float>(nodata)
                           : v == nodata;
      if (hit) return kNaN;
    }
    return static_cast<float>(v * vertical_m);
  }

  /// Decoded block `block` of level `li`, or nullptr on a read error.
  const std::vector<float>* Block(int li, int block) {
    const BlockKey key{li, block};
    auto hit = cache.find(key);
    if (hit != cache.end()) {
      lru.splice(lru.begin(), lru, hit->second.second);
      return &hit->second.first;
    }
    const Level& L = levels[li];
    if (current_dir != L.dir) {
      if (!TIFFSetDirectory(tif, L.dir)) return nullptr;
      current_dir = L.dir;
    }
    const tmsize_t size = L.tiled ? TIFFTileSize(tif) : TIFFStripSize(tif);
    std::vector<unsigned char> raw(static_cast<size_t>(size));
    const tmsize_t got = L.tiled ? TIFFReadEncodedTile(tif, block, raw.data(), size)
                                 : TIFFReadEncodedStrip(tif, block, raw.data(), size);
    if (got < 0) return nullptr;
    const size_t bytes_per = bits / 8;
    const size_t posts = static_cast<size_t>(L.block_w) * L.block_h;
    std::vector<float> out(posts, kNaN);
    const size_t have = std::min(posts, static_cast<size_t>(got) / bytes_per);
    for (size_t i = 0; i < have; ++i) out[i] = ToMeters(raw.data() + i * bytes_per);

    Evict(posts * sizeof(float));
    lru.push_front(key);
    cached_bytes += posts * sizeof(float);
    auto ins = cache.emplace(key, std::make_pair(std::move(out), lru.begin()));
    return &ins.first->second.first;
  }

  /// Drops least recently used blocks until `incoming` more bytes fit.
  void Evict(size_t incoming) {
    while (!lru.empty() && cached_bytes + incoming > cache_limit) {
      auto old = cache.find(lru.back());
      cached_bytes -= old->second.first.size() * sizeof(float);
      cache.erase(old);
      lru.pop_back();
    }
  }

  /// Adds box-mean levels of 4, 16 and 64 posts (while at least 32 posts
  /// wide and high) to a large file that has no overviews.
  void PlanSyntheticLevels() {
    // By value: push_back below reallocates `levels`.
    const int w0 = levels[0].width, h0 = levels[0].height;
    if (levels.size() > 1 || std::max(w0, h0) <= 1024) return;
    for (int f = 4; f <= 64; f *= 4) {
      Level lv;
      lv.width = (w0 + f - 1) / f;
      lv.height = (h0 + f - 1) / f;
      if (lv.width < 32 || lv.height < 32) break;
      lv.scale_x = lv.scale_y = f;
      lv.synthetic = true;
      levels.push_back(std::move(lv));
    }
  }

  /// Fills every synthetic level in one pass over the level-0 blocks.
  bool BuildSyntheticLevels() {
    if (synthetic_failed) return false;
    const Level& L0 = levels[0];
    const int f = static_cast<int>(levels[1].scale_x);
    const int w1 = levels[1].width, h1 = levels[1].height;
    std::vector<double> sum(static_cast<size_t>(w1) * h1, 0.0);
    std::vector<int> count(sum.size(), 0);
    const int blocks_down = (L0.height + L0.block_h - 1) / L0.block_h;
    for (int by = 0; by < blocks_down; ++by) {
      for (int bx = 0; bx < L0.blocks_across; ++bx) {
        const std::vector<float>* blk = Block(0, by * L0.blocks_across + bx);
        if (blk == nullptr) {
          synthetic_failed = true;
          return false;
        }
        for (int y = 0; y < L0.block_h; ++y) {
          const int r = by * L0.block_h + y;
          if (r >= L0.height) break;
          for (int x = 0; x < L0.block_w; ++x) {
            const int c = bx * L0.block_w + x;
            if (c >= L0.width) break;
            const float v = (*blk)[static_cast<size_t>(y) * L0.block_w + x];
            if (std::isnan(v)) continue;
            const size_t i = static_cast<size_t>(r / f) * w1 + c / f;
            sum[i] += v;
            ++count[i];
          }
        }
      }
    }
    // Each coarser level merges 4x4 cells of the one before, so its means
    // are weighted by the level-0 posts behind them.
    for (size_t li = 1; li < levels.size(); ++li) {
      Level& L = levels[li];
      if (li > 1) {
        const Level& P = levels[li - 1];
        std::vector<double> s2(static_cast<size_t>(L.width) * L.height, 0.0);
        std::vector<int> c2(s2.size(), 0);
        for (int r = 0; r < P.height; ++r)
          for (int c = 0; c < P.width; ++c) {
            const size_t from = static_cast<size_t>(r) * P.width + c;
            const size_t to = static_cast<size_t>(r / 4) * L.width + c / 4;
            s2[to] += sum[from];
            c2[to] += count[from];
          }
        sum.swap(s2);
        count.swap(c2);
      }
      L.posts.resize(sum.size());
      for (size_t i = 0; i < sum.size(); ++i)
        L.posts[i] = count[i] ? static_cast<float>(sum[i] / count[i]) : kNaN;
    }
    return true;
  }

  /// Post (c, r) of level `li`; `ok` false on a read error.
  float Post(int li, int c, int r, bool* ok) {
    const Level& L = levels[li];
    if (L.synthetic) {
      if (L.posts.empty() && !BuildSyntheticLevels()) {
        *ok = false;
        return kNaN;
      }
      return L.posts[static_cast<size_t>(r) * L.width + c];
    }
    const int bx = c / L.block_w, by = r / L.block_h;
    const std::vector<float>* blk = Block(li, by * L.blocks_across + bx);
    if (blk == nullptr) {
      *ok = false;
      return kNaN;
    }
    return (*blk)[static_cast<size_t>(r - by * L.block_h) * L.block_w + (c - bx * L.block_w)];
  }

  /// Bilinear sample of level `li` at level-0 post index (c0, r0).
  Status Sample(int li, double c0, double r0, float* meters) {
    const Level& L0 = levels[0];
    if (c0 < -0.5 || r0 < -0.5 || c0 > L0.width - 0.5 || r0 > L0.height - 0.5)
      return Status::Error(kOutOfCoverage, "point is off the grid");
    const Level& L = levels[li];
    // Reduced posts cover the same extent: scale pixel-edge coordinates.
    const double c = (c0 + 0.5) / L.scale_x - 0.5;
    const double r = (r0 + 0.5) / L.scale_y - 0.5;
    const double cc = std::clamp(c, 0.0, L.width - 1.0);
    const double rc = std::clamp(r, 0.0, L.height - 1.0);
    const int x0 = static_cast<int>(cc), y0 = static_cast<int>(rc);
    const int x1 = std::min(x0 + 1, L.width - 1), y1 = std::min(y0 + 1, L.height - 1);
    const double dx = cc - x0, dy = rc - y0;
    bool ok = true;
    const float v00 = Post(li, x0, y0, &ok), v10 = Post(li, x1, y0, &ok);
    const float v01 = Post(li, x0, y1, &ok), v11 = Post(li, x1, y1, &ok);
    if (!ok) return Status::Error(kIoError, "block read failed");
    if (std::isnan(v00) || std::isnan(v10) || std::isnan(v01) || std::isnan(v11)) {
      const bool right = dx >= 0.5, down = dy >= 0.5;
      *meters = down ? (right ? v11 : v01) : (right ? v10 : v00);
      return Status::Ok();
    }
    *meters = static_cast<float>((1 - dx) * (1 - dy) * v00 + dx * (1 - dy) * v10 +
                                 (1 - dx) * dy * v01 + dx * dy * v11);
    return Status::Ok();
  }
};

// MARK: GeoTiffDem

GeoTiffDem::GeoTiffDem(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GeoTiffDem::~GeoTiffDem() = default;

int GeoTiffDem::Width() const { return impl_->levels[0].width; }
int GeoTiffDem::Height() const { return impl_->levels[0].height; }
size_t GeoTiffDem::CachedBytes() const { return impl_->cached_bytes; }

void GeoTiffDem::SetCacheLimit(size_t bytes) {
  impl_->cache_limit = bytes;
  impl_->Evict(0);
}

Status GeoTiffDem::Open(const std::string& path, std::unique_ptr<GeoTiffDem>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  auto impl = std::make_unique<Impl>();
  impl->path = path;
  impl->tif = OpenTiff(impl->path);
  if (impl->tif == nullptr) return Status::Error(kIoError, "not a readable TIFF: " + path);
  TIFF* tif = impl->tif;

  uint16_t spp = 1, photometric = PHOTOMETRIC_MINISBLACK;
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &impl->sample_format);
  TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &impl->bits);
  TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
  const uint16_t fmt = impl->sample_format, bps = impl->bits;
  const bool sample_ok = (fmt == SAMPLEFORMAT_IEEEFP && (bps == 32 || bps == 64)) ||
                         ((fmt == SAMPLEFORMAT_INT || fmt == SAMPLEFORMAT_UINT) &&
                          (bps == 16 || bps == 32));
  if (spp != 1 || !sample_ok || photometric == PHOTOMETRIC_PALETTE)
    return Status::Error(kUnsupported, "not a single-band elevation grid: " + path);

  Impl::Level level0;
  Status s = impl->ReadLayout(0, &level0);
  if (!s.ok()) return s;
  impl->levels.push_back(level0);
  s = impl->ReadGeoreferencing(path);
  if (!s.ok()) return s;

  // Reduced-resolution IFDs (COG overviews) follow the full image.
  for (tdir_t d = 1; TIFFReadDirectory(tif); ++d) {
    uint32_t subfile = 0;
    TIFFGetField(tif, TIFFTAG_SUBFILETYPE, &subfile);
    if (!(subfile & FILETYPE_REDUCEDIMAGE) || (subfile & FILETYPE_MASK)) continue;
    if (!impl->SameSampleLayout()) continue;
    Impl::Level lv;
    if (impl->ReadLayout(d, &lv).ok() && lv.width < impl->levels.back().width) {
      lv.scale_x = static_cast<double>(level0.width) / lv.width;
      lv.scale_y = static_cast<double>(level0.height) / lv.height;
      impl->levels.push_back(lv);
    }
  }
  impl->PlanSyntheticLevels();
  TIFFSetDirectory(tif, 0);
  impl->current_dir = 0;

  std::unique_ptr<GeoTiffDem> dem(new GeoTiffDem(std::move(impl)));
  dem->path_ = path;
  const Impl& m = *dem->impl_;
  const double w = m.levels[0].width, h = m.levels[0].height;
  // Walk the pixel-extent outline: a UTM grid's edges are curves in lat/lon.
  bool first = true;
  constexpr int kSteps = 16;
  for (int i = 0; i <= kSteps; ++i) {
    const double t = static_cast<double>(i) / kSteps;
    const double edge[4][2] = {{-0.5 + t * w, -0.5}, {-0.5 + t * w, h - 0.5},
                               {-0.5, -0.5 + t * h}, {w - 0.5, -0.5 + t * h}};
    for (const auto& e : edge) {
      double lat, lon;
      m.IndexToGeo(e[0], e[1], &lat, &lon);
      if (first) {
        dem->bounds_ = GeoRect{{lat, lon}, {lat, lon}};
        first = false;
      }
      dem->bounds_.ll.lat = std::min(dem->bounds_.ll.lat, lat);
      dem->bounds_.ll.lon = std::min(dem->bounds_.ll.lon, lon);
      dem->bounds_.ur.lat = std::max(dem->bounds_.ur.lat, lat);
      dem->bounds_.ur.lon = std::max(dem->bounds_.ur.lon, lon);
    }
  }
  const double centre_lat = 0.5 * (dem->bounds_.ll.lat + dem->bounds_.ur.lat);
  const double step_x = std::hypot(m.a[1], m.b[1]);
  dem->post_m_ = m.geographic ? step_x * 111320.0 * std::cos(centre_lat * kDeg)
                              : step_x * m.linear_m;
  *out = std::move(dem);
  return Status::Ok();
}

Status GeoTiffDem::GetElevation(const GeoPoint& p, float* meters) {
  return GetElevationAtResolution(p, 0.0, meters);
}

Status GeoTiffDem::GetElevationAtResolution(const GeoPoint& p, double max_post_m,
                                            float* meters) {
  if (meters == nullptr) return Status::Error(kInvalidArg, "meters is null");
  if (!bounds_.Contains(p)) return Status::Error(kOutOfCoverage, "point is off the grid");
  int li = 0;
  for (int i = 1; i < static_cast<int>(impl_->levels.size()); ++i) {
    const Impl::Level& L = impl_->levels[i];
    if (L.synthetic && impl_->synthetic_failed) break;
    if (post_m_ * L.scale_x <= max_post_m) li = i;
  }
  double c, r;
  impl_->GeoToIndex(p, &c, &r);
  return impl_->Sample(li, c, r, meters);
}

bool GeoTiffDem::PostSpacing(const GeoPoint& p, double* lat_deg, double* lon_deg) const {
  if (!bounds_.Contains(p)) return false;
  double c, r, lat0, lon0, lat_c, lon_c, lat_r, lon_r;
  impl_->GeoToIndex(p, &c, &r);
  impl_->IndexToGeo(c, r, &lat0, &lon0);
  impl_->IndexToGeo(c + 1, r, &lat_c, &lon_c);
  impl_->IndexToGeo(c, r + 1, &lat_r, &lon_r);
  if (lat_deg) *lat_deg = std::max(std::fabs(lat_c - lat0), std::fabs(lat_r - lat0));
  if (lon_deg) *lon_deg = std::max(std::fabs(lon_c - lon0), std::fabs(lon_r - lon0));
  return true;
}

// MARK: Enumerator

namespace {

bool HasTiffExtension(const fs::path& p) {
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return ext == ".tif" || ext == ".tiff";
}

/// The TIFF files directly inside `dir`, sorted. Empty on an unreadable dir.
std::vector<std::string> TiffsIn(const std::string& dir, std::error_code* ec) {
  std::vector<std::string> out;
  for (fs::directory_iterator it(dir, *ec), end; !*ec && it != end; it.increment(*ec)) {
    std::error_code fec;
    if (it->is_regular_file(fec) && HasTiffExtension(it->path()))
      out.push_back(it->path().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

Status GeoTiffDemEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;
  std::error_code ec;
  const std::vector<std::string> files = TiffsIn(dir, &ec);
  if (ec) return Status::Error(kNotFound, "DEM dir not readable: " + dir + " (" + ec.message() + ")");
  for (const std::string& path : files) {
    std::unique_ptr<GeoTiffDem> dem;
    Status s = GeoTiffDem::Open(path, &dem);
    if (!s.ok()) {
      if (s.code != kUnsupported) FV_LOG_WARNING("lidar: skipping " << path << ": " << s.message);
      continue;
    }
    FrameInfo info;
    info.path = path;
    info.bounds = dem->Bounds();
    info.series_key = "Lidar";
    info.scale = std::max(1.0, std::round(dem->PostSizeMeters()));
    info.scale_units = MAP_SCALE_METERS;
    std::error_code sec;
    info.size_bytes = static_cast<int64_t>(fs::file_size(path, sec));
    if (sec) info.size_bytes = 0;
    frames_.push_back(std::move(info));
  }
  return Status::Ok();
}

bool GeoTiffDemEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

// MARK: GeoTiffDemSource

GeoTiffDemSource::GeoTiffDemSource(const std::string& dir)
    : GeoTiffDemSource(std::vector<std::string>{dir}) {}

GeoTiffDemSource::GeoTiffDemSource(const std::vector<std::string>& paths) {
  std::vector<std::string> files;
  for (const std::string& p : paths) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
      const std::vector<std::string> in = TiffsIn(p, &ec);
      files.insert(files.end(), in.begin(), in.end());
    } else {
      files.push_back(p);
    }
  }
  Index(files);
}

GeoTiffDemSource::~GeoTiffDemSource() = default;

void GeoTiffDemSource::SetCacheLimitPerFile(size_t bytes) {
  cache_limit_ = bytes;
  for (auto& [index, dem] : open_)
    if (dem) dem->SetCacheLimit(bytes);
}

void GeoTiffDemSource::Index(const std::vector<std::string>& paths) {
  for (const std::string& path : paths) {
    std::unique_ptr<GeoTiffDem> dem;
    Status s = GeoTiffDem::Open(path, &dem);
    if (!s.ok()) {
      if (s.code != kUnsupported) FV_LOG_WARNING("lidar: skipping " << path << ": " << s.message);
      continue;
    }
    files_.push_back(FileRef{path, dem->Bounds(), dem->PostSizeMeters()});
  }
  std::stable_sort(files_.begin(), files_.end(),
                   [](const FileRef& x, const FileRef& y) { return x.post_m < y.post_m; });
  for (size_t i = 0; i < files_.size(); ++i) {
    const GeoRect& b = files_[i].bounds;
    if (i == 0) {
      bounds_ = b;
      continue;
    }
    bounds_.ll.lat = std::min(bounds_.ll.lat, b.ll.lat);
    bounds_.ll.lon = std::min(bounds_.ll.lon, b.ll.lon);
    bounds_.ur.lat = std::max(bounds_.ur.lat, b.ur.lat);
    bounds_.ur.lon = std::max(bounds_.ur.lon, b.ur.lon);
  }
}

GeoTiffDem* GeoTiffDemSource::File(size_t i) {
  for (auto it = open_.begin(); it != open_.end(); ++it) {
    if (it->first == i) {
      open_.splice(open_.begin(), open_, it);
      return open_.front().second.get();
    }
  }
  std::unique_ptr<GeoTiffDem> dem;
  if (!GeoTiffDem::Open(files_[i].path, &dem).ok()) dem.reset();
  if (dem) dem->SetCacheLimit(cache_limit_);
  if (open_.size() >= kMaxOpenFiles) open_.pop_back();
  open_.emplace_front(i, std::move(dem));
  return open_.front().second.get();
}

Status GeoTiffDemSource::Sample(const GeoPoint& p, double max_post_m, float* meters) {
  Status result = Status::Error(kOutOfCoverage, "no DEM file covers the point");
  bool void_hit = false;
  for (size_t i = 0; i < files_.size(); ++i) {
    if (!files_[i].bounds.Contains(p)) continue;
    GeoTiffDem* dem = File(i);
    if (dem == nullptr) {
      result = Status::Error(kIoError, "DEM file no longer opens: " + files_[i].path);
      continue;
    }
    float v = kNaN;
    Status s = dem->GetElevationAtResolution(p, max_post_m, &v);
    if (s.ok() && !std::isnan(v)) {
      *meters = v;
      return s;
    }
    if (s.ok()) void_hit = true;
    else if (s.code != kOutOfCoverage) result = s;
  }
  // A void in the finest file may be filled by a coarser one; only when none
  // has a value is the post a void.
  if (void_hit) {
    *meters = kNaN;
    return Status::Ok();
  }
  return result;
}

Status GeoTiffDemSource::GetElevation(const GeoPoint& p, float* elevation_meters) {
  if (elevation_meters == nullptr) return Status::Error(kInvalidArg, "elevation_meters is null");
  return Sample(p, 0.0, elevation_meters);
}

bool GeoTiffDemSource::PostSpacing(const GeoPoint& p, double* lat_deg, double* lon_deg) {
  for (size_t i = 0; i < files_.size(); ++i) {
    if (!files_[i].bounds.Contains(p)) continue;
    GeoTiffDem* dem = File(i);
    if (dem != nullptr && dem->PostSpacing(p, lat_deg, lon_deg)) return true;
  }
  return false;
}

bool GeoTiffDemSource::SampleGrid(const GeoRect& bounds, int width, int height, float* meters,
                                  unsigned char* covered) {
  if (meters == nullptr || width < 2 || height < 2) return false;
  const double dlat = (bounds.ur.lat - bounds.ll.lat) / (height - 1);
  const double dlon = (bounds.ur.lon - bounds.ll.lon) / (width - 1);
  const double mid_lat = 0.5 * (bounds.ll.lat + bounds.ur.lat);
  // The grid step in metres; a level whose posts are no larger loses nothing.
  const double step_m = std::min(dlat * 110574.0, dlon * 111320.0 * std::cos(mid_lat * kDeg));
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const size_t i = static_cast<size_t>(r) * width + c;
      float v = kNaN;
      const bool ok = Sample(GeoPoint{bounds.ll.lat + r * dlat, bounds.ll.lon + c * dlon},
                             step_m, &v).ok();
      meters[i] = ok ? v : kNaN;
      if (covered != nullptr) covered[i] = ok ? 1 : 0;
    }
  }
  return true;
}

}  // namespace fv
