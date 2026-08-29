// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// pyfvw.nav — the moving map, bound (nav plan MM4).
//
// Its own TU for the reason pyfvw_app.cpp and pyfvw_draw.cpp are: it is a whole
// layer (a feed, a camera, a slew and the overlay that holds them), and
// pyfvw_module.cpp is already 2700 lines.
//
// WHAT IS BOUND AND WHAT IS NOT.
//
// `PositionFix` is bound with its `has_*` flags as ordinary attributes rather
// than hidden behind Optionals, because they ARE the fix's shape (MM1) and a
// Python source assembling one from NMEA sets them exactly as a C++ one does.
// The one convenience is `set_position`, which sets both fields and the flag —
// the single most common two-line mistake this seam can produce.
//
// `IPositionSource` IS bound as a base with a trampoline, so a Python receiver
// is a first-class source: that is the whole point of D6's plain-C++ seam, and
// a shell that reads its phone's GPS in Python should not have to write C++ to
// deliver it. `ScriptedSource` is bound over it.
//
// THE TICK IS THE API. `MovingMapOverlay.tick(proj, dt)` returns everything a
// shell needs and applies nothing, exactly as in C++ — so a Python shell reads
// `t.slew.center` and calls `proj.set_center(...)` itself. Binding an
// auto-applying variant would put the port's one "the overlay does not touch
// the engine" rule on the wrong side of the language boundary.

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <vector>

#include "pyfvw_common.h"

#include "fvkit/nav/camera.h"
#include "fvkit/nav/camera_slew.h"
#include "fvkit/nav/gpx.h"
#include "fvkit/nav/heading.h"
#include "fvkit/nav/line_transport.h"
#include "fvkit/nav/nmea.h"
#include "fvkit/nav/position.h"
#include "fvkit/nav/road_snap.h"
#include "fvkit/nav/scripted_source.h"
#include "fvkit/overlay/moving_map_overlay.h"

// The one road network the port has (MM5). It lives in port/Routing because
// fvkit does not link the router; pyfvw links both, so this is where the two
// are introduced to each other.
#include "fv_road_network.h"

namespace pyfvw {

namespace py = pybind11;
using namespace pybind11::literals;

namespace {

fv::FvColor ToColor(py::sequence s) {
  fv::FvColor c;
  c.r = static_cast<unsigned char>(py::cast<int>(s[0]));
  c.g = static_cast<unsigned char>(py::cast<int>(s[1]));
  c.b = static_cast<unsigned char>(py::cast<int>(s[2]));
  c.a = s.size() > 3 ? static_cast<unsigned char>(py::cast<int>(s[3])) : 255;
  return c;
}

// A Python position source. Only the three methods of the seam; `emit` is what
// a subclass calls when a fix arrives, and it is protected in C++ for a reason
// that does not survive the crossing (a Python subclass has no other way to
// deliver), so it is exposed here.
class PySource : public fv::PositionSourceBase {
 public:
  // PYTHON NEVER SEES A Status (the module's own rule, pyfvw_module.cpp:9):
  // a Python source reports a failure by RAISING, the way Python code says
  // everything, and returning normally is success. So the override is written
  // out rather than taken from PYBIND11_OVERRIDE_PURE, which would try to cast
  // whatever Python returned into an fv::Status.
  fv::Status Start() override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "start");
    if (!o)
      return fv::Status::Error(fv::kUnsupported,
                               "python position source has no start()");
    try {
      o();
      return fv::Status::Ok();
    } catch (py::error_already_set& e) {
      int code = fv::kInternal;
      try {
        if (py::hasattr(e.value(), "code"))
          code = e.value().attr("code").cast<int>();
      } catch (py::error_already_set&) {
        PyErr_Clear();
      }
      return fv::Status::Error(code,
                               std::string("python source start: ") + e.what());
    }
  }
  void Stop() override {
    py::gil_scoped_acquire gil;
    py::function o = py::get_override(this, "stop");
    if (!o) return;
    try {
      o();
    } catch (py::error_already_set& e) {
      // A stop that raises is not a failure anyone can act on -- the caller is
      // shutting the feed down -- so it is swallowed here rather than thrown
      // through a destructor or a mode change.
      e.restore();
      PyErr_Clear();
    }
  }
  // The base's Emit is protected; this is the same call, reachable.
  void EmitFix(const fv::PositionFix& f) { Emit(f); }
  void SetRunning(bool on) { running_ = on; }
};

}  // namespace

void BindNav(py::module_& m) {
  py::module_ nav = m.def_submodule(
      "nav",
      "Moving map (nav plan MM1-MM7): position fixes and feeds, the heading "
      "resolver, the road snapper, FalconView's camera, the slew that smooths "
      "it, and the MovingMapOverlay that holds them -- plus MM6's three real "
      "feeds, a live NMEA stream over TCP/UDP/file and the two recorded "
      "readers (NMEA log, GPX) that both arrive at "
      "build_scripted_track_from_fixes.");

  // ---- the fix ------------------------------------------------------------

  py::class_<fv::PositionFix>(
      nav, "PositionFix",
      "One position report. Every field carries its own validity flag "
      "(has_position, has_speed, ...) because that is what the NMEA sentences "
      "genuinely deliver -- a GLL has no speed, a VTG has no position. Units "
      "are FvKit's: degrees, metres, metres/second, degrees clockwise from "
      "true north, epoch seconds.")
      .def(py::init<>())
      .def_readwrite("lat", &fv::PositionFix::lat)
      .def_readwrite("lon", &fv::PositionFix::lon)
      .def_readwrite("has_position", &fv::PositionFix::has_position)
      .def_readwrite("altitude_msl_m", &fv::PositionFix::altitude_msl_m)
      .def_readwrite("has_altitude", &fv::PositionFix::has_altitude)
      .def_readwrite("speed_mps", &fv::PositionFix::speed_mps)
      .def_readwrite("has_speed", &fv::PositionFix::has_speed)
      .def_readwrite("true_heading_deg", &fv::PositionFix::true_heading_deg)
      .def_readwrite("has_true_heading", &fv::PositionFix::has_true_heading)
      .def_readwrite("magnetic_heading_deg",
                     &fv::PositionFix::magnetic_heading_deg)
      .def_readwrite("has_magnetic_heading",
                     &fv::PositionFix::has_magnetic_heading)
      .def_readwrite("time_s", &fv::PositionFix::time_s)
      .def_readwrite("has_time", &fv::PositionFix::has_time)
      .def_readwrite("hdop", &fv::PositionFix::hdop)
      .def_readwrite("has_hdop", &fv::PositionFix::has_hdop)
      .def_readwrite("satellite_count", &fv::PositionFix::satellite_count)
      .def_readwrite("has_satellite_count",
                     &fv::PositionFix::has_satellite_count)
      .def_property_readonly("position", &fv::PositionFix::position)
      .def("set_position", &fv::PositionFix::SetPosition, "lat"_a, "lon"_a,
           "Sets lat, lon and has_position together.")
      .def("merge", &fv::PositionFix::Merge, "other"_a,
           "Copies every VALID field of `other` over this one -- how two "
           "sentences of one epoch become one fix.")
      .def("__repr__", [](const fv::PositionFix& f) {
        if (!f.has_position) return std::string("<PositionFix (no position)>");
        return "<PositionFix " + std::to_string(f.lat) + ", " +
               std::to_string(f.lon) + ">";
      });

  nav.def("normalize_heading_deg", &fv::NormalizeHeadingDeg, "degrees"_a,
          "Wraps a heading into [0, 360).");

  // ---- the feed seam ------------------------------------------------------

  py::class_<fv::IPositionSource, std::shared_ptr<fv::IPositionSource>>(
      nav, "IPositionSource",
      "A position feed. Subclass PositionSource, not this.")
      .def("start", [](fv::IPositionSource& s) { ThrowIfError(s.Start()); })
      .def("stop", &fv::IPositionSource::Stop)
      .def_property_readonly("running", &fv::IPositionSource::running)
      .def("set_listener", &fv::IPositionSource::SetListener, "listener"_a,
           "ONE listener, replaced by a second call. Set it BEFORE start(): a "
           "source may deliver its first fix from inside start().");

  py::class_<fv::PositionSourceBase, fv::IPositionSource, PySource,
             std::shared_ptr<fv::PositionSourceBase>>(
      nav, "PositionSource",
      "Subclass this to write a feed in Python: override start()/stop() and "
      "call emit(fix) whenever one arrives. The listener runs on WHATEVER "
      "THREAD you emit from -- put a FixQueue in between if that is not the "
      "UI's.")
      .def(py::init<>())
      .def("emit", [](PySource& s, const fv::PositionFix& f) { s.EmitFix(f); },
           "fix"_a, "Deliver a fix to the listener.")
      .def("set_running",
           [](PySource& s, bool on) { s.SetRunning(on); }, "on"_a,
           "What running reports. A Python start() should set it True.");

  py::class_<fv::FixQueue>(
      nav, "FixQueue",
      "Bounded, thread-safe queue of fixes: filled from a source's thread, "
      "drained on the consumer's tick. FULL DROPS THE OLDEST and counts it -- "
      "a position feed is a stream of the present.")
      .def(py::init<std::size_t>(), "capacity"_a = 64)
      .def("push", &fv::FixQueue::Push, "fix"_a)
      .def("listener", &fv::FixQueue::Listener,
           "The callback to hand to IPositionSource.set_listener. Captures the "
           "queue, which must therefore outlive the source.")
      .def("drain",
           [](fv::FixQueue& q) {
             std::vector<fv::PositionFix> out;
             q.Drain(&out);
             return out;
           },
           "Everything queued, oldest first; empties the queue.")
      .def("drain_latest",
           [](fv::FixQueue& q) -> py::object {
             fv::PositionFix f;
             if (!q.DrainLatest(&f)) return py::none();
             return py::cast(f);
           },
           "The newest fix, or None. Discards the rest.")
      .def("clear", &fv::FixQueue::Clear)
      .def("__len__", &fv::FixQueue::size)
      .def_property_readonly("capacity", &fv::FixQueue::capacity)
      .def_property_readonly("dropped", &fv::FixQueue::dropped);

  // ---- the scripted source ------------------------------------------------

  py::class_<fv::ScriptedFix>(nav, "ScriptedFix",
                              "One entry of a script: t_s is RELATIVE to the "
                              "script, the fix's own time_s is absolute.")
      .def(py::init([](double t_s, const fv::PositionFix& fix) {
             return fv::ScriptedFix{t_s, fix};
           }),
           "t_s"_a, "fix"_a)
      .def_readwrite("t_s", &fv::ScriptedFix::t_s)
      .def_readwrite("fix", &fv::ScriptedFix::fix);

  py::class_<fv::ScriptedSource, fv::PositionSourceBase,
             std::shared_ptr<fv::ScriptedSource>>(
      nav, "ScriptedSource",
      "A source that plays a canned track. IT HAS NO THREAD: poll() asks the "
      "clock and emits whatever is due, so a whole flight is one loop with no "
      "sleeping. Its clock is injectable, which is what makes a timing "
      "assertion an equality.")
      .def(py::init<>())
      .def(py::init<std::vector<fv::ScriptedFix>>(), "track"_a)
      .def("set_track", &fv::ScriptedSource::SetTrack, "track"_a)
      .def_property_readonly("track", &fv::ScriptedSource::track)
      .def("set_clock", &fv::ScriptedSource::SetClock, "clock"_a,
           "A callable returning monotonic SECONDS. Only differences are taken.")
      .def_property("time_scale", &fv::ScriptedSource::time_scale,
                    &fv::ScriptedSource::SetTimeScale,
                    "Wall seconds -> script seconds. 60 replays an hour in a "
                    "minute.")
      .def_property("looping", &fv::ScriptedSource::looping,
                    &fv::ScriptedSource::SetLooping)
      .def("poll", &fv::ScriptedSource::Poll,
           "Emits every fix whose time has come; returns how many.")
      .def_property_readonly("script_time_s", &fv::ScriptedSource::script_time_s)
      .def_property_readonly("finished", &fv::ScriptedSource::finished)
      .def_property_readonly("duration_s", &fv::ScriptedSource::duration_s);

  py::class_<fv::ScriptedTrackOptions>(nav, "ScriptedTrackOptions")
      .def(py::init<>())
      .def_readwrite("sample_interval_s",
                     &fv::ScriptedTrackOptions::sample_interval_s)
      .def_readwrite("start_time_s", &fv::ScriptedTrackOptions::start_time_s)
      .def_readwrite("set_speed", &fv::ScriptedTrackOptions::set_speed)
      .def_readwrite("set_true_heading",
                     &fv::ScriptedTrackOptions::set_true_heading)
      .def_readwrite("set_altitude", &fv::ScriptedTrackOptions::set_altitude)
      .def_readwrite("altitude_msl_m", &fv::ScriptedTrackOptions::altitude_msl_m)
      .def_readwrite("set_hdop", &fv::ScriptedTrackOptions::set_hdop)
      .def_readwrite("hdop", &fv::ScriptedTrackOptions::hdop)
      .def_readwrite("include_endpoint",
                     &fv::ScriptedTrackOptions::include_endpoint);

  nav.def("build_scripted_track", &fv::BuildScriptedTrack, "path"_a,
          "speed_mps"_a, "options"_a = fv::ScriptedTrackOptions{},
          "Samples a POLYLINE at a constant ground speed, walking it with "
          "great-circle geodesy. `route.geometry` is exactly this argument -- "
          "which is why fvkit does not link port/Routing.");


  // ---- replaying a RECORDED track (MM6) -----------------------------------
  //
  // The two recorded readers below (`read_gpx_file`, `read_nmea_log`) both
  // hand their fixes to THIS function, which is the whole reason the moving
  // map has one replay path rather than one per file format. A shell's open
  // is three calls and no new C++:
  //
  //   fixes = nav.flatten_gpx_fixes(nav.read_gpx_file(path))
  //   src   = nav.ScriptedSource(nav.build_scripted_track_from_fixes(fixes))

  py::class_<fv::FixScriptOptions>(
      nav, "FixScriptOptions",
      "How recorded fixes become a schedule. max_gap_s caps a coffee stop; "
      "fallback_interval_s is what a track with no clock at all plays at.")
      .def(py::init<>())
      .def_readwrite("fallback_interval_s",
                     &fv::FixScriptOptions::fallback_interval_s)
      .def_readwrite("drop_non_monotonic",
                     &fv::FixScriptOptions::drop_non_monotonic)
      .def_readwrite("max_gap_s", &fv::FixScriptOptions::max_gap_s);

  nav.def("build_scripted_track_from_fixes", &fv::BuildScriptedTrackFromFixes,
          "fixes"_a, "options"_a = fv::FixScriptOptions{},
          "Schedules recorded fixes by their OWN timestamps, so a ride "
          "replays at the speed it was ridden. The fixes themselves are "
          "passed through untouched.");

  // ---- where a line of text comes from (MM6) ------------------------------
  //
  // A SEPARATE seam from the parser, so nothing here knows what NMEA is.
  //
  // NO PYTHON TRAMPOLINE, unlike IPositionSource. A Python object that wants
  // to deliver bytes has two better doors already: `StringLineTransport`,
  // whose `add_data` takes whatever a Python socket, serial port or subprocess
  // just read, and `PositionSourceBase`, which is subclassable when the Python
  // side would rather deliver whole fixes. A third seam would only be a way to
  // re-implement `LineBuffer` in a slower language.

  py::enum_<fv::LineResult>(
      nav, "LineResult",
      "What one read_line did. THREE OF THE FOUR ARE NOT FAILURES: AGAIN is "
      "the normal state of a live feed between sentences and END is how a "
      "recording says it is over.")
      .value("LINE", fv::LineResult::kLine)
      .value("AGAIN", fv::LineResult::kAgain)
      .value("END", fv::LineResult::kEnd)
      .value("ERROR", fv::LineResult::kError);

  py::class_<fv::LineBuffer>(
      nav, "LineBuffer",
      "The one framing implementation: holds the tail of a partial read and "
      "hands out whole lines, dropping '\\r' and blank lines and capping a "
      "line so a hostile stream stays bounded.")
      .def(py::init<std::size_t>(), "max_line_length"_a = 512)
      .def("append",
           [](fv::LineBuffer& b, const std::string& data) { b.Append(data); },
           "data"_a)
      .def("next_line",
           [](fv::LineBuffer& b) -> py::object {
             std::string line;
             if (!b.NextLine(&line)) return py::none();
             return py::str(line);
           },
           "The next whole line, or None.")
      .def("take_partial",
           [](fv::LineBuffer& b) -> py::object {
             std::string line;
             if (!b.TakePartial(&line)) return py::none();
             return py::str(line);
           },
           "Whatever is left with no terminator after it -- a recording whose "
           "last line has no newline still yields its last sentence.")
      .def("clear", &fv::LineBuffer::Clear)
      .def_property_readonly("pending_bytes", &fv::LineBuffer::pending_bytes)
      .def_property_readonly("overlong_dropped",
                             &fv::LineBuffer::overlong_dropped);

  py::class_<fv::ILineTransport, std::shared_ptr<fv::ILineTransport>>(
      nav, "LineTransport",
      "A source of lines. open() raises on failure; read_line() NEVER BLOCKS "
      "and returns (LineResult, text).")
      .def("open", [](fv::ILineTransport& t) { ThrowIfError(t.Open()); },
           "Idempotent, and re-opening a closed transport restarts it -- which "
           "is what makes a file replay re-runnable.")
      .def("close", &fv::ILineTransport::Close)
      .def_property_readonly("is_open", &fv::ILineTransport::is_open)
      .def("read_line",
           [](fv::ILineTransport& t) {
             std::string line;
             fv::LineResult r = t.ReadLine(&line);
             return py::make_tuple(r, line);
           },
           "(LineResult, text). The text is empty unless the result is LINE.")
      .def_property_readonly(
          "error_message",
          [](const fv::ILineTransport& t) { return t.error().message; },
          "Set when read_line last returned ERROR, and never cleared by a "
          "later AGAIN.")
      .def_property_readonly("description", &fv::ILineTransport::description)
      .def("__repr__", [](const fv::ILineTransport& t) {
        return "<LineTransport " + t.description() + ">";
      });

  py::class_<fv::StringLineTransport, fv::ILineTransport,
             std::shared_ptr<fv::StringLineTransport>>(
      nav, "StringLineTransport",
      "Lines out of a string -- the test double, AND the door for a Python "
      "reader: hand `add_data` whatever your own socket just read and the NMEA "
      "machinery does the rest.")
      .def(py::init<>())
      .def(py::init<std::string, bool>(), "data"_a, "auto_end"_a = true)
      .def("add_data", &fv::StringLineTransport::AddData, "data"_a)
      .def("set_ended", &fv::StringLineTransport::SetEnded,
           "Say the stream is over. Bytes already added are still handed out.");

  py::class_<fv::FileLineTransport, fv::ILineTransport,
             std::shared_ptr<fv::FileLineTransport>>(
      nav, "FileLineTransport",
      "Lines out of a file, as fast as they are asked for -- pacing a "
      "recording by its own stamps is build_scripted_track_from_fixes's job. "
      "`follow` tails it, so `nc host port >> log` in one window feeds the map "
      "in another.")
      .def(py::init<std::string, bool>(), "path"_a, "follow"_a = false)
      .def_property_readonly("path", &fv::FileLineTransport::path)
      .def_property_readonly("follow", &fv::FileLineTransport::follow);

  py::class_<fv::SocketLineTransport, fv::ILineTransport,
             std::shared_ptr<fv::SocketLineTransport>>(
      nav, "SocketLineTransport",
      "The shared socket plumbing. Not constructed directly.")
      .def_property_readonly(
          "bytes_received", &fv::SocketLineTransport::bytes_received,
          "Bytes pulled off the socket. A shell saying 'no data' wants THIS "
          "rather than a fix count: a stream of sentences the parser rejects "
          "looks exactly like silence otherwise.");

  py::class_<fv::TcpLineTransport, fv::SocketLineTransport,
             std::shared_ptr<fv::TcpLineTransport>>(
      nav, "TcpLineTransport",
      "A TCP client, which is what 'phone GPS' means: GPS2IP and every other "
      "phone-as-a-receiver app listens and the map connects. open() returns "
      "with the connect still in flight and read_line answers AGAIN until it "
      "finishes -- so an asleep phone never hangs the shell's tick.")
      .def(py::init<std::string, uint16_t>(), "host"_a, "port"_a)
      .def_property_readonly("host", &fv::TcpLineTransport::host)
      .def_property_readonly("port", &fv::TcpLineTransport::port);

  py::class_<fv::UdpLineTransport, fv::SocketLineTransport,
             std::shared_ptr<fv::UdpLineTransport>>(
      nav, "UdpLineTransport",
      "A UDP listener -- the other half of phone GPS, for an app that "
      "BROADCASTS rather than serving. Port 0 binds an ephemeral one, which "
      "bound_port then reports.")
      .def(py::init<uint16_t, std::string>(), "port"_a,
           "bind_host"_a = std::string())
      .def_property_readonly("port", &fv::UdpLineTransport::port)
      .def_property_readonly("bound_port", &fv::UdpLineTransport::bound_port);

  // ---- NMEA 0183 (MM6) ----------------------------------------------------

  py::enum_<fv::NmeaType>(nav, "NmeaType",
                          "The four sentences FalconView reads. VTG carries no "
                          "position at all.")
      .value("UNKNOWN", fv::NmeaType::kUnknown)
      .value("RMC", fv::NmeaType::kRmc)
      .value("GGA", fv::NmeaType::kGga)
      .value("GLL", fv::NmeaType::kGll)
      .value("VTG", fv::NmeaType::kVtg);

  py::class_<fv::NmeaReading>(
      nav, "NmeaReading",
      "What ONE sentence said. Time is split in two because NMEA splits it in "
      "two: every sentence but VTG carries a time of DAY and only RMC carries "
      "the date that turns it into an instant.")
      .def(py::init<>())
      .def_readwrite("type", &fv::NmeaReading::type)
      .def_readwrite("talker", &fv::NmeaReading::talker,
                     "The two characters after '$'. ANY talker is accepted, "
                     "not just 'GP' -- a phone talks $GNRMC.")
      .def_readwrite("fix", &fv::NmeaReading::fix)
      .def_readwrite("has_time_of_day", &fv::NmeaReading::has_time_of_day)
      .def_readwrite("time_of_day_s", &fv::NmeaReading::time_of_day_s)
      .def_readwrite("has_date", &fv::NmeaReading::has_date)
      .def_readwrite("year", &fv::NmeaReading::year)
      .def_readwrite("month", &fv::NmeaReading::month)
      .def_readwrite("day", &fv::NmeaReading::day)
      .def_readwrite("has_geoid_separation",
                     &fv::NmeaReading::has_geoid_separation)
      .def_readwrite("geoid_separation_m", &fv::NmeaReading::geoid_separation_m,
                     "Reported and NOT applied -- the altitude is already the "
                     "MSL height the receiver computed.")
      .def("__repr__", [](const fv::NmeaReading& r) {
        return std::string("<NmeaReading ") + fv::NmeaTypeName(r.type) + " $" +
               r.talker + ">";
      });

  nav.def("nmea_checksum",
          [](const std::string& payload) {
            return static_cast<int>(fv::NmeaChecksum(payload));
          },
          "payload"_a,
          "XOR of every byte BETWEEN the '$' and the '*', both excluded.");

  nav.def("nmea_sentence_looks_valid", &fv::NmeaSentenceLooksValid, "line"_a,
          "Starts with '$', no longer than 82, and -- only if a '*' is there "
          "-- the checksum matches. A sentence with no checksum PASSES, which "
          "is the original's rule and a real receiver's habit.");

  nav.def("split_nmea_fields", &fv::SplitNmeaFields, "line"_a,
          "fields[0] is the id without the '$'. Empty fields are preserved: an "
          "NMEA field's POSITION is its meaning.");

  nav.def("nmea_type_of",
          [](const std::string& line) {
            std::string talker;
            fv::NmeaType t = fv::NmeaTypeOf(line, &talker);
            return py::make_tuple(t, talker);
          },
          "line"_a, "(NmeaType, talker).");

  nav.def("parse_nmea_sentence",
          [](const std::string& line) -> py::object {
            fv::NmeaReading r;
            if (!fv::ParseNmeaSentence(line, &r)) return py::none();
            return py::cast(r);
          },
          "line"_a,
          "The reading, or None -- which is what the original returned FALSE "
          "for: not one of the four, a bad checksum, a location flag saying "
          "'invalid', an out-of-range position, or too few fields (quirk Q1, "
          "a short sentence is rejected WHOLE).");

  nav.def("nmea_y2k_year", &fv::NmeaY2kYear, "year"_a,
          "The original's rule verbatim: 71..99 are 1971..1999, 00..70 are "
          "2000..2070.");

  nav.def("utc_to_epoch_seconds", &fv::UtcToEpochSeconds, "year"_a, "month"_a,
          "day"_a, "seconds_of_day"_a);

  nav.def("epoch_seconds_to_utc",
          [](double epoch) {
            int y = 0, mo = 0, d = 0;
            double sod = 0.0;
            fv::EpochSecondsToUtc(epoch, &y, &mo, &d, &sod);
            return py::make_tuple(y, mo, d, sod);
          },
          "epoch_seconds"_a, "(year, month, day, seconds_of_day), all UTC.");

  py::class_<fv::NmeaFixAssembler>(
      nav, "NmeaFixAssembler",
      "Lines in, fixes out. Sentences sharing a time of DAY are merged into "
      "one fix (PositionFix.merge), which costs ONE EPOCH OF LATENCY because "
      "a group is only known to be over when the next one starts -- "
      "set_emit_per_sentence(True) is the live-feed way out. The date comes "
      "from the last RMC or from set_date_hint; until one has arrived a fix "
      "has has_time False rather than a made-up 1970 stamp.")
      .def(py::init<>())
      .def("add_line",
           [](fv::NmeaFixAssembler& a, const std::string& line) -> py::object {
             fv::PositionFix f;
             if (!a.AddLine(line, &f)) return py::none();
             return py::cast(f);
           },
           "line"_a, "The fix this line completed, or None.")
      .def("flush",
           [](fv::NmeaFixAssembler& a) -> py::object {
             fv::PositionFix f;
             if (!a.Flush(&f)) return py::none();
             return py::cast(f);
           },
           "Whatever is pending. Call at end of stream.")
      .def("reset", &fv::NmeaFixAssembler::Reset)
      .def_property("emit_per_sentence", &fv::NmeaFixAssembler::emit_per_sentence,
                    &fv::NmeaFixAssembler::SetEmitPerSentence)
      .def("set_date_hint", &fv::NmeaFixAssembler::SetDateHint, "year"_a,
           "month"_a, "day"_a)
      .def_property_readonly("has_date", &fv::NmeaFixAssembler::has_date)
      .def_property_readonly("lines_seen", &fv::NmeaFixAssembler::lines_seen)
      .def_property_readonly("sentences_parsed",
                             &fv::NmeaFixAssembler::sentences_parsed)
      .def_property_readonly("sentences_rejected",
                             &fv::NmeaFixAssembler::sentences_rejected,
                             "With lines_seen, this is how a shell says "
                             "'receiving, but nothing parses' -- the single "
                             "most common thing to be wrong about a feed.")
      .def_property_readonly("fixes_emitted",
                             &fv::NmeaFixAssembler::fixes_emitted);

  py::class_<fv::NmeaLineSource, fv::PositionSourceBase,
             std::shared_ptr<fv::NmeaLineSource>>(
      nav, "NmeaLineSource",
      "The LIVE feed: a LineTransport in, PositionFixes out. No thread, for "
      "ScriptedSource's reasons -- poll() from the shell's existing tick is "
      "the whole loop. max_lines_per_poll bounds one tick's work, so a file "
      "transport handed three hours of log does not replay it inside a frame.")
      .def(py::init<std::shared_ptr<fv::ILineTransport>>(), "transport"_a)
      .def("poll", &fv::NmeaLineSource::Poll,
           "Reads what has arrived and emits the fixes it made; returns how "
           "many. On end of stream it flushes the pending group ONCE, so the "
           "last second of a recording is not lost.")
      .def_property_readonly("at_end", &fv::NmeaLineSource::at_end)
      .def_property("max_lines_per_poll",
                    &fv::NmeaLineSource::max_lines_per_poll,
                    &fv::NmeaLineSource::SetMaxLinesPerPoll)
      .def_property_readonly(
          "assembler",
          [](fv::NmeaLineSource& s) { return &s.assembler(); },
          py::return_value_policy::reference_internal)
      .def_property_readonly("transport", &fv::NmeaLineSource::transport)
      .def_property_readonly("lines_read", &fv::NmeaLineSource::lines_read)
      .def_property_readonly("fixes_emitted", &fv::NmeaLineSource::fixes_emitted)
      .def_property_readonly(
          "error_message",
          [](const fv::NmeaLineSource& s) { return s.error().message; },
          "Set when the transport broke; the source stops on one.");

  nav.def("read_nmea_log",
          [](const std::string& path) {
            std::vector<fv::PositionFix> fixes;
            ThrowIfError(fv::ReadNmeaLog(path, &fixes));
            return fixes;
          },
          "path"_a,
          "A whole recorded log as fixes, ready for "
          "build_scripted_track_from_fixes. A file that opens and yields "
          "nothing is an empty list: an empty log is data, not an error.");

  nav.def("build_rmc", &fv::BuildRmc, "fix"_a);
  nav.def("build_gga", &fv::BuildGga, "fix"_a);
  nav.def("build_vtg", &fv::BuildVtg, "fix"_a,
          "The build side, kept for the recorder that does not exist yet. "
          "Empty string when the fix has nothing to say. A built position "
          "round-trips to about a metre -- minutes go out with three decimals, "
          "which is the wire format and not the parse.");

  nav.def("nmea_time_of_day_string", &fv::NmeaTimeOfDayString, "seconds"_a);

  // ---- GPX (MM6) ----------------------------------------------------------

  py::class_<fv::GpxTrackSegment>(
      nav, "GpxTrackSegment",
      "One <trkseg>: a run of points the recorder believes are continuous. "
      "The boundary is KEPT, because a straight line across a lunch break is "
      "not a track.")
      .def(py::init<>())
      .def_readwrite("points", &fv::GpxTrackSegment::points)
      .def("__len__",
           [](const fv::GpxTrackSegment& s) { return s.points.size(); });

  py::class_<fv::GpxTrack>(nav, "GpxTrack", "One <trk>, or one <rte>.")
      .def(py::init<>())
      .def_readwrite("name", &fv::GpxTrack::name)
      .def_readwrite("type", &fv::GpxTrack::type)
      .def_readwrite("segments", &fv::GpxTrack::segments)
      .def_property_readonly("point_count", &fv::GpxTrack::point_count);

  py::class_<fv::GpxDocument>(
      nav, "GpxDocument",
      "A whole GPX file. `time_s` is when the FILE was written, which is not "
      "when the ride happened -- the track points carry that.")
      .def(py::init<>())
      .def_readwrite("creator", &fv::GpxDocument::creator)
      .def_readwrite("version", &fv::GpxDocument::version)
      .def_readwrite("name", &fv::GpxDocument::name)
      .def_readwrite("has_time", &fv::GpxDocument::has_time)
      .def_readwrite("time_s", &fv::GpxDocument::time_s)
      .def_readwrite("waypoints", &fv::GpxDocument::waypoints)
      .def_readwrite("waypoint_names", &fv::GpxDocument::waypoint_names)
      .def_readwrite("tracks", &fv::GpxDocument::tracks)
      .def_readwrite("routes", &fv::GpxDocument::routes,
                     "<rte>: a PLANNED line and not a recorded one.")
      .def_property_readonly("track_point_count",
                             &fv::GpxDocument::track_point_count)
      .def_property_readonly("longest_track", &fv::GpxDocument::longest_track,
                             py::return_value_policy::reference_internal,
                             "What a shell means by 'open this ride', or None.");

  py::class_<fv::GpxReadOptions>(nav, "GpxReadOptions")
      .def(py::init<>())
      .def_readwrite("derive_speed", &fv::GpxReadOptions::derive_speed)
      .def_readwrite("derive_true_heading",
                     &fv::GpxReadOptions::derive_true_heading,
                     "OFF by default: HeadingResolver already derives one in "
                     "SCREEN space, and a true bearing written here would look "
                     "REPORTED and quietly win.")
      .def_readwrite("split_gap_s", &fv::GpxReadOptions::split_gap_s)
      .def_readwrite("drop_non_monotonic_time",
                     &fv::GpxReadOptions::drop_non_monotonic_time);

  nav.def("read_gpx_file",
          [](const std::string& path, const fv::GpxReadOptions& options) {
            fv::GpxDocument doc;
            ThrowIfError(fv::ReadGpxFile(path, &doc, options));
            return doc;
          },
          "path"_a, "options"_a = fv::GpxReadOptions{},
          "GPX 1.0 and 1.1 over expat, because a GPX file comes off the open "
          "internet. A well-formed file with no tracks is an EMPTY document "
          "and not an error.");

  nav.def("parse_gpx",
          [](const std::string& xml, const fv::GpxReadOptions& options) {
            fv::GpxDocument doc;
            ThrowIfError(fv::ParseGpx(xml, &doc, options));
            return doc;
          },
          "xml"_a, "options"_a = fv::GpxReadOptions{});

  nav.def("flatten_gpx_fixes", &fv::FlattenGpxFixes, "document"_a,
          "Every track point of every track, in file order -- what feeds a "
          "replay. gpx_segment_path is what feeds a DRAWING, where the segment "
          "boundaries matter.");

  nav.def("gpx_segment_path", &fv::GpxSegmentPath, "segment"_a,
          "One segment's positions, ready for draw.GeoDraw.geo_polyline.");

  nav.def("parse_iso8601_utc",
          [](const std::string& text) -> py::object {
            double epoch = 0.0;
            if (!fv::ParseIso8601Utc(text, &epoch)) return py::none();
            return py::float_(epoch);
          },
          "text"_a, "Epoch seconds, or None.");

  nav.def("format_iso8601_utc", &fv::FormatIso8601Utc, "epoch_seconds"_a);

  // ---- the heading --------------------------------------------------------

  py::class_<fv::ResolvedHeading>(
      nav, "ResolvedHeading",
      "known=False still carries degrees 0.0 (FalconView's 'assume north'); "
      "reported says the fix carried it rather than it being derived from "
      "movement.")
      .def_readonly("degrees", &fv::ResolvedHeading::degrees)
      .def_readonly("known", &fv::ResolvedHeading::known)
      .def_readonly("reported", &fv::ResolvedHeading::reported)
      .def("__repr__", [](const fv::ResolvedHeading& h) {
        return "<ResolvedHeading " + std::to_string(h.degrees) +
               (h.known ? (h.reported ? " reported>" : " derived>")
                        : " unknown>");
      });

  py::class_<fv::HeadingResolver>(
      nav, "HeadingResolver",
      "The fix's own course when it has one, otherwise a bearing derived from "
      "the last two distinct positions IN SCREEN SPACE (so the symbol agrees "
      "with the line of its own track on an equal-arc map).")
      .def(py::init<std::size_t>(), "history"_a = 8)
      .def("set_deg_per_pixel", &fv::HeadingResolver::SetDegPerPixel,
           "deg_per_pixel_lat"_a, "deg_per_pixel_lon"_a,
           "From the projection, whenever the scale changes. <= 0 restores the "
           "cos(lat) default.")
      .def("update", &fv::HeadingResolver::Update, "fix"_a)
      .def_property_readonly("current", &fv::HeadingResolver::current)
      .def("reset", &fv::HeadingResolver::Reset)
      .def_property_readonly("history_size",
                             &fv::HeadingResolver::history_size);

  nav.def(
      "screen_bearing_deg",
      [](const fv::GeoPoint& from, const fv::GeoPoint& to, double dpp_lat,
         double dpp_lon) -> py::object {
        double b = 0.0;
        if (!fv::ScreenBearingDeg(from, to, dpp_lat, dpp_lon, &b))
          return py::none();
        return py::cast(b);
      },
      "from_"_a, "to"_a, "deg_per_pixel_lat"_a = 0.0,
      "deg_per_pixel_lon"_a = 0.0,
      "The screen-space bearing, or None when the two points are identical.");

  // ---- snap to road (MM5) -------------------------------------------------

  py::class_<fv::RoadCandidate>(
      nav, "RoadCandidate",
      "One road the snapper considered, already projected. `arc` is the "
      "network's own id and is opaque.")
      .def_readonly("arc", &fv::RoadCandidate::arc)
      .def_readonly("from_node", &fv::RoadCandidate::from_node)
      .def_readonly("to_node", &fv::RoadCandidate::to_node)
      .def_property_readonly(
          "point", [](const fv::RoadCandidate& c) { return c.point; })
      .def_readonly("distance_m", &fv::RoadCandidate::distance_m)
      .def_readonly("along_m", &fv::RoadCandidate::along_m)
      .def_readonly("length_m", &fv::RoadCandidate::length_m)
      .def_readonly("bearing_deg", &fv::RoadCandidate::bearing_deg)
      .def_readonly("one_way", &fv::RoadCandidate::one_way)
      .def_readonly("name", &fv::RoadCandidate::name)
      .def("__repr__", [](const fv::RoadCandidate& c) {
        return "<RoadCandidate '" + c.name + "' " +
               std::to_string(c.distance_m) + " m>";
      });

  py::class_<fv::SnappedFix>(
      nav, "SnappedFix",
      "The snapper's answer. THE RAW FIX IS NEVER DESTROYED: `raw` is what the "
      "receiver said and `applied()` is the fix to consume.")
      .def_readonly("raw", &fv::SnappedFix::raw)
      .def_readonly("snapped", &fv::SnappedFix::snapped)
      .def_property_readonly(
          "position", [](const fv::SnappedFix& s) { return s.position; })
      .def_readonly("arc", &fv::SnappedFix::arc)
      .def_readonly("road_name", &fv::SnappedFix::road_name)
      .def_readonly("bearing_deg", &fv::SnappedFix::bearing_deg)
      .def_readonly("has_bearing", &fv::SnappedFix::has_bearing)
      .def_readonly("offset_m", &fv::SnappedFix::offset_m,
                    "How far the raw fix was from the road.")
      .def_readonly("confidence", &fv::SnappedFix::confidence,
                    "0..1. 1 is on the road with no other road near; 0 is at "
                    "the rim of the search radius or an even split.")
      .def_readonly("held", &fv::SnappedFix::held,
                    "The road was HELD rather than chosen, because the ship is "
                    "below the hold speed.")
      .def_readonly("candidates", &fv::SnappedFix::candidates)
      .def("applied", &fv::SnappedFix::Applied,
           "`raw` with the position replaced by the snapped one and, when "
           "has_bearing, the true heading replaced by the road's.")
      .def("__repr__", [](const fv::SnappedFix& s) {
        if (!s.snapped) return std::string("<SnappedFix unsnapped>");
        return "<SnappedFix '" + s.road_name + "' " +
               std::to_string(s.offset_m) + " m, conf " +
               std::to_string(s.confidence) + ">";
      });

  py::class_<fv::RoadSnapSettings>(
      nav, "RoadSnapSettings",
      "EVERY TERM IS IN METRES, so a setting reads as a sentence: the heading "
      "penalty is how far out of its way the snapper will look for a road "
      "pointing the right way, the stay bonus is how much closer another road "
      "has to be before it will leave the one it is on.")
      .def(py::init<>())
      .def_readwrite("hdop_scale", &fv::RoadSnapSettings::hdop_scale)
      .def_readwrite("min_radius_m", &fv::RoadSnapSettings::min_radius_m)
      .def_readwrite("max_radius_m", &fv::RoadSnapSettings::max_radius_m)
      .def_readwrite("default_radius_m", &fv::RoadSnapSettings::default_radius_m)
      .def_readwrite("heading_penalty_m", &fv::RoadSnapSettings::heading_penalty_m)
      .def_readwrite("stay_bonus_m", &fv::RoadSnapSettings::stay_bonus_m)
      .def_readwrite("connected_bonus_m", &fv::RoadSnapSettings::connected_bonus_m)
      .def_readwrite("hold_speed_mps", &fv::RoadSnapSettings::hold_speed_mps)
      .def_readwrite("ambiguity_m", &fv::RoadSnapSettings::ambiguity_m);

  // The seam itself is bound as an opaque base: a Python subclass is NOT
  // supported, deliberately. QueryNear is called once per fix with an output
  // vector, and a Python implementation would marshal a list per call for a
  // case nobody has — the supported network is RoadGraphNetwork below.
  py::class_<fv::IRoadNetwork, std::shared_ptr<fv::IRoadNetwork>>(
      nav, "RoadNetwork",
      "What the snapper asks: which roads are near this point. Construct a "
      "RoadGraphNetwork.");

  py::enum_<fv::routing::RoadSnapFilter>(
      nav, "RoadSnapFilter",
      "Which arcs may be snapped to. DRIVEABLE by default -- the graph keeps "
      "footways and cycleways for a walking profile, and a car that snapped to "
      "the nearest arc of any class would spend the drive on the cycle path.")
      .value("ALL", fv::routing::RoadSnapFilter::kAll)
      .value("DRIVEABLE", fv::routing::RoadSnapFilter::kDriveable)
      .value("CYCLEABLE", fv::routing::RoadSnapFilter::kCycleable);

  py::class_<fv::routing::RoadGraphNetwork, fv::IRoadNetwork,
             std::shared_ptr<fv::routing::RoadGraphNetwork>>(
      nav, "RoadGraphNetwork",
      "An O4 routing.RoadGraph offered to the snapper. Indexes the arc "
      "GEOMETRY (the router's own index is over junctions, which is the wrong "
      "question for a ship between two of them) at construction, so building "
      "one costs a pass over the graph.")
      .def(py::init([](std::shared_ptr<fv::routing::RoadGraph> graph,
                       fv::routing::RoadSnapFilter filter, double cell_size_m) {
             fv::routing::RoadNetworkOptions options;
             options.filter = filter;
             options.cell_size_m = cell_size_m;
             return std::make_shared<fv::routing::RoadGraphNetwork>(
                 std::move(graph), options);
           }),
           "graph"_a, "filter"_a = fv::routing::RoadSnapFilter::kDriveable,
           "cell_size_m"_a = 200.0)
      .def_property_readonly("indexed_arcs",
                             &fv::routing::RoadGraphNetwork::indexed_arcs,
                             "How many roads the filter admitted -- the number "
                             "to look at when a snapper that should be finding "
                             "roads finds none.")
      .def_property_readonly(
          "bounds", [](const fv::routing::RoadGraphNetwork& n) { return n.bounds(); },
          "The box of the indexed road SHAPE, which is not the graph's own "
          "bounds (those are the box of its junctions).");

  py::class_<fv::RoadSnapper>(
      nav, "RoadSnapper",
      "Put the ship on the road it is most likely on. Feed it every fix, in "
      "order: its whole memory is the road the last fix snapped to, which is "
      "the hysteresis that stops it flapping between parallel roads and the "
      "hold that stops it walking around a junction at a standstill.")
      .def(py::init<>())
      .def(py::init<std::shared_ptr<const fv::IRoadNetwork>>(), "network"_a)
      .def("set_network", &fv::RoadSnapper::SetNetwork, "network"_a,
           "None turns snapping off and forgets the previous road.")
      .def_property_readonly("enabled", &fv::RoadSnapper::enabled)
      .def("set_settings", &fv::RoadSnapper::SetSettings, "settings"_a)
      .def_property_readonly("settings", &fv::RoadSnapper::settings)
      .def("snap",
           [](fv::RoadSnapper& s, const fv::PositionFix& fix,
              py::object prior_heading_deg) {
             if (prior_heading_deg.is_none()) return s.Snap(fix);
             return s.Snap(fix, py::cast<double>(prior_heading_deg), true);
           },
           "fix"_a, "prior_heading_deg"_a = py::none(),
           "The heading is the PREVIOUS fix's -- being one fix stale is right: "
           "it describes the way the ship was going as it arrived here, which "
           "is what says which road it is on. The fix's own true heading wins "
           "when it reports one.")
      .def("reset", &fv::RoadSnapper::Reset,
           "Forget the road. A source restart must call it.")
      .def_property_readonly("last", &fv::RoadSnapper::last)
      .def_property_readonly("last_candidates", &fv::RoadSnapper::last_candidates,
                             "Scored, best first -- why it chose that road.");

  // ---- the camera ---------------------------------------------------------

  py::class_<fv::CameraModes>(
      nav, "CameraModes",
      "FalconView's three toggles. auto_rotate and continuous both change what "
      "auto-centring DOES, and neither does anything while auto_center is off.")
      .def(py::init([](bool auto_center, bool auto_rotate, bool continuous) {
             return fv::CameraModes{auto_center, auto_rotate, continuous};
           }),
           "auto_center"_a = true, "auto_rotate"_a = false,
           "continuous"_a = false)
      .def_readwrite("auto_center", &fv::CameraModes::auto_center)
      .def_readwrite("auto_rotate", &fv::CameraModes::auto_rotate)
      .def_readwrite("continuous", &fv::CameraModes::continuous);

  py::class_<fv::ApronRect>(
      nav, "ApronRect",
      "The box the ship may wander inside before the map moves. right/bottom "
      "are EXCLUSIVE (CRect's convention).")
      .def_readonly("left", &fv::ApronRect::left)
      .def_readonly("top", &fv::ApronRect::top)
      .def_readonly("right", &fv::ApronRect::right)
      .def_readonly("bottom", &fv::ApronRect::bottom)
      .def_property_readonly("empty", &fv::ApronRect::empty)
      .def("contains", &fv::ApronRect::Contains, "x"_a, "y"_a);

  py::class_<fv::CameraTarget>(
      nav, "CameraTarget",
      "Where the map should be. changed=False means the ship is still inside "
      "its apron and there is nothing to apply.")
      .def_readonly("changed", &fv::CameraTarget::changed)
      // BY VALUE, not def_readonly. A def_readonly on a registered class
      // member hands Python a REFERENCE into the C++ struct, and both of these
      // centres are per-frame snapshots of state that keeps moving: a caller
      // that stored `tick.slew.center` as its own map centre would find it
      // changing silently on the next Advance(), with no assignment anywhere.
      .def_property_readonly(
          "center", [](const fv::CameraTarget& t) { return t.center; })
      .def_readonly("rotation_deg", &fv::CameraTarget::rotation_deg)
      .def_readonly("rotation_changed", &fv::CameraTarget::rotation_changed)
      .def_readonly("delta_x", &fv::CameraTarget::delta_x)
      .def_readonly("delta_y", &fv::CameraTarget::delta_y)
      .def_readonly("world_escape", &fv::CameraTarget::world_escape);

  py::class_<fv::MovingMapCamera>(
      nav, "MovingMapCamera",
      "FalconView's screen-positioning algorithm, ported verbatim. THE CAMERA "
      "NEVER TOUCHES THE MAP: update() answers where it should be and the "
      "shell applies it.")
      .def(py::init<>())
      .def("set_modes", &fv::MovingMapCamera::SetModes, "modes"_a)
      .def_property_readonly("modes", &fv::MovingMapCamera::modes)
      .def("set_track_up_anchor", &fv::MovingMapCamera::SetTrackUpAnchor,
           "frac_x"_a, "frac_y"_a)
      .def_property("world_escape_scale",
                    &fv::MovingMapCamera::world_escape_scale,
                    &fv::MovingMapCamera::SetWorldEscapeScale)
      .def("recompute_apron", &fv::MovingMapCamera::RecomputeApron,
           "window_width"_a, "window_height"_a, "ship_x"_a, "ship_y"_a,
           "Call once per frame AFTER drawing, from where the ship was drawn. "
           "The ordering is load-bearing: the apron is built from where the "
           "ship WAS and tested against where it has just moved to.")
      .def("clear_apron", &fv::MovingMapCamera::ClearApron)
      .def_property_readonly("apron", &fv::MovingMapCamera::apron)
      .def("update", &fv::MovingMapCamera::Update, "proj"_a, "ship"_a,
           "heading_deg"_a, "map_rotation_deg"_a = 0.0,
           "convergence_deg"_a = 0.0, "force"_a = false);

  // ---- the slew -----------------------------------------------------------

  py::enum_<fv::SlewEasing>(nav, "SlewEasing")
      .value("LINEAR", fv::SlewEasing::kLinear)
      .value("EASE_IN_OUT", fv::SlewEasing::kEaseInOut);

  py::class_<fv::SlewSettings>(
      nav, "SlewSettings",
      "duration_s 0 IS the FalconView jump, and the rate caps do not "
      "resurrect it. The caps EXTEND the duration and never clip the motion.")
      .def(py::init<>())
      .def_readwrite("duration_s", &fv::SlewSettings::duration_s)
      .def_readwrite("easing", &fv::SlewSettings::easing)
      .def_readwrite("max_pan_px_per_s", &fv::SlewSettings::max_pan_px_per_s)
      .def_readwrite("max_rotation_deg_per_s",
                     &fv::SlewSettings::max_rotation_deg_per_s);

  py::class_<fv::SlewState>(nav, "SlewState",
                            "What to apply this tick. active says another "
                            "frame is coming.")
      .def_property_readonly(
          "center", [](const fv::SlewState& s) { return s.center; })
      .def_readonly("rotation_deg", &fv::SlewState::rotation_deg)
      .def_readonly("changed", &fv::SlewState::changed)
      .def_readonly("active", &fv::SlewState::active);

  nav.def("shortest_rotation_delta", &fv::ShortestRotationDelta, "from_deg"_a,
          "to_deg"_a, "The signed shortest way round, in (-180, +180].");

  py::class_<fv::CameraSlew>(
      nav, "CameraSlew",
      "The map ARRIVES rather than teleports. Interpolates in GEO (the frame "
      "that survives the projection being re-centred by this very animation), "
      "longitude the short way.")
      .def(py::init<>())
      .def("set_settings", &fv::CameraSlew::SetSettings, "settings"_a)
      .def_property_readonly("settings", &fv::CameraSlew::settings)
      .def("reset", &fv::CameraSlew::Reset, "center"_a, "rotation_deg"_a = 0.0,
           "The map moved behind the slew's back. Cancels any animation.")
      .def_property_readonly("started", &fv::CameraSlew::started)
      .def("retarget", &fv::CameraSlew::Retarget, "proj"_a, "target"_a)
      .def("retarget_to", &fv::CameraSlew::RetargetTo, "proj"_a, "center"_a,
           "rotation_deg"_a)
      .def("advance", &fv::CameraSlew::Advance, "dt_s"_a)
      .def("finish", &fv::CameraSlew::Finish)
      .def_property_readonly(
          "center", [](const fv::CameraSlew& c) { return c.center(); })
      .def_property_readonly("rotation_deg", &fv::CameraSlew::rotation_deg)
      .def_property_readonly("active", &fv::CameraSlew::active)
      .def_property_readonly("duration_s", &fv::CameraSlew::duration_s);

  // ---- the overlay --------------------------------------------------------

  py::class_<fv::MovingMapTick>(
      nav, "MovingMapTick",
      "What one tick decided. `slew` is the thing a shell applies; the rest is "
      "what it displays.")
      .def_readonly("new_fix", &fv::MovingMapTick::new_fix)
      .def_readonly("fix", &fv::MovingMapTick::fix)
      .def_readonly("heading", &fv::MovingMapTick::heading)
      .def_readonly("target", &fv::MovingMapTick::target)
      .def_readonly("snap", &fv::MovingMapTick::snap,
                    "MM5: what the snapper made of this fix. snap.raw is "
                    "always what the receiver said.")
      .def_readonly("snap_applied", &fv::MovingMapTick::snap_applied,
                    "Whether the overlay acted on it (confidence >= "
                    "snap_min_confidence).")
      .def_readonly("slew", &fv::MovingMapTick::slew);

  py::class_<fv::MovingMapOverlay, fv::Overlay,
             std::shared_ptr<fv::MovingMapOverlay>>(
      nav, "MovingMapOverlay",
      "The ship on the chart (fv.movingmap, a STATIC type). Holds the feed, "
      "the heading resolver, the camera and the slew, and draws the ownship "
      "symbol rotated to the heading. tick() returns what to apply; it applies "
      "nothing itself.")
      .def(py::init<std::string>(), "name"_a = "Moving Map")
      // Bound here and not on Overlay: a Python overlay DEFINES on_draw and
      // never calls one, so the base class has none. A C++ overlay's draw is
      // normally reached through OverlayManager.draw_all; this is the direct
      // form, which is what a shell drawing the ship into its own canvas
      // wants and what the tests use.
      .def("on_draw",
           [](fv::MovingMapOverlay& o, const fv::MapProjection& proj,
              fv::ICanvas& canvas) { ThrowIfError(o.OnDraw(proj, canvas)); },
           "proj"_a, "canvas"_a,
           "Draws the ship AND recomputes the apron from where it was drawn -- "
           "the ordering MM2 depends on.")
      .def_property_readonly_static(
          "TYPE_ID", [](py::object) { return fv::MovingMapOverlay::kTypeId; })
      // --- the feed
      .def("set_source", &fv::MovingMapOverlay::SetSource, "source"_a,
           py::keep_alive<1, 2>(),
           "Wires the source's listener to this overlay's queue. Clears the "
           "queue and the heading history: fixes in flight belong to the feed "
           "that is going away.")
      .def_property_readonly("source", &fv::MovingMapOverlay::source)
      .def("start", [](fv::MovingMapOverlay& o) { ThrowIfError(o.Start()); })
      .def("stop", &fv::MovingMapOverlay::Stop)
      .def_property_readonly("running", &fv::MovingMapOverlay::running)
      .def("push_fix", &fv::MovingMapOverlay::PushFix, "fix"_a,
           "Feed one fix directly, bypassing the source.")
      // --- snap to road (MM5)
      .def("set_road_network", &fv::MovingMapOverlay::SetRoadNetwork,
           "network"_a, py::keep_alive<1, 2>(),
           "Hand over a nav.RoadGraphNetwork and every fix is put on the road "
           "it is most likely on BEFORE anything else sees it -- the heading "
           "resolver, the camera and the drawn symbol all take the snapped "
           "position. None turns it off, which is the default. tick().snap.raw "
           "is still what the receiver said.")
      .def_property_readonly("snapping", &fv::MovingMapOverlay::snapping)
      .def_property_readonly(
          "snapper",
          [](fv::MovingMapOverlay& o) -> fv::RoadSnapper& { return o.snapper(); },
          py::return_value_policy::reference_internal)
      .def_property("snap_min_confidence",
                    &fv::MovingMapOverlay::snap_min_confidence,
                    &fv::MovingMapOverlay::SetSnapMinConfidence,
                    "How sure the snapper has to be before the overlay uses "
                    "its answer rather than the raw fix. 0.25 by default; a "
                    "ship jumped onto a guess is worse than a ship a few "
                    "metres off the road it is on.")
      .def_property_readonly("last_snap", &fv::MovingMapOverlay::last_snap)
      // --- modes
      .def("set_modes", &fv::MovingMapOverlay::SetModes, "modes"_a,
           "FORCES the next tick to recentre -- otherwise turning "
           "auto-centring on would do nothing until the ship wandered out of "
           "an apron computed while it was off.")
      .def_property_readonly("modes", &fv::MovingMapOverlay::modes)
      .def("set_auto_center", &fv::MovingMapOverlay::SetAutoCenter, "on"_a)
      .def("set_auto_rotate", &fv::MovingMapOverlay::SetAutoRotate, "on"_a)
      .def("set_continuous", &fv::MovingMapOverlay::SetContinuous, "on"_a)
      .def("force_recenter", &fv::MovingMapOverlay::ForceRecenter)
      .def_property_readonly(
          "camera",
          [](fv::MovingMapOverlay& o) -> fv::MovingMapCamera& {
            return o.camera();
          },
          py::return_value_policy::reference_internal)
      .def_property_readonly(
          "slew",
          [](fv::MovingMapOverlay& o) -> fv::CameraSlew& { return o.slew(); },
          py::return_value_policy::reference_internal)
      .def("set_slew_settings", &fv::MovingMapOverlay::SetSlewSettings,
           "settings"_a)
      .def_property("map_rotation_deg", &fv::MovingMapOverlay::map_rotation_deg,
                    &fv::MovingMapOverlay::SetMapRotation)
      .def_property("convergence_deg", &fv::MovingMapOverlay::convergence_deg,
                    &fv::MovingMapOverlay::SetConvergence)
      .def_property("rotation_supported",
                    &fv::MovingMapOverlay::rotation_supported,
                    &fv::MovingMapOverlay::SetRotationSupported,
                    "Can this shell actually rotate the map? Default True (the "
                    "contract: tick() answers, the shell applies). Since PR3 a "
                    "shell CAN say yes -- MapProjection.set_rotation turns both "
                    "the vector and the raster path -- and a shell that drops "
                    "tick().slew.rotation_deg must still set this False, or the "
                    "ownship is counter-rotated to match a rotation that never "
                    "happened.\n\n"
                    "Saying yes also makes tick() adopt the rotation from the "
                    "projection it is handed, so the projection is the single "
                    "place the applied rotation is true.")
      .def("reset_map", &fv::MovingMapOverlay::ResetMap, "center"_a,
           "rotation_deg"_a = 0.0,
           "The map moved behind our back (a user pan, a bookmark).")
      // --- the tick
      .def("tick", &fv::MovingMapOverlay::Tick, "proj"_a, "dt_s"_a,
           "Drains the queue, resolves a heading, asks the camera and advances "
           "the slew. Every queued fix reaches the resolver; only the last "
           "reaches the camera.")
      // --- what has been seen
      .def_property_readonly("has_fix", &fv::MovingMapOverlay::has_fix)
      .def_property_readonly("last_fix", &fv::MovingMapOverlay::last_fix)
      .def_property_readonly("heading", &fv::MovingMapOverlay::heading)
      .def_property_readonly("has_drawn", &fv::MovingMapOverlay::has_drawn)
      .def_property_readonly("drawn_x", &fv::MovingMapOverlay::drawn_x)
      .def_property_readonly("drawn_y", &fv::MovingMapOverlay::drawn_y)
      .def_property_readonly("screen_angle_deg",
                             &fv::MovingMapOverlay::screen_angle_deg,
                             "heading + convergence - map rotation, "
                             "normalized: the angle the ship is DRAWN at.")
      .def_property_readonly("history_size",
                             [](fv::MovingMapOverlay& o) {
                               return o.heading_resolver().history_size();
                             })
      // --- how the ship is drawn
      .def_property("symbol_id", &fv::MovingMapOverlay::symbol_id,
                    &fv::MovingMapOverlay::SetSymbolId,
                    "Any builtin id; the default is symbol.builtin.OWNSHIP. A "
                    "platform that is not an aircraft wants builtin.NORTH.")
      .def_property("size_px", &fv::MovingMapOverlay::size_px,
                    &fv::MovingMapOverlay::SetSizePx)
      .def("set_color",
           [](fv::MovingMapOverlay& o, py::sequence c) { o.SetColor(ToColor(c)); },
           "color"_a)
      .def_property("show_edge", &fv::MovingMapOverlay::show_edge,
                    &fv::MovingMapOverlay::SetShowEdge)
      .def_property("highlighted", &fv::MovingMapOverlay::highlighted,
                    &fv::MovingMapOverlay::SetHighlighted)
      .def_property("show_apron", &fv::MovingMapOverlay::show_apron,
                    &fv::MovingMapOverlay::SetShowApron,
                    "Draws the apron as a dashed rectangle. The apron is the "
                    "hardest part of the moving map to believe.")
      .def_property("symbol_dpi_scale", &fv::MovingMapOverlay::symbol_dpi_scale,
                    &fv::MovingMapOverlay::SetSymbolDpiScale);
}

}  // namespace pyfvw
