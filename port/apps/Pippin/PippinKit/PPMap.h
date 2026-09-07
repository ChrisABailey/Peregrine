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

#import <PippinKit/PPFix.h>
#import <PippinKit/PPGeometry.h>
#import <PippinKit/PPPoint.h>
#import <PippinKit/PPTrip.h>
#import <PippinKit/PPRoute.h>
#import <PippinKit/PPSearch.h>
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
};

/// Where the ship was, and pointing where, in the frame just drawn.
///
/// Reported rather than asked for, because the overlay's tick happens on the
/// render queue and this is the main thread's only honest view of it. The
/// ownship is drawn rather than described, so nothing consumes this except
/// the stats bar, the GPS button and the probes.
NS_SWIFT_SENDABLE
@interface PPOwnship : NSObject

- (instancetype)init NS_UNAVAILABLE;

@property(nonatomic, readonly) PPGeoPoint coordinate;

/// The angle the symbol was stamped at: heading + convergence − the map's own
/// rotation, in degrees clockwise, [0, 360). Not the heading — on a turned
/// chart the two differ by exactly the turn — and this is the one the drawing
/// is pinned against.
@property(nonatomic, readonly) double screenAngleDegrees;

/// YES when the heading came from the receiver's course, NO when
/// `HeadingResolver` derived it from successive positions. The two are not
/// the same quantity: a reported course is a true bearing and a derived one
/// is a screen angle.
@property(nonatomic, readonly) BOOL headingIsReported;
@property(nonatomic, readonly) BOOL hasHeading;

@property(nonatomic, readonly) double speedMetersPerSecond;
@property(nonatomic, readonly) BOOL hasSpeed;

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

/// One drawn frame: the images, the viewports they were drawn at, and what
/// they cost.
NS_SWIFT_SENDABLE
/// A frame is two layers.
///
/// `baseImage` is the map, drawn into a surface larger than the screen (the
/// guard band) and kept between frames, so a camera that has only panned,
/// turned a little, or not moved gets the same pixels back for nothing.
/// `overlayImage` is the route, the points and the ownship, drawn every frame
/// at the live camera on a transparent surface exactly screen-sized.
///
/// The compositor puts them together, which is what makes the band cheap:
/// `MapScreen`'s preview transform is applied twice, against two viewports,
/// on the GPU. So there is no blitter in this app, rotation and scale cost
/// nothing, and course-up is served like any other mode.
///
/// `viewport` is the camera the frame was asked for and the one the overlay
/// was drawn at; it is the frame's identity, which `applyCamera` and the
/// loop's "already showing this" test read. `baseViewport` is the band — the
/// same camera on a larger canvas — and is usually an older camera, because
/// that is what a cache hit means.
@interface PPFrame : NSObject

/// The overlays, at `viewport`, on a transparent surface. Composited over
/// `baseImage`.
@property(nonatomic, readonly) CGImageRef overlayImage;
@property(nonatomic, readonly) PPViewport *viewport;

/// The map, at `baseViewport`, opaque. Never nil on a frame that returned.
@property(nonatomic, readonly) CGImageRef baseImage;
@property(nonatomic, readonly) PPViewport *baseViewport;

/// NO when this frame was served the cached base map, so it cost an overlay
/// pass and nothing else.
@property(nonatomic, readonly) BOOL baseWasDrawn;

@property(nonatomic, readonly) NSUInteger featuresQueried;
@property(nonatomic, readonly) NSUInteger drawsEmitted;
@property(nonatomic, readonly) NSInteger queryZoom;
/// The whole frame. On a base hit, the overlay pass alone.
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
/// `renderViewport:bandMargin:reuseBase:error:`.
@property(nonatomic, readonly) double baseCacheBandMargin;

/// How far the chart may turn under a composited base map before it is drawn
/// again, in degrees. `display.base_cache_max_turn_deg`, 2.5 by default. The
/// shell reads it for `PPViewport`'s `covers(_:maxTurnDegrees:)`, so both
/// sides ask the same question with the same number.
@property(nonatomic, readonly) double baseCacheMaxTurnDegrees;

/// The bundled data's own extent, from `pippin.home_bounds`.
@property(nonatomic, readonly) PPGeoBounds homeBounds;

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
/// `bandMargin` is how much larger than the screen to draw the base map when
/// it has to be drawn at all. It is the caller's to choose per frame: a band
/// is paid for on every miss and earns its keep only on hits, so the shell
/// asks for one when hits are possible and for none when every frame will
/// miss. The case that matters is a pinch, where the scale moves so no cached
/// base can serve it (`PPBaseCoverage.h`) and 2.25x per frame would make the
/// most expensive gesture in the app half as fast for nothing.
///
/// `reuseBase` NO forces the base map to be drawn even when the cache would
/// have served. The shell uses it for the settle frame, which is what keeps
/// the cache from costing any sharpness: a composited base is resampled
/// whenever the transform is not the identity, so the band carries motion and
/// the moment the map rests it is redrawn exactly. Callers outside the loop
/// pass NO too, since none of them knows what the cache holds.
- (nullable PPFrame *)renderViewport:(PPViewport *)viewport
                          bandMargin:(double)bandMargin
                           reuseBase:(BOOL)reuseBase
                               error:(NSError **)error
    NS_SWIFT_NAME(render(_:bandMargin:reuseBase:));

/// Drops the cached base map. Nothing in the normal path needs it: this is
/// for a memory warning and for a test that wants to force a draw.
- (void)invalidateBaseLayer;

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
/// The overlays do not follow it. The route, points and ownship draw through
/// `GeoDraw`, which has its own `SetSymbolScale`; when they should grow with
/// the map, that is where it goes and this is the number to hand it.
///
/// Setting it drops the cached base map, because those pixels were rasterized
/// at the old size and the coverage test only asks about the camera.
@property(nonatomic) double symbolZoom;

/// The sizes the app's menu offers, smallest first, from
/// `display.symbol_zoom_steps`. Always at least one entry, the first being
/// the map as authored. A launch-time constant: the shell reads it once.
@property(nonatomic, readonly, copy) NSArray<NSNumber *> *symbolZoomSteps;

/// The style sheet's name, and whether a label font was found in the pack.
/// The about screen's material, and the one thing worth saying out loud when
/// a map draws with no names on it.
@property(nonatomic, readonly, copy) NSString *styleName;
@property(nonatomic, readonly) BOOL hasLabelFont;

@end

NS_ASSUME_NONNULL_END
