# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.
"""Tk key events -> pyfvw.overlay.KeyEvent.

WHY THIS IS A MODULE AND NOT FOUR LINES IN PythonView (2026-08-04, from
Chris hooking a route overlay to tkinter):

1. **`event.keycode` is unusable.**  It is the field that looks right and is
   not.  Aqua Tk packs the Mac virtual keycode and the character into one
   int, so the Left arrow arrives as 2063660802 and Escape as 889192475 —
   values that mean nothing, agree with nothing, and differ per platform.
   The portable fields are `keysym` (a stable NAME on every platform),
   `char`, and `state`.  This module reads those three and never `keycode`.

2. **The modifier bits are not the same on every platform**, so the decode
   needs a home where it can be read and tested:

       Aqua      Shift 0x1  Control 0x4  Option/Alt 0x20000  Command 0x8
       X11       Shift 0x1  Control 0x4  Alt (Mod1) 0x8      Super   0x40
       Windows   Shift 0x1  Control 0x4  Alt        0x20000  Win     0x40

   Note the trap: on X11 Alt is 0x8, and on Aqua 0x8 is COMMAND.  Reading
   0x8 as "alt" everywhere would silently turn every Cmd-shortcut on macOS
   into an Alt-shortcut.  (The Aqua values are measured on this machine's
   Tk; the others are Tk's documented Mod1/Mod4 assignments.)

3. `key_event()` takes anything with `.keysym`, `.char` and `.state`, so
   the mapping is testable without a display, an event loop, or a window.

`KeyEvent.key` values are Win32 virtual-key codes — see the rationale in
`port/include/fvkit/overlay/overlay.h`.  Letters and digits are their ASCII
UPPERCASE code points, which is why the table below holds only named keys.
"""

import sys

import pyfvw

# Tk keysym -> Win32 VK.  Only keys whose name is not a single character;
# everything else falls through to ord(keysym.upper()).
_NAMED = {
    "BackSpace": pyfvw.overlay.key.BACKSPACE,
    "Tab": pyfvw.overlay.key.TAB,
    "ISO_Left_Tab": pyfvw.overlay.key.TAB,      # what Shift-Tab reports on X11
    "Return": pyfvw.overlay.key.RETURN,
    "KP_Enter": pyfvw.overlay.key.RETURN,
    "Escape": pyfvw.overlay.key.ESCAPE,
    "space": pyfvw.overlay.key.SPACE,
    "Prior": pyfvw.overlay.key.PAGE_UP,          # Tk's name for Page Up
    "Next": pyfvw.overlay.key.PAGE_DOWN,         # ... and Page Down
    "End": pyfvw.overlay.key.END,
    "Home": pyfvw.overlay.key.HOME,
    "Left": pyfvw.overlay.key.LEFT,
    "Up": pyfvw.overlay.key.UP,
    "Right": pyfvw.overlay.key.RIGHT,
    "Down": pyfvw.overlay.key.DOWN,
    "Insert": pyfvw.overlay.key.INSERT,
    "Delete": pyfvw.overlay.key.DELETE,
}
for _n in range(1, 13):
    _NAMED["F%d" % _n] = getattr(pyfvw.overlay.key, "F%d" % _n)

# Numeric keypad digits and the arrows Tk reports when NumLock is off.
for _n in range(10):
    _NAMED["KP_%d" % _n] = 0x60 + _n            # VK_NUMPAD0..9
_NAMED.update({
    "KP_Left": pyfvw.overlay.key.LEFT, "KP_Up": pyfvw.overlay.key.UP,
    "KP_Right": pyfvw.overlay.key.RIGHT, "KP_Down": pyfvw.overlay.key.DOWN,
    "KP_Home": pyfvw.overlay.key.HOME, "KP_End": pyfvw.overlay.key.END,
    "KP_Prior": pyfvw.overlay.key.PAGE_UP,
    "KP_Next": pyfvw.overlay.key.PAGE_DOWN,
    "KP_Delete": pyfvw.overlay.key.DELETE,
    "KP_Insert": pyfvw.overlay.key.INSERT,
})

_SHIFT = 0x1
_CTRL = 0x4
if sys.platform == "darwin":
    _ALT, _META = 0x20000, 0x8
elif sys.platform.startswith("win"):
    _ALT, _META = 0x20000, 0x40
else:
    _ALT, _META = 0x8, 0x40

# Modifier keys themselves: Tk reports a press for each, and an overlay
# asking "was a key pressed" does not want to hear about them.
MODIFIER_KEYSYMS = frozenset({
    "Shift_L", "Shift_R", "Control_L", "Control_R", "Alt_L", "Alt_R",
    "Meta_L", "Meta_R", "Super_L", "Super_R", "Caps_Lock", "Num_Lock",
    "Scroll_Lock", "Mode_switch", "ISO_Level3_Shift",
})


def vk_for_keysym(keysym):
    """Win32 VK for a Tk keysym, or 0 if this shell has no name for it."""
    vk = _NAMED.get(keysym)
    if vk is not None:
        return vk
    if len(keysym) == 1:
        # Letters and digits ARE their ASCII uppercase values under VK.
        # Punctuation is deliberately left unnamed (VK_OEM_* are keyboard-
        # layout-specific and would be a lie on a non-US layout) — `text`
        # carries it, which is what punctuation is for anyway.
        upper = keysym.upper()
        if upper.isascii() and (upper.isalpha() or upper.isdigit()):
            return ord(upper)
    return pyfvw.overlay.key.NONE


def is_modifier(event):
    """True for a press of Shift/Ctrl/Alt/Cmd itself, which is not a key."""
    return getattr(event, "keysym", "") in MODIFIER_KEYSYMS


def key_event(event):
    """Tk event -> pyfvw.overlay.KeyEvent.

    `event` needs only `.keysym`, `.char` and `.state`; a real tkinter Event
    has them, and so does any stand-in a test builds.
    """
    keysym = getattr(event, "keysym", "") or ""
    char = getattr(event, "char", "") or ""
    state = getattr(event, "state", 0)
    if not isinstance(state, int):     # Tk hands back a string for some
        state = 0                      # synthetic events
    return pyfvw.overlay.KeyEvent(
        key=vk_for_keysym(keysym),
        # Control-<letter> reports the letter on Aqua and a C0 control on
        # some X11 builds; a control code is not text an overlay should
        # insert, so it is dropped and `key` + `ctrl` carry the meaning.
        text=ord(char) if len(char) == 1 and ord(char) >= 0x20 else 0,
        shift=bool(state & _SHIFT),
        ctrl=bool(state & _CTRL),
        alt=bool(state & _ALT),
        meta=bool(state & _META),
    )
