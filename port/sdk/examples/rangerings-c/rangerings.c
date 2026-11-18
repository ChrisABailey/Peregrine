/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (C) 2026 Chris Bailey
 * Part of Peregrine, a cross-platform port of FalconView(tm).
 * See COPYING.LESSER and NOTICE.md for the full licensing picture. */

/**
 * rangerings.c — a Peregrine overlay written against the C plugin API.
 *
 * Concentric range rings about a centre the user places with a click. The
 * rings are a file overlay (`.rng`, plain text), offer four properties, and
 * hit-test their centre and each ring. An editor gives them a toolbar button
 * and a palette; clicks reach the rings only while it is active, and each
 * centre move undoes and redoes from Edit ▸ Undo. The package
 * also contributes one tool, which opens the sample file shipped in the
 * package.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "peregrine/fv_plugin.h"

#define MAX_RINGS 10
#define MAX_HISTORY 32
#define METRES_PER_NM 1852.0
#define EARTH_RADIUS_M 6371008.8
#define PI 3.14159265358979323846
#define HOME_LAT 32.7765 /* Charleston, until a click or a file says otherwise */
#define HOME_LON -79.9311

typedef struct rings {
  const fv_host_api* host;
  fv_overlay_ref* ref;
  fv_geo_point center;
  int count;
  double spacing_nm;
  fv_color color;
  int labels;
  fv_geo_point undo[MAX_HISTORY]; /* earlier centres, oldest first */
  int undo_count;
  fv_geo_point redo[MAX_HISTORY];
  int redo_count;
  char text[256]; /* the last string returned to the host */
} rings;

/* MARK: Geometry */

/** Great-circle distance in metres. */
static double distance_m(fv_geo_point a, fv_geo_point b) {
  const double rad = PI / 180.0;
  const double dlat = (b.lat - a.lat) * rad, dlon = (b.lon - a.lon) * rad;
  const double h = sin(dlat / 2) * sin(dlat / 2) +
                   cos(a.lat * rad) * cos(b.lat * rad) * sin(dlon / 2) * sin(dlon / 2);
  return 2 * EARTH_RADIUS_M * asin(sqrt(h < 1 ? h : 1));
}

/** The point `metres` due north of `p`. */
static fv_geo_point north_of(fv_geo_point p, double metres) {
  fv_geo_point q = p;
  q.lat += metres / EARTH_RADIUS_M * 180.0 / PI;
  return q;
}

/* MARK: History */

/** Appends `p` to a bounded history, dropping the oldest entry when full. */
static void history_push(fv_geo_point* h, int* n, fv_geo_point p) {
  if (*n == MAX_HISTORY) {
    memmove(h, h + 1, (MAX_HISTORY - 1) * sizeof *h);
    --*n;
  }
  h[(*n)++] = p;
}

/** Moves the centre to `to` as one undoable edit and dirties the document. */
static void move_centre(rings* r, fv_geo_point to) {
  history_push(r->undo, &r->undo_count, r->center);
  r->redo_count = 0;
  r->center = to;
  r->host->set_dirty(r->ref, 1);
}

static int rings_can_undo(void* self) { return ((rings*)self)->undo_count > 0; }
static int rings_can_redo(void* self) { return ((rings*)self)->redo_count > 0; }

static void rings_undo(void* self) {
  rings* r = (rings*)self;
  if (r->undo_count == 0) return;
  history_push(r->redo, &r->redo_count, r->center);
  r->center = r->undo[--r->undo_count];
  r->host->set_dirty(r->ref, 1);
}

static void rings_redo(void* self) {
  rings* r = (rings*)self;
  if (r->redo_count == 0) return;
  history_push(r->undo, &r->undo_count, r->center);
  r->center = r->redo[--r->redo_count];
  r->host->set_dirty(r->ref, 1);
}

/* MARK: Overlay */

static void* rings_create(const fv_host_api* host, fv_overlay_ref* ref, const char* type_id) {
  rings* r = (rings*)calloc(1, sizeof(rings));
  (void)type_id;
  if (r == NULL) {
    host->set_error("out of memory");
    return NULL;
  }
  r->host = host;
  r->ref = ref;
  r->center.lat = HOME_LAT;
  r->center.lon = HOME_LON;
  r->count = 3;
  r->spacing_nm = 1.0;
  r->color.r = 255;
  r->color.a = 255;
  r->labels = 1;
  return r;
}

static void rings_destroy(void* self) { free(self); }

static int rings_draw(void* self, const fv_view* view, const fv_draw_api* d) {
  const rings* r = (const rings*)self;
  fv_line_style line = {sizeof(fv_line_style)};
  fv_symbol_style mark = {sizeof(fv_symbol_style)};
  fv_label_style text = {sizeof(fv_label_style)};
  int i;
  (void)view;
  line.color = r->color;
  line.width_px = 2;
  line.casing_color.r = line.casing_color.g = line.casing_color.b = 255;
  line.casing_color.a = 255;
  line.casing_px = 1;
  mark.color = r->color;
  mark.scale = 1.5;
  text.color = r->color;
  text.size_px = 12;
  text.halo_color = line.casing_color;
  text.halo_px = 1;
  text.halign = FV_HALIGN_CENTER;
  text.valign = FV_VALIGN_BOTTOM;
  text.dy = -2;

  d->symbol(d, r->center, "fv.crosshair", &mark);
  for (i = 1; i <= r->count; ++i) {
    const double radius = i * r->spacing_nm * METRES_PER_NM;
    int rc;
    if (d->cancelled(d)) return FV_ERR_INTERRUPTED;
    rc = d->circle(d, r->center, radius, &line);
    if (rc != FV_OK) return rc;
    if (r->labels) {
      char label[32];
      snprintf(label, sizeof label, "%g nm", i * r->spacing_nm);
      d->label(d, north_of(r->center, radius), label, &text);
    }
  }
  return FV_OK;
}

static int rings_open(void* self, const char* path) {
  rings* r = (rings*)self;
  rings next = *r;
  FILE* f = fopen(path, "r");
  char line[128];
  if (f == NULL) {
    r->host->set_error("cannot read the file");
    return FV_ERR_IO;
  }
  while (fgets(line, sizeof line, f) != NULL) {
    double a, b;
    int n;
    if (sscanf(line, "center %lf %lf", &a, &b) == 2) {
      next.center.lat = a;
      next.center.lon = b;
    } else if (sscanf(line, "rings %d", &n) == 1 && n >= 1 && n <= MAX_RINGS) {
      next.count = n;
    } else if (sscanf(line, "spacing_nm %lf", &a) == 1 && a > 0) {
      next.spacing_nm = a;
    }
  }
  fclose(f);
  next.undo_count = 0;
  next.redo_count = 0;
  *r = next;
  return FV_OK;
}

static int rings_save(void* self, const char* path, int format_index) {
  const rings* r = (const rings*)self;
  FILE* f;
  (void)format_index;
  f = fopen(path, "w");
  if (f == NULL) {
    r->host->set_error("cannot write the file");
    return FV_ERR_IO;
  }
  fprintf(f, "center %.8f %.8f\nrings %d\nspacing_nm %g\n", r->center.lat, r->center.lon,
          r->count, r->spacing_nm);
  return fclose(f) == 0 ? FV_OK : FV_ERR_IO;
}

/** Feature 0 is the centre; feature n is ring n. */
static int rings_hit_test(void* self, const fv_view* view, double x, double y, double tolerance_px,
                          fv_hit* out) {
  rings* r = (rings*)self;
  fv_geo_point at, beside;
  double cx, cy, metres, m_per_px;
  int i;
  if (view->geo_to_screen(view, r->center, &cx, &cy) == FV_OK &&
      hypot(cx - x, cy - y) <= tolerance_px) {
    out->feature = 0;
    out->distance_px = hypot(cx - x, cy - y);
    out->tooltip = "Range rings centre";
    out->status = NULL;
    return 1;
  }
  if (view->screen_to_geo(view, x, y, &at) != FV_OK ||
      view->screen_to_geo(view, x + 1, y, &beside) != FV_OK)
    return 0;
  m_per_px = distance_m(at, beside);
  metres = distance_m(r->center, at);
  for (i = 1; i <= r->count; ++i) {
    const double off_px = fabs(metres - i * r->spacing_nm * METRES_PER_NM) / m_per_px;
    if (off_px <= tolerance_px) {
      snprintf(r->text, sizeof r->text, "Ring %d: %g nm", i, i * r->spacing_nm);
      out->feature = (uint64_t)i;
      out->distance_px = off_px;
      out->tooltip = r->text;
      out->status = NULL;
      return 1;
    }
  }
  return 0;
}

/** Get Info for the centre or the ring under (x, y): the ring's radius, then the centre and the set. */
static int rings_item_info(void* self, const fv_view* view, double x, double y,
                           double tolerance_px, const fv_info* info) {
  rings* r = (rings*)self;
  fv_hit hit;
  char value[64];
  if (!rings_hit_test(self, view, x, y, tolerance_px, &hit)) return 0;
  info->title(info, "Range Rings");
  if (hit.feature > 0) {
    snprintf(value, sizeof value, "%d", (int)hit.feature);
    info->row(info, "Ring", "Number", value);
    snprintf(value, sizeof value, "%g nm", (double)hit.feature * r->spacing_nm);
    info->row(info, "Ring", "Radius", value);
  }
  snprintf(value, sizeof value, "%.6f", r->center.lat);
  info->row(info, "Centre", "Latitude", value);
  snprintf(value, sizeof value, "%.6f", r->center.lon);
  info->row(info, "Centre", "Longitude", value);
  snprintf(value, sizeof value, "%d", r->count);
  info->row(info, "Rings", "Count", value);
  snprintf(value, sizeof value, "%g nm", r->spacing_nm);
  info->row(info, "Rings", "Spacing", value);
  info->edit_label(info, "Edit Rings\xE2\x80\xA6" /* UTF-8 ellipsis */);
  return 1;
}

static const char* rings_properties_schema(void* self) {
  (void)self;
  return "["
         "{\"key\":\"ring_count\",\"label\":\"Rings\",\"group\":\"Rings\",\"type\":\"int\","
         "\"default\":3,\"min\":1,\"max\":10},"
         "{\"key\":\"spacing_nm\",\"label\":\"Spacing (nm)\",\"group\":\"Rings\","
         "\"type\":\"double\",\"default\":1,\"min\":0.1,\"max\":500},"
         "{\"key\":\"line_color\",\"label\":\"Colour\",\"group\":\"Style\",\"type\":\"color\","
         "\"default\":\"#FF0000FF\"},"
         "{\"key\":\"labels\",\"label\":\"Label each ring\",\"group\":\"Style\","
         "\"type\":\"bool\",\"default\":true}"
         "]";
}

static const char* rings_get_property(void* self, const char* key) {
  rings* r = (rings*)self;
  if (strcmp(key, "ring_count") == 0) {
    snprintf(r->text, sizeof r->text, "%d", r->count);
  } else if (strcmp(key, "spacing_nm") == 0) {
    snprintf(r->text, sizeof r->text, "%.10g", r->spacing_nm);
  } else if (strcmp(key, "line_color") == 0) {
    snprintf(r->text, sizeof r->text, "#%02X%02X%02X%02X", r->color.r, r->color.g, r->color.b,
             r->color.a);
  } else if (strcmp(key, "labels") == 0) {
    snprintf(r->text, sizeof r->text, "%s", r->labels ? "true" : "false");
  } else {
    return NULL;
  }
  return r->text;
}

static int rings_set_property(void* self, const char* key, const char* value) {
  rings* r = (rings*)self;
  if (strcmp(key, "ring_count") == 0) {
    const long n = strtol(value, NULL, 10);
    r->count = n < 1 ? 1 : (n > MAX_RINGS ? MAX_RINGS : (int)n);
  } else if (strcmp(key, "spacing_nm") == 0) {
    const double s = strtod(value, NULL);
    if (!(s > 0)) {
      r->host->set_error("spacing must be positive");
      return FV_ERR_INVALID_ARG;
    }
    r->spacing_nm = s;
  } else if (strcmp(key, "line_color") == 0) {
    unsigned int c[4];
    if (sscanf(value, "#%2x%2x%2x%2x", &c[0], &c[1], &c[2], &c[3]) != 4) {
      r->host->set_error("expected #RRGGBBAA");
      return FV_ERR_INVALID_ARG;
    }
    r->color.r = (uint8_t)c[0];
    r->color.g = (uint8_t)c[1];
    r->color.b = (uint8_t)c[2];
    r->color.a = (uint8_t)c[3];
  } else if (strcmp(key, "labels") == 0) {
    r->labels = strcmp(value, "true") == 0;
  } else {
    return FV_ERR_NOT_FOUND;
  }
  r->host->set_dirty(r->ref, 1);
  return FV_OK;
}

/** A left click moves the centre to the clicked point. */
static int rings_on_mouse(void* self, const fv_view* view, const fv_mouse_event* e) {
  rings* r = (rings*)self;
  fv_geo_point at;
  if (view == NULL || e->kind != FV_MOUSE_DOWN || e->button != 0) return 0;
  if (view->screen_to_geo(view, e->x, e->y, &at) != FV_OK) return 0;
  move_centre(r, at);
  return 1;
}

/** While editing, any left press moves the centre. */
static const char* rings_edit_hint(void* self, double x, double y) {
  (void)self;
  (void)x;
  (void)y;
  return "Move the centre here";
}

/* MARK: Editor */

/** The palette shown while editing range rings. It holds no state of its own. */
typedef struct rings_editor {
  const fv_host_api* host;
  fv_editor_ref* ref;
} rings_editor;

static void* editor_create(const fv_host_api* host, fv_editor_ref* ref) {
  rings_editor* e = (rings_editor*)calloc(1, sizeof(rings_editor));
  if (e == NULL) {
    host->set_error("out of memory");
    return NULL;
  }
  e->host = host;
  e->ref = ref;
  return e;
}

static void editor_destroy(void* self) { free(self); }

static void editor_tools(void* self, const fv_menu* menu) {
  const rings_editor* e = (const rings_editor*)self;
  const rings* r = (const rings*)e->host->edited(e->ref);
  const int at_home = r != NULL && r->center.lat == HOME_LAT && r->center.lon == HOME_LON;
  menu->item(menu, "labels", "Show Labels", "resources/labels.svg",
             r == NULL ? FV_ITEM_DISABLED : (r->labels ? FV_ITEM_CHECKED : 0));
  menu->item(menu, "reset", "Reset Centre", "recenter",
             r == NULL || at_home ? FV_ITEM_DISABLED : 0);
}

static int editor_run_tool(void* self, const char* id) {
  const rings_editor* e = (const rings_editor*)self;
  rings* r = (rings*)e->host->edited(e->ref);
  if (r == NULL) return FV_ERR_NOT_FOUND;
  if (strcmp(id, "labels") == 0) {
    r->labels = !r->labels;
  } else if (strcmp(id, "reset") == 0) {
    fv_geo_point home;
    home.lat = HOME_LAT;
    home.lon = HOME_LON;
    move_centre(r, home);
  } else {
    return FV_ERR_NOT_FOUND;
  }
  e->host->request_redraw(r->ref);
  return FV_OK;
}

/* MARK: Tool */

/** Opens the sample rings shipped in the package's resources. */
static int open_sample(const fv_tool_context* c) {
  char path[1024];
  snprintf(path, sizeof path, "%s/resources/charleston.rng", c->host->package_dir);
  return c->open_file(c, path);
}

/* MARK: Entry point */

static fv_overlay_class overlays[1];
static fv_editor_class editors[1];
static const fv_tool_class kTools[] = {{sizeof(fv_tool_class), "open_sample", open_sample}};

FV_PLUGIN_EXPORT int fv_plugin_entry(const fv_host_api* host, fv_plugin_api* out) {
  fv_overlay_class* o = &overlays[0];
  fv_editor_class* e = &editors[0];
  if (host->api_major != FV_PLUGIN_API_MAJOR) {
    host->set_error("rangerings needs plugin API 2");
    return FV_ERR_UNSUPPORTED;
  }
  o->struct_size = sizeof(fv_overlay_class);
  o->name = "rings";
  o->create = rings_create;
  o->destroy = rings_destroy;
  o->draw = rings_draw;
  o->open = rings_open;
  o->save = rings_save;
  o->hit_test = rings_hit_test;
  o->properties_schema = rings_properties_schema;
  o->get_property = rings_get_property;
  o->set_property = rings_set_property;
  o->on_mouse = rings_on_mouse;
  o->edit_hint = rings_edit_hint;
  o->item_info = rings_item_info;
  o->can_undo = rings_can_undo;
  o->undo = rings_undo;
  o->can_redo = rings_can_redo;
  o->redo = rings_redo;
  e->struct_size = sizeof(fv_editor_class);
  e->name = "rings_editor";
  e->create = editor_create;
  e->destroy = editor_destroy;
  e->cursor = FV_CURSOR_CROSSHAIR;
  e->tools = editor_tools;
  e->run_tool = editor_run_tool;

  out->api_major = FV_PLUGIN_API_MAJOR;
  out->api_minor = FV_PLUGIN_API_MINOR;
  out->overlays = overlays;
  out->overlay_count = 1;
  out->editors = editors;
  out->editor_count = 1;
  out->tools = kTools;
  out->tool_count = sizeof kTools / sizeof kTools[0];
  return FV_OK;
}
