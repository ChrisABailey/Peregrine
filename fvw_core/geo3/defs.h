// Copyright (c) 1994-2009 Georgia Tech Research Corporation, Atlanta, GA
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

// defs.h
//

#define MAX_LAT_DEG 90.0 
#define MAX_LON_DEG 180.0 
#define MIN_LAT_DEG -90.0 
#define MIN_LON_DEG -180.0 

#define SECONDS_ERROR -1
#define MINUTES_ERROR -2
#define DEGREES_ERROR -3
#define OUT_OF_RANGE -4
#define PARSE_ERROR -5
#define DIR_ERROR -6
#define MAGNITUDE_ERROR -7
#define STRING_TOO_LONG -8

#define LAT_SECONDS_ERROR -9
#define LON_SECONDS_ERROR -10
#define LAT_MINUTES_ERROR -11
#define LON_MINUTES_ERROR -12
#define LAT_DEGREES_ERROR -13
#define LON_DEGREES_ERROR -14
#define LAT_OUT_OF_RANGE -15
#define LON_OUT_OF_RANGE -16
#define LAT_PARSE_ERROR -17
#define LON_PARSE_ERROR -18
#define LAT_DIR_ERROR -19
#define LON_DIR_ERROR -20
#define LAT_MAGNITUDE_ERROR -21
#define LON_MAGNITUDE_ERROR -22
#define LAT_STRING_TOO_LONG -23
#define LON_STRING_TOO_LONG -24

#define MIN_TO_DEG(minutes)       (((double)(minutes)) * 1.6666666666666667e-2)
#define SEC_TO_DEG(seconds)       (((double)(seconds)) * 2.7777777777777778e-4)
#define DEG_TO_RAD(degrees)       (((double)(degrees)) * 1.7453292519943295e-2)
#define RAD_TO_DEG(radians)       (((double)(radians)) * 57.295779513082322)

typedef FLOAT8 minutes_t;