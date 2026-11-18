<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# analysis-py: Range & Bearing as a Python package

A Python package (`com.example.analysis`) that adds an **Analysis (Python)** file overlay
(`.pyana`, JSON) and a **Tools ▸ Analysis (Python) ▸ Open Sample Measurement** entry. It is
the PythonView analysis tool (`port/apps/analysis.py`) rebuilt on the package format in
`port/sdk/PACKAGING.md`, and it is meant as a realistic package for exercising the package
manager: several modules, resource files, an editor palette, properties, a context
menu, hit testing, persistence, undo, background work, host elevation and a tool.

## Building and installing

`cmake --build build --target analysis_pgpkg` packs
`build/port/sdk/examples/analysis-py/com.example.analysis.pgpkg`. Install it with the package
manager's Install…, or link `port/sdk/examples/analysis-py/package` with Install from Folder…
to edit the Python in place (relaunch to pick up a change).

## Using it

1. Tools ▸ Analysis (Python) ▸ Open Sample Measurement, or create a new Analysis (Python)
   overlay.
2. The terrain for the profile and the viewsheds comes from the elevation data (DTED, lidar)
   in the map data catalog; measurement needs none. Adding elevation data to the catalog
   computes any viewshed that lacked it.
3. Press the Analysis (Python) toolbar button to edit. The palette:
   - **Line / Path / Area** arm a drawing tool: the first click starts a new measurement
     (the others stay), each click adds a point, **Return** finishes, **Escape** cancels (a
     line ends at its second point). Pressing an armed tool disarms it.
   - **Viewshed** drops one observer at the next click, at the configured height and range;
     it computes in the background and the map repaints when it is done.
   - **Profile** shows or hides the terrain-profile inset for the current measurement (the
     last one drawn, or the one a point was last selected in).
   - **Delete Selected** and **Clear**. With no tool armed, drag a point or an observer to move
     it; a click selects it and **Delete** removes it. **Edit ▸ Undo** undoes path edits.
4. Right-click the map for the per-item commands, editing or not: **Measure … From Here**,
   **Add Viewshed Here**, **Move … / Move Here** and **Delete** / **Remove** over a point or an
   observer, **Delete Measurement** over a point, and the **Analysis (Python)** submenu (line type, profile and masked-ground
   toggles, undo and redo, recompute, clear).

Hovering a point shows its leg in the status bar. The profile is drawn as an inset in the
lower-left of the map.

## Differences from PythonView's tool

- The terrain profile is an inset chart, not a separate window.
- Viewsheds are capped by **Viewshed post limit**; the result is not saved, and is recomputed
  in the background when the document opens.
- No keyboard shortcuts for line type (`g`) or undo (`u`); the context menu and Edit ▸ Undo
  cover them.
- An overlay holds any number of measurements, not one.
- The measurements, viewsheds and settings are a document that saves and reopens (format 2;
  format 1 files, with one measurement, still open).
- No rubber-band line to the pointer while drawing: the host passes pointer moves to an
  editor only while a button is down.
- `numpy` is optional; without it a viewshed draws only its range ring.
