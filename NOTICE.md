# Licensing notice

Peregrine as a whole is distributed under the **GNU General Public License,
version 3 or later** (`LICENSE`). It combines code under several licenses; this
file records what came from where.

## 1. New Peregrine code — GPL-3.0-or-later

Everything under `port/`, except `port/vendor/` and the files listed in
section 3, is new work written for this port. Those files carry:

```
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
```

This includes the portability layer (`port/include/`), the FvKit library
(`port/fvkit/`), the Python bindings (`port/bindings/pyfvw/`), the CLI tools,
the demos and all tests.

## 2. FalconView — LGPL-3.0-or-later

FalconView(tm) itself is Copyright (c) 1994–2011 Georgia Tech Research
Corporation, Atlanta, GA, released under the GNU Lesser General Public License
version 3 or later (`COPYING.LESSER`). Original copyright and license headers
are preserved verbatim in every file.

**This covers most, but not all, of `fvw_core/`.** FalconView vendored several
third-party libraries *inside* its own source tree, and those keep their own
(permissive, GPL-compatible) licenses rather than the GTRC LGPL — chiefly
`fvw_core/ImageLib/{jpeg,jpeg12,tiff,png,zlib}` and `fvw_core/Utility/SVD/`.
They are itemised in section 4 below, which governs them. As a rough measure of
the split, of the `fvw_core/` files present here roughly a third carry the GTRC
notice and the rest belong to those vendored libraries.

Only the subset of FalconView that this port actually compiles or includes is
present here. The upstream project is the authoritative and complete source.

Where these files were modified for the port, the changes are marked in place
with a dated `NOTE (port ...)` comment or an `#ifdef _WIN32` guard, so the
delta from upstream stays reviewable. FalconView's own Windows build files are
not included in this repository and were never modified.

## 3. Code derived from FalconView — LGPL-3.0-or-later, relicensed to GPL-3.0

These files under `port/` were extracted from FalconView classes rather than
written fresh, and retain the Georgia Tech Research Corporation copyright
notice and the FalconView LGPL header:

```
port/DtedMapServer/fv_dted_cell.{h,cpp}
port/GeoTIFFMapServer/fv_geotiff_frame.{h,cpp}
port/MapScaleUtil/fv_map_scale_util.{h,cpp}
port/MapSeriesStringConverter/fv_map_series_string_converter.{h,cpp}
port/geoid/fv_geoid.{h,cpp}
```

LGPL-3.0 section 2 permits conveying a covered work under the terms of the
GNU GPL version 3, retaining the original notices. That is the basis on which
the combined work here is GPL-3.0. Their original LGPL terms remain available
to anyone who takes them from upstream FalconView instead.

## 4. Third-party components — their own licenses

| Path | Component | License |
|------|-----------|---------|
| `third_party/geotrans-3.3/` | NGA GEOTRANS 3.3 (MSP CCS) | U.S. Government work; distributed by NGA without restriction. See the notices in the source headers. |
| `third_party/gdal_1.9.1/frmts/jpeg/libjpeg/` | Independent JPEG Group libjpeg, as vendored by GDAL | IJG license (BSD-style) |
| `port/vendor/stb_truetype.h` | stb_truetype v1.26, Sean Barrett | Public domain / MIT (dual), as stated in the file |
| `fvw_core/Utility/SVD/` | ALGLIB (Sergey Bochkanov, 2005–2007), as vendored by FalconView | 3-clause BSD, stated in each source file |
| `fvw_core/ImageLib/zlib/` | zlib (Jean-loup Gailly, Mark Adler) | zlib license — full text in `zlib.h` |
| `fvw_core/ImageLib/png/` | libpng | libpng license — full text in `png.h` |
| `fvw_core/ImageLib/tiff/` | libtiff (Sam Leffler, Silicon Graphics) | libtiff/MIT-style permission notice in each source file |
| `fvw_core/ImageLib/jpeg/`, `jpeg12/` | Independent JPEG Group libjpeg, forked by FalconView | IJG license — see note below |

These are unmodified except where a port change is marked in place, and each
retains its own copyright headers.

**A note on the vendored libjpeg.** The IJG per-file headers say "for
conditions of distribution and use, see the accompanying README file". Upstream
FalconView vendored these sources *without* that README, so it is not present
here either — this repository reproduces upstream's file set rather than
inventing a license file for it. The applicable terms are the standard IJG
license, available from the Independent JPEG Group's distribution
(<https://www.ijg.org/>). FalconView's fork additionally adds a `crypt()` /
`jpeg_encrypt_table` feature not present in stock libjpeg.

Note also that `fvw_core/ImageLib/StdAfx.h` contains Windows-only `#import`
directives and a `KAKADU_SUPPORT` define referencing the MrSID and Kakadu SDKs.
Those are inert here: no such SDK headers, libraries or type libraries are
included in this repository, and none of that code is compiled by this build.

## 5. Data files

`fvw_core/PdfLib/sdk/lib/` here contains **only** the GEOTRANS reference data
that the coordinate-conversion tests need at runtime via `MSPCCS_DATA`:

```
3_param.dat   7_param.dat   ellips.dat   egm96.grd
```

These are NGA datum/ellipsoid parameter tables and the EGM96 geoid grid —
U.S. Government works. The commercial TerraGo GeoPDF SDK headers and
`pdfnet.res` that live alongside them in the upstream FalconView tree are
**not** included in this repository.

No map test data (DTED, CADRG, GeoTIFF, DNC/VPF, TIROS) is included. See
"Test data" in `README.md`.
