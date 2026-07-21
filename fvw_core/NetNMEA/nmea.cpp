// Copyright (c) 1994-2011 Georgia Tech Research Corporation, Atlanta, GA
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

// nmea.cpp

// This file contains the implementation of the NMEA_sentence class.  The
// purpose of this class is to extract latitude, longitude, time, speed,
// heading, altitude, and number of GPS satellites from NMEA sentences.
// Only GGA, GLL, RMC, and VTG are handled by this class.  See nmea.h
// for more information on what data is in which sentence.

#include "stdafx.h"
#include <string.h>
#include <stdio.h>
#include "nmea.h"
#include "gps.h"
#include "../geo_tool/geo_tool_d.h"

#define ERR_report TRACE

// step length in meters is the distance traveled between two GPS points that
// are 1 second apart assuming a speed of 300 knots
#define STEP_LENGTH_IN_METERS 154.333333333333L

NMEA_sentence::NMEA_sentence()
{
   // initialize parsing buffer
   m_buffer = NULL;

   // initialize data fields
   m_latitude = -1000.0f;
   m_longitude = -1000.0f;
   m_speed_knots = -1.0f;
   m_speed_km_hr = -1.0f;
   m_true_heading = -1.0f;
   m_magnetic_heading = -1.0f;
   m_satellites = 0;
   m_msl = -9999.0f;
	m_horizontal_dilution_of_precision = -9999;
	m_geoid_seperation_meters = -9999;

#if OLD_GPS_TIME_CODE
   set_time(-1.0f);
   set_date(255,255,255);
#else
//   m_dateTime.m_dt=0;
   m_dateTime.SetStatus(COleDateTime::null);
#endif
}

// Contructor - used for building NMEA sentences
NMEA_sentence::NMEA_sentence(GPSPointIcon &point)
{
   // initialize parsing buffer
   m_buffer = NULL;

   // initialize data fields
   m_latitude = point.m_latitude;
   m_longitude = point.m_longitude;
   m_speed_knots = point.m_speed_knots;
   m_speed_km_hr = point.m_speed_km_hr;
   m_true_heading = point.m_true_heading;
   m_magnetic_heading = point.m_magnetic_heading;
   m_satellites = point.m_satellites;
   m_msl = point.m_msl;

#if OLD_GPS_TIME_CODE
   set_time(point.get_time());
   set_date(point.get_year(),point.get_month(),point.get_day());
#else
   m_dateTime = point.m_dateTime;
#endif
}

// Parses all available data from the RMC sentence to sentence into this object.
// Returns TRUE if this sentence contained a valid geographic location  (latitude and longitude). 
//
// RMC sentences contain:
//    time              FIELD 0
//    loc flag          FIELD 1
//    latitude          FIELD 2,3
//    longitude         FIELD 4,5
//    speed in knots,   FIELD 6
//    true heading      FIELD 7
//    date              FIELD 8
//    magnetic heading. FIELD 9
// RMC sentences DON'T include: 
//    altitude
//    number of satellites
//    speed in km/hr

boolean_t NMEA_sentence::process_RMC(const char *RMC_sentence)
{
   int i = 0;
   const int BUF_LEN = 81;
   char buffer[BUF_LEN];
   char *fields[11];
   char *field;
   int day, month, year;
   
   // make sure this is a RMC sentence
   if (strncmp(RMC_sentence, "$GPRMC", 6) != 0)
      return FALSE;

   // copy the sentence into a buffer for parsing
   strncpy_s(buffer, BUF_LEN, RMC_sentence, 80);

   i = 0;
   field = field_parse(&buffer[7]);
   while (field && i < 11)
   {
      fields[i] = field;
      i++;
      field = field_parse(NULL);
   }

   // the line is incomplete
   if (i < 11)
      return FALSE;

   // the location invalid flag is set
   if (*fields[1] == 'V')
   {
      m_latitude = -1000.0f;
      m_longitude = -1000.0f;
   }
   else
   {
      // convert strings to latitude
      if (*fields[2] != '\0' && *fields[3] != '\0')
      {
         m_latitude = make_degrees(fields[2], *fields[3]);
         if (m_latitude < -90.0 || m_latitude > 90.0)
         {
            m_latitude = -1000.0f;
         }
      }

      // convert strings to longitude
      if (*fields[4] != '\0' || *fields[5] != '\0')
      {
         m_longitude = make_degrees(fields[4], *fields[5]);
         if (m_longitude < -180.0 || m_longitude > 180.0)
         {
            m_longitude = -1000.0f;
         }
      }
   }

   // set speed if available
   if (*fields[6] != '\0')
	   sscanf_s(fields[6], "%f", &m_speed_knots);

   // set true heading if available
   if (*fields[7] != '\0')
   {
	   sscanf_s(fields[7], "%f", &m_true_heading);
	
      // compute the magnetic heading from the true heading and the
      // magnetic variation - m_magnetic_heading must be between 0.0 - 360.0
      if (*fields[9] != '\0' && *fields[10] != '\0')
      {
         float magnetic_variation;

	      sscanf_s(fields[9], "%f", &magnetic_variation); 
	      if (*fields[10] == 'E' || *fields[10] == 'e')
         {
		      m_magnetic_heading = m_true_heading - magnetic_variation;
            if (m_magnetic_heading < 0.0f)
               m_magnetic_heading += 360.0f;
         }
	      else
         {
		      m_magnetic_heading = m_true_heading + magnetic_variation;
            if (m_magnetic_heading > 360.0f)
               m_magnetic_heading -= 360.0f;
         }
      }
   }

#if OLD_GPS_TIME_CODE
   // convert UTC string to time in seconds
   if (*fields[0] != '\0')
   {
      float secondsToday = UTC_to_time_in_seconds(fields[0]);
      set_time(secondsToday);
   }
   // set date if available
   if (*fields[8] != '\0')
   {
      if (sscanf(fields[8], "%02d%02d%02d", &day, &month, &year) == 3)
         set_date(year,month,day);
   }
#else
   // convert UTC string to time in seconds
   if (*fields[0] != '\0')
   {
      float secondsToday = UTC_to_time_in_seconds(fields[0]);
      set_time(secondsToday);
   }
   // set date if available
   if (*fields[8] != '\0')
   {
      if (sscanf_s(fields[8], "%02d%02d%02d", &day, &month, &year) == 3)
         set_date(year,month,day);
   }
#endif


   return TRUE;
}


// Parses all available data from the GGA sentence to sentence into this object.
// Returns TRUE if this sentence contained a valid geographic location (latitude and longitude).
//
// GGA sentences contain:
//    time        FIELD 0
//    latitude    FIELD 1,2
//    longitude   FIELD 3,4
//    loc flag    FIELD 5
//    satellites  FIELD 6
//    altitude    FIELD 8
//    msl flag    FIELD 9

// GGA sentences DON'T include
//    speed
//    heading

boolean_t NMEA_sentence::process_GGA(const char *GGA_sentence)
{
   int i = 0;
   const int BUF_LEN = 81;
   char buffer[BUF_LEN];
   char *fields[14];
   char *field;
   
   // make sure this is a GGA sentence
   if (strncmp(GGA_sentence, "$GPGGA", 6) != 0)
      return FALSE;

   // copy the sentence into a buffer for parsing
   strncpy_s(buffer, BUF_LEN, GGA_sentence, 80);

   i = 0;
   field = field_parse(&buffer[7]);
   while (field && i < 14)
   {
      fields[i] = field;
      i++;
      field = field_parse(NULL);
   }

   // the line is incomplete
   if (i < 14)
      return FALSE;

   // the location invalid flag is set
   if (*fields[5] == '0')
      return FALSE;

   // latitude or longitude is missing
   if (*fields[1] == '\0' || *fields[2] == '\0' ||
      *fields[3] == '\0' || *fields[4] == '\0')
      return FALSE;

   // convert strings to latitude
   m_latitude = make_degrees(fields[1], *fields[2]);
   if (m_latitude < -90.0 || m_latitude > 90.0)
   {
      m_latitude = -1000.0f;
      return FALSE;
   }

   // convert strings to longitude
   m_longitude = make_degrees(fields[3], *fields[4]);
   if (m_longitude < -180.0 || m_longitude > 180.0)
   {
      m_longitude = -1000.0f;
      return FALSE;
   }

#if OLD_GPS_TIME_CODE
   // convert UTC string to time in seconds
   if (*fields[0] != '\0')
   {
      float secondsToday = UTC_to_time_in_seconds(fields[0]);
      set_time(secondsToday);
   }
#else
   // convert UTC string to time in seconds
   if (*fields[0] != '\0')
   {
      float secondsToday = UTC_to_time_in_seconds(fields[0]);
      set_time(secondsToday);
   }
#endif

   // set number of satellites if available
   if (*fields[6] != '\0')
   {
      int satellites = 0;

      sscanf_s(fields[6], "%d", &satellites);
      m_satellites = (unsigned char)satellites;
   }

	// set the horizontal dilution of precision if available
	if (*fields[7] != '\0')
		sscanf_s(fields[7], "%f", &m_horizontal_dilution_of_precision);
	else
		m_horizontal_dilution_of_precision = -9999;

   // set altitude if available (but only if there are four or more satellites
   if (m_satellites > 3 && *fields[8] != '\0' && 
      (*fields[9] == 'M' || *fields[9] == 'F'))
   {
      sscanf_s(fields[8], "%f", &m_msl);
      if (*fields[9] == 'F')
         m_msl = static_cast<float>(FEET_TO_METERS(m_msl));
   }

	// set the geoid separation (meters)
	if (*fields[10] != '\0' && *fields[11] == 'M')
		sscanf_s(fields[10], "%f", &m_geoid_seperation_meters);
	else
		m_geoid_seperation_meters = -9999;

   return TRUE;
}


// Parses all available data from the GLL sentence to sentence into this object.
// Returns TRUE if this sentence contained a valid geographic location  (latitude and longitude).
//
// GLL sentences contain:
//    latitude    FIELD 0,1
//    longitude   FIELD 2,3
//    time        FIELD 4
// GLL sentences DON'T include:
//    altitude
//    number of satellites
//    speed
//    heading 

boolean_t NMEA_sentence::process_GLL(const char *GLL_sentence)
{
   int i = 0;
   const int BUF_LEN = 81;
   char buffer[BUF_LEN];
   char *fields[6];
   char *field;
   
   // make sure this is a GLL sentence
   if (strncmp(GLL_sentence, "$GPGLL", 6) != 0)
      return FALSE;

   // copy the sentence into a buffer for parsing
   strncpy_s(buffer, BUF_LEN, GLL_sentence, 80);

   i = 0;
   field = field_parse(&buffer[7]);
   while (field && i < 6)
   {
      fields[i] = field;
      i++;
      field = field_parse(NULL);
   }

   // the line is incomplete
   if (i < 6)
      return FALSE;

   // the location invalid flag is set
   if (*fields[5] == 'V')
      return FALSE;

   // latitude or longitude is missing
   if (*fields[0] == '\0' || *fields[1] == '\0' ||
      *fields[2] == '\0' || *fields[3] == '\0')
      return FALSE;

   // convert strings to latitude
   m_latitude = make_degrees(fields[0], *fields[1]);
   if (m_latitude < -90.0 || m_latitude > 90.0)
   {
      m_latitude = -1000.0f;
      return FALSE;
   }

   // convert strings to longitude
   m_longitude = make_degrees(fields[2], *fields[3]);
   if (m_longitude < -180.0 || m_longitude > 180.0)
   {
      m_longitude = -1000.0f;
      return FALSE;
   }

#if OLD_GPS_TIME_CODE
   // convert UTC string to time in seconds
   if (*fields[4] != '\0')
   {
      float secondsToday = UTC_to_time_in_seconds(fields[4]);
      set_time(secondsToday);
   }
#else
   // convert UTC string to time in seconds
   if (*fields[4] != '\0')
   {
      float secondsToday = UTC_to_time_in_seconds(fields[4]);
      set_time(secondsToday);
   }
#endif

   return TRUE;
}



// Parses all available data from the VTG sentence to sentence into this object.
// Returns FALSE if this sentence contains no valid speed or heading information.
// Returns TRUE otherwise.
//
// VTG sentences include:
//    true heading      FIELD 0
//    true flag         FIELD 1
//    magnetic heading  FIELD 2
//    mag flag          FIELD 3
//    speed in knots    FIELD 4
//    speed flag        FIELD 5
//    speed in km/hr    FIELD 6
//    speed flag        FIELD 7
// If there is no speed
//    there can be no heading.

boolean_t NMEA_sentence::process_VTG(const char *VTG_sentence)
{
   int i = 0;
   const int BUF_LEN = 81;
   char buffer[BUF_LEN];
   char *fields[8];
   char *field;
   boolean_t valid_data = FALSE;
   
   // make sure this is a VTG sentence
   if (strncmp(VTG_sentence, "$GPVTG", 6) != 0)
      return FALSE;

   // copy the sentence into a buffer for parsing
   strncpy_s(buffer, BUF_LEN, VTG_sentence, 80);

   i = 0;
   field = field_parse(&buffer[7]);
   while (field && i < 8)
   {
      fields[i] = field;
      i++;
      field = field_parse(NULL);
   }

   // the line is incomplete
   if (i < 8)
      return FALSE;

   // get true heading if available
   if (*fields[0] != '\0' && *fields[1] == 'T')
   {
      sscanf_s(fields[0], "%f", &m_true_heading);
      valid_data = TRUE;
   }

   // get magnetic heading if available
   if (*fields[2] != '\0' && *fields[3] == 'M')
   {
      sscanf_s(fields[2], "%f", &m_magnetic_heading);
      valid_data = TRUE;
   }

   // get speed in knots if available
   if (*fields[4] != '\0' && *fields[5] == 'N')
   {
      sscanf_s(fields[4], "%f", &m_speed_knots);
      valid_data = TRUE;
   }

   // get speed in kilometers/hour if available
   if (*fields[6] != '\0' && *fields[7] == 'K')
   {
      sscanf_s(fields[6], "%f", & m_speed_km_hr);
      valid_data = TRUE;
   }

   return valid_data;
}

boolean_t NMEA_sentence::process_ALM(const char *ALM_sentence)
{
   int i = 0;
   const int BUF_LEN = 81;
   char buffer[BUF_LEN];
   char *fields[15];
   char *field;
   boolean_t valid_data = FALSE;
   
   // make sure this is a VTG sentence
   if (strncmp(ALM_sentence, "$GPALM", 6) != 0)
      return FALSE;

   // copy the sentence into a buffer for parsing
   strncpy_s(buffer, BUF_LEN, ALM_sentence, 80);

   i = 0;
   field = field_parse(&buffer[7]);
   while (field && i < 15)
   {
      fields[i] = field;
      i++;
      field = field_parse(NULL);
   }

   // the line is incomplete
   if (i < 15)
      return FALSE;

   int gpsWeekdOffSet;
   // get GPS Week Number
   if (*fields[3] != '\0')
   {
      sscanf_s(fields[0], "%f", &gpsWeekdOffSet);
      valid_data = TRUE;
   }
   int timeOffset;
   // Time Offset
   if (*fields[6] != '\0')
   {
      sscanf_s(fields[6], "%f", & timeOffset);
      valid_data = TRUE;
   }

   COleDateTime time(1980,6,1,0,0,0);
   time += ((7*24*60*60)/*1 Week*/ * gpsWeekdOffSet) + timeOffset;


   return valid_data;
}

char NMEA_sentence::check_sum(const char *buffer, int length)
{
   int i;
   char sum = 0;

   for (i=0; i<length; i++)
      sum ^= buffer[i];

   return sum;
}

boolean_t NMEA_sentence::NMEA_test(const char *line)
{
   int length;
   char *check_sum_loc;
   short int check_sum_sent;
   char check_sum_calc;

   // all NMEA sentences start with $
   if (line[0] != '$')
   {
      //TRACE("Line doesn't start with $.\n");
      return FALSE;
   }

   // NMEA sentence length is 82 characters including the delimiting $ and the
   // terminating <CR><LF>.
   length = strlen(line);
   if (length > MAX_NMEA_SENTENCE_LENGTH)
   {
      //TRACE("Line too long:\n%s",line);
      return FALSE;
   }

   // test the checksum only if it is present
   check_sum_loc = const_cast<char *>(strchr(line, '*'));
   if (check_sum_loc != NULL)
   {
      // get checksum sent in sentence
      sscanf_s(check_sum_loc+1,"%hX",&check_sum_sent);

      // calculate the checksum from the data
      *check_sum_loc = '\0';
      check_sum_calc = check_sum(line+1, strlen(line)-1);
      *check_sum_loc = '*';
   
      if ((short int)check_sum_calc != check_sum_sent)
      {
         //TRACE("check_sum: received=%hX calculated=%hX\n",
            //check_sum_sent, (short)check_sum_calc);
         //TRACE("Line: %s\n", line);
         return FALSE;
      }      
   }

   return TRUE;
}

char* NMEA_sentence::field_parse(char *buffer)
{
   char *comma;
   char *star;
   char *field;

   // buffer != NULL sets m_buffer to start of buffer to be parsed
   if (buffer != NULL)
      m_buffer = buffer;

   // m_buffer == NULL means it hasn't been initialized, or parsing is done
   // *m_buffer == '\0' means the end of the line has been reached
   if (m_buffer == NULL || *m_buffer == '\0')
      return NULL;

   // look for next comma delimiter
   comma = strchr(m_buffer, ',');
   if (comma)
   {
      *comma = '\0';
      field = m_buffer;
      m_buffer = comma + 1;
      return field;
   }

   // Look for a '*' at the end of the last data field.  It preceeds the
   // checksum, when one is present.
   star = strchr(m_buffer, '*');
   if (star)
   {
      *star = '\0';
      field = m_buffer;
      m_buffer = NULL;
      return field;
   }

   // To handle lines that do not contain a checksum, it must be assumed that
   // what remains in m_buffer is the last field from the original string of
   // comma delimited fields.
   field = m_buffer;
   m_buffer = NULL;
   return field;
}


float NMEA_sentence::make_degrees(const char *geo_s, char dir_char)
{
   const int BUF_LEN = 4;
   char degrees_s[BUF_LEN];
   int degrees;
   float minutes;
   int sign;
   int degrees_digits;

   // Number of digits of degrees is 2 for latitude and 3 for longitude.
   // The dir_char is used to determine latitude or longitude as well as
   // the sign.
   if (dir_char == 'N' || dir_char == 'n')
   {
      degrees_digits = 2;
      sign = 1;
   }
   else if (dir_char == 'S' || dir_char == 's')
   {
      degrees_digits = 2;
      sign = -1;
   }
   else if (dir_char == 'E' || dir_char == 'e')
   {
      degrees_digits = 3;
      sign = 1;
   }
   else if (dir_char == 'W' || dir_char == 'w')
   {
      degrees_digits = 3;
      sign = -1;
   }
   else
      return -1000.0f;
     
   // get degrees  from geo string
   strncpy_s(degrees_s, BUF_LEN, geo_s, degrees_digits);
   degrees_s[degrees_digits] = '\0';
   degrees = atoi(degrees_s);

   // get the minutes from the string
   sscanf_s(geo_s + degrees_digits, "%f", &minutes);
      
   // return decimal degrees
   return (sign * (degrees + minutes/60.0f));
}


float NMEA_sentence::UTC_to_time_in_seconds(const char *UTC)
{
   int hour = 0;
   int minute = 0;
   float second = 0.0f;
   float time_point;

   if (sscanf_s(UTC, "%2d%2d%f", &hour, &minute, &second) == 3)
   {
      time_point = float(hour*3600)+float(minute*60)+second;
      return time_point;
   }

   return -1.0f;
}


void NMEA_sentence::time_in_seconds_to_UTC(float time, char *UTC, int UTC_len)
{
   int hour, minute;
   float second;

   if (time == -1.0) //unknown time
      *UTC = '\0';

   hour = int(time / float(3600.0));
   minute = int((time - float(hour * 3600)) / float(60.0));
   second = time - float(hour * 3600 + minute * 60);
   if (second < 10)
      sprintf_s(UTC, UTC_len, "%02d%02d%c%02.2f", hour, minute, '0',second);
   else
      sprintf_s(UTC, UTC_len, "%02d%02d%02.2f", hour, minute, second);
}
