#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Convert backslashes to forward slashes in #include directives only.
Forward-slash includes are valid for MSVC too, so this is Windows-safe.
Preserves CRLF and encoding byte-for-byte elsewhere.
Usage: fix_include_slashes.py <file>..."""
import re, sys
for p in sys.argv[1:]:
    data = open(p, 'rb').read()
    fixed = re.sub(
        rb'(#\s*include\s+["<][^">\r\n]*[">])',
        lambda m: m.group(1).replace(b'\\', b'/'),
        data)
    if fixed != data:
        open(p, 'wb').write(fixed)
        print(f'fixed: {p}')
