// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPAis+Internal.h"
#import "PPFix+Internal.h"

#include "fvkit/ais/receiver.h"

#include <string>
#include <string_view>

namespace {

fv::GeoRect GeoRectFrom(PPGeoBounds b) {
  return fv::GeoRect{{b.southWest.latitude, b.southWest.longitude},
                     {b.northEast.latitude, b.northEast.longitude}};
}

PPGeoBounds BoundsFrom(const fv::GeoRect& r) {
  PPGeoBounds b;
  b.southWest = PPGeoPointMake(r.ll.lat, r.ll.lon);
  b.northEast = PPGeoPointMake(r.ur.lat, r.ur.lon);
  return b;
}

NSString* NSStr(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding]
             ?: @"";
}

}  // namespace

@implementation PPOpenWatersLimits {
  fv::ais::OpenWatersLimits _limits;
}

- (instancetype)initWithLimits:(const fv::ais::OpenWatersLimits&)limits {
  self = [super init];
  if (self != nil) _limits = limits;
  return self;
}

+ (PPOpenWatersLimits*)anonymous {
  return [[PPOpenWatersLimits alloc] initWithLimits:fv::ais::OpenWatersLimits{}];
}

- (double)areaSquareDegrees { return _limits.area_deg2; }
- (double)messagesPerSecond { return _limits.rate_per_s; }
- (NSInteger)connectsPerMinute { return _limits.connects_per_min; }

- (PPGeoBounds)subscriptionBoxForView:(PPGeoBounds)view pad:(double)pad {
  return BoundsFrom(fv::ais::OpenWatersBox(GeoRectFrom(view), _limits, pad));
}

- (BOOL)box:(PPGeoBounds)box servesView:(PPGeoBounds)view {
  return fv::ais::OpenWatersBoxServes(GeoRectFrom(box), GeoRectFrom(view), _limits) ? YES : NO;
}

@end

@implementation PPOpenWatersFrame {
  fv::ais::OpenWatersFrame _frame;
  PPOpenWatersLimits* _limits;
}

+ (nullable PPOpenWatersFrame*)frameWithLine:(NSString*)line {
  const char* utf8 = line.UTF8String;
  if (utf8 == nullptr) return nil;
  const std::string_view json = fv::ais::OpenWatersSsePayload(utf8);
  if (json.empty()) return nil;
  PPOpenWatersFrame* f = [[PPOpenWatersFrame alloc] initPrivate];
  if (!fv::ais::ParseOpenWatersFrame(json, &f->_frame)) return nil;
  if (f->_frame.kind == fv::ais::OpenWatersFrameKind::kWelcome)
    f->_limits = [[PPOpenWatersLimits alloc] initWithLimits:f->_frame.limits];
  return f;
}

- (instancetype)initPrivate {
  return [super init];
}

- (const fv::ais::OpenWatersFrame&)frame { return _frame; }

- (PPOpenWatersFrameKind)kind {
  switch (_frame.kind) {
    case fv::ais::OpenWatersFrameKind::kWelcome: return PPOpenWatersFrameKindWelcome;
    case fv::ais::OpenWatersFrameKind::kEvent: return PPOpenWatersFrameKindEvent;
    case fv::ais::OpenWatersFrameKind::kError: return PPOpenWatersFrameKindError;
    case fv::ais::OpenWatersFrameKind::kOther: return PPOpenWatersFrameKindOther;
  }
  return PPOpenWatersFrameKindOther;
}

- (nullable PPOpenWatersLimits*)limits { return _limits; }
- (NSString*)errorMessage { return NSStr(_frame.error); }
- (NSString*)attribution { return NSStr(_frame.attribution); }

@end

NSURL* PPOpenWatersStreamURL(PPGeoBounds box, BOOL snapshot) {
  return [NSURL URLWithString:NSStr(fv::ais::OpenWatersStreamUrl(GeoRectFrom(box), snapshot))];
}

@implementation PPOpenWatersToken {
  fv::ais::OpenWatersToken _token;
  PPOpenWatersLimits* _limits;
}

+ (nullable PPOpenWatersToken*)tokenWithResponse:(NSData*)body
                                           error:(NSString* _Nullable* _Nullable)error {
  PPOpenWatersToken* t = [[PPOpenWatersToken alloc] initPrivate];
  std::string why;
  const std::string_view json(static_cast<const char*>(body.bytes), body.length);
  if (!fv::ais::ParseOpenWatersToken(json, &t->_token, &why)) {
    if (error != nullptr) *error = NSStr(why);
    return nil;
  }
  t->_limits = [[PPOpenWatersLimits alloc] initWithLimits:t->_token.limits];
  return t;
}

- (instancetype)initPrivate {
  return [super init];
}

- (NSString*)token { return NSStr(_token.token); }
- (BOOL)isSigned { return _token.is_signed ? YES : NO; }
- (PPOpenWatersLimits*)limits { return _limits; }

@end

NSURL* PPOpenWatersKeysURL(void) {
  return [NSURL URLWithString:@(fv::ais::kOpenWatersKeysUrl)];
}

NSData* PPOpenWatersMintMessage(NSString* pubkey, int64_t timestamp) {
  const std::string m = fv::ais::OpenWatersMintMessage(pubkey.UTF8String ?: "", timestamp);
  return [NSData dataWithBytes:m.data() length:m.size()];
}

NSData* PPOpenWatersMintBody(NSString* pubkey, int64_t timestamp, NSString* signature) {
  const std::string b =
      fv::ais::OpenWatersMintBody(pubkey.UTF8String ?: "", timestamp, signature.UTF8String ?: "");
  return [NSData dataWithBytes:b.data() length:b.size()];
}

#pragma mark - Local receiver

namespace {

PPAisReceiverState StateFrom(fv::ais::ReceiverState s) {
  switch (s) {
    case fv::ais::ReceiverState::kOff: return PPAisReceiverStateOff;
    case fv::ais::ReceiverState::kConnecting: return PPAisReceiverStateConnecting;
    case fv::ais::ReceiverState::kLive: return PPAisReceiverStateLive;
    case fv::ais::ReceiverState::kSilent: return PPAisReceiverStateSilent;
    case fv::ais::ReceiverState::kRetrying: return PPAisReceiverStateRetrying;
  }
  return PPAisReceiverStateOff;
}

/// A receiver fix as CoreLocation would have reported it. Accuracy comes from
/// HDOP (5 m per unit, one unit when the receiver sent none); the timestamp is
/// the receiver's own, else `now`.
PPLocationSample SampleFrom(const fv::PositionFix& f, double now_s) {
  PPLocationSample s;
  if (f.has_position) {
    s.latitude = f.lat;
    s.longitude = f.lon;
    s.horizontal_accuracy_m = (f.has_hdop ? f.hdop : 1.0) * kPPHdopMetresPerUnit;
  }
  if (f.has_altitude) {
    s.altitude_m = f.altitude_msl_m;
    s.vertical_accuracy_m = 1.5 * s.horizontal_accuracy_m;
  }
  if (f.has_true_heading) s.course_deg = fv::NormalizeHeadingDeg(f.true_heading_deg);
  if (f.has_speed) s.speed_mps = f.speed_mps;
  s.timestamp_s = f.has_time ? f.time_s : now_s;
  s.has_timestamp = true;
  return s;
}

}  // namespace

@interface PPAisReceiverBatch ()
- (instancetype)initWithLines:(NSArray<NSString*>*)lines
                        fixes:(NSArray<PPFix*>*)fixes
                        state:(PPAisReceiverState)state
                      changed:(BOOL)changed NS_DESIGNATED_INITIALIZER;
@end

@implementation PPAisReceiverBatch

- (instancetype)initWithLines:(NSArray<NSString*>*)lines
                        fixes:(NSArray<PPFix*>*)fixes
                        state:(PPAisReceiverState)state
                      changed:(BOOL)changed {
  self = [super init];
  if (self != nil) {
    _aisLines = [lines copy];
    _fixes = [fixes copy];
    _state = state;
    _stateChanged = changed;
  }
  return self;
}

@end

@implementation PPAisReceiver {
  std::unique_ptr<fv::ais::NmeaReceiver> _receiver;
}

- (instancetype)initWithProtocol:(PPAisReceiverProtocol)protocol
                            host:(NSString*)host
                            port:(uint16_t)port {
  self = [super init];
  if (self != nil) {
    fv::ais::ReceiverConfig config;
    config.protocol = protocol == PPAisReceiverProtocolUDP ? fv::ais::ReceiverProtocol::kUdp
                                                           : fv::ais::ReceiverProtocol::kTcp;
    config.host = host.UTF8String ?: "";
    config.port = port;
    _receiver = std::make_unique<fv::ais::NmeaReceiver>(fv::ais::NmeaReceiver::FactoryFor(config));
  }
  return self;
}

- (void)startAt:(NSTimeInterval)now { _receiver->Start(now); }
- (void)stop { _receiver->Stop(); }

- (PPAisReceiverBatch*)pollAt:(NSTimeInterval)now {
  fv::ais::ReceiverBatch batch;
  _receiver->Poll(now, &batch);
  NSMutableArray<NSString*>* lines = [NSMutableArray arrayWithCapacity:batch.ais_lines.size()];
  for (const std::string& l : batch.ais_lines) [lines addObject:NSStr(l)];
  NSMutableArray<PPFix*>* fixes = [NSMutableArray arrayWithCapacity:batch.fixes.size()];
  for (const fv::PositionFix& f : batch.fixes) {
    if (!f.has_position) continue;
    [fixes addObject:[[PPFix alloc] initWithSample:SampleFrom(f, now)]];
  }
  return [[PPAisReceiverBatch alloc] initWithLines:lines
                                             fixes:fixes
                                             state:StateFrom(_receiver->state())
                                           changed:batch.state_changed ? YES : NO];
}

- (PPAisReceiverState)state { return StateFrom(_receiver->state()); }
- (NSString*)lastError { return NSStr(_receiver->last_error()); }
- (NSTimeInterval)retryAt { return _receiver->retry_at_s(); }
- (NSString*)endpoint { return NSStr(_receiver->description()); }
- (NSInteger)linesReceived { return static_cast<NSInteger>(_receiver->lines_received()); }
- (NSInteger)aisLineCount { return static_cast<NSInteger>(_receiver->ais_lines()); }
- (NSInteger)fixCount { return static_cast<NSInteger>(_receiver->fixes_emitted()); }

@end
