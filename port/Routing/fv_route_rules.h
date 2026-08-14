// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_route_rules.h — routing cost rules read from JSON, reloadable in place
// (O5c). The shipped rule file is `port/Routing/rules/route-weights.json`, and
// its header comment is the schema documentation.
//
// WHY THIS EXISTS. Through O5b the three profiles were three branches in
// Router::ArcCost: a car's clock, a bike's flat speed and its four class
// multipliers, a pedestrian's flat speed. Every one of those numbers is a
// judgement about local roads — 0.5 for a cycleway is right on Kiawah's paved
// trail network and wrong somewhere with a painted shoulder called a cycleway —
// and a judgement that has to be rebuilt to change is a judgement nobody tunes.
// So the numbers move into a file, and the file is re-read while the
// application runs.
//
// THE DEFAULT IS COMPILED IN, NOT LOADED. RouteRules::Builtin() parses an
// embedded copy of the same rules and reproduces exactly what O5b hard-coded,
// so a router with no rule file behaves as it always did and the file is a
// tuning surface rather than a dependency. The shipped
// `port/Routing/rules/route-weights.json` is that default written out where it
// can be edited — it is EXPECTED to diverge as it is tuned, so the tests check
// that it parses and defines the three core profiles, and pin the O5b numbers
// on Builtin() where they belong.
//
// RELOAD IS POLL-ON-USE, NOT A WATCHER. RouteRulesFile::rules() stats the file
// (mtime + size) at most once per `poll_interval_ms` and reparses when the
// stamp moves. No thread, no inotify/FSEvents/ReadDirectoryChangesW — three
// platform implementations of a notification whose only consumer already asks
// for the rules once per route request. A route costs milliseconds; a stat
// costs microseconds.
//
// A BAD FILE NEVER TAKES THE ROUTER DOWN. Parsing is all-or-nothing into a new
// immutable RouteRules, and only a fully parsed one is swapped in. A syntax
// error, an unknown key, a negative weight, an `extends` that names nothing —
// each leaves the previously loaded rules in force and shows up in
// last_error(). That also covers the ordinary race of reading a file mid-save:
// the truncated read fails, the rules stand, and the writer's final mtime bump
// brings the next poll back to try again.
//
// THREADING. RouteRules is immutable once parsed and handed out by
// shared_ptr — a route already running keeps the rules it started with even if
// the file changes under it, which is the property that makes a mid-flight
// reload harmless. RouteRulesFile itself is safe to call from several threads.

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fv_road_graph.h"
#include "fv_router.h"
#include "fvkit/geo.h"

namespace fv {
namespace routing {

// Which mode's access bits an arc is judged by, and which turn signage binds.
enum class TravelMode : uint8_t {
  kMotorVehicle = 0,
  kBicycle,
  kFoot,
};

// Where an arc's speed comes from. kPosted is the arc's own speed_kph — the
// `maxspeed` tag, or the class default the builder substituted. kFixed is one
// speed for the whole network, which is what a bike and a pair of legs have.
enum class SpeedSource : uint8_t {
  kPosted = 0,
  kFixed,
};

// A weight of this value means "this profile may not use this class at all".
// Distinguished from a very large weight, which merely makes a class a last
// resort: an excluded class is not in the search space, a weighted one is.
constexpr double kClassExcluded = -1.0;

// One parsed profile. Immutable; owned by the RouteRules it was parsed into.
struct RouteProfile {
  std::string name;
  std::string description;

  TravelMode mode = TravelMode::kMotorVehicle;
  SpeedSource speed_source = SpeedSource::kPosted;
  double fixed_kph = 50.0;  // used when speed_source == kFixed
  RouteMetric metric = RouteMetric::kTime;
  bool turn_restrictions = true;
  double private_penalty = 5.0;

  // The profile's DEFAULT toll and ferry preferences (O5e). SelectProfile
  // copies them onto RouteOptions, which is what the router actually reads —
  // so a caller that wants "this profile but no ferries today" selects the
  // profile and then sets the option, without a profile per combination.
  // kAvoidExcluded here is `false` / "exclude" in the file.
  double toll_penalty = 1.0;
  double ferry_penalty = 1.0;

  // Indexed by RoadClass; kClassExcluded for a class this profile bars.
  double class_weight[static_cast<size_t>(RoadClass::kCount)];

  bool allows(RoadClass klass) const {
    if (klass >= RoadClass::kCount) return false;
    return class_weight[static_cast<size_t>(klass)] != kClassExcluded;
  }

  // The multiplier for `klass`. Only meaningful when allows() is true.
  double weight(RoadClass klass) const {
    if (klass >= RoadClass::kCount) return 1.0;
    const double w = class_weight[static_cast<size_t>(klass)];
    return w == kClassExcluded ? 1.0 : w;
  }

  // Seconds to travel `length_m` metres of an arc whose posted speed is
  // `posted_kph`, under this profile's speed rule.
  double Seconds(double length_m, int posted_kph) const;
};

// A parsed rule file: every profile in it, plus which one is the default.
class RouteRules {
 public:
  // Parses JSONC text. On success `*out` holds the new rules and the previous
  // value of *out is untouched on failure — the caller's working rules survive
  // a bad edit without any extra care.
  static Status Parse(const std::string& text, const std::string& origin,
                      std::shared_ptr<const RouteRules>* out);

  static Status Load(const std::string& path, std::shared_ptr<const RouteRules>* out);

  // The O5b profiles, compiled in. Never null, never fails.
  static std::shared_ptr<const RouteRules> Builtin();

  // Null when no profile has that name.
  const RouteProfile* Find(const std::string& name) const;

  const RouteProfile* default_profile() const { return Find(default_profile_name_); }
  const std::string& default_profile_name() const { return default_profile_name_; }

  std::vector<std::string> names() const;
  const std::vector<RouteProfile>& profiles() const { return profiles_; }

  // Where these rules came from — a path, or "<builtin>". Diagnostics only.
  const std::string& origin() const { return origin_; }

 private:
  std::vector<RouteProfile> profiles_;
  std::string default_profile_name_;
  std::string origin_;
};

// A rule file watched by polling. Hand one of these to whatever owns the
// router; call rules() per request and the file's current contents are what
// answers it.
class RouteRulesFile {
 public:
  // `path` may not exist yet: the builtin rules stand until it does, and it
  // will be picked up by a later poll if it appears. An unreadable or invalid
  // file is the same case — builtin rules, and last_error() says why.
  explicit RouteRulesFile(std::string path);

  // The rules in force. Polls the file first, at most once per
  // poll_interval_ms; the returned pointer is stable for as long as the caller
  // holds it, whatever the file does next.
  std::shared_ptr<const RouteRules> rules() const;

  // Stats and reparses regardless of the poll interval. Returns true when the
  // rules changed. Call it after writing the file yourself, or from a UI's
  // "reload rules" command, where waiting out a poll interval is silly.
  bool Reload() const;

  // How often rules() is allowed to stat the file. 0 stats every call, which
  // is what the tests want and what a low-rate caller can afford.
  void set_poll_interval_ms(int ms);

  // Bumped every time a new set of rules takes effect, starting at 0 for the
  // builtin ones. A caller that caches anything derived from the rules
  // (rendered weights in a UI, a preloaded RouteOptions) compares this rather
  // than the shared_ptr.
  uint64_t generation() const;

  // Empty when the rules in force came from the file. Otherwise why they did
  // not — a parse error, a missing file — with the builtin or last-good rules
  // still answering routes.
  std::string last_error() const;

  const std::string& path() const { return path_; }

 private:
  bool ReloadLocked() const;

  const std::string path_;

  mutable std::mutex mutex_;
  mutable std::shared_ptr<const RouteRules> rules_;
  mutable uint64_t generation_ = 0;
  mutable std::string last_error_;
  mutable int poll_interval_ms_ = 500;
  mutable int64_t last_poll_ms_ = 0;   // steady clock, ms
  mutable int64_t stamp_mtime_ = 0;    // last stat'ed write time
  mutable uint64_t stamp_size_ = 0;    // ... and size, for one-second mtimes
  mutable bool stamped_ = false;
};

// Points `options` at `name` from `rules`, and mirrors the profile onto the
// pre-O5c RouteOptions fields (driving / cycle_only / metric / private_penalty)
// so anything still reading those — a UI, a CLI's own printout — sees the same
// choice. The stored profile pointer shares ownership with `rules`, so the
// rules cannot be freed under a route in flight even if the file reloads.
//
// `toll_penalty` and `ferry_penalty` are mirrored too, and for those the
// mirror is not cosmetic: the router reads them from the options, so this is
// where a profile's defaults take effect and anything set AFTER this call
// overrides them. Call SelectProfile first, then apply the query's own
// avoidances.
Status SelectProfile(const std::shared_ptr<const RouteRules>& rules, const std::string& name,
                     RouteOptions* options);

}  // namespace routing
}  // namespace fv
