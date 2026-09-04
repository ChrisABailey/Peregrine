# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""A tk button bar over `pyfvw.app.MenuNode` — the thing an editor palette has
described since A6 and that nothing rendered.

WHY THIS EXISTS. `OverlayEditor::Tools()` has always returned a list of
`MenuNode`, and `route.py`'s docstring calls that "a statement about a
toolbar". Until now the only renderer was the Tools MENU, so entering a drawing
mode and then picking a tool cost two menu trips — and every new editor's every
new verb had to be another line in the menu structure. FalconView did not work
that way: its overlays put up a small palette of their own, you clicked the
tool you wanted, and the menu bar stayed the size it was.

THE WHOLE INPUT IS A LIST OF MenuNode, which is the point. This module knows
nothing about analysis, routes, or any other editor — an editor that returns a
palette gets a bar for free, and the same list drives the menu, so the two can
never disagree about what is enabled.

THREE RULES, and each one is a bug that would otherwise be found by hand:

1. **Every node is rendered as a TOGGLE** (`ttk.Checkbutton` in the
   `Toolbutton` style), because a `MenuNode` says whether it is `checked` and
   does not say whether it is checkABLE. A momentary action depresses for the
   length of the click and the next `set_tools` puts it back, which is what a
   toolbar button looks like anyway; a real mode button stays down because its
   editor reports `checked=True`. No editor had to be changed for this.

2. **`set_tools` is idempotent and cheap**, so a shell may call it on every
   frame — which it must, because whether "Profile..." is enabled changes when
   the user clicks the MAP, not when the editor changes. Widgets are reused
   whenever the SHAPE of the palette (the labels, in order) is unchanged and
   only `state`/`variable` are reconfigured; a changed shape rebuilds. Without
   the reuse, a bar rebuilt mid-drag would destroy the widget under the
   pointer.

3. **Nothing here takes focus** (`takefocus=0`). The application binds `<Key>`
   on the toplevel, so a focused button would still see the key — and would
   ALSO answer space and Return itself, which is how a toolbar starts eating
   the map's keyboard.
"""


class ToolBar:
    """One horizontal strip of buttons built from MenuNode data.

    `on_invoke` is called after a button's action has run: the shell repaints
    and re-reads the palette there, because a tool that changed the world has
    almost certainly changed what the OTHER buttons should say.
    """

    def __init__(self, parent, tk, ttk, on_invoke=None, pad=2):
        self._tk = tk
        self._ttk = ttk
        self._on_invoke = on_invoke
        self._pad = pad
        self.frame = ttk.Frame(parent)
        # (label, is_separator) per node — the SHAPE. Reconfiguring is only
        # legal while this is unchanged; see rule 2.
        self._shape = None
        # (label, enabled, checked) per node — the STATE. Held so that the
        # per-frame call in rule 2 costs a list comparison and not a dozen
        # `configure`s while the user is dragging.
        self._state = None
        self._slots = []        # [(widget, tk.BooleanVar or None)]
        self._nodes = []
        # True for the length of a button's own command. See set_tools.
        self._in_invoke = False
        self._deferred = None

    # --- the shell's half ---------------------------------------------------

    def set_tools(self, nodes):
        """Render `nodes`. Safe to call every frame."""
        nodes = list(nodes)
        shape = [(n.label, n.is_separator) for n in nodes]
        if shape != self._shape:
            if self._in_invoke:
                # A REBUILD INSIDE A BUTTON'S OWN COMMAND WOULD DESTROY THAT
                # BUTTON while tk is still dispatching to it. It is not
                # hypothetical: "Great circle" becomes "Rhumb" when pressed,
                # which is a changed shape by definition. Nothing is touched
                # here — `_nodes` and `_slots` stay consistent with each other,
                # which is what `_invoke` indexes through — and the whole
                # replacement happens at idle, before the next redraw.
                self._deferred = nodes
                self.frame.after_idle(self._flush_deferred)
                return
            self._rebuild(nodes, shape)
        self._nodes = nodes
        self._apply_state()

    def _flush_deferred(self):
        nodes, self._deferred = self._deferred, None
        if nodes is None:
            return
        self._rebuild(nodes, [(n.label, n.is_separator) for n in nodes])
        self._nodes = nodes
        self._apply_state()

    @property
    def empty(self):
        return not self._nodes

    # --- rendering ----------------------------------------------------------

    def _rebuild(self, nodes, shape):
        for w, _var in self._slots:
            w.destroy()
        self._slots = []
        self._shape = shape
        self._state = None      # new widgets, so nothing is known about them
        for i, node in enumerate(nodes):
            if node.is_separator:
                sep = self._ttk.Separator(self.frame, orient="vertical")
                sep.pack(side="left", fill="y", padx=6, pady=3)
                self._slots.append((sep, None))
                continue
            var = self._tk.BooleanVar(value=node.checked)
            btn = self._ttk.Checkbutton(
                self.frame, text=node.label, variable=var, takefocus=0,
                style="Toolbutton", command=lambda i=i: self._invoke(i))
            btn.pack(side="left", padx=self._pad, pady=self._pad)
            self._slots.append((btn, var))

    def _apply_state(self, force=False):
        state = [(n.label, n.enabled, n.checked) for n in self._nodes]
        if not force and state == self._state:
            return
        self._state = state
        for node, (w, var) in zip(self._nodes, self._slots):
            if var is None:
                continue
            var.set(node.checked)
            w.configure(text=node.label,
                        state=("normal" if node.enabled else "disabled"))

    def _invoke(self, index):
        # The index is into the list rendered LAST. A palette that changed
        # shape under the click has already rebuilt its widgets, so this can
        # only be stale if a rebuild happened between the press and the
        # command, which tk does not do.
        if index >= len(self._nodes):
            return
        node = self._nodes[index]
        # THE CLICK HAS ALREADY MOVED THE VARIABLE, whatever the action does
        # about it (rule 1), so the cached state is a lie from here on and the
        # next apply must write. Without this a momentary button that changes
        # nothing about the palette stays visibly depressed.
        self._state = None
        if not node.enabled:
            # A disabled ttk button does not fire, but a shell that pushed a
            # stale palette could still get here; refusing is cheaper than
            # explaining the action that ran.
            self._apply_state()
            return
        self._in_invoke = True
        try:
            node.invoke()
            if self._on_invoke is not None:
                self._on_invoke()
        finally:
            self._in_invoke = False
        self._apply_state()


def mode_nodes(registry, editors, on_toggle, MenuNode):
    """The FIRST section of the bar: one button per registered editor type,
    checked when that type's editor is the current mode.

    It is here rather than in the shell because it is the same list the Tools
    MENU builds, and two spellings of "which editors exist" is exactly the kind
    of drift a registry is supposed to end. `MenuNode` is passed in so this
    module imports nothing — it is the tk half of the application and the core
    is the caller's business.
    """
    mode = editors.current_mode
    out = []
    for desc in registry.with_editors():
        if not desc.display_name:
            continue
        out.append(MenuNode(label=desc.display_name,
                            checked=(desc.id == mode),
                            action=lambda d=desc: on_toggle(d.id)))
    return out
