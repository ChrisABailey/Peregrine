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

// MetadataStructs.cpp: implementations for various image file metadata items.
//
//////////////////////////////////////////////////////////////////////




#include "StdAfx.h"
#include "MetadataStructs.h"


//
// CTREDataACCPOBPosAcc
//
CTREDataACCPOBPosAcc::CTREDataACCPOBPosAcc()
{
   m_dABS_HORZ_ACC = 0.0;
   m_dABS_VERT_ACC = 0.0;
   m_dPTP_HORZ_ACC = 0.0;
   m_dPTP_VERT_ACC = 0.0;
   m_pLOC = NULL;
   m_iNUM_POINTS = 0;
}


CTREDataACCPOBPosAcc::	~CTREDataACCPOBPosAcc()
{
   delete [] m_pLOC;
}

CTREDataACCPOBPosAcc& CTREDataACCPOBPosAcc::operator=( const CTREDataACCPOBPosAcc &posacc )
{
   m_strUM_HORZ = posacc.m_strUM_HORZ;
   m_dABS_HORZ_ACC = posacc.m_dABS_HORZ_ACC;
   m_strUM_VERT = posacc.m_strUM_VERT;
   m_dABS_VERT_ACC = posacc.m_dABS_VERT_ACC;
   m_strUM_PTP_HORZ = posacc.m_strUM_PTP_HORZ;
   m_dPTP_HORZ_ACC = posacc.m_dPTP_HORZ_ACC;
   m_strUM_PTP_VERT = posacc.m_strUM_PTP_VERT;
   m_dPTP_VERT_ACC = posacc.m_dPTP_VERT_ACC;

   delete [] m_pLOC;
   m_pLOC = NULL;
   if ( ( m_iNUM_POINTS = posacc.m_iNUM_POINTS ) > 0 )
   {
      m_pLOC = new CTREDataLoc[ m_iNUM_POINTS ];
      for ( INT k = 0; k < m_iNUM_POINTS; k++ )
         m_pLOC[ k ] = posacc.m_pLOC[ k ];
   }
   return *this;
}

//
// CTREDataPoly
//

CTREDataPoly::CTREDataPoly()
{
   ZeroMemory( this, sizeof(*this) );
}


CTREDataPoly::~CTREDataPoly()
{
	delete [] m_pLOC;
}


CTREDataPoly& CTREDataPoly::operator=( const CTREDataPoly &poly )
{
   delete [] m_pLOC;
   m_pLOC = NULL;

   if ( ( m_iNUM_POINTS = poly.m_iNUM_POINTS ) > 0 )
   {
      m_pLOC = new CTREDataLoc[ m_iNUM_POINTS ];
      memcpy( m_pLOC, poly.m_pLOC, m_iNUM_POINTS * sizeof(CTREDataPoly) );
   }
   return *this;
}  // operator=


//
// CTREDataSOURCBSource
//

CTREDataSOURCBSource::CTREDataSOURCBSource()
{
   m_iNUM_POLY = 0;		
   m_pPOLY = NULL;
   m_dSCALE = 0.0;	
   m_dCON_INT = 0.0;		
   m_dHKE = 0.0;				
   m_dHKE_LON = 0.0;			
   m_dHKE_LAT = 0.0;		
   m_iNUM_MAG = 0;		
   m_pMAG_INFO = NULL;
   m_iNUM_LEG_IMG = 0;	
   m_pLEGEND_ID = NULL;
   m_iNUM_PRJ_PARAM = 0;	
   m_pPRJ_PARAM = NULL;	
   m_dPRJ_FALSE_X = 0.0;	
   m_dPRJ_FALSE_Y = 0.0;	
   m_iNUM_INSET = 0;
}

CTREDataSOURCBSource::~CTREDataSOURCBSource()
{
   delete [] m_pPOLY;
   delete [] m_pMAG_INFO;
   delete [] m_pLEGEND_ID;
   delete [] m_pPRJ_PARAM;
}

	
CTREDataSOURCBSource& CTREDataSOURCBSource::operator=( const CTREDataSOURCBSource &src )
{
   delete [] m_pPOLY;
   m_pPOLY = NULL;

   if ( ( m_iNUM_POLY = src.m_iNUM_POLY ) > 0 )
   {
      m_pPOLY = new CTREDataPoly[ m_iNUM_POLY ];
      for ( INT k = 0; k < m_iNUM_POLY; k++)
         m_pPOLY[ k ] = src.m_pPOLY[ k ];
   }

   m_strSERIES = src.m_strSERIES;		
   m_strSOURCE_ID = src.m_strSOURCE_ID;	
   m_strEDITION = src.m_strEDITION;		
   m_strNAME = src.m_strNAME;		
   m_strTYPE_DATE = src.m_strTYPE_DATE;	
   m_strSIG_DATE = src.m_strSIG_DATE;	
   m_strPERISH_DATE = src.m_strPERISH_DATE;	
   m_strSRC_REF_NUM = src.m_strSRC_REF_NUM;	
   m_dSCALE = src.m_dSCALE;	
   m_strUM_COV = src.m_strUM_COV;		
   m_strCOV = src.m_strCOV;			
   m_strUM_CON_INT = src.m_strUM_CON_INT;	
   m_dCON_INT = src.m_dCON_INT;		
   m_iWATER_COV = src.m_iWATER_COV;	
   m_iNAV_SYS_TYPE =src.m_iNAV_SYS_TYPE;
   m_strUM_HKE = src.m_strUM_HKE;		
   m_dHKE = src.m_dHKE;			
   m_dHKE_LON = src.m_dHKE_LON;		
   m_dHKE_LAT = src.m_dHKE_LAT;		
   m_strCLASS = src.m_strCLASS;		
   m_strDOWNGRADE = src.m_strDOWNGRADE;	
   m_strDG_DATE = src.m_strDG_DATE;		
   m_strQLE = src.m_strQLE;			
   m_strCPY = src.m_strCPY;

   delete [] m_pMAG_INFO;
   m_pMAG_INFO = NULL;
   if ( ( m_iNUM_MAG = src.m_iNUM_MAG ) > 0 )
   {
      m_pMAG_INFO = new CTREDataSOURCBMagInfo[ m_iNUM_MAG ];
      for ( INT k = 0; k < m_iNUM_MAG; k++ )
         m_pMAG_INFO[ k ] = src.m_pMAG_INFO[ k ];
   }

   delete [] m_pLEGEND_ID;
   m_pLEGEND_ID = NULL;
   if ( ( m_iNUM_LEG_IMG = src.m_iNUM_LEG_IMG ) > 0 )
   {
      m_pLEGEND_ID = new CString[ m_iNUM_LEG_IMG ];
      for ( INT k = 0; k < m_iNUM_LEG_IMG; k++ )
         m_pLEGEND_ID[ k ] = src.m_pLEGEND_ID[ k ];
   }

   m_strGEO_DAT_NAME = src.m_strGEO_DAT_NAME;
   m_strGEO_DAT_CODE = src.m_strGEO_DAT_CODE;
   m_strELP_NAME = src.m_strELP_NAME;	
   m_strELP_CODE = src.m_strELP_CODE;	
   m_strVER_DAT_NAME = src.m_strVER_DAT_NAME;
   m_strVER_DAT_CODE = src.m_strVER_DAT_CODE;
   m_strSND_DAT_NAME = src.m_strSND_DAT_NAME;
   m_strSND_DAT_CODE = src.m_strSND_DAT_CODE;
   m_strPRJ_NAME = src.m_strPRJ_NAME;	
   m_strPRJ_CODE = src.m_strPRJ_CODE;	

   delete [] m_pPRJ_PARAM;
   m_pPRJ_PARAM = NULL;
   if ( ( m_iNUM_PRJ_PARAM = src.m_iNUM_PRJ_PARAM ) > 0 )
   {
      m_pPRJ_PARAM = new CString[ m_iNUM_PRJ_PARAM ];
      for ( INT k = 0; k < m_iNUM_PRJ_PARAM; k++ )
         m_pPRJ_PARAM[ k ] = src.m_pPRJ_PARAM[ k ];
   }
   m_dPRJ_FALSE_X = src.m_dPRJ_FALSE_X;	
   m_dPRJ_FALSE_Y = src.m_dPRJ_FALSE_Y;	
   m_strGRD_CODE = src.m_strGRD_CODE;	
   m_strGRD_DESC = src.m_strGRD_DESC;	
   m_strGRD_ZONE = src.m_strGRD_ZONE;	
   m_iNUM_INSET = src.m_iNUM_INSET;	

   return *this;
}  // &operator=


//
// CTREDataACCPOB
//

CTREDataACCPOB::CTREDataACCPOB()
{
   ZeroMemory( this, sizeof(*this) );
}


CTREDataACCPOB::~CTREDataACCPOB()
{
   delete [] m_pACCPOS;
}


CTREDataACCPOB& CTREDataACCPOB::operator=( const CTREDataACCPOB &acc )
{
   m_bSUCCESS = acc.m_bSUCCESS;

   delete [] m_pACCPOS;
   if ( ( m_iNUM_ACPO = acc.m_iNUM_ACPO ) == 0 )
      m_pACCPOS = NULL;

   else
   {
      m_pACCPOS = new CTREDataACCPOBPosAcc[ m_iNUM_ACPO ];
      for ( INT k = 0; k < m_iNUM_ACPO; k++ )
         m_pACCPOS[ k ] = acc.m_pACCPOS[ k ];
   }
   return *this;
}


//
// CTREDataSOURCB
//
CTREDataSOURCB::CTREDataSOURCB()
{
   m_bSUCCESS = FALSE;
   m_dSCALE = 0.0;
   m_iNUM_SRC = 0;
   m_pSrc = NULL;
}


CTREDataSOURCB::~CTREDataSOURCB()
{
   delete [] m_pSrc;
}


CTREDataSOURCB& CTREDataSOURCB::operator=( const CTREDataSOURCB &src )
{
   m_bSUCCESS = src.m_bSUCCESS;
   m_dSCALE = src.m_dSCALE;

   delete [] m_pSrc;
   m_pSrc = NULL;
   if ( ( m_iNUM_SRC = src.m_iNUM_SRC ) > 0 )
   {
      m_pSrc = new CTREDataSOURCBSource[ m_iNUM_SRC ];
      for ( INT k = 0; k < m_iNUM_SRC; k++ )
         m_pSrc[ k ] = src.m_pSrc[ k ];
   }
   return *this;

}  // &operator=


//
// CTREDataRPCHDR
//

CTREDataRPFHDR::CTREDataRPFHDR()
{
   ZeroMemory( this, sizeof(*this) );
   m_bLittleEndian = FALSE;      // Redundant
}


CTREDataRPFHDR::~CTREDataRPFHDR()
{
}


CTREDataRPFHDR& CTREDataRPFHDR::operator=( const CTREDataRPFHDR &hdr )
{
   memcpy( this, &hdr, sizeof(*this) );
   return *this;

}  // &operator=


//
// CTREDataRPCIMG
//

CTREDataRPFIMG::CComponentLocation::CComponentLocation()
{
   ZeroMemory( this, sizeof(*this) );
}


CTREDataRPFIMG::CColorGSOffset::CColorGSOffset()
{
   ZeroMemory( this, sizeof(*this) );
}


CTREDataRPFIMG::CTREDataRPFIMG()
{
   ZeroMemory( this, sizeof(*this) );
}


CTREDataRPFIMG::~CTREDataRPFIMG()
{
   delete [] m_pComponentLocations;
   delete [] m_pColorMapOffsets;
}


CTREDataRPFIMG& CTREDataRPFIMG::operator=( const CTREDataRPFIMG &img )
{
   delete [] m_pComponentLocations;
   delete [] m_pColorMapOffsets;
   memcpy( this, &img, sizeof(*this) );

   m_pComponentLocations = NULL;
   if ( m_nComponentLocationRecords > 0 )
   {
      m_pComponentLocations = new CComponentLocation[ m_nComponentLocationRecords ];
      for ( INT k = 0; k < m_nComponentLocationRecords; k++ )
         m_pComponentLocations[ k ] = img.m_pComponentLocations[ k ];
   }

   m_pColorMapOffsets = NULL;
   if ( m_nColorGSOffsetRecords > 0 )
   {
      m_pColorMapOffsets = new CColorGSOffset[ m_nColorGSOffsetRecords ];
      for ( INT k = 0; k < m_nColorGSOffsetRecords; k++ )
         m_pColorMapOffsets[ k ] = img.m_pColorMapOffsets[ k ];
   }

   return *this;
}  // &operator=

// End of MetadataStructs.cpp

