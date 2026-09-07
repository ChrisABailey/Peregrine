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
#import "PPTrip+Internal.h"
#import "PPViewport+Internal.h"

#include <algorithm>
#include <cmath>
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

@implementation PPOwnship

- (instancetype)initWithFix:(const fv::PositionFix&)fix
            screenAngleDeg:(double)angle
                   heading:(const fv::ResolvedHeading&)heading
                   snapped:(BOOL)snapped
                  roadName:(const std::string&)roadName {
  self = [super init];
  if (self == nil) return nil;
  _coordinate = PPGeoPointMake(fix.lat, fix.lon);
  _screenAngleDegrees = angle;
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

@end

@implementation PPFrame {
  CGImageRef _overlayImage;
  CGImageRef _baseImage;
}

- (instancetype)initWithOverlayImage:(CGImageRef)overlayImage
                     overlayViewport:(PPViewport*)overlayViewport
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
                                slew:(const fv::SlewState&)slew {
  self = [super init];
  if (self == nil) return nil;
  // The frame takes both references: the overlay's is the one the bridge just
  // minted, and the base's is a retain on pixels the map is still holding.
  _overlayImage = overlayImage;
  _baseImage = baseImage;
  _viewport = overlayViewport;
  _baseViewport = baseViewport;
  _baseWasDrawn = baseWasDrawn;
  _featuresQueried = features;
  _drawsEmitted = draws;
  _queryZoom = zoom;
  _renderMilliseconds = ms;
  _baseMilliseconds = baseMs;
  _ownship = ownship;
  _trip = trip;
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

@implementation PPMap {
  std::string _packRoot;
  fv::Settings _settings;

  // The two layers. The base map is drawn into `_baseCanvas`, the guard band,
  // which is larger than the screen and kept between frames; the overlays are
  // drawn into `_overlayCanvas`, which is exactly the screen, cleared to
  // transparent every frame and composited over the base by the display.
  std::unique_ptr<fv::CpuCanvas> _baseCanvas;
  std::unique_ptr<fv::CpuCanvas> _overlayCanvas;
  std::shared_ptr<fv::OsmVectorSource> _source;
  std::shared_ptr<fv::OsmStyleEngine> _style;
  std::unique_ptr<fv::VectorRenderer> _renderer;

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
  return self;
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

- (PPViewport*)initialViewport {
  // The pack's zoom range is the pyramid's own, straight off the file: what
  // the data holds is what the camera is allowed to ask for.
  return [[PPViewport alloc] initWithHomeBounds:_homeBounds
                                    minTileZoom:_source->file().min_zoom()
                                    maxTileZoom:_source->file().max_zoom()
                             mmPerPixelOverride:_mmPerPixelOverride];
}

- (void)dealloc {
  // The cached base map's image is a CF reference held outside ARC's reach,
  // and it is the only one this class owns.
  if (_baseImage != nullptr) CGImageRelease(_baseImage);
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

- (nullable PPFrame*)renderViewport:(PPViewport*)viewport
                              error:(NSError**)error {
  // The unbanded, uncached call: what every caller outside the render loop
  // wants, and what the loop asks for when it settles.
  return [self renderViewport:viewport bandMargin:0.0 reuseBase:NO error:error];
}

- (nullable PPFrame*)renderViewport:(PPViewport*)viewport
                         bandMargin:(double)bandMargin
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
  const BOOL baseServes =
      reuseBase && haveBase &&
      pippin::BaseCovers(_baseViewport.projection, proj, _baseLimits);

  double baseMs = 0.0;
  if (!baseServes) {
    const CFAbsoluteTime b0 = CFAbsoluteTimeGetCurrent();
    PPViewport* band = [viewport viewportGrownByMargin:bandMargin];
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
    _source->SetDisplayMmPerPixel(mmPerPoint);
    _style->SetDisplayMmPerPixel(mmPerPoint);
    const double lat =
        std::round(viewport.center.latitude / kRefLatStep) * kRefLatStep;
    if (!_refLatSet || lat != _refLat) {
      _style->SetReferenceLatitude(lat);
      _refLat = lat;
      _refLatSet = YES;
    }
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
  // Every frame, at the live camera, on the screen's own surface. No band,
  // because it is redrawn every frame, so its preview transform covers only
  // the milliseconds the draw took. Cleared to transparent, which is what
  // `CpuCanvas::BlendPixel` composites onto.
  const int w = viewport.pixelWidth;
  const int h = viewport.pixelHeight;
  if (w != _overlayCanvasWidth || h != _overlayCanvasHeight) {
    _overlayCanvas = std::make_unique<fv::CpuCanvas>(w, h);
    _overlayCanvasWidth = w;
    _overlayCanvasHeight = h;
    if (_hasLabelFont) _overlayCanvas->SetDefaultFont(_fontPath);
  }
  _overlayCanvas->Clear(fv::FvColor{0, 0, 0, 0});

  // The moving map's tick happens here, at the top of the drawing rather than
  // on the main thread.
  //
  // MM4 put the camera on the overlay because an overlay is the only object
  // both drawn per frame and fed fixes: `OnDraw` recomputes the apron and
  // `Tick` consumes it, and the wrong order freezes the map. So the overlay
  // lives on the drawing side of Pippin's thread split. The main thread never
  // touches it; what crosses is a `PPFix` in, through MM1's locked queue, and
  // a `PPOwnship` out.
  //
  // The consequence is that the feed advances only when a frame is asked for.
  // The shell's loop pauses when the camera stops moving, so `MapModel`'s
  // content-dirty flag is what keeps it going for a running feed.
  //
  // Below the base map's `if` rather than inside it: the tick is the feed, the
  // trip computer and the camera, and thinning it along with the pixels would
  // be a moving map that stops whenever its picture is cheap enough to keep.
  const fv::MovingMapTick tick = [self tickMovingMap:proj
                                       pixelsPerPoint:pixelsPerPoint];

  _overlays->DrawAll(proj, *_overlayCanvas);

  CGImageRef overlay = PPCreateCGImageFromPixelBuffer(_overlayCanvas->Buffer());
  if (overlay == nullptr) {
    if (error) *error = MakeError(PPErrorRenderFailed, @"CGImage creation failed");
    return nil;
  }
  return [[PPFrame alloc]
        initWithOverlayImage:overlay
            overlayViewport:viewport
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
                       slew:tick.slew];
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

  const fv::MovingMapTick tick = _movingMap->Tick(proj, dt);
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

#pragma mark - GPS mode

- (BOOL)isGpsModeEnabled { return _gpsModeEnabled; }

- (void)setGpsModeEnabled:(BOOL)enabled {
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
    _trip.Start(PPEpochNow());
  } else {
    _trip.Stop(PPEpochNow());
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
  return [[PPRoute alloc] initWithSnapshot:snapshot
                          planMilliseconds:_routeStore->last_plan_ms()];
}

- (nullable PPRoute*)loadSavedRouteWithError:(NSError**)error {
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
  const pippin::RouteSnapshot snapshot = _routeStore->SetWaypoints(
      PPWaypointsToRoute(waypoints), profile != nil ? profile.UTF8String : "");
  return [self routeFromSnapshot:snapshot];
}

- (PPRoute*)clearRoute {
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

- (nullable NSString*)routeWaypointLabelNear:(CGPoint)screenPoint
                                  inViewport:(PPViewport*)viewport
                                   tolerance:(double)tolerance {
  if (viewport == nil || !viewport.hasSurface) return nil;
  const double scale = viewport.displayScale > 0.0 ? viewport.displayScale : 1.0;
  const pippin::RouteStore::WaypointHit hit = _routeStore->WaypointNear(
      PPSurfacePixel(screenPoint, viewport), tolerance * scale);
  if (!hit.found) return nil;
  return [NSString stringWithUTF8String:hit.label.c_str()];
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

  return _routeStore->BeginWaypointDrag(label.UTF8String,
                                        PPSurfacePixel(screenPoint, viewport))
             ? YES
             : NO;
}

- (BOOL)dragRouteWaypointTo:(CGPoint)screenPoint
                 inViewport:(PPViewport*)viewport {
  if (viewport == nil || !viewport.hasSurface) return NO;
  return _routeStore->DragWaypointTo(PPSurfacePixel(screenPoint, viewport))
             ? YES
             : NO;
}

- (PPRoute*)endRouteWaypointDragAt:(CGPoint)screenPoint
                        inViewport:(PPViewport*)viewport {
  if (viewport == nil || !viewport.hasSurface) {
    // No surface to release onto, and putting it back is the only answer that
    // cannot leave a waypoint somewhere nobody asked for.
    return [self routeFromSnapshot:_routeStore->CancelWaypointDrag()];
  }
  return [self routeFromSnapshot:_routeStore->EndWaypointDrag(
                                     PPSurfacePixel(screenPoint, viewport))];
}

- (PPRoute*)cancelRouteWaypointDrag {
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
  _pointStore->SetVisible(visible ? true : false);
  // Hiding the set drops the selection with it: a highlighted marker nobody
  // can see is a state the next tap on the button would restore for no reason
  // the user would remember.
  if (!visible) _pointStore->SetSelected(0);
}

- (int64_t)selectedPointId { return _pointStore->selected(); }

- (void)setSelectedPointId:(int64_t)pointId {
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
  const int64_t id = _pointStore->AddPoint([point mapPoint]);
  const fv::MapPoint* stored = _pointStore->Find(id);
  // The point as stored, because the id is what every later edit and delete
  // goes through and the caller handed in a 0.
  return stored != nullptr ? [[PPMapPoint alloc] initWithMapPoint:*stored]
                           : point;
}

- (BOOL)updatePoint:(PPMapPoint*)point {
  return _pointStore->UpdatePoint([point mapPoint]) ? YES : NO;
}

- (BOOL)removePointWithId:(int64_t)pointId {
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
