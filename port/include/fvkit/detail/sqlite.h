// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/detail/sqlite.h — minimal RAII over sqlite3, shared by the catalog
// (L2) and later the GeoPackage store (L5). Not a general wrapper: exactly
// the surface those need. Errors map to fv::Status (kIoError with sqlite's
// message; code conventions per contracts D3).

#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <string>

#include "fvkit/geo.h"

namespace fv {
namespace detail {

class SqliteDb {
 public:
  SqliteDb() = default;
  ~SqliteDb() { Close(); }
  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;

  Status Open(const std::string& path) {
    Close();
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
      std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open failed";
      Close();
      return Status::Error(kIoError, "sqlite open " + path + ": " + msg);
    }
    return Status::Ok();
  }

  // Read-only, and fails if the file is absent rather than creating it —
  // sqlite3_open() would happily hand back an empty new database for a typo'd
  // path. For published artefacts the port only ever reads (an MBTiles
  // pyramid, someone else's GeoPackage), this is the right door.
  Status OpenReadOnly(const std::string& path) {
    Close();
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
      std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open_v2 failed";
      Close();
      return Status::Error(kIoError, "sqlite open " + path + ": " + msg);
    }
    return Status::Ok();
  }

  void Close() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }

  bool IsOpen() const { return db_ != nullptr; }
  sqlite3* get() const { return db_; }

  Status Exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
      std::string msg = err ? err : "sqlite3_exec failed";
      sqlite3_free(err);
      return Status::Error(kIoError, "sqlite exec: " + msg);
    }
    return Status::Ok();
  }

  int64_t LastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

 private:
  sqlite3* db_ = nullptr;
};

class SqliteStmt {
 public:
  SqliteStmt() = default;
  ~SqliteStmt() { Finalize(); }
  SqliteStmt(const SqliteStmt&) = delete;
  SqliteStmt& operator=(const SqliteStmt&) = delete;

  Status Prepare(SqliteDb& db, const char* sql) {
    Finalize();
    if (sqlite3_prepare_v2(db.get(), sql, -1, &stmt_, nullptr) != SQLITE_OK)
      return Status::Error(kIoError, std::string("sqlite prepare: ") +
                                         sqlite3_errmsg(db.get()));
    return Status::Ok();
  }

  void Finalize() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
      stmt_ = nullptr;
    }
  }

  // 1-based parameter indexes, as in sqlite.
  void BindInt64(int i, int64_t v) { sqlite3_bind_int64(stmt_, i, v); }
  void BindDouble(int i, double v) { sqlite3_bind_double(stmt_, i, v); }
  void BindText(int i, const std::string& v) {
    sqlite3_bind_text(stmt_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT);
  }
  void BindBlob(int i, const void* data, size_t n) {
    sqlite3_bind_blob(stmt_, i, data, (int)n, SQLITE_TRANSIENT);
  }

  const void* ColBlob(int i) const { return sqlite3_column_blob(stmt_, i); }
  int ColBytes(int i) const { return sqlite3_column_bytes(stmt_, i); }

  // true if a row is available; false on SQLITE_DONE. Errors via *status.
  bool Step(Status* status = nullptr) {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc != SQLITE_DONE && status != nullptr)
      *status = Status::Error(kIoError,
                              std::string("sqlite step rc=") + std::to_string(rc));
    return false;
  }

  void Reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }

  // 0-based column indexes, as in sqlite.
  int64_t ColInt64(int i) const { return sqlite3_column_int64(stmt_, i); }
  double ColDouble(int i) const { return sqlite3_column_double(stmt_, i); }
  // SQLITE_NULL, SQLITE_INTEGER, ... — the one thing the Col* accessors above
  // cannot express, because sqlite coerces a NULL to 0 / "" rather than
  // saying so. A nullable column (an optional pivot, an unset elevation) is
  // only readable through this.
  int ColType(int i) const { return sqlite3_column_type(stmt_, i); }

  std::string ColText(int i) const {
    const unsigned char* t = sqlite3_column_text(stmt_, i);
    return t ? (const char*)t : "";
  }

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace detail
}  // namespace fv
