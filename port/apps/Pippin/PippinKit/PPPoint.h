// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPPoint.h — a point as the sheet sees it.
//
// One immutable value, which is the whole of the points' public bridge. What
// crosses the thread boundary is a value, so the main thread can hold one
// while the render queue edits the set, with no lock. `PPMap` owns the live
// document; this is a photograph of one row.
//
// Immutable while the editor is not, the same separation `RouteDraft` makes
// in Swift: a sheet edits its own draft, allowed to be half-filled and
// abandoned, and builds one of these only on Save. A mutable bridge object
// would become a second handle to render-queue state the moment somebody
// passed it back down.
//
// The palette is here too, as `PPPointSymbol`. A `.fvpoints` document carries
// its own artwork in a `symbols` table of PNG blobs, and a point's `symbolId`
// is a foreign key into it rather than a path into an icon directory, so the
// file opens with its symbology on a machine that has never heard of the icon
// set.
//
// The blob crosses as a blob. The document stores PNG bytes and
// `UIImage(data:)` takes exactly those: nothing is rasterised through
// `CpuCanvas`, nothing is re-encoded, and UIKit caches the decode.

#import <Foundation/Foundation.h>

#import <PippinKit/PPGeometry.h>

NS_ASSUME_NONNULL_BEGIN

/// The six shapes `fv::PointShape` has, spelled as the document spells them.
/// Strings rather than an enum because that is what the `shape` column holds,
/// and because a document written by a newer version must still open, which a
/// name survives and an enum does not.
FOUNDATION_EXPORT NSString *const PPPointShapeCircle;
FOUNDATION_EXPORT NSString *const PPPointShapeSquare;
FOUNDATION_EXPORT NSString *const PPPointShapeTriangle;
FOUNDATION_EXPORT NSString *const PPPointShapeDiamond;
FOUNDATION_EXPORT NSString *const PPPointShapeCross;
FOUNDATION_EXPORT NSString *const PPPointShapeStar;

/// Every shape, in the order a picker should offer them.
FOUNDATION_EXPORT NSArray<NSString *> *PPPointShapeNames(void);

/// One row of the document's embedded artwork.
///
/// A palette rather than a projection of the points: a symbol nothing
/// references is kept, saved and handed back, which lets a picker offer the
/// set the author assembled before any point wore it.
NS_SWIFT_SENDABLE
@interface PPPointSymbol : NSObject

- (instancetype)init NS_UNAVAILABLE;

/// The `symbols` row id — the value a point's `symbolId` holds.
@property(nonatomic, readonly) int64_t symbolId;

/// The author's own name for it ("bicycle"), unique within the document. What
/// a picker labels the cell with, and the handle for hand-written INSERTs.
@property(nonatomic, readonly, copy) NSString *name;

/// The PNG bytes exactly as the document stores them; `UIImage(data:)` takes
/// these directly.
///
/// Empty for a row whose blob would not read, which is not an error: a point
/// referencing it falls back to its `shape`, following the same rule as the
/// colour parser, that one bad cell must not fail a good document.
@property(nonatomic, readonly, copy) NSData *imageData;

@end

/// One point, photographed.
NS_SWIFT_SENDABLE
@interface PPMapPoint : NSObject

- (instancetype)init NS_UNAVAILABLE;

/// Builds a point from the fields an editor collected.
///
/// A `pointId` of 0 means not yet in the document, and the store assigns one.
/// It is not hidden behind a second initializer because an editor holds one
/// object through both states, and two types would mean converting between
/// them at the moment the user pressed Save.
- (instancetype)initWithPointId:(int64_t)pointId
                           name:(NSString *)name
                     coordinate:(PPGeoPoint)coordinate
                          shape:(NSString *)shape
                         sizePx:(double)sizePx
                       colorHex:(NSString *)colorHex
                       symbolId:(int64_t)symbolId
                       category:(NSString *)category
                    elevationFt:(double)elevationFt
                        remarks:(NSString *)remarks
                          phone:(NSString *)phone
                            url:(NSString *)url NS_DESIGNATED_INITIALIZER;

/// The document's own id, and the handle everything else uses: the store
/// updates and deletes by it and a hit test answers with it. 0 means not yet
/// written.
@property(nonatomic, readonly) int64_t pointId;

@property(nonatomic, readonly, copy) NSString *name;
@property(nonatomic, readonly) PPGeoPoint coordinate;

/// One of the `PPPointShape*` constants. An unrecognised name reads back as
/// `PPPointShapeCircle`, which is `fv::PointShapeFromString`'s rule.
@property(nonatomic, readonly, copy) NSString *shape;

/// The marker's full width in authored pixels, which `PPMap` turns into iOS
/// points through the overlay's `symbol_dpi_scale`, so it is a physical size
/// on any screen, like the ownship's and the route's.
@property(nonatomic, readonly) double sizePx;

/// `#rrggbb` or `#rrggbbaa`. A fully transparent colour is how a document asks
/// for the bare embedded icon with no badge under it, which is why the alpha
/// is carried rather than dropped.
@property(nonatomic, readonly, copy) NSString *colorHex;

/// The embedded-artwork row this point wears, or 0 for none: a key into the
/// document's own `symbols` table (`PPMap.pointSymbols`).
///
/// A symbol point is a badge, with the icon stamped on the point's shape in
/// the point's colour at 0.62 of its width. Icon sets are authored as black
/// artwork on transparency, so a bare tile would be invisible over a dark
/// chart and would throw `colorHex` away. An id not in the table, from a
/// document whose palette was edited out from under it, draws as the shape
/// alone, so `shape` is never dead weight.
@property(nonatomic, readonly) int64_t symbolId;

@property(nonatomic, readonly, copy) NSString *category;
@property(nonatomic, readonly) double elevationFt;
@property(nonatomic, readonly, copy) NSString *remarks;

/// Free text, both, and empty is the normal value. The document holds what
/// somebody typed; `-dialURL` and `-webURL` below are this layer's opinion
/// about it, offered rather than enforced.
@property(nonatomic, readonly, copy) NSString *phone;
@property(nonatomic, readonly, copy) NSString *url;

/// `tel:` for `phone`, or nil when there is nothing dialable in it.
///
/// The punctuation is stripped and the display string is not: a rider reads
/// "(843) 555-0100" and `tel:` wants the digits, so the two are different
/// strings for one fact. `+` survives because it leads an international
/// number; everything else non-digit goes, including any `ext.`, because no
/// tel: spelling of an extension is honoured by every carrier.
@property(nonatomic, readonly, nullable) NSURL *dialURL;

/// `url` as something openable, or nil.
///
/// A bare host gets `https://`, because "kiawahresort.com" is what a person
/// types and `NSURL` would return it as a schemeless relative path that opens
/// nothing. Anything with a scheme already is left exactly as it is,
/// including `http:`, which may be somebody's local weather station.
@property(nonatomic, readonly, nullable) NSURL *webURL;

@end

NS_ASSUME_NONNULL_END
