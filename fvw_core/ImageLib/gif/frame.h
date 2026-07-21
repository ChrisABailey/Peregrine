
/****************************************************************************
*                   frame.h
*
*  This header file is included by all C modules in POV-Ray. It defines all
*  globally-accessible types and constants.
*
*  from Persistence of Vision(tm) Ray Tracer
*  Copyright 1996-2002 Persistence of Vision Team
*---------------------------------------------------------------------------
*  NOTICE: This source code file is provided so that users may experiment
*  with enhancements to POV-Ray and to port the software to platforms other
*  than those supported by the POV-Ray Team.  There are strict rules under
*  which you are permitted to use this file.  The rules are in the file
*  named POVLEGAL.DOC which should be distributed with this file.
*  If POVLEGAL.DOC is not available it may be found online at -
*
*    http://www.povray.org/povlegal.html.
*
* This program is based on the popular DKB raytracer version 2.12.
* DKBTrace was originally written by David K. Buck.
* DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
*
* Modified by Andreas Dilger to add PNG file format support 05/09/95
*
* $File: //depot/povray/3.5/source/frame.h $
* $Revision: #75 $
* $Change: 1817 $
* $DateTime: 2002/07/27 10:45:37 $
* $Author: chrisc $
* $Log$
*
*****************************************************************************/

#ifndef FRAME_H
#define FRAME_H

/* Generic header for all modules */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

class pov_istream_class;
class pov_ostream_class;

typedef unsigned long u_int32 ;
typedef unsigned short u_int16 ;
typedef unsigned char u_int8 ;
typedef unsigned char byte ;

typedef signed long int32 ;
typedef signed short int16 ;
typedef signed char int8 ;

//#include "config.h"

#ifndef POV_ISTREAM
#define POV_ISTREAM pov_istream_class
#endif

#ifndef POV_OSTREAM
#define POV_OSTREAM pov_ostream_class
#endif


/* Upper bound for max_trace_level specified by the user */
#ifndef MAX_TRACE_LEVEL_LIMIT
#define MAX_TRACE_LEVEL_LIMIT 256
#endif

/* Various numerical constants that are used in the calculations */
#ifndef EPSILON     /* A small value used to see if a value is nearly zero */
#define EPSILON 1.0e-10
#endif

#ifndef HUGE_VAL    /* A very large value, can be considered infinity */
#define HUGE_VAL 1.0e+17
#endif

/*
 * If the width of a bounding box in one dimension is greater than
 * the critical length, the bounding box should be set to infinite.
 */

#ifndef CRITICAL_LENGTH
#define CRITICAL_LENGTH 1.0e6
#endif

#ifndef BOUND_HUGE  /* Maximum lengths of a bounding box. */
#define BOUND_HUGE 2.0e10
#endif

/*
 * These values determine the minumum and maximum distances
 * that qualify as ray-object intersections.
 */

#define Small_Tolerance 0.001
#define Max_Distance 1.0e7


#ifndef DBL_FORMAT_STRING
#define DBL_FORMAT_STRING "%lf"
#endif

#ifndef DBL
#define DBL double
#endif

#ifndef SNGL
#define SNGL float
#endif

#ifndef COLC
#define COLC float
#endif

#ifndef UCS2
#define UCS2 unsigned short
#endif

#ifndef UCS4
#define UCS4 unsigned long
#endif

#ifndef M_PI
#define M_PI   3.1415926535897932384626
#endif

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

#ifndef TWO_M_PI
#define TWO_M_PI 6.283185307179586476925286766560
#endif

#ifndef M_PI_180
#define M_PI_180 0.01745329251994329576
#endif

#ifndef M_PI_360
#define M_PI_360 0.00872664625997164788
#endif

/* Some implementations of scanf return 0 on failure rather than EOF */
#ifndef SCANF_EOF
#define SCANF_EOF EOF
#endif


#ifndef QSORT
#define QSORT(a,b,c,d) qsort((a),(b),(c),(d))
#endif

/* Get minimum/maximum of three values. */

#define max3(x,y,z) (max((x), max((y), (z))))
#define min3(x,y,z) (min((x), min((y), (z))))


/*
 * POV_NAME_MAX is for file systems that have a separation of the filename
 * into name.ext.  The POV_NAME_MAX is the name part.  FILE_NAME_LENGTH
 * is the sum of name + extension.
 */
#ifndef POV_NAME_MAX
#define POV_NAME_MAX 8
#endif

#ifndef FILE_NAME_LENGTH
#define FILE_NAME_LENGTH 150
#endif

#ifndef FILENAME_SEPARATOR
#define FILENAME_SEPARATOR '/'
#endif

#ifndef DRIVE_SEPARATOR
#define DRIVE_SEPARATOR ':'
#endif

/*
 * Splits a given string into the path and file components using the
 * FILENAME_SEPARATOR and DRIVE_SEPARATOR
 */
#ifndef POV_SPLIT_PATH
#define POV_SPLIT_PATH(s,p,f) POV_Split_Path((s),(p),(f))
#endif

/* The output file format used if the user doesn't specify one */
#ifndef DEFAULT_OUTPUT_FORMAT
#define DEFAULT_OUTPUT_FORMAT   't'
#endif

/* System specific image format like BMP for Windows or PICT for Mac */
#ifndef READ_SYS_IMAGE
#define READ_SYS_IMAGE(i,f) Read_Targa_Image(i,f)
#endif

#ifndef SYS_IMAGE_CLASS
#define SYS_IMAGE_CLASS Targa_Image
#endif

#ifndef SYS_DEF_EXT
#define SYS_DEF_EXT ".tga"
#endif


/*
 * The TIME macros are used when displaying the rendering time for the user.
 * These are called in such a manner that STOP_TIME can be called multiple
 * times for a givn START_TIME in order to get intermediate TIME_ELAPSED
 * values.  TIME_ELAPSED is often defined as (tstop - tstart).
 */
#ifndef START_TIME
#define START_TIME time(&tstart);     
#endif

#ifndef STOP_TIME
#define STOP_TIME  time(&tstop);
#endif

#ifndef TIME_ELAPSED
#define TIME_ELAPSED difftime (tstop, tstart);
#endif

#ifndef SPLIT_TIME
#define SPLIT_TIME(d,h,m,s) POV_Std_Split_Time ((d),(h),(m),(s))
#endif


/*
 * Font related macros [trf]
 */

#ifndef POV_CONVERT_TEXT_TO_UTF8
#define POV_CONVERT_TEXT_TO_UTF8(ts, tsl, as) (NULL)
#endif

/*
 * The COOPERATE macros are used on co-operative multi-tasking systems to
 * return control to the GUI or OS.  COOPERATE is the old form, and one
 * or both of COOPERATE_0 and COOPERATE_1 should be defined instead.
 */
#ifdef COOPERATE
#define COOPERATE_0     COOPERATE
#define COOPERATE_1     COOPERATE
#define COOPERATE_2     COOPERATE
#endif

#ifndef COOPERATE_0    /* Called less frequently */
#define COOPERATE_0
#endif

#ifndef COOPERATE_1    /* Called more frequently */
#define COOPERATE_1
#endif

#ifndef COOPERATE_2    /* Called only when using povray_cooperate */
#define COOPERATE_2
#endif


/* How to get input from the user */
#ifndef TEST_ABORT
#define TEST_ABORT
#endif

#ifndef WAIT_FOR_KEYPRESS
#define WAIT_FOR_KEYPRESS
#else
#define WAIT_FOR_KEYPRESS_EXISTS
#endif

#ifndef GET_KEY /* Gets a keystroke from the user without waiting */
#define GET_KEY
#else
#define GET_KEY_EXISTS
#endif


/*
 * Functions that write text for the user to see.  These functions will
 * usually be customized for GUI environments so that POV outputs its
 * messages to a status bar or popup window. If you don't want a specific
 * stream to output anything, just define the the macro for the stream
 * to NULL.
 */

#ifndef POV_BANNER
#define POV_BANNER POV_Std_Console
#endif

#ifndef POV_WARNING
#define POV_WARNING POV_Std_Console
#endif

#ifndef POV_RENDER_INFO
#define POV_RENDER_INFO POV_Std_Console
#endif

#ifndef POV_STATUS_INFO
#define POV_STATUS_INFO POV_Std_Console
#endif

#ifndef POV_DEBUG_INFO
#define POV_DEBUG_INFO POV_Std_Console
#endif

#ifndef POV_FATAL
#define POV_FATAL POV_Std_Console
#endif

#ifndef POV_STATISTICS
#define POV_STATISTICS POV_Std_Console
#endif

#ifndef POV_CONSOLE
#define POV_CONSOLE POV_Std_DummyConsole
#endif


#ifndef POV_GET_FULL_PATH      /* returns full pathspec */
#define POV_GET_FULL_PATH(f,p,b) if (b) strcpy(b,p);
#endif



/*****************************************************************************
 *
 * MEMIO.C Memory macros
 *
 *****************************************************************************/

#ifndef __FILE__
#define __FILE__ ""
#endif

#ifndef __LINE__
#define __LINE__ (-1)
#endif

/*
 * These functions define macros which do checking for memory allocation,
 * and can also do other things.  Check mem.c before you change them, since
 * they aren't simply replacements for malloc, calloc, realloc, and free.
 */
#ifndef POV_MALLOC
#define POV_MALLOC(size,msg)        pov_malloc ((size), __FILE__, __LINE__, (msg))
#endif

#ifndef POV_CALLOC
#define POV_CALLOC(nitems,size,msg) pov_calloc ((nitems), (size), __FILE__, __LINE__, (msg))
#endif

#ifndef POV_REALLOC
#define POV_REALLOC(ptr,size,msg)   pov_realloc ((ptr), (size), __FILE__, __LINE__, (msg))
#endif

#ifndef POV_FREE
#define POV_FREE(ptr)               { pov_free ((void *)(ptr), __FILE__, __LINE__); (ptr) = NULL; }
#endif

#ifndef POV_MEM_INIT
#define POV_MEM_INIT()              mem_init()
#endif

#ifndef POV_MEM_RELEASE_ALL
#define POV_MEM_RELEASE_ALL()       mem_release_all()
#endif

#ifndef POV_STRDUP
#define POV_STRDUP(str)             pov_strdup(str)
#endif

/* For those systems that don't have memmove, this can also be pov_memmove */
#ifndef POV_MEMMOVE
#define POV_MEMMOVE(dst,src,len)    pov_memmove((dst),(src),(len))
#endif


/*
 * These functions replace new and delete.  Do not, never ever use the
 * new[] or delete[] operator, use POV_MALLOC and POV_FREE only.  This
 * implies that you cannot use arrays of C++ objects in POV-Ray 3.5 source
 * code. There are reasons for this, and in POV-ray 4.0 memory managment
 * will be improved to allow regular C++ syntax, not these special macros.
 * 
 * The macros are based on POV_MALLOC and POV_FREE.  There are several
 * macros to adjust these to your local compiler and C++ implementation.
 * Currently POV-Ray does not support C++ exceptions, so be careful when 
 * changing these macros. The replacement of works by using the placement
 * forms of new and delete.
 *
 * POV_CPP_MEM_HAS_NEW_INCLUDE (default is 1)
 *   If <new> should be included.
 *
 * POV_CPP_MEM_HAS_PLACEMENT_FORMS (default is 0)
 *   If placement forms of operators are supported.
 *
 * POV_CPP_MEM_HAS_NOTHROW_SUPPORT (default is 1)
 *   If the nothrow argument for new and delete are supported.
 *
 */


#if(POV_CPP_MEM_HAS_PLACEMENT_FORMS == 1)

  #define POV_NEW(type)                 new((void *)POV_MALLOC(sizeof(type), "new")) type
  #define POV_DELETE(ptr, type)         { if(ptr != NULL) { ptr->~type(); operator delete(ptr, NULL); POV_FREE((void *)ptr); ptr = NULL; } }
//  #define POV_DELETE_PLAIN(ptr, type)   { if(ptr != NULL) { operator delete(ptr, NULL); POV_FREE((void *)ptr); ptr = NULL; } }

#else

//  #if(POV_CPP_MEM_HAS_NOTHROW_SUPPORT == 1)
//    #define POV_NEW(type)               new(nothrow) type
//    #define POV_DELETE(ptr, junk)       { if(ptr != NULL) { delete(nothrow) ptr; ptr = NULL; } }
//    #define POV_DELETE_PLAIN(ptr, junk) { if(ptr != NULL) { delete(nothrow) ptr; ptr = NULL; } }
//  #else
    #define POV_NEW(type)               new type
    #define POV_DELETE(ptr, junk)       { if(ptr != NULL) { delete ptr; ptr = NULL; } }
//    #define POV_DELETE_PLAIN(ptr, junk) { if(ptr != NULL) { delete ptr; ptr = NULL; } }
//  #endif

#endif


/*
 * Functions which invoke external programs to do work for POV, generally
 * at the request of the user.
 */
#ifndef POV_SHELLOUT
#define POV_SHELLOUT(string) pov_shellout(string)
#endif

#ifndef POV_MAX_CMD_LENGTH
#define POV_MAX_CMD_LENGTH 250
#endif

#ifndef POV_SYSTEM
#define POV_SYSTEM(string) system(string)
#endif


/*
 * New file io stuff.
 */

#ifndef POV_NEW_ISTREAM
#define POV_NEW_ISTREAM POV_New_IStream
#endif

#ifndef POV_NEW_OSTREAM
#define POV_NEW_OSTREAM POV_New_OStream
#endif

/* Functions to delete and rename a file */
#ifndef DELETE_FILE_ERR
#define DELETE_FILE_ERR -1
#endif

#ifndef DELETE_FILE
#define DELETE_FILE(name) unlink(name)
#endif

#ifndef RENAME_FILE_ERR
#define RENAME_FILE_ERR -1
#endif

#ifndef RENAME_FILE
#define RENAME_FILE(orig,new) rename(orig,new)
#endif

#ifndef EXIST_FILE
#define EXIST_FILE(name) POV_File_Exist(name)
#endif

#ifndef EXIST_FONT_FILE
#define EXIST_FONT_FILE(name) (0)
#endif

#ifndef FONT_FILE_PATH
#define FONT_FILE_PATH(name) POV_STRDUP(name)
#endif

#ifndef MAX_BUFSIZE  /* The maximum size of the output file buffer */
#define MAX_BUFSIZE INT_MAX
#endif


/*****************************************************************************
 *
 * Scalar, color and vector stuff.
 *
 *****************************************************************************/

typedef DBL UV_VECT [2];
typedef DBL VECTOR [3];
typedef DBL VECTOR_4D [4];
typedef DBL MATRIX [4][4];
typedef DBL EXPRESS [5];
typedef COLC COLOUR [5];
typedef COLC RGB [3];

/* Vector array elements. */
enum
{
	U = 0,
	V = 1
};

enum
{
	X = 0,
	Y = 1,
	Z = 2,
	T = 3
};



/*****************************************************************************
 *
 * Color map stuff.
 *
 *****************************************************************************/


/*****************************************************************************
 *
 * IFF file stuff.
 *
 *****************************************************************************/

#ifndef IFF_SWITCH_CAST
#define IFF_SWITCH_CAST (int)
#endif

typedef struct Image_Colour_Struct IMAGE_COLOUR;

typedef struct Image8_Line_Struct IMAGE8_LINE;

typedef struct Image16_Line_Struct IMAGE16_LINE;

struct Image_Colour_Struct
{
  unsigned short Red, Green, Blue, Filter, Transmit;
};

struct Image8_Line_Struct
{
  unsigned char *red, *green, *blue, *transm;
};

struct Image16_Line_Struct
{
  unsigned short *red, *green, *blue, *transm;
};


/*****************************************************************************
 *
 * Image stuff.
 *
 *****************************************************************************/

/* Legal image attributes. */

#define NO_FILE         0x00000000
#define GIF_FILE        0x00000001
#define POT_FILE        0x00000002
#define SYS_FILE        0x00000004
#define IFF_FILE        0x00000008
#define TGA_FILE        0x00000010
#define GRAD_FILE       0x00000020
#define PGM_FILE        0x00000040
#define PPM_FILE        0x00000080
#define PNG_FILE        0x00000100
#define JPEG_FILE       0x00000200
#define TIFF_FILE       0x00000400

#define IMAGE_FILE_MASK 0x000007FF

#define IMAGE_FTYPE     0x00000800
#define HF_FTYPE        0x00001000
#define HIST_FTYPE      0x00002000
#define GRAY_FTYPE      0x00004000
#define NORMAL_FTYPE    0x00008000
#define MATERIAL_FTYPE  0x00010000

#define IS16BITIMAGE    0x00020000
#define IS16GRAYIMAGE   0x00040000

/* Image types. */

#define IMAGE_FILE    IMAGE_FTYPE+GIF_FILE+SYS_FILE+IFF_FILE+GRAD_FILE+TGA_FILE+PGM_FILE+PPM_FILE+PNG_FILE+JPEG_FILE+TIFF_FILE
#define NORMAL_FILE   NORMAL_FTYPE+GIF_FILE+SYS_FILE+IFF_FILE+GRAD_FILE+TGA_FILE+PGM_FILE+PPM_FILE+PNG_FILE+JPEG_FILE+TIFF_FILE
#define MATERIAL_FILE MATERIAL_FTYPE+GIF_FILE+SYS_FILE+IFF_FILE+GRAD_FILE+TGA_FILE+PGM_FILE+PPM_FILE+PNG_FILE+JPEG_FILE+TIFF_FILE
#define HF_FILE       HF_FTYPE+GIF_FILE+SYS_FILE+POT_FILE+TGA_FILE+PGM_FILE+PPM_FILE+PNG_FILE+JPEG_FILE+TIFF_FILE

typedef struct Image_Struct IMAGE;

struct Image_Struct
{
  int References; /* Keeps track of number of pointers to this structure */
  int Map_Type;
  int File_Type;
  int Image_Type; /* What this image is being used for */
  int Interpolation_Type;
  int iwidth, iheight;
  short Colour_Map_Size;
  char Once_Flag;
  char Use_Colour_Flag;
  char Interlaced;
  char IsGif89;
  VECTOR Gradient;
  SNGL width, height;
  UV_VECT Offset;
  DBL AllFilter, AllTransmit; 
  IMAGE_COLOUR *Colour_Map;
  void *Object;
  union
  {
    IMAGE8_LINE *rgb8_lines;
    IMAGE16_LINE *rgb16_lines;
    unsigned short **gray16_lines;
    unsigned char **map_lines;
  } data;
};

#define PIGMENT_TYPE  0
#define NORMAL_TYPE   1
#define PATTERN_TYPE  2
#define TEXTURE_TYPE  4
#define COLOUR_TYPE   5
#define SLOPE_TYPE    6
#define DENSITY_TYPE  7

#define DEFAULT_FRACTAL_EXTERIOR_TYPE 1
#define DEFAULT_FRACTAL_INTERIOR_TYPE 0
#define DEFAULT_FRACTAL_EXTERIOR_FACTOR 1
#define DEFAULT_FRACTAL_INTERIOR_FACTOR 1


/*****************************************************************************
 *
 * Pigment, Tnormal, Finish, Texture & Warps stuff.
 *
 *****************************************************************************/

typedef struct Density_file_Struct DENSITY_FILE;
typedef struct Density_file_Data_Struct DENSITY_FILE_DATA;

struct Density_file_Struct
{
  int Interpolation;
  DENSITY_FILE_DATA *Data;
};

struct Density_file_Data_Struct
{
  int References;
  char *Name;
  int Sx, Sy, Sz;
  unsigned char ***Density;
};


#define Destroy_Finish(x) if ((x)!=NULL) POV_FREE(x)



/*****************************************************************************
 *
 * Frame tracking information
 *
 *****************************************************************************/

typedef enum
{
  FT_SINGLE_FRAME,
  FT_MULTIPLE_FRAME
} FRAMETYPE;

#define INT_VALUE_UNSET (-1)
#define DBL_VALUE_UNSET (-1.0)

typedef struct
{
  FRAMETYPE FrameType;
  DBL Clock_Value;      /* May change between frames of an animation */
  int FrameNumber;      /* May change between frames of an animation */

  int InitialFrame;
  DBL InitialClock;

  int FinalFrame;
  int FrameNumWidth;
  DBL FinalClock;

  int SubsetStartFrame;
  DBL SubsetStartPercent;
  int SubsetEndFrame;
  DBL SubsetEndPercent;
  
  bool Field_Render_Flag;
  bool Odd_Field_Flag;
} FRAMESEQ;


/*****************************************************************************
 *
 * Miscellaneous stuff.
 *
 *****************************************************************************/

typedef struct Chunk_Header_Struct CHUNK_HEADER;
typedef struct Data_File_Struct DATA_FILE;
typedef struct complex_block complex;
typedef struct file_handle_struct FILE_HANDLE;
typedef int TOKEN;
typedef struct Reserved_Word_Struct RESERVED_WORD;

struct Reserved_Word_Struct
{
  TOKEN Token_Number;
  char *Token_Name;
};

typedef struct Sym_Table_Entry SYM_ENTRY;

struct Sym_Table_Entry 
{
  SYM_ENTRY *next;
  char *Token_Name;
  void *Data;
  TOKEN Token_Number;
};

struct Chunk_Header_Struct
{
  long name;
  long size;
};

struct Data_File_Struct
{
  POV_ISTREAM *In_File;
  POV_OSTREAM *Out_File;
  int Line_Number,R_Flag;
  char *Filename;
};

struct complex_block
{
  DBL r, c;
};

enum
{
	READ_MODE = 0,
	WRITE_MODE = 1,
	APPEND_MODE = 2
};

// This is a terrible class design, but it fits well with the even worse 3.1 code... [trf]
class Image_File_Class
{
	public:
		Image_File_Class() { valid = false; };
		virtual ~Image_File_Class() { };

		virtual void Write_Line(COLOUR *line_data) = 0;
		virtual int Read_Line(COLOUR *line_data) = 0;

		virtual int Line() = 0;
		virtual int Width() = 0;
		virtual int Height() = 0;

		bool Valid() { return valid; };
	protected:
		bool valid;
};

#ifndef POV_ALLOW_FILE_READ
#define POV_ALLOW_FILE_READ(f,t) (1)
#endif

#ifndef POV_ALLOW_FILE_WRITE
#define POV_ALLOW_FILE_WRITE(f,t) (1)
#endif

#ifdef USE_SYSPROTO
#include "sysproto.h"
#endif

#endif
