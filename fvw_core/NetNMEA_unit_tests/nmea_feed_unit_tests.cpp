// Copyright (c) 1994-2012 Georgia Tech Research Corporation, Atlanta, GA
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
#include "../geo_tool/geo_tool_d.h"
#include "nmea.h"

static const float UNDEFINED_ALTITUDE = -9999.0f;

// Test that the 9th and 10th fields of a GGA sentence are parsed correctly
TEST(NMEA_sentence, gga_altitude_meters)
{
   NMEA_sentence sentence;
   EXPECT_TRUE(sentence.process_GGA("$GPGGA,201730,3229.3086,N,09339.2410,"
      "W,1,08,1.1,81.5,M,-26.2,M,,*48") == TRUE);

   EXPECT_EQ(sentence.get_altitude(), 81.5f);
}

// Make sure that unspecified altitude units field results in undefined alt
// 
TEST(NMEA_sentence, gga_altitude_no_units)
{
   NMEA_sentence sentence;
   EXPECT_TRUE(sentence.process_GGA("$GPGGA,201730,3229.3086,N,09339.2410,"
      "W,1,08,1.1,81.5,,-26.2,M,,*48") == TRUE);

   EXPECT_EQ(sentence.get_altitude(), UNDEFINED_ALTITUDE);
}

// Three or fewer satellites should result in an undefined altitude
// 
TEST(NMEA_sentence, gga_altitude_three_or_fewer_satellites)
{
   NMEA_sentence sentence;
   EXPECT_TRUE(sentence.process_GGA("$GPGGA,201730,3229.3086,N,09339.2410,"
      "W,1,03,1.1,81.5,M,-26.2,M,,*48") == TRUE);

   EXPECT_EQ(sentence.get_altitude(), UNDEFINED_ALTITUDE);
}

// Specifying an altitude in feet, 'F' in 10th field, should be accepted
// 
TEST(NMEA_sentence, gga_altitude_feet)
{
   NMEA_sentence sentence;
   EXPECT_TRUE(sentence.process_GGA("$GPGGA,201730,3229.3086,N,09339.2410,"
      "W,1,08,1.1,81.5,F,-26.2,M,,*48") == TRUE);

   EXPECT_NEAR(METERS_TO_FEET(sentence.get_altitude()), 81.5, 1e-6);
}

