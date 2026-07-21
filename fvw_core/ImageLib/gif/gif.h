
/****************************************************************************
*                   gif.h
*
*  This module contains all defines, typedefs, and prototypes for GIF.CPP.
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
* $File: //depot/povray/3.5/source/gif.h $
* $Revision: #8 $
* $Change: 1817 $
* $DateTime: 2002/07/27 10:45:37 $
* $Author: chrisc $
* $Log$
*
*****************************************************************************/


#ifndef GIF_H
#define GIF_H

#include "frame.h"

//#define LOCAL static
//#define IMPORT extern

#define FAST register

typedef unsigned short UWORD;
typedef unsigned char UTINY;
#ifdef _WIN32  // POSIX: 32-bit LONG/ULONG come from fv_compat.h
typedef long LONG;
typedef unsigned long ULONG;
#else
#include "fv_compat.h"
#endif
typedef int INT;


/* Various error codes used by decoder
 * and my own routines...   It's okay
 * for you to define whatever you want,
 * as long as it's negative...  It will be
 * returned intact up the various subroutine
 * levels...
 */
#define OUT_OF_MEMORY -10
#define BAD_CODE_SIZE -20
#if 0    // Not used, conflict with more generally used definitions
#define READ_ERROR -1
#define WRITE_ERROR -2
#define OPEN_ERROR -3
#define CREATE_ERROR -4
#endif
#define MAX_CODES   4095



class CReadGif
{
public:

	CReadGif();
	~CReadGif();


	IMAGE *Current_Image;
	int Bitmap_Line;
	POV_ISTREAM *Bit_File;
	unsigned char *decoderline  /*  [2049] */ ;  /* write-line routines use this */

	IMAGE_COLOUR *gif_colour_map;
	int colourmap_size;



	/* Static variables */
	short curr_size;                     /* The current code size */
	short clear_code;                         /* Value for a clear code */
	short ending;                        /* Value for a ending code */
	short newcodes;                      /* First available code */
	short top_slot;                      /* Highest code for current size */
	short slot;                          /* Last read code */

	/* The following static variables are used
	 * for seperating out codes
	 */
	short navail_bytes;              /* # bytes left in block */
	short nbits_left;                /* # bits left in current byte */
	UTINY b1;                           /* Current byte */
	UTINY byte_buff[257];               /* Current block */
	UTINY *pbytes;                      /* Pointer to next byte in block */

	UTINY *dstack;      /* Stack for storing pixels */
	UTINY *suffix;      /* Suffix table */
	UWORD *prefix;      /* Prefix linked list */

	/* IMPORT INT bad_code_count;
	 *
	 * This value is the only other global required by the using program, and
	 * is incremented each time an out of range code is read by the decoder.
	 * When this value is non-zero after a decode, your GIF file is probably
	 * corrupt in some way...
	 */
	INT bad_code_count;

	int out_line (unsigned char *pixels, int linelen);
	int gif_get_byte (void);
	int Read_Gif_Image (IMAGE *Image, char *filename, char * err_msg);
	int Load_Gif_Image (IMAGE *Image, char *filename, char * err_msg);
	void Destroy_Image(IMAGE *Image);

	// decoder functions
	void cleanup_gif_decoder (void);
	/* changed param to int to avoid problems with 32bit int ANSI compilers. */
	short init_exp (int i_size);
	short get_next_code (void);
	short decoder (int i_linewidth);



};


#endif
