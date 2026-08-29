// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_osm_name_index.h — the gazetteer that lives INSIDE the pack
// (port/search-plan-COMPLETE.md, S3).
//
// WHY IT IS IN THE .MBTILES AND NOT BESIDE IT. An MBTiles file is pure SQLite,
// extra tables are legal, and every other reader ignores what it does not
// know: a pack with an index is still a pack, opens in tippecanoe's tooling
// and in any tile server, and cannot be separated from its index by a copy, a
// download or an AirDrop to a phone. A sidecar file would be one more thing to
// ship, one more thing to lose, and one more way for a name to be looked up in
// a gazetteer of a DIFFERENT cut than the tiles being drawn.
//
// WHAT IS IN IT: the rows the tier-1 scan would have produced, precomputed.
// The builder (port/Osm/tools/fvnames.cpp) walks the pyramid through the very
// same `VectorMapOverlay`, so the index cannot hold a different idea of what a
// row is than the live scan does — same label tags, same "unnamed geometry is
// never a row", same merge (fvkit/vector/feature_rows.h).
//
// THE SCHEMA, and the two decisions in it:
//
//   search_names        one row per named thing: the name, its layer and
//                       class, its label anchor, its box, the shallowest zoom
//                       it survives to, and the TILE + FEATURE INDEX it can be
//                       identified from.
//   search_names_fts    an external-content FTS5 mirror of `name` alone. The
//                       text is not stored twice; the mirror is a candidate
//                       generator and the port's own rule
//                       (app::TextMatchQuality) still decides.
//   search_names_meta   what built it, when, from which label tags and zooms
//                       — so a pack can say why its answers look the way they
//                       do, and so a rebuild can be told from a stale index.
//
//   1. A REF IS NOT DURABLE, so the index does not store one. `FeatureRef`'s
//      `tile` is an index assigned by the open source on first use; writing it
//      down would name a different tile in the next process. The index stores
//      z/x/y and the layer NAME, and the source turns them back into a live
//      ref on the way out.
//   2. min_zoom IS THE RELEVANCE FUNCTION, precomputed. Tier 1 gets its
//      ranking for free from the tile budget stepping the zoom coarser; a
//      global query has no budget to step, so the same fact is stored instead:
//      how shallow a level the cutter kept this name at. Ordering by it means
//      a capped answer to "char" is Charleston before Charleston Court, which
//      is the only reason a cap on a pack-wide search is tolerable at all.
//
// FTS5 IS AN OPTIMISATION, NOT A REQUIREMENT. `search_names` is a plain table,
// so a reader whose SQLite was built without the FTS5 module (or a pack whose
// mirror was dropped) still answers — by scanning the table and applying the
// port's rule in C++, which for a pack-sized gazetteer is milliseconds. The
// reader falls back on its own and says which path it took.

#ifndef FV_OSM_NAME_INDEX_H_
#define FV_OSM_NAME_INDEX_H_

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fv_web_mercator.h"
#include "fvkit/geo.h"

namespace fv {
namespace osm {

// Bumped when the schema changes in a way a reader must notice. A reader that
// meets a NEWER version refuses the index and degrades to the tile scan rather
// than guessing at columns it does not know.
constexpr int kNameIndexVersion = 1;

// One indexed name. The geometry is a box plus an anchor, never the geometry
// itself: the pack already holds that, and the ref below is how to go back to
// it.
struct NameRow {
  std::string name;
  std::string layer;
  std::string style_key;  // OpenMapTiles `class`, the source's own sub-type
  GeoPoint position{};    // label anchor
  GeoRect bounds{};       // degenerate for a point
  int min_zoom = 0;       // shallowest level the name appears at
  webmerc::TileId tile{};  // the tile the identified feature was read from
  int feature = 0;         // raw MVT feature index within (tile, layer)
};

// Builds the index into an existing pack, in one transaction: either the pack
// gains a complete index or it is left exactly as it was.
class NameIndexWriter {
 public:
  NameIndexWriter();
  ~NameIndexWriter();
  NameIndexWriter(const NameIndexWriter&) = delete;
  NameIndexWriter& operator=(const NameIndexWriter&) = delete;

  // Opens the pack READ-WRITE. This is the one door in the port through which
  // a published pyramid is written to, which is why it is not MbtilesFile's.
  Status Open(const std::string& mbtiles_path);
  void Close();
  bool IsOpen() const;

  // Drops any existing index and creates the tables, then opens a transaction.
  // `fts` asks for the FTS5 mirror; if this SQLite has no FTS5 the index is
  // still built without it (see fts_built()).
  Status Begin(bool fts = true);
  Status Add(const NameRow& row);
  // Writes `meta` (key/value, verbatim) and commits. Rolls back on failure.
  Status Commit(const std::vector<std::pair<std::string, std::string>>& meta);
  // Removes every table this class creates. Safe on a pack with no index.
  Status DropIndex();

  bool fts_built() const;
  int64_t rows_written() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Reads it back. Opens READ-ONLY, on its own connection: the pack is already
// open elsewhere for tiles, and two read-only handles on one SQLite file cost
// a file descriptor and nothing else.
class NameIndexReader {
 public:
  NameIndexReader();
  ~NameIndexReader();
  NameIndexReader(const NameIndexReader&) = delete;
  NameIndexReader& operator=(const NameIndexReader&) = delete;

  // kNotFound when the pack has no index (the everyday case — every pack cut
  // before this existed), kUnsupported when it has one this reader is too old
  // to read.
  Status Open(const std::string& mbtiles_path);
  void Close();
  bool IsOpen() const;

  // `text` is token-prefix as fvkit/app/search.h means it; EMPTY lists
  // everything (an export). `area`, when given, is a box cut applied in SQL —
  // a box that crosses the antimeridian is NOT cut here, and the caller's own
  // test does that work. `max` of 0 is no cap.
  //
  // ROWS COME BACK MOST PROMINENT FIRST (min_zoom, then name), so a cap keeps
  // the answers worth keeping.
  Status Query(const std::string& text, const GeoRect* area, size_t max,
               std::vector<NameRow>* out) const;

  // Metadata, verbatim; empty for a key the builder did not write.
  std::string Meta(const std::string& key) const;
  int version() const;
  int64_t row_count() const;
  // Whether the FTS5 mirror is present AND usable by this SQLite. False means
  // queries scan `search_names` and match in C++ — same answers, more work.
  bool uses_fts() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

struct NameIndexBuildOptions {
  // Zoom range to scan; < 0 means "every level the pack has". EVERY level is
  // the default because that is where min_zoom comes from — a name is written
  // down with the shallowest level it was still drawn at, and a build that
  // read only the bottom of the pyramid would rank a city with a cul-de-sac.
  int min_zoom = -1;
  int max_zoom = -1;
  // Tiles per side of the block scanned per query. 8 (a 64-tile block) is the
  // tile cache's own size, so a block is decoded once.
  int window = 8;
  // The merge, exactly as VectorMapOverlay means it.
  double merge_gap_m = 100.0;
  // Empty = the overlay's default chain. Whatever is used is written into the
  // pack, so a reader can see which tag became a name.
  std::vector<std::string> label_tags;
  bool fts = true;
};

struct NameIndexBuildStats {
  int64_t tiles_scanned = 0;
  int64_t pieces = 0;      // rows before the whole-pack merge
  int64_t names = 0;       // rows written
  int64_t unresolved = 0;  // rows whose ref named no tile (never seen in the wild)
  bool fts = false;
};

// Scans the pyramid through VectorMapOverlay — the same class a live search
// asks — and writes the result into the pack. The pack is opened for reading,
// scanned, CLOSED, and only then opened for writing: a pack being read cannot
// be written to (see MbtilesFile::ReadTile), and the failure that gives is a
// "disk I/O error" rather than anything that looks like a lock.
//
// `on_zoom` (optional) is called after each level with (zoom, tiles at that
// zoom, rows found there) — the CLI's progress line, and nothing depends on it.
Status BuildNameIndex(const std::string& mbtiles_path,
                      const NameIndexBuildOptions& options,
                      NameIndexBuildStats* stats,
                      const std::function<void(int, int64_t, size_t)>& on_zoom);

}  // namespace osm
}  // namespace fv

#endif  // FV_OSM_NAME_INDEX_H_
