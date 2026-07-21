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



/////////////////////////////////////////////////////////////////////////////
//
//  MODULE:     GEOTRANS.CPP
//
//  PURPOSE:    To provide a simple DLL template. This module contains a
//              generic DLL entry point (replacing the LIbMain & WEP of
//              Win16),
//
//  FUNCTIONS:  convert_datum()
//              convert_geo()
//              convert_utm()
//              convert_milgrid()
//
//  COMMENTS:   Another name may be used for the entry point
//              ("DllMain" is only an example). If no initialization
//              or notification is required in a DLL, the DLL entry point
//              may be omitted. In either case, make sure to modify the
//              "-entry:DllMain" part of the DLL's link line in the
//              makefile; either substitute the new entry point name, or
//              delete the line altogether (if omitting the entry point).
//
/////////////////////////////////////////////////////////////////////////////



#include "stdafx.h"
#include <string.h>
#include <set>
#include "geotrans.h"

#include <map>
#pragma warning( push )
#pragma warning( disable : 4251 4996 )
#include "CoordinateConversion/CoordinateConversionService.h"
#include "dtcc/CoordinateSystems/datum/DatumLibraryImplementation.h"
#include "dtcc/CoordinateSystemParameters/CoordinateSystemParameters.h"
#include "dtcc/CoordinateSystemParameters/GeodeticParameters.h"
#include "dtcc/CoordinateSystemParameters/UTMParameters.h"
#include "dtcc/CoordinateTuples/Accuracy.h"
#include "dtcc/CoordinateTuples/CartesianCoordinates.h"
#include "dtcc/CoordinateTuples/CoordinateTuple.h"
#include "dtcc/DatumLibrary.h"
#include "dtcc/EllipsoidLibrary.h"
#include "dtcc/CoordinateTuples/GARSCoordinates.h"
#include "dtcc/CoordinateTuples/GeodeticCoordinates.h"
#include "dtcc/CoordinateTuples/MGRSorUSNGCoordinates.h"
#include "dtcc/CoordinateTuples/UTMCoordinates.h"
#include "dtcc/Enumerations/CoordinateType.h"
#include "dtcc/Enumerations/HeightType.h"
#include "dtcc/Exception/CoordinateConversionException.h"
#pragma warning( pop )
#pragma comment( lib, "MSPCoordinateConversionService.lib" )

#include <string>
#ifdef _WIN32
#include "ComErrorObject.h"
#else
// POSIX port: _bstr_t is only used as a dedup key for logged error strings,
// and WriteToLogFile comes from ComErrorObject.h on Windows.
#include <string>
typedef std::string _bstr_t;
static void WriteToLogFile(const _bstr_t& msg)
{
   fprintf(stderr, "%s\n", msg.c_str());
}
#endif
#include "defs.h"

// CoordinateConversionService maintains an instance of a
// MSP::CCS::CoordinateConversionService and recreates it as necessary whenever
// the source and target datum change
class CoordinateConversionService
{
   MSP::CCS::CoordinateSystemParameters* m_source_params;
   MSP::CCS::CoordinateSystemParameters* m_target_params;

   struct INSTANCE
   {
      MSP::CCS::CoordinateConversionService* ccs;
      std::string current_source_datum;
      std::string current_target_datum;
   };
   std::map<DWORD, INSTANCE> m_instance;
   CRITICAL_SECTION m_instance_cs;

public:
   CoordinateConversionService(
         MSP::CCS::CoordinateSystemParameters* source_params,
         MSP::CCS::CoordinateSystemParameters* target_params)
      : m_source_params(source_params), m_target_params(target_params)
   {
      ::InitializeCriticalSection(&m_instance_cs);
   }
   ~CoordinateConversionService()
   {
      ::DeleteCriticalSection(&m_instance_cs);
   }

   // GetInstance with default WGS84 source and target datum
   MSP::CCS::CoordinateConversionService* GetInstance()
   {
      INSTANCE* instance = FindInstance(::GetCurrentThreadId());
      if (instance != nullptr)
         return instance->ccs;

      return GetInstance("WGE", "WGE");
   }

   MSP::CCS::CoordinateConversionService* GetInstance(const char* source_datum,
         const char* target_datum)
   {
      DWORD thread_id = ::GetCurrentThreadId();
      
      INSTANCE* instance = FindInstance(thread_id);

      const char* src = CGeoTrans::get_common_datum_name(source_datum);
      const char* tgt = CGeoTrans::get_common_datum_name(target_datum);

      // if the input/output datum have changed then we need to create a new
      // instance of the CoordinateConversionService. Note that we want to
      // create a new instance before destroying the existing one so that the
      // singletons (DatumLibrary, ElliposidLibrary, and GeoidLibrary) remain
      // alive.
      if (instance == nullptr ||
         strcmp(instance->current_source_datum.c_str(), src) ||
         strcmp(instance->current_target_datum.c_str(), tgt))
      {
         MSP::CCS::CoordinateConversionService* cc = 
            new MSP::CCS::CoordinateConversionService(src, m_source_params,
               tgt, m_target_params);

         if (instance != nullptr)
            delete instance->ccs;

         ::EnterCriticalSection(&m_instance_cs);
         m_instance[thread_id].ccs = cc;
         m_instance[thread_id].current_source_datum = src;
         m_instance[thread_id].current_target_datum = tgt;
         ::LeaveCriticalSection(&m_instance_cs);

         return cc;
      }
      else
         return instance->ccs;
   }

private:
   INSTANCE* FindInstance(DWORD thread_id)
   {
      INSTANCE* ret = nullptr;
      ::EnterCriticalSection(&m_instance_cs);
      {
         auto it = m_instance.find(thread_id);
         if (it != m_instance.end())
            ret = &it->second;
      }
      ::LeaveCriticalSection(&m_instance_cs);

      return ret;
   }
};

static MSP::CCS::GeodeticParameters geodeticParameters(
      MSP::CCS::CoordinateType::geodetic,
      MSP::CCS::HeightType::ellipsoidHeight);

static MSP::CCS::UTMParameters utmParameters(
      MSP::CCS::CoordinateType::universalTransverseMercator, 1, 0 );

static MSP::CCS::CoordinateSystemParameters mgrsParameters(
      MSP::CCS::CoordinateType::militaryGridReferenceSystem );

static MSP::CCS::CoordinateSystemParameters garsParameters(
      MSP::CCS::CoordinateType::globalAreaReferenceSystem );

static CoordinateConversionService s_geodetic_to_geodetic(&geodeticParameters, &geodeticParameters);
static CoordinateConversionService s_geodetic_to_mgrs(&geodeticParameters, &mgrsParameters);
static CoordinateConversionService s_geodetic_to_utm(&geodeticParameters, &utmParameters);
static CoordinateConversionService s_geodetic_to_gars(&geodeticParameters, &garsParameters);
static CoordinateConversionService s_utm_to_geodetic(&utmParameters, &geodeticParameters);
static CoordinateConversionService s_utm_to_mgrs(&utmParameters, &mgrsParameters);
static CoordinateConversionService s_mgrs_to_geodetic(&mgrsParameters, &geodeticParameters);
static CoordinateConversionService s_mgrs_to_utm(&mgrsParameters, &utmParameters);

// Sounding Datum code string conversion
static LPCSTR const
   sounding_datum_str[][2] = {
      {"MHW",  "Mean High Water"},
      {"MHW",  "Mean High Water Neaps"},
      {"MHW",  "Mean High Water Springs"},
      {"MHW",  "Mean Higher High Water"},
      {"MLW",  "Mean Low Water"},
      {"MLW",  "Mean Low Water Neaps"},
      {"MLW",  "Mean Low Water Springs"},
      {"MLW",  "Mean Lower Low Water"},
      {"MLLW", "Mean Lower Low Water"}
};
static const int NUM_SOUNDING_DATUMS =
   sizeof(sounding_datum_str) / sizeof(sounding_datum_str[0]);

// Vertical Datum code string conversion
static LPCSTR const
   vertical_datum_str[][2] = {
      {"MSL",  "Mean Sea Level"},   // Elevations referenced from the geoid of the specified datum
      {"GEOD", "Geodetic"}          // Elevations references from the ellipsoid of the specified datum
   };
static const int NUM_VERTICAL_DATUMS =
   sizeof(vertical_datum_str) / sizeof(vertical_datum_str[0]);

static const LPCSTR
   datum_alias[][2] = {
      {"ADI",  "ADI-M"},
      {"AIN",  "AIN-A"},
      {"ARF",  "ARF-M"},
      {"ARS",  "ARS-M"},
      {"ASK",    "ENW"},  // ENW
      {"BEL",    "IBE"},  // IBE
      {"CHO",    "CHI"},  // CHI
      {"DES",    "GIZ"},  // GIZ
      {"DJA",    "BAT"},  // BAT
      {"EUR",  "EUR-A"},
      {"GOX",    "DOB"},  // DOB
      {"HUT",    "CAZ"},  // CAZ
      {"IND",  "IND-I"},
      {"INF",  "INF-A"},
      {"INH",  "INH-A"},
      {"KOR",    "KGS"},
      {"LUZ",    "LIB"},
      {"MCM",    "CAZ"},  // CAZ
      {"MIN",  "MIN-B"},
      {"NAH",  "NAH-B"},
      {"NAR",  "NAR-C"},
      {"NAS",  "NAS-C"},  // NAS-C
      {"OGB",  "OGB-M"},
      {"OHA",  "OHA-C"},
      {"OHA-P","OHA-A"},
      {"OHA-Q","OHI-C"},
      {"OHA-R","OHI-D"},
      {"OMA",  "EUR-T"},  // FAH
      {"PID",    "IST"},  // KAN
      {"PRP",  "PRP-G"},
      {"SAN",  "SAN-K"},
      {"SGS",    "WGE"},  // W84 ?
      {"SPK",  "SPK-B"},
      {"TOK",  "TOY-M"},  // TOY-M
      {"TOY",  "TOY-M"},
      {"UND",    "WGE"},  // W84
      {"UNK",    "WGE"},  // W84
      {"VIT",    "MOD"},  // MVS
      {"W72",    "WGC"},  // W72
      {"WGS72",  "WGC"},  // W72
      {"W84",    "WGE"},  // W84
      {"WGS84",  "WGE"},  // W84
      {"WGX",    "WGE"}}; // W84
static const int NUM_DATUM_ALIASES =
   sizeof(datum_alias) / sizeof(datum_alias[0]);

static LPCSTR const
   ui_datum_alias[][2] = {
      {"WGE",  "WGS84"},
      {"WGC",  "WGS72"},
      {"WGB",  "WGS66"}};
static const int NUM_UI_DATUM_ALIASES =
   sizeof(ui_datum_alias) / sizeof(ui_datum_alias[0]);

   std::set<_bstr_t> m_errorStrings;

   CGeoTrans::CGeoTrans() :
      m_pszErrorMsg( nullptr )
   {
   }

   CGeoTrans::~CGeoTrans()
   {
      delete m_pszErrorMsg;
   }

   long CGeoTrans::report_error( long lStatus,
               const std::stringstream& ssErrorMsg )
   {
      return report_error( lStatus, ssErrorMsg, TRUE );
   }

   LONG CGeoTrans::report_error( long lStatus,
               const std::stringstream& ssErrorMsg, boolean_t bLogError )
   {
      size_t c = ssErrorMsg.str().length() + 1;
      delete m_pszErrorMsg;
      m_pszErrorMsg = new CHAR[ c ];
      strcpy_s( m_pszErrorMsg, c, ssErrorMsg.str().c_str() );

      if ( bLogError )
      {
         _bstr_t bstrErrorMsg = ssErrorMsg.str().c_str();
         if( m_errorStrings.find( bstrErrorMsg ) == m_errorStrings.end() )
         {
            m_errorStrings.insert( bstrErrorMsg );
            WriteToLogFile( bstrErrorMsg );
         }
      }

      return lStatus;
   }  // report_error()

   /////////////////////////////////////////////////////////////////////////////
   //
   long CGeoTrans::convert_datum( double lat_in,
      double long_in,   // input in degrees (W is minus)
      double &lat_out,
      double &long_out, // output in degree
      LPCSTR sdatum_in,
      LPCSTR sdatum_out)
   {
      lat_out = lat_in;          // In case of conversion failure
      long_out = long_in;

      try
      {
         MSP::CCS::Accuracy sourceAccuracy;
         MSP::CCS::Accuracy targetAccuracy;

         MSP::CCS::GeodeticCoordinates geodeticSourceCoordinates(
            MSP::CCS::CoordinateType::geodetic,
            DEG_TO_RAD( long_in ), DEG_TO_RAD( lat_in ), 0.0 );

         MSP::CCS::GeodeticCoordinates geodeticTargetCoordinates;
         s_geodetic_to_geodetic.GetInstance(sdatum_in, sdatum_out)->convertSourceToTarget(
            &geodeticSourceCoordinates, &sourceAccuracy,
            geodeticTargetCoordinates, targetAccuracy );
         lat_out = RAD_TO_DEG( geodeticTargetCoordinates.latitude() );
         long_out = RAD_TO_DEG( geodeticTargetCoordinates.longitude() );

         return SUCCESS;

      }
      catch(MSP::CCS::CoordinateConversionException e)
      {
         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem converting from " << sdatum_in << " to " << sdatum_out << ", " <<
            e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      }
      return FAILURE;
   }
   // end of convert_datum()


   /////////////////////////////////////////////////////////////////////////////
   //
   long CGeoTrans::convert_geo(
      double lat_in, double long_in,          // input in degrees (W is minus)
      LPCSTR sdatum,
      int &utm_zone, 
      double &utm_northing,      // - if southern hemisphere
      double &utm_easting,
      char *milgrid, int milgrid_len )
   {
      char utm_hemisphere;
      long status = convert_geo(
         lat_in, long_in, sdatum,
         utm_zone, utm_hemisphere, utm_northing, utm_easting,
         milgrid, milgrid_len );
      if ( status == SUCCESS
            && utm_hemisphere == 'S' )
         utm_northing = -utm_northing;
      return status;

   }  // convert_geo() - implicit utm hemisphere

   // Convert from geodetic to MGRS coordinates
   long CGeoTrans::convert_geo_to_mgrs(double lat_in, double long_in,
      LPCSTR sdatum, char *milgrid, int milgrid_len)
   {
      try
      {
         MSP::CCS::CoordinateConversionService* ccs =
            s_geodetic_to_mgrs.GetInstance(sdatum, sdatum);

         MSP::CCS::Accuracy sourceAccuracy;
         MSP::CCS::Accuracy targetAccuracy;

         MSP::CCS::GeodeticCoordinates geodeticSourceCoordinates(
            MSP::CCS::CoordinateType::geodetic,
            DEG_TO_RAD( long_in ), DEG_TO_RAD( lat_in ), 0.0 );

         MSP::CCS::MGRSorUSNGCoordinates mgrsTargetCoordinates;
         ccs->convertSourceToTarget( &geodeticSourceCoordinates, &sourceAccuracy,
            mgrsTargetCoordinates, targetAccuracy );
         strcpy_s( milgrid, milgrid_len, mgrsTargetCoordinates.MGRSString() );

         return SUCCESS;

      }
      catch(MSP::CCS::CoordinateConversionException e)
      {
         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem converting from " << sdatum << " to milgrid, " <<
            e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      }
   }

   long CGeoTrans::convert_geo(
      double lat_in, double long_in,          // input in degrees (W is minus)
      LPCSTR sdatum,
      int &utm_zone, char& utm_hemisphere,
      double &utm_northing,
      double &utm_easting,
      char *milgrid, int milgrid_len )
   {
      try
      {
         MSP::CCS::CoordinateConversionService* ccs1 =
            s_geodetic_to_utm.GetInstance(sdatum, sdatum);
         MSP::CCS::CoordinateConversionService* ccs2 =
            s_geodetic_to_mgrs.GetInstance(sdatum, sdatum);

         MSP::CCS::Accuracy sourceAccuracy;
         MSP::CCS::Accuracy targetAccuracy;

         MSP::CCS::GeodeticCoordinates geodeticSourceCoordinates(
            MSP::CCS::CoordinateType::geodetic,
            DEG_TO_RAD( long_in ), DEG_TO_RAD( lat_in ), 0.0 );

         MSP::CCS::UTMCoordinates utmTargetCoordinates;
         ccs1->convertSourceToTarget( &geodeticSourceCoordinates,
               &sourceAccuracy, utmTargetCoordinates, targetAccuracy );
         utm_zone = utmTargetCoordinates.zone();
         utm_hemisphere = utmTargetCoordinates.hemisphere();
         utm_easting = utmTargetCoordinates.easting();
         utm_northing = utmTargetCoordinates.northing();

         MSP::CCS::MGRSorUSNGCoordinates mgrsTargetCoordinates;
         ccs2->convertSourceToTarget( &geodeticSourceCoordinates,
               &sourceAccuracy, mgrsTargetCoordinates, targetAccuracy );
         strcpy_s( milgrid, milgrid_len, mgrsTargetCoordinates.MGRSString() );

         return SUCCESS;

      }
      catch(MSP::CCS::CoordinateConversionException e)
      {
         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem converting from " << sdatum << " to utm and milgrid, " <<
            e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      }
   }
   // end of convert_geo            


   /////////////////////////////////////////////////////////////////////////////
   //
   long CGeoTrans::convert_geo(
      double lat_in, double long_in,
      LPCSTR sdatum, LPSTR gars, int gars_len)
   {
      long status = SUCCESS;
      try
      {
         MSP::CCS::GeodeticParameters geodeticParameters(
               MSP::CCS::CoordinateType::geodetic,
               MSP::CCS::HeightType::ellipsoidHeight );
         MSP::CCS::CoordinateSystemParameters garsParameters(
            MSP::CCS::CoordinateType::globalAreaReferenceSystem );

         MSP::CCS::CoordinateConversionService* ccs = s_geodetic_to_gars.GetInstance("WGE",
               sdatum);

         MSP::CCS::Accuracy sourceAccuracy;
         MSP::CCS::Accuracy targetAccuracy;

         MSP::CCS::GeodeticCoordinates geodeticSourceCoordinates(
            MSP::CCS::CoordinateType::geodetic,
            DEG_TO_RAD( long_in ), DEG_TO_RAD( lat_in ), 0.0 );

         MSP::CCS::GARSCoordinates garsTargetCoordinates;
         ccs->convertSourceToTarget( &geodeticSourceCoordinates,
               &sourceAccuracy, garsTargetCoordinates, targetAccuracy );
         strcpy_s( gars, gars_len, garsTargetCoordinates.GARSString() );

         return SUCCESS;

      }
      catch(MSP::CCS::CoordinateConversionException e)
      {
         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem converting from " << sdatum << " to utm and milgrid, " <<
            e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      } 
      return FAILURE;
   }

   /////////////////////////////////////////////////////////////////////////////
   //
   long CGeoTrans::convert_utm( int utm_zone, 
      double utm_northing,
      double utm_easting,
      LPCSTR sdatum,
      double &lat_out,
      double &long_out,   // output in degrees (W is minus)
      char *milgrid, int milgrid_len )
   {
      char utm_hemisphere = 'N';
      if ( utm_northing < 0.0 )
      {
         utm_northing = -utm_northing;
         utm_hemisphere = 'S';
      }
      return convert_utm(
         utm_zone, utm_hemisphere, utm_northing, utm_easting,
         sdatum,
         lat_out, long_out,      // output in degrees (W is minus)
         milgrid, milgrid_len );
   }  // convert_utm() - implicit hemisphere

   long CGeoTrans::convert_utm(
      int utm_zone, char utm_hemisphere,
      double utm_northing, double utm_easting,
      LPCSTR sdatum,
      double &lat_out, double &long_out,   // output in degrees (W is minus)
      char *milgrid, int milgrid_len )
   {
      try
      {
         MSP::CCS::CoordinateConversionService* ccs1 =
            s_utm_to_geodetic.GetInstance(sdatum, "WGE");
         MSP::CCS::CoordinateConversionService* ccs2 =
            s_utm_to_mgrs.GetInstance(sdatum, "WGE");

         MSP::CCS::Accuracy sourceAccuracy;
         MSP::CCS::Accuracy targetAccuracy;

         MSP::CCS::UTMCoordinates utmSourceCoordinates(
            MSP::CCS::CoordinateType::universalTransverseMercator,
            utm_zone, utm_hemisphere, utm_easting, utm_northing );

         MSP::CCS::GeodeticCoordinates geodeticTargetCoordinates;
         ccs1->convertSourceToTarget( &utmSourceCoordinates, &sourceAccuracy,
            geodeticTargetCoordinates, targetAccuracy );
         lat_out = RAD_TO_DEG( geodeticTargetCoordinates.latitude() );
         long_out = RAD_TO_DEG( geodeticTargetCoordinates.longitude() );

         MSP::CCS::MGRSorUSNGCoordinates mgrsTargetCoordinates;
         ccs2->convertSourceToTarget( &utmSourceCoordinates, &sourceAccuracy,
            mgrsTargetCoordinates, targetAccuracy );
         strcpy_s( milgrid, milgrid_len, mgrsTargetCoordinates.MGRSString() );

         return SUCCESS;
      }
      catch(MSP::CCS::CoordinateConversionException e)
      {
         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem converting from " << sdatum << " utm to WGS84 lat/long and milgrid, " <<
            e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      }
   }
   // end of convert_utm()


   /////////////////////////////////////////////////////////////////////////////
   //
   long CGeoTrans::convert_milgrid( LPCSTR milgrid,
      LPCSTR sdatum,
      double &lat_out,
      double &long_out, // output in degrees (W is minus)
      int &utm_zone,
      double &utm_northing, double &utm_easting)
   {
      CHAR utm_hemisphere;
      long status = convert_milgrid(
         milgrid,
         sdatum,
         lat_out, long_out,   // output in degrees (W is minus)
         utm_zone, utm_hemisphere,
         utm_northing, utm_easting );
      if ( status == SUCCESS
            && utm_hemisphere == 'S' )
         utm_northing = -utm_northing;
      return status;
   }  // convert_milgrid() - implicit utm hemisphere

   long CGeoTrans::convert_milgrid(
      LPCSTR milgrid,
      LPCSTR sdatum,
      double &lat_out, double &long_out, // output in degrees (W is minus)
      int &utm_zone, char& utm_hemisphere,
      double &utm_northing, double &utm_easting)
   {
      try
      {
         MSP::CCS::CoordinateConversionService* ccs1 =
            s_mgrs_to_geodetic.GetInstance(sdatum, "WGE");
         MSP::CCS::CoordinateConversionService* ccs2 =
            s_mgrs_to_utm.GetInstance(sdatum, "WGE");

         MSP::CCS::Accuracy sourceAccuracy;
         MSP::CCS::Accuracy targetAccuracy;

         MSP::CCS::MGRSorUSNGCoordinates mgrsSourceCoordinates(
            MSP::CCS::CoordinateType::militaryGridReferenceSystem, milgrid );

         MSP::CCS::GeodeticCoordinates geodeticTargetCoordinates;
         ccs1->convertSourceToTarget( &mgrsSourceCoordinates, &sourceAccuracy,
            geodeticTargetCoordinates, targetAccuracy );
         lat_out = RAD_TO_DEG( geodeticTargetCoordinates.latitude() );
         long_out = RAD_TO_DEG( geodeticTargetCoordinates.longitude() );

         MSP::CCS::UTMCoordinates utmTargetCoordinates;
         ccs2->convertSourceToTarget( &mgrsSourceCoordinates, &sourceAccuracy,
            utmTargetCoordinates, targetAccuracy );
         utm_zone = utmTargetCoordinates.zone();
         utm_hemisphere = utmTargetCoordinates.hemisphere();
         utm_easting = utmTargetCoordinates.easting();
         utm_northing = utmTargetCoordinates.northing();

         return SUCCESS;

      }
      catch(MSP::CCS::CoordinateConversionException e)
      {
         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem converting from " << sdatum << " mgrs to WGS lat/long and milgrid, " <<
            e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      }
   }
   // end of convert_milgrid

   /////////////////////////////////////////////////////////////////////////////
   //
   struct CPStringLess
   {
      bool operator()( LPCSTR const & x, LPCSTR const & y ) const
      {
         return strcmp( x, y) < 0;
      }
   };
   
   typedef std::map< LPCSTR, LPCSTR, CPStringLess > DATUM_ALIASES;
   static DATUM_ALIASES mpDatumAliases;

   /* static */
   LPCSTR CGeoTrans::get_common_datum_name( LPCSTR pszDatumName )
   {
      if ( mpDatumAliases.empty() )
      {
         for ( int i=0; i < NUM_DATUM_ALIASES; i++ )
            mpDatumAliases.insert(
               DATUM_ALIASES::value_type( datum_alias[ i ][ 0 ], datum_alias[ i ][ 1 ] ) );
      }

      DATUM_ALIASES::iterator it =
         mpDatumAliases.find( pszDatumName );
      if ( it != mpDatumAliases.end() )
         return it->second;

      return pszDatumName; // Just return the original
   }

   short CGeoTrans::get_common_datum_name(LPCSTR datum,LPSTR item, int item_len)
   {

      // search for match in alias table
      for (int i=0; i < NUM_DATUM_ALIASES; i++)
      {
         if (strcmp(datum, datum_alias[i][0]) == 0)
         {
            strcpy_s(item,item_len,datum_alias[i][1]);
            return SUCCESS;
         }
      }

      //we know of no other aliases
      strcpy_s(item, item_len, datum);

      return SUCCESS;
   }


   long CGeoTrans::get_descriptive_name( LPCSTR code_in, LPSTR name, int name_len)
   {
      try
      {
         LPCSTR pszCommonDatum = get_common_datum_name( code_in );
         long lDatumIndex;
         GetDatumLibrary()->getDatumIndex( pszCommonDatum, &lDatumIndex );

         char chDatumCode[ MSP::CCS::DATUM_CODE_LENGTH ] = "";
         char chDatumDesc[ MSP::CCS::DATUM_NAME_LENGTH ] = "";
         char chEllipsoidCode[ MSP::CCS::ELLIPSOID_CODE_LENGTH ] = "";
         GetDatumLibrary()->getDatumInfo( lDatumIndex, chDatumCode, chDatumDesc, chEllipsoidCode);
         strcpy_s( name, name_len, chDatumDesc );
         
         return SUCCESS;
      }
      catch( MSP::CCS::CoordinateConversionException e )
      {
         // Not a known datum.  Try some of our own known names
         LPCSTR pszCommonName = get_common_datum_name( code_in );
         int ct;
         for ( ct=0; ct<NUM_SOUNDING_DATUMS; ct++ )
         {
            if ( strcmp( pszCommonName, sounding_datum_str[ ct ][ 0 ] ) == 0 )
            {
               // Sounding datum recognized
               strcpy_s( name, name_len, sounding_datum_str[ ct ][ 1 ] );
               return SUCCESS;
            }
         }

         for ( ct=0; ct<NUM_VERTICAL_DATUMS; ct++ )
         {
            if ( strcmp( pszCommonName, vertical_datum_str[ ct ][ 0 ] ) == 0 )
            {
               // Vertical datum recognized
               strcpy_s( name, name_len, vertical_datum_str[ ct ][ 1 ] );
               return SUCCESS;
            }
         }

         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem getting datum descriptive name for code \"" <<
            code_in << L"\", " << e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      } 
   }

   void CGeoTrans::DLL_get_return_code_string(
      long error_code, LPCSTR separator, LPSTR string, int string_len)
   {
      if ( m_pszErrorMsg != nullptr )
         strcpy_s( string, string_len, m_pszErrorMsg );
      else if ( string_len > 0 )
         string[ 0 ] = '\0';
      return;
   }


   long CGeoTrans::get_user_datum_code( LPCSTR common_code,LPSTR ui_code, int ui_code_len )
   {   
      // search for match in alias table
      for (int i=0; i < NUM_UI_DATUM_ALIASES; i++)
      {
         if (strcmp(common_code, ui_datum_alias[i][0]) == 0)
         {
            strcpy_s(ui_code,ui_code_len,ui_datum_alias[i][1]);
            return SUCCESS;
         }
      }

      //we know of no other aliases
      strcpy_s(ui_code, ui_code_len, common_code);
      return SUCCESS;
   }


   long CGeoTrans::get_datum_ellipsoid_name( LPCSTR datum, 
      LPSTR ellipsoid_name, int ellipsoid_name_len )
   {
      try
      {
         long lDatumIndex;
         LONG lStatus = GetDatumIndex( datum, lDatumIndex, TRUE );
         if ( lStatus == SUCCESS )
         {
            char chDatumCode[ MSP::CCS::DATUM_CODE_LENGTH ] = "";
            char chDatumDesc[ MSP::CCS::DATUM_NAME_LENGTH ] = "";
            char chEllipsoidCode[ MSP::CCS::ELLIPSOID_CODE_LENGTH ] = "";
            GetDatumLibrary()->getDatumInfo( lDatumIndex, chDatumCode,
               chDatumDesc, chEllipsoidCode );

            MSP::CCS::EllipsoidLibrary* ellipsoid_lib = GetEllipsoidLibrary();

            long ellipsoid_index;
            ellipsoid_lib->getEllipsoidIndex(chEllipsoidCode, &ellipsoid_index);
            
            char chEllipsoidName[ MSP::CCS::ELLIPSOID_NAME_LENGTH ];
            ellipsoid_lib->getEllipsoidInfo(ellipsoid_index, chEllipsoidCode,
               chEllipsoidName);

            strcpy_s( ellipsoid_name, ellipsoid_name_len, chEllipsoidName );
         }
         return lStatus;
      }
      catch( MSP::CCS::CoordinateConversionException e )
      {
         if ( ellipsoid_name_len > 0 )
            ellipsoid_name[ 0 ] = '\0';

         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem getting datum descriptive name for code \"" <<
            datum << L"\", " << e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      }
   }


   long CGeoTrans::get_datum_count(long &datum_items)
   {
      try
      {
         GetDatumLibrary()->getDatumCount( &datum_items );
         return SUCCESS;
      }
      catch( MSP::CCS::CoordinateConversionException e )
      {
         datum_items = 0;

         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem getting datum count, " <<
            e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      }
   }


   long CGeoTrans::get_datum_code ( const long index, LPSTR code, int code_len )
   {
      try
      {
         // GEOTRANS 2.x used 1-based, rather than 0-based, indices. We want to
         // preserve this behavior.
         if (index == 0)
         {
            return FAILURE;
         }

         char chDatumCode[ MSP::CCS::DATUM_CODE_LENGTH ];
         char chDatumDesc[ MSP::CCS::DATUM_NAME_LENGTH ] = "";
         char chEllipsoidCode[ MSP::CCS::ELLIPSOID_CODE_LENGTH ] = "";
         GetDatumLibrary()->getDatumInfo( index - 1, chDatumCode, chDatumDesc,
            chEllipsoidCode);
         strcpy_s( code, code_len, chDatumCode );
         return SUCCESS;
      }
      catch( MSP::CCS::CoordinateConversionException e )
      {
         if ( code_len > 0 )
            code[ 0 ] = '\0';

         std::stringstream ssErrorMsg;
         ssErrorMsg <<
            "Problem getting datum code for index " << index << ", " <<
            e.getMessage() << std::ends;
         return report_error( FAILURE, ssErrorMsg );
      }
   }

LONG CGeoTrans::GetDatumIndex(
      __in LPCSTR pszDatumName,
      __out long& lDatumIndex,
      __in boolean_t bLogIfInvalid )
{
   try
   {
      LPCSTR pszCommonDatum = get_common_datum_name( pszDatumName );
      GetDatumLibrary()->getDatumIndex( pszCommonDatum, &lDatumIndex );
      return SUCCESS;

   }
   catch( MSP::CCS::CoordinateConversionException e )
   {
      std::stringstream ssErrorMsg;
      LONG lStatus;
      if ( 0 == _strnicmp( e.getMessage(), "Invalid datum", 13 ) )
         lStatus = INVALID_DATUM_CODE;
      else
      {
         lStatus = FAILURE;
         if ( bLogIfInvalid )
            ssErrorMsg << "Problem getting datum info for \"" <<
               pszDatumName  << "\", ";
      }
      ssErrorMsg << e.getMessage() << std::ends;
      return report_error( lStatus, ssErrorMsg, bLogIfInvalid );
   } 
}
  

MSP::CCS::DatumLibrary* CGeoTrans::GetDatumLibrary()
{
   return s_geodetic_to_geodetic.GetInstance()->getDatumLibrary();
}

MSP::CCS::EllipsoidLibrary* CGeoTrans::GetEllipsoidLibrary()
{
   return s_geodetic_to_geodetic.GetInstance()->getEllipsoidLibrary();
}

