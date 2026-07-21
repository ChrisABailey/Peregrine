// Copyright (c) 1994-2013 Georgia Tech Research Corporation, Atlanta, GA
// This file is part of FalconView(tm).

// FalconView(tm) is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// FalconView(tm) is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with FalconView(tm).  If not, see <http://www.gnu.org/licenses/>.

// FalconView(tm) is a trademark of Georgia Tech Research Corporation.

#include "stdafx.h"

#include "gtest/gtest.h"

#include "geotrans.h"

const int MAX_GEOTRANS_STRING_LENGTH = 64;

// Note that all unit tests that utilitize CGeoTrans are disabled. This is
// because the GEOTRANS library depends on i) data files ii) an environement
// variable and iii) the geotrans DLL. Since these are not set up on the build
// machine (or shouldn't be) we disable these tests. They can be run by passing
// in the --gtest_also_run_disabled_tests command line flag
//

// UTM coordinates are not expected to be negative in the southern hemisphere
TEST(GeoTransTest, DISABLED_UTMNegativeHemisphereConversion)
{
   CGeoTrans geo_trans;

   char buf[MAX_GEOTRANS_STRING_LENGTH];
   EXPECT_EQ(SUCCESS,
      geo_trans.DLL_calc_utm_string("WGS84", -0.2, -80.0, buf,
      MAX_GEOTRANS_STRING_LENGTH));

   EXPECT_TRUE(strchr(buf, '-') == nullptr);
}

TEST(GeoTransTest, DISABLED_wge_descriptive_name)
{
   CGeoTrans geo_trans;
   char buf[MAX_GEOTRANS_STRING_LENGTH];

   EXPECT_EQ(SUCCESS,
      geo_trans.get_descriptive_name("WGE", buf, MAX_GEOTRANS_STRING_LENGTH));
   EXPECT_STRCASEEQ("World Geodetic System 1984", buf);
}

// W84, WGS84, and WGX should be aliased to WGE. This test verifies
// that these codes alias to WGE.
TEST(GeoTransTest, DISABLED_descriptive_name_aliases)
{
   CGeoTrans geo_trans;
   char buf[MAX_GEOTRANS_STRING_LENGTH];

   EXPECT_EQ(SUCCESS,
      geo_trans.get_descriptive_name("W84", buf, MAX_GEOTRANS_STRING_LENGTH));
   EXPECT_STRCASEEQ("World Geodetic System 1984", buf);

   EXPECT_EQ(SUCCESS,
      geo_trans.get_descriptive_name("WGS84", buf, MAX_GEOTRANS_STRING_LENGTH));
   EXPECT_STRCASEEQ("World Geodetic System 1984", buf);

   EXPECT_EQ(SUCCESS,
      geo_trans.get_descriptive_name("WGX", buf, MAX_GEOTRANS_STRING_LENGTH));
   EXPECT_STRCASEEQ("World Geodetic System 1984", buf);
}

// Test that get_descriptive_name returns the appropriate description for
// vertical datum strings
TEST(GeoTransTest, DISABLED_vertical_datum_strings)
{
   CGeoTrans geo_trans;
   char buf[MAX_GEOTRANS_STRING_LENGTH];

   EXPECT_EQ(SUCCESS,
      geo_trans.get_descriptive_name("GEOD", buf, MAX_GEOTRANS_STRING_LENGTH));
   EXPECT_STRCASEEQ("Geodetic", buf);

   EXPECT_EQ(SUCCESS,
      geo_trans.get_descriptive_name("MSL", buf, MAX_GEOTRANS_STRING_LENGTH));
   EXPECT_STRCASEEQ("Mean Sea Level", buf);
}

// Test that an unknown datum, UNK, is treated as WGE
TEST(GeoTransTest, DISABLED_unknown_datum_default)
{
   CGeoTrans geo_trans;
   char buf[MAX_GEOTRANS_STRING_LENGTH];

   EXPECT_EQ(SUCCESS,
      geo_trans.get_descriptive_name("UNK", buf, MAX_GEOTRANS_STRING_LENGTH));
   EXPECT_STRCASEEQ("World Geodetic System 1984", buf);
}

// Test that an invalid datum returns an appropriate error code
TEST(GeoTransTest, DISABLED_invalid_datum)
{
   CGeoTrans geo_trans;
   char buf[MAX_GEOTRANS_STRING_LENGTH];

   EXPECT_NE(SUCCESS,
      geo_trans.get_descriptive_name("VVV", buf, MAX_GEOTRANS_STRING_LENGTH));
}

// Test that the number of datums is at least what it was in GeoTrans 2.x
TEST(GeoTransTest, DISABLED_num_of_datums)
{
   CGeoTrans geo_trans;
   long datum_items = 0;
   EXPECT_EQ(SUCCESS,
      geo_trans.get_datum_count(datum_items));
   EXPECT_GE(datum_items, 230);
}

// Verify that datum codes are one-based in the call to get_datum_code
TEST(GeoTransTest, DISABLED_one_based_datum_code)
{
   CGeoTrans geo_trans;
   char datum_code[GEO_MAX_DATUM_LENGTH + 1];

   EXPECT_NE(SUCCESS,
      geo_trans.get_datum_code(0, datum_code, GEO_MAX_DATUM_LENGTH + 1));

   EXPECT_EQ(SUCCESS,
      geo_trans.get_datum_code(1, datum_code, GEO_MAX_DATUM_LENGTH + 1));
}

// The max datum length should be at least six to account for datums such as
// "INH-A1"
TEST(GeoTransTest, DISABLED_max_datum_length)
{
   EXPECT_GE(GEO_MAX_DATUM_LENGTH, 6);
}

// Test that we can succesfully retrieve each datum code by one-based index
TEST(GeoTransTest, DISABLED_get_datum_code_by_index)
{
   CGeoTrans geo_trans;
   long datum_items = 0;
   EXPECT_EQ(SUCCESS,
      geo_trans.get_datum_count(datum_items));

   char datum_code[GEO_MAX_DATUM_LENGTH + 1];
   for (int i = 1; i <= datum_items; ++i)
   {
      EXPECT_EQ(SUCCESS,
         geo_trans.get_datum_code(i, datum_code, GEO_MAX_DATUM_LENGTH + 1));
   }
}

// Test that datum codes are aliases to the proper user interface strings
TEST(GeoTransTest, DISABLED_user_interface_datum_aliases)
{
   CGeoTrans geo_trans;
   char ui_code[GEO_MAX_DATUM_LENGTH + 1];

   EXPECT_EQ(SUCCESS,
      geo_trans.get_user_datum_code("WGE", ui_code, GEO_MAX_DATUM_LENGTH + 1));
   EXPECT_STRCASEEQ("WGS84", ui_code);

   EXPECT_EQ(SUCCESS,
      geo_trans.get_user_datum_code("WGC", ui_code, GEO_MAX_DATUM_LENGTH + 1));
   EXPECT_STRCASEEQ("WGS72", ui_code);

   EXPECT_EQ(SUCCESS,
      geo_trans.get_user_datum_code("WGB", ui_code, GEO_MAX_DATUM_LENGTH + 1));
   EXPECT_STRCASEEQ("WGS66", ui_code);

   // Any datum other than WGE, WGC, or WGB should alias to itself
   EXPECT_EQ(SUCCESS,
      geo_trans.get_user_datum_code("VVV", ui_code, GEO_MAX_DATUM_LENGTH + 1));
   EXPECT_STRCASEEQ("VVV", ui_code);
}

TEST(GeoTransTest, DISABLED_datum_ellipsoid_name)
{
   CGeoTrans geo_trans;
   char ellipsoid_name[GEO_MAX_ELLIPSOID_NAME_LENGTH + 1];

   EXPECT_EQ(SUCCESS, geo_trans.get_datum_ellipsoid_name(
      "WGE", ellipsoid_name, GEO_MAX_ELLIPSOID_NAME_LENGTH + 1));
   EXPECT_STRCASEEQ("WGS 84                        ", ellipsoid_name);

   EXPECT_EQ(SUCCESS, geo_trans.get_datum_ellipsoid_name(
      "EUR-7", ellipsoid_name, GEO_MAX_ELLIPSOID_NAME_LENGTH + 1));
   EXPECT_STRCASEEQ("International 1924            ", ellipsoid_name);

   EXPECT_EQ(SUCCESS, geo_trans.get_datum_ellipsoid_name(
      "CAC", ellipsoid_name, GEO_MAX_ELLIPSOID_NAME_LENGTH + 1));
   EXPECT_STRCASEEQ("Clarke 1866                   ", ellipsoid_name);

   EXPECT_EQ(SUCCESS, geo_trans.get_datum_ellipsoid_name(
      "KEA", ellipsoid_name, GEO_MAX_ELLIPSOID_NAME_LENGTH + 1));
   EXPECT_STRCASEEQ("Everest (W. Mal. & Sing. 1948)", ellipsoid_name);
}

TEST(GeoTransTest, DISABLED_convert_geo_to_mgrs_and_utm)
{
   CGeoTrans geo_trans;
   char milgrid[GEO_MAX_MGRS_LENGTH + 1];
   int utm_zone;
   char utm_hemisphere;
   double utm_northing, utm_easting;

   EXPECT_EQ(SUCCESS,
      geo_trans.convert_geo(40.0, -89.0, "WGS84", utm_zone, utm_hemisphere,
      utm_northing, utm_easting, milgrid, GEO_MAX_MGRS_LENGTH + 1));
   EXPECT_EQ(utm_zone, 16);
   EXPECT_EQ(utm_hemisphere, 'N');
   EXPECT_DOUBLE_EQ(4429672.9731045803, utm_northing);
   EXPECT_DOUBLE_EQ(329274.50621795497, utm_easting);
   EXPECT_STRCASEEQ("16TCK2927429672", milgrid);

   EXPECT_EQ(SUCCESS,
      geo_trans.convert_geo(40.0, 89.0, "WGS84", utm_zone, utm_hemisphere,
      utm_northing, utm_easting, milgrid, GEO_MAX_MGRS_LENGTH + 1));
   EXPECT_EQ(utm_zone, 45);
   EXPECT_EQ(utm_hemisphere, 'N');
   EXPECT_DOUBLE_EQ(4429672.9731155597, utm_northing);
   EXPECT_DOUBLE_EQ(670725.49427117023, utm_easting);
   EXPECT_STRCASEEQ("45TXE7072529672", milgrid);

   EXPECT_EQ(SUCCESS,
      geo_trans.convert_geo(-40.0, -89.0, "WGS84", utm_zone, utm_hemisphere,
      utm_northing, utm_easting, milgrid, GEO_MAX_MGRS_LENGTH + 1));
   EXPECT_EQ(utm_zone, 16);
   EXPECT_EQ(utm_hemisphere, 'S');
   EXPECT_DOUBLE_EQ(5570327.0268954197, utm_northing);
   EXPECT_DOUBLE_EQ(329274.50621795497, utm_easting);
   EXPECT_STRCASEEQ("16HCA2927470327", milgrid);

   EXPECT_EQ(SUCCESS, geo_trans.convert_geo(-40.0, 89.0, "WGS84", utm_zone,
      utm_hemisphere, utm_northing, utm_easting, milgrid,
      GEO_MAX_MGRS_LENGTH + 1));
   EXPECT_EQ(utm_zone, 45);
   EXPECT_EQ(utm_hemisphere, 'S');
   EXPECT_DOUBLE_EQ(5570327.0268844403, utm_northing);
   EXPECT_DOUBLE_EQ(670725.49427117023, utm_easting);
   EXPECT_STRCASEEQ("45HXR7072570327", milgrid);
}

// Converting from MGRS with some non-WGS 84 datums can return
// GEOTRANS_OUTPUT_WARNING.  But not this case.
TEST(GeoTransTest, DISABLED_convert_non_wgs84_datum_from_mgrs)
{
   CGeoTrans geo_trans;
   double lat, lon;
   int utm_zone;
   char utm_hemisphere;
   double utm_northing, utm_easting;

   EXPECT_EQ(SUCCESS,
      geo_trans.convert_milgrid("45HXR7072570327", "ADI-A", lat, lon, utm_zone,
      utm_hemisphere, utm_northing, utm_easting));
}

// Tests the conversion from latitude, longitude to GARS
TEST(GeoTransTest, DISABLED_convert_to_gars)
{
   CGeoTrans geo_trans;
   char gars[GEO_MAX_GARS_LENGTH + 1];

   EXPECT_EQ(SUCCESS, geo_trans.convert_geo(32, -84.0, "WGS84", gars,
      GEO_MAX_GARS_LENGTH + 1));
   EXPECT_STRCASEEQ("193LE37", gars);

   EXPECT_EQ(SUCCESS, geo_trans.convert_geo(-32, -84.0, "WGS84", gars,
      GEO_MAX_GARS_LENGTH + 1));
   EXPECT_STRCASEEQ("193EW37", gars);
}

TEST(GeoTransTest, DISABLED_convert_to_mgrs)
{
   CGeoTrans geo_trans;
   char milgrid[GEO_MAX_MGRS_LENGTH + 1];

   EXPECT_EQ(SUCCESS, geo_trans.DLL_calc_milgrid_string("WGE", 35, 47,
      milgrid, GEO_MAX_MGRS_LENGTH + 1));
   EXPECT_STRCASEEQ("38S  PD  82516  74870", milgrid);

   EXPECT_EQ(SUCCESS, geo_trans.DLL_calc_milgrid_string("WGE", -31, -16,
      milgrid, GEO_MAX_MGRS_LENGTH + 1));
   EXPECT_STRCASEEQ("28J  DL  04531  69968", milgrid);
}

TEST(GeoTransTest, DISABLED_convert_to_polar_mgrs)
{
   CGeoTrans geo_trans;
   char milgrid[GEO_MAX_MGRS_LENGTH + 1];

   EXPECT_EQ(SUCCESS, geo_trans.DLL_calc_milgrid_string("WGE", 90, 0,
      milgrid, GEO_MAX_MGRS_LENGTH + 1));
   EXPECT_STRCASEEQ("ZAH  00  00000  000", milgrid);

   EXPECT_EQ(SUCCESS, geo_trans.DLL_calc_milgrid_string("WGE", -90, 0,
      milgrid, GEO_MAX_MGRS_LENGTH + 1));
   EXPECT_STRCASEEQ("BAN  00  00000  000", milgrid);
}
