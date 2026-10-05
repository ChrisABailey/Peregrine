// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPViewport+Internal.h"

#include <algorithm>
#include <cmath>

#include "PPBaseCoverage.h"
#include "PPCameraFit.h"
#include "PPPerspective.h"
#include "fv_web_mercator.h"

namespace {

// Apple's baseline point density: one POINT is 1/163 inch on an iPhone, and
// the backing scale multiplies it. Getting the pitch from the scale — rather
// than assuming a desktop 96 dpi — is what makes a 0.32 mm line 0.32 mm.
constexpr double kPointsPerInchBaseline = 163.0;

// HOW FAR IN. The pyramid stops at its maxzoom (z14 in the delivered pack) and
// the display does not have to: `OsmVectorSource` clamps the READ zoom and
// lets the style keep going, which is overzoom, and it is documented there.
// The near limit is therefore maxzoom drawn at one tile pixel per POINT —
// the density a phone map is authored for — plus five levels. Five, because
// z14 + 3 (1:6,456, ~400 m across a phone) was the scale a rider reads at and
// not the scale a rider INSPECTS at: standing at a junction, or picking one
// path of a pair a few metres apart, wants the two more levels this asks for
// (1:1,614, ~100 m across a phone). Nothing is READ past the pyramid — the
// deepest tiles there are get drawn bigger, which is what overzoom means, so
// the cost of the last two levels is nothing but a coarser-looking map.
constexpr double kOverzoomLevels = 5.0;

// HOW FAR OUT. Two levels past the view that fits the whole pack: the island
// a quarter of the screen wide, which is far enough to see where you are and
// near enough that the way back is one pinch. The file's own minzoom is the
// outer bound (below), and for a one-island pack it is nowhere near as tight.
constexpr double kZoomOutBeyondHome = 2.0;

// Metres per degree of latitude, the same constant PythonView's
// `_fit_scale_rect` uses. A view-fitting margin, not a geodesic calculation.
constexpr double kMetresPerDegree = 111320.0;

// A box fitted exactly touches both edges, and an island that touches both
// edges looks like a rendering bug. PythonView's margin, kept. It applies to
// the contain fit, which is now only what the zoom-out limit is derived from;
// see `homeScaleFilling:`.
constexpr double kHomeFitMargin = 1.1;

// How much the opening view overfills the screen. A cover computed exactly
// touches on the short axis, where one pixel of rounding is a row of style
// background along an edge; 2% is below noticing and above the rounding.
constexpr double kHomeCoverOverfill = 1.02;

double Clamp(double v, double lo, double hi) {
  return std::max(lo, std::min(hi, v));
}

}  // namespace

@implementation PPViewport {
  fv::MapProjection _proj;

  // What the pack allows, carried so a derivation needs nothing else.
  PPGeoBounds _home;
  int _minTileZoom;
  int _maxTileZoom;
  double _mmPerPixelOverride;  // 0 = derive from the backing scale

  // The camera. `_proj` mirrors these; these are the authority.
  PPGeoPoint _center;
  double _scaleDenominator;
  double _rotationDegrees;
  // The screen's tilt. Not part of `_proj`, which stays the flat camera.
  double _pitchDegrees;

  // The surface.
  CGSize _sizeInPoints;
  CGFloat _displayScale;
  double _mmPerPixel;
  int _pixelWidth;
  int _pixelHeight;

  double _minDen;
  double _maxDen;
}

- (instancetype)initWithHomeBounds:(PPGeoBounds)home
                       minTileZoom:(int)minZoom
                       maxTileZoom:(int)maxZoom
                mmPerPixelOverride:(double)mmPerPixelOverride {
  self = [super init];
  if (self == nil) return nil;
  _home = home;
  _minTileZoom = minZoom;
  _maxTileZoom = maxZoom;
  _mmPerPixelOverride = mmPerPixelOverride > 0 ? mmPerPixelOverride : 0.0;

  _center = PPGeoPointMake((home.southWest.latitude + home.northEast.latitude) / 2.0,
                           (home.southWest.longitude + home.northEast.longitude) / 2.0);
  _scaleDenominator = 50.0e3;
  _rotationDegrees = 0.0;
  _pitchDegrees = 0.0;

  _sizeInPoints = CGSizeZero;
  _displayScale = 1.0;
  _mmPerPixel = _mmPerPixelOverride > 0 ? _mmPerPixelOverride
                                        : 25.4 / kPointsPerInchBaseline;
  _pixelWidth = 0;
  _pixelHeight = 0;
  // No surface yet, so no honest limits either: the first
  // `viewportWithSurfaceSize:` computes them. Until then the camera setters
  // clamp against a range that refuses nothing.
  _minDen = 1.0;
  _maxDen = 1.0e9;
  [self reconfigure];
  return self;
}

// Every derivation goes through here, so there is exactly one copy path and
// exactly one place the invariants are restored.
- (instancetype)derivedWithCenter:(PPGeoPoint)center
                 scaleDenominator:(double)den
                  rotationDegrees:(double)rot {
  PPViewport *v = [[PPViewport alloc] initWithHomeBounds:_home
                                             minTileZoom:_minTileZoom
                                             maxTileZoom:_maxTileZoom
                                      mmPerPixelOverride:_mmPerPixelOverride];
  v->_sizeInPoints = _sizeInPoints;
  v->_displayScale = _displayScale;
  v->_mmPerPixel = _mmPerPixel;
  v->_pixelWidth = _pixelWidth;
  v->_pixelHeight = _pixelHeight;
  v->_minDen = _minDen;
  v->_maxDen = _maxDen;
  v->_pitchDegrees = _pitchDegrees;
  [v setCamera:center scaleDenominator:den rotationDegrees:rot];
  return v;
}

- (void)setCamera:(PPGeoPoint)center
    scaleDenominator:(double)den
     rotationDegrees:(double)rot {
  // The centre may not leave the pack's box. A simple per-axis clamp: the
  // pack is one island, so there is no antimeridian case to get wrong, and
  // pretending to handle one would be untested code.
  _center = PPGeoPointMake(
      Clamp(center.latitude, _home.southWest.latitude, _home.northEast.latitude),
      Clamp(center.longitude, _home.southWest.longitude, _home.northEast.longitude));
  _scaleDenominator = Clamp(den, _minDen, _maxDen);
  double r = std::fmod(rot, 360.0);
  if (r < 0) r += 360.0;
  _rotationDegrees = r;
  [self reconfigure];
}

- (void)reconfigure {
  if (_pixelWidth > 0 && _pixelHeight > 0)
    _proj.SetSurfaceSize(_pixelWidth, _pixelHeight);
  _proj.SetCenter(fv::GeoPoint{_center.latitude, _center.longitude});
  _proj.SetPhysicalScale(_scaleDenominator, _mmPerPixel);
  // Rotation is set LAST so it cannot be read as one of the scale calls
  // (PythonView says the same thing in `_render_vector`).
  _proj.SetRotation(_rotationDegrees);
}

#pragma mark - Accessors

- (PPGeoPoint)center { return _center; }
- (double)scaleDenominator { return _scaleDenominator; }
- (double)rotationDegrees { return _rotationDegrees; }
- (double)pitchDegrees { return _pitchDegrees; }
- (CGSize)sizeInPoints { return _sizeInPoints; }
- (CGFloat)displayScale { return _displayScale; }
- (double)mmPerPixel { return _mmPerPixel; }
- (double)mmPerPoint { return _mmPerPixel * (double)_displayScale; }
- (double)minScaleDenominator { return _minDen; }
- (double)maxScaleDenominator { return _maxDen; }
- (PPGeoBounds)centerBounds { return _home; }
- (BOOL)hasSurface { return _pixelWidth > 0 && _pixelHeight > 0; }
- (int)pixelWidth { return _pixelWidth; }
- (int)pixelHeight { return _pixelHeight; }
- (const fv::MapProjection &)projection { return _proj; }

- (pippin::Perspective)perspective {
  pippin::PerspectiveParams p;
  p.width = _pixelWidth;
  p.height = _pixelHeight;
  p.pitch_deg = _pitchDegrees;
  p.anchor_x = (_pixelWidth - 1) / 2.0;
  p.anchor_y = (_pixelHeight - 1) * (0.5 + pippin::kTrackUpAheadFraction);
  return pippin::Perspective(p);
}

- (BOOL)groundQuadX:(double *)xs y:(double *)ys {
  if (_pitchDegrees == 0.0 || ![self hasSurface]) return NO;
  return [self perspective].GroundQuad(xs, ys) ? YES : NO;
}

- (id)copyWithZone:(nullable NSZone *)zone {
  // Immutable: a copy is the same value, and the whole point of the class is
  // that handing one to another thread costs nothing.
  return self;
}

#pragma mark - Geometry

- (PPGeoPoint)geoAtPoint:(CGPoint)pointInPoints {
  fv::GeoPoint g{_center.latitude, _center.longitude};
  if ([self hasSurface]) {
    _proj.SurfaceToGeo(pointInPoints.x * _displayScale,
                       pointInPoints.y * _displayScale, &g);
  }
  return PPGeoPointMake(g.lat, g.lon);
}

- (CGPoint)pointForGeo:(PPGeoPoint)geo {
  double sx = 0, sy = 0;
  if (![self hasSurface]) return CGPointZero;
  _proj.GeoToSurface(fv::GeoPoint{geo.latitude, geo.longitude}, &sx, &sy);
  return CGPointMake((CGFloat)(sx / _displayScale), (CGFloat)(sy / _displayScale));
}

- (CGPoint)screenPointForFlatPoint:(CGPoint)flat {
  if (_pitchDegrees == 0.0 || ![self hasSurface]) return flat;
  const double k = (double)_displayScale;
  double x = 0.0, y = 0.0;
  if (![self perspective].FlatToScreen(flat.x * k, flat.y * k, &x, &y))
    return CGPointMake(NAN, NAN);
  return CGPointMake((CGFloat)(x / k), (CGFloat)(y / k));
}

- (CGPoint)flatPointForScreenPoint:(CGPoint)screen {
  if (_pitchDegrees == 0.0 || ![self hasSurface]) return screen;
  const double k = (double)_displayScale;
  double x = 0.0, y = 0.0;
  if (![self perspective].ScreenToFlat(screen.x * k, screen.y * k, &x, &y))
    return CGPointMake(NAN, NAN);
  return CGPointMake((CGFloat)(x / k), (CGFloat)(y / k));
}

- (double)depthScaleAtScreenPoint:(CGPoint)screen {
  if (_pitchDegrees == 0.0 || ![self hasSurface]) return 1.0;
  return [self perspective].DepthScale(screen.y * (double)_displayScale);
}

// Spelled out rather than `CATransform3DIdentity`: PippinKit uses the struct
// but does not link QuartzCore.
static CATransform3D IdentityTransform() {
  CATransform3D t = {};
  t.m11 = t.m22 = t.m33 = t.m44 = 1.0;
  return t;
}

- (CATransform3D)perspectiveTransform {
  if (_pitchDegrees == 0.0 || ![self hasSurface]) return IdentityTransform();
  // H acts on device pixels and the compositor works in points; conjugating by
  // the backing scale moves the translation and perspective terms across.
  const double *h = [self perspective].flat_to_screen().m;
  const double k = (double)_displayScale;
  CATransform3D t = IdentityTransform();
  // Core Animation multiplies row vectors: x' = x*m11 + y*m21 + m41, and the
  // projective divisor is x*m14 + y*m24 + m44.
  t.m11 = h[0];     t.m21 = h[1];     t.m41 = h[2] / k;
  t.m12 = h[3];     t.m22 = h[4];     t.m42 = h[5] / k;
  t.m14 = h[6] * k; t.m24 = h[7] * k; t.m44 = h[8];
  return t;
}

- (PPFarFade)farFade {
  PPFarFade out = {0.0, 0.0};
  if (_pitchDegrees == 0.0 || ![self hasSurface]) return out;
  const pippin::Perspective::Fade f = [self perspective].FarFade();
  out.endY = f.end_y / (double)_displayScale;
  out.topAlpha = f.top_alpha;
  return out;
}

- (CGRect)flatBounds {
  const CGRect screen = CGRectMake(0, 0, _sizeInPoints.width, _sizeInPoints.height);
  if (_pitchDegrees == 0.0 || ![self hasSurface]) return screen;
  const pippin::PixelRect r = [self perspective].BandRect(0);
  if (r.width <= 0 || r.height <= 0) return screen;
  const double k = (double)_displayScale;
  return CGRectMake(r.x0 / k, r.y0 / k, r.width / k, r.height / k);
}

#pragma mark - Derivations

- (PPViewport *)viewportWithPitch:(double)degrees {
  const double d = std::isfinite(degrees)
                       ? Clamp(degrees, 0.0, pippin::kMaxPitchDeg) : 0.0;
  if (d == _pitchDegrees) return self;
  PPViewport *v = [self derivedWithCenter:_center
                         scaleDenominator:_scaleDenominator
                          rotationDegrees:_rotationDegrees];
  v->_pitchDegrees = d;
  return v;
}

- (PPViewport *)viewportWithSurfaceSize:(CGSize)sizeInPoints
                           displayScale:(CGFloat)displayScale {
  const CGFloat s = displayScale > 0 ? displayScale : 1.0;
  const int w = (int)std::lround(sizeInPoints.width * s);
  const int h = (int)std::lround(sizeInPoints.height * s);
  if (w <= 0 || h <= 0) return self;

  PPViewport *v = [self derivedWithCenter:_center
                         scaleDenominator:_scaleDenominator
                          rotationDegrees:_rotationDegrees];
  v->_sizeInPoints = sizeInPoints;
  v->_displayScale = s;
  v->_pixelWidth = w;
  v->_pixelHeight = h;
  v->_mmPerPixel = _mmPerPixelOverride > 0
                       ? _mmPerPixelOverride
                       : 25.4 / (kPointsPerInchBaseline * (double)s);
  [v recomputeLimits];
  // Re-clamp against the limits that just changed, and rebuild the projection
  // over the new surface.
  [v setCamera:v->_center scaleDenominator:v->_scaleDenominator
      rotationDegrees:v->_rotationDegrees];
  return v;
}

- (PPViewport *)viewportGrownByMargin:(double)margin {
  return [self viewportGrownByMargin:margin rotationSafe:NO];
}

- (PPViewport *)viewportGrownByMargin:(double)margin
                         rotationSafe:(BOOL)rotationSafe {
  if (![self hasSurface]) return self;
  if (_pitchDegrees != 0.0) return [self tiltedBandWithMargin:margin];
  int pw = 0, ph = 0;
  pippin::GrownSurfacePixels(_pixelWidth, _pixelHeight, margin,
                             rotationSafe ? true : false, &pw, &ph);
  if (pw == _pixelWidth && ph == _pixelHeight) return self;

  // Everything is copied — the camera, the pitch AND the limits — and only
  // the surface changes. `derivedWithCenter:` already carries the limits over
  // and re-clamps against them, which is a no-op here because the camera it is
  // handed is the one that already satisfied them. The size is set in pixels
  // and the points follow, so the growth stays whole pixels on each side.
  PPViewport *v = [self derivedWithCenter:_center
                         scaleDenominator:_scaleDenominator
                          rotationDegrees:_rotationDegrees];
  v->_pixelWidth = pw;
  v->_pixelHeight = ph;
  v->_sizeInPoints = CGSizeMake((double)pw / (double)_displayScale,
                                (double)ph / (double)_displayScale);
  [v reconfigure];
  return v;
}

// The flat band a tilted screen's base is drawn into: the ground quad's
// bounding box padded by `margin` of the short side, united with the screen
// (`Perspective::BandRect`). Its pixels sit on the screen's at whole offsets,
// so it is a band panned by whole pixels. The band itself is flat; the screen
// carries the tilt. Rotation safety is not offered: a turn while tilted is
// course-up's, which the turn limit redraws within 2.5 degrees anyway.
- (PPViewport *)tiltedBandWithMargin:(double)margin {
  const double m = (margin > 0.0 && std::isfinite(margin)) ? margin : 0.0;
  const int pad = std::max(
      1, (int)std::ceil(std::min(_pixelWidth, _pixelHeight) * m));
  const pippin::PixelRect r = [self perspective].BandRect(pad);
  if (r.width <= 0 || r.height <= 0) return self;
  fv::GeoPoint g;
  if (!_proj.SurfaceToGeo(r.x0 + (r.width - 1) / 2.0,
                          r.y0 + (r.height - 1) / 2.0, &g).ok())
    return self;
  PPViewport *v = [self derivedWithCenter:_center
                         scaleDenominator:_scaleDenominator
                          rotationDegrees:_rotationDegrees];
  // Set after the derivation: its centre clamp to the pack's box would move a
  // band centred ahead of a ship near the edge off the screen's pixel grid.
  v->_center = PPGeoPointMake(g.lat, g.lon);
  v->_pitchDegrees = 0.0;
  v->_pixelWidth = r.width;
  v->_pixelHeight = r.height;
  v->_sizeInPoints = CGSizeMake((double)r.width / (double)_displayScale,
                                (double)r.height / (double)_displayScale);
  [v reconfigure];
  return v;
}

- (BOOL)isPixelAlignedToViewport:(PPViewport *)live {
  if (live == nil || ![self hasSurface] || ![live hasSurface]) return NO;
  if (_displayScale != live->_displayScale) return NO;
  return pippin::PixelAligned(_proj, live->_proj) ? YES : NO;
}

- (double)bandHeadroomForViewport:(PPViewport *)live {
  if (live == nil || ![self hasSurface] || ![live hasSurface]) return -1e300;
  double xs[4], ys[4];
  const BOOL tilted = [live groundQuadX:xs y:ys];
  return pippin::BandHeadroomPx(_proj, live->_proj, tilted ? xs : nullptr,
                                tilted ? ys : nullptr);
}

- (PPViewport *)viewportForUnderlay {
  // `derivedWithCenter:` would clamp the scale to the limits, so the scale is
  // set after it, the way `viewportGrownByMargin:` sets the surface.
  PPViewport *v = [self derivedWithCenter:_center
                         scaleDenominator:_scaleDenominator
                          rotationDegrees:_rotationDegrees];
  v->_scaleDenominator = _scaleDenominator * pippin::kUnderlayZoomOut;
  v->_pitchDegrees = 0.0;
  double xs[4], ys[4];
  const BOOL tilted = [self groundQuadX:xs y:ys];
  int uw = 0, uh = 0;
  pippin::UnderlaySurfacePixels(_pixelWidth, _pixelHeight, tilted ? xs : nullptr,
                                tilted ? ys : nullptr, &uw, &uh);
  v->_pixelWidth = uw;
  v->_pixelHeight = uh;
  v->_sizeInPoints = CGSizeMake((double)uw / (double)_displayScale,
                                (double)uh / (double)_displayScale);
  [v reconfigure];
  return v;
}

- (BOOL)underlayServesViewport:(PPViewport *)live {
  if (live == nil || ![self hasSurface] || ![live hasSurface]) return NO;
  double xs[4], ys[4];
  const BOOL tilted = [live groundQuadX:xs y:ys];
  return pippin::UnderlayServes(_proj, live->_proj, tilted ? xs : nullptr,
                                tilted ? ys : nullptr) ? YES : NO;
}

- (PPScreenCover)coverageOfViewport:(PPViewport *)live
                           underlay:(nullable PPViewport *)underlay {
  if (live == nil || ![live hasSurface]) return PPScreenCoverSharp;
  const fv::MapProjection *u =
      (underlay != nil && [underlay hasSurface]) ? &underlay->_proj : nullptr;
  double xs[4], ys[4];
  const BOOL tilted = [live groundQuadX:xs y:ys];
  switch (pippin::ScreenCoverage(_proj, u, live->_proj, tilted ? xs : nullptr,
                                 tilted ? ys : nullptr)) {
    case pippin::ScreenCover::kSharp: return PPScreenCoverSharp;
    case pippin::ScreenCover::kUnderlay: return PPScreenCoverUnderlay;
    case pippin::ScreenCover::kBackground: return PPScreenCoverBackground;
  }
  return PPScreenCoverBackground;
}

// The zoom limits come from the data: how far in is what the pyramid holds,
// how far out is how much ground the pack covers.
- (void)recomputeLimits {
  const double lat = (_home.southWest.latitude + _home.northEast.latitude) / 2.0;
  // One tile pixel per point is the density a phone map is authored at, and
  // the renderer agrees: `PPMap` derives the style's and the source's zoom
  // over this same number, so the limits and the map inside them count in one
  // unit.
  const double mmPerPoint = [self mmPerPoint];

  const double nearDen = fv::webmerc::ScaleForZoomExact(
      (double)_maxTileZoom + kOverzoomLevels, lat, mmPerPoint);
  const double farFromFile =
      fv::webmerc::ScaleForZoomExact((double)_minTileZoom, lat, mmPerPoint);
  // The out limit stays on the contain fit, deliberately. Two levels past the
  // view that fits the pack is a statement about how much ground there is,
  // and the opening view being a cover does not make the island smaller.
  // Deriving this from the cover would take away most of the zoom-out the
  // pack has data for.
  const double farFromHome =
      [self homeScaleFilling:NO] * std::exp2(kZoomOutBeyondHome);

  if (std::isfinite(nearDen) && nearDen > 0) _minDen = nearDen;
  double far = farFromHome;
  if (std::isfinite(farFromFile) && farFromFile > 0)
    far = std::min(far, farFromFile);
  if (std::isfinite(far) && far > _minDen) _maxDen = far;
}

// PythonView's `_fit_scale_rect`, in C++ and over the pitch this phone has
// rather than the desktop's assumed one, plus a choice PythonView never has
// to make: a desktop window is often the shape of the data and a phone never
// is.
//
// `fill:NO` is contain, the whole box on screen with the shorter axis padded.
// `fill:YES` is cover, the screen full of data with the longer axis cropped.
// `home_bounds` is 0.20 deg of longitude by 0.12 deg of latitude — a landscape
// box — so on a portrait phone contain fills the width and about a third of
// the height and leaves the style's background in bands above and below. That
// is what the app opened with until P12, and it is what cover is for.
- (double)homeScaleFilling:(BOOL)fill {
  if (![self hasSurface]) return _scaleDenominator;
  const double clat =
      (_home.southWest.latitude + _home.northEast.latitude) / 2.0 * M_PI / 180.0;
  const double groundW =
      std::max((_home.northEast.longitude - _home.southWest.longitude) *
                   kMetresPerDegree * std::max(std::cos(clat), 1e-6),
               1.0);
  const double groundH = std::max(
      (_home.northEast.latitude - _home.southWest.latitude) * kMetresPerDegree, 1.0);
  const double screenW = _pixelWidth * _mmPerPixel / 1000.0;
  const double screenH = _pixelHeight * _mmPerPixel / 1000.0;
  const double byWidth = groundW / screenW;
  const double byHeight = groundH / screenH;
  // Larger denominator is further out: the axis that needs the MOST zooming
  // out is the one contain has to satisfy, and the one cover may ignore.
  return fill ? std::min(byWidth, byHeight) / kHomeCoverOverfill
              : std::max(byWidth, byHeight) * kHomeFitMargin;
}

- (PPViewport *)viewportAtHomeView {
  const PPGeoPoint c =
      PPGeoPointMake((_home.southWest.latitude + _home.northEast.latitude) / 2.0,
                     (_home.southWest.longitude + _home.northEast.longitude) / 2.0);
  return [self derivedWithCenter:c
                scaleDenominator:[self homeScaleFilling:YES]
                 rotationDegrees:0.0];
}

- (PPViewport *)viewportByPanningBy:(CGVector)deltaInPoints {
  if (![self hasSurface]) return self;
  // The content follows the finger, so the position that ends up under the
  // centre pixel is the one that was `delta` before it. Asking the projection
  // rather than dividing by degrees-per-pixel is what makes this correct under
  // rotation, at any latitude, with no second copy of the geometry.
  //
  // The centre pixel is asked for, not assumed. `MapProjection` puts the
  // centre at `((w-1)/2, (h-1)/2)`, FalconView's pixel-centre convention, and
  // half a pixel of disagreement is a constant offset on every pan — measured
  // at 0.24 pt when this assumed instead of asking. Projecting the current
  // centre gives whatever the convention is.
  const CGPoint origin = [self pointForGeo:_center];
  const CGPoint p = CGPointMake(origin.x - deltaInPoints.dx,
                                origin.y - deltaInPoints.dy);
  return [self derivedWithCenter:[self geoAtPoint:p]
                scaleDenominator:_scaleDenominator
                 rotationDegrees:_rotationDegrees];
}

- (PPViewport *)viewportByZoomingBy:(double)factor about:(CGPoint)anchorInPoints {
  if (![self hasSurface] || !(factor > 0) || !std::isfinite(factor)) return self;

  const PPGeoPoint under = [self geoAtPoint:anchorInPoints];
  // The clamp happens HERE, before the anchor correction, so a pinch that
  // runs into a limit still keeps the position under the finger — it simply
  // stops getting bigger. Clamping afterwards would slide the map instead.
  PPViewport *zoomed = [self derivedWithCenter:_center
                              scaleDenominator:_scaleDenominator / factor
                               rotationDegrees:_rotationDegrees];
  const CGPoint moved = [zoomed pointForGeo:under];
  return [zoomed viewportByPanningBy:CGVectorMake(anchorInPoints.x - moved.x,
                                                  anchorInPoints.y - moved.y)];
}

- (PPViewport *)viewportByRotatingBy:(double)degrees about:(CGPoint)anchorInPoints {
  if (![self hasSurface] || !std::isfinite(degrees) || degrees == 0.0) return self;

  // Exactly the pinch's shape, one line at a time: remember what is under the
  // fingers, turn the chart, and pan by however far that position moved. The
  // rotation is normalized (and wrapped) by `setCamera:`, so nothing here has
  // to think about 360.
  const PPGeoPoint under = [self geoAtPoint:anchorInPoints];
  PPViewport *turned = [self derivedWithCenter:_center
                              scaleDenominator:_scaleDenominator
                               rotationDegrees:_rotationDegrees + degrees];
  const CGPoint moved = [turned pointForGeo:under];
  return [turned viewportByPanningBy:CGVectorMake(anchorInPoints.x - moved.x,
                                                  anchorInPoints.y - moved.y)];
}

- (PPViewport *)viewportWithCenter:(PPGeoPoint)center
                  scaleDenominator:(double)scaleDenominator
                   rotationDegrees:(double)rotationDegrees {
  return [self derivedWithCenter:center
                scaleDenominator:scaleDenominator
                 rotationDegrees:rotationDegrees];
}

- (PPViewport *)viewportWideEnoughToShow:(PPGeoPoint)coordinate
                                fromShip:(PPGeoPoint)ship {
  if (![self hasSurface]) return self;
  pippin::CameraFitSurface surface;
  surface.pixel_width = _pixelWidth;
  surface.pixel_height = _pixelHeight;
  surface.mm_per_pixel = _mmPerPixel;
  const double den = pippin::ScaleToShow(
      fv::GeoPoint{ship.latitude, ship.longitude},
      fv::GeoPoint{coordinate.latitude, coordinate.longitude}, surface,
      _scaleDenominator);
  if (den == _scaleDenominator) return self;
  // `derivedWithCenter:` clamps against the pack's own zoom-out limit, so a
  // route start further away than the pack is wide comes back at the widest
  // view there is rather than at a scale with no data in it.
  return [self derivedWithCenter:_center
                scaleDenominator:den
                 rotationDegrees:_rotationDegrees];
}

- (PPViewport *)viewportFramingBounds:(PPGeoBounds)bounds
                  minScaleDenominator:(double)minScaleDenominator
                              topBias:(double)topBias {
  if (![self hasSurface]) return self;

  const fv::GeoRect rect{
      fv::GeoPoint{bounds.southWest.latitude, bounds.southWest.longitude},
      fv::GeoPoint{bounds.northEast.latitude, bounds.northEast.longitude}};

  pippin::CameraFitSurface surface;
  surface.pixel_width = _pixelWidth;
  surface.pixel_height = _pixelHeight;
  surface.mm_per_pixel = _mmPerPixel;
  const double den = pippin::ScaleToFitBounds(rect, surface, _scaleDenominator,
                                              minScaleDenominator, topBias);

  // The centre of the box rather than the result's own anchor, because this
  // is about what fits: a bent road's label point can sit near one end, and
  // centring on it would put the other end off screen at the very scale
  // computed to hold both. A point's box is degenerate, so the two agree.
  const PPGeoPoint centre =
      PPGeoPointMake(0.5 * (bounds.southWest.latitude + bounds.northEast.latitude),
                     0.5 * (bounds.southWest.longitude + bounds.northEast.longitude));
  if (!std::isfinite(centre.latitude) || !std::isfinite(centre.longitude)) {
    return self;
  }

  PPViewport *framed = [self derivedWithCenter:centre
                              scaleDenominator:den
                               rotationDegrees:_rotationDegrees];

  // Up the screen, through the pan rather than a second centre computation.
  // `viewportByPanningBy:` is expressed as which position is under the centre
  // pixel now, which is what makes it correct under rotation: a chart the
  // rider turned still moves the result towards the top of the screen rather
  // than towards north.
  //
  // The content follows the finger, so pushing the map down is a positive dy
  // and what was at the centre ends up above it.
  const double bias = std::isfinite(topBias) ? topBias : 0.0;
  if (bias <= 0.0) return framed;
  const double dy = std::min(bias, 0.45) * framed.sizeInPoints.height;
  return [framed viewportByPanningBy:CGVectorMake(0.0, dy)];
}

- (BOOL)coversViewport:(PPViewport *)other maxTurnDegrees:(double)maxTurn {
  if (other == nil) return NO;
  pippin::BaseCoverageLimits limits;
  limits.max_rotation_delta_deg = maxTurn;
  double xs[4], ys[4];
  const BOOL tilted = [other groundQuadX:xs y:ys];
  return pippin::BaseCovers(_proj, other->_proj, limits, tilted ? xs : nullptr,
                            tilted ? ys : nullptr)
             ? YES
             : NO;
}

- (BOOL)isEquivalentToViewport:(nullable PPViewport *)other {
  if (other == nil) return NO;
  if (other == self) return YES;
  return other->_pixelWidth == _pixelWidth && other->_pixelHeight == _pixelHeight &&
         other->_center.latitude == _center.latitude &&
         other->_center.longitude == _center.longitude &&
         other->_scaleDenominator == _scaleDenominator &&
         other->_rotationDegrees == _rotationDegrees &&
         other->_pitchDegrees == _pitchDegrees &&
         other->_mmPerPixel == _mmPerPixel;
}

- (NSString *)description {
  return [NSString stringWithFormat:@"<PPViewport %.5f,%.5f 1:%.0f %.0f° pitch %.0f° %.0fx%.0f@%.0fx>",
                                    _center.latitude, _center.longitude,
                                    _scaleDenominator, _rotationDegrees, _pitchDegrees,
                                    _sizeInPoints.width, _sizeInPoints.height,
                                    (double)_displayScale];
}

@end

double PPBandLead(double velocity, double seconds, double marginPoints) {
  return pippin::BandLead(velocity, seconds, marginPoints);
}
