// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// A plugin overlay that exercises the host services and every optional
/// overlay function of API 2.0. Its state is a set of int properties the
/// tests read back.
///
/// Writing `worker_redraws` = N calls `request_redraw` N times from a worker
/// thread. A left press captures the mouse and a left release frees it; a
/// middle press calls `set_dirty(1)` and a right press `set_dirty(0)`, both
/// unhandled. A move, handled, stores what `elevation_at` returned in
/// `elevation_rc`. Escape is handled and counted in `keys`. The context menu
/// holds "Mark Here" (`mark`, one edit) and a "More" submenu with a disabled
/// "Mark Twice"; edits undo and redo through the overlay's own history.
#include <cstdlib>
#include <map>
#include <string>
#include <thread>

#include "peregrine/fv_plugin.h"

namespace {

const fv_host_api* g_host = nullptr;

struct HostOverlay {
  fv_overlay_ref* ref = nullptr;
  std::map<std::string, int> values{{"worker_redraws", 0}, {"elevation_rc", 1}, {"keys", 0},
                                    {"focus", 0},          {"edits", 0},        {"redos", 0},
                                    {"last_format", -1},   {"reverted", 0}};
  std::string text;  ///< the last string returned to the host
};

HostOverlay& O(void* self) { return *static_cast<HostOverlay*>(self); }

void* Create(const fv_host_api*, fv_overlay_ref* ref, const char*) {
  auto* o = new HostOverlay;
  o->ref = ref;
  return o;
}
void Destroy(void* self) { delete &O(self); }
int Draw(void*, const fv_view*, const fv_draw_api*) { return FV_OK; }
int Open(void*, const char*) { return FV_OK; }

int Save(void* self, const char*, int format_index) {
  O(self).values["last_format"] = format_index;
  return FV_OK;
}

int Revert(void* self, const char*) {
  O(self).values["reverted"] = 1;
  return FV_OK;
}

const char* Schema(void* self) {
  std::string& t = O(self).text;
  t = "[";
  for (const auto& [key, value] : O(self).values) {
    if (t.size() > 1) t += ",";
    t += R"({"key": ")" + key + R"(", "type": "int"})";
  }
  t += "]";
  return t.c_str();
}

const char* GetProperty(void* self, const char* key) {
  HostOverlay& o = O(self);
  auto it = o.values.find(key);
  if (it == o.values.end()) return nullptr;
  o.text = std::to_string(it->second);
  return o.text.c_str();
}

int SetProperty(void* self, const char* key, const char* value) {
  if (std::string(key) != "worker_redraws") return FV_ERR_NOT_FOUND;
  const int n = std::atoi(value);
  fv_overlay_ref* ref = O(self).ref;
  std::thread worker([ref, n] {
    for (int i = 0; i < n; ++i) g_host->request_redraw(ref);
  });
  worker.join();
  return FV_OK;
}

int OnMouse(void* self, const fv_view*, const fv_mouse_event* e) {
  HostOverlay& o = O(self);
  if (e->kind == FV_MOUSE_MOVE) {
    double metres = 0;
    o.values["elevation_rc"] = g_host->elevation_at(o.ref, fv_geo_point{32.7765, -79.9311}, &metres);
    return 1;
  }
  if (e->kind == FV_MOUSE_DOWN && e->button == 0) return g_host->capture_mouse(o.ref) == FV_OK;
  if (e->kind == FV_MOUSE_UP && e->button == 0) {
    g_host->release_mouse(o.ref);
    return 1;
  }
  if (e->kind == FV_MOUSE_DOWN) g_host->set_dirty(o.ref, e->button == 1 ? 1 : 0);
  return 0;
}

int OnKey(void* self, const fv_key_event* e) {
  if (e->key != 0x1B) return 0;
  ++O(self).values["keys"];
  return 1;
}

const char* EditHint(void*, double x, double) { return x < 100 ? "Add mark" : nullptr; }

void ContextMenu(void*, const fv_view*, double, double, const fv_menu* m) {
  m->item(m, "mark", "Mark Here", nullptr, 0);
  m->separator(m);
  const fv_menu* more = m->submenu(m, "More");
  // The parent grows before the submenu is filled.
  m->item(m, "mark3", "Mark Thrice", nullptr, FV_ITEM_DISABLED);
  m->item(m, "mark4", "Mark Four Times", nullptr, FV_ITEM_DISABLED);
  more->item(more, "mark2", "Mark Twice", "marks", FV_ITEM_DISABLED);
}

int MenuCommand(void* self, const char* id) {
  HostOverlay& o = O(self);
  if (std::string(id) != "mark") {
    g_host->set_error("no such command");
    return FV_ERR_NOT_FOUND;
  }
  ++o.values["edits"];
  o.values["redos"] = 0;
  g_host->set_dirty(o.ref, 1);
  return FV_OK;
}

int ItemInfo(void*, const fv_view*, double x, double y, double tolerance_px, const fv_info* i) {
  if (std::abs(x - 50) > tolerance_px || std::abs(y - 50) > tolerance_px) return 0;
  i->title(i, "Host item");
  i->row(i, "Item", "Kind", "test");
  i->row(i, "Item", "Id", "7");
  i->edit_label(i, "Edit Host\xE2\x80\xA6");
  return 1;
}

void SnapTo(void*, const fv_view*, double, double, double, const fv_snap_list* out) {
  out->add(out, fv_geo_point{32.7765, -79.9311}, "Host centre", 2.0);
}

void Search(void*, const fv_search* q) {
  const struct {
    const char* title;
    fv_geo_point at;
  } kItems[] = {{"Host Centre", {32.7765, -79.9311}},
                {"Host North", {33.5, -79.9311}},
                {"Host South", {31.5, -79.9311}}};
  uint64_t id = 1;
  for (const auto& item : kItems) {
    if (q->cancelled(q)) return;
    const int quality = q->match(q, item.title);
    if (quality >= 0 && q->in_area(q, item.at))
      q->add(q, id, item.title, "host · test", item.at, item.at, item.at, quality);
    ++id;
  }
}

void EnterFocus(void* self) { O(self).values["focus"] = 1; }
void ReleaseFocus(void* self) { O(self).values["focus"] = 0; }
int CanUndo(void* self) { return O(self).values["edits"] > 0; }
void Undo(void* self) {
  --O(self).values["edits"];
  ++O(self).values["redos"];
}
int CanRedo(void* self) { return O(self).values["redos"] > 0; }
void Redo(void* self) {
  ++O(self).values["edits"];
  --O(self).values["redos"];
}

fv_overlay_class g_class = [] {
  fv_overlay_class c{};
  c.struct_size = sizeof(fv_overlay_class);
  c.name = "host";
  c.create = Create;
  c.destroy = Destroy;
  c.draw = Draw;
  c.open = Open;
  c.save = Save;
  c.revert = Revert;
  c.context_menu = ContextMenu;
  c.menu_command = MenuCommand;
  c.item_info = ItemInfo;
  c.snap_to = SnapTo;
  c.search = Search;
  c.properties_schema = Schema;
  c.get_property = GetProperty;
  c.set_property = SetProperty;
  c.on_mouse = OnMouse;
  c.on_key = OnKey;
  c.edit_hint = EditHint;
  c.enter_edit_focus = EnterFocus;
  c.release_edit_focus = ReleaseFocus;
  c.can_undo = CanUndo;
  c.undo = Undo;
  c.can_redo = CanRedo;
  c.redo = Redo;
  return c;
}();

}  // namespace

extern "C" FV_PLUGIN_EXPORT int fv_plugin_entry(const fv_host_api* host, fv_plugin_api* out) {
  if (host->api_major != FV_PLUGIN_API_MAJOR) return FV_ERR_UNSUPPORTED;
  g_host = host;
  out->api_major = FV_PLUGIN_API_MAJOR;
  out->api_minor = FV_PLUGIN_API_MINOR;
  out->overlays = &g_class;
  out->overlay_count = 1;
  return FV_OK;
}
