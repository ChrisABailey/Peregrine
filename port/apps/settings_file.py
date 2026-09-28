# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Writes changed keys back into a Peregrine settings (INI) file.

`fv::Settings` only reads. This rewrites the lines for the keys it is given
and nothing else, so comments, ordering and keys the app does not know about
survive. A key already set in its section has its line replaced; a missing key
is appended to the end of its section; a missing section is appended to the
file.
"""

import os
import re

_SECTION = re.compile(r"^\s*\[([^\]]+)\]\s*(?:[#;].*)?$")


def format_value(value):
    """An INI value as `fv::Settings` reads it back: bools as true/false,
    numbers bare, everything else double-quoted."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    return '"' + str(value) + '"'


def save_keys(path, values):
    """Writes `values` ({"section.key": value}) into the INI file at `path`,
    creating the file and its directory if needed. Replaces the file
    atomically. Raises OSError on a write failure."""
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except FileNotFoundError:
        lines = []

    for full_key, value in values.items():
        section, _, name = full_key.partition(".")
        entry = f"{name} = {format_value(value)}"
        key_re = re.compile(r"^(\s*)" + re.escape(name) + r"\s*=")

        start = end = None  # line range of the section's body
        for i, line in enumerate(lines):
            m = _SECTION.match(line)
            if m is None:
                continue
            if start is not None:
                end = i
                break
            if m.group(1).strip() == section:
                start = i + 1
        if start is None:
            if lines and lines[-1].strip():
                lines.append("")
            lines += [f"[{section}]", entry]
            continue
        if end is None:
            end = len(lines)

        for i in range(start, end):
            m = key_re.match(lines[i])
            if m:
                lines[i] = m.group(1) + entry
                break
        else:
            at = end
            while at > start and not lines[at - 1].strip():
                at -= 1
            lines.insert(at, entry)

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, path)
