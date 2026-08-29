// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/app/search.h — "where is X", asked of every data source at once
// (port/search-plan-COMPLETE.md, S1).
//
// This is the SECOND aggregating capability, and it is deliberately not a
// flavour of the first. Pick (app/pick.h) is pixel-space: it needs a
// projection and a tolerance in pixels, it runs on every mouse move, and it
// asks only what is ON SCREEN, because a tap that landed on something the user
// cannot see is indistinguishable from a bug. Search is geo-space, on-demand,
// and its whole value is finding what is NOT on the screen — a road two
// counties away, a point in an overlay the user turned off an hour ago. Same
// pattern (an opt-in capability, an aggregating session, an ordering the
// CALLER chose), different contract, so it is a different interface.
//
// THE TWO QUERY SURFACES, independently optional and combinable:
//
//   * SPATIAL — a geographic box, or a point and a radius. "What is in this
//     area": the geo-space sibling of pick.
//   * TEXT — a match on the source's PRIMARY LABEL, without the caller ever
//     learning what that source calls the field. "Ruddy Turnstone" finds the
//     road in the OSM tiles AND the `.fvpoints` point of that name, and one of
//     those spells its label `name` while ENC spells it `OBJNAM`. The session
//     only ever sees `title`.
//
// WHERE THE FIELD-NAME KNOWLEDGE LIVES, and it is the design in one line: in
// the PROVIDER, never here and never in the caller. Whoever implements a
// source decides where its text search looks, which is the only arrangement
// that lets a source be added without every caller learning about it.
//
// WHAT IS SHARED INSTEAD is the MATCH RULE — `TextMatchQuality` below. A
// provider decides WHICH string to match; it does not get to decide what
// matching means, or "rud tur" would find the point and miss the road for
// reasons no user could ever discover.
//
// SEARCH IGNORES VISIBILITY BY DEFAULT (`visible_only`, and it is false),
// which is the exact opposite of pick's rule and is meant to be. "Where is X"
// is a legitimate question about a layer that is switched off, and the answer
// — "in Points, which is currently hidden" — is useful; the caller has the
// `overlay` pointer and can say so. It is also what lets a shell hold a
// not-visible overlay purely as a search provider while it draws that data
// its own way.
//
// SYNCHRONOUS BUT CANCELLABLE. A provider takes `const std::atomic<bool>&
// cancel`, polls it between units of work, and may return what it has. There
// is no async streaming in this API on purpose: it is the one thing that would
// be too complicated to implement over the OSM pyramid, and the single async
// behaviour an incremental search box actually needs is cancellation on the
// next keystroke. THREADING IS THE SHELL'S — Pippin's render queue, a desktop
// worker thread — and nothing in here starts one.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fvkit/geo.h"  // GeoPoint, GeoRect
#include "fvkit/overlay/overlay.h"

namespace fv {

class OverlayManager;

namespace app {

// ---------------------------------------------------------------------------
// The query
// ---------------------------------------------------------------------------

// How the merged answers are ranked. NOT a query grammar and never will be:
// "order by" later is one more value here, which is a thing every binding and
// every UI already knows how to render.
enum class SearchOrder {
  // Let the query decide: kBestMatch when `text` is set, kNearest when it is
  // not. Spatial-only searching has no match quality to rank by, and a text
  // search that ranked by distance alone would put a distant exact hit under
  // every near-miss beside the user.
  kAuto,
  // Match quality, then distance from `near`, then the provider's place in the
  // stack. The last of those is PickSession::Gather's trick: the walk is
  // topmost-first and the sort is stable, so stack order is the tie-break for
  // free and two runs of the same query never come back in different orders.
  kBestMatch,
  // Distance from `near` (or, with no `near`, from the centre of `area`),
  // then stack order. With neither, this degrades to stack order alone —
  // there is nothing to measure from, and inventing an origin would be worse
  // than admitting it.
  kNearest,
};

const char* ToString(SearchOrder o);

struct SearchQuery {
  // Spatial filter. A provider sees this and only this: the session folds
  // `near`+`radius_m` into it before the walk (see SearchSession::Search), so
  // a provider never implements a circle.
  std::optional<GeoRect> area;

  // Distance origin. With `radius_m` it is also a CUT; alone it is only an
  // ordering (find it anywhere, nearest to me first).
  std::optional<GeoPoint> near;
  double radius_m = 0.0;  // 0 with `near` set = no cut, ordering only

  // Empty = a spatial-only query. When it is set the contract is
  // `TextMatchQuality`'s: case-insensitive, token-prefix, so "rud tur" finds
  // Ruddy Turnstone.
  std::string text;

  // False — the deliberate opposite of pick. See the header note.
  bool visible_only = false;

  // The cap, and it means the same thing at both ends: no provider may append
  // more than this, and the session returns at most this many after ranking.
  // A caller asking for the 20 best results wants 20 results, not 20 from each
  // of four overlays; a provider is capped as well because the point of the
  // number is that nobody builds the bigger list in the first place. 0 is no
  // cap at all, at both ends — an export, not a search box.
  size_t max_results = 50;

  SearchOrder order = SearchOrder::kAuto;
};

// ---------------------------------------------------------------------------
// The answer
// ---------------------------------------------------------------------------

// PROVENANCE IS FLAT — `Overlay* + uint64_t`, exactly HitItem's shape, and
// that is a decision rather than an economy (search-plan-COMPLETE.md §3). A
// vector
// source names a feature with a 128-bit FeatureRef; minting that down to one
// uint64 is PRIVATE to whatever wraps the source, the way fvkit/vector/pick.h
// already mints ids for the ink it emitted. Nothing crossing this seam — no
// binding, no shell, no serialised bookmark — should have to hold a variant.
struct SearchResult {
  // The primary label, whatever the provider decided that is. Never empty in
  // practice: a provider with nothing to call a thing falls back the way
  // SnapToItem::description does, so no UI has to render a blank row.
  std::string title;
  // One line of "what kind of thing is this": "road · residential",
  // "point · restaurant", "waypoint · Kiawah loop". It is what tells two rows
  // with the same title apart, and it is also the poor man's category filter —
  // a caller can grep it. A real cross-product taxonomy is a separate design.
  std::string detail;
  // The representative point — where a label would be anchored, and what
  // distance ordering and the radius cut measure to.
  GeoPoint position;
  // What "go there" should frame. Degenerate (ll == ur) for a point, which is
  // a legitimate answer: the caller zooms to its own scale around it.
  GeoRect bounds;
  // 0 exact, 1 whole-string prefix, 2 token match. See TextMatchQuality; a
  // spatial-only query leaves every result at 0, which is what makes
  // kBestMatch degrade cleanly into "whatever the distance says".
  int match_quality = 0;

  Overlay* overlay = nullptr;
  uint64_t feature = 0;
};

// ---------------------------------------------------------------------------
// The capability
// ---------------------------------------------------------------------------

class SearchProvider {
 public:
  virtual ~SearchProvider() = default;

  // APPENDS to `out`, like every other aggregating capability here. The
  // contract in full:
  //
  //   * honour `q.area` if it is set, `q.text` if it is not empty, and
  //     `q.max_results` always;
  //   * poll `cancel` between units of work — a tile, a table page, a
  //     thousand features — and return early with what you have. A partial
  //     answer to a cancelled query is never shown to anyone, so it does not
  //     have to be a good one; what matters is that the call RETURNS;
  //   * leave `overlay` alone if you like. The session stamps it, because the
  //     walk knows whose turn it is.
  //
  // Not const: minting a feature id is a mutation (RouteOverlay does exactly
  // that), and a provider that wants to cache an index has nowhere else to
  // put it.
  virtual void Search(const SearchQuery& q, const std::atomic<bool>& cancel,
                      std::vector<SearchResult>& out) = 0;
};

// ---------------------------------------------------------------------------
// The shared match rule
// ---------------------------------------------------------------------------

// How well `candidate` answers `query`, or -1 for not at all:
//
//   0  the whole string, case-insensitively ("ruddy turnstone")
//   1  a prefix of the whole string ("ruddy tur")
//   2  every query token is a prefix of some candidate token, in any order
//      ("rud tur", "turnstone rud")
//
// An EMPTY query matches everything at quality 0 — a spatial-only search is
// not a text search that everything happens to fail.
//
// Case folding is ASCII, and that is a stated limit rather than an oversight:
// a correct Unicode fold is ICU, which is a dependency this port does not
// carry, and the fallback everyone reaches for (tolower per byte over UTF-8)
// silently corrupts multi-byte sequences. Bytes above 0x7F therefore compare
// exactly, so a name matches itself in any script and "Ångström" does not
// match "ångström". S3's FTS5 index has the same property for the same reason.
int TextMatchQuality(const std::string& query, const std::string& candidate);

// The two lines a ten-line provider actually writes. `SearchTextAccepts` is
// TextMatchQuality plus the empty-query case, and it writes the quality
// straight into the result the provider is about to append.
bool SearchTextAccepts(const SearchQuery& q, const std::string& label,
                       int* out_quality);
// True when `p` is inside `q.area`, or when there is no area. Providers see
// only the box — the circle is the session's business.
bool SearchAreaAccepts(const SearchQuery& q, const GeoPoint& p);

// ---------------------------------------------------------------------------
// The session — the ONLY discovery path
// ---------------------------------------------------------------------------

// Walks the overlay stack, asks everything that answers `AsSearch()`, ranks
// the union, and returns it. There is no second registry of providers for
// data that is not an overlay: a map source that wants to be searchable is
// wrapped in an overlay (S2's `VectorMapOverlay`), which is also how it gets
// picked, drawn and put in the stack later.
//
// ORDERING IS THE SESSION'S, NEVER A PROVIDER'S. Only the aggregator sees all
// the streams, and N implementations must not each compute a distance their
// own way — the metre here is the one port/Routing and fvkit/nav/road_snap.h
// measure in, so a "within 500 m" search and a road snap agree about 500 m.
//
// It holds no state about the stack: the walk is re-done per call, so any
// amount of adding, removing and reordering happens underneath it with
// nothing to invalidate. Unlike PickSession it needs NO AppShell — a search
// asks the user nothing.
class SearchSession {
 public:
  // Not owned; must outlive the session.
  explicit SearchSession(const OverlayManager& manager);

  // WHO IS ASKED. With `visible_only` false (the default) it is every overlay
  // in the stack, topmost first, hidden and decluttered ones included. With it
  // true, it is exactly the pick layer's set: OverlayManager::DrawOrder()
  // reversed, so a search and a tap agree about what is on the screen.
  std::vector<SearchResult> Search(const SearchQuery& q) const;

  // The same, cancellable. `cancel` is polled between providers as well as
  // being handed to each one, and a cancelled search returns the results
  // gathered so far, RANKED — a caller that decides to show a partial answer
  // gets a sensible one, and a caller that throws it away pays nothing.
  std::vector<SearchResult> Search(const SearchQuery& q,
                                   const std::atomic<bool>& cancel) const;

 private:
  const OverlayManager& manager_;
};

// The metres between two points, on the tangent plane through the first —
// exposed because the session's ordering and a caller's own "how far is that"
// must not be two different numbers. Same metre as
// fvkit/nav/road_snap.h's, which is the same one the road graph is built in.
double SearchDistanceMeters(const GeoPoint& a, const GeoPoint& b);

}  // namespace app
}  // namespace fv
