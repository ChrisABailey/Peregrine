// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPMap.h — PippinKit's drawing side.
//
// PPMap owns the offline data pack, the OSM vector source, style engine and
// renderer, the overlay stack, and the CPU canvas everything lands in. It
// does not own the camera: where the map looks is a `PPViewport`, an
// immutable value the main thread moves and hands over, so this class is the
// pure "draw me that viewport" half.
//
// Threading contract:
//
//     A PPMap belongs to one queue at a time, and in Pippin that queue is the
//     render queue. Nothing here is thread-safe and nothing here is a lock.
//
// That is affordable because everything crossing the boundary is a value:
// `PPViewport` in, `PPFrame` out, and the frame's image is a copy of the
// canvas (`CpuCanvas` reuses its buffer, so the memcpy is what makes the
// image safe to hand to another thread).
//
// Two things are deliberately absent. `MapEngine`, because it is a catalog
// plus a raster compositor and this pack has no raster in it, so it would be
// an empty catalog and a `RenderBaseMap` that draws nothing; PythonView's own
// vector path drives a bare `MapProjection` for the same reason. The day
// Pippin bundles a raster chart the engine goes in beside the projection the
// viewport already carries. And the render loop, because a display link, a
// dirty flag and a queue are about when a frame is wanted, which is the
// shell's business (`MapModel.swift`); this class is about what one costs.
//
// The moving map is in the overlay stack, which means a frame can change
// while the camera stands still. See `pushFix:` and `PPFrame.ownship`.

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#import <PippinKit/PPAis.h>
#import <PippinKit/PPFix.h>
#import <PippinKit/PPGeometry.h>
#import <PippinKit/PPPoint.h>
#import <PippinKit/PPGuidance.h>
#import <PippinKit/PPTrip.h>
#import <PippinKit/PPRoute.h>
#import <PippinKit/PPSearch.h>
#import <PippinKit/PPTide.h>
#import <PippinKit/PPWind.h>
#import <PippinKit/PPViewport.h>

NS_ASSUME_NONNULL_BEGIN

/// Errors from `PippinKit`. `NSLocalizedDescriptionKey` carries the
/// `fv::Status` message verbatim; this layer does not paraphrase the C++.
FOUNDATION_EXPORT NSErrorDomain const PPErrorDomain;

typedef NS_ERROR_ENUM(PPErrorDomain, PPErrorCode) {
  PPErrorPackNotFound = 1,   ///< no `pippin.ini` where we were pointed
  PPErrorPackIncomplete = 2, ///< a settings key names a file that is not there
  PPErrorRenderFailed = 3,   ///< the renderer returned a bad Status
  PPErrorRouteFailed = 4,    ///< a saved route would not read
  PPErrorPointsFailed = 5,   ///< a saved point set would not read
  PPErrorRecordFailed = 6,   ///< a ride would not be written
  PPErrorChartImportFailed = 7, ///< a chart zip would not install or open
};

/// Where the ship was, and pointing where, in the frame just drawn.
///
/// Reported rather than asked for, because the overlay's tick happens on the
/// render queue and this is the main thread's only honest view of it. The
/// shell draws the ship from it as a sprite (`symbolImage`), placed at
/// `coordinate` against the live camera.
NS_SWIFT_SENDABLE
@interface PPOwnship : NSObject

- (instancetype)init NS_UNAVAILABLE;

@property(nonatomic, readonly) PPGeoPoint coordinate;

/// The angle the symbol was stamped at: heading + convergence − the map's own
/// rotation, in degrees clockwise, [0, 360). Not the heading — on a turned
/// chart the two differ by exactly the turn — and this is the one the drawing
/// is pinned against.
@property(nonatomic, readonly) double screenAngleDegrees;

/// The camera rotation `screenAngleDegrees` was computed against. On a live
/// camera turned by `live.rotationDegrees - viewRotationDegrees`, the sprite
/// turns by the same amount, as a drawn layer would.
@property(nonatomic, readonly) double viewRotationDegrees;

/// The ship, nose up, centred on a transparent square of device pixels; nil
/// if it could not be drawn. Turned clockwise by the screen angle for display.
@property(nonatomic, readonly, nullable) CGImageRef symbolImage;
/// Device pixels per point in `symbolImage`.
@property(nonatomic, readonly) double symbolPixelsPerPoint;

/// YES when the heading came from the receiver's course, NO when
/// `HeadingResolver` derived it from successive positions. The two are not
/// the same quantity: a reported course is a true bearing and a derived one
/// is a screen angle.
@property(nonatomic, readonly) BOOL headingIsReported;
@property(nonatomic, readonly) BOOL hasHeading;

@property(nonatomic, readonly) double speedMetersPerSecond;
@property(nonatomic, readonly) BOOL hasSpeed;

/// Course over ground as the receiver reported it, degrees true, [0, 360).
/// NO when the receiver gave none; a heading derived from movement is a
/// screen angle and is not offered here.
@property(nonatomic, readonly) double courseDegrees;
@property(nonatomic, readonly) BOOL hasCourse;

/// The fix was put on a road before anything else saw it, so `coordinate` is
/// the snapped one. Always NO outside GPS mode, where the snapper is off.
///
/// Reported because it is the only way to see the snapper working: a snapped
/// ship and an unsnapped one differ by a few metres, which cannot be read off
/// a chevron. `-PPShowStats YES` prints it.
@property(nonatomic, readonly) BOOL isSnappedToRoad;
/// The road the ship was put on, or empty. An unnamed lane snaps as readily
/// as a named one, so this is empty more often than `isSnappedToRoad` is NO.
@property(nonatomic, readonly, copy) NSString *roadName;

/// The receiver's stamp on the fix the ship was drawn from.
@property(nonatomic, readonly) NSTimeInterval timestamp;
@property(nonatomic, readonly) BOOL hasTimestamp;

@end

/// A route waypoint or point marker shown upright over a tilted map: its
/// diamond or badge and its name, as a sprite the shell places at
/// `coordinate` through the perspective.
NS_SWIFT_SENDABLE
@interface PPBillboard : NSObject

- (instancetype)init NS_UNAVAILABLE;

@property(nonatomic, readonly) PPGeoPoint coordinate;
/// The marker and its name, trimmed to their ink, in device pixels.
@property(nonatomic, readonly) CGImageRef image;
/// Device pixels per point in `image`.
@property(nonatomic, readonly) double pixelsPerPoint;
/// From the marker's own centre to the image's centre, in points. The name
/// sits to the right, so the image is not centred on the marker.
@property(nonatomic, readonly) CGPoint centerOffset;

@end

/// One drawn frame: the images, the viewports they were drawn at, and what
/// they cost.
NS_SWIFT_SENDABLE
/// A frame is two layers.
///
/// `baseImage` is the map, drawn into a surface larger than the screen (the
/// guard band) and kept between frames, so a camera that has only panned,
/// turned a little, or not moved gets the same pixels back for nothing.
/// `overlayImage` is the route and the points, drawn on a transparent surface
/// at the band's camera and kept with the base: it is redrawn when the base
/// is, or when their content changes. The ownship is not in either layer; the
/// shell draws it from `ownship.symbolImage`. On a tilted frame the overlay
/// holds only the route line, and the markers arrive as `billboards`.
///
/// The compositor puts them together, which is what makes the band cheap:
/// `MapScreen`'s preview transform is applied twice, against two viewports,
/// on the GPU. So there is no blitter in this app, rotation and scale cost
/// nothing, and course-up is served like any other mode.
///
/// `viewport` is the camera the frame was asked for; it is the frame's
/// identity, which `applyCamera` and the loop's "already showing this" test
/// read. `baseViewport` is the band — the same camera on a larger canvas —
/// and is usually an older camera, because that is what a cache hit means.
/// `overlayViewport` is the band the overlay was drawn at, normally
/// `baseViewport` itself.
@interface PPFrame : NSObject

/// The route and points, at `overlayViewport`, on a transparent surface.
/// Composited over `baseImage`.
@property(nonatomic, readonly) CGImageRef overlayImage;
@property(nonatomic, readonly) PPViewport *overlayViewport;
/// The route's waypoints and the points, in draw order, when `viewport` is
/// tilted; empty when it is flat and they are in `overlayImage`.
@property(nonatomic, readonly) NSArray<PPBillboard *> *billboards;
@property(nonatomic, readonly) PPViewport *viewport;

/// The map, at `baseViewport`, opaque. Never nil on a frame that returned.
@property(nonatomic, readonly) CGImageRef baseImage;
@property(nonatomic, readonly) PPViewport *baseViewport;

/// NO when this frame was served the cached base map.
@property(nonatomic, readonly) BOOL baseWasDrawn;

@property(nonatomic, readonly) NSUInteger featuresQueried;
@property(nonatomic, readonly) NSUInteger drawsEmitted;
@property(nonatomic, readonly) NSInteger queryZoom;
/// The whole frame. On a base hit, the tick and any overlay redraw.
@property(nonatomic, readonly) double renderMilliseconds;
/// The base map's share, and 0 on a hit. `featuresQueried`, `drawsEmitted`
/// and `queryZoom` describe the base map, so on a hit they describe the
/// cached one: they are last-drawn numbers, not last-frame ones.
@property(nonatomic, readonly) double baseMilliseconds;

/// The ownship as this frame drew it, or nil when no fix has arrived.
@property(nonatomic, readonly, nullable) PPOwnship *ownship;

/// The ride's numbers as of this frame, or nil outside GPS mode. The trip
/// runs with the mode, so nil means "no ride in progress" rather than a
/// missing number, and the bar it feeds is hidden in the same breath.
@property(nonatomic, readonly, nullable) PPTrip *trip;

/// The next turn as of this frame, or nil when there is no banner to draw:
/// outside GPS mode, with no planned route, or past the destination. Off
/// route is not one of those — the guidance is running and has stopped naming
/// a corner, which `PPGuidance.onRoute` reports.
@property(nonatomic, readonly, nullable) PPGuidance *guidance;

/// What the guidance said since the previous frame, in order — usually
/// empty, one entry on the frame that crosses a ring. Buffered rather than
/// sampled, so a frame that swallowed two fixes carries both fixes' events.
/// Each event is delivered on exactly one frame.
@property(nonatomic, readonly) NSArray<PPGuidanceEvent *> *guidanceEvents;

/// The course deviation indicator as of this frame, or nil outside GPS mode,
/// with no straight-leg route, or past the last point. Its events ride on
/// `guidanceEvents`.
@property(nonatomic, readonly, nullable) PPCourseDeviation *courseDeviation;

#pragma mark - What the camera decided

/// The moving map's answer travels on the frame.
///
/// MM2's contract is that the camera never touches the map: `Tick` answers a
/// centre and a rotation and the shell applies them. Here the tick happens on
/// the render queue and the camera the shell drives is a `PPViewport` on the
/// main thread, so the answer has to cross a queue — and the frame already
/// crosses it, carrying a value, once per tick. No second channel, no lock,
/// and no way for the answer to arrive without the frame it belongs to.
///
/// The shell applies the centre and rotation to its own current viewport,
/// keeping its own scale, so a pinch in flight is not undone by a camera that
/// has never heard of it.
@property(nonatomic, readonly) BOOL hasCameraUpdate;
@property(nonatomic, readonly) PPGeoPoint cameraCenter;
@property(nonatomic, readonly) double cameraRotationDegrees;

/// The slew is still in flight and another frame will move again. What keeps
/// the shell's display link awake, and the reason leaving GPS mode turns the
/// chart back to north smoothly rather than stopping halfway.
@property(nonatomic, readonly) BOOL cameraIsAnimating;

@end

/// The map two zoom levels out at screen size, shown under the base layer so
/// the screen past the guard band is soft rather than blank. Built by
/// `renderUnderlayForViewport:error:` when the loop is idle and kept by the
/// shell; it carries no overlay.
NS_SWIFT_SENDABLE
@interface PPUnderlay : NSObject
- (instancetype)init NS_UNAVAILABLE;

/// The map at `viewport`, opaque, screen-sized.
@property(nonatomic, readonly) CGImageRef image;
/// Where `image` was drawn: `PPViewport.forUnderlay()` of the camera asked for.
@property(nonatomic, readonly) PPViewport *viewport;
/// The style's background colour at the camera asked for, opaque. The shell
/// paints it behind every layer, so the worst case is land-coloured.
@property(nonatomic, readonly) CGColorRef backgroundColor;
/// What the draw cost.
@property(nonatomic, readonly) double renderMilliseconds;
@end

/// What a moving-map tick decided when no frame was drawn: the same camera
/// answer, ownship and ride numbers a `PPFrame` carries, without the images.
/// See `stepCameraAtViewport:`.
@interface PPCameraStep : NSObject
@property(nonatomic, readonly, nullable) PPOwnship *ownship;
@property(nonatomic, readonly, nullable) PPTrip *trip;
@property(nonatomic, readonly, nullable) PPGuidance *guidance;
@property(nonatomic, readonly) NSArray<PPGuidanceEvent *> *guidanceEvents;
@property(nonatomic, readonly, nullable) PPCourseDeviation *courseDeviation;
@property(nonatomic, readonly) BOOL hasCameraUpdate;
@property(nonatomic, readonly) PPGeoPoint cameraCenter;
@property(nonatomic, readonly) double cameraRotationDegrees;
@property(nonatomic, readonly) BOOL cameraIsAnimating;
@end

/// The slider a map key row offers for its setting.
typedef NS_ENUM(NSInteger, PPLegendSettingKind) {
  /// No slider: pace, paved surface, or a row with no single setting.
  PPLegendSettingKindNone = 0,
  /// A road type, on the tier scale (Most preferred … Never).
  PPLegendSettingKindRoadType = 1,
  /// A lane, route, unpaved surface or golf way, on the relative scale
  /// (Much better … Much worse).
  PPLegendSettingKindModifier = 2,
  /// Private, toll, ferry or beach: named by the row, adjusted elsewhere.
  PPLegendSettingKindCondition = 3,
};

/// A modifier row's specimen moved onto one road class, for the key's
/// "On the map this becomes" line.
@interface PPLegendOnRoad : NSObject
- (instancetype)init NS_UNAVAILABLE;
/// The key's name for the class, e.g. "Busy road".
@property(nonatomic, readonly, copy) NSString *label;
@property(nonatomic, readonly, copy) NSString *tierWord;
/// As `PPLegendEntry.tierRank`.
@property(nonatomic, readonly) NSInteger tierRank;
/// NaN when excluded.
@property(nonatomic, readonly) double factor;
@end

/// One row of the map key: a style legend entry drawn at the map's scale,
/// with the active rules' tier for the OSM tags it stands for.
@interface PPLegendEntry : NSObject
- (instancetype)init NS_UNAVAILABLE;
@property(nonatomic, readonly, copy) NSString *identifier;
/// Two to four words, e.g. "Painted bike lane".
@property(nonatomic, readonly, copy) NSString *label;
/// The style's text: how the line is drawn and the OSM tags behind it.
@property(nonatomic, readonly, copy) NSString *detail;
/// How the router weighs the row, in a rider's words; empty when the row
/// names no OSM tags.
@property(nonatomic, readonly, copy) NSString *routerSentence;
/// The multipliers behind `factor`, e.g. "secondary 2.5 × painted lane 0.6 = 1.5".
@property(nonatomic, readonly, copy) NSString *termsLine;
/// The rules' word for the row ("Preferred", "Never"); empty with no OSM tags.
@property(nonatomic, readonly, copy) NSString *tierWord;
/// The tier's position in the rules' `legend_tiers`, cheapest first; -1 for
/// "Never" and for a row with no OSM tags.
@property(nonatomic, readonly) NSInteger tierRank;
/// Cost per metre relative to the profile's own speed; NaN when excluded or
/// when the row has no OSM tags.
@property(nonatomic, readonly) double factor;
/// The zoom from which the map draws the row; negative when it never does.
@property(nonatomic, readonly) double minZoom;
/// The map draws the row at the scale the legend was asked for.
@property(nonatomic, readonly, getter=isShown) BOOL shown;
/// Every layer that draws the row's swatch drew in the last base map render,
/// and no earlier row on the map has an identical swatch. Before the first
/// render, `shown` stands in for the layer test.
@property(nonatomic, readonly, getter=isOnMap) BOOL onMap;
/// The specimen drawn by the style, `swatchPixelsPerPoint` pixels per point.
@property(nonatomic, readonly, nullable) CGImageRef swatch;
@property(nonatomic, readonly) double swatchPixelsPerPoint;
/// The profile setting the row's slider moves, e.g. "classes.tertiary" or
/// "bike_lane_factor.lane"; the key in `PPMap.routeAdjustments`. Empty when
/// the row has no single setting.
@property(nonatomic, readonly, copy) NSString *settingId;
@property(nonatomic, readonly) PPLegendSettingKind settingKind;
/// The key shows a lock instead of a slider: the shipped profile's
/// `key_locks` name the setting (a motorway for bikes), or the way's access
/// tags or a non-class rule close it. A class the rules merely exclude is
/// adjustable from Never.
@property(nonatomic, readonly, getter=isLocked) BOOL locked;
/// The reason the key shows with the lock; empty when not locked.
@property(nonatomic, readonly, copy) NSString *lockReason;
/// For a row with no single setting: the modifier and condition settings
/// behind its cost, in the order they apply ("set by Rough surface and Local
/// route"). Empty otherwise.
@property(nonatomic, readonly, copy) NSArray<NSString *> *termSettingIds;
/// The slider's stop words, best first; empty when the row has no slider.
@property(nonatomic, readonly, copy) NSArray<NSString *> *stopWords;
/// The stop the setting is on with the adjustments applied, and the stop
/// holding the shipped value (the "default" mark). -1 with no slider.
@property(nonatomic, readonly) NSInteger stop;
@property(nonatomic, readonly) NSInteger defaultStop;
/// For a modifier row: the result on a quiet street, a moderate road, a busy
/// road and an arterial, adjustments applied. Empty for other rows.
@property(nonatomic, readonly, copy) NSArray<PPLegendOnRoad *> *onRoads;
@end

/// A titled group of legend rows, in the style's order.
@interface PPLegendGroup : NSObject
- (instancetype)init NS_UNAVAILABLE;
@property(nonatomic, readonly, copy) NSString *title;
@property(nonatomic, readonly, copy) NSString *subtitle;
@property(nonatomic, readonly, copy) NSArray<PPLegendEntry *> *entries;
@end

/// A feature of the base map under a press, as the OSM tiles describe it.
/// What a new point is prefilled from.
NS_SWIFT_SENDABLE
@interface PPChartFeature : NSObject
- (instancetype)init NS_UNAVAILABLE;
/// The feature's own name ("Sanctuary Hotel"), or empty when nothing named
/// was drawn under the press.
@property(nonatomic, readonly, copy) NSString *name;
/// What kind of thing it is, from the tile's `class`, else `subclass`
/// ("grass", "pitch", "town"), with underscores as spaces. Never empty.
@property(nonatomic, readonly, copy) NSString *kind;
@end

/// Which S-52 display categories a chart draws; each includes the ones before.
typedef NS_ENUM(NSInteger, PPChartCategory) {
  PPChartCategoryBase = 0,
  PPChartCategoryStandard = 1,
  PPChartCategoryAll = 2,
};

/// The chart's colour table. Automatic is day, or night while `darkStyle` is on.
typedef NS_ENUM(NSInteger, PPChartPalette) {
  PPChartPaletteAutomatic = 0,
  PPChartPaletteDay = 1,
  PPChartPaletteDusk = 2,
  PPChartPaletteNight = 3,
};

/// One ENC feature under a tap, for the chart info sheet.
NS_SWIFT_SENDABLE
@interface PPChartFeatureInfo : NSObject
- (instancetype)init NS_UNAVAILABLE;
/// The object's name (OBJNAM), else its class name.
@property(nonatomic, readonly, copy) NSString *title;
/// The S-57 class name ("Light"); equal to `title` for an unnamed feature.
@property(nonatomic, readonly, copy) NSString *kind;
/// The S-57 class acronym ("LIGHTS").
@property(nonatomic, readonly, copy) NSString *acronym;
/// Chart notation ("Fl(2) G 6s 12m 5M", "Depth 2–5 m"); may be empty.
@property(nonatomic, readonly, copy) NSString *summary;
/// Attribute names in record order, and their decoded values at the same index.
@property(nonatomic, readonly, copy) NSArray<NSString *> *attributeNames;
@property(nonatomic, readonly, copy) NSArray<NSString *> *attributeValues;
/// Cell, edition and record id.
@property(nonatomic, readonly, copy) NSString *source;
@end

/// What `-[PPMap replanForAdjustments]` did to the route on screen.
NS_SWIFT_SENDABLE
@interface PPRouteReplan : NSObject
- (instancetype)init NS_UNAVAILABLE;
@property(nonatomic, readonly) PPRoute *route;
/// The route's length before the re-plan, metres.
@property(nonatomic, readonly) double previousLengthMeters;
/// The named road the new route rides least of what the old one rode, or
/// empty (`fv::RoadDropped`).
@property(nonatomic, readonly, copy) NSString *avoidedRoad;
/// The drawn line moved, so the previous one is shown faded.
@property(nonatomic, readonly) BOOL lineChanged;
@end

@interface PPMap : NSObject

/// Opens the bundled data pack: the directory holding `pippin.ini` and
/// everything that file names by a bundle-relative path.
///
/// Everything is opened here, at construction, so a pack missing a piece
/// fails now with the file name in the error rather than as a blank map at
/// 3 pm on a bike.
- (nullable instancetype)initWithDataPackURL:(NSURL *)packURL
                                       error:(NSError **)error
    NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

/// The pack's camera before it has met a screen: centred on the data, with
/// the pack's limits on it. A shell gives it a surface size, asks for the
/// home view, and owns the viewport from then on. Called once.
- (PPViewport *)initialViewport;

/// The guard band: how much larger than the screen the base map is drawn, as
/// a fraction added on each side. `display.base_cache_band_margin`, 0.25 by
/// default, which is 2.25x the pixels and about 600 m of riding absorbed
/// before the map is drawn again.
///
/// The shell reads it once and passes it back per frame, because which frames
/// should pay for a band is the shell's question. See
/// `renderViewport:band:reuseBase:error:`.
@property(nonatomic, readonly) double baseCacheBandMargin;

/// During a gesture the band is redrawn early once the screen, predicted one
/// draw ahead, comes within this fraction of the band margin of its edge.
/// `display.band_refresh_fraction`, 0.5 by default.
@property(nonatomic, readonly) double baseCacheRefreshFraction;

/// How far the chart may turn under a composited base map before it is drawn
/// again, in degrees. `display.base_cache_max_turn_deg`, 2.5 by default. The
/// shell reads it for `PPViewport`'s `covers(_:maxTurnDegrees:)`, so both
/// sides ask the same question with the same number.
@property(nonatomic, readonly) double baseCacheMaxTurnDegrees;

/// Release speed below which a drag stops rather than coasting, in points per
/// second. `display.fling_min_speed_pt`, 200 by default.
@property(nonatomic, readonly) double flingMinSpeedPoints;

/// The fling's deceleration as the speed kept per millisecond, UIKit's unit.
/// `display.fling_deceleration`, 0.998 (`DecelerationRate.normal`) by default.
@property(nonatomic, readonly) double flingDecelerationRate;

/// The tilt while following, in degrees, from
/// `display.follow_pitch_deg` (45). `PPViewport.pitched(_:)` clamps it.
@property(nonatomic, readonly) double followPitchDegrees;

/// Frames per second while following with no gesture, from
/// `display.follow_fps` (20). Unclamped; `FramePolicy` clamps it.
@property(nonatomic, readonly) double followFramesPerSecond;

/// Smallest camera movement in points that earns a follow frame, from
/// `display.min_move_pt` (0.25).
@property(nonatomic, readonly) double followMinMovePoints;

/// The map's extent: `pippin.home_bounds`, or the coverage of an imported
/// chart set once `openChartsAtURL:` has opened one.
@property(nonatomic, readonly) PPGeoBounds homeBounds;

/// The pack's tide table and rideable threshold (`[tides]`, `[beach]`), or
/// nil when the pack carries none or it would not load.
@property(nonatomic, readonly, nullable) PPTide *tide;

/// Where to ask for the wind and what to warn at (`[wind]`, and `[beach]
/// faces_deg`), or nil when the pack names no forecast point.
@property(nonatomic, readonly, nullable) PPWindSettings *wind;

/// Draws `viewport` and hands back a frame the caller owns.
///
/// Returns nil and fills `error` when the renderer refuses or the viewport
/// has no surface size yet. A frame with nothing in it — out past the pack's
/// edge — is not an error and comes back as the style's background colour.
- (nullable PPFrame *)renderViewport:(PPViewport *)viewport
                               error:(NSError **)error
    NS_SWIFT_NAME(render(_:));

/// The same, with the base cache's two knobs.
///
/// `band` is the viewport the base map is drawn at when it has to be drawn at
/// all: normally the live camera grown by `baseCacheBandMargin`
/// (`PPViewport.grown(byMargin:)`), and nil for the screen itself. It is the
/// caller's to choose per frame: a band is paid for on every miss and earns
/// its keep only on hits, so the shell asks for none during a pinch, where
/// the scale moves so no cached base can serve it (`PPBaseCoverage.h`). It
/// may be centred ahead of the live camera, and it may be rotation-safe; it
/// must share the live scale and rotation.
///
/// `reuseBase` NO forces the base map to be drawn even when the cache would
/// have served. The shell uses it for the settle frame and for a band
/// refreshed early during a gesture. Callers outside the loop pass NO too,
/// since none of them knows what the cache holds.
- (nullable PPFrame *)renderViewport:(PPViewport *)viewport
                                band:(nullable PPViewport *)band
                           reuseBase:(BOOL)reuseBase
                               error:(NSError **)error
    NS_SWIFT_NAME(render(_:band:reuseBase:));

/// Advances the moving map at `viewport` without drawing: polls the demo
/// feed, consumes queued fixes, feeds the trip and guidance, and moves the
/// slew. The shell calls it instead of `render:` for a follow frame whose
/// camera would move the picture too little to redraw, and keeps its last
/// frame on screen under the preview transform. Render queue only. Nil when
/// the viewport has no surface.
- (nullable PPCameraStep *)stepCameraAtViewport:(PPViewport *)viewport
    NS_SWIFT_NAME(stepCamera(at:));

/// Draws the underlay for `viewport`: the map at `viewport.forUnderlay()`,
/// through a renderer of its own so the base map's retained scene survives.
/// Render queue only, like everything else here; nil with `error` filled
/// when the viewport has no surface or the renderer refuses.
- (nullable PPUnderlay *)renderUnderlayForViewport:(PPViewport *)viewport
                                             error:(NSError **)error
    NS_SWIFT_NAME(renderUnderlay(for:));

/// Drops the cached base map. Nothing in the normal path needs it: this is
/// for a memory warning and for a test that wants to force a draw.
- (void)invalidateBaseLayer;

/// The pack draws S-57 ENC charts rather than an OSM map.
@property(nonatomic, readonly) BOOL drawsCharts;

/// The pack's S-52 directory, which holds the S-57 catalogue; nil when the
/// pack draws no charts. Fixed for the pack, so readable from any thread.
@property(nonatomic, readonly, nullable) NSURL *chartCatalogURL;

/// Replaces the chart set with the cells under `url` (an indexed set from
/// `PPChartStore`), keeping the pack's S-52 library, and sets `homeBounds` to
/// the new set's coverage. Render queue only. On failure, or for a pack that
/// does not draw charts, the current set and bounds stay.
- (BOOL)openChartsAtURL:(NSURL *)url error:(NSError **)error
    NS_SWIFT_NAME(openCharts(at:));

/// Cells of the open chart set drawn without some of their update files,
/// from its index; 0 for the pack's own charts. Render queue only.
@property(nonatomic, readonly) NSInteger outdatedChartCellCount;

/// Hands one position report to the moving map.
///
/// Render queue only, like everything else on this class. The fix was born on
/// CoreLocation's queue and passed through the main thread, which is why
/// `PPFix` is an immutable value; what it lands in is MM1's `FixQueue`, the
/// port's own thread crossing.
///
/// A fix draws nothing by itself: the overlay's `Tick` consumes the queue at
/// the top of the next `render:`, so the feed's clock is the render clock. A
/// shell that stops asking for frames stops advancing the moving map, which
/// is why `MapModel` keeps the loop awake with a content-dirty flag while a
/// feed is running.
- (void)pushFix:(PPFix *)fix NS_SWIFT_NAME(push(_:));

/// Prepares the moving map for the first frame after the app was in the
/// background. Render queue only.
///
/// Discards guidance events queued while nothing could play them, and makes
/// the next tick that consumes a fix land the camera on it rather than slew
/// from where the map was when the app left.
- (void)resumeFromBackground;

/// Drains the guidance events raised since the last frame or the last call.
/// Render queue only.
///
/// For the background, where no frame is drawn to carry them. Each event is
/// returned once, by this or by a frame, never both.
- (NSArray<PPGuidanceEvent *> *)takeGuidanceEvents;

/// The guidance and trip as a frame would carry them, for the background,
/// where no frame is drawn. Render queue only.
- (nullable PPGuidance *)currentGuidance;
- (nullable PPTrip *)currentTrip;
- (nullable PPCourseDeviation *)currentCourseDeviation;

/// Starts the demo feed: the recorded Kiawah ride in the pack, replayed at
/// the speed it was ridden, through the machinery a live receiver uses
/// (`ReadGpxFile` -> `BuildScriptedTrackFromFixes` -> `ScriptedSource`).
///
/// Here rather than in Swift because the whole path is C++ the mac test bed
/// already exercises. Returns NO with the reason when the pack carries no
/// track.
- (BOOL)startDemoFeedWithError:(NSError **)error
    NS_SWIFT_NAME(startDemoFeed());

/// Stops the demo feed and detaches it from the moving map.
///
/// Both halves matter: a scripted source is polled by every render, so a feed
/// merely stopped on the shell's side would keep emitting the recorded ride
/// underneath a live receiver. Harmless when no demo feed was started.
- (void)stopDemoFeed;

/// Whether the moving map has taken a fix yet — the feed's answer, as against
/// `PPFrame.ownship`, which is a frame's.
@property(nonatomic, readonly) BOOL hasFix;

/// How far the ownship symbol reaches from the position it is drawn at, in
/// iOS points (`movingmap.size_px` in the pack).
///
/// A radius, not a half-width, because the symbol turns: `fv.north` is drawn
/// larger than its shape box along its own axis, so the reach in the
/// direction the rider is pointing exceeds the box's half-width.
///
/// The answer over-states the current chevron, whose multiple is 1.30 rather
/// than 2.00, and is deliberately left that way. Its one caller is the render
/// gate, which grows a viewport by this before deciding whether a fix is
/// worth a frame: too big costs a render nobody needed, too small drops the
/// frame that brings the ship back on screen.
@property(nonatomic, readonly) double ownshipSymbolRadiusInPoints;

#pragma mark - GPS mode

/// The map follows the ship: auto-centre and track-up through MM2's camera
/// and MM3's slew, with the road snapper on. Off, the ship is drawn and the
/// map is the user's, which is the state the app starts in.
///
/// Render queue only, like everything else on this class.
///
/// Four things happen, and only the first is obvious:
///
///  * the camera modes go on (`auto_center`, `auto_rotate`), which forces a
///    recentre. MM4 does that on every `SetModes`, because a mode waiting for
///    the ship to leave an apron computed while it was off would look like a
///    button that did nothing;
///  * the road snapper goes on, over the same graph the router plans with
///    (`pippin::RouteStore::EnsureRoadNetwork`), and off again on exit. MM5's
///    measurement says the snap removes across-track error and not
///    along-track, so it is a navigation aid rather than a display
///    correction, and snapping while nobody is following would move the drawn
///    chevron for a reason the user did not ask for;
///  * on exit the chart is slewed back to north-up rather than snapped;
///  * the map moves on the next fix, not on the press. `force_` is a flag the
///    camera reads when a fix arrives, so at 1 Hz the pan begins within a
///    second. Lunging on the press would guess a placement the camera is
///    about to compute properly, and guessing it twice is what makes a map
///    look like it is arguing with itself.
@property(nonatomic, getter=isGpsModeEnabled) BOOL gpsModeEnabled;

/// Whether the snapper has roads. NO on a pack with no `.fvroad`, where GPS
/// mode still centres and rotates and simply does not snap. Answered by
/// loading the graph, so it is asked once, when the mode goes on.
@property(nonatomic, readonly) BOOL hasRoadNetwork;

#pragma mark - The compass: north-up or course-up

/// Course-up: the auto-rotate half of GPS mode, split off from the centring
/// half so the compass can toggle it.
///
/// Render queue only. Outside GPS mode it changes nothing on screen, but it
/// is remembered so the next press of GPS comes up in the mode the rider left
/// it in. ON is the default.
///
/// The two modes differ in more than an angle:
///
///  * Course-up is continuous. MM2's apron is switched off, so every fix
///    recentres and re-aims the chart. The apron is a W/5 x 2H/5 box with the
///    ship anchored at its lower edge, so with it on a rider heading up the
///    screen travels a third of a screen — a minute at bicycle speed on a
///    riding scale — before the map moves. That is right for a chart being
///    read and wrong for one being ridden.
///  * North-up keeps the apron untouched: the map holds still until the ship
///    approaches the edge, then moves once, easing in and out.
///
/// Continuous centring costs what MM3's header says unless the slew is
/// retuned with it, which is `PPFollowCadence.h`'s subject: the follow slew
/// is linear and lasts one measured fix interval, so the chart slides at the
/// rider's own speed instead of restarting an ease every second and trailing
/// by one duration.
///
/// Turning it off while following unwinds the chart to north rather than
/// snapping it, as leaving GPS mode does.
@property(nonatomic, getter=isCourseUpEnabled) BOOL courseUpEnabled;

/// Turns the chart back to north, slewed. What the compass does when it is on
/// screen because the user rotated the map rather than because the map is
/// following something.
///
/// Render queue only. It takes effect on the next frame, because the rate
/// caps that shape the unwind need a projection to turn degrees per second
/// into an angle, and the only place the map is measured is a frame
/// (`tickMovingMap`).
- (void)requestNorthUp;

#pragma mark - The route

/// Where `current.fvrte` lives. Set once, before `loadSavedRoute`, to the
/// app's `Documents/`.
///
/// Set from Swift because `Documents/` is a Foundation question and nothing
/// below this class may ask one. Everything after it — when to write, and in
/// what order relative to an edit — is decided in `pippin::RouteStore`,
/// because a write ordered by the shell would race the next edit and could
/// leave an older route on disk than the one on screen.
- (void)setRouteDocumentURL:(nullable NSURL *)url;

/// Reads the saved route and replans it. Two steps, not one: the road
/// geometry is not in the document — it is derived from the graph and the
/// rule file, either of which can change — so reading alone would show the
/// waypoints joined by straight legs.
///
/// A first launch is neither an error nor a nil: it returns a route whose
/// `exists` is NO. That is deliberate, because this imports into Swift as
/// `throws -> PPRoute` under the `NSError **` convention, where a nil return
/// without an error is undefined behaviour.
///
/// A document that exists and will not read does throw, and the route is
/// still usable and empty afterwards: a bad file costs the user their route,
/// not their app.
- (nullable PPRoute *)loadSavedRouteWithError:(NSError **)error
    NS_SWIFT_NAME(loadSavedRoute());

/// Makes the `.fvrte` at `url` (a route shared from another device) the
/// current route, replanned here and saved as the route document. `url` is
/// only read. A file that will not read, or has no stops, throws and leaves
/// the current route as it was.
- (nullable PPRoute *)importRouteFromURL:(NSURL *)url error:(NSError **)error
    NS_SWIFT_NAME(importRoute(from:));

/// Writes the current route as GPX for a watch, bike computer or
/// chartplotter: the planned line along the roads, or the stops as a route
/// where legs are straight (`fv::GpxDocument` via `RouteStore::ExportGpx`).
/// Throws when there is no line to follow yet. Render queue.
- (BOOL)writeRouteToGpxURL:(NSURL *)fileURL
                      name:(NSString *)name
                     error:(NSError **)error
    NS_SWIFT_NAME(writeRoute(toGpxURL:name:));

/// The route as it stands. Never nil: an empty route is `exists` = NO rather
/// than an absent object, so the sheet has something to bind to.
- (PPRoute *)currentRoute;

/// Replaces every waypoint, replans, and writes the document. `profile` is
/// `foot` or `bicycle` in the shipped rules; empty keeps the route's own.
///
/// Fewer than two waypoints is not an error: it is a half-built route, and it
/// is persisted so the sheet re-opens on it. The returned route's
/// `statusText` says what went wrong when something did.
///
/// The plan happens here, on the render queue with everything else. At
/// Kiawah's size that is a few milliseconds and the sheet's spinner covers
/// the hop. It does mean a plan is time the map is not being drawn, which is
/// fine at an island's scale and would not be at a continent's; the way out
/// would be a second `fv::RoutePlanner` on its own queue plus a "here is a
/// computed plan" setter on `fv::RouteOverlay`, which needs a thread-safety
/// decision on the router that no measurement justifies yet.
/// `PPRoute.planMilliseconds` is that measurement.
- (PPRoute *)setRouteWaypoints:(NSArray<PPWaypoint *> *)waypoints
                       profile:(NSString *)profile
    NS_SWIFT_NAME(setRoute(waypoints:profile:));

/// As above, also setting the route's beach use. The tide, when the pack has
/// a table, is judged for a departure now; without one, or past its end, the
/// beach is kept and each stretch's verdict is unknown.
- (PPRoute *)setRouteWaypoints:(NSArray<PPWaypoint *> *)waypoints
                       profile:(NSString *)profile
                      beachUse:(PPBeachUse)beachUse
    NS_SWIFT_NAME(setRoute(waypoints:profile:beachUse:));

/// As above, also setting whether the route avoids hills, which raises the
/// cost of climbing by `[routing] avoid_hills_penalty` (default 4).
- (PPRoute *)setRouteWaypoints:(NSArray<PPWaypoint *> *)waypoints
                       profile:(NSString *)profile
                      beachUse:(PPBeachUse)beachUse
                    avoidHills:(BOOL)avoidHills
    NS_SWIFT_NAME(setRoute(waypoints:profile:beachUse:avoidHills:));

/// Wind chevrons along the current route's beach stretches; an empty array
/// removes them. A replan removes them too, so call again with each new
/// route. Call it on the render queue.
- (void)setBeachWind:(NSArray<PPBeachWindMark *> *)marks
              warnAt:(double)alongWarnMetersPerSecond
    NS_SWIFT_NAME(setBeachWind(_:warnAt:));

/// The departure time the tide is judged for, epoch seconds; NaN (the
/// default) means the system clock. For debugging at a chosen tide.
@property(nonatomic) NSTimeInterval routeDepartureOverride;

/// The pack plans no roads (`[routing] legs = "rhumb"`): waypoints are joined
/// by rhumb lines and every route is `isStraight`. Fixed at construction.
@property(nonatomic, readonly) BOOL routeLegsAreStraight;

/// The road graph has a beach to ride, so the sheet offers "Use beach:".
/// Loads the graph on first call; call it on the render queue.
@property(nonatomic, readonly) BOOL beachAvailable;

/// The road graph carries elevation, so the sheet offers "Avoid hills".
/// Loads the graph on first call; call it on the render queue.
@property(nonatomic, readonly) BOOL elevationAvailable;

/// Draws the calculated route coloured by grade where the graph has
/// elevation: green downhill, blue on the flat, yellow to red uphill.
/// Off by default. Set it on the render queue.
@property(nonatomic) BOOL routeSlopeColors;

/// The rider's map-key adjustments: profile name → (`PPLegendEntry.settingId`
/// → stop index into its `stopWords`). Each plan on a profile and the legend
/// for it use that profile's entry; a stop on the shipped value restores that
/// exact value. Setting it does not replan; plan again to see it. A profile's
/// map that names an unknown setting or a stop out of range is ignored
/// whole. Set it on the render queue.
@property(nonatomic, copy)
    NSDictionary<NSString *, NSDictionary<NSString *, NSNumber *> *> *routeAdjustments;

/// Replans the route on screen after `routeAdjustments` changed. When the
/// line moves, the previous one stays drawn faded until `clearRouteGhost` or
/// the next edit. Without a calculated route nothing is planned and the
/// result's `lineChanged` is NO. Call it on the render queue.
- (PPRouteReplan *)replanForAdjustments NS_SWIFT_NAME(replanForAdjustments());

/// Stops drawing the previous line `replanForAdjustments` kept.
- (void)clearRouteGhost;

/// No route, and no document on disk either. Clear Route in the sheet.
- (PPRoute *)clearRoute;

/// The rule file's profile names, for a UI that offers them. The shipped file
/// defines `foot` and `bicycle`.
@property(nonatomic, readonly, copy) NSArray<NSString *> *routeProfileNames;

/// What a route is priced with when nobody has said: `[routing] profile` in
/// the pack. The segmented control opens on it.
@property(nonatomic, readonly, copy) NSString *routeDefaultProfile;

/// Names a coordinate in words: the nearest thing a route priced with
/// `profile` could travel on ("Flyaway Drive", "cycleway"), or nothing
/// usable. An empty `profile` asks with the route document's own, which is
/// what the point editor uses.
///
/// Render queue only, and here for a reason beyond the contract: this is the
/// first call to touch the road graph on a launch where nobody has routed, so
/// the first Pick on map pays for loading `.fvroad`.
///
/// It remembers the road it named last, so a crosshair drifting a pixel
/// between a road and the cycleway beside it does not blink between their
/// names while the rider reads the button. A pick that is STARTING should
/// send `-forgetNamedPlace` first, or the road named at the end of the last
/// pick gets a head start somewhere else entirely.
///
/// `remembering` NO asks without touching that memory, which is what a sheet
/// asks with: a row naming a stop set ten minutes ago is a lookup of a fixed
/// place, and letting it write the memory would hand the crosshair a head
/// start from another coordinate.
- (PPPlace *)describePlaceAt:(PPGeoPoint)coordinate
                     profile:(NSString *)profile
                 remembering:(BOOL)remembering
    NS_SWIFT_NAME(describePlace(at:profile:remembering:));

/// Forgets the road `describePlaceAt:profile:remembering:` named last. Sent
/// when a pick begins, not when it ends.
- (void)forgetNamedPlace;

/// What the pick would snap to, or nil.
///
/// The rule: if the place the rider is aiming at overlaps a feature some
/// overlay is drawing, the pick takes that feature's exact coordinate instead
/// of the crosshair's. Overlap is a screen question — within `tolerance` of
/// the drawn marker — and the answer is a coordinate out of a document.
///
/// This is not a points feature: it asks the overlay stack, so anything
/// implementing `fv::app::SnapTo` is a snap source from the moment it is
/// added. The point set and the route both are, and this method did not
/// change for the second one.
///
/// Nearest wins with no ambiguity question, as with
/// `point(near:in:tolerance:)`: the desktop can ask "which did you mean?"
/// because it has a mouse and a dialog, and a rider with one thumb gets the
/// closest. `fv::app::SnapCandidates` ranks them; this takes the front.
///
/// Invisible overlays are not snap targets, which falls out of the
/// aggregation rather than being decided here. A coordinate that jumped to
/// something the rider cannot see would be indistinguishable from the app
/// losing the pick.
///
/// `screenPoint` and `tolerance` are in iOS points in `viewport`'s space, and
/// the conversion to surface pixels happens in here.
- (nullable PPSnapTarget *)snapTargetNear:(CGPoint)screenPoint
                               inViewport:(PPViewport *)viewport
                                tolerance:(double)tolerance
    NS_SWIFT_NAME(snapTarget(near:in:tolerance:));

/// How close the crosshair must come before the pick snaps, in iOS points
/// (`pick.snap_tolerance`).
///
/// Separate from `pointHitTolerance` because the gestures differ: a tap is
/// deliberate and aimed at one marker, while a pick is a map dragged under a
/// fixed crosshair and wants a wider catch. Setting it to 0 turns snapping
/// off.
@property(nonatomic, readonly) double snapTolerance;

#pragma mark - Dragging a route waypoint

/// Which waypoint is under a press, by label, or nil.
///
/// A label rather than an index because that is the identity every edit in
/// `fv::RouteEditSession` uses, and because an index would go stale if a via
/// were inserted between the press and the answer returning off the render
/// queue.
///
/// Answered out of what was drawn: a press hits the diamond the rider can
/// see, at the size it is drawn, and an overlay that has never drawn answers
/// nothing. `screenPoint` and `tolerance` are in iOS points.
- (nullable NSString *)routeWaypointLabelNear:(CGPoint)screenPoint
                                   inViewport:(PPViewport *)viewport
                                    tolerance:(double)tolerance
    NS_SWIFT_NAME(routeWaypointLabel(near:in:tolerance:));

/// How far beyond a waypoint's drawn ink a press still grabs it, in iOS
/// points (`routing.drag_hit_tolerance`).
///
/// This is the same kind of gesture as a tap on a point marker, so it
/// defaults to the same 22 points rather than to the pick's 12: what makes 12
/// right for a pick is a crosshair the rider can place exactly, and nothing
/// about a long press is exact.
@property(nonatomic, readonly) double routeWaypointHitTolerance;
/// Peak level of the turn chimes, 0 to 1, from `guidance.alert_amplitude`.
/// Out-of-range values fall back to the default, so a mistyped key is a
/// normal chime rather than a silent or clipped one.
@property(nonatomic, readonly) double alertAmplitude;


/// How long a press is held before it grabs, in seconds
/// (`routing.drag_hold_seconds`).
///
/// The hold duration is the entire negotiation between grabbing a waypoint
/// and panning the chart: too short and a rider who pauses before panning
/// starts editing their route, too long and the grab feels broken. It is a
/// pack key for that reason. 0.5 is UIKit's own number and the default;
/// anything at or below 0 restores it.
@property(nonatomic, readonly) double routeWaypointHoldSeconds;

/// Takes hold of `label`. NO when there is no such waypoint, which a shell
/// can legitimately ask for because its gesture began before this answer came
/// back.
///
/// The grabbed waypoint is drawn with its selection halo while it is held,
/// which is the only acknowledgement a long press can give.
- (BOOL)beginRouteWaypointDrag:(NSString *)label
                            at:(CGPoint)screenPoint
                    inViewport:(PPViewport *)viewport
    NS_SWIFT_NAME(beginRouteWaypointDrag(_:at:in:));

/// Moves it. NO when nothing is being dragged.
///
/// No route comes back, and that is not an omission: nothing has been
/// committed or written, and the shell already knows the picture changed
/// because it moved the finger.
///
/// The legs follow the roads under the finger while the replan fits in
/// `routing.drag_replan_budget_ms`, and fall back to straight lines for the
/// rest of that drag once one overruns. See `PPRouteStore.h`: the decision is
/// measured rather than assumed, because it is a property of the graph.
- (BOOL)dragRouteWaypointTo:(CGPoint)screenPoint
                 inViewport:(PPViewport *)viewport
    NS_SWIFT_NAME(dragRouteWaypoint(to:in:));

/// Lets go: replans, writes the document, and returns the committed route. A
/// press that grabbed and let go without moving writes nothing.
- (PPRoute *)endRouteWaypointDragAt:(CGPoint)screenPoint
                         inViewport:(PPViewport *)viewport
    NS_SWIFT_NAME(endRouteWaypointDrag(at:in:));

/// Puts it back where it was: a cancelled touch, a phone call, edit focus
/// going away. The document on disk is untouched, because it never changed.
- (PPRoute *)cancelRouteWaypointDrag;

/// Whether a waypoint is held right now. The render queue's answer, so a
/// shell needing this on the main thread should keep its own flag; this is
/// for the queue's bookkeeping and for a test.
@property(nonatomic, readonly) BOOL isDraggingRouteWaypoint;


#pragma mark - Search

/// Searches the whole overlay stack.
///
/// Render queue only, and not as a formality: the first call builds the two
/// overlays that make the chart and the routing graph searchable, and
/// building the second loads `kiawah.fvroad` on a launch where nobody has
/// routed. It is also a walk over vector tiles, which are this queue's data.
///
/// Who answers is not decided here: `fv::app::SearchSession` walks the stack
/// and asks everything implementing `AsSearch()`, so the rider's points, the
/// chart's named features and the road network are found by one query and
/// ranked against each other once. Two overlays are added on the first search
/// purely so they can be asked, both not visible, which is the seam's own
/// arrangement (`search.h` on `visible_only`).
///
/// The route's own waypoints are dropped. `fv::RouteOverlay` is a search
/// provider and answers "Start", "End", "Via 1", which is useful in a shell
/// that can select a waypoint and confusing in the dialog asking where the
/// next stop should go.
///
/// The whole document and the whole road network are searched, not the part
/// on screen. That is what each provider can afford rather than a policy:
/// `PointOverlay` scans a few dozen rows and `RoadGraphOverlay` tests each
/// distinct name once against the graph's name table, so both answer over the
/// whole island for nothing. The tile pyramid cannot — a text query with no
/// area and no name index reads no tiles — so the viewport is lent to the
/// chart alone (`VectorMapOverlay::SetSearchFallbackArea`) rather than put on
/// the query, where it would have cut down the two providers that never
/// needed it.
///
/// `viewport` is that window and is also what distances are measured from.
/// Nil is allowed, and then the points and roads still answer.
///
/// An empty `text` finds nothing. A spatial-only query is supported by the
/// seam and deliberately not offered here: "everything on screen" is not an
/// answer to where a stop should go.
- (NSArray<PPSearchResult *> *)searchFor:(NSString *)text
                              inViewport:(nullable PPViewport *)viewport
    NS_SWIFT_NAME(search(for:in:));

/// Whether the chart is confined to what is on screen
/// (`search.chart_in_view_only`, YES by default).
///
/// It is about the chart alone: the points document and the road network are
/// searched whole either way.
///
/// Turning it off does less than it looks like it should. A chart answers a
/// text query with no area only through the pack's own name index
/// (`fvnames build`), and the shipped Kiawah pack carries none, so with this
/// off the chart contributes nothing and the answers are points and roads.
/// With an indexed pack it is already ignored, because the index answers
/// globally before the window is reached.
@property(nonatomic, readonly) BOOL chartSearchIsViewOnly;

/// What the search box opens on before anything has been searched:
/// `search.initial_text`, "Boardwalk" in the shipped pack, empty for none.
///
/// A pack key rather than a Swift constant because the right word is a fact
/// about the data: Kiawah's numbered beach boardwalks are in the road graph,
/// spread along the island, and are what a rider most often rides to. A pack
/// of a harbour would want something else and could say so without this app
/// being rebuilt.
///
/// Only the seed. What the box opens on after the first successful search is
/// that search, remembered per install by the shell, which is a fact about
/// the rider rather than the pack.
@property(nonatomic, readonly, copy) NSString *searchInitialText;

/// The most rows a search returns (`search.max_results`, 40 by default). It
/// caps each provider as well as the total, which is the seam's contract: the
/// point of the number is that nobody builds the bigger list.
@property(nonatomic, readonly) NSUInteger searchMaxResults;

/// How near two rows of the same name and kind must be before the second is
/// dropped as a duplicate (`search.duplicate_radius_m`, 250 m by default; 0
/// leaves only the overlapping-bounds test). `PPSearchRules.h` has the whole
/// rule and why it is two tests.
@property(nonatomic, readonly) double searchDuplicateRadiusMeters;

/// The scale a framed search result is never taken in past
/// (`search.frame_min_scale`, 1:2,500 by default; 0 for no floor). See
/// `pippin::ScaleToFitBounds`: a forty-metre cul-de-sac fitted exactly to a
/// phone is about 1:300, which is a street with no map around it.
@property(nonatomic, readonly) double searchFrameMinScale;

/// How far up the screen a framed result is pushed, as a fraction of the
/// height (`search.frame_top_bias`, 0.18 by default).
///
/// The sheet is still up when the map moves, so a result centred exactly sits
/// underneath the dialog that found it. The framing allows for the bias in
/// the fit as well as the pan, so a road pushed up is not half off the top.
@property(nonatomic, readonly) double searchFrameTopBias;

#pragma mark - The points

/// Where the user's `points.fvpoints` lives. Set once, before `loadPoints`,
/// to the app's `Documents/`. `setRouteDocumentURL:`'s reasoning applies word
/// for word, including why the decision about when to write is made in
/// `pippin::PointStore`.
- (void)setPointsDocumentURL:(nullable NSURL *)url;

/// Reads the user's point set, seeding it from the pack's copy on a first
/// launch. One step, unlike the route: a point has no derived geometry, so
/// the document holds the whole of it.
///
/// A first launch is not an error and neither is a pack that ships no points;
/// both come back as an empty or seeded set. A document that exists and will
/// not read does throw, and the app still comes up, with no points rather
/// than refusing to start.
- (BOOL)loadPointsWithError:(NSError **)error NS_SWIFT_NAME(loadPoints());

/// Every point in the document, in document order, which is also the draw
/// order, so a list on screen and the markers agree.
@property(nonatomic, readonly, copy) NSArray<PPMapPoint *> *points;

/// The document's embedded artwork in `symbols` id order: what a symbol
/// picker offers.
///
/// A palette rather than a projection of the points — a row nothing
/// references is still here, which is the whole reason a picker has anything
/// to offer. The pack's set embeds forty-odd maki icons and uses nine.
///
/// Read once after `loadPoints`. Nothing in this app adds artwork at runtime
/// (`fv::PointOverlay::AddSymbolFromPngFile` exists and no shell calls it),
/// so the palette is fixed for a session.
@property(nonatomic, readonly, copy) NSArray<PPPointSymbol *> *pointSymbols;

/// Whether the points are drawn. Not persisted in the document, which is a
/// set of places rather than a record of whether somebody had them switched
/// on; the startup value is `points.visible` in `pippin.ini`.
@property(nonatomic, getter=arePointsVisible) BOOL pointsVisible;

/// The point drawn highlighted, or 0 for none. What a tap sets and what
/// closing the sheet clears.
@property(nonatomic) int64_t selectedPointId;

/// What is under a finger, or nil.
///
/// `screenPoint` and `tolerance` are in iOS points in `viewport`'s coordinate
/// space, the same space a tap recognizer reports, and this method converts
/// to surface pixels because the backing scale is on the viewport.
///
/// Nearest wins with no ambiguity question; `pippin::PointStore::HitTest`
/// says why that is a phone decision rather than a disagreement with A5.
- (nullable PPMapPoint *)pointNear:(CGPoint)screenPoint
                        inViewport:(PPViewport *)viewport
                         tolerance:(double)tolerance
    NS_SWIFT_NAME(point(near:in:tolerance:));

/// Adds a point and writes the document. Returns the point as stored, with
/// the id the document gave it, which is the handle every later edit uses.
- (PPMapPoint *)addPoint:(PPMapPoint *)point NS_SWIFT_NAME(add(_:));

/// Replaces the row with `point.pointId` and writes. NO, and nothing written,
/// when there is no such row — which is what a sheet holding a deleted id
/// gets instead of a resurrection.
- (BOOL)updatePoint:(PPMapPoint *)point NS_SWIFT_NAME(update(_:));

- (BOOL)removePointWithId:(int64_t)pointId NS_SWIFT_NAME(removePoint(id:));

/// Takes hold of point `pointId`, pressed at `screenPoint` (flat iOS points
/// in `viewport`). The point is drawn selected while held and keeps its
/// offset from the finger, so it does not jump under the press. NO when
/// there is no such point.
- (BOOL)beginPointDrag:(int64_t)pointId
                    at:(CGPoint)screenPoint
            inViewport:(PPViewport *)viewport
    NS_SWIFT_NAME(beginPointDrag(_:at:in:));

/// Moves the held point. Redraws only; nothing is written. NO when nothing
/// is held.
- (BOOL)dragPointTo:(CGPoint)screenPoint
         inViewport:(PPViewport *)viewport
    NS_SWIFT_NAME(dragPoint(to:in:));

/// Lets go and writes the document. A release within 3 points of the press
/// leaves the point where it was and writes nothing. Returns whether the
/// point moved.
- (BOOL)endPointDragAt:(CGPoint)screenPoint
            inViewport:(PPViewport *)viewport
    NS_SWIFT_NAME(endPointDrag(at:in:));

/// Puts the held point back where it was. Nothing is written.
- (void)cancelPointDrag;

/// The error from the last point write, or empty. A failed write is not a
/// refused edit — the point is on screen and correct, and what the user loses
/// is the next launch — so it is reported here rather than thrown from the
/// call that made it.
@property(nonatomic, readonly, copy) NSString *pointWriteError;

/// The pack shipped these points and this launch is the first to see them.
/// Worth one line on screen once.
@property(nonatomic, readonly) BOOL pointsWereSeeded;

/// How far from a marker a tap still counts, in iOS points
/// (`points.hit_tolerance`). A pack key rather than a Swift constant because
/// the right value differs between a dense harbour set and a dozen places on
/// an island.
@property(nonatomic, readonly) double pointHitTolerance;

/// The topmost named feature the last base map render drew under
/// `screenPoint`. Unnamed ink above it (a building fill, a landuse area) is
/// passed over for the name, so a press on a shop inside a park names the
/// shop. With nothing named there, the topmost feature with an empty name;
/// nil only when nothing was drawn.
///
/// Render queue only: it reads the renderer's pick index and the tile cache.
/// `screenPoint` and `tolerance` are flat-surface iOS points in `viewport`'s
/// space; the conversion into the cached band happens here.
- (nullable PPChartFeature *)chartFeatureNear:(CGPoint)screenPoint
                                   inViewport:(PPViewport *)viewport
                                    tolerance:(double)tolerance
    NS_SWIFT_NAME(chartFeature(near:in:tolerance:));

/// How far from drawn ink a press still names a feature, in iOS points
/// (`points.feature_hit_tolerance`, 12 by default).
@property(nonatomic, readonly) double chartFeatureHitTolerance;

/// The AIS targets under `screenPoint`, nearest first, then the ENC features
/// the last base map render drew there, topmost first, at most eight; empty
/// when the pack does not draw charts. A target's `acronym` is "AIS".
/// Render queue only; coordinates as in `chartFeatureNear:inViewport:tolerance:`.
- (NSArray<PPChartFeatureInfo *> *)chartFeaturesNear:(CGPoint)screenPoint
                                         inViewport:(PPViewport *)viewport
                                          tolerance:(double)tolerance
    NS_SWIFT_NAME(chartFeatures(near:in:tolerance:));

#pragma mark - AIS traffic
// The `fv.ais` overlay, drawn with the route and points. Render queue only.

/// Draws AIS targets. Turning it off also forgets every target.
@property(nonatomic) BOOL aisVisible;

/// Queues Open Waters frames for the next render; frames other than events
/// are ignored. The caller asks for a frame afterwards.
- (void)pushOpenWatersFrames:(NSArray<PPOpenWatersFrame *> *)frames
    NS_SWIFT_NAME(push(openWatersFrames:));

/// Queues AIS sentences from a local receiver, heard at `heardAt` (epoch
/// seconds). The caller asks for a frame afterwards.
- (void)pushAisLines:(NSArray<NSString *> *)lines heardAt:(NSTimeInterval)heardAt
    NS_SWIFT_NAME(push(aisLines:heardAt:));

/// Targets in the table, drawn or not.
@property(nonatomic, readonly) NSInteger aisTargetCount;

#pragma mark - Chart settings
// Mariner settings for a chart pack. Each setter drops the cached base map;
// they are kept across `openChartsAtURL:`. No-ops for a pack without charts.
// Render queue only.

/// The safety contour in metres (30 by default, S-52's own).
@property(nonatomic) double chartSafetyContour;
/// Soundings at or shallower than this print black, in metres (30 by default).
@property(nonatomic) double chartSafetyDepth;
@property(nonatomic) PPChartCategory chartCategory;
@property(nonatomic) PPChartPalette chartPalette;
/// Draws buoy and beacon names on the chart (YES by default, S-52's own).
@property(nonatomic) BOOL chartNavaidNames;
/// Depths in `chartFeaturesNear:` summaries are in feet rather than metres.
@property(nonatomic) BOOL chartDepthsInFeet;

#pragma mark - Recording the ride

/// Starts recording into `fileURL`, which the shell names — the same division
/// as the route and point documents.
///
/// Render queue only, and not as a formality: the recorder is fed from
/// `consumeRawFix:`, the one point both feeds pass through.
///
/// What is written is the raw fix, never the snapped one. A recording is
/// evidence of where the rider went; the snap is this version of the
/// snapper's opinion about which road that was.
///
/// The file is a complete, valid GPX after every fix — `fv::GpxRecorder`
/// appends and rewrites its own footer — so a phone that dies at mile 30
/// leaves thirty miles behind rather than an empty file.
///
/// `name` goes into the track's `<name>` and may be empty. Returns NO with
/// the reason when the file will not open, and recording is then off.
- (BOOL)startRecordingToURL:(NSURL *)fileURL
                       name:(NSString *)name
                      error:(NSError **)error
    NS_SWIFT_NAME(startRecording(to:name:));

/// Closes the file. Harmless when nothing is recording, and the file is
/// already complete when it runs.
- (void)stopRecording;

@property(nonatomic, readonly, getter=isRecording) BOOL recording;

/// The file being written, or nil.
@property(nonatomic, readonly, nullable) NSURL *recordingURL;

/// Points written so far. Not the number of fixes offered: a fix with no
/// position, and one whose stamp does not advance, are both dropped.
@property(nonatomic, readonly) NSUInteger recordedPointCount;

/// Writes a single point as a one-waypoint GPX 1.1 file, for the share sheet.
///
/// A class method deliberately: it reads nothing from the map, so it is
/// exempt from the render-queue rule and a share button can call it where the
/// finger is. The name, elevation and remarks travel with the coordinate,
/// since a waypoint with no name is a pin somebody else has to guess at.
+ (BOOL)writePoint:(PPMapPoint *)point
          toGpxURL:(NSURL *)fileURL
             error:(NSError **)error
    NS_SWIFT_NAME(writePoint(_:toGpxURL:));

/// Magnetic variation at a position today, degrees, east positive, from the
/// compiled-in world magnetic model. Zero when the model has no answer.
/// A class method: it reads nothing from the map.
+ (double)magneticVariationAtLatitude:(double)latitude
                            longitude:(double)longitude
    NS_SWIFT_NAME(magneticVariation(latitude:longitude:));

#pragma mark - Symbol size

/// How big the map draws itself: a multiplier on everything the style authors
/// in pixels — line widths, text, halos, icons, an area pattern's pitch. 1.0
/// is the map as authored and is the default.
///
/// It is implemented as the device DPI. A GL style states widths in CSS
/// pixels and `OsmStyleEngine` scales each by `device_dpi / 96`, so
/// multiplying the dpi multiplies the symbology and nothing else: the tile
/// level comes from `display.mm_per_pixel` and the projection from the
/// viewport, so the map keeps showing the same ground. This is feature zoom,
/// not map scale, which is what a pinch is for.
///
/// `SetSymbolScale` is deliberately not set alongside it. For a GL style the
/// engine has already multiplied the icon's scale by the dpi factor and the
/// renderer multiplies again by `symbol_scale_`, so setting both would give
/// point symbols the square of what lines and text got.
///
/// The overlays follow it only where `overlaysFollowSymbolZoom` says so; the
/// route's beach wind arrows follow it either way.
///
/// Setting it drops the cached base map, because those pixels were rasterized
/// at the old size and the coverage test only asks about the camera.
@property(nonatomic) double symbolZoom;

/// Whether the route's markers and the AIS targets grow with `symbolZoom`.
/// NO (the default) keeps them at their authored size in points. Points and
/// the ownship stay at their authored size either way.
@property(nonatomic) BOOL overlaysFollowSymbolZoom;

/// The sizes the app's menu offers, smallest first, from
/// `display.symbol_zoom_steps`. Always at least one entry, the first being
/// the map as authored. A launch-time constant: the shell reads it once.
@property(nonatomic, readonly, copy) NSArray<NSNumber *> *symbolZoomSteps;

/// The style sheet's name, and whether a label font was found in the pack.
/// The about screen's material, and the one thing worth saying out loud when
/// a map draws with no names on it.
@property(nonatomic, readonly, copy) NSString *styleName;
@property(nonatomic, readonly) BOOL hasLabelFont;

/// Whether the pack names a dark style sheet (`osm.style_dark`), or draws
/// charts, whose night palette stands in for one.
@property(nonatomic, readonly) BOOL hasDarkStyle;

/// Draws with the dark sheet instead of `osm.style`. Reloads the sheet and
/// drops the cached base map; a no-op when the pack has no dark sheet. On a
/// chart pack it selects the night palette while `chartPalette` is automatic.
/// Render queue only, like every other call on this class.
@property(nonatomic) BOOL darkStyle;

/// The light style carries a legend (`peregrine:legend`). A launch-time
/// constant; the dark sheet copies the light one's legend.
@property(nonatomic, readonly) BOOL hasLegend;

/// The rule file's `name` ("City Rules"), or empty. The legend's header.
@property(nonatomic, readonly, copy) NSString *legendRulesName;

/// The active style's legend at `viewport`'s scale, quantised to a quarter
/// zoom. Each swatch is `swatchSize` points at the viewport's display scale
/// and the current `symbolZoom`; tiers come from `routing.profile`, and
/// `appName` is the subject of each router sentence. Swatches are cached per
/// quarter zoom until the style, the scale's latitude band or the size
/// changes. Rows are priced with `routeAdjustments` for that profile applied.
/// Empty when the style has no legend.
- (NSArray<PPLegendGroup *> *)legendForViewport:(PPViewport *)viewport
                                     swatchSize:(CGSize)swatchSize
                                        appName:(NSString *)appName
    NS_SWIFT_NAME(legend(for:swatchSize:appName:));

@end

NS_ASSUME_NONNULL_END
