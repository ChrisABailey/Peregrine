# Peregrine — instructions for Claude sessions

Peregrine is the published subset of a private FalconView port (FVW). Most of this tree is
**copied in from FVW by a sync script**; work here reaches FVW only by pull request and a
back-port. The current work in this repository is the Linux desktop app,
`port/apps/PeregrineGtk` (GTK4 via gtkmm-4).

## Read first

1. `port/linux-plan.md` — the Linux plan: ownership, the `#ifdef` policy, what must stay
   identical between the mac and Linux apps, the session list (LX1…).
2. `port/desktop-plan.md` — the desktop architecture (ViewKit, DeskKit, decisions K1–K14).
3. `port/PORTING.md` — the ledger, for status and conventions. Read it; do not edit it.
4. `port/apps/Peregrine/` — the macOS shell, the reference for behaviour the GTK shell
   mirrors. Read it; do not edit it.

Work on one LX session at a time. Be compiler-driven: build, fix the errors, rebuild; do not
explore the tree broadly.

## Build

```sh
sudo apt-get install -y build-essential cmake ninja-build pkg-config git \
    libsqlite3-dev libgtkmm-4.0-dev xvfb fonts-dejavu-core \
    python3-dev python3-numpy python3-pytest python3-tk
cmake -B build -G Ninja
cmake --build build -j
xvfb-run -a ctest --test-dir build -j8
```

The root `README.md` (*Build ▸ Linux*) explains each package and the offline
configure. `testdata/` holds a small sample data set and the build points the
tests at it; tests pinned to larger data skip themselves (`ctest` counts a
skip as a pass, so report the skip count too: 182 on Linux with the sample
set). Screenshots: `xvfb-run -a <binary> --shot out.png`; for the app on the
sample data see `port/apps/PeregrineGtk/README.md`.

## Hard rules

- **Never push to `main`.** Work on `linux/<topic>` and open a pull request. The PR
  description ends with a **Ledger notes** section: what landed, what is open, and any
  decision for the mac side.
- **The sync overwrites this tree.** Do not edit `port/PORTING.md`, `port/*-plan.md`,
  `port/apps/Peregrine/`, `port/tools/sync_peregrine.py`, or anything in `third_party/`
  (frozen; workarounds go in the module's CMake). Propose such changes in the PR description.
- **Peregrine-owned files** (never synced): `CLAUDE.md`, `README.md`, the root
  `CMakeLists.txt`, `.gitignore`, `PrivacyPolicy.md`, `Screenshots/`. The root
  `CMakeLists.txt` already adds `port/apps/PeregrineGtk` on Linux when it exists.
- **Shared code** (`port/` outside `port/apps/*`, `fvw_core/`) builds on macOS, iOS, Linux and
  Windows. Changes there are small separate commits titled by module (`DeskKit: …`), and API
  changes are **additive only** — the Swift shell cannot be built here to catch a break.
- **`#ifdef`**: portable C++17 first; then move the difference into the shell or data; only
  then a narrow guard (`__has_include`, `_WIN32`, `__APPLE__`, `__linux__` — never
  `#ifndef __APPLE__` to mean Linux) with a one-line reason, macOS behaviour unchanged. Do not
  grow `port/include/fv_compat.h`. Never `#ifdef` a test away; use `GTEST_SKIP()`. Details in
  `port/linux-plan.md` § `#ifdef` policy.
- **Keep the two apps one product.** Commands, menus, shortcuts (Ctrl for ⌘), status bar,
  settings keys and paths, workspace files and command-line options come from DeskKit and
  match the mac app. Decisions go in C++ under `port/` with a gtest (FakeDesk first), never in
  the GTK shell. Look, HIG conventions and dev tooling may differ.
- **The whole suite is green on Linux** before every PR, run under `xvfb-run` so the Tk tests
  of the Python app do not skip.
- New files carry the SPDX header used throughout `port/` (`LGPL-3.0-or-later`,
  `Copyright (C) 2026 Chris Bailey`).

## Comments

Doc comment (`///` or `/** */`) before every non-trivial function, type and file: one sentence
on what it does, plus parameters, units, thread or errors only when not obvious. Inline
comments only where the code does something unexpected, stating the reason in one or two
lines. Present tense, third person, terse. No session or plan-phase references ("LX2",
"as of this PR"), no diary prose, no emphasis or humour — design history belongs in the PR.
