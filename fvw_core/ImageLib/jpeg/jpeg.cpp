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

// Jpeg.cpp

#include "..//stdafx.h"
#include "jpeg.h"
#include "common.h"
//#include "jpeglib.h"
#include "cdjpeg.h"
#ifdef _WIN32
#include <io.h>
#endif
#include <stdio.h>

const int jpeg_natural_order[DCTSIZE2+16] = 
{
    0,  1,  8, 16,  9,  2,  3, 10,
   17, 24, 32, 25, 18, 11,  4,  5,
   12, 19, 26, 33, 40, 48, 41, 34,
   27, 20, 13,  6,  7, 14, 21, 28,
   35, 42, 49, 56, 57, 50, 43, 36,
   29, 22, 15, 23, 30, 37, 44, 51,
   58, 59, 52, 45, 38, 31, 39, 46,
   53, 60, 61, 54, 47, 55, 62, 63,
   63, 63, 63, 63, 63, 63, 63, 63, /* extra entries for safety in decoder */
   63, 63, 63, 63, 63, 63, 63, 63
};

// default quant tables
const int jpeg_default_quant_table_q1[64] =
{
    8, 72, 72, 72, 72, 72, 72, 72,
   72, 72, 78, 74, 76, 74, 78, 89,
   81, 84, 84, 81, 89, 106, 93, 94,
   99, 94, 93, 106, 129, 111, 108, 116,
   116, 108, 111, 129, 135, 128, 136, 145,
   136, 128, 135, 155, 160, 177, 177, 160,
   155, 193, 213, 228, 213, 193, 255, 255,
   255, 255, 255, 255, 255, 255, 255, 255
};

const int jpeg_default_quant_table_q2[64] =
{
    8, 36, 36, 36, 36, 36, 36, 36,
   36, 36, 39, 37, 38, 37, 39, 45,
   41, 42, 42, 41, 45, 53, 47, 47,
   50, 47, 47, 53, 65, 56, 54, 59,
   59, 54, 56, 65, 68, 64, 69, 73,
   69, 64, 68, 78, 81, 89, 89, 81,
   78, 98, 108, 115, 108, 98, 130, 144,
   144, 130, 178, 190, 178, 243, 243, 255
};

const int jpeg_default_quant_table_q3[64] =
{
    8, 10, 10, 10, 10, 10, 10, 10,
   10, 10, 11, 10, 11, 10, 11, 13,
   11, 12, 12, 11, 13, 15, 13, 13,
   14, 13, 13, 15, 18, 16, 15, 16,
   16, 15, 16, 18, 19, 18, 19, 21,
   19, 18, 19, 22, 23, 25, 25, 23,
   22, 27, 30, 32, 30, 27, 36, 40,
   40, 36, 50, 53, 50, 68, 68, 91
};

const int jpeg_default_quant_table_q4[64] =
{
    8, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 8, 7, 8, 7, 8, 9,
   8, 8, 8, 8, 9, 11, 9, 9,
   10, 9, 9, 11, 13, 11, 11, 12,
   12, 11, 11, 13, 14, 13, 14, 15,
   14, 13, 14, 16, 16, 18, 18, 16,
   16, 20, 22, 23, 22, 20, 29, 29,
   29, 26, 36, 38, 36, 49, 49, 65
};

const int jpeg_default_quant_table_q5[64] =
{
    4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 5,
   5, 5, 5, 5, 5, 6, 5, 5,
   6, 5, 5, 6, 7, 6, 6, 6,
   6, 6, 6, 7, 8, 7, 8, 8,
   8, 7, 8, 9, 9, 10, 10, 9,
   9, 11, 12, 13, 12, 11, 14, 16,
   16, 14, 20, 21, 20, 27, 27, 36
};


const UINT8 jpeg_default_huffbits_dc[17] = 
{
   0, 0, 1, 5, 1, 1, 1, 1, 1,
   1, 0, 0, 0, 0, 0, 0, 0
   
};

const UINT8 jpeg_default_huffbits_ac[17] = 
{
   0, 0, 2, 1, 3, 3, 2, 4, 3,
   5, 5, 4, 4, 0, 0, 1, 125
};

const UINT8 jpeg_default_huffval_dc[12] = 
{
   0, 1, 2, 3, 4, 5, 6, 7,
   8, 9, 10, 11
};

const UINT8 jpeg_default_huffval_ac[162] = 
{
   0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
   0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
   0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
   0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
   0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
   0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
   0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
   0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
   0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
   0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
   0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
   0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
   0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
   0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
   0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
   0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
   0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
   0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
   0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
   0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
   0xf9, 0xfa
};


//BYTE CJpeg::m_jpeg_table[1000];
//int CJpeg::m_jpeg_table_len = 0;


CJpeg::CJpeg()
{
   m_clear_r = 0;
   m_clear_g = 0;
   m_clear_b = 0;
   m_grayscale = FALSE;
   m_jpeg_table_len = 0;
   m_width = 0;
   m_height = 0;
   m_num_components = 0;
   m_jpeg_color_space = 0;
   m_out_color_space = 0;
   m_crypt_pos = 0;
   m_encrypt = FALSE;
}

// ***************************************************************
// ***************************************************************

CJpeg::~CJpeg()
{
}

// ***************************************************************
// ***************************************************************

BOOL CJpeg::set_grayscale(struct jpeg_compress_struct * cinfo, BOOL gray)
{
   BOOL cur = m_grayscale;

   m_grayscale = gray;
   if (m_grayscale)
   {
      cinfo->input_components = 1;
      cinfo->num_components = 1;

      cinfo->in_color_space = JCS_GRAYSCALE; 
      jpeg_set_colorspace(cinfo, JCS_GRAYSCALE);
   }
   else
   {
      cinfo->in_color_space = JCS_RGB; 
      jpeg_set_colorspace(cinfo, JCS_YCbCr);
   }

   return cur;
}

// ***************************************************************
// ***************************************************************

BOOL CJpeg::is_grayscale()
{
   return m_grayscale;
}

// ***************************************************************
// ***************************************************************

int CJpeg::set_clear_color(int r, int g, int b)
{
   if ((r < 0) || (r > 255))
      return FAILURE;
   if ((g < 0) || (g > 255))
      return FAILURE;
   if ((b < 0) || (b > 255))
      return FAILURE;

   m_clear_r = r;
   m_clear_g = g;
   m_clear_b = b;

   return SUCCESS;
}


// ***************************************************************
// ***************************************************************

int CJpeg::open(CString filename, CString & error_msg)
{

   return SUCCESS;
}


/*
 * Include file for users of JPEG library.
 * You will need to have included system headers that define at least
 * the typedefs FILE and size_t before you can include jpeglib.h.
 * (stdio.h is sufficient on ANSI-conforming systems.)
 * You may also wish to include "jerror.h".
 */


/*
 * <setjmp.h> is used for the optional error recovery mechanism shown in
 * the second part of the example.
 */

#include <setjmp.h>



/******************** JPEG COMPRESSION SAMPLE INTERFACE *******************/

/* This half of the example shows how to feed data into the JPEG compressor.
 * We present a minimal version that does not worry about refinements such
 * as error recovery (the JPEG code will just exit() if it gets an error).
 */


/*
 * IMAGE DATA FORMATS:
 *
 * The standard input image format is a rectangular array of pixels, with
 * each pixel having the same number of "component" values (color channels).
 * Each pixel row is an array of JSAMPLEs (which typically are unsigned chars).
 * If you are working with color data, then the color values for each pixel
 * must be adjacent in the row; for example, R,G,B,R,G,B,R,G,B,... for 24-bit
 * RGB color.
 *
 * For this example, we'll assume that this data structure matches the way
 * our application has stored the image in memory, so we can just pass a
 * pointer to our image buffer.  In particular, let's say that the image is
 * RGB color and is described by:
 */

extern JSAMPLE * image_buffer;   /* Points to large array of R,G,B-order data */
extern int image_height;   /* Number of rows in image */
extern int image_width;    /* Number of columns in image */


/*
 * Sample routine for JPEG compression.  We assume that the target file name
 * and a compression quality factor are passed in.
 */


/******************** JPEG DECOMPRESSION SAMPLE INTERFACE *******************/

/* This half of the example shows how to read data from the JPEG decompressor.
 * It's a bit more refined than the above, in that we show:
 *   (a) how to modify the JPEG library's standard error-reporting behavior;
 *   (b) how to allocate workspace using the library's memory manager.
 *
 * Just to make this example a little different from the first one, we'll
 * assume that we do not intend to put the whole image into an in-memory
 * buffer, but to send it line-by-line someplace else.  We need a one-
 * scanline-high JSAMPLE array as a work buffer, and we will let the JPEG
 * memory manager allocate it for us.  This approach is actually quite useful
 * because we don't need to remember to deallocate the buffer separately: it
 * will go away automatically when the JPEG object is cleaned up.
 */


/*
 * ERROR HANDLING:
 *
 * The JPEG library's standard error handler (jerror.c) is divided into
 * several "methods" which you can override individually.  This lets you
 * adjust the behavior without duplicating a lot of code, which you might
 * have to update with each future release.
 *
 * Our example here shows how to override the "error_exit" method so that
 * control is returned to the library's caller when a fatal error occurs,
 * rather than calling exit() as the standard error_exit method does.
 *
 * We use C's setjmp/longjmp facility to return control.  This means that the
 * routine which calls the JPEG library must first execute a setjmp() call to
 * establish the return point.  We want the replacement error_exit to do a
 * longjmp().  But we need to make the setjmp buffer accessible to the
 * error_exit routine.  To do this, we make a private extension of the
 * standard JPEG error handler object.  (If we were using C++, we'd say we
 * were making a subclass of the regular error handler.)
 *
 * Here's the extended error handler struct:
 */

struct my_error_mgr 
{
   struct jpeg_error_mgr pub; /* "public" fields */

   jmp_buf setjmp_buffer;  /* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF(void)
my_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message) (cinfo);

  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}


// **********************************************************************
// **********************************************************************

int CJpeg::load( CString filename, int &image_width, int &image_height,
                    CString &error_message )
{
// int rslt;
   struct jpeg_decompress_struct cinfo;
   // We use our private extension JPEG error handler.
   // Note that this struct must live as long as the main JPEG parameter
   // struct, to avoid dangling-pointer problems.
   
   struct my_error_mgr jerr;

   FILE * infile = NULL;      // source file 

   // In this example we want to open the input file before doing anything else,
   // so that the setjmp() error recovery below can assume the file is open.
   // VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   // requires it in order to read binary files.

   // check for TIROS file
   CString ext;
   ext = filename.Right(3);
   if (!ext.CompareNoCase("TIR"))
      use_encryption = 1;
   else
      use_encryption = 0;

   crypt_pos = 0;

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&infile, filename, "rb");
   if (infile == NULL) 
   {
      error_message.Format("Can't open %s", (LPCSTR)filename );
      return FAILURE;
   }

// rslt = check_jpeg_file(infile, error_message);
// if (rslt != SUCCESS)
//    return rslt;

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   // Establish the setjmp return context for my_error_exit to use. 
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.
      
      jpeg_destroy_decompress(&cinfo);
      fclose(infile);
      error_message = "Error opening JPEG file";
      return FAILURE;
   }
   // Now we can initialize the JPEG decompression object. 
   jpeg_create_decompress(&cinfo);

   // Step 2: specify data source (eg, a file) 

   jpeg_stdio_src(&cinfo, infile);

   // Step 3: read file parameters with jpeg_read_header() 

   (void) jpeg_read_header(&cinfo, TRUE);
   // We can ignore the return value from jpeg_read_header since
   //   (a) suspension is not possible with the stdio data source, and
   //  (b) we passed TRUE to reject a tables-only JPEG file as an error.
   // See libjpeg.doc for more info.
   

   // Step 4: set parameters for decompression 

   // In this example, we don't need to change any of the defaults set by
   // jpeg_read_header(), so we do nothing here.
   

   image_width = cinfo.image_width;
   image_height = cinfo.image_height;

   m_filename = filename;

// memcpy((void*) &m_cinfo, (void*) &cinfo, sizeof(cinfo));

   m_width = cinfo.image_width;
   m_height = cinfo.image_height;
   m_num_components = cinfo.num_components;
   m_jpeg_color_space = cinfo.jpeg_color_space;
   m_out_color_space = cinfo.out_color_space;


   // This is an important step since it will release a good deal of memory. 
   jpeg_destroy_decompress(&cinfo);

   // After finish_decompress, we can close the input file.
   // Here we postpone it until after no more JPEG errors are possible,
   // so as to simplify the setjmp error logic above.  (Actually, I don't
   // think that jpeg_destroy can do an error exit, but why assume anything...)
   
   fclose(infile);

   return SUCCESS;
}
// end of load

// **********************************************************************
// **********************************************************************

int CJpeg::load_file( FILE * infile, int &image_width, int &image_height,
                    CString &error_message )
{
   struct jpeg_decompress_struct cinfo;
   // We use our private extension JPEG error handler.
   // Note that this struct must live as long as the main JPEG parameter
   // struct, to avoid dangling-pointer problems.
   
   struct my_error_mgr jerr;

   if (infile == NULL) 
   {
      error_message = "Bad File Pointer";
      return FAILURE;
   }

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   // Establish the setjmp return context for my_error_exit to use. 
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.
      
      jpeg_destroy_decompress(&cinfo);
      fclose(infile);
      error_message = "Error opening JPEG file";
      return FAILURE;
   }
   // Now we can initialize the JPEG decompression object. 
   jpeg_create_decompress(&cinfo);

   // Step 2: specify data source (eg, a file) 

   jpeg_stdio_src(&cinfo, infile);

   // Step 3: read file parameters with jpeg_read_header() 

   (void) jpeg_read_header(&cinfo, TRUE);
   // We can ignore the return value from jpeg_read_header since
   //   (a) suspension is not possible with the stdio data source, and
   //  (b) we passed TRUE to reject a tables-only JPEG file as an error.
   // See libjpeg.doc for more info.
   

   // Step 4: set parameters for decompression 

   // In this example, we don't need to change any of the defaults set by
   // jpeg_read_header(), so we do nothing here.
   

   image_width = cinfo.image_width;
   image_height = cinfo.image_height;

   m_width = cinfo.image_width;
   m_height = cinfo.image_height;
   m_num_components = cinfo.num_components;
   m_jpeg_color_space = cinfo.jpeg_color_space;
   m_out_color_space = cinfo.out_color_space;


   // This is an important step since it will release a good deal of memory. 
   jpeg_destroy_decompress(&cinfo);

   // After finish_decompress, we can close the input file.
   // Here we postpone it until after no more JPEG errors are possible,
   // so as to simplify the setjmp error logic above.  (Actually, I don't
   // think that jpeg_destroy can do an error exit, but why assume anything...)
   
   fclose(infile);

   return SUCCESS;
}
// end of load_file

// **********************************************************************
// **********************************************************************

int CJpeg::get_jpeg_image(
                           int image_offset_x, int image_offset_y, 
                           int width, int height,
                           unsigned char *red_array, 
                           unsigned char *green_array,   
                           unsigned char *blue_array,
                           CString &error_message )
{
   if (m_filename.GetLength() < 5)
   {
      error_message.Format("Invalid Filename -- %s", (LPCSTR)m_filename );
      return FAILURE;
   }

   // This struct contains the JPEG decompression parameters and pointers to
   // working space (which is allocated as needed by the JPEG library).
   struct jpeg_decompress_struct cinfo;

   // We use our private extension JPEG error handler.
   // Note that this struct must live as long as the main JPEG parameter
   // struct, to avoid dangling-pointer problems.

   struct my_error_mgr jerr;
   FILE * infile = NULL;   // source file 
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   int row_stride;         // physical row width in output buffer 
   int x, y, cnt;
   int xpos, ypos;
   int pixsize;
   int rslt;
   const int LEN = 1000;
   char err_msg[LEN];

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&infile, m_filename, "rb");
   if (infile == NULL) 
   {
      error_message.Format("Cannot open file -- %s", (LPCSTR)m_filename );
      return FAILURE;
   }

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   // Establish the setjmp return context for my_error_exit to use. 
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.

      jpeg_destroy_decompress(&cinfo);
      fclose(infile);
      TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
      strcpy_s(err_msg, LEN, (char*) *cinfo.err->output_message);
      error_message = err_msg;
//    return FAILURE;
   return SUCCESS;
   }
  /* Now we can initialize the JPEG decompression object. */
  jpeg_create_decompress(&cinfo);

  /* Step 2: specify data source (eg, a file) */

  jpeg_stdio_src(&cinfo, infile);

  /* Step 3: read file parameters with jpeg_read_header() */

  (void) jpeg_read_header(&cinfo, TRUE);
  /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */

  /* Step 4: set parameters for decompression */

  /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

  /* Step 5: Start decompressor */

  (void) jpeg_start_decompress(&cinfo);
  // We can ignore the return value since suspension is not possible
  // with the stdio data source.
   

  // We may need to do some setup of our own at this point before reading
  // the data.  After jpeg_start_decompress() we have the correct scaled
  // output image dimensions available, as well as the output colormap
  // if we asked for color quantization.
  // In this example, we need to make an output work buffer of the right size.
    
  // JSAMPLEs per row in output buffer 
  row_stride = cinfo.output_width * cinfo.output_components;
  // Make a one-row-high sample array that will go away when done with image 
  buffer = (*cinfo.mem->alloc_sarray)
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

  // Step 6: while (scan lines remain to be read) 
  //           jpeg_read_scanlines(...); 

  // Here we use the library's state variable cinfo.output_scanline as the
  // loop counter, so that we don't have to keep track ourselves.
  
  pixsize = cinfo.num_components;

  // skip over the y offset
  for (y=0; y<image_offset_y; y++)
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);

  cnt = 0;

  for (y=0; y<height; y++)
  {
      ypos = y+image_offset_y;
      if (ypos >= (int) cinfo.output_height)
      {
         // shortcut for out of image range condition
         for (x=0; x < width; x++)
         {
            red_array[cnt] = (BYTE) m_clear_r;
            green_array[cnt] = (BYTE) m_clear_g;
            blue_array[cnt] = (BYTE) m_clear_b;
            cnt++;
         }
         continue;
      }

      ASSERT (ypos < (int) cinfo.output_height);
      cinfo.output_scanline = ypos;
      // jpeg_read_scanlines expects an array of pointers to scanlines.
      // Here the array is only one element long, but you could ask for
      // more than one scanline at a time if that's more convenient.
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);
      if (rslt != 1)
      {
         jpeg_destroy_decompress(&cinfo);
         fclose(infile);
         error_message = "Error reading JPEG scanline";
         return FAILURE;
      }
      inptr = buffer[0];

      inptr += (image_offset_x * pixsize);
      for (x=0; x < width; x++)
      {
         xpos = x+image_offset_x;
         if (xpos < (int) cinfo.output_width)
         {
            if (pixsize == 3)
            {
               red_array[cnt] = (BYTE) *inptr++;
               green_array[cnt] = (BYTE) *inptr++;
               blue_array[cnt] = (BYTE) *inptr++;
            }
            else
            {
               red_array[cnt] = (BYTE) *inptr;
               green_array[cnt] = (BYTE) *inptr;
               blue_array[cnt] = (BYTE) *inptr++;
            }

         }
         else
         {
            red_array[cnt] = (BYTE) m_clear_r;
            green_array[cnt] = (BYTE) m_clear_g;
            blue_array[cnt] = (BYTE) m_clear_b;
         }
         cnt++;
      }
  }

  /* Step 7: Finish decompression */

  (void) jpeg_finish_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* Step 8: Release JPEG decompression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_decompress(&cinfo);

  /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */
  fclose(infile);

  /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   */

  /* And we're done! */
  return SUCCESS;
}

// **********************************************************************
// **********************************************************************

// read a multispectral (4 bands) jpeg image
int CJpeg::get_ms_jpeg_image(CString filename, int image_offset_x, int image_offset_y, 
                        int width, int height,
                        unsigned char *img, 
                        CString &error_message )
{
   if (filename.GetLength() < 5)
   {
      error_message.Format("Invalid Filename -- %s", (LPCSTR)filename );
      return FAILURE;
   }

   // This struct contains the JPEG decompression parameters and pointers to
   // working space (which is allocated as needed by the JPEG library).
   struct jpeg_decompress_struct cinfo;

   // We use our private extension JPEG error handler.
   // Note that this struct must live as long as the main JPEG parameter
   // struct, to avoid dangling-pointer problems.

   struct my_error_mgr jerr;
   FILE * infile = NULL;   // source file 
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   int row_stride;         // physical row width in output buffer 
   int x, y, cnt, k;
   int xpos, ypos;
   int pixsize;
   int rslt;
   const int LEN = 1000;
   char err_msg[LEN];

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&infile, filename, "rb");
   if (infile == NULL) 
   {
      error_message.Format("Cannot open file -- %s", (LPCSTR)filename );
      return FAILURE;
   }

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   // Establish the setjmp return context for my_error_exit to use. 
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.

      jpeg_destroy_decompress(&cinfo);
      fclose(infile);
      TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
      strcpy_s(err_msg, LEN, (char*) *cinfo.err->output_message);
      error_message = err_msg;
//    return FAILURE;
   return SUCCESS;
   }
  /* Now we can initialize the JPEG decompression object. */
  jpeg_create_decompress(&cinfo);

  /* Step 2: specify data source (eg, a file) */

  jpeg_stdio_src(&cinfo, infile);

  /* Step 3: read file parameters with jpeg_read_header() */

  (void) jpeg_read_header(&cinfo, TRUE);
  /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */

  /* Step 4: set parameters for decompression */

  /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

  /* Step 5: Start decompressor */

  (void) jpeg_start_decompress(&cinfo);
  // We can ignore the return value since suspension is not possible
  // with the stdio data source.
   

  // We may need to do some setup of our own at this point before reading
  // the data.  After jpeg_start_decompress() we have the correct scaled
  // output image dimensions available, as well as the output colormap
  // if we asked for color quantization.
  // In this example, we need to make an output work buffer of the right size.
    
  // JSAMPLEs per row in output buffer 
  row_stride = cinfo.output_width * cinfo.output_components;
  // Make a one-row-high sample array that will go away when done with image 
  buffer = (*cinfo.mem->alloc_sarray)
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

  // Step 6: while (scan lines remain to be read) 
  //           jpeg_read_scanlines(...); 

  // Here we use the library's state variable cinfo.output_scanline as the
  // loop counter, so that we don't have to keep track ourselves.
  
  pixsize = cinfo.num_components;

  // skip over the y offset
  for (y=0; y<image_offset_y; y++)
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);

  cnt = 0;

  for (y=0; y<height; y++)
  {
      ypos = y+image_offset_y;
      if (ypos >= (int) cinfo.output_height)
      {
         // shortcut for out of image range condition
         for (x=0; x < width; x++)
         {
            img[cnt] = (BYTE) 0;
            cnt++;
         }
         continue;
      }

      ASSERT (ypos < (int) cinfo.output_height);
      cinfo.output_scanline = ypos;
      // jpeg_read_scanlines expects an array of pointers to scanlines.
      // Here the array is only one element long, but you could ask for
      // more than one scanline at a time if that's more convenient.
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);
      if (rslt != 1)
      {
         jpeg_destroy_decompress(&cinfo);
         fclose(infile);
         error_message = "Error reading JPEG scanline";
         return FAILURE;
      }
      inptr = buffer[0];

      inptr += (image_offset_x * pixsize);
      for (x=0; x < width; x++)
      {
         xpos = x+image_offset_x;
         if (xpos < (int) cinfo.output_width)
         {
            for (k=0; k<4; k++)
            {
               img[cnt] = (BYTE) *inptr++;
               cnt++;
            }
         }
         else
         {
            for (k=0; k<4; k++)
            {
               img[cnt] = (BYTE) 0;
               cnt++;
            }
         }
         cnt++;
      }
  }

  /* Step 7: Finish decompression */

  (void) jpeg_finish_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* Step 8: Release JPEG decompression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_decompress(&cinfo);

  /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */
  fclose(infile);

  /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   */

  /* And we're done! */
  return SUCCESS;
}
// end of get_ms_jpeg_image

// **********************************************************************
// **********************************************************************

// read a multispectral (4 bands) jpeg image
int CJpeg::get_ms_stream_jpeg_image(FILE *infile, int image_offset_x, int image_offset_y, 
                        int width, int height,
                        BYTE *img, 
                        CString &error_message )
{
   // This struct contains the JPEG decompression parameters and pointers to
   // working space (which is allocated as needed by the JPEG library).
   struct jpeg_decompress_struct cinfo;

   // We use our private extension JPEG error handler.
   // Note that this struct must live as long as the main JPEG parameter
   // struct, to avoid dangling-pointer problems.

   struct my_error_mgr jerr;
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   int row_stride;         // physical row width in output buffer 
   int x, y, cnt, k;
   int xpos, ypos;
   int pixsize;
   int rslt;
   const int LEN = 1000;
   char err_msg[LEN];

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   // Establish the setjmp return context for my_error_exit to use. 
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.

      jpeg_destroy_decompress(&cinfo);
      fclose(infile);
      TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
      strcpy_s(err_msg, LEN, (char*) *cinfo.err->output_message);
      error_message = err_msg;
//    return FAILURE;
   return SUCCESS;
   }
  /* Now we can initialize the JPEG decompression object. */
  jpeg_create_decompress(&cinfo);

  /* Step 2: specify data source (eg, a file) */

  jpeg_stdio_src(&cinfo, infile);

  /* Step 3: read file parameters with jpeg_read_header() */

  (void) jpeg_read_header(&cinfo, TRUE);
  /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */

  /* Step 4: set parameters for decompression */

  /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

  /* Step 5: Start decompressor */

  (void) jpeg_start_decompress(&cinfo);
  // We can ignore the return value since suspension is not possible
  // with the stdio data source.
   

  // We may need to do some setup of our own at this point before reading
  // the data.  After jpeg_start_decompress() we have the correct scaled
  // output image dimensions available, as well as the output colormap
  // if we asked for color quantization.
  // In this example, we need to make an output work buffer of the right size.
    
  // JSAMPLEs per row in output buffer 
  row_stride = cinfo.output_width * cinfo.output_components;
  // Make a one-row-high sample array that will go away when done with image 
  buffer = (*cinfo.mem->alloc_sarray)
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

  // Step 6: while (scan lines remain to be read) 
  //           jpeg_read_scanlines(...); 

  // Here we use the library's state variable cinfo.output_scanline as the
  // loop counter, so that we don't have to keep track ourselves.
  
  pixsize = cinfo.num_components;

  // skip over the y offset
  for (y=0; y<image_offset_y; y++)
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);

  cnt = 0;

  try
  {
     for (y=0; y<height; y++)
     {
         ypos = y+image_offset_y;
         if (ypos >= (int) cinfo.output_height)
         {
            // shortcut for out of image range condition
            for (x=0; x < width; x++)
            {
               img[cnt] = (BYTE) 0;
               cnt++;
            }
            continue;
         }

         ASSERT (ypos < (int) cinfo.output_height);
         cinfo.output_scanline = ypos;
         // jpeg_read_scanlines expects an array of pointers to scanlines.
         // Here the array is only one element long, but you could ask for
         // more than one scanline at a time if that's more convenient.
         rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);
         if (rslt != 1)
         {
            jpeg_destroy_decompress(&cinfo);
            fclose(infile);
            error_message = "Error reading JPEG scanline";
            return FAILURE;
         }
         inptr = buffer[0];

         inptr += (image_offset_x * pixsize);
         for (x=0; x < width; x++)
         {
            xpos = x+image_offset_x;
            if (xpos < (int) cinfo.output_width)
            {
               for (k=0; k<4; k++)
               {
                  img[cnt] = (BYTE) *inptr++;
                  cnt++;
               }
            }
            else
            {
               for (k=0; k<4; k++)
               {
                  img[cnt] = (BYTE) 0;
                  cnt++;
               }
            }
         }
     }
  }
  catch(...)
  {
      ASSERT(0);
      error_message = "Exception Error in JPEG decompression";
      return FAILURE;
  }


  /* Step 7: Finish decompression */

  (void) jpeg_finish_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* Step 8: Release JPEG decompression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_decompress(&cinfo);

  /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   */

  /* And we're done! */
  return SUCCESS;
}
// end of get_ms_stream_jpeg_image

// **********************************************************************
// **********************************************************************


// ****************************************************************

void CJpeg::get_default_huff_tables (j_decompress_ptr cinfo)
{
   JHUFF_TBL **htblptr;
   int k;

   if (cinfo->dc_huff_tbl_ptrs[0] == NULL)
      cinfo->dc_huff_tbl_ptrs[0] = jpeg_alloc_huff_table((j_common_ptr) cinfo);
   if (cinfo->ac_huff_tbl_ptrs[0] == NULL)
      cinfo->ac_huff_tbl_ptrs[0] = jpeg_alloc_huff_table((j_common_ptr) cinfo);
   htblptr = &cinfo->dc_huff_tbl_ptrs[0];
   memcpy((*htblptr)->bits, jpeg_default_huffbits_dc, 17);
   for (k=0; k<256; k++)
      (*htblptr)->huffval[k] = (UINT8) k;
// memcpy((*htblptr)->huffval, jpeg_default_huffval_dc, 12);
   htblptr = &cinfo->ac_huff_tbl_ptrs[0];
   memcpy((*htblptr)->bits, jpeg_default_huffbits_ac, 17);
   memcpy((*htblptr)->huffval, jpeg_default_huffval_ac, 162);
}
// end of get_default_huff_tables

// ****************************************************************
// ****************************************************************

BOOL CJpeg::get_tif_dht (j_decompress_ptr cinfo, int & ptr)
/* Process a DHT marker */
{
   INT32 length;
   UINT8 bits[17];
   UINT8 huffval[256];
   int i, index, count;
   JHUFF_TBL **htblptr;
// INPUT_VARS(cinfo);

   length = (int) m_jpeg_table[ptr] * 256;
   length += (int) m_jpeg_table[ptr+1];
   ptr += 2;

// INPUT_2BYTES(cinfo, length, return FALSE);
   length -= 2;

   while (length > 16) 
   {
//    INPUT_BYTE(cinfo, index, return FALSE);
      index = m_jpeg_table[ptr];
      ptr++;

//    TRACEMS1(cinfo, 1, JTRC_DHT, index);

      bits[0] = 0;
      count = 0;
      for (i = 1; i <= 16; i++) 
      {
//       INPUT_BYTE(cinfo, bits[i], return FALSE);
         bits[i] = m_jpeg_table[ptr];
         ptr++;
         count += bits[i];
      }

      length -= 1 + 16;

      TRACEMS8(cinfo, 2, JTRC_HUFFBITS,
      bits[1], bits[2], bits[3], bits[4],
      bits[5], bits[6], bits[7], bits[8]);
      TRACEMS8(cinfo, 2, JTRC_HUFFBITS,
      bits[9], bits[10], bits[11], bits[12],
      bits[13], bits[14], bits[15], bits[16]);

      /* Here we just do minimal validation of the counts to avoid walking
      * off the end of our table space.  jdhuff.c will check more carefully.
      */
      if (count > 256 || ((INT32) count) > length)
         ERREXIT(cinfo, JERR_BAD_HUFF_TABLE);

      for (i = 0; i < count; i++)
      {
//       INPUT_BYTE(cinfo, huffval[i], return FALSE);
         huffval[i] = m_jpeg_table[ptr];
         ptr++;
      }

      length -= count;

      if (index & 0x10) 
      {     /* AC table definition */
         index -= 0x10;
         htblptr = &cinfo->ac_huff_tbl_ptrs[index];
      } 
      else 
      {        /* DC table definition */
         htblptr = &cinfo->dc_huff_tbl_ptrs[index];
      }

      if (index < 0 || index >= NUM_HUFF_TBLS)
         ERREXIT1(cinfo, JERR_DHT_INDEX, index);

      if (*htblptr == NULL)
         *htblptr = jpeg_alloc_huff_table((j_common_ptr) cinfo);

      MEMCOPY((*htblptr)->bits, bits, SIZEOF((*htblptr)->bits));
      MEMCOPY((*htblptr)->huffval, huffval, SIZEOF((*htblptr)->huffval));
   }

   if (length != 0)
      ERREXIT(cinfo, JERR_BAD_LENGTH);

// INPUT_SYNC(cinfo);

   return TRUE;
}
// end of get_dht

// ****************************************************************
// ****************************************************************

BOOL CJpeg::get_tif_dqt (j_decompress_ptr cinfo, int & ptr)
/* Process a DQT marker */
{
   INT32 length;
   int n, i, prec;
   unsigned int tmp;
   JQUANT_TBL *quant_ptr;
   BOOL natural = TRUE;

// INPUT_VARS(cinfo);


// INPUT_2BYTES(cinfo, length, return FALSE);

   length = (int) m_jpeg_table[ptr] * 256;
   length += (int) m_jpeg_table[ptr+1];
   ptr += 2;


   length -= 2;

   while (length > 0) 
   {
//    INPUT_BYTE(cinfo, n, return FALSE);
      n = m_jpeg_table[ptr];
      ptr++;

      if (n == 255)
      {
         // flag for no natural
         natural = FALSE;
         n = 1;
      }

      prec = n >> 4;
      n &= 0x0F;

      TRACEMS2(cinfo, 1, JTRC_DQT, n, prec);

      if (n >= NUM_QUANT_TBLS)
         ERREXIT1(cinfo, JERR_DQT_INDEX, n);

      if (cinfo->quant_tbl_ptrs[n] == NULL)
         cinfo->quant_tbl_ptrs[n] = jpeg_alloc_quant_table((j_common_ptr) cinfo);

      quant_ptr = cinfo->quant_tbl_ptrs[n];

      for (i = 0; i < DCTSIZE2; i++) 
      {
         if (prec)
         {
//          INPUT_2BYTES(cinfo, tmp, return FALSE);
            tmp = (int) m_jpeg_table[ptr] * 256;
            tmp += (int) m_jpeg_table[ptr+1];
            ptr += 2;
         }
         else
         {
//          INPUT_BYTE(cinfo, tmp, return FALSE);
            tmp = m_jpeg_table[ptr];
            ptr++;
         }

         /* We convert the zigzag-order table to natural array order. */
         if (natural)
            quant_ptr->quantval[jpeg_natural_order[i]] = (UINT16) tmp;
         else
            quant_ptr->quantval[i] = (UINT16) tmp;
      }

      if (cinfo->err->trace_level >= 2) 
      {
         for (i = 0; i < DCTSIZE2; i += 8) 
         {
            TRACEMS8(cinfo, 2, JTRC_QUANTVALS,
            quant_ptr->quantval[i],   quant_ptr->quantval[i+1],
            quant_ptr->quantval[i+2], quant_ptr->quantval[i+3],
            quant_ptr->quantval[i+4], quant_ptr->quantval[i+5],
            quant_ptr->quantval[i+6], quant_ptr->quantval[i+7]);
         }
      }

      length -= DCTSIZE2+1;
      if (prec) 
         length -= DCTSIZE2;
   }

   if (length != 0)
      ERREXIT(cinfo, JERR_BAD_LENGTH);

// INPUT_SYNC(cinfo);
   return TRUE;
}
// end of get_dqt

// ****************************************************************
// ****************************************************************

void CJpeg::set_jpeg_tables(BYTE *tables, int count)
{
   m_apbJpegTable.reset( new BYTE[ m_jpeg_table_len = count ] );
   m_jpeg_table = m_apbJpegTable.get();
   memcpy( m_jpeg_table, tables, count * sizeof(*m_jpeg_table) );
}

// ****************************************************************
// ****************************************************************

// Returns the specified subimage in rgb format at the specified level.
// Fill areas with no image data with black
// Return value is SUCCESS or FAILURE.

int CJpeg::get_filled_rgb_subimage( double factor, const int center_x, const int center_y, 
                           const int scr_width, const int scr_height, 
                           BYTE *red_buffer, BYTE *grn_buffer, BYTE *blu_buffer,
                           CString &error_message )
{
   int width, height, k, ndx, size, isize, rslt, x, y, offx, offy;
   int startx, endx, starty, endy, iwidth, iheight, outpos;
   int fwidth, fheight, iendx, iendy;
   int ulx, uly, lrx, lry;
   BYTE *red_buf, *grn_buf, *blu_buf;

   red_buf = grn_buf = blu_buf = NULL;

   size = scr_width * scr_height;
   // fill image with black
   for (k=0; k<size; k++)
   {
      red_buffer[k] = 0;
      grn_buffer[k] = 0;
      blu_buffer[k] = 0;
   }

   ulx = center_x - (scr_width/2);
   uly = center_y - (scr_height/2);

   iwidth = (int) ((double) scr_width / factor);
   iheight = (int) ((double) scr_height / factor);

   lrx = ulx + iwidth - 1;
   lry = uly + iheight - 1;

   startx = center_x - (iwidth / 2);
   starty = center_y - (iheight / 2);

   offx = 0;
   offy = 0;

   if (startx < 0)
   {
      offx = - (int) ((double) startx * factor);
      startx = 0;
   }
   if (starty < 0)
   {
      offy = - (int) ((double) starty * factor);
      starty = 0;
   }

   endx = startx + scr_width - 1;
   endy = starty + scr_height - 1;
   iendx = startx + iwidth - 1;
   iendy = starty + iheight - 1;

   width = scr_width;
   height = scr_height;

   // early exit if no image on buffer
   if ((startx > m_width) || (iendx < 0))
      return SUCCESS;
   if ((starty > m_height) || (iendy < 0))
      return SUCCESS;

   fwidth = (int) ((double) m_width / factor);
   fheight = (int) ((double) m_height / factor);

   // limit the range the that of the image buffer
   if (endx > lrx)
      endx = lrx;
   if (endy > lry)
      endy = lry;
   
   if (endx >= m_width )
      endx = m_width-1;
   if (endy >= m_height)
      endy = m_height-1;

   ASSERT(startx <= iendx);
   ASSERT(starty <= iendy);

   iwidth = endx - startx + 1;
   iheight = endy - starty + 1;

// if (iendx > m_width)
//    iwidth -= (int) ((double)(iendx - m_width) * factor);
// if (iendy > m_height)
//    iheight -= (int) ((double)(iendy - m_height) * factor);
   isize = iwidth * iheight;

   // get the image data
   red_buf = (BYTE*) malloc(isize);
   grn_buf = (BYTE*) malloc(isize);
   blu_buf = (BYTE*) malloc(isize);
   if ((red_buf == NULL) || (grn_buf == NULL) || (blu_buf == NULL))
   {
      ASSERT(0);
      if (red_buf)
         free(red_buf);
      if (grn_buf)
         free(grn_buf);
      if (blu_buf)
         free(blu_buf);
      error_message = "Memory Allocation Error";
      return FAILURE;
   }

   if (factor == 1.0)
      rslt = get_jpeg_image(startx, starty, iwidth, iheight, red_buf, grn_buf, blu_buf, error_message);
   else if (factor > 1.0)
   {
      rslt = get_supersampled_rgb_image(factor, startx, starty, iwidth, iheight, red_buf, grn_buf, blu_buf, error_message);
   }
   else
   {
      int samp = (int) (1.0 / factor);
      rslt = get_subsampled_rgb_image(samp, startx, starty, iwidth, iheight, red_buf, grn_buf, blu_buf, error_message);

   }

   if (rslt != SUCCESS)
   {
      free(red_buf);
      free(grn_buf);
      free(blu_buf);
      return rslt;
   }

   // move the image data to the image buffer
   offx = startx - ulx;
   offy = starty - uly;
// offx /= 2;
// offy /= 2;
   ndx = 0;
   for (y = 0; y < iheight; y++)
   {
      if ((offy+y) >= height)
         continue;
      for (x = 0; x < iwidth; x++)
      {
         if ((offx+x) >= width)
            continue;
         outpos = ((offy + y) * width) + (offx + x);
         ASSERT(outpos < size);
         ASSERT(ndx < isize);
         red_buffer[outpos] = red_buf[ndx];
         grn_buffer[outpos] = grn_buf[ndx];
         blu_buffer[outpos] = blu_buf[ndx];
         ndx++;
      }
   }

   free(red_buf);
   free(grn_buf);
   free(blu_buf);

   return rslt;
}
// end of get_filled_rgb_subimage

// ****************************************************************
// ****************************************************************

int CJpeg::get_histogram(FILE *infile, unsigned int *hist, unsigned int *freq_red, 
                           unsigned int *freq_grn, unsigned int *freq_blu, CString &error_message )
{
   // This struct contains the JPEG decompression parameters and pointers to
   // working space (which is allocated as needed by the JPEG library).
   struct jpeg_decompress_struct cinfo;

   // We use our private extension JPEG error handler.
   // Note that this struct must live as long as the main JPEG parameter
   // struct, to avoid dangling-pointer problems.

   struct my_error_mgr jerr;
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   int row_stride;         // physical row width in output buffer 
   int x, cnt;
   int ypos;
   int pixsize;
   int rslt;
   const int LEN = 1000;
   char err_msg[LEN];
   int red, grn, blu, pos;


   if (infile == NULL) 
   {
      error_message = "Bad File Pointer";
      return FAILURE;
   }

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;

   // Establish the setjmp return context for my_error_exit to use. 
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.

      jpeg_destroy_decompress(&cinfo);
//    fclose(infile);
      strcpy_s(err_msg, LEN, (char*) *cinfo.err->output_message);
//    error_message = err_msg;
      error_message = "JPEG decode error";
      return FAILURE;
//    return SUCCESS;
   }

   /* Now we can initialize the JPEG decompression object. */
   jpeg_create_decompress(&cinfo);

   /* Step 2: specify data source (eg, a file) */


   jpeg_stdio_src(&cinfo, infile);

   /* Step 3: read file parameters with jpeg_read_header() */

   (void) jpeg_read_header(&cinfo, TRUE);

   // get data from JPEGTables if there is any
   if (m_jpeg_table_len > 0)
   {
      int ptr, k;

//    ptr = 4;
//    get_tif_dqt(&cinfo, ptr);
//    ptr += 2;
      // find huffman table
      k = 0;
      while (k<m_jpeg_table_len-1)
      {
         if ((m_jpeg_table[k] == 255) && (m_jpeg_table[k+1] == 219))
         {
            ptr = k + 2;
            get_tif_dqt(&cinfo, ptr);
         }
         if ((m_jpeg_table[k] == 255) && (m_jpeg_table[k+1] == 196))
         {
            ptr = k + 2;
            get_tif_dht(&cinfo, ptr);
         }
         k++;
      }
   }

   /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */

   /* Step 4: set parameters for decompression */

   /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

   /* Step 5: Start decompressor */

   (void) jpeg_start_decompress(&cinfo);

   // We can ignore the return value since suspension is not possible
   // with the stdio data source.


   // We may need to do some setup of our own at this point before reading
   // the data.  After jpeg_start_decompress() we have the correct scaled
   // output image dimensions available, as well as the output colormap
   // if we asked for color quantization.
   // In this example, we need to make an output work buffer of the right size.

   // JSAMPLEs per row in output buffer 
   row_stride = cinfo.output_width * cinfo.output_components;

   // Make a one-row-high sample array that will go away when done with image 
   buffer = (*cinfo.mem->alloc_sarray) ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

   // Step 6: while (scan lines remain to be read) 
   //           jpeg_read_scanlines(...); 

   // Here we use the library's state variable cinfo.output_scanline as the
   // loop counter, so that we don't have to keep track ourselves.

   pixsize = cinfo.num_components;

   cnt = 0;

   for (ypos=0; ypos<(int) cinfo.output_height; ypos++)
   {
      cinfo.output_scanline = ypos;

      // jpeg_read_scanlines expects an array of pointers to scanlines.
      // Here the array is only one element long, but you could ask for
      // more than one scanline at a time if that's more convenient.
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);
      if (rslt != 1)
      {
         jpeg_destroy_decompress(&cinfo);
         fclose(infile);
         error_message = "Error reading JPEG scanline";
         return FAILURE;
      }
      inptr = buffer[0];

      for (x=0; x < (int) cinfo.output_width; x++)
      {
         // clip to 5 bits
         red = *inptr++;
         grn = *inptr++;
         blu = *inptr++;
         if ((red == 0) && (grn == 0) && (blu == 0))
            continue;
         freq_red[red]++;
         freq_grn[grn]++;
         freq_blu[blu]++;
         red = (int) (red >> 3);
         grn = (int) (grn >> 3);
         blu = (int) (blu >> 3);
         pos = (red << 10) + (grn << 5) + blu;
         hist[pos] += 1;
      }
   }

   /* Step 7: Finish decompression */

   (void) jpeg_finish_decompress(&cinfo);
   /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

   /* Step 8: Release JPEG decompression object */

   /* This is an important step since it will release a good deal of memory. */
   jpeg_destroy_decompress(&cinfo);

   /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */
// fclose(infile);

   /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   */

   /* And we're done! */
   return SUCCESS;
}
// end of get_histogram

// **********************************************************************
// ****************************************************************

int CJpeg::get_jpeg_bitstream_image(FILE *infile, 
                           int image_offset_x, int image_offset_y, 
                           int & width, int & height,
                           BOOL rgb_colorspace,
                           unsigned char *red_array, 
                           unsigned char *green_array,   
                           unsigned char *blue_array,
                           CString &error_message, int default_comrat )
{
   // This struct contains the JPEG decompression parameters and pointers to
   // working space (which is allocated as needed by the JPEG library).
   struct jpeg_decompress_struct cinfo;

   // We use our private extension JPEG error handler.
   // Note that this struct must live as long as the main JPEG parameter
   // struct, to avoid dangling-pointer problems.

   struct my_error_mgr jerr;
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   int row_stride;         // physical row width in output buffer 
   int x, y, cnt;
   int xpos, ypos;
   int pixsize;
   int rslt, iwidth;
   const int LEN = 1000;
   char err_msg[LEN];
   short val;


   if (infile == NULL) 
   {
      error_message = "Bad File Pointer";
      return FAILURE;
   }

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;

   // Establish the setjmp return context for my_error_exit to use. 
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.

      jpeg_destroy_decompress(&cinfo);
//    fclose(infile);
      TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
      strcpy_s(err_msg, LEN, (char*) *cinfo.err->output_message);
//    error_message = err_msg;
      error_message = "JPEG decode error";
      return FAILURE;
//    return SUCCESS;
   }

   /* Now we can initialize the JPEG decompression object. */
   jpeg_create_decompress(&cinfo);

   /* Step 2: specify data source (eg, a file) */


   jpeg_stdio_src(&cinfo, infile);

   /* Step 3: read file parameters with jpeg_read_header() */

   (void) jpeg_read_header(&cinfo, TRUE);

   if (rgb_colorspace)
      cinfo.jpeg_color_space = JCS_RGB;


   // get data from JPEGTables if there is any
   if (m_jpeg_table_len > 0)
   {
      int ptr, k;

//    ptr = 4;
//    get_tif_dqt(&cinfo, ptr);
//    ptr += 2;
      // find huffman table
      k = 0;
      while (k<m_jpeg_table_len-1)
      {
         if ((m_jpeg_table[k] == 255) && (m_jpeg_table[k+1] == 219))
         {
            ptr = k + 2;
            get_tif_dqt(&cinfo, ptr);
         }
         if ((m_jpeg_table[k] == 255) && (m_jpeg_table[k+1] == 196))
         {
            ptr = k + 2;
            get_tif_dht(&cinfo, ptr);
         }
         k++;
      }
   }

   if (cinfo.quant_tbl_ptrs[0] == NULL)
   {
      int i;
      JQUANT_TBL *quant_ptr;
      jpeg_component_info *compptr;

      compptr = cinfo.cur_comp_info[0];
      compptr->quant_tbl_no = 0;

      cinfo.quant_tbl_ptrs[0] = (JQUANT_TBL*) malloc(sizeof(JQUANT_TBL));

      quant_ptr = cinfo.quant_tbl_ptrs[0];

      for (i = 0; i < DCTSIZE2; i++) 
      {
         if (default_comrat == 1)
            quant_ptr->quantval[i] = (UINT16) jpeg_default_quant_table_q1[i];
         else if (default_comrat == 2)
            quant_ptr->quantval[i] = (UINT16) jpeg_default_quant_table_q2[i];
         else if (default_comrat == 3)
            quant_ptr->quantval[i] = (UINT16) jpeg_default_quant_table_q3[i];
         else if (default_comrat == 4)
            quant_ptr->quantval[i] = (UINT16) jpeg_default_quant_table_q4[i];
         else if (default_comrat == 5)
            quant_ptr->quantval[i] = (UINT16) jpeg_default_quant_table_q5[i];
      }

   }

   if (cinfo.dc_huff_tbl_ptrs[0] == NULL)
      get_default_huff_tables(&cinfo);

   /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */

   /* Step 4: set parameters for decompression */

   /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

   /* Step 5: Start decompressor */

   (void) jpeg_start_decompress(&cinfo);

   iwidth = width;
   width = cinfo.image_width;
   if (cinfo.image_height < (unsigned int) height)
      height = cinfo.image_height;

   // We can ignore the return value since suspension is not possible
   // with the stdio data source.


   // We may need to do some setup of our own at this point before reading
   // the data.  After jpeg_start_decompress() we have the correct scaled
   // output image dimensions available, as well as the output colormap
   // if we asked for color quantization.
   // In this example, we need to make an output work buffer of the right size.

   // JSAMPLEs per row in output buffer 
   row_stride = cinfo.output_width * cinfo.output_components;

   // Make a one-row-high sample array that will go away when done with image 
   buffer = (*cinfo.mem->alloc_sarray) ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

   // Step 6: while (scan lines remain to be read) 
   //           jpeg_read_scanlines(...); 

   // Here we use the library's state variable cinfo.output_scanline as the
   // loop counter, so that we don't have to keep track ourselves.

   pixsize = cinfo.num_components;

   // skip over the y offset
   for (y=0; y<image_offset_y; y++)
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);

   cnt = 0;

   for (y=0; y<height; y++)
   {
      ypos = y+image_offset_y;
      if (ypos >= (int) cinfo.output_height)
      {
         // shortcut for out of image range condition
         for (x=0; x < width; x++)
         {
            red_array[cnt] = (BYTE) m_clear_r;
            green_array[cnt] = (BYTE) m_clear_g;
            blue_array[cnt] = (BYTE) m_clear_b;
            cnt++;
         }
         continue;
      }

      ASSERT (ypos < (int) cinfo.output_height);
      cinfo.output_scanline = ypos;

      // jpeg_read_scanlines expects an array of pointers to scanlines.
      // Here the array is only one element long, but you could ask for
      // more than one scanline at a time if that's more convenient.
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);
      if (rslt != 1)
      {
         jpeg_destroy_decompress(&cinfo);
         fclose(infile);
         error_message = "Error reading JPEG scanline";
         return FAILURE;
      }
      inptr = buffer[0];

      inptr += (image_offset_x * pixsize);

      for (x=0; x < iwidth; x++)
      {
         xpos = x+image_offset_x;
         if (xpos < (int) cinfo.output_width)
         {
            if (cinfo.data_precision == 12)
            {
               val = *inptr++;
               val = (short) (val >> 4);
               red_array[cnt] = (BYTE) val;
               green_array[cnt] = (BYTE) val;
               blue_array[cnt] = (BYTE) val;
            }
            else
            {
               if (pixsize == 3)
               {
                  red_array[cnt] = (BYTE) *inptr++;
                  green_array[cnt] = (BYTE) *inptr++;
                  blue_array[cnt] = (BYTE) *inptr++;
               }
               else
               {
                  red_array[cnt] = (BYTE) *inptr;
                  green_array[cnt] = (BYTE) *inptr;
                  blue_array[cnt] = (BYTE) *inptr++;
               }
            }

         }
         else
         {
            red_array[cnt] = (BYTE) m_clear_r;
            green_array[cnt] = (BYTE) m_clear_g;
            blue_array[cnt] = (BYTE) m_clear_b;
         }
         cnt++;
      }
   }

   /* Step 7: Finish decompression */

   (void) jpeg_finish_decompress(&cinfo);
   /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

   /* Step 8: Release JPEG decompression object */

   /* This is an important step since it will release a good deal of memory. */
   jpeg_destroy_decompress(&cinfo);

   /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */
// fclose(infile);

   /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   */

   /* And we're done! */
   return SUCCESS;
}
// end of get_jpeg_bitstream_image

// **********************************************************************
// **********************************************************************

int CJpeg::get_supersampled_rgb_image( double factor, // the degree of oversampling
                           int image_offset_x, int image_offset_y, 
                           int width, int height,
                           unsigned char *red_array, 
                           unsigned char *green_array,   
                           unsigned char *blue_array,
                           CString &error_message )
{
   if (m_filename.GetLength() < 5)
   {
      error_message.Format("Invalid Filename -- %s", (LPCSTR)m_filename );
      return FAILURE;
   }

   /* This struct contains the JPEG decompression parameters and pointers to
   * working space (which is allocated as needed by the JPEG library).
   */
   struct jpeg_decompress_struct cinfo;
   /* We use our private extension JPEG error handler.
   * Note that this struct must live as long as the main JPEG parameter
   * struct, to avoid dangling-pointer problems.
   */
   struct my_error_mgr jerr;
   /* More stuff */
   FILE * infile = NULL;   /* source file */
   JSAMPARRAY buffer;      /* Output row buffer */
   JSAMPROW inptr;
   int row_stride;         /* physical row width in output buffer */
   int x, cnt;
   int pixsize;
   int rslt;
   const int LEN = 1000;
   char err_msg[LEN];
   int row, col, iwidth, irow, index, icol;
   unsigned char *red_row, *green_row, *blue_row;
   int samp;

   // In this example we want to open the input file before doing anything else,
   // so that the setjmp() error recovery below can assume the file is open.
   // VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   // requires it in order to read binary files.
   
   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&infile, m_filename, "rb");
   if (infile == NULL) 
   {
      error_message.Format("Cannot open file -- %s", (LPCSTR)m_filename );
      return FAILURE;
   }

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;

   // Establish the setjmp return context for my_error_exit to use. */
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.

      jpeg_destroy_decompress(&cinfo);
      fclose(infile);
      TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
      strcpy_s(err_msg, LEN, (char*) *cinfo.err->output_message);
      error_message = err_msg;
//  return FAILURE;
      return SUCCESS;
   }

   // Now we can initialize the JPEG decompression object. 
   jpeg_create_decompress(&cinfo);

   // Step 2: specify data source (eg, a file) 
   jpeg_stdio_src(&cinfo, infile);

   // Step 3: read file parameters with jpeg_read_header() 
   (void) jpeg_read_header(&cinfo, TRUE);

   // We can ignore the return value from jpeg_read_header since
   //  (a) suspension is not possible with the stdio data source, and
   //  (b) we passed TRUE to reject a tables-only JPEG file as an error.
   // See libjpeg.doc for more info.
   

   // Step 4: set parameters for decompression 

   // In this example, we don't need to change any of the defaults set by
   // jpeg_read_header(), so we do nothing here.

   // Step 5: Start decompressor 

   (void) jpeg_start_decompress(&cinfo);
   // We can ignore the return value since suspension is not possible
   // with the stdio data source.


   // We may need to do some setup of our own at this point before reading
   // the data.  After jpeg_start_decompress() we have the correct scaled
   // output image dimensions available, as well as the output colormap
   // if we asked for color quantization.
   // In this example, we need to make an output work buffer of the right size.
    
   // JSAMPLEs per row in output buffer 
   row_stride = cinfo.output_width * cinfo.output_components;
   // Make a one-row-high sample array that will go away when done with image 
   buffer = (*cinfo.mem->alloc_sarray)
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

   iwidth = image_offset_x+(int) ((double) width / factor)-1;

   // Step 6: while (scan lines remain to be read) 
   //           jpeg_read_scanlines(...); 

   // Here we use the library's state variable cinfo.output_scanline as the
   // loop counter, so that we don't have to keep track ourselves.
  
   // allocate arrays to hold one row of data
   red_row = green_row = blue_row = NULL;

   red_row = (unsigned char*)malloc( iwidth );
   if ( red_row == NULL ) 
      goto FAIL;

   green_row = (unsigned char*)malloc( iwidth );
   if ( green_row == NULL ) 
      goto FAIL;

   blue_row = (unsigned char*)malloc( iwidth );
   if ( blue_row == NULL ) 
      goto FAIL;
   
   pixsize = cinfo.num_components;

   samp = (int) factor;

   // skip over the y offset
   for (row=0; row<image_offset_y; row++)
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);

   cnt = samp-1;

   index = 0;
   if ((unsigned int) iwidth > (int) cinfo.output_width)
      iwidth = (int) cinfo.output_width -1;
   for( row = 0; row < height; row++ )
   {
      irow = (int) ((double) row / factor);
      irow += image_offset_y;
      if (irow >= (int) cinfo.output_height)
      {
         // shortcut for out of image range condition
         for (x=0; x < width; x++)
         {
            red_array[index] = (BYTE) m_clear_r;
            green_array[index] = (BYTE) m_clear_g;
            blue_array[index] = (BYTE) m_clear_b;
            index++;
         }
         continue;
      }

      cnt++;
      if (cnt == samp)
      {
         ASSERT (irow < (int) cinfo.output_height);
         cinfo.output_scanline = irow;
         // jpeg_read_scanlines expects an array of pointers to scanlines.
         // Here the array is only one element long, but you could ask for
         // more than one scanline at a time if that's more convenient.
         rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);
         if (rslt != 1)
         {
            jpeg_destroy_decompress(&cinfo);
            fclose(infile);
            error_message = "Error reading JPEG scanline";
            goto FAIL;
         }
         inptr = buffer[0];

         inptr += (image_offset_x * pixsize);

         // put the data is a buffer
         for (x=0; x<iwidth; x++)
         {
            if (x < (int) cinfo.image_width - image_offset_x)
            {
               if (pixsize == 3)
               {
                  red_row[x] = (BYTE) *inptr++;
                  green_row[x] = (BYTE) *inptr++;
                  blue_row[x] = (BYTE) *inptr++;
               }
               else
               {
                  red_row[x] = (BYTE) *inptr;
                  green_row[x] = (BYTE) *inptr;
                  blue_row[x] = (BYTE) *inptr++;
               }
            }
         }
         cnt = 0;
      }

      for( col = 0; col < width; col++ )
      {
         icol = col;
         icol = (int) ((double) icol / factor);
         if ((icol <= iwidth) && ((unsigned int) irow < (int) cinfo.output_height))
         {
            red_array[index] = red_row[icol];
            green_array[index] = green_row[icol];
            blue_array[index] = blue_row[icol];
         }
         else
         {
            red_array[index]   = (BYTE) m_clear_r;
            green_array[index] = (BYTE) m_clear_g;
            blue_array[index]  = (BYTE) m_clear_b;
         }

         index++;
      }
   }

  // Step 7: Finish decompression 

   (void) jpeg_finish_decompress(&cinfo);
   // We can ignore the return value since suspension is not possible
   // with the stdio data source.


   // Step 8: Release JPEG decompression object 

   // This is an important step since it will release a good deal of memory. 
   jpeg_destroy_decompress(&cinfo);

   // After finish_decompress, we can close the input file.
   // Here we postpone it until after no more JPEG errors are possible,
   // so as to simplify the setjmp error logic above.  (Actually, I don't
   // think that jpeg_destroy can do an error exit, but why assume anything...)
   
   fclose(infile);

   // deallocate memory
   free( red_row );
   free( green_row );
   free( blue_row );
   return SUCCESS;

FAIL:
   // deallocate memory
   if ( red_row != NULL ) 
      free( red_row );
   if ( green_row != NULL ) 
      free( green_row );
   if ( blue_row != NULL ) 
      free( blue_row );
   return SUCCESS;
}
// end of get_supersampled_rgb_image

// **********************************************************************
// **********************************************************************


int CJpeg::get_subsampled_rgb_image(int sampling,  // inverse of zoom level i.e. 2=0.5, 4=0.25, etc.
                           int image_offset_x, int image_offset_y, 
                           int width, int height,
                           unsigned char *red_array, 
                           unsigned char *green_array,   
                           unsigned char *blue_array,
                           CString &error_message )
{
   if (m_filename.GetLength() < 5)
   {
      error_message.Format("Invalid Filename -- %s", (LPCSTR)m_filename );
      return FAILURE;
   }

   // This struct contains the JPEG decompression parameters and pointers to
   // working space (which is allocated as needed by the JPEG library).

   struct jpeg_decompress_struct cinfo;
   // We use our private extension JPEG error handler.
   // Note that this struct must live as long as the main JPEG parameter
   // struct, to avoid dangling-pointer problems.

   struct my_error_mgr jerr;
   FILE * infile = NULL;   // source file 
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   int row_stride;         // physical row width in output buffer 
   int x, cnt, k;
// int xpos, ypos;
   int pixsize;
   int rslt;
   const int LEN = 1000;
   char err_msg[LEN];
   int row, col, iwidth, iheight, irow, index, icol;
   unsigned char *red_row, *green_row, *blue_row;
   int max_array;

   max_array = width * height;

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&infile, m_filename, "rb");
   if (infile == NULL) 
   {
      error_message.Format("Cannot open file -- %s", (LPCSTR)m_filename );
      return FAILURE;
   }

   // Step 1: allocate and initialize JPEG decompression object 

   // We set up the normal JPEG error routines, then override error_exit. 
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   // Establish the setjmp return context for my_error_exit to use. 
   if (setjmp(jerr.setjmp_buffer)) 
   {
      // If we get here, the JPEG code has signaled an error.
      // We need to clean up the JPEG object, close the input file, and return.
     
      jpeg_destroy_decompress(&cinfo);
      fclose(infile);
      TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
      strcpy_s(err_msg, LEN, (char*) *cinfo.err->output_message);
      error_message = err_msg;
//    return FAILURE;
      return SUCCESS;
   }
   // initialize the JPEG decompression object. 
   jpeg_create_decompress(&cinfo);

   // Step 2: specify data source (eg, a file) 

   jpeg_stdio_src(&cinfo, infile);

   // Step 3: read file parameters with jpeg_read_header() 

   (void) jpeg_read_header(&cinfo, TRUE);
   // We can ignore the return value from jpeg_read_header since
   //   (a) suspension is not possible with the stdio data source, and
   //   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   // See libjpeg.doc for more info.


   // Step 4: set parameters for decompression 

   // In this example, we don't need to change any of the defaults set by
   ///jpeg_read_header(), so we do nothing here.
   

   // Step 5: Start decompressor 

   (void) jpeg_start_decompress(&cinfo);
   // We can ignore the return value since suspension is not possible
   // with the stdio data source.


   // We may need to do some setup of our own at this point before reading
   // the data.  After jpeg_start_decompress() we have the correct scaled
   // output image dimensions available, as well as the output colormap
   // if we asked for color quantization.
   // In this example, we need to make an output work buffer of the right size.
 
   // JSAMPLEs per row in output buffer 
   row_stride = cinfo.output_width * cinfo.output_components;
   // Make a one-row-high sample array that will go away when done with image 
   buffer = (*cinfo.mem->alloc_sarray)
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

   // Step 6: while (scan lines remain to be read) 
   //           jpeg_read_scanlines(...); 

   // Here we use the library's state variable cinfo.output_scanline as the
   // loop counter, so that we don't have to keep track ourselves.

   pixsize = cinfo.num_components;

   cnt = sampling-1;

   // skip over the y offset
   for (row=0; row<image_offset_y; row++)
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);

   // allocate arrays to hold one row of data
   red_row = green_row = blue_row = NULL;

   iwidth = image_offset_x + (width * sampling);
   if ((unsigned int) iwidth > cinfo.output_width)
      iwidth = cinfo.output_width;
   iheight = image_offset_y + (height * sampling);
   if ((unsigned int) iheight > cinfo.output_height)
      iheight = cinfo.output_height;

   red_row = (unsigned char*)malloc( iwidth );
   if ( red_row == NULL ) 
      goto FAIL;

   green_row = (unsigned char*)malloc( iwidth );
   if ( green_row == NULL ) 
      goto FAIL;

   blue_row = (unsigned char*)malloc( iwidth );
   if ( blue_row == NULL ) 
      goto FAIL;
   
   index = 0;
   for ( row = 0; row < height; row++ )
   {
      irow = image_offset_y + (row * sampling);
      if (irow >= iheight)
      {
         ASSERT(index < max_array);
         // shortcut for out of image range condition
         for (x=0; x < width; x++)
         {
            red_array[index] = (BYTE) m_clear_r;
            green_array[index] = (BYTE) m_clear_g;
            blue_array[index] = (BYTE) m_clear_b;
            index++;
         }
         continue;
      }

//    ASSERT (irow < (int) cinfo.output_height);
      cinfo.output_scanline = irow;
      // jpeg_read_scanlines expects an array of pointers to scanlines.
      // Here the array is only one element long, but you could ask for
      // more than one scanline at a time if that's more convenient.
      rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);

      if (rslt != 1)
      {
         jpeg_destroy_decompress(&cinfo);
         fclose(infile);
         error_message = "Error reading JPEG scanline";
         goto FAIL;
      }

      inptr = buffer[0];
      inptr += (image_offset_x * pixsize);

      // put the data is a buffer
      for (x=0; x<iwidth; x++)
      {
         if (x >= (int) cinfo.output_width - image_offset_x)
         {
            x = iwidth;
            continue;
         }
         if (pixsize == 3)
         {
            red_row[x] = (BYTE) *inptr++;
            green_row[x] = (BYTE) *inptr++;
            blue_row[x] = (BYTE) *inptr++;
         }
         else
         {
            red_row[x] = (BYTE) *inptr;
            green_row[x] = (BYTE) *inptr;
            blue_row[x] = (BYTE) *inptr++;
         }
      }

      for ( col = 0; col < width; col++ )
      {
         ASSERT(index < max_array);
         icol = col * sampling;
         if ((icol < (iwidth - image_offset_x)) && (irow < iheight))
         {
            red_array[index]   = red_row[icol];
            green_array[index] = green_row[icol];
            blue_array[index]  = blue_row[icol];
         }
         else
         {
            red_array[index]   = (BYTE) m_clear_r;
            green_array[index] = (BYTE) m_clear_g;
            blue_array[index]  = (BYTE) m_clear_b;
         }
         index++;
      }
      // read the unused lines
      for (k=0; k<sampling-1; k++)
      {
         rslt =  jpeg_read_scanlines(&cinfo, buffer, 1);

         if (rslt != 1)
         {
            jpeg_destroy_decompress(&cinfo);
            fclose(infile);
            error_message = "Error reading JPEG scanline";
            goto FAIL;
         }
      }
   }
 

   // Step 7: Finish decompression 

   (void) jpeg_finish_decompress(&cinfo);
   // We can ignore the return value since suspension is not possible
   // with the stdio data source.
   
   // Step 8: Release JPEG decompression object 

   // This is an important step since it will release a good deal of memory.
   jpeg_destroy_decompress(&cinfo);

   // After finish_decompress, we can close the input file.
   // Here we postpone it until after no more JPEG errors are possible,
   // so as to simplify the setjmp error logic above.  (Actually, I don't
   // think that jpeg_destroy can do an error exit, but why assume anything...)
   
   fclose(infile);

   // At this point you may want to check to see whether any corrupt-data
   // warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   

   // And we're done! 
   // deallocate memory
   free( red_row );
   free( green_row );
   free( blue_row );
   return SUCCESS;

FAIL:
   // deallocate memory
   if ( red_row != NULL ) 
      free( red_row );
   if ( green_row != NULL ) 
      free( green_row );
   if ( blue_row != NULL ) 
      free( blue_row );
   return FAILURE;
}
// end of get_subsampled_rgb_image

/*
 * SOME FINE POINTS:
 *
 * In the above code, we ignored the return value of jpeg_read_scanlines,
 * which is the number of scanlines actually read.  We could get away with
 * this because we asked for only one line at a time and we weren't using
 * a suspending data source.  See libjpeg.doc for more info.
 *
 * We cheated a bit by calling alloc_sarray() after jpeg_start_decompress();
 * we should have done it beforehand to ensure that the space would be
 * counted against the JPEG max_memory setting.  In some systems the above
 * code would risk an out-of-memory error.  However, in general we don't
 * know the output image dimensions before jpeg_start_decompress(), unless we
 * call jpeg_calc_output_dimensions().  See libjpeg.doc for more about this.
 *
 * Scanlines are returned in the same order as they appear in the JPEG file,
 * which is standardly top-to-bottom.  If you must emit data bottom-to-top,
 * you can use one of the virtual arrays provided by the JPEG memory manager
 * to invert the data.  See wrbmp.c for an example.
 *
 * As with compression, some operating modes may require temporary files.
 * On some systems you may need to set up a signal handler to ensure that
 * temporary files are deleted if the program is interrupted.  See libjpeg.doc.
 */


// **********************************************************************
// ********************************************************************

WORD WINAPI DIBNumColors(BYTE* lpbi)
{
   WORD wBitCount;  // DIB bit count

   /*  If this is a Windows-style DIB, the number of colors in the
    *  color table can be less than the number of bits per pixel
    *  allows for (i.e. lpbi->biClrUsed can be set to some value).
    *  If this is the case, return the appropriate value.
    */

   if (IS_WIN30_DIB(lpbi))
   {
      DWORD dwClrUsed;

      dwClrUsed = ((LPBITMAPINFOHEADER)lpbi)->biClrUsed;
      if (dwClrUsed != 0)
         return (WORD)dwClrUsed;
   }

   /*  Calculate the number of colors in the color table based on
    *  the number of bits per pixel for the DIB.
    */
   if (IS_WIN30_DIB(lpbi))
      wBitCount = ((LPBITMAPINFOHEADER)lpbi)->biBitCount;
   else
      wBitCount = ((LPBITMAPCOREHEADER)lpbi)->bcBitCount;

   /* return number of colors based on bits per pixel */
   switch (wBitCount)
   {
      case 1:
         return 2;

      case 4:
         return 16;

      case 8:
         return 256;

      default:
         return 0;
   }
}


// ********************************************************************
// ********************************************************************

WORD WINAPI PaletteSize(BYTE* lpbi)
{
   /* calculate the size required by the palette */
   if (IS_WIN30_DIB (lpbi))
     return (WORD)(DIBNumColors(lpbi) * sizeof(RGBQUAD));
   else
     return (WORD)(DIBNumColors(lpbi) * sizeof(RGBTRIPLE));
}

// ********************************************************************
// ********************************************************************

BYTE* WINAPI FindDIBBits(BYTE* lpbi)
{
   return (lpbi + *(LPDWORD)lpbi + PaletteSize(lpbi));
}

// ********************************************************************
// ****************************************************************

int get_pixel( BYTE* hdib,  int byte_width, int height,
            int hpix, int vpix, unsigned char &red,
            unsigned char &green, unsigned char &blue )
{
   // sets the specified pixel in the dib to the specified color
   // returns 0 for success or -1 for failure

   int index;
   BYTE * data;
 
   index = 3*hpix + byte_width * (height - 1 - vpix);

   data = FindDIBBits(hdib);
   red = data[index];
   green = data[index+1];
   blue = data[index+2];

   return 0;
}

// ****************************************************************
// ****************************************************************

BYTE get_pixel_216( BYTE* hdib,  int byte_width, int height, int hpix, int vpix )
{
   int index;
   int color;
   BYTE* data;

   index = hpix + byte_width * (height - 1 - vpix);

   data = FindDIBBits(hdib);
   color = data[index];
   return (BYTE) color;
}


// ********************************************************************
// **********************************************************************

int CJpeg::write_jpeg_file( CString filename,  
                           BYTE* hdib,   // HDIB
                           int quality,   // valid values 0 (terrible) to 100 (very good)
                           CString &error_message )
{
   FILE *outfile = NULL;
   struct jpeg_compress_struct cinfo;
   struct jpeg_error_mgr jerr;
// cjpeg_source_ptr src_mgr;
// JDIMENSION num_scanlines;
   int width, height;
// J_COLOR_SPACE colorspace;
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   LPBITMAPINFOHEADER lpbmi;  // pointer to a Win 3.0-style DIB
   int bits_pixel;
   int byte_width;

   if (filename.GetLength() < 5)
   {
     error_message.Format("Invalid file name -- %s", (LPCSTR)filename );
     return FAILURE;
   }

   // check for TIROS file
   CString ext;
   ext = filename.Right(3);
   if (!ext.CompareNoCase("TIR"))
      use_encryption = 1;
   else
      use_encryption = 0;

   crypt_pos = 0;

   lpbmi = (LPBITMAPINFOHEADER) hdib;
   byte_width = WIDTHBYTES((lpbmi->biWidth)*((DWORD)lpbmi->biBitCount));
   width = lpbmi->biWidth;
   height = lpbmi->biHeight;
   bits_pixel = lpbmi->biBitCount;

   // Initialize the JPEG compression object with default error handling. 
   cinfo.err = jpeg_std_error(&jerr);
   jpeg_create_compress(&cinfo);

   /* Initialize JPEG parameters.
   * Much of this may be overridden later.
   * In particular, we don't yet know the input file's color space,
   * but we need to provide some value for jpeg_set_defaults() to work.
   */

   cinfo.in_color_space = JCS_RGB; /* arbitrary guess */
   jpeg_set_defaults(&cinfo);

   // set up compress parameters
   cinfo.dct_method = JDCT_FLOAT;
// cinfo.err->trace_level = 0;
   jpeg_set_colorspace(&cinfo, JCS_YCbCr);
   cinfo.mem->max_memory_to_use = 20000 * 1000L;
   cinfo.optimize_coding = TRUE;
   jpeg_set_quality(&cinfo, quality, FALSE);
   cinfo.arith_code = FALSE;


  /* Now safe to enable signal catcher. */
//#ifdef NEED_SIGNAL_CATCHER
//  enable_signal_catcher((j_common_ptr) &cinfo);
//#endif

   /* Scan command line to find file names.
   * It is convenient to use just one switch-parsing routine, but the switch
   * values read here are ignored; we will rescan the switches after opening
   * the input file.
   */

// file_index = parse_switches(&cinfo, argc, argv, 0, FALSE);

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&outfile, filename, "wb");
   if (outfile == NULL)
   {                    
      error_message = "Unable to create JPEG file -- ";
      error_message += filename;
      return FAILURE;
   }
   
   /* Now that we know input colorspace, fix colorspace-dependent defaults */
// jpeg_default_colorspace(&cinfo);

   // Specify data destination for compression 
   jpeg_stdio_dest(&cinfo, outfile);

   cinfo.image_height = height;
   cinfo.image_width = width;

   cinfo.num_components = 3;
   cinfo.input_components = 3;

   // Start compressor 
   jpeg_start_compress(&cinfo, TRUE);


   // Process data 
   int x, y;
   BYTE color, r, g, b;

   // Make a one-row-high sample array that will go away when done with image 
   int row_stride = cinfo.image_width * cinfo.input_components;
   buffer = (*cinfo.mem->alloc_sarray)
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);


// while (cinfo.next_scanline < cinfo.image_height) 
   for (y=0; y<height; y++)
   {
      // build a scan line
      inptr =  buffer[0];
      for (x=0; x<width; x++)
      {
         if (lpbmi->biBitCount <= 8)
         {
            color = get_pixel_216(hdib, byte_width, height, x, y);
            *inptr = color;
            inptr++;
         }
         else
         {
            get_pixel(hdib, byte_width, height, x, y, b, g, r);
            *inptr = r;
            inptr++;
            *inptr = g;
            inptr++;
            *inptr = b;
            inptr++;
         }
      }
      (void) jpeg_write_scanlines(&cinfo, buffer, 1);
   }

   /* Finish compression and release memory */
   jpeg_finish_compress(&cinfo);
   jpeg_destroy_compress(&cinfo);

   /* Close files, if we opened them */
   fclose(outfile);

   // All done. 
   return SUCCESS;      
}

// **********************************************************************
// **********************************************************************

int CJpeg::write_jpeg_file( CString filename, int width, int height, BYTE* img, 
                           int quality,   // valid values 0 (terrible) to 100 (very good)
                           CString &error_message )
{
   FILE *outfile = NULL;
   struct jpeg_compress_struct cinfo;
   struct jpeg_error_mgr jerr;
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
// LPBITMAPINFOHEADER lpbmi;  // pointer to a Win 3.0-style DIB
   int bits_pixel;
   int byte_width;

   if (filename.GetLength() < 5)
   {
     error_message.Format("Invalid file name -- %s", (LPCSTR)filename );
     return FAILURE;
   }

   // check for TIROS file
   CString ext;
   ext = filename.Right(3);
   if (!ext.CompareNoCase("TIR"))
      use_encryption = 1;
   else
      use_encryption = 0;

   crypt_pos = 0;

// byte_width = WIDTHBYTES(width * 3);
   byte_width = width * 3;
   bits_pixel = 24;

   // Initialize the JPEG compression object with default error handling. 
   cinfo.err = jpeg_std_error(&jerr);
   jpeg_create_compress(&cinfo);

   /* Initialize JPEG parameters.
   * Much of this may be overridden later.
   * In particular, we don't yet know the input file's color space,
   * but we need to provide some value for jpeg_set_defaults() to work.
   */

   cinfo.in_color_space = JCS_RGB; /* arbitrary guess */
   jpeg_set_defaults(&cinfo);

   // set up compress parameters
   cinfo.dct_method = JDCT_FLOAT;
// cinfo.err->trace_level = 0;
   jpeg_set_colorspace(&cinfo, JCS_YCbCr);
   cinfo.mem->max_memory_to_use = 20000 * 1000L;
   cinfo.optimize_coding = TRUE;
   jpeg_set_quality(&cinfo, quality, FALSE);
   cinfo.arith_code = FALSE;


  /* Now safe to enable signal catcher. */
//#ifdef NEED_SIGNAL_CATCHER
//  enable_signal_catcher((j_common_ptr) &cinfo);
//#endif

   /* Scan command line to find file names.
   * It is convenient to use just one switch-parsing routine, but the switch
   * values read here are ignored; we will rescan the switches after opening
   * the input file.
   */

// file_index = parse_switches(&cinfo, argc, argv, 0, FALSE);

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&outfile, filename, "wb");
   if (outfile == NULL)
   {                    
      error_message = "Unable to create JPEG file -- ";
      error_message += filename;
      return FAILURE;
   }
   
   /* Now that we know input colorspace, fix colorspace-dependent defaults */
// jpeg_default_colorspace(&cinfo);

   // Specify data destination for compression 
   jpeg_stdio_dest(&cinfo, outfile);

   cinfo.image_height = height;
   cinfo.image_width = width;
   cinfo.num_components = 3;
   cinfo.input_components = 3;

   // Start compressor 
   jpeg_start_compress(&cinfo, TRUE);


   // Process data 
// int x, y;
// BYTE color, r, g, b;

   // Make a one-row-high sample array that will go away when done with image 
   int row_stride = cinfo.image_width * cinfo.input_components;
   buffer = (*cinfo.mem->alloc_sarray)
//    ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, height);

/*
// while (cinfo.next_scanline < cinfo.image_height) 
   for (y=0; y<height; y++)
   {
      // build a scan line
      inptr =  buffer[0];
      for (x=0; x<width; x++)
      {
//       if (lpbmi->biBitCount <= 8)
//       {
//          color = get_pixel_216(hdib, byte_width, height, x, y);
//          *inptr = color;
//          inptr++;
//       }
//       else
//       {
//          get_pixel(hdib, byte_width, height, x, y, b, g, r);
//          *inptr = r;
//          inptr++;
//          *inptr = g;
//          inptr++;
//          *inptr = b;
//          inptr++;
//       }
      }
      memcpy(buffer, img + (y * byte_width), byte_width);
      (void) jpeg_write_scanlines(&cinfo, buffer, 1);
   }
*/    
   inptr =  buffer[0];
   memcpy(inptr, img, width * height * 3);

   (void) jpeg_write_scanlines(&cinfo, buffer, height);

   /* Finish compression and release memory */
   jpeg_finish_compress(&cinfo);
   jpeg_destroy_compress(&cinfo);

   /* Close files, if we opened them */
   fclose(outfile);

   // All done. 
   return SUCCESS;      
}
// end of write_jpeg_file

// **********************************************************************
// **********************************************************************

int CJpeg::write_ms_jpeg_file( CString filename, int width, int height, BYTE* img, 
                           int quality,   // valid values 0 (terrible) to 100 (very good)
                           CString &error_message )
{
   FILE *outfile = NULL;
   struct jpeg_compress_struct cinfo;
   struct jpeg_error_mgr jerr;
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
// LPBITMAPINFOHEADER lpbmi;  // pointer to a Win 3.0-style DIB
   int bits_pixel;
   int byte_width;

   if (filename.GetLength() < 5)
   {
     error_message.Format("Invalid file name -- %s", (LPCSTR)filename );
     return FAILURE;
   }

   // check for TIROS file
   CString ext;
   ext = filename.Right(3);
   if (!ext.CompareNoCase("TIR"))
      use_encryption = 1;
   else
      use_encryption = 0;

   crypt_pos = 0;

// byte_width = WIDTHBYTES(width * 3);
   byte_width = width * 3;
   bits_pixel = 24;

   // Initialize the JPEG compression object with default error handling. 
   cinfo.err = jpeg_std_error(&jerr);
   jpeg_create_compress(&cinfo);

   /* Initialize JPEG parameters.
   * Much of this may be overridden later.
   * In particular, we don't yet know the input file's color space,
   * but we need to provide some value for jpeg_set_defaults() to work.
   */

   cinfo.in_color_space = JCS_MS; /* arbitrary guess */
   jpeg_set_defaults(&cinfo);

   // set up compress parameters
   cinfo.dct_method = JDCT_FLOAT;
// cinfo.err->trace_level = 0;
   jpeg_set_colorspace(&cinfo, JCS_MS);
   cinfo.mem->max_memory_to_use = 20000 * 1000L;
   cinfo.optimize_coding = TRUE;
   jpeg_set_quality(&cinfo, quality, FALSE);
   cinfo.arith_code = FALSE;


  /* Now safe to enable signal catcher. */
//#ifdef NEED_SIGNAL_CATCHER
//  enable_signal_catcher((j_common_ptr) &cinfo);
//#endif

   /* Scan command line to find file names.
   * It is convenient to use just one switch-parsing routine, but the switch
   * values read here are ignored; we will rescan the switches after opening
   * the input file.
   */

// file_index = parse_switches(&cinfo, argc, argv, 0, FALSE);

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&outfile, filename, "wb");
   if (outfile == NULL)
   {                    
      error_message = "Unable to create JPEG file -- ";
      error_message += filename;
      return FAILURE;
   }
   
   /* Now that we know input colorspace, fix colorspace-dependent defaults */
// jpeg_default_colorspace(&cinfo);

   // Specify data destination for compression 
   jpeg_stdio_dest(&cinfo, outfile);

   cinfo.image_height = height;
   cinfo.image_width = width;
   cinfo.num_components = 4;
   cinfo.input_components = 4;

   // Start compressor 
   jpeg_start_compress(&cinfo, TRUE);


   // Process data 
// int x, y;
// BYTE color, r, g, b;

   // Make a one-row-high sample array that will go away when done with image 
   int row_stride = cinfo.image_width * cinfo.input_components;
   buffer = (*cinfo.mem->alloc_sarray)
//    ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, height);

   inptr =  buffer[0];
   memcpy(inptr, img, width * height * 4);

   (void) jpeg_write_scanlines(&cinfo, buffer, height);

   /* Finish compression and release memory */
   jpeg_finish_compress(&cinfo);
   jpeg_destroy_compress(&cinfo);

   /* Close files, if we opened them */
   fclose(outfile);

   // All done. 
   return SUCCESS;      
}
// end of write_ms_jpeg_file

// **********************************************************************
// **********************************************************************

/*
int CJpeg::write_rgb_jpeg_file( CString filename, int width, int height, 
                        BYTE *red, BYTE *grn, BYTE *blu,
                        int quality,   // valid values 0 (terrible) to 100 (very good)
                        CString &error_message )
{
   FILE *outfile;
   struct jpeg_compress_struct cinfo;
   struct jpeg_error_mgr jerr;
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
// LPBITMAPINFOHEADER lpbmi;  // pointer to a Win 3.0-style DIB
   int bits_pixel;
   int byte_width;

   if (filename.GetLength() < 5)
   {
     error_message.Format("Invalid file name -- %s", filename);
     return FAILURE;
   }

// byte_width = WIDTHBYTES(width * 3);
   byte_width = width * 3;
   bits_pixel = 24;

   // Initialize the JPEG compression object with default error handling. 
   cinfo.err = jpeg_std_error(&jerr);
   jpeg_create_compress(&cinfo);

   // Initialize JPEG parameters.
   // Much of this may be overridden later.
   // In particular, we don't yet know the input file's color space,
   // but we need to provide some value for jpeg_set_defaults() to work.
   

   cinfo.in_color_space = JCS_RGB; // arbitrary guess 
   jpeg_set_defaults(&cinfo);

   // set up compress parameters
   cinfo.dct_method = JDCT_FLOAT;
// cinfo.err->trace_level = 0;
   jpeg_set_colorspace(&cinfo, JCS_YCbCr);
   cinfo.mem->max_memory_to_use = 20000 * 1000L;
   cinfo.optimize_coding = TRUE;
   jpeg_set_quality(&cinfo, quality, FALSE);
   cinfo.arith_code = FALSE;


  // Now safe to enable signal catcher. 
//#ifdef NEED_SIGNAL_CATCHER
//  enable_signal_catcher((j_common_ptr) &cinfo);
//#endif

   // Scan command line to find file names.
   // It is convenient to use just one switch-parsing routine, but the switch
   // values read here are ignored; we will rescan the switches after opening
   // the input file.
   

// file_index = parse_switches(&cinfo, argc, argv, 0, FALSE);

   outfile = fopen(filename, "wb");
   if (outfile == NULL)
   {                    
      error_message = "Unable to create JPEG file -- ";
      error_message += filename;
      return FAILURE;
   }
   
   // Now that we know input colorspace, fix colorspace-dependent defaults 
// jpeg_default_colorspace(&cinfo);

   // Specify data destination for compression 
   jpeg_stdio_dest(&cinfo, outfile);

   cinfo.image_height = height;
   cinfo.image_width = width;
   cinfo.num_components = 3;
   cinfo.input_components = 3;

   // Start compressor 
   jpeg_start_compress(&cinfo, TRUE);


   // Process data 
// int x, y;
// BYTE color, r, g, b;

   // Make a one-row-high sample array that will go away when done with image 
   int row_stride = cinfo.image_width * cinfo.input_components;
   buffer = (*cinfo.mem->alloc_sarray)
//    ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
      ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, height);

   inptr =  buffer[0];
   memcpy(inptr, img, width * height * 3);
   (void) jpeg_write_scanlines(&cinfo, buffer, height);

   // Finish compression and release memory 
   jpeg_finish_compress(&cinfo);
   jpeg_destroy_compress(&cinfo);

   // Close files, if we opened them 
   fclose(outfile);

   // All done. 
   return SUCCESS;      
}
// end of write_rgb_jpeg_file
*/

// **********************************************************************
// **********************************************************************

int CJpeg::open_jpeg_file_for_write( CString filename, int width, int height, 
                           int quality,   // valid values 0 (terrible) to 100 (very good)
                           struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                           FILE **outfile,  // file pointer of jpeg file
                           CString &error_message )
{
   struct jpeg_error_mgr jerr;
   int bits_pixel;
   int byte_width;

   if (filename.GetLength() < 5)
   {
     error_message.Format("Invalid file name -- %s", (LPCSTR)filename );
     return FAILURE;
   }

// byte_width = WIDTHBYTES(width * 3);
   byte_width = width * 3;
   bits_pixel = 24;

   // Initialize the JPEG compression object with default error handling. 
   cinfo->err = jpeg_std_error(&jerr);
   jpeg_create_compress(cinfo);

   /* Initialize JPEG parameters.
   * Much of this may be overridden later.
   * In particular, we don't yet know the input file's color space,
   * but we need to provide some value for jpeg_set_defaults() to work.
   */

   cinfo->in_color_space = JCS_RGB; /* arbitrary guess */
   jpeg_set_defaults(cinfo);

   // set up compress parameters
   cinfo->dct_method = JDCT_FLOAT;
// cinfo.err->trace_level = 0;
   jpeg_set_colorspace(cinfo, JCS_YCbCr);
   cinfo->mem->max_memory_to_use = 20000 * 1000L;
   cinfo->optimize_coding = TRUE;
   jpeg_set_quality(cinfo, quality, FALSE);
   cinfo->arith_code = FALSE;


  /* Now safe to enable signal catcher. */
//#ifdef NEED_SIGNAL_CATCHER
//  enable_signal_catcher((j_common_ptr) &cinfo);
//#endif

   /* Scan command line to find file names.
   * It is convenient to use just one switch-parsing routine, but the switch
   * values read here are ignored; we will rescan the switches after opening
   * the input file.
   */

// file_index = parse_switches(&cinfo, argc, argv, 0, FALSE);

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   *outfile = NULL;
   fopen_s(outfile, filename, "wb");
   if (*outfile == NULL)
   {                    
      error_message = "Unable to create JPEG file -- ";
      error_message += filename;
      return FAILURE;
   }
   
   /* Now that we know input colorspace, fix colorspace-dependent defaults */
// jpeg_default_colorspace(&cinfo);

   // Specify data destination for compression 
   jpeg_stdio_dest(cinfo, *outfile);

   cinfo->image_height = height;
   cinfo->image_width = width;
   cinfo->num_components = 3;
   cinfo->input_components = 3;

//set_grayscale(cinfo, TRUE);

   // Start compressor 
   jpeg_start_compress(cinfo, TRUE);

   // All done. 
   return SUCCESS;      
}
// end of open_jpeg_file_for_write

// **********************************************************************
// **********************************************************************

int CJpeg::write_jpeg_lines( BYTE* line_buf, int num_lines,
                           struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                           FILE *outfile,
                           CString &error_message )
{
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;

   // Make a one-row-high sample array that will go away when done with image 
   int row_stride = cinfo->image_width * cinfo->input_components;
   buffer = (*cinfo->mem->alloc_sarray)
      ((j_common_ptr) cinfo, JPOOL_IMAGE, row_stride, num_lines);

   inptr =  buffer[0];
   memcpy(inptr, line_buf, cinfo->image_width * num_lines * 3);
   (void) jpeg_write_scanlines(cinfo, buffer, num_lines);


   // All done. 
   error_message = "";
   return SUCCESS;      
}
// end of write_jpeg_line

// **********************************************************************
// **********************************************************************

int CJpeg::write_jpeg_line( BYTE* red, BYTE* grn, BYTE* blu, 
                     struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                     FILE *outfile,
                     CString &error_message )
{
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   BYTE *line_buf;
   int k, pos, width;

   width = cinfo->image_width;
   line_buf = (BYTE*) malloc(width * 3);
   if (line_buf == NULL)
   {
      ASSERT(0);
      error_message = "Memory Allocation Error";
      return FAILURE;
   }
   for (k=0; k<width; k++)
   {
      pos = k * 3;
      line_buf[pos+0] = red[k];
      line_buf[pos+1] = grn[k];
      line_buf[pos+2] = blu[k];
   }
   
   // Make a one-row-high sample array that will go away when done with image 
   int row_stride = cinfo->image_width * cinfo->input_components;
   buffer = (*cinfo->mem->alloc_sarray)
      ((j_common_ptr) cinfo, JPOOL_IMAGE, row_stride, 1);

   inptr =  buffer[0];
   memcpy(inptr, line_buf, cinfo->image_width * 3);
   (void) jpeg_write_scanlines(cinfo, buffer, 1);

   // All done. 
   error_message = "";
   return SUCCESS;      
}
// end of write_jpeg_line

// **********************************************************************
// **********************************************************************

int CJpeg::write_jpeg_line_1( BYTE* line_buf,  
                     struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                     CString &error_message )
{
   JSAMPARRAY buffer;      // Output row buffer 
   JSAMPROW inptr;
   int width;

   width = cinfo->image_width;
   
   // Make a one-row-high sample array that will go away when done with image 
   int row_stride = cinfo->image_width * cinfo->input_components;
   buffer = (*cinfo->mem->alloc_sarray) ((j_common_ptr) cinfo, JPOOL_IMAGE, row_stride, 1);

   inptr =  buffer[0];
   memcpy(inptr, line_buf, cinfo->image_width * 3);
   (void) jpeg_write_scanlines(cinfo, buffer, 1);

   // All done. 
   error_message = "";
   return SUCCESS;      
}
// end of write_jpeg_line

// **********************************************************************
// **********************************************************************

void CJpeg::close_jpeg_write(struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                     FILE *outfile)
{
   /* Finish compression and release memory */
   jpeg_finish_compress(cinfo);
   jpeg_destroy_compress(cinfo);

   /* Close files, if we opened them */
   fclose(outfile);
}

// **********************************************************************
// **********************************************************************

int CJpeg::check_jpeg_file(CString filename, CString & error_msg)
{
   FILE *f = NULL;
   int rslt;

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   fopen_s(&f, filename, "rb");
   if (f == NULL)
   {
      error_msg.Format("Error opening file - %s", (LPCSTR)filename );
      return FAILURE;
   }

   rslt = check_jpeg_file(f, error_msg);

   fclose(f);

   return rslt;
}

// **********************************************************************
// **********************************************************************

// check for a valid jpep file, preserve file position

int CJpeg::check_jpeg_file(FILE *f, CString & error_msg)
{
   BYTE c1, c2;
   int cnt;

//return SUCCESS;

   // Save the current file position
   fpos_t fpos;
   JFGETPOS( f, &fpos);
   JFREAD( f, &c1, 1 );
   JFREAD( f, &c2, 1 );

   // check for a SOI marker at beginning of the file
   if ((c1 != 255) || (c2 != 216))
   {
      error_msg = "No SOI marker at beginning of file";
      JFSETPOS( f, &fpos );
      return FAILURE;
   }
   
   cnt = 0;

   // Search until found EOI
   JFREAD( f, &c1, 1 );
   while ((c1 != 255) && (!feof(f)))
   {
      JFREAD( f, &c1, 1 );
      cnt++;
   }

   // Restore the file position
   if ( JFSETPOS( f, &fpos ) != 0 )
   {
      error_msg = "fsetpos error";
      return FAILURE;
   }

   return SUCCESS;
}
// end of check_jpep_file


#if defined MAPPEDFILE_SUPPORT || defined READFILE_SUPPORT || defined LLFILE_SUPPORT

#pragma auto_inline( off )

static VOID RewriteByte( PCHAR pchIn, PCHAR pchOut )
{
   *pchOut = *pchIn;             // Will make target page dirty
}

#pragma auto_inline( on )

//
// jpeg_fread() - Alternate handler for fread() for memory-mapped file
//
size_t jpeg_fread( void* pvbuf, size_t sizeofbuf, FILE* pfile )
{
   // If normal file read, do it
   if ( ( pfile->_flag & SPECIAL_IOBUF_FLAG ) == 0 )
      return fread( pvbuf, (size_t) 1, sizeofbuf, pfile );

   // If set up for ReadFile access, do it that way
   OVERLAPPED* pOverlapped;
   if ( ( pOverlapped = (OVERLAPPED*) pfile->_tmpfname ) != NULL )
   {
      DWORD cBytesRead;
      LARGE_INTEGER liPosition;
      liPosition.LowPart = pOverlapped->Offset; // Remember original file position
      liPosition.HighPart = pOverlapped->OffsetHigh;

      do    // Once
      {
#if 0
         static const INT MIN_BYTES_TO_READ = 0x10000;//0x1000; // 4096
         if ( sizeofbuf < MIN_BYTES_TO_READ )
         {
            BYTE bTemp[ 2 * MIN_BYTES_TO_READ ];

            pOverlapped->Offset &= -MIN_BYTES_TO_READ;   // Round file position to 4096 boundary
            if ( jpeg_fread( bTemp, 2 * MIN_BYTES_TO_READ, pfile ) == 2 * MIN_BYTES_TO_READ )
            {
               // Copy out the fragment
               memcpy( pvbuf, bTemp + liPosition.LowPart + ( 2 * MIN_BYTES_TO_READ ) - pOverlapped->Offset,
                  sizeofbuf );
               cBytesRead = sizeofbuf;    // Original count
               break;
            }
            pOverlapped->Offset = liPosition.LowPart;    // Reset original address if error
         }
#endif
         if ( ! ReadFile( (HANDLE) pfile->_file, pvbuf, (DWORD) sizeofbuf, &cBytesRead, pOverlapped ) )
         {
            DWORD dwErr;
            if ( ( dwErr = GetLastError() ) == ERROR_IO_PENDING )
            {
               // Wait for completion and retrieve byte count
               GetOverlappedResult( (HANDLE) pfile->_file, pOverlapped, &cBytesRead, TRUE );
            }
         }
      } while ( FALSE );

      // Update the file position in the OVERLAPPED struct for the bytes read
      liPosition.QuadPart += cBytesRead;
      pOverlapped->Offset = liPosition.LowPart; pOverlapped->OffsetHigh = liPosition.HighPart;

      return cBytesRead;
   }

   // If low-level file operation
   if ( pfile->_file != NULL )
   {
      pfile->_ptr += sizeofbuf;
      return _read( pfile->_file, pvbuf, (UINT) sizeofbuf );
   }

   // Must be a mapped file descriptor
   size_t iPos = pfile->_ptr - pfile->_base;
   if ( iPos > (size_t) pfile->_bufsiz )
      sizeofbuf = 0;
   else if ( iPos + sizeofbuf > (size_t) pfile->_bufsiz )
      sizeofbuf = (size_t) pfile->_bufsiz - iPos;  // Not beyond file

   __try    // Protect against read failure or network problem
   {
      memcpy( pvbuf, pfile->_ptr, sizeofbuf );

      // If not local hard drive, mark dirty to force local paging
      if ( pfile->_cnt > 0 )     // Page size left here
      {
         // Tickle each page in the range
         for ( PCHAR pch = pfile->_ptr;
            pch < pfile->_ptr + sizeofbuf;
            pch += pfile->_cnt )
         {
            RewriteByte( pch, pch );   // Rewrite one byte in each page
         }
      }

   }
   __except( GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH )
   {
      return 0;      // Assume nothing copied
   }
   
   pfile->_ptr += sizeofbuf;
   return sizeofbuf;

}  // End of jpeg_fread



//
// jpeg_fgetpos() - Alternate handler for fgetpos() for memory-mapped file
//
int jpeg_fgetpos( FILE* pfile, fpos_t* pfpos )
{
   // If normal file fgetpos, do it
   if ( ( pfile->_flag & 0x80000000 ) == 0 )
      return fgetpos( pfile, pfpos );

   // If set up for ReadFile access, do it that way
   OVERLAPPED* pOverlapped;
   if ( ( pOverlapped = (OVERLAPPED*) pfile->_tmpfname ) != NULL )
   {
      *pfpos = (fpos_t) pOverlapped->Offset;
   }

   // If low-level I/O setup
   else if ( pfile->_file != 0 )
   {
      if ( ( *pfpos = (fpos_t) _telli64( pfile->_file ) ) == -1 )
         return -1;     // Bad handle
   }

   // Must be a pseudo-FILE descriptor for mapped file
   else
   {
      *pfpos = (fpos_t) ( pfile->_ptr - pfile->_base );
   }
   return 0;

}  // End of jpeg_fgetpos



//
// jpeg_fsetpos() - Alternate handler for fsetpos() for memory-mapped file
//
int jpeg_fsetpos( FILE* pfile, fpos_t* pfpos )
{
   // If normal file fsetpos, do it
   if ( ( pfile->_flag & 0x80000000 ) == 0 )
      return fsetpos( pfile, pfpos );

   // If set up for ReadFile access, do it that way
   OVERLAPPED* pOverlapped;
   if ( ( pOverlapped = (OVERLAPPED*) pfile->_tmpfname ) != NULL )
   {
      pOverlapped->Offset = (DWORD) *pfpos;
   }

   // If low-level file I/O
   else if ( pfile->_file != 0 )
   {
      if ( _lseeki64( pfile->_file, *pfpos, SEEK_SET ) == -1 )
         return -1;     // May be bad handle
   }
   else   // Must be a mapped-file descriptor
   {
      pfile->_ptr = pfile->_base + (INT) *pfpos;
   }
   return 0;

}  // End of jpeg_fgetpos
#endif


// **********************************************************************
// *****************************************************************

int CJpeg::get_file_info(CString filename, CString & info)
{
   CString edit;
   CString tstr, error_msg;
   CString cr("\r\n");
   int rslt, width, height, num_bands;

   // load the file
   rslt = load(filename, width, height, error_msg);
   if (rslt != SUCCESS)
      return FAILURE;

   tstr.Format("Filename: %s", (LPCSTR)filename );
   edit = tstr;
   edit += cr;
   tstr.Format("Image Size: %dx%d", width, height);
   edit += tstr;
   edit += cr;
   tstr.Format("Bits Per Pixel: %d", 24);
   edit += tstr;
   edit += cr;
   num_bands = 3;
   tstr.Format("Num Bands: %d", num_bands);
   edit += tstr;
   edit += cr;

   // image representaion
   tstr = "Representation: RGB";
   edit += tstr;
   edit += cr;
   tstr = "Compression: JPEG";
   edit += tstr;
   edit += cr;

   info = edit;

   return SUCCESS;
}
// end of get_file_info

// *************************************************************
// **********************************************************************

/*

// * Find the next JPEG marker and return its marker code.
// * We expect at least one FF byte, possibly more if the compressor used FFs
// * to pad the file.
// * There could also be non-FF garbage between markers.  The treatment of such
// * garbage is unspecified; we choose to skip over it but emit a warning msg.
// * NB: this routine must not be used after seeing SOS marker, since it will
// * not deal correctly with FF/00 sequences in the compressed image data...
 

static int next_marker (void)
{
  int c;
  int discarded_bytes = 0;

  // Find 0xFF byte; count and skip any non-FFs. 
  c = read_1_byte();
  while (c != 0xFF) {
    discarded_bytes++;
    c = read_1_byte();
  }
  // Get marker code byte, swallowing any duplicate FF bytes.  Extra FFs
  // are legal as pad bytes, so don't count them in discarded_bytes.
   
  do {
    c = read_1_byte();
  } while (c == 0xFF);

  if (discarded_bytes != 0) {
    fprintf(stderr, "Warning: garbage data found in JPEG file\n");
  }

  return c;
}


// **********************************************************************
// **********************************************************************


// * Read the initial marker, which should be SOI.
// * For a JFIF file, the first two bytes of the file should be literally
// * 0xFF M_SOI.  To be more general, we could use next_marker, but if the
// * input file weren't actually JPEG at all, next_marker might read the whole
// * file and then return a misleading error message...

static int first_marker (void)
{
  int c1, c2;

  c1 = NEXTBYTE();
  c2 = NEXTBYTE();
  if (c1 != 0xFF || c2 != M_SOI)
    ERREXIT("Not a JPEG file");
  return c2;
}


// **********************************************************************
// **********************************************************************


// * Most types of marker are followed by a variable-length parameter segment.
// * This routine skips over the parameters for any marker we don't otherwise
// * want to process.
// * Note that we MUST skip the parameter segment explicitly in order not to
// * be fooled by 0xFF bytes that might appear within the parameter segment;
// * such bytes do NOT introduce new markers.

static void skip_variable (void)
// Skip over an unknown or uninteresting variable-length marker 
{
  unsigned int length;

  // Get the marker parameter length count 
  length = read_2_bytes();
  // Length includes itself, so must be at least 2 
  if (length < 2)
    ERREXIT("Erroneous JPEG marker length");
  length -= 2;
  
  // Skip over the remaining bytes 
  while (length > 0) 
  {
    (void) read_1_byte();
    length--;
  }
}


// **********************************************************************
// **********************************************************************

// * Process a COM marker.
// * We want to print out the marker contents as legible text;
// * we must guard against non-text junk and varying newline representations.

int CJpeg::process_COM (void)
{
  unsigned int length;
  int ch;
  int lastch = 0;

  // Get the marker parameter length count 
  length = read_2_bytes();
  // Length includes itself, so must be at least 2 
  if (length < 2)
    ERREXIT("Erroneous JPEG marker length");
  length -= 2;

  while (length > 0) {
    ch = read_1_byte();
    // Emit the character in a readable form.
    // Nonprintables are converted to \nnn form,
    // while \ is converted to \\.
    // Newlines in CR, CR/LF, or LF form will be printed as one newline.
    
    if (ch == '\r') {
      printf("\n");
    } else if (ch == '\n') {
      if (lastch != '\r')
   printf("\n");
    } else if (ch == '\\') {
      printf("\\\\");
    } else if (isprint(ch)) {
      putc(ch, stdout);
    } else {
      printf("\\%03o", ch);
    }
    lastch = ch;
    length--;
  }
  printf("\n");
}


// **********************************************************************
// **********************************************************************

// * Process a SOFn marker.
// * This code is only needed if you want to know the image dimensions...
 
int CJpeg::process_SOFn (int marker, jpeg_info_t *info, CString & error_message)
{
   unsigned int length;
   unsigned int image_height, image_width;
   int data_precision, num_components;
   const char * process;
   int ci;

   length = read_2_bytes();   //usual parameter length count 

   data_precision = read_1_byte();
   image_height = read_2_bytes();
   image_width = read_2_bytes();
   num_components = read_1_byte();

   switch (marker) 
   {
      case M_SOF0:   process = "Baseline";  break;
      case M_SOF1:   process = "Extended sequential";  break;
      case M_SOF2:   process = "Progressive";  break;
      case M_SOF3:   process = "Lossless";  break;
      case M_SOF5:   process = "Differential sequential";  break;
      case M_SOF6:   process = "Differential progressive";  break;
      case M_SOF7:   process = "Differential lossless";  break;
      case M_SOF9:   process = "Extended sequential, arithmetic coding";  break;
      case M_SOF10:  process = "Progressive, arithmetic coding";  break;
      case M_SOF11:  process = "Lossless, arithmetic coding";  break;
      case M_SOF13:  process = "Differential sequential, arithmetic coding";  break;
      case M_SOF14:  process = "Differential progressive, arithmetic coding"; break;
      case M_SOF15:  process = "Differential lossless, arithmetic coding";  break;
      default: process = "Unknown";  break;
   }

   strcpy(msg, process);

   sprintf(comp, "JPEG image is %uw * %uh, %d color components, %d bits per sample\n",
            image_width, image_height, num_components, data_precision);

   if (length != (unsigned int) (8 + num_components * 3))
   {
     strcpy(err_msg, "Bogus SOF marker length");
     return FAILURE:
   }

   for (ci = 0; ci < num_components; ci++) 
   {
    (void) read_1_byte();  // Component ID code 
    (void) read_1_byte();  // H, V sampling factors 
    (void) read_1_byte();  // Quantization table number 
   }
}


// **********************************************************************
// **********************************************************************


// Parse the marker stream until SOS or EOI is seen;
// display any COM markers.
// While the companion program wrjpgcom will always insert COM markers before
// SOFn, other implementations might not, so we scan to SOS before stopping.
// If we were only interested in the image dimensions, we would stop at SOFn.
// (Conversely, if we only cared about COM markers, there would be no need
// for special code to handle SOFn; we could treat it like other markers.)
 

int CJpeg::scan_JPEG_header (jpeg_info_t *info, CString & error_message)
{
  int marker;

  // Expect SOI at start of file 
  if (first_marker() != M_SOI)
  {
     error_message = "Expected SOI marker first";
     return FAILURE;
  }

  // Scan miscellaneous markers until we reach SOS. 
  for (;;) 
  {
    marker = next_marker();
    switch (marker) 
    {
      // Note that marker codes 0xC4, 0xC8, 0xCC are not, and must not be,
      // treated as SOFn.  C4 in particular is actually DHT.
       
       case M_SOF0:     // Baseline 
       case M_SOF1:     // Extended sequential, Huffman 
       case M_SOF2:     // Progressive, Huffman 
       case M_SOF3:     // Lossless, Huffman 
       case M_SOF5:     // Differential sequential, Huffman 
       case M_SOF6:     // Differential progressive, Huffman 
       case M_SOF7:     // Differential lossless, Huffman 
       case M_SOF9:     // Extended sequential, arithmetic 
       case M_SOF10:    // Progressive, arithmetic
       case M_SOF11:    // Lossless, arithmetic 
       case M_SOF13:    // Differential sequential, arithmetic 
       case M_SOF14:    // Differential progressive, arithmetic 
       case M_SOF15:    // Differential lossless, arithmetic 
            if (verbose)
               process_SOFn(marker);
            else
               skip_variable();
            break;

       case M_SOS:         // stop before hitting compressed data 
         return marker;

       case M_EOI:         // in case it's a tables-only JPEG stream 
         return marker;

       case M_COM:
         process_COM();
         break;

       case M_APP12:
         // Some digital camera makers put useful textual information into
         // APP12 markers, so we print those out too when in -verbose mode.
          
         if (verbose) 
         {
            printf("APP12 contains:\n");
            process_COM();
         } 
         else
            skip_variable();
         break;

      default:       // Anything else just gets skipped 
         skip_variable();     // we assume it has a parameter count... 
         break;
    }
  } // end loop 
}




// **********************************************************************
// **********************************************************************

int CJpeg::get_jpeg_info(jpeg_info_t *info, CString & error_message)
{
   if (m_filename.GetLength() < 5)
   {
      error_message.Format("Invalid Filename -- %s", m_filename);
      return FAILURE;
   }

   if ((infile = fopen(m_filename, "rb")) == NULL) 
   {
      error_message.Format("Cannot open file -- %s", m_filename);
      return FAILURE;
   }

  // Scan the JPEG headers. 
  (void) scan_JPEG_header(verbose);


}



// **********************************************************************
// **********************************************************************

*/