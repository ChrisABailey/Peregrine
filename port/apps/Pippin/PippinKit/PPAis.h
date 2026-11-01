// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPAis.h — AIS input: the Open Waters stream, as `fv::ais` parses it, and a
// local receiver over TCP or UDP.
//
// The shell owns the HTTPS connection and hands each line of the event stream
// to `PPOpenWatersFrame`; frames go to `-[PPMap pushOpenWatersFrames:]`. The
// subscription box and the stream URL are chosen here. Those classes are
// immutable, so usable from any thread. `PPAisReceiver` owns its socket and is
// polled from one queue; its AIS lines go to `-[PPMap pushAisLines:heardAt:]`.

#import <Foundation/Foundation.h>
#import <PippinKit/PPFix.h>
#import <PippinKit/PPGeometry.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, PPOpenWatersFrameKind) {
  PPOpenWatersFrameKindWelcome = 0,
  PPOpenWatersFrameKindEvent = 1,
  PPOpenWatersFrameKindError = 2,
  PPOpenWatersFrameKindOther = 3,
};

/// The limits of the subscription tier, from the stream's `welcome` frame.
/// The default is the anonymous tier's.
NS_SWIFT_SENDABLE
@interface PPOpenWatersLimits : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (PPOpenWatersLimits *)anonymous;
/// Largest subscription box, square degrees.
@property(nonatomic, readonly) double areaSquareDegrees;
/// Messages per second before the server thins the stream.
@property(nonatomic, readonly) double messagesPerSecond;
@property(nonatomic, readonly) NSInteger connectsPerMinute;

/// The box to subscribe for `view`: padded by `pad` of its span on each side
/// and shrunk about its centre to the tier's area.
- (PPGeoBounds)subscriptionBoxForView:(PPGeoBounds)view pad:(double)pad
    NS_SWIFT_NAME(subscriptionBox(for:pad:));

/// Whether a stream subscribed to `box` still serves `view`.
- (BOOL)box:(PPGeoBounds)box servesView:(PPGeoBounds)view NS_SWIFT_NAME(box(_:serves:));
@end

/// One frame of the stream.
NS_SWIFT_SENDABLE
@interface PPOpenWatersFrame : NSObject
- (instancetype)init NS_UNAVAILABLE;

/// The frame on one line of the event stream; nil for a line carrying none
/// (a comment, a blank separator) or JSON that does not parse.
+ (nullable PPOpenWatersFrame *)frameWithLine:(NSString *)line NS_SWIFT_NAME(init(line:));

@property(nonatomic, readonly) PPOpenWatersFrameKind kind;
/// Set on a welcome frame.
@property(nonatomic, readonly, nullable) PPOpenWatersLimits *limits;
/// The server's message on an error frame; empty otherwise.
@property(nonatomic, readonly, copy) NSString *errorMessage;
/// The credit line the event's source requires; empty for other frames.
@property(nonatomic, readonly, copy) NSString *attribution;
@end

/// `https://ais.openwaters.io/v1/stream` for `box`, asking for the box's last
/// known state first when `snapshot` is set.
FOUNDATION_EXPORT NSURL *PPOpenWatersStreamURL(PPGeoBounds box, BOOL snapshot)
    NS_SWIFT_NAME(openWatersStreamURL(box:snapshot:));

/// A personal token from `POST /v1/keys`.
NS_SWIFT_SENDABLE
@interface PPOpenWatersToken : NSObject
- (instancetype)init NS_UNAVAILABLE;

/// The token in a mint response body; nil, with `error` set, when the body
/// carries none.
+ (nullable PPOpenWatersToken *)tokenWithResponse:(NSData *)body
                                            error:(NSString *_Nullable *_Nullable)error
    NS_SWIFT_NAME(init(response:error:));

/// Sent as `Authorization: Bearer`.
@property(nonatomic, readonly, copy) NSString *token;
/// The server confirmed it checked the request's signature.
@property(nonatomic, readonly) BOOL isSigned;
/// The tier the token grants, from its claims.
@property(nonatomic, readonly) PPOpenWatersLimits *limits;
@end

/// `https://ais.openwaters.io/v1/keys`, where a personal token is minted.
FOUNDATION_EXPORT NSURL *PPOpenWatersKeysURL(void) NS_SWIFT_NAME(openWatersKeysURL());

/// The bytes the install's Ed25519 key signs to mint a token. `pubkey` is the
/// raw public key, base64url without padding; `timestamp` is Unix seconds.
FOUNDATION_EXPORT NSData *PPOpenWatersMintMessage(NSString *pubkey, int64_t timestamp)
    NS_SWIFT_NAME(openWatersMintMessage(pubkey:timestamp:));

/// JSON body of the signed mint request; `signature` is base64url.
FOUNDATION_EXPORT NSData *PPOpenWatersMintBody(NSString *pubkey, int64_t timestamp,
                                               NSString *signature)
    NS_SWIFT_NAME(openWatersMintBody(pubkey:timestamp:signature:));

#pragma mark - Local receiver

typedef NS_ENUM(NSInteger, PPAisReceiverProtocol) {
  PPAisReceiverProtocolTCP = 0,
  PPAisReceiverProtocolUDP = 1,
};

typedef NS_ENUM(NSInteger, PPAisReceiverState) {
  PPAisReceiverStateOff = 0,
  /// Open, nothing heard yet.
  PPAisReceiverStateConnecting = 1,
  PPAisReceiverStateLive = 2,
  /// Open, nothing heard for 15 s.
  PPAisReceiverStateSilent = 3,
  /// Closed after a failure; reopens at `retryAt`.
  PPAisReceiverStateRetrying = 4,
};

/// What one `poll` read.
NS_SWIFT_SENDABLE
@interface PPAisReceiverBatch : NSObject
- (instancetype)init NS_UNAVAILABLE;
/// `!xxVDM` / `!xxVDO` sentences, TAG blocks kept.
@property(nonatomic, readonly, copy) NSArray<NSString *> *aisLines;
/// Position fixes from RMC/GGA/GLL/VTG, with an HDT heading attached when the
/// fix has no course or is slower than about a knot.
@property(nonatomic, readonly, copy) NSArray<PPFix *> *fixes;
@property(nonatomic, readonly) PPAisReceiverState state;
@property(nonatomic, readonly) BOOL stateChanged;
@end

/// An AIS receiver or boat network sending NMEA 0183: a TCP client of
/// `host`:`port`, or a unicast UDP listener on `port`. Reconnects a TCP stream
/// that ends, fails or is silent for 60 s, backing off from 2 s to 60 s. Not
/// thread safe: create, start, poll and stop it on one serial queue.
@interface PPAisReceiver : NSObject
- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithProtocol:(PPAisReceiverProtocol)protocol
                            host:(NSString *)host
                            port:(uint16_t)port NS_DESIGNATED_INITIALIZER;

/// Opens the connection at `now` (epoch seconds); a failure schedules a retry.
- (void)startAt:(NSTimeInterval)now NS_SWIFT_NAME(start(at:));
- (void)stop;
/// Reads what has arrived, reopening first when a retry is due.
- (PPAisReceiverBatch *)pollAt:(NSTimeInterval)now NS_SWIFT_NAME(poll(at:));

@property(nonatomic, readonly) PPAisReceiverState state;
/// Why the last connection closed; empty until one has.
@property(nonatomic, readonly, copy) NSString *lastError;
@property(nonatomic, readonly) NSTimeInterval retryAt;
/// "tcp:host:port" or "udp:*:port".
@property(nonatomic, readonly, copy) NSString *endpoint;
@property(nonatomic, readonly) NSInteger linesReceived;
@property(nonatomic, readonly) NSInteger aisLineCount;
@property(nonatomic, readonly) NSInteger fixCount;
@end

NS_ASSUME_NONNULL_END
