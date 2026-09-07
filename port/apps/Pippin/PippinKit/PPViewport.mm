// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPViewport+Internal.h"

#include <algorithm>
#include <cmath>

#include "PPBaseCoverage.h"
#include "PPCameraFit.h"
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

#pragma mark - Derivations

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
  if (![self hasSurface]) return self;
  double w = 0.0, h = 0.0;
  pippin::GrownSurfaceSize(_sizeInPoints.width, _sizeInPoints.height, margin,
                           &w, &h);
  if (w == _sizeInPoints.width && h == _sizeInPoints.height) return self;

  // Everything is copied — the camera, the pitch AND the limits — and only
  // the surface changes. `derivedWithCenter:` already carries the limits over
  // and re-clamps against them, which is a no-op here because the camera it is
  // handed is the one that already satisfied them.
  PPViewport *v = [self derivedWithCenter:_center
                         scaleDenominator:_scaleDenominator
                          rotationDegrees:_rotationDegrees];
  v->_sizeInPoints = CGSizeMake(w, h);
  v->_pixelWidth = (int)std::lround(w * (double)_displayScale);
  v->_pixelHeight = (int)std::lround(h * (double)_displayScale);
  [v reconfigure];
  return v;
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
  return pippin::BaseCovers(_proj, other->_proj, limits) ? YES : NO;
}

- (BOOL)isEquivalentToViewport:(nullable PPViewport *)other {
  if (other == nil) return NO;
  if (other == self) return YES;
  return other->_pixelWidth == _pixelWidth && other->_pixelHeight == _pixelHeight &&
         other->_center.latitude == _center.latitude &&
         other->_center.longitude == _center.longitude &&
         other->_scaleDenominator == _scaleDenominator &&
         other->_rotationDegrees == _rotationDegrees &&
         other->_mmPerPixel == _mmPerPixel;
}

- (NSString *)description {
  return [NSString stringWithFormat:@"<PPViewport %.5f,%.5f 1:%.0f %.0f° %.0fx%.0f@%.0fx>",
                                    _center.latitude, _center.longitude,
                                    _scaleDenominator, _rotationDegrees,
                                    _sizeInPoints.width, _sizeInPoints.height,
                                    (double)_displayScale];
}

@end
