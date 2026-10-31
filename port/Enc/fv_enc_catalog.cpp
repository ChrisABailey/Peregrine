// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_enc_catalog.h"

#include <expat.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace fv {

namespace {

/** Collects the leaf elements the comparison needs from each `<cell>`. */
struct CatalogParser {
  EncCatalog* out = nullptr;
  std::vector<std::string> stack;  // open element names
  std::string text;
  bool in_cell = false;
  EncCatalogCell cell;

  static void XMLCALL OnStart(void* user, const XML_Char* name, const XML_Char**) {
    auto* p = static_cast<CatalogParser*>(user);
    if (p->stack.size() == 1 && std::strcmp(name, "cell") == 0) {
      p->in_cell = true;
      p->cell = EncCatalogCell();
    }
    p->stack.emplace_back(name);
    p->text.clear();
  }

  static void XMLCALL OnEnd(void* user, const XML_Char* name) {
    auto* p = static_cast<CatalogParser*>(user);
    p->stack.pop_back();
    const std::string& v = p->text;
    // Leaves: <EncProductCatalog><cell><x> is depth 2 once popped; the
    // state and district lists sit one level deeper.
    if (p->in_cell && p->stack.size() == 2) {
      if (std::strcmp(name, "name") == 0) {
        p->cell.name = v;
      } else if (std::strcmp(name, "status") == 0) {
        p->cell.cancelled = v == "Cancelled";
      } else if (std::strcmp(name, "edtn") == 0) {
        p->cell.edition = std::atoi(v.c_str());
      } else if (std::strcmp(name, "updn") == 0) {
        p->cell.update = std::atoi(v.c_str());
      }
    } else if (p->in_cell && p->stack.size() == 3) {
      if (std::strcmp(name, "state") == 0 && p->stack[2] == "states") {
        p->cell.regions.push_back(v);
      } else if (std::strcmp(name, "coast_guard_district") == 0 &&
                 p->stack[2] == "coast_guard_districts") {
        const int d = std::atoi(v.c_str());
        char id[16];
        std::snprintf(id, sizeof id, "%02dCGD", d);
        if (d > 0) p->cell.regions.push_back(id);
      }
    } else if (p->stack.size() == 1 && std::strcmp(name, "cell") == 0) {
      p->in_cell = false;
      if (!p->cell.name.empty()) p->out->cells.push_back(std::move(p->cell));
    } else if (p->stack.size() == 2 && std::strcmp(name, "dt_valid") == 0 &&
               p->stack[1] == "Header") {
      p->out->valid = v;
    }
    p->text.clear();
  }

  static void XMLCALL OnText(void* user, const XML_Char* s, int len) {
    auto* p = static_cast<CatalogParser*>(user);
    // Only leaf text is kept; whitespace between child elements is cleared
    // by the next start or end tag.
    p->text.append(s, static_cast<size_t>(len));
  }
};

int EditionNumber(const std::string& edition) { return std::atoi(edition.c_str()); }

}  // namespace

Status ReadEncCatalog(const std::string& path, EncCatalog* out) {
  *out = EncCatalog();
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot open " + path);

  CatalogParser state;
  state.out = out;
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (parser == nullptr) return Status::Error(kInternal, "XML_ParserCreate failed");
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, &CatalogParser::OnStart, &CatalogParser::OnEnd);
  XML_SetCharacterDataHandler(parser, &CatalogParser::OnText);

  std::vector<char> buf(1 << 16);
  Status st = Status::Ok();
  for (;;) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize got = in.gcount();
    const bool last = !in;
    if (XML_Parse(parser, buf.data(), static_cast<int>(got), last ? 1 : 0) ==
        XML_STATUS_ERROR) {
      st = Status::Error(kIoError, path + ": XML error at line " +
                                       std::to_string(XML_GetCurrentLineNumber(parser)) +
                                       ": " + XML_ErrorString(XML_GetErrorCode(parser)));
      break;
    }
    if (last) break;
  }
  XML_ParserFree(parser);
  if (!st.ok()) {
    *out = EncCatalog();
    return st;
  }
  if (out->cells.empty()) return Status::Error(kNotFound, path + ": no cells listed");
  return Status::Ok();
}

std::string InferEncRegion(const std::vector<EncIndexEntry>& installed,
                           const EncCatalog& catalog) {
  std::unordered_set<std::string> drawn;
  for (const EncIndexEntry& e : installed)
    if (!e.cancelled) drawn.insert(e.name);

  std::vector<std::string> order;  // first appearance in the catalog
  std::unordered_map<std::string, std::pair<size_t, size_t>> counts;  // {active, shared}
  for (const EncCatalogCell& c : catalog.cells) {
    if (c.cancelled) continue;
    const bool shared = drawn.count(c.name) != 0;
    for (const std::string& r : c.regions) {
      auto it = counts.find(r);
      if (it == counts.end()) {
        order.push_back(r);
        it = counts.emplace(r, std::make_pair(size_t{0}, size_t{0})).first;
      }
      ++it->second.first;
      if (shared) ++it->second.second;
    }
  }

  std::string best;
  double best_score = 0.0;
  for (const std::string& r : order) {
    const auto& [active, shared] = counts[r];
    if (shared == 0) continue;
    const double score =
        static_cast<double>(shared) / static_cast<double>(drawn.size() + active - shared);
    if (score > best_score) {
      best_score = score;
      best = r;
    }
  }
  return best;
}

EncCatalogComparison CompareEncCatalog(const std::vector<EncIndexEntry>& installed,
                                       const EncCatalog& catalog,
                                       const std::string& region) {
  EncCatalogComparison out;
  out.region = region.empty() ? InferEncRegion(installed, catalog) : region;

  std::unordered_map<std::string, const EncCatalogCell*> by_name;
  for (const EncCatalogCell& c : catalog.cells) by_name[c.name] = &c;

  std::unordered_set<std::string> have;
  for (const EncIndexEntry& e : installed) {
    have.insert(e.name);
    auto it = by_name.find(e.name);
    if (it == by_name.end()) {
      out.unlisted.push_back(e.name);
      continue;
    }
    const EncCatalogCell& c = *it->second;
    if (c.cancelled) {
      if (!e.cancelled) out.cancelled.push_back(e.name);
    } else if (e.cancelled || c.edition > EditionNumber(e.edition)) {
      // A cell cancelled here but active in the catalog has been reissued.
      out.new_editions.push_back(e.name);
    } else if (c.edition == EditionNumber(e.edition) && c.update > e.update_count) {
      out.new_updates.push_back(e.name);
    }
  }

  if (!out.region.empty()) {
    for (const EncCatalogCell& c : catalog.cells) {
      if (c.cancelled || have.count(c.name)) continue;
      for (const std::string& r : c.regions) {
        if (r == out.region) {
          out.added.push_back(c.name);
          break;
        }
      }
    }
  }
  return out;
}

std::string EncRegionFromZipName(const std::string& file_name) {
  static const char kSuffix[] = "_ENCs.zip";
  const size_t n = std::strlen(kSuffix);
  if (file_name.size() <= n || file_name.compare(file_name.size() - n, n, kSuffix) != 0)
    return {};
  const std::string id = file_name.substr(0, file_name.size() - n);
  auto upper = [](char ch) { return std::isupper(static_cast<unsigned char>(ch)) != 0; };
  auto digit = [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; };
  if (id.size() == 2 && upper(id[0]) && upper(id[1])) return id;
  if (id.size() == 5 && digit(id[0]) && digit(id[1]) && id.compare(2, 3, "CGD") == 0)
    return id;
  return {};
}

}  // namespace fv
