#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

"""Sync the Peregrine publishable subset from this working tree.

Peregrine (<https://github.com/ChrisABailey/Peregrine>) is a curated subset,
not a mirror: it ships all of `port/` plus exactly the `fvw_core/` and
`third_party/` files the CMake build compiles or includes. That file set is
re-derived here from the build's own dependency output, so it stays honest as
modules are added — nothing is maintained by hand.

Usage (from the repo root, after a full `cmake --build build`):

    python3 port/tools/sync_peregrine.py --dest ../Peregrine          # dry run
    python3 port/tools/sync_peregrine.py --dest ../Peregrine --apply

Then review `git status` in the destination, commit, and push.

The copy is byte-for-byte. It used to have to graft a licence preamble onto
every `port/` file on the way out, because the headers lived only downstream;
the originals now carry them, so there is no transformation left to get wrong.
Keep it that way — a new file under `port/` should be born with the SPDX header
(see port/NOTICE.md), not acquire one here.

Destination-owned files are never touched: Peregrine's root README.md and
CMakeLists.txt describe the published repo and are edited there. LICENSE,
COPYING.LESSER and NOTICE.md are authored here under `port/` and are copied to
the destination root, which is where the file headers point.
"""

import argparse
import glob
import json
import os
import subprocess
import sys

# Authored under port/, published at the destination root.
ROOT_DOCS = {"port/LICENSE": "LICENSE",
             "port/COPYING.LESSER": "COPYING.LESSER",
             "port/NOTICE.md": "NOTICE.md"}

# Edited in the destination, never overwritten from here.
DEST_OWNED = {"README.md", "CMakeLists.txt", ".gitignore"}

# Same, but whole subtrees: the README's screenshots are authored downstream and
# have no upstream original, so a strict closure diff would propose deleting
# them on every sync.
DEST_OWNED_DIRS = ("Screenshots/",)

# Loaded at run time, so they never appear in a compile-time dependency
# closure — but the geo tests do not pass without them.
RUNTIME_DATA = [
    "fvw_core/PdfLib/sdk/lib/3_param.dat",
    "fvw_core/PdfLib/sdk/lib/7_param.dat",
    "fvw_core/PdfLib/sdk/lib/ellips.dat",
    "fvw_core/PdfLib/sdk/lib/egm96.grd",
    "port/Osm/styles/peregrine-osm.json",
]


def canonical(repo, rel):
    """Real on-disk spelling of `rel`.

    macOS is case-insensitive, so a .o.d can record the spelling used in the
    #include rather than the file's own name (`jpeglib.h` vs `JPEGLIB.H`).
    Left uncorrected, the file set churns by ~70 phantom entries per sync.
    """
    cur, out = repo, []
    for part in rel.split(os.sep):
        try:
            entries = os.listdir(cur)
        except OSError:
            return None
        if part in entries:
            real = part
        else:
            m = [e for e in entries if e.lower() == part.lower()]
            if not m:
                return None
            real = m[0]
        out.append(real)
        cur = os.path.join(cur, real)
    return os.sep.join(out)


def build_closure(repo, build_dir):
    """Every in-repo file the build compiled or included."""
    deps = set()
    for d in glob.glob(os.path.join(build_dir, "**", "*.o.d"), recursive=True):
        text = open(d, errors="ignore").read().replace("\\\n", " ")
        if ":" not in text:
            continue
        for tok in text.split(":", 1)[1].split():
            p = os.path.abspath(tok)
            if p.startswith(repo + os.sep) and os.path.isfile(p):
                deps.add(os.path.relpath(p, repo))
    cc = os.path.join(build_dir, "compile_commands.json")
    if os.path.exists(cc):
        for e in json.load(open(cc)):
            p = os.path.abspath(os.path.join(e.get("directory", "."), e["file"]))
            if p.startswith(repo + os.sep) and os.path.isfile(p):
                deps.add(os.path.relpath(p, repo))
    # Generated/fetched output is not source.
    return {d for d in deps if not d.startswith(("build/", "port/third_party/_"))}


def port_extras(repo):
    """port/ files the build never names but the repo still ships."""
    out = []
    for root, dirs, files in os.walk(os.path.join(repo, "port")):
        dirs[:] = [d for d in dirs if d not in ("__pycache__", ".git")]
        for fn in files:
            if fn == ".DS_Store" or fn.endswith(".pyc"):
                continue
            if (fn.endswith((".md", ".py", ".sh", ".h", ".cpp", ".c", ".hpp"))
                    or fn in ("CMakeLists.txt", "LICENSE", "COPYING.LESSER")):
                out.append(os.path.relpath(os.path.join(root, fn), repo))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dest", required=True, help="Peregrine clone")
    ap.add_argument("--build", default="build", help="build dir (default: build)")
    ap.add_argument("--apply", action="store_true", help="write; default is a dry run")
    args = ap.parse_args()

    repo = os.path.abspath(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))))
    dest = os.path.abspath(args.dest)
    build_dir = os.path.join(repo, args.build)
    if not os.path.isdir(os.path.join(dest, ".git")):
        sys.exit(f"not a git clone: {dest}")
    if not glob.glob(os.path.join(build_dir, "**", "*.o.d"), recursive=True):
        sys.exit(f"no dependency output under {build_dir} — build first, so the "
                 "file set can be derived from what actually compiled")

    wanted = build_closure(repo, build_dir)
    wanted.update(port_extras(repo))
    wanted.update(RUNTIME_DATA)

    mapping = {}
    for rel in sorted(wanted):
        real = canonical(repo, rel)
        if real is None:
            print(f"  ! vanished from the tree, skipping: {rel}")
            continue
        mapping[real] = ROOT_DOCS.get(real, real)

    have = subprocess.run(["git", "ls-files"], cwd=dest, capture_output=True,
                          text=True).stdout.split()
    stale = sorted(f for f in set(have) - set(mapping.values()) - DEST_OWNED
                   if not f.startswith(DEST_OWNED_DIRS))

    added = updated = 0
    for src_rel, dst_rel in sorted(mapping.items()):
        src, dst = os.path.join(repo, src_rel), os.path.join(dest, dst_rel)
        data = open(src, "rb").read()
        if os.path.exists(dst):
            if open(dst, "rb").read() == data:
                continue
            updated += 1
        else:
            added += 1
            print(f"  + {dst_rel}")
        if args.apply:
            os.makedirs(os.path.dirname(dst) or dest, exist_ok=True)
            with open(dst, "wb") as f:
                f.write(data)

    for rel in stale:
        print(f"  - {rel}")
        if args.apply:
            subprocess.run(["git", "rm", "-q", "--", rel], cwd=dest, check=False)

    print(f"\n{len(mapping)} files tracked: {added} added, {updated} updated, "
          f"{len(stale)} removed{'' if args.apply else '  (DRY RUN)'}")
    if args.apply:
        print("\nNow, in the destination:")
        print("  cmake -B build && cmake --build build -j && ctest --test-dir build")
        print("  git add -A && git commit && git push")
        print("Build and test there before pushing — that is what proves the "
              "curated subset is complete.")


if __name__ == "__main__":
    main()
