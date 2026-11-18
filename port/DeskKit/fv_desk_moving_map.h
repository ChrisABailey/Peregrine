// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_moving_map.h — moving maps in the desktop app.
///
/// In Peregrine `fv.movingmap` is a file overlay, one per feed (`DeskMovingMap`).
/// A new one takes its `FeedConfig` from the default feed in the `movingmap.*`
/// settings (the demo replay, or NMEA over TCP or UDP) and records what it
/// receives; an opened GPX track or NMEA log is placed at the window's replay
/// clock time. Saving writes GPX.
///
/// `MovingMapOverlay` draws the ship and answers where the map should be; it
/// never moves the map. `MovingMapDriver` is the shell side for one overlay:
/// it runs the feed, snaps to the routing graph, and says what the view should
/// do each tick. UI thread only.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/geo.h"
#include "fvkit/nav/camera.h"
#include "fvkit/nav/heading.h"
#include "fvkit/nav/position.h"
#include "fvkit/nav/track_replay.h"
#include "fvkit/overlay/moving_map_overlay.h"

namespace fv {
class IRoadNetwork;
class ScriptedSource;
class MapProjection;
class Settings;

namespace desk {

/// Where a moving map's fixes come from. A live map's feed is one of the
/// first three; an opened file is `kReplay`.
enum class MovingMapFeed { kDemo, kNmeaTcp, kNmeaUdp, kReplay };

/// The `movingmap.feed` choice names, in `MovingMapFeed` order: demo, nmea_tcp, nmea_udp.
const std::vector<std::string>& MovingMapFeedNames();

/// The live feed the settings name: `movingmap.feed` when set, else NMEA over
/// TCP when `movingmap.nmea_host` is set, else the demo.
MovingMapFeed MovingMapFeedFromSettings(const Settings& settings);

/// One moving map's feed. A live map holds it from when it is created; only
/// its own `SetMovingMapFeed` changes it.
struct FeedConfig {
  MovingMapFeed kind = MovingMapFeed::kDemo;  ///< a live kind; replay maps ignore it
  std::string host;                           ///< the receiver, for NMEA over TCP
  uint16_t port = 10110;                      ///< TCP port to connect to, UDP port to listen on
  double demo_speed_mps = 15.0;
  double demo_scatter_m = 0.0;  ///< demo fixes moved by up to this, seeded
  bool snap_to_road = true;
  bool listening = true;  ///< false: the feed is stopped and the ship holds its last fix

  bool operator==(const FeedConfig& o) const;
  bool operator!=(const FeedConfig& o) const { return !(*this == o); }
};

/// The default feed: the `movingmap.*` feed keys a new moving map copies.
FeedConfig DefaultFeedConfig(const Settings& settings);

/// A `FeedConfig` as properties: what the Feed dialog edits and a workspace
/// saves. Keys: feed, nmea_host, nmea_port, speed_mps, noise_m, snap_to_road,
/// listening. A replay map's set declares snap_to_road alone. The dialog's
/// set (`for_dialog`) leaves out snap_to_road, which the toolbar's Snap to
/// Road button sets; the key is still read and written.
class FeedConfigProperties : public app::Properties {
 public:
  explicit FeedConfigProperties(bool replay = false, bool for_dialog = false);

  const std::vector<app::PropertySpec>& Describe() const override;
  Status GetProperty(const std::string& key, app::PropertyValue* out) const override;
  Status SetProperty(const std::string& key, const app::PropertyValue& value) override;

  const FeedConfig& config() const { return config_; }
  void set_config(const FeedConfig& config) { config_ = config; }
  /// Called after every accepted `SetProperty`.
  void set_on_change(std::function<void()> fn) { on_change_ = std::move(fn); }

 private:
  bool replay_;
  bool for_dialog_;
  FeedConfig config_;
  std::function<void()> on_change_;
};

/// How a moving map draws its trail, from the `movingmap.*` trail settings.
struct TrailStyle {
  bool dots = true;            ///< a dot at each trail fix (`show_trail_dots`)
  bool line = false;           ///< a line through the trail (`show_trail_line`)
  size_t trail_max = 5000;     ///< newest recorded fixes drawn on a live map
  size_t record_max = 500000;  ///< fixes a live map records before it stops
};

/// A contiguous run of fixes inside a vector the map owns.
struct FixSpan {
  const PositionFix* data = nullptr;
  size_t size = 0;
};

/// A moving map as a document: a recorded track it replays, or the recording
/// of fixes a live feed has delivered. Draws its trail under the ship.
class DeskMovingMap : public MovingMapOverlay, public app::Persistence {
 public:
  DeskMovingMap();

  app::Persistence* AsPersistence() override { return this; }

  /// An empty live map named "Moving Map N".
  Status FileNew() override;
  /// Reads `spec` as the track to replay (see `ReplayableFixes`); fewer than
  /// two replayable fixes is an error.
  Status FileOpen(const std::string& spec) override;
  /// Writes the track (replay) or the whole recording (live) as one GPX track.
  Status FileSaveAs(const std::string& spec, int format_index) override;

  /// True for an opened track, false for a live map.
  bool replay() const { return !track_.empty(); }
  /// The opened track's fixes, in file order; empty for a live map.
  const std::vector<PositionFix>& track() const { return track_; }
  /// The track's first and last fix times; 0 for a live map.
  double start_s() const { return track_.empty() ? 0.0 : track_.front().time_s; }
  double end_s() const { return track_.empty() ? 0.0 : track_.back().time_s; }
  /// The replay phase the ship is drawn in; before its start it is dimmed.
  void SetPhase(ReplayPhase phase);
  ReplayPhase phase() const { return phase_; }
  /// The replay clock time the ship is placed at; track fixes after it are
  /// drawn as the future.
  void SetReplayTime(double t_s) { replay_time_s_ = t_s; }

  /// Records a raw fix a live map has received. Marks the map dirty, unless
  /// the feed is the demo. At `record_max` fixes the recording stops.
  void AddTrailFix(const PositionFix& fix);
  /// Every fix recorded, oldest first: what Save writes.
  const std::vector<PositionFix>& recording() const { return recording_; }
  /// True once the recording reached `record_max` and dropped a fix.
  bool recording_full() const { return recording_full_; }
  /// The fixes the trail is drawn through: a replay's whole track, else the
  /// newest `trail_max` of the recording.
  FixSpan trail() const;
  void SetTrailStyle(const TrailStyle& style) { trail_style_ = style; }
  const TrailStyle& trail_style() const { return trail_style_; }

  /// Draws the trail, then the ship.
  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  /// The map's feed; false from `has_feed_config` until the Desk sets one.
  const FeedConfig& feed_config() const { return feed_props_.config(); }
  bool has_feed_config() const { return has_feed_config_; }
  void SetFeedConfig(const FeedConfig& config);
  /// Changes whenever the feed config does, by `SetFeedConfig` or a property.
  uint64_t feed_generation() const { return feed_generation_; }

  /// A live map reopens from its feed, not from a file it was saved to.
  std::string ConfigurationSpec() const override;
  app::Properties* ConfigurationProperties() override { return &feed_props_; }

 private:
  std::vector<PositionFix> track_;
  void DrawTrail(const MapProjection& proj, ICanvas& canvas) const;

  std::vector<PositionFix> recording_;
  bool recording_full_ = false;
  TrailStyle trail_style_;
  double replay_time_s_ = 0.0;
  FeedConfigProperties feed_props_;
  bool has_feed_config_ = false;
  uint64_t feed_generation_ = 0;
  ReplayPhase phase_ = ReplayPhase::kDuring;
};

/// The desktop descriptor for `fv.movingmap`: a file type over `.gpx` and
/// `.nmea`, saved as GPX. Replaces fvkit's static registration.
app::OverlayTypeDesc MovingMapFileType();

/// What a tick asks of the view.
struct MovingMapStep {
  bool move = false;    ///< set the camera to `center` / `rotation_deg`
  GeoPoint center;
  double rotation_deg = 0.0;
  bool redraw = false;  ///< a new fix arrived or the camera moved
  bool new_fix = false;  ///< `fix` and `heading` are this tick's
  PositionFix fix;       ///< the fix drawn: snapped when snapping applied
  /// The true heading at `fix`: reported, else the bearing of movement.
  ResolvedHeading heading;
};

class FeedReceivers;
class FeedSubscription;

/// Runs one moving map's feed and follows its ship.
class MovingMapDriver {
 public:
  /// `settings` and `receivers` must outlive the driver. A map with no feed
  /// config yet takes the default feed from `settings`.
  MovingMapDriver(Settings& settings, FeedReceivers& receivers,
                  std::shared_ptr<DeskMovingMap> overlay);
  ~MovingMapDriver();
  MovingMapDriver(const MovingMapDriver&) = delete;
  MovingMapDriver& operator=(const MovingMapDriver&) = delete;

  DeskMovingMap* overlay() const { return overlay_.get(); }

  /// Applies the settings and starts the map's feed. A replay map has no feed:
  /// it waits for `SetReplayTime`. A live map that is not listening starts
  /// nothing. The map is at `center` / `rotation_deg`; `demo_path` is the
  /// route the demo replays (fewer than two points circles `center`). A feed
  /// that fails to start is returned.
  Status Start(const GeoPoint& center, double rotation_deg, std::vector<GeoPoint> demo_path);
  /// Stops the feed.
  void Stop();

  /// Places a replay map at clock time `t_s` on the next `Tick`; ignored by a
  /// live map. `seek` marks a jump: the heading history, the road snap and
  /// the slew start again, so no heading is derived across it.
  void SetReplayTime(double t_s, bool seek);

  /// The road network fixes are snapped to; null turns snapping off. Applied
  /// only while the map's feed config snaps to roads.
  void SetRoadNetwork(std::shared_ptr<const IRoadNetwork> network);

  /// One frame: polls the feed, ticks the overlay over `proj` (the view as it
  /// is now) and says what to do with the view. With `follow` false the
  /// camera belongs to something else (a gesture, another moving map): the
  /// slew is re-based on the view instead of applied.
  MovingMapStep Tick(const MapProjection& proj, double dt_s, bool follow);

  /// The view moved by something other than `Tick` (a pan, a Go To).
  void MapMoved(const GeoPoint& center, double rotation_deg);
  /// Records the camera the view settled on after applying a tick's step, so
  /// the next tick does not mistake it for a user move.
  void NoteApplied(const GeoPoint& center, double rotation_deg);

  /// Re-reads the follow modes, slew and symbol settings, and the map's
  /// snapping, and pushes them into the overlay; a mode change recentres on
  /// the next fix.
  void ApplySettings();

  /// The true heading at the last fix; a raised map has it at once.
  const ResolvedHeading& heading() const { return heading_; }

  /// The status bar text: the road the ship is on and the feed's state.
  std::string StatusText() const;
  /// One line naming the feed: "demo", a track's file name, "host:port", "udp:port".
  std::string FeedDescription() const;
  MovingMapFeed feed() const { return feed_; }
  /// True when the map's feed config changed since `Start`.
  bool feed_stale() const { return started_generation_ != overlay_->feed_generation(); }
  /// The shared connection a live NMEA map listens to; null otherwise.
  const FeedSubscription* subscription() const { return subscription_.get(); }

 private:
  Status StartFeed(const GeoPoint& center, double rotation_deg, std::vector<GeoPoint> demo_path);

  Settings& settings_;
  FeedReceivers& receivers_;
  std::shared_ptr<DeskMovingMap> overlay_;
  std::shared_ptr<ScriptedSource> demo_;
  std::shared_ptr<FeedSubscription> subscription_;
  HeadingResolver true_heading_;  ///< no degrees-per-pixel: true bearings
  ResolvedHeading heading_;
  std::shared_ptr<const IRoadNetwork> network_;
  MovingMapFeed feed_ = MovingMapFeed::kDemo;
  uint64_t started_generation_ = 0;
  PositionFix held_;           ///< the last fix, handed back to the overlay by `Start`
  bool held_pending_ = false;  ///< `held_` not yet taken by a tick
  bool has_applied_ = false;
  GeoPoint applied_center_;
  double applied_rotation_ = 0.0;
  std::string feed_error_;
  bool replay_pending_ = false;  ///< `replay_time_` not yet placed
  bool replay_seek_ = false;     ///< the pending time is a jump
  double replay_time_ = 0.0;
  ResolvedHeading replay_heading_;  ///< the track's heading at the placed time
};

}  // namespace desk
}  // namespace fv
