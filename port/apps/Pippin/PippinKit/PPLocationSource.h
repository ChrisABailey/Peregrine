// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPLocationSource.h — the phone's own receiver.
//
// The one class in Pippin that knows CoreLocation exists. Everything else
// speaks `PPFix`, which is why the demo feed, a replayed GPX and a real phone
// are the same shape above here rather than three special cases.
//
// Not an `fv::IPositionSource`, though the interface is only three methods. A
// source delivers fixes to a listener, and a listener that is a C++
// `std::function` cannot be handed to Swift, so the ObjC delegate is the
// listener and the C++ side of the feed lives where the drawing lives. A
// fix's journey is CoreLocation's queue -> here -> the main thread ->
// `PPMap.pushFix:` on the render queue -> `fv::FixQueue` -> `Tick`. Only the
// last hop needs a lock, and MM1 built it.
//
// Authorization is when-in-use and v1 asks for no more. Background updates
// need a capability, a second purpose string and a battery story.

#import <Foundation/Foundation.h>

#import <PippinKit/PPFix.h>

NS_ASSUME_NONNULL_BEGIN

/// `CLAuthorizationStatus`, without CoreLocation in the header. The two "no"
/// cases stay apart because they are different sentences to a user: `Denied`
/// is a decision they can change in Settings, `Restricted` is one they
/// cannot.
typedef NS_ENUM(NSInteger, PPLocationAuthorization) {
  PPLocationAuthorizationNotDetermined = 0,
  PPLocationAuthorizationRestricted = 1,
  PPLocationAuthorizationDenied = 2,
  PPLocationAuthorizationAuthorized = 3,
};

/// How hard the receiver is being worked.
///
/// `desiredAccuracy` is a hint rather than a dial: every tier from `Best` down
/// through `HundredMeters` keeps the GNSS chip powered, so the only step
/// change worth having is at `Kilometer`/`ThreeKilometers`, where iOS can
/// answer from cell and wifi. Hence two named configurations rather than a
/// passed-through `CLLocationAccuracy`: there is nothing useful in between,
/// and a caller offered the whole enum would pick something costing as much
/// as `Best` and believe it had saved something.
typedef NS_ENUM(NSInteger, PPLocationAccuracyMode) {
  /// The moving map's own setting, and the one this starts in:
  /// `BestForNavigation`, every fix, no automatic pause.
  PPLocationAccuracyModeNavigation = 0,
  /// Nobody is watching the ship: `ThreeKilometers`, a large distance filter,
  /// and CoreLocation allowed to pause the journey it thinks has ended.
  PPLocationAccuracyModeCoarse = 1,
};

@class PPLocationSource;

/// Everything arrives on the main thread. `CLLocationManager` delivers to the
/// run loop of the thread it was created on, this object is created on the
/// main thread, and the source does not hop, so a SwiftUI model can take
/// these calls directly.
@protocol PPLocationSourceDelegate <NSObject>

// Every name is pinned with NS_SWIFT_NAME rather than left to the importer's
// omit-needless-words rule, which drops a trailing type name from a selector
// piece and would rename these the day a parameter type is renamed.
- (void)locationSource:(PPLocationSource *)source
         didProduceFix:(PPFix *)fix
    NS_SWIFT_NAME(locationSource(_:didProduce:));

@optional
/// The user answered the permission sheet, or changed their mind in Settings.
- (void)locationSource:(PPLocationSource *)source
    didChangeAuthorization:(PPLocationAuthorization)authorization
    NS_SWIFT_NAME(locationSource(_:didChangeAuthorization:));

/// CoreLocation gave up on this attempt. Neither fatal nor rare, since a
/// phone indoors reports one and then starts working, so it is worth showing
/// rather than stopping for.
- (void)locationSource:(PPLocationSource *)source
      didFailWithError:(NSError *)error
    NS_SWIFT_NAME(locationSource(_:didFailWithError:));
@end

@interface PPLocationSource : NSObject

/// Main thread only, and not as a formality: it fixes the queue every later
/// callback arrives on.
- (instancetype)init NS_DESIGNATED_INITIALIZER;

@property(nonatomic, weak, nullable) id<PPLocationSourceDelegate> delegate;

/// What the user has said so far. `NotDetermined` until `start` asks.
@property(nonatomic, readonly) PPLocationAuthorization authorization;

/// Whether updates are wanted. True from the moment `start` is called,
/// including while the permission sheet is up: this reports the intent, not
/// the state of the hardware.
@property(nonatomic, readonly) BOOL running;

/// The last fix delivered, or nil. Kept because a shell starting the feed
/// after the map has drawn wants something to draw immediately, and because
/// the accuracy decision has to be re-taken when the camera moves, at a
/// moment when no new fix is arriving to take it with.
@property(nonatomic, readonly, nullable) PPFix *lastFix;

/// What the receiver is being asked for. `Navigation` at birth; the shell's
/// `LocationPolicy` writes it.
///
/// Setting it is live and cheap: CoreLocation applies `desiredAccuracy` and
/// `distanceFilter` to a running manager, so this does not stop and restart
/// updates and may be written on every fix. Writing the mode it is already in
/// does nothing, which makes it safe to drive from a gesture.
///
/// The one thing it does restart is a run of updates CoreLocation paused on
/// its own. `pausesLocationUpdatesAutomatically` is YES only in coarse mode,
/// and returning to navigation with the receiver parked would otherwise wait
/// for iOS to notice motion, which is the moment somebody asked to be
/// followed.
@property(nonatomic) PPLocationAccuracyMode accuracyMode;

/// Asks for permission if it has not been asked for, and starts updates as
/// soon as there is permission to start them. Calling it twice is harmless.
- (void)start;

/// Stops updates. The authorization is untouched: this is "not now", not
/// "never".
- (void)stop;

/// Whether the device has location services switched on at all, which is a
/// different question from whether this app may use them.
@property(class, nonatomic, readonly) BOOL locationServicesEnabled;

@end

NS_ASSUME_NONNULL_END
