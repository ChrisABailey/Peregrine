// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// Open Waters AIS stream (https://openwaters.io/api/ais/): parses the JSON
/// frames of the server-sent-event stream and chooses the subscription box
/// for a view. No I/O; the shell owns the HTTPS connection (AIS-D2).
/// Plan: port/ais-plan.md (AIS4).

#ifndef FVKIT_AIS_OPENWATERS_H_
#define FVKIT_AIS_OPENWATERS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fvkit/ais/decoder.h"
#include "fvkit/geo.h"

namespace fv {
namespace ais {

/// Limits the server states in its `welcome` frame. Defaults are the
/// anonymous tier's.
struct OpenWatersLimits {
  double area_deg2 = 100.0;   ///< largest subscription box, square degrees
  double rate_per_s = 20.0;   ///< messages per second before the stream thins
  int connections = 2;        ///< concurrent streams per address
  int connects_per_min = 20;
};

enum class OpenWatersFrameKind { kWelcome, kEvent, kError, kOther };

/// One `data:` payload of the stream.
struct OpenWatersFrame {
  OpenWatersFrameKind kind = OpenWatersFrameKind::kOther;

  // kWelcome.
  std::string role;  ///< "anonymous", …
  OpenWatersLimits limits;

  // kError.
  std::string error;

  // kEvent.
  std::uint32_t mmsi = 0;
  std::string msg_type;  ///< "PositionReport", "ShipStaticData", …
  double time_s = 0.0;   ///< reception time, epoch seconds; 0 when absent
  bool synthesized = false;  ///< rebuilt from the server's snapshot
  std::string source;       ///< "aishub", "barentswatch", "station:…"
  std::string license;      ///< "NLOD-2.0", …
  std::string attribution;  ///< the credit line the source requires
  std::string station;
  std::vector<std::string> nmea;  ///< raw `!AIVDM` sentences, all parts in order
  /// Built from the decoded `message` object when `nmea` is empty (AIS-D1).
  bool has_fallback = false;
  AisMessage fallback;
};

/// The JSON after "data:" in an SSE line, trimmed; empty for any other line
/// (comments, `event:`, blank separators).
std::string_view OpenWatersSsePayload(std::string_view line);

/// Parses one frame. False, with `error` set, when `json` is not a JSON object.
bool ParseOpenWatersFrame(std::string_view json, OpenWatersFrame* out,
                          std::string* error = nullptr);

/// The box to subscribe for `view`: the view grown by `pad_fraction` of its
/// span on every side, shrunk about its centre to fit `limits.area_deg2`
/// (with a 2% margin for rounding in the URL), and clipped to the world. A
/// view crossing the antimeridian is cut at 180°.
GeoRect OpenWatersBox(const GeoRect& view, const OpenWatersLimits& limits,
                      double pad_fraction = 0.25);

/// Whether a stream subscribed to `box` still serves `view`. A view that fits
/// is served when inside the box. A view larger than the tier allows is served
/// while its centre stays in the middle half of the box, so panning a
/// zoomed-out chart does not reconnect on every move.
bool OpenWatersBoxServes(const GeoRect& box, const GeoRect& view,
                         const OpenWatersLimits& limits);

/// `https://ais.openwaters.io/v1/stream?bbox=minLat,minLon,maxLat,maxLon`,
/// with `&snapshot=1` to receive the last known state of the box first.
std::string OpenWatersStreamUrl(const GeoRect& box, bool snapshot);

/// `POST` here mints a personal token.
extern const char kOpenWatersKeysUrl[];

/// The bytes an Ed25519 key signs to mint a token: six lines (`aiscast-key`,
/// pubkey, ts, bind_ip flag, name, vessel_name) joined by '\n', no trailing
/// newline. `pubkey` is the raw 32-byte key, base64url without padding.
std::string OpenWatersMintMessage(std::string_view pubkey, std::int64_t ts, bool bind_ip = false,
                                  std::string_view name = {}, std::string_view vessel_name = {});

/// JSON body of a signed mint request that names no station. `ts` is Unix
/// seconds; `sig` is the signature of `OpenWatersMintMessage`, base64url.
std::string OpenWatersMintBody(std::string_view pubkey, std::int64_t ts, std::string_view sig);

/// A personal token as `POST /v1/keys` returns it.
struct OpenWatersToken {
  std::string token;  ///< sent as `Authorization: Bearer`
  std::string sub;    ///< the station the token names
  std::string role;   ///< "personal"
  double exp_s = 0;   ///< expiry, epoch seconds; 0 when it never expires
  bool is_signed = false;  ///< the server checked the request's signature
  std::string name_error;  ///< why a requested name was refused; the token is still issued
  OpenWatersLimits limits;  ///< from the claims; absent fields keep the defaults
};

/// Parses a mint response. False, with `error` set, when it carries no token.
bool ParseOpenWatersToken(std::string_view json, OpenWatersToken* out,
                          std::string* error = nullptr);

}  // namespace ais
}  // namespace fv

#endif  // FVKIT_AIS_OPENWATERS_H_
