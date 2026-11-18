// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// ais_traffic.h — the peregrine.ais package's overlay: `fv::AisOverlay` fed
/// by the Open Waters network and a local NMEA receiver, with a collision
/// alarm against the host's ownship and a status and credit line on the map.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "fvkit/ais/openwaters.h"
#include "fvkit/app/overlay_host.h"
#include "fvkit/nav/nmea_receiver.h"
#include "fvkit/overlay/ais_overlay.h"
#include "sse_stream.h"

namespace peregrine_ais {

/// The settings the package adds to `fv::AisOverlay`'s own.
struct FeedSettings {
  bool open_waters = true;
  bool receiver = false;
  fv::ReceiverConfig receiver_config;
  bool cpa_alarm = true;
  double cpa_m = 926.0;     ///< 0.5 nm
  double tcpa_s = 600.0;
};

/// AIS traffic in Peregrine. While it has a host, a worker thread reads both
/// sources every 250 ms, advances the vessel table and asks for a redraw when
/// anything drawn changed. Every overridden entry point takes one lock, so
/// draws, hover and the worker may run on different threads.
class TrafficOverlay : public fv::AisOverlay, public fv::app::HostAware {
 public:
  /// `user_agent` goes on every Open Waters request. `run_worker` false
  /// leaves stepping to the caller (tests call `Step`).
  explicit TrafficOverlay(std::string user_agent, bool run_worker = true);
  ~TrafficOverlay() override;

  fv::app::HostAware* AsHostAware() override { return this; }

  /// Starts the worker on the first host and stops it when the host goes.
  void SetHost(fv::app::OverlayHost* host) override;

  /// One pass: reads both sources, ticks the table, checks CPA against the
  /// host's ownship and raises new alarms. Returns true when a redraw is due.
  bool Step(double now_s);

  /// MMSIs currently inside the alarm thresholds.
  std::set<std::uint32_t> Alarmed() const;
  /// The status line drawn under the map: sources and target count.
  std::string StatusLine() const;
  /// Credit lines of the sources heard, without repeats, in first-heard order.
  std::vector<std::string> Credits() const;
  /// The settings as last set.
  FeedSettings settings() const;

  fv::Status OnDraw(const fv::MapProjection& proj, fv::ICanvas& canvas) override;
  void HitTestPoint(const fv::MapProjection& proj, fv::PixelPoint p, double tolerance_px,
                    std::vector<fv::app::HitItem>& out) override;
  std::optional<fv::app::ItemInfoSheet> InfoAt(const fv::MapProjection& proj, fv::PixelPoint p,
                                               double tolerance_px) override;
  void Search(const fv::app::SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<fv::app::SearchResult>& out) override;

  const std::vector<fv::app::PropertySpec>& Describe() const override;
  fv::Status GetProperty(const std::string& key, fv::app::PropertyValue* out) const override;
  fv::Status SetProperty(const std::string& key, const fv::app::PropertyValue& value) override;

 private:
  void StartWorker();
  void StopWorker();
  void WorkerLoop();
  /// Reads the receiver into the overlay's queue. Worker thread only.
  void StepReceiver(const FeedSettings& s, double now_s);
  /// Opens, moves or closes the Open Waters stream. Worker thread only.
  void StepOpenWaters(const FeedSettings& s, double now_s);
  /// Updates `alarmed_` and returns the texts of alarms new this step.
  /// Caller holds `mutex_`.
  std::vector<std::string> CheckCpa(const FeedSettings& s, double now_s);
  void OnPayload(std::string_view payload);
  fv::Status DrawStatus(fv::ICanvas& canvas);

  const std::string user_agent_;
  const bool run_worker_;
  mutable std::recursive_mutex mutex_;  ///< the table, settings, view and status below
  FeedSettings settings_;
  std::vector<fv::app::PropertySpec> specs_;
  std::optional<fv::GeoRect> view_;  ///< the last drawn view
  fv::ais::OpenWatersLimits limits_;
  fv::PositionFix own_;  ///< the host's ownship at the last step
  std::set<std::uint32_t> alarmed_;
  std::string ow_status_;
  std::string receiver_status_;
  std::string label_font_;
  double display_scale_ = 1.0;

  // Worker-thread state.
  std::unique_ptr<fv::NmeaReceiver> receiver_;
  fv::ReceiverConfig receiver_config_;
  SseStream stream_;
  bool stream_wanted_ = false;
  fv::GeoRect box_{};
  double next_connect_s_ = 0.0;
  double backoff_s_ = 2.0;
  bool table_from_open_waters_ = false;

  std::thread worker_;
  std::mutex wake_mutex_;
  std::condition_variable wake_;
  bool stop_ = false;
};

}  // namespace peregrine_ais
