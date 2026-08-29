# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""The route type, as PythonView registers it: a tool palette and a descriptor.

WHAT USED TO BE HERE, and why it left. This file was a whole route overlay
written in Python — a `.fvrte` reader and writer, the drawing, the pick, road
following, and an editor with a drag, an armed add-mode and an undo stack. All
of it now lives in C++ under `port/RouteKit/` and is reached through
`pyfvw.route`:

    RouteDoc            the document, byte-compatible with what this file wrote
    RoutePlanner        follow_roads, over the O4 graph and the O5c rule file
    RouteOverlay        the picture, the pick and the snap
    RouteEditSession    select, drag, add, delete, undo -- `overlay.edit`

The reason for the move was not that C++ is better at any of it. It is that
Pippin drew the same route from the same document and could not EDIT it: the
overlay had been ported in P5 and the editor had not, so the desktop had
gestures and the phone had `SetWaypoints`. Two shells were on their way to two
answers for "what does dragging a waypoint mean", which is the one thing a
port cannot afford. The editor went where both could reach it.

WHAT IS LEFT IS SHELL FURNITURE, and it is left on purpose. A palette is a
statement about a toolbar, `MenuNode`s and keyboard hints included; a phone has
no toolbar and would render none of it. The type DESCRIPTOR stays here for the
same reason it always took callables: the factory has to bind this
application's graph and rule-file settings, and the editor wants a handle on
this application's window.

A NOTE ON THE ACCEPTANCE TEST. The app plan called this overlay its proof that
the framework is usable from a shell rather than only from C++. The Overlay SPI
is still exercised from Python — PythonView's own `Crosshair`,
`CoverageOverlay` and `TrackPointsOverlay`, and the pytest suite's subclasses —
but `EditTarget` plus the mouse SPI no longer are. That is a real thing given
up in exchange for one editor instead of two.

Editing, unchanged from the user's side (the keys are `RouteEditSession`'s now):
    click       select the waypoint under the cursor
    drag        move it -- and it SNAPS to any exact position under the
                cursor: a point in a `.fvpoints` set, or another route's
                waypoint (P19's `app.snap_candidates`, which nothing in this
                application called until the editor moved)
    a           arm "add a point"; the next click inserts after the selected
                one (at the end when nothing is selected), and selects it
    d / Delete  delete the selected waypoint
    u           undo (ctrl-Z in the app; shift for redo)
    r           follow the roads
    b           the same by bicycle
    g           cycle the leg geometry: great circle -> rhumb -> straight
    Esc         cancel a drag, disarm add, drop back to straight legs, deselect
"""

import pyfvw

app = pyfvw.app
route = pyfvw.route

#: The registered type id and the document extension. Taken from the C++
#: constants rather than restated, so there is exactly one spelling of each.
ROUTE_TYPE_ID = route.ROUTE_TYPE_ID
ROUTE_EXTENSION = route.ROUTE_EXTENSION

#: What the on-disk document says it is. Kept as module attributes because
#: PythonView's help text and the pytest suite both name them.
ROUTE_FORMAT = route.ROUTE_FORMAT
ROUTE_VERSION = route.ROUTE_VERSION

#: The overlay class itself, re-exported so `from route import RouteOverlay`
#: still means what it always meant.
RouteOverlay = route.RouteOverlay


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

    It holds NO editing state. The gestures, the armed mode and the undo stack
    are the overlay's `edit` session, which is what makes this object a
    palette rather than half an editor: everything below reads the session and
    calls into it, and a phone doing the same edits shares that session while
    sharing none of this.
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

    #: Great circle -> rhumb -> straight-in-projection, spelled for a menu.
    LEG_KIND_NAMES = {
        pyfvw.geo.LineKind.GREAT_CIRCLE: "great circle",
        pyfvw.geo.LineKind.RHUMB: "rhumb",
        pyfvw.geo.LineKind.SIMPLE: "straight",
    }

    def tools(self):
        """The palette, as DATA — the shell renders a toolbar from it. The
        plan's §6 risk note applies: MenuNode has no colour well and no live
        readout, and a real drawing editor will want both."""
        r = self._route()
        edit = r.edit if r is not None else None
        routable = bool(r is not None and len(r.waypoints) >= 2)
        return [
            app.MenuNode(label="Add point (a)",
                         checked=bool(edit is not None and edit.adding),
                         action=self._toggle_add),
            app.MenuNode(label="Delete point (d)",
                         enabled=bool(r is not None and r.selected),
                         action=self._delete),
            app.MenuNode(label=""),                       # separator
            app.MenuNode(
                label=("Legs: %s (g)" %
                       self.LEG_KIND_NAMES.get(
                           r.leg_kind if r is not None else
                           pyfvw.geo.LineKind.GREAT_CIRCLE, "great circle")),
                enabled=routable,
                action=lambda: edit is not None and edit.cycle_leg_kind()),
            app.MenuNode(label="Follow roads (r)", enabled=routable,
                         action=lambda: edit is not None and edit.follow_roads()),
            app.MenuNode(label="By bicycle (b)", enabled=routable,
                         action=lambda: (edit is not None and
                                         edit.follow_roads_by_bicycle())),
            app.MenuNode(label="Straight legs (Esc)",
                         enabled=bool(r is not None and r.has_plan),
                         action=lambda: r is not None and r.clear_roads()),
            app.MenuNode(label=""),
            app.MenuNode(label="Undo (u)",
                         enabled=bool(edit is not None and edit.can_undo()),
                         action=lambda: edit is not None and edit.undo()),
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
        r = self._route()
        if r is not None:
            r.edit.adding = not r.edit.adding

    def _delete(self):
        r = self._route()
        if r is not None and r.selected:
            r.edit.delete(r.selected)


def route_type_desc(factory, editor_factory):
    """The registered type. A shell calls this with closures of its own — the
    factory has to bind the planner from the settings, and the editor wants a
    handle on the shell — which is exactly why OverlayTypeDesc takes callables
    rather than classes.

    `pyfvw.route.register_route_overlay_type` registers the same type WITHOUT
    an editor, which is what a shell with no toolbar (Pippin) wants. The two
    differ by this file's palette and by nothing else."""
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
