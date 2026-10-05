// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPMap.h"

#import "PPFix+Internal.h"
#import "PPPixelBridge.h"
#import "PPPoint+Internal.h"
#import "PPRoute+Internal.h"
#import "PPSearch+Internal.h"
#import "PPTide+Internal.h"
#import "PPWind+Internal.h"
#import "PPGuidance+Internal.h"
#import "PPTrip+Internal.h"
#import "PPViewport+Internal.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fv_osm_style.h"
#include "fv_osm_vector_source.h"
#include "fvkit/app/pick.h"
#include "fvkit/app/search.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/nav/gpx.h"
#include "fvkit/nav/gpx_recorder.h"
#include "fvkit/nav/scripted_source.h"
#include "fvkit/nav/tide.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/overlay/vector_map_overlay.h"

// The routing graph as a search provider. It lives in RouteKit beside the
// planner whose graph it shares, not in fvkit: fvkit does not link
// port/Routing, which is the invariant this app's CMakeLists states.
#include "fv_road_graph_overlay.h"

// Header-only, pure C++ helpers, so the mac test bed can reach them: how long
// the follow slew should take, and whether the cached base map still covers
// the live camera.
#include "PPBaseCoverage.h"
#include "PPFollowCadence.h"

// The point document's life in the app. `PPRouteStore.h` arrives through
// `PPRoute+Internal.h`, since a route snapshot has one in its signature; a
// point value carries no store type, so this one is included by name.
#include "PPPointStore.h"
#include "PPSprite.h"

// `fv::SlewSettings` and `fv::ShortestRotationDelta`. These arrive through the
// overlay too; named here because this file uses both directly.
#include "fvkit/nav/camera_slew.h"
#include "fvkit/nav/trip.h"
#include "fvkit/proj.h"
#include "fvkit/settings.h"
#include "fvkit/symbol/builtin.h"
#include "fvkit/vector/renderer.h"

NSErrorDomain const PPErrorDomain = @"org.peregrine.Pippin.PippinKit";

namespace {

// The style engine's reference latitude is re-set only when the map has
// moved this far, exactly as PythonView does it and for the same reason: it
// bumps the style epoch, and an epoch bump on every pan means the retained
// scene is never reused. Half a degree is ~0.01 of a zoom level here.
constexpr double kRefLatStep = 0.5;

// An authored pixel is one iOS point, for symbols and for the map alike.
//
// A pixel in authored artwork — a symbol's `SetSizePx`, a GL style's
// `line-width`, a sprite's extent — is N of something physical, and on a 3x
// phone the surface pixel is not it. Taking the surface pixel gives a symbol
// a third of its size (24 surface pixels is 8 points), and handing the style
// engine `25.4 / mmPerPixel` as a device DPI gives 489 here, which
// `OsmStyleEngine` divides by 96 for a 5.09x over-weight, and derives the
// zoom over the surface pixel besides, styling the map 1.6 levels further in
// than the view it shows. Together those drew a minor road at 1:25,000 as
// 1.87 mm against the desktop shell's 0.75 mm.
//
// So the reference throughout is one iOS point — `viewport.mmPerPoint`,
// Apple's baseline pitch times the backing scale. That is the unit every
// other piece of furniture on this screen is authored in, and the unit
// MapLibre renders a GL style in on a phone.
//
// The ledger suggests 0.32 mm (a ~79-dpi reference pixel) for the same gap,
// which is right for chart symbology pinned against paper. Pippin's ownship
// is the app's furniture rather than chart symbology, so it takes the app's
// unit. A chart product would want the two references told apart at their
// sites, not here.

// What a GL style's `px` is authored against, restated from
// `OsmStyleEngine::kStyleNominalDpi` because this is the shell that undoes
// it. A constant of the style format (a CSS pixel is 1/96 inch) rather than a
// property of any screen, which is why feeding a screen's true dpi in is
// wrong.
constexpr double kStyleNominalDpi = 96.0;

// The ownship's size in authored pixels, which `pixelsPerPoint` turns into
// points. MM4's default is 24, which is the drawn width for `fv.ownship`,
// whose ring fits its shape box exactly.
//
// `fv.north` does not fit the box: builtin.cpp draws it at twice the shape box
// (±2·kR along its axis, ±0.55·kR across), so `SetSizePx` describes the box
// and not the ink, and a chevron asking for 24 draws 48 points long. Hence
// 14, a 28-point chevron about 8 points across. The pack can override it.
constexpr double kOwnshipSizePx = 14.0;

// How far the map may be from where the camera last put it before it counts
// as moved by something else (see `tickMovingMap`). A degree of latitude is
// 111 km, so this is a tenth of a millimetre: the shell applies the reported
// centre as a double and hands the same double back through the projection,
// so anything above the noise floor is a real move — a finger, or a viewport
// clamped at the pack's edge.
constexpr double kCenterEpsilonDeg = 1e-9;

// The same question for the rotation: how far the chart may be from where the
// slew last put it before it counts as turned by something else — a
// two-finger twist, or a shell that dropped an update mid-gesture. A
// millionth of a degree is far below anything a finger produces and far above
// the round trip through `SetRotation`'s normalization.
//
// Compared the short way (`fv::ShortestRotationDelta`), not by subtraction:
// 359.999 and 0.001 are two thousandths apart and a plain difference calls
// them 360, which would re-base the slew every frame and cancel the very
// animation carrying the chart across north.
constexpr double kRotationEpsilonDeg = 1e-6;

NSError* MakeError(PPErrorCode code, NSString* message) {
  return [NSError errorWithDomain:PPErrorDomain
                             code:code
                         userInfo:@{NSLocalizedDescriptionKey : message}];
}

NSError* ErrorFromStatus(PPErrorCode code, const fv::Status& s,
                         NSString* what) {
  // The Status message verbatim: the C++ already says which file and why.
  NSString* detail = [NSString stringWithUTF8String:s.message.c_str()];
  return MakeError(code, [NSString stringWithFormat:@"%@: %@", what, detail]);
}

// "west,south,east,north" — the order MBTiles' own metadata.bounds uses, and
// what `pippin.home_bounds` is written in.
bool ParseBounds(const std::string& text, PPGeoBounds* out) {
  double v[4] = {0, 0, 0, 0};
  int n = 0;
  size_t pos = 0;
  while (n < 4 && pos <= text.size()) {
    const size_t comma = text.find(',', pos);
    const std::string field =
        text.substr(pos, comma == std::string::npos ? std::string::npos
                                                    : comma - pos);
    try {
      v[n++] = std::stod(field);
    } catch (...) {
      return false;
    }
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  if (n != 4) return false;
  out->southWest = PPGeoPointMake(v[1], v[0]);
  out->northEast = PPGeoPointMake(v[3], v[2]);
  return true;
}

// `display.symbol_zoom_steps`, a comma-separated list of multipliers smallest
// first. Anything unreadable or not positive is dropped rather than failing
// the pack: a mistyped size setting must not be the reason a map will not
// open. An empty result becomes the single step 1.0, which is the map as
// authored — i.e. the menu still works and offers exactly one size.
NSArray<NSNumber*>* ParseZoomSteps(const std::string& text) {
  NSMutableArray<NSNumber*>* steps = [NSMutableArray array];
  size_t pos = 0;
  while (pos <= text.size()) {
    const size_t comma = text.find(',', pos);
    const std::string field =
        text.substr(pos, comma == std::string::npos ? std::string::npos
                                                    : comma - pos);
    try {
      const double v = std::stod(field);
      if (v > 0.0) [steps addObject:@(v)];
    } catch (...) {
      // one bad field, not a bad file
    }
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  if (steps.count == 0) [steps addObject:@(1.0)];
  return [steps copy];
}

}  // namespace

@implementation PPOwnship {
  CGImageRef _symbolImage;
}

/// `symbolImage` is retained, and may be null.
- (instancetype)initWithFix:(const fv::PositionFix&)fix
            screenAngleDeg:(double)angle
              viewRotation:(double)viewRotation
               symbolImage:(CGImageRef)symbolImage
            pixelsPerPoint:(double)pixelsPerPoint
                   heading:(const fv::ResolvedHeading&)heading
                   snapped:(BOOL)snapped
                  roadName:(const std::string&)roadName {
  self = [super init];
  if (self == nil) return nil;
  _coordinate = PPGeoPointMake(fix.lat, fix.lon);
  _screenAngleDegrees = angle;
  _viewRotationDegrees = viewRotation;
  _symbolImage = symbolImage != nullptr ? CGImageRetain(symbolImage) : nullptr;
  _symbolPixelsPerPoint = pixelsPerPoint > 0.0 ? pixelsPerPoint : 1.0;
  _hasHeading = heading.known ? YES : NO;
  _headingIsReported = heading.reported ? YES : NO;
  _speedMetersPerSecond = fix.speed_mps;
  _hasSpeed = fix.has_speed ? YES : NO;
  _isSnappedToRoad = snapped;
  _roadName = [NSString stringWithUTF8String:roadName.c_str()] ?: @"";
  _timestamp = fix.time_s;
  _hasTimestamp = fix.has_time ? YES : NO;
  return self;
}

- (void)dealloc {
  if (_symbolImage != nullptr) CGImageRelease(_symbolImage);
}

- (nullable CGImageRef)symbolImage { return _symbolImage; }

@end

@implementation PPBillboard {
  CGImageRef _image;
}

/// Takes ownership of `image`.
- (instancetype)initWithCoordinate:(PPGeoPoint)coordinate
                             image:(CGImageRef)image
                    pixelsPerPoint:(double)pixelsPerPoint
                      centerOffset:(CGPoint)centerOffset {
  self = [super init];
  if (self == nil) return nil;
  _coordinate = coordinate;
  _image = image;
  _pixelsPerPoint = pixelsPerPoint > 0.0 ? pixelsPerPoint : 1.0;
  _centerOffset = centerOffset;
  return self;
}

- (void)dealloc {
  if (_image != nullptr) CGImageRelease(_image);
}

- (CGImageRef)image { return _image; }

@end

@implementation PPFrame {
  CGImageRef _overlayImage;
  CGImageRef _baseImage;
}

- (instancetype)initWithViewport:(PPViewport*)viewport
                    overlayImage:(CGImageRef)overlayImage
                 overlayViewport:(PPViewport*)overlayViewport
                          billboards:(NSArray<PPBillboard*>*)billboards
                           baseImage:(CGImageRef)baseImage
                        baseViewport:(PPViewport*)baseViewport
                        baseWasDrawn:(BOOL)baseWasDrawn
                     featuresQueried:(NSUInteger)features
                        drawsEmitted:(NSUInteger)draws
                           queryZoom:(NSInteger)zoom
                  renderMilliseconds:(double)ms
                    baseMilliseconds:(double)baseMs
                             ownship:(PPOwnship*)ownship
                                trip:(PPTrip*)trip
                            guidance:(PPGuidance*)guidance
                      guidanceEvents:(NSArray<PPGuidanceEvent*>*)guidanceEvents
                                slew:(const fv::SlewState&)slew {
  self = [super init];
  if (self == nil) return nil;
  // The frame takes both references, each a retain on pixels the map is
  // still holding.
  _overlayImage = overlayImage;
  _baseImage = baseImage;
  _viewport = viewport;
  _overlayViewport = overlayViewport;
  _billboards = billboards;
  _baseViewport = baseViewport;
  _baseWasDrawn = baseWasDrawn;
  _featuresQueried = features;
  _drawsEmitted = draws;
  _queryZoom = zoom;
  _renderMilliseconds = ms;
  _baseMilliseconds = baseMs;
  _ownship = ownship;
  _trip = trip;
  _guidance = guidance;
  _guidanceEvents = guidanceEvents;
  _hasCameraUpdate = slew.changed ? YES : NO;
  _cameraCenter = PPGeoPointMake(slew.center.lat, slew.center.lon);
  _cameraRotationDegrees = slew.rotation_deg;
  _cameraIsAnimating = slew.active ? YES : NO;
  return self;
}

- (void)dealloc {
  if (_overlayImage != nullptr) CGImageRelease(_overlayImage);
  if (_baseImage != nullptr) CGImageRelease(_baseImage);
}

- (CGImageRef)overlayImage { return _overlayImage; }
- (CGImageRef)baseImage { return _baseImage; }

@end

@interface PPUnderlay ()
- (instancetype)initWithImage:(CGImageRef)image
                     viewport:(PPViewport*)viewport
              backgroundColor:(CGColorRef)backgroundColor
           renderMilliseconds:(double)ms;
@end

@implementation PPUnderlay {
  CGImageRef _image;
  CGColorRef _backgroundColor;
}

/// Takes ownership of both references.
- (instancetype)initWithImage:(CGImageRef)image
                     viewport:(PPViewport*)viewport
              backgroundColor:(CGColorRef)backgroundColor
           renderMilliseconds:(double)ms {
  self = [super init];
  if (self == nil) return nil;
  _image = image;
  _viewport = viewport;
  _backgroundColor = backgroundColor;
  _renderMilliseconds = ms;
  return self;
}

- (void)dealloc {
  if (_image != nullptr) CGImageRelease(_image);
  if (_backgroundColor != nullptr) CGColorRelease(_backgroundColor);
}

- (CGImageRef)image { return _image; }
- (CGColorRef)backgroundColor { return _backgroundColor; }

@end

@implementation PPCameraStep

- (instancetype)initWithOwnship:(PPOwnship*)ownship
                           trip:(PPTrip*)trip
                       guidance:(PPGuidance*)guidance
                 guidanceEvents:(NSArray<PPGuidanceEvent*>*)guidanceEvents
                           slew:(const fv::SlewState&)slew {
  self = [super init];
  if (self == nil) return nil;
  _ownship = ownship;
  _trip = trip;
  _guidance = guidance;
  _guidanceEvents = guidanceEvents;
  _hasCameraUpdate = slew.changed ? YES : NO;
  _cameraCenter = PPGeoPointMake(slew.center.lat, slew.center.lon);
  _cameraRotationDegrees = slew.rotation_deg;
  _cameraIsAnimating = slew.active ? YES : NO;
  return self;
}

@end

@implementation PPMap {
  std::string _packRoot;
  fv::Settings _settings;

  // The two layers. The base map is drawn into `_baseCanvas`, the guard band,
  // which is larger than the screen and kept between frames. The route and
  // points are drawn into `_overlayCanvas` at the same band camera, on
  // transparent, and kept with it; the ownship is a sprite the shell places.
  std::unique_ptr<fv::CpuCanvas> _baseCanvas;
  std::unique_ptr<fv::CpuCanvas> _overlayCanvas;
  CGImageRef _overlayImage;
  PPViewport* _overlayViewport;  // the band `_overlayImage` was drawn at
  // Bumped by every change to what the route or point overlays draw; the
  // cached overlay is redrawn when its stamp falls behind.
  uint64_t _contentEpoch;
  uint64_t _overlayEpoch;
  double _overlaySymbolScale;
  // Whether `_overlayImage` was drawn without markers, for a tilted view.
  BOOL _overlayUpright;
  // The markers as upright sprites, rebuilt when `_contentEpoch` moves.
  NSArray<PPBillboard*>* _billboards;
  uint64_t _billboardEpoch;
  // The ownship sprite, rendered once per symbol scale, and the 1x1 canvas
  // its placement draw goes to (the overlay stamps nothing in sprite mode).
  std::unique_ptr<fv::CpuCanvas> _shipScratch;
  CGImageRef _shipImage;
  double _shipImageScale;
  double _tickRotation;  // the projection rotation of the last moving-map tick
  std::shared_ptr<fv::OsmVectorSource> _source;
  std::shared_ptr<fv::OsmStyleEngine> _style;
  std::unique_ptr<fv::VectorRenderer> _renderer;
  // The underlay's own renderer over the same source and style. A scene
  // retained at the underlay's zoom would evict the base map's.
  std::unique_ptr<fv::VectorRenderer> _underlayRenderer;

  // The route store owns the planner and the overlay, because the overlay
  // borrows a raw pointer to the planner and neither may outlive the other.
  //
  // Declared before `_overlays` deliberately: C++ ivars are destroyed in
  // reverse declaration order and the manager holds its own strong reference
  // to the overlay, so a store declared after the manager would free the
  // planner first and leave the still-living overlay pointing at it for the
  // length of the manager's destructor.
  std::unique_ptr<pippin::RouteStore> _routeStore;

  // Here for the same destruction-order reason as the route store above: a
  // store destroyed first would leave the manager's destructor looking at a
  // freed document.
  std::unique_ptr<pippin::PointStore> _pointStore;

  std::unique_ptr<fv::OverlayManager> _overlays;

  // The two search-only overlays, built on the first search and never drawn
  // (see `ensureSearchProviders`). Borrowed pointers into the manager, held
  // here only so a result can be traced back to who answered it.
  //
  // Declared after `_overlays`, the opposite of the two stores above and by
  // the same reasoning: these hold nothing the manager's destructor could
  // reach into. `_searchChart` shares the source with the renderer and
  // `_searchRoads` the planner's graph, so both point at data outliving
  // either.
  std::shared_ptr<fv::VectorMapOverlay> _searchChart;
  std::shared_ptr<fv::RoadGraphOverlay> _searchRoads;
  // A pack with no `.fvroad` reports once and is then remembered: a missing
  // graph is a configuration, not an error to repeat on every keystroke.
  bool _searchRoadsFailed;

  // Owned by the manager; this is the borrowed pointer the fixes and the tick
  // go through. The stack owns overlays and the shell keeps a handle to the
  // one it feeds, as PythonView does.
  std::shared_ptr<fv::MovingMapOverlay> _movingMap;
  std::shared_ptr<fv::ScriptedSource> _demoFeed;
  double _lastTickTime;  // CFAbsoluteTime of the previous render, 0 = none

  // The trip computer lives beside the moving map because this is the only
  // place both feeds are visible: a live receiver's fixes arrive through
  // `pushFix:` and a scripted replay's are polled inside the tick, and
  // `MovingMapTick::new_fix` is where the two become one stream. On the main
  // thread it would see the receiver and miss the replay, which is the feed
  // every acceptance run uses.
  fv::TripComputer _trip;
  // The stamp of the last fix fed to it. A frame can be drawn without a new
  // fix arriving, and re-feeding one would be harmless for the odometer (the
  // anchor step is zero) but wrong in principle — so it is refused by name
  // rather than by luck.
  double _lastTripFixTime;
  BOOL _hasLastTripFixTime;

  // The turn-by-turn machine, fed from `consumeRawFix:` beside the trip
  // computer and given its route in the same two places. It holds the
  // maneuvers as well as the line, so setting one without the other is not
  // possible from here.
  fv::nav::Guidance _guidance;
  // The events since the last frame. A fix is consumed whenever one arrives
  // and a frame is drawn when the display link runs, so the two do not
  // correspond one to one: a frame that swallowed two fixes carries both
  // fixes' events, and an alert is never dropped for being early.
  std::vector<fv::nav::GuidanceEvent> _pendingGuidanceEvents;

  // Beside the trip computer and fed from the same place, `consumeRawFix:`,
  // so a ride recorded from the demo replay and one from a real receiver take
  // the same path. Recording the demo is deliberate: it is the only way to
  // exercise the record-and-share path on a simulator with no receiver.
  fv::GpxRecorder _recorder;
  NSURL* _recordingURL;

  // `_reportedCenter` is the last centre this class handed the shell on a
  // frame. Comparing the next projection against it is how a move by anything
  // else is noticed; see `tickMovingMap`.
  BOOL _gpsModeEnabled;
  BOOL _wantNorthUp;  // exit asked for the chart to unwind; the next tick aims it
  fv::GeoPoint _reportedCenter;
  BOOL _reportedCenterValid;
  // The rotation half of `_reportedCenter`. A two-finger twist moves the chart
  // behind the slew's back as a drag moves its centre, and re-basing on one
  // and not the other would animate the next unwind from an angle the map
  // left minutes ago.
  double _reportedRotation;
  // Set by `resumeFromBackground`; the next tick with a fix finishes the slew.
  BOOL _jumpCameraOnNextFix;

  // `_courseUp` is remembered whether or not the map is following, so the next
  // press of GPS comes up in the mode the rider left it in. The two slew
  // settings come from the pack and `applyCameraModes` chooses between them.
  BOOL _courseUp;
  // North-up follow pins the chart at north, and the pin is DECIDED by a
  // button press and APPLIED by the next tick, because applying it needs a
  // projection to measure the chart with (`applyRotationPin:`).
  BOOL _rotationPinWanted;
  BOOL _rotationPinApplied;
  fv::SlewSettings _idleSlew;
  fv::SlewSettings _followSlew;
  // How long the follow slew should take, measured rather than assumed. See
  // `PPFollowCadence.h`.
  pippin::FixCadence _cadence;

  PPGeoBounds _homeBounds;
  PPTide* _tide;
  PPWindSettings* _wind;
  NSTimeInterval _routeDepartureOverride;
  std::string _fontPath;
  double _mmPerPixelOverride;  // `display.mm_per_pixel`, 0 when unset
  int _baseCanvasWidth;
  int _baseCanvasHeight;
  int _overlayCanvasWidth;
  int _overlayCanvasHeight;
  double _refLat;  // the latitude the style engine is currently set to
  BOOL _refLatSet;

  // The cache: the last base map drawn, the camera it was drawn for, and the
  // image handed to the display. `_baseViewport` is the band's viewport — the
  // same camera on a larger canvas — so its projection is what
  // `pippin::BaseCovers` measures the live one against.
  //
  // The image is retained here as well as on every frame carrying it, which is
  // why a hit costs nothing: a served frame hands out another reference to
  // pixels nobody re-rasterized.
  CGImageRef _baseImage;
  PPViewport* _baseViewport;
  pippin::BaseCoverageLimits _baseLimits;
}

- (nullable instancetype)initWithDataPackURL:(NSURL*)packURL
                                       error:(NSError**)error {
  self = [super init];
  if (self == nil) return nil;

  _packRoot = packURL.fileSystemRepresentation ?: "";
  if (_packRoot.empty()) {
    if (error) *error = MakeError(PPErrorPackNotFound, @"no data pack URL");
    return nil;
  }
  if (!_packRoot.empty() && _packRoot.back() == '/') _packRoot.pop_back();

  const std::string ini = _packRoot + "/pippin.ini";
  const fv::Status ss = _settings.Load(ini);
  if (!ss.ok()) {
    if (error)
      *error = ErrorFromStatus(PPErrorPackNotFound, ss, @"pippin.ini");
    return nil;
  }

  auto packPath = [&](const char* key, const char* fallback) {
    return _packRoot + "/" + _settings.GetString(key, fallback);
  };

  _source = std::make_shared<fv::OsmVectorSource>();
  const fv::Status os =
      _source->Open(packPath("pippin.mbtiles", "kiawah.mbtiles"));
  if (!os.ok()) {
    if (error) *error = ErrorFromStatus(PPErrorPackIncomplete, os, @"mbtiles");
    return nil;
  }

  _style = std::make_shared<fv::OsmStyleEngine>();
  std::string styleError;
  const fv::Status ys =
      _style->LoadFile(packPath("osm.style", "peregrine-osm.json"), &styleError);
  if (!ys.ok()) {
    if (error) *error = ErrorFromStatus(PPErrorPackIncomplete, ys, @"style");
    return nil;
  }

  _renderer = std::make_unique<fv::VectorRenderer>(_source, _style);
  // R3a's retained scene, sized so a drag-pan re-projects rather than
  // re-queries. A phone pans further per gesture than a mouse does, so this
  // is the settings key's default rather than nothing.
  _renderer->SetSceneMargin(_settings.GetDouble("vector.scene_margin", 0.25));
  _renderer->SetSimplifyPixels(_settings.GetDouble("vector.simplify_pixels", 0.0));
  // No scene margin: an underlay is redrawn at a new camera, never re-panned.
  _underlayRenderer = std::make_unique<fv::VectorRenderer>(_source, _style);
  _underlayRenderer->SetSceneMargin(0.0);
  _underlayRenderer->SetSimplifyPixels(
      _settings.GetDouble("vector.simplify_pixels", 0.0));
  _overlays = std::make_unique<fv::OverlayManager>();

  // The label font. The pack carries its own because iOS gives an app no
  // readable path to a system face; a pack without one still draws a map,
  // with no names on it, and says so through `hasLabelFont` rather than
  // failing to open.
  _baseCanvas = std::make_unique<fv::CpuCanvas>(1, 1);
  _baseCanvasWidth = 1;
  _baseCanvasHeight = 1;
  _overlayCanvas = std::make_unique<fv::CpuCanvas>(1, 1);
  _overlayCanvasWidth = 1;
  _overlayCanvasHeight = 1;
  _shipScratch = std::make_unique<fv::CpuCanvas>(1, 1);
  _baseImage = nullptr;
  _baseViewport = nil;
  // How far the chart may turn under the cached base before it is drawn
  // again. It is a QUALITY number, not a coverage one (`PPBaseCoverage.h`),
  // and it is in the pack so a rider who dislikes a softened map during a
  // turn can take it to zero without a build.
  _baseLimits.max_rotation_delta_deg =
      _settings.GetDouble("display.base_cache_max_turn_deg", 2.5);
  _fontPath = packPath("pippin.font", "");
  _hasLabelFont = NO;
  if (_fontPath.size() > _packRoot.size() + 1) {
    _hasLabelFont = _baseCanvas->SetDefaultFont(_fontPath).ok() ? YES : NO;
    if (_hasLabelFont) _overlayCanvas->SetDefaultFont(_fontPath);
  }
  _style->SetDrawLabels(_hasLabelFont ? true : false);

  _styleName = [NSString stringWithUTF8String:_style->style_name().c_str()];

  _homeBounds.southWest = PPGeoPointMake(32.55, -80.17);
  _homeBounds.northEast = PPGeoPointMake(32.67, -79.97);
  ParseBounds(_settings.GetString("pippin.home_bounds", ""), &_homeBounds);

  _mmPerPixelOverride = _settings.GetDouble("display.mm_per_pixel", 0.0);
  _refLatSet = NO;
  // The map draws itself as authored until the menu says otherwise, and the
  // sizes that menu offers are the pack's — a rider who wants the middle step
  // bigger changes a line rather than a build.
  _symbolZoom = 1.0;
  _symbolZoomSteps =
      ParseZoomSteps(_settings.GetString("display.symbol_zoom_steps",
                                         "1.0, 1.35, 1.75"));

  // Order is the draw order: this manager has no type registry, so every
  // overlay shares a display order and `Add` stacks equal orders
  // newest-on-top. The route goes in first so the ownship draws over it.
  [self buildRoute];
  [self buildPoints];
  [self buildMovingMap];
  _routeDepartureOverride = NAN;
  [self loadTides];
  [self loadWindSettings];
  return self;
}

// The tide table is optional: a pack without `[tides] file` has no card. A
// table that is named but will not load is logged and treated the same way,
// because a missing tide card is no reason to refuse the map.
// The route's beach gate gets the same table, or none: without one the beach
// stays routable and the sheet warns that the tide is unknown.
- (void)loadTides {
  [self overlayContentChanged];
  _tide = nil;
  fv::nav::BeachTideLimits limits;
  limits.rideable_below_m = _settings.GetDouble("beach.rideable_below_m", limits.rideable_below_m);
  limits.good_below_m = _settings.GetDouble("beach.good_below_m", limits.good_below_m);
  limits.exit_margin_s = _settings.GetDouble("beach.exit_margin_s", limits.exit_margin_s);
  // Walking has no passable limit unless the pack sets one.
  fv::nav::BeachTideLimits foot = limits;
  foot.rideable_below_m = _settings.GetDouble("beach.walk_passable_below_m",
                                              std::numeric_limits<double>::infinity());
  foot.good_below_m = _settings.GetDouble("beach.walk_good_below_m", foot.rideable_below_m);
  _routeStore->SetTide(nullptr, limits, foot);

  const std::string name = _settings.GetString("tides.file", "");
  if (name.empty()) return;
  auto table = std::make_shared<fv::nav::TideTable>();
  const fv::Status s = table->Load(_packRoot + "/" + name);
  if (!s.ok() || table->empty()) {
    NSLog(@"PippinKit: tide table %s: %s", name.c_str(), s.message.c_str());
    return;
  }
  _routeStore->SetTide(table, limits, foot);
  _tide = [[PPTide alloc] initWithTable:std::move(table)
                    rideableBelowMeters:limits.rideable_below_m
                    walkEasyBelowMeters:foot.good_below_m];
}

- (nullable PPTide*)tide {
  return _tide;
}

// The forecast point is a spot on the pack's beach, so the request never
// carries the rider's position. No point, no wind: the card and the sheet
// simply leave the wind out.
- (void)loadWindSettings {
  _wind = nil;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double lat = _settings.GetDouble("wind.lat", nan);
  const double lon = _settings.GetDouble("wind.lon", nan);
  if (std::isnan(lat) || std::isnan(lon)) return;
  _wind = [[PPWindSettings alloc]
               initWithLatitude:lat
                      longitude:lon
              beachFacesDegrees:_settings.GetDouble("beach.faces_deg", nan)
     onshoreWarnMetersPerSecond:_settings.GetDouble("wind.onshore_warn_mps", 7.0)
    headwindWarnMetersPerSecond:_settings.GetDouble("wind.headwind_warn_mps", 5.0)];
}

- (nullable PPWindSettings*)wind {
  return _wind;
}

// The points join the stack at construction and stay there, like the route and
// the moving map: an empty or hidden set draws nothing, so always being in the
// stack costs a virtual call per frame and saves a mode that adds and removes
// an overlay while a user is looking at it.
//
// Added between the route and the ownship. A marker under the route line
// cannot be tapped where the route crosses it, and the chevron must be over
// both. `Add` stacks equal display orders newest-on-top, so the order of these
// three calls is the draw order.
- (void)buildPoints {
  auto packPath = [&](const char* key, const char* fallback) {
    return _packRoot + "/" + _settings.GetString(key, fallback);
  };
  // The pack's own set, copied into `Documents/` on the first launch and never
  // read again. It need not exist: a pack shipping no points starts the app
  // with an empty map. The document path arrives later, from Swift, because
  // `Documents/` is a Foundation question.
  _pointStore = std::make_unique<pippin::PointStore>(
      packPath("points.seed", "kiawah.fvpoints"), std::string());

  // Startup state from the pack, as the moving map reads its camera modes.
  // Neither of these belongs in the document.
  _pointStore->SetVisible(_settings.GetBool("points.visible", true));
  _pointStore->SetShowLabels(_settings.GetBool("points.show_labels", true));

  const fv::Status s = _overlays->Add(_pointStore->overlay());
  (void)s;  // a fresh manager with no type registry cannot refuse an Add
}

// Like the moving map, the route overlay joins the stack at construction and
// stays there: with no waypoints it draws nothing, so always being in the
// stack costs a virtual call per frame and saves a mode that adds and removes
// an overlay while a user is looking at it.
- (void)buildRoute {
  auto packPath = [&](const char* key, const char* fallback) {
    return _packRoot + "/" + _settings.GetString(key, fallback);
  };
  // Both paths come from the pack and neither has to exist: a missing graph is
  // reported by the first route that needs one, and a missing rule file leaves
  // the builtin weights routing. The document path arrives later, from Swift.
  _routeStore = std::make_unique<pippin::RouteStore>(
      packPath("routing.graph", "kiawah.fvroad"),
      packPath("routing.rules", "route-weights.json"), std::string());

  _routeDefaultProfile = [NSString
      stringWithUTF8String:_settings.GetString("routing.profile", "bicycle").c_str()];

  const fv::Status s = _overlays->Add(_routeStore->overlay());
  (void)s;  // a fresh manager with no type registry cannot refuse an Add

  // The back-pointer, which `Add` does not set. `fv::RouteOverlay` keeps its
  // own manager pointer — an overlay does not otherwise know its stack — and
  // that is what `RouteEditSession::ResolvePixel` asks
  // `fv::app::SnapCandidates` through, and what takes the mouse capture. Null,
  // the drag still works and simply never snaps, which shows up as a
  // coordinate eleven metres from the marker the waypoint was dropped on.
  _routeStore->overlay()->SetManager(_overlays.get());
}

// The moving map joins the stack at construction and stays there. It is a
// static overlay type (MM4: at most one, toggled rather than opened), so there
// is no document to wait for and nothing to add later, and with no fix it
// draws nothing.
- (void)buildMovingMap {
  _movingMap = std::make_shared<fv::MovingMapOverlay>("Ownship");

  // The ship is shown, not followed: a map that lunges after the first fix has
  // thrown away wherever the rider panned to, and following is what the GPS
  // button asks for. The pack holds the three keys because MM4 reads them as
  // startup state.
  fv::CameraModes modes;
  modes.auto_center = _settings.GetBool("movingmap.auto_center", false);
  modes.auto_rotate = _settings.GetBool("movingmap.auto_rotate", false);
  modes.continuous = _settings.GetBool("movingmap.continuous", false);
  _movingMap->SetModes(modes);

  // The compass starts on, because pressing GPS is specified to centre and
  // auto-rotate, and the compass is how a rider who wants north-up says so.
  // Startup state rather than a document property, so it lives in the pack
  // beside the three modes above.
  _courseUp = _settings.GetBool("movingmap.course_up", true) ? YES : NO;

  // Two slews, which is what gives course-up no lag while north-up keeps the
  // lag it is meant to have.
  //
  // The idle slew is fvkit's default and serves north-up follow, the
  // exit-to-north unwind and the compass tap: a third of a second, eased in
  // and out, which is what a recentre seconds after the last one should look
  // like.
  _idleSlew = fv::SlewSettings{};
  _idleSlew.duration_s = _settings.GetDouble("movingmap.slew_s",
                                             _idleSlew.duration_s);

  // The follow slew is what continuous course-up wants, and MM3's header names
  // both halves: `kLinear`, because an ease restarted on every fix never
  // leaves its own slow opening, and a duration matched to the fix interval,
  // which is the only length at which the chart is still moving when the next
  // target arrives. The interval is measured (`PPFollowCadence.h`), because a
  // phone reports at 1 Hz and the demo feed at four times its recorded
  // schedule.
  _followSlew = _idleSlew;
  _followSlew.easing = fv::SlewEasing::kLinear;

  pippin::FollowCadenceSettings cadence;
  cadence.initial_s =
      _settings.GetDouble("movingmap.follow_slew_s", cadence.initial_s);
  cadence.min_s =
      _settings.GetDouble("movingmap.follow_slew_min_s", cadence.min_s);
  cadence.max_s =
      _settings.GetDouble("movingmap.follow_slew_max_s", cadence.max_s);
  _cadence.SetSettings(cadence);

  // The pack's three keys above are startup state, so `applyCameraModes`
  // deliberately does not run here: it takes the modes over from the first
  // press of GPS or the compass. The slew has nothing to take over from, so it
  // is set now.
  _movingMap->SetSlewSettings(_idleSlew);

  // True, and honestly so: `PPViewport` carries a rotation and
  // `MapProjection` applies it, so this shell really can turn a chart. The
  // overlay adopts `proj.Rotation()` at the top of every tick, so the
  // projection is the one place the applied rotation is true and this flag
  // only decides whether that adoption happens.
  _movingMap->SetRotationSupported(true);

  // A chevron, not the aircraft. `fv.ownship` is an aircraft in plan view and
  // G2 says a caller whose platform is not one should ask for `fv.north`. The
  // key is in the pack, so the aeroplane is available without a build.
  _movingMap->SetSymbolId(
      _settings.GetString("movingmap.symbol", fv::builtin_symbol::kNorthArrow));
  _movingMap->SetSizePx(_settings.GetDouble("movingmap.size_px", kOwnshipSizePx));

  // The ship is a sprite (`PPOwnship.symbolImage`). The overlay stays in the
  // stack, hidden from `DrawAll`, which draws at the band's camera; its
  // placement draw happens at the live camera in `placeShipAt:`.
  _movingMap->SetDrawSymbol(false);
  _movingMap->SetVisible(false);

  const fv::Status s = _overlays->Add(_movingMap);
  (void)s;  // a fresh manager with no type registry cannot refuse an Add
}

- (PPGeoBounds)homeBounds {
  return _homeBounds;
}

- (double)baseCacheMaxTurnDegrees {
  return _baseLimits.max_rotation_delta_deg;
}

- (double)baseCacheBandMargin {
  return _settings.GetDouble("display.base_cache_band_margin", 0.25);
}

- (double)baseCacheRefreshFraction {
  return _settings.GetDouble("display.band_refresh_fraction", 0.5);
}

- (double)flingMinSpeedPoints {
  return _settings.GetDouble("display.fling_min_speed_pt", 200.0);
}

- (double)flingDecelerationRate {
  return _settings.GetDouble("display.fling_deceleration", 0.998);
}

- (double)followPitchDegrees {
  return _settings.GetDouble("display.follow_pitch_deg", 45.0);
}

- (double)followFramesPerSecond {
  return _settings.GetDouble("display.follow_fps", 20.0);
}

- (double)followMinMovePoints {
  return _settings.GetDouble("display.min_move_pt", 0.25);
}

- (PPViewport*)initialViewport {
  // The pack's zoom range is the pyramid's own, straight off the file: what
  // the data holds is what the camera is allowed to ask for.
  return [[PPViewport alloc] initWithHomeBounds:_homeBounds
                                    minTileZoom:_source->file().min_zoom()
                                    maxTileZoom:_source->file().max_zoom()
                             mmPerPixelOverride:_mmPerPixelOverride];
}

- (void)dealloc {
  // The cached images are CF references held outside ARC's reach.
  if (_baseImage != nullptr) CGImageRelease(_baseImage);
  if (_overlayImage != nullptr) CGImageRelease(_overlayImage);
  if (_shipImage != nullptr) CGImageRelease(_shipImage);
}

/// Marks the route and point overlays' drawing stale.
- (void)overlayContentChanged {
  ++_contentEpoch;
}

/// Drops the cached base map so the next frame draws one. Nothing inside this
/// class needs it: the coverage test refuses a camera the cache cannot serve,
/// and every content change is in the overlay. It exists for a memory warning
/// and for a shell that wants to prove a frame is being drawn.
- (void)invalidateBaseLayer {
  if (_baseImage != nullptr) {
    CGImageRelease(_baseImage);
    _baseImage = nullptr;
  }
  _baseViewport = nil;
  if (_overlayImage != nullptr) {
    CGImageRelease(_overlayImage);
    _overlayImage = nullptr;
  }
  _overlayViewport = nil;
}

/// The number does nothing until the next base map is drawn, since it is read
/// at the top of the draw and nowhere else, so the only work here is making
/// sure there is a next one. The cached band was rasterized at the old size
/// and `pippin::BaseCovers` asks only about the camera, so without this the
/// map keeps its old symbology until the rider pans out of the cache.
- (void)setSymbolZoom:(double)symbolZoom {
  const double z = symbolZoom > 0.0 ? symbolZoom : 1.0;
  if (z == _symbolZoom) return;
  _symbolZoom = z;
  [self invalidateBaseLayer];
}

/// Sets the source's and style's pitch and the style's reference latitude
/// for a draw at `viewport`. The latitude is stepped, so a draw that does not
/// cross a step leaves the style epoch, and the retained scene, alone.
- (void)prepareStyleForViewport:(PPViewport*)viewport {
  const double mmPerPoint = viewport.mmPerPoint;
  _source->SetDisplayMmPerPixel(mmPerPoint);
  _style->SetDisplayMmPerPixel(mmPerPoint);
  const double lat =
      std::round(viewport.center.latitude / kRefLatStep) * kRefLatStep;
  if (!_refLatSet || lat != _refLat) {
    _style->SetReferenceLatitude(lat);
    _refLat = lat;
    _refLatSet = YES;
  }
}

- (nullable PPUnderlay*)renderUnderlayForViewport:(PPViewport*)viewport
                                            error:(NSError**)error {
  if (viewport == nil || !viewport.hasSurface) {
    if (error) *error = MakeError(PPErrorRenderFailed, @"no surface size set");
    return nil;
  }
  const CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
  PPViewport* under = [viewport viewportForUnderlay];
  const double mmPerPixel = under.mmPerPixel;
  const double pixelsPerPoint =
      mmPerPixel > 0 ? under.mmPerPoint / mmPerPixel : 1.0;

  // Symbology at a quarter size, so that once the compositor magnifies the
  // underlay back to the live scale its lines and text are the sharp map's
  // size rather than four times it.
  [self prepareStyleForViewport:under];
  _underlayRenderer->SetDeviceDpi(kStyleNominalDpi * pixelsPerPoint *
                                  _symbolZoom / pippin::kUnderlayZoomOut);

  // The background under the live camera, for the shell to paint behind
  // everything, and the one at the underlay's own scale to clear it with.
  fv::FvColor liveBg{255, 255, 255, 255};
  _style->background(viewport.projection.Scale(), &liveBg);
  fv::FvColor bg{255, 255, 255, 255};
  _style->background(under.projection.Scale(), &bg);
  bg.a = 255;

  // A canvas per build: an underlay is drawn a few times a minute at most,
  // and holding a second screen of pixels between builds would double its
  // memory.
  fv::CpuCanvas canvas(under.pixelWidth, under.pixelHeight);
  if (_hasLabelFont) canvas.SetDefaultFont(_fontPath);
  canvas.Clear(bg);
  const fv::Status rs = _underlayRenderer->Render(under.projection, &canvas);
  if (!rs.ok()) {
    if (error) *error = ErrorFromStatus(PPErrorRenderFailed, rs, @"underlay");
    return nil;
  }
  CGImageRef image = PPCreateCGImageFromPixelBuffer(canvas.Buffer());
  if (image == nullptr) {
    if (error)
      *error = MakeError(PPErrorRenderFailed, @"CGImage creation failed");
    return nil;
  }
  // sRGB, the space the map's own pixels are tagged with.
  CGColorRef color =
      CGColorCreateSRGB(liveBg.r / 255.0, liveBg.g / 255.0, liveBg.b / 255.0, 1.0);
  return [[PPUnderlay alloc]
           initWithImage:image
                viewport:under
         backgroundColor:color
      renderMilliseconds:(CFAbsoluteTimeGetCurrent() - t0) * 1000.0];
}

- (nullable PPFrame*)renderViewport:(PPViewport*)viewport
                              error:(NSError**)error {
  // The unbanded, uncached call: what every caller outside the render loop
  // wants, and what the loop asks for when it settles.
  return [self renderViewport:viewport band:nil reuseBase:NO error:error];
}

- (nullable PPFrame*)renderViewport:(PPViewport*)viewport
                               band:(nullable PPViewport*)bandViewport
                          reuseBase:(BOOL)reuseBase
                              error:(NSError**)error {
  if (viewport == nil || !viewport.hasSurface) {
    if (error) *error = MakeError(PPErrorRenderFailed, @"no surface size set");
    return nil;
  }
  const CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();

  // The viewport is the projection: surface, centre, physical scale and
  // rotation are already applied. Nothing here re-states them, which is what
  // stops the drawn frame and the frame's recorded viewport from disagreeing,
  // since the screen transforms one against the other.
  const fv::MapProjection& proj = viewport.projection;
  const double mmPerPixel = viewport.mmPerPixel;
  // One point, which is what an authored pixel means here (see the constants
  // above). The projection is the one thing that stays on the surface pixel:
  // it places geometry on a real screen, and `SetPhysicalScale` already got
  // the true pitch when the viewport was made.
  const double mmPerPoint = viewport.mmPerPoint;
  const double pixelsPerPoint = mmPerPixel > 0 ? mmPerPoint / mmPerPixel : 1.0;

  // --- The base layer ----------------------------------------------------
  //
  // Drawn only when the cached one cannot serve this camera. The test is
  // `PPBaseCoverage.h`, asked of the cached band whatever margin it was built
  // at: a base kept from a wider request still serves a narrower one, and a
  // settle frame that asked for no band replaces it with an exact one.
  const BOOL haveBase = _baseImage != nullptr && _baseViewport != nil;
  double quadX[4], quadY[4];
  const BOOL tilted = [viewport groundQuadX:quadX y:quadY];
  const BOOL baseServes =
      reuseBase && haveBase &&
      pippin::BaseCovers(_baseViewport.projection, proj, _baseLimits,
                         tilted ? quadX : nullptr, tilted ? quadY : nullptr);

  double baseMs = 0.0;
  if (!baseServes) {
    const CFAbsoluteTime b0 = CFAbsoluteTimeGetCurrent();
    // A tilted screen shows more ground than its own surface, so even the
    // unbanded draw is the tilted footprint.
    PPViewport* band = bandViewport != nil ? bandViewport
                       : tilted          ? [viewport viewportGrownByMargin:0.0]
                                         : viewport;
    const fv::MapProjection& bandProj = band.projection;
    const int bw = band.pixelWidth;
    const int bh = band.pixelHeight;
    if (bw != _baseCanvasWidth || bh != _baseCanvasHeight) {
      _baseCanvas = std::make_unique<fv::CpuCanvas>(bw, bh);
      _baseCanvasWidth = bw;
      _baseCanvasHeight = bh;
      if (_hasLabelFont) _baseCanvas->SetDefaultFont(_fontPath);
    }

    // Both halves of the zoom-to-scale relation must get the same pitch, or
    // the style's minzoom disagrees with the tile level the source read, which
    // is the failure O2 was designed around. The pitch is the point's, because
    // that is what the pyramid is authored for: a tile pixel is a point, the
    // density `PPViewport`'s zoom limits assume and the level MapLibre would
    // read for the same view.
    //
    // All of this is inside the miss, and not only for thrift. Setting the
    // reference latitude bumps the style epoch, which drops R3a's retained
    // scene, so doing it on a frame that is not drawing the base map would
    // throw away the query and the styling behind a picture nobody asked to
    // redraw.
    [self prepareStyleForViewport:band];
    // The style engine wants how many device pixels an authored pixel is,
    // expressed as a dpi it divides by 96, so the answer is the backing scale
    // and not the screen's own 489 dpi. Written as the ratio rather than
    // `96 * displayScale`, so a pack overriding `display.mm_per_pixel` moves
    // the physical size of the result without moving how many pixels a point
    // is made of.
    //
    // Times the user's symbol size, which is the whole of the menu's setting:
    // a GL style's widths are all `device_dpi / 96`, so one multiplication
    // grows lines, text, halos, icons and an area pattern's pitch together and
    // touches neither the tile level nor the projection. See `symbolZoom` in
    // the header.
    _renderer->SetDeviceDpi(kStyleNominalDpi * pixelsPerPoint * _symbolZoom);

    // A GL style states its own background, and `background` is a layer rather
    // than a feature, so it cannot arrive as a StyleResult: the application
    // has to clear with it.
    fv::FvColor bg{255, 255, 255, 255};
    _style->background(proj.Scale(), &bg);
    bg.a = 255;  // the base layer handed to the compositor is opaque
    _baseCanvas->Clear(bg);

    const fv::Status rs = _renderer->Render(bandProj, _baseCanvas.get());
    if (!rs.ok()) {
      if (error) *error = ErrorFromStatus(PPErrorRenderFailed, rs, @"render");
      return nil;
    }

    CGImageRef base = PPCreateCGImageFromPixelBuffer(_baseCanvas->Buffer());
    if (base == nullptr) {
      if (error)
        *error = MakeError(PPErrorRenderFailed, @"CGImage creation failed");
      return nil;
    }
    if (_baseImage != nullptr) CGImageRelease(_baseImage);
    _baseImage = base;
    _baseViewport = band;
    baseMs = (CFAbsoluteTimeGetCurrent() - b0) * 1000.0;
  }

  // --- The overlay layer -------------------------------------------------
  //
  // The route and points, at the band's camera, kept with the base and
  // redrawn when the base is or when their content changes. The ship is not
  // in it: `placeShipAt:` places it at the live camera for the shell's sprite.
  //
  // The moving map's tick comes first, at the top of the drawing rather than
  // on the main thread.
  //
  // MM4 put the camera on the overlay because an overlay is the only object
  // both drawn per frame and fed fixes: `OnDraw` recomputes the apron and
  // `Tick` consumes it, and the wrong order freezes the map. So the overlay
  // lives on the drawing side of Pippin's thread split. The main thread never
  // touches it; what crosses is a `PPFix` in, through MM1's locked queue, and
  // a `PPOwnship` out.
  //
  // The consequence is that the feed advances only when a frame or a step is
  // asked for. The shell's loop pauses when the camera stops moving, so
  // `MapModel`'s ship-dirty flag is what keeps it going for a running feed.
  //
  // Below the base map's `if` rather than inside it: the tick is the feed, the
  // trip computer and the camera, and thinning it along with the pixels would
  // be a moving map that stops whenever its picture is cheap enough to keep.
  const fv::MovingMapTick tick = [self tickMovingMap:proj
                                       pixelsPerPoint:pixelsPerPoint];

  // The tick sets the overlays' symbol scale, so the overlay draw follows it.
  const BOOL upright = viewport.pitchDegrees > 0.0 ? YES : NO;
  if (![self drawOverlayIfStale:upright]) {
    if (error) *error = MakeError(PPErrorRenderFailed, @"CGImage creation failed");
    return nil;
  }
  [self placeShipAt:proj];

  return [[PPFrame alloc]
           initWithViewport:viewport
               overlayImage:CGImageRetain(_overlayImage)
            overlayViewport:_overlayViewport
                 billboards:upright ? [self billboards] : @[]
                  baseImage:CGImageRetain(_baseImage)
               baseViewport:_baseViewport
              baseWasDrawn:(baseServes ? NO : YES)
            featuresQueried:(NSUInteger)_renderer->features_queried()
               drawsEmitted:(NSUInteger)_renderer->draws_emitted()
                  queryZoom:(NSInteger)_source->last_query_zoom()
         renderMilliseconds:(CFAbsoluteTimeGetCurrent() - t0) * 1000.0
           baseMilliseconds:baseMs
                    ownship:[self ownshipOrNil]
                       trip:[self tripOrNil]
                   guidance:[self guidanceOrNil]
             guidanceEvents:[self takeGuidanceEvents]
                       slew:tick.slew];
}

- (nullable PPCameraStep*)stepCameraAtViewport:(PPViewport*)viewport {
  if (viewport == nil || !viewport.hasSurface) return nil;
  const double mmPerPixel = viewport.mmPerPixel;
  const double pixelsPerPoint =
      mmPerPixel > 0 ? viewport.mmPerPoint / mmPerPixel : 1.0;
  // No pixels are drawn, but the ship is placed: the shell shows it as a
  // sprite at this camera, so the apron is rebuilt from here.
  const fv::MovingMapTick tick = [self tickMovingMap:viewport.projection
                                      pixelsPerPoint:pixelsPerPoint];
  [self placeShipAt:viewport.projection];
  return [[PPCameraStep alloc] initWithOwnship:[self ownshipOrNil]
                                          trip:[self tripOrNil]
                                      guidance:[self guidanceOrNil]
                                guidanceEvents:[self takeGuidanceEvents]
                                          slew:tick.slew];
}

/// Redraws the route and point overlays into a band-sized transparent image
/// at `_baseViewport` when the base moved on, their content changed, or the
/// view tilted or flattened. `upright` leaves the markers out, for billboards.
/// NO only when the image could not be made.
- (BOOL)drawOverlayIfStale:(BOOL)upright {
  if (_overlayImage != nullptr && _overlayViewport == _baseViewport &&
      _overlayEpoch == _contentEpoch && _overlayUpright == upright)
    return YES;
  _routeStore->overlay()->SetDrawMarkers(!upright);
  _pointStore->overlay()->SetDrawMarkers(!upright);
  PPViewport* band = _baseViewport;
  const int w = band.pixelWidth;
  const int h = band.pixelHeight;
  if (w != _overlayCanvasWidth || h != _overlayCanvasHeight) {
    _overlayCanvas = std::make_unique<fv::CpuCanvas>(w, h);
    _overlayCanvasWidth = w;
    _overlayCanvasHeight = h;
    if (_hasLabelFont) _overlayCanvas->SetDefaultFont(_fontPath);
  }
  _overlayCanvas->Clear(fv::FvColor{0, 0, 0, 0});
  _overlays->DrawAll(band.projection, *_overlayCanvas);
  CGImageRef image = PPCreateCGImageFromPixelBuffer(_overlayCanvas->Buffer());
  if (image == nullptr) return NO;
  if (_overlayImage != nullptr) CGImageRelease(_overlayImage);
  _overlayImage = image;
  _overlayViewport = band;
  _overlayEpoch = _contentEpoch;
  _overlayUpright = upright;
  return YES;
}

/// The route's waypoints, then the points, as upright sprites at the current
/// symbol scale. Cached until `_contentEpoch` moves; a hidden overlay has none.
- (NSArray<PPBillboard*>*)billboards {
  if (_billboards != nil && _billboardEpoch == _contentEpoch) return _billboards;
  NSMutableArray<PPBillboard*>* out = [NSMutableArray array];
  const auto& route = _routeStore->overlay();
  if (route->IsVisible()) {
    const auto& wps = route->waypoints();
    // A route's names are a fixed 12 px, not scaled with the symbols.
    const double reach = 9.0 * 1.6 / 2.0 * route->symbol_dpi_scale() + 6.0;
    for (size_t i = 0; i < wps.size(); ++i) {
      const std::string& name = route->show_labels() ? wps[i].label : std::string();
      PPBillboard* b = [self billboardAt:wps[i].position
                                   reach:reach
                                   label:name
                                labelPx:12.0
                             labelStartX:10.0
                                   scale:route->symbol_dpi_scale()
                                   stamp:[&](const fv::MapProjection& proj,
                                             fv::CpuCanvas& canvas, double x, double y) {
                                     return route->DrawMarkerAt(proj, canvas, i, x, y);
                                   }];
      if (b != nil) [out addObject:b];
    }
  }
  const auto& points = _pointStore->overlay();
  if (points->IsVisible()) {
    const double scale = points->symbol_dpi_scale();
    const auto& pts = points->points();
    for (size_t i = 0; i < pts.size(); ++i) {
      const double r = pts[i].size_px * scale / 2.0;
      const std::string& name = points->show_labels() ? pts[i].name : std::string();
      PPBillboard* b = [self billboardAt:pts[i].position
                                   reach:r + scale + 6.0
                                   label:name
                                 labelPx:12.0 * scale
                             labelStartX:r + 3.0
                                   scale:scale
                                   stamp:[&](const fv::MapProjection& proj,
                                             fv::CpuCanvas& canvas, double x, double y) {
                                     return points->DrawMarkerAt(proj, canvas, i, x, y);
                                   }];
      if (b != nil) [out addObject:b];
    }
  }
  _billboards = [out copy];
  _billboardEpoch = _contentEpoch;
  return _billboards;
}

/// Stamps one marker at the centre of a canvas sized for its symbol (`reach`
/// pixels each way, halo included) and its name, trims it to its ink, and
/// wraps it. Nil when nothing was inked or no image could be made.
- (nullable PPBillboard*)billboardAt:(const fv::GeoPoint&)position
                               reach:(double)reach
                               label:(const std::string&)label
                             labelPx:(double)labelPx
                         labelStartX:(double)labelStartX
                               scale:(double)scale
                               stamp:(const std::function<fv::Status(
                                          const fv::MapProjection&, fv::CpuCanvas&,
                                          double, double)>&)stamp {
  // A generous text width: the byte count overstates a multibyte name, and
  // the trim takes back whatever is unused.
  double halfW = reach, halfH = reach;
  if (!label.empty()) {
    halfW = std::max(halfW, labelStartX + labelPx * 0.75 * label.size() + 8.0);
    halfH = std::max(halfH, labelStartX + labelPx * 1.5);
  }
  const int cx = (int)std::ceil(halfW);
  const int cy = (int)std::ceil(halfH);
  const int w = 2 * cx + 1;
  const int h = 2 * cy + 1;
  fv::CpuCanvas canvas(w, h);
  canvas.Clear(fv::FvColor{0, 0, 0, 0});
  if (_hasLabelFont) canvas.SetDefaultFont(_fontPath);
  fv::MapProjection proj;
  if (!proj.SetSurfaceSize(w, h).ok()) return nil;
  (void)proj.SetCenter(position);
  (void)proj.SetScale(100000.0);
  // A failed name still leaves the marker stamped, which is worth showing.
  (void)stamp(proj, canvas, cx, cy);
  const pippin::InkRect ink = pippin::InkBounds(canvas.Buffer());
  if (ink.empty()) return nil;
  CGImageRef image = PPCreateCGImageFromPixelBuffer(pippin::Crop(canvas.Buffer(), ink));
  if (image == nullptr) return nil;
  const double ppp = scale > 0.0 ? scale : 1.0;
  const CGPoint offset = CGPointMake((ink.x + ink.width / 2.0 - (cx + 0.5)) / ppp,
                                     (ink.y + ink.height / 2.0 - (cy + 0.5)) / ppp);
  return [[PPBillboard alloc] initWithCoordinate:PPGeoPointMake(position.lat, position.lon)
                                           image:image
                                  pixelsPerPoint:ppp
                                    centerOffset:offset];
}

/// Places the ship and rebuilds the moving map's apron at `proj`, the camera
/// the shell shows the sprite against. Stamps nothing.
- (void)placeShipAt:(const fv::MapProjection&)proj {
  (void)_movingMap->OnDraw(proj, *_shipScratch);
}

/// The ownship symbol at angle 0 (nose up), centred on an odd-sized
/// transparent square, at the moving map's current symbol scale. Cached;
/// null if it could not be drawn.
- (CGImageRef)shipSpriteImage {
  const double scale = _movingMap->symbol_dpi_scale();
  if (_shipImage != nullptr && scale == _shipImageScale) return _shipImage;
  // `ownshipSymbolRadiusInPoints` is the whole size; the edge stamp is two
  // authored pixels larger.
  const double reach = (_movingMap->size_px() + 2.0) * scale;
  const int n = 2 * (int)std::ceil(reach) + 1;
  fv::CpuCanvas canvas(n, n);
  canvas.Clear(fv::FvColor{0, 0, 0, 0});
  fv::MapProjection proj;
  if (!proj.SetSurfaceSize(n, n).ok()) return nullptr;
  (void)proj.SetCenter(fv::GeoPoint{0.0, 0.0});
  (void)proj.SetScale(100000.0);
  const double c = (n - 1) / 2.0;
  if (!_movingMap->DrawSymbolAt(proj, canvas, c, c, 0.0).ok()) return nullptr;
  CGImageRef image = PPCreateCGImageFromPixelBuffer(canvas.Buffer());
  if (image == nullptr) return nullptr;
  if (_shipImage != nullptr) CGImageRelease(_shipImage);
  _shipImage = image;
  _shipImageScale = scale;
  return _shipImage;
}

- (fv::MovingMapTick)tickMovingMap:(const fv::MapProjection&)proj
                   pixelsPerPoint:(double)pixelsPerPoint {
  // A scripted feed has no thread (MM1's rule, so every timing assertion in
  // its tests is an equality): it emits what is due when polled, and this is
  // the poll. A live receiver's fixes arrive through `pushFix:` instead.
  if (_demoFeed && _demoFeed->running()) _demoFeed->Poll();

  // Real seconds since the last render. The slew and the heading resolver both
  // want wall time, and the frame clock varies from 14 to 110 ms on this pack,
  // so it is measured rather than assumed to be 1/60.
  const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  const double dt = _lastTickTime > 0.0 ? now - _lastTickTime : 0.0;
  _lastTickTime = now;

  // `symbol_dpi_scale` exists because an overlay does not know the device;
  // this is the shell telling it. Set per tick rather than at construction,
  // because the pitch belongs to the viewport — a phone can be handed another
  // screen — and this is where the viewport's number arrives.
  //
  // It is the same expression the style engine is given: an authored pixel is
  // a point, whether authored in a symbol table or a style sheet.
  const double symbolScale = pixelsPerPoint;
  _movingMap->SetSymbolDpiScale(symbolScale);

  // The same reference for the route, and not the 0.32 mm chart reference: a
  // route's diamonds are the app's own furniture, sized in the units its
  // buttons are, like the ownship. The hit test scales with this too, so a
  // marker drawn twice the size is twice the target.
  _routeStore->overlay()->SetSymbolDpiScale(symbolScale);

  // And again for the points. A `.fvpoints` `size_px` is an authored pixel
  // like everything else this app draws, so a 22-px marker is 22 points wide
  // on any screen, and the hit test scales with it inside the overlay.
  _pointStore->overlay()->SetSymbolDpiScale(symbolScale);
  if (symbolScale != _overlaySymbolScale) {
    _overlaySymbolScale = symbolScale;
    [self overlayContentChanged];
  }
  _tickRotation = proj.Rotation();

  // Where the map is, asked rather than remembered. The slew interpolates from
  // where it believes the map to be, so anything that moved the map without
  // going through it — a drag, a pinch, a viewport clamped at the pack's edge,
  // a skipped frame — has to re-base it, or the next fix animates from a place
  // the map left some time ago and reads as a jump.
  //
  // The projection handed in is where the map is and `_reportedCenter` is
  // where this class last said it should be, so a disagreement means somebody
  // else moved it. `Tick` does the same for the rotation by adopting
  // `proj.Rotation()`.
  //
  // This does not stop the user panning in GPS mode: the pan is honoured, the
  // slew re-bases, and the next fix pulls the map back, which is MM2's apron
  // doing its job rather than a mode fighting a finger.
  //
  // The rotation is in the same sentence, because two fingers turn the chart
  // behind the slew's back as a drag moves its centre, and re-basing one and
  // not the other would unwind the next compass tap from a stale angle.
  // Compared the short way round, since 359.999 and 0.001 are two thousandths
  // apart and a subtraction calls them 360.
  const fv::GeoPoint here = proj.Center();
  const bool centre_moved =
      std::fabs(here.lat - _reportedCenter.lat) > kCenterEpsilonDeg ||
      std::fabs(here.lon - _reportedCenter.lon) > kCenterEpsilonDeg;
  const bool chart_turned =
      std::fabs(fv::ShortestRotationDelta(_reportedRotation,
                                          proj.Rotation())) >
      kRotationEpsilonDeg;
  if (_reportedCenterValid && (centre_moved || chart_turned)) {
    [self rebaseSlewTo:proj];
  }

  // The compass's mode change, applied now that there is a projection to
  // measure the chart with. After the re-base above, deliberately: the pin has
  // its own opinion about what the slew should believe and must be the last
  // word.
  [self applyRotationPin:proj];

  // Set before `Tick`, because `Tick` is where the retarget happens and the
  // settings are read there. One measured fix interval; any other length
  // either stutters once a second or trails permanently
  // (`PPFollowCadence.h`).
  if (_gpsModeEnabled && _courseUp) {
    _followSlew.duration_s = _cadence.interval_s();
    _movingMap->SetSlewSettings(_followSlew);
  }

  // The exit slew, aimed here rather than in `setGpsModeEnabled:` because the
  // rate caps need a projection to turn pixels per second into degrees per
  // second, and the map is only ever measured at a frame.
  if (_wantNorthUp) {
    _wantNorthUp = NO;
    _movingMap->slew().RetargetTo(proj, here, 0.0);
  }

  fv::MovingMapTick tick = _movingMap->Tick(proj, dt);
  // Back from the background: the map lands on the first fix instead of
  // slewing across however far the rider went. `changed` is kept from the
  // first `Advance`, which may already have arrived and reported the move.
  if (_jumpCameraOnNextFix && tick.new_fix) {
    _jumpCameraOnNextFix = NO;
    const bool changed = tick.slew.changed;
    _movingMap->slew().Finish();
    tick.slew = _movingMap->slew().Advance(0.0);
    tick.slew.changed = tick.slew.changed || changed;
  }
  // Where the slew now believes the map to be, remembered so the next tick can
  // tell whether that belief survived. Recorded on every tick and not only on
  // a change, because the two differ exactly when the belief was corrected: a
  // tick that just re-based reports the map's own centre with `changed` false,
  // and skipping it would compare against a centre the shell never got and
  // re-base every frame until the next fix.
  _reportedCenter = tick.slew.center;
  _reportedRotation = tick.slew.rotation_deg;
  _reportedCenterValid = YES;

  // Measured on the ticks that saw a fix, not on the fixes themselves. `Tick`
  // drains its queue in a loop and reports one `new_fix` however many arrived,
  // so on a phone whose loop is slower than its feed the retargets do arrive
  // at the slower rate — and what the slew needs is how long until it is aimed
  // somewhere else. Observed after the tick that used it, so the duration in
  // force is always predicted from intervals already seen.
  if (tick.new_fix) _cadence.Observe(now);

  // The trip is fed the raw fix, the same rule a recorded GPX follows: a trip
  // records what happened, and the snapper's answer is a guess about which
  // road it happened on. That guess is right for drawing a ship and wrong for
  // an odometer, where confident guesses alternating between a road and the
  // cycleway beside it would add metres nobody rode. `snap.raw` is what the
  // receiver said, snapped or not.
  if (tick.new_fix) [self consumeRawFix:tick.snap.raw];
  return tick;
}

// Feeds one raw fix to the two things that keep a record of the ride, the trip
// computer and the recorder, at most once each.
//
// Called from two places deliberately. A live receiver's fixes arrive at
// `pushFix:` one at a time, so that is where they are counted:
// `MovingMapTick` cannot serve them, because `Tick` drains its queue in a loop
// and reports only the last fix consumed, so a delayed frame that swallowed
// two would chord the trip straight across the first. A scripted replay never
// touches `pushFix:`, being polled inside the tick, so the tick counts that
// one.
//
// A live fix reaches both paths, and the timestamp check below is what makes
// that harmless rather than double distance.
- (void)consumeRawFix:(const fv::PositionFix&)raw {
  if (!raw.has_position) return;
  if (raw.has_time && _hasLastTripFixTime && raw.time_s == _lastTripFixTime) {
    return;
  }
  _trip.OnFix(raw);
  for (const fv::nav::GuidanceEvent& e : _guidance.OnFix(raw)) {
    _pendingGuidanceEvents.push_back(e);
  }
  // The same gate serves both, which is why the recorder is here rather than
  // in `pushFix:`: a live fix reaches this method twice, once on arrival and
  // once through the tick that consumed it, and the stamp above makes that one
  // point in the file. `GpxRecorder` would drop the repeat itself, but a
  // recorder counting a fix it was never offered differs from one refusing a
  // duplicate.
  if (_recorder.is_open()) _recorder.Add(raw);
  if (raw.has_time) {
    _lastTripFixTime = raw.time_s;
    _hasLastTripFixTime = YES;
  }
}

// The wall clock the trip is timed against, in epoch seconds rather than
// `CFAbsoluteTime`, whose zero is 2001: the ETA leaves here as a timestamp a
// shell formats as a time of day.
static double PPEpochNow() {
  return CFAbsoluteTimeGetCurrent() + kCFAbsoluteTimeIntervalSince1970;
}

- (nullable PPTrip*)tripOrNil {
  // Nil outside GPS mode rather than a zeroed trip: the ride is tied to the
  // mode, so not being in it means no trip rather than a trip with no numbers,
  // and the bar reading it is hidden in the same breath.
  if (!_gpsModeEnabled) return nil;
  return [[PPTrip alloc] initWithStats:_trip.Stats(PPEpochNow())];
}

// Nil in the three cases that mean "no banner": no ride, no route to be
// guided along, and nothing left to say. Off route is NOT one of them — the
// guidance is running and has stopped naming a corner, which the banner
// reports rather than disappearing over.
- (nullable PPGuidance*)guidanceOrNil {
  if (!_gpsModeEnabled) return nil;
  if (!_guidance.has_route()) return nil;
  const fv::nav::GuidanceState& state = _guidance.state();
  if (!state.valid) return nil;
  if (state.on_route && !state.has_next) return nil;

  NSString* road = @"";
  if (state.has_next && state.next_index < _guidance.maneuvers().size()) {
    const std::string& name = _guidance.maneuvers()[state.next_index].road;
    if (!name.empty()) road = @(name.c_str());
  }
  return [[PPGuidance alloc] initWithState:state road:road];
}

- (nullable PPGuidance*)currentGuidance {
  return [self guidanceOrNil];
}

- (nullable PPTrip*)currentTrip {
  return [self tripOrNil];
}

// Drains the buffer: every event belongs to exactly one frame, and calling
// this twice for one frame would announce a turn twice.
- (NSArray<PPGuidanceEvent*>*)takeGuidanceEvents {
  if (_pendingGuidanceEvents.empty()) return @[];
  NSMutableArray<PPGuidanceEvent*>* out =
      [NSMutableArray arrayWithCapacity:_pendingGuidanceEvents.size()];
  const std::vector<fv::nav::Maneuver>& maneuvers = _guidance.maneuvers();
  for (const fv::nav::GuidanceEvent& e : _pendingGuidanceEvents) {
    NSString* road = @"";
    if (e.maneuver_index < maneuvers.size() && !maneuvers[e.maneuver_index].road.empty()) {
      road = @(maneuvers[e.maneuver_index].road.c_str());
    }
    [out addObject:[[PPGuidanceEvent alloc] initWithEvent:e road:road]];
  }
  _pendingGuidanceEvents.clear();
  return out;
}

- (nullable PPOwnship*)ownshipOrNil {
  if (!_movingMap->has_fix()) return nil;
  // The snapper's answer and whether the overlay took it. `last_snap()`
  // reports what it made of the last fix whether or not it was applied, and
  // `last_fix()` is the applied one; below the confidence floor the two
  // disagree, and what to report is the one describing the position drawn.
  const fv::SnappedFix& snap = _movingMap->last_snap();
  const BOOL applied = (snap.snapped && snap.confidence >= _movingMap->snap_min_confidence())
                           ? YES
                           : NO;
  return [[PPOwnship alloc] initWithFix:_movingMap->last_fix()
                         screenAngleDeg:_movingMap->screen_angle_deg()
                           viewRotation:_tickRotation
                            symbolImage:[self shipSpriteImage]
                         pixelsPerPoint:_movingMap->symbol_dpi_scale()
                                heading:_movingMap->heading()
                                snapped:applied
                               roadName:applied ? snap.road_name : std::string()];
}

- (BOOL)hasFix { return _movingMap->has_fix() ? YES : NO; }

- (double)ownshipSymbolRadiusInPoints {
  // Asked of the overlay rather than re-read from settings, so a symbol
  // resized by anything else still gives the shell the truth. An authored
  // pixel is a point on this screen, so no conversion belongs here, and the
  // reach is the whole size rather than half because `fv.north` draws larger
  // than its shape box along its axis.
  //
  // The current chevron's multiple is 1.30 rather than 2.00, so this
  // over-states the reach by about a third. Left deliberately: the one caller
  // grows a viewport by this before asking whether a fix is worth drawing, and
  // there the safe direction is a frame that was not needed rather than a
  // missed one.
  return _movingMap->size_px();
}

- (void)pushFix:(PPFix*)fix {
  if (fix == nil) return;
  const fv::PositionFix f = PPFixFromLocationSample([fix sample]);
  // A fix with no position is one the moving map has no use for, and the queue
  // is bounded and drops the oldest when full, so putting one in costs a real
  // fix. It is still a whole `PPFix` above this line, because a shell may want
  // to say "no signal" from the same delivery.
  if (!f.has_position) return;
  // The raw fix, before the overlay's snapper sees it: a trip records what
  // happened, not which road the snapper guessed it happened on.
  [self consumeRawFix:f];
  _movingMap->PushFix(f);
}

- (void)resumeFromBackground {
  // Events raised while the app was in the background name corners the rider
  // has already reached; the banner is recomputed from the current state.
  _pendingGuidanceEvents.clear();
  _jumpCameraOnNextFix = YES;
}

#pragma mark - GPS mode

- (BOOL)isGpsModeEnabled { return _gpsModeEnabled; }

- (void)setGpsModeEnabled:(BOOL)enabled {
  [self overlayContentChanged];
  if (enabled == _gpsModeEnabled) return;
  _gpsModeEnabled = enabled;

  // The camera modes are a function of two switches, this one and the compass,
  // so they are computed in one place rather than written out at each press.
  // The single call also forces a recentre on the next fix (MM4).
  [self applyCameraModes];
  // A cadence learned on a ride that ended twenty minutes ago describes
  // nothing about this one.
  _cadence.Reset();

  // The snapper goes on with the mode and off without it. The graph loads at
  // the first press rather than at launch, so a rider who never presses GPS
  // never pays for it and one who has already planned a route has paid once,
  // since the store hands out the same graph the router loaded.
  //
  // The trip runs with the mode, and starts here rather than at the first fix:
  // the elapsed clock is wall time, and the twenty seconds a cold receiver
  // spends getting a lock are twenty seconds the rider has been waiting.
  if (enabled) {
    _hasLastTripFixTime = NO;
    _trip.SetRoute(_routeStore->RoutePath());
    _guidance.SetRoute(_routeStore->RoutePath(), _routeStore->RouteManeuvers());
    _trip.Start(PPEpochNow());
  } else {
    _trip.Stop(PPEpochNow());
    // Anything not yet drawn is not announced either: the ride is over.
    _pendingGuidanceEvents.clear();
  }

  if (enabled) {
    fv::Status s;
    const std::shared_ptr<const fv::routing::RoadGraphNetwork> network =
        _routeStore->EnsureRoadNetwork(&s);
    _hasRoadNetwork = network != nullptr ? YES : NO;
    // A pack with no `.fvroad` still follows and turns; it simply does not
    // snap, which is what MM5 calls the feature off.
    _movingMap->SetRoadNetwork(network);
  } else {
    _movingMap->SetRoadNetwork(nullptr);
    // An unfollowed ship is drawn from the raw fixes. `hasRoadNetwork` is left
    // as found: it answers whether this pack has roads, which a button press
    // does not change.
    _wantNorthUp = YES;
  }
}

#pragma mark - The compass

- (BOOL)isCourseUpEnabled { return _courseUp; }

- (void)setCourseUpEnabled:(BOOL)enabled {
  if (enabled == _courseUp) return;
  _courseUp = enabled;
  [self applyCameraModes];
  _cadence.Reset();
  // Leaving course-up unwinds the chart rather than snapping it, the same
  // mechanism as leaving GPS mode. Aimed on the next tick, because the rate
  // caps that shape it need a projection.
  //
  // Asked for only when the map is actually following. Outside GPS mode this
  // switch is a remembered preference and the chart is the user's own; turning
  // a preference off is no reason to undo a twist they made by hand.
  if (!enabled && _gpsModeEnabled) _wantNorthUp = YES;
}

- (void)requestNorthUp {
  _wantNorthUp = YES;
}

// The camera modes as a function of the two buttons. Called from both and
// nowhere else, so there is one answer to what the camera is doing.
//
// `SetModes` forces a recentre on the next fix, which is what makes either
// button do something visible rather than waiting for the ship to leave an
// apron computed under the other mode.
- (void)applyCameraModes {
  const BOOL following = _gpsModeEnabled && _courseUp;

  fv::CameraModes modes;
  modes.auto_center = _gpsModeEnabled ? true : false;
  modes.auto_rotate = following ? true : false;
  // Continuous belongs to course-up alone. North-up keeps MM2's apron, so the
  // map holds still until the ship approaches the edge and then moves once,
  // easing in and out. Course-up switches the apron off, because in track-up
  // it is a W/5 x 2H/5 box with the ship at its lower edge, and a rider
  // heading up the screen crosses a third of a screen — a minute of riding —
  // before the map moves or the chart turns.
  //
  // The pack can still ask for continuous north-up (`movingmap.continuous`).
  modes.continuous =
      following ? true : _settings.GetBool("movingmap.continuous", false);
  _movingMap->SetModes(modes);

  // The slew goes with the mode, because continuous centring costs what MM3's
  // header says unless the animation is retuned alongside it. `tickMovingMap`
  // sets the follow slew's duration per frame from the measured cadence; this
  // picks which of the two settings is in force.
  _movingMap->SetSlewSettings(following ? _followSlew : _idleSlew);

  // North-up follow pins the rotation at zero. The pin is applied in
  // `tickMovingMap`, where there is a projection to measure the chart with;
  // see `applyRotationPin:`.
  _rotationPinWanted = (_gpsModeEnabled && !_courseUp) ? YES : NO;
}

// Applies the north-up rotation pin. Here rather than in `applyCameraModes`
// because it needs a projection.
//
// Why a pin at all: MM2 answers `rotation_deg = map_rotation_deg` when
// `auto_rotate` is off, preserving whatever turn the chart has. So a rider
// leaving course-up mid-ride would have the next fix retarget the slew at
// whatever angle the unwind was passing through, freezing the chart
// half-turned and re-aiming it there on every fix. Telling the overlay the
// shell will not rotate stops it adopting `proj.Rotation()`, so the camera is
// asked for a placement on an unrotated chart, answers rotation 0, and the
// unwind and the recentre become one slew.
//
// The `slew().Reset` is load-bearing. `SetRotationSupported(false)` also does
// `slew_.Reset(center, 0)`, being written for a shell that could never rotate,
// so it takes "unsupported" to mean the chart is already at north. This shell
// can rotate and the chart is still turned, so the slew is told where the map
// actually is — on the slew alone, not `ResetMap`, which would put the same
// angle back into `map_rotation_deg_` and hand the camera the very number the
// pin exists to remove. Without it the chart snaps to north on the next fix
// instead of unwinding to it.
//
// The cost: with rotation unsupported the overlay draws the ship at its true
// bearing, so for the second the unwind takes, the chevron is off by whatever
// turn is left. It corrects itself the moment the chart reaches north.
- (void)applyRotationPin:(const fv::MapProjection&)proj {
  if (_rotationPinWanted == _rotationPinApplied) return;
  _rotationPinApplied = _rotationPinWanted;
  _movingMap->SetRotationSupported(_rotationPinApplied ? false : true);
  if (_rotationPinApplied) {
    _movingMap->slew().Reset(proj.Center(), proj.Rotation());
    _wantNorthUp = YES;  // and start for north now rather than on the next fix
  }
}

// Tells the slew the map was moved by something else — a drag, a pinch, a
// twist, a viewport clamped at the pack's edge, a skipped frame. Without it
// the next animation starts from a place the map left some time ago.
- (void)rebaseSlewTo:(const fv::MapProjection&)proj {
  if (_rotationPinApplied) {
    // Pinned, in north-up follow: the overlay must go on believing the chart
    // is at north, or the camera starts preserving the turn again. So only the
    // slew is corrected and the unwind is re-aimed rather than abandoned,
    // which is the case of a rider dragging the map while the chart is still
    // coming back to north.
    _movingMap->slew().Reset(proj.Center(), proj.Rotation());
    _wantNorthUp = YES;
    return;
  }
  _movingMap->ResetMap(proj.Center(), proj.Rotation());
}

#pragma mark - The route

- (void)setRouteDocumentURL:(NSURL*)url {
  _routeStore->set_document_path(url != nil ? url.fileSystemRepresentation ?: ""
                                            : "");
}

- (PPRoute*)routeFromSnapshot:(const pippin::RouteSnapshot&)snapshot {
  // Every route mutation funnels through here — the launch load, the sheet's
  // OK, and Clear — which is why the trip's copy of the line is refreshed here
  // and nowhere else. A distance-to-go measured along a route the rider has
  // already replaced is the failure this shape cannot have.
  _trip.SetRoute(_routeStore->RoutePath());
  // ...and the guidance's, for the same reason: a rider being counted down to
  // a corner on a route they have just replaced is the same defect.
  _guidance.SetRoute(_routeStore->RoutePath(), _routeStore->RouteManeuvers());
  return [[PPRoute alloc] initWithSnapshot:snapshot
                          planMilliseconds:_routeStore->last_plan_ms()];
}

- (nullable PPRoute*)loadSavedRouteWithError:(NSError**)error {
  [self overlayContentChanged];
  const fv::Status s = _routeStore->LoadAtLaunch();
  if (!s.ok()) {
    if (error) *error = ErrorFromStatus(PPErrorRouteFailed, s, @"saved route");
    return nil;
  }
  // A first launch comes back as a route whose `exists` is NO, never nil:
  // under the `NSError **` convention a nil return with no error is undefined
  // in Swift, so this method has two answers, a route or a throw.
  return [self routeFromSnapshot:_routeStore->Snapshot()];
}

- (PPRoute*)currentRoute {
  return [self routeFromSnapshot:_routeStore->Snapshot()];
}

- (PPRoute*)setRouteWaypoints:(NSArray<PPWaypoint*>*)waypoints
                      profile:(NSString*)profile {
  [self overlayContentChanged];
  const pippin::RouteSnapshot snapshot = _routeStore->SetWaypoints(
      PPWaypointsToRoute(waypoints), profile != nil ? profile.UTF8String : "");
  return [self routeFromSnapshot:snapshot];
}

- (PPRoute*)setRouteWaypoints:(NSArray<PPWaypoint*>*)waypoints
                      profile:(NSString*)profile
                     beachUse:(PPBeachUse)beachUse {
  [self overlayContentChanged];
  fv::RouteBeach beach = fv::RouteBeach::kNever;
  if (beachUse == PPBeachUseToSaveTime) beach = fv::RouteBeach::kToSaveTime;
  if (beachUse == PPBeachUseWheneverPossible) beach = fv::RouteBeach::kWheneverPossible;
  const pippin::RouteSnapshot snapshot = _routeStore->SetWaypoints(
      PPWaypointsToRoute(waypoints), profile != nil ? profile.UTF8String : "", beach);
  return [self routeFromSnapshot:snapshot];
}

- (NSTimeInterval)routeDepartureOverride {
  return _routeDepartureOverride;
}

- (void)setRouteDepartureOverride:(NSTimeInterval)t {
  [self overlayContentChanged];
  _routeDepartureOverride = t;
  if (std::isfinite(t)) {
    _routeStore->set_clock([t] { return t; });
  } else {
    _routeStore->set_clock(nullptr);
  }
}

- (BOOL)beachAvailable {
  return _routeStore->BeachAvailable() ? YES : NO;
}

- (PPRoute*)clearRoute {
  [self overlayContentChanged];
  return [self routeFromSnapshot:_routeStore->Clear()];
}

- (NSArray<NSString*>*)routeProfileNames {
  const std::vector<std::string> names = _routeStore->ProfileNames();
  NSMutableArray<NSString*>* out =
      [NSMutableArray arrayWithCapacity:names.size()];
  for (const std::string& n : names) {
    NSString* s = [NSString stringWithUTF8String:n.c_str()];
    if (s != nil) [out addObject:s];
  }
  return [out copy];
}

- (PPPlace*)describePlaceAt:(PPGeoPoint)coordinate
                    profile:(NSString*)profile
                remembering:(BOOL)remembering {
  // The radius and margin come from the pack, read per call rather than
  // cached: `FvSettings` is already in memory and this is one map lookup
  // against a label about to be laid out.
  const double radius = _settings.GetDouble("routing.pick_radius_m", 50.0);
  _routeStore->set_describe_stay_bonus_m(
      _settings.GetDouble("routing.pick_stay_bonus_m", 10.0));
  const pippin::PlaceDescription place = _routeStore->DescribePoint(
      fv::GeoPoint{coordinate.latitude, coordinate.longitude},
      radius > 0.0 ? radius : 50.0, profile != nil ? profile.UTF8String : "",
      remembering ? true : false);
  return [[PPPlace alloc] initWithDescription:place];
}

- (void)forgetNamedPlace { _routeStore->ForgetNamedPlace(); }

- (nullable PPSnapTarget*)snapTargetNear:(CGPoint)screenPoint
                              inViewport:(PPViewport*)viewport
                               tolerance:(double)tolerance {
  if (viewport == nil || !viewport.hasSurface) return nil;
  // 0 is how the pack spells no snapping, and it must short-circuit rather
  // than ask with a zero radius: a crosshair exactly on a marker would still
  // snap, which is not what switching it off means.
  if (tolerance <= 0.0) return nil;

  const double scale = viewport.displayScale > 0.0 ? viewport.displayScale : 1.0;
  const fv::PixelPoint px{(int)std::lround(screenPoint.x * scale),
                          (int)std::lround(screenPoint.y * scale)};

  // The stack, not a store: every overlay implementing the capability answers,
  // and nothing here knows which ones do.
  const std::vector<fv::app::SnapToItem> candidates = fv::app::SnapCandidates(
      *_overlays, [viewport projection], px, tolerance * scale);
  if (candidates.empty()) return nil;

  // Ranked nearest-first by the aggregation, so the front is the answer.
  return [[PPSnapTarget alloc] initWithItem:candidates.front()
                             distancePoints:candidates.front().distance_px /
                                            scale];
}

#pragma mark - Dragging a route waypoint

// Surface pixels out of iOS points. A helper rather than four copies of one
// line: the backing scale lives on the viewport, every one of these calls
// needs it, and a drag whose moves and hit test disagreed about units would
// put the waypoint two pixels from the finger on a 3x phone.
static inline fv::PixelPoint PPSurfacePixel(CGPoint p, PPViewport* viewport) {
  const double scale = viewport.displayScale > 0.0 ? viewport.displayScale : 1.0;
  return fv::PixelPoint{(int)std::lround(p.x * scale),
                        (int)std::lround(p.y * scale)};
}

/// `screenPoint` in the route overlay's drawn surface pixels. The overlay
/// is drawn at the band's camera, and its hit test and drag read that
/// projection, so a screen pixel goes through the ground to reach it.
- (fv::PixelPoint)routePixel:(CGPoint)screenPoint
                  inViewport:(PPViewport*)viewport {
  const fv::PixelPoint live = PPSurfacePixel(screenPoint, viewport);
  const auto& overlay = _routeStore->overlay();
  if (!overlay->has_projection()) return live;
  const double scale = viewport.displayScale > 0.0 ? viewport.displayScale : 1.0;
  fv::GeoPoint g;
  if (!viewport.projection.SurfaceToGeo(screenPoint.x * scale,
                                        screenPoint.y * scale, &g).ok())
    return live;
  double x = 0.0, y = 0.0;
  if (!overlay->last_projection().GeoToSurface(g, &x, &y).ok()) return live;
  return fv::PixelPoint{(int)std::lround(x), (int)std::lround(y)};
}

- (nullable NSString*)routeWaypointLabelNear:(CGPoint)screenPoint
                                  inViewport:(PPViewport*)viewport
                                   tolerance:(double)tolerance {
  if (viewport == nil || !viewport.hasSurface) return nil;
  const double scale = viewport.displayScale > 0.0 ? viewport.displayScale : 1.0;
  const pippin::RouteStore::WaypointHit hit = _routeStore->WaypointNear(
      [self routePixel:screenPoint inViewport:viewport], tolerance * scale);
  if (!hit.found) return nil;
  return [NSString stringWithUTF8String:hit.label.c_str()];
}

- (double)alertAmplitude {
  const double v = _settings.GetDouble("guidance.alert_amplitude", 0.9);
  return (v > 0.0 && v <= 1.0) ? v : 0.9;
}

- (double)routeWaypointHitTolerance {
  const double v = _settings.GetDouble("routing.drag_hit_tolerance", 22.0);
  return v > 0.0 ? v : 22.0;
}

- (double)routeWaypointHoldSeconds {
  const double v = _settings.GetDouble("routing.drag_hold_seconds", 0.5);
  return v > 0.0 ? v : 0.5;
}

- (BOOL)beginRouteWaypointDrag:(NSString*)label
                            at:(CGPoint)screenPoint
                    inViewport:(PPViewport*)viewport {
  [self overlayContentChanged];
  if (label.length == 0 || viewport == nil || !viewport.hasSurface) return NO;

  // Set at the press rather than at construction, for the same reason the
  // symbol DPI is set per tick: both are the shell's numbers and depend on the
  // surface the gesture is happening on. A phone handed a second screen
  // mid-ride would otherwise drag with the first one's pixels.
  //
  // The snap tolerance is `[pick] snap_tolerance`, deliberately the same
  // number the crosshair uses: how near a place must be to something exact
  // before it takes that thing's coordinate is one question, and answering it
  // two ways is how a route ends up with waypoints that look snapped and are
  // not.
  const double scale = viewport.displayScale > 0.0 ? viewport.displayScale : 1.0;
  _routeStore->SetDragSnapTolerancePx([self snapTolerance] * scale);
  _routeStore->set_drag_replan_budget_ms(
      _settings.GetDouble("routing.drag_replan_budget_ms", 30.0));

  return _routeStore->BeginWaypointDrag(
             label.UTF8String, [self routePixel:screenPoint inViewport:viewport])
             ? YES
             : NO;
}

- (BOOL)dragRouteWaypointTo:(CGPoint)screenPoint
                 inViewport:(PPViewport*)viewport {
  [self overlayContentChanged];
  if (viewport == nil || !viewport.hasSurface) return NO;
  return _routeStore->DragWaypointTo(
             [self routePixel:screenPoint inViewport:viewport])
             ? YES
             : NO;
}

- (PPRoute*)endRouteWaypointDragAt:(CGPoint)screenPoint
                        inViewport:(PPViewport*)viewport {
  [self overlayContentChanged];
  if (viewport == nil || !viewport.hasSurface) {
    // No surface to release onto, and putting it back is the only answer that
    // cannot leave a waypoint somewhere nobody asked for.
    return [self routeFromSnapshot:_routeStore->CancelWaypointDrag()];
  }
  return [self routeFromSnapshot:_routeStore->EndWaypointDrag(
                                     [self routePixel:screenPoint
                                           inViewport:viewport])];
}

- (PPRoute*)cancelRouteWaypointDrag {
  [self overlayContentChanged];
  return [self routeFromSnapshot:_routeStore->CancelWaypointDrag()];
}

- (BOOL)isDraggingRouteWaypoint {
  return _routeStore->dragging_waypoint() ? YES : NO;
}

#pragma mark - Search

// Puts the two sources that are not already overlays into the stack, so one
// walk finds everything. Both are held not visible and neither ever draws:
// Pippin renders its base map through `VectorRenderer` and has never drawn a
// road graph. That is the arrangement `search.h` describes when it says search
// ignores visibility by default.
//
// Lazy because of the graph: `EnsureRoadNetwork` reads `kiawah.fvroad`, and
// doing it at construction would put that cost in every launch of an app most
// of whose sessions never search and never route. `describePlaceAt:` already
// pays it on the first pick, so a launch that picked before it searched gets
// this free.
//
// The graph is shared, never loaded twice. `RoadGraphOverlay::EnsureGraph`
// would read the file itself and the app would hold Kiawah's network twice, so
// the planner's graph is handed over instead — the same sharing
// `PPRouteStore.h` documents between the router and the snapper.
- (void)ensureSearchProviders {
  if (_searchChart == nullptr) {
    _searchChart = std::make_shared<fv::VectorMapOverlay>("Chart", _source);
    _searchChart->SetVisible(false);
    _overlays->Add(_searchChart);
  }
  if (_searchRoads != nullptr || _searchRoadsFailed) return;

  // A missing or unreadable `.fvroad` is a configuration rather than an error
  // to repeat on every keystroke, so it is remembered: a pack without one
  // costs one failed load and then nothing.
  fv::Status s;
  _routeStore->EnsureRoadNetwork(&s);
  std::shared_ptr<const fv::routing::RoadGraph> graph = _routeStore->road_graph();
  if (graph == nullptr) {
    _searchRoadsFailed = true;
    return;
  }
  _searchRoads = std::make_shared<fv::RoadGraphOverlay>("Road graph");
  _searchRoads->SetGraph(std::move(graph));
  _searchRoads->SetVisible(false);
  _overlays->Add(_searchRoads);
}

- (NSArray<PPSearchResult*>*)searchFor:(NSString*)text
                            inViewport:(PPViewport*)viewport {
  [self ensureSearchProviders];

  fv::app::SearchQuery q;
  q.text = text != nil && text.UTF8String != nullptr ? text.UTF8String : "";
  q.max_results = (size_t)self.searchMaxResults;

  // The query carries no area, which is the whole of searching everything. An
  // area on the query cuts every provider in the stack, and only one needed
  // it: the points document and the road graph hold complete name tables and
  // answer over the whole island for nothing, while the tile pyramid cannot
  // answer a text query at all without an area or a name index. So the window
  // is lent to the chart (`VectorMapOverlay::SetSearchFallbackArea`) and the
  // query stays global.
  //
  // `near` is still always set. With text the session ranks by match quality
  // and uses distance only to break ties, which is what makes "beach" find the
  // beach road the rider is looking at before the one at the far end of the
  // island, and it is the only thing making a global answer read as a local
  // one.
  std::optional<fv::GeoRect> window;
  if (viewport != nil && viewport.hasSurface) {
    const fv::MapProjection& proj = [viewport projection];
    q.near = proj.Center();
    window = proj.VmapBounds();
  }
  // A degenerate box is no box, and the overlay makes that test itself: a
  // projection nobody has drawn with reports a zero-sized viewport.
  if (_searchChart != nullptr) {
    _searchChart->SetSearchFallbackArea(
        self.chartSearchIsViewOnly ? window : std::nullopt);
  }
  if (q.text.empty()) return @[];

  const std::vector<fv::app::SearchResult> found =
      fv::app::SearchSession(*_overlays).Search(q);

  // Who answered, by pointer identity against the overlays this class owns,
  // never by parsing the `detail` string, which is a provider's own words and
  // not a contract (`PPSearchRules.h` at `SearchSource`).
  const fv::Overlay* points = _pointStore->overlay().get();
  const fv::Overlay* route = _routeStore->overlay().get();

  std::vector<pippin::SearchRow> rows;
  rows.reserve(found.size());
  for (const fv::app::SearchResult& r : found) {
    pippin::SearchSource source;
    if (r.overlay == points) {
      source = pippin::SearchSource::kPoints;
    } else if (r.overlay == _searchRoads.get()) {
      source = pippin::SearchSource::kRoadGraph;
    } else if (r.overlay == _searchChart.get()) {
      source = pippin::SearchSource::kChart;
    } else if (r.overlay == route) {
      // The route's own waypoints, dropped. See `searchFor:inViewport:` in
      // `PPMap.h`: the dialog asking where a stop should go is the one place
      // they are noise.
      continue;
    } else {
      // An overlay this class did not add and cannot name. Skipping is the
      // conservative answer: a row with no honest word for what it is cannot
      // be one of the three kinds this list allows.
      continue;
    }

    pippin::SearchRow row;
    row.title = r.title;
    row.kind = pippin::ClassifySearchResult(source, r.detail);
    row.position = r.position;
    row.bounds = r.bounds;
    row.distance_m = q.near.has_value()
                         ? fv::app::SearchDistanceMeters(*q.near, r.position)
                         : 0.0;
    rows.push_back(std::move(row));
  }

  const std::vector<pippin::SearchRow> deduped =
      pippin::DedupeSearchRows(rows, self.searchDuplicateRadiusMeters);

  NSMutableArray<PPSearchResult*>* out =
      [NSMutableArray arrayWithCapacity:deduped.size()];
  for (const pippin::SearchRow& row : deduped)
    [out addObject:[[PPSearchResult alloc] initWithRow:row]];
  return [out copy];
}

- (BOOL)chartSearchIsViewOnly {
  return _settings.GetBool("search.chart_in_view_only", true) ? YES : NO;
}

- (NSString*)searchInitialText {
  NSString* text = [NSString
      stringWithUTF8String:_settings.GetString("search.initial_text", "")
                               .c_str()];
  return text != nil ? text : @"";
}

- (NSUInteger)searchMaxResults {
  const double v = _settings.GetDouble("search.max_results", 40.0);
  return v > 0.0 ? (NSUInteger)v : 40;
}

- (double)searchDuplicateRadiusMeters {
  const double v = _settings.GetDouble("search.duplicate_radius_m", 250.0);
  return v >= 0.0 ? v : 250.0;
}

- (double)searchFrameMinScale {
  const double v = _settings.GetDouble("search.frame_min_scale", 2500.0);
  return v >= 0.0 ? v : 2500.0;
}

- (double)searchFrameTopBias {
  const double v = _settings.GetDouble("search.frame_top_bias", 0.18);
  return v >= 0.0 && v < 0.5 ? v : 0.18;
}

- (double)snapTolerance {
  // Narrower than a tap's 22. At 30 the crosshair snapped to a marker 28
  // points away, plainly beside it on screen, and the rule is that the pick
  // overlaps the feature.
  //
  // The number is not the whole reach: `HitTestPoint` adds the marker's drawn
  // half-width first, so this is the margin beyond the ink. A 26-point marker
  // is caught from 13 + 12 = 25 points out, about when the crosshair's ring
  // touches it. A tap affords more because a fingertip is blunt; here the map
  // is steered under a crosshair the rider can place exactly, and eagerness
  // reads as the app picking the wrong thing.
  const double v = _settings.GetDouble("pick.snap_tolerance", 12.0);
  return v >= 0.0 ? v : 12.0;
}

#pragma mark - The points

- (void)setPointsDocumentURL:(NSURL*)url {
  _pointStore->set_document_path(url != nil ? url.fileSystemRepresentation ?: ""
                                            : "");
}

- (BOOL)loadPointsWithError:(NSError**)error {
  [self overlayContentChanged];
  const fv::Status s = _pointStore->LoadAtLaunch();
  if (!s.ok()) {
    if (error) *error = ErrorFromStatus(PPErrorPointsFailed, s, @"saved points");
    return NO;
  }
  return YES;
}

- (NSArray<PPMapPoint*>*)points {
  const std::vector<fv::MapPoint>& rows = _pointStore->Points();
  NSMutableArray<PPMapPoint*>* out =
      [NSMutableArray arrayWithCapacity:rows.size()];
  for (const fv::MapPoint& p : rows)
    [out addObject:[[PPMapPoint alloc] initWithMapPoint:p]];
  return [out copy];
}

- (NSArray<PPPointSymbol*>*)pointSymbols {
  const std::vector<fv::PointSymbol>& rows = _pointStore->Symbols();
  NSMutableArray<PPPointSymbol*>* out =
      [NSMutableArray arrayWithCapacity:rows.size()];
  for (const fv::PointSymbol& sym : rows)
    [out addObject:[[PPPointSymbol alloc] initWithPointSymbol:sym]];
  return [out copy];
}

- (BOOL)arePointsVisible { return _pointStore->visible() ? YES : NO; }

- (void)setPointsVisible:(BOOL)visible {
  [self overlayContentChanged];
  _pointStore->SetVisible(visible ? true : false);
  // Hiding the set drops the selection with it: a highlighted marker nobody
  // can see is a state the next tap on the button would restore for no reason
  // the user would remember.
  if (!visible) _pointStore->SetSelected(0);
}

- (int64_t)selectedPointId { return _pointStore->selected(); }

- (void)setSelectedPointId:(int64_t)pointId {
  [self overlayContentChanged];
  _pointStore->SetSelected(pointId);
}

- (nullable PPMapPoint*)pointNear:(CGPoint)screenPoint
                       inViewport:(PPViewport*)viewport
                        tolerance:(double)tolerance {
  if (viewport == nil || !viewport.hasSurface) return nil;

  // Points in, surface pixels out. The conversion happens here because the
  // backing scale is on the viewport; a shell doing it itself is how a tap
  // lands 2 px from the finger on a 3x phone.
  const double scale = viewport.displayScale > 0.0 ? viewport.displayScale : 1.0;
  const fv::PixelPoint px{(int)std::lround(screenPoint.x * scale),
                          (int)std::lround(screenPoint.y * scale)};

  // The projection the viewport is: the same one the frame under the finger
  // was drawn with, asked rather than reconstructed.
  const int64_t id =
      _pointStore->HitTest([viewport projection], px, tolerance * scale);
  if (id == 0) return nil;
  const fv::MapPoint* hit = _pointStore->Find(id);
  return hit != nullptr ? [[PPMapPoint alloc] initWithMapPoint:*hit] : nil;
}

- (PPMapPoint*)addPoint:(PPMapPoint*)point {
  [self overlayContentChanged];
  const int64_t id = _pointStore->AddPoint([point mapPoint]);
  const fv::MapPoint* stored = _pointStore->Find(id);
  // The point as stored, because the id is what every later edit and delete
  // goes through and the caller handed in a 0.
  return stored != nullptr ? [[PPMapPoint alloc] initWithMapPoint:*stored]
                           : point;
}

- (BOOL)updatePoint:(PPMapPoint*)point {
  [self overlayContentChanged];
  return _pointStore->UpdatePoint([point mapPoint]) ? YES : NO;
}

- (BOOL)removePointWithId:(int64_t)pointId {
  [self overlayContentChanged];
  return _pointStore->RemovePoint(pointId) ? YES : NO;
}

- (NSString*)pointWriteError {
  NSString* s = [NSString
      stringWithUTF8String:_pointStore->last_write_error().c_str()];
  return s != nil ? s : @"";
}

- (BOOL)pointsWereSeeded { return _pointStore->seeded() ? YES : NO; }

- (double)pointHitTolerance {
  // 22 points is about a thumb, and it is added to the marker's drawn
  // half-width inside `HitTestPoint`, so a 22-pt marker is pressable 33 points
  // from its centre. That is Apple's 44-point target arrived at honestly
  // rather than by padding a rectangle.
  const double v = _settings.GetDouble("points.hit_tolerance", 22.0);
  return v > 0.0 ? v : 22.0;
}

#pragma mark - Recording the ride

- (BOOL)startRecordingToURL:(NSURL*)fileURL
                       name:(NSString*)name
                      error:(NSError**)error {
  [self stopRecording];
  if (fileURL == nil || fileURL.path == nil) {
    if (error)
      *error = MakeError(PPErrorRecordFailed, @"no file to record into");
    return NO;
  }

  fv::GpxRecorderOptions options;
  options.track_name = name != nil ? std::string(name.UTF8String) : std::string();
  // What sort of ride, in the vocabulary Strava and Garmin Connect read, so a
  // shared file lands in the right sport rather than as "other". It follows
  // the pack's routing profile, which is what the rider told the app they are
  // doing; the mapping lives here rather than in the settings file so a pack
  // cannot spell it wrong.
  const std::string profile = _settings.GetString("routing.profile", "bicycle");
  options.track_type = _settings.GetString(
      "movingmap.record_type", profile == "foot" ? "walking" : "cycling");
  // The gap that starts a new <trkseg>. The default is the recorder's own
  // 20 s: long enough to survive a receiver stumbling under live oaks, short
  // enough that a stop for coffee is not drawn as a line across the marsh.
  options.split_gap_s = _settings.GetDouble("movingmap.record_split_gap_s", 20.0);
  options.write.creator = "Peregrine Pippin";

  const fv::Status status =
      _recorder.Open(std::string(fileURL.path.UTF8String), options);
  if (!status.ok()) {
    if (error)
      *error = ErrorFromStatus(PPErrorRecordFailed, status, @"start recording");
    return NO;
  }
  _recordingURL = fileURL;
  return YES;
}

- (void)stopRecording {
  if (!_recorder.is_open()) return;
  _recorder.Close();
  _recordingURL = nil;
}

- (BOOL)isRecording { return _recorder.is_open() ? YES : NO; }

- (NSURL*)recordingURL { return _recordingURL; }

- (NSUInteger)recordedPointCount {
  return static_cast<NSUInteger>(_recorder.point_count());
}

+ (BOOL)writePoint:(PPMapPoint*)point
          toGpxURL:(NSURL*)fileURL
             error:(NSError**)error {
  if (point == nil || fileURL == nil || fileURL.path == nil) {
    if (error) *error = MakeError(PPErrorRecordFailed, @"no point to write");
    return NO;
  }

  const fv::MapPoint& p = [point mapPoint];

  fv::GpxDocument document;
  document.name = p.name;
  fv::PositionFix fix;
  fix.SetPosition(p.position.lat, p.position.lon);
  // Feet in the document, metres in the file: `.fvpoints` stores elevation in
  // feet and GPX's <ele> is metres. A zero is taken as not given rather than
  // as sea level, which is what the document means by it.
  if (p.elevation_ft != 0.0) {
    fix.altitude_msl_m = p.elevation_ft * 0.3048;
    fix.has_altitude = true;
  }
  document.waypoints.push_back(fix);
  document.waypoint_names.push_back(p.name);
  // The remarks become the waypoint's <desc>, the field every map app shows
  // under the name.
  document.waypoint_descriptions.push_back(p.remarks);

  fv::GpxWriteOptions options;
  options.creator = "Peregrine Pippin";
  const fv::Status status =
      fv::WriteGpxFile(std::string(fileURL.path.UTF8String), document, options);
  if (!status.ok()) {
    if (error)
      *error = ErrorFromStatus(PPErrorRecordFailed, status, @"write point");
    return NO;
  }
  return YES;
}

- (void)stopDemoFeed {
  if (!_demoFeed) return;
  _demoFeed->Stop();
  // Detached, not merely stopped: `tickMovingMap` polls whatever source hangs
  // off this object on every render, so leaving a stopped one wired is one
  // `Start()` away from replaying a recorded ride under a live receiver. Null
  // is how MM4 spells no source, and it clears the queue and heading history,
  // which is right because those fixes belong to a feed that is going away.
  _movingMap->SetSource(nullptr);
  _demoFeed.reset();
}

- (BOOL)startDemoFeedWithError:(NSError**)error {
  const std::string path =
      _packRoot + "/" + _settings.GetString("movingmap.demo_track", "");
  if (path.size() <= _packRoot.size() + 1) {
    if (error)
      *error = MakeError(PPErrorPackIncomplete,
                         @"no movingmap.demo_track in pippin.ini");
    return NO;
  }

  // One replay path: the GPX and NMEA readers both arrive at
  // `BuildScriptedTrackFromFixes`, so the moving map does not learn a second
  // kind of track (MM6). The fixes' own stamps are the schedule, so the ride
  // replays at the speed it was ridden.
  fv::GpxDocument document;
  const fv::Status gs = fv::ReadGpxFile(path, &document);
  if (!gs.ok()) {
    if (error) *error = ErrorFromStatus(PPErrorPackIncomplete, gs, @"demo track");
    return NO;
  }
  std::vector<fv::PositionFix> fixes = fv::FlattenGpxFixes(document);
  std::vector<fv::ScriptedFix> track = fv::BuildScriptedTrackFromFixes(fixes);
  if (track.size() < 2) {
    if (error)
      *error = MakeError(PPErrorPackIncomplete,
                         @"the demo track has fewer than two points in it");
    return NO;
  }

  _demoFeed = std::make_shared<fv::ScriptedSource>(std::move(track));
  // 1.0 is the speed it was ridden, which is the honest default and a slow
  // demo; the key is how a viewer gets 28 minutes of Kiawah in three.
  _demoFeed->SetTimeScale(_settings.GetDouble("movingmap.demo_time_scale", 1.0));
  // Looping, with one wart: this ride is not a closed circuit — it ends 5.5 km
  // from where it began — so once a lap the ship teleports home. That is the
  // price of looping an open track, and a demo that stops after 28 minutes
  // looks broken in a way a jump every 28 minutes does not.
  _demoFeed->SetLooping(_settings.GetBool("movingmap.demo_loop", true));
  _movingMap->SetSource(_demoFeed);
  const fv::Status ss = _movingMap->Start();
  if (!ss.ok()) {
    if (error) *error = ErrorFromStatus(PPErrorPackIncomplete, ss, @"demo feed");
    return NO;
  }
  return YES;
}

@end
