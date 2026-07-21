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


#ifndef _GPS_H_
#define _GPS_H_

#include "stdafx.h"
#define GPS_TIME		    0x0001
#define GPS_LOCATION		0x0002
#define GPS_SPEED_KTS		0x0004
#define GPS_TRUE_HEADING	0x0008
#define GPS_DATE			0x0010
#define GPS_MAG_HEADING		0x0020
#define GPS_NUM_SAT			0x0040
#define GPS_ALTITUDE		0x0080
// #define GPS_MSL_FLAG			0x0100

#define GPS_RMC GPS_TIME|GPS_LOCATION|GPS_SPEED_KTS|GPS_TRUE_HEADING|GPS_DATE|GPS_MAG_HEADING
#define GPS_GGA GPS_TIME|GPS_LOCATION|GPS_NUM_SAT|GPS_ALTITUDE // |GPS_MSL_FLAG
#define GPS_GLL GPS_TIME|GPS_LOCATION
#define GPS_VTG GPS_TRUE_HEADING|GPS_MAG_HEADING|GPS_SPEED_KTS
#define GPS_MINIMUM_REQUIRED GPS_TIME|GPS_LOCATION|GPS_SPEED_KTS|GPS_TRUE_HEADING|GPS_DATE

struct GPS_FIELDS {
	bool	bTime : 1;
	bool	bLocation : 1;
	bool	bSpeedKts : 1;
	bool	bTrueHeading : 1;
	bool	bDate : 1;
	bool	bMagHeading : 1;
	bool	bNumSat : 1;
	bool	bAltitude : 1;
	bool	bMslFlag : 1;
};

typedef int boolean_t;

int GPS_get_y2k_compliant_year( int year );

class CProperties;
class C_gps_trail;
class C_gps_auxdata;

struct GPSWaypoint
{
public:
   std::string wayId;
	float latitude;
	float longitude;
	float distanceNM;
	float distanceKM;
	//These were added to support the drawing
	long waypointCircleID;
	long waypointTextID;
	long wayToWayLineID;
	bool wayToWayHasDistance;
};

#ifdef _WIN32  // GPSRoute draws via COM ILayerPtr
class GPSRoute
{
public:
	GPSRoute();
	~GPSRoute();
	void Initialize();
	void Reset();
	char m_RouteId[30];
	char m_RouteType; //'W' for working, 'C' for complete
   std::vector<GPSWaypoint> m_waypoints;
	int numWaypoint;	

	int process_RTE_sentence(const char *sentence);
	int process_WPL_sentence(const char *sentence);
	int process_WNC_sentence(const char *sentence);

	bool IsComplete(){ return m_Complete; };

	//Drawing Options
	void set_drawRoute(bool drawRoute);
	bool get_drawRoute() {return m_drawRoute; };
	void set_drawWaypointDistance(bool drawWaypointDistance);
	bool get_drawWaypointDistance() { return m_drawWaypointDistance; };

	void DrawRoute(ILayerPtr layer = NULL);	
	void DrawWaypoints(ILayerPtr layer = NULL);
	void DrawWaypoint(size_t waypoint_num, ILayerPtr layer = NULL);
	void DrawWaypoint(GPSWaypoint* way1, GPSWaypoint* way2, ILayerPtr layer);
	void ClearRoute(ILayerPtr layer = NULL);
	void ClearWaypoints(ILayerPtr layer = NULL);
	void ClearWaypoint(size_t waypoint_num, ILayerPtr layer = NULL);
	
	bool FirstWaypointMoved;
	C_gps_auxdata* m_auxdata;
private:
	
	
	bool m_Complete;	

   // used for special case of route type 'w', appended to the end of the
   // list of waypoints once the route is complete
	GPSWaypoint m_tail;
	int m_numSentences;
	int m_nextSentence;

	//Drawing related
	bool m_drawRoute;
	bool m_drawWaypointDistance;	

	char* field_parse(char *buffer);
	char* m_buffer;
};
#endif  // _WIN32
	
struct GPSSatellite
{
   std::string SVPRNNumber;
   int Evelation;
   int Azimuth;
   int SNR;
};

class GPSSatellitesInView
{
public:
	GPSSatellitesInView();
	~GPSSatellitesInView();
	void Initialize();
	void Reset();
	int m_numSat;
	std::vector<GPSSatellite> m_Satellites;

	bool IsComplete(){ return m_IsComplete; };
	int process_GSV_sentence(const char *sentence);
private:
	int m_numSentences;
	int m_nextSentence;
	bool m_IsComplete;
	char* field_parse(char *buffer);
	char* m_buffer;
};

//This is for aux gps data that needs to draw to a moving map overlay
//Currently it's limited to displaying Bearing and Distance to 
#ifdef _WIN32  // C_gps_auxdata draws via COM ILayerPtr/IStream
class C_gps_auxdata
{
public:
	C_gps_auxdata();
	~C_gps_auxdata();
	void Initialize();
	void reset();
	
	//IMovingMapFeedAuxiliaryData
	void GetNAuxDataTypes(long* pNumAuxTypes);
	void GetAuxDataType(int nAuxType, BSTR* pbstrDisplayName, long* pEnabled);
	void EnableAuxDataType(int nAuxType, long nEnabled);

	GPSRoute* m_Route;
	int process_sentence(const char *sentence);		
	void get_Route(GPSRoute *route);

	void set_displayDistanceToNextWay(bool display);
	bool get_displayDistanceToNextWay() { return m_displayDistanceToNextWay; };
	void set_displayBearingToNextWay(bool display);
	bool get_displayBearingToNextWay() { return m_displayBearingToNextWay; };

	void set_overlay_handle(int h) { m_overlay_handle = h;};
	int get_overlay_handle() { return m_overlay_handle; };
	int m_overlay_handle;

	void MarshallInterfacePointer();
	ILayerPtr m_ilayer;	
	ILayerPtr m_marshalled_ilayer;

	static float make_degrees(char *geo_s, char *dir_char);
	//static char* GetNMessagePart(int position, const char* buffer);
	char* field_parse(char *buffer);
	char* m_buffer;

private:	
	IStream *m_pMarshallingStream;	

	int m_nAuxTypes;
	bool m_displayDistanceToNextWay;
	bool m_displayBearingToNextWay;

	float m_curlat;
	float m_curlon;
	float m_distanceToNextWay;
	float m_bearingMagToNextWay;
	float m_bearingTrueToNextWay;

	long m_lineToNextWayID;
	bool m_lineHasDistance;
	bool m_lineHasBearing;
	
	void DrawLineToNextWay(ILayerPtr layer);

	void ClearAll(); //Called when closing
	void ClearLineToNextWay(ILayerPtr layer);	

	int process_BWC_sentence(const char *sentence);
	//static float make_degrees(const char *geo_s, char *dir_char);
};
#endif  // _WIN32


class GPSPointIcon
{

public:

   float m_latitude;
   float m_longitude;      
   float m_speed_knots;
   float m_speed_km_hr;
   float m_true_heading;
   float m_magnetic_heading;
   float m_msl;               // mean sea level altitude 
   unsigned char m_satellites;
   COleDateTime m_almanac_dateTime;   

public:
   COleDateTime m_dateTime;   //GPS Playback stuff
   float get_second() { return (float)m_dateTime.GetSecond(); };
   unsigned char get_minute() { return m_dateTime.GetMinute(); };
   unsigned char get_hour() { return m_dateTime.GetHour(); };
   unsigned char get_day() { return m_dateTime.GetDay(); };      //1-31
   unsigned char get_month() { return m_dateTime.GetMonth(); };  //1-12
   unsigned char get_year() { return m_dateTime.GetYear()%100; };    //100-99
   // All return 255 for unknown.
#define GPS_ATOMIC_CLOCK_BASE_TIME (COleDateTime(1980,1,6,0,0,0))
#define GPS_VALID_BASE_DATE (COleDateTime(1971,1,1,0,0,0)) //1971 and beyond is valid
   boolean_t valid_date() {return (m_dateTime.GetStatus()==COleDateTime::valid && m_dateTime >= GPS_VALID_BASE_DATE );};
   boolean_t valid_time() {return (m_dateTime.GetStatus()==COleDateTime::valid);};
   boolean_t invalid_time() {return (m_dateTime.GetStatus()==COleDateTime::invalid);};
   boolean_t null_time() {return (m_dateTime.GetStatus()==COleDateTime::null);};

   float get_time() {return m_dateTime.GetHour()*3600.0f + 
      m_dateTime.GetMinute()*60.0f + m_dateTime.GetSecond();};

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

public:
   // Constructor
   GPSPointIcon();
   void initialize();

   // destructor
   ~GPSPointIcon() {};

   // calculates data not already set, like speed and heading, returns
   // FAILURE if the point is not valid
   int calc();

   // assignment operator
   GPSPointIcon& operator =(GPSPointIcon &point);

   // returns TRUE if the point contains a valid location, FALSE otherwise
   boolean_t is_valid();
};
// end GPSPointIcon

#ifdef _WIN32  // C_gps_trail: COM feed machinery (IGPS2Ptr/IStream/CString)
class C_gps_trail
{
	int m_gll_sentence_count;
	GPSPointIcon m_next_point;       // next point to be added to the trail
	unsigned char m_satellites;
	COleDateTime m_best_date;
	int m_next_point_type;            // type of sentence to start m_next_point

	IGPS2Ptr m_gps;
   IGPS2Ptr m_marshalled_gps;       // used when C_gps_trail is in another thread

	int m_overlay_handle;
	CProperties *m_properties;
	COleDateTime m_last_added_date;
   IStream *m_pMarshallingStream;

public:
	C_gps_trail();
	~C_gps_trail();
		
	GPSSatellitesInView* m_satellitesInView; //Satellites in View detailed information

	void set_overlay_handle(int h) { m_overlay_handle = h; }
	int get_overlay_handle() { return m_overlay_handle; }

	void set_properties(CProperties *p) { m_properties = p; }
	void reset_last_added_date();

	int process_sentence(const char *line);

   void MarshallInterfacePointer();

	// NMEA functions
   int process_RMC_sentence(const char *sentence);
   int process_GGA_sentence(const char *sentence);
   int process_GLL_sentence(const char *sentence);
   int process_VTG_sentence(const char *sentence);
   int process_ALM_sentence(const char *sentence);
	int add_point();

	// for addition NMEA data
	// A usable GPS reading should contain at least the minumum fields
	// contained in the sentence mask
	// Notes: 
	//		1)	an "add_point" call was added to within the VTG function
	//		2)	the timeout function was disabled as it was causing an invalid
	//			DATE to be passed to the add_point function. Need to investigate.
	GPS_FIELDS	m_gps_fields;
	
	BOOL C_gps_trail::has_minimum_required_data( GPS_FIELDS& fields, BOOL bDate ) 
	{
		BOOL bOk = FALSE;

		// uncomment for GPS_DELAY_FIX
		// return FALSE;

		if (	fields.bTime && 
				fields.bLocation && 
				fields.bSpeedKts && 
				fields.bTrueHeading && 
				fields.bAltitude && 
				( bDate == TRUE || fields.bDate ) )
		{
			bOk = TRUE;
		}
		
#ifdef GPS_NORMAL_INPUT_TRACE
		TRACE("C_gps_trail::has_minumum_required_data() = %d\r\n", bOk );
#endif
		return bOk;
	}

	CString ShowFlagStatus()
	{	
		CString s;
		s.Format("alt:%d date:%d loc:%d mhdg:%d msl:%d nsat:%d kts:%d time:%d thdg:%d",	
			this->m_gps_fields.bAltitude, 
			this->m_gps_fields.bDate,
			this->m_gps_fields.bLocation,
			this->m_gps_fields.bMagHeading,
			this->m_gps_fields.bMslFlag,
			this->m_gps_fields.bNumSat,
			this->m_gps_fields.bSpeedKts,
			this->m_gps_fields.bTime,
			this->m_gps_fields.bTrueHeading );
		
		return s;
	}
};
#endif  // _WIN32


#endif _GPS_H_

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

