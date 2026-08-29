#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Wrap named function definitions in `#ifdef _WIN32` / `#endif`.

The recurring shape of a FalconView port: a .cpp mixes portable logic with
GDI/COM rendering methods, and the rendering half must stay Windows-only while
the file compiles in place on POSIX. Hand-editing a dozen brace-matched spans
is error-prone, so this does it mechanically.

A target is matched by its *definition* line — "Class::Method" appearing before
the opening brace. The span guarded runs from the start of the signature
(including any leading comment block directly above it) to the matching close
brace, tracking braces outside string/char literals and comments.

Usage:
    guard_win32_functions.py <file.cpp> Class::Method [Class::Method ...]

Idempotent: a target already immediately preceded by `#ifdef _WIN32` is
skipped, so re-running after adding targets is safe. Prints what it guarded.
"""

import re
import sys


def find_matching_brace(src, open_idx):
    """Index just past the brace matching src[open_idx] == '{'."""
    depth = 0
    i = open_idx
    n = len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            i = src.find('\n', i)
            if i < 0:
                return -1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            end = src.find('*/', i + 2)
            if end < 0:
                return -1
            i = end + 2
            continue
        if c in ('"', "'"):
            quote = c
            i += 1
            while i < n:
                if src[i] == '\\':
                    i += 2
                    continue
                if src[i] == quote:
                    break
                i += 1
            i += 1
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def signature_start(src, name_idx):
    """Walk back to the first column of the return type, absorbing the
    comment block directly above the definition."""
    line_start = src.rfind('\n', 0, name_idx) + 1
    while True:
        prev_end = line_start - 1
        if prev_end <= 0:
            break
        prev_start = src.rfind('\n', 0, prev_end) + 1
        prev = src[prev_start:prev_end].strip()
        # Absorb comment lines and a bare return type on its own line.
        if prev.startswith('//') or prev.startswith('/*') or prev.startswith('*'):
            line_start = prev_start
            continue
        break
    return line_start


def guard(path, targets):
    # newline='' keeps CRLF intact. FalconView's shared sources are CRLF, and
    # a naive text-mode round-trip rewrites every line, burying a 100-line
    # change in a 6000-line diff.
    with open(path, newline='') as f:
        src = f.read()
    done, missing = [], []
    for target in targets:
        cls, _, meth = target.partition('::')
        # The definition: "Class::Method" followed by '(' then eventually '{'.
        pat = re.compile(r'\b' + re.escape(cls) + r'\s*::\s*' + re.escape(meth)
                         + r'\s*\(')
        placed = False
        for m in pat.finditer(src):
            brace = src.find('{', m.end())
            if brace < 0:
                continue
            # Between ')' and '{' only an initializer list / const / whitespace
            # may appear; a ';' means this was a declaration, not a definition.
            if ';' in src[m.end():brace]:
                continue
            end = find_matching_brace(src, brace)
            if end < 0:
                continue
            start = signature_start(src, m.start())
            if src[:start].rstrip().endswith('#ifdef _WIN32'):
                placed = True  # already guarded
                break
            src = (src[:start] + '#ifdef _WIN32\n' + src[start:end]
                   + '\n#endif  // _WIN32 (' + target + ')' + src[end:])
            done.append(target)
            placed = True
            break
        if not placed:
            missing.append(target)
    with open(path, 'w', newline='') as f:
        f.write(src)
    return done, missing


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    done, missing = guard(sys.argv[1], sys.argv[2:])
    for t in done:
        print('guarded  ', t)
    for t in missing:
        print('NOT FOUND', t)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
