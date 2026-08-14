// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_route_rules.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fv {
namespace routing {
namespace {

using Json = nlohmann::json;

// The compiled-in default. Substance-identical to
// port/Routing/rules/route-weights.json; that file is this text written out
// where a user can edit it, and is expected to drift from it as it is tuned.
//
// O5e added `"ferry": 1.0` to the two profiles that exclude what they do not
// list. That is the ONLY departure from the O5b numbers these rules
// reproduce, and it cannot change an existing answer: no graph built before
// O5e holds an arc of that class.
// Kept as JSON rather than a table of structs so that there is exactly ONE
// parser, and so the default is exercised by the same code path every load
// takes — a schema change that breaks files breaks the built-in rules too,
// loudly, in every test run.
const char* const kBuiltinRules = R"JSON({
  "version": 1,
  "default_profile": "car",
  "profiles": {
    "car": {
      "description": "Fastest driving route on posted/assumed speed limits.",
      "mode": "motor_vehicle",
      "speed": { "source": "posted" },
      "metric": "time",
      "turn_restrictions": true,
      "private_penalty": 5.0,
      "unlisted_classes": "exclude",
      "classes": {
        "motorway": 1.0, "motorway_link": 1.0,
        "trunk": 1.0, "trunk_link": 1.0,
        "primary": 1.0, "primary_link": 1.0,
        "secondary": 1.0, "secondary_link": 1.0,
        "tertiary": 1.0, "tertiary_link": 1.0,
        "unclassified": 1.0, "residential": 1.0, "living_street": 1.0,
        "service": 1.0, "track": 1.0,
        "ferry": 1.0
      }
    },
    "bicycle": {
      "description": "Cycling at a flat speed, preferring cycleways and quiet streets.",
      "mode": "bicycle",
      "speed": { "source": "fixed", "kph": 15.0 },
      "metric": "time",
      "turn_restrictions": false,
      "private_penalty": 5.0,
      "unlisted_classes": "exclude",
      "classes": {
        "cycleway": 0.50,
        "path": 0.65,
        "residential": 0.85, "living_street": 0.85,
        "track": 1.0, "unclassified": 1.0, "service": 1.0,
        "tertiary": 1.0, "secondary": 1.0,
        "footway": 1.5, "pedestrian": 1.5,
        "ferry": 1.0
      }
    },
    "foot": {
      "description": "Walking at a flat speed; anything on foot, no class preference.",
      "mode": "foot",
      "speed": { "source": "fixed", "kph": 5.0 },
      "metric": "time",
      "turn_restrictions": false,
      "private_penalty": 5.0,
      "unlisted_classes": 1.0,
      "classes": {}
    },
    "car_shortest": {
      "description": "Driving, minimising distance instead of time.",
      "extends": "car",
      "metric": "distance"
    }
  }
})JSON";

Status Fail(const std::string& origin, const std::string& what) {
  return Status::Error(kInvalidArg, origin + ": " + what);
}

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Every key each object level understands. An unknown key is an error rather
// than something skipped: the whole value of a hand-edited rule file is that a
// typo tells you, instead of quietly leaving the old weight in place.
bool KnownProfileKey(const std::string& k) {
  return k == "description" || k == "extends" || k == "mode" || k == "speed" ||
         k == "metric" || k == "turn_restrictions" || k == "private_penalty" ||
         k == "toll_penalty" || k == "ferry_penalty" ||
         k == "unlisted_classes" || k == "classes";
}

Status ParseMode(const Json& v, const std::string& where, TravelMode* out) {
  if (!v.is_string()) return Fail(where, "'mode' must be a string");
  const std::string s = v.get<std::string>();
  if (s == "motor_vehicle" || s == "car") *out = TravelMode::kMotorVehicle;
  else if (s == "bicycle") *out = TravelMode::kBicycle;
  else if (s == "foot" || s == "pedestrian") *out = TravelMode::kFoot;
  else return Fail(where, "unknown mode '" + s + "' (motor_vehicle, bicycle, foot)");
  return Status::Ok();
}

Status ParseMetric(const Json& v, const std::string& where, RouteMetric* out) {
  if (!v.is_string()) return Fail(where, "'metric' must be a string");
  const std::string s = v.get<std::string>();
  if (s == "time") *out = RouteMetric::kTime;
  else if (s == "distance") *out = RouteMetric::kDistance;
  else return Fail(where, "unknown metric '" + s + "' (time, distance)");
  return Status::Ok();
}

Status ParseSpeed(const Json& v, const std::string& where, RouteProfile* p) {
  if (!v.is_object()) return Fail(where, "'speed' must be an object");
  for (auto it = v.begin(); it != v.end(); ++it) {
    if (it.key() != "source" && it.key() != "kph")
      return Fail(where, "unknown key 'speed." + it.key() + "'");
  }
  if (!v.contains("source") || !v["source"].is_string())
    return Fail(where, "'speed' needs a string 'source' (posted, fixed)");
  const std::string src = v["source"].get<std::string>();
  if (src == "posted") {
    p->speed_source = SpeedSource::kPosted;
    return Status::Ok();
  }
  if (src != "fixed") return Fail(where, "unknown speed source '" + src + "' (posted, fixed)");
  p->speed_source = SpeedSource::kFixed;
  if (!v.contains("kph") || !v["kph"].is_number())
    return Fail(where, "speed source 'fixed' needs a numeric 'kph'");
  p->fixed_kph = v["kph"].get<double>();
  if (!(p->fixed_kph > 0.0)) return Fail(where, "'speed.kph' must be greater than zero");
  return Status::Ok();
}

// A class weight: a positive number, or `false` / "exclude" for a class this
// profile may not use. Zero and negatives are rejected — a zero-cost road is a
// free ride the search would take everywhere, which is never what was meant.
Status ParseWeight(const Json& v, const std::string& where, const std::string& what,
                   double* out) {
  if (v.is_boolean()) {
    if (v.get<bool>()) return Fail(where, what + ": `true` is not a weight; give a number");
    *out = kClassExcluded;
    return Status::Ok();
  }
  if (v.is_string()) {
    if (v.get<std::string>() != "exclude")
      return Fail(where, what + ": expected a number, false, or \"exclude\"");
    *out = kClassExcluded;
    return Status::Ok();
  }
  if (!v.is_number()) return Fail(where, what + ": expected a number, false, or \"exclude\"");
  const double w = v.get<double>();
  if (!(w > 0.0)) return Fail(where, what + ": weight must be greater than zero");
  *out = w;
  return Status::Ok();
}

Status ParseProfile(const std::string& name, const Json& obj, const std::string& origin,
                    const std::vector<RouteProfile>& done, RouteProfile* out) {
  const std::string where = origin + " profile '" + name + "'";
  if (!obj.is_object()) return Fail(where, "must be an object");
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    if (!KnownProfileKey(it.key())) return Fail(where, "unknown key '" + it.key() + "'");
  }

  RouteProfile p;
  for (size_t i = 0; i < static_cast<size_t>(RoadClass::kCount); ++i)
    p.class_weight[i] = kClassExcluded;

  // `extends` copies a base and this object overlays it. The base must already
  // be parsed, which means declaration order in the file is the resolution
  // order — no cycles are possible and none have to be detected.
  if (obj.contains("extends")) {
    if (!obj["extends"].is_string()) return Fail(where, "'extends' must be a string");
    const std::string base_name = obj["extends"].get<std::string>();
    const RouteProfile* base = nullptr;
    for (const RouteProfile& b : done) {
      if (b.name == base_name) base = &b;
    }
    if (base == nullptr)
      return Fail(where, "extends '" + base_name + "', which is not a profile defined above it");
    p = *base;
  }
  p.name = name;
  p.description.clear();

  if (obj.contains("description")) {
    if (!obj["description"].is_string()) return Fail(where, "'description' must be a string");
    p.description = obj["description"].get<std::string>();
  }
  if (obj.contains("mode")) {
    const Status s = ParseMode(obj["mode"], where, &p.mode);
    if (!s.ok()) return s;
  }
  if (obj.contains("metric")) {
    const Status s = ParseMetric(obj["metric"], where, &p.metric);
    if (!s.ok()) return s;
  }
  if (obj.contains("speed")) {
    const Status s = ParseSpeed(obj["speed"], where, &p);
    if (!s.ok()) return s;
  }
  if (obj.contains("turn_restrictions")) {
    if (!obj["turn_restrictions"].is_boolean())
      return Fail(where, "'turn_restrictions' must be true or false");
    p.turn_restrictions = obj["turn_restrictions"].get<bool>();
  }
  if (obj.contains("private_penalty")) {
    if (!obj["private_penalty"].is_number())
      return Fail(where, "'private_penalty' must be a number");
    p.private_penalty = obj["private_penalty"].get<double>();
    if (!(p.private_penalty > 0.0))
      return Fail(where, "'private_penalty' must be greater than zero (1.0 = no penalty)");
  }

  // The two avoidances (O5e) take the same grammar as a class weight — a
  // positive multiplier, or `false` / "exclude" — because they mean the same
  // two things: price it up, or take it out of the network entirely. Reusing
  // ParseWeight is also what keeps "exclude" spelled one way across the file.
  struct AvoidKey { const char* key; double* slot; };
  const AvoidKey avoid_keys[] = {{"toll_penalty", &p.toll_penalty},
                                 {"ferry_penalty", &p.ferry_penalty}};
  for (const AvoidKey& a : avoid_keys) {
    if (!obj.contains(a.key)) continue;
    double v = 1.0;
    const Status s = ParseWeight(obj[a.key], where, std::string("'") + a.key + "'", &v);
    if (!s.ok()) return s;
    *a.slot = (v == kClassExcluded) ? kAvoidExcluded : v;
  }

  // `unlisted_classes` sets every class the object does not name, so it has to
  // be applied BEFORE "classes" — and, on a profile that extends another, it
  // wipes the base's per-class weights, which is what "everything unlisted"
  // has to mean for the result to be readable off this object alone.
  if (obj.contains("unlisted_classes")) {
    double fill = kClassExcluded;
    const Status s = ParseWeight(obj["unlisted_classes"], where, "'unlisted_classes'", &fill);
    if (!s.ok()) return s;
    for (size_t i = 0; i < static_cast<size_t>(RoadClass::kCount); ++i)
      p.class_weight[i] = fill;
  }
  if (obj.contains("classes")) {
    if (!obj["classes"].is_object()) return Fail(where, "'classes' must be an object");
    for (auto it = obj["classes"].begin(); it != obj["classes"].end(); ++it) {
      // By NAME, not by highway tag: "ferry" is a class and is not a
      // `highway=*` value (O5e).
      const RoadClass klass = RoadClassFromName(it.key());
      if (klass == RoadClass::kNone)
        return Fail(where, "'classes." + it.key() + "' is not a routable road class");
      double w = 1.0;
      const Status s = ParseWeight(it.value(), where, "'classes." + it.key() + "'", &w);
      if (!s.ok()) return s;
      p.class_weight[static_cast<size_t>(klass)] = w;
    }
  }

  *out = std::move(p);
  return Status::Ok();
}

}  // namespace

double RouteProfile::Seconds(double length_m, int posted_kph) const {
  double kph = fixed_kph;
  if (speed_source == SpeedSource::kPosted) {
    kph = posted_kph > 0 ? static_cast<double>(posted_kph) : 1.0;
  }
  if (!(kph > 0.0)) kph = 1.0;
  return length_m / (kph * (1000.0 / 3600.0));
}

// ---------------------------------------------------------------------------
// RouteRules
// ---------------------------------------------------------------------------

Status RouteRules::Parse(const std::string& text, const std::string& origin,
                         std::shared_ptr<const RouteRules>* out) {
  Json root;
  try {
    // ignore_comments: the rule file is meant to be read and annotated by
    // whoever tunes it, and a weight without its reason beside it is a number
    // nobody dares change.
    root = Json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/true,
                       /*ignore_comments=*/true);
  } catch (const std::exception& e) {
    return Fail(origin, std::string("not valid JSON: ") + e.what());
  }
  if (!root.is_object()) return Fail(origin, "root is not an object");
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (it.key() != "version" && it.key() != "default_profile" && it.key() != "profiles")
      return Fail(origin, "unknown key '" + it.key() + "'");
  }
  if (!root.contains("version") || !root["version"].is_number_integer())
    return Fail(origin, "missing integer 'version'");
  const int version = root["version"].get<int>();
  if (version != 1)
    return Fail(origin, "rule format version " + std::to_string(version) + " is not 1");
  if (!root.contains("profiles") || !root["profiles"].is_object())
    return Fail(origin, "missing 'profiles' object");

  auto rules = std::make_shared<RouteRules>();
  rules->origin_ = origin;

  // nlohmann preserves no insertion order, so `extends` cannot mean "defined
  // above" over the raw object. Two passes instead: profiles without extends
  // first, then the rest, repeatedly, until nothing more resolves.
  const Json& profiles = root["profiles"];
  std::vector<std::string> pending;
  for (auto it = profiles.begin(); it != profiles.end(); ++it) pending.push_back(it.key());

  while (!pending.empty()) {
    std::vector<std::string> still_pending;
    bool progressed = false;
    for (const std::string& name : pending) {
      const Json& obj = profiles[name];
      if (obj.is_object() && obj.contains("extends") && obj["extends"].is_string()) {
        const std::string base = obj["extends"].get<std::string>();
        bool have_base = false;
        for (const RouteProfile& p : rules->profiles_) have_base |= (p.name == base);
        if (!have_base && profiles.contains(base)) {
          still_pending.push_back(name);  // base is in the file but not parsed yet
          continue;
        }
      }
      RouteProfile parsed;
      const Status s = ParseProfile(name, obj, origin, rules->profiles_, &parsed);
      if (!s.ok()) return s;
      rules->profiles_.push_back(std::move(parsed));
      progressed = true;
    }
    if (!progressed) {
      // Only reachable through an `extends` cycle: every remaining profile is
      // waiting on another that is also waiting.
      return Fail(origin, "'extends' cycle among profiles, starting at '" +
                              still_pending.front() + "'");
    }
    pending.swap(still_pending);
  }

  if (rules->profiles_.empty()) return Fail(origin, "'profiles' is empty");

  rules->default_profile_name_ = rules->profiles_.front().name;
  if (root.contains("default_profile")) {
    if (!root["default_profile"].is_string())
      return Fail(origin, "'default_profile' must be a string");
    rules->default_profile_name_ = root["default_profile"].get<std::string>();
    if (rules->Find(rules->default_profile_name_) == nullptr)
      return Fail(origin, "default_profile '" + rules->default_profile_name_ +
                              "' is not one of the profiles");
  }

  *out = std::move(rules);
  return Status::Ok();
}

Status RouteRules::Load(const std::string& path, std::shared_ptr<const RouteRules>* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot open route rules '" + path + "'");
  std::ostringstream text;
  text << in.rdbuf();
  if (!in.good() && !in.eof())
    return Status::Error(kIoError, "cannot read route rules '" + path + "'");
  return Parse(text.str(), path, out);
}

std::shared_ptr<const RouteRules> RouteRules::Builtin() {
  // Parsed once. The literal is fixed, so a failure here is a programming
  // error caught by the first test that routes anything.
  static const std::shared_ptr<const RouteRules>* const kRules = [] {
    auto* slot = new std::shared_ptr<const RouteRules>();
    const Status s = RouteRules::Parse(kBuiltinRules, "<builtin>", slot);
    if (!s.ok()) *slot = std::make_shared<RouteRules>();  // never null
    return slot;
  }();
  return *kRules;
}

const RouteProfile* RouteRules::Find(const std::string& name) const {
  for (const RouteProfile& p : profiles_) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

std::vector<std::string> RouteRules::names() const {
  std::vector<std::string> out;
  out.reserve(profiles_.size());
  for (const RouteProfile& p : profiles_) out.push_back(p.name);
  return out;
}

// ---------------------------------------------------------------------------
// RouteRulesFile
// ---------------------------------------------------------------------------

RouteRulesFile::RouteRulesFile(std::string path) : path_(std::move(path)) {
  std::lock_guard<std::mutex> lock(mutex_);
  rules_ = RouteRules::Builtin();
  ReloadLocked();
  last_poll_ms_ = NowMs();
}

bool RouteRulesFile::ReloadLocked() const {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path p(path_);
  const auto write_time = fs::last_write_time(p, ec);
  if (ec) {
    // Missing or unreadable. Say so once and keep the rules in force — a file
    // being replaced by a rename is momentarily absent, and dropping back to
    // the builtin weights mid-edit would be worse than waiting a poll.
    last_error_ = "route rules '" + path_ + "' cannot be read: " + ec.message();
    stamped_ = false;
    return false;
  }
  const auto size = fs::file_size(p, ec);
  if (ec) {
    last_error_ = "route rules '" + path_ + "' cannot be sized: " + ec.message();
    stamped_ = false;
    return false;
  }
  const int64_t mtime = write_time.time_since_epoch().count();
  if (stamped_ && mtime == stamp_mtime_ && static_cast<uint64_t>(size) == stamp_size_) {
    return false;  // unchanged since the last look
  }
  // Stamp BEFORE parsing: a file that fails to parse must not be retried on
  // every poll, only when it changes again — which is exactly what a user
  // fixing the error will do.
  stamp_mtime_ = mtime;
  stamp_size_ = static_cast<uint64_t>(size);
  stamped_ = true;

  std::shared_ptr<const RouteRules> loaded;
  const Status s = RouteRules::Load(path_, &loaded);
  if (!s.ok()) {
    last_error_ = s.message;
    return false;  // previously loaded rules stay in force
  }
  rules_ = std::move(loaded);
  ++generation_;
  last_error_.clear();
  return true;
}

std::shared_ptr<const RouteRules> RouteRulesFile::rules() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const int64_t now = NowMs();
  if (poll_interval_ms_ <= 0 || now - last_poll_ms_ >= poll_interval_ms_) {
    last_poll_ms_ = now;
    ReloadLocked();
  }
  return rules_;
}

bool RouteRulesFile::Reload() const {
  std::lock_guard<std::mutex> lock(mutex_);
  last_poll_ms_ = NowMs();
  return ReloadLocked();
}

void RouteRulesFile::set_poll_interval_ms(int ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  poll_interval_ms_ = ms;
}

uint64_t RouteRulesFile::generation() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return generation_;
}

std::string RouteRulesFile::last_error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

// ---------------------------------------------------------------------------

Status SelectProfile(const std::shared_ptr<const RouteRules>& rules, const std::string& name,
                     RouteOptions* options) {
  if (rules == nullptr || options == nullptr)
    return Status::Error(kInvalidArg, "SelectProfile: null rules or options");
  const RouteProfile* p = name.empty() ? rules->default_profile() : rules->Find(name);
  if (p == nullptr)
    return Status::Error(kNotFound, "no routing profile named '" + name + "' in " +
                                        rules->origin());
  // Aliasing constructor: the profile keeps the whole rule set alive, so a
  // reload cannot free the profile a route is running on.
  options->profile = std::shared_ptr<const RouteProfile>(rules, p);
  options->driving = p->mode == TravelMode::kMotorVehicle;
  options->cycle_only = p->mode == TravelMode::kBicycle;
  options->metric = p->metric;
  options->private_penalty = p->private_penalty;
  options->toll_penalty = p->toll_penalty;
  options->ferry_penalty = p->ferry_penalty;
  return Status::Ok();
}

}  // namespace routing
}  // namespace fv
