// Copyright (c) 1994-2010 Georgia Tech Research Corporation, Atlanta, GA
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

// Transform.cpp : Implementation of CTransform



#include "stdafx.h"

#include <float.h>
#include <math.h>
#include "common.h"
#include "err.h"
#include "file.h"
#include "mem.h"
#include "Transfrm.h"
#include "svd.h"
#include "geo_tool_d.h"
#ifdef _WIN32
#include "DTEDManager.h"   // For elevation adjustments
#else
#include "fv_dted_instance_posix.h"
#endif
#ifdef _DEBUG
#  include "crtdbg.h"
#endif

static const int SINGULAR_MATRIX = +1; // Local status code
const DOUBLE CTransform::m_aprox0 = 1.0e-12;

// ****************************************************************
// ****************************************************************

CTransform::CTransform( BOOL bGeoMode, TransformationModeEnum eTransformMode ) :
   m_bGeoMode( bGeoMode ), m_eTransformMode( eTransformMode )
{
   m_num_tiepoints = 0;
   m_num_rows = 0;
   clear( );
}  // End of CTransform()



// ****************************************************************
// ****************************************************************

CTransform::~CTransform( )
{
}  // End of ~CTransform()



// ****************************************************************
// ****************************************************************

void CTransform::clear( )
{
   // This function clears the object
   m_defined = FALSE;
   m_idl_cross = FALSE;
   m_bLonUnwrapMode = TRUE;

   m_cPolynomialTerms = 0;
   m_dLatitudeOrigin = m_dLongitudeOrigin = m_dHeightOrigin = 0.0;
}  // End of clear()



// ****************************************************************
// ****************************************************************

VOID CTransform::InitMem( INT cTiepoints, INT cCoefs )
{
   if ( cTiepoints != m_num_tiepoints )
   {
      m_num_tiepoints = cTiepoints;

      ResetDOUBLEPtr( m_apdTiepointsX, new DOUBLE[ m_num_tiepoints ] );
      m_tiepoint_x = m_apdTiepointsX.get();

      ResetDOUBLEPtr( m_apdTiepointsY, new DOUBLE[ m_num_tiepoints ] );
      m_tiepoint_y = m_apdTiepointsY.get();

      ResetDOUBLEPtr( m_apdTiepointsLat, new DOUBLE[ m_num_tiepoints ] );
      m_tiepoint_latitude = m_apdTiepointsLat.get();

      ResetDOUBLEPtr( m_apdTiepointsLon, new DOUBLE[ m_num_tiepoints ] );
      m_tiepoint_longitude = m_apdTiepointsLon.get();
   }

   if ( cCoefs != m_num_rows )
   {
      m_num_rows = cCoefs;

      ResetDOUBLEPtr( m_apdCoefsX, new DOUBLE[ m_num_rows ] );
      m_x_coef = m_apdCoefsX.get();

      ResetDOUBLEPtr( m_apdCoefsY, new DOUBLE[ m_num_rows ] );
      m_y_coef = m_apdCoefsY.get();

      ResetDOUBLEPtr( m_apdCoefsLat, new DOUBLE[ m_num_rows ] );
      m_latitude_coef = m_apdCoefsLat.get();

      ResetDOUBLEPtr( m_apdCoefsLon, new DOUBLE[ m_num_rows ] );
      m_longitude_coef = m_apdCoefsLon.get();
   }
}


// ****************************************************************
// ****************************************************************

int CTransform::define_tiepoints( int num_tiepoints, const double *x,
   const double *y, const double *latitude, const double *longitude )
{
   // First clear the object
   clear( );
   
   // Depends on transform mode
   switch ( m_eTransformMode )
   {
      case TRANSFORM_MODE_NORMAL:
      {
         return define_tiepoints_original( num_tiepoints,
            x, y, latitude, longitude );
      }
      
      case TRANSFORM_MODE_ICHIPB:
      case TRANSFORM_MODE_POLYNOMIAL:
      case TRANSFORM_MODE_SVD_POLYNOMIAL:
      {
         return DefineTiepointsPolynomial( num_tiepoints,
            x, y, latitude, longitude );
      }
      
      default:
         ASSERT( FALSE );  // Invalid mode
         break;            // Fail
      
   }  // Transformation mode
   return FAILURE;
}  // End of define_tiepoints()



// ****************************************************************
// ****************************************************************

int CTransform::define_tiepoints_original( int num_tiepoints,
   const double *x, const double *y,
   const double *latitude, const double *longitude )
{
   // this function is used to define the tiepoints
   int i, j, index;
   double dx, dy, r_sqrd;
   DoublePtr apdTmpMatrix, apdFwdMatrix, apdInvMatrix;
   double *tmp_matrix;
   double *fwd_matrix;
   double *inv_matrix;
	double min_lon, max_lon;
	int imin, imax;
	double xmin, xmax;

   // first clear the object
   clear( );

   // must be at least 3 tiepoints
   if( num_tiepoints < 3 )
      goto FAIL;

   // find the min/max longitude
   min_lon = longitude[0];
   max_lon = longitude[0];
   xmin = x[0];
   xmax = x[0];
   imin = imax = 0;
   for (i=0; i<num_tiepoints; i++)
   {
	   if (longitude[i] < min_lon)
	   {
		   min_lon = longitude[i];
		   xmin = x[i];
		   imin = i;
	   }
	   if (longitude[i] > max_lon)
	   {
		   max_lon = longitude[i];
		   xmax = x[i];
		   imax = i;
	   }
   }

	if (xmin > xmax)  // crosses IDL
		m_idl_cross = TRUE;
	else
		m_idl_cross = FALSE;

   INT cElements;

   InitMem( num_tiepoints, num_tiepoints + 3 );

   // copy tiepoints
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      m_tiepoint_x[i] = x[i];
      m_tiepoint_y[i] = y[i];
      m_tiepoint_latitude[i] = latitude[i];
      m_tiepoint_longitude[i] = longitude[i];
	  if (m_idl_cross)
	  {
		  if (longitude[i] < 0.0)
			  m_tiepoint_longitude[i] += 360.0;
	  }
   }

   // allocate temporary memory for calculating matrices
   cElements = m_num_rows * m_num_rows;

   ResetDOUBLEPtr( apdFwdMatrix, new DOUBLE[ cElements ] );
   fwd_matrix = apdFwdMatrix.get();

   ResetDOUBLEPtr( apdInvMatrix, new DOUBLE[ cElements ] );
   inv_matrix = apdInvMatrix.get();

   ResetDOUBLEPtr( apdTmpMatrix, new DOUBLE[ cElements ] );
   tmp_matrix = apdTmpMatrix.get();

   // calculate inverse of inverse transformation matrix
   index = 0;
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      for( j = 0; j < m_num_tiepoints; j++ )
      {
         if( j == i )
         {
            tmp_matrix[index] = 0.0;
         }
         else
         {
            dx = m_tiepoint_x[j] - m_tiepoint_x[i];
            dy = m_tiepoint_y[j] - m_tiepoint_y[i];
            r_sqrd = dx*dx + dy*dy;
            // check for coincident points
            if( r_sqrd < m_aprox0 )
               goto FAIL;
            tmp_matrix[index] = r_sqrd * log( r_sqrd );
         }
         index++;
      }

      tmp_matrix[index] = 1.0;
      index++;
      tmp_matrix[index] = m_tiepoint_x[i];
      index++;
      tmp_matrix[index] = m_tiepoint_y[i];
      index++;
   }

   i = m_num_tiepoints;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = 1.0;
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   
   i = m_num_tiepoints + 1;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_x[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   
   i = m_num_tiepoints + 2;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_y[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   
   // now invert the temporary matrix to obtain the inverse transform matrix
   // which allows calculation of latitude and longitude from x and y   
   if( invert_matrix( tmp_matrix, m_num_rows, inv_matrix ) != SUCCESS )
      goto FAIL;

   // now calculate the coefficients for latitude and longitude
   index = 0;
   for( i = 0; i < m_num_rows; i++ )
   {
      m_latitude_coef[i] = 0.0;
      m_longitude_coef[i] = 0.0;
      
      for( j = 0; j < m_num_tiepoints; j++ )
      {
         m_latitude_coef[i] += inv_matrix[index] * m_tiepoint_latitude[j];
         m_longitude_coef[i] += inv_matrix[index] * m_tiepoint_longitude[j];
         index++;
      }
      index += 3;
   }


   // calculate inverse of forward transformation matrix
   index = 0;
   for( i = 0; i < m_num_tiepoints; i++ )
   {
      for( j = 0; j < m_num_tiepoints; j++ )
      {
         if( j == i )
         {
            tmp_matrix[index] = 0.0;
         }
         else
         {
            dx = m_tiepoint_longitude[j] - m_tiepoint_longitude[i];
            dy = m_tiepoint_latitude[j] - m_tiepoint_latitude[i];
            r_sqrd = dx*dx + dy*dy;
            // check for coincident points
            if( r_sqrd < m_aprox0 )
               goto FAIL;
            tmp_matrix[index] = r_sqrd * log( r_sqrd );
         }
         index++;
      }

      tmp_matrix[index] = 1.0;
      index++;
      tmp_matrix[index] = m_tiepoint_longitude[i];
      index++;
      tmp_matrix[index] = m_tiepoint_latitude[i];
      index++;
   }

   i = m_num_tiepoints;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = 1.0;
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   
   i = m_num_tiepoints + 1;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_longitude[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   
   i = m_num_tiepoints + 2;
   for( j = 0; j < m_num_tiepoints; j++ )
   {
      tmp_matrix[index] = m_tiepoint_latitude[j];
      index++;
   }
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   tmp_matrix[index] = 0.0;
   index++;   
   
   // now invert the temporary matrix to obtain the inverse transform matrix
   // which allows calculation of latitude and longitude from x and y   
   if( invert_matrix( tmp_matrix, m_num_rows, fwd_matrix ) != SUCCESS )
      goto FAIL;

   // now calculate the coefficients for x and y
   index = 0;
   for( i = 0; i < m_num_rows; i++ )
   {
      m_x_coef[i] = 0.0;
      m_y_coef[i] = 0.0;
      
      for( j = 0; j < m_num_tiepoints; j++ )
      {
         m_x_coef[i] += fwd_matrix[index] * m_tiepoint_x[j];
         m_y_coef[i] += fwd_matrix[index] * m_tiepoint_y[j];
         index++;
      }
      index += 3;
   }

   m_defined = TRUE;
   return SUCCESS;

FAIL:
   clear( );
   return FAILURE;
}  // End of define_tiepoints_original()



// ****************************************************************
// ****************************************************************

//
// DefineTiepointsPolynomial()
//
// Note that in "non-Geo" mode, the lat/lon values are treated as simple numerics (u/v)
int CTransform::DefineTiepointsPolynomial( INT cTiepoints,
                           const DOUBLE* pdX, const DOUBLE* pdY,
                           const DOUBLE* pdLatitude, const DOUBLE* pdLongitude )
{
   do          // Once only
   {
      // Must be at least 4 tiepoints
      if ( cTiepoints < 4 )
         break;      // Fail

      // We expect a square matrix of tiepoints with the number of points
      // in one dimension to be the number of terms in the coordinate polynomials
      m_cPolynomialTerms = (INT) sqrt( cTiepoints + 1e-6 );
      INT cRows = m_cPolynomialTerms * m_cPolynomialTerms;

      // Any extra tiepoints are only used if doing a SVD fit
      if ( m_eTransformMode != TRANSFORM_MODE_SVD_POLYNOMIAL )
         cTiepoints = cRows;     // Use only the square part
      
      const INT cTiepointDOUBLEBytes = cTiepoints * sizeof(DOUBLE);
      ASSERT( sizeof(DOUBLE) == sizeof(*pdLongitude) ); // etc.

      InitMem( cTiepoints, cRows );

      // Copy the tiepoints
      memcpy( m_tiepoint_x, pdX, cTiepointDOUBLEBytes );
      memcpy( m_tiepoint_y, pdY, cTiepointDOUBLEBytes );
      
      // Limits for offset and dateline crossing calculations
      DOUBLE dMinLon = +1e30, dMaxLon = -1e30, dMinLat = +1e30, dMaxLat = -1e30;
      INT iTiepoint;
      for ( iTiepoint = 0; iTiepoint < cTiepoints; iTiepoint++ )
      {
         if ( pdLongitude[ iTiepoint ] > dMaxLon )
            dMaxLon = pdLongitude[ iTiepoint ];
         if ( pdLongitude[ iTiepoint ] < dMinLon )
            dMinLon = pdLongitude[ iTiepoint ];
         if ( pdLatitude[ iTiepoint ] > dMaxLat )
            dMaxLat = pdLatitude[ iTiepoint ];
         if ( pdLatitude[ iTiepoint ] < dMinLat )
            dMinLat = pdLatitude[ iTiepoint ];
      }

      // Dateline crossing check
      m_idl_cross = FALSE;    // Assume not geo mode
      if ( m_eTransformMode == TRANSFORM_MODE_POLYNOMIAL
         || m_eTransformMode == TRANSFORM_MODE_SVD_POLYNOMIAL )
      {
         if ( m_bGeoMode )       // If target values are lat/lons rather than u/v
         {
            m_idl_cross= dMaxLon > dMinLon + 180.0;  // TRUE if tiepoints cross dateline
            if ( m_idl_cross )
               dMinLon += 360.0;
         }
         
         // Offset and dateline crossing adjust
         m_dLongitudeOrigin = 0.5 * ( dMinLon + dMaxLon );
         m_dLatitudeOrigin = 0.5 * ( dMinLat + dMaxLat );
      }

      // Copy and maybe offset the tiepoints
      for ( iTiepoint = 0; iTiepoint < cTiepoints; iTiepoint++ )
      {
         m_tiepoint_latitude[ iTiepoint ] = pdLatitude[ iTiepoint ]
            - m_dLatitudeOrigin;
         m_tiepoint_longitude[ iTiepoint ] = pdLongitude[ iTiepoint ]
            + ( ( m_idl_cross && pdLongitude[ iTiepoint ] < 0.0 ) ? 360.0 : 0.0 )
            - m_dLongitudeOrigin;
      }

#if defined CALC_POLY_TEST || defined CALC_LINEAR_TEST
      TRACE( _T(" Latitude coeffs:\n") );
#endif
      // Latitude (or target X) = poly( x, y )
      if ( SUCCESS != CalcPolynomialCoefs(
                           m_eTransformMode == TRANSFORM_MODE_SVD_POLYNOMIAL,
                           m_tiepoint_x, m_tiepoint_y, 
                           m_tiepoint_latitude, m_latitude_coef ) )
         break;      // Fail
      
      // Longitude (or target Y) = poly( x, y )
#if defined CALC_POLY_TEST || defined CALC_LINEAR_TEST
      TRACE( _T(" Longitude coeffs:\n") );
#endif
      if ( SUCCESS != CalcPolynomialCoefs(
                           m_eTransformMode == TRANSFORM_MODE_SVD_POLYNOMIAL,
                           m_tiepoint_x, m_tiepoint_y, 
                           m_tiepoint_longitude, m_longitude_coef ) )
         break;      // Fail

      // X = poly( latitude, longitude )
#if defined CALC_POLY_TEST || defined CALC_LINEAR_TEST
      TRACE( _T(" X coeffs:\n") );
#endif
      if ( SUCCESS != CalcPolynomialCoefs(
                        FALSE,
                        m_tiepoint_latitude, m_tiepoint_longitude, 
                        m_tiepoint_x, m_x_coef ) )
         break;      // Fail
      
#if defined CALC_POLY_TEST || defined CALC_LINEAR_TEST
      INT iT;
      for ( iT = 0; iT < m_num_tiepoints; iT++ )
      {
         DOUBLE* pdCoef = m_x_coef;
         DOUBLE dLonPow = 1.0;
         DOUBLE dX = 0.0;
         for ( INT iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
         {
            DOUBLE dLatPow = 1.0;
            for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            {
               dX += dLonPow * dLatPow * *(pdCoef++);
               dLatPow *= m_tiepoint_latitude[ iT ];
            }
            dLonPow *= m_tiepoint_longitude[ iT ];
         }
         TRACE( _T("Tiepoint #%d, lat =%16.6e, lon =%16.6e, x(targ) =%16.6e, x(calc) =%16.6e\n"),
            iT, m_tiepoint_latitude[ iT ], m_tiepoint_longitude[ iT ],
            m_tiepoint_x[ iT ], dX );
      }
#endif
      // Y = poly( latitude, longitude )
#if defined CALC_POLY_TEST || defined CALC_LINEAR_TEST
      TRACE( _T(" Y coeffs:\n") );
#endif
      if ( SUCCESS != CalcPolynomialCoefs(
                        FALSE,
                        m_tiepoint_latitude, m_tiepoint_longitude, 
                        m_tiepoint_y, m_y_coef ) )
         break;      // Fail
      
#if defined CALC_POLY_TEST || defined CALC_LINEAR_TEST
      for ( iT = 0; iT < m_num_tiepoints; iT++ )
      {
         DOUBLE* pdCoef = m_y_coef;
         DOUBLE dLonPow = 1.0;
         DOUBLE dY = 0.0;
         for ( INT iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
         {
            DOUBLE dLatPow = 1.0;
            for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            {
               dY += dLonPow * dLatPow * *(pdCoef++);
               dLatPow *= m_tiepoint_latitude[ iT ];
            }
            dLonPow *= m_tiepoint_longitude[ iT ];
         }
         TRACE( _T("Tiepoint #%d, lat =%16.6e, lon =%16.6e, y(targ) =%16.6e, y(calc) =%16.6e\n"),
            iT, m_tiepoint_latitude[ iT ], m_tiepoint_longitude[ iT ],
            m_tiepoint_y[ iT ], dY );
      }
#endif
      m_defined = TRUE;
      return SUCCESS;

   } while ( FALSE );      // One-shot "do"

   clear();
   return FAILURE;

}  // End of DefineTiepointsPolynomial()


//
// CalcPolynomialCoeffs() - Fit W to polynomial in U,V
//
// This calculation is always performed using SVD.
// This will reset the CTransform object
//
INT CTransform::CalcPolynomialCoeffs(
                        /*[in]*/ INT cPolynomialTerms,         // Terms in the output polynomial (order + 1 )
                        /*[in]*/ INT cUVWValues,               // Number of W=f(U,W) fit values
                        /*[in]*/ const DOUBLE* pdU,
                        /*[in]*/ const DOUBLE* pdV,
                        /*[in]*/ const DOUBLE* pdW,
                        /*[out,retval]*/ DOUBLE* pdWCoefs )   // Must be [cPolynomialTerms][cPolynomialTerms]
{
   // Check for too few values to fit the requested polynomial degree (under constrained)
   if ( cUVWValues < ( cPolynomialTerms * ( cPolynomialTerms + 1 ) / 2 ) )
   {
      ASSERT( FALSE && "Too few data points to support output degree" );
      return FAILURE;
   }

   clear();

   m_cPolynomialTerms = cPolynomialTerms;
   m_num_rows = cPolynomialTerms * cPolynomialTerms;
   m_num_tiepoints = cUVWValues;

   return CalcPolynomialCoefs( TRUE,   // SVD mode always
                           pdU, pdV, pdW, pdWCoefs );
}


//
// DefineRPC00BTransform() - Define transform in RPC mode
//
// See STDI-002 version 2.1, paragraph 8.2.4 for explanations
//
VOID CTransform::DefineRPC00BTransform(
      const CTREDataRPC00B& tredRPC00B,         // Input image RPC00B struct
      const CTransform& tfrmICHIPB,             // ICHIPB transform
      DOUBLE dXOriginIn, DOUBLE dYOriginIn,     // Origin of transformed image in input coordinates
      DOUBLE dFactor,                           // Subsampling factor (<= 1.0)
      DOUBLE dXYOriginOut )                     // Offset to X or Y origin of output
{
   clear();
   m_dRPC00BLat = m_dRPC00BLon =                // Start a origin for inv_transform() iterations
      m_dRPC00BHgt = 0.0;

   DOUBLE
      dLineOffset = tredRPC00B.dLINE_OFF,
      dSampOffset = tredRPC00B.dSAMP_OFF;

   // Find whether to include ICHIPB information at this time (must be non-rotated)
   DOUBLE dAlmostZero = 1e-8 * tfrmICHIPB.m_dICHIPBCoeffsX[2];    // Would be ~ magnification
   if ( tfrmICHIPB.m_dICHIPBCoeffsX[2] > 0.0                   // Quadrant 1 or 4
      && fabs( tfrmICHIPB.m_dICHIPBCoeffsX[1] ) < dAlmostZero  // No X, Y cross terms
      && fabs( tfrmICHIPB.m_dICHIPBCoeffsY[2] ) < dAlmostZero
      && fabs( tfrmICHIPB.m_dICHIPBCoeffsX[2] - tfrmICHIPB.m_dICHIPBCoeffsY[1] ) 
         < dAlmostZero )   // Isotropic
   {
      m_eTransformMode = TRANSFORM_MODE_RPC00B;

#if 0 && defined _DEBUG
      TRACE( _T("No ICHIPB rotate\n") );
#endif
      
      dLineOffset += ( dXYOriginOut / dFactor ) - dYOriginIn + tfrmICHIPB.m_dICHIPBCoeffsY[0],
      dSampOffset += ( dXYOriginOut / dFactor ) - dXOriginIn + tfrmICHIPB.m_dICHIPBCoeffsX[0];
      dFactor *= tfrmICHIPB.m_dICHIPBCoeffsX[2];   // Additional demagnification
   }
   else     // Rotated and possible scaled
   {
      m_eTransformMode = TRANSFORM_MODE_RPC00B_ICHIPB;      // Need post-RPC00B transform

      // x = a + b*u + c*v
      m_dICHIPBCoeffsX[0] = dXYOriginOut
         + ( dFactor * ( tfrmICHIPB.m_dICHIPBCoeffsX[0] - dXOriginIn ) );
      m_dICHIPBCoeffsX[1] = dFactor * tfrmICHIPB.m_dICHIPBCoeffsX[2];           // 1st index was lat (y)
      m_dICHIPBCoeffsX[2] = dFactor * tfrmICHIPB.m_dICHIPBCoeffsX[1];

      // y = d + e*u + f*v
      m_dICHIPBCoeffsY[0] = dXYOriginOut
         + ( dFactor * ( tfrmICHIPB.m_dICHIPBCoeffsY[0] - dYOriginIn ) );
      m_dICHIPBCoeffsY[1] = dFactor * tfrmICHIPB.m_dICHIPBCoeffsY[2];
      m_dICHIPBCoeffsY[2] = dFactor * tfrmICHIPB.m_dICHIPBCoeffsY[1];
      dFactor = 1.0;          // RPC00B output now is for full image
   }

   // Set reference point of input
   m_dLatitudeOrigin = tredRPC00B.dLAT_OFF;
   m_dLongitudeOrigin = tredRPC00B.dLONG_OFF;
   m_dHeightOrigin = tredRPC00B.dHEIGHT_OFF;
   m_bRPC00BHeightSensitive = tredRPC00B.m_bHeightSensitive;

   m_cPolynomialTerms = 3 + 1;        // 3rd degree in P, L, H
   INT
      cCoeffs = m_cPolynomialTerms * m_cPolynomialTerms * m_cPolynomialTerms,
      cCoeffBytes = cCoeffs * sizeof(DOUBLE);

   INT k;
   for ( k = 0; k < 4; k++ )
   {
      ResetDOUBLEPtr( m_apdRatPolyCoeffs[ k ], new DOUBLE[ cCoeffs ] );  // 3rd power coefficient arrays
      ZeroMemory( m_apdRatPolyCoeffs[ k ].get(), cCoeffBytes );
   }

   // Scale and offset RPC00B polynomial coefficients to get directly back
   // to X and Y image addresses.  For now, assume the height is the height offset
   DOUBLE dPScalePowers[4], dLScalePowers[4], dHScalePowers[4];
   dPScalePowers[0] = dLScalePowers[0] = dHScalePowers[0] = 1.0;
   for ( k = 1; k <= 3; k++ )
   {
      dPScalePowers[ k ] = dPScalePowers[ k - 1 ] * tredRPC00B.dLAT_SCALE;
      dLScalePowers[ k ] = dLScalePowers[ k - 1 ] * tredRPC00B.dLONG_SCALE;
      dHScalePowers[ k ] = dHScalePowers[ k - 1 ] * tredRPC00B.dHEIGHT_SCALE;
   }
   ASSERT( TRE_RPC00B_N_LINE_NUM_COEFF == TRE_RPC00B_N_POLYNOMIAL_COEFF );
   ASSERT( TRE_RPC00B_N_LINE_DEN_COEFF == TRE_RPC00B_N_POLYNOMIAL_COEFF );
   ASSERT( TRE_RPC00B_N_SAMP_NUM_COEFF == TRE_RPC00B_N_POLYNOMIAL_COEFF );
   ASSERT( TRE_RPC00B_N_SAMP_DEN_COEFF == TRE_RPC00B_N_POLYNOMIAL_COEFF );
   DOUBLE
      *pdLineNumCoeffs = m_apdRatPolyCoeffs[ LINE_NUM_COEFFS ].get(),
      *pdLineDenCoeffs = m_apdRatPolyCoeffs[ LINE_DEN_COEFFS ].get(),
      *pdSampNumCoeffs = m_apdRatPolyCoeffs[ SAMP_NUM_COEFFS ].get(),
      *pdSampDenCoeffs = m_apdRatPolyCoeffs[ SAMP_DEN_COEFFS ].get();

   for ( k = 0; k < TRE_RPC00B_N_POLYNOMIAL_COEFF; k++ )
   {
      INT
         kP = RPC00BCoeffPLHPowers[ k ][ 0 ],   // Lat ("P") powers
         kL = RPC00BCoeffPLHPowers[ k ][ 1 ],   // Long ("L") powers
         kH = RPC00BCoeffPLHPowers[ k ][ 2 ],   // Height ("H") powers

         // Index into coefficient arrays
         kPLH = kP + ( m_cPolynomialTerms * ( kL + ( m_cPolynomialTerms * kH ) ) );

      DOUBLE 
         dScaleIn =
            dPScalePowers[ kP ]        // Lat ("P") powers
            * dLScalePowers[ kL ]      // Long ("L") powers
            * dHScalePowers[ kH ],     // Height ("H") powers
         dLineNumCoeff = tredRPC00B.dLINE_NUM_COEFF[ k ] / dScaleIn,
         dLineDenCoeff = tredRPC00B.dLINE_DEN_COEFF[ k ] / dScaleIn,
         dSampNumCoeff = tredRPC00B.dSAMP_NUM_COEFF[ k ] / dScaleIn,
         dSampDenCoeff = tredRPC00B.dSAMP_DEN_COEFF[ k ] / dScaleIn;
      
      pdLineNumCoeffs[ kPLH ] = dFactor *
         ( ( tredRPC00B.dLINE_SCALE * dLineNumCoeff )
         + ( dLineOffset * dLineDenCoeff ) );
      pdLineDenCoeffs[ kPLH ] = dLineDenCoeff;;
      pdSampNumCoeffs[ kPLH ] = dFactor *
         ( ( tredRPC00B.dSAMP_SCALE * dSampNumCoeff )
         + ( dSampOffset * dSampDenCoeff ) );
      pdSampDenCoeffs[ kPLH ] = dSampDenCoeff;;
   }

   m_defined = TRUE;
}  // End of DefineRPC00BTransform()



//
// DefineICHIPBTransform() - Define ICHIPB transform
//
// See STDI-002 version 2.1, chapter 5 for explanations
//
VOID CTransform::DefineICHIPBTransform(
      const CTREDataICHIPB& tredICHIPB )  // Input image ICHIPB struct
{
   clear();
   m_eTransformMode = TRANSFORM_MODE_POLYNOMIAL;   // Assume no ICHIPB or valid OP points


   // If non-dewarped ICHIPB TRE is available, RPC00B interpretation gets scaled and offset
   if ( tredICHIPB.iXFRM_FLAG == CTREDataICHIPB::XFRM_FLAG_NONDEWARPED )
   {
      DOUBLE 
         dFIRows[4] =
         {
            tredICHIPB.dFI_ROW_11,
            tredICHIPB.dFI_ROW_12,
            tredICHIPB.dFI_ROW_21,
            tredICHIPB.dFI_ROW_22
         },
         dFICols[4] =
         {
            tredICHIPB.dFI_COL_11,
            tredICHIPB.dFI_COL_12,
            tredICHIPB.dFI_COL_21,
            tredICHIPB.dFI_COL_22
         },
         dOPRows[4] =
         {
            tredICHIPB.dOP_ROW_11,
            tredICHIPB.dOP_ROW_12,
            tredICHIPB.dOP_ROW_21,
            tredICHIPB.dOP_ROW_22
         },
         dOPCols[4] =
         {
            tredICHIPB.dOP_COL_11,
            tredICHIPB.dOP_COL_12,
            tredICHIPB.dOP_COL_21,
            tredICHIPB.dOP_COL_22
         };
               
      // Check for proper output product corners
      if ( dOPCols[0] >= dOPCols[1]
         || dOPCols[0] >= dOPCols[3]
         || dOPRows[0] >= dOPRows[2]
         || dOPRows[0] >= dOPRows[3]
         || dOPCols[1] <= dOPCols[2]
         || dOPRows[1] >= dOPRows[2]
         || dOPRows[1] >= dOPRows[3]
         || dOPCols[2] >= dOPCols[3] )
      {
         m_eTransformMode = TRANSFORM_MODE_ICHIPB;    // Use only three points
      }
                  
      // Compute ICHIPB polynomial transform.  Note that the input "lat"
      // is a row value and "lon" is a column value
      DefineTiepointsPolynomial( 4, dOPCols, dOPRows, dFIRows, dFICols );

   }  // ICHIPB available

   else  // Generate a unit transform
   {
      static const DOUBLE
         dUnitCols[] = { -1.0, +1.0, -1.0, +1.0 },
         dUnitRows[] = { -1.0, -1.0, +1.0, +1.0 };

      DefineTiepointsPolynomial( 4, dUnitRows, dUnitCols, dUnitCols, dUnitRows );
   }

   // Put into ICHIPB coefficients too
   memcpy( m_dICHIPBCoeffsX, m_x_coef, sizeof(m_dICHIPBCoeffsX) );
   m_dICHIPBCoeffsX[0] -= m_dLongitudeOrigin;
   memcpy( m_dICHIPBCoeffsY, m_y_coef, sizeof(m_dICHIPBCoeffsY) );
   m_dICHIPBCoeffsY[0] -= m_dLatitudeOrigin;

   m_eTransformMode = TRANSFORM_MODE_ICHIPB; // In case it was processed as POLYNOMIAL

}  // End of DefineICHIPBTransform()



// SetPolynomialLatLongOrigin()
//
INT CTransform::SetPolynomialLatLongOrigin(
      DOUBLE dLatitudeOrigin, DOUBLE dLongitudeOrigin )
{
   if ( !m_defined
         || ( m_eTransformMode != TRANSFORM_MODE_POLYNOMIAL
            && m_eTransformMode != TRANSFORM_MODE_SVD_POLYNOMIAL ) )
      return FAILURE;

   DOUBLE
      dLatOffsetCoeffs[4] = { dLatitudeOrigin - m_dLatitudeOrigin, 1.0, 0.0, 0.0 },
      dLonOffsetCoeffs[4] = { dLongitudeOrigin - m_dLongitudeOrigin, 0.0, 1.0, 0.0 };
   
   INT nDegree = m_cPolynomialTerms - 1;
   DoublePtr apdNewCoeffs( new DOUBLE[ m_num_rows ] );

#ifdef POLY_MULT_TEST
   if ( !m_bGeoMode )
   {
      Sleep( 100 );
      TRACE( _T("Before poly lat/long origin change:\n")
         _T("  Origin =%14.4e,%14.4e\n"),
         m_dLatitudeOrigin, m_dLongitudeOrigin );

      TRACE( _T("  X coeffs =") );
      INT iLon;
      for ( iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
      {
         TRACE( _T("\n  ") );
         for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            TRACE( _T("%14.4e,"), m_x_coef[ iLat + ( iLon * m_cPolynomialTerms ) ] );
      }
      
      TRACE( _T("\n  Y coeffs =") );
      for ( iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
      {
         TRACE( _T("\n  ") );
         for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            TRACE( _T("%14.4e,"), m_y_coef[ iLat + ( iLon * m_cPolynomialTerms ) ] );
      }
      
      TRACE( _T("\n") );
   }
   Sleep( 100 );
#endif   // POLY_MULT_TEST

   PolynomialMultiply( 1, dLatOffsetCoeffs, dLonOffsetCoeffs,
      nDegree, m_x_coef, nDegree, apdNewCoeffs.get() );
   memcpy( m_x_coef, apdNewCoeffs.get(), m_num_rows * sizeof(DOUBLE) );

   PolynomialMultiply( 1, dLatOffsetCoeffs, dLonOffsetCoeffs,
      nDegree, m_y_coef, nDegree, apdNewCoeffs.get() );
   memcpy( m_y_coef, apdNewCoeffs.get(), m_num_rows * sizeof(DOUBLE) );

   m_latitude_coef[0] -= dLatOffsetCoeffs[0];
   m_longitude_coef[0] -= dLonOffsetCoeffs[0];

   m_dLatitudeOrigin = dLatitudeOrigin;
   m_dLongitudeOrigin = dLongitudeOrigin;

#ifdef POLY_MULT_TEST
   if ( !m_bGeoMode )
   {
      Sleep( 100 );
      TRACE( _T("After poly lat/long origin change:\n")
         _T("  Origin =%14.4e,%14.4e\n"),
         m_dLatitudeOrigin, m_dLongitudeOrigin );

      TRACE( _T("  X coeffs =") );
      INT iLon;
      for ( iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
      {
         TRACE( _T("\n  ") );
         for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            TRACE( _T("%14.4e,"), m_x_coef[ iLat + ( iLon * m_cPolynomialTerms ) ] );
      }
      
      TRACE( _T("\n  Y coeffs =") );
      for ( iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
      {
         TRACE( _T("\n  ") );
         for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            TRACE( _T("%14.4e,"), m_y_coef[ iLat + ( iLon * m_cPolynomialTerms ) ] );
      }
      
      TRACE( _T("\n") );
   }
#endif   // POLY_MULT_TEST

   return SUCCESS;

}  // End of SetPolynomialLatLongOrigin()



//
// SetPolynomialXYScaleAndOrigin()
//
INT CTransform::SetPolynomialXYScaleAndOrigin(
                        DOUBLE dXOffsetIn, DOUBLE dYOffsetIn,
                        DOUBLE dXScale, DOUBLE dYScale,
                        DOUBLE dXOffsetOut, DOUBLE dYOffsetOut )
{
   if ( !m_defined ||
      ( m_eTransformMode != TRANSFORM_MODE_POLYNOMIAL
         && m_eTransformMode != TRANSFORM_MODE_SVD_POLYNOMIAL ) )
      return FAILURE;

#ifdef POLY_MULT_TEST
   if ( !m_bGeoMode )
   {
      Sleep( 100 );
      TRACE( _T("Before poly X/Y scale and origin change:\n") );

      TRACE( _T("  X coeffs =") );
      INT iLon;
      for ( iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
      {
         TRACE( _T("\n  ") );
         for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            TRACE( _T("%14.4e,"), m_x_coef[ iLat + ( iLon * m_cPolynomialTerms ) ] );
      }
      
      TRACE( _T("\n  Y coeffs =") );
      for ( iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
      {
         TRACE( _T("\n  ") );
         for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            TRACE( _T("%14.4e,"), m_y_coef[ iLat + ( iLon * m_cPolynomialTerms ) ] );
      }
      
      TRACE( _T("\n") );
   }
   Sleep( 100 );
#endif   // POLY_MULT_TEST

   m_x_coef[0] -= dXOffsetIn;
   m_y_coef[0] -= dYOffsetIn;

   for ( INT i = 0; i < m_num_tiepoints; i++ )
   {
      m_x_coef[i] *= dXScale;
      m_y_coef[i] *= dYScale;
   }

   m_x_coef[0] += dXOffsetOut;
   m_y_coef[0] += dYOffsetOut;

#ifdef POLY_MULT_TEST
   if ( !m_bGeoMode )
   {
      Sleep( 100 );
      TRACE( _T("After poly X/Y scale and origin change:\n") );

      TRACE( _T("  X coeffs =") );
      INT iLon;
      for ( iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
      {
         TRACE( _T("\n  ") );
         for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            TRACE( _T("%14.4e,"), m_x_coef[ iLat + ( iLon * m_cPolynomialTerms ) ] );
      }
      
      TRACE( _T("\n  Y coeffs =") );
      for ( iLon = 0; iLon < m_cPolynomialTerms; iLon++ )
      {
         TRACE( _T("\n  ") );
         for ( INT iLat = 0; iLat < m_cPolynomialTerms; iLat++ )
            TRACE( _T("%14.4e,"), m_y_coef[ iLat + ( iLon * m_cPolynomialTerms ) ] );
      }
      
      TRACE( _T("\n") );
   }
#endif   // POLY_MULT_TEST

    return SUCCESS;
}



// ****************************************************************
// ****************************************************************
//
// CalcPolynomialCoefs - calculate polynomial coefficients to
//    fit W as polynomial in U and V
//
int CTransform::CalcPolynomialCoefs( BOOL bSVDMode,
                        const DOUBLE* pdU, const DOUBLE* pdV, 
                        const DOUBLE* pdW, DOUBLE* pdWCoefs )
{
   INT cMatrixElements =  m_num_tiepoints * m_num_rows;

   // Input matrix for inversion
   DoublePtr apdFwdMatrix( new DOUBLE[ cMatrixElements ] );
   
   // Fill the input matrix with evaluated powers of U and V
   DOUBLE* pdMatrix = apdFwdMatrix.get();      // Fill matrix in raster order
   for ( INT iTiepoint = 0; iTiepoint < m_num_tiepoints; iTiepoint++ )
   {
      DOUBLE dVPower = 1.0;       // V^0
      for ( INT iVPower = 0; iVPower < m_cPolynomialTerms; iVPower++ )
      {
         DOUBLE dUPower = 1.0;       // U^0
         for ( INT iUPower = 0; iUPower < m_cPolynomialTerms; iUPower++ )
         {
            *(pdMatrix++) = dUPower * dVPower;
            dUPower *= pdU[ iTiepoint ];     // Next power of U
         }  // U power loop
         
         dVPower *= pdV[ iTiepoint ];     // Next power of V
      }  // V power loop
   }  // Tiepoint loop
   
#ifdef CALC_POLY_TEST
   DoublePtr apdFwdMatrixSave( new DOUBLE[ cMatrixElements ] );
   memcpy( apdFwdMatrixSave.get(), apdFwdMatrix.get(), cMatrixElements * sizeof(DOUBLE) );
   
   Sleep( 100 );
   TRACE( _T("Inputs:\n") );
   INT iT;
   for ( iT = 0; iT < m_num_tiepoints; iT++ )
      TRACE( _T("%2d:  %.6f,%.6f  %.6f\n"),
      iT, pdU[iT], pdV[iT], pdW[iT] );
   
   Sleep( 100 );
   TRACE( _T("Forward matrix:") );
   INT iY;
   for ( iY = 0; iY < m_num_tiepoints; iY++ )
   {
      TRACE( _T("\n%2d:  "), iY );
      for ( INT iX = 0; iX < m_num_rows; iX++ )
         TRACE( _T("%14.4e"), apdFwdMatrix.get()[ iX + ( iY * m_num_rows ) ] );
   }
#endif
   
   // Matrix inversion output
   DoublePtr apdInvMatrix( new DOUBLE[ cMatrixElements ] );

   if ( bSVDMode )
   {
      ap::real_2d_array a, u, vt;
      ap::real_1d_array w;

      INT cSVDVals = ( m_cPolynomialTerms * ( m_cPolynomialTerms + 1 ) ) / 2;
      a.setbounds( 0, m_num_tiepoints - 1, 0, cSVDVals - 1 );

#ifdef CALC_POLY_TEST
      TRACE( _T("\nSVD input:") );
#endif
      for ( INT iT = 0; iT < m_num_tiepoints; iT++ )
      {
#ifdef CALC_POLY_TEST
         TRACE( _T("\n") );
#endif
         INT iCol = 0;
         for ( INT iVPow = 0; iVPow < m_cPolynomialTerms; iVPow++ )
         {
            for ( INT iUPow = 0; iUPow < m_cPolynomialTerms - iVPow; iUPow++ )
            {
               a( iT, iCol ) = apdFwdMatrix.get()
                  [ iUPow + ( iVPow * m_cPolynomialTerms ) + ( iT * m_num_rows ) ];
#ifdef CALC_POLY_TEST
               TRACE( _T("  %.4e"), a( iT, iCol ) );
#endif
               iCol++;
            }
         }
         ASSERT( iCol == cSVDVals );
      }

      bool b = rmatrixsvd(
                     // Input parameters
                     a,                // A           -   matrix to be decomposed.
                                       // Array whose indexes range within [0..M-1, 0..N-1].
                     m_num_tiepoints,  // M           -   number of rows in matrix A.
                     cSVDVals,         // N           -   number of columns in matrix A.
                     1,                // UNeeded     -   0, 1 or 2. See the description of the parameter U.
                     1,                // VTNeeded    -   0, 1 or 2. See the description of the parameter VT.
                     2,                // AdditionalMemory -
                                       //    If the parameter:
                                       //                   * equals 0, the algorithm doesn’t use additional
                                       //                     memory (lower requirements, lower performance).
                                       //                   * equals 1, the algorithm uses additional
                                       //                     memory of size min(M,N)*min(M,N) of real numbers.
                                       //                     It often speeds up the algorithm.
                                       //                   * equals 2, the algorithm uses additional
                                       //                     memory of size M*min(M,N) of real numbers.
                                       //                     It allows to get a maximum performance.
                                       //                   The recommended value of the parameter is 2.
                                       // Output parameters:
                     w,                // W           -   contains singular values in descending order.
                     u,                // U           -   if UNeeded=0, U isn't changed, the left singular vectors
                                       //                 are not calculated.
                                       //                 if Uneeded=1, U contains left singular vectors (first
                                       //                 min(M,N) columns of matrix U). Array whose indexes range
                                       //                 within [0..M-1, 0..Min(M,N)-1].
                                       //                 if UNeeded=2, U contains matrix U wholly. Array whose
                                       //                 indexes range within [0..M-1, 0..M-1].
                     vt                // VT          -   if VTNeeded=0, VT isn’t changed, the right singular vectors
                                       //                 are not calculated.
                                       //                 if VTNeeded=1, VT contains right singular vectors (first
                                       //                 min(M,N) rows of matrix V^T). Array whose indexes range
                                       //                 within [0..min(M,N)-1, 0..N-1].
                                       //                 if VTNeeded=2, VT contains matrix V^T wholly. Array whose
                                       //                 indexes range within [0..N-1, 0..N-1].
                     );

      INT iRow;


#ifdef CALC_POLY_TEST
      TRACE( _T("\nu matrix:") );
      for ( iRow = 0; iRow < m_num_tiepoints; iRow++ )
      {
         TRACE( _T("\n") );
         for ( INT iCol = 0; iCol < cSVDVals; iCol++ )
         {
            TRACE( _T("  %.4e"), u( iRow, iCol ) );
         }
      }
      TRACE( _T("\nSingular values:") );
      for ( iRow = 0; iRow < cSVDVals; iRow++ )
      {
         TRACE( _T("  %.4e"), w( iRow ) );
      }
      TRACE( _T("\nvt matrix:") );
      for ( iRow = 0; iRow < cSVDVals; iRow++ )
      {
         TRACE( _T("\n") );
         for ( INT iCol = 0; iCol < cSVDVals; iCol++ )
         {
            TRACE( _T("  %.4e"), vt( iRow, iCol ) );
         }
      }
#endif

      // Back substitute
      DoublePtr apdTemp( new DOUBLE[ m_num_tiepoints ] );

      // [ u* ] * W;
      for ( iRow = 0; iRow < cSVDVals; iRow++ )
      {
         DOUBLE d = 0.0;
         if ( w( iRow ) != 0.0 )
         {
            for ( INT iCol = 0; iCol < m_num_tiepoints; iCol++ )
            {
               d += u( iCol, iRow ) * pdW[ iCol ]; // u transpose
            }
            d /= w( iRow );
         }
         apdTemp.get()[ iRow ] = d;
      }

      // [ v ] * temp
#ifdef CALC_POLY_TEST
      TRACE( _T("\nLSQ coefs:\n") );
#endif
      ZeroMemory( pdWCoefs, m_num_rows * sizeof(*pdWCoefs) );
      INT iUOut = 0, iVOut = 0;     // Output coefficient matrix
      for ( iRow = 0; iRow < cSVDVals; iRow++ )
      {
         DOUBLE d = 0.0;
         for ( INT iCol = 0; iCol < cSVDVals; iCol++ )
         {
            d += vt( iCol, iRow ) * apdTemp.get()[ iCol ];  // Untranspose v
         }
         pdWCoefs[ iUOut + ( m_cPolynomialTerms * iVOut ) ] = d;
#ifdef CALC_POLY_TEST
         TRACE( _T("  %.4e"), d );
#endif

         // Filling upper left of the output matrix
         iUOut++;
         if ( iUOut >= m_cPolynomialTerms - iVOut )
         {
            iUOut = 0;
            iVOut++;
         }
      }

#ifdef CALC_POLY_TEST
      TRACE( _T("\n") );
#endif
   }  // SVD mode

   else  // Not SVD mode
   {

      do
      {
         if ( m_eTransformMode != TRANSFORM_MODE_ICHIPB )   // Normal case
         {
            INT iResult = invert_matrix( apdFwdMatrix.get(), m_num_rows, apdInvMatrix.get() );
            if ( iResult == SUCCESS )
               break;      // Ok, continue

            if ( iResult != SINGULAR_MATRIX )
               return FAILURE;   // Fail

         }

         // ICHIPB mode or normal matrix inversion failed
         return CalcLinearCoefs( pdU, pdV, pdW, pdWCoefs );

      } while ( FALSE );

#ifdef CALC_POLY_TEST
      Sleep( 100 );
      TRACE( _T("\nInverse matrix:") );
      for ( iY = 0; iY < m_num_rows; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < m_num_rows; iX++ )
            TRACE( _T("%14.4e"), apdInvMatrix.get()[ iX + ( iY * m_num_rows ) ] );
      }

      Sleep( 100 );
      TRACE( _T("\nAdjustment matrix:") );
      DoublePtr apdAdjMatrix( new DOUBLE[ cMatrixElements ] );
      for ( iY = 0; iY < m_num_rows; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < m_num_rows; iX++ )
         {
            DOUBLE d = 0.0;
            for ( INT k = 0; k < m_num_rows; k++ )
               d += apdInvMatrix.get()[ k + ( iY * m_num_rows ) ]
            * apdFwdMatrixSave.get()[ iX + ( k * m_num_rows ) ];
            apdAdjMatrix.get()[ iX + ( m_num_rows * iY ) ] = d
               = ( iX == iY ) ? 2.0 - d : -d;
            TRACE( _T("%14.4e"), d );
         }
      }

      DoublePtr apdAdjInvMatrix( new DOUBLE[ cMatrixElements ] );
      TRACE( _T("\nAdjusted inverse matrix:") );
      for ( iY = 0; iY < m_num_rows; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < m_num_rows; iX++ )
         {
            DOUBLE d = 0.0;
            for ( INT k = 0; k < m_num_rows; k++ )
               d += apdAdjMatrix.get()[ k + ( iY * m_num_rows ) ]
                  * apdInvMatrix.get()[ iX + ( k * m_num_rows ) ];
            apdAdjInvMatrix.get()[ iX + ( iY * m_num_rows ) ] = d;
            TRACE( _T("%14.4e"), d );
         }
      }

      Sleep( 100 );
      TRACE( _T("\nAdjusted matrix inversion check:") );
      for ( iY = 0; iY < m_num_rows; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < m_num_rows; iX++ )
         {
            DOUBLE d = 0.0;
            for ( INT k = 0; k < m_num_rows; k++ )
               d += apdAdjInvMatrix.get()[ k + ( iY * m_num_rows ) ]
                  * apdFwdMatrixSave.get()[ iX + ( k * m_num_rows ) ];
            TRACE( _T("%14.4e"), d );
         }
      }
      TRACE( _T("\n") );
      pdMatrix = apdAdjInvMatrix.get();
#else // ndef CALC_POLY_TEST

      // Multiply out the cofficients
      pdMatrix = apdInvMatrix.get();       // Inverse matrix pointer
#endif
      for ( INT iCoef = 0; iCoef < m_num_rows; iCoef++ )
      {
         pdWCoefs[ iCoef ] = 0.0;        // Init the accumulation
         for ( INT iT = 0; iT < m_num_rows; iT++ )
            pdWCoefs[ iCoef ] += *pdMatrix++ * pdW[ iT ];
      }
   }
#ifdef CALC_POLY_TEST
   {
      ATLTRACE( _T("U:\n") );
      const DOUBLE* pd = pdU;
      INT iU;
      for ( iU = 0; iU < m_cPolynomialTerms; iU++ )
      {
         ATLTRACE( _T("  ") );
         for ( INT iV = 0; iV < m_cPolynomialTerms; iV++ )
         {
            ATLTRACE( _T("%12.3f"), *pd++ );
         }
         ATLTRACE( _T("\n") );
      }
      
      ATLTRACE( _T("V:\n") );
      pd = pdV;
      for ( iU = 0; iU < m_cPolynomialTerms; iU++ )
      {
         ATLTRACE( _T("  ") );
         for ( INT iV = 0; iV < m_cPolynomialTerms; iV++ )
         {
            ATLTRACE( _T("%12.3f"), *pd++ );
         }
         ATLTRACE( _T("\n") );
      }
      
      ATLTRACE( _T("W:\n") );
      pd = pdW;
      for ( iU = 0; iU < m_cPolynomialTerms; iU++ )
      {
         ATLTRACE( _T("  ") );
         for ( INT iV = 0; iV < m_cPolynomialTerms; iV++ )
         {
            ATLTRACE( _T("%12.3f"), *pd++ );
         }
         ATLTRACE( _T("\n") );
      }
      
      ATLTRACE( _T("Poly coefs:\n") );
      pd = pdWCoefs;
      for ( iU = 0; iU < m_cPolynomialTerms; iU++ )
      {
         ATLTRACE( _T("  ") );
         for ( INT iV = 0; iV < m_cPolynomialTerms; iV++ )
         {
            ATLTRACE( _T("%17.7e"), *pd++ );
         }
         ATLTRACE( _T("\n") );
      }
   }
#endif   // def CALC_POLY_TEST

   return SUCCESS;

}



// ****************************************************************
// ****************************************************************
//
// CalcLinearCoefs - calculate polynomial coefficients to
//    fit W as first degree polynomial in U and V
//
int CTransform::CalcLinearCoefs( const DOUBLE* pdU, const DOUBLE* pdV, 
                        const DOUBLE* pdW, DOUBLE* pdWCoefs )
{
   do    // Once only
   {
      if ( m_num_tiepoints != 4 )      // Only works for 4 points
         break;

      // Discard point with smallest sum of distances to the other points
      DOUBLE dMinEdges = DBL_MAX;
      INT iMinEdgesTiepoint;
      INT iTiepoint;
      for ( iTiepoint = 0; iTiepoint < m_num_tiepoints; iTiepoint++ )
      {
         DOUBLE dEdges = 0.0;
         for ( INT i = 0; i < m_num_tiepoints; i++ )
         {
            DOUBLE
               dDU = pdU[ i ] - pdU[ iTiepoint ],
               dDV = pdV[ i ] - pdV[ iTiepoint ];
            dEdges += sqrt( ( dDU * dDU ) + ( dDV * dDV ) );
         }
         if ( dEdges < dMinEdges )
         {
            dMinEdges = dEdges;
            iMinEdgesTiepoint = iTiepoint;
         }
      }

#ifdef CALC_LINEAR_TEST
      TRACE( _T("Linear polynomial coefs:\n")
         _T("  Discarded point = %d\n"), iMinEdgesTiepoint );
#endif

      INT cLinearTiepoints = 3,
         cMatrixElements =  cLinearTiepoints * cLinearTiepoints;  // 3x3 matrix after discarding one point

      // Input matrix for inversion
      DoublePtr apdFwdMatrix( new DOUBLE[ cMatrixElements ] );
      
      // Fill the input matrix with evaluated powers of U and V (only to 1st power here)
      DOUBLE* pdMatrix = apdFwdMatrix.get();      // Fill matrix in raster order
      for ( iTiepoint = 0; iTiepoint < m_num_tiepoints; iTiepoint++ )
      {
         if ( iTiepoint != iMinEdgesTiepoint )  // Skip discarded point
         {
            *pdMatrix++ = 1.0;               // U^0 * V^0
            *pdMatrix++ = pdU[ iTiepoint ];  // U^1 * V^0
            *pdMatrix++ = pdV[ iTiepoint ];  // U^0 * V^1
         }
      }  // Tiepoint loop
      
#ifdef CALC_LINEAR_TEST
      DoublePtr apdFwdMatrixSave( new DOUBLE[ cMatrixElements ] );
      memcpy( apdFwdMatrixSave.get(), apdFwdMatrix.get(), cMatrixElements * sizeof(DOUBLE) );

      Sleep( 100 );
      TRACE( _T("Inputs:\n") );
      for ( iTiepoint = 0; iTiepoint < m_num_tiepoints; iTiepoint++ )
         if ( iTiepoint != iMinEdgesTiepoint )
            TRACE( _T("%2d:  %.6f,%.6f  %.6f\n"),
               iTiepoint, pdU[ iTiepoint ], pdV[ iTiepoint ], pdW[ iTiepoint ] );

      Sleep( 100 );
      TRACE( _T("Forward matrix:") );
      INT iY;
      for ( iY = 0; iY < cLinearTiepoints; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < cLinearTiepoints; iX++ )
            TRACE( _T("%14.4e"), apdFwdMatrix.get()[ iX + ( iY * cLinearTiepoints ) ] );
      }
#endif

      // Matrix inversion output
      DoublePtr apdInvMatrix( new DOUBLE[ cMatrixElements ] );

      if ( SUCCESS != invert_matrix( apdFwdMatrix.get(), cLinearTiepoints, apdInvMatrix.get() ) )
         break;      // Fail

#ifdef CALC_LINEAR_TEST
      Sleep( 100 );
      TRACE( _T("\nInverse matrix:") );
      for ( iY = 0; iY < cLinearTiepoints; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < cLinearTiepoints; iX++ )
            TRACE( _T("%14.4e"), apdInvMatrix.get()[ iX + ( iY * cLinearTiepoints ) ] );
      }

      Sleep( 100 );
      TRACE( _T("\nAdjustment matrix:") );
      DoublePtr apdAdjMatrix( new DOUBLE[ cMatrixElements ] );
      for ( iY = 0; iY < cLinearTiepoints; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < cLinearTiepoints; iX++ )
         {
            DOUBLE d = 0.0;
            for ( INT k = 0; k < cLinearTiepoints; k++ )
               d += apdInvMatrix.get()[ k + ( iY * cLinearTiepoints ) ]
                  * apdFwdMatrixSave.get()[ iX + ( k * cLinearTiepoints ) ];
            apdAdjMatrix.get()[ iX + ( cLinearTiepoints * iY ) ] = d
               = ( iX == iY ) ? 2.0 - d : -d;
            TRACE( _T("%14.4e"), d );
         }
      }

      DoublePtr apdAdjInvMatrix( new DOUBLE[ cMatrixElements ] );
      TRACE( _T("\nAdjusted inverse matrix:") );
      for ( iY = 0; iY < cLinearTiepoints; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < cLinearTiepoints; iX++ )
         {
            DOUBLE d = 0.0;
            for ( INT k = 0; k < cLinearTiepoints; k++ )
               d += apdAdjMatrix.get()[ k + ( iY * cLinearTiepoints ) ]
                  * apdInvMatrix.get()[ iX + ( k * cLinearTiepoints ) ];
            apdAdjInvMatrix.get()[ iX + ( iY * cLinearTiepoints ) ] = d;
            TRACE( _T("%14.4e"), d );
         }
      }

      Sleep( 100 );
      TRACE( _T("\nAdjusted matrix inversion check:") );
      for ( iY = 0; iY < cLinearTiepoints; iY++ )
      {
         TRACE( _T("\n%2d:  "), iY );
         for ( INT iX = 0; iX < cLinearTiepoints; iX++ )
         {
            DOUBLE d = 0.0;
            for ( INT k = 0; k < cLinearTiepoints; k++ )
               d += apdAdjInvMatrix.get()[ k + ( iY * cLinearTiepoints ) ]
                  * apdFwdMatrixSave.get()[ iX + ( k * cLinearTiepoints ) ];
            TRACE( _T("%14.4e"), d );
         }
      }
      TRACE( _T("\n") );
      pdMatrix = apdAdjInvMatrix.get();
#else // ndef CALC_LINEAR_TEST

      // Multiply out the cofficients
      pdMatrix = apdInvMatrix.get();         // Inverse matrix pointer
#endif
      for ( INT iCoef = 0; iCoef < m_num_tiepoints; iCoef++ )
      {
         pdWCoefs[ iCoef ] = 0.0;        // Init the accumulation

         // Only linear terms exist
         if ( ( iCoef % m_cPolynomialTerms )
            + ( ( iCoef / m_cPolynomialTerms ) % m_cPolynomialTerms ) <= 1 )
         {
            for ( INT iTiepoint = 0; iTiepoint < m_num_tiepoints; iTiepoint++ )
               if ( iTiepoint != iMinEdgesTiepoint )
                  pdWCoefs[ iCoef ] += *pdMatrix++ * pdW[ iTiepoint ];
         }
      }
#ifdef CALC_LINEAR_TEST
{
      ATLTRACE( _T("U:\n") );
      const DOUBLE* pd = pdU;
      INT iU;
      for ( iU = 0; iU < m_cPolynomialTerms; iU++ )
      {
         ATLTRACE( _T("  ") );
         for ( INT iV = 0; iV < m_cPolynomialTerms; iV++ )
         {
            ATLTRACE( _T("%12.3f"), *pd++ );
         }
         ATLTRACE( _T("\n") );
      }

      ATLTRACE( _T("V:\n") );
      pd = pdV;
      for ( iU = 0; iU < m_cPolynomialTerms; iU++ )
      {
         ATLTRACE( _T("  ") );
         for ( INT iV = 0; iV < m_cPolynomialTerms; iV++ )
         {
            ATLTRACE( _T("%12.3f"), *pd++ );
         }
         ATLTRACE( _T("\n") );
      }
      
      ATLTRACE( _T("W:\n") );
      pd = pdW;
      for ( iU = 0; iU < m_cPolynomialTerms; iU++ )
      {
         ATLTRACE( _T("  ") );
         for ( INT iV = 0; iV < m_cPolynomialTerms; iV++ )
         {
            ATLTRACE( _T("%12.3f"), *pd++ );
         }
         ATLTRACE( _T("\n") );
      }

      ATLTRACE( _T("Poly coefs:\n") );
      pd = pdWCoefs;
      for ( iU = 0; iU < m_cPolynomialTerms; iU++ )
      {
         ATLTRACE( _T("  ") );
         for ( INT iV = 0; iV < m_cPolynomialTerms; iV++ )
         {
            ATLTRACE( _T("%17.7e"), *pd++ );
         }
         ATLTRACE( _T("\n") );
      }
}
#endif   // def CALC_LINEAR_TEST
      
      return SUCCESS;

   } while ( FALSE );
   return FAILURE;
}



// ****************************************************************
// ****************************************************************

int CTransform::invert_matrix( double *matrix, int n,
                                       double *inverse_matrix )
{
	// this function inverts the input matrix, which is destroyed
	// returns SUCCESS or FAILURE

	int i, j, index;
   int *index_array = NULL;
   double *col = NULL;;
   std::unique_ptr<int[]> apiIndexArray;  // was auto_ptr (removed in C++17); holds int[]
   DoublePtr apdCol;

	if( n <= 0 ) goto FAIL;

	if( n == 1 )
	{
		if( fabs( matrix[0]) < m_aprox0 ) goto FAIL;
		inverse_matrix[0] = 1.0 / matrix[0];
		goto SUCCEED;
	}

	// allocate memory for index
   ResetAutoPtr( apiIndexArray, int, new int[ n ] );
   index_array = apiIndexArray.get();

	// perform LU decomposition, return if error
   int iResult;
	if( ( iResult = decompose_lu( matrix, n, index_array ) ) != SUCCESS )
      return iResult;

	// allocate memory for one column
   ResetDOUBLEPtr( apdCol, new DOUBLE[ n ] );
   col = apdCol.get();
	
	// calculate inverse column by column using back substitution
	for( j = 0; j < n; j++ )
	{
		for( i = 0; i < n; i++ )
			col[i] = 0.0;
		col[j] = 1.0;
		back_substitute_lu( matrix, n, index_array, col );
		for( i = 0; i < n; i++ )
		{
			index = i*n + j;
			inverse_matrix[index] = col[i];
		}
	}

SUCCEED:
   return SUCCESS;

FAIL:
   return FAILURE;
}

// ****************************************************************
// ****************************************************************

int CTransform::decompose_lu( double *a, int n, int *indx )
{
   // performs LU decomposition on matrix in a
   // returns SUCCESS or FAILURE
   int i,imax,j,k;
	int index, index1, index2;
	double d, big,dum,sum,temp;
   double *scratch;
   INT iResult = SUCCESS;     // Assumed

	// allocate scratch space
   DoublePtr apdScratch( new DOUBLE[n] );
   scratch = apdScratch.get();

	d=1.0;
	
	for( i = 0; i < n; i++ )   // find largest absolute value in each row of a
	{
		big = 0.0;
		for( j = 0; j < n; j++ )
		{
			index = n*i+j;
			if( (temp=fabs(a[index])) > big )
				big=temp;
		}
		if ( big == 0.0 )
         goto FAIL;
		scratch[i]=1.0/big; // scratch now contains largest abs value in each row
	}
	
	for( j = 0; j < n; j++ )
	{
		for( i = 0; i < j; i++ )
		{
			index = n*i+j;
			sum=a[index];
			for ( k = 0; k < i; k++ )
			{
				index1 = n*i+k;
				index2 = n*k+j;
				sum -= a[index1]*a[index2];
			}
			a[index]=sum;
		}
		
		big=0.0;
		imax = 0;
		
		for( i = j; i < n; i++ )
		{
			index = n*i+j;
			sum=a[index];
			for( k = 0; k < j; k++ )
			{
				index1 = n*i+k;
				index2 = n*k+j;
				sum -= a[index1]*a[index2];
			}
			a[index]=sum;
			if ( (dum=scratch[i]*fabs(sum)) >= big)
			{
				big=dum;
				imax=i;
			}
		}
		
		if( j != imax )
		{
			for( k = 0; k < n; k++)
			{
				index1 = n*imax+k;
				dum=a[index1];
				index2 = n*j+k;
				a[index1]=a[index2];
				a[index2]=dum;
			}
			d = -(d);
			scratch[imax]=scratch[j];
		}
		
		indx[j]=imax;
		index1 = n*j+j;
		if( a[index1] == 0.0 )
      {
         iResult = SINGULAR_MATRIX;
			a[index1] = m_aprox0;
      }
		if (j != n - 1)
		{
			dum=1.0/(a[index1]);
			for( i= j + 1; i < n; i++ )
			{
				index = n*i+j;
				a[index] *= dum;
			}
		}
	}
	return iResult;

FAIL:
   return FAILURE;
}
// end of decompose_lu

// ****************************************************************
// ****************************************************************

void CTransform::back_substitute_lu( double *a, int n, int *indx,
                                             double *b )
{
	int i,ii=-1,ip,j;
	int index;
	double sum;

	for( i = 0; i < n; i++ )
	{
		ip=indx[i];
		sum=b[ip];
		b[ip]=b[i];
		if( ii >= 0 )
			for( j = ii; j <= i-1; j++ )
			{
				index = n*i+j;
				sum -= a[index]*b[j];
			}
		else
			if( sum )
				ii=i;
		b[i]=sum;
	}
	
	for( i = n - 1; i >= 0; i-- )
	{
		sum=b[i];
		for( j = i+1; j < n; j++ )
		{
			index = n*i+j;
			sum -= a[index]*b[j];
		}
		index = n*i+i;
		b[i]=sum/a[index];
	}
}
// end of back_substitute_lu



// ****************************************************************
// **********************************************
//
// TransformProduct() - find product of two chained transforms
//
// Note: only an forward transform is valid afterward with the output values
// in the X/Y pair

INT CTransform::SetTransformProduct( CTransform& tfrm1, CTransform& tfrm2 )
{
   clear();

   // Assume success
   m_defined = TRUE;
   m_bGeoMode = FALSE;

   do
   {
      if ( tfrm1.m_eTransformMode != TRANSFORM_MODE_POLYNOMIAL
            && tfrm1.m_eTransformMode != TRANSFORM_MODE_SVD_POLYNOMIAL )
         break;      // Only polynomial first transformation

      // Get the output reference lat/long and pointers to coefficients to
      // convert lat/long to image X and Y.  The latter may be null if RPC00B mode
      DOUBLE dLatOrigin2, dLonOrigin2;
      const DOUBLE *pdCoeffsX2, *pdCoeffsY2;
      INT nDegree2;
      if ( SUCCESS != tfrm2.GetFwdPolynomialCoeffs( nDegree2,
            dLatOrigin2, dLonOrigin2, pdCoeffsX2, pdCoeffsY2 ) )
         break;

      // Set the map-to-lat/long lat/long origin to
      // match the lat/long-to-image lat/long origin
      tfrm1.SetPolynomialLatLongOrigin( dLatOrigin2, dLonOrigin2 );

      // Get pointers to coefficients to convert map X and Y to relative lat/long
      DOUBLE dLatOrigin1, dLonOrigin1, *pdCoeffsLat1, *pdCoeffsLon1;
      INT nDegree1;
      if ( SUCCESS != tfrm1.GetInvPolynomialCoeffs( nDegree1,
         dLatOrigin1, dLonOrigin1, pdCoeffsLat1, pdCoeffsLon1 ) )
         break;
      
      // Degree of product polynomials
      INT nDegree = nDegree1 * nDegree2;
      m_cPolynomialTerms = nDegree + 1;
      m_num_tiepoints = m_num_rows = m_cPolynomialTerms * m_cPolynomialTerms;

      // Product operation resets origin
      m_dLatitudeOrigin = m_dLongitudeOrigin = 0.0;

      // Product has same mode as 2nd input tranform
      m_eTransformMode = tfrm2.m_eTransformMode; 

      // If second transform is simple polynomial
      if ( m_eTransformMode == TRANSFORM_MODE_POLYNOMIAL
         || m_eTransformMode == TRANSFORM_MODE_SVD_POLYNOMIAL )
      {
         // Allocate the forward coefficients
         ResetDOUBLEPtr( m_apdCoefsX, new DOUBLE[ m_num_rows ] );
         m_x_coef = m_apdCoefsX.get();
         
         ResetDOUBLEPtr( m_apdCoefsY, new DOUBLE[ m_num_rows ] );
         m_y_coef = m_apdCoefsY.get();

         //m_num_rows = 0;      // Force reallocation next time
         
         // Compute the product tranform polynomials.  These will convert the map
         // X and Y directly to the image X and Y.
#ifdef POLY_MULT_TEST
         TRACE( _T("Polynomial transform product result:\n")
            _T("  X coeffs\n") );
#endif
         PolynomialMultiply( nDegree1, pdCoeffsLat1, pdCoeffsLon1,
            nDegree2, pdCoeffsX2,
            nDegree, m_x_coef );
         
#ifdef POLY_MULT_TEST
         TRACE( _T("Polynomial transform product result:\n")
            _T("  Y coeffs\n") );
#endif
         PolynomialMultiply( nDegree1, pdCoeffsLat1, pdCoeffsLon1,
            nDegree2, pdCoeffsY2,
            nDegree, m_y_coef );
         
         return SUCCESS;

      }  // End of polynomial 2nd transform case

      // If second transform is defined as RPC00B or RPC00B + ICHIPB
      else if ( m_eTransformMode == TRANSFORM_MODE_RPC00B
         || m_eTransformMode == TRANSFORM_MODE_RPC00B_ICHIPB )
      {
         // Get the output pointers to RPC00B coefficients to convert lat/long
         // to full image X (sample) and Y (line).
         const DOUBLE *pdLineNumCoeffs2, *pdLineDenCoeffs2, *pdSampNumCoeffs2, *pdSampDenCoeffs2;
         if ( SUCCESS != tfrm2.GetRPC00BPolynomialCoeffs(
            pdLineNumCoeffs2, pdLineDenCoeffs2, pdSampNumCoeffs2, pdSampDenCoeffs2 ) )
            break;

         // Allocate the transformed RPC00B coefficients
         for ( INT k = 0; k < 4; k++ )
            ResetDOUBLEPtr( m_apdRatPolyCoeffs[ k ], new DOUBLE[ m_num_tiepoints ] );

         PolynomialMultiply( nDegree1, pdCoeffsLat1, pdCoeffsLon1,
            nDegree2, pdLineNumCoeffs2,
            nDegree, m_apdRatPolyCoeffs[ LINE_NUM_COEFFS ].get() );
#ifdef RPC00B_TEST
         INT cPolyTerms1 = nDegree1 + 1, cPolyTerms2 = nDegree2 + 1;
         TRACE( _T("Transform product:\n")
            _T("  Degree 1 = %d, degree 2 = %d, degree out = %d\n")
            _T("  Origin 1 = lat = %.6f, long = %.6f\n")
            _T("  LatCf1[0][0] = %+11.4e, [0][1] = %+11.4e, [1][0] = %+11.4e, [1][1] = %+11.4e\n")
            _T("  LonCf1[0][0] = %+11.4e, [0][1] = %+11.4e, [1][0] = %+11.4e, [1][1] = %+11.4e\n"),
            nDegree1, nDegree2, nDegree,
            dLatOrigin1, dLonOrigin1,
            pdCoeffsLat1[ 0 + ( 0 * cPolyTerms1 ) ],
            pdCoeffsLat1[ 1 + ( 0 * cPolyTerms1 ) ],
            pdCoeffsLat1[ 0 + ( 1 * cPolyTerms1 ) ],
            pdCoeffsLat1[ 1 + ( 1 * cPolyTerms1 ) ],
            pdCoeffsLon1[ 0 + ( 0 * cPolyTerms1 ) ],
            pdCoeffsLon1[ 1 + ( 0 * cPolyTerms1 ) ],
            pdCoeffsLon1[ 0 + ( 1 * cPolyTerms1 ) ],
            pdCoeffsLon1[ 1 + ( 1 * cPolyTerms1 ) ] );
         TRACE(
            _T("  Origin 2 and 3 = lat = %.6f, long = %.6f\n")
            _T("  LineNumCf2[0][0] = %+11.4e, [0][1] = %+11.4e, [1][0] = %+11.4e, [1][1] = %+11.4e\n")
            _T("  LineNumCfOut[0][0] = %+11.4e, [0][1] = %+11.4e, [1][0] = %+11.4e, [1][1] = %+11.4e\n"),
            m_dLatitudeOrigin, m_dLongitudeOrigin,
            pdLineNumCoeffs2[ 0 + ( 0 * cPolyTerms2 ) ],
            pdLineNumCoeffs2[ 1 + ( 0 * cPolyTerms2 ) ],
            pdLineNumCoeffs2[ 0 + ( 1 * cPolyTerms2 ) ],
            pdLineNumCoeffs2[ 1 + ( 1 * cPolyTerms2 ) ],
            m_apdLineNumCoeffs.get()[ 0 + ( 0 * m_cPolynomialTerms ) ],
            m_apdLineNumCoeffs.get()[ 1 + ( 0 * m_cPolynomialTerms ) ],
            m_apdLineNumCoeffs.get()[ 0 + ( 1 * m_cPolynomialTerms ) ],
            m_apdLineNumCoeffs.get()[ 1 + ( 1 * m_cPolynomialTerms ) ] );
#endif
         
         PolynomialMultiply( nDegree1, pdCoeffsLat1, pdCoeffsLon1,
            nDegree2, pdLineDenCoeffs2,
            nDegree, m_apdRatPolyCoeffs[ LINE_DEN_COEFFS ].get() );
         
         PolynomialMultiply( nDegree1, pdCoeffsLat1, pdCoeffsLon1,
            nDegree2, pdSampNumCoeffs2,
            nDegree, m_apdRatPolyCoeffs[ SAMP_NUM_COEFFS ].get() );
         
         PolynomialMultiply( nDegree1, pdCoeffsLat1, pdCoeffsLon1,
            nDegree2, pdSampDenCoeffs2,
            nDegree, m_apdRatPolyCoeffs[ SAMP_DEN_COEFFS ].get() );
                  
         // If ICHIPB post transform, copy the ICHIPB coefficients
         if ( m_eTransformMode == TRANSFORM_MODE_RPC00B_ICHIPB )
         {
            memcpy( m_dICHIPBCoeffsX, tfrm2.m_dICHIPBCoeffsX, sizeof(m_dICHIPBCoeffsX) );
            memcpy( m_dICHIPBCoeffsY, tfrm2.m_dICHIPBCoeffsY, sizeof(m_dICHIPBCoeffsY) );
         }

         return SUCCESS;

      }  // End of RPC00B second transform mode

      else
         break;      // Invalid 2nd mode

   } while ( FALSE );

   clear();          // Undefined
   return FAILURE;
}

// ****************************************************************
// **********************************************
//
// GetInvPolynomialCoeffs() - get inverse (X,Y to lat/lon) polynomial coefficients
//

INT CTransform::GetInvPolynomialCoeffs( INT& nDegree,
         DOUBLE& dLatOrigin, DOUBLE& dLonOrigin,
         DOUBLE*& pdCoeffsLat, DOUBLE*& pdCoeffsLon ) const
{
   if ( !m_defined )
      return FAILURE;

   nDegree = m_cPolynomialTerms - 1;      // Polynomial degree
   dLatOrigin = m_dLatitudeOrigin;
   dLonOrigin = m_dLongitudeOrigin;
   pdCoeffsLat = m_latitude_coef;
   pdCoeffsLon = m_longitude_coef;

   return SUCCESS;
}


// ****************************************************************
// **********************************************
//
// GetFwdPolynomialCoeffs() - get forward (lat/lon to X,Y) polynomial coefficients
//

INT CTransform::GetFwdPolynomialCoeffs( INT& nDegree,
         DOUBLE& dLatOrigin, DOUBLE& dLonOrigin,
         const DOUBLE*& pdCoeffsX, const DOUBLE*& pdCoeffsY ) const
{
   if ( !m_defined )
      return FAILURE;

   nDegree = m_cPolynomialTerms - 1;      // Polynomial degree
   dLatOrigin = m_dLatitudeOrigin;
   dLonOrigin = m_dLongitudeOrigin;
   pdCoeffsX = m_x_coef;
   pdCoeffsY = m_y_coef;

   return SUCCESS;
}


// ****************************************************************
// **********************************************
//
// GetRPC00BPolynomialCoeffs() - get forward (lat/lon to X,Y) RPC00B polynomial coefficients
//

INT CTransform::GetRPC00BPolynomialCoeffs(
         const DOUBLE*& pdLineNumCoeffs, const DOUBLE*& pdLineDenCoeffs,
         const DOUBLE*& pdSampNumCoeffs, const DOUBLE*& pdSampDenCoeffs ) const
{
   if ( !m_defined ||
      ( m_eTransformMode != TRANSFORM_MODE_RPC00B
      && m_eTransformMode != TRANSFORM_MODE_RPC00B_ICHIPB ) )
      return FAILURE;

   pdLineNumCoeffs = m_apdRatPolyCoeffs[ LINE_NUM_COEFFS ].get();
   pdLineDenCoeffs = m_apdRatPolyCoeffs[ LINE_DEN_COEFFS ].get();
   pdSampNumCoeffs = m_apdRatPolyCoeffs[ SAMP_NUM_COEFFS ].get();
   pdSampDenCoeffs = m_apdRatPolyCoeffs[ SAMP_DEN_COEFFS ].get();

   return SUCCESS;
}


// ****************************************************************
// **********************************************
//
// GetICHIPBCoeffs() - get ICHIPB transform coefficients
//

INT CTransform::GetICHIPBCoeffs( DOUBLE (&dCoeffsX)[3], DOUBLE (&dCoeffsY)[3] ) const
{
   if ( !m_defined ||
      m_eTransformMode != TRANSFORM_MODE_RPC00B_ICHIPB )
      return FAILURE;

   memcpy( dCoeffsX, m_dICHIPBCoeffsX, sizeof(m_dICHIPBCoeffsX) );
   memcpy( dCoeffsY, m_dICHIPBCoeffsY, sizeof(m_dICHIPBCoeffsY) );

   return SUCCESS;
}


// ****************************************************************
// **********************************************
//
// PolynomialMultiply() - find polynomial coefficients of two nested polynomials
//
// P(x,y) = polynomial(u,v) = sum( Aij * u^i * v^j ) where
//    u = polynomial(x,y) = sum( Bkl * x^k * y^l ) and
//    v = polynomial(x,y) = sum( Cmn * x^m * y^n )
// The array A will probably be zero for i + j > degree(P) and B and C will probably
// be zero for k + l and m + n > degree(U or V)
//
VOID CTransform::PolynomialMultiply(
      INT nDegreeUVXY,           // Degree of polynomials U and V in X and Y
      const DOUBLE* pdCoeffsUXY, // Polynomial coefficients of U in X and Y
      const DOUBLE* pdCoeffsVXY, // Polynomial coefficients of V in X and Y
      INT nDegreePUV,            // Degree of polynomial P in U and V
      const DOUBLE* pdCoeffsPUV, // Polynomial coefficients of P in U and V
      INT nDegreePXY,            // Degree of output polynomiai P in X and Y
      DOUBLE* pdCoeffsPXY )      // [out] Polynomial coeffients of P in X and Y
{
   INT
      cCoeffsPXY = nDegreePXY + 1,
      cCoeffsPUV = nDegreePUV + 1,
      cCoeffsUVXY = nDegreeUVXY + 1;
   memset( pdCoeffsPXY, 0, cCoeffsPXY * cCoeffsPXY * sizeof(DOUBLE) );  // Clear output

   // Allocate arrays for powers of U and V
   INT nDegreeUVXYPower = nDegreePUV * nDegreeUVXY,   // Degree of U(x,y) or V(x,y) to power of degree P(u,v)
      cCoeffsUVXYPower = nDegreeUVXYPower + 1,
      cMatrixUVXYPower = cCoeffsUVXYPower * cCoeffsUVXYPower,   // Elements in [Y][X] coefficients submatrix
      k = cCoeffsPUV * cMatrixUVXYPower;        // Total U^n or V^n matrix size
   std::unique_ptr< DOUBLE[] >
      apdCoeffsUPower( new DOUBLE[ k ] ), // B[power of U][power of Y][power of X]
      apdCoeffsVPower( new DOUBLE[ k ] ); // C[power of U][power of Y][power of X]
   memset( apdCoeffsUPower.get(), 0, k * sizeof( DOUBLE ) );
   memset( apdCoeffsVPower.get(), 0, k * sizeof( DOUBLE ) );

   // Load the 0th powers
   *( apdCoeffsUPower.get() + 0 ) = 1.0;     // B[0][0][0]
   *( apdCoeffsVPower.get() + 0 ) = 1.0;     // C[0][0][0]

   // Compute the higher powers by recursion
   DOUBLE
      *pdCoeffsUPower2 = apdCoeffsUPower.get(),
      *pdCoeffsVPower2 = apdCoeffsVPower.get();
   for ( INT iN = 1; iN <= nDegreePUV; iN++ )
   {
      // Advance to next power of U or V
      DOUBLE
         *pdCoeffsUPower1 = pdCoeffsUPower2,
         *pdCoeffsVPower1 = pdCoeffsVPower2;
      pdCoeffsUPower2 += cMatrixUVXYPower;
      pdCoeffsVPower2 += cMatrixUVXYPower;

      for ( INT iY1 = 0; iY1 <= nDegreeUVXY; iY1++ )
      {
         for ( INT iX1 = 0; iX1 <= nDegreeUVXY; iX1++ )
         {
            for ( INT iYN = iY1; iYN <= nDegreeUVXYPower; iYN++ )
            {
               for ( INT iXN = iX1; iXN <= nDegreeUVXYPower; iXN++ )
               {
                  pdCoeffsUPower2[ iXN + ( cCoeffsUVXYPower * iYN ) ] +=
                     pdCoeffsUXY[ iX1 + ( cCoeffsUVXY * iY1 ) ]
                     * pdCoeffsUPower1[ iXN - iX1 + ( cCoeffsUVXYPower * ( iYN - iY1 ) ) ];
                     
                  pdCoeffsVPower2[ iXN + ( cCoeffsUVXYPower * iYN ) ] +=
                     pdCoeffsVXY[ iX1 + ( cCoeffsUVXY * iY1 ) ]
                     * pdCoeffsVPower1[ iXN - iX1 + ( cCoeffsUVXYPower * ( iYN - iY1 ) ) ];
               }
            }
         }
      }
   }
   
#ifdef POLY_MULT_TEST
   const DOUBLE *pd = apdCoeffsUPower.get();
   ATLTRACE( _T("U coeff powers:\n") );
   for ( k = 0; k < cCoeffsPUV; k++ )
   {
      Sleep( 50 );
      for ( INT j = 0; j < cCoeffsUVXYPower; j++ )
      {
         for ( INT i = 0; i < cCoeffsUVXYPower; i++ )
            ATLTRACE( _T("%14.4e"), *pd++ );
         if ( j == 0 )
            ATLTRACE( _T("    (%d)"), k );
         ATLTRACE( _T("\n") );
      }
      ATLTRACE( _T("\n") );
   }
   pd = apdCoeffsVPower.get();
   ATLTRACE( _T("V coeff powers:\n") );
   for ( k = 0; k < cCoeffsPUV; k++ )
   {
      Sleep( 50 );
      for ( INT j = 0; j < cCoeffsUVXYPower; j++ )
      {
         for ( INT i = 0; i < cCoeffsUVXYPower; i++ )
            ATLTRACE( _T("%14.4e"), *pd++ );
         if ( j == 0 )
            ATLTRACE( _T("    (%d)"), k );
         ATLTRACE( _T("\n") );
      }
      ATLTRACE( _T("\n") );
   }
#endif

   DOUBLE* pdCoeffsUPower = apdCoeffsUPower.get();
   for ( INT iU = 0; iU <= nDegreePUV; pdCoeffsUPower += cMatrixUVXYPower, iU++ )
   {
      DOUBLE* pdCoeffsVPower = apdCoeffsVPower.get();
      for ( INT iV = 0; iV <= nDegreePUV; pdCoeffsVPower += cMatrixUVXYPower, iV++ )
      {
         // Coefficient of U and V in P
         DOUBLE dCoeffPUV = pdCoeffsPUV[ iU + ( iV * cCoeffsPUV ) ];

         for ( INT iUX = 0; iUX <= nDegreeUVXYPower; iUX++ )
         {
            for ( INT iUY = 0; iUY <= nDegreeUVXYPower; iUY++ )
            {
                for ( INT iVX = 0; iVX <= nDegreeUVXYPower; iVX++ )
               {
                  INT iPX = iUX + iVX;
                  if ( iPX > nDegreePXY )
                     break;

                  for ( INT iVY = 0; iVY <= nDegreeUVXYPower; iVY++ )
                  {
                     INT iPY = iUY + iVY;
                     if ( iPY > nDegreePXY )
                        break;

                     pdCoeffsPXY[ iPX + ( cCoeffsPXY * iPY ) ] +=
                        dCoeffPUV
                        * pdCoeffsUPower[ iUX + ( cCoeffsUVXYPower * iUY ) ]
                        * pdCoeffsVPower[ iVX + ( cCoeffsUVXYPower * iVY ) ];
                  }
               }

            }
         }
      }
   }
#ifdef POLY_MULT_TEST
   Sleep( 100 );
   ATLTRACE( _T("P(U,V) coeffs:\n") );
   pd = pdCoeffsPUV;
   INT iY;
   for ( iY = 0; iY < cCoeffsPUV; iY++ )
   {
      for ( INT iX = 0; iX < cCoeffsPUV; iX++ )
            ATLTRACE( _T("%14.4e"), *pd++ );
      ATLTRACE( _T("\n") );
   }

   Sleep( 100 );
   ATLTRACE( _T("P(X,Y) coeffs:\n") );
   pd = pdCoeffsPXY;
   for ( iY = 0; iY < cCoeffsPXY; iY++ )
   {
      for ( INT iX = 0; iX < cCoeffsPXY; iX++ )
            ATLTRACE( _T("%14.4e"), *pd++ );
      ATLTRACE( _T("\n") );
   }
#endif
}
// End of PolynomialMultiply()


// ****************************************************************
// ****************************************************************

int CTransform::inv_transform( double x, double y,
                                       double &latitude, double &longitude )
{
   // performs inverse transform: returns longitude and latitude corresponding
   // to the arguments x and y
   // returns SUCCESS or FAILURE
   if ( m_defined )
   {
      switch ( m_eTransformMode )
      {
         case TRANSFORM_MODE_NORMAL:
            return InvXfrmNormal( x, y, latitude, longitude );
 
         case TRANSFORM_MODE_POLYNOMIAL:
         case TRANSFORM_MODE_SVD_POLYNOMIAL:
            return InvXfrmPolynomial( x, y, latitude, longitude );

         case TRANSFORM_MODE_RPC00B:
            return InvXfrmRPC00B( x, y, latitude, longitude );
         
      default:
         break;         // Illegal
         
      }  // Transformation mode

   }  // m_defined      
   return FAILURE;
}
// end of inv_transform

int CTransform::InvXfrmNormal( double x, double y,
                                       double &latitude, double &longitude )
{
   int i;
   double r_sqrd, dx, dy;

   // check for defined transformation
   longitude = 0.0;
   latitude = 0.0;

   for( i = 0; i < m_num_tiepoints; i++ )
   {
      dx = x - m_tiepoint_x[i];
      dy = y - m_tiepoint_y[i];
      r_sqrd = dx*dx + dy*dy;
      if( r_sqrd < m_aprox0 )
      {
         latitude = m_tiepoint_latitude[i];
         longitude = m_tiepoint_longitude[i];
         goto SUCCEED;
      }
      latitude += r_sqrd * log( r_sqrd ) * m_latitude_coef[i];
      longitude += r_sqrd * log( r_sqrd ) * m_longitude_coef[i];
   }

   latitude += m_latitude_coef[i];
   longitude += m_longitude_coef[i];

   latitude += x * m_latitude_coef[i+1];
   longitude += x * m_longitude_coef[i+1];

   latitude += y * m_latitude_coef[i+2];
   longitude += y * m_longitude_coef[i+2];

SUCCEED:
   if ( m_bLonUnwrapMode )
   {
      if ( longitude > 180.0 )
         longitude -= 360.0;
      else if ( longitude < -180.0 )
         longitude += 360.0;
   }
   return SUCCESS;
}  // End of InvXfrmNormal()


int CTransform::InvXfrmPolynomial( double x, double y,
                                       double &latitude, double &longitude )
{
   latitude = m_dLatitudeOrigin;    // Init polynomial sums
   longitude = m_dLongitudeOrigin;

   DOUBLE dYPow = 1.0;
   DOUBLE *pLatitudeCoef = m_latitude_coef,
      *pLongitudeCoef = m_longitude_coef;
   INT iY = m_cPolynomialTerms;
   do
   {
      DOUBLE dXYPow = dYPow;
      INT iX = m_cPolynomialTerms;
      do
      {
         latitude += dXYPow * *pLatitudeCoef++;
         longitude += dXYPow * *pLongitudeCoef++;
         if ( --iX <= 0 )
            break;
         dXYPow *= x;
      } while ( TRUE );    // Until break
      if ( -- iY <= 0 )
         break;
      dYPow *= y;
   } while ( TRUE );       // Until break

   if ( m_bLonUnwrapMode )
   {
      if ( longitude > 180.0 )
         longitude -= 360.0;
      else if ( longitude < -180.0 )
         longitude += 360.0;
   }
   return SUCCESS;
}  // End of InvXfrmPolynomial()

 
int CTransform::InvXfrmRPC00B( double x, double y,
                                       double &latitude, double &longitude )
{
   int iResult = FAILURE;
   CDTEDInstance dted;              // We might need elevation data
   BOOL bGeoidDatumSet = FALSE;     // Do only once
   INT cPasses = 0;
   DOUBLE dXError, dYError;
   do    // Til converges
   {
      DOUBLE
         *pdLineNumCoeffs,
         *pdLineDenCoeffs,
         *pdSampNumCoeffs,
         *pdSampDenCoeffs;

      // Evaluate the RPC00B polynomials at this point.  The coefficients have had the PLHXY scalings
      // removed and the XY offsets but not the PLH offsets
      DOUBLE
         dHPower = 1.0,
         dLineNum = 0.0, dLineDen = 0.0,
         dSampNum = 0.0, dSampDen = 0.0;

      INT
         iHPower = 0,
         iHPowerLimit = m_bRPC00BHeightSensitive ? m_cPolynomialTerms : 1;
      do // H power loop
      {
         DOUBLE
            dLHPower = dHPower;

         INT
            iLPower = 0,
            iLPowerLimit = m_cPolynomialTerms - iHPower;
         do // L power loop
         {
            DOUBLE dPLHPower = dLHPower;
            INT
               iPPower = 0,
               iPPowerLimit = iLPowerLimit - iLPower,
               iTermOffset = m_cPolynomialTerms * ( iLPower + ( m_cPolynomialTerms * iHPower ) );
            
            pdLineNumCoeffs = m_apdRatPolyCoeffs[ LINE_NUM_COEFFS ].get() + iTermOffset,
            pdLineDenCoeffs = m_apdRatPolyCoeffs[ LINE_DEN_COEFFS ].get() + iTermOffset,
            pdSampNumCoeffs = m_apdRatPolyCoeffs[ SAMP_NUM_COEFFS ].get() + iTermOffset,
            pdSampDenCoeffs = m_apdRatPolyCoeffs[ SAMP_DEN_COEFFS ].get() + iTermOffset;
            
            do // P power loop
            {
               dLineNum += dPLHPower * pdLineNumCoeffs[ iPPower ];
               dLineDen += dPLHPower * pdLineDenCoeffs[ iPPower ];
               dSampNum += dPLHPower * pdSampNumCoeffs[ iPPower ];
               dSampDen += dPLHPower * pdSampDenCoeffs[ iPPower ];

               if ( ++iPPower >= iPPowerLimit )  // Limit to max degree
                  break;

               dPLHPower *= m_dRPC00BLat;
            } while ( TRUE );    // Latitude power loop

            pdLineNumCoeffs += m_cPolynomialTerms;
            pdLineDenCoeffs += m_cPolynomialTerms;
            pdSampNumCoeffs += m_cPolynomialTerms;
            pdSampDenCoeffs += m_cPolynomialTerms;

            if ( ++iLPower >= iLPowerLimit )
               break;

            dLHPower *= m_dRPC00BLon;
         } while ( TRUE );       // Longitude power loop

         if ( ++iHPower >= iHPowerLimit )
            break;

         dHPower *= m_dRPC00BHgt;
      } while ( TRUE );    // Height power loop

      // Calculate partial derivatives
      pdLineNumCoeffs = m_apdRatPolyCoeffs[ LINE_NUM_COEFFS ].get();
      pdSampNumCoeffs = m_apdRatPolyCoeffs[ SAMP_NUM_COEFFS ].get();
      pdLineDenCoeffs = m_apdRatPolyCoeffs[ LINE_DEN_COEFFS ].get();
      pdSampDenCoeffs = m_apdRatPolyCoeffs[ SAMP_DEN_COEFFS ].get();

#define CF( p, l ) ( p + ( m_cPolynomialTerms * l ) ) // 2-D array lookup

      DOUBLE
         dDSDP = ( pdSampNumCoeffs[ CF( 1, 0 ) ]
               + ( 2.0 * m_dRPC00BLat * ( pdSampNumCoeffs[ CF( 2, 0 ) ]
               + ( 1.5 * m_dRPC00BLat * pdSampNumCoeffs[ CF( 3, 0 ) ] ) ) )
               + ( m_dRPC00BLon * ( pdSampNumCoeffs[ CF( 1, 1 ) ]
               + ( 2.0 * m_dRPC00BLat * pdSampNumCoeffs[ CF( 2, 1 ) ] )
               + ( m_dRPC00BLon * pdSampNumCoeffs[ CF( 1, 2 ) ] ) ) )
            - ( dSampNum * ( pdSampDenCoeffs[ CF( 1, 0 ) ]
               + ( 2.0 * m_dRPC00BLat * ( pdSampDenCoeffs[ CF( 2, 0 ) ]
               + ( 1.5 * m_dRPC00BLat * pdSampDenCoeffs[ CF( 3, 0 ) ] ) ) )
               + ( m_dRPC00BLon * (
                  pdSampDenCoeffs[ CF( 1, 1 ) ] + ( 2.0 * m_dRPC00BLat * pdSampDenCoeffs[ CF( 2, 1 ) ] )
               + ( m_dRPC00BLon * pdSampDenCoeffs[ CF( 1, 2 ) ] ) ) ) ) / dSampDen )
            ) / dSampDen,

         dDSDL = ( pdSampNumCoeffs[ CF( 0, 1 ) ] + ( m_dRPC00BLat * ( pdSampNumCoeffs[ CF( 1, 1 ) ]
               + ( m_dRPC00BLat * pdSampNumCoeffs[ CF( 2, 1 ) ] ) ) )
               + ( 2.0 * m_dRPC00BLon * ( pdSampNumCoeffs[ CF( 0, 2 ) ] + ( m_dRPC00BLat * pdSampNumCoeffs[ CF( 1, 2 ) ] )
               + ( 1.5 * m_dRPC00BLon * pdSampNumCoeffs[ 12 ] ) ) )
            - ( dSampNum * ( pdSampDenCoeffs[ CF( 0, 1 ) ] + ( m_dRPC00BLat * ( pdSampDenCoeffs[ CF( 1, 1 ) ]
               + ( m_dRPC00BLat * pdSampDenCoeffs[ CF( 2, 1 ) ] ) ) )
               + ( 2.0 * m_dRPC00BLon * ( pdSampDenCoeffs[ CF( 0, 2 ) ] + ( m_dRPC00BLat * pdSampDenCoeffs[ CF( 1, 2 ) ] )
               + ( 1.5 * m_dRPC00BLon * pdSampDenCoeffs[ CF( 0, 3 ) ] ) ) ) ) / dSampDen )
            ) / dSampDen,

         dDLDP = ( pdLineNumCoeffs[ CF( 1, 0 ) ]
               + ( 2.0 * m_dRPC00BLat * ( pdLineNumCoeffs[ CF( 2, 0 ) ]
               + ( 1.5 * m_dRPC00BLat * pdLineNumCoeffs[ CF( 3, 0 ) ] ) ) )
               + ( m_dRPC00BLon * (
                  pdLineNumCoeffs[ CF( 1, 1 ) ] + ( 2.0 * m_dRPC00BLat * pdLineNumCoeffs[ CF( 2, 1 ) ] )
               + ( m_dRPC00BLon * pdLineNumCoeffs[ CF( 1, 2 ) ] ) ) )
            - ( dLineNum * ( pdLineDenCoeffs[ CF( 1, 0 ) ]
               + ( 2.0 * m_dRPC00BLat * ( pdLineDenCoeffs[ CF( 2, 0 ) ]
               + ( 1.5 * m_dRPC00BLat * pdLineDenCoeffs[ CF( 3, 0 ) ] ) ) )
               + ( m_dRPC00BLon * (
                  pdLineDenCoeffs[ CF( 1, 1 ) ] + ( 2.0 * m_dRPC00BLat * pdLineDenCoeffs[ CF( 2, 1 ) ] )
               + ( m_dRPC00BLon * pdLineDenCoeffs[ CF( 1, 2 ) ] ) ) ) ) / dLineDen )
            ) / dLineDen,

         dDLDL = ( pdLineNumCoeffs[ CF( 0, 1 ) ] + ( m_dRPC00BLat* ( pdLineNumCoeffs[ CF( 1, 1 ) ]
               + ( m_dRPC00BLat * pdLineNumCoeffs[ CF( 2, 1 ) ] ) ) )
               + ( 2.0 * m_dRPC00BLon * ( pdLineNumCoeffs[ CF( 0, 2 ) ] + ( m_dRPC00BLat * pdLineNumCoeffs[ CF( 1, 2 ) ] )
               + ( 1.5 * m_dRPC00BLon * pdLineNumCoeffs[ CF( 0, 3 ) ] ) ) )
            - ( dLineNum * ( pdLineDenCoeffs[ CF( 0, 1 ) ] + ( m_dRPC00BLat * ( pdLineDenCoeffs[ CF( 1, 1 ) ]
               + ( m_dRPC00BLat * pdLineDenCoeffs[ CF( 2, 1 ) ] ) ) )
               + ( 2.0 * m_dRPC00BLon * ( pdLineDenCoeffs[ CF( 0, 2 ) ] + ( m_dRPC00BLat * pdLineDenCoeffs[ CF( 1, 2 ) ] )
               + ( 1.5 * m_dRPC00BLon * pdLineDenCoeffs[ CF( 0, 3 ) ] ) ) ) ) / dLineDen )
            ) / dLineDen,

         d = ( dDLDL * dDSDP ) - ( dDSDL * dDLDP );

      dXError = x - ( dSampNum / dSampDen );
      dYError = y - ( dLineNum / dLineDen );

      m_dRPC00BLon += ( ( dDSDP * dYError ) - ( dDLDP * dXError ) ) / d;
      m_dRPC00BLat += ( ( dDLDL * dXError ) - ( dDSDL * dYError ) ) / d;

      latitude = m_dRPC00BLat + m_dLatitudeOrigin;
      longitude = m_dRPC00BLon + m_dLongitudeOrigin;

      // If accurate enough, exit
      if ( fabs( dXError ) <= 0.05 && fabs( dYError ) <= 0.05
         && cPasses >= 4 )
      {
         iResult = SUCCESS;
         break;
      }

      do
      {
         if ( !m_bRPC00BHeightSensitive || cPasses > 4 )
            break;

         if ( !dted.IsValid() )
            break;                  // No DTED available

#ifdef _WIN32  // POSIX: IsValid() is always FALSE (see
               // fv_dted_instance_posix.h), so this block is unreachable
         INT iErrorCode;
         _bstr_t bstrErrorMsg;
         if ( !bGeoidDatumSet )
         {
            if ( S_OK != dted.IGeoid()->set_vertical_datum( 1996, &iErrorCode, bstrErrorMsg.GetAddress() )
                  || iErrorCode != 0 )
               break;      // Geoid error
            bGeoidDatumSet = TRUE;
         }

         LONG lElevation;
         SHORT iLevelUsed;
         if ( S_OK != dted.IDted()->raw_GetElevation(
                  latitude, longitude,
                  0, DTED_ELEVATION_METERS, &iLevelUsed, &lElevation )
               || lElevation == MISSING_DTED_ELEVATION
               || iLevelUsed == 0 )
            break;      // No elevation value available

         DOUBLE dHAEMSLDelta;
         if ( S_OK != dted.IGeoid()->raw_get_geoid_delta( latitude, longitude,
               &dHAEMSLDelta, &iErrorCode, bstrErrorMsg.GetAddress() ) )
            break;

         dHAEMSLDelta = FEET_TO_METERS( dHAEMSLDelta );

         // Remove the height offset
         m_dRPC00BHgt = lElevation + dHAEMSLDelta - m_dHeightOrigin;
#endif  // _WIN32

      } while ( FALSE );

      if ( ++cPasses >= 20 )
#if 0 && defined _DEBUG
      {
         WriteToLogFile( L"CTransform::InvXfrmRPC00B() - 20 passes w/o convergence" );
         break;
      }
#else
         break;
#endif

   } while ( TRUE );

   return iResult;
}
// end of InvXfrmRPC00B()


// ****************************************************************
// ****************************************************************

int CTransform::fwd_transform( double latitude, double longitude,
                                       double &x, double &y )
{
   // performs forward transform: returns x and y corresponding
   // to the arguments longitude and latitude
   // returns SUCCESS or FAILURE

   if ( m_defined )
   {
      switch ( m_eTransformMode )
      {
         case TRANSFORM_MODE_NORMAL:
         {
            int i;
            double r_sqrd, d_latitude, d_longitude;
            double tlon;
            
            tlon = longitude;
            if (m_idl_cross)
            {
               if (longitude < 0.0)
                  tlon += 360.0;
            }
            
            // check for defined transformation
            if( !m_defined )
               goto FAIL;
            
            x = 0.0;
            y = 0.0;
            
            for( i = 0; i < m_num_tiepoints; i++ )
            {
               d_latitude = latitude - m_tiepoint_latitude[i];
               d_longitude = tlon - m_tiepoint_longitude[i];
               r_sqrd = d_latitude*d_latitude + d_longitude*d_longitude;
               if( r_sqrd < m_aprox0 )
               {
                  x = m_tiepoint_x[i];
                  y = m_tiepoint_y[i];
                  goto SUCCEED;
               }
               x += r_sqrd * log( r_sqrd ) * m_x_coef[i];
               y += r_sqrd * log( r_sqrd ) * m_y_coef[i];
            }
            
            x += m_x_coef[i];
            y += m_y_coef[i];
            
            x += tlon * m_x_coef[i+1];
            y += tlon * m_y_coef[i+1];
            
            x += latitude * m_x_coef[i+2];
            y += latitude * m_y_coef[i+2];

SUCCEED:
            return SUCCESS;
         }

         case TRANSFORM_MODE_SVD_POLYNOMIAL:
         case TRANSFORM_MODE_POLYNOMIAL:
         {
            latitude -= m_dLatitudeOrigin;
            longitude -= m_dLongitudeOrigin;
            DOUBLE
               dLonPow = 1.0,
               *pXCoef = m_x_coef,
               *pYCoef = m_y_coef;
            x = y = 0.0;
            INT iLon = m_cPolynomialTerms;
            do
            {
               DOUBLE dLonLatPow = dLonPow;
               INT iLat = m_cPolynomialTerms;
               do
               {
                  x += dLonLatPow * *pXCoef++;
                  y += dLonLatPow * *pYCoef++;
                  if ( --iLat <= 0 )
                     break;
                  dLonLatPow *= latitude;
               } while ( TRUE );    // Until break
               if ( --iLon <= 0 )
                  break;
               dLonPow *= longitude;
            } while ( TRUE );       // Until break
            
            return SUCCESS;
         }
         
         case TRANSFORM_MODE_RPC00B:
         {
            // Offset lat/long to RPC00B origin
            latitude -= m_dLatitudeOrigin;
            longitude -= m_dLongitudeOrigin;
            DOUBLE
               dLongPower = 1.0,
               *pdLineNumCoeffs = m_apdRatPolyCoeffs[ LINE_NUM_COEFFS ].get(),
               *pdLineDenCoeffs = m_apdRatPolyCoeffs[ LINE_DEN_COEFFS ].get(),
               *pdSampNumCoeffs = m_apdRatPolyCoeffs[ SAMP_NUM_COEFFS ].get(),
               *pdSampDenCoeffs = m_apdRatPolyCoeffs[ SAMP_DEN_COEFFS ].get(),
               dLineNum = 0.0, dLineDen = 0.0,
               dSampNum = 0.0, dSampDen = 0.0;

            INT
               iLongPower = 0,
               iLatPowerLimit = m_cPolynomialTerms;
            do
            {
               DOUBLE dLatLongPower = dLongPower;
               INT
                  iLatPower = 0;
               do
               {
                  dLineNum += dLatLongPower * pdLineNumCoeffs[ iLatPower ];
                  dLineDen += dLatLongPower * pdLineDenCoeffs[ iLatPower ];
                  dSampNum += dLatLongPower * pdSampNumCoeffs[ iLatPower ];
                  dSampDen += dLatLongPower * pdSampDenCoeffs[ iLatPower ];

                  if ( ++iLatPower >= iLatPowerLimit )  // Limit to max degree
                     break;
               
                  dLatLongPower *= latitude;
               } while ( TRUE );    // Until break
               
               if ( ++iLongPower >= m_cPolynomialTerms )
                  break;

               dLongPower *= longitude;
               pdLineNumCoeffs += m_cPolynomialTerms;
               pdLineDenCoeffs += m_cPolynomialTerms;
               pdSampNumCoeffs += m_cPolynomialTerms;
               pdSampDenCoeffs += m_cPolynomialTerms;
               iLatPowerLimit--;
            } while ( TRUE );       // Until break
            
            x = dSampNum / dSampDen;
            y = dLineNum / dLineDen;
            
            return SUCCESS;
         }
         
      default:
         break;         // Illegal
         
      }  // Transformation mode

   }  // m_defined      
FAIL:
   return FAILURE;
}
// end of fwd_transform

// ****************************************************************
// ****************************************************************

