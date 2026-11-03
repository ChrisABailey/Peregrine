/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (C) 2026 Chris Bailey
 * Part of Peregrine, a cross-platform port of FalconView(tm).
 * See COPYING.LESSER and NOTICE.md for the full licensing picture. */

/**
 * fv_plugin.h — the C plugin API for Peregrine overlay packages (`library`
 * kind, port/extensions-plan.md §2c).
 *
 * C99, UTF-8 strings, no C++ types. A plugin exports one function,
 * `fv_plugin_entry`, which receives the host's function table and fills in
 * the plugin's. Every struct that crosses the boundary starts with
 * `struct_size`; a later minor version only appends fields, and each side
 * reads a field only when the other side's `struct_size` covers it
 * (`FV_HAS_FIELD`).
 *
 * Threading: `draw` runs on the render worker; every other call, including
 * `hit_test` and tools, runs on the UI thread. A plugin never calls the UI.
 *
 * Memory: what the host passes is valid for the duration of the call. Strings
 * a plugin returns are owned by the plugin and must stay valid until its next
 * call on the same object (or, for class-level strings, for the life of the
 * process).
 *
 * Errors: functions returning `int` return FV_OK (0) or a negative
 * `fv_status`. Before returning an error a plugin may call
 * `host->set_error(msg)`; the message reaches the log and the user.
 */
#ifndef PEREGRINE_FV_PLUGIN_H_
#define PEREGRINE_FV_PLUGIN_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FV_PLUGIN_API_MAJOR 1
#define FV_PLUGIN_API_MINOR 0

#if defined(_WIN32)
#define FV_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FV_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/** True when `s` (a pointer to a struct with `struct_size`) includes `field`. */
#define FV_HAS_FIELD(s, type, field) \
  ((s)->struct_size >= offsetof(type, field) + sizeof(((type*)0)->field))

/** Status codes; the same values as fv::StatusCode. */
typedef enum fv_status {
  FV_OK = 0,
  FV_ERR_INVALID_ARG = -1,
  FV_ERR_NOT_FOUND = -2,
  FV_ERR_IO = -3,
  FV_ERR_UNSUPPORTED = -4,
  FV_ERR_OUT_OF_COVERAGE = -5,
  FV_ERR_INTERRUPTED = -6,
  FV_ERR_NOT_PROJECTABLE = -7,
  FV_ERR_INTERNAL = -100
} fv_status;

typedef enum fv_log_level {
  FV_LEVEL_DEBUG = 0,
  FV_LEVEL_INFO = 1,
  FV_LEVEL_WARNING = 2,
  FV_LEVEL_ERROR = 3
} fv_log_level;

/* MARK: Host */

/** What the host offers every plugin. One table per loaded library, valid for
 *  the life of the process. */
typedef struct fv_host_api {
  uint32_t struct_size;
  uint32_t api_major, api_minor; /**< what the host implements */
  const char* host_version;      /**< "1.1.0" */
  const char* package_dir;       /**< the package root; resources resolve against it */
  void (*log)(int level, const char* message);
  /** Attaches a message to the error the current call is about to return. */
  void (*set_error)(const char* message);
} fv_host_api;

/* MARK: The view */

typedef struct fv_color {
  uint8_t r, g, b, a;
} fv_color;

typedef struct fv_geo_point {
  double lat, lon; /**< degrees, WGS 84 */
} fv_geo_point;

typedef enum fv_projection {
  FV_PROJ_EQUAL_ARC = 0,
  FV_PROJ_MERCATOR = 1,
  FV_PROJ_LAMBERT = 2,
  FV_PROJ_AZIMUTHAL_EQUIDISTANT = 3,
  FV_PROJ_ORTHOGRAPHIC = 4
} fv_projection;

/** The map as it is drawn: surface size, extent and scale, plus conversions.
 *  Surface pixels have x rightward and y downward from the top-left. */
typedef struct fv_view {
  uint32_t struct_size;
  int width, height;          /**< surface pixels */
  fv_geo_point south_west;    /**< the surface's geographic bounds; west > east */
  fv_geo_point north_east;    /**<   when they cross the antimeridian */
  double scale_denominator;   /**< 1:N at the centre; 0 when the view has no map scale */
  int projection;             /**< an fv_projection */
  double rotation_deg;        /**< map rotation, clockwise */
  double dpi_scale;           /**< device pixels per layout pixel; 1 on a standard display */
  const void* host;           /**< opaque; pass back to the functions below */
  /** FV_OK, or FV_ERR_NOT_PROJECTABLE for a point with no image in the projection. */
  int (*geo_to_screen)(const struct fv_view* view, fv_geo_point p, double* x, double* y);
  int (*screen_to_geo)(const struct fv_view* view, double x, double y, fv_geo_point* out);
} fv_view;

/* MARK: Drawing */

typedef enum fv_line_kind {
  FV_LINE_SIMPLE = 0, /**< straight on the surface */
  FV_LINE_RHUMB = 1,  /**< constant bearing */
  FV_LINE_GREAT = 2   /**< great circle */
} fv_line_kind;

typedef struct fv_line_style {
  uint32_t struct_size;
  fv_color color;
  double width_px;
  /** null or "solid", or a preset: "dash", "long-dash", "dot", "dash-dot",
   *  "railroad", "arrow", "tick", "notch", "feba". */
  const char* preset;
  fv_color casing_color;
  double casing_px; /**< extra width of a casing drawn beneath the line; 0 for none */
} fv_line_style;

typedef struct fv_fill_style {
  uint32_t struct_size;
  fv_color color;
} fv_fill_style;

/** Symbols in API 1 are Peregrine's built-in shapes: "fv.circle", "fv.square",
 *  "fv.triangle", "fv.diamond", "fv.cross", "fv.star", "fv.north",
 *  "fv.crosshair", "fv.ownship". */
typedef struct fv_symbol_style {
  uint32_t struct_size;
  fv_color color;
  double rotation_deg; /**< clockwise from north-up */
  double scale;        /**< 1 = nominal size; 0 is taken as 1 */
} fv_symbol_style;

typedef enum fv_halign { FV_HALIGN_LEFT = 0, FV_HALIGN_CENTER = 1, FV_HALIGN_RIGHT = 2 } fv_halign;
typedef enum fv_valign {
  FV_VALIGN_BASELINE = 0,
  FV_VALIGN_BOTTOM = 1,
  FV_VALIGN_CENTER = 2,
  FV_VALIGN_TOP = 3
} fv_valign;

typedef struct fv_label_style {
  uint32_t struct_size;
  fv_color color;
  double size_px;      /**< 0 is taken as 12 */
  fv_color halo_color;
  double halo_px;      /**< 0 for no halo */
  int halign, valign;  /**< fv_halign, fv_valign */
  int dx, dy;          /**< pixel offset from the anchor */
} fv_label_style;

/** Drawing for one `draw` call. Geographic shapes are clipped and projected
 *  by the host; sizes in metres are on the ground. */
typedef struct fv_draw_api {
  uint32_t struct_size;
  const void* host; /**< opaque; pass back to every function */
  int (*polyline)(const struct fv_draw_api* d, const fv_geo_point* points, size_t count,
                  int closed, int line_kind, const fv_line_style* style);
  /** Fills a ring (edges straight on the surface), then outlines it when
   *  `outline` is non-null. Either style may be null. */
  int (*polygon)(const struct fv_draw_api* d, const fv_geo_point* points, size_t count,
                 const fv_fill_style* fill, const fv_line_style* outline);
  int (*circle)(const struct fv_draw_api* d, fv_geo_point center, double radius_m,
                const fv_line_style* style);
  int (*ellipse)(const struct fv_draw_api* d, fv_geo_point center, double vert_radius_m,
                 double horz_radius_m, double rotation_deg, const fv_line_style* style);
  /** Clockwise from `start_bearing_deg` through `sweep_deg`. */
  int (*arc)(const struct fv_draw_api* d, fv_geo_point center, double radius_m,
             double start_bearing_deg, double sweep_deg, const fv_line_style* style);
  int (*symbol)(const struct fv_draw_api* d, fv_geo_point at, const char* symbol_id,
                const fv_symbol_style* style);
  int (*symbol_px)(const struct fv_draw_api* d, double x, double y, const char* symbol_id,
                   const fv_symbol_style* style);
  int (*label)(const struct fv_draw_api* d, fv_geo_point at, const char* utf8,
               const fv_label_style* style);
  int (*label_px)(const struct fv_draw_api* d, double x, double y, const char* utf8,
                  const fv_label_style* style);
  /** Nonzero when the frame has been superseded; stop drawing and return
   *  FV_ERR_INTERRUPTED. Check it between features. */
  int (*cancelled)(const struct fv_draw_api* d);
} fv_draw_api;

/* MARK: Events */

typedef enum fv_mouse_kind {
  FV_MOUSE_MOVE = 0,
  FV_MOUSE_DOWN = 1,
  FV_MOUSE_UP = 2,
  FV_MOUSE_DOUBLE_CLICK = 3,
  FV_MOUSE_WHEEL = 4
} fv_mouse_kind;

typedef struct fv_mouse_event {
  uint32_t struct_size;
  int kind;            /**< fv_mouse_kind */
  int x, y;            /**< surface pixels */
  int button;          /**< 0 left, 1 middle, 2 right */
  int shift, ctrl;     /**< nonzero when held */
  int clicks;          /**< 2 on the second press of a double-click */
  double wheel_delta;  /**< FV_MOUSE_WHEEL only */
} fv_mouse_event;

/** What `hit_test` reports about the item under the cursor. */
typedef struct fv_hit {
  uint32_t struct_size;
  uint64_t feature;   /**< the plugin's own item id */
  double distance_px; /**< from the cursor, for nearest-wins */
  const char* tooltip; /**< may be null; plugin-owned */
  const char* status;  /**< status-bar text; may be null; plugin-owned */
} fv_hit;

/* MARK: Overlays */

/**
 * One overlay class. `create`, `destroy` and `draw` are required; a null
 * optional function means the capability is absent.
 *
 * Properties: `properties_schema` returns a JSON array of
 *   {"key", "label", "group", "type", "default", "min", "max", "choices", "help"}
 * with "type" one of "bool", "int", "double", "string", "color", "choice",
 * "path". A value crosses as text: "true"/"false", a decimal number, the
 * choice's index, "#RRGGBBAA", or the string itself.
 */
typedef struct fv_overlay_class {
  uint32_t struct_size;
  const char* name; /**< what the manifest's implementation.library names */
  void* (*create)(const fv_host_api* host, const char* type_id);
  void (*destroy)(void* self);
  int (*draw)(void* self, const fv_view* view, const fv_draw_api* draw);
  /* Optional. */
  int (*open)(void* self, const char* path);
  int (*save)(void* self, const char* path);
  /** 1 and fills `out` when an item lies within `tolerance_px` of (x, y); 0 otherwise. */
  int (*hit_test)(void* self, const fv_view* view, double x, double y, double tolerance_px,
                  fv_hit* out);
  const char* (*properties_schema)(void* self);
  /** The value as text, or null for an unknown key. */
  const char* (*get_property)(void* self, const char* key);
  int (*set_property)(void* self, const char* key, const char* value);
  /** `view` is the last frame drawn, or null before the first. Nonzero = handled. */
  int (*on_mouse)(void* self, const fv_view* view, const fv_mouse_event* event);
} fv_overlay_class;

/* MARK: Tools */

/** What a running tool may do. Valid for the duration of `run`. */
typedef struct fv_tool_context {
  uint32_t struct_size;
  const fv_host_api* host;
  const void* ctx; /**< opaque; pass back to the functions below */
  /** Opens `path` as an overlay of the type that claims its extension. */
  int (*open_file)(const struct fv_tool_context* c, const char* path);
  void (*request_redraw)(const struct fv_tool_context* c);
} fv_tool_context;

typedef struct fv_tool_class {
  uint32_t struct_size;
  const char* name; /**< what the manifest's implementation.library names */
  int (*run)(const fv_tool_context* context);
} fv_tool_class;

/* MARK: Entry point */

/** What a plugin hands back from its entry point. The arrays must stay valid
 *  for the life of the process (static storage). */
typedef struct fv_plugin_api {
  uint32_t struct_size;
  uint32_t api_major, api_minor; /**< what the plugin was built against */
  const fv_overlay_class* overlays;
  size_t overlay_count;
  const fv_tool_class* tools;
  size_t tool_count;
} fv_plugin_api;

/** The one symbol the host looks up. `out` arrives zeroed with its
 *  struct_size set. Returns FV_OK, or an error if the plugin refuses this
 *  host (after saying why through host->set_error). */
typedef int (*fv_plugin_entry_fn)(const fv_host_api* host, fv_plugin_api* out);
#define FV_PLUGIN_ENTRY_SYMBOL "fv_plugin_entry"

FV_PLUGIN_EXPORT int fv_plugin_entry(const fv_host_api* host, fv_plugin_api* out);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* PEREGRINE_FV_PLUGIN_H_ */
