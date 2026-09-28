// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPRoute.h — the route as the sheet sees it.
//
// Two immutable values, which is the whole of the route's public bridge, in
// `PPViewport`'s and `PPFix`'s pattern: what crosses the thread boundary is a
// value, so the main thread can hold one while the render queue builds the
// next. `PPMap` owns the live route; this is a photograph of it.
//
// There is no `PPRoutePlanner` class, and that is deliberate.
// `fv::RoutePlanner` is borrowed by the overlay, the overlay lives in
// `PPMap`'s stack, and both are confined to the render queue, so a public
// planner object would be a second handle to render-queue state. What route
// planning off the main thread actually looks like is
// `-[PPMap setRouteWaypoints:profile:]` called on the render queue.
//
// The C++ behind all of it is `PPRouteStore.{h,cpp}`, pure `std::` with no
// Foundation, which is what lets the mac test bed own the whole lifecycle.

#import <Foundation/Foundation.h>

#import <PippinKit/PPGeometry.h>

NS_ASSUME_NONNULL_BEGIN

/// One stop. The label is the identity rather than the index: `fv::RouteDoc`
/// selects and deletes by it, and the map draws it beside the diamond.
NS_SWIFT_SENDABLE
@interface PPWaypoint : NSObject

- (instancetype)initWithLabel:(NSString *)label
                   coordinate:(PPGeoPoint)coordinate
    NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@property(nonatomic, readonly, copy) NSString *label;
@property(nonatomic, readonly) PPGeoPoint coordinate;

@end

/// The "Use beach:" setting, as saved in the route document.
typedef NS_ENUM(NSInteger, PPBeachUse) {
  PPBeachUseNever = 0,
  PPBeachUseToSaveTime,
  PPBeachUseWheneverPossible,
};

/// The tide's judgement of one beach stretch (`fv::nav::BeachVerdict`).
typedef NS_ENUM(NSInteger, PPBeachVerdict) {
  PPBeachVerdictGood = 0,
  PPBeachVerdictMarginal,  ///< Passable, soft sand likely.
  PPBeachVerdictPoor,      ///< Covered on the way.
  PPBeachVerdictUnknown,   ///< No tide table, or past its end.
};

/// Why a route that admitted the beach came back without it.
typedef NS_ENUM(NSInteger, PPBeachDropped) {
  PPBeachDroppedNone = 0,
  PPBeachDroppedHigh,     ///< Above the rideable height on arrival.
  PPBeachDroppedRising,   ///< Covered before the stretch ends.
  PPBeachDroppedNoTable,  ///< Unknown and the gate drops unknown stretches.
};

/// One stretch along the water and the tide's verdict on it. Times are
/// epoch seconds, heights metres above the tide table's datum; a time or
/// height that does not apply is NaN.
NS_SWIFT_SENDABLE
@interface PPBeachStretch : NSObject
- (instancetype)init NS_UNAVAILABLE;
/// Distance along the route where the stretch starts, and its length.
@property(nonatomic, readonly) double startMeters;
@property(nonatomic, readonly) double lengthMeters;
@property(nonatomic, readonly) NSTimeInterval enterTime;
@property(nonatomic, readonly) NSTimeInterval exitTime;
@property(nonatomic, readonly) PPBeachVerdict verdict;
@property(nonatomic, readonly) double enterHeightMeters;
@property(nonatomic, readonly) double peakMeters;
@property(nonatomic, readonly) NSTimeInterval peakTime;
/// When the water rises over the rideable height during the stretch.
@property(nonatomic, readonly) NSTimeInterval coveredTime;
/// The earliest entry at or after arrival that is passable, or good.
@property(nonatomic, readonly) NSTimeInterval passableFrom;
@property(nonatomic, readonly) NSTimeInterval goodFrom;
/// Net heading in the direction of travel, degrees true, start to end; NaN
/// for the dropped stretch, which has no geometry on the kept plan.
@property(nonatomic, readonly) double headingDegrees;
@end

/// The route, photographed. Every field is meaningful when `isCalculated` is
/// NO — that is the state the status line exists for.
NS_SWIFT_SENDABLE
@interface PPRoute : NSObject

- (instancetype)init NS_UNAVAILABLE;

/// There is a route at all, meaning at least one waypoint. A one-waypoint
/// route is a real, persisted state: it is what the sheet re-opens pre-filled
/// with after somebody set a start and pocketed the phone.
@property(nonatomic, readonly) BOOL exists;

@property(nonatomic, readonly, copy) NSArray<PPWaypoint *> *waypoints;

/// The rule-file profile this route is priced with — `foot` or `bicycle` in
/// the shipped rules. Empty means the router's own default.
@property(nonatomic, readonly, copy) NSString *profile;

/// The route's own beach setting.
@property(nonatomic, readonly) PPBeachUse beachUse;

/// The beach stretches the planned route rides, in travel order, each with
/// the tide's verdict for `departTime`.
@property(nonatomic, readonly, copy) NSArray<PPBeachStretch *> *beachStretches;

/// Total metres of `beachStretches`.
@property(nonatomic, readonly) double beachMeters;

/// Set when the setting admitted the beach and the tide took it off; the
/// stretch that did it, as timed on the plan that was discarded, is
/// `droppedStretch`.
@property(nonatomic, readonly) PPBeachDropped beachDropped;
@property(nonatomic, readonly, nullable) PPBeachStretch *droppedStretch;

/// The departure the tide was judged for, epoch seconds.
@property(nonatomic, readonly) NSTimeInterval departTime;

/// The drawn line follows roads rather than straight legs between waypoints.
/// NO both before anything has been planned and when the plan came back with
/// nothing drawable.
@property(nonatomic, readonly) BOOL isCalculated;

/// Every leg followed a road. NO when some pair fell back to a straight line;
/// `straightLegs` says how many. A route can be calculated and incomplete, and
/// the map draws exactly that — roads where there are roads, a straight line
/// where there is not.
@property(nonatomic, readonly) BOOL isComplete;
@property(nonatomic, readonly) NSInteger straightLegs;

/// Priced as a bicycle route, which is why the line is drawn dashed. Derived
/// from the profile name, which is right for the two modes this app has a
/// control for and silent about a third.
@property(nonatomic, readonly) BOOL isBicycle;

@property(nonatomic, readonly) double lengthMeters;
@property(nonatomic, readonly) NSTimeInterval seconds;

/// The planner's own sentence, or empty. Shown by the sheet: the overlay's
/// on-canvas status line is off, because route.py draws it at (10, 20), which
/// is underneath the Dynamic Island on a phone.
@property(nonatomic, readonly, copy) NSString *statusText;

/// What the last plan cost. Planning runs on the queue that draws, so this is
/// how long a frame was not being drawn. See `PPMap.h` on why that is
/// acceptable at Kiawah's size and what the way out is.
@property(nonatomic, readonly) double planMilliseconds;

@end

/// Where a point is, in words a rider can act on.
///
/// A lat/lon is the one thing somebody on a bike cannot check, so the pick
/// button and the Location rows name the nearest thing the rider could ride
/// on instead. This carries the fact; the English is composed in SwiftUI,
/// because the button says "Use Flyaway Drive" and the row says "Flyaway
/// Drive" and neither should take the other's sentence apart.
///
/// `isUsable` NO is not a refusal: the pick stands and the button stays
/// enabled. It is what the open sea says, what a pack with no road graph
/// says, and what a footpath-only corner says to a bicycle.
NS_SWIFT_SENDABLE
@interface PPPlace : NSObject

- (instancetype)init NS_UNAVAILABLE;

/// Something the selected profile could actually travel on was found.
@property(nonatomic, readonly) BOOL isUsable;

/// The road's own name ("Flyaway Drive"), or its class spelled for a human
/// ("cycleway") when it has none. Empty when `isUsable` is NO.
@property(nonatomic, readonly, copy) NSString *name;

/// Whether `name` came from the road or from its class.
@property(nonatomic, readonly) BOOL isNamed;

/// How far the point is from that road, metres. 0 when nothing was found.
@property(nonatomic, readonly) double distanceMeters;

@end

/// A feature the pick landed on, and its exact coordinate.
///
/// A different kind of answer from `PPPlace`. A place describes a coordinate
/// the user chose, naming the road they are over; a snap target replaces that
/// coordinate, returning the surveyed position out of the document rather
/// than the un-projection of the pixel a thumb managed to hit.
///
/// Which overlay it came from is deliberately absent. `name` already carries
/// whatever the answering overlay thought worth showing, and the shell's job
/// is to put those words on a button rather than know who minted them. The
/// day something must treat a route waypoint differently from a point is the
/// day this gains a field.
NS_SWIFT_SENDABLE
@interface PPSnapTarget : NSObject

- (instancetype)init NS_UNAVAILABLE;

/// The feature's own coordinate, at the document's full precision.
@property(nonatomic, readonly) PPGeoPoint coordinate;

/// What to call it: "Ruddy Turnstone", "Beach loop: RTURN". Never empty: an
/// overlay with nothing to say falls back to an id rather than a blank.
@property(nonatomic, readonly, copy) NSString *name;

/// How far the finger was from it, in iOS points rather than surface pixels.
/// The bridge converts back, because points are what the shell reasons in.
@property(nonatomic, readonly) double distancePoints;

@end

NS_ASSUME_NONNULL_END
