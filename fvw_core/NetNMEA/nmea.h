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



#ifndef NMEA_H
#define NMEA_H 1

#include "NetNmeaDll.h"
#include "gps.h"

#define MAX_NMEA_SENTENCE_LENGTH 82
#define GPS_ATOMIC_CLOCK_BASE_TIME (COleDateTime(1980,1,6,0,0,0))

class NETNMEA_DLL NMEA_sentence
{
// data values extracted from NMEA sentences
private:
   float m_latitude;
   float m_longitude;      
   float m_speed_knots;
   float m_speed_km_hr;
   float m_true_heading;
   float m_magnetic_heading;
   float m_msl;               // mean sea level altitude, meters
   unsigned char m_satellites;

   float m_horizontal_dilution_of_precision;
   float m_geoid_seperation_meters;
   
#if OLD_GPS_TIME_DATA

   float         m_secondsToday; //seconds since the start of the day
   float         m_second;    // 00.00-59.99
   unsigned char m_minute;    // 0-59
   unsigned char m_hour;      // 0-23
   unsigned char m_day;       // 1-31
   unsigned char m_month;     // 1-12
   unsigned char m_year;      // relative to 0-99
public:
   float get_second() { return m_second; };
   unsigned char get_minute() { return m_minute; };
   unsigned char get_hour() { return m_hour; };
   unsigned char get_day() { return m_day; };      //1-31
   unsigned char get_month() { return m_month; };  //1-12
   unsigned char get_year() { return m_year; };    //0-254 relative to 1900
   // All return 255 for unknown.

   boolean_t valid_date() {return (m_year!=255 && m_month!=255 && m_day!=255);};
   boolean_t valid_time() {return (0 <= m_secondsToday && m_secondsToday <= (24*3600));};
   boolean_t invalid_time() {return (m_secondsToday == -1.0f);};
   boolean_t null_time() {return (m_secondsToday == -1.0f);};

   float get_time(){return m_secondsToday;};
   void set_time(float s)
   {
      m_secondsToday = s;
      m_hour = int(s / float(3600.0));
      m_minute = int((s - float(m_hour * 3600)) / float(60.0));
      m_second = s - float(m_hour * 3600 + m_minute * 60);
   };

   void get_date(int& year, int& month, int& day){year=m_year;month=m_month;day=m_day;};
   void set_date(int year, int month, int day){m_year=year;m_month=month;m_day=day;};

   COleDateTime get_date_time()
   {
      COleDateTime dt(get_year(),get_month(),get_day(),get_hour(),get_minute(),(int)get_second());
      return dt;
   };

#else
public:
   COleDateTime m_dateTime;   //GPS Playback stuff
   COleDateTime m_almanac_dateTime;
   float get_second() { return (float)m_dateTime.GetSecond(); };
   unsigned char get_minute() { return m_dateTime.GetMinute(); };
   unsigned char get_hour() { return m_dateTime.GetHour(); };
   unsigned char get_day() { return m_dateTime.GetDay(); };      //1-31
   unsigned char get_month() { return m_dateTime.GetMonth(); };  //1-12
   unsigned char get_year() { return m_dateTime.GetYear()%100; };    //100-99
   // All return 255 for unknown.

   boolean_t valid_date() {return (m_dateTime.GetStatus()==COleDateTime::valid && m_dateTime >= GPS_VALID_BASE_DATE );};
   boolean_t valid_time() {return (m_dateTime.GetStatus()==COleDateTime::valid);};
   boolean_t invalid_time() {return (m_dateTime.GetStatus()==COleDateTime::invalid);};
   boolean_t null_time() {return (m_dateTime.GetStatus()==COleDateTime::null);};

   float get_time() {return m_dateTime.GetHour()*3600.0f + m_dateTime.GetMinute()*60.0f + m_dateTime.GetSecond();};

   void set_time(float seconds)
   { 
      int s=(int)seconds;
      if (valid_date())
         m_dateTime.SetDateTime( m_dateTime.GetYear(), m_dateTime.GetMonth(), m_dateTime.GetDay(), s/3600, (s/60)%60,  s%60 );
      else
         m_dateTime.SetTime( s/3600, (s/60)%60, s%60 );
   };

   void get_date(int& year, int& month, int& day)
   {
      year=m_dateTime.GetYear();
      month=m_dateTime.GetMonth();
      day=m_dateTime.GetDay();
   };

   void set_date(int year, int month, int day)
   {
      year = GPS_get_y2k_compliant_year(year);
      if (valid_time())
         m_dateTime.SetDateTime(year,month,day,m_dateTime.GetHour(),m_dateTime.GetMinute(),m_dateTime.GetSecond());
      else
         m_dateTime.SetDate(year,month,day);
   };

   COleDateTime get_date_time() { return m_dateTime; };
   COleDateTime get_almanac_date_time() { return m_almanac_dateTime; };

#endif

// parsing buffer
private:
   char *m_buffer;

public:
   // Constructor - used for parsing NMEA sentences
   NMEA_sentence();

   // Contructor - used for building NMEA sentences
   NMEA_sentence(GPSPointIcon &point);

   // Destructor
   ~NMEA_sentence() {};

   // returns check sum of length characters in buffer
   static char check_sum(const char *buffer, int length);

   // performs some basic tests for NMEA compliance, returns FALSE if it fails
   // any test, TRUE otherwise
   static boolean_t NMEA_test(const char *line);

public:
   // Parses all available data from the RMC sentence to sentence into this
   // object.  Returns TRUE if this sentence contained a valid geographic
   // location  (latitude and longitude). 
   //
   // Note: RMC sentences contain latitude, longitude, time, speed in knots,
   // true heading, and magnetic heading.  RMC sentences DON'T include 
   // altitude, number of satellites, or speed in kilometers per hour.

   boolean_t process_RMC(const char *RMC_sentence);


   // Parses all available data from the GGA sentence to sentence into this
   // object.  Returns TRUE if this sentence contained a valid geographic
   // location (latitude and longitude).
   //
   // Note: GGA sentences contain latitude, longitude, time, altitude, and
   // number of satellites.  GGA sentences DON'T include speed or heading
   // information.

   boolean_t process_GGA(const char *GGA_sentence);

   // Parses all available data from the GLL sentence to sentence into this
   // object.  Returns TRUE if this sentence contained a valid geographic
   // location  (latitude and longitude).
   //
   // Note: GLL sentences contain latitude, longitude, and time.  GLL sentences
   // DON'T include altitude, number of satellites, speed, or heading 
   // information.

   boolean_t process_GLL(const char *GLL_sentence);

   // Parses all available data from the VTG sentence to sentence into this
   // object.  Returns FALSE if this sentence contains no valid speed or
   // heading information, returns TRUE otherwise.
   //
   // Note: VTG sentences ONLY include speed in knonts, speed in km/hr, true
   // heading, and magnetic heading.  If there is no speed, there can be no
   // heading.

   boolean_t process_VTG(const char *VTG_sentence);

   // Parses the almanac data from the ALM sentence to sentence into this
   // object.  Returns FALSE if this sentence contains no valid almanac
   // information, returns TRUE otherwise.
   // 

   boolean_t process_ALM(const char *VTG_sentence);

public:
   // get the latitude, returns -1000.0 for unknown
   float get_latitude() { return m_latitude; }

   // get the longitude, returns -1000.0 for unknown
   float get_longitude() { return m_longitude; }

   // get speed in nautical miles/hour, -1.0 for unknown
   float get_speed_in_knots() { return m_speed_knots; }

   // get speed in km/hr, -1.0 for unknown
   float get_speed_in_km_hr() { return m_speed_km_hr; }

   // get true heading in degrees, E/W = +/-, -1000.0 for unknown
   float get_true_heading() { return m_true_heading; }

   // get magnetic headings in degrees, E/W = +/-, -1000.0 for unknown
   float get_mag_heading() { return m_magnetic_heading; }

   // get MSL antenna altitude in meters, returns -9999.0 for unknown
   float get_altitude() { return m_msl; }

   // get number of satellites, 0 for unknown
   unsigned char get_num_satellites() { return m_satellites; }

private:
   char* field_parse(char *buffer);

   float make_degrees(const char *geo_s, char dir_char);

   float UTC_to_time_in_seconds(const char *UTC);

   void time_in_seconds_to_UTC(float time, char *UTC, int UTC_len);
};

#endif