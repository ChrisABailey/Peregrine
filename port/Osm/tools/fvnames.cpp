// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvnames — build, inspect and query the name index INSIDE an .mbtiles pack
// (port/search-plan-COMPLETE.md, S3). A host tool, in the fvpack/fvgraph
// pattern: it
// makes the data a phone reads.
//
//   fvnames build  PACK.mbtiles [--min-zoom N] [--max-zoom N] [--gap-m M]
//                  [--label-tags a,b,c] [--window N] [--no-fts] [-q]
//   fvnames info   PACK.mbtiles
//   fvnames search PACK.mbtiles TEXT [--max N] [--bbox S,W,N,E] [--scan]
//   fvnames drop   PACK.mbtiles
//
// HOW THE BUILD WORKS, and why it is so short: it does not read tiles. It asks
// `VectorMapOverlay` — the very class a running search asks — for the named
// things in one window of the pyramid at a time, and writes down what it says.
// Which tag is the label, that unnamed geometry is never a row, where a road's
// anchor sits and which pieces are one road are therefore decided ONCE, in
// fvkit, for the live scan and the index alike. The only thing this file
// decides is which windows to ask about and what to store.
//
// THE ONE THING IT ADDS is min_zoom: every zoom the pack holds is scanned,
// shallowest first, and a name's row remembers the shallowest level it
// survived to. That is the cutter's own opinion of how important a name is,
// paid for already, and it is what makes a capped pack-wide search return
// Charleston rather than an arbitrary fifty Charleston Courts.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fv_osm_name_index.h"
#include "fv_osm_vector_source.h"
#include "fvkit/app/search.h"
#include "fvkit/overlay/vector_map_overlay.h"

namespace {

using fv::GeoRect;
using fv::Status;

int Usage() {
  std::fprintf(
      stderr,
      "usage:\n"
      "  fvnames build  PACK.mbtiles [--min-zoom N] [--max-zoom N]\n"
      "                 [--gap-m M] [--label-tags a,b,c] [--window N]\n"
      "                 [--no-fts] [-q]\n"
      "  fvnames info   PACK.mbtiles\n"
      "  fvnames search PACK.mbtiles TEXT [--max N] [--bbox S,W,N,E] [--scan]\n"
      "  fvnames drop   PACK.mbtiles\n"
      "\n"
      "  build writes a `search_names` table INTO the pack (extra SQLite\n"
      "  tables are invisible to every other MBTiles reader), so a pack and\n"
      "  its gazetteer cannot be separated by a copy or a download.\n"
      "  --window is the tile block scanned per query (default 8, so 8x8=64\n"
      "  tiles, which is the tile cache's size).\n"
      "  --label-tags is the tag chain a name is read from, first present\n"
      "  wins; it is written into the pack so a reader can see what built it.\n"
      "  search --scan forces the tile scan, which is what the index is\n"
      "  supposed to agree with.\n");
  return 2;
}

std::vector<std::string> SplitCommas(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= s.size()) {
    const size_t c = s.find(',', i);
    const size_t end = c == std::string::npos ? s.size() : c;
    if (end > i) out.push_back(s.substr(i, end - i));
    if (c == std::string::npos) break;
    i = c + 1;
  }
  return out;
}

bool ParseBbox(const char* text, GeoRect* out) {
  const std::vector<std::string> parts = SplitCommas(text);
  if (parts.size() != 4) return false;
  out->ll.lat = std::atof(parts[0].c_str());
  out->ll.lon = std::atof(parts[1].c_str());
  out->ur.lat = std::atof(parts[2].c_str());
  out->ur.lon = std::atof(parts[3].c_str());
  return true;
}

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------

int Build(int argc, char** argv) {
  if (argc < 1) return Usage();
  const std::string path = argv[0];
  fv::osm::NameIndexBuildOptions options;
  bool quiet = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "fvnames: %s needs a value\n", what);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--min-zoom") options.min_zoom = std::atoi(next("--min-zoom"));
    else if (a == "--max-zoom") options.max_zoom = std::atoi(next("--max-zoom"));
    else if (a == "--window") options.window = std::atoi(next("--window"));
    else if (a == "--gap-m") options.merge_gap_m = std::atof(next("--gap-m"));
    else if (a == "--label-tags")
      options.label_tags = SplitCommas(next("--label-tags"));
    else if (a == "--no-fts") options.fts = false;
    else if (a == "-q" || a == "--quiet") quiet = true;
    else return Usage();
  }

  fv::osm::NameIndexBuildStats stats;
  const Status s = fv::osm::BuildNameIndex(
      path, options, &stats,
      quiet ? std::function<void(int, int64_t, size_t)>()
            : [](int z, int64_t tiles, size_t rows) {
                std::printf("z%-2d %6lld tiles -> %zu rows\n", z,
                            static_cast<long long>(tiles), rows);
                std::fflush(stdout);
              });
  if (!s.ok()) {
    std::fprintf(stderr, "fvnames: %s\n", s.message.c_str());
    return 1;
  }
  if (stats.unresolved != 0) {
    std::fprintf(stderr, "fvnames: %lld rows had no resolvable tile\n",
                 static_cast<long long>(stats.unresolved));
  }
  std::printf("%lld pieces -> %lld names%s\n",
              static_cast<long long>(stats.pieces),
              static_cast<long long>(stats.names),
              stats.fts ? " (with FTS5)" : " (no FTS5 in this SQLite)");
  return 0;
}

// ---------------------------------------------------------------------------
// info / search / drop
// ---------------------------------------------------------------------------

int Info(int argc, char** argv) {
  if (argc != 1) return Usage();
  fv::osm::NameIndexReader reader;
  const Status s = reader.Open(argv[0]);
  if (!s.ok()) {
    std::fprintf(stderr, "fvnames: %s\n", s.message.c_str());
    return 1;
  }
  std::printf("names   %lld\n", static_cast<long long>(reader.row_count()));
  std::printf("version %d\n", reader.version());
  std::printf("lookup  %s\n", reader.uses_fts() ? "FTS5" : "table scan");
  for (const char* key : {"built", "builder", "label_tags", "merge_gap_m",
                          "zoom_min", "zoom_max", "tiles_scanned"}) {
    const std::string v = reader.Meta(key);
    if (!v.empty()) std::printf("%-8s %s\n", key, v.c_str());
  }
  return 0;
}

int Search(int argc, char** argv) {
  if (argc < 2) return Usage();
  const std::string path = argv[0];
  std::string text = argv[1];
  size_t max = 20;
  bool scan = false;
  GeoRect box;
  bool have_box = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--max" && i + 1 < argc) max = std::strtoul(argv[++i], nullptr, 10);
    else if (a == "--bbox" && i + 1 < argc) have_box = ParseBbox(argv[++i], &box);
    else if (a == "--scan") scan = true;
    else return Usage();
  }

  auto source = std::make_shared<fv::OsmVectorSource>();
  Status s = source->Open(path);
  if (!s.ok()) {
    std::fprintf(stderr, "fvnames: %s\n", s.message.c_str());
    return 1;
  }
  fv::VectorMapOverlay overlay("pack", source);
  overlay.SetUseNameIndex(!scan);

  fv::app::SearchQuery q;
  q.text = text;
  q.max_results = max;
  if (have_box) q.area = box;
  std::vector<fv::app::SearchResult> hits;
  const std::atomic<bool> never{false};
  overlay.Search(q, never, hits);

  std::printf("%zu result(s) from the %s\n", hits.size(),
              overlay.last_search_used_index() ? "index" : "tiles");
  for (const fv::app::SearchResult& r : hits) {
    std::printf("  %-40s %-28s %10.6f %11.6f  q%d\n", r.title.c_str(),
                r.detail.c_str(), r.position.lat, r.position.lon,
                r.match_quality);
  }
  return hits.empty() ? 1 : 0;
}

int Drop(int argc, char** argv) {
  if (argc != 1) return Usage();
  fv::osm::NameIndexWriter writer;
  Status s = writer.Open(argv[0]);
  if (s.ok()) s = writer.DropIndex();
  if (!s.ok()) {
    std::fprintf(stderr, "fvnames: %s\n", s.message.c_str());
    return 1;
  }
  // No VACUUM: on a multi-gigabyte pack it costs more than the pages it hands
  // back, and the pack is still correct with them free. `sqlite3 pack VACUUM`
  // is one command away for anyone who wants the space.
  std::printf("index dropped\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return Usage();
  const std::string cmd = argv[1];
  if (cmd == "build") return Build(argc - 2, argv + 2);
  if (cmd == "info") return Info(argc - 2, argv + 2);
  if (cmd == "search") return Search(argc - 2, argv + 2);
  if (cmd == "drop") return Drop(argc - 2, argv + 2);
  return Usage();
}
