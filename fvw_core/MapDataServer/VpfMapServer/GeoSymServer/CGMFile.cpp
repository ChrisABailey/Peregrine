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

// CGMFile.cpp: implementation of the CCGMFile class.
//
//////////////////////////////////////////////////////////////////////



#include "stdafx.h"
#include <memory>
#include "CGMFile.h"
#include "math.h"

#ifdef _DEBUG
   #undef THIS_FILE
   static char THIS_FILE[]=__FILE__;
   #define new DEBUG_NEW
#endif

#ifdef PARSE_TRACE
   #define PARSETRACE0(a)                                TRACE(a)
   #define PARSETRACE1(a,b)                              TRACE(a,b)
   #define PARSETRACE2(a,b,c)                            TRACE(a,b,c)
   #define PARSETRACE4(a,b,c,d,e)                        TRACE(a,b,c,d,e)
   #define PARSETRACE6(a,b,c,d,e,f,g)                    TRACE(a,b,c,d,e,f,g)
   #define PARSETRACE9(a,b,c,d,e,f,g,h,i,j)              TRACE(a,b,c,d,e,f,g,h,i,j)
   #define PARSETRACE10(a,b,c,d,e,f,g,h,i,j,k)           TRACE(a,b,c,d,e,f,g,h,i,j,k)
   #define PARSETRACE15(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) TRACE(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p)
#else
   #define PARSETRACE0(a)
   #define PARSETRACE1(a,b)
   #define PARSETRACE2(a,b,c)
   #define PARSETRACE4(a,b,c,d,e)
   #define PARSETRACE6(a,b,c,d,e,f,g)
   #define PARSETRACE9(a,b,c,d,e,f,g,h,i,j)
   #define PARSETRACE10(a,b,c,d,e,f,g,h,i,j,k)
   #define PARSETRACE15(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p)
#endif

#ifdef DRAW_TRACE
   #define DRAWTRACE1(a,b)                                  TRACE(a,b)
   #define DRAWTRACE4(a,b,c,d,e)                            TRACE(a,b,c,d,e)
   #define DRAWTRACE9(a,b,c,d,e,f,g,h,i,j)                  TRACE(a,b,c,d,e,f,g,h,i)
   #define DRAWTRACE10(a,b,c,d,e,f,g,h,i,j,k)               TRACE(a,b,c,d,e,f,g,h,i,j)
   #define DRAWTRACE17(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r) TRACE(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r)
#else
   #define DRAWTRACE1(a,b)
   #define DRAWTRACE4(a,b,c,d,e)
   #define DRAWTRACE9(a,b,c,d,e,f,g,h,i,j)
   #define DRAWTRACE10(a,b,c,d,e,f,g,h,i,j,k)
   #define DRAWTRACE17(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r)
#endif

#ifdef APS_TRACE
   #define APSTRACE TRACE
#else
#ifdef _WIN32
   #define APSTRACE 1 ? (void)0 : ::AfxTrace
#else
   // No MFC AfxTrace off-Windows; swallow the varargs entirely.
   #define APSTRACE(...) ((void)0)
#endif
#endif

// GDI font mapping table (CGM font name -> Windows typeface). Only GetFont
// consumes it, and GetFont is Windows-only, so the whole table is severed
// on POSIX: the display list carries the CGM font INDEX and V5 resolves it
// against whatever text engine the canvas has.
#ifdef _WIN32
static const struct FontDesc afdFontDescList[] =
{
   FontDesc( _T("HERSHEY/CARTOGRAPHIC_ROMAN"), _T("ARIAL"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/CARTOGRAPHIC_GREEK"), _T("ARIAL"), FW_NORMAL, FALSE, GREEK_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/SIMPLEX_ROMAN"), _T("ARIAL"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/SIMPLEX_GREEK"), _T("ARIAL"), FW_NORMAL, FALSE, GREEK_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/SIMPLEX_SCRIPT"), _T("SCRIPT MT BOLD"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/COMPLEX_ROMAN"), _T("ARIAL"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/COMPLEX_GREEK"), _T("ARIAL"), FW_NORMAL, FALSE, GREEK_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/COMPLEX_SCRIPT"), _T("SCRIPT MT BOLD"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/COMPLEX_ITALIC"), _T("ARIAL"), FW_NORMAL, TRUE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/COMPLEX_CYRILLIC"), _T("ARIAL"), FW_NORMAL, FALSE, RUSSIAN_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/DUPLEX_ROMAN"), _T("ARIAL"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/TRIPLEX_ROMAN"), _T("ARIAL"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/TRIPLEX_ITALIC"), _T("ARIAL"), FW_NORMAL, TRUE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/GOTHIC_GERMAN"), _T("CENTURY GOTHIC"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/GOTHIC_ENGLISH"), _T("CENTURY GOTHIC"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HERSHEY/GOTHIC_ITALIAN"), _T("CENTURY GOTHIC"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("TIMES_ROMAN"), _T("TIMES NEW ROMAN"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("TIMES_ITALIC"), _T("TIMES NEW ROMAN"), FW_NORMAL, TRUE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("TIMES_BOLD"), _T("TIMES NEW ROMAN"), FW_BOLD, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("TIMES_BOLD_ITALIC"), _T("TIMES NEW ROMAN"), FW_BOLD, TRUE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HELVETICA"), _T("HELVETICA"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("HELVETICA_OBLIQUE"), _T("HELVETICA"), FW_NORMAL, TRUE, ANSI_CHARSET,  1.00 ),
   FontDesc( _T("HELVETICA_BOLD"), _T("HELVETICA"), FW_BOLD, FALSE, ANSI_CHARSET,  1.00 ),
   FontDesc( _T("HELVETICA_BOLD_OBLIQUE"), _T("HELVETICA"), FW_BOLD, TRUE, ANSI_CHARSET,  1.00 ),
   FontDesc( _T("COURIER"), _T("COURIER NEW"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00 ),
   FontDesc( _T("COURIER_ITALIC"), _T("COURIER NEW ITALIC"), FW_NORMAL, TRUE, ANSI_CHARSET,  1.00 ),
   FontDesc( _T("COURIER_BOLD"), _T("COURIER NEW BOLD"), FW_BOLD, FALSE, ANSI_CHARSET,  1.00 ),
   FontDesc( _T("COURIER_BOLD_ITALIC"), _T("COURIER NEW BOLD ITALIC"), FW_BOLD, TRUE, ANSI_CHARSET,  1.00 ),
   FontDesc( _T("DEFAULT"), _T("ARIAL"), FW_NORMAL, FALSE, ANSI_CHARSET, 1.00, TRUE ),
   FontDesc( _T("DEFAULT_OBLIQUE"), _T("ARIAL"), FW_NORMAL, TRUE, ANSI_CHARSET,  1.00, TRUE ),
   FontDesc( _T("DEFAULT_BOLD"), _T("ARIAL"), FW_BOLD, FALSE, ANSI_CHARSET,  1.00, TRUE ),
   FontDesc( _T("DEFAULT_BOLD_OBLIQUE"), _T("ARIAL"), FW_BOLD, TRUE, ANSI_CHARSET,  1.00, TRUE ),
   FontDesc()     // Stopper
};
#endif  // _WIN32 (afdFontDescList)


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CCGMFile::CCGMFile() : m_nRefCount(1)
{
	m_pelement_list = NULL;
	m_picture = NULL;
   InitCGM();
}

CCGMFile::~CCGMFile()
{
   InitCGM();           // Clear all accumulated pictures
}


VOID CCGMFile::InitCGM()
{
	m_version = 0;          // Not specified

	m_vdctype = CGM_VDCTYPE_INTEGER;

	m_integer_precision = 16;
	m_real_precision.type = CGM_REAL_FIXED_POINT;
	m_real_precision.int_precision = 16;
	m_real_precision.dec_precision = 16;
	m_index_precision = 16;
	m_color_precision = 8;
	m_color_index_precision = 8;
   m_eColorModel = CGM_COLOR_MODEL_RGB;

	m_min_color_extent = RGB(0,0,0);
	m_max_color_extent = RGB(255,255,255);
	m_max_color_index = 63;

   m_iTransparencyMode = CGM_TRANSPARENCY_ON;
	m_crBackgroundColor = RGB(0,0,0);
   m_crAuxiliaryColor = RGB(0,0,0);
   m_eHatchStyleIndex = CGM_HATCH_STYLE_HORZ;

   m_coCharOrientation.lUpX = 0;
   m_coCharOrientation.lUpY = +1;
   m_coCharOrientation.lBaseX = +1;
   m_coCharOrientation.lBaseY = 0;
   m_crTextColor = RGB( 255, 255, 255 );
   m_lFontIndex = 1;
   m_acsFontList.RemoveAll();

	m_current_rotation = -999; // unset flag

   if ( m_pelement_list != NULL )
	{
		delete [] m_pelement_list;
	   m_pelement_list = NULL;
	}

   for ( INT i = 0; i < m_apPictures.GetSize(); i++ )
   {
      CCGMPicture* pPic = m_apPictures[ i ];
      if ( pPic != m_picture )
         delete pPic;
   }
   m_apPictures.RemoveAll();

	if ( m_picture != NULL )
	{
		delete m_picture;
	   m_picture = NULL;
	}

   m_ptDrawingOffset.x = 0;
   m_ptDrawingOffset.y = 0;
   m_dDrawingScale = 1.0;

   SetRectEmpty( &m_rectBoundingRectangle );
}



////////////////////////////////////////////////////////////////////////////////
// CGM stream reading support
////////////////////////////////////////////////////////////////////////////////

DOUBLE CCGMFile::ReadRealDouble()
{
   return ReadDouble( m_real_precision );
}


DOUBLE CCGMFile::ReadDouble( const REAL_PRECISION& real_precision )
{
	DOUBLE dResult = 0.0;
		
	// Determine if we are in 32 bit or 64 bit.
	// Currently, we only support 32 bits.
	if (real_precision.dec_precision + real_precision.int_precision == 32)
	{
		// Read in the encoded information.  We only handle low-precision floating point.
		long lvalue = ReadInteger( real_precision.dec_precision + real_precision.int_precision );

		switch ( real_precision.type )
		{
		case CGM_REAL_FLOATING_POINT:
			{
				// We only support 23 bit mantissa's. 
				if (real_precision.dec_precision == 23)
				{
					dResult = (double) *( (float*) &lvalue);
				}
			}
			break;

		case CGM_REAL_FIXED_POINT:
			// TODO:  Not currently implemented.  All Geosym's use floating point.
			break;
		}
	}
	
	return dResult;
}


COLORREF CCGMFile::ReadColor()
{
   BYTE bColors[3];
   for ( INT i = 0; i < 3; i++ )
      bColors[ i ] = (BYTE) ReadInteger( m_color_precision );

   return RGB( bColors[0], bColors[1], bColors[2] );     // Pack into a COLORREF
}


LONG CCGMFile::ReadString( CString& csValue )
{
   BYTE bLength = (BYTE) ReadInteger( 8 );

   csValue = CString( (PCHAR) ( m_pbCGMData + m_lCGMDataIndex ), bLength );
   m_lCGMDataIndex += bLength;
   return bLength;
}


LONG CCGMFile::ReadScaledVDC()
{
   return ReadScaledInteger( m_picture->m_vdc_precision );
}


LONG CCGMFile::ReadVDCScaledX()
{
   return m_picture->m_iDirX * ReadScaledVDC();
}


LONG CCGMFile::ReadVDCScaledY()
{
   return m_picture->m_iDirY * ReadScaledVDC();
}


// NOTE: parameter is `long`, matching the declaration in CGMFile.h. On
// Windows LONG is a typedef for long so both spellings agreed; on LP64
// POSIX LONG is int32_t (= int) and the mismatch is a hard error.
LONG CCGMFile::ReadScaledInteger( long int_precision )
{
   return -1000000 + (INT) ( 1000000.5 + ( 100.0 * m_picture->m_scale * ReadInteger( int_precision ) ) );
}


LONG CCGMFile::ReadScaledIntInteger()
{
   return ReadScaledInteger( m_integer_precision );
}


LONG CCGMFile::ReadIntInteger()
{
   return ReadInteger( m_integer_precision );
}


LONG CCGMFile::ReadColorIndex()
{
   return ReadInteger( m_color_index_precision );
}


LONG CCGMFile::ReadIndex()
{
   return ReadInteger( m_index_precision );
}


////////////////////////////////////////////////////////////////////////////////
// Method: LoadCGM
////////////////////////////////////////////////////////////////////////////////
CCGMFile* CCGMFile::LoadCGM( LPCTSTR szFile, LPCTSTR szFileName )
{
  // Open the file first of all
  HANDLE  hFile = ::CreateFile( szFile, GENERIC_READ, 
                               FILE_SHARE_READ, 
                               NULL, OPEN_EXISTING,  
                               FILE_ATTRIBUTE_NORMAL, NULL );
    
  // Memory map in the file, We need the file size
  DWORD dwFileSize = ::GetFileSize(hFile, NULL);
    
  // If we had a problem
  if (INVALID_HANDLE_VALUE == hFile)
  {
    TRACE(_T("CCGMFile::LoadCGM:Error opening file %s\n"), szFile);
    return NULL;
  }
    
  // Now open the shared file
  HANDLE  hFileMapping = ::CreateFileMapping(hFile, NULL, 
                                             PAGE_READONLY, 
                                             0, 0, NULL);
    
  // Close the handle to the file since it is already referenced
  ::CloseHandle(hFile);
  hFile = INVALID_HANDLE_VALUE;
    
  // Make sure we we have a valid handle
  _ASSERTE(NULL != hFileMapping);
    
  // Now map to the file
  void *pCGMFileData = ::MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    
  // Close the mapping file handle after we have mapped the view
  ::CloseHandle(hFileMapping);
  hFileMapping = NULL;
    
  // Make sure we we have a valid memory pointer
  _ASSERTE(NULL != pCGMFileData);
    
  // Get the file record
  char* pCurrent   = static_cast<char*>(pCGMFileData);
    
  // Create a new instance
  CCGMFile* pCGMFile = new CCGMFile;
    
  // Attempt to load the file
  PARSETRACE1( _T("LoadCGM(\"%s\")\n"), szFile );
  
  CGM_ERROR cgmError = pCGMFile->LoadCGM(pCurrent, dwFileSize, FALSE, FALSE );

  // Now unmap the file (happens either way
  ::UnmapViewOfFile(pCGMFileData);

  // See how we did loading
  if (E_CGM_SUCCESS == cgmError)
  {
    // fill in the filename
    pCGMFile->m_strFileName = szFileName;
  }
  else
  {
    // Error parsing the file
    TRACE(_T("CCGMFile::LoadCGM:Error parsing file %s\n"), szFile);

    // get rid of what we created
    delete pCGMFile;
    pCGMFile = NULL;
  }

  // Now give it back
  return pCGMFile;

}

////////////////////////////////////////////////////////////////////////////////
// Method: AddRef
////////////////////////////////////////////////////////////////////////////////
long CCGMFile::AddRef() const
{
  return ::InterlockedIncrement(&m_nRefCount);
}

////////////////////////////////////////////////////////////////////////////////
// Method: Release
////////////////////////////////////////////////////////////////////////////////
long CCGMFile::Release() const
{
   // DVL :: 20050110 - removed illegal "delete this" call, caller now is
   // responsible for deleting its copy of this object
   return ::InterlockedDecrement(&m_nRefCount);




  // Bump the count down
  long nRefCount = ::InterlockedDecrement(&m_nRefCount);
  if (0 == nRefCount)
  {
    // everyone is done with this toss it
    delete this;
  }

  return nRefCount;
}





CGM_ERROR CCGMFile::LoadCGM( PCHAR pCGMData, UINT cgm_size, BOOL bVDCOrientationEnable, BOOL bNewDrawing )
{
	CGM_ERROR error = E_CGM_SUCCESS;

   m_bVDCOrientationEnable = bVDCOrientationEnable;   // If VDC orientation is to be used

	CGM_LOAD_STATE state = STATE_CGM_START;
   bool in_figure = false;
	CGM_OPCODE opcode;

   long next_command = 0;
   m_pbCGMData = (PBYTE) pCGMData;
   m_cCGMDataBytes = cgm_size;

   // Catch any decoding error
   try
   {

      CList<short, short&> aps_stack;
      
      do
      {
         if ( ( error = DecodeCommandHeader( next_command, opcode ) ) == E_CGM_SUCCESS )
         {
            // Decode the CGM command.
            switch ( opcode.cgm_class )
            {
            case CGM_CLASS_DELIMITER:
               switch (opcode.cgm_id_delimeter)
               {
               case CGM_ID_BEGMF:
                  if ( state != STATE_CGM_START && state != STATE_CGM_END )
                     goto BadState;
                     
                  InitCGM();        // Delete anything previously decoded
                     
                  // Save the metafile name
                  ReadString( m_meta_filename );

                  state = STATE_CGM_BEGMF;
                  break;
                  
               case CGM_ID_ENDMF:
                  if ( state != STATE_CGM_BEGMF && state != STATE_CGM_ENDPIC )
                     goto BadState;
                  
                  state = STATE_CGM_END;
                  break;
                  
               case CGM_ID_BEGPIC:	
                  if ( state != STATE_CGM_BEGMF && state != STATE_CGM_ENDPIC )
                     goto BadState;
                     
                  m_picture = (CCGMPicture*) new CCGMPicture;
                     
                  ReadString( m_picture->m_name );
                  m_picture->m_crBackgroundColor = m_crBackgroundColor;

                  state = STATE_CGM_BEGPIC;

                  break;
                  
               case CGM_ID_BEGPICBODY:  
                  state = STATE_CGM_BEGPICBODY;
                  break;
                  
               case CGM_ID_ENDPIC:
                  if ( state != STATE_CGM_BEGPIC && state != STATE_CGM_BEGPICBODY )
                     goto BadState;
                  
                  m_apPictures.Add( m_picture );   // Add current picture to picture list
                  state = STATE_CGM_ENDPIC;     // Allow a new picture
                  break;
                  
               case CGM_ID_BEGSEG:      
                  ASSERT(false);
                  break;
                  
               case CGM_ID_ENDSEG:      
                  ASSERT(false);
                  break;
                  
               case CGM_ID_BEGFIGURE:	
                  in_figure = true;
                  break;
                  
               case CGM_ID_ENDFIGURE:   
                  in_figure = false;
                  break;
                  
               case CGM_ID_BEGPROTREGION:  
                  ASSERT(false);
                  break;
                  
               case CGM_ID_ENDPROTREGION:  
                  ASSERT(false);
                  break;
                  
               case CGM_ID_BEGCOMPOLINE:   
                  ASSERT(false);
                  break;
                  
               case CGM_ID_ENDCOMPOLINE:   
                  ASSERT(false);
                  break;
                  
               case CGM_ID_BEGCOMPOTEXTPATH: 
                  ASSERT(false);
                  break;
                  
               case CGM_ID_ENDCOMPOTEXTPATH: 					
                  ASSERT(false);
                  break;
                  
               case CGM_ID_BEGTILEARRAY:     
                  ASSERT(false);
                  break;
                  
               case CGM_ID_ENDTILEARRAY:     
                  ASSERT(false);
                  break;
                  
               case CGM_ID_BEGAPS:		// Begin Application Structure
                  {
                     // A BEGAPS consists of 3 parts.  
                     //   1.  Unique name for the structure.  (clear text).
                     //   2.  The type of structure.  (clear text).
                     //   3.  The type of data in the structure.  (STLIST).
                     
                     // For now, we ignore the name of the struture and type of data
                     // in the structure.  So we are just getting out the type of structure
                     // from the data stream. 
                     
                     CString cstrTOS;

                     // Ignore the name of the structure.
                     ReadString( cstrTOS );
                     APSTRACE( _T("Begin APS: name = \"%s\""), (LPCTSTR) cstrTOS );
                     
                     // Read in the type of structure.
                     ReadString( cstrTOS );
                     APSTRACE( _T(", type = \"%s\"\n"), (LPCTSTR) cstrTOS );
                     
                     // Decode cstring and add type to stack.
                     short aps_type = APS_IGNORE;
                     if ( cstrTOS == "LineStyle" )
                     {
							   aps_type = APS_LINE_STYLE;

							   // Currently assumes that there is only 1 line style per cgm file.  So 
							   // always use m_sami_line in picture.
						   }
						   else if (cstrTOS == "LineStyleComponent")
						   {
							   aps_type = APS_LINE_COMPONENT;

							   // Create a new component.
							   CApsLineComponent* component = new CApsLineComponent;
								m_picture->m_sami_line.m_components.AddTail(component);
						   }
						   else if (cstrTOS == "LineComponentElement")
						   {
							   aps_type = APS_LINE_ELEMENT;

							   // Create a new element.
							   CApsLineElement* element = new CApsLineElement;

								CApsLineComponent* component = m_picture->m_sami_line.m_components.GetTail();
								if (component != NULL)
									component->m_elements.AddTail(element);
							}
						
						   // Add tree level to command stack.
						   aps_stack.AddTail(aps_type);

					   }  // CGM_ID_BEGAPS
					   break;

               case CGM_ID_BEGAPSBODY:
                  // Just jump over the aps for now.
                  break;
                  
               case CGM_ID_ENDAPS:
                  // Pop off command from aps stack.
                  aps_stack.RemoveTail();
                  break;

            }  // CGM_CLASS_DELIMITER
				break;

			case CGM_CLASS_METAFILE:
            if ( state != STATE_CGM_BEGMF )
               goto BadState;

				switch (opcode.cgm_id_metafile)
				{
				case CGM_ID_MFVERSION:
					m_version = ReadI16Integer();
					break;

				case CGM_ID_MFDESC:
					ReadString( m_meta_description );
					break;
				
				case CGM_ID_VDCTYPE:
					m_vdctype = ReadI16Integer();
					break;
				
				case CGM_ID_INTEGERPREC:
					m_integer_precision = ReadI16Integer();
					break;
				
				case CGM_ID_REALPREC:
					m_real_precision.type = ReadI16Integer();
					m_real_precision.int_precision = ReadI16Integer();
					m_real_precision.dec_precision = ReadI16Integer();
					break;
				
				case CGM_ID_INDEXPREC:
					m_index_precision = ReadI16Integer();
					break;
				
				case CGM_ID_COLRPREC:
					m_color_precision = ReadI16Integer();
					break;
				
				case CGM_ID_COLRINDEXPREC:
					m_color_index_precision = ReadI16Integer();
					break;
				
				case CGM_ID_MAXCOLRINDEX:
					m_max_color_index = ReadColorIndex();
					break;
				
				case CGM_ID_COLRVALUEEXT:
               m_min_color_extent = ReadColor();
               m_max_color_extent = ReadColor();
					break;
				
            case CGM_ID_MFELEMLIST:
               {
                  // Allocate memory for the element list.
                  m_pelement_list = (long*) new long[opcode.opcode_size];
                  
                  // Read in element list
                  for (short loop = 0; loop < opcode.opcode_size / 2; loop++)
                  {
                     m_pelement_list[loop] = ReadI16Integer();
                  }
               }
               break;
				
				case CGM_ID_BEGMFDEFAULTS:
               ASSERT(false);
					break;
				
				case CGM_ID_FONTLIST:
					{
                  m_acsFontList.RemoveAll();
                  while ( opcode.opcode_size > 1 )
                  {
                     CString cs;
                     opcode.opcode_size -= ReadString( cs );
						   m_acsFontList.Add( cs );
                  }
					}
					break;
				
				case CGM_ID_CHARSETLIST:
               // Character set lists are not supported.
					break;
				
				case CGM_ID_CHARCODING:
               ASSERT(false);
					break;
				
				case CGM_ID_NAMEPREC:
               ASSERT(false);
					break;
				
				case CGM_ID_MAXVDCEXT:
               ASSERT(false);
					break;
				
				case CGM_ID_SEGPRIEXT:
               ASSERT(false);
					break;
				
				case CGM_ID_COLRMODEL:
               {
                  eColorModel ColorModel = (eColorModel)ReadIntInteger();
                  
                  ASSERT(ColorModel == CGM_COLOR_MODEL_RGB );
                  break;
               }
				
				case CGM_ID_COLRCALIB:
               ASSERT(false);
					break;
				
				case CGM_ID_FONTPROP:
               ASSERT(false);
					break;
				
				case CGM_ID_GLYPHMAP:
               ASSERT(false);
					break;
				
				case CGM_ID_SYMBOLLIBLIST:
               ASSERT(false);
					break;
				}  // End of switch (opcode.cgm_id_metafile)
				break;

			case CGM_CLASS_PICTURE:
            if ( state != STATE_CGM_BEGPIC && state != STATE_CGM_BEGPICBODY )
               goto BadState;

            switch (opcode.cgm_id_picture)
				{
				case CGM_ID_SCALEMODE:
					// Read the units.
					m_picture->m_scale_mode = ReadIntInteger();
               if ( m_picture->m_scale_mode == CGM_SCALE_ABSTRACT )
                  m_picture->m_scale = 100.0;
               else if ( m_picture->m_scale_mode == CGM_SCALE_METRIC )
               {
					   m_picture->m_scale = ReadDouble( m_real_precision );
               }
               else
                  ASSERT(0);     // Invalid mode

               PARSETRACE2( _T("Scaling mode = %d, scaling = %.4e\n"), 
                  m_picture->m_scale_mode, m_picture->m_scale );
					break;

				case CGM_ID_COLRMODE:
					m_picture->m_eColorMode = static_cast< eColorMode >( ReadIntInteger() );
					break;

				case CGM_ID_LINEWIDTHMODE:
					m_picture->m_eLineWidthMode = static_cast< eSpecMode >( ReadIntInteger() );
               PARSETRACE1( _T("Line width mode = %d\n"), m_picture->m_eLineWidthMode );
					break;

				case CGM_ID_MARKERSIZEMODE:
					m_picture->m_eMarkerSizeMode = static_cast< eSpecMode >( ReadIntInteger() );
					break;

				case CGM_ID_EDGEWIDTHMODE:
					m_picture->m_eEdgeWidthMode = static_cast< eSpecMode >( ReadIntInteger() );
               PARSETRACE1( _T("Edge width mode = %d\n"), m_picture->m_eEdgeWidthMode );
					break;

				case CGM_ID_VDCEXT:
					if ( m_vdctype == CGM_VDCTYPE_INTEGER )
					{	
						m_picture->m_vdc_extent.int_extent.llx = ReadScaledIntInteger();
						m_picture->m_vdc_extent.int_extent.lly = ReadScaledIntInteger();
						m_picture->m_vdc_extent.int_extent.urx = ReadScaledIntInteger();
						m_picture->m_vdc_extent.int_extent.ury = ReadScaledIntInteger();

                  // Set position multipliers according to the VDC orientation
                  if ( m_bVDCOrientationEnable )
                  {
                     m_picture->m_iDirX =
                        ( m_picture->m_vdc_extent.int_extent.llx < m_picture->m_vdc_extent.int_extent.urx )
                        ? +1 : -1;
                     m_picture->m_iDirY =
                        ( m_picture->m_vdc_extent.int_extent.lly < m_picture->m_vdc_extent.int_extent.ury )
                        ? -1 : +1;
                  }
                  else
                  {
                     m_picture->m_iDirX = m_picture->m_iDirY = +1;       // No VDC-controlled reorientation
                  }
PARSETRACE6( _T("VDC extent: %d, %d, %d, %d, directions = %d, %d\n"),
   m_picture->m_vdc_extent.int_extent.llx, m_picture->m_vdc_extent.int_extent.lly,
   m_picture->m_vdc_extent.int_extent.urx, m_picture->m_vdc_extent.int_extent.ury,
   m_picture->m_iDirX, m_picture->m_iDirY );

                  // Accumulate bounding rectangle union
                  RECT rect;
                  rect.left = m_picture->m_iDirX * m_picture->m_vdc_extent.int_extent.llx;
                  rect.top = m_picture->m_iDirY * m_picture->m_vdc_extent.int_extent.ury;
                  rect.right = m_picture->m_iDirX * m_picture->m_vdc_extent.int_extent.urx;
                  rect.bottom = m_picture->m_iDirY * m_picture->m_vdc_extent.int_extent.lly;
                  UnionRect( &m_rectBoundingRectangle, &m_rectBoundingRectangle, &rect );
PARSETRACE4( _T("New bounding rect = %d, %d, %d, %d\n"), m_rectBoundingRectangle.left,
   m_rectBoundingRectangle.top, m_rectBoundingRectangle.right, m_rectBoundingRectangle.bottom );
					}	
					else
					{
                  m_picture->m_vdc_extent.real_extent.llx = ReadRealDouble();
                  m_picture->m_vdc_extent.real_extent.lly = ReadRealDouble();
                  m_picture->m_vdc_extent.real_extent.urx = ReadRealDouble();
                  m_picture->m_vdc_extent.real_extent.ury = ReadRealDouble();
					}
					break;

				case CGM_ID_BACKCOLR:
               m_crBackgroundColor = ReadColor();     // Always direct mode
               if ( m_picture->m_eColorMode == CGM_COLORMODE_INDEXED )
                  m_picture->m_color_table.AddByColor( 0, m_crBackgroundColor );

               break;

				case CGM_ID_DEVVP:
               ASSERT(false);
					break;

				case CGM_ID_DEVVPMODE:
               ASSERT(false);
					break;

				case CGM_ID_DEVVPMAP:
               ASSERT(false);
					break;

				case CGM_ID_LINEREP:
               ASSERT(false);
					break;

				case CGM_ID_MARKERREP:
               ASSERT(false);
					break;

				case CGM_ID_TEXTREP:
               ASSERT(false);
					break;

				case CGM_ID_FILLREP:
               ASSERT(false);
					break;

				case CGM_ID_EDGEREP:
               ASSERT(false);
					break;

				case CGM_ID_INTSTYLEMODE:
               ASSERT(false);
					break;

				case CGM_ID_LINEEDGETYPEDEF:
               ASSERT(false);
					break;

				case CGM_ID_HATCHSTYLEDEF:
               ASSERT(false);
					break;

				case CGM_ID_GEOPATDEF:
               ASSERT(false);
					break;
				}
				break;

			case CGM_CLASS_CONTROL:
				switch (opcode.cgm_id_control)
				{
				case CGM_ID_VDCINTEGERPREC:
					m_picture->m_vdc_precision = ReadIntInteger();
					break;

				case CGM_ID_VDCREALPREC:
               {
                  // Throwing away values for now              
	   				int type = ReadI16Integer();
		   			int int_precision = ReadI16Integer();
			   		int dec_precision = ReadI16Integer();
               }
					break;

				case CGM_ID_AUXCOLR:
               m_crAuxiliaryColor = ReadColor();
					break;

				case CGM_ID_TRANSPARENCY:
					if (ReadIntInteger() == CGM_TRANSPARENCY_OFF)
					{
						m_picture->m_use_transparency = false;
					}
					else
					{
						m_picture->m_use_transparency = true;
					}
					break;

				case CGM_ID_CLIPRECT:
               ASSERT(false);
					break;

				case CGM_ID_CLIP:
               ASSERT(false);
					break;

				case CGM_ID_LINECLIPMODE:
               ASSERT(false);
					break;

				case CGM_ID_MARKERCLIPMODE:
               ASSERT(false);
					break;

				case CGM_ID_EDGECLIPMODE:
               ASSERT(false);
					break;

				case CGM_ID_NEWREGION:
               ASSERT(false);
					break;

				case CGM_ID_SAVEPRIMCONT:
               ASSERT(false);
					break;

				case CGM_ID_RESPRIMCONT:
               ASSERT(false);
					break;

				case CGM_ID_PROTREGION:
               ASSERT(false);
					break;

				case CGM_ID_GENTEXTPATHMODE:
               ASSERT(false);
					break;

				case CGM_ID_MITRELIMIT:
               {
                  // Throw away for now.
                  double mitre_limit = ReadRealDouble();
               }
					break;

				case CGM_ID_TRANSPCELLCOLR:
               ASSERT(false);
					break;
				}
				break;

			case CGM_CLASS_GRAPHICAL:
				switch (opcode.cgm_id_graphical)
				{
				case CGM_ID_LINE:
					{
						// Determine the number of points.  Remember 2 pts / vertex.
						long num_vertices = opcode.opcode_size / ((m_picture->m_vdc_precision * 2) / 8);

						// Create a new graphical component.
                  // If inside a figure, a line is used to represent a filled-polygon.  There can be multiple lines to makeup the filled-polygon.
                  // Corel 10 converts polygons to filled line segments, so we support that functionality here.  We do not support full 
                  // figures though and do not handle multiple lines.
                  if (in_figure)
                  {
   						// Create a new graphical component.
	   					CCGMPolygon* polygon = new CCGMPolygon();
                     polygon->Initialize( this );
                     
                     // Set up the number of verticies
                     polygon->SetVertCount(num_vertices);
                     
                     // Read all the vertices in the polygon
                     POINT pt;
                     for (long loop = 0; loop < num_vertices; loop++)
                     {
                        pt.x = ReadVDCScaledX();
                        pt.y = ReadVDCScaledY();
                        // Remember, parameters are processed in reverse order.
                        polygon->AddVertex( pt );
                     }
                     
                     if (m_vdctype == CGM_VDCTYPE_INTEGER)
                     {
                        polygon->SetInteriorStyle(
                           CGM_INTSTYLE_SOLID, 
                           &m_picture->m_pattern[m_picture->m_pattern_index-1],
                           m_picture->m_vdc_extent.int_extent.urx - m_picture->m_vdc_extent.int_extent.llx,
                           m_picture->m_vdc_extent.int_extent.ury - m_picture->m_vdc_extent.int_extent.lly
                           );
                     }
                     else
                     {
                        polygon->SetInteriorStyle(
                           CGM_INTSTYLE_SOLID, 
                           &m_picture->m_pattern[m_picture->m_pattern_index-1],
                           static_cast<long>(m_picture->m_vdc_extent.real_extent.urx - m_picture->m_vdc_extent.real_extent.llx),
                           static_cast<long>(m_picture->m_vdc_extent.real_extent.ury - m_picture->m_vdc_extent.real_extent.lly)
                           );
                     }
                     
                     // Add polygon to list of drawing objects.
                     m_picture->m_drawing_objects.AddTail(polygon);

                  }  // In figure
                  else
                  {
						   CCGMPolyLine* line = new CCGMPolyLine();
                     line->Initialize( this );
                     
                     // Set up the number of vertices
                     line->SetVertCount(num_vertices);
                     
                     // Read all the vertices in the polyline.
                     POINT pt;
                     
                     for (long loop = 0; loop < num_vertices; loop++)
                     {
                        pt.x = ReadVDCScaledX();
                        pt.y = ReadVDCScaledY();
                        line->AddVertex( pt );
                     }
                     
                     // Add line to list of drawing objects.
                     m_picture->m_drawing_objects.AddTail(line);

                  }  // Not in figure
					}
					break;

				case CGM_ID_DISJTLINE:
               ASSERT(false);
					break;

				case CGM_ID_MARKER:						
               ASSERT(false);
					break;

				case CGM_ID_TEXT:
 					{
                  CPoint ptPosition;
						ptPosition.x = ReadVDCScaledX();
						ptPosition.y = ReadVDCScaledY();
             
                  // Discard non-final text
                  if ( 1 != ReadIntInteger() )
                     break;

                  CString csText;
                  ReadString( csText );       // Get the text
  
                  CCGMText* pCGMText = new CCGMText();
                  pCGMText->Initialize(
                     csText, ptPosition, m_lCharHeight,
                     m_coCharOrientation, m_crTextColor, m_lFontIndex, this );

                  // Add text to list of drawing objects.
                  m_picture->m_drawing_objects.AddTail( pCGMText );
					}
					break;


				case CGM_ID_RESTRTEXT:
               ASSERT(false);
					break;

				case CGM_ID_APNDTEXT:					
               ASSERT(false);
					break;

				case CGM_ID_POLYGON:						
					{
						// Determine the number of points.  Remember 2 pts / vertex.
						long num_vertices = opcode.opcode_size / ((m_picture->m_vdc_precision * 2) / 8);

						// Create a new graphical component.
						CCGMPolygon* polygon = new CCGMPolygon();
                  polygon->Initialize( this );
                  
                  // Set up the number of verticies
                  polygon->SetVertCount(num_vertices);
                  
                  // Read all the vertices in the polygon.
                  POINT pt;
                  
                  for (long loop = 0; loop < num_vertices; loop++)
                  {
                     pt.x = ReadVDCScaledX();
                     pt.y = ReadVDCScaledY();

                     // Remember, parameters are processed in reverse order.
                     polygon->AddVertex( pt );
                  }
                  
                  if (m_vdctype == CGM_VDCTYPE_INTEGER)
                  {
                     polygon->SetInteriorStyle(
                        m_picture->m_eFillStyle, 
                        &m_picture->m_pattern[m_picture->m_pattern_index-1],
                        m_picture->m_vdc_extent.int_extent.urx - m_picture->m_vdc_extent.int_extent.llx,
                        m_picture->m_vdc_extent.int_extent.ury - m_picture->m_vdc_extent.int_extent.lly
                        );
                  }
                  else
                  {
                     polygon->SetInteriorStyle(
                        m_picture->m_eFillStyle, 
                        &m_picture->m_pattern[m_picture->m_pattern_index-1],
                        static_cast<long>(m_picture->m_vdc_extent.real_extent.urx - m_picture->m_vdc_extent.real_extent.llx),
                        static_cast<long>(m_picture->m_vdc_extent.real_extent.ury - m_picture->m_vdc_extent.real_extent.lly)
                        );
                  }
                  
                  PARSETRACE0( _T("Polygon:\n") );

                  // Add polygon to list of drawing objects.
                  m_picture->m_drawing_objects.AddTail(polygon);
               }
					break;

				case CGM_ID_POLYGONSET:
					{
						// Determine the number of points.  Remember 2 pts / vertex + type.
						long num_vertices = opcode.opcode_size / (((m_picture->m_vdc_precision * 2) / 8) + (m_integer_precision / 8));

						// Create a new graphical component.
						CCGMPolygonSet* polyset = new CCGMPolygonSet();
                  polyset->Initialize( this );
                  
                  // Read all the vertices in the polygon.
                  long x;
                  long y;
                  long poly_type;
                  
                  // Set up the number of verticies
                  polyset->SetVertCount(num_vertices);
                  
                  for (long loop = 0; loop < num_vertices; loop++)
                  {
                     x = ReadVDCScaledX();
                     y = ReadVDCScaledY();
                     poly_type = ReadIntInteger();
                     
                     // Remember, parameters are processed in reverse order.
                     polyset->AddVertex(loop, x, y, poly_type);
                  }
                  
                  if (m_vdctype == CGM_VDCTYPE_INTEGER)
                  {
                     polyset->SetInteriorStyle(
                        m_picture->m_eFillStyle, 
                        &m_picture->m_pattern[m_picture->m_pattern_index-1],
                        m_picture->m_vdc_extent.int_extent.urx - m_picture->m_vdc_extent.int_extent.llx,
                        m_picture->m_vdc_extent.int_extent.ury - m_picture->m_vdc_extent.int_extent.lly
                        );
                  }
                  else
                  {
                     polyset->SetInteriorStyle(
                        m_picture->m_eFillStyle, 
                        &m_picture->m_pattern[m_picture->m_pattern_index-1],
                        static_cast<long>(m_picture->m_vdc_extent.real_extent.urx - m_picture->m_vdc_extent.real_extent.llx),
                        static_cast<long>(m_picture->m_vdc_extent.real_extent.ury - m_picture->m_vdc_extent.real_extent.lly)
                        );
                  }
                  
                  // Add polygon to list of drawing objects.
                  m_picture->m_drawing_objects.AddTail(polyset);
               }
					break;

				case CGM_ID_CELLARRAY:						
               ASSERT(false);
					break;

				case CGM_ID_GDP:						
               ASSERT(false);
					break;

				case CGM_ID_RECT:
               {
                  // A rectangle is just a particular polygon
                  
                  // Create a new graphical component.
                  CCGMPolygon* polygon = new CCGMPolygon();
                  polygon->Initialize( this );
                  
                  // Set up the number of verticies
                  polygon->SetVertCount( 4 );
                  
                  // Read a pair of diagonally opposite corners
                  POINT pt1, pt2;
                  pt1.x = ReadVDCScaledX();
                  pt1.y = ReadVDCScaledY();
                  pt2.x = ReadVDCScaledX();
                  pt2.y = ReadVDCScaledY();
                  polygon->AddVertex( pt1 );
                  polygon->AddVertex( 1, pt1.x, pt2.y );
                  polygon->AddVertex( pt2 );
                  polygon->AddVertex( 3, pt2.x, pt1.y );
                  
                  // Now copy over all necessary current attributes for polygon.
                  if ( m_vdctype == CGM_VDCTYPE_INTEGER )
                  {
                     polygon->SetInteriorStyle(
                        m_picture->m_eFillStyle, 
                        &m_picture->m_pattern[m_picture->m_pattern_index-1],
                        m_picture->m_vdc_extent.int_extent.urx - m_picture->m_vdc_extent.int_extent.llx,
                        m_picture->m_vdc_extent.int_extent.ury - m_picture->m_vdc_extent.int_extent.lly
                        );
                  }
                  else
                  {
                     polygon->SetInteriorStyle(
                        m_picture->m_eFillStyle, 
                        &m_picture->m_pattern[m_picture->m_pattern_index-1],
                        static_cast<long>(m_picture->m_vdc_extent.real_extent.urx - m_picture->m_vdc_extent.real_extent.llx),
                        static_cast<long>(m_picture->m_vdc_extent.real_extent.ury - m_picture->m_vdc_extent.real_extent.lly)
                        );
                  }
                  
                  PARSETRACE0( _T("Rectangle:\n") );

                  // Add polygon to list of drawing objects.
                  m_picture->m_drawing_objects.AddTail(polygon);
               }

               break;


				case CGM_ID_CIRCLE:
					{
                  CCGMEllipse* ellipse = new CCGMEllipse;
                  CPoint ptCen;

                  ptCen.x = ReadVDCScaledX();
                  ptCen.y = ReadVDCScaledY();
                  LONG lRad = ReadScaledVDC();

                  ellipse->Initialize( ptCen, CPoint( ptCen.x + lRad, ptCen.y ),
                     CPoint( ptCen.x, ptCen.y + lRad ), this );
                  
                  m_picture->m_drawing_objects.AddTail( ellipse );
					}
					break;

				case CGM_ID_ARC3PT:							
               ASSERT(false);
					break;

				case CGM_ID_ARC3PTCLOSE:
               ASSERT(false);
					break;

				case CGM_ID_ARCCTR:	
               {
				      // Create a new graphical component.
                  CCGMCircularArc* arc = new CCGMCircularArc;
                  arc->Initialize( this );
                  
                  PARSETRACE15( _T("Circular arc: center = %d, %d,"
                        " conjugate radii = %d, %d and %d, %d, CW draw = %d,\n"
                     "  major radius = %d, minor radius = %d, rotation = %.4f,\n"
                     "  rays = %d, %d and %d, %d, close = %d\n"),
                     arc->m_ptCenter.x, arc->m_ptCenter.y,
                     arc->m_ptCanonicalRadius1.x, arc->m_ptCanonicalRadius1.y, 
                     arc->m_ptCanonicalRadius2.x, arc->m_ptCanonicalRadius2.y, arc->m_bCWDrawing,
                     arc->m_lMajorRadius, arc->m_lMinorRadius, arc->m_dBoundingRotation,
                     arc->m_ptRays[0].x, arc->m_ptRays[0].y, arc->m_ptRays[1].x, arc->m_ptRays[1].y,
                     arc->m_eCloseType );

                  // Add circle to list of drawing objects.
                  m_picture->m_drawing_objects.AddTail( arc );
               }

               break;

				case CGM_ID_ARCCTRCLOSE:
               {
				      // Create a new graphical component.
                  CCGMCircularArcClose* arc = new CCGMCircularArcClose;
                  arc->Initialize( this );

                  // Add circle to list of drawing objects.
                  m_picture->m_drawing_objects.AddTail( arc );
               }

					break;

				case CGM_ID_ELLIPSE:	
					{
						// Create a new graphical component.
						CCGMEllipse* ellipse = new CCGMEllipse;
                  ellipse->Initialize( this );
                  
                  PARSETRACE10( _T("Ellipse: center = %d, %d,"
                        " conjugate radii = %d, %d and %d, %d, CW draw = %d,\n"
                     "  major radius = %d, minor radius = %d, rotation = %.4f,\n"),
                     ellipse->m_ptCenter.x, ellipse->m_ptCenter.y,
                     ellipse->m_ptCanonicalRadius1.x, ellipse->m_ptCanonicalRadius1.y, 
                     ellipse->m_ptCanonicalRadius2.x, ellipse->m_ptCanonicalRadius2.y, ellipse->m_bCWDrawing,
                     ellipse->m_lMajorRadius, ellipse->m_lMinorRadius, ellipse->m_dBoundingRotation );

					   // Add to list of drawing objects.
						m_picture->m_drawing_objects.AddTail( ellipse );
					}
					break;

				case CGM_ID_ELLIPARC:
					{
						// Create a new graphical component.
						CCGMEllipticalArc* arc = new CCGMEllipticalArc;
                  arc->Initialize( this );

                  PARSETRACE15( _T("Elliptical arc: center = %d, %d,"
                        " conjugate radii = %d, %d and %d, %d, CW draw = %d,\n"
                     "  major radius = %d, minor radius = %d, rotation = %.4f,\n"
                     "  rays = %d, %d and %d, %d, close = %d\n"),
                     arc->m_ptCenter.x, arc->m_ptCenter.y,
                     arc->m_ptCanonicalRadius1.x, arc->m_ptCanonicalRadius1.y, 
                     arc->m_ptCanonicalRadius2.x, arc->m_ptCanonicalRadius2.y, arc->m_bCWDrawing,
                     arc->m_lMajorRadius, arc->m_lMinorRadius, arc->m_dBoundingRotation,
                     arc->m_ptRays[0].x, arc->m_ptRays[0].y, arc->m_ptRays[1].x, arc->m_ptRays[1].y,
                     arc->m_eCloseType );

						// Add to list of drawing objects.
						m_picture->m_drawing_objects.AddTail( arc );
				
					}
					break;

				case CGM_ID_ELLIPARCCLOSE:
               {
						// Create a new graphical component.
						CCGMEllipticalArcClose* arc = new CCGMEllipticalArcClose;
                  arc->Initialize( this );
                  PARSETRACE15( _T("Elliptical arc close: center = %d, %d,"
                        " conjugate radii = %d, %d and %d, %d, CW draw = %d,\n"
                     "  major radius = %d, minor radius = %d, rotation = %.4f,\n"
                     "  rays = %d, %d and %d, %d, close = %d\n"),
                     arc->m_ptCenter.x, arc->m_ptCenter.y,
                     arc->m_ptCanonicalRadius1.x, arc->m_ptCanonicalRadius1.y, 
                     arc->m_ptCanonicalRadius2.x, arc->m_ptCanonicalRadius2.y, arc->m_bCWDrawing,
                     arc->m_lMajorRadius, arc->m_lMinorRadius, arc->m_dBoundingRotation,
                     arc->m_ptRays[0].x, arc->m_ptRays[0].y, arc->m_ptRays[1].x, arc->m_ptRays[1].y,
                     arc->m_eCloseType );
						
                  // Add to list of drawing objects.
						m_picture->m_drawing_objects.AddTail( arc );
				   }
					break;

				case CGM_ID_ARCCTRREV:					
               ASSERT(false);
					break;

				case CGM_ID_CONNEDGE:					
               ASSERT(false);
					break;

				case CGM_ID_HYPERBARC:						
               ASSERT(false);
					break;

				case CGM_ID_PARABARC:					
               ASSERT(false);
					break;

				case CGM_ID_NUB:						
               ASSERT(false);
					break;

				case CGM_ID_NURB:								
               ASSERT(false);
					break;

				case CGM_ID_POLYBEZIER:
               ASSERT(false);
					break;

				case CGM_ID_SYMBOL:						
               ASSERT(false);
					break;

				case CGM_ID_BITONALTILE:
               ASSERT(false);
					break;

				case CGM_ID_TILE:					
               ASSERT(false);
					break;
				}				
				break;

			case CGM_CLASS_GRAPHICAL2:
				switch (opcode.cgm_id_graphical2)
				{
				case CGM_ID_LINEINDEX:
               ASSERT(false);
					break;

				case CGM_ID_LINETYPE:
					m_picture->m_eLineType = static_cast<eLineType>(ReadIntInteger());
					break;

				case CGM_ID_LINEWIDTH:
					//m_picture->m_lLineWidth = (long)((READ_METRIC( m_integer_precision, opcode_index)/25.4)+.5);
					m_picture->m_line_width = ReadScaledIntInteger();
               PARSETRACE1( _T("Line width = %d\n"), m_picture->m_line_width );
					break;

				case CGM_ID_LINECOLR:
               switch( m_picture->m_eColorMode )
               {
                  case CGM_COLORMODE_INDEXED:
   					   m_picture->m_line_color = m_picture->m_color_table.FindColor( ReadColorIndex() );
                     break;

                  case CGM_COLORMODE_DIRECT:
                     m_picture->m_line_color = ReadColor();
                     break;
               }
					break;

				case CGM_ID_MARKERINDEX:
               ASSERT(false);
					break;

				case CGM_ID_MARKERTYPE:
               ASSERT(false);
					break;

				case CGM_ID_MARKERSIZE:
               ASSERT(false);
					break;

				case CGM_ID_MARKERCOLR:
               ASSERT(false);
					break;

				case CGM_ID_TEXTINDEX:
               ASSERT(false);
					break;

				case CGM_ID_TEXTFONTINDEX:
               m_lFontIndex = ReadIntInteger();
					break;

				case CGM_ID_TEXTPREC:
               ASSERT(false);
					break;

				case CGM_ID_CHAREXPAN:
               ASSERT(false);
					break;

				case CGM_ID_CHARSPACE:
               ASSERT(false);
					break;

				case CGM_ID_TEXTCOLR:
               m_crTextColor = ReadColor();
               PARSETRACE1( _T("Text color = %08x\n"), m_crTextColor );
					break;

				case CGM_ID_CHARHEIGHT:
               m_lCharHeight = ReadIntInteger();
					break;

				case CGM_ID_CHARORI:
               {
				      if (m_vdctype == CGM_VDCTYPE_INTEGER)
				      {	
      					m_coCharOrientation.lUpX = ReadScaledIntInteger();
      					m_coCharOrientation.lUpY = ReadScaledIntInteger();
      					m_coCharOrientation.lBaseX = ReadScaledIntInteger();
      					m_coCharOrientation.lBaseY = ReadScaledIntInteger();
				      }	
				      else
				      {
					      // Not implemented yet.
				      }
               }
					break;

				case CGM_ID_TEXTPATH:
               ASSERT(false);
					break;

				case CGM_ID_TEXTALIGN:
               ASSERT(false);
					break;

				case CGM_ID_CHARSETINDEX:
               ASSERT(false);
					break;

				case CGM_ID_ALTCHARSETINDEX:
               ASSERT(false);
					break;

				case CGM_ID_FILLINDEX:
               ASSERT(false);
					break;

				case CGM_ID_INTSTYLE:
					m_picture->m_eFillStyle = static_cast< eFillStyle >( ReadIntInteger() );
					break;

				case CGM_ID_FILLCOLR:
               switch( m_picture->m_eColorMode)
               {
               case CGM_COLORMODE_INDEXED:
   					m_picture->m_fill_color = m_picture->m_color_table.FindColor( ReadColorIndex() );
                  break;

               case CGM_COLORMODE_DIRECT:
                  m_picture->m_fill_color = ReadColor();
                  break;
               }
					break;

				case CGM_ID_HATCHINDEX:
               m_eHatchStyleIndex = static_cast< eHatchStyle >( ReadIndex() );
					break;

				case CGM_ID_PATINDEX:
					m_picture->m_pattern_index = ReadIntInteger();
					break;

				case CGM_ID_EDGEINDEX:
					break;

				case CGM_ID_EDGETYPE:
					m_picture->m_eEdgeType = static_cast< eLineType >( ReadIntInteger() );
					break;

				case CGM_ID_EDGEWIDTH:
					m_picture->m_lEdgeWidth = ReadScaledIntInteger();
               PARSETRACE1( _T("Edge width = %d\n"), m_picture->m_lEdgeWidth );
					break;

				case CGM_ID_EDGECOLR:
               switch( m_picture->m_eColorMode )
               {
               case CGM_COLORMODE_INDEXED:
   					m_picture->m_crEdgeColor = m_picture->m_color_table.FindColor( ReadColorIndex() );
                  break;

               case CGM_COLORMODE_DIRECT:
                  m_picture->m_crEdgeColor = ReadColor();
                  break;
               }
               break;

				case CGM_ID_EDGEVIS:
				   m_picture->m_bEdgeIsVisible = 
					   ReadIntInteger() == CGM_EDGE_VISIBILITY_ON;
					break;

				case CGM_ID_FILLREFPT:
					if (m_vdctype == CGM_VDCTYPE_INTEGER)
					{	
                  // Throw away for now.
						long x = ReadScaledIntInteger();
						long y = ReadScaledIntInteger();
					}	
					else
					{
						// Not implemented yet.
					}
					break;

				case CGM_ID_PATTABLE:
					{
						// Load pattern.
						long pattern_index = ReadIntInteger();
						long size_x = ReadIntInteger();
						long size_y = ReadIntInteger();
						long pattern_precision = ReadIntInteger();

						// Create memory
						if ((pattern_index > 64) || (pattern_index < 1))
						{
							// TODO:  Error out.
							ASSERT(false);
						}
						
						if (m_picture->m_pattern[pattern_index-1].Alloc(size_x, size_y))
						{
							// We only support Monochrome patterns.
							switch( m_picture->m_eColorMode )
							{
							case CGM_COLORMODE_INDEXED:
								{
									long temp_color_index;
									long num_bytes = size_x * size_y;

									for (long loop = 0; loop < num_bytes; loop++)
									{
										temp_color_index = ReadInteger( pattern_precision );
										m_picture->m_pattern[pattern_index-1].AddMonochromeBit(temp_color_index != 0);
									}
								}
								break;

							case CGM_COLORMODE_DIRECT:
								{
									// Not supported.
									ASSERT(false);
								}
								break;

							default:
								ASSERT(false);
								break;
							}
						}
						else
						{
							// TODO:  Error out.
							ASSERT(false);
						}						
					}
					break;

				case CGM_ID_PATSIZE:
					{
						// bottom = height x
						// right = height y
						// top = width x
						// left = width y
						m_picture->m_pattern_size.bottom = ReadIntInteger();
						m_picture->m_pattern_size.right = ReadIntInteger();
						m_picture->m_pattern_size.top = ReadIntInteger();
						m_picture->m_pattern_size.left = ReadIntInteger();
					}
					break;

				case CGM_ID_COLRTABLE:
					{
                  ASSERT( m_eColorModel == CGM_COLOR_MODEL_RGB );

						long color_index = ReadColorIndex();
                  opcode.opcode_size -= m_color_index_precision / 8; // Count the starting index
                  

						// Take the opcode size and remove the bytes for the index.  Divide by size of each color.  3 Colors RGB.
                  while ( opcode.opcode_size -= 3 * ( m_color_precision / 8 ), opcode.opcode_size >= 0 )
							m_picture->m_color_table.AddByColor( color_index++, ReadColor() );
					}
					break;

				case CGM_ID_ASF:
               ASSERT(false);
					break;

				case CGM_ID_PICKID:
               ASSERT(false);
					break;

				case CGM_ID_LINECAP:
					m_picture->m_eLineEndCap = static_cast< LineCapEnum >( ReadIntInteger() );
					m_picture->m_eLineDashCap = static_cast< LineCapEnum >( ReadIntInteger() );
               PARSETRACE1(_T("Line end cap = %d\n"), m_picture->m_eLineEndCap );
               PARSETRACE1(_T("Line dash cap = %d\n"), m_picture->m_eLineDashCap );
					break;

				case CGM_ID_LINEJOIN:
					m_picture->m_LineJoinEnum = static_cast< LineJoinEnum >( ReadIntInteger() );
               PARSETRACE1(_T("Line join = %d\n"), m_picture->m_LineJoinEnum );
					break;

				case CGM_ID_LINETYPECONT:
					m_picture->m_line_type_cont = ReadIntInteger();
					break;

				case CGM_ID_LINETYPEINITOFFSET:
               ASSERT(false);
					break;

				case CGM_ID_TEXTSCORETYPE:
               ASSERT(false);
					break;

				case CGM_ID_RESTRTEXTTYPE:
               ASSERT(false);
					break;

				case CGM_ID_INTERPINT:
               ASSERT(false);
					break;

				case CGM_ID_EDGECAP:
					m_picture->m_eEdgeCap = static_cast< LineCapEnum >( ReadIntInteger() );
					m_picture->m_eEdgeDashCap = static_cast< LineCapEnum >( ReadIntInteger() );
               PARSETRACE1(_T("Edge end cap = %d\n"), m_picture->m_eEdgeCap );
               PARSETRACE1(_T("Edge dash cap = %d\n"), m_picture->m_eEdgeDashCap );
					break;

				case CGM_ID_EDGEJOIN:
               m_picture->m_eEdgeJoin = static_cast< LineJoinEnum >( ReadIntInteger() );
					break;

				case CGM_ID_EDGETYPECONT:
               ASSERT(false);
					break;

				case CGM_ID_EDGETYPEINITOFFSET:
               ASSERT(false);
					break;

				case CGM_ID_SYMBOLLIBINDEX:
               ASSERT(false);
					break;

				case CGM_ID_SYMBOLCOLR:
               ASSERT(false);
					break;

				case CGM_ID_SYMBOLSIZE:
               ASSERT(false);
					break;

				case CGM_ID_SYMBOLORI:
               ASSERT(false);
					break;
				}
				break;

			case CGM_CLASS_ESCAPE:
            ASSERT(false);
				break;

			case CGM_CLASS_EXTERNAL:
            ASSERT(false);
				break;

			case CGM_CLASS_SEGMENT:
            ASSERT(false);
				break;

			case CGM_CLASS_APS:
            APSTRACE( _T("APS class: opcode = %d\n"), opcode.cgm_id_aps );
				switch (opcode.cgm_id_aps)
				{
				case CGM_ID_APSATTR:
					{
						// Determine if we should ignore the attribute.
						short aps_type = aps_stack.GetTail();

						CString cstrAttributeName;
                  ReadString( cstrAttributeName );

						short attributes_size = (SHORT) ReadInteger( 8 );
					
						// Check to see if attribute size exceeds 1 byte.
						if ( attributes_size == 0xFF )
							attributes_size = (SHORT) ReadI16Integer();

                  APSTRACE( _T(" Attribute = \"%s\", size = %d\n"), 
                     (LPCTSTR) cstrAttributeName, attributes_size );

						switch(aps_type)
						{
						default:				// flow through
						case APS_IGNORE:
						case APS_LINE_STYLE:
							break;

						case APS_LINE_COMPONENT:
							{
								// Get the last component for the sami line.
								CApsLineComponent* component = m_picture->m_sami_line.m_components.GetTail();

								if (cstrAttributeName == "LineWidth")
								{
									ReadAttribute( component->m_line_width );
                           //component->m_dLineWidth /= .254;
                           component->m_line_width *= 100; // to get 100ths of mm
								}
								else if (cstrAttributeName == "LineColor")
								{
									long color;
									ReadAttribute( color );
									component->m_line_color = m_picture->m_color_table.FindColor(color);
								}
								else if (cstrAttributeName == "StartAnchor")
								{
									ReadAttribute( component->m_start_anchor );
								}
								else if (cstrAttributeName == "IterationType")
								{
									ReadAttribute( component->m_iteration_type );
								}
								else if (cstrAttributeName == "StartPhase")
								{
									ReadAttribute( component->m_start_phase );
								}
								else
								{
									ASSERT(FALSE);
								}
							}
							break;

						case APS_LINE_ELEMENT:
								// Get the last component for the sami line.
								CApsLineComponent* component = m_picture->m_sami_line.m_components.GetTail();
								CApsLineElement* element = component->m_elements.GetTail();

								if (cstrAttributeName == "ElementType")
								{
									ReadAttribute( element->m_type );
								}
								else if (cstrAttributeName == "ElementLength")
								{
									ReadAttribute( element->m_length );
                           // Gaps are not showing up because the end cap of the line is going 
                           // past the end point of the line.
                           // Semi-kludge, encoding could be incorrect

                           // found a negative length in panama 3186.cgm
									element->m_length = fabs(element->m_length * 2);
								}
								else if (cstrAttributeName == "VerticalDisplacement")
								{
									ReadAttribute( element->m_vertical_displacement );
                           // This unit needs to be in 100ths of mm
                           element->m_vertical_displacement *= 100;
								}
								else if (cstrAttributeName == "SymbolDefinition")
								{
									ReadAttribute( element->m_symbol_definition );
								}
								else if (cstrAttributeName == "SymbolScale")
								{
									ReadAttribute( element->m_symbol_scale );
								}
								else if (cstrAttributeName == "SymbolOrientation")
								{
									ReadAttribute( element->m_symbol_orientation );
                           APSTRACE( _T("  Symbol orientation = %d, VDC dirs = %d, %d\n"),
                              element->m_symbol_orientation, m_picture->m_iDirX, m_picture->m_iDirY );
								}
								else
								{
									ASSERT(FALSE);
								}
							break;

						}  // switch( apstype )
					}  // case CGM_ID_APSATTR
					break;
				}  // case APS class
				break;

			default:
				break;
			}
		}
	   } while ((state != STATE_CGM_END) && (error == E_CGM_SUCCESS));
      return error;

BadState:
      return E_CGM_BAD_STATE;
   }
   catch ( CMemoryException* e )
   {
      error = E_CGM_NOMEMORY;
      e->Delete();
   }
   catch ( CException* e )
   {
      error = E_CGM_MISC_EXCEPTION;
      e->Delete();
   }

	return error;
}


CGM_ERROR CCGMFile::DecodeCommandHeader(
	long& next_command,
	CGM_OPCODE& opcode
	)
{
   m_lCGMDataIndex = next_command;

	if ( m_lCGMDataIndex + 2 > m_cCGMDataBytes )
		return E_CGM_UNEXPECTED_EOF;

	long command = ReadI16Integer();

   opcode.opcode_size = command & 0x0000001F;
	if ( opcode.opcode_size == 0x0000001F )
	{
		if ( m_lCGMDataIndex + 4 > m_cCGMDataBytes )
			return E_CGM_UNEXPECTED_EOF;
	
      opcode.opcode_size = ReadI16Integer();
	}

	opcode.cgm_class = static_cast<CGM_OPCODE_CLASS>( ( command & 0xF000 ) >> 12 );
	opcode.cgm_id = ( command & 0x0FE0 ) >> 5;

	// Check to make sure there are still enough bytes to decode operand.
	next_command = ( m_lCGMDataIndex + opcode.opcode_size + 1 ) & ~0x1;
	if ( next_command > m_cCGMDataBytes )
		return E_CGM_UNEXPECTED_EOF;

	return E_CGM_SUCCESS;
}

CCGMPicture::CCGMPicture()
{
	m_scale_mode = CGM_SCALE_ABSTRACT;
	m_scale = 0.01;
	m_eColorMode = CGM_COLORMODE_INDEXED;
//	m_back_color = RGB(0,0,0);
	m_eMarkerSizeMode = CGM_SPECMODE_ABSOLUTE;     

	m_line_type_cont = CGM_LINE_CONT_UNSPECIFIED;
	
   m_vdc_precision = 16;
   m_vdc_extent.int_extent.llx = 0;          // CGM standard defaults
   m_vdc_extent.int_extent.lly = 0;
   m_vdc_extent.int_extent.urx = 32767;
   m_vdc_extent.int_extent.ury = 32767;
   m_iDirX = +1; m_iDirY = -1;               // Corresponding direction multipliers

	m_use_transparency = true;
	m_eFillStyle = CGM_INTSTYLE_HOLLOW;
	m_fill_color = RGB( 0, 0, 0 );

	m_lEdgeWidth = 1;
	m_eEdgeWidthMode = CGM_SPECMODE_SCALED;
	m_eEdgeType = CGM_LINE_TYPE_SOLID;
	m_crEdgeColor = RGB( 0, 0, 0 );
	m_eEdgeCap = CGM_LINE_CAP_UNSPECIFIED;
	m_eEdgeDashCap = CGM_LINE_CAP_UNSPECIFIED;
	m_eEdgeJoin = CGM_LINE_JOIN_UNSPECIFIED;
	m_bEdgeIsVisible = FALSE;

	m_line_width = 1;
	m_eLineWidthMode = CGM_SPECMODE_SCALED;
	m_eLineType = CGM_LINE_TYPE_SOLID;
	m_line_color = RGB( 0, 0, 0 );
	m_eLineEndCap = CGM_LINE_CAP_UNSPECIFIED;
	m_eLineDashCap = CGM_LINE_CAP_UNSPECIFIED;
	m_LineJoinEnum = CGM_LINE_JOIN_UNSPECIFIED;

	// Patterns
	m_pattern_index = 1;
}

CCGMPicture::~CCGMPicture()
{
	// Free memory for all drawing objects.
	POSITION pos = m_drawing_objects.GetHeadPosition();

	while (pos != NULL)
	{
		CCGMDrawingObject* obj = (CCGMDrawingObject*) m_drawing_objects.GetNext(pos);
		delete obj;
	}
}

CCGMColorTable::CCGMColorTable()
{
}

CCGMColorTable::~CCGMColorTable()
{
}

void CCGMColorTable::AddByColor( ULONG index, COLORREF color )
{
   if ( m_colors.size() <= index )
      m_colors.resize( index + 1 );

   m_colors[ index ].m_index = index;
   m_colors[ index ].m_color = color;
}

COLORREF CCGMColorTable::FindColor( ULONG index )
{
   return ( index < m_colors.size() ) ? m_colors[ index ].m_color : RGB(0,0,0);
}

LONG CCGMColorTable::FindColorIndex(COLORREF color)
{
   ULONG index;
   for ( index = 0; index < m_colors.size(); index++ )
      if ( m_colors[ index ].m_color == color      // Color match
         && m_colors[ index ].m_index >= 0 )       // Initialized
         return index;

   return -1;
}


void CCGMDrawingObject::Initialize( CCGMFile* pParentCGMFile )
{
   m_pParentCGMFile = pParentCGMFile;

   m_eLineEndCap = pParentCGMFile->m_picture->m_eLineEndCap;
   m_eLineDashCap = pParentCGMFile->m_picture->m_eLineDashCap;
   m_LineJoinEnum = pParentCGMFile->m_picture->m_LineJoinEnum;
   m_crLineColor = pParentCGMFile->m_picture->m_line_color;
   m_eLineType = pParentCGMFile->m_picture->m_eLineType;
   m_lLineWidth = pParentCGMFile->m_picture->m_line_width;
   m_eLineWidthMode = pParentCGMFile->m_picture->m_eLineWidthMode;

   m_crEdgeColor = pParentCGMFile->m_picture->m_crEdgeColor;
   m_eEdgeType = pParentCGMFile->m_picture->m_eEdgeType;
   m_lEdgeWidth = pParentCGMFile->m_picture->m_lEdgeWidth;
   m_eEdgeWidthMode = pParentCGMFile->m_picture->m_eEdgeWidthMode;
   m_eEdgeCap = pParentCGMFile->m_picture->m_eEdgeCap;
   m_eEdgeDashCap = pParentCGMFile->m_picture->m_eEdgeDashCap;
   m_eEdgeJoin = pParentCGMFile->m_picture->m_eEdgeJoin;
   m_bEdgeIsVisible = pParentCGMFile->m_picture->m_bEdgeIsVisible;

   m_crFillColor = pParentCGMFile->m_picture->m_fill_color;

   m_eFillStyle = pParentCGMFile->m_picture->m_eFillStyle;
   m_eHatchStyleIndex = pParentCGMFile->m_eHatchStyleIndex;
 
   m_eMarkerSizeMode = pParentCGMFile->m_picture->m_eMarkerSizeMode;

   m_crAuxiliaryColor = RGB( 0, 0, 0 );
   m_iBackgroundMode = pParentCGMFile->m_iTransparencyMode == CGM_TRANSPARENCY_ON
      ? TRANSPARENT : OPAQUE;

   m_iDirX = pParentCGMFile->m_picture->m_iDirX;
   m_iDirY = pParentCGMFile->m_picture->m_iDirY;

   m_eElementType = ELEMTYPE_UNKNOWN;
}


#ifdef _WIN32
// Return:
// true - brush created.
// false - brush not created, or brush was hollow.
BOOL CCGMDrawingObject::CreateBrush(CBrush& brush, long interior_style, COLORREF color, CBitmap* bitmap)
{
	BOOL result = FALSE;
	switch (interior_style)
	{
   case CGM_INTSTYLE_EMPTY:
		result = brush.CreateStockObject(NULL_BRUSH);
		break;
	
	case CGM_INTSTYLE_HOLLOW:
		result = brush.CreateStockObject(HOLLOW_BRUSH);
		break;
	
	case CGM_INTSTYLE_SOLID:
		result = brush.CreateSolidBrush(color);
		break;
	
	case CGM_INTSTYLE_PATTERN:
		result = brush.CreatePatternBrush(bitmap);
		break;
	
	case CGM_INTSTYLE_HATCH:
      {
         INT iHatchStyle;
         switch ( m_eHatchStyleIndex )
         {
            default:
            case CGM_HATCH_STYLE_HORZ:
               iHatchStyle = HS_HORIZONTAL;
               break;

            case CGM_HATCH_STYLE_VERT:
               iHatchStyle = HS_VERTICAL;
               break;

            case CGM_HATCH_STYLE_P_SLOPE:
               iHatchStyle = HS_BDIAGONAL;
               break;

            case CGM_HATCH_STYLE_N_SLOPE:
               iHatchStyle = HS_FDIAGONAL;
               break;

            case CGM_HATCH_STYLE_HV_CROSS:
               iHatchStyle = HS_CROSS;
               break;

            case CGM_HATCH_STYLE_PN_CROSS:
               iHatchStyle = HS_DIAGCROSS;
               break;
         }
         result = brush.CreateHatchBrush( iHatchStyle, m_crFillColor );
      }
		break;
	
	case CGM_INTSTYLE_GEOMETRIC:
		ASSERT(false);
		break;
	
	case CGM_INTSTYLE_INTERPOLATED:	
		ASSERT(false);
		break;
		
	default:
		ASSERT(false);
		break;
	}

	return result;
}
#endif  // _WIN32 (CCGMDrawingObject::CreateBrush)

#ifdef _WIN32
// Return:
// true - pen created.
// false - pen not created.
BOOL CCGMDrawingObject::CreatePen( CPen& pen, BOOL bIsVisible, eLineType eLineType,
      LineCapEnum eLineEndCap, LineCapEnum eLineDashCap, LineJoinEnum LineJoinEnum, LONG lWidth, COLORREF color)
{
	// Create pen.
	long pen_type = PS_GEOMETRIC;
	if ( bIsVisible )
	{
		switch ( eLineType )
		{
		case CGM_LINE_TYPE_SOLID:
			pen_type |= PS_SOLID;
			break;

		case CGM_LINE_TYPE_DASH:
			pen_type |= PS_DASH;
			break;

		case CGM_LINE_TYPE_DOT:
			pen_type |= PS_DOT;
			break;

		case CGM_LINE_TYPE_DASHDOT:
			pen_type |= PS_DASHDOT;
			break;

		case CGM_LINE_TYPE_DASHDOTDOT:
			pen_type |= PS_DASHDOTDOT;
			break;

		default:
			ASSERT(false);
			break;
		}

		switch ( eLineEndCap )
		{
		case CGM_LINE_CAP_UNSPECIFIED:		// Flow through
		case CGM_LINE_CAP_BUTT:
			pen_type |= PS_ENDCAP_FLAT;
			break;

		case CGM_LINE_CAP_ROUND:
			pen_type |= PS_ENDCAP_ROUND;
			break;

		case CGM_LINE_CAP_PROJ_SQUARE:
			pen_type |= PS_ENDCAP_SQUARE;
			break;

		case CGM_LINE_CAP_TRIANGLE:
			ASSERT(false);
			break;
		}

		switch ( LineJoinEnum )
		{
		case CGM_LINE_JOIN_UNSPECIFIED:		// Flow through
		case CGM_LINE_JOIN_MITRE:
			pen_type |= PS_JOIN_MITER;
			break;

		case CGM_LINE_JOIN_ROUND:
			pen_type |= PS_JOIN_ROUND;
			break;

		case CGM_LINE_JOIN_BEVEL:
			pen_type |= PS_JOIN_BEVEL;
			break;
		}
	}
	else
	{
		pen_type = PS_NULL;
	}

	LOGBRUSH brush;
	brush.lbColor = color;
	brush.lbStyle = BS_SOLID;

	return pen.CreatePen(pen_type, lWidth, &brush);
}
#endif  // _WIN32 (CCGMDrawingObject::CreatePen)


VOID CCGMDrawingObject::Rotate( const DOUBLE dRotationAngle )
{
   if ( dRotationAngle != m_dCacheAngle )
   {
	   m_dCachedSine = sin( dRotationAngle );
	   m_dCachedCosine = cos( dRotationAngle );
      m_dCacheAngle = dRotationAngle;
   }
}


// The following routine arguments may be the same
VOID CCGMDrawingObject::Rotate( const CPoint& from, CPoint& to )
{
   LONG x = from.x;
	to.x = (LONG) ( ( m_dCachedCosine * (DOUBLE) x ) - ( m_dCachedSine * (DOUBLE) from.y ) );
	to.y = (LONG) ( ( m_dCachedSine * (DOUBLE) x ) + ( m_dCachedCosine * (DOUBLE) from.y ) );
}


VOID CCGMDrawingObject::RotateVDC( const CPoint& from, CPoint& to )
{
   Rotate( from, to );
	to.x *= m_iDirX;
	to.y *= m_iDirY;
}


VOID CCGMDrawingObject::RotateVDC( POINT_ARRAY& original_pts, POINT_ARRAY& dest_pts, DOUBLE dRotationAngle )
{
   if ( &original_pts != &dest_pts )
      dest_pts.RemoveAll();
   
   const int cPoints = original_pts.GetSize();
   
   // Need to do VDC adjustments for reflection even if zero rotation angle
   Rotate( dRotationAngle );
   for ( INT i = 0; i < cPoints; i++ )
   {
      CPoint ptNew;
      RotateVDC( original_pts.GetAt( i ), ptNew );
      dest_pts.SetAtGrow( i, ptNew );
   }
}


VOID CCGMDrawingObject::Rotate( POINT_ARRAY& original_pts, POINT_ARRAY& dest_pts, DOUBLE dRotationAngle )
{
   // This function suffers from rounding errors in Rotate() above, so the original points should
   // be saved aside and the dest points used for drawing, then kill them ASAP to save memory.
   if ( &original_pts == &dest_pts )
   {
      if ( dRotationAngle == 0.0 )
         return;
   }
   else
      dest_pts.RemoveAll();


   const INT cPoints = original_pts.GetSize();

   if ( dRotationAngle == 0.0 )
   {
      for ( INT i = 0; i < cPoints; i++ )
         dest_pts.SetAtGrow( i, original_pts.GetAt( i ) );
   }
   else  // Non-0 rotation
   {
      Rotate( dRotationAngle );
      for ( INT i = 0; i < cPoints; i++ )
      {
         CPoint ptNew;
         Rotate( original_pts.GetAt( i ), ptNew );
         dest_pts.SetAtGrow( i, ptNew );
      }
   }  // dRotationAngle != 0.0
}


bool CCGMDrawingObject::UsesLineOrFillColor( COLORREF cr, long mask )
{
   return ( ( m_crLineColor & mask ) == cr ) || ( ( m_crFillColor & mask ) == cr );
}



///////////////////////////////////////
//
// CCGMText
//
///////////////////////////////////////

void CCGMText::Initialize(
   CString csText, CPoint ptPosition, long lCharHeight,
   CHAR_ORIENTATION coCharOrientation, COLORREF crColor,
   long lFontIndex, CCGMFile* pParentCGMFile )
{
   CCGMDrawingObject::Initialize( pParentCGMFile );

   m_csText = csText;
   m_ptPosition = ptPosition;
   m_lCharHeight = lCharHeight;
   m_coCharOrientation = coCharOrientation;
   m_lFontIndex = lFontIndex;
   m_crTextColor = crColor;

   m_eElementType = ELEMTYPE_TEXT;
}

      
      
CCGMText::~CCGMText()
{
}


#ifdef _WIN32
VOID CCGMText::Draw( HDC hDC, const CSymColorAdjuster& sca )
{
   DRAWTRACE10( _T("CGMText::Draw() font = \"%s\", height = %d,"
         " orientation up = %d,%d, base = %d,%d, color = 0x%06x,\n"
      "  position = %d, %d, text = \"%s\"\n"),
      m_pParentCGMFile->m_acsFontList[ m_lFontIndex ], m_lCharHeight,
      m_coCharOrientation.lUpX, m_coCharOrientation.lUpY, m_coCharOrientation.lBaseX, m_coCharOrientation.lBaseY,
      m_crTextColor, m_ptPosition.x, m_ptPosition.y, m_csText );

   // Only if the font index is valid
   if ( m_lFontIndex < m_pParentCGMFile->m_acsFontList.GetSize() )
   {
      CString csFontName = m_pParentCGMFile->m_acsFontList[ m_lFontIndex ];
      BOOL bBold = csFontName.Find( _T("_BOLD") ) >= 0,
         bItalic = csFontName.Find( _T("_ITALIC") ) >= 0
            || csFontName.Find( _T("_OBLIQUE") ) >= 0;

      // Map CGM font name to typeface
      const FontDesc* pfd = afdFontDescList - 1;
      while ( (++pfd)->m_pszFontName != NULL )
      {
         // Match CGM font name
         if ( csFontName.CompareNoCase( pfd->m_pszFontName ) == 0 // Exact match
            || ( pfd->m_bDefault && bItalic == pfd->m_bItalic     // Default font, right italitization
               && ( pfd->m_iWeight == FW_BOLD ) == bBold ) )      // right boldness
         {
            DWORD dwCharSet = pfd->m_dwCharacterSet;     // Assume not default
            if ( pfd->m_bDefault )
            {
               dwCharSet = ANSI_CHARSET;           // Assume LATIN
               if ( csFontName.Find( _T("_CYRILLIC") ) >= 0 )
                  dwCharSet = RUSSIAN_CHARSET;
               if ( csFontName.Find( _T("_GREEK") ) >= 0 )
                  dwCharSet = GREEK_CHARSET;
            }

            DRAWTRACE4( _T("  Char set = %d, typeface = \"%s\", weight = %d, italic = %d\n"),
               dwCharSet, pfd->m_pszTypeFace, pfd->m_iWeight, pfd->m_bItalic );

            HFONT hFont = CreateFont(
               (INT) ( -0.5 -                // Total height of font
                  ( pfd->m_dHeightScaling * m_lCharHeight ) ),
               0,                            // Default width
               0,                            // Angle of escapement
               0,                            // Base-line orientation angle
               pfd->m_iWeight,               // Font weight
               (DWORD) pfd->m_bItalic,       // Italic attribute option
               0,                            // Underline attribute option
               0,                            // Strikeout attribute option
               dwCharSet,                    // Character set identifier
               OUT_DEFAULT_PRECIS,           // Output precision
               CLIP_DEFAULT_PRECIS,          // Clipping precision
               DEFAULT_QUALITY,              // Output quality
               DEFAULT_PITCH | FF_DONTCARE,  // Pitch and family
               pfd->m_pszTypeFace );         // Typeface name
         
            HFONT hOldFont = (HFONT) SelectObject( hDC, hFont );
            if ( hOldFont != NULL )
            {
               POINT ptStart;
                        
               // Final scale and offset
               ptStart.x = (LONG) ( ( m_ptPosition.x + m_pParentCGMFile->m_ptDrawingOffset.x )
                  * m_pParentCGMFile->m_dDrawingScale );
               ptStart.y = (LONG) ( ( m_ptPosition.y + m_pParentCGMFile->m_ptDrawingOffset.y )
                  * m_pParentCGMFile->m_dDrawingScale );

               INT iOldBkMode = SetBkMode( hDC, m_iBackgroundMode );   // OPAQUE or TRANSPARENT
               if ( iOldBkMode != 0 )
               {
                  COLORREF crOldBkColor = SetBkColor( hDC, m_crAuxiliaryColor );
                  if ( crOldBkColor != CLR_INVALID )
                  {
                     COLORREF crOldTextColor = SetTextColor( hDC, m_crTextColor );
                     if ( crOldTextColor != CLR_INVALID )
                     {
                        UINT uiOldTextAlign = SetTextAlign( hDC, TA_BASELINE );
                        if ( uiOldTextAlign != GDI_ERROR )
                        {
                           TextOut(
                              hDC,                       // Handle to DC
                              ptStart.x,                 // x-coordinate of starting position
                              ptStart.y,                 // y-coordinate of starting position
                              (LPCTSTR) m_csText,        // Text to draw
                              m_csText.GetLength() );    // Text length
                           
                           SetTextAlign( hDC, uiOldTextAlign );
                        }  // SetTextAlign succeeded
                     }  // SetTextColor succeeded
                  }  // SetBkColor succeeded
               }  // SetBkMode succeeded

               // Deselect font
               SelectObject( hDC, hOldFont );
            }
            DeleteObject( hFont );
            break;   // Done

         }  // Font name match
      }  // Font descriptor search
   }  // Font index valid
}
#endif  // _WIN32 (CCGMText::Draw)  // End of CGMText::Draw()


            
///////////////////////////////////////
//
// CCGMPolyLine
//
///////////////////////////////////////

#ifdef _WIN32
void CCGMPolyLine::Draw( HDC hDC, const CSymColorAdjuster& sca )
{
	CPen penLine;
	HPEN hPenOld;

	// If the line color is red (0xff), get the current fill color and use that to
	// create the pen that will be used to draw the polyline...
	COLORREF new_line_color = m_crLineColor;
	if (new_line_color == 0xff)
		new_line_color = sca.GetFillColor(new_line_color);

	// create the pen that will be used to draw the polyline...
	if ( ! CreatePen( penLine, true, m_eLineType, m_eLineEndCap, m_eLineDashCap,
      m_LineJoinEnum, m_lLineWidth, sca.Adjust(new_line_color) ) )
      return;

	hPenOld = (HPEN) SelectObject( hDC, penLine );

	if ( hPenOld )
   {
	   // Display the line.
      INT cPoints = m_disp_vertices.GetSize();
      if ( cPoints > 0 )
      {
         std::auto_ptr< POINT > apptPoints( new POINT[ cPoints ] );
         PPOINT pptPoints = apptPoints.get();
         
         for ( INT i = 0; i < cPoints; i++ )
         {
            pptPoints[ i ].x = (LONG) ( ( m_disp_vertices[ i ].x + m_pParentCGMFile->m_ptDrawingOffset.x )
               * m_pParentCGMFile->m_dDrawingScale );
            pptPoints[ i ].y = (LONG) ( ( m_disp_vertices[ i ].y + m_pParentCGMFile->m_ptDrawingOffset.y )
               * m_pParentCGMFile->m_dDrawingScale );
         }
         
         //dc.Polyline(m_vertices.GetData(), m_vertices.GetSize());
         Polyline( hDC, pptPoints, cPoints );
      }
		SelectObject( hDC, hPenOld );
   }
}
#endif  // _WIN32 (CCGMPolyLine::Draw)  // End of CCGMPolyLine::Draw()



VOID CCGMPolygon::Initialize( CCGMFile* pParentCGMFile )
{
   CCGMPolyLine::Initialize( pParentCGMFile );

   m_eElementType = ELEMTYPE_POLYGON;
}

CCGMPolygon::~CCGMPolygon()
{
}


#ifdef _WIN32
void CCGMPolygon::Draw( HDC hDC, const CSymColorAdjuster& sca)
{
   // Create brush.
	CBrush brushPolygon;
	if ( !CreateBrush( brushPolygon, m_eFillStyle,  sca.Adjust(sca.GetFillColor(m_crFillColor)), &m_bitmap_pattern ) )
   {
      ASSERT( FALSE );
      return;
   }

	CPen penEdge;
	if ( !CreatePen( penEdge, m_bEdgeIsVisible, m_eEdgeType, m_eEdgeCap, m_eEdgeDashCap,
      m_eEdgeJoin, m_lEdgeWidth, sca.Adjust(m_crEdgeColor) ) )
   {
      ASSERT( FALSE );
      return;
   }

   HPEN hOldPen = (HPEN) SelectObject( hDC, penEdge );
   if ( hOldPen != NULL )
   {
      do
      {
         INT cVertices = m_disp_vertices.GetSize();
         if ( cVertices <= 0 )
            break;

         DRAWTRACE1( _T("Polygon draw: %d points\n"), cVertices );

         std::auto_ptr< POINT > apptVertices( new POINT[ cVertices ] );
         PPOINT pptVertices = apptVertices.get();
         
         for ( INT i = 0; i < cVertices; i++ )
         {
            pptVertices[ i ].x = (LONG) ( ( m_disp_vertices[ i ].x + m_pParentCGMFile->m_ptDrawingOffset.x )
               * m_pParentCGMFile->m_dDrawingScale );
            pptVertices[ i ].y = (LONG) ( ( m_disp_vertices[ i ].y + m_pParentCGMFile->m_ptDrawingOffset.y )
               * m_pParentCGMFile->m_dDrawingScale );
         }
         
         HBRUSH hOldBrush = NULL;
         if ( m_eFillStyle != CGM_INTSTYLE_PATTERN )
         {
            hOldBrush = (HBRUSH) SelectObject( hDC, brushPolygon );
            if ( hOldBrush == NULL )
            {
               ASSERT( FALSE );
               break;
            }
         }
      
         // Display the polygon.
         Polygon( hDC, pptVertices, cVertices );

	      // Now draw the pattern if necessary.
	      if ( m_eFillStyle == CGM_INTSTYLE_PATTERN )
	      {
		      // Select the image bitmap into the dc.
		      SIZE extent;
            GetWindowExtEx( hDC, &extent );

		      hOldBrush = (HBRUSH) SelectObject( hDC, brushPolygon );
            if ( hOldBrush == NULL )
            {
               ASSERT( FALSE);
               break;
            }

            // Setup the region for the dc.
		      CArray< tagPOINT, tagPOINT& > device_polygon;
		      device_polygon.Append( m_disp_vertices );
		      LPtoDP( hDC, device_polygon.GetData(), device_polygon.GetSize() );

            HRGN hRgn;
		      VERIFY( ( hRgn = CreatePolygonRgn( device_polygon.GetData(), device_polygon.GetSize(), ALTERNATE ) ) != NULL );
		      SelectClipRgn( hDC, hRgn );
            DeleteObject(hRgn);

		      COLORREF clrOld = SetTextColor( hDC, sca.GetFillColor(m_crFillColor) );
		      PatBlt( hDC, (extent.cx / 2) * -1, (extent.cy / 2) * -1, extent.cx, extent.cy, PATCOPY );

            SelectClipRgn(hDC, 0);

		      SelectObject( hDC, hOldBrush );
            SetTextColor( hDC, clrOld );
         }  // fill_style == CGM_INTSTYLE_PATTER
      } while ( FALSE );

      SelectObject( hDC, hOldPen );
	}  // Pen selected

}
#endif  // _WIN32 (CCGMPolygon::Draw)  // End of CCGMPolygon::Draw()


void CCGMPolygon::SetInteriorStyle(eFillStyle style, CCGMPattern* pattern, long image_size_x, long image_size_y)
{
	m_eFillStyle = style;

	if (m_eFillStyle == CGM_INTSTYLE_PATTERN)
	{
#ifdef _WIN32
		// Create a bitmap for the patterm.
		m_bitmap_pattern.CreateBitmap(pattern->m_pattern_size.cx, pattern->m_pattern_size.cy, 1, 1, pattern->m_bits);
#else
		// Headless: the fill style above is the part the display list needs.
		// The monochrome bits stay in the CCGMPattern the caller owns; V5
		// realizes them against ICanvas instead of a GDI bitmap.
		(void) pattern;
#endif
	}
}


VOID CCGMPolygonSet::Initialize( CCGMFile* pParentCGMFile )
{
   CCGMPolygon::Initialize( pParentCGMFile );

   m_eElementType = ELEMTYPE_POLYGON_SET;
}

CCGMPolygonSet::~CCGMPolygonSet()
{
}

void CCGMPolygonSet::AddVertex(long index, long x, long y, long type)
{
	// We only support a polygonset with 1 polygon.  None of the Polygonsets currently in Geosym 4.0 have more than 1 polygon in 
	// a set. 
	CCGMPolygon::AddVertex(index, x, y);
	
	switch (type)
	{
	case CGM_POLYGONSET_LINE_INVISIBLE:
	case CGM_POLYGONSET_LINE_VISIBLE:
		m_vertex_types.Add(type);
		break;

	case CGM_POLYGONSET_LINE_CLOSE_INV:
		// In the future... may want to close current polygon here and prepare for new polygon.
		m_vertex_types.Add((LONG) CGM_POLYGONSET_LINE_INVISIBLE);
		break;

	case CGM_POLYGONSET_LINE_CLOSE_VIS:
		// In the future... may want to close current polygon here and prepare for new polygon.
		m_vertex_types.Add((LONG) CGM_POLYGONSET_LINE_VISIBLE);
		break;
	}
}


#ifdef _WIN32
void CCGMPolygonSet::Draw( HDC hDC, const CSymColorAdjuster& sca)
{
	// Draw the interior and hide the edge.
	SetEdgeVisible( false );
	CCGMPolygon::Draw( hDC, sca );
	
	// Now draw each edge.
	CPen penEdge;
	if ( !CreatePen( penEdge, true, m_eLineType, m_eLineEndCap, m_eLineDashCap, m_LineJoinEnum,
      m_lLineWidth, sca.Adjust(m_crLineColor) ) )
      return;

	HPEN hOldPen = (HPEN) SelectObject( hDC, penEdge );
   if ( hOldPen )
   {
	   BeginPath( hDC );

	   int size = __min( m_vertex_types.GetSize(), m_disp_vertices.GetSize() );

	   ASSERT ( size > 0 );
	   for ( int loop = 0; loop <= size; loop++ )
	   {
         POINT pt = m_disp_vertices.ElementAt( loop % size );

         if ( loop == 0 )
            MoveToEx( hDC, pt.x, pt.y, NULL );

         else
         {
		      switch( m_vertex_types.ElementAt( loop - 1 ) )
			   {
			      case CGM_POLYGONSET_LINE_INVISIBLE:
				      MoveToEx( hDC, pt.x, pt.y, NULL );
				      break;

			      case CGM_POLYGONSET_LINE_VISIBLE:
				      PolylineTo( hDC, &pt, 1 );
				      break;
			   }
		   }
	   }

	   EndPath( hDC );
	   StrokePath( hDC );

	   SelectObject( hDC, hOldPen );
   }

}
#endif  // _WIN32 (CCGMPolygonSet::Draw)  // End of CCGMPolygonSet::Draw()


void CCGMEllipticalObject::Initialize( CCGMFile* pParentCGMFile )
{
   CPoint ptCenter;
   CPoint ptCanonicalRadius1;
   CPoint ptCanonicalRadius2;
   
   // Read in the points.
   ptCenter.x = pParentCGMFile->ReadVDCScaledX();
   ptCenter.y = pParentCGMFile->ReadVDCScaledY();
   ptCanonicalRadius1.x = pParentCGMFile->ReadVDCScaledX();
   ptCanonicalRadius1.y = pParentCGMFile->ReadVDCScaledY();
   ptCanonicalRadius2.x = pParentCGMFile->ReadVDCScaledX();
   ptCanonicalRadius2.y = pParentCGMFile->ReadVDCScaledY();
   
   Initialize( ptCenter, ptCanonicalRadius1, ptCanonicalRadius2, pParentCGMFile );
}



void CCGMEllipticalObject::Initialize( const CPoint& ptCenter, const CPoint& ptCanonicalRadius1, 
                                 const CPoint& ptCanonicalRadius2, CCGMFile* pParentCGMFile )
{
   CCGMDrawingObject::Initialize( pParentCGMFile );

   // Step 1.  Save off center for later.
   m_ptCenter = ptCenter;

   // Step 2.  Find axis ends relative to the center.
	m_ptCanonicalRadius1 = ptCanonicalRadius1 - ptCenter;
	m_ptCanonicalRadius2 = ptCanonicalRadius2 - ptCenter;

   // Step 3.  Find min and max radii at 4 different canonical angles
   DOUBLE dRSqMax = 0.0, dRSqMin = 1e30, dPhiMax;
   INT i;
   for ( i = 0; i < 4; i++ )
   {
      DOUBLE
         dPhi = i * 3.14159265 / 4,
         dX = ( m_ptCanonicalRadius1.x * cos( dPhi ) ) + ( m_ptCanonicalRadius2.x * sin( dPhi ) ),
         dY = ( m_ptCanonicalRadius1.y * cos( dPhi ) ) + ( m_ptCanonicalRadius2.y * sin( dPhi ) ),
         dRSq = ( dX * dX ) + ( dY * dY );
      if ( dRSq > dRSqMax )
      {
         dRSqMax = dRSq;
         dPhiMax = dPhi;         // For search starting point
      }
      if ( dRSq < dRSqMin )
         dRSqMin = dRSq;
   }

   // Determine the drawing direction.  CW if axis 2 is CW from axis 1
   m_bCWDrawing = ( m_iDirX * m_iDirY *
      ( ( m_ptCanonicalRadius1.x * m_ptCanonicalRadius2.y )
         - ( m_ptCanonicalRadius2.x * m_ptCanonicalRadius1.y ) ) ) < 0;
      
   // Find the major axis as a canonical angle
   if ( dRSqMax - dRSqMin < 1.0 )   // If the ellipse is really a circle
   {
      m_lMajorRadius = m_lMinorRadius = (LONG) (  0.5 + sqrt( dRSqMax ) );
      m_dBoundingRotation = 0.0;
   }
   else
   {
      // Looking for min of dRSq/dPhi.  Need 1st and 2nd derivatives
      DOUBLE 
         dDCoefCos2 = 2.0 * ( ( m_ptCanonicalRadius1.x * m_ptCanonicalRadius2.x ) 
            + ( m_ptCanonicalRadius1.y * m_ptCanonicalRadius2.y ) ),
         dDCoefSin2 = ( m_ptCanonicalRadius2.x * m_ptCanonicalRadius2.x ) - ( m_ptCanonicalRadius1.x * m_ptCanonicalRadius1.x )
            + ( m_ptCanonicalRadius2.y * m_ptCanonicalRadius2.y ) - ( m_ptCanonicalRadius1.y * m_ptCanonicalRadius1.y ),
         dDDCoefCos2 = 2.0 * ( ( m_ptCanonicalRadius2.x * m_ptCanonicalRadius2.x ) - ( m_ptCanonicalRadius1.x * m_ptCanonicalRadius1.x )
            + ( m_ptCanonicalRadius2.y * m_ptCanonicalRadius2.y ) - ( m_ptCanonicalRadius1.y * m_ptCanonicalRadius1.y ) ),
         dDDCoefSin2 = -4.0 * ( ( m_ptCanonicalRadius1.x * m_ptCanonicalRadius2.x )
            + ( m_ptCanonicalRadius1.y * m_ptCanonicalRadius2.y ) );
      for ( i = 0; i < 4; i++ )     // Three iterations
      {
         DOUBLE dPhi2 = 2.0 * dPhiMax;
         dPhiMax = dPhiMax
            - ( ( dDCoefCos2 * cos( dPhi2 ) ) + ( dDCoefSin2 * sin( dPhi2 ) ) )
               / ( ( dDDCoefCos2 * cos( dPhi2 ) ) + ( dDDCoefSin2 * sin( dPhi2 ) ) );
      }
      
      // Convert major axis angle from canonical to normal
      m_dBoundingRotation = atan2(
         ( m_ptCanonicalRadius1.y * cos( dPhiMax ) ) + ( m_ptCanonicalRadius2.y * sin( dPhiMax ) ),
         ( m_ptCanonicalRadius1.x * cos( dPhiMax ) ) + ( m_ptCanonicalRadius2.x * sin( dPhiMax ) ) );

      // Find the length of the major and minor axes
      DOUBLE dX, dY;
      dX = ( m_ptCanonicalRadius1.x * cos( dPhiMax ) ) + ( m_ptCanonicalRadius2.x * sin( dPhiMax ) );
      dY = ( m_ptCanonicalRadius1.y * cos( dPhiMax ) ) + ( m_ptCanonicalRadius2.y * sin( dPhiMax ) );
      m_lMajorRadius = (LONG) ( 0.5 + sqrt( ( dX * dX ) + ( dY * dY ) ) );
      dPhiMax += 0.5 * 3.14159265;
      dX = ( m_ptCanonicalRadius1.x * cos( dPhiMax ) ) + ( m_ptCanonicalRadius2.x * sin( dPhiMax ) );
      dY = ( m_ptCanonicalRadius1.y * cos( dPhiMax ) ) + ( m_ptCanonicalRadius2.y * sin( dPhiMax ) );
      m_lMinorRadius = (LONG) ( 0.5 + sqrt( ( dX * dX ) + ( dY * dY ) ) );
   }

   m_eElementType = ELEMTYPE_ELLIPTICAL_OBJECT;
}


CCGMEllipticalObject::~CCGMEllipticalObject()
{
}

	
void CCGMEllipse::Initialize( const CPoint& ptCenter, const CPoint& ptCanonicalRadius1, 
                        const CPoint& ptCanonicalRadius2, CCGMFile* pParentCGMFile )
{
   CCGMEllipticalObject::Initialize( ptCenter, ptCanonicalRadius1, ptCanonicalRadius2, pParentCGMFile );
   Initialize();        // Common CCGMEllipse initialization
}

void CCGMEllipse::Initialize( CCGMFile* pParentCGMFile )
{
   CCGMEllipticalObject::Initialize( pParentCGMFile );
   Initialize();        // Common CCGMEllipse initialization
}

CCGMEllipse::~CCGMEllipse()
{
}



void CCGMEllipse::Initialize()
{
   m_eElementType = ELEMTYPE_ELLIPSE;
}


#ifdef _WIN32
void CCGMEllipse::Draw( HDC hDC, const CSymColorAdjuster& sca)
{
	CBrush brushEllipse;
	if ( !CreateBrush( brushEllipse, m_eFillStyle,  sca.Adjust(sca.GetFillColor(m_crFillColor)), NULL) )
      return;

	CPen penEdge;
	if ( !CreatePen( penEdge, m_bEdgeIsVisible, m_eEdgeType, m_eEdgeCap, m_eEdgeDashCap,
      m_eEdgeJoin, m_lEdgeWidth, sca.Adjust(m_crEdgeColor) ) )
      return;

	HBRUSH hOldBrush = (HBRUSH) SelectObject( hDC, brushEllipse );
   if ( hOldBrush != NULL )
   {
      HPEN hOldPen = (HPEN) SelectObject( hDC, penEdge );
      if ( hOldPen != NULL )
      {
         // This routine makes perfect elliptical arcs for Window's 2000 & Window's NT.  It works better than our routine, but is 
         // not supported under window's 95/98.
         BYTE types[ MAX_ELLIPTICAL_POINTS ];
         CPoint pts[ MAX_ELLIPTICAL_POINTS ];
         
         BeginPath( hDC );
         Ellipse( hDC, -m_lMajorRadius, -m_lMinorRadius, +m_lMajorRadius + 1, +m_lMinorRadius + 1 );
         EndPath( hDC );

         int count = GetPath( hDC, pts, types, MAX_ELLIPTICAL_POINTS );
         Rotate( m_dBoundingRotation );
         int i;
         for ( i = 0; i < count; i++ )
         {
            Rotate( pts[ i ], pts[ i ] );
            pts[ i ] += m_ptCenter;
         }

         // Rotate Points around.
         if ( m_dObjectRotation != 0.0 )
         {
            Rotate( m_dObjectRotation );
            for ( i = 0; i < count; i++ )
               RotateVDC( pts[ i ], pts[ i ] );
         }

         // Final scale and offset
         for ( i = 0; i < count; i++ )
         {
            pts[ i ].x = (LONG) ( ( pts[ i ].x + m_pParentCGMFile->m_ptDrawingOffset.x )
               * m_pParentCGMFile->m_dDrawingScale );
            pts[ i ].y = (LONG) ( ( pts[ i ].y + m_pParentCGMFile->m_ptDrawingOffset.y )
               * m_pParentCGMFile->m_dDrawingScale );
         }
         
         BeginPath( hDC );
         PolyDraw( hDC, pts, types, count );
         EndPath( hDC );
         
         if ( m_eFillStyle == CGM_INTSTYLE_SOLID )
            StrokeAndFillPath( hDC );
         else
            StrokePath( hDC );

         SelectObject( hDC, hOldPen );
      }  // Select pen succeeded

      SelectObject( hDC, hOldBrush );
   }  // Select brush succeeded

}
#endif  // _WIN32 (CCGMEllipse::Draw)  // End of CCGMEllipse::Draw()



void CCGMCircularArc::Initialize( CCGMFile* pParentCGMFile )
{
   CPoint ptCenter;
   CPoint ptRays[2];			   // Rays have start at center of circle
   LONG lRadius;
   
   // Read in the points.
   ptCenter.x = pParentCGMFile->ReadVDCScaledX();
   ptCenter.y = pParentCGMFile->ReadVDCScaledY();
   ptRays[0].x = pParentCGMFile->ReadVDCScaledX();
   ptRays[0].y = pParentCGMFile->ReadVDCScaledY();
   ptRays[1].x = pParentCGMFile->ReadVDCScaledX();
   ptRays[1].y = pParentCGMFile->ReadVDCScaledY();
   lRadius = pParentCGMFile->ReadScaledVDC();
   
   CCGMEllipticalArc::Initialize( ptCenter,
      CPoint( ptCenter.x + ( pParentCGMFile->m_picture->m_iDirX * lRadius ), ptCenter.y ),
      CPoint( ptCenter.x, ptCenter.y + ( pParentCGMFile->m_picture->m_iDirY * lRadius ) ),
      ptRays, pParentCGMFile );

   m_eElementType = ELEMTYPE_CIRCULAR_ARC;
}
                  


void CCGMCircularArcClose::Initialize( CCGMFile* pParentCGMFile )
{
   CCGMCircularArc::Initialize( pParentCGMFile );

	m_eCloseType = (eArcClose) pParentCGMFile->ReadIntInteger();
   m_eElementType = ELEMTYPE_CIRCULAR_ARC_CLOSE;
}


void CCGMEllipticalArc::Initialize( CCGMFile* pParentCGMFile )
{
   CCGMEllipticalObject::Initialize( pParentCGMFile );

   CPoint ptRays[2];
	ptRays[0].x = pParentCGMFile->ReadVDCScaledX();
	ptRays[0].y = pParentCGMFile->ReadVDCScaledY();
	ptRays[1].x = pParentCGMFile->ReadVDCScaledX();
	ptRays[1].y = pParentCGMFile->ReadVDCScaledY();

   Initialize( ptRays );

   m_eElementType = ELEMTYPE_ELLIPTICAL_ARC;
}

void CCGMEllipticalArc::Initialize( const CPoint& ptCenter, const CPoint& ptCanonicalRadius1, const CPoint& ptCanonicalRadius2,
                                     CPoint (&ptRays)[2], CCGMFile* pParentCGMFile )
{
   CCGMEllipticalObject::Initialize( ptCenter, ptCanonicalRadius1, ptCanonicalRadius2, pParentCGMFile );
   Initialize( ptRays );
}


void CCGMEllipticalArc::Initialize( CPoint (&ptRays)[2] )
{
   Rotate( -m_dBoundingRotation );
   Rotate( ptRays[0], m_ptRays[0] );
   Rotate( ptRays[1], m_ptRays[1] );

   m_eCloseType = CGM_ARCCLOSE_OPEN;        // Assume simple arc
}

CCGMEllipticalArc::~CCGMEllipticalArc()
{
}


#ifdef _WIN32
VOID CCGMEllipticalArc::Draw( HDC hDC, const CSymColorAdjuster& sca )
{
   DRAWTRACE17( _T("Elliptical arc draw: major/minor radii = %d, %d, center = %d, %d,"
      " rotation = %.3f\n"
      "  line color = %08x, type = %d, width = %d, width mode = %d, line cap = %d, line join = %d,\n"
      "  edge color = %08x, type = %d, width = %d, width mode = %d, edge cap = %d, edge join = %d\n"),
      m_lMajorRadius, m_lMinorRadius, m_ptCenter.x, m_ptCenter.y, m_dBoundingRotation,
      m_crLineColor, m_eLineType, m_lLineWidth, m_eLineWidthMode, m_eLineEndCap, m_LineJoinEnum,
      m_crEdgeColor, m_eEdgeType, m_lEdgeWidth, m_eEdgeWidthMode, m_eEdgeCap, m_eEdgeJoin );

	CBrush brushEllipse;
	if ( !CreateBrush( brushEllipse, m_eFillStyle,  sca.Adjust(sca.GetFillColor(m_crFillColor)), NULL) )
      return;

	CPen penEdge;
   if ( m_eCloseType == CGM_ARCCLOSE_OPEN )
   {
	   if ( !CreatePen( penEdge, m_bEdgeIsVisible, m_eLineType, m_eLineEndCap, m_eLineDashCap,
         m_LineJoinEnum, m_lLineWidth, sca.Adjust( m_crLineColor ) ) )
         return;
   }
   else
   {
	   if ( !CreatePen( penEdge, m_bEdgeIsVisible, m_eEdgeType, m_eEdgeCap, m_eEdgeDashCap,
         m_eEdgeJoin, m_lEdgeWidth, sca.Adjust( m_crEdgeColor ) ) )
         return;

   }

	HBRUSH hOldBrush = (HBRUSH) SelectObject( hDC, brushEllipse );
   if ( hOldBrush != NULL )
   {
      HPEN hOldPen = (HPEN) SelectObject( hDC, penEdge );
      if ( hOldPen != NULL )
      {
         // This routine makes perfect elliptical arcs for Window's 2000 & Window's NT.  It works better than our routine, but is 
         // not supported under window's 95/98.
         BYTE types[ MAX_ELLIPTICAL_POINTS + 1 ];
         CPoint pts[ MAX_ELLIPTICAL_POINTS + 1 ];
         BeginPath( hDC );
         
         SetArcDirection( hDC, m_bCWDrawing ? AD_CLOCKWISE : AD_COUNTERCLOCKWISE );
         Arc( hDC, -m_lMajorRadius, -m_lMinorRadius, +m_lMajorRadius, +m_lMinorRadius,
            m_ptRays[0].x, m_ptRays[0].y, m_ptRays[1].x, m_ptRays[1].y );

         EndPath( hDC );

         int count = GetPath( hDC, pts, types, MAX_ELLIPTICAL_POINTS );

         // Close the arc if so specified
         switch ( m_eCloseType )
         {
            case CGM_ARCCLOSE_PIE:
               pts[ count ] = CPoint( 0, 0 );
               types[ count ] = PT_LINETO | PT_CLOSEFIGURE;
               count++;
               break;

            case CGM_ARCCLOSE_CHORD:
               types[ count - 1 ] |= PT_CLOSEFIGURE;
               break;
         }

         int i;
         for ( i = 0; i < count; i++ )
         {
            Rotate( m_dBoundingRotation );
            // Translate and rotate pts.
            Rotate( pts[ i ], pts[ i ] );
            pts[ i ] += m_ptCenter;
         }
         
         // Rotate Points around.
         if ( m_dObjectRotation != 0.0 )
         {
            Rotate( m_dObjectRotation );
            for ( i = 0; i < count; i++ )
               RotateVDC( pts[ i ], pts[ i ] );
         }

         // Final scale and offset
         for ( i = 0; i < count; i++ )
         {
            pts[ i ].x = (LONG) ( ( pts[ i ].x + m_pParentCGMFile->m_ptDrawingOffset.x )
               * m_pParentCGMFile->m_dDrawingScale );
            pts[ i ].y = (LONG) ( ( pts[ i ].y + m_pParentCGMFile->m_ptDrawingOffset.y )
               * m_pParentCGMFile->m_dDrawingScale );
         }
         
         BeginPath( hDC );
         PolyDraw( hDC, pts, types, count );
         EndPath( hDC );
         
         if ( m_eCloseType != CGM_ARCCLOSE_OPEN )
            StrokeAndFillPath( hDC );
         else
            StrokePath( hDC );

         SelectObject( hDC, hOldPen );
      }  // Select pen succeeded

      SelectObject( hDC, hOldBrush );
   }  // Select brush succeeded

}
#endif  // _WIN32 (CCGMEllipticalArc::Draw)



void CCGMEllipticalArcClose::Initialize( CCGMFile* pParentCGMFile )
{
   CCGMEllipticalArc::Initialize( pParentCGMFile );
	
   m_eCloseType = (eArcClose) pParentCGMFile->ReadIntInteger();

   m_eElementType = ELEMTYPE_ELLIPTICAL_ARC_CLOSE;
}


void CCGMEllipticalArcClose::Initialize( const CPoint& ptCenter,
      const CPoint& ptCanonicalRadius1, const CPoint& ptCanonicalRadius2, 
      CPoint (&ptRays)[2], LONG lCloseType, CCGMFile* pParentCGMFile )
{
   CCGMEllipticalArc::Initialize( ptCenter,
      ptCanonicalRadius1, ptCanonicalRadius2, ptRays, pParentCGMFile );
	m_eCloseType = (eArcClose) pParentCGMFile->ReadIntInteger();
}


CCGMEllipticalArcClose::~CCGMEllipticalArcClose()
{
}


CCGMPattern::CCGMPattern()
{
	m_index = 0;
	m_bits = NULL;
	m_pattern_size = CSize(0, 0);
	m_num_bytes = 0;
}

CCGMPattern::~CCGMPattern()
{
	if (m_bits != NULL)
	{
		delete [] m_bits;
	}
}

// return: 
//  zero - error.
//  nonzero - success.
bool CCGMPattern::Alloc(long pixel_x, long pixel_y)
{
	if (m_bits != NULL)
	{
		delete [] m_bits;
		m_index = 0;
		m_num_bytes = 0;
	}

	m_pattern_size = CSize(pixel_x, pixel_y);
	m_num_bytes = (pixel_x * pixel_y) / 8;
	m_bits = (char*) new char[m_num_bytes];

	if (m_bits == NULL)
	{
		m_pattern_size = CSize(0, 0);
		m_num_bytes = 0;
		return false;
	}
	else
	{
		// Set all off.
		memset(m_bits, 0, m_num_bytes);
	}

	return true;
}
	
// Assumes that patterns are either 16x16 or 32x32 and thus are on even byte boundaries
void CCGMPattern::AddMonochromeBit(bool is_on)
{
	// Find current index;
	long current_byte = m_index / 8;
	
	if (current_byte < m_num_bytes)
	{
		// shift bits 1 to the left.
		m_bits[current_byte] = m_bits[current_byte] << 1;

		// add new bit.  Invert pattern.
		if (!is_on)
		{
			m_bits[current_byte] |= 1;
		}
	}

	m_index++;
}

bool CCGMPattern::IsSolid()
{
	for (int nLoop = 0; nLoop < m_num_bytes; nLoop++)
	{
		if (m_bits[nLoop] != 0xFF)
			return false;
	}

	return true;
}


CApsLineStyle::CApsLineStyle()
{
}

CApsLineStyle::~CApsLineStyle()
{
	// Free memory for all drawing objects.
	POSITION pos = m_components.GetHeadPosition();

	while (pos != NULL)
	{
		CApsLineComponent* component = (CApsLineComponent*) m_components.GetNext(pos);
		delete component;
	}
}

CApsLineComponent::CApsLineComponent()
{
	m_line_width = 0.0;
	m_line_color = RGB(0,0,0);
	m_start_anchor = SAMI_ANCHOR_BEGINNING;
	m_iteration_type = SAMI_ITERATION_CONTINUOUS;
	m_start_phase = 0;
}

CApsLineComponent::~CApsLineComponent()
{
	// Free memory for all drawing objects.
	POSITION pos = m_elements.GetHeadPosition();

	while (pos != NULL)
	{
		CApsLineElement* element = (CApsLineElement*) m_elements.GetNext(pos);
		delete element;
	}
}

CApsLineElement::CApsLineElement()
{
	m_type = SAMI_ELEMENT_TYPE_GAP;
	m_length = 0;
	m_vertical_displacement = 0.0;
	m_symbol_scale = 1;
	m_symbol_orientation = SAMI_SYMBOL_ORIENTATION_TANGENTIAL;
}

CApsLineElement::~CApsLineElement()
{
}
