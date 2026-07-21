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

// projsp.h

#ifndef PROJSP_H
#define PROJSP_H 1

typedef struct
{
	char name[33];
	int id;
	int units;
	char datum[6];
	int code;
	double table[9];
} sp_param_t;

// units defines

#define PROJ_UNITS_RADIAN 0		// radians
#define PROJ_UNITS_US_FEET 1
#define PROJ_UNITS_METERS 2
#define PROJ_UNITS_SECOND 3
#define PROJ_UNITS_DEGREES 4
#define PROJ_UNITS_INTER_FEET 5

// zone define
#define PROJ_ZONE_UTM 1
#define PROJ_ZONE_STATE_PLANE 2

// projection types
#define  PROJ_TYPE_GEO       0  // (Geographic)
#define  PROJ_TYPE_UTM       1 // (Universal Transverse Mercator)
#define  PROJ_TYPE_SPCS      2 // (State Plane Coordinates)
#define  PROJ_TYPE_ALBERS    3 // (Albers Conical Equal Area)
#define  PROJ_TYPE_LAMCC     4 // (Lambert Conformal Conic)
#define  PROJ_TYPE_MERCAT    5 // (Mercator)
#define  PROJ_TYPE_PS        6 // (Polar Stereographic)
#define  PROJ_TYPE_POLYC     7 // (Polyconic)
#define  PROJ_TYPE_EQUIDC    8 // (Equidistant Conic)
#define  PROJ_TYPE_TM        9 // (Transverse Mercator)
#define  PROJ_TYPE_STEREO   10 // (Stereographic)
#define  PROJ_TYPE_LAMAZ    11 // (Lambert Azimuthal Equal Area)
#define  PROJ_TYPE_AZMEQD   12 // (Azimuthal Equidistant)
#define  PROJ_TYPE_GNOMON   13 // (Gnomonic)
#define  PROJ_TYPE_ORTHO    14 // (Orthographic)
#define  PROJ_TYPE_GVNSP    15 // (General Vertical Near-Side Perspective)
#define  PROJ_TYPE_SNSOID   16 // (Sinusoidal)
#define  PROJ_TYPE_EQRECT   17 // (Equirectangular)
#define  PROJ_TYPE_MILLER   18 // (Miller Cylindrical)
#define  PROJ_TYPE_VGRINT   19 // (Van der Grinten)
#define  PROJ_TYPE_HOM      20 // (Hotine Oblique Mercator--HOM)
#define  PROJ_TYPE_ROBIN    21 // (Robinson)
#define  PROJ_TYPE_SOM      22 // (Space Oblique Mercator--SOM)
#define  PROJ_TYPE_ALASKA   23 // (Modified Stereographic Conformal--Alaska)
#define  PROJ_TYPE_GOOD     24 // (Interrupted Goode Homolosine)
#define  PROJ_TYPE_MOLL     25 // (Mollweide)
#define  PROJ_TYPE_IMOLL    26 // (Interrupted Mollweide)
#define  PROJ_TYPE_HAMMER   27 // (Hammer)
#define  PROJ_TYPE_WAGIV    28 // (Wagner IV)
#define  PROJ_TYPE_WAGVII   29 // (Wagner VII)
#define  PROJ_TYPE_OBLEQA   30 // (Oblated Equal Area)

#define COEFCT 15		//  projection coefficient count 

//#define PI 	3.141592653589793238
//#define HALF_PI (PI*0.5)
//#define TWO_PI 	(PI*2.0)
#define EPSLN	1.0e-10
#define R2D     57.2957795131
/*
#define D2R     0.0174532925199
*/
#define D2R     1.745329251994328e-2
#define S2R	4.848136811095359e-6

#define OK	0

#define STPLN_TABLE 6

/* General code numbers */

#define IN_BREAK -2		/*  Return status if the interupted projection
				    point lies in the break area */
#define COEFCT 15		/*  projection coefficient count */
#define PROJCT 30		/*  projection count */
#define SPHDCT 31		/*  spheroid count */

#define MAXPROJ 31		/*  Maximum projection number */
#define MAXUNIT 5		/*  Maximum unit code number */
#define GEO_TERM 0		/*  Array index for print-to-term flag */
#define GEO_FILE 1		/*  Array index for print-to-file flag */
#define GEO_TRUE 1		/*  True value for geometric true/false flags */
#define GEO_FALSE -1		/*  False val for geometric true/false flags */

#ifndef ERROR
#define ERROR  -1
#endif

#define IN_BREAK -2
#define MAX_VAL 4
#define DBLLONG 4.61168601e18

/* Misc macros
  -----------*/
#define SQUARE(x)       x * x   /* x**2 */
#define CUBE(x)     x * x * x   /* x**3 */
#define QUAD(x) x * x * x * x   /* x**4 */

#define GMAX(A, B)      ((A) > (B) ? (A) : (B)) /* assign maximum of a and b */
#define GMIN(A, B)      ((A) < (B) ? (A) : (B)) /* assign minimum of a and b */

#define IMOD(A, B)      (A) - (((A) / (B)) * (B)) /* Integer mod function */


class CProj
{
public:
	CProj();
	~CProj();

	void clear_vars();

	int set_datum(CString datum_str);
	int set_proj_type(long proj_type, long zone);
	int set_zone(long zone);
	int set_units(long units);





	int xy_to_geo(double x, double y, double *lat, double *lon);
	int geo_to_xy(double lat, double lon, double *x, double *y);

protected:
	long m_proj_type;
	long m_datum;
	long m_zone;
	long m_in_units;
	long m_out_units;
	long m_state_units;
	BOOL m_initialized;
	double m_in_params[15];
	double m_out_params[15];
	long m_err_flag;
	CString m_last_error;

	int string_to_datum(CString datum_str, long *datum);

	int inverse_trans(double x, double y, double *lat, double *lon);
	int forward_trans(double lat, double lon, double *x, double *y);


	int stplninvint( long zone, long sphere);
	long stplninv(double x, double y, double *lon, double *lat);
	int stplnforint( long zone, long sphere);
	int stplnfor(double lon, double lat, double *x, double *y);
	int tminvint(double r_maj, double r_min, // major and minor axis
		 double scale_fact, 
		 double center_lon, double center_lat,
		 double false_east, double false_north); // offsets in meters
	int tminv(double x, double y, double *lon, double *lat);
	int tmforint(double r_maj, double r_min,
		 double scale_fact,
		 double center_lon, double center_lat,
		 double false_east, double false_north); // offsets in meters
	int tmfor(double lon, double lat, double *x, double *y);
	int lamccinvint(double r_maj, double r_min,
			double  lat1,	// first standard parallel
			double lat2,	// second standard parallel
			double c_lon, double c_lat,  // center lat and long
			double false_east, double false_north);  // offsets in meters
	int lamccinv(double x , double y, double *lon, double *lat);
	int lamccforint(double r_maj, double r_min,
				 double  lat1,	// first standard parallel 
				 double lat2,	// second standard parallel 
				 double c_lon, double c_lat,	// center lat and long
				 double false_east, double false_north);	// offsets in meters
	int lamccfor(double lon, double lat, double *x, double *y);
	int polyinvint(double r_maj, double r_min,
				double center_lon, double center_lat,
				double false_east, double false_north);
	int polyinv(double x, double y, double *lon, double *lat);
	int polyforint(double r_maj, double r_min,
				double center_lon,
				double center_lat,
				double false_east, double false_north);
	int polyfor(double lon, double lat, double *x, double *y);
	int omerinvint(double r_maj, double r_min,	// major and minor axis
				double scale_fact,			// scale factor
				double azimuth,				// azimuth east of north
				double lon_orig,			// longitude of origin
				double lat_orig,			// center latitude
				double false_east,			// x offset in meters
				double false_north,			// y offset in meters
				double lon1, double lat1,	// first point to define central line
				double lon2, double lat2,	// second point to define central line
				long mode);					// which type: A or B
	int omerinv(double x, double y, double *lon, double *lat);
	int omerforint(double r_maj, double r_min,	// major and minor axis
				double scale_fact,			// scale factor	
				double azimuth,				// azimuth east of north
				double lon_orig,			// longitude of origin
				double lat_orig,			// center latitude
				double false_east,			// x offset in meters
				double false_north,			// y offset in meters
				double lon1, double lat1,	// first point to define central line
				double lon2, double lat2,	// second point to define central line
				long mode);					// which format type A or B
	int omerfor(double lon, double lat, double *x, double *y);

	int utminvint(double r_maj, double r_min, double scale_fact, long zone);
	int utminv(double x, double y, double *lon, double *lat);


	int get_sp_data(long ind, long sphere, char *pname, long *id, long *units, double *table);

	int get_sp_units_from_zone(long fips_zone, long sphere);
	int untfz(long inunit, long outunit, double *ffactor);


	int convert(double *incoor, // input coordinates				
					long *insys,	// input projection code
					long *inzone,	// input zone number
					long *inunit,	// input units
					long *inspheroid,	// input spheroid 
					double *outcoor,	// output coordinates
					long *outsys,		// output projection code
					long *outzone,		// output zone
					long *outunit,		// output units
					long *outspheroid);	// output spheroid


	int inv_init(long insys, long inzone, double *inparm, long indatum, long *iflg);
	int for_init(long outsys, long outzone, double *outparm, long outdatum, long *iflg);



	void sincos(double val, double *sin_val, double *cos_val);
	double asinz (double con);
	double msfnz (double eccent,double sinphi,double cosphi);
	double qsfnz (double eccent,double sinphi,double cosphi);
	double phi1z (double eccent,	// Eccentricity angle in radians
					  double qs,		// Angle in radians	
					  long  *flag);		// Error flag number
	double phi2z(double eccent,  /* Spheroid eccentricity		*/
					 double ts,		/* Constant value t			*/
					 long *flag);	/* Error flag number			*/
	double phi3z(double ml,double e0,double e1,double e2,double e3, long *flag);
	int phi4z (double eccent,double e0,double e1,double e2,double e3,double a,double b,double *c,double *phi);
	double pakcz(double pak); /* Angle in alternate packed DMS format	*/
	double pakr2dm(double pak);  /* Angle in radians			*/
	double tsfnz(double eccent, /* Eccentricity of the spheroid		*/
					 double phi,	/* Latitude phi				*/
					 double sinphi);	/* Sine of the latitude			*/
	int sign(double x);
	double adjust_lon(double x); /* Angle in radians			*/
	double e0fn(double x);
	double e1fn(double x);
	double e2fn(double x);
	double e3fn(double x);
	double e4fn(double x);
	double mlfn(double e0,double e1,double e2,double e3,double phi);
	long calc_utm_zone(double lon); 
	double paksz(double ang, long *iflg = 0);
	void sphdz(long isph, double *parm, double *r_major, double *r_minor, double *radius);

	int alberinvint(double r_maj,		// major axis
					   double r_min,		// minor axis 
					   double lat1,			// first standard parallel
					   double lat2,			// second standard parallel 
					   double lon0,			// center longitude
					   double lat0,			// center lattitude 
					   double false_east,	// x offset in meters
					   double false_north);	// y offset in meters

	int alberinv(double x, double y, double *lon, double *lat);
	int utmforint(double r_maj, double r_min, double scale_fact, long zone);
	int utmfor(double lon, double lat, double *x, double *y);
	int alberforint(double r_maj,	// major axis
					 double r_min,	// minor axis
					 double lat1,	// first standard parallel
					 double lat2,	// second standard parallel
					 double lon0,	// center longitude
					 double lat0,	// center lattitude
					 double false_east,	// x offset in meters
					 double false_north); // y offset in meters
	int alberfor(double lon, double lat, double *x, double *y);

private:
   // Class shared data
   long id;
   long inzone;
   long iter;			                  /* First time flag		*/
   long inpj[MAXPROJ + 1];		         /* input projection array	*/
   long indat[MAXPROJ + 1];		      /* input dataum array		*/
   long inzn[MAXPROJ + 1];		         /* input zone array		*/
   double pdin[MAXPROJ + 1][COEFCT];   /* input projection parm array	*/
   long outpj[MAXPROJ + 1];		      /* output projection array	*/
   long outdat[MAXPROJ + 1];	         /* output dataum array		*/
   long outzn[MAXPROJ + 1];		      /* output zone array		*/
   double pdout[MAXPROJ+1][COEFCT];    /* output projection parm array	*/

   double r_major;                     /* major axis                   */
   double r_minor;                     /* minor axis                   */
   double center_lon;                  /* center longituted            */
   double center_lat;                  /* cetner latitude              */
   double ns;                          /* ratio of angle between meridian*/
   double f0;                          /* flattening of ellipsoid      */
   double rh;                          /* height above ellipsoid       */
   double false_easting;               /* x offset in meters           */
   double false_northing;		         /* y offset in meters		*/

   double scale_factor;                /* scale factor                         */
   double lon_center;                  /* Center longitude (projection center) */
   double lat_origin;                  /* center latitude                      */
   double e0,e1,e2,e3;                 /* eccentricity constants               */
   double e,es,esp;                    /* eccentricity constants               */
   double ml0;                         /* small value m                        */
   long ind;		                     /* sphere flag value			*/

   double sin_p20,cos_p20;             /* sin and cos values			*/
   double bl;
   double al;
   double d, ts, c;
   double el, u;
   double singam,cosgam;
   double sinaz,cosaz;

   double lon_origin;	               /* center longitude			*/
   double ns0;                         /* ratio between meridians              */

};

#endif   // #ifndef PROJSP_H