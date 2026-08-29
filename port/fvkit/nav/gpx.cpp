// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/gpx.h"

#include <expat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

#include "fvkit/nav/nmea.h"  // UtcToEpochSeconds / EpochSecondsToUtc
#include "geo_tool.h"        // fv_geo_tool: great-circle range and bearing

namespace fv {
namespace {

// GPX is namespaced and every real file uses a prefix for its extensions
// (`gpxtpx:hr`) while leaving the GPX elements themselves in the default
// namespace. Matching on the LOCAL name handles both without a namespace-aware
// parser, and it is also what makes a file that prefixes the GPX elements
// themselves (legal, and rare) read the same.
std::string LocalName(const char* qualified) {
  const char* colon = std::strrchr(qualified, ':');
  return colon != nullptr ? std::string(colon + 1) : std::string(qualified);
}

bool ParseDouble(const std::string& text, double* out) {
  if (text.empty()) return false;
  const char* begin = text.c_str();
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin) return false;
  if (out != nullptr) *out = value;
  return true;
}

bool ParseInt(const std::string& text, int* out) {
  if (text.empty()) return false;
  const char* begin = text.c_str();
  char* end = nullptr;
  const long value = std::strtol(begin, &end, 10);
  if (end == begin) return false;
  if (out != nullptr) *out = static_cast<int>(value);
  return true;
}

std::string Trim(const std::string& text) {
  const std::size_t begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return std::string();
  const std::size_t end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

const char* FindAttribute(const char** attributes, const char* name) {
  for (int i = 0; attributes != nullptr && attributes[i] != nullptr; i += 2) {
    if (LocalName(attributes[i]) == name) return attributes[i + 1];
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// The parse
// ---------------------------------------------------------------------------

// Which kind of point the parser is inside, if any. <wpt>, <trkpt> and <rtept>
// carry the same children, so one accumulator serves all three and only the
// destination differs.
enum class PointKind { kNone, kWaypoint, kTrackPoint, kRoutePoint };

struct ParseState {
  GpxDocument* document = nullptr;
  XML_Parser parser = nullptr;  // so a handler can abort the parse
  std::vector<std::string> stack;  // local names, outermost first
  std::string text;                // character data of the element being closed

  PointKind point_kind = PointKind::kNone;
  PositionFix point;
  std::string point_name;
  std::string point_desc;
  bool point_has_position = false;

  bool in_metadata = false;
  GpxTrack* current_track = nullptr;  // points into document->tracks or ->routes

  std::string fatal;  // set by a handler that wants the parse abandoned

  const std::string& parent() const {
    static const std::string kNone;
    return stack.size() >= 2 ? stack[stack.size() - 2] : kNone;
  }
};

void StartPoint(ParseState* state, PointKind kind, const char** attributes) {
  state->point_kind = kind;
  state->point = PositionFix{};
  state->point_name.clear();
  state->point_desc.clear();
  state->point_has_position = false;

  const char* lat = FindAttribute(attributes, "lat");
  const char* lon = FindAttribute(attributes, "lon");
  double lat_value = 0.0;
  double lon_value = 0.0;
  if (lat != nullptr && lon != nullptr && ParseDouble(lat, &lat_value) &&
      ParseDouble(lon, &lon_value)) {
    // A point outside the graticule is a corrupt file, not a place; it is
    // dropped rather than clamped.
    if (lat_value >= -90.0 && lat_value <= 90.0 && lon_value >= -180.0 && lon_value <= 180.0) {
      state->point.SetPosition(lat_value, lon_value);
      state->point_has_position = true;
    }
  }
}

void FinishPoint(ParseState* state) {
  if (state->point_has_position) {
    switch (state->point_kind) {
      case PointKind::kWaypoint:
        state->document->waypoints.push_back(state->point);
        state->document->waypoint_names.push_back(state->point_name);
        state->document->waypoint_descriptions.push_back(state->point_desc);
        break;
      case PointKind::kTrackPoint:
      case PointKind::kRoutePoint:
        if (state->current_track != nullptr) {
          if (state->current_track->segments.empty()) {
            // A <trkpt> outside any <trkseg> is malformed but common enough in
            // hand-written files; give it a segment rather than losing it.
            state->current_track->segments.emplace_back();
          }
          state->current_track->segments.back().points.push_back(state->point);
        }
        break;
      case PointKind::kNone:
        break;
    }
  }
  state->point_kind = PointKind::kNone;
  state->point_has_position = false;
}

void XMLCALL OnStartElement(void* user_data, const XML_Char* name, const XML_Char** attributes) {
  ParseState* state = static_cast<ParseState*>(user_data);
  const std::string local = LocalName(name);
  state->stack.push_back(local);
  state->text.clear();

  if (local == "gpx") {
    const char* creator = FindAttribute(attributes, "creator");
    const char* version = FindAttribute(attributes, "version");
    if (creator != nullptr) state->document->creator = creator;
    if (version != nullptr) state->document->version = version;
  } else if (local == "metadata") {
    state->in_metadata = true;
  } else if (local == "trk") {
    state->document->tracks.emplace_back();
    state->current_track = &state->document->tracks.back();
  } else if (local == "rte") {
    state->document->routes.emplace_back();
    state->current_track = &state->document->routes.back();
    // A route has no <rtept> container of its own; give it the one segment it
    // conceptually is, so a route and a track read the same downstream.
    state->current_track->segments.emplace_back();
  } else if (local == "trkseg") {
    if (state->current_track != nullptr) state->current_track->segments.emplace_back();
  } else if (local == "wpt") {
    StartPoint(state, PointKind::kWaypoint, attributes);
  } else if (local == "trkpt") {
    StartPoint(state, PointKind::kTrackPoint, attributes);
  } else if (local == "rtept") {
    StartPoint(state, PointKind::kRoutePoint, attributes);
  }
}

void XMLCALL OnEndElement(void* user_data, const XML_Char* name) {
  ParseState* state = static_cast<ParseState*>(user_data);
  const std::string local = LocalName(name);
  const std::string text = Trim(state->text);
  const std::string& parent = state->parent();

  if (state->point_kind != PointKind::kNone) {
    // Inside a point. Everything here is one of its children, whether it sits
    // directly under it or inside its <extensions>.
    if (local == "ele") {
      double value = 0.0;
      if (ParseDouble(text, &value)) {
        // Rule 3: <ele> is read as height above mean sea level.
        state->point.altitude_msl_m = value;
        state->point.has_altitude = true;
      }
    } else if (local == "time") {
      double epoch = 0.0;
      if (ParseIso8601Utc(text, &epoch)) {
        state->point.time_s = epoch;
        state->point.has_time = true;
      }
    } else if (local == "hdop") {
      double value = 0.0;
      if (ParseDouble(text, &value)) {
        state->point.hdop = value;
        state->point.has_hdop = true;
      }
    } else if (local == "sat") {
      int value = 0;
      if (ParseInt(text, &value)) {
        state->point.satellite_count = value;
        state->point.has_satellite_count = true;
      }
    } else if (local == "speed") {
      // GPX 1.0's <speed> and Garmin's <gpxtpx:speed> are both metres per
      // second, which is already FvKit's unit.
      double value = 0.0;
      if (ParseDouble(text, &value)) {
        state->point.speed_mps = value;
        state->point.has_speed = true;
      }
    } else if (local == "course") {
      double value = 0.0;
      if (ParseDouble(text, &value)) {
        state->point.true_heading_deg = NormalizeHeadingDeg(value);
        state->point.has_true_heading = true;
      }
    } else if (local == "magvar") {
      double value = 0.0;
      if (ParseDouble(text, &value) && state->point.has_true_heading) {
        // GPX's <magvar> is degrees of easterly variation, so the magnetic
        // course is the true one minus it — the same sign convention RMC's
        // 'E' branch uses (nmea.h, Q3).
        state->point.magnetic_heading_deg =
            NormalizeHeadingDeg(state->point.true_heading_deg - value);
        state->point.has_magnetic_heading = true;
      }
    } else if (local == "name") {
      state->point_name = text;
    } else if (local == "desc") {
      // Kept for a WAYPOINT only (see `GpxDocument::waypoint_descriptions`);
      // FinishPoint ignores it for a track or route point.
      state->point_desc = text;
    }
    // Anything else inside a point — <gpxtpx:hr>, <sym>, <cmt> — has nowhere
    // to live on a PositionFix and is deliberately dropped.

    if (local == "wpt" || local == "trkpt" || local == "rtept") FinishPoint(state);
  } else if (local == "name") {
    if (state->in_metadata || parent == "gpx") {
      // GPX 1.1 puts the document name in <metadata>; 1.0 puts it directly
      // under <gpx>.
      state->document->name = text;
    } else if ((parent == "trk" || parent == "rte") && state->current_track != nullptr) {
      state->current_track->name = text;
    }
  } else if (local == "type" && state->current_track != nullptr &&
             (parent == "trk" || parent == "rte")) {
    state->current_track->type = text;
  } else if (local == "time" && state->in_metadata) {
    double epoch = 0.0;
    if (ParseIso8601Utc(text, &epoch)) {
      state->document->time_s = epoch;
      state->document->has_time = true;
    }
  }

  if (local == "metadata") state->in_metadata = false;
  if (local == "trk" || local == "rte") state->current_track = nullptr;

  if (!state->stack.empty()) state->stack.pop_back();
  state->text.clear();
}

void XMLCALL OnCharacterData(void* user_data, const XML_Char* s, int length) {
  ParseState* state = static_cast<ParseState*>(user_data);
  // Bound the accumulator: a file with one megabyte of text in a <desc> is not
  // something to hold in memory on the way to discarding it.
  constexpr std::size_t kMaxTextLength = 64 * 1024;
  if (state->text.size() >= kMaxTextLength) return;
  state->text.append(s, static_cast<std::size_t>(length));
}

// A GPX file is something a user downloaded, so the parser refuses entity
// declarations outright rather than relying on an expansion limit. There is no
// legitimate GPX that declares one. (expat 2.8 also defends itself, but this
// is the port's first reader of a file from the open internet and the refusal
// is worth being explicit about.)
void XMLCALL OnEntityDecl(void* user_data, const XML_Char* /*entity_name*/, int /*is_parameter*/,
                          const XML_Char* /*value*/, int /*value_length*/, const XML_Char* /*base*/,
                          const XML_Char* /*system_id*/, const XML_Char* /*public_id*/,
                          const XML_Char* /*notation*/) {
  ParseState* state = static_cast<ParseState*>(user_data);
  state->fatal = "GPX: entity declarations are not accepted";
  // Stop AT the declaration. Recording the refusal and letting the parse run
  // on would still pay for whatever expansion expat's own amplification limit
  // permits, which is the thing this handler exists to avoid.
  XML_StopParser(state->parser, /*resumable=*/XML_FALSE);
}

// ---------------------------------------------------------------------------
// Post-processing
// ---------------------------------------------------------------------------

bool RangeAndBearing(const PositionFix& a, const PositionFix& b, double* metres, double* bearing) {
  double range = 0.0;
  double brg = 0.0;
  if (GEO_calc_range_and_bearing(a.lat, a.lon, b.lat, b.lon, &range, &brg, TRUE) != SUCCESS) {
    return false;
  }
  if (metres != nullptr) *metres = range;
  if (bearing != nullptr) *bearing = brg;
  return true;
}

// Applies drop_non_monotonic_time and split_gap_s, returning the segments that
// survive. Done as a rebuild rather than in place because a split turns one
// segment into several.
std::vector<GpxTrackSegment> CleanSegments(const std::vector<GpxTrackSegment>& segments,
                                           const GpxReadOptions& options) {
  std::vector<GpxTrackSegment> out;
  for (const GpxTrackSegment& segment : segments) {
    GpxTrackSegment current;
    for (const PositionFix& point : segment.points) {
      if (!current.points.empty() && current.points.back().has_time && point.has_time) {
        const double dt = point.time_s - current.points.back().time_s;
        if (options.drop_non_monotonic_time && dt <= 0.0) continue;
        if (options.split_gap_s > 0.0 && dt > options.split_gap_s) {
          out.push_back(std::move(current));
          current = GpxTrackSegment{};
        }
      }
      current.points.push_back(point);
    }
    if (!current.points.empty()) out.push_back(std::move(current));
  }
  return out;
}

void DeriveMotion(GpxTrackSegment* segment, const GpxReadOptions& options) {
  std::vector<PositionFix>& points = segment->points;
  for (std::size_t i = 0; i < points.size(); ++i) {
    // Speed from the PREVIOUS point: it is an average over the interval that
    // just ended, which is what a fix's own speed field means.
    if (options.derive_speed && !points[i].has_speed && i > 0 && points[i].has_time &&
        points[i - 1].has_time) {
      const double dt = points[i].time_s - points[i - 1].time_s;
      double metres = 0.0;
      if (dt > 0.0 && RangeAndBearing(points[i - 1], points[i], &metres, nullptr)) {
        points[i].speed_mps = metres / dt;
        points[i].has_speed = true;
      }
    }
    // Heading toward the NEXT point: the direction the ship is about to go,
    // which is what a course over ground is. Off by default (rule 2).
    if (options.derive_true_heading && !points[i].has_true_heading && i + 1 < points.size()) {
      double bearing = 0.0;
      if (RangeAndBearing(points[i], points[i + 1], nullptr, &bearing)) {
        points[i].true_heading_deg = NormalizeHeadingDeg(bearing);
        points[i].has_true_heading = true;
      }
    }
  }
  // The last point has no next one; carry the heading forward rather than
  // leaving one point of a track pointing north.
  if (options.derive_true_heading && points.size() >= 2) {
    PositionFix& last = points.back();
    const PositionFix& previous = points[points.size() - 2];
    if (!last.has_true_heading && previous.has_true_heading) {
      last.true_heading_deg = previous.true_heading_deg;
      last.has_true_heading = true;
    }
  }
}

void PostProcess(GpxDocument* document, const GpxReadOptions& options) {
  for (std::vector<GpxTrack>* group : {&document->tracks, &document->routes}) {
    for (GpxTrack& track : *group) {
      track.segments = CleanSegments(track.segments, options);
      for (GpxTrackSegment& segment : track.segments) {
        DeriveMotion(&segment, options);
      }
      // Drop segments a <trkseg> declared and never filled.
      std::vector<GpxTrackSegment> kept;
      for (GpxTrackSegment& segment : track.segments) {
        if (!segment.points.empty()) kept.push_back(std::move(segment));
      }
      track.segments = std::move(kept);
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// GpxTrack / GpxDocument
// ---------------------------------------------------------------------------

std::size_t GpxTrack::point_count() const {
  std::size_t total = 0;
  for (const GpxTrackSegment& segment : segments) total += segment.points.size();
  return total;
}

std::size_t GpxDocument::track_point_count() const {
  std::size_t total = 0;
  for (const GpxTrack& track : tracks) total += track.point_count();
  return total;
}

const GpxTrack* GpxDocument::longest_track() const {
  const GpxTrack* best = nullptr;
  std::size_t best_count = 0;
  for (const GpxTrack& track : tracks) {
    const std::size_t count = track.point_count();
    if (best == nullptr || count > best_count) {
      best = &track;
      best_count = count;
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// ISO 8601
// ---------------------------------------------------------------------------

bool ParseIso8601Utc(const std::string& text, double* epoch_seconds) {
  // YYYY-MM-DDThh:mm:ss[.sss][Z|±hh:mm|±hhmm]. A GPX stamp with no zone marker
  // is read as UTC, which the schema requires it to be.
  if (text.size() < 19) return false;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int consumed = 0;
  if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%n", &year, &month, &day, &hour, &minute,
                  &consumed) != 5 ||
      consumed == 0) {
    return false;
  }
  char* end = nullptr;
  const char* seconds_begin = text.c_str() + consumed;
  const double second = std::strtod(seconds_begin, &end);
  if (end == seconds_begin) return false;
  if (month < 1 || month > 12 || day < 1 || day > 31) return false;

  double offset_s = 0.0;
  while (*end == ' ') ++end;
  if (*end == '+' || *end == '-') {
    const int sign = (*end == '-') ? -1 : 1;
    int offset_hour = 0;
    int offset_minute = 0;
    if (std::sscanf(end + 1, "%2d:%2d", &offset_hour, &offset_minute) != 2 &&
        std::sscanf(end + 1, "%2d%2d", &offset_hour, &offset_minute) != 2) {
      return false;
    }
    offset_s = sign * (offset_hour * 3600.0 + offset_minute * 60.0);
  }

  const double utc =
      UtcToEpochSeconds(year, month, day, hour * 3600.0 + minute * 60.0 + second) - offset_s;
  if (epoch_seconds != nullptr) *epoch_seconds = utc;
  return true;
}

std::string FormatIso8601Utc(double epoch_seconds) {
  int year = 0;
  int month = 0;
  int day = 0;
  double seconds_of_day = 0.0;
  EpochSecondsToUtc(epoch_seconds, &year, &month, &day, &seconds_of_day);
  const int whole = static_cast<int>(seconds_of_day);
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day,
                whole / 3600, (whole / 60) % 60, whole % 60);
  return buffer;
}

std::string FormatIso8601UtcFractional(double epoch_seconds, int fractional_digits) {
  if (fractional_digits <= 0) return FormatIso8601Utc(epoch_seconds);
  if (fractional_digits > 3) fractional_digits = 3;

  // Round to the requested precision FIRST, then split. Splitting first and
  // rounding the fraction can carry into a whole second that the date part no
  // longer agrees with — 23:59:59.9996 becoming "…T23:59:60.000Z" on the wrong
  // day, which is the classic form of this bug.
  const double quantum = (fractional_digits == 1) ? 0.1 : (fractional_digits == 2 ? 0.01 : 0.001);
  const double rounded = std::floor(epoch_seconds / quantum + 0.5) * quantum;

  int year = 0;
  int month = 0;
  int day = 0;
  double seconds_of_day = 0.0;
  EpochSecondsToUtc(rounded, &year, &month, &day, &seconds_of_day);
  const int whole = static_cast<int>(seconds_of_day);
  double fraction = seconds_of_day - whole;
  if (fraction < 0.0) fraction = 0.0;

  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%0*dZ", year, month, day,
                whole / 3600, (whole / 60) % 60, whole % 60, fractional_digits,
                static_cast<int>(fraction / quantum + 0.5));
  return buffer;
}

// ---------------------------------------------------------------------------
// ParseGpx / ReadGpxFile
// ---------------------------------------------------------------------------

Status ParseGpx(const std::string& xml, GpxDocument* out, const GpxReadOptions& options) {
  if (out == nullptr) return Status::Error(kInvalidArg, "ParseGpx: null out");
  *out = GpxDocument{};

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (parser == nullptr) return Status::Error(kInternal, "ParseGpx: XML_ParserCreate failed");

  ParseState state;
  state.document = out;
  state.parser = parser;
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, OnStartElement, OnEndElement);
  XML_SetCharacterDataHandler(parser, OnCharacterData);
  XML_SetEntityDeclHandler(parser, OnEntityDecl);

  const XML_Status status =
      XML_Parse(parser, xml.data(), static_cast<int>(xml.size()), /*isFinal=*/1);
  Status result = Status::Ok();
  if (!state.fatal.empty()) {
    result = Status::Error(kInvalidArg, state.fatal);
  } else if (status == XML_STATUS_ERROR) {
    std::ostringstream message;
    message << "GPX: " << XML_ErrorString(XML_GetErrorCode(parser)) << " at line "
            << XML_GetCurrentLineNumber(parser) << ", column " << XML_GetCurrentColumnNumber(parser);
    result = Status::Error(kInvalidArg, message.str());
  }
  XML_ParserFree(parser);
  if (!result.ok()) {
    *out = GpxDocument{};
    return result;
  }

  PostProcess(out, options);
  return Status::Ok();
}

Status ReadGpxFile(const std::string& path, GpxDocument* out, const GpxReadOptions& options) {
  if (out == nullptr) return Status::Error(kInvalidArg, "ReadGpxFile: null out");
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return Status::Error(kNotFound, "ReadGpxFile: cannot open " + path);
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  return ParseGpx(contents.str(), out, options);
}

// ---------------------------------------------------------------------------
// Getting at the points
// ---------------------------------------------------------------------------

std::vector<PositionFix> FlattenGpxFixes(const GpxDocument& document) {
  std::vector<PositionFix> fixes;
  fixes.reserve(document.track_point_count());
  for (const GpxTrack& track : document.tracks) {
    for (const GpxTrackSegment& segment : track.segments) {
      fixes.insert(fixes.end(), segment.points.begin(), segment.points.end());
    }
  }
  return fixes;
}

std::vector<GeoPoint> GpxSegmentPath(const GpxTrackSegment& segment) {
  std::vector<GeoPoint> path;
  path.reserve(segment.points.size());
  for (const PositionFix& point : segment.points) {
    path.push_back(point.position());
  }
  return path;
}


// ---------------------------------------------------------------------------
// Writing (P10)
// ---------------------------------------------------------------------------

namespace {

// The five XML predefined entities. An element's text and an attribute's value
// need different subsets in principle; one escaper doing all five is correct
// for both and is one function to be right about.
void AppendEscaped(const std::string& text, std::string* out) {
  for (const char c : text) {
    switch (c) {
      case '&': out->append("&amp;"); break;
      case '<': out->append("&lt;"); break;
      case '>': out->append("&gt;"); break;
      case '"': out->append("&quot;"); break;
      case '\'': out->append("&apos;"); break;
      default: out->push_back(c); break;
    }
  }
}

std::string FormatFixed(double value, int decimals) {
  if (decimals < 0) decimals = 0;
  if (decimals > 12) decimals = 12;
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  return buffer;
}

// -1 (auto) writes three digits only for a stamp that actually carries a
// fraction. The 0.5 ms window is the format's own precision, not a guess: a
// stamp inside it prints the same either way.
std::string FormatPointTime(double epoch_seconds, int time_decimals) {
  if (time_decimals >= 0) return FormatIso8601UtcFractional(epoch_seconds, time_decimals);
  const double fraction = epoch_seconds - std::floor(epoch_seconds);
  const bool whole = fraction < 0.0005 || fraction > 0.9995;
  return whole ? FormatIso8601Utc(epoch_seconds) : FormatIso8601UtcFractional(epoch_seconds, 3);
}

void AppendIndent(int level, bool pretty, std::string* out) {
  if (!pretty) return;
  out->append(static_cast<std::size_t>(level) * 2, ' ');
}

void AppendTextElement(const std::string& tag, const std::string& text, int level,
                       const GpxWriteOptions& options, std::string* out) {
  if (text.empty()) return;
  AppendIndent(level, options.pretty, out);
  out->push_back('<');
  out->append(tag);
  out->push_back('>');
  AppendEscaped(text, out);
  out->append("</");
  out->append(tag);
  out->append(">\n");
}

// One <trkpt>/<rtept>/<wpt>, children and all. `name` is the waypoint case;
// a track point has none.
void AppendPoint(const char* tag, const PositionFix& fix, const std::string& name,
                 const std::string& description, int level, const GpxWriteOptions& options,
                 std::string* out) {
  AppendIndent(level, options.pretty, out);
  out->push_back('<');
  out->append(tag);
  out->append(" lat=\"");
  out->append(FormatFixed(fix.lat, options.coordinate_decimals));
  out->append("\" lon=\"");
  out->append(FormatFixed(fix.lon, options.coordinate_decimals));
  out->push_back('"');

  const bool has_children =
      fix.has_altitude || fix.has_time || !name.empty() || !description.empty();
  if (!has_children) {
    out->append("/>\n");
    return;
  }
  out->append(">\n");

  // GPX 1.1's sequence is fixed: <ele>, <time>, then the rest. A file with
  // them out of order fails schema validation in the tools that check.
  if (fix.has_altitude) {
    AppendTextElement("ele", FormatFixed(fix.altitude_msl_m, options.elevation_decimals), level + 1,
                      options, out);
  }
  if (fix.has_time) {
    AppendTextElement("time", FormatPointTime(fix.time_s, options.time_decimals), level + 1, options,
                      out);
  }
  AppendTextElement("name", name, level + 1, options, out);
  AppendTextElement("desc", description, level + 1, options, out);

  AppendIndent(level, options.pretty, out);
  out->append("</");
  out->append(tag);
  out->append(">\n");
}

void AppendTrack(const GpxTrack& track, const char* track_tag, const char* point_tag,
                 bool with_segments, const GpxWriteOptions& options, std::string* out) {
  AppendIndent(1, options.pretty, out);
  out->push_back('<');
  out->append(track_tag);
  out->append(">\n");
  AppendTextElement("name", track.name, 2, options, out);
  AppendTextElement("type", track.type, 2, options, out);
  for (const GpxTrackSegment& segment : track.segments) {
    if (with_segments) {
      AppendIndent(2, options.pretty, out);
      out->append("<trkseg>\n");
    }
    for (const PositionFix& point : segment.points) {
      AppendPoint(point_tag, point, /*name=*/std::string(), /*description=*/std::string(),
                  with_segments ? 3 : 2, options, out);
    }
    if (with_segments) {
      AppendIndent(2, options.pretty, out);
      out->append("</trkseg>\n");
    }
  }
  AppendIndent(1, options.pretty, out);
  out->append("</");
  out->append(track_tag);
  out->append(">\n");
}

}  // namespace

std::string FormatGpxTrackPoint(const PositionFix& fix, const GpxWriteOptions& options,
                                int indent_level) {
  std::string out;
  AppendPoint("trkpt", fix, /*name=*/std::string(), /*description=*/std::string(),
              indent_level, options, &out);
  return out;
}

std::string WriteGpx(const GpxDocument& document, const GpxWriteOptions& options) {
  std::string out;
  out.reserve(64 * (document.track_point_count() + 16));

  out.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  out.append("<gpx version=\"1.1\" creator=\"");
  AppendEscaped(options.creator.empty() ? document.creator : options.creator, &out);
  out.append(
      "\"\n     xmlns=\"http://www.topografix.com/GPX/1/1\""
      "\n     xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "\n     xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1"
      " http://www.topografix.com/GPX/1/1/gpx.xsd\">\n");

  const bool want_metadata =
      !document.name.empty() || (options.write_metadata_time && document.has_time);
  if (want_metadata) {
    AppendIndent(1, options.pretty, &out);
    out.append("<metadata>\n");
    AppendTextElement("name", document.name, 2, options, &out);
    if (options.write_metadata_time && document.has_time) {
      AppendTextElement("time", FormatPointTime(document.time_s, options.time_decimals), 2, options,
                        &out);
    }
    AppendIndent(1, options.pretty, &out);
    out.append("</metadata>\n");
  }

  for (std::size_t i = 0; i < document.waypoints.size(); ++i) {
    const std::string name =
        i < document.waypoint_names.size() ? document.waypoint_names[i] : std::string();
    const std::string description = i < document.waypoint_descriptions.size()
                                        ? document.waypoint_descriptions[i]
                                        : std::string();
    AppendPoint("wpt", document.waypoints[i], name, description, 1, options, &out);
  }
  for (const GpxTrack& track : document.tracks) {
    AppendTrack(track, "trk", "trkpt", /*with_segments=*/true, options, &out);
  }
  // A <rte> has no segments in the schema, so a multi-segment route is written
  // as one run of <rtept>. The reader made a route a single-segment track, so
  // anything it produced round-trips exactly; only a caller who built a
  // many-segment route by hand loses the boundaries, and GPX has nowhere to
  // put them.
  for (const GpxTrack& route : document.routes) {
    AppendTrack(route, "rte", "rtept", /*with_segments=*/false, options, &out);
  }

  out.append("</gpx>\n");
  return out;
}

Status WriteGpxFile(const std::string& path, const GpxDocument& document,
                    const GpxWriteOptions& options) {
  const std::string text = WriteGpx(document, options);
  const std::string temp = path + ".tmp";
  {
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) return Status::Error(kIoError, "WriteGpxFile: cannot open " + temp);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    if (!stream.good()) {
      stream.close();
      std::remove(temp.c_str());
      return Status::Error(kIoError, "WriteGpxFile: write failed for " + temp);
    }
  }
  std::remove(path.c_str());
  if (std::rename(temp.c_str(), path.c_str()) != 0) {
    std::remove(temp.c_str());
    return Status::Error(kIoError, "WriteGpxFile: cannot rename onto " + path);
  }
  return Status::Ok();
}

GpxDocument BuildGpxTrack(const std::vector<PositionFix>& fixes, const std::string& track_name,
                          const std::string& track_type, double split_gap_s) {
  GpxDocument document;
  document.version = "1.1";
  document.creator = "Peregrine";
  document.name = track_name;

  GpxTrack track;
  track.name = track_name;
  track.type = track_type;

  GpxTrackSegment current;
  for (const PositionFix& fix : fixes) {
    if (!fix.has_position) continue;
    if (split_gap_s > 0.0 && !current.points.empty() && current.points.back().has_time &&
        fix.has_time && fix.time_s - current.points.back().time_s > split_gap_s) {
      track.segments.push_back(std::move(current));
      current = GpxTrackSegment{};
    }
    if (fix.has_time && !document.has_time) {
      document.has_time = true;
      document.time_s = fix.time_s;
    }
    current.points.push_back(fix);
  }
  if (!current.points.empty()) track.segments.push_back(std::move(current));
  if (!track.segments.empty()) document.tracks.push_back(std::move(track));
  return document;
}

}  // namespace fv
