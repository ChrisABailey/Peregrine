// leave defined in third-party code
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE

/****************************************************************************
*                   gif.cpp
*
*  Gif-format file reader.
*
*  NOTE:  Portions of this module were written by Steve Bennett and are used
*         here with his permission.
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
* $File: //depot/povray/3.5/source/gif.cpp $
* $Revision: #11 $
* $Change: 1817 $
* $DateTime: 2002/07/27 10:45:37 $
* $Author: chrisc $
* $Log$
*
*****************************************************************************/

/*
 * The following routines were borrowed freely from FRACTINT, and represent
 * a generalized GIF file decoder.  This once seemed the best, most universal
 * format for reading in Bitmapped images, until Unisys began enforcing
 * its patent on the LZ compression that GIF uses.  POV-Ray, as freeware, is
 * exempt from GIF licensing fees.  GIF is a Copyright of Compuserve, Inc.
 *
 * Swiped and converted to entirely "C" coded routines by AAC for the most
 * in future portability!
 */

#include "stdafx.h"
#include "frame.h"
#include "gif.h"
#include "gifdecod.h"
#include "file_pov.h"

/*
static void Error(char *msg)
{
	char msg2[200];

	sprintf(msg2, msg);
	AfxMessageBox(msg2);
}
*/

CReadGif::CReadGif()
{
	navail_bytes = 0;              /* # bytes left in block */
	nbits_left = 0;                /* # bits left in current byte */
	pbytes = NULL;                      /* Pointer to next byte in block */

	dstack = NULL;      /* Stack for storing pixels */
	suffix = NULL;      /* Suffix table */
	prefix = NULL;      /* Prefix linked list */
	decoderline = NULL;              /* decoded line goes here */
	Bit_File = NULL;

}

CReadGif::~CReadGif()
{
	cleanup_gif_decoder ();
	if (Bit_File != NULL)
	{
		free(Bit_File);
		Bit_File = NULL;
	}
}



/*****************************************************************************
* Static functions
******************************************************************************/



/*****************************************************************************
*
* FUNCTION
*
*   out_line
*
* INPUT
*   
* OUTPUT
*   
* RETURNS
*   
* AUTHOR
*
*   POV-Ray Team
*   
* DESCRIPTION
*
*   -
*
* CHANGES
*
*   -
*
******************************************************************************/

int CReadGif::out_line (unsigned char *pixels, int linelen)
{
  register int x;
  register unsigned char *line;

  if (Bitmap_Line == Current_Image->iheight)
  {
//    Warning (0, "Extra data at end of GIF image.");
    return (0) ;
  }

  line = Current_Image->data.map_lines[Bitmap_Line++];

  for (x = 0; x < linelen; x++)
  {
    if ((int)(*pixels) > Current_Image->Colour_Map_Size)
    {
//      Error ("Error - GIF image map color out of range.");
    }

    line[x] = *pixels;

    pixels++;
  }

  return (0);
}




/*****************************************************************************
*
* FUNCTION
*
*   gif_get_byte
*
* INPUT
*   
* OUTPUT
*   
* RETURNS
*   
* AUTHOR
*
*   POV-Ray Team
*   
* DESCRIPTION
*
*   Get byte from file, return the next byte or an error.
*
* CHANGES
*
*   -
*
******************************************************************************/

int CReadGif::gif_get_byte()
{
  register int byte;

  if ((byte = Bit_File->Read_Byte ()) != EOF)
  {
    return (byte);
  }
  else
  {
//    Error ("Error reading data from GIF image.");
  }

  /* Keep the compiler happy. */

  return(0);
}



/*****************************************************************************
*
* FUNCTION
*
*   Read_Gif_Image
*
* INPUT
*   
* OUTPUT
*   
* RETURNS
*   
* AUTHOR
*
*   POV-Ray Team
*   
* DESCRIPTION
*
*   Main GIF file decoder.
*
* CHANGES
*
*   -
*
******************************************************************************/

int CReadGif::Read_Gif_Image(IMAGE *Image, char *filename, char * err_msg)
{
	register int i, j, status;
	unsigned finished, planes;
	unsigned char buffer[16];

	status = 0;
	j = 0;

	Current_Image = Image;

	if ((Bit_File = Locate_File(filename, POV_File_Image_GIF,NULL,true)) == NULL)
	{
		strcpy(err_msg, "Error opening GIF image.");
		return -1;
	}

	/* Get the screen description. */

	for (i = 0; i < 13; i++)
	{
		buffer[i] = (unsigned char)gif_get_byte();
	}

	/* Use updated GIF specs. */

	if (strncmp((char *) buffer,"GIF",3) == 0)  /* Allow only GIF87 and GIF89 */
	{
		if ((buffer[3] != '8') || ((buffer[4] != '7') && (buffer[4]) != '9') ||
			(buffer[5] < 'A') || (buffer[5]) > 'z')
		{
			sprintf(err_msg, "Unsupported GIF version %c%c%c.", buffer[3], buffer[4], buffer[5]);
			return -1;
		}
	}
	else
	{
		strcpy(err_msg, "File is not in GIF format.");
		return -1;
	}

	planes = ((unsigned)buffer[10] & 0x0F) + 1;

	colourmap_size = (int)(1 << planes);

	//  gif_colour_map = (IMAGE_COLOUR *)POV_CALLOC((size_t)colourmap_size, sizeof(IMAGE_COLOUR), "GIF color map");
	gif_colour_map = (IMAGE_COLOUR *) calloc((size_t)colourmap_size, sizeof(IMAGE_COLOUR));

	// Color map (better be!) 

	if ((buffer[10] & 0x80) == 0)
	{
		strcpy(err_msg, "Error in GIF color map.");
		return -1;
	}

	for (i = 0; i < colourmap_size ; i++)
	{
		gif_colour_map[i].Red    = (unsigned char)gif_get_byte();
		gif_colour_map[i].Green  = (unsigned char)gif_get_byte();
		gif_colour_map[i].Blue   = (unsigned char)gif_get_byte();
		gif_colour_map[i].Filter = 0;
		gif_colour_map[i].Transmit = 0;
	}

	// Now display one or more GIF objects. 

	finished = false;

	while (!finished)
	{
		switch (gif_get_byte())
		{
			// End of the GIF dataset. 
			case ';':
				finished = true;
				status = 0;
				break;

			// GIF Extension Block. 
			case '!':

				// Read (and ignore) the ID. 
				gif_get_byte();

				// Get data len. 
				while ((i = gif_get_byte()) > 0)
				{
					for (j = 0; j < i; j++)
					{
						// Flush data. 
						gif_get_byte();
					}
				}

				break;

			// Start of image object. Get description. 
			case ',':

				for (i = 0; i < 9; i++)
				{
					// EOF test (?).
					if ((j = gif_get_byte()) < 0)
					{
						status = -1;
						break;
					}
					buffer[i] = (unsigned char) j;
				}

				// Check "interlaced" bit. 

				if (j & 0x40)
				{
					Image->Interlaced = 1;
//					Error ("Interlacing in GIF image unsupported.");
				}
				else
					Image->Interlaced = 0;

				if (status < 0)
				{
					finished = true;
					break;
				}

				Image->iwidth  = buffer[4] | (buffer[5] << 8);
				Image->iheight = buffer[6] | (buffer[7] << 8);

				Image->width = (float) Image->iwidth;
				Image->height = (float) Image->iheight;

				Bitmap_Line = 0;

				Image->Colour_Map_Size = (short) colourmap_size;
				Image->Colour_Map = gif_colour_map;

				Image->data.map_lines = (unsigned char **) malloc(Image->iheight * sizeof(unsigned char *));

				for (i = 0 ; i < Image->iheight ; i++)
				{
					Image->data.map_lines[i] = (unsigned char *) calloc((size_t)Image->iwidth, sizeof(unsigned char));
				}

				/* Setup the color palette for the image. */
				decoderline = (unsigned char *) calloc(Image->iwidth, sizeof(unsigned char));

				/* Put bytes in Buf. */
				status = decoder(Image->iwidth);
				free (decoderline);
				decoderline = NULL;
				finished = true;
				break;

			default:
				status = -1;
				finished = true;
				break;
		}
	}

	if (Bit_File != NULL)
	{
		delete Bit_File;
		Bit_File = NULL;
	}

	return 0;
}
// end of Read_Gif_Image


// ****************************************************************
// ****************************************************************

void CReadGif::Destroy_Image(IMAGE *Image)
{
  int i;

  if ((Image == NULL) || (--(Image->References) > 0))
  {
    return;
  }

  if (Image->Colour_Map != NULL)
  {
	free(Image->Colour_Map);

    Image->Colour_Map = NULL;

    if (Image->data.map_lines != NULL)
    {
      for (i = 0; i < Image->iheight; i++)
      {
        free(Image->data.map_lines[i]);
      }

      free(Image->data.map_lines);

      Image->data.map_lines = NULL;
    }
  }
  else
  {
    if ((Image->Image_Type & IS16BITIMAGE) == IS16BITIMAGE)
    {
        if ((Image->Image_Type & IS16GRAYIMAGE) == IS16GRAYIMAGE)
	    {
	      if (Image->data.gray16_lines != NULL)
	      {
	        for (i = 0; i < Image->iheight; i++)
	        {
	          free(Image->data.gray16_lines[i]);
	        }

	        free(Image->data.gray16_lines);

	        Image->data.gray16_lines = NULL;
	      }
	    }
	    else if (Image->data.rgb16_lines != NULL)
	    {
	      for (i = 0; i < Image->iheight; i++)
	      {
	        free(Image->data.rgb16_lines[i].red);
	        free(Image->data.rgb16_lines[i].green);
	        free(Image->data.rgb16_lines[i].blue);

	        if (Image->data.rgb16_lines[i].transm != NULL)
	        {
	          free(Image->data.rgb16_lines[i].transm);
	        }
	      }

	      free(Image->data.rgb16_lines);

	      Image->data.rgb16_lines = NULL;
	    }
    }
    else
    {
	    if (Image->data.rgb8_lines != NULL)
	    {
	      for (i = 0; i < Image->iheight; i++)
	      {
	        free(Image->data.rgb8_lines[i].red);
	        free(Image->data.rgb8_lines[i].green);
	        free(Image->data.rgb8_lines[i].blue);

	        if (Image->data.rgb8_lines[i].transm != NULL)
	        {
	          free(Image->data.rgb8_lines[i].transm);
	        }
	      }

	      free(Image->data.rgb8_lines);

	      Image->data.rgb8_lines = NULL;
	    }
	}
  }

  delete Image;
  Image = NULL;
}
// end of Destroy_Image

// ****************************************************************
// ****************************************************************

int CReadGif::Load_Gif_Image(IMAGE *Image, char *filename, char * err_msg)
{
	register int i, j, status;
	unsigned finished, planes;
	unsigned char buffer[16];

	status = 0;
	j = 0;

	Current_Image = Image;

	if ((Bit_File = Locate_File(filename, POV_File_Image_GIF,NULL,true)) == NULL)
	{
		strcpy(err_msg, "Error opening GIF image.");
		return -1;
	}

	/* Get the screen description. */

	for (i = 0; i < 13; i++)
	{
		buffer[i] = (unsigned char)gif_get_byte();
	}

	/* Use updated GIF specs. */

	if (strncmp((char *) buffer,"GIF",3) == 0)  /* Allow only GIF87 and GIF89 */
	{
		if ((buffer[3] != '8') || ((buffer[4] != '7') && (buffer[4]) != '9') ||
			(buffer[5] < 'A') || (buffer[5]) > 'z')
		{
			sprintf(err_msg, "Unsupported GIF version %c%c%c.", buffer[3], buffer[4], buffer[5]);
			return -1;
		}
		if (buffer[4] == '9')
			Image->IsGif89 = 1;
		else
			Image->IsGif89 = 0;
	}
	else
	{
		strcpy(err_msg, "File is not in GIF format.");
		return -1;
	}

	planes = ((unsigned)buffer[10] & 0x0F) + 1;

	colourmap_size = (int)(1 << planes);

	//  gif_colour_map = (IMAGE_COLOUR *)POV_CALLOC((size_t)colourmap_size, sizeof(IMAGE_COLOUR), "GIF color map");
	gif_colour_map = (IMAGE_COLOUR *) calloc((size_t)colourmap_size, sizeof(IMAGE_COLOUR));

	// Color map (better be!) 

	if ((buffer[10] & 0x80) == 0)
	{
		strcpy(err_msg, "Error in GIF color map.");
		return -1;
	}

	for (i = 0; i < colourmap_size ; i++)
	{
		gif_colour_map[i].Red    = (unsigned char)gif_get_byte();
		gif_colour_map[i].Green  = (unsigned char)gif_get_byte();
		gif_colour_map[i].Blue   = (unsigned char)gif_get_byte();
		gif_colour_map[i].Filter = 0;
		gif_colour_map[i].Transmit = 0;
	}

	// Now display one or more GIF objects. 

	finished = false;

	while (!finished)
	{
		switch (gif_get_byte())
		{
			// End of the GIF dataset. 
			case ';':
				finished = true;
				status = 0;
				break;

			// GIF Extension Block. 
			case '!':

				// Read (and ignore) the ID. 
				gif_get_byte();

				// Get data len. 
				while ((i = gif_get_byte()) > 0)
				{
					for (j = 0; j < i; j++)
					{
						// Flush data. 
						gif_get_byte();
					}
				}

				break;

			// Start of image object. Get description. 
			case ',':

				for (i = 0; i < 9; i++)
				{
					// EOF test (?).
					if ((j = gif_get_byte()) < 0)
					{
						status = -1;
						break;
					}
					buffer[i] = (unsigned char) j;
				}

				// Check "interlaced" bit. 

				if (j & 0x40)
				{
					Image->Interlaced = 1;
//					Error ("Interlacing in GIF image unsupported.");
				}
				else
					Image->Interlaced = 0;

				if (status < 0)
				{
					finished = true;
					break;
				}

				Image->iwidth  = buffer[4] | (buffer[5] << 8);
				Image->iheight = buffer[6] | (buffer[7] << 8);

				Image->width = (float) Image->iwidth;
				Image->height = (float) Image->iheight;

				Bitmap_Line = 0;

				Image->Colour_Map_Size = (short) colourmap_size;

				free(gif_colour_map);
/*
				Image->Colour_Map = gif_colour_map;

				Image->data.map_lines = (unsigned char **) malloc(Image->iheight * sizeof(unsigned char *));

				for (i = 0 ; i < Image->iheight ; i++)
				{
					Image->data.map_lines[i] = (unsigned char *) calloc((size_t)Image->iwidth, sizeof(unsigned char));
				}

				// Setup the color palette for the image. 
				decoderline = (unsigned char *) calloc(Image->iwidth, sizeof(unsigned char));

				// Put bytes in Buf. 
				status = decoder(Image->iwidth);
				free (decoderline);
*/
				decoderline = NULL;
				finished = true;
				break;

			default:
				status = -1;
				finished = true;
				break;
		}
	}

	if (Bit_File != NULL)
	{
		delete Bit_File;
		Bit_File = NULL;
	}

	return 0;
}
// enb of Load_Gif_Image

// ****************************************************************
// ****************************************************************

