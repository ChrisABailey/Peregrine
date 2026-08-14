// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_road_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "fv_osm_reader.h"

namespace fv {
namespace routing {
namespace {

constexpr double kEarthRadiusMeters = 6371008.8;  // WGS-84 mean radius
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr uint32_t kNoIndex = 0xFFFFFFFFu;
constexpr int32_t kNoCoord = std::numeric_limits<int32_t>::min();

const char kMagic[8] = {'F', 'V', 'R', 'O', 'A', 'D', '0', '1'};
// 1 = O4 (no turn restrictions). 2 = O5, which appends a restriction count to
// the header and a restriction table to the body. A version-1 file still
// loads and simply has no restrictions — it is a build artifact, but so is
// the hour it takes to rebuild one from a continent.
constexpr uint32_t kFormatVersion = 2;

struct ClassInfo {
  const char* tag;
  int speed_kph;
  bool driveable;
};

// Indexed by RoadClass. Speeds are the usual defaults a router assumes for
// US roads when `maxspeed` is absent — which it is on most of OSM.
const ClassInfo kClassInfo[] = {
    {"motorway", 110, true},      {"motorway_link", 70, true},
    {"trunk", 90, true},          {"trunk_link", 60, true},
    {"primary", 80, true},        {"primary_link", 50, true},
    {"secondary", 65, true},      {"secondary_link", 45, true},
    {"tertiary", 55, true},       {"tertiary_link", 40, true},
    {"unclassified", 45, true},   {"residential", 35, true},
    {"living_street", 15, true},  {"service", 20, true},
    {"track", 20, true},          {"pedestrian", 5, false},
    {"footway", 5, false},        {"path", 5, false},
    {"cycleway", 15, false},      {"steps", 2, false},
    // A ferry's speed is a fallback only: a way carrying `duration` gets the
    // speed its own crossing time implies. 20 km/h ~ 11 knots, a small
    // vehicle ferry.
    {"ferry", 20, true},
};
static_assert(sizeof(kClassInfo) / sizeof(kClassInfo[0]) ==
                  static_cast<size_t>(RoadClass::kCount),
              "kClassInfo must cover every RoadClass");

// --- little-endian scalar I/O ---------------------------------------------
// Written explicitly rather than memcpy'ing structs: the file is a build
// artifact that may be produced on one machine and read on another, and a
// struct's padding is nobody's contract.

void PutU32(std::string* buf, uint32_t v) {
  char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
               static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
  buf->append(b, 4);
}

void PutI32(std::string* buf, int32_t v) { PutU32(buf, static_cast<uint32_t>(v)); }

void PutI64(std::string* buf, int64_t v) {
  uint64_t u = static_cast<uint64_t>(v);
  PutU32(buf, static_cast<uint32_t>(u & 0xFFFFFFFFu));
  PutU32(buf, static_cast<uint32_t>(u >> 32));
}

void PutF32(std::string* buf, float v) {
  uint32_t u;
  std::memcpy(&u, &v, 4);
  PutU32(buf, u);
}

class Cursor {
 public:
  Cursor(const char* p, size_t n) : p_(p), end_(p + n) {}
  bool ok() const { return ok_; }
  size_t remaining() const { return static_cast<size_t>(end_ - p_); }

  uint32_t U32() {
    if (remaining() < 4) { ok_ = false; return 0; }
    const unsigned char* b = reinterpret_cast<const unsigned char*>(p_);
    p_ += 4;
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
  }
  int32_t I32() { return static_cast<int32_t>(U32()); }
  int64_t I64() {
    uint64_t lo = U32();
    uint64_t hi = U32();
    return static_cast<int64_t>(lo | (hi << 32));
  }
  float F32() {
    uint32_t u = U32();
    float f;
    std::memcpy(&f, &u, 4);
    return f;
  }
  bool Bytes(char* out, size_t n) {
    if (remaining() < n) { ok_ = false; return false; }
    std::memcpy(out, p_, n);
    p_ += n;
    return true;
  }

 private:
  const char* p_;
  const char* end_;
  bool ok_ = true;
};

// --- tag helpers ----------------------------------------------------------

bool IsTrue(const std::string& v) { return v == "yes" || v == "true" || v == "1"; }
bool IsFalse(const std::string& v) { return v == "no" || v == "false" || v == "0"; }

// `maxspeed` in the wild: "55 mph", "50", "none", "walk", "RU:urban".
// Returns 0 when nothing usable is there, and the caller falls back to the
// class default.
int ParseMaxSpeedKph(const std::string& v) {
  size_t i = 0;
  while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) ++i;
  size_t start = i;
  while (i < v.size() && v[i] >= '0' && v[i] <= '9') ++i;
  if (i == start) return v == "walk" ? 5 : 0;
  int value = std::atoi(v.substr(start, i - start).c_str());
  while (i < v.size() && v[i] == ' ') ++i;
  if (v.compare(i, 3, "mph") == 0) value = static_cast<int>(value * 1.609344 + 0.5);
  if (value < 1) return 0;
  return value > 255 ? 255 : value;
}

// OSM's `duration`, seconds, or 0 when there is nothing usable. The tag is
// written `hh:mm:ss` and shortened to `hh:mm` or a bare count of MINUTES —
// so "20" is twenty minutes and "0:20" is the same thing, which is the one
// trap in it. The last field may be fractional ("0:12.5").
int ParseDurationSeconds(const std::string& v) {
  double parts[3] = {0.0, 0.0, 0.0};
  int n = 0;
  size_t i = 0;
  while (i <= v.size() && n < 3) {
    size_t j = v.find(':', i);
    if (j == std::string::npos) j = v.size();
    const std::string field = v.substr(i, j - i);
    if (field.empty()) return 0;
    char* end = nullptr;
    const double value = std::strtod(field.c_str(), &end);
    if (end == field.c_str() || *end != '\0' || value < 0.0) return 0;
    parts[n++] = value;
    if (j == v.size()) break;
    i = j + 1;
    if (i > v.size()) return 0;
  }
  double seconds = 0.0;
  if (n == 1) seconds = parts[0] * 60.0;                                  // minutes
  else if (n == 2) seconds = parts[0] * 3600.0 + parts[1] * 60.0;         // h:m
  else if (n == 3) seconds = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
  else return 0;
  if (!(seconds > 0.0)) return 0;
  return static_cast<int>(seconds + 0.5);
}

enum class EdgeDir : uint8_t { kBoth, kForward, kBackward };

struct KeptWay {
  int64_t id = 0;  // needed to match a restriction relation's from/to members
  std::vector<int64_t> refs;
  uint32_t name = 0;
  RoadClass klass = RoadClass::kNone;
  uint8_t speed_kph = 0;
  EdgeDir dir = EdgeDir::kBoth;
  uint16_t access_flags = 0;
  int duration_s = 0;  // ferries only; 0 = use the class default speed
};

struct BuiltEdge {
  int64_t way_id = 0;
  uint32_t u = 0;
  uint32_t v = 0;
  uint32_t geom_begin = 0;
  uint32_t geom_count = 0;
  uint32_t name = 0;
  float length_m = 0.0f;
  RoadClass klass = RoadClass::kNone;
  uint8_t speed_kph = 0;
  EdgeDir dir = EdgeDir::kBoth;
  uint16_t access_flags = 0;  // kArcNoBicycle / kArcNoFoot / kArcNoMotorVehicle
};

// --- OSM access, per mode --------------------------------------------------
// A generic `access` sets the default and a mode key overrides it. Getting
// that precedence right is not academic: Kiawah's whole leisure-trail network
// is `access=private` + `bicycle=yes` — private to drive, explicitly open to
// ride — so treating `access` as the last word deletes every trail on the
// island from a bicycle route.
//
// `private` is its own answer, not a synonym for `no` (O5b). A private road
// exists, connects and is the only way to reach whatever is behind it; the
// router prices it rather than pretending it is not there. `no` stays a
// denial.

enum class Access { kUnset, kAllowed, kDenied, kPrivate };

Access ParseAccess(const std::string* v) {
  if (v == nullptr) return Access::kUnset;
  if (*v == "no") return Access::kDenied;
  if (*v == "private") return Access::kPrivate;
  return Access::kAllowed;  // yes, designated, permissive, destination, ...
}

// The first key that says anything wins, so `keys` runs most specific first.
Access AccessFor(const OsmWay& w, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const Access a = ParseAccess(w.Find(key));
    if (a != Access::kUnset) return a;
  }
  return Access::kUnset;
}

// --- turn restrictions, as they come off the relation ----------------------

// A `type=restriction` relation after its tags are understood but before
// anything has been resolved onto the graph — pass 1 sees relations before
// there are any graph nodes to resolve them against.
struct PendingRestriction {
  int64_t from_way = 0;
  int64_t to_way = 0;
  int64_t via_node = 0;
  TurnRestrictionKind kind = TurnRestrictionKind::kNo;
};

// True when `except` excuses a car from this restriction. OSM writes it as a
// semicolon-separated mode list ("bicycle;psv"); only the modes that cover a
// private car matter here, since that is the only profile the router applies
// restrictions to.
bool ExceptsMotorCar(const std::string& except) {
  size_t i = 0;
  while (i < except.size()) {
    size_t j = except.find(';', i);
    if (j == std::string::npos) j = except.size();
    std::string mode = except.substr(i, j - i);
    // Trim: "bicycle; psv" is as common as the canonical form.
    size_t b = mode.find_first_not_of(" \t");
    size_t e = mode.find_last_not_of(" \t");
    if (b != std::string::npos) mode = mode.substr(b, e - b + 1);
    if (mode == "motorcar" || mode == "motor_vehicle" || mode == "vehicle") return true;
    i = j + 1;
  }
  return false;
}

// Pass 1: ways and restriction relations. Collects the ways worth keeping and
// every node ID they touch (endpoints twice, so an endpoint always ends up a
// vertex). Relations come after ways in every OSM file, XML or PBF, so one
// pass sees both — and nothing here needs the ways to be resolved yet.
class WayPass : public OsmSink {
 public:
  WayPass(const RoadGraphBuildOptions& opts, std::vector<KeptWay>* ways,
          std::vector<int64_t>* ref_uses, std::vector<std::string>* names,
          std::unordered_map<std::string, uint32_t>* name_index,
          std::unordered_set<int64_t>* seen_way_ids,
          std::vector<PendingRestriction>* restrictions, RoadGraphBuildStats* stats)
      : opts_(opts), ways_(ways), ref_uses_(ref_uses), names_(names),
        name_index_(name_index), seen_way_ids_(seen_way_ids),
        restrictions_(restrictions), stats_(stats) {}

  bool WantNodes() const override { return false; }
  bool WantRelations() const override { return opts_.honor_turn_restrictions; }
  void Node(int64_t, double, double) override {}

  void Relation(const OsmRelation& r) override {
    const std::string* type = r.Find("type");
    if (type == nullptr || *type != "restriction") return;
    // `restriction:hgv` and friends are lorry-only signage and do not bind a
    // car; the plain key is the one this router honours.
    const std::string* value = r.Find("restriction");
    if (value == nullptr) return;
    ++stats_->restrictions_seen;

    PendingRestriction pending;
    if (value->compare(0, 3, "no_") == 0) {
      pending.kind = TurnRestrictionKind::kNo;
    } else if (value->compare(0, 5, "only_") == 0) {
      pending.kind = TurnRestrictionKind::kOnly;
    } else {
      return;  // not a turn restriction we understand
    }

    const std::string* except = r.Find("except");
    if (except != nullptr && ExceptsMotorCar(*except)) {
      ++stats_->restrictions_excepted;
      return;
    }

    bool via_is_way = false;
    bool have_from = false, have_to = false, have_via = false;
    for (const OsmMember& m : r.members) {
      if (m.role == "from" && m.type == OsmMemberType::kWay) {
        // A restriction with two `from` members is malformed; the first wins.
        if (!have_from) { pending.from_way = m.ref; have_from = true; }
      } else if (m.role == "to" && m.type == OsmMemberType::kWay) {
        if (!have_to) { pending.to_way = m.ref; have_to = true; }
      } else if (m.role == "via") {
        if (m.type == OsmMemberType::kWay) {
          via_is_way = true;
        } else if (m.type == OsmMemberType::kNode && !have_via) {
          pending.via_node = m.ref;
          have_via = true;
        }
      }
    }
    if (via_is_way) {
      // Recognised and deliberately not applied — see the note in the header.
      ++stats_->restrictions_via_way;
      return;
    }
    if (!have_from || !have_to || !have_via) {
      ++stats_->restrictions_unresolved;
      return;
    }
    // Nothing is added to ref_uses_ here on purpose. The via node is named by
    // both the from and the to way, so if either survives the filter above it
    // already has the two uses that make it a vertex — and if neither does,
    // forcing it to be one would split a road that nothing else splits, i.e.
    // the graph would change shape depending on whether restrictions were
    // read. Resolution below catches the case where the ways went away.
    restrictions_->push_back(pending);
  }

  void Way(const OsmWay& w) override {
    ++stats_->ways_seen;
    // `route=ferry` takes first refusal over `highway=*` (O5e). A few ferry
    // ways also carry a highway tag for the slipway at either end, and a way
    // that says it is a ferry line is a ferry line whatever else is on it.
    RoadClass klass = RoadClass::kNone;
    const std::string* route = w.Find("route");
    const bool is_ferry = opts_.include_ferries && route != nullptr && *route == "ferry";
    if (is_ferry) {
      klass = RoadClass::kFerry;
    } else {
      const std::string* highway = w.Find("highway");
      if (highway == nullptr) return;
      klass = RoadClassFromHighwayTag(*highway);
    }
    if (klass == RoadClass::kNone) return;
    uint16_t access_flags = 0;
    if (opts_.honor_access) {
      // `vehicle` is bicycle AND motor_vehicle in OSM's hierarchy, so it sits
      // between the two mode keys and the generic one for a bike, and is not
      // in foot's chain at all.
      const Access bicycle = AccessFor(w, {"bicycle", "vehicle", "access"});
      const Access foot = AccessFor(w, {"foot", "access"});
      const Access motor = AccessFor(w, {"motor_vehicle", "vehicle", "access"});
      if (bicycle == Access::kDenied) access_flags |= kArcNoBicycle;
      if (bicycle == Access::kPrivate) access_flags |= kArcPrivateBicycle;
      if (foot == Access::kDenied) access_flags |= kArcNoFoot;
      if (foot == Access::kPrivate) access_flags |= kArcPrivateFoot;
      if (motor == Access::kDenied) access_flags |= kArcNoMotorVehicle;
      if (motor == Access::kPrivate) access_flags |= kArcPrivateMotorVehicle;
      // Nobody at all may travel it: not worth a node. A way barred to only
      // SOME modes stays, carrying the bits above — that is what lets one
      // graph serve a driving, a walking and a bicycle query. Only an
      // outright `no` counts here: a bare `access=private` marks all three
      // modes private and this test does not fire, which is the whole of the
      // Kiawah fix — the way stays in the graph and costs more to use.
      const uint16_t kAllModes = kArcNoBicycle | kArcNoFoot | kArcNoMotorVehicle;
      if ((access_flags & kAllModes) == kAllModes) {
        ++stats_->ways_dropped_access;
        return;
      }
    }

    // A toll is not an access statement — everyone may use the road, it just
    // costs — so it is read whatever `honor_access` says. The mode-qualified
    // forms (`toll:hgv`, `toll:N3`) are lorry signage and are left alone.
    const std::string* toll = w.Find("toll");
    if (toll != nullptr && IsTrue(*toll)) access_flags |= kArcToll;

    if (opts_.cycle_only) {
      if (!IsCycleable(klass) || (access_flags & kArcNoBicycle) != 0) return;
    }
    // cycle_only wins over the driving filter, the same way it does in the
    // router: a bike graph that then dropped every non-driveable class would
    // throw away the cycleways and paths it exists to hold.
    if (!opts_.cycle_only && !opts_.include_non_driveable && !IsDriveable(klass)) return;

    // A `highway=pedestrian` + `area=yes` is a plaza polygon, not a line to
    // travel along; routing across one needs a mesh nobody has built.
    const std::string* area = w.Find("area");
    if (area != nullptr && IsTrue(*area)) return;

    if (w.refs.size() < 2) return;

    // Adjacent extracts overlap: the API returns a way that crosses the bbox
    // in full, so the same way ID appears in two files. Taking it twice would
    // duplicate every edge AND promote its shape points to junctions (they
    // would be "used" twice), so the graph would silently change shape with
    // the number of inputs.
    if (seen_way_ids_ != nullptr && !seen_way_ids_->insert(w.id).second) return;

    KeptWay kept;
    kept.id = w.id;
    kept.klass = klass;
    kept.refs = w.refs;
    kept.access_flags = access_flags;

    int speed = 0;
    if (klass == RoadClass::kFerry) {
      // A boat has no speed limit posted on it. What the crossing takes is
      // `duration`, and a speed cannot be had from it until the geometry is
      // resolved and the line has a length — so the duration is carried and
      // the class default stands in until then.
      ++stats_->ways_ferry;
      const std::string* duration = w.Find("duration");
      if (duration != nullptr) kept.duration_s = ParseDurationSeconds(*duration);
      if (kept.duration_s > 0) ++stats_->ferries_timed;
    } else {
      const std::string* maxspeed = w.Find("maxspeed");
      if (maxspeed != nullptr) speed = ParseMaxSpeedKph(*maxspeed);
    }
    if (speed == 0) speed = DefaultSpeedKph(klass);
    kept.speed_kph = static_cast<uint8_t>(speed);

    kept.dir = ResolveDirection(w, klass);
    kept.name = InternName(w);

    for (int64_t ref : w.refs) ref_uses_->push_back(ref);
    ref_uses_->push_back(w.refs.front());  // endpoints are always vertices
    ref_uses_->push_back(w.refs.back());

    ways_->push_back(std::move(kept));
    ++stats_->ways_kept;
  }

 private:
  EdgeDir ResolveDirection(const OsmWay& w, RoadClass klass) const {
    if (!opts_.honor_oneway) return EdgeDir::kBoth;

    bool implied = klass == RoadClass::kMotorway || klass == RoadClass::kMotorwayLink;
    const std::string* junction = w.Find("junction");
    if (junction != nullptr && (*junction == "roundabout" || *junction == "circular")) {
      implied = true;
    }

    const std::string* oneway = w.Find("oneway");
    if (oneway == nullptr) return implied ? EdgeDir::kForward : EdgeDir::kBoth;
    if (IsTrue(*oneway)) return EdgeDir::kForward;
    // `oneway=-1` is deprecated upstream and still all over the planet file.
    if (*oneway == "-1" || *oneway == "reverse") return EdgeDir::kBackward;
    // An explicit `oneway=no` beats the motorway/roundabout implication.
    if (IsFalse(*oneway)) return EdgeDir::kBoth;
    return implied ? EdgeDir::kForward : EdgeDir::kBoth;
  }

  uint32_t InternName(const OsmWay& w) const {
    const std::string* name = w.Find("name");
    if (name == nullptr) name = w.Find("ref");  // "I 26" beats nothing
    if (name == nullptr || name->empty()) return 0;
    auto it = name_index_->find(*name);
    if (it != name_index_->end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(names_->size());
    names_->push_back(*name);
    name_index_->emplace(*name, idx);
    return idx;
  }

  const RoadGraphBuildOptions& opts_;
  std::vector<KeptWay>* ways_;
  std::vector<int64_t>* ref_uses_;
  std::vector<std::string>* names_;
  std::unordered_map<std::string, uint32_t>* name_index_;
  std::unordered_set<int64_t>* seen_way_ids_;
  std::vector<PendingRestriction>* restrictions_;
  RoadGraphBuildStats* stats_;
};

// Pass 2: nodes only. Resolves coordinates for the IDs pass 1 asked for and
// nothing else — this is the whole reason the build is two passes.
class NodePass : public OsmSink {
 public:
  NodePass(const std::vector<int64_t>& needed, std::vector<int32_t>* coords)
      : needed_(needed), coords_(coords) {}

  bool WantWays() const override { return false; }
  void Way(const OsmWay&) override {}

  void Node(int64_t id, double lat, double lon) override {
    auto it = std::lower_bound(needed_.begin(), needed_.end(), id);
    if (it == needed_.end() || *it != id) return;
    size_t i = static_cast<size_t>(it - needed_.begin());
    (*coords_)[2 * i] = static_cast<int32_t>(std::lround(lat * 1e7));
    (*coords_)[2 * i + 1] = static_cast<int32_t>(std::lround(lon * 1e7));
  }

 private:
  const std::vector<int64_t>& needed_;
  std::vector<int32_t>* coords_;
};

}  // namespace

// ---------------------------------------------------------------------------
// Class tables
// ---------------------------------------------------------------------------

const char* RoadClassName(RoadClass klass) {
  if (klass >= RoadClass::kCount) return "";
  return kClassInfo[static_cast<size_t>(klass)].tag;
}

RoadClass RoadClassFromHighwayTag(const std::string& value) {
  for (size_t i = 0; i < static_cast<size_t>(RoadClass::kCount); ++i) {
    // kFerry is deliberately not reachable from a highway tag: OSM has no
    // `highway=ferry`, and letting one through here would put a boat's speed
    // and access rules on whatever mistagged way carried it.
    if (static_cast<RoadClass>(i) == RoadClass::kFerry) continue;
    if (value == kClassInfo[i].tag) return static_cast<RoadClass>(i);
  }
  // `road` is OSM's "class unsurveyed" placeholder; it is still a road.
  if (value == "road") return RoadClass::kUnclassified;
  return RoadClass::kNone;
}

RoadClass RoadClassFromName(const std::string& name) {
  if (name == "ferry") return RoadClass::kFerry;
  return RoadClassFromHighwayTag(name);
}

bool IsDriveable(RoadClass klass) {
  if (klass >= RoadClass::kCount) return false;
  return kClassInfo[static_cast<size_t>(klass)].driveable;
}

bool IsCycleable(RoadClass klass) {
  switch (klass) {
    case RoadClass::kCycleway:
    case RoadClass::kPath:
    case RoadClass::kResidential:
    case RoadClass::kLivingStreet:
    case RoadClass::kTrack:
    case RoadClass::kUnclassified:  // OSM's minor public road, not "unknown"
    case RoadClass::kTertiary:
    case RoadClass::kSecondary:
    case RoadClass::kService:
      return true;
    // Walking the bike, slowly — the cost model prices these accordingly.
    case RoadClass::kFootway:
    case RoadClass::kPedestrian:
      return true;
    // Bicycles are carried on essentially every ferry that takes anything at
    // all; the ones that do not say so with `bicycle=no`, which is on the arc.
    case RoadClass::kFerry:
      return true;
    default:
      // Motorway/trunk/primary and every _link off them: no bikes. Steps too
      // — carrying a bike up a staircase is not a route anyone asked for.
      return false;
  }
}

int DefaultSpeedKph(RoadClass klass) {
  if (klass >= RoadClass::kCount) return 0;
  return kClassInfo[static_cast<size_t>(klass)].speed_kph;
}

double GreatCircleMeters(double lat1, double lon1, double lat2, double lon2) {
  const double phi1 = lat1 * kDegToRad;
  const double phi2 = lat2 * kDegToRad;
  const double dphi = (lat2 - lat1) * kDegToRad;
  const double dlam = (lon2 - lon1) * kDegToRad;
  const double s1 = std::sin(dphi * 0.5);
  const double s2 = std::sin(dlam * 0.5);
  double a = s1 * s1 + std::cos(phi1) * std::cos(phi2) * s2 * s2;
  if (a > 1.0) a = 1.0;
  return 2.0 * kEarthRadiusMeters * std::asin(std::sqrt(a));
}

// ---------------------------------------------------------------------------
// RoadGraph
// ---------------------------------------------------------------------------

void RoadGraph::Finalize() {
  if (names_.empty()) names_.push_back(std::string());
  if (arc_begin_.size() != nodes_.size() + 1) {
    // A hand-assembled graph that forgot the sentinel is a programming
    // error, not a data error; make it loud rather than reading past the end.
    arc_begin_.resize(nodes_.size() + 1, static_cast<uint32_t>(arcs_.size()));
  }

  BuildRestrictionIndex();

  bounds_ = GeoRect{};
  grid_cols_ = grid_rows_ = 0;
  grid_begin_.clear();
  grid_items_.clear();
  if (nodes_.empty()) return;

  int32_t min_lat = nodes_[0].lat_e7, max_lat = nodes_[0].lat_e7;
  int32_t min_lon = nodes_[0].lon_e7, max_lon = nodes_[0].lon_e7;
  for (const RoadNode& n : nodes_) {
    min_lat = std::min(min_lat, n.lat_e7);
    max_lat = std::max(max_lat, n.lat_e7);
    min_lon = std::min(min_lon, n.lon_e7);
    max_lon = std::max(max_lon, n.lon_e7);
  }
  bounds_.ll = GeoPoint{min_lat * 1e-7, min_lon * 1e-7};
  bounds_.ur = GeoPoint{max_lat * 1e-7, max_lon * 1e-7};

  // NOTE: an extract that straddles the antimeridian gets a bounds spanning
  // the world and a grid with useless cells. No public extract this port
  // ships does; a Fiji-shaped one would need the split-at-180 treatment the
  // catalog already has.
  const double target_per_cell = 64.0;
  int side = static_cast<int>(std::ceil(std::sqrt(nodes_.size() / target_per_cell)));
  if (side < 1) side = 1;
  if (side > 1024) side = 1024;
  grid_cols_ = grid_rows_ = side;
  grid_lat_step_ = (bounds_.ur.lat - bounds_.ll.lat) / grid_rows_;
  grid_lon_step_ = (bounds_.ur.lon - bounds_.ll.lon) / grid_cols_;
  if (grid_lat_step_ <= 0.0) grid_lat_step_ = 1e-9;
  if (grid_lon_step_ <= 0.0) grid_lon_step_ = 1e-9;

  const size_t cells = static_cast<size_t>(grid_cols_) * grid_rows_;
  std::vector<uint32_t> counts(cells + 1, 0);
  std::vector<uint32_t> cell_of(nodes_.size());
  for (size_t i = 0; i < nodes_.size(); ++i) {
    const GeoPoint p = location(static_cast<uint32_t>(i));
    int col = static_cast<int>((p.lon - bounds_.ll.lon) / grid_lon_step_);
    int row = static_cast<int>((p.lat - bounds_.ll.lat) / grid_lat_step_);
    col = std::min(std::max(col, 0), grid_cols_ - 1);
    row = std::min(std::max(row, 0), grid_rows_ - 1);
    uint32_t c = static_cast<uint32_t>(row) * grid_cols_ + col;
    cell_of[i] = c;
    ++counts[c + 1];
  }
  for (size_t c = 0; c < cells; ++c) counts[c + 1] += counts[c];
  grid_begin_ = counts;
  grid_items_.resize(nodes_.size());
  std::vector<uint32_t> fill = counts;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    grid_items_[fill[cell_of[i]]++] = static_cast<uint32_t>(i);
  }
}

void RoadGraph::BuildRestrictionIndex() {
  restriction_at_.clear();
  state_base_.clear();
  state_node_.clear();
  state_count_ = static_cast<uint32_t>(nodes_.size());

  // A restriction naming an arc or a node the graph does not have would be an
  // out-of-bounds read in the router's hot loop; drop it here, once, rather
  // than bounds-checking every relaxation.
  restrictions_.erase(
      std::remove_if(restrictions_.begin(), restrictions_.end(),
                     [this](const TurnRestriction& r) {
                       if (r.via_node >= nodes_.size()) return true;
                       const uint32_t lo = arc_begin_[r.via_node];
                       const uint32_t hi = arc_begin_[r.via_node + 1];
                       return r.from_arc < lo || r.from_arc >= hi || r.to_arc < lo ||
                              r.to_arc >= hi;
                     }),
      restrictions_.end());
  if (restrictions_.empty()) return;

  std::sort(restrictions_.begin(), restrictions_.end(),
            [](const TurnRestriction& a, const TurnRestriction& b) {
              if (a.via_node != b.via_node) return a.via_node < b.via_node;
              if (a.from_arc != b.from_arc) return a.from_arc < b.from_arc;
              return a.to_arc < b.to_arc;
            });

  for (size_t i = 0; i < restrictions_.size();) {
    size_t j = i;
    const uint32_t via = restrictions_[i].via_node;
    while (j < restrictions_.size() && restrictions_[j].via_node == via) ++j;
    restriction_at_.emplace(via, std::make_pair(static_cast<uint32_t>(i),
                                                static_cast<uint32_t>(j)));
    // One state per arc this node can be entered along, plus one for
    // "arrived along nothing" — the state a route that starts here is in.
    const uint32_t degree = arc_begin_[via + 1] - arc_begin_[via];
    state_base_.emplace(via, state_count_);
    for (uint32_t k = 0; k <= degree; ++k) state_node_.push_back(via);
    state_count_ += degree + 1;
    i = j;
  }
}

bool RoadGraph::TurnAllowed(uint32_t via, uint32_t from_arc, uint32_t to_arc) const {
  // Neither end of a route is a turn.
  if (from_arc == kNoArc || to_arc == kNoArc) return true;
  auto it = restriction_at_.find(via);
  if (it == restriction_at_.end()) return true;

  bool only_applies = false;
  for (uint32_t i = it->second.first; i < it->second.second; ++i) {
    const TurnRestriction& r = restrictions_[i];
    if (r.from_arc != from_arc) continue;
    if (r.kind == TurnRestrictionKind::kNo) {
      if (r.to_arc == to_arc) return false;
    } else {
      // `only_*` forbids every turn off this approach except the one named.
      // A second only_* off the same approach is contradictory data; taking
      // the union (any named turn passes) is the reading that never strands
      // a driver at a junction with no legal exit.
      if (r.to_arc == to_arc) return true;
      only_applies = true;
    }
  }
  return !only_applies;
}

uint32_t RoadGraph::ArcBetween(uint32_t from_node, uint32_t to_node) const {
  for (uint32_t a = arc_begin_[from_node]; a < arc_begin_[from_node + 1]; ++a) {
    if (arcs_[a].target == to_node) return a;
  }
  return kNoArc;
}

bool RoadGraph::NearestNode(const GeoPoint& p, double max_meters, uint32_t* out_node,
                            double* out_meters) const {
  if (nodes_.empty() || grid_cols_ == 0) return false;

  int col = static_cast<int>((p.lon - bounds_.ll.lon) / grid_lon_step_);
  int row = static_cast<int>((p.lat - bounds_.ll.lat) / grid_lat_step_);
  col = std::min(std::max(col, 0), grid_cols_ - 1);
  row = std::min(std::max(row, 0), grid_rows_ - 1);

  // Metres per cell, at this latitude, taking the smaller of the two axes:
  // a point in ring r+1 is at least r cells away, which is the stopping rule.
  const double lat_cell_m = grid_lat_step_ * kDegToRad * kEarthRadiusMeters;
  const double lon_cell_m =
      grid_lon_step_ * kDegToRad * kEarthRadiusMeters * std::cos(p.lat * kDegToRad);
  double cell_m = std::min(lat_cell_m, std::fabs(lon_cell_m));
  if (cell_m < 1e-6) cell_m = 1e-6;

  double best = std::numeric_limits<double>::infinity();
  uint32_t best_node = 0;
  const int max_ring = grid_cols_ + grid_rows_;
  for (int r = 0; r <= max_ring; ++r) {
    if (std::isfinite(best) && static_cast<double>(r - 1) * cell_m > best) break;
    if (static_cast<double>(r - 1) * cell_m > max_meters) break;

    bool any_cell = false;
    for (int dr = -r; dr <= r; ++dr) {
      for (int dc = -r; dc <= r; ++dc) {
        if (r > 0 && std::abs(dr) != r && std::abs(dc) != r) continue;  // ring only
        int rr = row + dr, cc = col + dc;
        if (rr < 0 || rr >= grid_rows_ || cc < 0 || cc >= grid_cols_) continue;
        any_cell = true;
        uint32_t c = static_cast<uint32_t>(rr) * grid_cols_ + cc;
        for (uint32_t k = grid_begin_[c]; k < grid_begin_[c + 1]; ++k) {
          const uint32_t n = grid_items_[k];
          const GeoPoint q = location(n);
          const double d = GreatCircleMeters(p.lat, p.lon, q.lat, q.lon);
          if (d < best) { best = d; best_node = n; }
        }
      }
    }
    if (!any_cell && r > 0 && std::isfinite(best)) break;
  }

  if (!std::isfinite(best) || best > max_meters) return false;
  if (out_node != nullptr) *out_node = best_node;
  if (out_meters != nullptr) *out_meters = best;
  return true;
}

Status RoadGraph::Save(const std::string& path) const {
  std::string buf;
  buf.append(kMagic, sizeof(kMagic));
  PutU32(&buf, kFormatVersion);
  PutU32(&buf, 0);  // flags, reserved
  PutU32(&buf, static_cast<uint32_t>(nodes_.size()));
  PutU32(&buf, static_cast<uint32_t>(arcs_.size()));
  PutU32(&buf, static_cast<uint32_t>(geometry_.size() / 2));
  PutU32(&buf, static_cast<uint32_t>(names_.size()));
  PutU32(&buf, static_cast<uint32_t>(restrictions_.size()));  // version 2 and up

  for (const RoadNode& n : nodes_) {
    PutI32(&buf, n.lat_e7);
    PutI32(&buf, n.lon_e7);
    PutI64(&buf, n.osm_id);
  }
  for (size_t i = 0; i <= nodes_.size(); ++i) {
    PutU32(&buf, i < arc_begin_.size() ? arc_begin_[i] : static_cast<uint32_t>(arcs_.size()));
  }
  for (const RoadArc& a : arcs_) {
    PutU32(&buf, a.target);
    PutU32(&buf, a.geom_begin);
    PutU32(&buf, a.geom_count);
    PutU32(&buf, a.name);
    PutF32(&buf, a.length_m);
    // klass | flags(low 8) | speed | flags(high 8). The top byte was spare
    // through O5, so the flag bits that landed in it are silently clear in an
    // older file — no version bump, and an O4/O5 graph still loads.
    PutU32(&buf, static_cast<uint32_t>(a.klass) |
                     (static_cast<uint32_t>(a.flags & 0xFF) << 8) |
                     (static_cast<uint32_t>(a.speed_kph) << 16) |
                     (static_cast<uint32_t>((a.flags >> 8) & 0xFF) << 24));
  }
  for (int32_t v : geometry_) PutI32(&buf, v);
  for (const std::string& s : names_) {
    PutU32(&buf, static_cast<uint32_t>(s.size()));
    buf.append(s);
  }
  for (const TurnRestriction& r : restrictions_) {
    PutU32(&buf, r.via_node);
    PutU32(&buf, r.from_arc);
    PutU32(&buf, r.to_arc);
    PutU32(&buf, static_cast<uint32_t>(r.kind));
  }

  std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!out) return Status::Error(kIoError, "cannot write " + path);
  out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
  if (!out) return Status::Error(kIoError, "short write on " + path);
  return Status::Ok();
}

Status RoadGraph::Load(const std::string& path, RoadGraph* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "RoadGraph::Load: null out");
  std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
  if (!in) return Status::Error(kIoError, "cannot open " + path);
  const std::streamoff size = in.tellg();
  in.seekg(0);
  std::string buf(static_cast<size_t>(size), '\0');
  if (size > 0 && !in.read(&buf[0], size)) {
    return Status::Error(kIoError, "short read on " + path);
  }

  Cursor c(buf.data(), buf.size());
  char magic[8];
  if (!c.Bytes(magic, 8) || std::memcmp(magic, kMagic, 8) != 0) {
    return Status::Error(kInvalidArg, path + ": not an fvroad graph");
  }
  const uint32_t version = c.U32();
  if (version < 1 || version > kFormatVersion) {
    return Status::Error(kUnsupported, path + ": unsupported fvroad version");
  }
  c.U32();  // flags
  const uint32_t node_count = c.U32();
  const uint32_t arc_count = c.U32();
  const uint32_t geom_count = c.U32();
  const uint32_t name_count = c.U32();
  // Version 1 predates turn restrictions and has no count field at all.
  const uint32_t restriction_count = version >= 2 ? c.U32() : 0;
  if (!c.ok()) return Status::Error(kIoError, path + ": truncated header");

  // Reject a header whose counts cannot fit in the bytes that follow before
  // reserving gigabytes on their say-so.
  const uint64_t min_bytes = static_cast<uint64_t>(node_count) * 16 +
                             (static_cast<uint64_t>(node_count) + 1) * 4 +
                             static_cast<uint64_t>(arc_count) * 24 +
                             static_cast<uint64_t>(geom_count) * 8 +
                             static_cast<uint64_t>(name_count) * 4 +
                             static_cast<uint64_t>(restriction_count) * 16;
  if (min_bytes > c.remaining()) {
    return Status::Error(kIoError, path + ": header counts exceed file size");
  }

  RoadGraph g;
  g.nodes_.resize(node_count);
  for (RoadNode& n : g.nodes_) {
    n.lat_e7 = c.I32();
    n.lon_e7 = c.I32();
    n.osm_id = c.I64();
  }
  g.arc_begin_.resize(static_cast<size_t>(node_count) + 1);
  for (uint32_t& v : g.arc_begin_) v = c.U32();
  g.arcs_.resize(arc_count);
  for (RoadArc& a : g.arcs_) {
    a.target = c.U32();
    a.geom_begin = c.U32();
    a.geom_count = c.U32();
    a.name = c.U32();
    a.length_m = c.F32();
    const uint32_t packed = c.U32();
    a.klass = static_cast<RoadClass>(packed & 0xFF);
    a.flags = static_cast<uint16_t>(((packed >> 8) & 0xFF) | (((packed >> 24) & 0xFF) << 8));
    a.speed_kph = static_cast<uint8_t>((packed >> 16) & 0xFF);
  }
  g.geometry_.resize(static_cast<size_t>(geom_count) * 2);
  for (int32_t& v : g.geometry_) v = c.I32();
  g.names_.resize(name_count);
  for (std::string& s : g.names_) {
    const uint32_t len = c.U32();
    if (!c.ok() || len > c.remaining()) {
      return Status::Error(kIoError, path + ": truncated name table");
    }
    s.resize(len);
    if (len > 0) c.Bytes(&s[0], len);
  }
  g.restrictions_.resize(restriction_count);
  for (TurnRestriction& r : g.restrictions_) {
    r.via_node = c.U32();
    r.from_arc = c.U32();
    r.to_arc = c.U32();
    r.kind = c.U32() == 1 ? TurnRestrictionKind::kOnly : TurnRestrictionKind::kNo;
  }
  if (!c.ok()) return Status::Error(kIoError, path + ": truncated graph");

  // Structural validation: a corrupt index here is an out-of-bounds read in
  // every hot loop of the router.
  for (size_t i = 0; i + 1 < g.arc_begin_.size(); ++i) {
    if (g.arc_begin_[i] > g.arc_begin_[i + 1] || g.arc_begin_[i + 1] > arc_count) {
      return Status::Error(kIoError, path + ": arc index is not monotonic");
    }
  }
  for (const RoadArc& a : g.arcs_) {
    if (a.target >= node_count) return Status::Error(kIoError, path + ": arc target out of range");
    if (static_cast<uint64_t>(a.geom_begin) + a.geom_count > geom_count) {
      return Status::Error(kIoError, path + ": arc geometry out of range");
    }
    if (a.name >= name_count) return Status::Error(kIoError, path + ": arc name out of range");
  }

  g.Finalize();
  *out = std::move(g);
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------

Status BuildRoadGraph(const std::vector<std::string>& inputs,
                      const RoadGraphBuildOptions& options, RoadGraph* out,
                      RoadGraphBuildStats* stats) {
  if (out == nullptr) return Status::Error(kInvalidArg, "BuildRoadGraph: null out");
  if (inputs.empty()) return Status::Error(kInvalidArg, "BuildRoadGraph: no inputs");

  RoadGraphBuildStats local;
  RoadGraphBuildStats& st = (stats != nullptr) ? *stats : local;
  st = RoadGraphBuildStats{};

  std::vector<KeptWay> ways;
  std::vector<int64_t> ref_uses;
  std::vector<std::string> names(1);  // names[0] == "" == "unnamed"
  std::unordered_map<std::string, uint32_t> name_index;

  // --- pass 1: ways -------------------------------------------------------
  // The de-duplication set is only worth its memory when more than one file
  // is being merged: a single OSM file cannot contain a way twice.
  std::unordered_set<int64_t> seen_way_ids;
  std::vector<PendingRestriction> pending_restrictions;
  {
    WayPass pass(options, &ways, &ref_uses, &names, &name_index,
                 inputs.size() > 1 ? &seen_way_ids : nullptr, &pending_restrictions, &st);
    for (const std::string& path : inputs) {
      Status s = ReadOsmFile(path, &pass);
      if (!s.ok()) return s;
    }
  }
  if (ways.empty()) {
    return Status::Error(kNotFound, "BuildRoadGraph: no routable highway ways in the input");
  }

  // A node ID used twice is a junction (or a way end, counted twice above).
  std::sort(ref_uses.begin(), ref_uses.end());
  std::vector<int64_t> needed;
  std::vector<uint8_t> is_vertex;
  needed.reserve(ref_uses.size() / 2);
  for (size_t i = 0; i < ref_uses.size();) {
    size_t j = i;
    while (j < ref_uses.size() && ref_uses[j] == ref_uses[i]) ++j;
    needed.push_back(ref_uses[i]);
    is_vertex.push_back((j - i) >= 2 ? 1 : 0);
    i = j;
  }
  std::vector<int64_t>().swap(ref_uses);
  st.nodes_needed = static_cast<int64_t>(needed.size());

  // --- pass 2: nodes ------------------------------------------------------
  std::vector<int32_t> coords(needed.size() * 2, kNoCoord);
  {
    NodePass pass(needed, &coords);
    for (const std::string& path : inputs) {
      Status s = ReadOsmFile(path, &pass);
      if (!s.ok()) return s;
    }
  }
  for (size_t i = 0; i < needed.size(); ++i) {
    if (coords[2 * i] != kNoCoord) ++st.nodes_resolved;
  }

  // --- assign graph node indices to resolved vertices ---------------------
  std::vector<uint32_t> node_of(needed.size(), kNoIndex);
  RoadGraph g;
  std::vector<RoadNode>& gnodes = g.mutable_nodes();
  for (size_t i = 0; i < needed.size(); ++i) {
    if (!is_vertex[i] || coords[2 * i] == kNoCoord) continue;
    node_of[i] = static_cast<uint32_t>(gnodes.size());
    RoadNode n;
    n.lat_e7 = coords[2 * i];
    n.lon_e7 = coords[2 * i + 1];
    n.osm_id = needed[i];
    gnodes.push_back(n);
  }

  // --- cut each way into edges at its vertices ----------------------------
  std::vector<int32_t>& geom = g.mutable_geometry();
  std::vector<BuiltEdge> edges;
  std::vector<int32_t> pending;  // interior geometry of the edge in progress

  auto index_of = [&needed](int64_t id) -> size_t {
    auto it = std::lower_bound(needed.begin(), needed.end(), id);
    if (it == needed.end() || *it != id) return static_cast<size_t>(-1);
    return static_cast<size_t>(it - needed.begin());
  };

  for (const KeptWay& w : ways) {
    size_t start = static_cast<size_t>(-1);
    pending.clear();
    // O5e: a ferry's speed is its crossing time over its own length, which is
    // only knowable here. The way's edges are rewritten once it is cut, at
    // one speed for all of them — the boat does not go faster in the middle.
    // On a clipped extract the length is short of the real crossing, so the
    // implied speed is low and the ferry merely looks less attractive; that
    // is the safe direction to be wrong in.
    const size_t way_edge_begin = edges.size();
    double way_length_m = 0.0;
    for (int64_t ref : w.refs) {
      const size_t ni = index_of(ref);
      const bool resolved = ni != static_cast<size_t>(-1) && coords[2 * ni] != kNoCoord;
      if (!resolved) {
        // A clipped extract references nodes it does not contain. The edge in
        // progress runs off the map; drop it and restart past the gap.
        if (start != static_cast<size_t>(-1)) ++st.edges_dropped_unresolved;
        start = static_cast<size_t>(-1);
        pending.clear();
        continue;
      }
      if (start == static_cast<size_t>(-1)) {
        if (node_of[ni] != kNoIndex) start = ni;
        continue;
      }
      if (node_of[ni] == kNoIndex) {  // interior shape point
        pending.push_back(coords[2 * ni]);
        pending.push_back(coords[2 * ni + 1]);
        continue;
      }

      if (ni != start) {  // a zero-length self loop is not an edge
        BuiltEdge e;
        e.way_id = w.id;
        e.u = node_of[start];
        e.v = node_of[ni];
        e.geom_begin = static_cast<uint32_t>(geom.size() / 2);
        e.geom_count = static_cast<uint32_t>(pending.size() / 2);
        e.name = w.name;
        e.klass = w.klass;
        e.speed_kph = w.speed_kph;
        e.dir = w.dir;
        e.access_flags = w.access_flags;

        double len = 0.0;
        double plat = coords[2 * start] * 1e-7, plon = coords[2 * start + 1] * 1e-7;
        for (size_t k = 0; k < pending.size(); k += 2) {
          const double qlat = pending[k] * 1e-7, qlon = pending[k + 1] * 1e-7;
          len += GreatCircleMeters(plat, plon, qlat, qlon);
          plat = qlat;
          plon = qlon;
        }
        len += GreatCircleMeters(plat, plon, coords[2 * ni] * 1e-7, coords[2 * ni + 1] * 1e-7);
        e.length_m = static_cast<float>(len);
        way_length_m += len;

        geom.insert(geom.end(), pending.begin(), pending.end());
        edges.push_back(e);
        ++st.edges;
      }
      start = ni;
      pending.clear();
    }

    if (w.duration_s > 0 && way_length_m > 0.0) {
      const double kph = (way_length_m / 1000.0) / (w.duration_s / 3600.0);
      // Clamped into the byte the speed is stored in. A crossing so slow it
      // rounds to zero would divide by 1 km/h in travel_seconds(), so the
      // floor is 1 — slow, but finite and monotone in the duration.
      const int rounded = static_cast<int>(kph + 0.5);
      const uint8_t speed = static_cast<uint8_t>(rounded < 1 ? 1 : (rounded > 255 ? 255 : rounded));
      for (size_t i = way_edge_begin; i < edges.size(); ++i) edges[i].speed_kph = speed;
    }
  }

  // --- CSR ----------------------------------------------------------------
  std::vector<uint32_t>& begin = g.mutable_arc_begin();
  begin.assign(gnodes.size() + 1, 0);
  for (const BuiltEdge& e : edges) {
    ++begin[e.u + 1];
    ++begin[e.v + 1];
  }
  for (size_t i = 1; i < begin.size(); ++i) begin[i] += begin[i - 1];

  std::vector<RoadArc>& arcs = g.mutable_arcs();
  arcs.resize(edges.size() * 2);
  // Which OSM way each arc came from. Build-time only: a restriction names
  // ways, the graph knows arcs, and this is the join between them. It is not
  // persisted — nothing after resolution asks the question again.
  std::vector<int64_t> arc_way(arcs.size(), 0);
  std::vector<uint32_t> fill(begin.begin(), begin.end());
  for (const BuiltEdge& e : edges) {
    RoadArc a;
    a.geom_begin = e.geom_begin;
    a.geom_count = e.geom_count;
    a.name = e.name;
    a.length_m = e.length_m;
    a.klass = e.klass;
    a.speed_kph = e.speed_kph;

    RoadArc fwd = a;
    fwd.target = e.v;
    fwd.flags = static_cast<uint16_t>((e.dir != EdgeDir::kBackward ? kArcForward : 0) |
                                      (e.dir != EdgeDir::kForward ? kArcBackward : 0) |
                                      e.access_flags);
    arc_way[fill[e.u]] = e.way_id;
    arcs[fill[e.u]++] = fwd;

    RoadArc rev = a;
    rev.target = e.u;
    // Mirrored: from v, "forward" means v -> u, which is the way's backward.
    // Access is a property of the road, not of a direction, so it is not.
    rev.flags = static_cast<uint16_t>((e.dir != EdgeDir::kForward ? kArcForward : 0) |
                                      (e.dir != EdgeDir::kBackward ? kArcBackward : 0) |
                                      kArcGeomReversed | e.access_flags);
    arc_way[fill[e.v]] = e.way_id;
    arcs[fill[e.v]++] = rev;
  }

  // --- resolve turn restrictions onto arcs --------------------------------
  // A restriction says (from way, via node, to way). What the router needs is
  // two arcs OF THE VIA NODE: the one running back along the from way (which
  // is exactly the identity of "how I arrived") and the one taking the to
  // way. Both are found by scanning the via node's own adjacency.
  if (!pending_restrictions.empty()) {
    std::vector<TurnRestriction>& out_restrictions = g.mutable_restrictions();
    std::vector<uint32_t> from_arcs, to_arcs;
    for (const PendingRestriction& p : pending_restrictions) {
      const size_t vi = index_of(p.via_node);
      if (vi == static_cast<size_t>(-1) || node_of[vi] == kNoIndex) {
        ++st.restrictions_unresolved;
        continue;
      }
      const uint32_t v = node_of[vi];

      from_arcs.clear();
      to_arcs.clear();
      for (uint32_t a = begin[v]; a < begin[v + 1]; ++a) {
        if (arc_way[a] == p.from_way) from_arcs.push_back(a);
        if (arc_way[a] == p.to_way) to_arcs.push_back(a);
      }
      if (from_arcs.empty() || to_arcs.empty()) {
        // One of the named ways is not a routable highway, was filtered out
        // by access, or runs off the edge of a clipped extract.
        ++st.restrictions_unresolved;
        continue;
      }

      // A way that only touches the via node contributes one arc; a way that
      // runs THROUGH it contributes two, and the relation does not say which
      // side the sign is on. Taking every pair applies the restriction to
      // both approaches: for a `no_*` that is the safe reading (it can forbid
      // a turn that was legal, never allow one that was not), for an `only_*`
      // it constrains an approach the mapper may not have meant. OSM's own
      // guidance is that the from way ends at the via node; a way that does
      // not is already ambiguous data.
      const bool u_turn = p.from_way == p.to_way;
      for (uint32_t fa : from_arcs) {
        for (uint32_t ta : to_arcs) {
          // from == to is a U-turn at the via node. When the relation names
          // the same way twice that IS the restriction; when it names two
          // different ways, a pair that came out equal is a resolution
          // artifact of a way meeting the node twice, not the sign.
          if (u_turn != (fa == ta)) continue;
          TurnRestriction r;
          r.via_node = v;
          r.from_arc = fa;
          r.to_arc = ta;
          r.kind = p.kind;
          out_restrictions.push_back(r);
          ++st.restrictions_applied;
        }
      }
    }
  }

  g.mutable_names() = std::move(names);
  g.Finalize();

  // Per-mode arc counts, over the finished graph. Counted here rather than as
  // the ways go by because what a caller wants to know is what the ROUTER
  // will see, and a way becomes two arcs per segment.
  for (const RoadArc& a : g.mutable_arcs()) {
    if (!a.motor_vehicle_allowed()) ++st.arcs_no_motor_vehicle;
    if (!a.bicycle_allowed()) ++st.arcs_no_bicycle;
    if (!a.foot_allowed()) ++st.arcs_no_foot;
    if (a.motor_vehicle_private()) ++st.arcs_private_motor_vehicle;
    if (a.bicycle_private()) ++st.arcs_private_bicycle;
    if (a.foot_private()) ++st.arcs_private_foot;
    if (a.is_ferry()) ++st.arcs_ferry;
    if (a.tolled()) ++st.arcs_toll;
  }

  *out = std::move(g);
  return Status::Ok();
}

}  // namespace routing
}  // namespace fv
