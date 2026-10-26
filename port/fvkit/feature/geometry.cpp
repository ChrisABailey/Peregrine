// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/geometry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace fv {

namespace {

constexpr const char* kTypeNames[] = {"GEOMETRY",   "POINT",           "LINESTRING",
                                      "POLYGON",    "MULTIPOINT",      "MULTILINESTRING",
                                      "MULTIPOLYGON", "GEOMETRYCOLLECTION"};

/// Deeper nesting than this is treated as a corrupt blob, not a geometry.
constexpr int kMaxDepth = 32;

bool IsCollection(GeometryType t) {
  return t == GeometryType::kMultiPoint || t == GeometryType::kMultiLineString ||
         t == GeometryType::kMultiPolygon || t == GeometryType::kGeometryCollection;
}

/// The part type a Multi* may hold; kGeometry for a collection (any type).
GeometryType MemberType(GeometryType t) {
  switch (t) {
    case GeometryType::kMultiPoint: return GeometryType::kPoint;
    case GeometryType::kMultiLineString: return GeometryType::kLineString;
    case GeometryType::kMultiPolygon: return GeometryType::kPolygon;
    default: return GeometryType::kGeometry;
  }
}

bool SameDouble(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }

// MARK: WKB reader

class WkbReader {
 public:
  WkbReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  size_t offset() const { return pos_; }

  Status Read(Geometry* out, int depth) {
    if (depth > kMaxDepth) return Fail("nesting deeper than 32");
    uint8_t order = 0;
    if (!Byte(&order)) return Fail("truncated byte order");
    if (order > 1) return Fail("bad byte order " + std::to_string(order));
    little_ = order == 1;
    uint32_t code = 0;
    if (!U32(&code)) return Fail("truncated type");

    // EWKB carries Z/M as high bits; ISO as thousands.
    bool z = (code & 0x80000000u) != 0;
    bool m = (code & 0x40000000u) != 0;
    code &= 0x0FFFFFFFu;
    const uint32_t dims = code / 1000;
    const uint32_t base = code % 1000;
    if (dims == 1 || dims == 3) z = true;
    if (dims == 2 || dims == 3) m = true;
    if (dims > 3 || base < 1 || base > 7) return Fail("unknown type code " + std::to_string(code));

    Geometry g;
    g.type = static_cast<GeometryType>(base);
    g.has_z = z;
    g.has_m = m;
    const size_t stride = 16 + (z ? 8 : 0) + (m ? 8 : 0);

    if (g.type == GeometryType::kPoint) {
      Position p;
      if (!Coord(z, m, &p)) return Fail("truncated point");
      if (!(std::isnan(p.lat) && std::isnan(p.lon))) g.paths.push_back({p});
    } else if (g.type == GeometryType::kLineString) {
      Path path;
      Status s = ReadPath(z, m, stride, &path);
      if (!s.ok()) return s;
      g.paths.push_back(std::move(path));
    } else if (g.type == GeometryType::kPolygon) {
      uint32_t rings = 0;
      if (!Count(&rings, 4)) return Fail("bad ring count");
      g.paths.resize(rings);
      for (Path& ring : g.paths) {
        Status s = ReadPath(z, m, stride, &ring);
        if (!s.ok()) return s;
      }
    } else {
      uint32_t n = 0;
      // A part is at least a byte order and a type code.
      if (!Count(&n, 5)) return Fail("bad part count");
      const GeometryType member = MemberType(g.type);
      g.parts.resize(n);
      for (Geometry& part : g.parts) {
        Status s = Read(&part, depth + 1);
        if (!s.ok()) return s;
        if (member != GeometryType::kGeometry && part.type != member)
          return Fail(std::string(GeometryTypeName(g.type)) + " holds a " +
                      GeometryTypeName(part.type));
      }
    }
    *out = std::move(g);
    return Status::Ok();
  }

 private:
  Status Fail(const std::string& why) const {
    return Status::Error(kInvalidArg, "WKB at byte " + std::to_string(pos_) + ": " + why);
  }

  bool Byte(uint8_t* v) {
    if (size_ - pos_ < 1) return false;
    *v = data_[pos_++];
    return true;
  }

  bool U32(uint32_t* v) {
    if (size_ - pos_ < 4) return false;
    const uint8_t* b = data_ + pos_;
    *v = little_ ? (uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24)
                 : (uint32_t(b[3]) | uint32_t(b[2]) << 8 | uint32_t(b[1]) << 16 | uint32_t(b[0]) << 24);
    pos_ += 4;
    return true;
  }

  bool F64(double* v) {
    if (size_ - pos_ < 8) return false;
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
      const uint64_t byte = data_[pos_ + (little_ ? i : 7 - i)];
      bits |= byte << (8 * i);
    }
    std::memcpy(v, &bits, sizeof bits);
    pos_ += 8;
    return true;
  }

  /// A count whose items need at least `min_bytes` each must fit in what is left.
  bool Count(uint32_t* n, size_t min_bytes) {
    if (!U32(n)) return false;
    return *n <= (size_ - pos_) / min_bytes;
  }

  bool Coord(bool z, bool m, Position* p) {
    if (!F64(&p->lon) || !F64(&p->lat)) return false;
    if (z && !F64(&p->z)) return false;
    if (m && !F64(&p->m)) return false;
    return true;
  }

  Status ReadPath(bool z, bool m, size_t stride, Path* path) {
    uint32_t n = 0;
    if (!Count(&n, stride)) return Fail("bad point count");
    path->resize(n);
    for (Position& p : *path)
      if (!Coord(z, m, &p)) return Fail("truncated coordinates");
    return Status::Ok();
  }

  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
  bool little_ = true;
};

// MARK: WKB writer

void PutU32(std::vector<uint8_t>* out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out->push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void PutF64(std::vector<uint8_t>* out, double v) {
  uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof bits);
  for (int i = 0; i < 8; ++i) out->push_back(static_cast<uint8_t>(bits >> (8 * i)));
}

void PutCoord(std::vector<uint8_t>* out, const Position& p, bool z, bool m) {
  PutF64(out, p.lon);
  PutF64(out, p.lat);
  if (z) PutF64(out, p.z);
  if (m) PutF64(out, p.m);
}

void PutPath(std::vector<uint8_t>* out, const Path& path, bool z, bool m) {
  PutU32(out, static_cast<uint32_t>(path.size()));
  for (const Position& p : path) PutCoord(out, p, z, m);
}

Status Write(const Geometry& g, std::vector<uint8_t>* out) {
  if (g.IsNull()) return Status::Error(kInvalidArg, "a null geometry has no WKB");
  out->push_back(1);  // little-endian
  const uint32_t dims = g.has_z && g.has_m ? 3000 : g.has_z ? 1000 : g.has_m ? 2000 : 0;
  PutU32(out, dims + static_cast<uint32_t>(g.type));
  switch (g.type) {
    case GeometryType::kPoint: {
      Position p;
      if (g.paths.empty() || g.paths[0].empty()) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        p = Position{nan, nan, nan, nan};
      } else {
        p = g.paths[0][0];
      }
      PutCoord(out, p, g.has_z, g.has_m);
      break;
    }
    case GeometryType::kLineString:
      PutPath(out, g.paths.empty() ? Path{} : g.paths[0], g.has_z, g.has_m);
      break;
    case GeometryType::kPolygon:
      PutU32(out, static_cast<uint32_t>(g.paths.size()));
      for (const Path& ring : g.paths) PutPath(out, ring, g.has_z, g.has_m);
      break;
    default:
      PutU32(out, static_cast<uint32_t>(g.parts.size()));
      for (const Geometry& part : g.parts) {
        Status s = Write(part, out);
        if (!s.ok()) return s;
      }
      break;
  }
  return Status::Ok();
}

}  // namespace

const char* GeometryTypeName(GeometryType type) {
  const int i = static_cast<int>(type);
  return i >= 0 && i <= 7 ? kTypeNames[i] : "GEOMETRY";
}

const char* GeometryTypeTitle(GeometryType type) {
  switch (type) {
    case GeometryType::kPoint: return "Point";
    case GeometryType::kLineString: return "Line";
    case GeometryType::kPolygon: return "Area";
    case GeometryType::kMultiPoint: return "Multipoint";
    case GeometryType::kMultiLineString: return "Multiline";
    case GeometryType::kMultiPolygon: return "Multi-area";
    default: return "Any geometry";
  }
}

bool GeometryTypeFromName(const char* name, GeometryType* out) {
  if (name == nullptr) return false;
  std::string upper(name);
  for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  for (int i = 0; i <= 7; ++i) {
    if (upper == kTypeNames[i]) {
      *out = static_cast<GeometryType>(i);
      return true;
    }
  }
  return false;
}

Geometry Geometry::Point(const GeoPoint& p) {
  Geometry g;
  g.type = GeometryType::kPoint;
  g.paths.push_back({Position::At(p)});
  return g;
}

Geometry Geometry::LineString(const std::vector<GeoPoint>& points) {
  Geometry g;
  g.type = GeometryType::kLineString;
  g.paths.emplace_back();
  for (const GeoPoint& p : points) g.paths[0].push_back(Position::At(p));
  return g;
}

Geometry Geometry::Polygon(const std::vector<std::vector<GeoPoint>>& rings) {
  Geometry g;
  g.type = GeometryType::kPolygon;
  for (const std::vector<GeoPoint>& ring : rings) {
    Path path;
    for (const GeoPoint& p : ring) path.push_back(Position::At(p));
    if (!path.empty() &&
        (path.front().lat != path.back().lat || path.front().lon != path.back().lon))
      path.push_back(path.front());
    g.paths.push_back(std::move(path));
  }
  return g;
}

Geometry Geometry::Collection(GeometryType type, std::vector<Geometry> parts) {
  Geometry g;
  g.type = IsCollection(type) ? type : GeometryType::kGeometryCollection;
  g.parts = std::move(parts);
  for (const Geometry& p : g.parts) {
    g.has_z = g.has_z || p.has_z;
    g.has_m = g.has_m || p.has_m;
  }
  return g;
}

bool Geometry::IsEmpty() const { return PositionCount() == 0; }

size_t Geometry::PositionCount() const {
  size_t n = 0;
  for (const Path& p : paths) n += p.size();
  for (const Geometry& g : parts) n += g.PositionCount();
  return n;
}

bool Geometry::Envelope(GeoRect* out) const {
  bool any = false;
  GeoRect box{};
  auto take = [&](const Position& p) {
    if (!any) {
      box = GeoRect{{p.lat, p.lon}, {p.lat, p.lon}};
      any = true;
      return;
    }
    box.ll.lat = std::min(box.ll.lat, p.lat);
    box.ll.lon = std::min(box.ll.lon, p.lon);
    box.ur.lat = std::max(box.ur.lat, p.lat);
    box.ur.lon = std::max(box.ur.lon, p.lon);
  };
  for (const Path& path : paths)
    for (const Position& p : path) take(p);
  for (const Geometry& part : parts) {
    GeoRect b;
    if (!part.Envelope(&b)) continue;
    take(Position{b.ll.lat, b.ll.lon});
    take(Position{b.ur.lat, b.ur.lon});
  }
  if (any && out != nullptr) *out = box;
  return any;
}

bool Geometry::operator==(const Geometry& o) const {
  if (type != o.type || has_z != o.has_z || has_m != o.has_m) return false;
  if (paths.size() != o.paths.size() || parts != o.parts) return false;
  for (size_t i = 0; i < paths.size(); ++i) {
    if (paths[i].size() != o.paths[i].size()) return false;
    for (size_t k = 0; k < paths[i].size(); ++k) {
      const Position& a = paths[i][k];
      const Position& b = o.paths[i][k];
      if (!SameDouble(a.lat, b.lat) || !SameDouble(a.lon, b.lon)) return false;
      if (has_z && !SameDouble(a.z, b.z)) return false;
      if (has_m && !SameDouble(a.m, b.m)) return false;
    }
  }
  return true;
}

Status ReadWkb(const uint8_t* data, size_t size, Geometry* out, size_t* consumed) {
  if (data == nullptr || out == nullptr) return Status::Error(kInvalidArg, "WKB: null argument");
  WkbReader r(data, size);
  Geometry g;
  Status s = r.Read(&g, 0);
  if (!s.ok()) return s;
  if (consumed != nullptr) *consumed = r.offset();
  *out = std::move(g);
  return Status::Ok();
}

Status WriteWkb(const Geometry& geometry, std::vector<uint8_t>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "WKB: null argument");
  std::vector<uint8_t> bytes;
  Status s = Write(geometry, &bytes);
  if (!s.ok()) return s;
  *out = std::move(bytes);
  return Status::Ok();
}

}  // namespace fv
