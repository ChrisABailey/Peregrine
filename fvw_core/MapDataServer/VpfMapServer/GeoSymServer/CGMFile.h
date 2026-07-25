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

// CGMFile.h: interface for the CCGMFile class.
//
//////////////////////////////////////////////////////////////////////



#pragma once

#ifdef _WIN32
#include <afxtempl.h>
#include <atlbase.h>
#endif
#include <vector>
#include "CGMDefines.h"
#include "SymColors.h"


enum eColorModel
{
   CGM_COLOR_MODEL_RGB = 1,
   CGM_COLOR_MODEL_CIELAB = 2,
   CGM_COLOR_MODEL_CIELUV = 3,
   CGM_COLOR_MODEL_CMYK = 4,
   CGM_COLOR_MODEL_RGB_RELATED = 5
};

enum eColorMode
{
   CGM_COLORMODE_INDEXED      = CGM_COLOR_MODE_INDEXED,
   CGM_COLORMODE_DIRECT       = CGM_COLOR_MODE_DIRECT
};

enum eLineType
{
	CGM_LINE_TYPE_SOLID			= 1,
	CGM_LINE_TYPE_DASH			= 2,
	CGM_LINE_TYPE_DOT			   = 3,
	CGM_LINE_TYPE_DASHDOT		= 4,
	CGM_LINE_TYPE_DASHDOTDOT   = 5
};

// CGM Interior / Fill Styles.
enum eFillStyle
{
	CGM_INTSTYLE_HOLLOW			= 0,
	CGM_INTSTYLE_SOLID			= 1,
	CGM_INTSTYLE_PATTERN		   = 2,
	CGM_INTSTYLE_HATCH			= 3,
	CGM_INTSTYLE_EMPTY			= 4,
	CGM_INTSTYLE_GEOMETRIC		= 5,
	CGM_INTSTYLE_INTERPOLATED	= 6
};

// Line, edge, marker width specification mode
enum eSpecMode
{
   CGM_SPECMODE_ABSOLUTE      = 0,
   CGM_SPECMODE_SCALED        = 1,
   CGM_SPECMODE_FRACTIONAL    = 2,
   CGM_SPECMODE_MM            = 3
};


// Hatch styles
enum eHatchStyle
{
   CGM_HATCH_STYLE_HORZ       = 1,
   CGM_HATCH_STYLE_VERT       = 2,
   CGM_HATCH_STYLE_P_SLOPE    = 3,
   CGM_HATCH_STYLE_N_SLOPE    = 4,
   CGM_HATCH_STYLE_HV_CROSS   = 5,
   CGM_HATCH_STYLE_PN_CROSS   = 6
};


// Anchor Types.
enum eAnchor
{
	ANCHOR_NONE					  = 0,
	ANCHOR_UPPER_RIGHT		= 1,
	ANCHOR_LOWER_RIGHT		= 2,
	ANCHOR_UPPER_LEFT			= 3,
	ANCHOR_LOWER_LEFT			= 4,
	ANCHOR_CENTER_CENTER	= 5,
	ANCHOR_UPPER_CENTER		= 6,
	ANCHOR_LOWER_CENTER		= 7,
	ANCHOR_CENTER_RIGHT		= 8,
	ANCHOR_CENTER_LEFT		= 9	
};



// Circular and elliptical arc close types
enum eArcClose
{
   CGM_ARCCLOSE_OPEN = -1,         // Not closed
   CGM_ARCCLOSE_PIE = 0,           // Pie
   CGM_ARCCLOSE_CHORD = 1          // Chord
};

// Mask for overrides.
enum OverrideMask
{
	OVERRIDE_LINE_COLOR		= 1,
	OVERRIDE_FILL_COLOR		= 2,
	OVERRIDE_LINE_TYPE		= 4,
	OVERRIDE_FILL_STYLE		= 8
};


// For creating fonts
#ifdef _WIN32   // GDI font creation — headless builds never realize a font
struct FontDesc
{
   LPCTSTR  m_pszFontName;       // CGM name
   LPCTSTR  m_pszTypeFace;       // Font family
   INT      m_iWeight;           // See CreateFont
   DOUBLE   m_dHeightScaling;    // Expected ( ascent + decent ) / ascent
   BOOL     m_bItalic;
   DWORD    m_dwCharacterSet;    // ANSI_CHARSET, etc
   BOOL     m_bDefault;          // Default font
   FontDesc( LPCTSTR pszFontName, LPCTSTR pszTypeFace, INT iWeight, 
      BOOL bItalic = FALSE, DWORD dwCharacterSet = ANSI_CHARSET, 
      DOUBLE dHeightScaling = 1.25, BOOL bDefault = FALSE ) :
      m_pszFontName( pszFontName ), m_pszTypeFace( pszTypeFace ), 
      m_iWeight( iWeight ), m_bItalic( bItalic ),
      m_dwCharacterSet( dwCharacterSet), m_dHeightScaling( dHeightScaling ), m_bDefault( bDefault )
   {}
   FontDesc() : m_pszFontName( NULL ) {}  // To make a stopper
};
#endif  // _WIN32 (FontDesc)



#define MAX_PATTERNS 64


// APS
#define APS_IGNORE			0
#define APS_LINE_STYLE		1
#define APS_LINE_COMPONENT	2
#define APS_LINE_ELEMENT	3

// Attribute data types.
#define ATTRIBUTE_TYPE_CODEDLIST	5
#define ATTRIBUTE_TYPE_INDEX		11
#define ATTRIBUTE_TYPE_REAL		12
#define ATTRIBUTE_TYPE_STRING		14

class CApsLineElement : public CObject
{
public:
	CApsLineElement();
	~CApsLineElement();

	long m_type;
	double m_length;
	double m_vertical_displacement;
	CString m_symbol_definition;
	double m_symbol_scale;
	long m_symbol_orientation;
};

class CApsLineComponent : public CObject
{
public:
	CApsLineComponent();
	~CApsLineComponent();
	CTypedPtrList<CPtrList, CApsLineElement*> m_elements;

   DOUBLE   m_line_width;
	COLORREF	m_line_color;
	
   long		m_start_anchor;
	long		m_iteration_type;
	double	m_start_phase;
};

class CApsLineStyle : public CObject
{
public:
	CApsLineStyle();
	~CApsLineStyle();
	CTypedPtrList<CPtrList, CApsLineComponent*> m_components;
};


enum CGM_LOAD_STATE
{
	STATE_CGM_START,
	STATE_CGM_BEGMF,
	STATE_CGM_BEGPIC,
	STATE_CGM_BEGPICBODY,
	STATE_CGM_BEGAPS,
	STATE_CGM_BEGAPSBODY,
   STATE_CGM_ENDPIC,
	STATE_CGM_END
};


enum CGM_ERROR
{
	E_CGM_SUCCESS = 0,
	E_CGM_UNEXPECTED_EOF = 1,
	E_CGM_NOMEMORY = 2,
   E_CGM_BAD_STATE = 3,
   E_CGM_MISC_EXCEPTION = 4
};

typedef struct _CHAR_ORIENTATION
{
   long  lUpX;
   long  lUpY;
   long  lBaseX;
   long  lBaseY;
} CHAR_ORIENTATION;

typedef struct _REAL_PRECISION
{
	long type;									
	long int_precision;						// Number of integer digits.
	long dec_precision;						// Number of decimal digits.
} REAL_PRECISION;

typedef struct _VDC_EXTENT_REAL
{
	double llx;			// lower left.
	double lly;
	double urx;			// Upper right.
	double ury;				
} VDC_EXTENT_REAL;

typedef struct _VDC_EXTENT_INT
{
	long llx;
	long lly;
	long urx;
	long ury;
} VDC_EXTENT_INT;

typedef struct _VDC_EXTENT
{
	union
	{
		VDC_EXTENT_REAL real_extent;
		VDC_EXTENT_INT	 int_extent;
	};
} VDC_EXTENT;

// We only support monichrome patterns.  All colored patterns are converted to monochrome.
class CCGMPattern
{
public:
	CCGMPattern();
	~CCGMPattern();

	bool Alloc(long pixel_x, long pixel_y);
	void AddMonochromeBit(bool is_on);
	bool IsSolid();

public:
	CSize m_pattern_size;

   char* m_bits;
	long m_index;
	long m_num_bytes;
};

class CCGMColorEntry
{
public:
   CCGMColorEntry() : m_color( RGB( 0, 0, 0 ) ), m_index( -1 ) {}
	COLORREF m_color;
	CString m_name;
	long m_index;
};

class CCGMColorTable
{
public: 
	CCGMColorTable();
	~CCGMColorTable();
	
	void AddByColor( ULONG index, COLORREF color );
	COLORREF FindColor( ULONG index );
	LONG FindColorIndex(COLORREF color);

private:
   std::vector< CCGMColorEntry > m_colors;
};

// ARG_TYPE is const& (was tagPOINT&): CArray::Add must accept the temporary
// CPoint that AddVertex forwards. MFC's CArray is equally happy with a
// const ARG_TYPE, so the Windows build is unaffected.
typedef CArray<tagPOINT, const tagPOINT&> POINT_ARRAY;

class CCGMFile;         // Forward reference

class CCGMDrawingObject : public CObject
{
public:
   enum eElementType
   {
      ELEMTYPE_UNKNOWN,
      ELEMTYPE_TEXT,
      ELEMTYPE_POLYLINE,
      ELEMTYPE_POLYGON,
      ELEMTYPE_POLYGON_SET,
      ELEMTYPE_CIRCLE,
      ELEMTYPE_ELLIPTICAL_OBJECT,
      ELEMTYPE_ELLIPSE,
      ELEMTYPE_ELLIPTICAL_ARC,
      ELEMTYPE_ELLIPTICAL_ARC_CLOSE,
      ELEMTYPE_CIRCULAR_ARC,
      ELEMTYPE_CIRCULAR_ARC_CLOSE
   };

public:
	virtual ~CCGMDrawingObject() {};
   eElementType GetElementType() { return m_eElementType; }
#ifdef _WIN32
   // GDI rendering half — severed on POSIX (phase V4). The parsed geometry
   // below IS the display list; fv::CgmSymbol walks it and V5 draws it
   // through ICanvas instead of an HDC.
	virtual void Draw( HDC hDC, const CSymColorAdjuster& sca) = 0;
   VOID Draw( CDC& cDC, const CSymColorAdjuster& sca ) { Draw( cDC.m_hDC, sca ); }
#endif
   virtual VOID RotateObject( DOUBLE dObjectRotation ) { m_dObjectRotation = dObjectRotation; };

   inline void SetLineCap( LONG lLineEndCap ) 
      { m_eLineEndCap = static_cast< LineCapEnum >( lLineEndCap ); }
	inline void SetLineJoin( LONG lLineJoin )
      { m_LineJoinEnum = static_cast< LineJoinEnum >( lLineJoin ); }
   virtual bool UsesColor( COLORREF cr, long mask ) { return UsesLineColor( cr, mask ); }

   inline COLORREF GetLineColor() { return m_crLineColor; }
   inline COLORREF GetFillColor() { return m_crFillColor; }
   inline eLineType GetLineType() { return m_eLineType; }
   inline eFillStyle GetFillStyle() { return m_eFillStyle; }
   inline long GetLineWidth() { return m_lLineWidth; }

   inline void SetLineColor( COLORREF crLineColor ) { m_crLineColor = crLineColor; }
   inline void SetFillColor( COLORREF crFillColor ) { m_crFillColor = crFillColor; }
   inline void SetLineType( eLineType eLineType ) { m_eLineType = eLineType; }
   inline void SetFillStyle( eFillStyle eFillStyle ) { m_eFillStyle = eFillStyle; }
   inline void SetLineWidth( LONG lLineWidth ) { m_lLineWidth = lLineWidth; }
	inline void SetEdgeVisible( BOOL bIsVisible ) { m_bEdgeIsVisible = bIsVisible; }
   inline BOOL GetEdgeVisible() const { return m_bEdgeIsVisible; }
   inline COLORREF GetEdgeColor() const { return m_crEdgeColor; }
   inline LONG GetEdgeWidth() const { return m_lEdgeWidth; }

protected:
   CCGMDrawingObject()  // Called only from derived classes
      { m_dCacheAngle = m_dCachedSine = 0.0; m_dCachedCosine = 1.0; }
   void Initialize( CCGMFile* pParentCGMFile );

#ifdef _WIN32   // GDI object factories — see the Draw note above
	BOOL CreateBrush( CBrush& brush, long interior_style, COLORREF color = RGB(0,0,0), CBitmap* bitmap = NULL);
	BOOL CreatePen( CPen& pen, BOOL bIsVisible, eLineType eLineType, LineCapEnum eLineEndCap,
      LineCapEnum eLineDashCap, LineJoinEnum LineJoinEnum, LONG lWidth, COLORREF color );
#endif

   bool UsesLineColor( COLORREF cr, long mask ) { return ( m_crLineColor & mask ) == cr; }
   bool UsesLineOrFillColor( COLORREF cr, long mask );

   VOID Rotate( const DOUBLE dRotationAngle );
	VOID Rotate( const CPoint& from, CPoint& to );
	VOID Rotate( const CPoint& from, const DOUBLE dRotationAngle, CPoint& to );
   void Rotate( POINT_ARRAY& original_pts, POINT_ARRAY& dest_pts, DOUBLE dRotationAngle );
	VOID RotateVDC( const CPoint& from, CPoint& to );
   void RotateVDC( POINT_ARRAY& original_pts, POINT_ARRAY& dest_pts, DOUBLE dRotationAngle );

protected:
   COLORREF             m_crLineColor;
   eLineType            m_eLineType;
   LONG                 m_lLineWidth;
	eSpecMode            m_eLineWidthMode;
	LineCapEnum             m_eLineEndCap;
   LineCapEnum             m_eLineDashCap;
	LineJoinEnum            m_LineJoinEnum;


   COLORREF             m_crEdgeColor;
   eLineType            m_eEdgeType;
   LONG                 m_lEdgeWidth;
	eSpecMode            m_eEdgeWidthMode;
   LineCapEnum             m_eEdgeCap;
   LineCapEnum             m_eEdgeDashCap;
   LineJoinEnum            m_eEdgeJoin;
   BOOL                 m_bEdgeIsVisible;

   COLORREF             m_crFillColor;
   eFillStyle           m_eFillStyle;
   eHatchStyle          m_eHatchStyleIndex;
   
   eSpecMode            m_eMarkerSizeMode;

   COLORREF             m_crAuxiliaryColor;
   INT                  m_iBackgroundMode;   // OPAQUE or TRANSPARENT

   INT                  m_iDirX;             // VDC orientation
   INT                  m_iDirY;
   DOUBLE               m_dObjectRotation;

   DOUBLE               m_dCacheAngle;    // Cached sine/cosines
   DOUBLE               m_dCachedCosine;
   DOUBLE               m_dCachedSine;

   CCGMFile*            m_pParentCGMFile;
   eElementType         m_eElementType;
};



class CCGMText : public CCGMDrawingObject
{
public:
   CCGMText() {};
   void Initialize( CString csText, CPoint ptPosition, long lCharHeight,
      CHAR_ORIENTATION coCharOrientation, COLORREF crColor,
      long lFontIndex, CCGMFile* pParentCGMFile );
	virtual ~CCGMText();
#ifdef _WIN32
	virtual void Draw( HDC hDC, const CSymColorAdjuster& sca);
#endif
   virtual bool UsesColor( COLORREF cr, long mask ){ return UsesLineColor( cr, mask ); }

   // Read-only access for headless consumers (fv::CgmSymbol, phase V4).
   const CString& GetText() const { return m_csText; }
   const CPoint& GetPosition() const { return m_ptPosition; }
   LONG GetCharHeight() const { return m_lCharHeight; }
   LONG GetFontIndex() const { return m_lFontIndex; }
   COLORREF GetTextColor() const { return m_crTextColor; }
   const CHAR_ORIENTATION& GetCharOrientation() const { return m_coCharOrientation; }

protected:
   CString           m_csText;
   CPoint            m_ptPosition;
   LONG              m_lCharHeight;
   LONG              m_lFontIndex;
   COLORREF          m_crTextColor;
   CHAR_ORIENTATION  m_coCharOrientation;
}; // End CCGMText


class CCGMPolyLine : public CCGMDrawingObject
{
public:
	virtual ~CCGMPolyLine();
   VOID Initialize( CCGMFile* pParentCGMFile );

#ifdef _WIN32
	virtual void Draw( HDC hDC, const CSymColorAdjuster& sca);
#endif
   virtual void RotateObject( DOUBLE dRotationAngle );
   virtual bool UsesColor( COLORREF cr, long mask ){ return UsesLineColor( cr, mask ); }
	
   void SetVertCount(long nVertCount);
	void AddVertex(long index, long x, long y);
   void AddVertex(const POINT& ptAdd);   // const&: AddVertex( CPoint(x,y) ) passes a temporary

	POINT_ARRAY m_vertices;
	POINT_ARRAY m_disp_vertices;
};


class CCGMPolygon : public CCGMPolyLine
{
public:
	virtual ~CCGMPolygon();
   VOID Initialize( CCGMFile* pParentCGMFile );

#ifdef _WIN32
	virtual void Draw( HDC hDC, const CSymColorAdjuster& sca);
#endif
	
	// Setup 
	void SetInteriorStyle(eFillStyle style, CCGMPattern* pattern, long image_size_x, long image_size_y);

   virtual bool UsesColor( COLORREF cr, long mask ){ return UsesLineOrFillColor( cr, mask ); }

protected:
#ifdef _WIN32
	CBitmap m_bitmap_pattern;   // realized GDI pattern; POSIX keeps only the parsed CCGMPattern bits
#endif
};



class CCGMPolygonSet : public CCGMPolygon
{
public:
	virtual ~CCGMPolygonSet();
   VOID Initialize( CCGMFile* pParentCGMFile );

#ifdef _WIN32
   virtual void Draw( HDC hDC, const CSymColorAdjuster& sca);
#endif
	void AddVertex(long index, long x, long y, long type);

   // Per-vertex edge flag (CGM EDGEOUT); read-only access for fv::CgmSymbol.
   const CArray<long, long>& GetVertexTypes() const { return m_vertex_types; }

private:
	CArray<long, long> m_vertex_types;
};


#define MAX_ELLIPTICAL_POINTS 20

class CCGMEllipticalObject : public CCGMDrawingObject
{
protected:
   CCGMEllipticalObject() : CCGMDrawingObject() {};       // Nop first constructor
	void Initialize( CCGMFile* pParentCGMFile );
   void Initialize( const CPoint& center, const CPoint& ptCanonicalRadius1, const CPoint& ptCanonicalRadius2, CCGMFile* pParentCGMFile );
	virtual ~CCGMEllipticalObject();

public:
   CPoint   m_ptCenter;
   BOOL     m_bCWDrawing;           // TRUE for CW draw, FALSE for CCW
   CPoint   m_ptCanonicalRadius1;
   CPoint   m_ptCanonicalRadius2;
   LONG     m_lMajorRadius;         //
   LONG     m_lMinorRadius;         //
   DOUBLE   m_dBoundingRotation;    // In direction from +X to +Y axis.  To get bounding rect,
                                    // rotate axes using m_dBoundingRotation 
                                    // and transpose using m_ptCenter
};


class CCGMEllipse : public CCGMEllipticalObject
{
public:
   CCGMEllipse() {}
   void Initialize( CCGMFile* pParentCGMFile );
   void Initialize( const CPoint& ptCenter, const CPoint& ptCanonicalRadius1, const CPoint& ptCanonicalRadius2, CCGMFile* pParentCGMFile );
	virtual ~CCGMEllipse();

#ifdef _WIN32
	virtual void Draw( HDC hDC, const CSymColorAdjuster& sca);
#endif
   virtual bool UsesColor( COLORREF cr, long mask ){ return UsesLineOrFillColor( cr, mask ); }

private:
   void Initialize();        // Common initialization
};


class CCGMEllipticalArc : public CCGMEllipticalObject
{
public:
   CCGMEllipticalArc() {}
   void Initialize( CCGMFile* pParentCGMFile );
	virtual ~CCGMEllipticalArc();

#ifdef _WIN32
   virtual void Draw( HDC hDC, const CSymColorAdjuster& sca );
#endif
   virtual bool UsesColor( COLORREF cr, long mask ) { return UsesLineColor( cr, mask ); }

protected:
	void Initialize( const CPoint& ptCenter, const CPoint& ptCanonicalRadius1, const CPoint& ptCanonicalRadius2, 
      CPoint (&ptRays)[2], CCGMFile* pParentCGMFile );

public:     // Only for tracing
   CPoint         m_ptRays[2];   // In canonical reference frame
   eArcClose   m_eCloseType;

private:
   void Initialize( CPoint (&ptRays)[2] );
};



class CCGMEllipticalArcClose : public CCGMEllipticalArc
{
public:
   CCGMEllipticalArcClose() {}
   void Initialize( CCGMFile* pParentCGMFile );
	virtual ~CCGMEllipticalArcClose();
   virtual bool UsesColor( COLORREF cr, long mask ){ return UsesLineOrFillColor( cr, mask ); }

protected:
	void Initialize( const CPoint& ptCenter, const CPoint& ptCanonicalRadius1, const CPoint& ptCanonicalRadius2, 
      CPoint (&ptRays)[2], LONG lCloseType, CCGMFile* pParentCGMFile );
};


class CCGMCircularArc : public CCGMEllipticalArc
{
public:
   CCGMCircularArc() {}
   void Initialize( CCGMFile* pParentCGMFile );
};


class CCGMCircularArcClose : public CCGMCircularArc
{
public:
   CCGMCircularArcClose() {}
   void Initialize( CCGMFile* pParentCGMFile );
};

class CCGMPicture
{
public:
	CCGMPicture();
	~CCGMPicture();

	CString        m_name;
	long           m_scale_mode;
	double         m_scale;
	eColorMode     m_eColorMode;
   eSpecMode      m_eMarkerSizeMode;
	VDC_EXTENT     m_vdc_extent;
   INT            m_iDirX;          // VDC orientation multipliers
   INT            m_iDirY;          //

	// BEGINPICBODY
// TODO:  Move these to the decoding routine.  These are now in each object created.
	long m_line_type_cont;
	long m_vdc_precision;
	CCGMColorTable m_color_table;

	bool m_use_transparency;
   COLORREF m_crBackgroundColor;
	
   eFillStyle m_eFillStyle;
   COLORREF m_fill_color;

	LONG              m_line_width;
	eSpecMode         m_eLineWidthMode;
	eLineType         m_eLineType;
   COLORREF          m_line_color;
	LineCapEnum          m_eLineEndCap;
	LineCapEnum          m_eLineDashCap;
	LineJoinEnum         m_LineJoinEnum;

	LONG              m_lEdgeWidth;
	eSpecMode         m_eEdgeWidthMode;
	eLineType         m_eEdgeType;
	COLORREF          m_crEdgeColor;
   LineCapEnum          m_eEdgeCap;
   LineCapEnum          m_eEdgeDashCap;
	LineJoinEnum         m_eEdgeJoin;
   BOOL              m_bEdgeIsVisible;

	CRect m_pattern_size;		// Used to limit a pattern for odd shaped objects.
	long m_pattern_index;		// Current pattern to use from the array of patterns.
	CCGMPattern m_pattern[MAX_PATTERNS];	// 64 is the maximum # of patterns.

	// SAMI Line support.
	CApsLineStyle m_sami_line;

	// objects.
	CTypedPtrList<CPtrList, CCGMDrawingObject*> m_drawing_objects;
};

class CCGMFile : public CObject
{
public:
   CCGMFile();          // Public interface for direct use
	virtual ~CCGMFile();

  //////////////////////////////////////////////////////////////////////////////
  //Method:      LoadCGM
  //Description: Load, given filespec
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  static CCGMFile* LoadCGM(LPCTSTR szFile,
                           LPCTSTR szFileName);

  //////////////////////////////////////////////////////////////////////////////
  //Method:      LoadCGM
  //Description: Load, given buffer
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  // NOTE: the "CCGMFile::" qualifier that used to sit here is illegal inside
  // the class body (MSVC accepted it as an extension); removed so clang/GCC
  // parse the declaration.
  CGM_ERROR LoadCGM( PCHAR pCGMData, UINT cgm_size, BOOL bVDCOrientationEnable, BOOL bNewDrawing = FALSE );

  //////////////////////////////////////////////////////////////////////////////
  //Method:      AddRef
  //Description:
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  long AddRef() const;

  //////////////////////////////////////////////////////////////////////////////
  //Method:      Release
  //Description:
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  long Release() const;

  //////////////////////////////////////////////////////////////////////////////
  //Method:      FileName
  //Description: Returns the file name
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  const CString& FileName() const;

  VOID GetBoundingRectangle( RECT& rectBoundingRectangle ) { rectBoundingRectangle = m_rectBoundingRectangle; }

   // TODO:  Make private.
public:
#ifdef _WIN32
   CFont& GetFont( LONG lFontIndex );  // Font from font cache
#endif
   VOID SetDrawingScaling( POINT ptDrawingOffset, DOUBLE dDrawingScale );

	// Misc. File information.  Not used directly.
	CString m_meta_filename;				// Name of Metafile encoded in CGM file.
	CString m_meta_description;			// Metafile description.
	long m_version;							// CGM Version level of file.
	long* m_pelement_list;					// List of elements used in CGM file.
	mutable double m_current_rotation;				// Current rotation of elements stored in this CGM file.
  
	// Precision.  Needed to decode CGM values.
	long m_vdctype;							// VDCTYPE.  Integer or Real.  Virtual Device Coords are encoded as reals or integers.
	long m_integer_precision;				// The number of bits of precision for integers.  Default is 16bit.
	REAL_PRECISION m_real_precision;		// Real precision.
	long m_index_precision;					// Index precision.
	long m_color_precision;					// Color precision.
	long m_color_index_precision;			// Color index precision.
   eColorModel m_eColorModel;       // Color model (must be RGB)

	// Extents
   BOOL GetVDCOrientationEnable() { return m_bVDCOrientationEnable; }
	COLORREF m_min_color_extent;			// Minimum color values.
	COLORREF m_max_color_extent;			// Maximum color values.
	long m_max_color_index;					// Maximum color index.

	// Picture
	CCGMPicture* m_picture;
   CTypedPtrArray< CPtrArray, CCGMPicture* >  m_apPictures;

   // Drawing scale and offset
   POINT    m_ptDrawingOffset;
   DOUBLE   m_dDrawingScale;

   // Miscellaneous
   CStringArray      m_acsFontList;
   INT               m_iTransparencyMode;
	COLORREF          m_crAuxiliaryColor;
   eHatchStyle       m_eHatchStyleIndex;
  

   // Utilities for class constructors
   LONG ReadVDCScaledX();
   LONG ReadVDCScaledY();
   LONG ReadScaledVDC();
   LONG ReadIntInteger();
   LONG ReadIndex();


protected:

  mutable long m_nRefCount;
  CString      m_strFileName; // Filename no extension and path

private:

   // Methods
   VOID InitCGM();
	VOID InitPicture();

	CGM_ERROR DecodeCommandHeader( long& next_command, CGM_OPCODE& opcode );
	
   LONG ReadInteger( long int_precision );
   LONG ReadI16Integer() { return ReadInteger( 16 ); }
   LONG ReadScaledInteger( long int_precision );
   LONG ReadScaledIntInteger();
	DOUBLE ReadDouble( const REAL_PRECISION& dbl_precision );
   DOUBLE ReadRealDouble();
   LONG ReadString( CString& csValue );
	void ReadAttribute( long& lValue );
	void ReadAttribute( DOUBLE& dValue );
	void ReadAttribute( CString& csValue );
   COLORREF ReadColor();
   LONG ReadColorIndex();
   VOID ReadEllipticalObject( CCGMEllipticalObject& eoEllipticalObject );

   // Data
   RECT              m_rectBoundingRectangle;

   // Buffer data
   PBYTE             m_pbCGMData;
   LONG              m_cCGMDataBytes;
   LONG              m_lCGMDataIndex;

   // VDC
   BOOL              m_bVDCOrientationEnable;
   // Text
   CHAR_ORIENTATION  m_coCharOrientation;
   COLORREF          m_crTextColor;
   LONG              m_lCharHeight;
   LONG              m_lFontIndex;

	COLORREF          m_crBackgroundColor;
}; // End CCGMFile


//
// Inline methods
//

//////////////////////////////////////////////////////////////////////////////
//Method:      FileName
//////////////////////////////////////////////////////////////////////////////
inline
const CString& CCGMFile::FileName() const
{
  return m_strFileName;
}


//////////////////////////////////////////////////////////////////////
// Reads the integer according to the encoded precision.  
// The number of bytes consumed is added to the index passed in.  
// pCGMData is a pointer to the start of the encoded integer.
//////////////////////////////////////////////////////////////////////
inline
LONG CCGMFile::ReadInteger( long int_precision )
{
   LONG lResult;
   switch ( int_precision )
   {
      case 16:
		   lResult = (LONG) MAKESHORT( m_pbCGMData + m_lCGMDataIndex );
		   m_lCGMDataIndex += 2;
         break;
  
      case 8:
		   lResult = (LONG) (signed) m_pbCGMData[ m_lCGMDataIndex ];
	      m_lCGMDataIndex += 1;
         break;

      case 32:
		   lResult = MAKELONGVAL( m_pbCGMData + m_lCGMDataIndex );
	      m_lCGMDataIndex += 4;
         break;

      default:
         lResult = 0;
         ASSERT( FALSE );
   }
	return lResult;
}


inline
VOID CCGMPolyLine::Initialize( CCGMFile* pParentCGMFile )
{
   CCGMDrawingObject::Initialize( pParentCGMFile );
   m_eElementType = ELEMTYPE_POLYLINE;
}

inline
CCGMPolyLine::~CCGMPolyLine() 
{
}

inline
void CCGMPolyLine::SetVertCount(long nVertCount)
{
	m_vertices.SetSize(0, nVertCount);
}

inline
void CCGMPolyLine::AddVertex( long index, long x, long y )
{
	AddVertex( CPoint( x, y ) );
}

inline
void CCGMPolyLine::AddVertex( const POINT& ptAdd )
{
	m_vertices.Add( ptAdd );
   m_disp_vertices.Add( ptAdd );    // In case not rotating
}

inline
void CCGMPolyLine::RotateObject( DOUBLE dRotationAngle )
{
   RotateVDC( m_vertices, m_disp_vertices, dRotationAngle );
}

inline
void CCGMFile::ReadAttribute( long& lValue )
{
	// Get the attribute type.
	long attribute_type = ReadInteger( 16 );
	
	// Get the number of values.
	long num_values = ReadInteger( 16 );

	// TODO:  Error check here?
	ASSERT(num_values == 1);
	ASSERT((attribute_type == ATTRIBUTE_TYPE_INDEX) || (attribute_type == ATTRIBUTE_TYPE_CODEDLIST));

	lValue = ReadInteger( 16 );
}

inline
void CCGMFile::ReadAttribute( DOUBLE& dValue )
{
	// Get the attribute type.
	long attribute_type = ReadInteger( 16 );
	
	// Get the number of values.
	long num_values = ReadInteger( 16 );

	// TODO:  Error check here?
	ASSERT(num_values == 1);
	ASSERT(attribute_type == ATTRIBUTE_TYPE_REAL);

	dValue = ReadDouble( m_real_precision );
}

inline
void CCGMFile::ReadAttribute( CString& csValue )
{
	// Get the attribute type.
	long attribute_type = ReadInteger( 16 );
	
	// Get the number of values.
	long num_values = ReadInteger( 16 );

	// TODO:  Error check here?
	ASSERT(num_values == 1);
	ASSERT(attribute_type == ATTRIBUTE_TYPE_STRING);

	BYTE length = m_pbCGMData[ m_lCGMDataIndex++ ];
	csValue = CString( (PCHAR) ( m_pbCGMData + m_lCGMDataIndex ), length );
   m_lCGMDataIndex += length;
}


   inline
VOID CCGMFile::SetDrawingScaling( POINT ptDrawingOffset, DOUBLE dDrawingScale )
{
   m_ptDrawingOffset = ptDrawingOffset;
   m_dDrawingScale = dDrawingScale;
}

// End of CGMFile.h
