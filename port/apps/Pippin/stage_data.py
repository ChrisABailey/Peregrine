#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Stage Pippin's offline data pack (Pippin plan, P1).

Pippin is offline BY CONSTRUCTION: there is no network code in v1, so
everything the app reads has to be inside the .app bundle. This script
assembles that bundle directory out of the working tree.

    python3 port/apps/Pippin/stage_data.py            # stage, then verify
    python3 port/apps/Pippin/stage_data.py --check    # verify only
    python3 port/apps/Pippin/stage_data.py --release  # the pack that ships
    python3 port/apps/Pippin/stage_data.py --regen-points   # redo the points

TWO OF THOSE FLAGS ARE ABOUT WHAT MUST NOT HAPPEN BY ACCIDENT. `--release`
leaves out the rows that exist to measure the app rather than to ride with it
(today: the 407 KB demo ride, reachable only from a `#if DEBUG` launch
argument). And the point set is NOT rewritten when the pack already has one,
because the shipping set is hand-edited and lives nowhere else — `Data/` is
git-ignored, so a silent rewrite is a silent loss. `--regen-points` is the
deliberate way back to the generated stand-in.

The pack itself (`port/apps/Pippin/Data/`) is GIT-IGNORED — it is a few MB of
build artifacts cut out of git-ignored `testdata/`, and the repo has been
through one purge of committed binaries already. What is tracked is this
script, `pippin.ini`, and `README.md`: the pack is reproducible from them.

THE MANIFEST IS BELOW AND IS THE POINT OF THE FILE. Adding an asset means
adding a row, and every row that `pippin.ini` names by a bundle-relative path
is checked to exist after staging — a settings key pointing at nothing is the
failure mode a phone reports as a blank map at 3 pm on a bike.
"""

import argparse
import configparser
import glob
import json
import os
import shutil
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


class Item:
    """One staged file: where it comes from, where it lands, why it is here."""

    def __init__(self, src, dst, why, required=True, debug_only=False):
        self.src = src      # repo-relative
        self.dst = dst      # pack-relative
        self.why = why
        self.required = required
        # A measuring instrument rather than a feature: staged for
        # development and left out by `--release`, because nothing a rider
        # can reach names it. See `--release` in `main`.
        self.debug_only = debug_only


MANIFEST = [
    Item("testdata/OSM/kiawah.mbtiles", "kiawah.mbtiles",
         "the map, cut from us-south by port/tools/mbtiles_cut.py"),
    Item("testdata/OSM/kiawah.fvroad", "kiawah.fvroad",
         "the road graph, built by fvgraph HONOURING access"),
    Item("port/Osm/styles/style.json", "style.json",
         "the look pippin.ini selects: CyclOSM, tuned for riding"),
    # The sheet style.json's `sprite` names. That value is RELATIVE, and the
    # engine resolves it against the directory of the style file it read — so
    # the sheet has to land in `symbols/` beside the style, under the sheet's
    # own two names. Both halves are required: PngSymbolLibrary opens the
    # sheet as a .png/.json PAIR, and half a pair is no sheet at all.
    Item("port/Osm/styles/symbols/osm-liberty-topo.png",
         "symbols/osm-liberty-topo.png",
         "the icon artwork style.json's `sprite` points at"),
    Item("port/Osm/styles/symbols/osm-liberty-topo.json",
         "symbols/osm-liberty-topo.json",
         "and its index, without which the artwork is one opaque bitmap"),
    Item("port/Osm/styles/peregrine-osm.json", "peregrine-osm.json",
         "the plainer look, and PPMap's compiled-in fallback if osm.style goes"),
    Item("port/Routing/rules/route-weights.json", "route-weights.json",
         "the cost rules the walk and cycle profiles come out of"),
    # The demo feed: a real 28-minute Kiawah ride, so the ownship can be seen
    # on a phone nowhere near the island. In the pack rather than the Xcode
    # target because it is data the settings file names, and everything
    # pippin.ini names lives here.
    Item("testdata/kiawah_cycle.gpx", "kiawah_cycle.gpx",
         "the demo feed: a real ride, replayed at the speed it was ridden",
         debug_only=True),
    Item("port/apps/Pippin/pippin.ini", "pippin.ini",
         "the settings file, every path in it bundle-relative"),
    # The label face and its licence, which travel together: the Bitstream
    # Vera licence DejaVu is under permits redistribution only with the
    # notice attached, so staging the TTF without LICENSE_DEJAVU would ship
    # a font we do not have permission to ship.
    Item("port/apps/Pippin/fonts/DejaVuSans.ttf", "fonts/DejaVuSans.ttf",
         "the label face; iOS gives an app no readable path to a system one"),
    Item("port/apps/Pippin/fonts/LICENSE_DEJAVU", "fonts/LICENSE_DEJAVU",
         "and the licence that lets it be in the bundle at all"),
]

# Where to find the font when `port/apps/Pippin/fonts/` is empty. That
# directory is git-ignored (it is 756 KB of binary, and this repo has been
# through one purge of committed binaries), so a fresh checkout has to source
# the face from somewhere — and every one of these is a copy already sitting
# on a normal development machine. Nothing here reaches the network: if none
# of them exists the script says what to download and stops.
FONT_SOURCES = [
    "/usr/local/lib/python3.9/site-packages/matplotlib/mpl-data/fonts/ttf",
    "/opt/homebrew/lib/python3*/site-packages/matplotlib/mpl-data/fonts/ttf",
    "/usr/share/fonts/truetype/dejavu",
    "/usr/local/share/fonts/dejavu",
]
FONT_FILES = ["DejaVuSans.ttf", "LICENSE_DEJAVU"]

# Settings keys whose value is a bundle-relative path into the pack. Checked
# after staging, which is what keeps pippin.ini and MANIFEST from drifting.
PATH_KEYS = ["pippin.mbtiles", "pippin.font", "osm.style",
             "routing.graph", "routing.rules", "movingmap.demo_track",
             "points.seed"]

# The keys whose file only exists in a development pack. `pippin.ini` is one
# tracked file for both packs, so it names the demo track either way; under
# `--release` the file is deliberately absent and a dangling-path complaint
# would be the script objecting to what it was just told to do.
DEBUG_PATH_KEYS = ["movingmap.demo_track"]


# ---------------------------------------------------------------------------
# The point set
# ---------------------------------------------------------------------------
#
# Generated rather than copied, and the only item here that is. A `.fvpoints`
# document is a SQLite database, which is why the format was chosen, so
# Python's own sqlite3 writes it with no tool of ours and no C++ in the loop.
# Staging a binary out of `testdata/` instead would put a generated artifact
# in a manifest of real data and leave nobody anywhere to edit the set.
#
# The schema is written out here, which duplicates the authority in
# `port/fvkit/overlay/point_overlay.cpp`'s `kSchema`. That is tolerable
# because the reader is defensive by design, probing for each column and
# defaulting a missing one, so a schema that grows leaves this file writing a
# valid older document rather than a broken new one. If they disagree, the C++
# is right.
#
# The contact details are deliberately not real. These are real places on a
# real island, and real telephone numbers would mean a tap on a sample marker
# ringing a business that never asked to be in a demo file. The phones are in
# the reserved 555 range and the hosts are `.invalid`, reserved by RFC 2606
# and resolving nowhere, so they exercise the tap-to-dial and tap-to-open
# paths exactly.
POINTS_SCHEMA = """
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS points(
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL DEFAULT '',
  lat REAL NOT NULL, lon REAL NOT NULL,
  shape TEXT NOT NULL DEFAULT 'circle',
  size_px REAL NOT NULL DEFAULT 9,
  color TEXT NOT NULL DEFAULT '#c82828',
  category TEXT NOT NULL DEFAULT '',
  elevation_ft REAL NOT NULL DEFAULT 0,
  remarks TEXT NOT NULL DEFAULT '',
  symbol_id INTEGER NOT NULL DEFAULT 0,
  phone TEXT NOT NULL DEFAULT '',
  url TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS symbols(
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  format TEXT NOT NULL DEFAULT 'png',
  pixel_ratio REAL NOT NULL DEFAULT 1,
  pivot_x REAL, pivot_y REAL,
  image BLOB NOT NULL);
"""

# The embedded palette. `points.symbol_id` is a foreign key into the
# document's own `symbols` table, which holds each icon's PNG bytes as a blob,
# rather than a path into a directory. That is the point of schema 2: a
# `.fvpoints` file opens with its symbology on a machine that has never heard
# of the icon set it was authored against. So an icon enters a document by
# being read and embedded here, once, at staging time.
#
# The table is separate from `points` because many points share one symbol:
# two beaches are one row and two references, one decode and one cached tile.
#
# It is a palette rather than a projection of the points — a row nothing
# references is kept, saved and handed back — which lets the app's symbol
# picker offer a set the author assembled before any point wears it.
# Everything below the first blank line is in that category.
#
# Forty rather than all 216, because the whole maki set is 868 KB and the
# store copies the seed into Documents/ on the first launch, so every byte
# here is a byte on the phone twice. Forty is a picker somebody can scroll
# without a search field. Adding one is adding a line: the names are the maki
# file stems in `testdata/GeoSymbol/makiPng`, and an icon that is not there is
# skipped rather than fatal.
# A marker's full width in authored pixels, which `PPMap` turns into that many
# iOS points through the overlay's `symbol_dpi_scale`, so it is a physical
# size on any screen rather than a count of device pixels.
#
# It also sizes the icon, which is the thing to know before changing it. An
# embedded icon is stamped at `kIconFractionOfBadge` = 0.62 of the badge,
# pulled in that far so a diamond still reads as a diamond, so:
#
#     12 -> a  7.4-point glyph   maki's 32-px artwork does not resolve here
#     22 -> a 13.6-point glyph   legible, small
#     26 -> a 16.1-point glyph   about the size maki is drawn at on a web map
#
# It is also the target: `points.hit_tolerance` (22 points) is added to the
# marker's drawn half-width, so 12 gives a 28-point target and 26 gives 35.
MARKER_SIZE_PX = 18.0

ICON_DIR = "testdata/GeoSymbol/makiPng"
ICONS = [
    # The ones POINTS below actually wear.
    "bicycle", "beach", "golf", "picnic-site", "slipway", "danger",
    "drinking-water", "shop", "viewpoint",
    # And the rest of the starter palette, for the picker.
    "cafe", "restaurant", "fast-food", "bar", "ice-cream", "bakery",
    "grocery", "lodging", "parking", "toilet", "fuel", "bank",
    "hospital", "pharmacy", "veterinary", "police", "fire-station", "post",
    "park", "garden", "playground", "dog-park", "campsite", "swimming",
    "harbor", "ferry", "lighthouse", "water", "attraction", "information",
    "bus", "car", "marker", "star",
]

# name, lat, lon, shape, colour, category, remarks, phone, url, icon.
#
# The ICON is a name from `ICONS` above, or "" for a bare shape. It is a NAME
# and not an id because an id is a position in a list, and a list somebody
# reorders would silently re-skin every point in the file.
#
# INSIDE `pippin.home_bounds`, every one of them: a marker outside the pack's
# extent draws over an empty background, which reads as a bug rather than as
# a point somewhere else. The route's own two ends are here (the demo ride
# starts and finishes at them) because those are the two a rider actually
# navigates to, and a point that snaps a waypoint is the requirement's own
# example of what this set is for.
POINTS = [
    ("Ruddy Turnstone", 32.6044007, -80.1083007, "diamond", "#c82828",
     "junction", "great place to live", "", "", "lodging"),
    ("Beach Access 12", 32.5957369, -80.1097501, "diamond", "#c82828",
     "junction", "east end of the demo route", "", "", "beach"),
    ("Freshfields Village", 32.60710, -80.14820, "circle", "#8246a0",
     "services", "shops, coffee, and the only cash machine",
     "+1 843-555-0142", "https://freshfields.example.invalid/", "shop"),
    ("Beachwalker Park", +32.58778, -80.13069, "circle", "#1e8c46",
     "park", "public beach showers and a car park at the west end",
     "+1 843-555-0177", "https://beachwalker.example.invalid/", "picnic-site"),
    ("The Ocean Course", +32.61203, -80.02327, "circle", "#1e8c46",
     "landmark", "", "+1 800-576-1570", "https://kiawahresort.com/golf/the-ocean-course/",
     "golf"),
    ("Retts Bluff", 32.61550, -80.08881, "square", "#1e8c46",
     "landing", "Boats must be registered with the Kiawah Island Community Association to use the launch facilities", "", "",
     "slipway"),
    ("Sandcatle", 32.6162, -80.1509, "circle", "#e1a01e",
     "club", "Property owners Beachclub", "", "https://kica.us/beach-cam/",
     "beach"),
    ("Night Heron Park", 32.6013, -80.0999, "circle", "#1e8c46",
     "park", "market / cafe / activities",
     "", "", "cafe"),
    ("Bike repair", +32.61851, -80.15235, "star", "#4a5a6e",
     "services", "rentals and repairs (includes pickup/delivery)",
     "843.768.1158", "https://www.islandbikeandsurf.com/bike-repairs.php", "bicycle"),
    ("Sandy Point", 32.6205, -80.1379, "cross", "#4a5a6e",
     "viewpoint", "turn here for the beach path", "", "", "viewpoint"),
]


def build_points_seed(pack, rel, regenerate=False):
    """Write the pack's `.fvpoints` starter set. Returns the path, or None.

    None when a set is already there, which is the case that matters: the
    shipping starter points are HAND-AUTHORED. `POINTS` below is a generated
    stand-in — real places with reserved 555 numbers and `.invalid` hosts —
    and the set that goes to the store is that file opened in a SQLite editor
    and corrected: the actual telephone numbers, the real links, the names
    spelled the way the island spells them. Rewriting the pack's copy on
    every stage would throw that work away, silently, in the middle of a
    build somebody ran for an unrelated reason.

    SO AN EXISTING FILE WINS AND `--regen-points` IS THE WAY BACK. It is not
    the same safety the note here used to claim: the pack's copy is the one
    the app installs into `Documents/` on first launch, so it is not a user's
    edited document — but it IS a hand-edited SOURCE that lives nowhere else,
    because `Data/` is git-ignored. Keep a copy outside the pack if it
    matters, and `--regen-points` when the schema or `POINTS` moves.
    """
    dst = os.path.join(pack, rel)
    os.makedirs(os.path.dirname(dst) or pack, exist_ok=True)
    if os.path.exists(dst):
        if not regenerate:
            return None
        os.remove(dst)
    db = sqlite3.connect(dst)
    try:
        db.executescript(POINTS_SCHEMA)
        db.execute("INSERT OR REPLACE INTO meta VALUES('schema_version','3')")
        db.execute("INSERT OR REPLACE INTO meta VALUES('name',?)",
                   ("Kiawah",))

        # The palette FIRST, so the file is never — even mid-transaction — a
        # set of points referencing rows that are not there yet. `symbol_id`
        # is then looked up by NAME, so an icon missing from ICON_DIR leaves a
        # hole and the points that wanted it keep their shapes, rather than
        # shifting every icon after it onto the wrong points.
        ids = {}
        missing = []
        for icon_name in ICONS:
            path = os.path.join(ROOT, ICON_DIR, icon_name + ".png")
            try:
                with open(path, "rb") as f:
                    blob = f.read()
            except OSError:
                blob = b""
            if not blob:
                missing.append(icon_name)
                continue
            cur = db.execute(
                "INSERT INTO symbols(name, format, pixel_ratio, image)"
                " VALUES(?, 'png', 1, ?)",
                (icon_name, sqlite3.Binary(blob)))
            ids[icon_name] = cur.lastrowid
        if missing:
            print("  points: no artwork for %s (those points draw as shapes)"
                  % ", ".join(missing))

        for i, row in enumerate(POINTS, start=1):
            (name, lat, lon, shape, color, category, remarks, phone, url,
             icon) = row
            db.execute(
                "INSERT INTO points(id, name, lat, lon, shape, size_px, color,"
                " category, elevation_ft, remarks, symbol_id, phone, url)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (i, name, lat, lon, shape, MARKER_SIZE_PX, color, category,
                 0.0, remarks, ids.get(icon, 0), phone, url))
        db.commit()
    finally:
        db.close()
    return dst


def check_sprite(pack, style_rel):
    """The style's own `sprite` is a path too, and nothing else checks it.

    A GL style names its icon sheet inside itself rather than in pippin.ini,
    so PATH_KEYS cannot see it, and OsmStyleEngine treats a DERIVED sprite
    base that will not open as a non-error — a style published without its
    sheet still draws, just with no icons. That is the right call for the
    engine and the wrong one for a pack: here it means the app comes up, the
    map draws, and the icons are simply absent, which is a bug that looks
    like a style choice. So the sheet gets the same teeth as the ini paths.

    Returns a list of missing pack-relative paths.
    """
    style_path = os.path.join(pack, style_rel)
    if not os.path.isfile(style_path):
        return []                       # PATH_KEYS already reported this one
    try:
        with open(style_path) as f:
            sprite = json.load(f).get("sprite", "")
    except (ValueError, OSError) as e:
        print("  UNREADABLE  %s (%s)" % (style_rel, e))
        return [style_rel]
    if not sprite:
        return []                       # a style with no icons is a fine style
    if sprite.startswith(("http://", "https://", "mapbox://")):
        # Pippin is offline by construction; the engine resolves these to
        # nothing and the icons go unresolved, silently.
        print("  REMOTE SPRITE  %s names %s, which nothing in Pippin fetches"
              % (style_rel, sprite))
        return [sprite]
    # GL's `sprite` is a URL without an extension, resolved relative to the
    # style file — which in the pack is the pack root.
    base = os.path.normpath(os.path.join(os.path.dirname(style_rel), sprite))
    missing = []
    for ext in (".png", ".json"):
        if not os.path.isfile(os.path.join(pack, base + ext)):
            print("  DANGLING    sprite %s (named by %s)" % (base + ext,
                                                             style_rel))
            missing.append(base + ext)
    if not missing:
        ids = 0
        try:
            with open(os.path.join(pack, base + ".json")) as f:
                ids = len(json.load(f))
        except (ValueError, OSError):
            pass
        print("  sprite sheet %s: %d icon(s)" % (base, ids))
    return missing


def read_ini_paths(ini_path):
    """The dotted key -> value map fv::Settings would build from this file."""
    cp = configparser.ConfigParser()
    # fv::Settings is case-preserving on values and takes quoted strings.
    cp.optionxform = str
    with open(ini_path) as f:
        cp.read_file(f)
    out = {}
    for section in cp.sections():
        for key, value in cp.items(section):
            out["%s.%s" % (section, key)] = value.strip().strip('"')
    return out


def fetch_font():
    """Put the label face in `fonts/` from a copy already on this machine.

    A no-op once the directory has one. This exists because the alternative
    is a session that stops to ask a human to download a font, which is what
    P1 did and what P2 had to finish.
    """
    dest = os.path.join(HERE, "fonts")
    if all(os.path.isfile(os.path.join(dest, f)) for f in FONT_FILES):
        return
    for pattern in FONT_SOURCES:
        for src_dir in sorted(glob.glob(pattern)):
            if not all(os.path.isfile(os.path.join(src_dir, f))
                       for f in FONT_FILES):
                continue
            os.makedirs(dest, exist_ok=True)
            for f in FONT_FILES:
                shutil.copy2(os.path.join(src_dir, f), os.path.join(dest, f))
            print("  fonts/               <- %s" % src_dir)
            return
    print("  no DejaVu Sans on this machine. Put DejaVuSans.ttf and its\n"
          "  licence in port/apps/Pippin/fonts/ (dejavu-fonts.github.io),\n"
          "  or point FONT_SOURCES at a copy.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pack", default=os.path.join(HERE, "Data"),
                    help="where to stage (default port/apps/Pippin/Data)")
    ap.add_argument("--check", action="store_true",
                    help="verify an already-staged pack, copy nothing")
    # What ships is a smaller pack than what is developed against. The demo
    # ride is 407 KB of the 3 MB and is reachable only from a launch argument
    # a Release build does not read, since the probe screens and `-PPDemoFeed`
    # are `#if DEBUG`, so in a store build it is bytes on every phone for no
    # rider's benefit.
    ap.add_argument("--release", action="store_true",
                    help="leave out the development-only rows (the demo ride)")
    ap.add_argument("--regen-points", action="store_true",
                    help="rewrite kiawah.fvpoints from POINTS, discarding "
                         "a hand-edited set already in the pack")
    args = ap.parse_args()

    if not args.check:
        fetch_font()

    missing = []
    staged = 0
    total_bytes = 0
    for item in MANIFEST:
        if item.debug_only and args.release:
            # Said out loud, and the removal is real: a pack staged without
            # `--release` earlier is still sitting there with the file in it.
            dst = os.path.join(args.pack, item.dst)
            if os.path.isfile(dst) and not args.check:
                os.remove(dst)
                print("  %-22s removed  (--release: %s)" % (item.dst, item.why))
            else:
                print("  %-22s skipped  (--release: %s)" % (item.dst, item.why))
            continue
        src = os.path.join(ROOT, item.src)
        dst = os.path.join(args.pack, item.dst)
        if not os.path.isfile(src):
            if args.check and os.path.isfile(dst):
                continue  # already staged from a source since moved away
            if item.required:
                missing.append(item.src)
            print("  MISSING%s  %s" % ("" if item.required else " (optional)",
                                       item.src))
            continue
        if not args.check:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
        if not os.path.isfile(dst):
            missing.append(item.dst)
            print("  NOT STAGED  %s" % item.dst)
            continue
        size = os.path.getsize(dst)
        total_bytes += size
        staged += 1
        print("  %-22s %8.1f KB  %s" % (item.dst, size / 1024.0, item.why))

    # The teeth: every path pippin.ini names must be in the pack.
    ini = os.path.join(args.pack, "pippin.ini")
    if os.path.isfile(ini):
        values = read_ini_paths(ini)
        # The point set is GENERATED rather than copied, so it is built here
        # — after the manifest (which staged pippin.ini, where its name is)
        # and before the dangling-path check, which is what gives it teeth.
        seed_rel = values.get("points.seed")
        if seed_rel and not args.check:
            seed = build_points_seed(args.pack, seed_rel, args.regen_points)
            if seed is None:
                kept = os.path.join(args.pack, seed_rel)
                print("  %-22s %8.1f KB  %s" %
                      (seed_rel, os.path.getsize(kept) / 1024.0,
                       "the point set ALREADY IN THE PACK, kept "
                       "(--regen-points rewrites it)"))
            else:
                print("  %-22s %8.1f KB  %s" %
                      (seed_rel, os.path.getsize(seed) / 1024.0,
                       "the starter point set: %d places, %d embedded icons"
                       % (len(POINTS), len(ICONS))))
        for key in PATH_KEYS:
            if key in DEBUG_PATH_KEYS and args.release:
                continue
            rel = values.get(key)
            if not rel:
                print("  pippin.ini names no %s" % key)
                continue
            if not os.path.isfile(os.path.join(args.pack, rel)):
                print("  DANGLING    %s = %s (not in the pack)" % (key, rel))
                missing.append(rel)
        style_rel = values.get("osm.style")
        if style_rel:
            missing += check_sprite(args.pack, style_rel)
    else:
        missing.append("pippin.ini")

    print("pippin: %d file(s), %.1f MB in %s%s"
          % (staged, total_bytes / 1048576.0, args.pack,
             " (release pack)" if args.release else ""))
    if missing:
        print("pippin: INCOMPLETE — %s" % ", ".join(sorted(set(missing))))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
