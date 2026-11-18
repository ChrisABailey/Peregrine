// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// RemoteFeatureLayer: a web service's features cached in a GeoPackage and
/// fetched tile by tile on a worker thread.
///
/// The fetched tiles of every layer of a cache file are kept beside it in
/// `<cache>-tiles.sqlite`: `wf_tiles` (state and fetch time per tile) and
/// `wf_tile_features` (the feature ids each tile returned). A feature stays in
/// the cache while some tile lists it; a refetch that no longer returns it
/// deletes it. A completed tile replaces every tile inside it, so no complete
/// tile ever lies under another.

#include "fvkit/feature/web/remote_layer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "fvkit/detail/sqlite.h"
#include "fvkit/feature/geopackage.h"
#include "fvkit/feature/web/tiles.h"
#include "fvkit/log.h"

namespace fv {
namespace web {
namespace {

namespace fs = std::filesystem;

constexpr int kComplete = 1;
constexpr int kSplit = 2;

uint64_t Fnv1a(const std::string& text) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : text) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

std::string Hex(uint64_t v, int digits) {
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
  return std::string(buf + 16 - digits);
}

/// The cache table for a layer: its id made safe for SQL, and a hash of the
/// schema so a service whose fields change gets a fresh table.
std::string CacheTableName(const RemoteLayerInfo& info) {
  std::string name = "wf_";
  for (char c : info.id) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'), ok = true;
    name += ok ? c : '_';
  }
  std::string signature = GeometryTypeName(info.schema.geometry_type);
  for (const FieldDef& f : info.schema.fields)
    signature += "|" + f.name + ":" + std::to_string(static_cast<int>(f.type));
  return name + "_" + Hex(Fnv1a(signature), 8);
}

/// The cache keeps names, types and titles only: constraints the service
/// declares are not enforced on what it sends. Any geometry type is accepted.
FeatureSchema CacheSchema(const FeatureSchema& service) {
  FeatureSchema s;
  s.geometry_type = GeometryType::kGeometry;
  for (const FieldDef& f : service.fields) {
    FieldDef d;
    d.name = f.name;
    d.type = f.type;
    d.title = f.title;
    d.description = f.description;
    d.unit = f.unit;
    s.fields.push_back(d);
  }
  return s;
}

/// Drops Z and M; the cache table is two-dimensional.
void Flatten(Geometry* g) {
  g->has_z = false;
  g->has_m = false;
  for (Geometry& part : g->parts) Flatten(&part);
}

bool Intersects(const GeoRect& a, const GeoRect& b) {
  return a.ll.lat <= b.ur.lat && b.ll.lat <= a.ur.lat && a.ll.lon <= b.ur.lon &&
         b.ll.lon <= a.ur.lon;
}

/// `box` as one or two boxes that do not cross ±180.
std::vector<GeoRect> SplitAtAntimeridian(const GeoRect& box) {
  if (!box.CrossesAntimeridian()) return {box};
  GeoRect east = box, west = box;
  east.ur.lon = 180.0;
  west.ll.lon = -180.0;
  return {east, west};
}

/// One cache file and its tile index, shared by the layers of one service.
struct CacheFile {
  std::mutex mu;
  std::shared_ptr<GeoPackage> gpkg;
  detail::SqliteDb tiles;
};

std::mutex g_files_mu;
std::map<std::string, std::weak_ptr<CacheFile>> g_files;

constexpr const char* kTileTables =
    "CREATE TABLE IF NOT EXISTS wf_tiles (layer TEXT NOT NULL, z INTEGER NOT NULL,"
    " x INTEGER NOT NULL, y INTEGER NOT NULL, state INTEGER NOT NULL,"
    " fetched_at INTEGER NOT NULL, PRIMARY KEY (layer, z, x, y));"
    "CREATE TABLE IF NOT EXISTS wf_tile_features (layer TEXT NOT NULL, z INTEGER NOT NULL,"
    " x INTEGER NOT NULL, y INTEGER NOT NULL, fid INTEGER NOT NULL,"
    " PRIMARY KEY (layer, z, x, y, fid));"
    "CREATE INDEX IF NOT EXISTS wf_tile_features_fid ON wf_tile_features (layer, fid);";

Status OpenCacheFile(const std::string& path, std::shared_ptr<CacheFile>* out) {
  std::lock_guard<std::mutex> lock(g_files_mu);
  auto it = g_files.find(path);
  if (it != g_files.end()) {
    if ((*out = it->second.lock())) return Status::Ok();
  }
  std::error_code ec;
  fs::path parent = fs::path(path).parent_path();
  if (!parent.empty()) fs::create_directories(parent, ec);
  auto file = std::make_shared<CacheFile>();
  Status s = fs::exists(path) ? GeoPackage::Open(path, /*writable=*/true, &file->gpkg)
                              : GeoPackage::Create(path, &file->gpkg);
  if (!s.ok()) return s;
  s = file->tiles.Open(path + "-tiles.sqlite");
  if (s.ok()) s = file->tiles.Exec(kTileTables);
  if (!s.ok()) return s;
  g_files[path] = file;
  *out = file;
  return Status::Ok();
}

}  // namespace

std::string WebCachePath(const std::string& cache_dir, const std::string& source_url) {
  return (fs::path(cache_dir) / "web-features" / (Hex(Fnv1a(source_url), 16) + ".gpkg")).string();
}

struct RemoteFeatureLayer::Impl {
  std::shared_ptr<IFeatureService> service;
  RemoteLayerInfo info;
  RemoteLayerOptions options;
  std::string table;

  // Guarded by cache->mu.
  std::shared_ptr<CacheFile> cache;
  std::shared_ptr<GeoPackageFeatureLayer> layer;

  // Guarded by mu.
  mutable std::mutex mu;
  std::condition_variable work_cv;
  mutable std::condition_variable idle_cv;
  struct TileState {
    int state = 0;
    int64_t fetched_at = 0;
  };
  std::map<TileKey, TileState> tiles;
  std::map<TileKey, int64_t> retry_at;
  std::deque<TileKey> queue;
  bool loading = false;
  TileKey current;  ///< the tile being fetched while `loading`
  size_t features_cached = 0;
  bool capped = false;
  std::string last_error;
  std::function<void()> on_change;
  bool stop = false;

  std::atomic<bool> cancel{false};
  std::atomic<uint64_t> revision{1};
  std::thread worker;

  int64_t Now() const {
    if (options.now) return options.now();
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  }

  bool Stale(const TileState& t, int64_t now) const {
    return options.refresh_minutes > 0 && now - t.fetched_at >= options.refresh_minutes * 60;
  }

  /// Adds to `want` what `t` needs fetched for `box`: nothing when a complete
  /// tile covers it (that tile when stale), its children when it was split.
  void Resolve(const TileKey& t, const GeoRect& box, int64_t now, std::vector<TileKey>* want) {
    for (TileKey a = t;; a = TileParent(a)) {
      auto it = tiles.find(a);
      if (it != tiles.end()) {
        if (it->second.state == kComplete) {
          if (Stale(it->second, now)) want->push_back(a);
          return;
        }
        if (it->second.state == kSplit && a == t) {
          for (const TileKey& c : TileChildren(t))
            if (Intersects(TileBounds(c), box)) Resolve(c, box, now, want);
          return;
        }
      }
      if (a.z == 0) break;
    }
    auto failed = retry_at.find(t);
    if (failed != retry_at.end() && failed->second > now) return;
    want->push_back(t);
  }

  /// Queues the tiles `box` needs, ahead of earlier requests.
  void Schedule(const GeoRect& box) {
    std::vector<TileKey> want;
    {
      std::lock_guard<std::mutex> lock(mu);
      if (capped || stop) return;
      int64_t now = Now();
      int z = TileZoomForBox(box, options.min_zoom, options.max_zoom);
      for (const GeoRect& part : SplitAtAntimeridian(box))
        for (const TileKey& t : TilesCovering(part, z)) Resolve(t, part, now, &want);
      if (want.empty()) return;
      for (auto it = want.rbegin(); it != want.rend(); ++it) {
        if (loading && *it == current) continue;
        auto q = std::find(queue.begin(), queue.end(), *it);
        if (q != queue.end()) queue.erase(q);
        queue.push_front(*it);
      }
      while (queue.size() > options.max_queued_tiles) queue.pop_back();
    }
    work_cv.notify_one();
  }

  void Run() {
    for (;;) {
      TileKey t;
      {
        std::unique_lock<std::mutex> lock(mu);
        work_cv.wait(lock, [&] { return stop || !queue.empty(); });
        if (stop) break;
        t = queue.front();
        queue.pop_front();
        loading = true;
        current = t;
      }
      if (Fetch(t)) revision.fetch_add(1);
      std::function<void()> callback;
      {
        std::lock_guard<std::mutex> lock(mu);
        loading = false;
        callback = on_change;
      }
      idle_cv.notify_all();
      if (callback) callback();
    }
  }

  /// Fetches and stores one tile. True when the cache changed.
  bool Fetch(const TileKey& t) {
    const GeoRect box = TileBounds(t);
    std::vector<Feature> features;
    std::string cursor, next;
    int pages = 0;
    bool split = false;
    Status s;
    do {
      std::vector<Feature> page;
      next.clear();
      s = service->FetchPage(info.id, box, cursor, &page, &next, &cancel);
      if (!s.ok()) break;
      ++pages;
      std::move(page.begin(), page.end(), std::back_inserter(features));
      cursor = next;
      if (!next.empty() && pages >= options.max_pages_per_tile && t.z < options.max_zoom) {
        split = true;
        break;
      }
    } while (!next.empty() && !cancel.load());
    if (cancel.load()) return false;

    const int64_t now = Now();
    if (!s.ok()) {
      FV_LOG_WARNING("web layer " << info.id << ": tile " << t.z << "/" << t.x << "/" << t.y
                                  << ": " << s.message);
      std::lock_guard<std::mutex> lock(mu);
      retry_at[t] = now + options.retry_seconds;
      last_error = s.message;
      return false;
    }
    if (split) {
      MarkSplit(t, now);
      return false;
    }
    return Store(t, std::move(features), now);
  }

  /// Records `t` as split and queues its children in its place.
  void MarkSplit(const TileKey& t, int64_t now) {
    {
      std::lock_guard<std::mutex> lock(cache->mu);
      detail::SqliteStmt st;
      if (st.Prepare(cache->tiles,
                     "INSERT OR REPLACE INTO wf_tiles (layer, z, x, y, state, fetched_at)"
                     " VALUES (?, ?, ?, ?, ?, ?)")
              .ok()) {
        st.BindText(1, table);
        st.BindInt64(2, t.z);
        st.BindInt64(3, t.x);
        st.BindInt64(4, t.y);
        st.BindInt64(5, kSplit);
        st.BindInt64(6, now);
        st.Step();
      }
    }
    std::lock_guard<std::mutex> lock(mu);
    tiles[t] = TileState{kSplit, now};
    std::vector<TileKey> children = TileChildren(t);
    for (auto it = children.rbegin(); it != children.rend(); ++it) queue.push_front(*it);
    while (queue.size() > options.max_queued_tiles) queue.pop_back();
  }

  /// Writes a fetched tile: upserts its features, replaces the tile's id list
  /// and every tile inside it, and deletes features no tile lists any more.
  bool Store(const TileKey& t, std::vector<Feature> features, int64_t now) {
    std::set<int64_t> ids;
    size_t count = 0;
    {
      std::lock_guard<std::mutex> lock(cache->mu);
      size_t fresh = 0;
      for (const Feature& f : features) {
        Feature existing;
        if (f.id != 0 && !layer->Get(f.id, &existing).ok()) ++fresh;
      }
      if (layer->Count() + fresh > options.max_features) {
        std::lock_guard<std::mutex> state(mu);
        capped = true;
        queue.clear();
        return false;
      }
      for (Feature& f : features) {
        if (f.id == 0) continue;
        Flatten(&f.geometry);
        Feature existing;
        Status s = layer->Get(f.id, &existing).ok() ? layer->Update(f) : layer->InsertWithId(f);
        if (s.ok())
          ids.insert(f.id);
        else
          FV_LOG_WARNING("web layer " << info.id << ": feature " << f.id << ": " << s.message);
      }
      Status s = layer->Save();
      if (!s.ok()) {
        FV_LOG_WARNING("web layer " << info.id << ": cache: " << s.message);
        layer->DiscardEdits();
        std::lock_guard<std::mutex> state(mu);
        last_error = s.message;
        return false;
      }

      std::vector<int64_t> released;
      s = ReplaceTileRows(t, ids, now, &released);
      if (!s.ok()) FV_LOG_WARNING("web layer " << info.id << ": tile index: " << s.message);
      if (!released.empty()) {
        for (int64_t id : released) layer->Delete(id);
        s = layer->Save();
        if (!s.ok()) FV_LOG_WARNING("web layer " << info.id << ": cache: " << s.message);
      }
      count = layer->Count();
    }
    std::lock_guard<std::mutex> lock(mu);
    for (auto it = tiles.begin(); it != tiles.end();) {
      const TileKey& k = it->first;
      int d = k.z - t.z;
      bool inside = d > 0 && (k.x >> d) == t.x && (k.y >> d) == t.y;
      it = inside ? tiles.erase(it) : std::next(it);
    }
    tiles[t] = TileState{kComplete, now};
    retry_at.erase(t);
    features_cached = count;
    last_error.clear();
    return true;
  }

  /// Under cache->mu. Replaces the rows of `t` and of every tile inside it
  /// with `ids`; `released` gets the ids those rows held that no tile lists now.
  Status ReplaceTileRows(const TileKey& t, const std::set<int64_t>& ids, int64_t now,
                         std::vector<int64_t>* released) {
    detail::SqliteDb& db = cache->tiles;
    // Tiles at t and below: z >= t.z with (x, y) shifted down to t's zoom.
    const char* kUnder = " WHERE layer = ?1 AND z >= ?2 AND (x >> (z - ?2)) = ?3 AND (y >> (z - ?2)) = ?4";
    auto bind_under = [&](detail::SqliteStmt& st) {
      st.BindText(1, table);
      st.BindInt64(2, t.z);
      st.BindInt64(3, t.x);
      st.BindInt64(4, t.y);
    };
    Status s = db.Exec("BEGIN");
    if (!s.ok()) return s;
    std::set<int64_t> old;
    {
      detail::SqliteStmt st;
      s = st.Prepare(db, (std::string("SELECT DISTINCT fid FROM wf_tile_features") + kUnder).c_str());
      if (s.ok()) {
        bind_under(st);
        while (st.Step()) old.insert(st.ColInt64(0));
      }
    }
    for (const char* table_name : {"wf_tile_features", "wf_tiles"}) {
      if (!s.ok()) break;
      detail::SqliteStmt st;
      s = st.Prepare(db, (std::string("DELETE FROM ") + table_name + kUnder).c_str());
      if (s.ok()) {
        bind_under(st);
        st.Step(&s);
      }
    }
    if (s.ok()) {
      detail::SqliteStmt st;
      s = st.Prepare(db, "INSERT INTO wf_tile_features (layer, z, x, y, fid) VALUES (?, ?, ?, ?, ?)");
      for (int64_t id : ids) {
        if (!s.ok()) break;
        st.Reset();
        st.BindText(1, table);
        st.BindInt64(2, t.z);
        st.BindInt64(3, t.x);
        st.BindInt64(4, t.y);
        st.BindInt64(5, id);
        st.Step(&s);
      }
    }
    if (s.ok()) {
      detail::SqliteStmt st;
      s = st.Prepare(db,
                     "INSERT INTO wf_tiles (layer, z, x, y, state, fetched_at) VALUES (?, ?, ?, ?, ?, ?)");
      if (s.ok()) {
        st.BindText(1, table);
        st.BindInt64(2, t.z);
        st.BindInt64(3, t.x);
        st.BindInt64(4, t.y);
        st.BindInt64(5, kComplete);
        st.BindInt64(6, now);
        st.Step(&s);
      }
    }
    if (s.ok()) {
      detail::SqliteStmt st;
      s = st.Prepare(db, "SELECT 1 FROM wf_tile_features WHERE layer = ? AND fid = ? LIMIT 1");
      for (int64_t id : old) {
        if (!s.ok()) break;
        if (ids.count(id)) continue;
        st.Reset();
        st.BindText(1, table);
        st.BindInt64(2, id);
        if (!st.Step()) released->push_back(id);
      }
    }
    if (!s.ok()) {
      db.Exec("ROLLBACK");
      released->clear();
      return s;
    }
    return db.Exec("COMMIT");
  }

  /// Under cache->mu: the tiles of this layer recorded in the index.
  Status LoadTiles() {
    detail::SqliteStmt st;
    Status s = st.Prepare(cache->tiles,
                          "SELECT z, x, y, state, fetched_at FROM wf_tiles WHERE layer = ?");
    if (!s.ok()) return s;
    st.BindText(1, table);
    while (st.Step()) {
      TileKey k{static_cast<int>(st.ColInt64(0)), static_cast<int>(st.ColInt64(1)),
                static_cast<int>(st.ColInt64(2))};
      tiles[k] = TileState{static_cast<int>(st.ColInt64(3)), st.ColInt64(4)};
    }
    return Status::Ok();
  }
};

RemoteFeatureLayer::RemoteFeatureLayer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

RemoteFeatureLayer::~RemoteFeatureLayer() {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->stop = true;
  }
  impl_->cancel.store(true);
  impl_->work_cv.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
}

Status RemoteFeatureLayer::Open(std::shared_ptr<IFeatureService> service, const RemoteLayerInfo& info,
                                const std::string& cache_path, RemoteLayerOptions options,
                                std::shared_ptr<RemoteFeatureLayer>* out) {
  if (!service || info.id.empty()) return Status::Error(kInvalidArg, "web layer: no service or layer id");
  auto impl = std::make_unique<Impl>();
  impl->service = std::move(service);
  impl->info = info;
  impl->options = std::move(options);
  impl->table = CacheTableName(info);
  Status s = OpenCacheFile(cache_path, &impl->cache);
  if (!s.ok()) return s;
  {
    std::lock_guard<std::mutex> lock(impl->cache->mu);
    GeoPackage& gpkg = *impl->cache->gpkg;
    impl->layer = gpkg.FindOpenLayer(impl->table);
    if (!impl->layer) {
      std::vector<GeoPackageLayerInfo> layers;
      s = gpkg.Layers(&layers);
      if (!s.ok()) return s;
      bool exists = std::any_of(layers.begin(), layers.end(),
                                [&](const GeoPackageLayerInfo& l) { return l.table == impl->table; });
      if (exists) {
        s = gpkg.OpenLayer(impl->table, &impl->layer);
      } else {
        GeoPackageLayerOptions opts;
        // gpkg_contents.identifier is UNIQUE, and a schema change leaves the
        // superseded table of the same layer in the file; the table name tells them apart.
        opts.identifier = (info.title.empty() ? info.id : info.title) + " (" + impl->table + ")";
        opts.description = info.description;
        s = gpkg.CreateLayer(impl->table, CacheSchema(info.schema), opts, &impl->layer);
        if (s.ok()) s = gpkg.SaveTableChanges();
      }
      if (!s.ok()) return s;
    }
    s = impl->LoadTiles();
    if (!s.ok()) return s;
    impl->features_cached = impl->layer->Count();
  }
  Impl* raw = impl.get();
  out->reset(new RemoteFeatureLayer(std::move(impl)));
  raw->worker = std::thread([raw] { raw->Run(); });
  return Status::Ok();
}

std::string RemoteFeatureLayer::Name() const {
  return impl_->info.title.empty() ? impl_->info.id : impl_->info.title;
}

const FeatureSchema& RemoteFeatureLayer::Schema() const { return impl_->info.schema; }

size_t RemoteFeatureLayer::Count() const {
  std::lock_guard<std::mutex> lock(impl_->cache->mu);
  return impl_->layer->Count();
}

Status RemoteFeatureLayer::Extent(GeoRect* out) const {
  if (!impl_->info.has_extent) return Status::Error(kNotFound, "web layer: the service gives no extent");
  *out = impl_->info.extent;
  return Status::Ok();
}

Status RemoteFeatureLayer::All(std::vector<Feature>* out) const {
  std::lock_guard<std::mutex> lock(impl_->cache->mu);
  return impl_->layer->All(out);
}

Status RemoteFeatureLayer::Query(const GeoRect& box, std::vector<Feature>* out) const {
  return QueryFields(box, {}, out);
}

Status RemoteFeatureLayer::QueryFields(const GeoRect& box, const std::vector<bool>& fields,
                                       std::vector<Feature>* out) const {
  Status s;
  {
    std::lock_guard<std::mutex> lock(impl_->cache->mu);
    s = impl_->layer->QueryFields(box, fields, out);
  }
  impl_->Schedule(box);
  return s;
}

Status RemoteFeatureLayer::Get(int64_t id, Feature* out) const {
  std::lock_guard<std::mutex> lock(impl_->cache->mu);
  return impl_->layer->Get(id, out);
}

bool RemoteFeatureLayer::Revision(uint64_t* out) const {
  *out = impl_->revision.load();
  return true;
}

const RemoteLayerInfo& RemoteFeatureLayer::info() const { return impl_->info; }

RemoteLayerProgress RemoteFeatureLayer::Progress() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  RemoteLayerProgress p;
  p.tiles_queued = impl_->queue.size();
  p.tiles_loading = impl_->loading ? 1 : 0;
  p.features_cached = impl_->features_cached;
  p.capped = impl_->capped;
  p.last_error = impl_->last_error;
  return p;
}

void RemoteFeatureLayer::SetOnChange(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->on_change = std::move(callback);
}

Status RemoteFeatureLayer::Refresh() {
  Status s;
  {
    std::lock_guard<std::mutex> lock(impl_->cache->mu);
    std::vector<Feature> all;
    s = impl_->layer->All(&all);
    for (const Feature& f : all) {
      if (!s.ok()) break;
      s = impl_->layer->Delete(f.id);
    }
    if (s.ok()) s = impl_->layer->Save();
    for (const char* table_name : {"wf_tile_features", "wf_tiles"}) {
      if (!s.ok()) break;
      detail::SqliteStmt st;
      s = st.Prepare(impl_->cache->tiles, (std::string("DELETE FROM ") + table_name + " WHERE layer = ?").c_str());
      if (s.ok()) {
        st.BindText(1, impl_->table);
        st.Step();
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->tiles.clear();
    impl_->retry_at.clear();
    impl_->queue.clear();
    impl_->capped = false;
    impl_->features_cached = 0;
  }
  impl_->revision.fetch_add(1);
  return s;
}

bool RemoteFeatureLayer::WaitIdle(double timeout_s) const {
  std::unique_lock<std::mutex> lock(impl_->mu);
  return impl_->idle_cv.wait_for(lock, std::chrono::duration<double>(timeout_s),
                                 [&] { return impl_->queue.empty() && !impl_->loading; });
}

}  // namespace web
}  // namespace fv
