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

// MetadataStructs.h: declarations for various image file metadata items.
//
//////////////////////////////////////////////////////////////////////




#pragma once

typedef CString TREString;       // May be defined as some other collection such as std::string


class CTREDataLoc
{
public:
	DOUBLE		m_dLAT;
	DOUBLE		m_dLON;

	CTREDataLoc()
	{
      ZeroMemory( this, sizeof(*this) );
	}
}; // CTREDataLoc


class CTREDataPoly
{
public:
	int			m_iNUM_POINTS;
	CTREDataLoc* m_pLOC;

	CTREDataPoly();
	~CTREDataPoly();
	CTREDataPoly &operator=( const CTREDataPoly& poly );

}; // class CTREDataPoly


///////////////////////////////////////////////////////////////////////
// Interesting NITF TRE data
///////////////////////////////////////////////////////////////////////

//
// BLOCKA - See STDI-002 8.3.4
//
class CTREDataBLOCKA
{
public:
   enum BLOCK_INSTANCE_VALIDITY_ENUM
   {
      BLOCK_INSTANCE_INVALID = 0
   };
   INT         m_iBlockInstance;
   INT         m_cNGray;
   INT         m_cLLines;
   INT         m_iLayoverAngle;
   INT         m_iShadowAngle;
   DOUBLE      m_dULLat;
   DOUBLE      m_dULLon;
   DOUBLE      m_dURLat;
   DOUBLE      m_dURLon;
   DOUBLE      m_dLRLat;
   DOUBLE      m_dLRLon;
   DOUBLE      m_dLLLat;
   DOUBLE      m_dLLLon;
   CTREDataBLOCKA()
   {
      m_iBlockInstance = BLOCK_INSTANCE_INVALID;  // Invalid to start with
   }
}; // class CTREDataBLOCKA


//
// BNDPLB - See Digest D1.2.7.7
//
class CTREDataBNDPLB : public CTREDataPoly
{
public:
   CTREDataBNDPLB()
   {
      m_iNUM_POINTS = 0;    // Invalid to start with
   }
}; // class CTREDataBNDPLB


//
// CSCRNA - See STANAG 4545
//
class CTREDataCSCRNA
{
public:
   TREString   m_strPredictedCorners;     // Invalid if blank
   DOUBLE      m_dCornerLats[ 4 ];        // UL/UR/LR/LL
   DOUBLE      m_dCornerLons[ 4 ];
   DOUBLE      m_dCornerHeights[ 4 ];
 
}; // class CTREDataCSCRNA


//
// GEOLOB - See Digest D1.2.7.4
//
class CTREDataGEOLOB
{
public:
   INT         m_iLonDensity;
   INT         m_iLatDensity;
   DOUBLE      m_dLonRefOrigin;
   DOUBLE      m_dLatRefOrigin;
 
   CTREDataGEOLOB()
   {
      m_iLonDensity = 0;   // Invalid to start with
   }
}; // class CTREDataGEOLOB


//
// GEOPSB - See Digest D1.2.7.1
//
class CTREDataGEOPSB
{
public:
   BOOL        m_bValid;
   TREString   m_strCoordSysType;
   TREString   m_strCoordSysUnits;
   TREString   m_strGeodeticDatumName;
   TREString   m_strGeodeticDatumCode;
   TREString   m_strEllipsoidName;
   TREString   m_strEllipsoidCode;
   TREString   m_strVerticalDatumReference;
   TREString   m_strVerticalDatumCode;
   TREString   m_strSoundingDatumName;
   TREString   m_strSoundingDatumCode;
   ULONGLONG   m_ullFalseOriginZ;
   TREString   m_strGridCode;
   TREString   m_strGridDescription;
   TREString   m_strGridZoneNumber; // Text format

   CTREDataGEOPSB()
   {
      m_bValid = FALSE;
   }
}; // class CTREDataGEOPSB


//
// ICHIPB - See STDI-002 5.6.6
//
class CTREDataICHIPB
{
public:
   enum XFRM_FLAG_ENUM
   {
      XFRM_FLAG_INVALID = -1,
      XFRM_FLAG_NONDEWARPED = 0,
      XFRM_FLAG_DEWARPED = +1
   };
   INT         iXFRM_FLAG;
   DOUBLE      dSCALE_FACTOR;
   INT         iANAMRPH_CORR;
   INT         iSCANBLK_NUM;
   DOUBLE      dOP_ROW_11;
   DOUBLE      dOP_COL_11;
   DOUBLE      dOP_ROW_12;
   DOUBLE      dOP_COL_12;
   DOUBLE      dOP_ROW_21;
   DOUBLE      dOP_COL_21;
   DOUBLE      dOP_ROW_22;
   DOUBLE      dOP_COL_22;
   DOUBLE      dFI_ROW_11;
   DOUBLE      dFI_COL_11;
   DOUBLE      dFI_ROW_12;
   DOUBLE      dFI_COL_12;
   DOUBLE      dFI_ROW_21;
   DOUBLE      dFI_COL_21;
   DOUBLE      dFI_ROW_22;
   DOUBLE      dFI_COL_22;
   INT         iFI_ROW;
   INT         iFI_COL;

   CTREDataICHIPB()
   {
      iXFRM_FLAG = XFRM_FLAG_INVALID;  // Invalid to start with
   }
}; // class CTREDataICHIPB


//
// PIAIMC - See STDI-002 6.1
//
class CTREDataPIAIMC
{
public:
   BOOL        m_bValid;
   INT         iCLOUDCVR;
   TREString   strSRP;
   TREString   strSENSMODE;
   TREString   strSENSNAME;
   TREString   strSOURCE;
   INT         iCOMGEN;
   TREString   strSUBQUAL;
   TREString   strPIAMSNNUM;
   TREString   strCAMSPECS;
   TREString   strPROJID;
   INT         iGENERATION;
   TREString   strESD;
   TREString   strOTHERCOND;
   DOUBLE      dMEANGSD;
   TREString   strIDATUM;
   TREString   strIELLIP;
   TREString   strPREPROC;
   TREString   strIPROJ;
   INT         iSATTRACK[2];

   CTREDataPIAIMC()
   {
      m_bValid = FALSE;
   }
};


//
// RPC00B - See STDI-002 8.3.12
//
static const int TRE_RPC00B_N_POLYNOMIAL_COEFF = 20;  // Assume all 4 polynomials are the same length
static const int TRE_RPC00B_N_LINE_NUM_COEFF = 20;
static const int TRE_RPC00B_N_LINE_DEN_COEFF = 20;
static const int TRE_RPC00B_N_SAMP_NUM_COEFF = 20;
static const int TRE_RPC00B_N_SAMP_DEN_COEFF = 20;

// Powers of P, L, and H in RPC00B polynomials
static const INT RPC00BCoeffPLHPowers[][3] =
{
   { 0, 0, 0 },   // 1
   { 0, 1, 0 },
   { 1, 0, 0 },
   { 0, 0, 1 },
   { 1, 1, 0 },   // 5
   { 0, 1, 1 },
   { 1, 0, 1 },
   { 0, 2, 0 },
   { 2, 0, 0 },
   { 0, 0, 2 },   // 10
   { 1, 1, 1 },
   { 0, 3, 0 },
   { 2, 1, 0 },
   { 0, 1, 2 },
   { 1, 2, 0 },   // 15
   { 3, 0, 0 },
   { 1, 0, 2 },
   { 0, 2, 1 },
   { 2, 0, 1 },
   { 0, 0, 3 }    // 20
}; // RPC00BCoeffPLHPowers

class CTREDataRPC00B
{
public:
   INT      iSUCCESS;
   DOUBLE   dERR_BIAS;
   DOUBLE   dERR_RAND;
   DOUBLE   dLINE_OFF;
   DOUBLE   dSAMP_OFF;
   DOUBLE   dLAT_OFF;
   DOUBLE   dLONG_OFF;
   DOUBLE   dHEIGHT_OFF;
   DOUBLE   dLINE_SCALE;
   DOUBLE   dSAMP_SCALE;
   DOUBLE   dLAT_SCALE;
   DOUBLE   dLONG_SCALE;
   DOUBLE   dHEIGHT_SCALE;
   union
   {
      struct
      {
         DOUBLE   dLINE_NUM_COEFF[ TRE_RPC00B_N_LINE_NUM_COEFF ];
         DOUBLE   dLINE_DEN_COEFF[ TRE_RPC00B_N_LINE_DEN_COEFF ];
         DOUBLE   dSAMP_NUM_COEFF[ TRE_RPC00B_N_SAMP_NUM_COEFF ];
         DOUBLE   dSAMP_DEN_COEFF[ TRE_RPC00B_N_SAMP_DEN_COEFF ];
      };
      DOUBLE dRatPolyCoeffs[ 4 ][ TRE_RPC00B_N_POLYNOMIAL_COEFF ];
   };
   BOOL m_bHeightSensitive;
   
   CTREDataRPC00B() : iSUCCESS( 0 ) {} // Invalid to start with

}; // class CTREDataRPC00B


//
// STDIDC - See STDI-002 7.2
//
class CTREDataSTDIDC
{
public:
   BOOL        m_bValid;
   TREString   strACQUISITION_DATE;
   TREString   strMISSION;
   TREString   strPASS;
   TREString   strOP_NUM;
   TREString   strSTART_SEGMENT;
   TREString   strREPRO_NUM;
   TREString   strREPLAY_REGEN;
   INT         iSTART_COLUMN;
   INT         iSTART_ROW;
   TREString   strEND_SEGMENT;
   INT         iEND_COLUMN;
   INT         iEND_ROW;
   TREString   strCOUNTRY;
   INT         iWAC;
   TREString   strLOCATION;

   CTREDataSTDIDC()
   {
      m_bValid = FALSE;
   }
}; // class CTREDataSTDIDC


//
// USE00A - See STDI-002 7.3
//
class CTREDataUSE00A
{
public:
   BOOL        m_bValid;
   DOUBLE      dANGLE_TO_NORTH;
   DOUBLE      dMEAN_GSD;
   DOUBLE      dDYNAMIC_RANGE;
   DOUBLE      dOBL_ANG;
   DOUBLE      dROLL_ANG;
   INT         iN_REF;
   INT         iREV_NUM;
   INT         iN_SEG;
   INT         iMAX_LP_SEG;
   DOUBLE      dSUN_EL;
   DOUBLE      dSUN_AZ;

   CTREDataUSE00A()
   {
      m_bValid = FALSE;
   }
}; // class CTREDataUSE00A


class CTREDataSOURCBMagInfo
{
public:

	CString		m_strDATE;	// 8
	CString		m_strUM_RATE;	// 3
	double		m_dRATE;		// 8
	CString		m_strUM_GMA;	// 3
	double		m_dGMA;		// 8
	double		m_dGMA_LON;	// 15
	double		m_dGMA_LAT;	// 15
	CString		m_strUM_GCA;	// 3
	double		m_dGCA;		// 8

	CTREDataSOURCBMagInfo()
	{
		m_dRATE = 0.0;
		m_dGMA = 0.0;
		m_dGMA_LON = 0.0;
		m_dGMA_LAT = 0.0;
		m_dGCA = 0.0;
	}

}; // class CTREDataSOURCBMagInfo

class CTREDataACCPOBPosAcc
{
public:
	CString	      m_strUM_HORZ;		// 3 unit of measure
	double	      m_dABS_HORZ_ACC;	// 5
	CString	      m_strUM_VERT;		// 3
	double	      m_dABS_VERT_ACC;	// 5
	CString	      m_strUM_PTP_HORZ;	// 3
	double	      m_dPTP_HORZ_ACC;	// 5
	CString	      m_strUM_PTP_VERT;	// 3
	double	      m_dPTP_VERT_ACC;	// 5
	INT		      m_iNUM_POINTS;	// 3
	CTREDataLoc*   m_pLOC;

	CTREDataACCPOBPosAcc();
	~CTREDataACCPOBPosAcc();
   CTREDataACCPOBPosAcc &operator=( const CTREDataACCPOBPosAcc &posacc );

}; // class CTREDataACCPOBPosAcc


class CTREDataSOURCBSource
{
public:
	int		m_iNUM_POLY;		// 3
	CTREDataPoly	*m_pPOLY;
	CString	m_strSERIES;		// 10
	CString	m_strSOURCE_ID;		// 20
	CString	m_strEDITION;		// 7
	CString	m_strNAME;			// 20
	CString	m_strTYPE_DATE;		// 3
	CString	m_strSIG_DATE;		// 8
	CString	m_strPERISH_DATE;	// 8
	CString	m_strSRC_REF_NUM;	// 80
	double	m_dSCALE;			// 9
	CString	m_strUM_COV;		// 3
	CString	m_strCOV;			// 10
	CString	m_strUM_CON_INT;	// 3
	double	m_dCON_INT;			// 4
	INT	   m_iWATER_COV;		// 3
	INT	   m_iNAV_SYS_TYPE;	// 3
	CString	m_strUM_HKE;		// 3
	double	m_dHKE;				// 6
	double	m_dHKE_LON;			// 15
	double	m_dHKE_LAT;			// 15
	CString	m_strCLASS;			// 1
	CString	m_strDOWNGRADE;		// 1
	CString	m_strDG_DATE;		// 8
	CString	m_strQLE;			// 80
	CString	m_strCPY;			// 80
	int		m_iNUM_MAG;			// 2
	CTREDataSOURCBMagInfo	*m_pMAG_INFO;
	int		m_iNUM_LEG_IMG;		// 2
	CString	*m_pLEGEND_ID   ;	// 10 each
	CString	m_strGEO_DAT_NAME;	// 80
	CString	m_strGEO_DAT_CODE;	// 4
	CString	m_strELP_NAME;		// 80
	CString	m_strELP_CODE;		// 3
	CString	m_strVER_DAT_NAME;	// 80
	CString	m_strVER_DAT_CODE;	// 4
	CString	m_strSND_DAT_NAME;	// 80
	CString	m_strSND_DAT_CODE;	// 4
	CString	m_strPRJ_NAME;		// 80
	CString	m_strPRJ_CODE;		// 2
	int		m_iNUM_PRJ_PARAM;	// 1
	CString	*m_pPRJ_PARAM;		// 15 each
	double	m_dPRJ_FALSE_X;		// 15
	double	m_dPRJ_FALSE_Y;		// 15
	CString	m_strGRD_CODE;		// 3
	CString	m_strGRD_DESC;		// 80
	CString	m_strGRD_ZONE;		// 4
	int		m_iNUM_INSET;		// 2

	CTREDataSOURCBSource();
	~CTREDataSOURCBSource();
	CTREDataSOURCBSource &operator=( const CTREDataSOURCBSource &src );

}; // class CTREDataSOURCBSource

//
// ACCPOB - See STDI-002 7.3
//

class CTREDataACCPOB
{
public:
	BOOL				         m_bSUCCESS;
	INT				         m_iNUM_ACPO;
	CTREDataACCPOBPosAcc*   m_pACCPOS;

	CTREDataACCPOB();
	~CTREDataACCPOB();
	CTREDataACCPOB &operator=( const CTREDataACCPOB &acc );

}; // class CTREDataACCPOB

//
// SOURCB - See STDI-002 7.3
//
class CTREDataSOURCB
{
public:
	BOOL				      m_bSUCCESS;
	DOUBLE			      m_dSCALE;	// 9
	CString			      m_strCOLOR_PATCH_ID;	// 10
	int				      m_iNUM_SRC;
	CTREDataSOURCBSource *m_pSrc;

	CTREDataSOURCB();
	~CTREDataSOURCB();
	CTREDataSOURCB &operator=( const CTREDataSOURCB &src );

}; // class CTREDataSOURCB


//
// RPCHDR - See MIL-C-89038
//
class CTREDataRPFHDR
{
public:
   BOOL                 m_bValid;
   BYTE                 m_bLittleEndian;
   USHORT               m_cHeaderSectionLen;
	CHAR                 m_strFileName[ 12 + 1 ];
	BYTE                 m_uchNewReplacementUpdateIndicator;
   CHAR                 m_strGoverningSpecNumber[ 15 + 1 ];
	CHAR                 m_strGoverningSpecDate[ 8 + 1 ];
   CHAR                 m_strSecurityClassification[ 1 + 1 ];
   CHAR                 m_strSecurityCountry[ 2 + 1 ];
	CHAR                 m_strSecurityReleaseMarking[ 2 + 1 ];
   ULONG                m_iLocationSectionLocation;

	CTREDataRPFHDR();
	~CTREDataRPFHDR();
	CTREDataRPFHDR &operator=( const CTREDataRPFHDR &hdr );

}; // class CTREDataRPFHDR

//
// RPCIMG - See MIL-C-89038
//
class CTREDataRPFIMG
{
public:
   BOOL              m_bValid;

   // Location section
   USHORT            m_cLocationSectionLen;
   ULONG             m_iComponentLocationTableOffset;
   USHORT            m_nComponentLocationRecords;
   USHORT            m_cComponentLocationRecordLen;
   ULONG             m_cComponentAggregateLen;

   class CComponentLocation
   {
   public:
      USHORT            m_iComponentID;
      ULONG             m_cComponentLen;
      ULONG             m_iComponentLocation;

      CComponentLocation();

   }; // class CComponentLocation
   CComponentLocation*  m_pComponentLocations;

   // Coverage section
   DOUBLE               m_dULLat;
	DOUBLE               m_dULLon;
   DOUBLE               m_dLLLat;
	DOUBLE               m_dLLLon;
	DOUBLE               m_dURLat;
	DOUBLE               m_dURLon;
	DOUBLE               m_dLRLat;
	DOUBLE               m_dLRLon;
	DOUBLE               m_dVertResolution;
	DOUBLE               m_dHorzResolution;
	DOUBLE               m_dVertInterval;
	DOUBLE               m_dHorzInterval;

   // The compression section is implicit

   // Color / grayscale section
   // Color / grayscale subsection
   BYTE                 m_nColorGSOffsetRecords;
   BYTE                 m_nColorGSConverterOffsetRecords;
   CHAR                 m_strExtColorGSFileName[ 12 + 1 ];
   // ColorMap subsection
   ULONG                m_iColorMapOffsetTableOffset;
   USHORT               m_cColorGSOffsetRecordLen;
   
   class CColorGSOffset
   {
   public:
      USHORT            m_iColorGSTableID;
      ULONG             m_nColorGSRecords;
      BYTE              m_cColorGSElementLen;
      USHORT            m_cHistogramRecordLen;
      ULONG             m_iColorGSTableOffset;
      ULONG             m_iHistogramTableOffset;

      CColorGSOffset();
   }; // class CColorGSOffset
   CColorGSOffset*      m_pColorMapOffsets;



	CTREDataRPFIMG();
	~CTREDataRPFIMG();
	CTREDataRPFIMG &operator=( const CTREDataRPFIMG &img );

}; // class CTREDataRPFIMG

// End of MetadataStructs.h

