// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// `fv.ais`: live AIS traffic. NMEA lines in, `VesselTable` inside, ship
/// symbols, course vectors, trails and labels out, with pick, search, a
/// details sheet and options. Plan: port/ais-plan.md (AIS3).

#ifndef FVKIT_OVERLAY_AIS_OVERLAY_H_
#define FVKIT_OVERLAY_AIS_OVERLAY_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fvkit/ais/decoder.h"
#include "fvkit/ais/openwaters.h"
#include "fvkit/ais/vessel_table.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
#include "fvkit/app/search.h"
#include "fvkit/nav/line_transport.h"
#include "fvkit/nav/position.h"
#include "fvkit/overlay/overlay.h"

namespace fv {

/// AIS traffic overlay. Lines arrive by `PushLine` (any thread) or from a
/// non-blocking transport; `Tick` on the UI thread decodes them into the
/// table. Everything else runs on the UI thread. The receiver's own
/// transponder (VDO) is kept in the table but never drawn, picked or searched.
class AisOverlay : public Overlay,
                   public app::HitTest,
                   public app::ItemInfo,
                   public app::SearchProvider,
                   public app::Properties {
 public:
  static const char kTypeId[];  // "fv.ais"

  explicit AisOverlay(std::string name = "AIS");

  app::HitTest* AsHitTest() override { return this; }
  app::ItemInfo* AsItemInfo() override { return this; }
  app::SearchProvider* AsSearch() override { return this; }
  app::Properties* AsProperties() override { return this; }

  /// One queued input: an NMEA line, or an already decoded message.
  struct Input {
    std::string line;
    bool has_message = false;
    ais::AisMessage message;
    double heard_s = 0.0;     ///< reception time; 0 uses the `Tick` clock
    std::string station;      ///< used when the line has no TAG block s:
    std::string attribution;  ///< used when the line has no TAG block t:
  };

  /// Queues one NMEA line for the next `Tick`. Thread safe.
  void PushLine(std::string line);

  /// Queues one input for the next `Tick`. Thread safe.
  void Push(Input input);

  /// Queues an Open Waters event's sentences (or its decoded fallback) with
  /// its reception time, station and credit line; other frames are ignored.
  /// Thread safe.
  void PushFrame(const ais::OpenWatersFrame& frame);

  /// Reads the transport on every `Tick` until it has nothing more; it must
  /// be non-blocking (TCP, UDP, a file). Null detaches.
  void SetTransport(std::shared_ptr<ILineTransport> transport);

  /// Decodes queued and transport lines at `now_s` (epoch seconds), then
  /// expires old records. Returns true when anything drawn may have changed.
  bool Tick(double now_s);

  /// Decodes one line now, on the calling (UI) thread. Advances the clock.
  void FeedLine(std::string_view line, double now_s);

  /// Ownship for CPA/TCPA in the details sheet; an empty fix clears it.
  void SetOwnship(const PositionFix& fix) { ownship_ = fix; }

  const ais::VesselTable& table() const { return table_; }
  ais::VesselTable& table() { return table_; }
  const ais::DecoderStats& decoder_stats() const { return decoder_.stats(); }

  /// Time of the last `Tick` or `FeedLine`, epoch seconds.
  double now_s() const { return now_s_; }

  /// Device pixels per point (2 or 3 on a phone).
  void SetSymbolDpiScale(double s) { dpi_scale_ = s > 0.0 ? s : 1.0; }

  /// Whether `v` is drawn under the current options.
  bool Shows(const ais::Vessel& v) const;

  /// Details for one MMSI, as the info sheet shows them.
  std::optional<app::ItemInfoSheet> Details(std::uint32_t mmsi) const;

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  // app::HitTest: `feature` is the MMSI.
  void HitTestPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                    std::vector<app::HitItem>& out) override;

  // app::ItemInfo
  std::optional<app::ItemInfoSheet> InfoAt(const MapProjection& proj, PixelPoint p,
                                           double tolerance_px) override;

  // app::SearchProvider: name, MMSI and call sign.
  void Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<app::SearchResult>& out) override;

  // app::Properties
  const std::vector<app::PropertySpec>& Describe() const override;
  Status GetProperty(const std::string& key, app::PropertyValue* out) const override;
  Status SetProperty(const std::string& key, const app::PropertyValue& value) override;

 private:
  void Decode(std::string_view line, double now_s);
  void Decode(const Input& input, double now_s);
  void ApplyTableOptions();
  /// Drawn hull length in device pixels.
  double HullLengthPx(const ais::Vessel& v) const;
  std::string FormatSpeed(double mps) const;

  ais::AisDecoder decoder_;
  ais::VesselTable table_;
  std::mutex queue_mutex_;
  std::deque<Input> queue_;
  std::shared_ptr<ILineTransport> transport_;
  PositionFix ownship_;
  double now_s_ = 0.0;
  double dpi_scale_ = 1.0;
  std::vector<app::PropertyValue> values_;
};

}  // namespace fv

#endif  // FVKIT_OVERLAY_AIS_OVERLAY_H_
