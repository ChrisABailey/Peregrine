// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RouteKit/fv_route_doc.h — the `.fvrte` route document (Pippin plan, P5).
//
// The document route.py has written since the routing sessions, moved into
// C++ so that every shell can read and write it. THE FORMAT DOES NOT CHANGE:
// a route Pippin saves opens in PythonView and a route PythonView saves opens
// in Pippin, which is only true if the two writers agree BYTE FOR BYTE. That
// is a stronger claim than "both emit valid JSON" and it is the one the tests
// pin, because the moment the two disagree the divergence hides in a diff
// nobody reads until a file round-trips through both.
//
// WHY THE WRITER IS HAND-ROLLED. `json.dump(doc, f, indent=2)` is a specific
// serializer, not a generic one: two-space indent, `": "` between key and
// value, keys in INSERTION order (not sorted), non-ASCII escaped as \uXXXX,
// and floats written as Python's `repr` — the SHORTEST decimal string that
// reads back as the same double, in fixed notation for exponents in
// [-4, 16] and scientific outside it. nlohmann::json agrees on most of that
// and not on all of it (its Grisu2 dtoa is shortest for all but a handful of
// doubles, and `dump()` chooses its own fixed/scientific cut). So writing is
// done here, explicitly, against Python's rules; READING is nlohmann's job,
// where a permissive parser is exactly what is wanted.
//
// WHAT IS NOT IN THE DOCUMENT: the road geometry. It is derived from the
// graph and the rule file, either of which can change under it, so a saved
// copy would be a stale answer wearing a document's clothes. RoutePlanner
// recomputes it; `.fvrte` carries the waypoints and the intent.

#pragma once

#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"  // FvColor
#include "fvkit/geo.h"

namespace fv {

// What the on-disk document says it is, and the version this build writes.
// Bumped only when a reader has to BEHAVE differently — never for a field a
// reader can ignore. (route.py's ROUTE_FORMAT / ROUTE_VERSION.)
extern const char kRouteFormat[];  // "peregrine-route"
constexpr int kRouteVersion = 1;

// The registered overlay type id and the document extension. `.fvrte` and
// deliberately not FalconView's `.rte`: claiming that extension for a JSON
// document of our own would make the day a real `.rte` reader lands a
// migration instead of an addition.
extern const char kRouteTypeId[];     // "fv.route"
extern const char kRouteExtension[];  // "fvrte"

// One waypoint. The label is the IDENTITY — route.py selects and deletes by
// it, and two waypoints sharing one label would delete as a pair — so it is
// carried rather than derived from the index.
struct RouteWaypoint {
  std::string label;
  GeoPoint position;
};

// The whole document, and nothing else. A plain value: it has no graph, no
// projection and no overlay in it, so a tool that only wants to rewrite a
// route's name links this and none of the rest of RouteKit.
class RouteDoc {
 public:
  // --- the fields, exactly the ones the file carries -----------------------

  const std::string& name() const { return name_; }
  void set_name(std::string v) { name_ = std::move(v); }

  // route.py writes THREE channels (`list(self.color)` over a 3-tuple) and
  // reads `doc["color"][:3]`, so alpha is not part of this format. It is kept
  // opaque in memory because everything downstream draws with an FvColor.
  const FvColor& color() const { return color_; }
  void set_color(FvColor v) {
    color_ = v;
    color_.a = 255;
  }

  // The rule-file profile this route is priced with ("" = the router's
  // default). Held as a plain string and resolved at ROUTE time, never cached
  // into a parsed profile here: the rule file is polled per query, so an edit
  // lands on the next route with nothing reopened.
  const std::string& profile() const { return profile_; }
  void set_profile(std::string v) { profile_ = std::move(v); }

  const std::vector<RouteWaypoint>& waypoints() const { return waypoints_; }
  std::vector<RouteWaypoint>& waypoints() { return waypoints_; }
  void set_waypoints(std::vector<RouteWaypoint> v) { waypoints_ = std::move(v); }

  // Empties every field back to what a `RouteDoc{}` has. Named for the
  // Persistence verb it serves rather than `clear()`, because that is the only
  // caller it has.
  void Reset();

  // --- disk ----------------------------------------------------------------

  // Reads `path`. On failure the document is UNTOUCHED — a bad file does not
  // cost the caller the route they had open.
  Status Read(const std::string& path);

  // The same from text already in hand (a bundle resource, a test).
  // `origin` names it in error messages only.
  Status Parse(const std::string& text, const std::string& origin);

  Status Write(const std::string& path) const;

  // The exact bytes `Write` would put on disk, trailing newline and all.
  // Exposed because byte-compatibility with route.py is a property of THIS
  // string, and a test that has to write a file to check it is a test about
  // the filesystem.
  std::string ToJson() const;

 private:
  std::string name_;
  FvColor color_{220, 30, 30, 255};  // route.py's default red
  std::string profile_;
  std::vector<RouteWaypoint> waypoints_;
};

// Python's `repr(float)` for a double, which is what `json.dump` writes.
// Public because it is the whole of the byte-compatibility claim and deserves
// to be tested directly rather than through a document.
//
// Shortest decimal that round-trips; fixed notation when the decimal point
// falls in (-4, 16] and scientific otherwise; always at least one digit after
// the point in fixed notation ("32.0", never "32"). Non-finite doubles are
// NOT valid JSON and are written the way Python writes them anyway
// ("NaN", "Infinity", "-Infinity"), because silently emitting `null` would
// turn a broken coordinate into a plausible one.
std::string PythonFloatRepr(double v);

}  // namespace fv
