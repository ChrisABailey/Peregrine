// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPSearch.h — one answer to "where is X".
//
// What the search seam produces, in the shape a SwiftUI list can render
// without asking a second question. The seam is `fvkit/app/search.h`: a query
// goes to every overlay answering `AsSearch()`, the union is ranked once, and
// what arrives here is that ranking with two Pippin decisions on top, both in
// `PPSearchRules.h`.
//
// Provenance is deliberately absent. `fv::app::SearchResult` carries
// `Overlay*` and a `uint64_t feature`, enough for a shell to select the thing
// it found in the document it came from, but Pippin never does: a search here
// answers where a stop should be, and a stop is made of a coordinate. A raw
// overlay pointer crossing the render queue into SwiftUI would be a lifetime
// this bridge cannot promise, for a field nothing reads. `PPSnapTarget` has
// the same note.

#import <Foundation/Foundation.h>

#import <PippinKit/PPGeometry.h>

NS_ASSUME_NONNULL_BEGIN

/// What kind of thing was found, in the three words Pippin's list is allowed
/// to use. `PPSearchRules.h` is where a provider's answer becomes one of
/// these, and why there are three and not thirty.
typedef NS_ENUM(NSInteger, PPSearchKind) {
  /// A place in the rider's own `points.fvpoints`.
  PPSearchKindPoint = 0,
  /// A street: the routing graph, or the chart's transportation layers.
  PPSearchKindRoad = 1,
  /// Anything else the chart knows a name for.
  PPSearchKindPoi = 2,
};

/// The word for a kind: "Point", "Road", "POI". A C function rather than a
/// Swift extension, so the vocabulary has one definition and it is the one
/// the mac test asserts on.
FOUNDATION_EXPORT NSString *PPSearchKindWord(PPSearchKind kind);

/// One row of a search.
NS_SWIFT_SENDABLE
@interface PPSearchResult : NSObject

- (instancetype)init NS_UNAVAILABLE;

/// The provider's primary label. Never empty: a provider with nothing to call
/// a thing falls back to an id rather than a blank, so no row is unreadable.
@property(nonatomic, readonly, copy) NSString *title;

@property(nonatomic, readonly) PPSearchKind kind;
/// `PPSearchKindWord(kind)`, carried on the row so a list cell is one
/// property access rather than a function call in a view body.
@property(nonatomic, readonly, copy) NSString *kindWord;

/// Where a stop placed here would go, and what the distance is measured to:
/// the label anchor, which for a road is a point on it rather than the centre
/// of its box, since a bent road's box centre can be off the road entirely.
@property(nonatomic, readonly) PPGeoPoint coordinate;

/// What framing should show. Degenerate for a point, which is the honest
/// answer rather than a missing one: no scale fits a dimensionless thing, so
/// the camera centres and keeps the scale the rider was reading
/// (`pippin::ScaleToFitBounds`).
@property(nonatomic, readonly) PPGeoBounds bounds;
/// NO when `bounds` is that degenerate box. Read by the framing, so no caller
/// has to compare two doubles for equality.
@property(nonatomic, readonly) BOOL hasExtent;

/// How far from the centre of the view the search was asked from, in metres.
/// 0 when there was no viewport to measure from, which is the first frame of
/// a launch and nothing else.
///
/// The same metre the router, the snapper and `fv::app::SearchDistanceMeters`
/// measure in, so a row saying 400 m agrees with a route that says 400 m.
@property(nonatomic, readonly) double distanceMeters;

@end

NS_ASSUME_NONNULL_END
