// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/nav/position.h — the fix and the feed seam (nav plan MM1).
//
// This is the left-hand edge of the moving map:
//
//   IPositionSource -> [FixQueue] -> HeadingResolver -> [MM2 camera] -> engine
//
// Ported in SHAPE from Applications/FalconView/MovingMapOverlay (GPSPointIcon
// carries the fix, IGPSFeed/IMovingMapFeed deliver it). The COM feeds stay on
// Windows: this is the D6 adapter seam, a plain C++ interface a scripted, an
// NMEA (MM6) or a platform source all implement.
//
// TWO THINGS ARE DELIBERATELY DIFFERENT FROM THE ORIGINAL.
//
// 1. EVERY FIELD CARRIES ITS OWN VALIDITY. FalconView spells "I have no
//    latitude" as -1000.0, "no heading" as -1.0 and "no altitude" as -32767,
//    and every consumer has to know which sentinel belongs to which field.
//    The sentinels do not port: a partial fix is what the NMEA sentences
//    genuinely deliver (GLL has no speed, VTG has no position at all), so the
//    partiality is real and is expressed once, here, as a `has_*` per field.
//    fix.Merge(other) is how two sentences of one epoch become one fix.
//
// 2. THREADING IS STATED UP FRONT. A source delivers on WHATEVER THREAD IT
//    LIKES — a serial reader has its own, a scripted one has the caller's —
//    and the listener runs there. A UI toolkit that owns its thread (tk, and
//    every native shell) must therefore not touch its widgets from a
//    listener; it puts a FixQueue in between and drains it on its own tick.
//    The queue is built here in MM1, not later when it hurts.

#ifndef FVKIT_NAV_POSITION_H_
#define FVKIT_NAV_POSITION_H_

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "fvkit/geo.h"

namespace fv {

// ---------------------------------------------------------------------------
// The fix
// ---------------------------------------------------------------------------

// One position report. Units are FvKit's throughout (D2/D4): degrees for
// geography, METRES for altitude, METRES PER SECOND for speed, degrees
// clockwise from north for headings, epoch SECONDS (double, so sub-second
// fixes survive) for time. NMEA's knots and its hhmmss.ss are converted in
// the NMEA source (MM6), never here.
//
// A default-constructed fix is the empty one: nothing valid, and
// `has_position` false is what every consumer tests first.
struct PositionFix {
  // Where. lat/lon are meaningless unless has_position.
  double lat = 0.0;
  double lon = 0.0;
  bool has_position = false;

  // Height above mean sea level, metres. (An NMEA GGA carries the geoid
  // separation beside it; the source applies it, so this is always MSL.)
  double altitude_msl_m = 0.0;
  bool has_altitude = false;

  // Ground speed, metres per second.
  double speed_mps = 0.0;
  bool has_speed = false;

  // Course over ground, degrees clockwise from TRUE north. This is the
  // heading the ownship symbol is drawn at when it is valid; when it is not,
  // HeadingResolver derives one (fvkit/nav/heading.h).
  double true_heading_deg = 0.0;
  bool has_true_heading = false;

  // The same course relative to MAGNETIC north. Carried, never converted:
  // the conversion needs a magnetic model (geo_tool's geomag) and a date, and
  // a receiver that reports both is not to be second-guessed.
  double magnetic_heading_deg = 0.0;
  bool has_magnetic_heading = false;

  // Epoch seconds (UTC). Not the arrival time — the time the RECEIVER
  // stamped, which is what makes a recorded log replayable.
  double time_s = 0.0;
  bool has_time = false;

  // Horizontal dilution of precision, and how many satellites produced it.
  // MM5 sizes its snap radius off hdop, which is the only reason a quality
  // number is at this seam at all.
  double hdop = 0.0;
  bool has_hdop = false;
  int satellite_count = 0;
  bool has_satellite_count = false;

  GeoPoint position() const { return GeoPoint{lat, lon}; }

  void SetPosition(double lat_deg, double lon_deg) {
    lat = lat_deg;
    lon = lon_deg;
    has_position = true;
  }

  // Copies every VALID field of `other` over this one, leaving the rest
  // alone. This is how a GGA (position + altitude + quality) and the RMC
  // (position + speed + course) of the same second become one fix, and it is
  // why the validity flags are per field rather than per fix.
  void Merge(const PositionFix& other);
};

// Wraps a heading into [0, 360). Every heading this layer produces has been
// through it; a heading a source REPORTS is left exactly as reported.
double NormalizeHeadingDeg(double degrees);

// ---------------------------------------------------------------------------
// The feed seam
// ---------------------------------------------------------------------------

// What a source hands its listener. A std::function and not a one-method
// interface: there is exactly one thing to deliver, a shell wants to write a
// lambda, and pybind binds a Python callable to it for free (D1 reserves
// shared_ptr for interfaces the app HOLDS — a callback is not one).
using FixCallback = std::function<void(const PositionFix&)>;

// A feed. Held as std::shared_ptr<IPositionSource> per D1.
//
// Start()/Stop() may be called repeatedly; both are idempotent. A source that
// has been stopped may be started again, and a scripted one replays from the
// top when it is (which is what makes the demo track re-runnable).
class IPositionSource {
 public:
  virtual ~IPositionSource() = default;
  IPositionSource(const IPositionSource&) = delete;
  IPositionSource& operator=(const IPositionSource&) = delete;

  virtual Status Start() = 0;
  virtual void Stop() = 0;
  virtual bool running() const = 0;

  // ONE listener, replaced by a second call, cleared by an empty function.
  // Fan-out is the app's business and costs it a lambda; making the source
  // own a list would put a per-source subscription lifetime into every
  // implementation for a case no shell has yet.
  //
  // Set it BEFORE Start(): a source is allowed to deliver its first fix from
  // inside Start().
  virtual void SetListener(FixCallback listener) = 0;

 protected:
  IPositionSource() = default;
};

// The listener storage and the running flag every source would otherwise
// repeat. D1 keeps IPositionSource free of data members; this is where the
// data lives, and every source in the port derives from it.
class PositionSourceBase : public IPositionSource {
 public:
  void SetListener(FixCallback listener) override { listener_ = std::move(listener); }
  bool running() const override { return running_; }

 protected:
  // Delivers to the listener if there is one. Runs on the caller's thread,
  // which is the whole of the threading rule at the top of this file.
  void Emit(const PositionFix& fix) const {
    if (listener_) listener_(fix);
  }

  bool running_ = false;

 private:
  FixCallback listener_;
};

// ---------------------------------------------------------------------------
// The thread crossing
// ---------------------------------------------------------------------------

// A bounded queue of fixes, filled from a source's thread and drained on the
// consumer's tick. `Listener()` is the FixCallback to hand to
// IPositionSource::SetListener, so wiring a threaded source into a
// single-threaded UI is two lines and no locking of the UI's own state.
//
// FULL DROPS THE OLDEST. A position feed is a stream of the present: when the
// consumer has fallen behind, the fix it has not yet read is the one that
// matters least. `dropped()` counts, so a shell can say so instead of
// silently animating history.
class FixQueue {
 public:
  explicit FixQueue(std::size_t capacity = 64) : capacity_(capacity ? capacity : 1) {}

  void Push(const PositionFix& fix);

  // The callback form of Push. Captures `this`: the queue must outlive the
  // source it is wired to.
  FixCallback Listener();

  // Moves everything queued into `out` (appending) and empties the queue.
  // Returns how many were taken.
  std::size_t Drain(std::vector<PositionFix>* out);

  // The common case: only the newest fix matters, the rest are history the
  // camera would only have to catch up through. Returns false when empty.
  bool DrainLatest(PositionFix* out);

  void Clear();

  std::size_t size() const;
  bool empty() const { return size() == 0; }
  std::size_t capacity() const { return capacity_; }
  std::size_t dropped() const;

 private:
  mutable std::mutex mutex_;
  std::deque<PositionFix> fixes_;
  std::size_t capacity_;
  std::size_t dropped_ = 0;
};

}  // namespace fv

#endif  // FVKIT_NAV_POSITION_H_
