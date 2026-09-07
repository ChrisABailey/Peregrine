// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPLocationSource.h"

#import <CoreLocation/CoreLocation.h>

#import "PPFix+Internal.h"

namespace {

PPLocationAuthorization Translate(CLAuthorizationStatus status) {
  switch (status) {
    case kCLAuthorizationStatusAuthorizedAlways:
    case kCLAuthorizationStatusAuthorizedWhenInUse:
      return PPLocationAuthorizationAuthorized;
    case kCLAuthorizationStatusDenied:
      return PPLocationAuthorizationDenied;
    case kCLAuthorizationStatusRestricted:
      return PPLocationAuthorizationRestricted;
    case kCLAuthorizationStatusNotDetermined:
    default:
      return PPLocationAuthorizationNotDetermined;
  }
}

// Copies fields, and nothing more. Every rule about which of these numbers
// means "I do not know" is in PPLocationFix.h, where the mac test bed can
// reach it.
PPLocationSample SampleFromLocation(CLLocation* l) {
  PPLocationSample s;
  s.latitude = l.coordinate.latitude;
  s.longitude = l.coordinate.longitude;
  s.horizontal_accuracy_m = l.horizontalAccuracy;
  s.altitude_m = l.altitude;
  s.vertical_accuracy_m = l.verticalAccuracy;
  s.course_deg = l.course;
  s.course_accuracy_deg = l.courseAccuracy;
  s.speed_mps = l.speed;
  s.speed_accuracy_mps = l.speedAccuracy;
  s.timestamp_s = l.timestamp.timeIntervalSince1970;
  s.has_timestamp = l.timestamp != nil;
  return s;
}

}  // namespace

@interface PPLocationSource () <CLLocationManagerDelegate>
@end

@implementation PPLocationSource {
  CLLocationManager* _manager;
  BOOL _wantsUpdates;   // `start` was called; the intent, not the hardware
  BOOL _updatesLive;    // `startUpdatingLocation` has actually been called
  BOOL _updatesPaused;  // CoreLocation paused a live run on its own
}

- (instancetype)init {
  self = [super init];
  if (self == nil) return nil;

  _manager = [[CLLocationManager alloc] init];
  _manager.delegate = self;

  // The fitness activity type tells CoreLocation the platform stops at traffic
  // lights and starts again, which is what stops it deciding the ride is over.
  // It is the one setting that is right in BOTH modes, so it is set once here
  // and `applyAccuracyMode` never touches it.
  _manager.activityType = CLActivityTypeFitness;

  // Born in navigation mode: a launch is the one moment somebody is certainly
  // looking for the ship, and P17's policy above only ever moves off this
  // after a fix has said the ship is nowhere near the screen.
  _accuracyMode = PPLocationAccuracyModeNavigation;
  [self applyAccuracyMode];

  _authorization = Translate(_manager.authorizationStatus);
  return self;
}

// MARK: - Accuracy

- (void)setAccuracyMode:(PPLocationAccuracyMode)mode {
  if (mode == _accuracyMode) return;
  _accuracyMode = mode;
  [self applyAccuracyMode];

  // A run CoreLocation paused on its own does not come back because the
  // settings changed — the system resumes it when it next sees motion, which
  // is no use to a rider who has just pressed the GPS button while stopped.
  // So the return to navigation restarts it by hand.
  if (mode == PPLocationAccuracyModeNavigation && _updatesLive && _updatesPaused) {
    _updatesPaused = NO;
    [_manager startUpdatingLocation];
  }
}

- (void)applyAccuracyMode {
  if (_accuracyMode == PPLocationAccuracyModeCoarse) {
    // Nobody is watching the ship. `ThreeKilometers` is the tier where iOS
    // can answer from cell and wifi and largely power the GNSS chip down;
    // everything above it keeps the receiver running, which is why this is
    // not merely a notch off `Best`.
    _manager.desiredAccuracy = kCLLocationAccuracyThreeKilometers;
    // A hundred metres, not three kilometres. The filter is how far a fix
    // must move before it is delivered, and what these fixes exist to notice
    // is a rider travelling back towards the part of the map on screen. At a
    // riding zoom the viewport is a few hundred metres across, so a filter
    // the size of the accuracy tier would step over the return threshold; a
    // hundred metres still discards the standing-still jitter.
    _manager.distanceFilter = 100.0;
    // The `NO` below is justified by a navigation app that must not quietly
    // stop navigating, which is true while navigating and costs nothing when
    // nothing is following and nothing is recording.
    _manager.pausesLocationUpdatesAutomatically = YES;
    return;
  }
  // A bike on an island, and the plan's own settings. `BestForNavigation`
  // asks for the receiver's best and lets it use the accelerometers.
  _manager.desiredAccuracy = kCLLocationAccuracyBestForNavigation;
  // Every update, not every N metres: the moving map wants a fix while the
  // rider is stopped at a junction as much as while they are moving.
  _manager.distanceFilter = kCLDistanceFilterNone;
  // CoreLocation pauses updates when it thinks the journey has ended, and
  // resuming is the app's job. A navigation app that quietly stops navigating
  // is worse than one that costs a little more battery.
  _manager.pausesLocationUpdatesAutomatically = NO;
}

- (void)dealloc {
  [_manager stopUpdatingLocation];
  _manager.delegate = nil;
}

+ (BOOL)locationServicesEnabled {
  return [CLLocationManager locationServicesEnabled];
}

- (BOOL)running { return _wantsUpdates; }

- (void)start {
  _wantsUpdates = YES;
  if (_authorization == PPLocationAuthorizationNotDetermined) {
    // The answer arrives at `locationManagerDidChangeAuthorization:`, which
    // is where updates actually begin. Asking here and starting there is the
    // only ordering that works: `startUpdatingLocation` before an answer is a
    // no-op that never retries.
    [_manager requestWhenInUseAuthorization];
    return;
  }
  [self startUpdatesIfPermitted];
}

- (void)stop {
  _wantsUpdates = NO;
  if (_updatesLive) {
    [_manager stopUpdatingLocation];
    _updatesLive = NO;
  }
  _updatesPaused = NO;
}

- (void)startUpdatesIfPermitted {
  if (!_wantsUpdates || _updatesLive) return;
  if (_authorization != PPLocationAuthorizationAuthorized) return;
  [_manager startUpdatingLocation];
  _updatesLive = YES;
  _updatesPaused = NO;
}

// MARK: - CLLocationManagerDelegate (main thread, per the header)

- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager {
  const PPLocationAuthorization now = Translate(manager.authorizationStatus);
  if (now == _authorization) return;
  _authorization = now;

  if (now == PPLocationAuthorizationAuthorized) {
    [self startUpdatesIfPermitted];
  } else if (_updatesLive) {
    // Permission withdrawn while running (Settings, or a screen-time rule).
    [manager stopUpdatingLocation];
    _updatesLive = NO;
  }

  if ([self.delegate respondsToSelector:@selector(locationSource:
                                            didChangeAuthorization:)]) {
    [self.delegate locationSource:self didChangeAuthorization:now];
  }
}

- (void)locationManager:(CLLocationManager*)manager
     didUpdateLocations:(NSArray<CLLocation*>*)locations {
  // CoreLocation may hand over several at once (it batches while the app is
  // busy). EVERY ONE IS DELIVERED, not just the newest: the heading resolver
  // derives its answer from a history of distinct positions, so dropping the
  // middle of a batch would make a derived heading depend on how busy the
  // main thread happened to be. MM4 makes the same distinction one layer
  // down — every queued fix reaches the resolver, only the last reaches the
  // camera — and this is the shell keeping its side of it.
  for (CLLocation* location in locations) {
    if (location == nil) continue;
    PPFix* fix =
        [[PPFix alloc] initWithSample:SampleFromLocation(location)];
    _lastFix = fix;
    [self.delegate locationSource:self didProduceFix:fix];
  }
}

// A pause can only happen in coarse mode, the only one that sets
// `pausesLocationUpdatesAutomatically`. It is recorded rather than acted on:
// iOS resumes a paused run when it next sees motion, and the one case that
// cannot wait for motion — somebody pressing GPS or Record while stopped — is
// handled by `setAccuracyMode:` restarting updates on its way back to
// navigation.
- (void)locationManagerDidPauseLocationUpdates:(CLLocationManager*)manager {
  _updatesPaused = YES;
}

- (void)locationManagerDidResumeLocationUpdates:(CLLocationManager*)manager {
  _updatesPaused = NO;
}

- (void)locationManager:(CLLocationManager*)manager
       didFailWithError:(NSError*)error {
  // kCLErrorLocationUnknown means "not yet" rather than "no": CoreLocation
  // keeps trying, and a phone that has just come indoors reports a run of
  // them. Passing it up as a failure would warn on screen every time a rider
  // goes under a bridge.
  if ([error.domain isEqualToString:kCLErrorDomain] &&
      error.code == kCLErrorLocationUnknown) {
    return;
  }
  if ([self.delegate respondsToSelector:@selector(locationSource:
                                                 didFailWithError:)]) {
    [self.delegate locationSource:self didFailWithError:error];
  }
}

@end
