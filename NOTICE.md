# Licensing notice

Peregrine as a whole is distributed under the **GNU Lesser General Public
License, version 3 or later** (`COPYING.LESSER`). LGPL-3.0 is the terms and
conditions of GNU GPL version 3 (`COPYING`) supplemented by a set of additional
permissions, so both texts ship here and both apply. Peregrine combines code
under several licenses; this file records what came from where.

## 1. New Peregrine code — LGPL-3.0-or-later

Everything under `port/`, except `port/vendor/` and the files listed in
section 3, is new work written for this port. Those files carry:

```
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
```

This includes the portability layer (`port/include/`), the FvKit library
(`port/fvkit/`), the Python bindings (`port/bindings/pyfvw/`), the CLI tools,
the demos and all tests.

LGPL rather than GPL is deliberate. Section 2 of this notice is a floor that
cannot be lowered — the FalconView code Peregrine is built from is LGPL, and a
port of it stays LGPL. Matching that floor for the new code, instead of raising
the whole work to GPL, keeps the one property the upstream license was chosen
for: an application may link FvKit without itself becoming a derivative work.
See section 6.

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

## 3. Code derived from FalconView — LGPL-3.0-or-later

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

These are derivative works of FalconView and are conveyed under FalconView's
own terms, unchanged. No relicensing takes place anywhere in this repository —
which is why sections 1, 2 and 3 all name the same license, and why the work as
a whole can be distributed under a single one.

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

## 6. Linking Peregrine into your own application

This is what the LGPL choice in section 1 is for. An application that links
FvKit — statically or dynamically — is a "Combined Work" under LGPL-3.0
section 4, not a derivative of it, and may carry any license you like,
including a proprietary one. In exchange, section 4 asks that you:

- state that FvKit is used and is covered by the LGPL, and ship both
  `COPYING` and `COPYING.LESSER` with your application (§4a, §4b);
- carry the FalconView and Peregrine copyright notices wherever your
  application displays its own (§4c);
- leave the user able to relink your application against a modified FvKit —
  satisfied either by linking FvKit as a shared library, or by shipping your
  object files alongside FvKit's source (§4d).

Note that LGPL-3.0 inherits GPL-3.0 section 10: a distributor may not impose
further restrictions on the rights the license grants. Storefronts whose terms
limit how many devices a purchaser may install on have historically been read
as imposing exactly that, which constrains the channels an application built on
this code can ship through. Also note LGPL-3.0 section 3, which applies when
you use more than a small amount of material from FvKit's *headers* — templates
and inline functions above the section 3 threshold carry the notice
requirements above even without linking.

Nothing here is legal advice; read `COPYING.LESSER` rather than this summary.

## 7. Trademarks

FalconView is a trademark of Georgia Tech Research Corporation, as stated in
the header of every GTRC file here. Peregrine is an independent port. It is not
FalconView, is not a Georgia Tech product, and is neither endorsed by nor
affiliated with Georgia Tech Research Corporation. The trademark is preserved
in the upstream file headers as an attribution, and no license to it is granted
by the LGPL or by this notice.
