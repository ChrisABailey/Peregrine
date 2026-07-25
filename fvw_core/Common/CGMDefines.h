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

#ifndef __CGMDEFINES_H__
#define __CGMDEFINES_H__

//////////////////////////////////////
// Opcodes - Classes
//////////////////////////////////////

enum CGM_OPCODE_CLASS
{
	CGM_CLASS_DELIMITER   = 0,
	CGM_CLASS_METAFILE    = 1, 
	CGM_CLASS_PICTURE	    = 2, 
	CGM_CLASS_CONTROL	    = 3, 
	CGM_CLASS_GRAPHICAL   = 4, 
	CGM_CLASS_GRAPHICAL2  = 5,
	CGM_CLASS_ESCAPE      = 6,
	CGM_CLASS_EXTERNAL    = 7,
	CGM_CLASS_SEGMENT     = 8,
	CGM_CLASS_APS			 = 9			// Version 4.	
};

enum CGM_OPCODE_ID_DELIMITER
{
	CGM_ID_BEGMF			 = 1,
	CGM_ID_ENDMF			 = 2,
	CGM_ID_BEGPIC			 = 3,
	CGM_ID_BEGPICBODY     = 4,
	CGM_ID_ENDPIC			 = 5,
	CGM_ID_BEGSEG         = 6,
	CGM_ID_ENDSEG         = 7,
	CGM_ID_BEGFIGURE		 = 8,
	CGM_ID_ENDFIGURE      = 9,
	CGM_ID_BEGPROTREGION  = 13,
	CGM_ID_ENDPROTREGION  = 14,
	CGM_ID_BEGCOMPOLINE   = 15,
	CGM_ID_ENDCOMPOLINE   = 16,
	CGM_ID_BEGCOMPOTEXTPATH = 17,
	CGM_ID_ENDCOMPOTEXTPATH = 18,
	CGM_ID_BEGTILEARRAY     = 19,
	CGM_ID_ENDTILEARRAY     = 20,
	CGM_ID_BEGAPS				= 21,
	CGM_ID_BEGAPSBODY			= 22,
	CGM_ID_ENDAPS				= 23
};

enum CGM_OPCODE_ID_METAFILE
{
	CGM_ID_MFVERSION			= 1,
	CGM_ID_MFDESC				= 2,
	CGM_ID_VDCTYPE				= 3,
	CGM_ID_INTEGERPREC		= 4,
	CGM_ID_REALPREC			= 5,
	CGM_ID_INDEXPREC			= 6,
	CGM_ID_COLRPREC			= 7,
	CGM_ID_COLRINDEXPREC    = 8,
	CGM_ID_MAXCOLRINDEX     = 9,
	CGM_ID_COLRVALUEEXT     = 10,
	CGM_ID_MFELEMLIST       = 11,
	CGM_ID_BEGMFDEFAULTS    = 12,
	CGM_ID_FONTLIST			= 13,
	CGM_ID_CHARSETLIST		= 14,
	CGM_ID_CHARCODING			= 15,
	CGM_ID_NAMEPREC			= 16,
	CGM_ID_MAXVDCEXT			= 17,
	CGM_ID_SEGPRIEXT			= 18,
	CGM_ID_COLRMODEL			= 19,
	CGM_ID_COLRCALIB			= 20,
	CGM_ID_FONTPROP			= 21,
	CGM_ID_GLYPHMAP			= 22,
	CGM_ID_SYMBOLLIBLIST		= 23
};

enum CGM_OPCODE_ID_CONTROL
{
	CGM_ID_VDCINTEGERPREC	= 1,
	CGM_ID_VDCREALPREC		= 2,
	CGM_ID_AUXCOLR				= 3,
	CGM_ID_TRANSPARENCY		= 4,
	CGM_ID_CLIPRECT			= 5,
	CGM_ID_CLIP					= 6,
	CGM_ID_LINECLIPMODE		= 7,
	CGM_ID_MARKERCLIPMODE	= 8,
	CGM_ID_EDGECLIPMODE		= 9,
	CGM_ID_NEWREGION			= 10,
	CGM_ID_SAVEPRIMCONT		= 11,
	CGM_ID_RESPRIMCONT		= 12,
	CGM_ID_PROTREGION			= 17,
	CGM_ID_GENTEXTPATHMODE	= 18,
	CGM_ID_MITRELIMIT			= 19,
	CGM_ID_TRANSPCELLCOLR	= 20
};

enum CGM_OPCODE_ID_PICTURE
{
	CGM_ID_SCALEMODE			= 1,
	CGM_ID_COLRMODE			= 2,
	CGM_ID_LINEWIDTHMODE		= 3,
	CGM_ID_MARKERSIZEMODE	= 4,
	CGM_ID_EDGEWIDTHMODE		= 5, 
	CGM_ID_VDCEXT				= 6,
	CGM_ID_BACKCOLR			= 7,
	CGM_ID_DEVVP				= 8,
	CGM_ID_DEVVPMODE			= 9,
	CGM_ID_DEVVPMAP			= 10,
	CGM_ID_LINEREP				= 11,
	CGM_ID_MARKERREP			= 12,
	CGM_ID_TEXTREP				= 13,
	CGM_ID_FILLREP				= 14,
	CGM_ID_EDGEREP				= 15,
	CGM_ID_INTSTYLEMODE		= 16,
	CGM_ID_LINEEDGETYPEDEF  = 17,
	CGM_ID_HATCHSTYLEDEF		= 18,
	CGM_ID_GEOPATDEF			= 19
};

enum CGM_OPCODE_ID_GRAPHICAL
{
	CGM_ID_LINE					= 1,
	CGM_ID_DISJTLINE			= 2,
	CGM_ID_MARKER				= 3,
	CGM_ID_TEXT					= 4,
	CGM_ID_RESTRTEXT			= 5,
	CGM_ID_APNDTEXT			= 6,
	CGM_ID_POLYGON				= 7,
	CGM_ID_POLYGONSET			= 8,
	CGM_ID_CELLARRAY			= 9,
	CGM_ID_GDP					= 10,
	CGM_ID_RECT					= 11,
	CGM_ID_CIRCLE				= 12,
	CGM_ID_ARC3PT				= 13,
	CGM_ID_ARC3PTCLOSE		= 14,
	CGM_ID_ARCCTR				= 15,
	CGM_ID_ARCCTRCLOSE		= 16,
	CGM_ID_ELLIPSE				= 17,
	CGM_ID_ELLIPARC			= 18,
	CGM_ID_ELLIPARCCLOSE		= 19,
	CGM_ID_ARCCTRREV			= 20,
	CGM_ID_CONNEDGE			= 21,
	CGM_ID_HYPERBARC			= 22,
	CGM_ID_PARABARC			= 23,
	CGM_ID_NUB					= 24,
	CGM_ID_NURB					= 25, 
	CGM_ID_POLYBEZIER			= 26,
	CGM_ID_SYMBOL				= 27,
	CGM_ID_BITONALTILE		= 28,
	CGM_ID_TILE					= 29
};

enum CGM_OPCODE_ID_GRAPHICAL2
{
	CGM_ID_LINEINDEX					= 1,
	CGM_ID_LINETYPE					= 2, 
	CGM_ID_LINEWIDTH					= 3,
	CGM_ID_LINECOLR					= 4,
	CGM_ID_MARKERINDEX				= 5,
	CGM_ID_MARKERTYPE					= 6,
	CGM_ID_MARKERSIZE					= 7,
	CGM_ID_MARKERCOLR					= 8,
	CGM_ID_TEXTINDEX					= 9,
	CGM_ID_TEXTFONTINDEX				= 10,
	CGM_ID_TEXTPREC					= 11,
	CGM_ID_CHAREXPAN					= 12,
	CGM_ID_CHARSPACE					= 13,
	CGM_ID_TEXTCOLR					= 14,
	CGM_ID_CHARHEIGHT					= 15,
	CGM_ID_CHARORI						= 16,
	CGM_ID_TEXTPATH					= 17,
	CGM_ID_TEXTALIGN					= 18,
	CGM_ID_CHARSETINDEX				= 19,
	CGM_ID_ALTCHARSETINDEX			= 20,
	CGM_ID_FILLINDEX					= 21,
	CGM_ID_INTSTYLE					= 22,
	CGM_ID_FILLCOLR					= 23,
	CGM_ID_HATCHINDEX					= 24,
	CGM_ID_PATINDEX					= 25,
	CGM_ID_EDGEINDEX					= 26,
	CGM_ID_EDGETYPE					= 27,
	CGM_ID_EDGEWIDTH					= 28,
	CGM_ID_EDGECOLR					= 29,
	CGM_ID_EDGEVIS						= 30,
	CGM_ID_FILLREFPT					= 31,
	CGM_ID_PATTABLE					= 32,
	CGM_ID_PATSIZE						= 33,
	CGM_ID_COLRTABLE					= 34,
	CGM_ID_ASF							= 35,
	CGM_ID_PICKID						= 36,
	CGM_ID_LINECAP						= 37,
	CGM_ID_LINEJOIN					= 38,
	CGM_ID_LINETYPECONT				= 39,
	CGM_ID_LINETYPEINITOFFSET		= 40,
	CGM_ID_TEXTSCORETYPE				= 41,
	CGM_ID_RESTRTEXTTYPE				= 42,
	CGM_ID_INTERPINT					= 43,
	CGM_ID_EDGECAP						= 44,
	CGM_ID_EDGEJOIN					= 45,
	CGM_ID_EDGETYPECONT				= 46,
	CGM_ID_EDGETYPEINITOFFSET		= 47,
	CGM_ID_SYMBOLLIBINDEX			= 48,
	CGM_ID_SYMBOLCOLR					= 49,
	CGM_ID_SYMBOLSIZE					= 50,
	CGM_ID_SYMBOLORI					= 51
};

enum CGM_OPCODE_ID_APS
{
	CGM_ID_APSATTR						= 1
};


// VDCTYPE 
#define CGM_VDCTYPE_INTEGER 0
#define CGM_VDCTYPE_REAL    1

// Real precision 
#define CGM_REAL_FLOATING_POINT 0
#define CGM_REAL_FIXED_POINT    1

// Scaling mode
#define CGM_SCALE_ABSTRACT		0
#define CGM_SCALE_METRIC		1

// Color Selection Mode
#define CGM_COLOR_MODE_INDEXED	0
#define CGM_COLOR_MODE_DIRECT		1

#if 0
// Line CAP
#define CGM_LINE_CAP_UNSPECIFIED		1		// Line / Dash
#define CGM_LINE_CAP_BUTT				2		// Line / Dash	
#define CGM_LINE_CAP_ROUND				3		// Line
#define CGM_LINE_CAP_MATCH				3		// Dash
#define CGM_LINE_CAP_PROJ_SQUARE		4		// Line
#define CGM_LINE_CAP_TRIANGLE			5		// Line

// Line Join
#define CGM_LINE_JOIN_UNSPECIFIED	1
#define CGM_LINE_JOIN_MITRE			2
#define CGM_LINE_JOIN_ROUND			3
#define CGM_LINE_JOIN_BEVEL			4
#else

// Line CAP
enum LineCapEnum
{
   CGM_LINE_CAP_UNSPECIFIED	= 1,  // Line / Dash
   CGM_LINE_CAP_BUTT				= 2,  // Line / Dash	
   CGM_LINE_CAP_ROUND			= 3,	// Line
   CGM_LINE_CAP_MATCH			= 3,	// Dash
   CGM_LINE_CAP_PROJ_SQUARE	= 4,	// Line
   CGM_LINE_CAP_TRIANGLE		= 5   // Line
};

// Line Join
enum LineJoinEnum
{
   CGM_LINE_JOIN_UNSPECIFIED  = 1,
   CGM_LINE_JOIN_MITRE			= 2,
   CGM_LINE_JOIN_ROUND			= 3,
   CGM_LINE_JOIN_BEVEL			= 4
};
#endif

// Line Type Continuation
#define CGM_LINE_CONT_UNSPECIFIED	1
#define CGM_LINE_CONT_CONTINUE		2
#define CGM_LINE_CONT_RESTART			3
#define CGM_LINE_CONT_ADAPTCONT		4		

// Transparency
#define CGM_TRANSPARENCY_OFF			0
#define CGM_TRANSPARENCY_ON			1

// Interior / Fill Style
   // Defined in IDL.

// Line type / Edge type
   // Defined in IDL.

// Edge Visibility
#define CGM_EDGE_VISIBILITY_OFF		0
#define CGM_EDGE_VISIBILITY_ON		1

// Polygonset line segment types
#define CGM_POLYGONSET_LINE_INVISIBLE	0
#define CGM_POLYGONSET_LINE_VISIBLE		1
#define CGM_POLYGONSET_LINE_CLOSE_INV	2
#define CGM_POLYGONSET_LINE_CLOSE_VIS	3


typedef struct _CGM_OPCODE
{
	CGM_OPCODE_CLASS	cgm_class;
	long opcode_size;
	union
	{
		DWORD								cgm_id;
		CGM_OPCODE_ID_DELIMITER		cgm_id_delimeter;
		CGM_OPCODE_ID_METAFILE		cgm_id_metafile;
		CGM_OPCODE_ID_PICTURE		cgm_id_picture;
		CGM_OPCODE_ID_CONTROL		cgm_id_control;
		CGM_OPCODE_ID_GRAPHICAL		cgm_id_graphical;
		CGM_OPCODE_ID_GRAPHICAL2	cgm_id_graphical2;
		CGM_OPCODE_ID_APS				cgm_id_aps;
	};
} CGM_OPCODE;


#define MAKESHORT(pbuffer) ((short) ((BYTE) *(pbuffer) << 8)) | ((short)((BYTE)*(pbuffer + 1)))
#define MAKELONGVAL(pbuffer) ((long) ((BYTE) *(pbuffer) << 24)) |			\
									  ((long) (BYTE) *(pbuffer + 1) << 16) |		\
									  ((long) (BYTE) *(pbuffer + 2) << 8) |		\
									  ((long) (BYTE) *(pbuffer + 3))


// SAMI 

// Anchors
#define SAMI_ANCHOR_BEGINNING		0
#define SAMI_ANCHOR_MIDDLE			1
#define SAMI_ANCHOR_END				2

// Iteration Type
#define SAMI_ITERATION_CONTINUOUS	0
#define SAMI_ITERATION_SINGLE			1

// Element Type
#define SAMI_ELEMENT_TYPE_GAP				0
#define SAMI_ELEMENT_TYPE_DASH			1
#define SAMI_ELEMENT_TYPE_POINTSYMBOL	2

// Symbol Orientation
#define SAMI_SYMBOL_ORIENTATION_CONSTANTANGLE	0
#define SAMI_SYMBOL_ORIENTATION_TANGENTIAL		1





#endif
