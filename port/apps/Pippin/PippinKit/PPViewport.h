// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPViewport.h — where the map looks, as an immutable value.
//
// Pippin is split along a thread boundary: `PPMap` (pack, renderer, canvas)
// is confined to the render queue and the main thread drives a camera. This
// is the camera, and it is a value rather than a setter on the map for three
// reasons:
//
//   1. It crosses threads safely. Immutable and self-contained, since every
//      derivation returns a new one, so the main thread can hold one, hand a
//      copy to the render queue, and keep moving the one it has. No lock.
//   2. It does the gesture arithmetic with the real projection. A pan is
//      which position is under this pixel now, and a pinch is keep this
//      position under this finger: both are `MapProjection`'s own
//      `SurfaceToGeo`/`GeoToSurface`, so nothing re-derives equal-arc
//      geometry in Swift and every derivation is already rotation-aware.
//   3. The frame the render queue returns carries the viewport it was drawn
//      at, which is what the preview transform needs: a frame and the live
//      camera are two viewports, and the difference between them is the
//      transform the screen applies while the next frame is in flight.
//
// Limits are part of the value rather than of the gesture code. The pack
// decides how far in and out its data supports, `PPMap` stamps that onto the
// first viewport, and every derivation enforces it, leaving gestures as pure
// geometry. A clamped pinch still keeps the anchor under the finger, because
// the clamp happens before the anchor correction.
//
// Everything public here is in points, what UIKit hands a gesture recognizer.
// The backing scale is carried alongside, so the pixel surface and the
// physical pitch are derived in one place.

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#import <PippinKit/PPGeometry.h>

NS_ASSUME_NONNULL_BEGIN

/// `NS_SWIFT_SENDABLE` holds because every property is read-only, every
/// derivation returns a new object, and nothing is mutated after `init`. That
/// is what lets the main thread hand one to the render queue with no lock.
NS_SWIFT_SENDABLE
@interface PPViewport : NSObject <NSCopying>

/// Viewports are minted by `PPMap`, which knows the pack, and derived from
/// each other. No public initializer on purpose: a viewport with limits
/// nobody set is a map that pans into the ocean and cannot get back.
- (instancetype)init NS_UNAVAILABLE;

// --- the camera ----------------------------------------------------------

@property(nonatomic, readonly) PPGeoPoint center;
/// The 1:N of a paper chart — larger is further out.
@property(nonatomic, readonly) double scaleDenominator;
/// Clockwise turn of the chart on screen. Zero is the byte-exact identity
/// (`MapProjection::SetRotation`). Two writers: the two-finger rotate
/// gesture, and GPS mode's track-up.
@property(nonatomic, readonly) double rotationDegrees;

// --- the surface ---------------------------------------------------------

@property(nonatomic, readonly) CGSize sizeInPoints;
/// `UIScreen.scale`: the pixel surface is `sizeInPoints * displayScale`.
@property(nonatomic, readonly) CGFloat displayScale;
/// One rendered pixel in millimetres: the surface's own pitch, which belongs
/// to the projection. `MapProjection::SetPhysicalScale` places geometry on a
/// real screen, so it wants the real pixel.
@property(nonatomic, readonly) double mmPerPixel;

/// One point in millimetres (`mmPerPixel * displayScale`), which is what an
/// authored pixel means on a phone. The style engine and tile source derive
/// their zoom over this, the camera's limits are built from it, and a GL
/// style's `line-width` is drawn in it. Given the surface pixel instead, the
/// map comes out 1.6 zoom levels in and 5x heavy.
@property(nonatomic, readonly) double mmPerPoint;

// --- what the pack allows ------------------------------------------------

@property(nonatomic, readonly) double minScaleDenominator;  ///< zoomed in
@property(nonatomic, readonly) double maxScaleDenominator;  ///< zoomed out
/// The box the centre may not leave, which is the pack's own extent: a map
/// draggable until the data is off screen has no way home.
@property(nonatomic, readonly) PPGeoBounds centerBounds;

/// True when the surface has a size. A viewport straight out of `PPMap` has
/// not met the screen yet and cannot be rendered or panned.
@property(nonatomic, readonly) BOOL hasSurface;

// --- geometry ------------------------------------------------------------

/// Which position is under this point of the screen.
- (PPGeoPoint)geoAtPoint:(CGPoint)pointInPoints NS_SWIFT_NAME(geo(at:));
/// And back. Positions outside the surface come back outside it.
- (CGPoint)pointForGeo:(PPGeoPoint)geo NS_SWIFT_NAME(point(forGeo:));

// --- derivations, all clamped, all returning a new value -----------------

/// The screen changed size, rotated, or moved to another screen. The limits
/// are recomputed, because how far out is useful depends on how much screen
/// there is to put the island on.
- (PPViewport *)viewportWithSurfaceSize:(CGSize)sizeInPoints
                           displayScale:(CGFloat)displayScale
    NS_SWIFT_NAME(resized(to:displayScale:));

/// The startup view: centred on the pack, at the scale whose data fills the
/// screen. Cover rather than contain, because the pack's box is landscape and
/// a phone is portrait, so containing it would show a third of a screen of
/// map between two bands of background. The long axis runs off the screen and
/// one pinch brings it back.
- (PPViewport *)viewportAtHomeView NS_SWIFT_NAME(atHomeView());

/// The content follows the finger: a drag of +dx moves the map +dx, so the
/// centre moves the other way. Expressed as which position is under the
/// centre pixel now, so rotation needs no special case.
- (PPViewport *)viewportByPanningBy:(CGVector)deltaInPoints
    NS_SWIFT_NAME(panned(by:));

/// `factor` > 1 zooms in, being a pinch's magnification, and the position
/// under `anchor` stays there. When the factor hits a limit the anchor is
/// still honoured, at the scale the limit allows.
- (PPViewport *)viewportByZoomingBy:(double)factor
                              about:(CGPoint)anchorInPoints
    NS_SWIFT_NAME(zoomed(by:about:));

/// Turns the chart. `degrees` is a clockwise increment and the position under
/// `anchor` stays there, the same shape as the pinch above and for the same
/// reason: turning about the middle of the screen while the fingers are
/// elsewhere slides the map out from under them.
///
/// The dead zone that keeps an accidental twist during a pinch from turning
/// the chart belongs to the recognizer (`MapGestureView`), not to this value:
/// by the time a delta arrives here somebody has meant it.
- (PPViewport *)viewportByRotatingBy:(double)degrees
                               about:(CGPoint)anchorInPoints
    NS_SWIFT_NAME(rotated(by:about:));

/// The camera, set outright. Used by the ownship recentre and by track-up.
- (PPViewport *)viewportWithCenter:(PPGeoPoint)center
                  scaleDenominator:(double)scaleDenominator
                   rotationDegrees:(double)rotationDegrees
    NS_SWIFT_NAME(moved(toCenter:scaleDenominator:rotationDegrees:));

/// The zoom-out rule: widen, never narrow, until `coordinate` is in view from
/// `ship`. The centre and rotation are untouched, because on entering GPS
/// mode the centre becomes the camera's business and this is only about how
/// much ground the screen shows.
///
/// The arithmetic is `PPCameraFit.h`, pure C++ tested on the mac; this is the
/// plumbing that gives it the surface. It fits a disc rather than a bounding
/// box, so the answer survives the chart turning, and allows for the ship
/// sitting at MM2's track-up anchor rather than at the centre.
///
/// Nothing to do — already in view, no surface yet, the pack's zoom-out limit
/// reached — comes back as the same viewport's camera.
- (PPViewport *)viewportWideEnoughToShow:(PPGeoPoint)coordinate
                                fromShip:(PPGeoPoint)ship
    NS_SWIFT_NAME(widened(toShow:from:));

/// Frames a search result: centred on `bounds`, at the scale that shows it,
/// pushed `topBias` of the height up the screen.
///
/// Three things it does that `widened(toShow:from:)` does not, which is the
/// difference between making room for something and going to it.
///
///  * It narrows as well as widening, because this is the answer to a search.
///    The zoom-out rule may never narrow, since it fires on a button press
///    that was about something else.
///  * A degenerate box only re-centres. A point's honest bounds has `ll ==
///    ur` and no scale fits a dimensionless thing, so the rider keeps the
///    scale they were reading. `PPSearchResult.hasExtent` is the same test.
///  * It aims above the middle, because the sheet that asked is still on
///    screen. `topBias` is a fraction of the height, allowed for in the fit
///    as well as the pan, so a road pushed up does not end up half off the
///    top.
///
/// The rotation is untouched, and the fit is a disc for that reason.
/// `minScaleDenominator` is the floor the fit will not pass;
/// `PPMap.searchFrameMinScale` is the pack's own and 0 asks for none.
/// Everything is clamped against the pack's zoom limits afterwards.
- (PPViewport *)viewportFramingBounds:(PPGeoBounds)bounds
                  minScaleDenominator:(double)minScaleDenominator
                              topBias:(double)topBias
    NS_SWIFT_NAME(framing(_:minScaleDenominator:topBias:));

/// The guard band: the same camera on a larger canvas, `margin` of the surface
/// added on each side, so 0.25 is 2.25x the pixels.
///
/// Not `resized(to:displayScale:)`, and the difference is load-bearing. That
/// method is for a screen changing, so it recomputes the zoom limits and
/// re-clamps the camera against them. A band is not a screen but a canvas the
/// screen is a window onto, and running the limits over it would move
/// `maxScaleDenominator` and could clamp the very scale the cache is keyed
/// on, returning a band drawn at a scale the live camera is not at. The
/// limits here are the screen's, copied.
///
/// A margin of 0 returns the same surface, which is what a running pinch
/// asks for: nothing is reusable during a zoom, so nothing should be paid
/// for.
- (PPViewport *)viewportGrownByMargin:(double)margin
    NS_SWIFT_NAME(grown(byMargin:));

/// The cache question, asked of a band: could a base map drawn for this
/// viewport be composited for `other` without being drawn again?
///
/// The arithmetic is `PPBaseCoverage.h` and `PPMap` asks it too. It is
/// exposed so the shell can ask before deciding whether a frame is worth
/// starting, which matters because of the live-render budget: the loop drops
/// to the preview transform during a gesture when the last frame was slow,
/// and the last frame is no longer a fair guess at the next one's cost. A
/// frame the cache can serve is an overlay pass whatever the last one cost.
///
/// `maxTurn` is the same quality limit `PPMap` uses
/// (`display.base_cache_max_turn_deg`).
- (BOOL)coversViewport:(PPViewport *)other maxTurnDegrees:(double)maxTurn
    NS_SWIFT_NAME(covers(_:maxTurnDegrees:));

/// Same camera and same surface, so nothing to redraw. Not `isEqual:`: this
/// compares what a render depends on, and the limits are not part of that.
- (BOOL)isEquivalentToViewport:(nullable PPViewport *)other
    NS_SWIFT_NAME(isEquivalent(to:));

@end

NS_ASSUME_NONNULL_END
