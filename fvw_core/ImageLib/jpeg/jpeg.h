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

// jpeg.h

#ifndef JPEG_H
#define JPEG_H

#ifndef _WIN32
#include "fv_compat.h"
#include "fv_cstring.h"
#endif
#include "jpeglib.h"
#include "jinclude.h"

// * JPEG markers consist of one or more 0xFF bytes, followed by a marker
// * code byte (which is not an FF).  Here are the marker codes of interest
// * in this program.  (See jdmarker.c for a more complete list.)

#define M_SOF0  0xC0    // Start Of Frame N 
#define M_SOF1  0xC1    // N indicates which compression process 
#define M_SOF2  0xC2    // Only SOF0-SOF2 are now in common use 
#define M_SOF3  0xC3
#define M_SOF5  0xC5    // NB: codes C4 and CC are NOT SOF markers 
#define M_SOF6  0xC6
#define M_SOF7  0xC7
#define M_SOF9  0xC9
#define M_SOF10 0xCA
#define M_SOF11 0xCB
#define M_SOF13 0xCD
#define M_SOF14 0xCE
#define M_SOF15 0xCF
#define M_SOI   0xD8    // Start Of Image (beginning of datastream) 
#define M_EOI   0xD9    // End Of Image (end of datastream) 
#define M_SOS   0xDA    // Start Of Scan (begins compressed data) 
#define M_APP0  0xE0    // Application-specific marker, type N 
#define M_APP12 0xEC // (we don't bother to list all 16 APPn's) 
#define M_COM   0xFE    // COMment 

#define WIDTHBYTES(bits)    (((bits) + 31) / 32 * 4)

#define IS_WIN30_DIB(lpbi)  ((*(LPDWORD)(lpbi)) == sizeof(BITMAPINFOHEADER))
#define PALVERSION   0x300

typedef struct
{
   BYTE data_precision;
   int  image_height;
   int  image_width;      
} jpeg_info_t;


class CJpeg
{
public:
   CJpeg();
   ~CJpeg();

   CString m_filename;
   int m_height;
   int m_width;
   int m_num_components;
   int m_jpeg_color_space;
   int m_out_color_space;

   int m_crypt_pos;

   int m_clear_r, m_clear_g, m_clear_b;
// struct jpeg_compress_struct m_cinfo;

   BOOL m_grayscale;

   BOOL m_encrypt;

   // JPEGTables
   // NOTE (port 2026-07-15): was std::auto_ptr<BYTE> (removed in C++17). It
   // holds a BYTE[] (see reset(new BYTE[...])), so unique_ptr<BYTE[]> is both
   // the modern replacement and a latent-bug fix (auto_ptr called scalar
   // delete on array memory). Decoded output is unchanged.
   std::unique_ptr< BYTE[] > m_apbJpegTable;
   BYTE* m_jpeg_table;
   int m_jpeg_table_len;

   BOOL set_grayscale(struct jpeg_compress_struct * cinfo, BOOL gray);
   BOOL is_grayscale();

   int set_clear_color(int r, int g, int b);
   int load(CString filename, int &image_width, int &image_height, CString &error_message);
   int load_file(FILE *infile, int &image_width, int &image_height, CString &error_message);
   int get_jpeg_image(int image_offset_x, int image_offset_y, 
                              int width, int height,
                              unsigned char *red_array, 
                              unsigned char *green_array,   
                              unsigned char *blue_array,
                              CString &error_message);

   // read a multispectral (4 bands) jpeg image
   int get_ms_jpeg_image(CString filename, int image_offset_x, int image_offset_y, 
                     int width, int height,
                     unsigned char *img, 
                     CString &error_message );

   int get_ms_stream_jpeg_image(FILE *infile, int image_offset_x, int image_offset_y, 
                        int width, int height,
                        BYTE *img, 
                        CString &error_message );

   int write_ms_jpeg_file( CString filename, int width, int height, BYTE* img, 
                     int quality,   // valid values 0 (terrible) to 100 (very good)
                     CString &error_message );


   int get_filled_rgb_subimage( double factor, const int ulx, const int uly, const int lrx, const int lry, 
                           BYTE *red_buffer, BYTE *grn_buffer, BYTE *blu_buffer,
                           CString &error_message );

   BOOL get_tif_dht (j_decompress_ptr cinfo, int & ptr);
   BOOL get_tif_dqt (j_decompress_ptr cinfo, int & ptr);

   void set_jpeg_tables(BYTE *tables, int count);

   int get_jpeg_bitstream_image(FILE *infile,
                           int image_offset_x, int image_offset_y, 
                           int & width, int & height,
                           BOOL rgb_colorspace,
                           unsigned char *red_array, 
                           unsigned char *green_array,   
                           unsigned char *blue_array,
                           CString &error_message, int default_comrat = 0 );

   int get_supersampled_rgb_image( double factor,  // the degree of oversampling
                           int image_offset_x, int image_offset_y, 
                           int width, int height,
                           unsigned char *red_array, 
                           unsigned char *green_array,   
                           unsigned char *blue_array,
                           CString &error_message );

   int get_subsampled_rgb_image( int sampling,
                           int image_offset_x, int image_offset_y, 
                           int width, int height,
                           unsigned char *red_array, 
                           unsigned char *green_array,   
                           unsigned char *blue_array,
                           CString &error_message );

   int open(CString filename, CString & error_msg);

   int write_jpeg_file( CString filename,  BYTE* hdib,
                           int quality, CString &error_message );

   int write_jpeg_file( CString filename, int width, int height, BYTE* img, 
                  int quality,   // valid values 0 (terrible) to 100 (very good)
                  CString &error_message );

   int open_jpeg_file_for_write( CString filename, int width, int height, 
                           int quality,   // valid values 0 (terrible) to 100 (very good)
                           struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                           FILE **outfile,  // file pointer of jpeg file
                           CString &error_message );

   int write_jpeg_line( BYTE* red, BYTE* grn, BYTE* blu, 
                     struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                     FILE *outfile,
                     CString &error_message );

   int write_jpeg_line_1( BYTE* line_buf,  
                     struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                     CString &error_message );

   int write_jpeg_lines( BYTE* line_buf, int num_lines,
                     struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                     FILE *outfile,
                     CString &error_message );

   void close_jpeg_write(struct jpeg_compress_struct * cinfo,  // structure required for writing lines
                     FILE *outfile);

   int check_jpeg_file(CString filename, CString & error_msg);
   int check_jpeg_file(FILE *f, CString & error_msg);
   int get_histogram(FILE *infile, unsigned int *hist, unsigned int *freq_red, 
                  unsigned int *freq_grn, unsigned int *freq_blu, CString &error_message );
   void get_default_huff_tables (j_decompress_ptr cinfo);

   int get_file_info(CString filename, CString & info);



};

#endif   // ifndef JPEG_H
