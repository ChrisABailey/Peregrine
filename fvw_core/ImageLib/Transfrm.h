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

// transfrm.h - Declaration of the CTransform class


// 25-Mar-2005  (RAC)  Add support for generalized polynomial coordinate transforms

#pragma once

#include "MetadataStructs.h"     // For RPC00B


class CTransform
{
   friend class CImageLib;

public:
   enum TransformationModeEnum
   {
      TRANSFORM_MODE_NORMAL,        // Original
      TRANSFORM_MODE_POLYNOMIAL,    // Simple polynomial
      TRANSFORM_MODE_RPC00B,        // Rapid Positioning Capability rational polynomial
      TRANSFORM_MODE_ICHIPB,        // Chipped NITF image
      TRANSFORM_MODE_RPC00B_ICHIPB, // ICHIPB chip from RPC image
      TRANSFORM_MODE_SVD_POLYNOMIAL // Truncated polynomial from SVD fit
   };

   CTransform( BOOL bGeoMode = TRUE,
      TransformationModeEnum eTransformationMode = TRANSFORM_MODE_NORMAL );
   ~CTransform( );

   int define_tiepoints( int num_tiepoints, const double *x, const double *y,
                         const double *latitude, const double *longitude );
   int inv_transform( double x, double y, double &latitude, double &longitude );
   int fwd_transform( double latitude, double longitude, double &x, double &y );
   VOID DefineRPC00BTransform(
                        const CTREDataRPC00B& tredRPCOOB,
                        const CTransform& tfrmICHIPB,
                        DOUBLE dXOriginIn, DOUBLE dYOriginIn,
                        DOUBLE dFactor, DOUBLE dXYOriginOut );
   VOID DefineICHIPBTransform( const CTREDataICHIPB& tredICHIPB );
   VOID SetTransformationMode( TransformationModeEnum eTransformMode )
      {
         m_eTransformMode = eTransformMode;
      }
   VOID SetTransformationModeSVDPolynomial( INT nSVDPolynomialOrderX, INT nSVDPolynomialOrderY )
      {
         m_eTransformMode = TRANSFORM_MODE_SVD_POLYNOMIAL;
         m_nSVDPolynomialOrderX = nSVDPolynomialOrderX;
         m_nSVDPolynomialOrderY = nSVDPolynomialOrderY;
      }
   TransformationModeEnum GetTransformMode() const
      { return m_eTransformMode; }
   VOID SetGeoMode( BOOL bGeoMode ) { m_bGeoMode = bGeoMode; }
   INT SetPolynomialLatLongOrigin( DOUBLE dLatitudeOrigin, DOUBLE dLongitudeOrigin );
   INT SetPolynomialXYScaleAndOrigin(
                        DOUBLE dXOffsetIn, DOUBLE dYOffsetIn,
                        DOUBLE dXScale, DOUBLE dYScale,
                        DOUBLE dXOffsetOut, DOUBLE dYOffsetOut );
   static VOID PolynomialMultiply( INT nDegreeUVXY,
                        const DOUBLE* pdCoeffsUXY, const DOUBLE* pdCoeffsVXY,
                        INT nDegreePUV, const DOUBLE* pdCoeffsPUV,
                        INT nDegreePXY, DOUBLE* pdCoeffsPXY );
   INT SetTransformProduct( CTransform& tfrm1, CTransform& tfrm2 );
   INT GetFwdPolynomialCoeffs( INT& nDegree, DOUBLE& dLatOrigin, DOUBLE& dLonOrigin,
                        const DOUBLE*& pdCoeffsLat, const DOUBLE*& pdCoeffsLon ) const;
   INT GetInvPolynomialCoeffs( INT& nDegree, DOUBLE& dLatOrigin, DOUBLE& dLonOrigin,
                        DOUBLE*& pdCoeffsX, DOUBLE*& pdCoeffsY ) const;
   INT GetRPC00BPolynomialCoeffs(
                        const DOUBLE*& pdLineNumCoeffs, const DOUBLE*& pdLineDenCoeffs,
                        const DOUBLE*& pdSampNumCoeffs, const DOUBLE*& pdSampDenCoeffs ) const;
   INT GetICHIPBCoeffs( DOUBLE (&dCoeffsX)[3], DOUBLE (&dCoeffsY)[3] ) const;
   VOID SetLonUnwrapMode( BOOL bLonUnwrapMode ){ m_bLonUnwrapMode = bLonUnwrapMode; }
   INT CalcPolynomialCoeffs(
                        /*[in]*/ INT cPolynomialTerms,         // Terms in the output polynomial (order + 1 )
                        /*[in]*/ INT cUVWValues,               // Number of W=f(U,W) fit values
                        /*[in]*/ const DOUBLE* pdU,
                        /*[in]*/ const DOUBLE* pdV,
                        /*[in]*/ const DOUBLE* pdW,
                        /*[out,retval]*/ DOUBLE* pdWCoefs );   // Must be [cPolynomialTerms][cPolynomialTerms]

private:
   void clear( );
   void InitMem( INT cTiepoints, INT cCoefs );
   int define_tiepoints_original( int num_tiepoints,
                        const double *x, const double *y,
                        const double *latitude, const double *longitude );
   int DefineTiepointsPolynomial( INT cTiepoints,
                        const DOUBLE* pdX, const double* pdY,
                        const DOUBLE* pdLatitude, const DOUBLE* pdLongitude );
   int InvXfrmNormal( double x, double y, double &latitude, double &longitude );
   int InvXfrmPolynomial( double x, double y, double &latitude, double &longitude );
   int InvXfrmRPC00B( double x, double y, double &latitude, double &longitude );
   int CalcPolynomialCoefs( BOOL bSVDMode,
                        const DOUBLE* pdU, const DOUBLE* pdV,
                        const DOUBLE* pdW, DOUBLE* pdWCoefs );
   int CalcLinearCoefs( const DOUBLE* pdU, const DOUBLE* pdV,
                        const DOUBLE* pdW, DOUBLE* pdWCoefs );
   int invert_matrix( double *matrix, int n, double *inverse_matrix );
   int decompose_lu( double *a, int n, int *indx );
   void back_substitute_lu( double *a, int n, int *indx, double *b );

private:
   BOOL m_defined;
   BOOL m_idl_cross;
   BOOL m_bGeoMode;        // TRUE if tiepoint lat/lons are geo rather than u/v
   BOOL m_bLonUnwrapMode;  // TRUE if calculated longitude values are to be remapped to -180 to +180
   int m_num_tiepoints, m_num_rows;
   static const DOUBLE m_aprox0;
   DoublePtr
         m_apdTiepointsX,
         m_apdTiepointsY,
         m_apdTiepointsLat,
         m_apdTiepointsLon,
         m_apdCoefsX,
         m_apdCoefsY,
         m_apdCoefsLat,
         m_apdCoefsLon;
   double *m_tiepoint_x, *m_tiepoint_y;
   double *m_tiepoint_latitude, *m_tiepoint_longitude;
   double *m_x_coef, *m_y_coef, *m_latitude_coef, *m_longitude_coef;
   DOUBLE m_dLatitudeOrigin, m_dLongitudeOrigin, m_dHeightOrigin;
   TransformationModeEnum m_eTransformMode;
   INT m_nSVDPolynomialOrderX;
   INT m_nSVDPolynomialOrderY;
   INT m_cPolynomialTerms;             // Rows or cols in tiepoint array
                                       // or degree + 1 for RPC00B

   // For RPC00B mode, powers in height, long, and lat
   BOOL        m_bRPC00BHeightSensitive;
   enum        RatPolyCoeffsEnum { LINE_NUM_COEFFS = 0, LINE_DEN_COEFFS = 1, SAMP_NUM_COEFFS = 2, SAMP_DEN_COEFFS = 3 };
   DoublePtr   m_apdRatPolyCoeffs[ 4 ];

   DOUBLE
         m_dICHIPBCoeffsX[3],          // For ICHIPB post-transform
         m_dICHIPBCoeffsY[3];
   DOUBLE
         m_dRPC00BLat,                 // For RPC00B inv_transform() iterations
         m_dRPC00BLon,
         m_dRPC00BHgt;
};

// End of Transform.h
