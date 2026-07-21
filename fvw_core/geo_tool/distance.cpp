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



#include "stdafx.h"

#include "geo_tool.h"

/* ------------------------------------------------------------- */

typedef struct {
   double Latitude; 
   double Longitude;
} Geo_t;

/* ------------------------------------------------------------- */

static
int GreatCircleDistance(Geo_t Point1, Geo_t Point2, double *Distance,
   double *Angle);

/* ------------------------------------------------------------- */

int GEO_distance(degrees_t pt1_lat, degrees_t pt1_lon, 
   degrees_t pt2_lat, degrees_t pt2_lon, double* distance_in_kilometers,
   degrees_t* angle_between_points)
{
   Geo_t pt1, pt2;
   radians_t angle;

   pt1.Latitude = DEG_TO_RAD(pt1_lat);
   pt1.Longitude = DEG_TO_RAD(pt1_lon);
   pt2.Latitude = DEG_TO_RAD(pt2_lat);
   pt2.Longitude = DEG_TO_RAD(pt2_lon);

   if (GreatCircleDistance(pt1, pt2, distance_in_kilometers, &angle) 
      != SUCCESS)
   {
      ERR_report("GreatCircleDistance failed");
      return FAILURE;
   }

   *angle_between_points = RAD_TO_DEG(angle);

   return SUCCESS;
}

/*----------------------------------------------------------------
-  FILE NAME    : gcdist.c
-  CSC NAME     : Distance Calculation
-  PROGRAMMER   : Jim Rhodes
-  DATE WRITTEN : Feburary 1992
-
-  FUNCTION NAME- GreatCircleDistance
-
-  PURPOSE-
-     This procedure will calculate the Great Circle distance 
-  and azimuth between two Geo_t points in nautical miles and radians.
-
-  PARAMETERS-
-     const char  *Point1        IN, lat/long
-     const char  *Point2        IN, lat/long
-     double      *Distance      OUT, kilometers
-     double      *Angle         OUT, radians
-
-  RETURN VALUES-
-
-  PRECONDITIONS-
-  none
-
-  EXTERNALS MODIFIED-
-  none
-
-  PROCESSING DESCRIPTION-
-     This procedure calculates the Great Circle distance 
-  and angle between the two points specified by the parameters 
-  Point1 and Point2.  If Point1 and Point2 have different spheroids, 
-  then Point2 will be converted to Point1's spheroid before the 
-  distance is calculated.  
- 
-  MODIFICATION HISTORY-
-       Revision $Author: vinny $
-
- ---------------------------------------------------------------*/ 

#define EPSILON 0.000000001
#define EARTH_RADIUS 6378.0  

/*
 *  Note: Point1 and Point2 must be in radians
 */
static
int GreatCircleDistance(Geo_t Point1, Geo_t Point2, double *Distance,
   double *Angle )
{
   double      Deltalong;
   double      Arc1;
   double      Arc2;
   double      Avglat;
   double      XX;
   double      Rn;
   Geo_t TmpGeo;

   TmpGeo.Latitude   = Point2.Latitude;
   TmpGeo.Longitude  = Point2.Longitude;

   Avglat      = (Point1.Latitude + TmpGeo.Latitude)/2.0;
   Arc1        = HALF_PI - Point1.Latitude;
   Arc2        = HALF_PI - TmpGeo.Latitude;

   Rn = EARTH_RADIUS;  /* use nominal value */
  
   Deltalong = TmpGeo.Longitude - Point1.Longitude;
   if (Deltalong > PI)
      Deltalong -= TWO_PI;
   else if (Deltalong < -PI)
      Deltalong += TWO_PI;

   /* Find distance in radians. */
   XX = cos(Deltalong)*sin(Arc1)*sin(Arc2) + cos(Arc1)*cos(Arc2);
   if (XX > 1.0)
      XX = 1.0;
   else if (XX < -1.0)
      XX = -1.0;
   *Distance = acos(XX);

   /* Find the angle in radians. */
   if ( fabs(Arc1) < EPSILON )
      *Angle = PI;
   else
   {
      if ((fabs(Arc1 - PI) < EPSILON ) || (fabs(*Distance) < EPSILON))
         *Angle = 0.0;
      else
      {
         XX = (cos(Arc2)-cos(Arc1)*cos(*Distance))/(sin(Arc1)*sin(*Distance));
         if ( XX > 1.0 )
            XX = 1.0;
         else if ( XX < -1.0 )
            XX = -1.0;
         *Angle = acos(XX);
      }
   }

   *Angle = (Deltalong >= 0.0) ? *Angle : TWO_PI - (*Angle);

   /*  Convert distance from radians to kilometers. */
   *Distance = *Distance * Rn;

   return SUCCESS;
}

// These functions return the difference between the latitudes or
// longitudes given.
// NOTES:  1. the functions ALWAYS return a positive value
//         2. all values passed in are assumed to be correct; there is
//            no error-checking done on them
//         3. the functions always calculate the shortest difference
//            i.e. GEO_delta_lon would return 40 degrees instead of 320
degrees_t GEO_delta_lat(degrees_t lat1, degrees_t lat2)
{
   degrees_t delta_lat = lat1 - lat2;

   if (delta_lat < 0.0)
      delta_lat = -delta_lat;

   return delta_lat;
}

degrees_t GEO_delta_lon(degrees_t lon1, degrees_t lon2)
{
   degrees_t delta_lon = lon1 - lon2;

   if (delta_lon < -180.0)
      delta_lon = 360.0 + delta_lon;
   else if (delta_lon > 180.0)
      delta_lon = 360.0 - delta_lon;
   else if (delta_lon < 0.0)
      delta_lon = -delta_lon;

   return delta_lon;
}
