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

// GeoMag.cpp



/* PROGRAM MAGPOINT (GEOMAG DRIVER) */

#include "Stdafx.h"

#ifdef _WIN32
#include "..\FvCore\Include\Registry.h"
#else
#include <stdlib.h>
#include <time.h>
#endif
#include <math.h>
#include <string>
#include "geo_tool.h"

#define KILOMETERS_per_FOOT      (3.048e-4)

static BOOL mag_file_loaded = FALSE;

typedef struct
{
   int n;
   int m;
   double gnm;
   double hnm;
   double dgnm;
   double dhnm;
} mag_param_t;

#define MAX_ORD 90


//************************************************************************
//************************************************************************

// load the world magnetic model file
static BOOL load_mag_file(double *epoch, char *model, int model_len, mag_param_t *mag)
{
   FILE *in = NULL;
   char buf[121];
   BOOL notdone;
   BOOL ok;
   std::string path;
   int cnt, items;
   char *read_ok;
   int n;
   int m;
   float gnm;
   float hnm;
   float dgnm;
   float dhnm;

#ifndef _WIN32
   // POSIX port: the geodata directory comes from the FVW_GEODATA_DIR
   // environment variable instead of the registry search below.
   {
      const char* geodata_dir = getenv( "FVW_GEODATA_DIR" );
      if ( geodata_dir == NULL )
      {
         OutputDebugString( "load_mag_file() - ERROR:  FVW_GEODATA_DIR not set\n" );
         return FALSE;
      }
      path = geodata_dir;
      if ( path.length() > 0
         && path[path.length() - 1] != '/' && path[path.length() - 1] != '\\' )
         path += "/";
   }
#else
   //read the registry to get the geodata directory
   static const struct GeoDataInfo
   {
      LPCSTR pszKey;
      LPCSTR pszValue;
      LPCSTR pszSubdirectory;
   } aGeoDataInfo[] =
   {
      { "Software\\XPlan\\FalconView\\Main", "ReadWriteAppData","\\..\\GeoData\\" },
      { "Software\\JMPS",              "DataDir",  "\\GeoData\\" },
      { "Software\\JMPS\\FW",          "DataDir",  "\\GeoData\\" },
      { NULL }
   };
   const GeoDataInfo* pGeoDataInfo = aGeoDataInfo;

   UCHAR buffer[MAX_PATH];
   do
   {
      if ( pGeoDataInfo->pszKey == NULL )
      {
         ATLTRACE( _T("load_mag_file() - ERROR:  Could not find registry key\n") );
         return FALSE;
      }

      DWORD dwType;
      DWORD dwBufferSize = MAX_PATH;
      if ( SUCCESS == reg::read_registry( HKEY_LOCAL_MACHINE, pGeoDataInfo->pszKey,
         pGeoDataInfo->pszValue, &dwType, (PUCHAR) &buffer, &dwBufferSize ) )
      {
         //Check type to see that it was a string
         if ( dwType == REG_SZ )
            break;

         AfxMessageBox( _T("load_mag_file() - ERROR:  Registry value type error.") );
         return FALSE;
      }
      pGeoDataInfo++;
   } while ( TRUE ); // Registry search loop

   path = (const char*) buffer;
   path += pGeoDataInfo->pszSubdirectory;
#endif  // _WIN32


   cnt = 0;

   // First look for wmm.dat (2005)
   fopen_s( &in, (path + "wmm.dat").c_str(), "rt");
   if (in == NULL)
      return FALSE;

   fgets(buf, 80, in);
   sscanf_s(buf,"%f%s", epoch, model, model_len);

   // fix the epoch
   char sline[21];
   strncpy_s(sline, buf, 10);
   sline[10] = '\0';
   *epoch = atof(sline);

   ok = TRUE;
   notdone = TRUE;
   while (notdone)
   {
      read_ok = fgets(buf, 80, in);
      if (read_ok)
      {
         items = sscanf_s(buf, "%d%d%f%f%f%f", &n, &m, &gnm, &hnm, &dgnm, &dhnm);
         if (items == 6)
         {
            mag[cnt].n = n;
            mag[cnt].m = m;
            mag[cnt].gnm = gnm;
            mag[cnt].hnm = hnm;
            mag[cnt].dgnm = dgnm;
            mag[cnt].dhnm = dhnm;
            cnt++;
            if (cnt > MAX_ORD)
               notdone = FALSE;
         }
         else
            notdone = FALSE;
      }
      else
      {
         notdone = FALSE;
      }
   }
   fclose(in);
   if (cnt == 90)
      return TRUE;
   return FALSE;
}
// end of load_mag_file

//************************************************************************
//************************************************************************

// load the compiled-in World Magnetic Model (WMM2025, valid 2025.0-2030.0);
// regenerate with port/tools/wmm_cof_to_table.py when a new model is released

static BOOL load_mem_mag_data(double *epoch, char *model, int model_len, mag_param_t *mag)
{
   mag_param_t mag2[MAX_ORD] =
   {
      {  1,  0,  -29351.8,       0.0,       12.0,        0.0},
      {  1,  1,   -1410.8,    4545.4,        9.7,      -21.5},
      {  2,  0,   -2556.6,       0.0,      -11.6,        0.0},
      {  2,  1,    2951.1,   -3133.6,       -5.2,      -27.7},
      {  2,  2,    1649.3,    -815.1,       -8.0,      -12.1},
      {  3,  0,    1361.0,       0.0,       -1.3,        0.0},
      {  3,  1,   -2404.1,     -56.6,       -4.2,        4.0},
      {  3,  2,    1243.8,     237.5,        0.4,       -0.3},
      {  3,  3,     453.6,    -549.5,      -15.6,       -4.1},
      {  4,  0,     895.0,       0.0,       -1.6,        0.0},
      {  4,  1,     799.5,     278.6,       -2.4,       -1.1},
      {  4,  2,      55.7,    -133.9,       -6.0,        4.1},
      {  4,  3,    -281.1,     212.0,        5.6,        1.6},
      {  4,  4,      12.1,    -375.6,       -7.0,       -4.4},
      {  5,  0,    -233.2,       0.0,        0.6,        0.0},
      {  5,  1,     368.9,      45.4,        1.4,       -0.5},
      {  5,  2,     187.2,     220.2,        0.0,        2.2},
      {  5,  3,    -138.7,    -122.9,        0.6,        0.4},
      {  5,  4,    -142.0,      43.0,        2.2,        1.7},
      {  5,  5,      20.9,     106.1,        0.9,        1.9},
      {  6,  0,      64.4,       0.0,       -0.2,        0.0},
      {  6,  1,      63.8,     -18.4,       -0.4,        0.3},
      {  6,  2,      76.9,      16.8,        0.9,       -1.6},
      {  6,  3,    -115.7,      48.8,        1.2,       -0.4},
      {  6,  4,     -40.9,     -59.8,       -0.9,        0.9},
      {  6,  5,      14.9,      10.9,        0.3,        0.7},
      {  6,  6,     -60.7,      72.7,        0.9,        0.9},
      {  7,  0,      79.5,       0.0,       -0.0,        0.0},
      {  7,  1,     -77.0,     -48.9,       -0.1,        0.6},
      {  7,  2,      -8.8,     -14.4,       -0.1,        0.5},
      {  7,  3,      59.3,      -1.0,        0.5,       -0.8},
      {  7,  4,      15.8,      23.4,       -0.1,        0.0},
      {  7,  5,       2.5,      -7.4,       -0.8,       -1.0},
      {  7,  6,     -11.1,     -25.1,       -0.8,        0.6},
      {  7,  7,      14.2,      -2.3,        0.8,       -0.2},
      {  8,  0,      23.2,       0.0,       -0.1,        0.0},
      {  8,  1,      10.8,       7.1,        0.2,       -0.2},
      {  8,  2,     -17.5,     -12.6,        0.0,        0.5},
      {  8,  3,       2.0,      11.4,        0.5,       -0.4},
      {  8,  4,     -21.7,      -9.7,       -0.1,        0.4},
      {  8,  5,      16.9,      12.7,        0.3,       -0.5},
      {  8,  6,      15.0,       0.7,        0.2,       -0.6},
      {  8,  7,     -16.8,      -5.2,       -0.0,        0.3},
      {  8,  8,       0.9,       3.9,        0.2,        0.2},
      {  9,  0,       4.6,       0.0,       -0.0,        0.0},
      {  9,  1,       7.8,     -24.8,       -0.1,       -0.3},
      {  9,  2,       3.0,      12.2,        0.1,        0.3},
      {  9,  3,      -0.2,       8.3,        0.3,       -0.3},
      {  9,  4,      -2.5,      -3.3,       -0.3,        0.3},
      {  9,  5,     -13.1,      -5.2,        0.0,        0.2},
      {  9,  6,       2.4,       7.2,        0.3,       -0.1},
      {  9,  7,       8.6,      -0.6,       -0.1,       -0.2},
      {  9,  8,      -8.7,       0.8,        0.1,        0.4},
      {  9,  9,     -12.9,      10.0,       -0.1,        0.1},
      { 10,  0,      -1.3,       0.0,        0.1,        0.0},
      { 10,  1,      -6.4,       3.3,        0.0,        0.0},
      { 10,  2,       0.2,       0.0,        0.1,       -0.0},
      { 10,  3,       2.0,       2.4,        0.1,       -0.2},
      { 10,  4,      -1.0,       5.3,       -0.0,        0.1},
      { 10,  5,      -0.6,      -9.1,       -0.3,       -0.1},
      { 10,  6,      -0.9,       0.4,        0.0,        0.1},
      { 10,  7,       1.5,      -4.2,       -0.1,        0.0},
      { 10,  8,       0.9,      -3.8,       -0.1,       -0.1},
      { 10,  9,      -2.7,       0.9,       -0.0,        0.2},
      { 10, 10,      -3.9,      -9.1,       -0.0,       -0.0},
      { 11,  0,       2.9,       0.0,        0.0,        0.0},
      { 11,  1,      -1.5,       0.0,       -0.0,       -0.0},
      { 11,  2,      -2.5,       2.9,        0.0,        0.1},
      { 11,  3,       2.4,      -0.6,        0.0,       -0.0},
      { 11,  4,      -0.6,       0.2,        0.0,        0.1},
      { 11,  5,      -0.1,       0.5,       -0.1,       -0.0},
      { 11,  6,      -0.6,      -0.3,        0.0,       -0.0},
      { 11,  7,      -0.1,      -1.2,       -0.0,        0.1},
      { 11,  8,       1.1,      -1.7,       -0.1,       -0.0},
      { 11,  9,      -1.0,      -2.9,       -0.1,        0.0},
      { 11, 10,      -0.2,      -1.8,       -0.1,        0.0},
      { 11, 11,       2.6,      -2.3,       -0.1,        0.0},
      { 12,  0,      -2.0,       0.0,        0.0,        0.0},
      { 12,  1,      -0.2,      -1.3,        0.0,       -0.0},
      { 12,  2,       0.3,       0.7,       -0.0,        0.0},
      { 12,  3,       1.2,       1.0,       -0.0,       -0.1},
      { 12,  4,      -1.3,      -1.4,       -0.0,        0.1},
      { 12,  5,       0.6,      -0.0,       -0.0,       -0.0},
      { 12,  6,       0.6,       0.6,        0.1,       -0.0},
      { 12,  7,       0.5,      -0.1,       -0.0,       -0.0},
      { 12,  8,      -0.1,       0.8,        0.0,        0.0},
      { 12,  9,      -0.4,       0.1,        0.0,       -0.0},
      { 12, 10,      -0.2,      -1.0,       -0.1,       -0.0},
      { 12, 11,      -1.3,       0.1,       -0.0,        0.0},
      { 12, 12,      -0.7,       0.2,       -0.1,       -0.1}
   };

   memcpy(mag, mag2, sizeof(mag_param_t) * MAX_ORD);

   *epoch = 2025.0;
   strcpy_s(model, model_len, "WMM-2025");

   return TRUE;
}
// end of load_mem_mag_data

//************************************************************************
//************************************************************************


static int E0000(int IENTRY, int *maxdeg, double alt, double glat, double glon,
   double time, double *dec, double *dip, double *ti, double *gv)

{
   static mag_param_t mag[MAX_ORD];
   static mag_param_t mag2[MAX_ORD];
   static int maxord,i,icomp,n,m,j,D1,D2,D3,D4;
   static double c[13][13],cd[13][13],tc[13][13],dp[13][13],snorm[169],
      sp[13],cp[13],fn[13],fm[13],pp[13],k[13][13],pi,dtr,a,b,re,
      a2,b2,c2,a4,b4,c4,epoch,gnm,hnm,dgnm,dhnm,flnmj,otime,oalt,
      olat,olon,dt,rlon,rlat,srlon,srlat,crlon,crlat,srlat2,
      crlat2,q,q1,q2,ct,st,r2,r,d,ca,sa,aor,ar,br,bt,bp,bpp,
      par,temp1,temp2,parp,bx,by,bz,bh;
   const int MODEL_LEN = 20;
   static char model[MODEL_LEN], c_str[81], c_new[5];
   static double *p = snorm;

   switch(IENTRY){case 0: goto GEOMAG; case 1: goto GEOMG1;}

GEOMAG:
   /* INITIALIZE CONSTANTS */
   maxord = *maxdeg;
   if (maxord > MAX_ORD)
      maxord = MAX_ORD;

   sp[0] = 0.0;
   cp[0] = *p = pp[0] = 1.0;
   dp[0][0] = 0.0;
   a = 6378.137;
   b = 6356.7523142;
   re = 6371.2;
   a2 = a*a;
   b2 = b*b;
   c2 = a2-b2;
   a4 = a2*a2;
   b4 = b2*b2;
   c4 = a4 - b4;

   /* READ WORLD MAGNETIC MODEL SPHERICAL HARMONIC COEFFICIENTS */
   c[0][0] = 0.0;
   cd[0][0] = 0.0;
   //      epoch = 1995.0;
   //      strcpy(model, "WMM-95");
   if (!mag_file_loaded)
   {
      if (load_mag_file(&epoch, model, MODEL_LEN, mag2))
         memcpy(mag, mag2, sizeof(mag_param_t) * MAX_ORD);
      else
      {
         load_mem_mag_data(&epoch, model, MODEL_LEN, mag);
         ERR_report("error reading geomag model file");
      }
      mag_file_loaded = TRUE;
   }

   // get the coefficients
   for (i=0; i<90; i++)
   {
      n =    mag[i].n;
      m =    mag[i].m;
      gnm =  mag[i].gnm;
      hnm =  mag[i].hnm;
      dgnm = mag[i].dgnm;
      dhnm = mag[i].dhnm;

      if (m <= n)
      {
         c[m][n] = gnm;
         cd[m][n] = dgnm;
         if (m != 0)
         {
            c[n][m-1] = hnm;
            cd[n][m-1] = dhnm;
         }
      }
   }

   /* CONVERT SCHMIDT NORMALIZED GAUSS COEFFICIENTS TO UNNORMALIZED */
   *snorm = 1.0;

   /*
   COMPILER WRINKLE
   This original loop control causes the compiler to error
   when the maximize speed option is active in a Release compile
   It doesn't like the n=1 for some inexplicable reason.
   //    for (n=1; n<=maxord; n++) 
   */
   if (n!=1)
      n=1;
   for (; n<=maxord; n++) 
   {
      *(snorm+n) = *(snorm+n-1)*(double)(2*n-1)/(double)n;
      j = 2;
      for (m=0,D1=1,D2=(n-m+D1)/D1; D2>0; D2--,m+=D1) 
      {
         k[m][n] = (double)(((n-1)*(n-1))-(m*m))/(double)((2*n-1)*(2*n-3));
         if (m > 0) 
         {
            flnmj = (double)((n-m+1)*j)/(double)(n+m);
            *(snorm+n+m*13) = *(snorm+n+(m-1)*13)*sqrt(flnmj);
            j = 1;
            c[n][m-1] = *(snorm+n+m*13)*c[n][m-1];
            cd[n][m-1] = *(snorm+n+m*13)*cd[n][m-1];
         }
         c[m][n] = *(snorm+n+m*13)*c[m][n];
         cd[m][n] = *(snorm+n+m*13)*cd[m][n];
      }
      fn[n] = (double)(n+1);
      fm[n] = (double)n;
   }
   k[1][1] = 0.0;

   otime = oalt = olat = olon = -1000.0;
   return 1;

   /*************************************************************************/

GEOMG1:

   dt = time - epoch;
   pi = 3.14159265359;
   dtr = pi/180.0;
   rlon = glon*dtr;
   rlat = glat*dtr;
   srlon = sin(rlon);
   srlat = sin(rlat);
   crlon = cos(rlon);
   crlat = cos(rlat);
   srlat2 = srlat*srlat;
   crlat2 = crlat*crlat;
   sp[1] = srlon;
   cp[1] = crlon;

   /* CONVERT FROM GEODETIC COORDS. TO SPHERICAL COORDS. */
   if (alt != oalt || glat != olat) 
   {
      q = sqrt(a2-c2*srlat2);
      q1 = alt*q;
      q2 = ((q1+a2)/(q1+b2))*((q1+a2)/(q1+b2));
      ct = srlat/sqrt(q2*crlat2+srlat2);
      st = sqrt(1.0-(ct*ct));
      r2 = (alt*alt)+2.0*q1+(a4-c4*srlat2)/(q*q);
      r = sqrt(r2);
      d = sqrt(a2*crlat2+b2*srlat2);
      ca = (alt+d)/r;
      sa = c2*crlat*srlat/(r*d);
   }
   if (glon != olon) 
   {
      for (m=2; m<=maxord; m++) 
      {
         sp[m] = sp[1]*cp[m-1]+cp[1]*sp[m-1];
         cp[m] = cp[1]*cp[m-1]-sp[1]*sp[m-1];
      }
   }
   aor = re/r;
   ar = aor*aor;
   br = bt = bp = bpp = 0.0;
   for (n=1; n<=maxord; n++) 
   {
      ar = ar*aor;
      for (m=0,D3=1,D4=(n+m+D3)/D3; D4>0; D4--,m+=D3) 
      {
         /*
         COMPUTE UNNORMALIZED ASSOCIATED LEGENDRE POLYNOMIALS
         AND DERIVATIVES VIA RECURSION RELATIONS
         */
         if (alt != oalt || glat != olat) 
         {
            if (n == m) 
            {
               *(p+n+m*13) = st**(p+n-1+(m-1)*13);
               dp[m][n] = st*dp[m-1][n-1]+ct**(p+n-1+(m-1)*13);
               goto S50;
            }
            if (n == 1 && m == 0) 
            {
               *(p+n+m*13) = ct**(p+n-1+m*13);
               dp[m][n] = ct*dp[m][n-1]-st**(p+n-1+m*13);
               goto S50;
            }
            if (n > 1 && n != m) 
            {
               if (m > n-2) 
                  *(p+n-2+m*13) = 0.0;
               if (m > n-2) 
                  dp[m][n-2] = 0.0;
               *(p+n+m*13) = ct**(p+n-1+m*13)-k[m][n]**(p+n-2+m*13);
               dp[m][n] = ct*dp[m][n-1] - st**(p+n-1+m*13)-k[m][n]*dp[m][n-2];
            }
         }
S50:
         /*
         TIME ADJUST THE GAUSS COEFFICIENTS
         */
         if (time != otime) 
         {
            tc[m][n] = c[m][n]+dt*cd[m][n];
            if (m != 0) 
               tc[n][m-1] = c[n][m-1]+dt*cd[n][m-1];
         }
         /*
         ACCUMULATE TERMS OF THE SPHERICAL HARMONIC EXPANSIONS
         */
         par = ar**(p+n+m*13);
         if (m == 0) 
         {
            temp1 = tc[m][n]*cp[m];
            temp2 = tc[m][n]*sp[m];
         }
         else 
         {
            temp1 = tc[m][n]*cp[m]+tc[n][m-1]*sp[m];
            temp2 = tc[m][n]*sp[m]-tc[n][m-1]*cp[m];
         }
         bt = bt-ar*temp1*dp[m][n];
         bp += (fm[m]*temp2*par);
         br += (fn[n]*temp1*par);
         /*
         SPECIAL CASE:  NORTH/SOUTH GEOGRAPHIC POLES
         */
         if (st == 0.0 && m == 1) 
         {
            if (n == 1) 
               pp[n] = pp[n-1];
            else 
               pp[n] = ct*pp[n-1]-k[m][n]*pp[n-2];
            parp = ar*pp[n];
            bpp += (fm[m]*temp2*parp);
         }
      }
   }
   if (st == 0.0) 
      bp = bpp;
   else 
      bp /= st;
   /*
   ROTATE MAGNETIC VECTOR COMPONENTS FROM SPHERICAL TO
   GEODETIC COORDINATES
   */
   bx = -bt*ca-br*sa;
   by = bp;
   bz = bt*sa-br*ca;

   //    COMPUTE DECLINATION (DEC), INCLINATION (DIP) AND
   //    TOTAL INTENSITY (TI)
   bh = sqrt((bx*bx)+(by*by));
   *ti = sqrt((bh*bh)+(bz*bz));
   *dec = atan2(by,bx)/dtr;
   *dip = atan2(bz,bh)/dtr;
   /*
   COMPUTE MAGNETIC GRID VARIATION IF THE CURRENT
   GEODETIC POSITION IS IN THE ARCTIC OR ANTARCTIC
   (I.E. GLAT > +55 DEGREES OR GLAT < -55 DEGREES)

   OTHERWISE, SET MAGNETIC GRID VARIATION TO -999.0
   */
   *gv = -999.0;
   if (fabs(glat) >= 55.) 
   {
      if (glat > 0.0 && glon >= 0.0) *gv = *dec-glon;
      if (glat > 0.0 && glon < 0.0) *gv = *dec+fabs(glon);
      if (glat < 0.0 && glon >= 0.0) *gv = *dec+glon;
      if (glat < 0.0 && glon < 0.0) *gv = *dec-fabs(glon);
      if (*gv > +180.0) *gv -= 360.0;
      if (*gv < -180.0) *gv += 360.0;
   }
   otime = time;
   oalt = alt;
   olat = glat;
   olon = glon;
   return 1;
}
// end of E0000

/*************************************************************************/

int geomag(int *maxdeg)
{
   return E0000(0,maxdeg,0.0,0.0,0.0,0.0,NULL,NULL,NULL,NULL);
}

/*************************************************************************/

int geomg1(double alt, double glat, double glon, double time,
   double *dec, double *dip, double *ti, double *gv)
{
   return E0000(1, NULL, alt, glat, glon, time, dec, dip, ti, gv);
}

/*************************************************************************/

int GEO_magnetic_variation(double dlat, double dlon, 
   int year, // e.g.  1995 
   int month, // e.g.   3 (= March) 
   int altitude,               // IN  - altitude in meters
   double *magvar)
{ 
   // Mid-month; the Windows code used year + month/12, a month late.
   return GEO_magnetic_variation_at(dlat, dlon,
      (double) year + ((double) month - 0.5) / 12.0, (double) altitude, magvar);
}
// end of GEO_magnetic_variation

/*************************************************************************/

int GEO_magnetic_variation_at(double dlat, double dlon,
   double decimal_year,        // IN  - e.g. 2025.5
   double altitude,            // IN  - altitude in meters
   double *magvar)
{
   //    extern int geomag(), geomg1();
   static int maxdeg;
   static double altm;
   static double ati, adec, adip;
   static double alt, time, dec, dip, ti, gv;
   static double time1, dec1, dip1, ti1;
   static double time2, dec2, dip2, ti2;
   double x1,x2,y1,y2,z1,z2,h1,h2;
   double ax,ay,az,ah;
   double rTd=0.017453292;
   int rslt;

   /* INITIALIZE GEOMAG ROUTINE */
   maxdeg = 12;

   rslt = geomag(&maxdeg);
   if (!rslt)
      return FAILURE;

   // convert meters to kilometers
   alt = altitude / 1000.0;

   time = decimal_year;

   rslt = geomg1(alt,dlat,dlon,time,&dec,&dip,&ti,&gv);
   if (!rslt)
      return FAILURE;

   time1 = time;
   dec1 = dec;
   dip1 = dip;
   ti1 = ti;
   time = time1 + 1.0;

   rslt = geomg1(alt,dlat,dlon,time,&dec,&dip,&ti,&gv);
   if (!rslt)
      return FAILURE;

   time2 = time;
   dec2 = dec;
   dip2 = dip;
   ti2 = ti;

   /*COMPUTE X, Y, Z, AND H COMPONENTS OF THE MAGNETIC FIELD*/

   x1=ti1*(cos((dec1*rTd))*cos((dip1*rTd)));
   x2=ti2*(cos((dec2*rTd))*cos((dip2*rTd)));
   y1=ti1*(cos((dip1*rTd))*sin((dec1*rTd)));
   y2=ti2*(cos((dip2*rTd))*sin((dec2*rTd)));
   z1=ti1*(sin((dip1*rTd)));
   z2=ti2*(sin((dip2*rTd)));
   h1=ti1*(cos((dip1*rTd)));
   h2=ti2*(cos((dip2*rTd)));

   /*  COMPUTE ANNUAL CHANGE FOR TOTAL INTENSITY  */
   ati = ti2 - ti1;

   /*  COMPUTE ANNUAL CHANGE FOR DIP & DEC  */
   adip = (dip2 - dip1) * 60.;
   adec = (dec2 - dec1) * 60.;


   /*  COMPUTE ANNUAL CHANGE FOR X, Y, Z, AND H */
   ax = x2-x1;
   ay = y2-y1;
   az = z2-z1;
   ah = h2-h1;
   //      clrscr ();

   *magvar = dec1;

   return  SUCCESS;

}
// end of GEO_magnetic_variation_at

/*************************************************************************/
/*************************************************************************/

int GEO_current_magnetic_variation(double dlat, double dlon, 
   int altitude,  // IN  - altitude in meters
   double *magvar)
{ 
   int year, month, rslt;
#ifdef _WIN32
   SYSTEMTIME time;

   GetSystemTime(&time);
   year = time.wYear;
   month = time.wMonth;
#else
   // POSIX port: UTC date, matching GetSystemTime
   time_t now = ::time(NULL);
   struct tm utc;
   gmtime_r(&now, &utc);
   year = utc.tm_year + 1900;
   month = utc.tm_mon + 1;
#endif

   rslt = GEO_magnetic_variation(dlat, dlon, year, month, altitude, magvar);

   return  rslt;

}  // end of GEO_current_magnetic_variation()

// End of GeoMag.cpp


