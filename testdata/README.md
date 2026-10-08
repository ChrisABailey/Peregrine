# Sample map data

A small, redistributable subset of the map data the Peregrine tests run
against. CMake points every data-backed test at this directory
(`FVW_TESTDATA_DIR=<repo>/testdata`). Tests whose files are here run; tests
pinned to larger files that are not here skip themselves.

Most of it is one area, Kiawah Island and Charleston SC (32.6N 80.1W), so
the formats overlap on screen. TIROS adds whole-world background.

Built by `port/tools/make_sampledata.py` from the full test tree. Files are
copied byte for byte, except for the GeoTIFFs noted below.

| Directory | Contents | Source | Terms |
|---|---|---|---|
| `dted/` | DTED level 1 cells w082/n31, w081/n32, W084/n34, W084/n35 (the upper-case `W084` is deliberate) | NIMA/NGA DTED Level 1 | Public Domain |
| `tiros3/topobath/` | TopoBath 16 km and 8 km (whole world), two 500 m tiles | FalconView TIROS TopoBath derived from NASA Blue Marble Data | Public Domain |
| `enc/US*` | 8 NOAA ENC cells, Charleston approaches (usage bands 2–5), with their update files | NOAA Office of Coast Survey | NOAA ENC User Agreement, `enc/ENC_ROOT/` |
| `enc/CATALOG.031` | Exchange-set catalogue for US5CHSEC | NOAA | as above |
| `enc/s57*.csv` | S-57 object/attribute catalogue | GDAL S-57 support files | MIT |
| `enc/chartsymbols.xml`, `enc/rastersymbols-*.png` | S-52 presentation library | OpenCPN | GPL licence |
| `geotiff/22620e2710n_1ft.tif` | 1024×1024 window, 1 ft/pixel | Charleston County SC 2012 colour orthoimagery (USGS distribution) | public domain |
| `geotiff/22620e2710n_4ft.tif` | 2048 ft around the same spot, box-filtered to 4 ft/pixel | as above | as above |
| `OSM/kiawah.mbtiles` | OpenMapTiles-schema vector tiles, z0–14, cut from a US-south build | © OpenStreetMap contributors | ODbL 1.0 |
| `OSM/kiawah.fvroad` | Routing graph built from the same extract | © OpenStreetMap contributors | ODbL 1.0 |
| `OSM/map-*.osm` | Overlapping OSM XML exports over Kiawah | © OpenStreetMap contributors | ODbL 1.0 |
| `tides/8667062.json` | Tide predictions, Kiawah Bridge station | NOAA CO-OPS | public domain |
| `kiawah_cycle.gpx` | A recorded bicycle ride on Kiawah Island | Chris Bailey | LGPL-3.0-or-later, as the rest of the repository |
| `sim/kiawah_*.gpx` | Simulated-GPS feeds derived from that ride (1× and 2× speed) | as above | as above |

## Notes

- NOAA ENCs redistributed here are **not official charts and are not for
  navigation**. NOAA asks that the ENC user agreement travel with the cells
  and that users be pointed at <https://www.nauticalcharts.noaa.gov/> for
  current editions.
- The two GeoTIFFs are windows of sheet `22620e2710n` (SC State Plane, NAD83,
  US feet). Their tiepoint and pixel scale are rewritten for the window; every
  other tag is copied. The `.tfw` beside each matches.
- OSM-derived files are a Derivative Database under the ODbL; the same terms
  apply to anything built from them.

