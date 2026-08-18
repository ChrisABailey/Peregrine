# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

"""A route overlay: the smallest thing that EDITS a map rather than drawing on
one, and therefore the test of whether the overlay SPI is really enough.

Editing:
    click       select the waypoint under the cursor
    a           arm "add a point"; the next click on the map inserts a new
                waypoint AFTER the selected one (at the end when nothing is
                selected), and selects it
    d / Delete  delete the selected waypoint
    u           undo the last waypoint edit (ctrl-Z in the app)
    r           follow the roads: replace the straight legs with a driving
                route over the .fvroad graph (O4), if one is configured,
                priced by the overlay's profile from the JSON rule file
                (O5c) — which is reread per route, so an edited weight
                applies to this press and not the one after it
    b           the same by bicycle
    Esc        deselect, disarm add mode, or drop back to straight legs

WHY ADD IS TWO GESTURES AND NOT ONE. A click has to keep meaning "select" —
an editor where every click on open map appends a point cannot be used to pick
anything — so the key ARMS the mode and the click supplies the position. That
is FalconView's own route editor's behaviour and every drawing tool's, and it
costs the overlay one bool.

A6: THIS IS THE FIRST OVERLAY OF ANY LANGUAGE TO IMPLEMENT THE EDITOR HALF, and
it is written in Python on purpose — the app plan's acceptance test is whether
the framework is usable from a shell, not whether C++ can satisfy it. Four
things arrive with the app layer, and all four are additive: the overlay became
a FILE overlay (`file_new`/`file_open`/`file_save_as`/`revert` — defining them
is what MAKES it one, see pyfvw.overlay.Overlay), it answers picks
(`hit_test_point`, which replaced the hand-rolled loop in `on_mouse_down` so
that one hit test now serves the click, the hover and the right-click menu), it
is an EditTarget with a real undo stack, and `RouteEditor` is the per-type tool
state the mode dance switches to.

WHY NOT `.rte`. FalconView's own route files are `.rte` and the port cannot
read one yet. Claiming the extension for a JSON document of our own would make
the day that reader lands a migration instead of an addition, so these are
`.fvrte` and the name is deliberately not FalconView's.
"""

import json
import os

import pyfvw

app = pyfvw.app

#: What the on-disk document says it is. Bumped only when a reader has to
#: behave differently, never for a field a reader can ignore.
ROUTE_FORMAT = "peregrine-route"
ROUTE_VERSION = 1

#: The registered type id and the document extension.
ROUTE_TYPE_ID = "fv.route"
ROUTE_EXTENSION = "fvrte"


class RouteOverlay(pyfvw.overlay.Overlay):
    """A named route: leg lines, waypoint diamonds, labels."""

    def __init__(self, name, waypoints, color=(220, 30, 30), graph_path=None,
                 rules_path="", profile="", manager=None):
        super().__init__(name)
        self.waypoints = waypoints          # [(label, lat, lon), ...]
        self.color = color
        self.selected = None
        self.adding = False                 # armed by "a", spent by a click
        # How a leg is filled in between its two waypoints (G1). GREAT_CIRCLE
        # is the default because it is what the route actually IS — a leg is
        # flown, not drawn, and the straight line between two projected pixels
        # is an artifact of the projection rather than a path. It is visibly
        # the same line at harbour scale and visibly a different one across an
        # ocean, which is the honest way round for a default.
        self.leg_kind = pyfvw.geo.LineKind.GREAT_CIRCLE
        self._drawn = []                    # where the last on_draw put them
        # Road following (O4). The graph is a build artifact — minutes and
        # gigabytes to make from a raw extract — so it is loaded from a
        # .fvroad file, once, and only when someone actually asks for it.
        self.graph_path = graph_path
        # The O5c cost rules: a path to a JSON rule file (empty = the weights
        # compiled into the router) and the profile in it that "r" follows.
        # Both are held as plain strings and read at ROUTE time, never cached
        # into a parsed object here — the core polls the file's mtime per
        # route() call, so an edit to the file lands on the next route with
        # nothing reopened and nothing restarted.
        self.rules_path = rules_path or ""
        self.profile = profile or ""
        self._graph = None
        self._router = None
        self.road_legs = None               # [[GeoPoint, ...], ...] or None
        self.road_status = ""               # what to tell the user about it
        # Which MODE the drawn road line was priced as, because the line SAYS
        # so (dashed for a bicycle route, solid for the default car one) and
        # not only the status text. Set by follow_roads, cleared with the road.
        self.road_is_bicycle = False
        # G2/G3. The waypoint markers come from a symbol library instead of
        # being hand-rolled polygons. Colour is a LIBRARY setting rather than a
        # per-draw one — a display list carries its own colours. Since G4 it is
        # baked only when the route's colour actually changes: selection no
        # longer needs a second colour, so the per-marker re-bake is gone.
        self._symbols = pyfvw.symbol.BuiltinSymbolLibrary()
        self._symbol_color = None
        # A COPY of the projection the last frame was drawn with. The SPI
        # hands a projection to on_draw and none to on_mouse_down, so an
        # overlay that turns a click into a position has to keep one — and
        # keeping a copy beats keeping the borrowed reference, whose lifetime
        # is the caller's business (PythonView replaces its whole engine, and
        # its projection with it, on any catalog change).
        self._snap = pyfvw.engine.MapProjection()
        self._snap_ok = False
        # A6. Edit focus is the app layer's, not ours: the EditorManager
        # brackets it (release on the overlay losing edit, enter on the one
        # gaining it), and until this overlay holds it the editing keys and
        # the add-point click are not ours to take. An overlay driven WITHOUT
        # an EditorManager never hears either call, so it starts editable —
        # which is what keeps the pre-A6 behaviour for a plain pyfvw script.
        self._has_edit_focus = True
        # Undo is a stack of whole waypoint lists. They are a handful of
        # tuples; a command pattern over them would be more code than the
        # thing it is undoing.
        self._undo, self._redo = [], []
        # HitItem.feature is a number and a waypoint is identified by its
        # label, so the two are mapped here. Assigned on demand and never
        # reused, so a feature id stays valid for the life of the overlay.
        self._feature_ids, self._next_feature = {}, 1
        # The stack, for mouse CAPTURE alone. An overlay does not otherwise
        # know its manager and should not want to -- but a drag is precisely
        # the case the capture mechanism exists for: once the press is taken,
        # every move belongs to this overlay whatever is drawn on top of it
        # and wherever the cursor wanders. None is legal (a plain pyfvw script,
        # or a test): the gesture still works, it is just interruptible.
        self._manager = manager
        # The drag in flight, or None. See on_mouse_down.
        self._drag = None

    # --- drawing -----------------------------------------------------------
    #
    # G3. Every line, marker and name below goes through GeoDraw, which is what
    # the draw plan calls this overlay's acceptance test. Three things came
    # with it and none of them could be said before:
    #
    #   * a CALCULATED route is blue with a WHITE CASING, so it reads over a
    #     chart, and DASHED when it was priced as a bicycle route — the mode is
    #     visible in the line rather than only in the status text;
    #   * an UNCALCULATED route stays red straight legs, which is the honest
    #     picture: those are the waypoints joined, not a route anybody can ride;
    #   * the waypoint markers are builtin symbols and the labels are haloed,
    #     so a name over dense linework is still readable.

    #: Blue for a route that came out of the graph, red for straight legs.
    ROAD_COLOR = (40, 90, 210)
    CASING_COLOR = (255, 255, 255)

    def _road_style(self):
        """The line a CALCULATED route is drawn with.

        Dashed for a bicycle route and solid otherwise (the default profile is
        the car), both blue over a white casing. The dash is not decoration:
        the same waypoints route differently by mode, and the mode is otherwise
        only stated in one line of small text at the top of the map."""
        preset = "dash" if self.road_is_bicycle else "solid"
        style = pyfvw.draw.preset_line(preset, self.ROAD_COLOR, 3)
        style.add_casing(self.CASING_COLOR, 2)
        return style

    def _straight_style(self):
        """The line an UNCALCULATED route is drawn with: the overlay's own
        colour (red by default) and no casing, so the two are not confusable
        at a glance."""
        return pyfvw.draw.solid_line(self.color, 2)

    def on_draw(self, proj, canvas):
        # set_resolution reproduces the transform whatever mode the original
        # was in: below this layer a projection IS a centre plus a
        # degrees-per-pixel pair, and scale/physical-scale are two ways of
        # arriving at that pair.
        try:
            w, h = proj.surface_size
            self._snap.set_surface_size(w, h)
            self._snap.set_resolution(proj.deg_per_pixel_lat,
                                      proj.deg_per_pixel_lon)
            self._snap.set_center(proj.center)
            self._snap_ok = True
        except pyfvw.FvError:
            self._snap_ok = False

        self._drawn = []
        for label, lat, lon in self.waypoints:
            try:
                x, y = proj.geo_to_surface(pyfvw.geo.GeoPoint(lat, lon))
            except pyfvw.FvError:
                continue                    # not on this projection; skip it
            self._drawn.append((label, int(round(x)), int(round(y))))

        d = pyfvw.draw.GeoDraw(proj, canvas, self._symbols)

        if self.road_legs is not None:
            # The road line is drawn INSTEAD of the direct legs. Road geometry
            # is already dense — it is the road — so its legs are SIMPLE:
            # there is nothing to interpolate between two points a few metres
            # apart, and asking for a great circle between them would cost the
            # geodesy and return the same line.
            style = self._road_style()
            for leg in self.road_legs:
                d.polyline(leg, style, kind=pyfvw.geo.LineKind.SIMPLE)
        elif len(self.waypoints) >= 2:
            # G1/G3. The legs are GEOGRAPHIC lines, not lines between projected
            # pixels: a great circle is densified to ~20-pixel chords, clipped
            # in geographic space first, and broken rather than joined across
            # the antimeridian OR across a leg that clipped away. Drawn from
            # `waypoints` and not from `_drawn` — _drawn has dropped any
            # waypoint that would not project, and joining across the hole
            # would invent a leg.
            d.polyline([pyfvw.geo.GeoPoint(lat, lon)
                        for _l, lat, lon in self.waypoints],
                       self._straight_style(), kind=self.leg_kind)

        # G4. The marker library is baked ONCE now, in the route's own colour,
        # and selection is a RenderState rather than a re-bake: the selected
        # waypoint keeps the route's colour and gains a halo of its own
        # silhouette in the selection colour. Before G4 a selected waypoint
        # turned yellow, which said "selected" by throwing away the one thing
        # that said which route it belongs to.
        if self._symbol_color != tuple(self.color):
            self._symbols.set_color(self.color)
            self._symbol_color = tuple(self.color)
        for label, x, y in self._drawn:
            d.state = (pyfvw.draw.RenderState.HIGHLIGHTED
                       if label == self.selected
                       else pyfvw.draw.RenderState.NORMAL)
            d.symbol_at_pixel(x, y, pyfvw.symbol.builtin.DIAMOND, scale=1.6)
            # The name is not highlighted; the marker is (the same rule
            # PointOverlay follows).
            d.state = pyfvw.draw.RenderState.NORMAL
            d.label_at_pixel(x + 10, y - 10, label, (0, 0, 0), size=12.0,
                             halo_color=(255, 255, 255), halo_width=1.0)

        # Armed mode is a MODE, so it has to be visible: a click that is about
        # to mean something different than usual should say so before it is
        # clicked, not after.
        if self.adding:
            where = f"after {self.selected}" if self.selected else "at the end"
            canvas.draw_text(f"add point {where} - click the map (Esc cancels)",
                             10, 20, color=self.color, size=13.0)
        elif self.road_status:
            canvas.draw_text(self.road_status, 10, 20, color=(40, 90, 210),
                             size=13.0)

    # --- the document (A6: defining these MAKES this a file overlay) --------

    def file_new(self):
        """A fresh, empty route. Not dirty: there is nothing in it to lose."""
        self.waypoints = []
        self.selected = None
        self.clear_roads()
        self._undo, self._redo = [], []
        self.dirty = False

    def file_open(self, spec):
        """Read a .fvrte. Raising IS the failure path — the flow layer turns
        the exception into a failed Status and the shell reports it, so there
        is no error code to invent here."""
        with open(spec, "r", encoding="utf-8") as f:
            doc = json.load(f)
        if doc.get("format") != ROUTE_FORMAT:
            raise ValueError(f"{os.path.basename(spec)} is not a route document")
        if int(doc.get("version", 0)) > ROUTE_VERSION:
            raise ValueError(f"route version {doc['version']} is newer than "
                             f"this build understands ({ROUTE_VERSION})")
        self.waypoints = [(w["label"], float(w["lat"]), float(w["lon"]))
                          for w in doc.get("waypoints", [])]
        if doc.get("color"):
            self.color = tuple(int(c) for c in doc["color"][:3])
        if doc.get("name"):
            # The document's own name, not the path: an overlay list should
            # say "Ruddy Turnstone to the beach", not "/tmp/x.fvrte".
            self.name = doc["name"]
        if doc.get("profile") is not None:
            self.profile = doc["profile"]
        self.selected = None
        self.clear_roads()
        self._undo, self._redo = [], []

    def file_save_as(self, spec, format_index=0):
        if format_index != 0:
            raise ValueError(f"routes have one format (0), asked for {format_index}")
        doc = {
            "format": ROUTE_FORMAT,
            "version": ROUTE_VERSION,
            "name": self.name,
            "color": list(self.color),
            "profile": self.profile,
            # The ROAD geometry is not saved: it is derived from the graph and
            # the rule file, both of which can change, and a saved copy would
            # be a stale answer that looks like a document.
            "waypoints": [{"label": l, "lat": lat, "lon": lon}
                          for l, lat, lon in self.waypoints],
        }
        with open(spec, "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2)
            f.write("\n")

    def revert(self, spec):
        """Defining this is what makes the session OFFER to discard edits when
        the user re-opens a file that is already open and dirty."""
        self.file_open(spec)
        self.dirty = False

    # --- picking (A6) ------------------------------------------------------

    def _feature_id(self, label):
        fid = self._feature_ids.get(label)
        if fid is None:
            fid = self._next_feature
            self._feature_ids[label] = fid
            self._next_feature += 1
        return fid

    def _label_for_feature(self, feature):
        for label, fid in self._feature_ids.items():
            if fid == feature:
                return label
        return None

    def hit_test_point(self, proj, x, y, tolerance_px):
        """What is under the cursor. Hit-tests what was DRAWN, so a pick agrees
        with the screen — and it is now the ONE hit test: the click, the hover
        hint and the right-click menu all come through here, where before
        on_mouse_down had a loop of its own."""
        hits = []
        for label, wx, wy in self._drawn:
            d = ((wx - x) ** 2 + (wy - y) ** 2) ** 0.5
            if d > tolerance_px + 7:        # 7 = the diamond's half-width
                continue
            hits.append(app.HitItem(
                feature=self._feature_id(label),
                distance_px=d,
                hint=app.HintText(label, f"{self.name}: waypoint {label}"),
                cursor=app.CursorId.MOVE))
        return hits

    def menu_items(self, proj, x, y):
        """This overlay's section of a right-click menu."""
        items = []
        for hit in self.hit_test_point(proj, x, y, 8.0):
            label = self._label_for_feature(hit.feature)
            items.append(app.MenuNode(
                label=f"Select waypoint {label}",
                checked=(label == self.selected),
                action=lambda l=label: self.select(l)))
            items.append(app.MenuNode(
                label=f"Delete waypoint {label}",
                action=lambda l=label: self.delete(l)))
        if len(self.waypoints) >= 2:
            items.append(app.MenuNode(label="Follow roads",
                                      action=self.follow_roads))
        return items

    # --- the editor's per-instance half (EditTarget) ------------------------

    def enter_edit_focus(self):
        self._has_edit_focus = True

    def release_edit_focus(self):
        # Leaving edit must also leave any half-finished gesture, or the next
        # entry starts armed for a click the user made a minute ago. A drag in
        # flight is the same thing one step further along, so it is cancelled
        # rather than abandoned mid-move.
        self.cancel_drag()
        self._has_edit_focus = False
        self.adding = False

    def can_undo(self):
        return bool(self._undo)

    def undo(self):
        if not self._undo:
            return
        self._redo.append(list(self.waypoints))
        self.waypoints = self._undo.pop()
        if self.selected not in {w[0] for w in self.waypoints}:
            self.selected = None
        self.clear_roads()
        self.dirty = True

    def can_redo(self):
        return bool(self._redo)

    def redo(self):
        if not self._redo:
            return
        self._undo.append(list(self.waypoints))
        self.waypoints = self._redo.pop()
        self.clear_roads()
        self.dirty = True

    # --- editing -----------------------------------------------------------

    def _record(self):
        """Snapshot for undo, and mark the document dirty. Every mutation goes
        through here, which is also the only place `dirty` is set — an overlay
        that dirties itself in six places forgets in the seventh."""
        self._undo.append(list(self.waypoints))
        del self._redo[:]
        if self.is_file_overlay:
            self.dirty = True

    def select(self, label):
        self.selected = label

    def delete(self, label):
        if label not in {w[0] for w in self.waypoints}:
            return False
        self._record()
        self.waypoints = [w for w in self.waypoints if w[0] != label]
        if self.selected == label:
            self.selected = None
        return True

    def _insert_index(self):
        """Where a new point goes: after the selected one, else at the end."""
        for i, (label, _lat, _lon) in enumerate(self.waypoints):
            if label == self.selected:
                return i + 1
        return len(self.waypoints)

    def _new_label(self):
        """A short unique label. Names have to stay unique because selection
        and deletion are BY LABEL — two waypoints called WP3 would delete as
        one."""
        used = {w[0] for w in self.waypoints}
        n = len(self.waypoints) + 1
        while f"WP{n}" in used:
            n += 1
        return f"WP{n}"

    # --- road following ----------------------------------------------------

    def _ensure_router(self):
        """Opens the graph on first use. Returns an error string, or ""."""
        if self._router is not None:
            return ""
        if not self.graph_path:
            return "no road graph configured ([routing] graph in the ini)"
        try:
            self._graph = pyfvw.routing.RoadGraph.load(self.graph_path)
        except pyfvw.FvError as err:
            return f"road graph: {err}"
        self._router = pyfvw.routing.Router(self._graph)
        return ""

    def graph(self):
        """The opened road graph, or None until the user first follows a road.

        Public because the moving map's snapper (MM5) wants THE SAME graph
        rather than a second copy: a `.fvroad` is a build artifact that runs to
        hundreds of megabytes on anything larger than an island, and two of
        them in memory is a cost with nothing to show for it."""
        return self._graph

    def profiles(self):
        """The profile names the current rule file defines, for a menu to be
        built from. Rereads the file if it changed, so a profile added while
        the app is running appears here without a restart. Never raises: a
        broken file leaves the last good (or built-in) names."""
        try:
            return list(pyfvw.routing.rule_profiles(self.rules_path))
        except pyfvw.FvError:
            return []

    def rules_error(self):
        """Why the rule file is not in force, or "" when it is."""
        try:
            return pyfvw.routing.rules_error(self.rules_path)
        except pyfvw.FvError as err:
            return str(err)

    @staticmethod
    def _is_bicycle_request(profile, cycle_only):
        name = (profile or "").lower()
        if name:
            return "bike" in name or "cycle" in name
        return bool(cycle_only)

    def _bike_profile(self):
        """The rule file's bicycle profile if it defines one. Lets "b" honour
        a tuned file while still working against a file that has no such
        profile, where the boolean argument does the same job."""
        return "bicycle" if "bicycle" in self.profiles() else ""

    def follow_roads(self, snap_meters=2000.0, cycle_only=False, profile=None):
        """Replace the straight legs with a driving route through the
        waypoints, in order (O5d): ONE route that passes through each of them
        rather than a string of independent pairs, so a turn restriction at a
        waypoint binds and the route does not drive out to a waypoint, turn
        round, and come back unless there is no other way out.

        Falls back to routing the pairs separately when the through route
        cannot be had at all — a waypoint dropped in the sea, or two that are
        not connected. That is the honest thing to draw and it is what this
        did before O5d, but it is a lesser answer, so the status line says so.

        cycle_only switches to the bicycle profile: only classes a bike may
        ride, at a flat speed, with cycleways and quiet streets preferred.

        profile names a profile from the rule file (O5c) and OVERRIDES
        cycle_only and every other cost argument. None means "whatever the
        overlay is configured with"; "" means none, i.e. the booleans decide."""
        self.road_legs = None
        # What the LINE will say. A profile named on the call wins, because it
        # overrides cycle_only in the router too; otherwise the boolean decides.
        # Matched on the name rather than on a flag out of the router because
        # the rule file owns the profiles and a user may well call theirs
        # "bicycle-winter" — a profile with "bike"/"cycle" in its name is one.
        self.road_is_bicycle = self._is_bicycle_request(
            self.profile if profile is None else profile, cycle_only)
        if len(self.waypoints) < 2:
            self.road_status = "a route needs two waypoints"
            return False
        problem = self._ensure_router()
        if problem:
            self.road_status = problem
            return False
        if profile is None:
            profile = self.profile

        stops = [pyfvw.geo.GeoPoint(lat, lon) for _, lat, lon in self.waypoints]
        try:
            # The rule file is polled by THIS call, so each press of "r" routes
            # on the weights currently on disk.
            through = self._router.route_via(stops, snap_meters=snap_meters,
                                             cycle_only=cycle_only,
                                             profile=profile,
                                             rules=self.rules_path)
        except pyfvw.FvError as err:
            # A stop nowhere near a road is the one failure the per-pair
            # fallback can still say something useful about. Anything else —
            # above all a profile the rule file does not define — is about the
            # REQUEST, would fail identically per pair, and is reported once.
            if err.code != pyfvw.OUT_OF_COVERAGE:
                self.road_status = str(err.message)
                return False
            through = None

        if through is not None and through.found:
            # Cut the one line back into per-leg pieces at the stops: the
            # drawing code wants polylines, and a leg is still the unit a user
            # thinks in even when the route through them is single.
            cuts = list(through.stop_geometry_index)
            geom = list(through.geometry)
            self.road_legs = [geom[a:b + 1] for a, b in zip(cuts, cuts[1:])
                              if b > a]
            turned = (f" (turned round at {len(through.u_turn_stops)} stop(s)"
                      " with no other way out)"
                      if through.u_turn_stops else "")
            self.road_status = self._road_status_line(
                profile, cycle_only, through.length_m, through.seconds, turned)
            return True

        # No through route. Say why, then draw what can be drawn.
        if through is not None and through.unreachable_leg != pyfvw.routing.Route.NO_LEG:
            note = (f" (stops {through.unreachable_leg + 1} and "
                    f"{through.unreachable_leg + 2} are not connected)")
        else:
            note = " (a waypoint is not near a road)"
        self._follow_roads_per_pair(snap_meters, cycle_only, profile)
        if self.road_legs is not None:
            self.road_status = "pairs" + note + " - " + self.road_status
        # False whatever the pairs managed: the route asked for was a route
        # through the waypoints, and this is not one.
        return False

    def _follow_roads_per_pair(self, snap_meters, cycle_only, profile):
        """The pre-O5d behaviour: each consecutive pair routed on its own, so
        one unreachable pair does not throw the whole route away — it just
        stays straight. Kept as the fallback, because half a drawn route is
        more use than none."""
        legs, meters, seconds, straight = [], 0.0, 0.0, 0
        for (_, lat1, lon1), (_, lat2, lon2) in zip(self.waypoints,
                                                    self.waypoints[1:]):
            a = pyfvw.geo.GeoPoint(lat1, lon1)
            b = pyfvw.geo.GeoPoint(lat2, lon2)
            try:
                # The rule file is polled by THIS call, so each press of "r"
                # routes on the weights currently on disk.
                leg = self._router.route(a, b, snap_meters=snap_meters,
                                         cycle_only=cycle_only,
                                         profile=profile,
                                         rules=self.rules_path)
            except pyfvw.FvError as err:
                # Only "this end is nowhere near a road" is a per-leg problem
                # that leaves the other legs worth routing. Anything else —
                # above all a profile the rule file does not define — is about
                # the REQUEST, would fail identically on every leg, and is
                # reported once instead of being hidden as straight lines.
                if err.code != pyfvw.OUT_OF_COVERAGE:
                    self.road_legs = None
                    self.road_status = str(err.message)
                    return False
                leg = None
            if leg is None or not leg.found:
                legs.append([a, b])         # honest about what it could not do
                straight += 1
                continue
            legs.append(list(leg.geometry))
            meters += leg.length_m
            seconds += leg.seconds

        self.road_legs = legs
        note = f" ({straight} leg(s) not on the network)" if straight else ""
        self.road_status = self._road_status_line(profile, cycle_only, meters,
                                                  seconds, note)
        return straight == 0

    def _road_status_line(self, profile, cycle_only, meters, seconds, note):
        """The one line of map the route gets to explain itself on."""
        # A rule file that failed to load still routes — on the last good or
        # built-in weights — so this is a warning on an answer, not an error
        # instead of one. Saying nothing would be the bug: the user edited a
        # file and would otherwise see a route that ignored the edit.
        # Clamped: a JSON parse error names the file, the line and the token,
        # which is what the Options dialog is for. Here it has to fit on one
        # line of map without pushing the distance off the left of it.
        bad = self.rules_error()
        if len(bad) > 64:
            bad = bad[:61] + "..."
        warn = f" [rules: {bad}]" if bad else ""
        what = profile if profile else ("bike" if cycle_only else "roads")
        return (f"{what}: {meters / 1000.0:.1f} km, "
                f"{seconds / 60.0:.0f} min{note}{warn}"
                " - Esc for straight legs")

    def clear_roads(self):
        self.road_legs = None
        self.road_status = ""
        self.road_is_bicycle = False

    # --- leg geometry ------------------------------------------------------

    LEG_KINDS = ("GREAT_CIRCLE", "RHUMB", "SIMPLE")

    def leg_kind_name(self):
        for n in self.LEG_KINDS:
            if self.leg_kind == getattr(pyfvw.geo.LineKind, n):
                return n
        return "GREAT_CIRCLE"

    def cycle_leg_kind(self):
        """Great circle -> rhumb -> straight-in-projection -> round again.
        Three ways to answer "what is between these two points", and seeing
        them switch over one route is the quickest way to know the geodesy is
        actually running."""
        i = self.LEG_KINDS.index(self.leg_kind_name())
        name = self.LEG_KINDS[(i + 1) % len(self.LEG_KINDS)]
        self.leg_kind = getattr(pyfvw.geo.LineKind, name)
        return name

    # --- editing -----------------------------------------------------------

    def on_mouse_down(self, e):
        if not self._has_edit_focus:
            # Not the overlay being edited: a click on it is somebody else's
            # business (the pick session's, usually). Declining is what lets
            # two routes be open at once without both eating the same click.
            return False
        if self.adding:
            # The overlay is handed surface pixels and has to turn them into a
            # position itself, through the projection that drew the frame the
            # user is looking at.
            if not self._snap_ok:
                self.adding = False
                return True
            try:
                p = self._snap.surface_to_geo(e.x, e.y)
            except pyfvw.FvError:
                self.adding = False
                return True
            label = self._new_label()
            self._record()
            self.waypoints.insert(self._insert_index(), (label, p.lat, p.lon))
            self.selected = label           # ready to add the next one after it
            self.adding = False
            return True                     # handled -> stops routing

        # One hit test, shared with the hover and the context menu.
        hits = self.hit_test_point(None, e.x, e.y, 8.0)
        if hits:
            nearest = min(hits, key=lambda h: h.distance_px)
            label = self._label_for_feature(nearest.feature)
            self.selected = label
            # A press on a waypoint is the START of a drag, not yet a move.
            # It stays a plain selection until the cursor actually travels, so
            # clicking to select costs no undo entry and does not dirty the
            # document -- the user has to mean it.
            self._begin_drag(label, e.x, e.y)
            return True                     # handled -> stops routing
        return False

    # --- dragging a waypoint -----------------------------------------------
    #
    # The gesture is press-move-release, and all three halves are the
    # overlay's: the shell hands over raw surface pixels and the overlay turns
    # them into a position through the projection that drew the frame the user
    # is looking at (the same `_snap` copy the add-point click uses). What the
    # STACK contributes is capture -- phase 0 of OverlayManager's routing --
    # so a cursor dragged over another overlay, or off the top waypoint
    # entirely, keeps feeding this one until the button comes up.

    def _begin_drag(self, label, x, y):
        if label is None:
            return
        for _l, lat, lon in self.waypoints:
            if _l == label:
                break
        else:
            return
        # (label, origin lat/lon for a cancel, press point, moved yet?, the
        # dirty flag as it was — `dirty` only exists once the overlay has been
        # through file_new/file_open, hence the getattr.)
        self._drag = [label, lat, lon, x, y, False,
                      getattr(self, "dirty", False)]
        if self._manager is not None:
            try:
                self._manager.capture_mouse(self)
            except pyfvw.FvError:
                pass                        # not in the stack: drag uncaptured

    def _end_drag(self):
        self._drag = None
        if self._manager is not None:
            self._manager.release_mouse()

    def cancel_drag(self):
        """Escape mid-drag: the waypoint goes back where it was and the undo
        entry the first move pushed is spent putting it there, so a cancelled
        drag leaves no trace in the history at all."""
        if self._drag is None:
            return False
        label, lat, lon, _x, _y, moved, was_dirty = self._drag
        self._end_drag()
        if not moved:
            return True                     # nothing happened; nothing to undo
        if self._undo:
            # The snapshot the first move pushed IS the pre-drag route, so
            # spending it restores the position and the history in one step.
            self.waypoints = self._undo.pop()
        else:
            self._move_waypoint(label, lat, lon)
        if getattr(self, "dirty", False) and not was_dirty:
            self.dirty = False
        return True

    def _move_waypoint(self, label, lat, lon):
        self.waypoints = [(l, lat, lon) if l == label else (l, wlat, wlon)
                          for l, wlat, wlon in self.waypoints]

    def on_mouse_move(self, e):
        if self._drag is None:
            return False
        label, _lat0, _lon0, x0, y0, moved, _was_dirty = self._drag
        if not moved:
            if abs(e.x - x0) + abs(e.y - y0) < 3:
                return True                 # a still hand: still just a click
            # The first real movement is what makes this an EDIT. One undo
            # snapshot per drag, taken here rather than at the press, so the
            # whole drag undoes as the single thing the user did.
            self._record()
            self._drag[5] = moved = True
        if not self._snap_ok:
            return True
        try:
            p = self._snap.surface_to_geo(e.x, e.y)
        except pyfvw.FvError:
            return True                     # off the projection; hold position
        self._move_waypoint(label, p.lat, p.lon)
        # A followed road was computed for waypoints that have now moved, so
        # it no longer describes this route. Dropping it is honest; leaving it
        # on screen would show a road line joining somewhere the route is not.
        if self.road_legs is not None:
            self.clear_roads()
        return True

    def on_mouse_up(self, e):
        if self._drag is None:
            return False
        moved = self._drag[5]
        if moved:
            self.on_mouse_move(e)           # commit the release position
        self._end_drag()
        return True

    def on_key_down(self, e):
        # e is a pyfvw.overlay.KeyEvent: e.key is a virtual-key code
        # (pyfvw.overlay.key.*), e.text the character the layout produced,
        # plus e.shift / e.ctrl / e.alt / e.meta.
        k = pyfvw.overlay.key
        if not self._has_edit_focus:
            return False                    # not the overlay being edited
        if e.key == k.ESCAPE:
            # Escape backs out of one thing at a time — the mode first, since
            # that is the one that changes what a click will do. A drag in
            # flight outranks even that: the manager gives the CAPTURING
            # overlay the key first precisely so a gesture can be cancelled.
            if self.cancel_drag():
                return True
            if self.adding:
                self.adding = False
                return True
            if self.road_legs is not None or self.road_status:
                self.clear_roads()
                return True
            if self.selected is not None:
                self.selected = None
                return True
            return False                    # nothing to cancel: let the app quit
        if e.key == ord("A"):
            self.adding = not self.adding
            return True
        if e.key == ord("R"):
            self.follow_roads()
            return True
        if e.key == ord("G"):
            self.cycle_leg_kind()
            return True
        if e.key == ord("B"):
            # The rule file's bicycle profile when it has one, so "b" follows
            # tuned weights; the boolean otherwise, which is the same ride.
            self.follow_roads(cycle_only=True, profile=self._bike_profile())
            return True
        if e.key == ord("U") or (e.ctrl and e.key == ord("Z")):
            if e.shift:
                self.redo()
            else:
                self.undo()
            return True
        if self.selected is None:
            return False
        if e.key == k.DELETE or e.key == k.BACKSPACE or e.key == ord("D"):
            self.delete(self.selected)
            return True                     # handled -> stops routing
        return False


# ---------------------------------------------------------------------------
# The editor (A6) — one instance per TYPE, not per route
# ---------------------------------------------------------------------------

class RouteEditor:
    """The route TOOL. There is one of these however many routes are open,
    which is the whole reason the app layer keeps editors per type: "I am
    drawing routes" is a statement about the user, not about a document.

    Duck-typed on purpose (see pyfvw.app.OverlayTypeDesc): an editor is any
    object with activate() and deactivate(), plus whichever of tools(),
    default_cursor(), ui_constraints() and auto_enter_on_create() it cares to
    define. There is no base class to inherit and nothing to call up into.
    """

    def __init__(self, app_window=None):
        # The shell, so a palette button can do what a key press does. None in
        # a test, which is why every use of it is guarded.
        self.app = app_window
        self.active = False

    # --- the bracket the mode dance calls ---------------------------------

    def activate(self):
        self.active = True

    def deactivate(self):
        self.active = False

    def default_cursor(self):
        return app.CursorId.CROSSHAIR

    def ui_constraints(self):
        # A route is edited in map coordinates and the port has no rotation to
        # disable, so this is honest about constraining nothing. It is here to
        # show where a real drawing editor would say "north up only".
        return app.EditorUiConstraints()

    def auto_enter_on_create(self):
        # Creating a route means you are about to draw one.
        return True

    def tools(self):
        """The palette, as DATA — the shell renders a toolbar from it. The
        plan's §6 risk note applies: MenuNode has no colour well and no live
        readout, and a real drawing editor will want both."""
        route = self._route()
        return [
            app.MenuNode(label="Add point (a)", checked=bool(route and route.adding),
                         action=self._toggle_add),
            app.MenuNode(label="Delete point (d)",
                         enabled=bool(route and route.selected),
                         action=self._delete),
            app.MenuNode(label=""),                       # separator
            app.MenuNode(
                label=("Legs: %s (g)" %
                       (route.leg_kind_name().lower().replace("_", " ")
                        if route else "great circle")),
                enabled=bool(route and len(route.waypoints) >= 2),
                action=lambda: route and route.cycle_leg_kind()),
            app.MenuNode(label="Follow roads (r)",
                         enabled=bool(route and len(route.waypoints) >= 2),
                         action=lambda: route and route.follow_roads()),
            app.MenuNode(label="By bicycle (b)",
                         enabled=bool(route and len(route.waypoints) >= 2),
                         action=lambda: route and route.follow_roads(
                             cycle_only=True, profile=route._bike_profile())),
            app.MenuNode(label="Straight legs (Esc)",
                         enabled=bool(route and route.road_legs is not None),
                         action=lambda: route and route.clear_roads()),
            app.MenuNode(label=""),
            app.MenuNode(label="Undo (u)", enabled=bool(route and route.can_undo()),
                         action=lambda: route and route.undo()),
        ]

    # --- what the tools act on --------------------------------------------

    def _route(self):
        """The route being edited. The EditorManager knows it — asking IT
        rather than remembering is what keeps the tools correct when the user
        makes a different route current."""
        if self.app is None or self.app.editors is None:
            return None
        edited = self.app.editors.edited
        return edited if isinstance(edited, RouteOverlay) else None

    def _toggle_add(self):
        route = self._route()
        if route is not None:
            route.adding = not route.adding

    def _delete(self):
        route = self._route()
        if route is not None and route.selected:
            route.delete(route.selected)


def route_type_desc(factory, editor_factory):
    """The registered type. A shell calls this with closures of its own — the
    factory has to bind the graph and rule-file paths from the settings, and
    the editor wants a handle on the shell — which is exactly why
    OverlayTypeDesc takes callables rather than classes."""
    return app.OverlayTypeDesc(
        id=ROUTE_TYPE_ID,
        display_name="Route",
        icon="route",
        factory=factory,
        editor_factory=editor_factory,
        file=app.FileTypeDesc(
            default_extension=ROUTE_EXTENSION,
            open_filters=[("Peregrine Routes (*.fvrte)", "*.fvrte")]),
        # Above the points overlay (950) and the graticule (900): a route is
        # what the user is working on.
        default_display_order=1000)
