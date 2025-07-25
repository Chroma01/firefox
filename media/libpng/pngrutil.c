/* pngrutil.c - utilities to read a PNG file
 *
 * Copyright (c) 2018-2025 Cosmin Truta
 * Copyright (c) 1998-2002,2004,2006-2018 Glenn Randers-Pehrson
 * Copyright (c) 1996-1997 Andreas Dilger
 * Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 *
 * This file contains routines that are only called from within
 * libpng itself during the course of reading an image.
 */

#include "pngpriv.h"

#ifdef PNG_READ_SUPPORTED

/* The minimum 'zlib' stream is assumed to be just the 2 byte header, 5 bytes
 * minimum 'deflate' stream, and the 4 byte checksum.
 */
#define LZ77Min  (2U+5U+4U)

#ifdef PNG_READ_INTERLACING_SUPPORTED
/* Arrays to facilitate interlacing - use pass (0 - 6) as index. */

/* Start of interlace block */
static const png_byte png_pass_start[7] = {0, 4, 0, 2, 0, 1, 0};
/* Offset to next interlace block */
static const png_byte png_pass_inc[7] = {8, 8, 4, 4, 2, 2, 1};
/* Start of interlace block in the y direction */
static const png_byte png_pass_ystart[7] = {0, 0, 4, 0, 2, 0, 1};
/* Offset to next interlace block in the y direction */
static const png_byte png_pass_yinc[7] = {8, 8, 8, 4, 4, 2, 2};

/* TODO: Move these arrays to a common utility module to avoid duplication. */
#endif

png_uint_32 PNGAPI
png_get_uint_31(png_const_structrp png_ptr, png_const_bytep buf)
{
   png_uint_32 uval = png_get_uint_32(buf);

   if (uval > PNG_UINT_31_MAX)
      png_error(png_ptr, "PNG unsigned integer out of range");

   return uval;
}

#ifdef PNG_READ_INT_FUNCTIONS_SUPPORTED
/* NOTE: the read macros will obscure these definitions, so that if
 * PNG_USE_READ_MACROS is set the library will not use them internally,
 * but the APIs will still be available externally.
 *
 * The parentheses around "PNGAPI function_name" in the following three
 * functions are necessary because they allow the macros to co-exist with
 * these (unused but exported) functions.
 */

/* Grab an unsigned 32-bit integer from a buffer in big-endian format. */
png_uint_32 (PNGAPI
png_get_uint_32)(png_const_bytep buf)
{
   png_uint_32 uval =
       ((png_uint_32)(*(buf    )) << 24) +
       ((png_uint_32)(*(buf + 1)) << 16) +
       ((png_uint_32)(*(buf + 2)) <<  8) +
       ((png_uint_32)(*(buf + 3))      ) ;

   return uval;
}

/* Grab a signed 32-bit integer from a buffer in big-endian format.  The
 * data is stored in the PNG file in two's complement format and there
 * is no guarantee that a 'png_int_32' is exactly 32 bits, therefore
 * the following code does a two's complement to native conversion.
 */
png_int_32 (PNGAPI
png_get_int_32)(png_const_bytep buf)
{
   png_uint_32 uval = png_get_uint_32(buf);
   if ((uval & 0x80000000) == 0) /* non-negative */
      return (png_int_32)uval;

   uval = (uval ^ 0xffffffff) + 1;  /* 2's complement: -x = ~x+1 */
   if ((uval & 0x80000000) == 0) /* no overflow */
      return -(png_int_32)uval;
   /* The following has to be safe; this function only gets called on PNG data
    * and if we get here that data is invalid.  0 is the most safe value and
    * if not then an attacker would surely just generate a PNG with 0 instead.
    */
   return 0;
}

/* Grab an unsigned 16-bit integer from a buffer in big-endian format. */
png_uint_16 (PNGAPI
png_get_uint_16)(png_const_bytep buf)
{
   /* ANSI-C requires an int value to accommodate at least 16 bits so this
    * works and allows the compiler not to worry about possible narrowing
    * on 32-bit systems.  (Pre-ANSI systems did not make integers smaller
    * than 16 bits either.)
    */
   unsigned int val =
       ((unsigned int)(*buf) << 8) +
       ((unsigned int)(*(buf + 1)));

   return (png_uint_16)val;
}

#endif /* READ_INT_FUNCTIONS */

/* Read and check the PNG file signature */
void /* PRIVATE */
png_read_sig(png_structrp png_ptr, png_inforp info_ptr)
{
   size_t num_checked, num_to_check;

   /* Exit if the user application does not expect a signature. */
   if (png_ptr->sig_bytes >= 8)
      return;

   num_checked = png_ptr->sig_bytes;
   num_to_check = 8 - num_checked;

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_SIGNATURE;
#endif

   /* The signature must be serialized in a single I/O call. */
   png_read_data(png_ptr, &(info_ptr->signature[num_checked]), num_to_check);
   png_ptr->sig_bytes = 8;

   if (png_sig_cmp(info_ptr->signature, num_checked, num_to_check) != 0)
   {
      if (num_checked < 4 &&
          png_sig_cmp(info_ptr->signature, num_checked, num_to_check - 4) != 0)
         png_error(png_ptr, "Not a PNG file");
      else
         png_error(png_ptr, "PNG file corrupted by ASCII conversion");
   }
   if (num_checked < 3)
      png_ptr->mode |= PNG_HAVE_PNG_SIGNATURE;
}

/* This function is called to verify that a chunk name is valid.
 * Do this using the bit-whacking approach from contrib/tools/pngfix.c
 *
 * Copied from libpng 1.7.
 */
static int
check_chunk_name(png_uint_32 name)
{
   png_uint_32 t;

   /* Remove bit 5 from all but the reserved byte; this means
    * every 8-bit unit must be in the range 65-90 to be valid.
    * So bit 5 must be zero, bit 6 must be set and bit 7 zero.
    */
   name &= ~PNG_U32(32,32,0,32);
   t = (name & ~0x1f1f1f1fU) ^ 0x40404040U;

   /* Subtract 65 for each 8-bit quantity, this must not
    * overflow and each byte must then be in the range 0-25.
    */
   name -= PNG_U32(65,65,65,65);
   t |= name;

   /* Subtract 26, handling the overflow which should set the
    * top three bits of each byte.
    */
   name -= PNG_U32(25,25,25,26);
   t |= ~name;

   return (t & 0xe0e0e0e0U) == 0U;
}

/* Read the chunk header (length + type name).
 * Put the type name into png_ptr->chunk_name, and return the length.
 */
png_uint_32 /* PRIVATE */
png_read_chunk_header(png_structrp png_ptr)
{
   png_byte buf[8];
   png_uint_32 chunk_name, length;

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_HDR;
#endif

   /* Read the length and the chunk name.  png_struct::chunk_name is immediately
    * updated even if they are detectably wrong.  This aids error message
    * handling by allowing png_chunk_error to be used.
    */
   png_read_data(png_ptr, buf, 8);
   length = png_get_uint_31(png_ptr, buf);
   png_ptr->chunk_name = chunk_name = PNG_CHUNK_FROM_STRING(buf+4);

   /* Reset the crc and run it over the chunk name. */
   png_reset_crc(png_ptr);
   png_calculate_crc(png_ptr, buf + 4, 4);

   png_debug2(0, "Reading chunk typeid = 0x%lx, length = %lu",
       (unsigned long)png_ptr->chunk_name, (unsigned long)length);

   /* Sanity check the length (first by <= 0x80) and the chunk name.  An error
    * here indicates a broken stream and libpng has no recovery from this.
    */
   if (buf[0] >= 0x80U)
      png_chunk_error(png_ptr, "bad header (invalid length)");

   /* Check to see if chunk name is valid. */
   if (!check_chunk_name(chunk_name))
      png_chunk_error(png_ptr, "bad header (invalid type)");

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_DATA;
#endif

   return length;
}

/* Read data, and (optionally) run it through the CRC. */
void /* PRIVATE */
png_crc_read(png_structrp png_ptr, png_bytep buf, png_uint_32 length)
{
   if (png_ptr == NULL)
      return;

   png_read_data(png_ptr, buf, length);
   png_calculate_crc(png_ptr, buf, length);
}

/* Compare the CRC stored in the PNG file with that calculated by libpng from
 * the data it has read thus far.
 */
static int
png_crc_error(png_structrp png_ptr, int handle_as_ancillary)
{
   png_byte crc_bytes[4];
   png_uint_32 crc;
   int need_crc = 1;

   /* There are four flags two for ancillary and two for critical chunks.  The
    * default setting of these flags is all zero.
    *
    * PNG_FLAG_CRC_ANCILLARY_USE
    * PNG_FLAG_CRC_ANCILLARY_NOWARN
    *  USE+NOWARN: no CRC calculation (implemented here), else;
    *  NOWARN:     png_chunk_error on error (implemented in png_crc_finish)
    *  else:       png_chunk_warning on error (implemented in png_crc_finish)
    *              This is the default.
    *
    *    I.e. NOWARN without USE produces png_chunk_error.  The default setting
    *    where neither are set does the same thing.
    *
    * PNG_FLAG_CRC_CRITICAL_USE
    * PNG_FLAG_CRC_CRITICAL_IGNORE
    *  IGNORE: no CRC calculation (implemented here), else;
    *  USE:    png_chunk_warning on error (implemented in png_crc_finish)
    *  else:   png_chunk_error on error (implemented in png_crc_finish)
    *          This is the default.
    *
    * This arose because of original mis-implementation and has persisted for
    * compatibility reasons.
    *
    * TODO: the flag names are internal so maybe this can be changed to
    * something comprehensible.
    */
   if (handle_as_ancillary || PNG_CHUNK_ANCILLARY(png_ptr->chunk_name) != 0)
   {
      if ((png_ptr->flags & PNG_FLAG_CRC_ANCILLARY_MASK) ==
          (PNG_FLAG_CRC_ANCILLARY_USE | PNG_FLAG_CRC_ANCILLARY_NOWARN))
         need_crc = 0;
   }

   else /* critical */
   {
      if ((png_ptr->flags & PNG_FLAG_CRC_CRITICAL_IGNORE) != 0)
         need_crc = 0;
   }

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_CRC;
#endif

   /* The chunk CRC must be serialized in a single I/O call. */
   png_read_data(png_ptr, crc_bytes, 4);

   if (need_crc != 0)
   {
      crc = png_get_uint_32(crc_bytes);
      return crc != png_ptr->crc;
   }

   else
      return 0;
}

/* Optionally skip data and then check the CRC.  Depending on whether we
 * are reading an ancillary or critical chunk, and how the program has set
 * things up, we may calculate the CRC on the data and print a message.
 * Returns '1' if there was a CRC error, '0' otherwise.
 *
 * There is one public version which is used in most places and another which
 * takes the value for the 'critical' flag to check.  This allows PLTE and IEND
 * handling code to ignore the CRC error and removes some confusing code
 * duplication.
 */
static int
png_crc_finish_critical(png_structrp png_ptr, png_uint_32 skip,
      int handle_as_ancillary)
{
   /* The size of the local buffer for inflate is a good guess as to a
    * reasonable size to use for buffering reads from the application.
    */
   while (skip > 0)
   {
      png_uint_32 len;
      png_byte tmpbuf[PNG_INFLATE_BUF_SIZE];

      len = (sizeof tmpbuf);
      if (len > skip)
         len = skip;
      skip -= len;

      png_crc_read(png_ptr, tmpbuf, len);
   }

   /* If 'handle_as_ancillary' has been requested and this is a critical chunk
    * but PNG_FLAG_CRC_CRITICAL_IGNORE was set then png_read_crc did not, in
    * fact, calculate the CRC so the ANCILLARY settings should not be used
    * instead.
    */
   if (handle_as_ancillary &&
       (png_ptr->flags & PNG_FLAG_CRC_CRITICAL_IGNORE) != 0)
      handle_as_ancillary = 0;

   /* TODO: this might be more comprehensible if png_crc_error was inlined here.
    */
   if (png_crc_error(png_ptr, handle_as_ancillary) != 0)
   {
      /* See above for the explanation of how the flags work. */
      if (handle_as_ancillary || PNG_CHUNK_ANCILLARY(png_ptr->chunk_name) != 0 ?
          (png_ptr->flags & PNG_FLAG_CRC_ANCILLARY_NOWARN) == 0 :
          (png_ptr->flags & PNG_FLAG_CRC_CRITICAL_USE) != 0)
         png_chunk_warning(png_ptr, "CRC error");

      else
         png_chunk_error(png_ptr, "CRC error");

      return 1;
   }

   return 0;
}

int /* PRIVATE */
png_crc_finish(png_structrp png_ptr, png_uint_32 skip)
{
   return png_crc_finish_critical(png_ptr, skip, 0/*critical handling*/);
}

#if defined(PNG_READ_iCCP_SUPPORTED) || defined(PNG_READ_iTXt_SUPPORTED) ||\
    defined(PNG_READ_pCAL_SUPPORTED) || defined(PNG_READ_sCAL_SUPPORTED) ||\
    defined(PNG_READ_sPLT_SUPPORTED) || defined(PNG_READ_tEXt_SUPPORTED) ||\
    defined(PNG_READ_zTXt_SUPPORTED) || defined(PNG_READ_eXIf_SUPPORTED) ||\
    defined(PNG_SEQUENTIAL_READ_SUPPORTED)
/* Manage the read buffer; this simply reallocates the buffer if it is not small
 * enough (or if it is not allocated).  The routine returns a pointer to the
 * buffer; if an error occurs and 'warn' is set the routine returns NULL, else
 * it will call png_error on failure.
 */
static png_bytep
png_read_buffer(png_structrp png_ptr, png_alloc_size_t new_size)
{
   png_bytep buffer = png_ptr->read_buffer;

   if (new_size > png_chunk_max(png_ptr)) return NULL;

   if (buffer != NULL && new_size > png_ptr->read_buffer_size)
   {
      png_ptr->read_buffer = NULL;
      png_ptr->read_buffer_size = 0;
      png_free(png_ptr, buffer);
      buffer = NULL;
   }

   if (buffer == NULL)
   {
      buffer = png_voidcast(png_bytep, png_malloc_base(png_ptr, new_size));

      if (buffer != NULL)
      {
#        ifndef PNG_NO_MEMZERO /* for detecting UIM bugs **only** */
            memset(buffer, 0, new_size); /* just in case */
#        endif
         png_ptr->read_buffer = buffer;
         png_ptr->read_buffer_size = new_size;
      }
   }

   return buffer;
}
#endif /* READ_iCCP|iTXt|pCAL|sCAL|sPLT|tEXt|zTXt|eXIf|SEQUENTIAL_READ */

/* png_inflate_claim: claim the zstream for some nefarious purpose that involves
 * decompression.  Returns Z_OK on success, else a zlib error code.  It checks
 * the owner but, in final release builds, just issues a warning if some other
 * chunk apparently owns the stream.  Prior to release it does a png_error.
 */
static int
png_inflate_claim(png_structrp png_ptr, png_uint_32 owner)
{
   if (png_ptr->zowner != 0)
   {
      char msg[64];

      PNG_STRING_FROM_CHUNK(msg, png_ptr->zowner);
      /* So the message that results is "<chunk> using zstream"; this is an
       * internal error, but is very useful for debugging.  i18n requirements
       * are minimal.
       */
      (void)png_safecat(msg, (sizeof msg), 4, " using zstream");
#if PNG_RELEASE_BUILD
      png_chunk_warning(png_ptr, msg);
      png_ptr->zowner = 0;
#else
      png_chunk_error(png_ptr, msg);
#endif
   }

   /* Implementation note: unlike 'png_deflate_claim' this internal function
    * does not take the size of the data as an argument.  Some efficiency could
    * be gained by using this when it is known *if* the zlib stream itself does
    * not record the number; however, this is an illusion: the original writer
    * of the PNG may have selected a lower window size, and we really must
    * follow that because, for systems with with limited capabilities, we
    * would otherwise reject the application's attempts to use a smaller window
    * size (zlib doesn't have an interface to say "this or lower"!).
    *
    * inflateReset2 was added to zlib 1.2.4; before this the window could not be
    * reset, therefore it is necessary to always allocate the maximum window
    * size with earlier zlibs just in case later compressed chunks need it.
    */
   {
      int ret; /* zlib return code */
#if ZLIB_VERNUM >= 0x1240
      int window_bits = 0;

# if defined(PNG_SET_OPTION_SUPPORTED) && defined(PNG_MAXIMUM_INFLATE_WINDOW)
      if (((png_ptr->options >> PNG_MAXIMUM_INFLATE_WINDOW) & 3) ==
          PNG_OPTION_ON)
      {
         window_bits = 15;
         png_ptr->zstream_start = 0; /* fixed window size */
      }

      else
      {
         png_ptr->zstream_start = 1;
      }
# endif

#endif /* ZLIB_VERNUM >= 0x1240 */

      /* Set this for safety, just in case the previous owner left pointers to
       * memory allocations.
       */
      png_ptr->zstream.next_in = NULL;
      png_ptr->zstream.avail_in = 0;
      png_ptr->zstream.next_out = NULL;
      png_ptr->zstream.avail_out = 0;

      if ((png_ptr->flags & PNG_FLAG_ZSTREAM_INITIALIZED) != 0)
      {
#if ZLIB_VERNUM >= 0x1240
         ret = inflateReset2(&png_ptr->zstream, window_bits);
#else
         ret = inflateReset(&png_ptr->zstream);
#endif
      }

      else
      {
#if ZLIB_VERNUM >= 0x1240
         ret = inflateInit2(&png_ptr->zstream, window_bits);
#else
         ret = inflateInit(&png_ptr->zstream);
#endif

         if (ret == Z_OK)
            png_ptr->flags |= PNG_FLAG_ZSTREAM_INITIALIZED;
      }

#ifdef PNG_DISABLE_ADLER32_CHECK_SUPPORTED
      if (((png_ptr->options >> PNG_IGNORE_ADLER32) & 3) == PNG_OPTION_ON)
         /* Turn off validation of the ADLER32 checksum in IDAT chunks */
         ret = inflateValidate(&png_ptr->zstream, 0);
#endif

      if (ret == Z_OK)
         png_ptr->zowner = owner;

      else
         png_zstream_error(png_ptr, ret);

      return ret;
   }

#ifdef window_bits
# undef window_bits
#endif
}

#if ZLIB_VERNUM >= 0x1240
/* Handle the start of the inflate stream if we called inflateInit2(strm,0);
 * in this case some zlib versions skip validation of the CINFO field and, in
 * certain circumstances, libpng may end up displaying an invalid image, in
 * contrast to implementations that call zlib in the normal way (e.g. libpng
 * 1.5).
 */
int /* PRIVATE */
png_zlib_inflate(png_structrp png_ptr, int flush)
{
   if (png_ptr->zstream_start && png_ptr->zstream.avail_in > 0)
   {
      if ((*png_ptr->zstream.next_in >> 4) > 7)
      {
         png_ptr->zstream.msg = "invalid window size (libpng)";
         return Z_DATA_ERROR;
      }

      png_ptr->zstream_start = 0;
   }

   return inflate(&png_ptr->zstream, flush);
}
#endif /* Zlib >= 1.2.4 */

#ifdef PNG_READ_COMPRESSED_TEXT_SUPPORTED
#if defined(PNG_READ_zTXt_SUPPORTED) || defined (PNG_READ_iTXt_SUPPORTED)
/* png_inflate now returns zlib error codes including Z_OK and Z_STREAM_END to
 * allow the caller to do multiple calls if required.  If the 'finish' flag is
 * set Z_FINISH will be passed to the final inflate() call and Z_STREAM_END must
 * be returned or there has been a problem, otherwise Z_SYNC_FLUSH is used and
 * Z_OK or Z_STREAM_END will be returned on success.
 *
 * The input and output sizes are updated to the actual amounts of data consumed
 * or written, not the amount available (as in a z_stream).  The data pointers
 * are not changed, so the next input is (data+input_size) and the next
 * available output is (output+output_size).
 */
static int
png_inflate(png_structrp png_ptr, png_uint_32 owner, int finish,
    /* INPUT: */ png_const_bytep input, png_uint_32p input_size_ptr,
    /* OUTPUT: */ png_bytep output, png_alloc_size_t *output_size_ptr)
{
   if (png_ptr->zowner == owner) /* Else not claimed */
   {
      int ret;
      png_alloc_size_t avail_out = *output_size_ptr;
      png_uint_32 avail_in = *input_size_ptr;

      /* zlib can't necessarily handle more than 65535 bytes at once (i.e. it
       * can't even necessarily handle 65536 bytes) because the type uInt is
       * "16 bits or more".  Consequently it is necessary to chunk the input to
       * zlib.  This code uses ZLIB_IO_MAX, from pngpriv.h, as the maximum (the
       * maximum value that can be stored in a uInt.)  It is possible to set
       * ZLIB_IO_MAX to a lower value in pngpriv.h and this may sometimes have
       * a performance advantage, because it reduces the amount of data accessed
       * at each step and that may give the OS more time to page it in.
       */
      png_ptr->zstream.next_in = PNGZ_INPUT_CAST(input);
      /* avail_in and avail_out are set below from 'size' */
      png_ptr->zstream.avail_in = 0;
      png_ptr->zstream.avail_out = 0;

      /* Read directly into the output if it is available (this is set to
       * a local buffer below if output is NULL).
       */
      if (output != NULL)
         png_ptr->zstream.next_out = output;

      do
      {
         uInt avail;
         Byte local_buffer[PNG_INFLATE_BUF_SIZE];

         /* zlib INPUT BUFFER */
         /* The setting of 'avail_in' used to be outside the loop; by setting it
          * inside it is possible to chunk the input to zlib and simply rely on
          * zlib to advance the 'next_in' pointer.  This allows arbitrary
          * amounts of data to be passed through zlib at the unavoidable cost of
          * requiring a window save (memcpy of up to 32768 output bytes)
          * every ZLIB_IO_MAX input bytes.
          */
         avail_in += png_ptr->zstream.avail_in; /* not consumed last time */

         avail = ZLIB_IO_MAX;

         if (avail_in < avail)
            avail = (uInt)avail_in; /* safe: < than ZLIB_IO_MAX */

         avail_in -= avail;
         png_ptr->zstream.avail_in = avail;

         /* zlib OUTPUT BUFFER */
         avail_out += png_ptr->zstream.avail_out; /* not written last time */

         avail = ZLIB_IO_MAX; /* maximum zlib can process */

         if (output == NULL)
         {
            /* Reset the output buffer each time round if output is NULL and
             * make available the full buffer, up to 'remaining_space'
             */
            png_ptr->zstream.next_out = local_buffer;
            if ((sizeof local_buffer) < avail)
               avail = (sizeof local_buffer);
         }

         if (avail_out < avail)
            avail = (uInt)avail_out; /* safe: < ZLIB_IO_MAX */

         png_ptr->zstream.avail_out = avail;
         avail_out -= avail;

         /* zlib inflate call */
         /* In fact 'avail_out' may be 0 at this point, that happens at the end
          * of the read when the final LZ end code was not passed at the end of
          * the previous chunk of input data.  Tell zlib if we have reached the
          * end of the output buffer.
          */
         ret = PNG_INFLATE(png_ptr, avail_out > 0 ? Z_NO_FLUSH :
             (finish ? Z_FINISH : Z_SYNC_FLUSH));
      } while (ret == Z_OK);

      /* For safety kill the local buffer pointer now */
      if (output == NULL)
         png_ptr->zstream.next_out = NULL;

      /* Claw back the 'size' and 'remaining_space' byte counts. */
      avail_in += png_ptr->zstream.avail_in;
      avail_out += png_ptr->zstream.avail_out;

      /* Update the input and output sizes; the updated values are the amount
       * consumed or written, effectively the inverse of what zlib uses.
       */
      if (avail_out > 0)
         *output_size_ptr -= avail_out;

      if (avail_in > 0)
         *input_size_ptr -= avail_in;

      /* Ensure png_ptr->zstream.msg is set (even in the success case!) */
      png_zstream_error(png_ptr, ret);
      return ret;
   }

   else
   {
      /* This is a bad internal error.  The recovery assigns to the zstream msg
       * pointer, which is not owned by the caller, but this is safe; it's only
       * used on errors!
       */
      png_ptr->zstream.msg = PNGZ_MSG_CAST("zstream unclaimed");
      return Z_STREAM_ERROR;
   }
}

/*
 * Decompress trailing data in a chunk.  The assumption is that read_buffer
 * points at an allocated area holding the contents of a chunk with a
 * trailing compressed part.  What we get back is an allocated area
 * holding the original prefix part and an uncompressed version of the
 * trailing part (the malloc area passed in is freed).
 */
static int
png_decompress_chunk(png_structrp png_ptr,
    png_uint_32 chunklength, png_uint_32 prefix_size,
    png_alloc_size_t *newlength /* must be initialized to the maximum! */,
    int terminate /*add a '\0' to the end of the uncompressed data*/)
{
   /* TODO: implement different limits for different types of chunk.
    *
    * The caller supplies *newlength set to the maximum length of the
    * uncompressed data, but this routine allocates space for the prefix and
    * maybe a '\0' terminator too.  We have to assume that 'prefix_size' is
    * limited only by the maximum chunk size.
    */
   png_alloc_size_t limit = png_chunk_max(png_ptr);

   if (limit >= prefix_size + (terminate != 0))
   {
      int ret;

      limit -= prefix_size + (terminate != 0);

      if (limit < *newlength)
         *newlength = limit;

      /* Now try to claim the stream. */
      ret = png_inflate_claim(png_ptr, png_ptr->chunk_name);

      if (ret == Z_OK)
      {
         png_uint_32 lzsize = chunklength - prefix_size;

         ret = png_inflate(png_ptr, png_ptr->chunk_name, 1/*finish*/,
             /* input: */ png_ptr->read_buffer + prefix_size, &lzsize,
             /* output: */ NULL, newlength);

         if (ret == Z_STREAM_END)
         {
            /* Use 'inflateReset' here, not 'inflateReset2' because this
             * preserves the previously decided window size (otherwise it would
             * be necessary to store the previous window size.)  In practice
             * this doesn't matter anyway, because png_inflate will call inflate
             * with Z_FINISH in almost all cases, so the window will not be
             * maintained.
             */
            if (inflateReset(&png_ptr->zstream) == Z_OK)
            {
               /* Because of the limit checks above we know that the new,
                * expanded, size will fit in a size_t (let alone an
                * png_alloc_size_t).  Use png_malloc_base here to avoid an
                * extra OOM message.
                */
               png_alloc_size_t new_size = *newlength;
               png_alloc_size_t buffer_size = prefix_size + new_size +
                   (terminate != 0);
               png_bytep text = png_voidcast(png_bytep, png_malloc_base(png_ptr,
                   buffer_size));

               if (text != NULL)
               {
                  memset(text, 0, buffer_size);

                  ret = png_inflate(png_ptr, png_ptr->chunk_name, 1/*finish*/,
                      png_ptr->read_buffer + prefix_size, &lzsize,
                      text + prefix_size, newlength);

                  if (ret == Z_STREAM_END)
                  {
                     if (new_size == *newlength)
                     {
                        if (terminate != 0)
                           text[prefix_size + *newlength] = 0;

                        if (prefix_size > 0)
                           memcpy(text, png_ptr->read_buffer, prefix_size);

                        {
                           png_bytep old_ptr = png_ptr->read_buffer;

                           png_ptr->read_buffer = text;
                           png_ptr->read_buffer_size = buffer_size;
                           text = old_ptr; /* freed below */
                        }
                     }

                     else
                     {
                        /* The size changed on the second read, there can be no
                         * guarantee that anything is correct at this point.
                         * The 'msg' pointer has been set to "unexpected end of
                         * LZ stream", which is fine, but return an error code
                         * that the caller won't accept.
                         */
                        ret = PNG_UNEXPECTED_ZLIB_RETURN;
                     }
                  }

                  else if (ret == Z_OK)
                     ret = PNG_UNEXPECTED_ZLIB_RETURN; /* for safety */

                  /* Free the text pointer (this is the old read_buffer on
                   * success)
                   */
                  png_free(png_ptr, text);

                  /* This really is very benign, but it's still an error because
                   * the extra space may otherwise be used as a Trojan Horse.
                   */
                  if (ret == Z_STREAM_END &&
                      chunklength - prefix_size != lzsize)
                     png_chunk_benign_error(png_ptr, "extra compressed data");
               }

               else
               {
                  /* Out of memory allocating the buffer */
                  ret = Z_MEM_ERROR;
                  png_zstream_error(png_ptr, Z_MEM_ERROR);
               }
            }

            else
            {
               /* inflateReset failed, store the error message */
               png_zstream_error(png_ptr, ret);
               ret = PNG_UNEXPECTED_ZLIB_RETURN;
            }
         }

         else if (ret == Z_OK)
            ret = PNG_UNEXPECTED_ZLIB_RETURN;

         /* Release the claimed stream */
         png_ptr->zowner = 0;
      }

      else /* the claim failed */ if (ret == Z_STREAM_END) /* impossible! */
         ret = PNG_UNEXPECTED_ZLIB_RETURN;

      return ret;
   }

   else
   {
      /* Application/configuration limits exceeded */
      png_zstream_error(png_ptr, Z_MEM_ERROR);
      return Z_MEM_ERROR;
   }
}
#endif /* READ_zTXt || READ_iTXt */
#endif /* READ_COMPRESSED_TEXT */

#ifdef PNG_READ_iCCP_SUPPORTED
/* Perform a partial read and decompress, producing 'avail_out' bytes and
 * reading from the current chunk as required.
 */
static int
png_inflate_read(png_structrp png_ptr, png_bytep read_buffer, uInt read_size,
    png_uint_32p chunk_bytes, png_bytep next_out, png_alloc_size_t *out_size,
    int finish)
{
   if (png_ptr->zowner == png_ptr->chunk_name)
   {
      int ret;

      /* next_in and avail_in must have been initialized by the caller. */
      png_ptr->zstream.next_out = next_out;
      png_ptr->zstream.avail_out = 0; /* set in the loop */

      do
      {
         if (png_ptr->zstream.avail_in == 0)
         {
            if (read_size > *chunk_bytes)
               read_size = (uInt)*chunk_bytes;
            *chunk_bytes -= read_size;

            if (read_size > 0)
               png_crc_read(png_ptr, read_buffer, read_size);

            png_ptr->zstream.next_in = read_buffer;
            png_ptr->zstream.avail_in = read_size;
         }

         if (png_ptr->zstream.avail_out == 0)
         {
            uInt avail = ZLIB_IO_MAX;
            if (avail > *out_size)
               avail = (uInt)*out_size;
            *out_size -= avail;

            png_ptr->zstream.avail_out = avail;
         }

         /* Use Z_SYNC_FLUSH when there is no more chunk data to ensure that all
          * the available output is produced; this allows reading of truncated
          * streams.
          */
         ret = PNG_INFLATE(png_ptr, *chunk_bytes > 0 ?
             Z_NO_FLUSH : (finish ? Z_FINISH : Z_SYNC_FLUSH));
      }
      while (ret == Z_OK && (*out_size > 0 || png_ptr->zstream.avail_out > 0));

      *out_size += png_ptr->zstream.avail_out;
      png_ptr->zstream.avail_out = 0; /* Should not be required, but is safe */

      /* Ensure the error message pointer is always set: */
      png_zstream_error(png_ptr, ret);
      return ret;
   }

   else
   {
      png_ptr->zstream.msg = PNGZ_MSG_CAST("zstream unclaimed");
      return Z_STREAM_ERROR;
   }
}
#endif /* READ_iCCP */

/* CHUNK HANDLING */
/* Read and check the IDHR chunk */
static png_handle_result_code
png_handle_IHDR(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[13];
   png_uint_32 width, height;
   int bit_depth, color_type, compression_type, filter_type;
   int interlace_type;

   png_debug(1, "in png_handle_IHDR");

   /* Length and position are checked by the caller. */

   png_ptr->mode |= PNG_HAVE_IHDR;

   png_crc_read(png_ptr, buf, 13);
   png_crc_finish(png_ptr, 0);

   width = png_get_uint_31(png_ptr, buf);
   height = png_get_uint_31(png_ptr, buf + 4);
   bit_depth = buf[8];
   color_type = buf[9];
   compression_type = buf[10];
   filter_type = buf[11];
   interlace_type = buf[12];

#ifdef PNG_READ_APNG_SUPPORTED
   png_ptr->first_frame_width = width;
   png_ptr->first_frame_height = height;
#endif

   /* Set internal variables */
   png_ptr->width = width;
   png_ptr->height = height;
   png_ptr->bit_depth = (png_byte)bit_depth;
   png_ptr->interlaced = (png_byte)interlace_type;
   png_ptr->color_type = (png_byte)color_type;
#ifdef PNG_MNG_FEATURES_SUPPORTED
   png_ptr->filter_type = (png_byte)filter_type;
#endif
   png_ptr->compression_type = (png_byte)compression_type;

   /* Find number of channels */
   switch (png_ptr->color_type)
   {
      default: /* invalid, png_set_IHDR calls png_error */
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_PALETTE:
         png_ptr->channels = 1;
         break;

      case PNG_COLOR_TYPE_RGB:
         png_ptr->channels = 3;
         break;

      case PNG_COLOR_TYPE_GRAY_ALPHA:
         png_ptr->channels = 2;
         break;

      case PNG_COLOR_TYPE_RGB_ALPHA:
         png_ptr->channels = 4;
         break;
   }

   /* Set up other useful info */
   png_ptr->pixel_depth = (png_byte)(png_ptr->bit_depth * png_ptr->channels);
   png_ptr->rowbytes = PNG_ROWBYTES(png_ptr->pixel_depth, png_ptr->width);
   png_debug1(3, "bit_depth = %d", png_ptr->bit_depth);
   png_debug1(3, "channels = %d", png_ptr->channels);
   png_debug1(3, "rowbytes = %lu", (unsigned long)png_ptr->rowbytes);

   /* Rely on png_set_IHDR to completely validate the data and call png_error if
    * it's wrong.
    */
   png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth,
       color_type, interlace_type, compression_type, filter_type);

   return handled_ok;
   PNG_UNUSED(length)
}

/* Read and check the palette */
/* TODO: there are several obvious errors in this code when handling
 * out-of-place chunks and there is much over-complexity caused by trying to
 * patch up the problems.
 */
static png_handle_result_code
png_handle_PLTE(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_const_charp errmsg = NULL;

   png_debug(1, "in png_handle_PLTE");

   /* 1.6.47: consistency.  This used to be especially treated as a critical
    * error even in an image which is not colour mapped, there isn't a good
    * justification for treating some errors here one way and others another so
    * everything uses the same logic.
    */
   if ((png_ptr->mode & PNG_HAVE_PLTE) != 0)
      errmsg = "duplicate";

   else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0)
      errmsg = "out of place";

   else if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) == 0)
      errmsg = "ignored in grayscale PNG";

   else if (length > 3*PNG_MAX_PALETTE_LENGTH || (length % 3) != 0)
      errmsg = "invalid";

   /* This drops PLTE in favour of tRNS or bKGD because both of those chunks
    * can have an effect on the rendering of the image whereas PLTE only matters
    * in the case of an 8-bit display with a decoder which controls the palette.
    *
    * The alternative here is to ignore the error and store the palette anyway;
    * destroying the tRNS will definately cause problems.
    *
    * NOTE: the case of PNG_COLOR_TYPE_PALETTE need not be considered because
    * the png_handle_ routines for the three 'after PLTE' chunks tRNS, bKGD and
    * hIST all check for a preceding PLTE in these cases.
    */
   else if (png_ptr->color_type != PNG_COLOR_TYPE_PALETTE &&
            (png_has_chunk(png_ptr, tRNS) || png_has_chunk(png_ptr, bKGD)))
      errmsg = "out of place";

   else
   {
      /* If the palette has 256 or fewer entries but is too large for the bit
       * depth we don't issue an error to preserve the behavior of previous
       * libpng versions. We silently truncate the unused extra palette entries
       * here.
       */
      const unsigned max_palette_length =
         (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE) ?
            1U << png_ptr->bit_depth : PNG_MAX_PALETTE_LENGTH;

      /* The cast is safe because 'length' is less than
       * 3*PNG_MAX_PALETTE_LENGTH
       */
      const unsigned num = (length > 3U*max_palette_length) ?
         max_palette_length : (unsigned)length / 3U;

      unsigned i, j;
      png_byte buf[3*PNG_MAX_PALETTE_LENGTH];
      png_color palette[PNG_MAX_PALETTE_LENGTH];

      /* Read the chunk into the buffer then read to the end of the chunk. */
      png_crc_read(png_ptr, buf, num*3U);
      png_crc_finish_critical(png_ptr, length - 3U*num,
            /* Handle as ancillary if PLTE is optional: */
            png_ptr->color_type != PNG_COLOR_TYPE_PALETTE);

      for (i = 0U, j = 0U; i < num; i++)
      {
         palette[i].red = buf[j++];
         palette[i].green = buf[j++];
         palette[i].blue = buf[j++];
      }

      /* A valid PLTE chunk has been read */
      png_ptr->mode |= PNG_HAVE_PLTE;

      /* TODO: png_set_PLTE has the side effect of setting png_ptr->palette to
       * its own copy of the palette.  This has the side effect that when
       * png_start_row is called (this happens after any call to
       * png_read_update_info) the info_ptr palette gets changed.  This is
       * extremely unexpected and confusing.
       *
       * REVIEW: there have been consistent bugs in the past about gamma and
       * similar transforms to colour mapped images being useless because the
       * modified palette cannot be accessed because of the above.
       *
       * CONSIDER: Fix this by not sharing the palette in this way.  But does
       * this completely fix the problem?
       */
      png_set_PLTE(png_ptr, info_ptr, palette, num);
      return handled_ok;
   }

   /* Here on error: errmsg is non NULL. */
   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_error(png_ptr, errmsg);
   }

   else /* not critical to this image */
   {
      png_crc_finish_critical(png_ptr, length, 1/*handle as ancillary*/);
      png_chunk_benign_error(png_ptr, errmsg);
   }

   /* Because PNG_UNUSED(errmsg) does not work if all the uses are compiled out
    * (this does happen).
    */
   return errmsg != NULL ? handled_error : handled_error;
}

/* On read the IDAT chunk is always handled specially, even if marked for
 * unknown handling (this is allowed), so:
 */
#define png_handle_IDAT NULL

static png_handle_result_code
png_handle_IEND(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_debug(1, "in png_handle_IEND");

   png_ptr->mode |= (PNG_AFTER_IDAT | PNG_HAVE_IEND);

   if (length != 0)
      png_chunk_benign_error(png_ptr, "invalid");

   png_crc_finish_critical(png_ptr, length, 1/*handle as ancillary*/);

   return handled_ok;
   PNG_UNUSED(info_ptr)
}

#ifdef PNG_READ_gAMA_SUPPORTED
static png_handle_result_code
png_handle_gAMA(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_uint_32 ugamma;
   png_byte buf[4];

   png_debug(1, "in png_handle_gAMA");

   png_crc_read(png_ptr, buf, 4);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   ugamma = png_get_uint_32(buf);

   if (ugamma > PNG_UINT_31_MAX)
   {
      png_chunk_benign_error(png_ptr, "invalid");
      return handled_error;
   }

   png_set_gAMA_fixed(png_ptr, info_ptr, (png_fixed_point)/*SAFE*/ugamma);

#ifdef PNG_READ_GAMMA_SUPPORTED
      /* PNGv3: chunk precedence for gamma is cICP, [iCCP], sRGB, gAMA.  gAMA is
       * at the end of the chain so simply check for an unset value.
       */
      if (png_ptr->chunk_gamma == 0)
         png_ptr->chunk_gamma = (png_fixed_point)/*SAFE*/ugamma;
#endif /*READ_GAMMA*/

   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_gAMA NULL
#endif

#ifdef PNG_READ_sBIT_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_sBIT(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   unsigned int truelen, i;
   png_byte sample_depth;
   png_byte buf[4];

   png_debug(1, "in png_handle_sBIT");

   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      truelen = 3;
      sample_depth = 8;
   }

   else
   {
      truelen = png_ptr->channels;
      sample_depth = png_ptr->bit_depth;
   }

   if (length != truelen)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "bad length");
      return handled_error;
   }

   buf[0] = buf[1] = buf[2] = buf[3] = sample_depth;
   png_crc_read(png_ptr, buf, truelen);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   for (i=0; i<truelen; ++i)
   {
      if (buf[i] == 0 || buf[i] > sample_depth)
      {
         png_chunk_benign_error(png_ptr, "invalid");
         return handled_error;
      }
   }

   if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0)
   {
      png_ptr->sig_bit.red = buf[0];
      png_ptr->sig_bit.green = buf[1];
      png_ptr->sig_bit.blue = buf[2];
      png_ptr->sig_bit.alpha = buf[3];
   }

   else /* grayscale */
   {
      png_ptr->sig_bit.gray = buf[0];
      png_ptr->sig_bit.red = buf[0];
      png_ptr->sig_bit.green = buf[0];
      png_ptr->sig_bit.blue = buf[0];
      png_ptr->sig_bit.alpha = buf[1];
   }

   png_set_sBIT(png_ptr, info_ptr, &(png_ptr->sig_bit));
   return handled_ok;
}
#else
#  define png_handle_sBIT NULL
#endif

#ifdef PNG_READ_cHRM_SUPPORTED
static png_int_32
png_get_int_32_checked(png_const_bytep buf, int *error)
{
   png_uint_32 uval = png_get_uint_32(buf);
   if ((uval & 0x80000000) == 0) /* non-negative */
      return (png_int_32)uval;

   uval = (uval ^ 0xffffffff) + 1;  /* 2's complement: -x = ~x+1 */
   if ((uval & 0x80000000) == 0) /* no overflow */
      return -(png_int_32)uval;

   /* This version of png_get_int_32 has a way of returning the error to the
    * caller, so:
    */
   *error = 1;
   return 0; /* Safe */
}

static png_handle_result_code /* PRIVATE */
png_handle_cHRM(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   int error = 0;
   png_xy xy;
   png_byte buf[32];

   png_debug(1, "in png_handle_cHRM");

   png_crc_read(png_ptr, buf, 32);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   xy.whitex = png_get_int_32_checked(buf +  0, &error);
   xy.whitey = png_get_int_32_checked(buf +  4, &error);
   xy.redx   = png_get_int_32_checked(buf +  8, &error);
   xy.redy   = png_get_int_32_checked(buf + 12, &error);
   xy.greenx = png_get_int_32_checked(buf + 16, &error);
   xy.greeny = png_get_int_32_checked(buf + 20, &error);
   xy.bluex  = png_get_int_32_checked(buf + 24, &error);
   xy.bluey  = png_get_int_32_checked(buf + 28, &error);

   if (error)
   {
      png_chunk_benign_error(png_ptr, "invalid");
      return handled_error;
   }

   /* png_set_cHRM may complain about some of the values but this doesn't matter
    * because it was a cHRM and it did have vaguely (if, perhaps, ridiculous)
    * values.  Ridiculousity will be checked if the values are used later.
    */
   png_set_cHRM_fixed(png_ptr, info_ptr, xy.whitex, xy.whitey, xy.redx, xy.redy,
         xy.greenx, xy.greeny, xy.bluex, xy.bluey);

   /* We only use 'chromaticities' for RGB to gray */
#  ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
      /* There is no need to check sRGB here, cICP is NYI and iCCP is not
       * supported so just check mDCV.
       */
      if (!png_has_chunk(png_ptr, mDCV))
      {
         png_ptr->chromaticities = xy;
      }
#  endif /* READ_RGB_TO_GRAY */

   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_cHRM NULL
#endif

#ifdef PNG_READ_sRGB_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_sRGB(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte intent;

   png_debug(1, "in png_handle_sRGB");

   png_crc_read(png_ptr, &intent, 1);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   /* This checks the range of the "rendering intent" because it is specified in
    * the PNG spec itself; the "reserved" values will result in the chunk not
    * being accepted, just as they do with the various "reserved" values in
    * IHDR.
    */
   if (intent > 3/*PNGv3 spec*/)
   {
      png_chunk_benign_error(png_ptr, "invalid");
      return handled_error;
   }

   png_set_sRGB(png_ptr, info_ptr, intent);
   /* NOTE: png_struct::chromaticities is not set here because the RGB to gray
    * coefficients are known without a need for the chromaticities.
    */

#ifdef PNG_READ_GAMMA_SUPPORTED
      /* PNGv3: chunk precedence for gamma is cICP, [iCCP], sRGB, gAMA.  iCCP is
       * not supported by libpng so the only requirement is to check for cICP
       * setting the gamma (this is NYI, but this check is safe.)
       */
      if (!png_has_chunk(png_ptr, cICP) || png_ptr->chunk_gamma == 0)
         png_ptr->chunk_gamma = PNG_GAMMA_sRGB_INVERSE;
#endif /*READ_GAMMA*/

   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_sRGB NULL
#endif /* READ_sRGB */

#ifdef PNG_READ_iCCP_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_iCCP(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
/* Note: this does not properly handle profiles that are > 64K under DOS */
{
   png_const_charp errmsg = NULL; /* error message output, or no error */
   int finished = 0; /* crc checked */

   png_debug(1, "in png_handle_iCCP");

   /* PNGv3: allow PNG files with both sRGB and iCCP because the PNG spec only
    * ever said that there "should" be only one, not "shall" and the PNGv3
    * colour chunk precedence rules give a handling for this case anyway.
    */
   {
      uInt read_length, keyword_length;
      char keyword[81];

      /* Find the keyword; the keyword plus separator and compression method
       * bytes can be at most 81 characters long.
       */
      read_length = 81; /* maximum */
      if (read_length > length)
         read_length = (uInt)/*SAFE*/length;

      png_crc_read(png_ptr, (png_bytep)keyword, read_length);
      length -= read_length;

      if (length < LZ77Min)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "too short");
         return handled_error;
      }

      keyword_length = 0;
      while (keyword_length < 80 && keyword_length < read_length &&
         keyword[keyword_length] != 0)
         ++keyword_length;

      /* TODO: make the keyword checking common */
      if (keyword_length >= 1 && keyword_length <= 79)
      {
         /* We only understand '0' compression - deflate - so if we get a
          * different value we can't safely decode the chunk.
          */
         if (keyword_length+1 < read_length &&
            keyword[keyword_length+1] == PNG_COMPRESSION_TYPE_BASE)
         {
            read_length -= keyword_length+2;

            if (png_inflate_claim(png_ptr, png_iCCP) == Z_OK)
            {
               Byte profile_header[132]={0};
               Byte local_buffer[PNG_INFLATE_BUF_SIZE];
               png_alloc_size_t size = (sizeof profile_header);

               png_ptr->zstream.next_in = (Bytef*)keyword + (keyword_length+2);
               png_ptr->zstream.avail_in = read_length;
               (void)png_inflate_read(png_ptr, local_buffer,
                   (sizeof local_buffer), &length, profile_header, &size,
                   0/*finish: don't, because the output is too small*/);

               if (size == 0)
               {
                  /* We have the ICC profile header; do the basic header checks.
                   */
                  png_uint_32 profile_length = png_get_uint_32(profile_header);

                  if (png_icc_check_length(png_ptr, keyword, profile_length) !=
                      0)
                  {
                     /* The length is apparently ok, so we can check the 132
                      * byte header.
                      */
                     if (png_icc_check_header(png_ptr, keyword, profile_length,
                              profile_header, png_ptr->color_type) != 0)
                     {
                        /* Now read the tag table; a variable size buffer is
                         * needed at this point, allocate one for the whole
                         * profile.  The header check has already validated
                         * that none of this stuff will overflow.
                         */
                        png_uint_32 tag_count =
                           png_get_uint_32(profile_header + 128);
                        png_bytep profile = png_read_buffer(png_ptr,
                              profile_length);

                        if (profile != NULL)
                        {
                           memcpy(profile, profile_header,
                               (sizeof profile_header));

                           size = 12 * tag_count;

                           (void)png_inflate_read(png_ptr, local_buffer,
                               (sizeof local_buffer), &length,
                               profile + (sizeof profile_header), &size, 0);

                           /* Still expect a buffer error because we expect
                            * there to be some tag data!
                            */
                           if (size == 0)
                           {
                              if (png_icc_check_tag_table(png_ptr,
                                       keyword, profile_length, profile) != 0)
                              {
                                 /* The profile has been validated for basic
                                  * security issues, so read the whole thing in.
                                  */
                                 size = profile_length - (sizeof profile_header)
                                     - 12 * tag_count;

                                 (void)png_inflate_read(png_ptr, local_buffer,
                                     (sizeof local_buffer), &length,
                                     profile + (sizeof profile_header) +
                                     12 * tag_count, &size, 1/*finish*/);

                                 if (length > 0 && !(png_ptr->flags &
                                     PNG_FLAG_BENIGN_ERRORS_WARN))
                                    errmsg = "extra compressed data";

                                 /* But otherwise allow extra data: */
                                 else if (size == 0)
                                 {
                                    if (length > 0)
                                    {
                                       /* This can be handled completely, so
                                        * keep going.
                                        */
                                       png_chunk_warning(png_ptr,
                                           "extra compressed data");
                                    }

                                    png_crc_finish(png_ptr, length);
                                    finished = 1;

                                    /* Steal the profile for info_ptr. */
                                    if (info_ptr != NULL)
                                    {
                                       png_free_data(png_ptr, info_ptr,
                                           PNG_FREE_ICCP, 0);

                                       info_ptr->iccp_name = png_voidcast(char*,
                                           png_malloc_base(png_ptr,
                                           keyword_length+1));
                                       if (info_ptr->iccp_name != NULL)
                                       {
                                          memcpy(info_ptr->iccp_name, keyword,
                                              keyword_length+1);
                                          info_ptr->iccp_proflen =
                                              profile_length;
                                          info_ptr->iccp_profile = profile;
                                          png_ptr->read_buffer = NULL; /*steal*/
                                          info_ptr->free_me |= PNG_FREE_ICCP;
                                          info_ptr->valid |= PNG_INFO_iCCP;
                                       }

                                       else
                                          errmsg = "out of memory";
                                    }

                                    /* else the profile remains in the read
                                     * buffer which gets reused for subsequent
                                     * chunks.
                                     */

                                    if (errmsg == NULL)
                                    {
                                       png_ptr->zowner = 0;
                                       return handled_ok;
                                    }
                                 }
                                 if (errmsg == NULL)
                                    errmsg = png_ptr->zstream.msg;
                              }
                              /* else png_icc_check_tag_table output an error */
                           }
                           else /* profile truncated */
                              errmsg = png_ptr->zstream.msg;
                        }

                        else
                           errmsg = "out of memory";
                     }

                     /* else png_icc_check_header output an error */
                  }

                  /* else png_icc_check_length output an error */
               }

               else /* profile truncated */
                  errmsg = png_ptr->zstream.msg;

               /* Release the stream */
               png_ptr->zowner = 0;
            }

            else /* png_inflate_claim failed */
               errmsg = png_ptr->zstream.msg;
         }

         else
            errmsg = "bad compression method"; /* or missing */
      }

      else
         errmsg = "bad keyword";
   }

   /* Failure: the reason is in 'errmsg' */
   if (finished == 0)
      png_crc_finish(png_ptr, length);

   if (errmsg != NULL) /* else already output */
      png_chunk_benign_error(png_ptr, errmsg);

   return handled_error;
}
#else
#  define png_handle_iCCP NULL
#endif /* READ_iCCP */

#ifdef PNG_READ_sPLT_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_sPLT(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
/* Note: this does not properly handle chunks that are > 64K under DOS */
{
   png_bytep entry_start, buffer;
   png_sPLT_t new_palette;
   png_sPLT_entryp pp;
   png_uint_32 data_length;
   int entry_size, i;
   png_uint_32 skip = 0;
   png_uint_32 dl;
   size_t max_dl;

   png_debug(1, "in png_handle_sPLT");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return handled_error;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         png_warning(png_ptr, "No space in chunk cache for sPLT");
         png_crc_finish(png_ptr, length);
         return handled_error;
      }
   }
#endif

   buffer = png_read_buffer(png_ptr, length+1);
   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return handled_error;
   }


   /* WARNING: this may break if size_t is less than 32 bits; it is assumed
    * that the PNG_MAX_MALLOC_64K test is enabled in this case, but this is a
    * potential breakage point if the types in pngconf.h aren't exactly right.
    */
   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, skip) != 0)
      return handled_error;

   buffer[length] = 0;

   for (entry_start = buffer; *entry_start; entry_start++)
      /* Empty loop to find end of name */ ;

   ++entry_start;

   /* A sample depth should follow the separator, and we should be on it  */
   if (length < 2U || entry_start > buffer + (length - 2U))
   {
      png_warning(png_ptr, "malformed sPLT chunk");
      return handled_error;
   }

   new_palette.depth = *entry_start++;
   entry_size = (new_palette.depth == 8 ? 6 : 10);
   /* This must fit in a png_uint_32 because it is derived from the original
    * chunk data length.
    */
   data_length = length - (png_uint_32)(entry_start - buffer);

   /* Integrity-check the data length */
   if ((data_length % (unsigned int)entry_size) != 0)
   {
      png_warning(png_ptr, "sPLT chunk has bad length");
      return handled_error;
   }

   dl = (png_uint_32)(data_length / (unsigned int)entry_size);
   max_dl = PNG_SIZE_MAX / (sizeof (png_sPLT_entry));

   if (dl > max_dl)
   {
      png_warning(png_ptr, "sPLT chunk too long");
      return handled_error;
   }

   new_palette.nentries = (png_int_32)(data_length / (unsigned int)entry_size);

   new_palette.entries = (png_sPLT_entryp)png_malloc_warn(png_ptr,
       (png_alloc_size_t) new_palette.nentries * (sizeof (png_sPLT_entry)));

   if (new_palette.entries == NULL)
   {
      png_warning(png_ptr, "sPLT chunk requires too much memory");
      return handled_error;
   }

   for (i = 0; i < new_palette.nentries; i++)
   {
      pp = new_palette.entries + i;

      if (new_palette.depth == 8)
      {
         pp->red = *entry_start++;
         pp->green = *entry_start++;
         pp->blue = *entry_start++;
         pp->alpha = *entry_start++;
      }

      else
      {
         pp->red   = png_get_uint_16(entry_start); entry_start += 2;
         pp->green = png_get_uint_16(entry_start); entry_start += 2;
         pp->blue  = png_get_uint_16(entry_start); entry_start += 2;
         pp->alpha = png_get_uint_16(entry_start); entry_start += 2;
      }

      pp->frequency = png_get_uint_16(entry_start); entry_start += 2;
   }

   /* Discard all chunk data except the name and stash that */
   new_palette.name = (png_charp)buffer;

   png_set_sPLT(png_ptr, info_ptr, &new_palette, 1);

   png_free(png_ptr, new_palette.entries);
   return handled_ok;
}
#else
#  define png_handle_sPLT NULL
#endif /* READ_sPLT */

#ifdef PNG_READ_tRNS_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_tRNS(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte readbuf[PNG_MAX_PALETTE_LENGTH];

   png_debug(1, "in png_handle_tRNS");

   if (png_ptr->color_type == PNG_COLOR_TYPE_GRAY)
   {
      png_byte buf[2];

      if (length != 2)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "invalid");
         return handled_error;
      }

      png_crc_read(png_ptr, buf, 2);
      png_ptr->num_trans = 1;
      png_ptr->trans_color.gray = png_get_uint_16(buf);
   }

   else if (png_ptr->color_type == PNG_COLOR_TYPE_RGB)
   {
      png_byte buf[6];

      if (length != 6)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "invalid");
         return handled_error;
      }

      png_crc_read(png_ptr, buf, length);
      png_ptr->num_trans = 1;
      png_ptr->trans_color.red = png_get_uint_16(buf);
      png_ptr->trans_color.green = png_get_uint_16(buf + 2);
      png_ptr->trans_color.blue = png_get_uint_16(buf + 4);
   }

   else if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      if ((png_ptr->mode & PNG_HAVE_PLTE) == 0)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "out of place");
         return handled_error;
      }

      if (length > (unsigned int) png_ptr->num_palette ||
         length > (unsigned int) PNG_MAX_PALETTE_LENGTH ||
         length == 0)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "invalid");
         return handled_error;
      }

      png_crc_read(png_ptr, readbuf, length);
      png_ptr->num_trans = (png_uint_16)length;
   }

   else
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid with alpha channel");
      return handled_error;
   }

   if (png_crc_finish(png_ptr, 0) != 0)
   {
      png_ptr->num_trans = 0;
      return handled_error;
   }

   /* TODO: this is a horrible side effect in the palette case because the
    * png_struct ends up with a pointer to the tRNS buffer owned by the
    * png_info.  Fix this.
    */
   png_set_tRNS(png_ptr, info_ptr, readbuf, png_ptr->num_trans,
       &(png_ptr->trans_color));
   return handled_ok;
}
#else
#  define png_handle_tRNS NULL
#endif

#ifdef PNG_READ_bKGD_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_bKGD(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   unsigned int truelen;
   png_byte buf[6];
   png_color_16 background;

   png_debug(1, "in png_handle_bKGD");

   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      if ((png_ptr->mode & PNG_HAVE_PLTE) == 0)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "out of place");
         return handled_error;
      }

      truelen = 1;
   }

   else if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0)
      truelen = 6;

   else
      truelen = 2;

   if (length != truelen)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid");
      return handled_error;
   }

   png_crc_read(png_ptr, buf, truelen);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   /* We convert the index value into RGB components so that we can allow
    * arbitrary RGB values for background when we have transparency, and
    * so it is easy to determine the RGB values of the background color
    * from the info_ptr struct.
    */
   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      background.index = buf[0];

      if (info_ptr != NULL && info_ptr->num_palette != 0)
      {
         if (buf[0] >= info_ptr->num_palette)
         {
            png_chunk_benign_error(png_ptr, "invalid index");
            return handled_error;
         }

         background.red = (png_uint_16)png_ptr->palette[buf[0]].red;
         background.green = (png_uint_16)png_ptr->palette[buf[0]].green;
         background.blue = (png_uint_16)png_ptr->palette[buf[0]].blue;
      }

      else
         background.red = background.green = background.blue = 0;

      background.gray = 0;
   }

   else if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) == 0) /* GRAY */
   {
      if (png_ptr->bit_depth <= 8)
      {
         if (buf[0] != 0 || buf[1] >= (unsigned int)(1 << png_ptr->bit_depth))
         {
            png_chunk_benign_error(png_ptr, "invalid gray level");
            return handled_error;
         }
      }

      background.index = 0;
      background.red =
      background.green =
      background.blue =
      background.gray = png_get_uint_16(buf);
   }

   else
   {
      if (png_ptr->bit_depth <= 8)
      {
         if (buf[0] != 0 || buf[2] != 0 || buf[4] != 0)
         {
            png_chunk_benign_error(png_ptr, "invalid color");
            return handled_error;
         }
      }

      background.index = 0;
      background.red = png_get_uint_16(buf);
      background.green = png_get_uint_16(buf + 2);
      background.blue = png_get_uint_16(buf + 4);
      background.gray = 0;
   }

   png_set_bKGD(png_ptr, info_ptr, &background);
   return handled_ok;
}
#else
#  define png_handle_bKGD NULL
#endif

#ifdef PNG_READ_cICP_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_cICP(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[4];

   png_debug(1, "in png_handle_cICP");

   png_crc_read(png_ptr, buf, 4);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   png_set_cICP(png_ptr, info_ptr, buf[0], buf[1],  buf[2], buf[3]);

   /* We only use 'chromaticities' for RGB to gray */
#  ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
      if (!png_has_chunk(png_ptr, mDCV))
      {
         /* TODO: png_ptr->chromaticities = chromaticities; */
      }
#  endif /* READ_RGB_TO_GRAY */

#ifdef PNG_READ_GAMMA_SUPPORTED
      /* PNGv3: chunk precedence for gamma is cICP, [iCCP], sRGB, gAMA.  cICP is
       * at the head so simply set the gamma if it can be determined.  If not
       * chunk_gamma remains unchanged; sRGB and gAMA handling check it for
       * being zero.
       */
      /* TODO: set png_struct::chunk_gamma when possible */
#endif /*READ_GAMMA*/

   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_cICP NULL
#endif

#ifdef PNG_READ_cLLI_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_cLLI(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[8];

   png_debug(1, "in png_handle_cLLI");

   png_crc_read(png_ptr, buf, 8);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   /* The error checking happens here, this puts it in just one place: */
   png_set_cLLI_fixed(png_ptr, info_ptr, png_get_uint_32(buf),
         png_get_uint_32(buf+4));
   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_cLLI NULL
#endif

#ifdef PNG_READ_mDCV_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_mDCV(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_xy chromaticities;
   png_byte buf[24];

   png_debug(1, "in png_handle_mDCV");

   png_crc_read(png_ptr, buf, 24);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   /* The error checking happens here, this puts it in just one place.  The
    * odd /50000 scaling factor makes it more difficult but the (x.y) values are
    * only two bytes so a <<1 is safe.
    *
    * WARNING: the PNG specification defines the cHRM chunk to **start** with
    * the white point (x,y).  The W3C PNG v3 specification puts the white point
    * **after* R,G,B.  The x,y values in mDCV are also scaled by 50,000 and
    * stored in just two bytes, whereas those in cHRM are scaled by 100,000 and
    * stored in four bytes.  This is very, very confusing.  These APIs remove
    * the confusion by copying the existing, well established, API.
    */
   chromaticities.redx   = png_get_uint_16(buf+ 0U) << 1; /* red x */
   chromaticities.redy   = png_get_uint_16(buf+ 2U) << 1; /* red y */
   chromaticities.greenx = png_get_uint_16(buf+ 4U) << 1; /* green x */
   chromaticities.greeny = png_get_uint_16(buf+ 6U) << 1; /* green y */
   chromaticities.bluex  = png_get_uint_16(buf+ 8U) << 1; /* blue x */
   chromaticities.bluey  = png_get_uint_16(buf+10U) << 1; /* blue y */
   chromaticities.whitex = png_get_uint_16(buf+12U) << 1; /* white x */
   chromaticities.whitey = png_get_uint_16(buf+14U) << 1; /* white y */

   png_set_mDCV_fixed(png_ptr, info_ptr,
         chromaticities.whitex, chromaticities.whitey,
         chromaticities.redx, chromaticities.redy,
         chromaticities.greenx, chromaticities.greeny,
         chromaticities.bluex, chromaticities.bluey,
         png_get_uint_32(buf+16U), /* peak luminance */
         png_get_uint_32(buf+20U));/* minimum perceivable luminance */

   /* We only use 'chromaticities' for RGB to gray */
#  ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
      png_ptr->chromaticities = chromaticities;
#  endif /* READ_RGB_TO_GRAY */

   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_mDCV NULL
#endif

#ifdef PNG_READ_eXIf_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_eXIf(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_bytep buffer = NULL;

   png_debug(1, "in png_handle_eXIf");

   buffer = png_read_buffer(png_ptr, length);

   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return handled_error;
   }

   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   /* PNGv3: the code used to check the byte order mark at the start for MM or
    * II, however PNGv3 states that the the first 4 bytes should be checked.
    * The caller ensures that there are four bytes available.
    */
   {
      png_uint_32 header = png_get_uint_32(buffer);

      /* These numbers are copied from the PNGv3 spec: */
      if (header != 0x49492A00 && header != 0x4D4D002A)
      {
         png_chunk_benign_error(png_ptr, "invalid");
         return handled_error;
      }
   }

   png_set_eXIf_1(png_ptr, info_ptr, length, buffer);
   return handled_ok;
}
#else
#  define png_handle_eXIf NULL
#endif

#ifdef PNG_READ_hIST_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_hIST(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   unsigned int num, i;
   png_uint_16 readbuf[PNG_MAX_PALETTE_LENGTH];

   png_debug(1, "in png_handle_hIST");

   /* This cast is safe because the chunk definition limits the length to a
    * maximum of 1024 bytes.
    *
    * TODO: maybe use png_uint_32 anyway, not unsigned int, to reduce the
    * casts.
    */
   num = (unsigned int)length / 2 ;

   if (length != num * 2 ||
       num != (unsigned int)png_ptr->num_palette ||
       num > (unsigned int)PNG_MAX_PALETTE_LENGTH)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "invalid");
      return handled_error;
   }

   for (i = 0; i < num; i++)
   {
      png_byte buf[2];

      png_crc_read(png_ptr, buf, 2);
      readbuf[i] = png_get_uint_16(buf);
   }

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   png_set_hIST(png_ptr, info_ptr, readbuf);
   return handled_ok;
}
#else
#  define png_handle_hIST NULL
#endif

#ifdef PNG_READ_pHYs_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_pHYs(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[9];
   png_uint_32 res_x, res_y;
   int unit_type;

   png_debug(1, "in png_handle_pHYs");

   png_crc_read(png_ptr, buf, 9);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   res_x = png_get_uint_32(buf);
   res_y = png_get_uint_32(buf + 4);
   unit_type = buf[8];
   png_set_pHYs(png_ptr, info_ptr, res_x, res_y, unit_type);
   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_pHYs NULL
#endif

#ifdef PNG_READ_oFFs_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_oFFs(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[9];
   png_int_32 offset_x, offset_y;
   int unit_type;

   png_debug(1, "in png_handle_oFFs");

   png_crc_read(png_ptr, buf, 9);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   offset_x = png_get_int_32(buf);
   offset_y = png_get_int_32(buf + 4);
   unit_type = buf[8];
   png_set_oFFs(png_ptr, info_ptr, offset_x, offset_y, unit_type);
   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_oFFs NULL
#endif

#ifdef PNG_READ_pCAL_SUPPORTED
/* Read the pCAL chunk (described in the PNG Extensions document) */
static png_handle_result_code /* PRIVATE */
png_handle_pCAL(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_int_32 X0, X1;
   png_byte type, nparams;
   png_bytep buffer, buf, units, endptr;
   png_charpp params;
   int i;

   png_debug(1, "in png_handle_pCAL");
   png_debug1(2, "Allocating and reading pCAL chunk data (%u bytes)",
       length + 1);

   buffer = png_read_buffer(png_ptr, length+1);

   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return handled_error;
   }

   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   buffer[length] = 0; /* Null terminate the last string */

   png_debug(3, "Finding end of pCAL purpose string");
   for (buf = buffer; *buf; buf++)
      /* Empty loop */ ;

   endptr = buffer + length;

   /* We need to have at least 12 bytes after the purpose string
    * in order to get the parameter information.
    */
   if (endptr - buf <= 12)
   {
      png_chunk_benign_error(png_ptr, "invalid");
      return handled_error;
   }

   png_debug(3, "Reading pCAL X0, X1, type, nparams, and units");
   X0 = png_get_int_32((png_bytep)buf+1);
   X1 = png_get_int_32((png_bytep)buf+5);
   type = buf[9];
   nparams = buf[10];
   units = buf + 11;

   png_debug(3, "Checking pCAL equation type and number of parameters");
   /* Check that we have the right number of parameters for known
    * equation types.
    */
   if ((type == PNG_EQUATION_LINEAR && nparams != 2) ||
       (type == PNG_EQUATION_BASE_E && nparams != 3) ||
       (type == PNG_EQUATION_ARBITRARY && nparams != 3) ||
       (type == PNG_EQUATION_HYPERBOLIC && nparams != 4))
   {
      png_chunk_benign_error(png_ptr, "invalid parameter count");
      return handled_error;
   }

   else if (type >= PNG_EQUATION_LAST)
   {
      png_chunk_benign_error(png_ptr, "unrecognized equation type");
   }

   for (buf = units; *buf; buf++)
      /* Empty loop to move past the units string. */ ;

   png_debug(3, "Allocating pCAL parameters array");

   params = png_voidcast(png_charpp, png_malloc_warn(png_ptr,
       nparams * (sizeof (png_charp))));

   if (params == NULL)
   {
      png_chunk_benign_error(png_ptr, "out of memory");
      return handled_error;
   }

   /* Get pointers to the start of each parameter string. */
   for (i = 0; i < nparams; i++)
   {
      buf++; /* Skip the null string terminator from previous parameter. */

      png_debug1(3, "Reading pCAL parameter %d", i);

      for (params[i] = (png_charp)buf; buf <= endptr && *buf != 0; buf++)
         /* Empty loop to move past each parameter string */ ;

      /* Make sure we haven't run out of data yet */
      if (buf > endptr)
      {
         png_free(png_ptr, params);
         png_chunk_benign_error(png_ptr, "invalid data");
         return handled_error;
      }
   }

   png_set_pCAL(png_ptr, info_ptr, (png_charp)buffer, X0, X1, type, nparams,
       (png_charp)units, params);

   /* TODO: BUG: png_set_pCAL calls png_chunk_report which, in this case, calls
    * png_benign_error and that can error out.
    *
    * png_read_buffer needs to be allocated with space for both nparams and the
    * parameter strings.  Not hard to do.
    */
   png_free(png_ptr, params);
   return handled_ok;
}
#else
#  define png_handle_pCAL NULL
#endif

#ifdef PNG_READ_sCAL_SUPPORTED
/* Read the sCAL chunk */
static png_handle_result_code /* PRIVATE */
png_handle_sCAL(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_bytep buffer;
   size_t i;
   int state;

   png_debug(1, "in png_handle_sCAL");
   png_debug1(2, "Allocating and reading sCAL chunk data (%u bytes)",
       length + 1);

   buffer = png_read_buffer(png_ptr, length+1);

   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return handled_error;
   }

   png_crc_read(png_ptr, buffer, length);
   buffer[length] = 0; /* Null terminate the last string */

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   /* Validate the unit. */
   if (buffer[0] != 1 && buffer[0] != 2)
   {
      png_chunk_benign_error(png_ptr, "invalid unit");
      return handled_error;
   }

   /* Validate the ASCII numbers, need two ASCII numbers separated by
    * a '\0' and they need to fit exactly in the chunk data.
    */
   i = 1;
   state = 0;

   if (png_check_fp_number((png_const_charp)buffer, length, &state, &i) == 0 ||
       i >= length || buffer[i++] != 0)
      png_chunk_benign_error(png_ptr, "bad width format");

   else if (PNG_FP_IS_POSITIVE(state) == 0)
      png_chunk_benign_error(png_ptr, "non-positive width");

   else
   {
      size_t heighti = i;

      state = 0;
      if (png_check_fp_number((png_const_charp)buffer, length,
          &state, &i) == 0 || i != length)
         png_chunk_benign_error(png_ptr, "bad height format");

      else if (PNG_FP_IS_POSITIVE(state) == 0)
         png_chunk_benign_error(png_ptr, "non-positive height");

      else
      {
         /* This is the (only) success case. */
         png_set_sCAL_s(png_ptr, info_ptr, buffer[0],
             (png_charp)buffer+1, (png_charp)buffer+heighti);
         return handled_ok;
      }
   }

   return handled_error;
}
#else
#  define png_handle_sCAL NULL
#endif

#ifdef PNG_READ_tIME_SUPPORTED
static png_handle_result_code /* PRIVATE */
png_handle_tIME(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_byte buf[7];
   png_time mod_time;

   png_debug(1, "in png_handle_tIME");

   /* TODO: what is this doing here?  It should be happened in pngread.c and
    * pngpread.c, although it could be moved to png_handle_chunk below and
    * thereby avoid some code duplication.
    */
   if ((png_ptr->mode & PNG_HAVE_IDAT) != 0)
      png_ptr->mode |= PNG_AFTER_IDAT;

   png_crc_read(png_ptr, buf, 7);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   mod_time.second = buf[6];
   mod_time.minute = buf[5];
   mod_time.hour = buf[4];
   mod_time.day = buf[3];
   mod_time.month = buf[2];
   mod_time.year = png_get_uint_16(buf);

   png_set_tIME(png_ptr, info_ptr, &mod_time);
   return handled_ok;
   PNG_UNUSED(length)
}
#else
#  define png_handle_tIME NULL
#endif

#ifdef PNG_READ_tEXt_SUPPORTED
/* Note: this does not properly handle chunks that are > 64K under DOS */
static png_handle_result_code /* PRIVATE */
png_handle_tEXt(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_text  text_info;
   png_bytep buffer;
   png_charp key;
   png_charp text;
   png_uint_32 skip = 0;

   png_debug(1, "in png_handle_tEXt");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return handled_error;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "no space in chunk cache");
         return handled_error;
      }
   }
#endif

   buffer = png_read_buffer(png_ptr, length+1);

   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return handled_error;
   }

   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, skip) != 0)
      return handled_error;

   key = (png_charp)buffer;
   key[length] = 0;

   for (text = key; *text; text++)
      /* Empty loop to find end of key */ ;

   if (text != key + length)
      text++;

   text_info.compression = PNG_TEXT_COMPRESSION_NONE;
   text_info.key = key;
   text_info.lang = NULL;
   text_info.lang_key = NULL;
   text_info.itxt_length = 0;
   text_info.text = text;
   text_info.text_length = strlen(text);

   if (png_set_text_2(png_ptr, info_ptr, &text_info, 1) == 0)
      return handled_ok;

   png_chunk_benign_error(png_ptr, "out of memory");
   return handled_error;
}
#else
#  define png_handle_tEXt NULL
#endif

#ifdef PNG_READ_zTXt_SUPPORTED
/* Note: this does not correctly handle chunks that are > 64K under DOS */
static png_handle_result_code /* PRIVATE */
png_handle_zTXt(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_const_charp errmsg = NULL;
   png_bytep       buffer;
   png_uint_32     keyword_length;

   png_debug(1, "in png_handle_zTXt");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return handled_error;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "no space in chunk cache");
         return handled_error;
      }
   }
#endif

   /* Note, "length" is sufficient here; we won't be adding
    * a null terminator later.  The limit check in png_handle_chunk should be
    * sufficient.
    */
   buffer = png_read_buffer(png_ptr, length);

   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return handled_error;
   }

   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   /* TODO: also check that the keyword contents match the spec! */
   for (keyword_length = 0;
      keyword_length < length && buffer[keyword_length] != 0;
      ++keyword_length)
      /* Empty loop to find end of name */ ;

   if (keyword_length > 79 || keyword_length < 1)
      errmsg = "bad keyword";

   /* zTXt must have some LZ data after the keyword, although it may expand to
    * zero bytes; we need a '\0' at the end of the keyword, the compression type
    * then the LZ data:
    */
   else if (keyword_length + 3 > length)
      errmsg = "truncated";

   else if (buffer[keyword_length+1] != PNG_COMPRESSION_TYPE_BASE)
      errmsg = "unknown compression type";

   else
   {
      png_alloc_size_t uncompressed_length = PNG_SIZE_MAX;

      /* TODO: at present png_decompress_chunk imposes a single application
       * level memory limit, this should be split to different values for iCCP
       * and text chunks.
       */
      if (png_decompress_chunk(png_ptr, length, keyword_length+2,
          &uncompressed_length, 1/*terminate*/) == Z_STREAM_END)
      {
         png_text text;

         if (png_ptr->read_buffer == NULL)
           errmsg="Read failure in png_handle_zTXt";
         else
         {
            /* It worked; png_ptr->read_buffer now looks like a tEXt chunk
             * except for the extra compression type byte and the fact that
             * it isn't necessarily '\0' terminated.
             */
            buffer = png_ptr->read_buffer;
            buffer[uncompressed_length+(keyword_length+2)] = 0;

            text.compression = PNG_TEXT_COMPRESSION_zTXt;
            text.key = (png_charp)buffer;
            text.text = (png_charp)(buffer + keyword_length+2);
            text.text_length = uncompressed_length;
            text.itxt_length = 0;
            text.lang = NULL;
            text.lang_key = NULL;

            if (png_set_text_2(png_ptr, info_ptr, &text, 1) == 0)
               return handled_ok;

            errmsg = "out of memory";
         }
      }

      else
         errmsg = png_ptr->zstream.msg;
   }

   png_chunk_benign_error(png_ptr, errmsg);
   return handled_error;
}
#else
#  define png_handle_zTXt NULL
#endif

#ifdef PNG_READ_iTXt_SUPPORTED
/* Note: this does not correctly handle chunks that are > 64K under DOS */
static png_handle_result_code /* PRIVATE */
png_handle_iTXt(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   png_const_charp errmsg = NULL;
   png_bytep buffer;
   png_uint_32 prefix_length;

   png_debug(1, "in png_handle_iTXt");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return handled_error;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, "no space in chunk cache");
         return handled_error;
      }
   }
#endif

   buffer = png_read_buffer(png_ptr, length+1);

   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return handled_error;
   }

   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, 0) != 0)
      return handled_error;

   /* First the keyword. */
   for (prefix_length=0;
      prefix_length < length && buffer[prefix_length] != 0;
      ++prefix_length)
      /* Empty loop */ ;

   /* Perform a basic check on the keyword length here. */
   if (prefix_length > 79 || prefix_length < 1)
      errmsg = "bad keyword";

   /* Expect keyword, compression flag, compression type, language, translated
    * keyword (both may be empty but are 0 terminated) then the text, which may
    * be empty.
    */
   else if (prefix_length + 5 > length)
      errmsg = "truncated";

   else if (buffer[prefix_length+1] == 0 ||
      (buffer[prefix_length+1] == 1 &&
      buffer[prefix_length+2] == PNG_COMPRESSION_TYPE_BASE))
   {
      int compressed = buffer[prefix_length+1] != 0;
      png_uint_32 language_offset, translated_keyword_offset;
      png_alloc_size_t uncompressed_length = 0;

      /* Now the language tag */
      prefix_length += 3;
      language_offset = prefix_length;

      for (; prefix_length < length && buffer[prefix_length] != 0;
         ++prefix_length)
         /* Empty loop */ ;

      /* WARNING: the length may be invalid here, this is checked below. */
      translated_keyword_offset = ++prefix_length;

      for (; prefix_length < length && buffer[prefix_length] != 0;
         ++prefix_length)
         /* Empty loop */ ;

      /* prefix_length should now be at the trailing '\0' of the translated
       * keyword, but it may already be over the end.  None of this arithmetic
       * can overflow because chunks are at most 2^31 bytes long, but on 16-bit
       * systems the available allocation may overflow.
       */
      ++prefix_length;

      if (compressed == 0 && prefix_length <= length)
         uncompressed_length = length - prefix_length;

      else if (compressed != 0 && prefix_length < length)
      {
         uncompressed_length = PNG_SIZE_MAX;

         /* TODO: at present png_decompress_chunk imposes a single application
          * level memory limit, this should be split to different values for
          * iCCP and text chunks.
          */
         if (png_decompress_chunk(png_ptr, length, prefix_length,
             &uncompressed_length, 1/*terminate*/) == Z_STREAM_END)
            buffer = png_ptr->read_buffer;

         else
            errmsg = png_ptr->zstream.msg;
      }

      else
         errmsg = "truncated";

      if (errmsg == NULL)
      {
         png_text text;

         buffer[uncompressed_length+prefix_length] = 0;

         if (compressed == 0)
            text.compression = PNG_ITXT_COMPRESSION_NONE;

         else
            text.compression = PNG_ITXT_COMPRESSION_zTXt;

         text.key = (png_charp)buffer;
         text.lang = (png_charp)buffer + language_offset;
         text.lang_key = (png_charp)buffer + translated_keyword_offset;
         text.text = (png_charp)buffer + prefix_length;
         text.text_length = 0;
         text.itxt_length = uncompressed_length;

         if (png_set_text_2(png_ptr, info_ptr, &text, 1) == 0)
            return handled_ok;

         errmsg = "out of memory";
      }
   }

   else
      errmsg = "bad compression info";

   if (errmsg != NULL)
      png_chunk_benign_error(png_ptr, errmsg);
   return handled_error;
}
#else
#  define png_handle_iTXt NULL
#endif

#ifdef PNG_READ_APNG_SUPPORTED
void /* PRIVATE */
png_handle_acTL(png_structp png_ptr, png_infop info_ptr, png_uint_32 length)
{
    png_byte data[8];
    png_uint_32 num_frames;
    png_uint_32 num_plays;
    png_uint_32 didSet;

    png_debug(1, "in png_handle_acTL");

    if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
    {
        png_error(png_ptr, "Missing IHDR before acTL");
    }
    else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0)
    {
        png_warning(png_ptr, "Invalid acTL after IDAT skipped");
        png_crc_finish(png_ptr, length);
        return;
    }
    else if ((png_ptr->mode & PNG_HAVE_acTL) != 0)
    {
        png_warning(png_ptr, "Duplicate acTL skipped");
        png_crc_finish(png_ptr, length);
        return;
    }
    else if (length != 8)
    {
        png_warning(png_ptr, "acTL with invalid length skipped");
        png_crc_finish(png_ptr, length);
        return;
    }

    png_crc_read(png_ptr, data, 8);
    png_crc_finish(png_ptr, 0);

    num_frames = png_get_uint_31(png_ptr, data);
    num_plays = png_get_uint_31(png_ptr, data + 4);

    /* the set function will do error checking on num_frames */
    didSet = png_set_acTL(png_ptr, info_ptr, num_frames, num_plays);
    if (didSet != 0)
        png_ptr->mode |= PNG_HAVE_acTL;
}

void /* PRIVATE */
png_handle_fcTL(png_structp png_ptr, png_infop info_ptr, png_uint_32 length)
{
    png_byte data[22];
    png_uint_32 width;
    png_uint_32 height;
    png_uint_32 x_offset;
    png_uint_32 y_offset;
    png_uint_16 delay_num;
    png_uint_16 delay_den;
    png_byte dispose_op;
    png_byte blend_op;

    png_debug(1, "in png_handle_fcTL");

    png_ensure_sequence_number(png_ptr, length);

    if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
    {
        png_error(png_ptr, "Missing IHDR before fcTL");
    }
    else if ((png_ptr->mode & PNG_HAVE_IDAT) != 0)
    {
        /* for any frames other then the first this message may be misleading,
        * but correct. PNG_HAVE_IDAT is unset before the frame head is read
        * i can't think of a better message */
        png_warning(png_ptr, "Invalid fcTL after IDAT skipped");
        png_crc_finish(png_ptr, length-4);
        return;
    }
    else if ((png_ptr->mode & PNG_HAVE_fcTL) != 0)
    {
        png_warning(png_ptr, "Duplicate fcTL within one frame skipped");
        png_crc_finish(png_ptr, length-4);
        return;
    }
    else if (length != 26)
    {
        png_warning(png_ptr, "fcTL with invalid length skipped");
        png_crc_finish(png_ptr, length-4);
        return;
    }

    png_crc_read(png_ptr, data, 22);
    png_crc_finish(png_ptr, 0);

    width = png_get_uint_31(png_ptr, data);
    height = png_get_uint_31(png_ptr, data + 4);
    x_offset = png_get_uint_31(png_ptr, data + 8);
    y_offset = png_get_uint_31(png_ptr, data + 12);
    delay_num = png_get_uint_16(data + 16);
    delay_den = png_get_uint_16(data + 18);
    dispose_op = data[20];
    blend_op = data[21];

    if (png_ptr->num_frames_read == 0 && (x_offset != 0 || y_offset != 0))
    {
        png_warning(png_ptr, "fcTL for the first frame must have zero offset");
        return;
    }

    if (info_ptr != NULL)
    {
        if (png_ptr->num_frames_read == 0 &&
            (width != info_ptr->width || height != info_ptr->height))
        {
            png_warning(png_ptr, "size in first frame's fcTL must match "
                               "the size in IHDR");
            return;
        }

        /* The set function will do more error checking */
        png_set_next_frame_fcTL(png_ptr, info_ptr, width, height,
                                x_offset, y_offset, delay_num, delay_den,
                                dispose_op, blend_op);

        png_read_reinit(png_ptr, info_ptr);

        png_ptr->mode |= PNG_HAVE_fcTL;
    }
}

void /* PRIVATE */
png_have_info(png_structp png_ptr, png_infop info_ptr)
{
    if ((info_ptr->valid & PNG_INFO_acTL) != 0 &&
        (info_ptr->valid & PNG_INFO_fcTL) == 0)
    {
        png_ptr->apng_flags |= PNG_FIRST_FRAME_HIDDEN;
        info_ptr->num_frames++;
    }
}

void /* PRIVATE */
png_handle_fdAT(png_structp png_ptr, png_infop info_ptr, png_uint_32 length)
{
    png_ensure_sequence_number(png_ptr, length);

    /* This function is only called from png_read_end(), png_read_info(),
    * and png_push_read_chunk() which means that:
    * - the user doesn't want to read this frame
    * - or this is an out-of-place fdAT
    * in either case it is safe to ignore the chunk with a warning */
    png_warning(png_ptr, "ignoring fdAT chunk");
    png_crc_finish(png_ptr, length - 4);
    PNG_UNUSED(info_ptr)
}

void /* PRIVATE */
png_ensure_sequence_number(png_structp png_ptr, png_uint_32 length)
{
    png_byte data[4];
    png_uint_32 sequence_number;

    if (length < 4)
        png_error(png_ptr, "invalid fcTL or fdAT chunk found");

    png_crc_read(png_ptr, data, 4);
    sequence_number = png_get_uint_31(png_ptr, data);

    if (sequence_number != png_ptr->next_seq_num)
        png_error(png_ptr, "fcTL or fdAT chunk with out-of-order sequence "
                           "number found");

    png_ptr->next_seq_num++;
}
#endif /* READ_APNG */

#ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
/* Utility function for png_handle_unknown; set up png_ptr::unknown_chunk */
static int
png_cache_unknown_chunk(png_structrp png_ptr, png_uint_32 length)
{
   const png_alloc_size_t limit = png_chunk_max(png_ptr);

   if (png_ptr->unknown_chunk.data != NULL)
   {
      png_free(png_ptr, png_ptr->unknown_chunk.data);
      png_ptr->unknown_chunk.data = NULL;
   }

   if (length <= limit)
   {
      PNG_CSTRING_FROM_CHUNK(png_ptr->unknown_chunk.name, png_ptr->chunk_name);
      /* The following is safe because of the PNG_SIZE_MAX init above */
      png_ptr->unknown_chunk.size = (size_t)length/*SAFE*/;
      /* 'mode' is a flag array, only the bottom four bits matter here */
      png_ptr->unknown_chunk.location = (png_byte)png_ptr->mode/*SAFE*/;

      if (length == 0)
         png_ptr->unknown_chunk.data = NULL;

      else
      {
         /* Do a 'warn' here - it is handled below. */
         png_ptr->unknown_chunk.data = png_voidcast(png_bytep,
             png_malloc_warn(png_ptr, length));
      }
   }

   if (png_ptr->unknown_chunk.data == NULL && length > 0)
   {
      /* This is benign because we clean up correctly */
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "unknown chunk exceeds memory limits");
      return 0;
   }

   else
   {
      if (length > 0)
         png_crc_read(png_ptr, png_ptr->unknown_chunk.data, length);
      png_crc_finish(png_ptr, 0);
      return 1;
   }
}
#endif /* READ_UNKNOWN_CHUNKS */

/* Handle an unknown, or known but disabled, chunk */
png_handle_result_code /*PRIVATE*/
png_handle_unknown(png_structrp png_ptr, png_inforp info_ptr,
    png_uint_32 length, int keep)
{
   png_handle_result_code handled = handled_discarded; /* the default */

   png_debug(1, "in png_handle_unknown");

#ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
   /* NOTE: this code is based on the code in libpng-1.4.12 except for fixing
    * the bug which meant that setting a non-default behavior for a specific
    * chunk would be ignored (the default was always used unless a user
    * callback was installed).
    *
    * 'keep' is the value from the png_chunk_unknown_handling, the setting for
    * this specific chunk_name, if PNG_HANDLE_AS_UNKNOWN_SUPPORTED, if not it
    * will always be PNG_HANDLE_CHUNK_AS_DEFAULT and it needs to be set here.
    * This is just an optimization to avoid multiple calls to the lookup
    * function.
    */
#  ifndef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
#     ifdef PNG_SET_UNKNOWN_CHUNKS_SUPPORTED
   keep = png_chunk_unknown_handling(png_ptr, png_ptr->chunk_name);
#     endif
#  endif

   /* One of the following methods will read the chunk or skip it (at least one
    * of these is always defined because this is the only way to switch on
    * PNG_READ_UNKNOWN_CHUNKS_SUPPORTED)
    */
#  ifdef PNG_READ_USER_CHUNKS_SUPPORTED
   /* The user callback takes precedence over the chunk keep value, but the
    * keep value is still required to validate a save of a critical chunk.
    */
   if (png_ptr->read_user_chunk_fn != NULL)
   {
      if (png_cache_unknown_chunk(png_ptr, length) != 0)
      {
         /* Callback to user unknown chunk handler */
         int ret = (*(png_ptr->read_user_chunk_fn))(png_ptr,
             &png_ptr->unknown_chunk);

         /* ret is:
          * negative: An error occurred; png_chunk_error will be called.
          *     zero: The chunk was not handled, the chunk will be discarded
          *           unless png_set_keep_unknown_chunks has been used to set
          *           a 'keep' behavior for this particular chunk, in which
          *           case that will be used.  A critical chunk will cause an
          *           error at this point unless it is to be saved.
          * positive: The chunk was handled, libpng will ignore/discard it.
          */
         if (ret < 0) /* handled_error */
            png_chunk_error(png_ptr, "error in user chunk");

         else if (ret == 0)
         {
            /* If the keep value is 'default' or 'never' override it, but
             * still error out on critical chunks unless the keep value is
             * 'always'  While this is weird it is the behavior in 1.4.12.
             * A possible improvement would be to obey the value set for the
             * chunk, but this would be an API change that would probably
             * damage some applications.
             *
             * The png_app_warning below catches the case that matters, where
             * the application has not set specific save or ignore for this
             * chunk or global save or ignore.
             */
            if (keep < PNG_HANDLE_CHUNK_IF_SAFE)
            {
#              ifdef PNG_SET_UNKNOWN_CHUNKS_SUPPORTED
               if (png_ptr->unknown_default < PNG_HANDLE_CHUNK_IF_SAFE)
               {
                  png_chunk_warning(png_ptr, "Saving unknown chunk:");
                  png_app_warning(png_ptr,
                      "forcing save of an unhandled chunk;"
                      " please call png_set_keep_unknown_chunks");
                      /* with keep = PNG_HANDLE_CHUNK_IF_SAFE */
               }
#              endif
               keep = PNG_HANDLE_CHUNK_IF_SAFE;
            }
         }

         else /* chunk was handled */
         {
            handled = handled_ok;
            /* Critical chunks can be safely discarded at this point. */
            keep = PNG_HANDLE_CHUNK_NEVER;
         }
      }

      else
         keep = PNG_HANDLE_CHUNK_NEVER; /* insufficient memory */
   }

   else
   /* Use the SAVE_UNKNOWN_CHUNKS code or skip the chunk */
#  endif /* READ_USER_CHUNKS */

#  ifdef PNG_SAVE_UNKNOWN_CHUNKS_SUPPORTED
   {
      /* keep is currently just the per-chunk setting, if there was no
       * setting change it to the global default now (not that this may
       * still be AS_DEFAULT) then obtain the cache of the chunk if required,
       * if not simply skip the chunk.
       */
      if (keep == PNG_HANDLE_CHUNK_AS_DEFAULT)
         keep = png_ptr->unknown_default;

      if (keep == PNG_HANDLE_CHUNK_ALWAYS ||
         (keep == PNG_HANDLE_CHUNK_IF_SAFE &&
          PNG_CHUNK_ANCILLARY(png_ptr->chunk_name)))
      {
         if (png_cache_unknown_chunk(png_ptr, length) == 0)
            keep = PNG_HANDLE_CHUNK_NEVER;
      }

      else
         png_crc_finish(png_ptr, length);
   }
#  else
#     ifndef PNG_READ_USER_CHUNKS_SUPPORTED
#        error no method to support READ_UNKNOWN_CHUNKS
#     endif

   {
      /* If here there is no read callback pointer set and no support is
       * compiled in to just save the unknown chunks, so simply skip this
       * chunk.  If 'keep' is something other than AS_DEFAULT or NEVER then
       * the app has erroneously asked for unknown chunk saving when there
       * is no support.
       */
      if (keep > PNG_HANDLE_CHUNK_NEVER)
         png_app_error(png_ptr, "no unknown chunk support available");

      png_crc_finish(png_ptr, length);
   }
#  endif

#  ifdef PNG_STORE_UNKNOWN_CHUNKS_SUPPORTED
   /* Now store the chunk in the chunk list if appropriate, and if the limits
    * permit it.
    */
   if (keep == PNG_HANDLE_CHUNK_ALWAYS ||
      (keep == PNG_HANDLE_CHUNK_IF_SAFE &&
       PNG_CHUNK_ANCILLARY(png_ptr->chunk_name)))
   {
#     ifdef PNG_USER_LIMITS_SUPPORTED
      switch (png_ptr->user_chunk_cache_max)
      {
         case 2:
            png_ptr->user_chunk_cache_max = 1;
            png_chunk_benign_error(png_ptr, "no space in chunk cache");
            /* FALLTHROUGH */
         case 1:
            /* NOTE: prior to 1.6.0 this case resulted in an unknown critical
             * chunk being skipped, now there will be a hard error below.
             */
            break;

         default: /* not at limit */
            --(png_ptr->user_chunk_cache_max);
            /* FALLTHROUGH */
         case 0: /* no limit */
#  endif /* USER_LIMITS */
            /* Here when the limit isn't reached or when limits are compiled
             * out; store the chunk.
             */
            png_set_unknown_chunks(png_ptr, info_ptr,
                &png_ptr->unknown_chunk, 1);
            handled = handled_saved;
#  ifdef PNG_USER_LIMITS_SUPPORTED
            break;
      }
#  endif
   }
#  else /* no store support: the chunk must be handled by the user callback */
   PNG_UNUSED(info_ptr)
#  endif

   /* Regardless of the error handling below the cached data (if any) can be
    * freed now.  Notice that the data is not freed if there is a png_error, but
    * it will be freed by destroy_read_struct.
    */
   if (png_ptr->unknown_chunk.data != NULL)
      png_free(png_ptr, png_ptr->unknown_chunk.data);
   png_ptr->unknown_chunk.data = NULL;

#else /* !PNG_READ_UNKNOWN_CHUNKS_SUPPORTED */
   /* There is no support to read an unknown chunk, so just skip it. */
   png_crc_finish(png_ptr, length);
   PNG_UNUSED(info_ptr)
   PNG_UNUSED(keep)
#endif /* !READ_UNKNOWN_CHUNKS */

   /* Check for unhandled critical chunks */
   if (handled < handled_saved && PNG_CHUNK_CRITICAL(png_ptr->chunk_name))
      png_chunk_error(png_ptr, "unhandled critical chunk");

   return handled;
}

/* APNG handling: the minimal implementation of APNG handling in libpng 1.6
 * requires that those significant applications which already handle APNG not
 * get hosed.  To do this ensure the code here will have to ensure than APNG
 * data by default (at least in 1.6) gets stored in the unknown chunk list.
 * Maybe this can be relaxed in a few years but at present it's just the only
 * safe way.
 *
 * ATM just cause unknown handling for all three chunks:
 */
#define png_handle_acTL NULL
#define png_handle_fcTL NULL
#define png_handle_fdAT NULL

/*
 * 1.6.47: This is the new table driven interface to all the chunk handling.
 *
 * The table describes the PNG standard rules for **reading** known chunks -
 * every chunk which has an entry in PNG_KNOWN_CHUNKS.  The table contains an
 * entry for each PNG_INDEX_cHNK describing the rules.
 *
 * In this initial version the only information in the entry is the
 * png_handle_cHNK function for the chunk in question.  When chunk support is
 * compiled out the entry will be NULL.
 */
static const struct
{
   png_handle_result_code (*handler)(
         png_structrp, png_inforp, png_uint_32 length);
      /* A chunk-specific 'handler', NULL if the chunk is not supported in this
       * build.
       */

   /* Crushing these values helps on modern 32-bit architectures because the
    * pointer and the following bit fields both end up requiring 32 bits.
    * Typically this will halve the table size.  On 64-bit architectures the
    * table entries will typically be 8 bytes.
    */
   png_uint_32 max_length :12; /* Length min, max in bytes */
   png_uint_32 min_length :8;
      /* Length errors on critical chunks have special handling to preserve the
       * existing behaviour in libpng 1.6.  Anciallary chunks are checked below
       * and produce a 'benign' error.
       */
   png_uint_32 pos_before :4; /* PNG_HAVE_ values chunk must precede */
   png_uint_32 pos_after  :4; /* PNG_HAVE_ values chunk must follow */
      /* NOTE: PLTE, tRNS and bKGD require special handling which depends on
       * the colour type of the base image.
       */
   png_uint_32 multiple   :1; /* Multiple occurences permitted */
      /* This is enabled for PLTE because PLTE may, in practice, be optional */
}
read_chunks[PNG_INDEX_unknown] =
{
   /* Definitions as above but done indirectly by #define so that
    * PNG_KNOWN_CHUNKS can be used safely to build the table in order.
    *
    * Each CDcHNK definition lists the values for the parameters **after**
    * the first, 'handler', function.  'handler' is NULL when the chunk has no
    * compiled in support.
    */
#  define NoCheck 0x801U      /* Do not check the maximum length */
#  define Limit   0x802U      /* Limit to png_chunk_max bytes */
#  define LKMin   3U+LZ77Min  /* Minimum length of keyword+LZ77 */

#define hIHDR PNG_HAVE_IHDR
#define hPLTE PNG_HAVE_PLTE
#define hIDAT PNG_HAVE_IDAT
   /* For the two chunks, tRNS and bKGD which can occur in PNGs without a PLTE
    * but must occur after the PLTE use this and put the check in the handler
    * routine for colour mapped images were PLTE is required.  Also put a check
    * in PLTE for other image types to drop the PLTE if tRNS or bKGD have been
    * seen.
    */
#define hCOL  (PNG_HAVE_PLTE|PNG_HAVE_IDAT)
   /* Used for the decoding chunks which must be before PLTE. */
#define aIDAT PNG_AFTER_IDAT

   /* Chunks from W3C PNG v3: */
   /*       cHNK  max_len,   min, before, after, multiple */
#  define CDIHDR      13U,   13U,  hIHDR,     0,        0
#  define CDPLTE  NoCheck,    0U,      0, hIHDR,        1
      /* PLTE errors are only critical for colour-map images, consequently the
       * hander does all the checks.
       */
#  define CDIDAT  NoCheck,    0U,  aIDAT, hIHDR,        1
#  define CDIEND  NoCheck,    0U,      0, aIDAT,        0
      /* Historically data was allowed in IEND */
#  define CDtRNS     256U,    0U,  hIDAT, hIHDR,        0
#  define CDcHRM      32U,   32U,   hCOL, hIHDR,        0
#  define CDgAMA       4U,    4U,   hCOL, hIHDR,        0
#  define CDiCCP  NoCheck, LKMin,   hCOL, hIHDR,        0
#  define CDsBIT       4U,    1U,   hCOL, hIHDR,        0
#  define CDsRGB       1U,    1U,   hCOL, hIHDR,        0
#  define CDcICP       4U,    4U,   hCOL, hIHDR,        0
#  define CDmDCV      24U,   24U,   hCOL, hIHDR,        0
#  define CDeXIf    Limit,    4U,      0, hIHDR,        0
#  define CDcLLI       8U,    8U,   hCOL, hIHDR,        0
#  define CDtEXt  NoCheck,    2U,      0, hIHDR,        1
      /* Allocates 'length+1'; checked in the handler */
#  define CDzTXt    Limit, LKMin,      0, hIHDR,        1
#  define CDiTXt  NoCheck,    6U,      0, hIHDR,        1
      /* Allocates 'length+1'; checked in the handler */
#  define CDbKGD       6U,    1U,  hIDAT, hIHDR,        0
#  define CDhIST    1024U,    0U,  hPLTE, hIHDR,        0
#  define CDpHYs       9U,    9U,  hIDAT, hIHDR,        0
#  define CDsPLT  NoCheck,    3U,  hIDAT, hIHDR,        1
      /* Allocates 'length+1'; checked in the handler */
#  define CDtIME       7U,    7U,      0, hIHDR,        0
#  define CDacTL       8U,    8U,  hIDAT, hIHDR,        0
#  define CDfcTL      25U,   26U,      0, hIHDR,        1
#  define CDfdAT    Limit,    4U,  hIDAT, hIHDR,        1
   /* Supported chunks from PNG extensions 1.5.0, NYI so limit */
#  define CDoFFs       9U,    9U,  hIDAT, hIHDR,        0
#  define CDpCAL  NoCheck,   14U,  hIDAT, hIHDR,        0
      /* Allocates 'length+1'; checked in the handler */
#  define CDsCAL    Limit,    4U,  hIDAT, hIHDR,        0
      /* Allocates 'length+1'; checked in the handler */

#  define PNG_CHUNK(cHNK, index) { png_handle_ ## cHNK, CD ## cHNK },
   PNG_KNOWN_CHUNKS
#  undef PNG_CHUNK
};


static png_index
png_chunk_index_from_name(png_uint_32 chunk_name)
{
   /* For chunk png_cHNK return PNG_INDEX_cHNK.  Return PNG_INDEX_unknown if
    * chunk_name is not known.  Notice that in a particular build "known" does
    * not necessarily mean "supported", although the inverse applies.
    */
   switch (chunk_name)
   {
#     define PNG_CHUNK(cHNK, index)\
         case png_ ## cHNK: return PNG_INDEX_ ## cHNK; /* == index */

      PNG_KNOWN_CHUNKS

#     undef PNG_CHUNK

      default: return PNG_INDEX_unknown;
   }
}

png_handle_result_code /*PRIVATE*/
png_handle_chunk(png_structrp png_ptr, png_inforp info_ptr, png_uint_32 length)
{
   /* CSE: these things don't change, these autos are just to save typing and
    * make the code more clear.
    */
   const png_uint_32 chunk_name = png_ptr->chunk_name;
   const png_index chunk_index = png_chunk_index_from_name(chunk_name);

   png_handle_result_code handled = handled_error;
   png_const_charp errmsg = NULL;

   /* Is this a known chunk?  If not there are no checks performed here;
    * png_handle_unknown does the correct checks.  This means that the values
    * for known but unsupported chunks in the above table are not used here
    * however the chunks_seen fields in png_struct are still set.
    */
   if (chunk_index == PNG_INDEX_unknown ||
       read_chunks[chunk_index].handler == NULL)
   {
      handled = png_handle_unknown(
            png_ptr, info_ptr, length, PNG_HANDLE_CHUNK_AS_DEFAULT);
   }

   /* First check the position.   The first check is historical; the stream must
    * start with IHDR and anything else causes libpng to give up immediately.
    */
   else if (chunk_index != PNG_INDEX_IHDR &&
            (png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_chunk_error(png_ptr, "missing IHDR"); /* NORETURN */

   /* Before all the pos_before chunks, after all the pos_after chunks. */
   else if (((png_ptr->mode & read_chunks[chunk_index].pos_before) != 0) ||
            ((png_ptr->mode & read_chunks[chunk_index].pos_after) !=
             read_chunks[chunk_index].pos_after))
   {
      errmsg = "out of place";
   }

   /* Now check for duplicates: duplicated critical chunks also produce a
    * full error.
    */
   else if (read_chunks[chunk_index].multiple == 0 &&
            png_file_has_chunk(png_ptr, chunk_index))
   {
      errmsg = "duplicate";
   }

   else if (length < read_chunks[chunk_index].min_length)
      errmsg = "too short";
   else
   {
      /* NOTE: apart from IHDR the critical chunks (PLTE, IDAT and IEND) are set
       * up above not to do any length checks.
       *
       * The png_chunk_max check ensures that the variable length chunks are
       * always checked at this point for being within the system allocation
       * limits.
       */
      unsigned max_length = read_chunks[chunk_index].max_length;

      switch (max_length)
      {
         case Limit:
            /* png_read_chunk_header has already png_error'ed chunks with a
             * length exceeding the 31-bit PNG limit, so just check the memory
             * limit:
             */
            if (length <= png_chunk_max(png_ptr))
               goto MeetsLimit;

            errmsg = "length exceeds libpng limit";
            break;

         default:
            if (length <= max_length)
               goto MeetsLimit;

            errmsg = "too long";
            break;

         case NoCheck:
         MeetsLimit:
            handled = read_chunks[chunk_index].handler(
                  png_ptr, info_ptr, length);
            break;
      }
   }

   /* If there was an error or the chunk was simply skipped it is not counted as
    * 'seen'.
    */
   if (errmsg != NULL)
   {
      if (PNG_CHUNK_CRITICAL(chunk_name)) /* stop immediately */
         png_chunk_error(png_ptr, errmsg);
      else /* ancillary chunk */
      {
         /* The chunk data is skipped: */
         png_crc_finish(png_ptr, length);
         png_chunk_benign_error(png_ptr, errmsg);
      }
   }

   else if (handled >= handled_saved)
   {
      if (chunk_index != PNG_INDEX_unknown)
         png_file_add_chunk(png_ptr, chunk_index);
   }

   return handled;
}

/* Combines the row recently read in with the existing pixels in the row.  This
 * routine takes care of alpha and transparency if requested.  This routine also
 * handles the two methods of progressive display of interlaced images,
 * depending on the 'display' value; if 'display' is true then the whole row
 * (dp) is filled from the start by replicating the available pixels.  If
 * 'display' is false only those pixels present in the pass are filled in.
 */
void /* PRIVATE */
png_combine_row(png_const_structrp png_ptr, png_bytep dp, int display)
{
   unsigned int pixel_depth = png_ptr->transformed_pixel_depth;
   png_const_bytep sp = png_ptr->row_buf + 1;
   png_alloc_size_t row_width = png_ptr->width;
   unsigned int pass = png_ptr->pass;
   png_bytep end_ptr = 0;
   png_byte end_byte = 0;
   unsigned int end_mask;

   png_debug(1, "in png_combine_row");

   /* Added in 1.5.6: it should not be possible to enter this routine until at
    * least one row has been read from the PNG data and transformed.
    */
   if (pixel_depth == 0)
      png_error(png_ptr, "internal row logic error");

   /* Added in 1.5.4: the pixel depth should match the information returned by
    * any call to png_read_update_info at this point.  Do not continue if we got
    * this wrong.
    */
   if (png_ptr->info_rowbytes != 0 && png_ptr->info_rowbytes !=
          PNG_ROWBYTES(pixel_depth, row_width))
      png_error(png_ptr, "internal row size calculation error");

   /* Don't expect this to ever happen: */
   if (row_width == 0)
      png_error(png_ptr, "internal row width error");

   /* Preserve the last byte in cases where only part of it will be overwritten,
    * the multiply below may overflow, we don't care because ANSI-C guarantees
    * we get the low bits.
    */
   end_mask = (pixel_depth * row_width) & 7;
   if (end_mask != 0)
   {
      /* end_ptr == NULL is a flag to say do nothing */
      end_ptr = dp + PNG_ROWBYTES(pixel_depth, row_width) - 1;
      end_byte = *end_ptr;
#     ifdef PNG_READ_PACKSWAP_SUPPORTED
      if ((png_ptr->transformations & PNG_PACKSWAP) != 0)
         /* little-endian byte */
         end_mask = (unsigned int)(0xff << end_mask);

      else /* big-endian byte */
#     endif
      end_mask = 0xff >> end_mask;
      /* end_mask is now the bits to *keep* from the destination row */
   }

   /* For non-interlaced images this reduces to a memcpy(). A memcpy()
    * will also happen if interlacing isn't supported or if the application
    * does not call png_set_interlace_handling().  In the latter cases the
    * caller just gets a sequence of the unexpanded rows from each interlace
    * pass.
    */
#ifdef PNG_READ_INTERLACING_SUPPORTED
   if (png_ptr->interlaced != 0 &&
       (png_ptr->transformations & PNG_INTERLACE) != 0 &&
       pass < 6 && (display == 0 ||
       /* The following copies everything for 'display' on passes 0, 2 and 4. */
       (display == 1 && (pass & 1) != 0)))
   {
      /* Narrow images may have no bits in a pass; the caller should handle
       * this, but this test is cheap:
       */
      if (row_width <= PNG_PASS_START_COL(pass))
         return;

      if (pixel_depth < 8)
      {
         /* For pixel depths up to 4 bpp the 8-pixel mask can be expanded to fit
          * into 32 bits, then a single loop over the bytes using the four byte
          * values in the 32-bit mask can be used.  For the 'display' option the
          * expanded mask may also not require any masking within a byte.  To
          * make this work the PACKSWAP option must be taken into account - it
          * simply requires the pixels to be reversed in each byte.
          *
          * The 'regular' case requires a mask for each of the first 6 passes,
          * the 'display' case does a copy for the even passes in the range
          * 0..6.  This has already been handled in the test above.
          *
          * The masks are arranged as four bytes with the first byte to use in
          * the lowest bits (little-endian) regardless of the order (PACKSWAP or
          * not) of the pixels in each byte.
          *
          * NOTE: the whole of this logic depends on the caller of this function
          * only calling it on rows appropriate to the pass.  This function only
          * understands the 'x' logic; the 'y' logic is handled by the caller.
          *
          * The following defines allow generation of compile time constant bit
          * masks for each pixel depth and each possibility of swapped or not
          * swapped bytes.  Pass 'p' is in the range 0..6; 'x', a pixel index,
          * is in the range 0..7; and the result is 1 if the pixel is to be
          * copied in the pass, 0 if not.  'S' is for the sparkle method, 'B'
          * for the block method.
          *
          * With some compilers a compile time expression of the general form:
          *
          *    (shift >= 32) ? (a >> (shift-32)) : (b >> shift)
          *
          * Produces warnings with values of 'shift' in the range 33 to 63
          * because the right hand side of the ?: expression is evaluated by
          * the compiler even though it isn't used.  Microsoft Visual C (various
          * versions) and the Intel C compiler are known to do this.  To avoid
          * this the following macros are used in 1.5.6.  This is a temporary
          * solution to avoid destabilizing the code during the release process.
          */
#        if PNG_USE_COMPILE_TIME_MASKS
#           define PNG_LSR(x,s) ((x)>>((s) & 0x1f))
#           define PNG_LSL(x,s) ((x)<<((s) & 0x1f))
#        else
#           define PNG_LSR(x,s) ((x)>>(s))
#           define PNG_LSL(x,s) ((x)<<(s))
#        endif
#        define S_COPY(p,x) (((p)<4 ? PNG_LSR(0x80088822,(3-(p))*8+(7-(x))) :\
           PNG_LSR(0xaa55ff00,(7-(p))*8+(7-(x)))) & 1)
#        define B_COPY(p,x) (((p)<4 ? PNG_LSR(0xff0fff33,(3-(p))*8+(7-(x))) :\
           PNG_LSR(0xff55ff00,(7-(p))*8+(7-(x)))) & 1)

         /* Return a mask for pass 'p' pixel 'x' at depth 'd'.  The mask is
          * little endian - the first pixel is at bit 0 - however the extra
          * parameter 's' can be set to cause the mask position to be swapped
          * within each byte, to match the PNG format.  This is done by XOR of
          * the shift with 7, 6 or 4 for bit depths 1, 2 and 4.
          */
#        define PIXEL_MASK(p,x,d,s) \
            (PNG_LSL(((PNG_LSL(1U,(d)))-1),(((x)*(d))^((s)?8-(d):0))))

         /* Hence generate the appropriate 'block' or 'sparkle' pixel copy mask.
          */
#        define S_MASKx(p,x,d,s) (S_COPY(p,x)?PIXEL_MASK(p,x,d,s):0)
#        define B_MASKx(p,x,d,s) (B_COPY(p,x)?PIXEL_MASK(p,x,d,s):0)

         /* Combine 8 of these to get the full mask.  For the 1-bpp and 2-bpp
          * cases the result needs replicating, for the 4-bpp case the above
          * generates a full 32 bits.
          */
#        define MASK_EXPAND(m,d) ((m)*((d)==1?0x01010101:((d)==2?0x00010001:1)))

#        define S_MASK(p,d,s) MASK_EXPAND(S_MASKx(p,0,d,s) + S_MASKx(p,1,d,s) +\
            S_MASKx(p,2,d,s) + S_MASKx(p,3,d,s) + S_MASKx(p,4,d,s) +\
            S_MASKx(p,5,d,s) + S_MASKx(p,6,d,s) + S_MASKx(p,7,d,s), d)

#        define B_MASK(p,d,s) MASK_EXPAND(B_MASKx(p,0,d,s) + B_MASKx(p,1,d,s) +\
            B_MASKx(p,2,d,s) + B_MASKx(p,3,d,s) + B_MASKx(p,4,d,s) +\
            B_MASKx(p,5,d,s) + B_MASKx(p,6,d,s) + B_MASKx(p,7,d,s), d)

#if PNG_USE_COMPILE_TIME_MASKS
         /* Utility macros to construct all the masks for a depth/swap
          * combination.  The 's' parameter says whether the format is PNG
          * (big endian bytes) or not.  Only the three odd-numbered passes are
          * required for the display/block algorithm.
          */
#        define S_MASKS(d,s) { S_MASK(0,d,s), S_MASK(1,d,s), S_MASK(2,d,s),\
            S_MASK(3,d,s), S_MASK(4,d,s), S_MASK(5,d,s) }

#        define B_MASKS(d,s) { B_MASK(1,d,s), B_MASK(3,d,s), B_MASK(5,d,s) }

#        define DEPTH_INDEX(d) ((d)==1?0:((d)==2?1:2))

         /* Hence the pre-compiled masks indexed by PACKSWAP (or not), depth and
          * then pass:
          */
         static const png_uint_32 row_mask[2/*PACKSWAP*/][3/*depth*/][6] =
         {
            /* Little-endian byte masks for PACKSWAP */
            { S_MASKS(1,0), S_MASKS(2,0), S_MASKS(4,0) },
            /* Normal (big-endian byte) masks - PNG format */
            { S_MASKS(1,1), S_MASKS(2,1), S_MASKS(4,1) }
         };

         /* display_mask has only three entries for the odd passes, so index by
          * pass>>1.
          */
         static const png_uint_32 display_mask[2][3][3] =
         {
            /* Little-endian byte masks for PACKSWAP */
            { B_MASKS(1,0), B_MASKS(2,0), B_MASKS(4,0) },
            /* Normal (big-endian byte) masks - PNG format */
            { B_MASKS(1,1), B_MASKS(2,1), B_MASKS(4,1) }
         };

#        define MASK(pass,depth,display,png)\
            ((display)?display_mask[png][DEPTH_INDEX(depth)][pass>>1]:\
               row_mask[png][DEPTH_INDEX(depth)][pass])

#else /* !PNG_USE_COMPILE_TIME_MASKS */
         /* This is the runtime alternative: it seems unlikely that this will
          * ever be either smaller or faster than the compile time approach.
          */
#        define MASK(pass,depth,display,png)\
            ((display)?B_MASK(pass,depth,png):S_MASK(pass,depth,png))
#endif /* !USE_COMPILE_TIME_MASKS */

         /* Use the appropriate mask to copy the required bits.  In some cases
          * the byte mask will be 0 or 0xff; optimize these cases.  row_width is
          * the number of pixels, but the code copies bytes, so it is necessary
          * to special case the end.
          */
         png_uint_32 pixels_per_byte = 8 / pixel_depth;
         png_uint_32 mask;

#        ifdef PNG_READ_PACKSWAP_SUPPORTED
         if ((png_ptr->transformations & PNG_PACKSWAP) != 0)
            mask = MASK(pass, pixel_depth, display, 0);

         else
#        endif
         mask = MASK(pass, pixel_depth, display, 1);

         for (;;)
         {
            png_uint_32 m;

            /* It doesn't matter in the following if png_uint_32 has more than
             * 32 bits because the high bits always match those in m<<24; it is,
             * however, essential to use OR here, not +, because of this.
             */
            m = mask;
            mask = (m >> 8) | (m << 24); /* rotate right to good compilers */
            m &= 0xff;

            if (m != 0) /* something to copy */
            {
               if (m != 0xff)
                  *dp = (png_byte)((*dp & ~m) | (*sp & m));
               else
                  *dp = *sp;
            }

            /* NOTE: this may overwrite the last byte with garbage if the image
             * is not an exact number of bytes wide; libpng has always done
             * this.
             */
            if (row_width <= pixels_per_byte)
               break; /* May need to restore part of the last byte */

            row_width -= pixels_per_byte;
            ++dp;
            ++sp;
         }
      }

      else /* pixel_depth >= 8 */
      {
         unsigned int bytes_to_copy, bytes_to_jump;

         /* Validate the depth - it must be a multiple of 8 */
         if (pixel_depth & 7)
            png_error(png_ptr, "invalid user transform pixel depth");

         pixel_depth >>= 3; /* now in bytes */
         row_width *= pixel_depth;

         /* Regardless of pass number the Adam 7 interlace always results in a
          * fixed number of pixels to copy then to skip.  There may be a
          * different number of pixels to skip at the start though.
          */
         {
            unsigned int offset = PNG_PASS_START_COL(pass) * pixel_depth;

            row_width -= offset;
            dp += offset;
            sp += offset;
         }

         /* Work out the bytes to copy. */
         if (display != 0)
         {
            /* When doing the 'block' algorithm the pixel in the pass gets
             * replicated to adjacent pixels.  This is why the even (0,2,4,6)
             * passes are skipped above - the entire expanded row is copied.
             */
            bytes_to_copy = (1<<((6-pass)>>1)) * pixel_depth;

            /* But don't allow this number to exceed the actual row width. */
            if (bytes_to_copy > row_width)
               bytes_to_copy = (unsigned int)/*SAFE*/row_width;
         }

         else /* normal row; Adam7 only ever gives us one pixel to copy. */
            bytes_to_copy = pixel_depth;

         /* In Adam7 there is a constant offset between where the pixels go. */
         bytes_to_jump = PNG_PASS_COL_OFFSET(pass) * pixel_depth;

         /* And simply copy these bytes.  Some optimization is possible here,
          * depending on the value of 'bytes_to_copy'.  Special case the low
          * byte counts, which we know to be frequent.
          *
          * Notice that these cases all 'return' rather than 'break' - this
          * avoids an unnecessary test on whether to restore the last byte
          * below.
          */
         switch (bytes_to_copy)
         {
            case 1:
               for (;;)
               {
                  *dp = *sp;

                  if (row_width <= bytes_to_jump)
                     return;

                  dp += bytes_to_jump;
                  sp += bytes_to_jump;
                  row_width -= bytes_to_jump;
               }

            case 2:
               /* There is a possibility of a partial copy at the end here; this
                * slows the code down somewhat.
                */
               do
               {
                  dp[0] = sp[0]; dp[1] = sp[1];

                  if (row_width <= bytes_to_jump)
                     return;

                  sp += bytes_to_jump;
                  dp += bytes_to_jump;
                  row_width -= bytes_to_jump;
               }
               while (row_width > 1);

               /* And there can only be one byte left at this point: */
               *dp = *sp;
               return;

            case 3:
               /* This can only be the RGB case, so each copy is exactly one
                * pixel and it is not necessary to check for a partial copy.
                */
               for (;;)
               {
                  dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];

                  if (row_width <= bytes_to_jump)
                     return;

                  sp += bytes_to_jump;
                  dp += bytes_to_jump;
                  row_width -= bytes_to_jump;
               }

            default:
#if PNG_ALIGN_TYPE != PNG_ALIGN_NONE
               /* Check for double byte alignment and, if possible, use a
                * 16-bit copy.  Don't attempt this for narrow images - ones that
                * are less than an interlace panel wide.  Don't attempt it for
                * wide bytes_to_copy either - use the memcpy there.
                */
               if (bytes_to_copy < 16 /*else use memcpy*/ &&
                   png_isaligned(dp, png_uint_16) &&
                   png_isaligned(sp, png_uint_16) &&
                   bytes_to_copy % (sizeof (png_uint_16)) == 0 &&
                   bytes_to_jump % (sizeof (png_uint_16)) == 0)
               {
                  /* Everything is aligned for png_uint_16 copies, but try for
                   * png_uint_32 first.
                   */
                  if (png_isaligned(dp, png_uint_32) &&
                      png_isaligned(sp, png_uint_32) &&
                      bytes_to_copy % (sizeof (png_uint_32)) == 0 &&
                      bytes_to_jump % (sizeof (png_uint_32)) == 0)
                  {
                     png_uint_32p dp32 = png_aligncast(png_uint_32p,dp);
                     png_const_uint_32p sp32 = png_aligncastconst(
                         png_const_uint_32p, sp);
                     size_t skip = (bytes_to_jump-bytes_to_copy) /
                         (sizeof (png_uint_32));

                     do
                     {
                        size_t c = bytes_to_copy;
                        do
                        {
                           *dp32++ = *sp32++;
                           c -= (sizeof (png_uint_32));
                        }
                        while (c > 0);

                        if (row_width <= bytes_to_jump)
                           return;

                        dp32 += skip;
                        sp32 += skip;
                        row_width -= bytes_to_jump;
                     }
                     while (bytes_to_copy <= row_width);

                     /* Get to here when the row_width truncates the final copy.
                      * There will be 1-3 bytes left to copy, so don't try the
                      * 16-bit loop below.
                      */
                     dp = (png_bytep)dp32;
                     sp = (png_const_bytep)sp32;
                     do
                        *dp++ = *sp++;
                     while (--row_width > 0);
                     return;
                  }

                  /* Else do it in 16-bit quantities, but only if the size is
                   * not too large.
                   */
                  else
                  {
                     png_uint_16p dp16 = png_aligncast(png_uint_16p, dp);
                     png_const_uint_16p sp16 = png_aligncastconst(
                        png_const_uint_16p, sp);
                     size_t skip = (bytes_to_jump-bytes_to_copy) /
                        (sizeof (png_uint_16));

                     do
                     {
                        size_t c = bytes_to_copy;
                        do
                        {
                           *dp16++ = *sp16++;
                           c -= (sizeof (png_uint_16));
                        }
                        while (c > 0);

                        if (row_width <= bytes_to_jump)
                           return;

                        dp16 += skip;
                        sp16 += skip;
                        row_width -= bytes_to_jump;
                     }
                     while (bytes_to_copy <= row_width);

                     /* End of row - 1 byte left, bytes_to_copy > row_width: */
                     dp = (png_bytep)dp16;
                     sp = (png_const_bytep)sp16;
                     do
                        *dp++ = *sp++;
                     while (--row_width > 0);
                     return;
                  }
               }
#endif /* ALIGN_TYPE code */

               /* The true default - use a memcpy: */
               for (;;)
               {
                  memcpy(dp, sp, bytes_to_copy);

                  if (row_width <= bytes_to_jump)
                     return;

                  sp += bytes_to_jump;
                  dp += bytes_to_jump;
                  row_width -= bytes_to_jump;
                  if (bytes_to_copy > row_width)
                     bytes_to_copy = (unsigned int)/*SAFE*/row_width;
               }
         }

         /* NOT REACHED*/
      } /* pixel_depth >= 8 */

      /* Here if pixel_depth < 8 to check 'end_ptr' below. */
   }
   else
#endif /* READ_INTERLACING */

   /* If here then the switch above wasn't used so just memcpy the whole row
    * from the temporary row buffer (notice that this overwrites the end of the
    * destination row if it is a partial byte.)
    */
   memcpy(dp, sp, PNG_ROWBYTES(pixel_depth, row_width));

   /* Restore the overwritten bits from the last byte if necessary. */
   if (end_ptr != NULL)
      *end_ptr = (png_byte)((end_byte & end_mask) | (*end_ptr & ~end_mask));
}

#ifdef PNG_READ_INTERLACING_SUPPORTED
void /* PRIVATE */
png_do_read_interlace(png_row_infop row_info, png_bytep row, int pass,
    png_uint_32 transformations /* Because these may affect the byte layout */)
{
   png_debug(1, "in png_do_read_interlace");
   if (row != NULL && row_info != NULL)
   {
      png_uint_32 final_width;

      final_width = row_info->width * png_pass_inc[pass];

      switch (row_info->pixel_depth)
      {
         case 1:
         {
            png_bytep sp = row + (size_t)((row_info->width - 1) >> 3);
            png_bytep dp = row + (size_t)((final_width - 1) >> 3);
            unsigned int sshift, dshift;
            unsigned int s_start, s_end;
            int s_inc;
            int jstop = (int)png_pass_inc[pass];
            png_byte v;
            png_uint_32 i;
            int j;

#ifdef PNG_READ_PACKSWAP_SUPPORTED
            if ((transformations & PNG_PACKSWAP) != 0)
            {
                sshift = ((row_info->width + 7) & 0x07);
                dshift = ((final_width + 7) & 0x07);
                s_start = 7;
                s_end = 0;
                s_inc = -1;
            }

            else
#endif
            {
                sshift = 7 - ((row_info->width + 7) & 0x07);
                dshift = 7 - ((final_width + 7) & 0x07);
                s_start = 0;
                s_end = 7;
                s_inc = 1;
            }

            for (i = 0; i < row_info->width; i++)
            {
               v = (png_byte)((*sp >> sshift) & 0x01);
               for (j = 0; j < jstop; j++)
               {
                  unsigned int tmp = *dp & (0x7f7f >> (7 - dshift));
                  tmp |= (unsigned int)(v << dshift);
                  *dp = (png_byte)(tmp & 0xff);

                  if (dshift == s_end)
                  {
                     dshift = s_start;
                     dp--;
                  }

                  else
                     dshift = (unsigned int)((int)dshift + s_inc);
               }

               if (sshift == s_end)
               {
                  sshift = s_start;
                  sp--;
               }

               else
                  sshift = (unsigned int)((int)sshift + s_inc);
            }
            break;
         }

         case 2:
         {
            png_bytep sp = row + (png_uint_32)((row_info->width - 1) >> 2);
            png_bytep dp = row + (png_uint_32)((final_width - 1) >> 2);
            unsigned int sshift, dshift;
            unsigned int s_start, s_end;
            int s_inc;
            int jstop = (int)png_pass_inc[pass];
            png_uint_32 i;

#ifdef PNG_READ_PACKSWAP_SUPPORTED
            if ((transformations & PNG_PACKSWAP) != 0)
            {
               sshift = (((row_info->width + 3) & 0x03) << 1);
               dshift = (((final_width + 3) & 0x03) << 1);
               s_start = 6;
               s_end = 0;
               s_inc = -2;
            }

            else
#endif
            {
               sshift = ((3 - ((row_info->width + 3) & 0x03)) << 1);
               dshift = ((3 - ((final_width + 3) & 0x03)) << 1);
               s_start = 0;
               s_end = 6;
               s_inc = 2;
            }

            for (i = 0; i < row_info->width; i++)
            {
               png_byte v;
               int j;

               v = (png_byte)((*sp >> sshift) & 0x03);
               for (j = 0; j < jstop; j++)
               {
                  unsigned int tmp = *dp & (0x3f3f >> (6 - dshift));
                  tmp |= (unsigned int)(v << dshift);
                  *dp = (png_byte)(tmp & 0xff);

                  if (dshift == s_end)
                  {
                     dshift = s_start;
                     dp--;
                  }

                  else
                     dshift = (unsigned int)((int)dshift + s_inc);
               }

               if (sshift == s_end)
               {
                  sshift = s_start;
                  sp--;
               }

               else
                  sshift = (unsigned int)((int)sshift + s_inc);
            }
            break;
         }

         case 4:
         {
            png_bytep sp = row + (size_t)((row_info->width - 1) >> 1);
            png_bytep dp = row + (size_t)((final_width - 1) >> 1);
            unsigned int sshift, dshift;
            unsigned int s_start, s_end;
            int s_inc;
            png_uint_32 i;
            int jstop = (int)png_pass_inc[pass];

#ifdef PNG_READ_PACKSWAP_SUPPORTED
            if ((transformations & PNG_PACKSWAP) != 0)
            {
               sshift = (((row_info->width + 1) & 0x01) << 2);
               dshift = (((final_width + 1) & 0x01) << 2);
               s_start = 4;
               s_end = 0;
               s_inc = -4;
            }

            else
#endif
            {
               sshift = ((1 - ((row_info->width + 1) & 0x01)) << 2);
               dshift = ((1 - ((final_width + 1) & 0x01)) << 2);
               s_start = 0;
               s_end = 4;
               s_inc = 4;
            }

            for (i = 0; i < row_info->width; i++)
            {
               png_byte v = (png_byte)((*sp >> sshift) & 0x0f);
               int j;

               for (j = 0; j < jstop; j++)
               {
                  unsigned int tmp = *dp & (0xf0f >> (4 - dshift));
                  tmp |= (unsigned int)(v << dshift);
                  *dp = (png_byte)(tmp & 0xff);

                  if (dshift == s_end)
                  {
                     dshift = s_start;
                     dp--;
                  }

                  else
                     dshift = (unsigned int)((int)dshift + s_inc);
               }

               if (sshift == s_end)
               {
                  sshift = s_start;
                  sp--;
               }

               else
                  sshift = (unsigned int)((int)sshift + s_inc);
            }
            break;
         }

         default:
         {
            size_t pixel_bytes = (row_info->pixel_depth >> 3);

            png_bytep sp = row + (size_t)(row_info->width - 1)
                * pixel_bytes;

            png_bytep dp = row + (size_t)(final_width - 1) * pixel_bytes;

            int jstop = (int)png_pass_inc[pass];
            png_uint_32 i;

            for (i = 0; i < row_info->width; i++)
            {
               png_byte v[8]; /* SAFE; pixel_depth does not exceed 64 */
               int j;

               memcpy(v, sp, pixel_bytes);

               for (j = 0; j < jstop; j++)
               {
                  memcpy(dp, v, pixel_bytes);
                  dp -= pixel_bytes;
               }

               sp -= pixel_bytes;
            }
            break;
         }
      }

      row_info->width = final_width;
      row_info->rowbytes = PNG_ROWBYTES(row_info->pixel_depth, final_width);
   }
#ifndef PNG_READ_PACKSWAP_SUPPORTED
   PNG_UNUSED(transformations)  /* Silence compiler warning */
#endif
}
#endif /* READ_INTERLACING */

static void
png_read_filter_row_sub(png_row_infop row_info, png_bytep row,
    png_const_bytep prev_row)
{
   size_t i;
   size_t istop = row_info->rowbytes;
   unsigned int bpp = (row_info->pixel_depth + 7) >> 3;
   png_bytep rp = row + bpp;

   PNG_UNUSED(prev_row)

   for (i = bpp; i < istop; i++)
   {
      *rp = (png_byte)(((int)(*rp) + (int)(*(rp-bpp))) & 0xff);
      rp++;
   }
}

static void
png_read_filter_row_up(png_row_infop row_info, png_bytep row,
    png_const_bytep prev_row)
{
   size_t i;
   size_t istop = row_info->rowbytes;
   png_bytep rp = row;
   png_const_bytep pp = prev_row;

   for (i = 0; i < istop; i++)
   {
      *rp = (png_byte)(((int)(*rp) + (int)(*pp++)) & 0xff);
      rp++;
   }
}

static void
png_read_filter_row_avg(png_row_infop row_info, png_bytep row,
    png_const_bytep prev_row)
{
   size_t i;
   png_bytep rp = row;
   png_const_bytep pp = prev_row;
   unsigned int bpp = (row_info->pixel_depth + 7) >> 3;
   size_t istop = row_info->rowbytes - bpp;

   for (i = 0; i < bpp; i++)
   {
      *rp = (png_byte)(((int)(*rp) +
         ((int)(*pp++) / 2 )) & 0xff);

      rp++;
   }

   for (i = 0; i < istop; i++)
   {
      *rp = (png_byte)(((int)(*rp) +
         (int)(*pp++ + *(rp-bpp)) / 2 ) & 0xff);

      rp++;
   }
}

static void
png_read_filter_row_paeth_1byte_pixel(png_row_infop row_info, png_bytep row,
    png_const_bytep prev_row)
{
   png_bytep rp_end = row + row_info->rowbytes;
   int a, c;

   /* First pixel/byte */
   c = *prev_row++;
   a = *row + c;
   *row++ = (png_byte)a;

   /* Remainder */
   while (row < rp_end)
   {
      int b, pa, pb, pc, p;

      a &= 0xff; /* From previous iteration or start */
      b = *prev_row++;

      p = b - c;
      pc = a - c;

#ifdef PNG_USE_ABS
      pa = abs(p);
      pb = abs(pc);
      pc = abs(p + pc);
#else
      pa = p < 0 ? -p : p;
      pb = pc < 0 ? -pc : pc;
      pc = (p + pc) < 0 ? -(p + pc) : p + pc;
#endif

      /* Find the best predictor, the least of pa, pb, pc favoring the earlier
       * ones in the case of a tie.
       */
      if (pb < pa)
      {
         pa = pb; a = b;
      }
      if (pc < pa) a = c;

      /* Calculate the current pixel in a, and move the previous row pixel to c
       * for the next time round the loop
       */
      c = b;
      a += *row;
      *row++ = (png_byte)a;
   }
}

static void
png_read_filter_row_paeth_multibyte_pixel(png_row_infop row_info, png_bytep row,
    png_const_bytep prev_row)
{
   unsigned int bpp = (row_info->pixel_depth + 7) >> 3;
   png_bytep rp_end = row + bpp;

   /* Process the first pixel in the row completely (this is the same as 'up'
    * because there is only one candidate predictor for the first row).
    */
   while (row < rp_end)
   {
      int a = *row + *prev_row++;
      *row++ = (png_byte)a;
   }

   /* Remainder */
   rp_end = rp_end + (row_info->rowbytes - bpp);

   while (row < rp_end)
   {
      int a, b, c, pa, pb, pc, p;

      c = *(prev_row - bpp);
      a = *(row - bpp);
      b = *prev_row++;

      p = b - c;
      pc = a - c;

#ifdef PNG_USE_ABS
      pa = abs(p);
      pb = abs(pc);
      pc = abs(p + pc);
#else
      pa = p < 0 ? -p : p;
      pb = pc < 0 ? -pc : pc;
      pc = (p + pc) < 0 ? -(p + pc) : p + pc;
#endif

      if (pb < pa)
      {
         pa = pb; a = b;
      }
      if (pc < pa) a = c;

      a += *row;
      *row++ = (png_byte)a;
   }
}

static void
png_init_filter_functions(png_structrp pp)
   /* This function is called once for every PNG image (except for PNG images
    * that only use PNG_FILTER_VALUE_NONE for all rows) to set the
    * implementations required to reverse the filtering of PNG rows.  Reversing
    * the filter is the first transformation performed on the row data.  It is
    * performed in place, therefore an implementation can be selected based on
    * the image pixel format.  If the implementation depends on image width then
    * take care to ensure that it works correctly if the image is interlaced -
    * interlacing causes the actual row width to vary.
    */
{
   unsigned int bpp = (pp->pixel_depth + 7) >> 3;

   pp->read_filter[PNG_FILTER_VALUE_SUB-1] = png_read_filter_row_sub;
   pp->read_filter[PNG_FILTER_VALUE_UP-1] = png_read_filter_row_up;
   pp->read_filter[PNG_FILTER_VALUE_AVG-1] = png_read_filter_row_avg;
   if (bpp == 1)
      pp->read_filter[PNG_FILTER_VALUE_PAETH-1] =
         png_read_filter_row_paeth_1byte_pixel;
   else
      pp->read_filter[PNG_FILTER_VALUE_PAETH-1] =
         png_read_filter_row_paeth_multibyte_pixel;

#ifdef PNG_FILTER_OPTIMIZATIONS
   /* To use this define PNG_FILTER_OPTIMIZATIONS as the name of a function to
    * call to install hardware optimizations for the above functions; simply
    * replace whatever elements of the pp->read_filter[] array with a hardware
    * specific (or, for that matter, generic) optimization.
    *
    * To see an example of this examine what configure.ac does when
    * --enable-arm-neon is specified on the command line.
    */
   PNG_FILTER_OPTIMIZATIONS(pp, bpp);
#endif
}

void /* PRIVATE */
png_read_filter_row(png_structrp pp, png_row_infop row_info, png_bytep row,
    png_const_bytep prev_row, int filter)
{
   /* OPTIMIZATION: DO NOT MODIFY THIS FUNCTION, instead #define
    * PNG_FILTER_OPTIMIZATIONS to a function that overrides the generic
    * implementations.  See png_init_filter_functions above.
    */
   if (filter > PNG_FILTER_VALUE_NONE && filter < PNG_FILTER_VALUE_LAST)
   {
      if (pp->read_filter[0] == NULL)
         png_init_filter_functions(pp);

      pp->read_filter[filter-1](row_info, row, prev_row);
   }
}

#ifdef PNG_SEQUENTIAL_READ_SUPPORTED
void /* PRIVATE */
png_read_IDAT_data(png_structrp png_ptr, png_bytep output,
    png_alloc_size_t avail_out)
{
   /* Loop reading IDATs and decompressing the result into output[avail_out] */
   png_ptr->zstream.next_out = output;
   png_ptr->zstream.avail_out = 0; /* safety: set below */

   if (output == NULL)
      avail_out = 0;

   do
   {
      int ret;
      png_byte tmpbuf[PNG_INFLATE_BUF_SIZE];

      if (png_ptr->zstream.avail_in == 0)
      {
         uInt avail_in;
         png_bytep buffer;

#ifdef PNG_READ_APNG_SUPPORTED
         png_uint_32 bytes_to_skip = 0;

         while (png_ptr->idat_size == 0 || bytes_to_skip != 0)
         {
            png_crc_finish(png_ptr, bytes_to_skip);
            bytes_to_skip = 0;

            png_ptr->idat_size = png_read_chunk_header(png_ptr);
            if (png_ptr->num_frames_read == 0)
            {
               if (png_ptr->chunk_name != png_IDAT)
                  png_error(png_ptr, "Not enough image data");
            }
            else
            {
               if (png_ptr->chunk_name == png_IEND)
                  png_error(png_ptr, "Not enough image data");
               if (png_ptr->chunk_name != png_fdAT)
               {
                  png_warning(png_ptr, "Skipped (ignored) a chunk "
                                       "between APNG chunks");
                  bytes_to_skip = png_ptr->idat_size;
                  continue;
               }

               png_ensure_sequence_number(png_ptr, png_ptr->idat_size);

               png_ptr->idat_size -= 4;
            }
         }
#else
         while (png_ptr->idat_size == 0)
         {
            png_crc_finish(png_ptr, 0);

            png_ptr->idat_size = png_read_chunk_header(png_ptr);
            /* This is an error even in the 'check' case because the code just
             * consumed a non-IDAT header.
             */
            if (png_ptr->chunk_name != png_IDAT)
               png_error(png_ptr, "Not enough image data");
         }
#endif /* READ_APNG */

         avail_in = png_ptr->IDAT_read_size;

         if (avail_in > png_chunk_max(png_ptr))
            avail_in = (uInt)/*SAFE*/png_chunk_max(png_ptr);

         if (avail_in > png_ptr->idat_size)
            avail_in = (uInt)png_ptr->idat_size;

         /* A PNG with a gradually increasing IDAT size will defeat this attempt
          * to minimize memory usage by causing lots of re-allocs, but
          * realistically doing IDAT_read_size re-allocs is not likely to be a
          * big problem.
          *
          * An error here corresponds to the system being out of memory.
          */
         buffer = png_read_buffer(png_ptr, avail_in);

         if (buffer == NULL)
            png_chunk_error(png_ptr, "out of memory");

         png_crc_read(png_ptr, buffer, avail_in);
         png_ptr->idat_size -= avail_in;

         png_ptr->zstream.next_in = buffer;
         png_ptr->zstream.avail_in = avail_in;
      }

      /* And set up the output side. */
      if (output != NULL) /* standard read */
      {
         uInt out = ZLIB_IO_MAX;

         if (out > avail_out)
            out = (uInt)avail_out;

         avail_out -= out;
         png_ptr->zstream.avail_out = out;
      }

      else /* after last row, checking for end */
      {
         png_ptr->zstream.next_out = tmpbuf;
         png_ptr->zstream.avail_out = (sizeof tmpbuf);
      }

      /* Use NO_FLUSH; this gives zlib the maximum opportunity to optimize the
       * process.  If the LZ stream is truncated the sequential reader will
       * terminally damage the stream, above, by reading the chunk header of the
       * following chunk (it then exits with png_error).
       *
       * TODO: deal more elegantly with truncated IDAT lists.
       */
      ret = PNG_INFLATE(png_ptr, Z_NO_FLUSH);

      /* Take the unconsumed output back. */
      if (output != NULL)
         avail_out += png_ptr->zstream.avail_out;

      else /* avail_out counts the extra bytes */
         avail_out += (sizeof tmpbuf) - png_ptr->zstream.avail_out;

      png_ptr->zstream.avail_out = 0;

      if (ret == Z_STREAM_END)
      {
         /* Do this for safety; we won't read any more into this row. */
         png_ptr->zstream.next_out = NULL;

         png_ptr->mode |= PNG_AFTER_IDAT;
         png_ptr->flags |= PNG_FLAG_ZSTREAM_ENDED;
#ifdef PNG_READ_APNG_SUPPORTED
         png_ptr->num_frames_read++;
#endif

         if (png_ptr->zstream.avail_in > 0 || png_ptr->idat_size > 0)
            png_chunk_benign_error(png_ptr, "Extra compressed data");
         break;
      }

      if (ret != Z_OK)
      {
         png_zstream_error(png_ptr, ret);

         if (output != NULL)
            png_chunk_error(png_ptr, png_ptr->zstream.msg);

         else /* checking */
         {
            png_chunk_benign_error(png_ptr, png_ptr->zstream.msg);
            return;
         }
      }
   } while (avail_out > 0);

   if (avail_out > 0)
   {
      /* The stream ended before the image; this is the same as too few IDATs so
       * should be handled the same way.
       */
      if (output != NULL)
         png_error(png_ptr, "Not enough image data");

      else /* the deflate stream contained extra data */
         png_chunk_benign_error(png_ptr, "Too much image data");
   }
}

void /* PRIVATE */
png_read_finish_IDAT(png_structrp png_ptr)
{
   /* We don't need any more data and the stream should have ended, however the
    * LZ end code may actually not have been processed.  In this case we must
    * read it otherwise stray unread IDAT data or, more likely, an IDAT chunk
    * may still remain to be consumed.
    */
   if ((png_ptr->flags & PNG_FLAG_ZSTREAM_ENDED) == 0)
   {
      /* The NULL causes png_read_IDAT_data to swallow any remaining bytes in
       * the compressed stream, but the stream may be damaged too, so even after
       * this call we may need to terminate the zstream ownership.
       */
      png_read_IDAT_data(png_ptr, NULL, 0);
      png_ptr->zstream.next_out = NULL; /* safety */

      /* Now clear everything out for safety; the following may not have been
       * done.
       */
      if ((png_ptr->flags & PNG_FLAG_ZSTREAM_ENDED) == 0)
      {
         png_ptr->mode |= PNG_AFTER_IDAT;
         png_ptr->flags |= PNG_FLAG_ZSTREAM_ENDED;
      }
   }

   /* If the zstream has not been released do it now *and* terminate the reading
    * of the final IDAT chunk.
    */
   if (png_ptr->zowner == png_IDAT)
   {
      /* Always do this; the pointers otherwise point into the read buffer. */
      png_ptr->zstream.next_in = NULL;
      png_ptr->zstream.avail_in = 0;

      /* Now we no longer own the zstream. */
      png_ptr->zowner = 0;

      /* The slightly weird semantics of the sequential IDAT reading is that we
       * are always in or at the end of an IDAT chunk, so we always need to do a
       * crc_finish here.  If idat_size is non-zero we also need to read the
       * spurious bytes at the end of the chunk now.
       */
      (void)png_crc_finish(png_ptr, png_ptr->idat_size);
   }
}

void /* PRIVATE */
png_read_finish_row(png_structrp png_ptr)
{
   png_debug(1, "in png_read_finish_row");
   png_ptr->row_number++;
   if (png_ptr->row_number < png_ptr->num_rows)
      return;

   if (png_ptr->interlaced != 0)
   {
      png_ptr->row_number = 0;

      /* TO DO: don't do this if prev_row isn't needed (requires
       * read-ahead of the next row's filter byte.
       */
      memset(png_ptr->prev_row, 0, png_ptr->rowbytes + 1);

      do
      {
         png_ptr->pass++;

         if (png_ptr->pass >= 7)
            break;

         png_ptr->iwidth = (png_ptr->width +
            png_pass_inc[png_ptr->pass] - 1 -
            png_pass_start[png_ptr->pass]) /
            png_pass_inc[png_ptr->pass];

         if ((png_ptr->transformations & PNG_INTERLACE) == 0)
         {
            png_ptr->num_rows = (png_ptr->height +
                png_pass_yinc[png_ptr->pass] - 1 -
                png_pass_ystart[png_ptr->pass]) /
                png_pass_yinc[png_ptr->pass];
         }

         else  /* if (png_ptr->transformations & PNG_INTERLACE) */
            break; /* libpng deinterlacing sees every row */

      } while (png_ptr->num_rows == 0 || png_ptr->iwidth == 0);

      if (png_ptr->pass < 7)
         return;
   }

   /* Here after at the end of the last row of the last pass. */
   png_read_finish_IDAT(png_ptr);
}
#endif /* SEQUENTIAL_READ */

void /* PRIVATE */
png_read_start_row(png_structrp png_ptr)
{
   unsigned int max_pixel_depth;
   size_t row_bytes;

   png_debug(1, "in png_read_start_row");

#ifdef PNG_READ_TRANSFORMS_SUPPORTED
   png_init_read_transformations(png_ptr);
#endif
   if (png_ptr->interlaced != 0)
   {
      if ((png_ptr->transformations & PNG_INTERLACE) == 0)
         png_ptr->num_rows = (png_ptr->height + png_pass_yinc[0] - 1 -
             png_pass_ystart[0]) / png_pass_yinc[0];

      else
         png_ptr->num_rows = png_ptr->height;

      png_ptr->iwidth = (png_ptr->width +
          png_pass_inc[png_ptr->pass] - 1 -
          png_pass_start[png_ptr->pass]) /
          png_pass_inc[png_ptr->pass];
   }

   else
   {
      png_ptr->num_rows = png_ptr->height;
      png_ptr->iwidth = png_ptr->width;
   }

   max_pixel_depth = (unsigned int)png_ptr->pixel_depth;

   /* WARNING: * png_read_transform_info (pngrtran.c) performs a simpler set of
    * calculations to calculate the final pixel depth, then
    * png_do_read_transforms actually does the transforms.  This means that the
    * code which effectively calculates this value is actually repeated in three
    * separate places.  They must all match.  Innocent changes to the order of
    * transformations can and will break libpng in a way that causes memory
    * overwrites.
    *
    * TODO: fix this.
    */
#ifdef PNG_READ_PACK_SUPPORTED
   if ((png_ptr->transformations & PNG_PACK) != 0 && png_ptr->bit_depth < 8)
      max_pixel_depth = 8;
#endif

#ifdef PNG_READ_EXPAND_SUPPORTED
   if ((png_ptr->transformations & PNG_EXPAND) != 0)
   {
      if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      {
         if (png_ptr->num_trans != 0)
            max_pixel_depth = 32;

         else
            max_pixel_depth = 24;
      }

      else if (png_ptr->color_type == PNG_COLOR_TYPE_GRAY)
      {
         if (max_pixel_depth < 8)
            max_pixel_depth = 8;

         if (png_ptr->num_trans != 0)
            max_pixel_depth *= 2;
      }

      else if (png_ptr->color_type == PNG_COLOR_TYPE_RGB)
      {
         if (png_ptr->num_trans != 0)
         {
            max_pixel_depth *= 4;
            max_pixel_depth /= 3;
         }
      }
   }
#endif

#ifdef PNG_READ_EXPAND_16_SUPPORTED
   if ((png_ptr->transformations & PNG_EXPAND_16) != 0)
   {
#  ifdef PNG_READ_EXPAND_SUPPORTED
      /* In fact it is an error if it isn't supported, but checking is
       * the safe way.
       */
      if ((png_ptr->transformations & PNG_EXPAND) != 0)
      {
         if (png_ptr->bit_depth < 16)
            max_pixel_depth *= 2;
      }
      else
#  endif
      png_ptr->transformations &= ~PNG_EXPAND_16;
   }
#endif

#ifdef PNG_READ_FILLER_SUPPORTED
   if ((png_ptr->transformations & (PNG_FILLER)) != 0)
   {
      if (png_ptr->color_type == PNG_COLOR_TYPE_GRAY)
      {
         if (max_pixel_depth <= 8)
            max_pixel_depth = 16;

         else
            max_pixel_depth = 32;
      }

      else if (png_ptr->color_type == PNG_COLOR_TYPE_RGB ||
         png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      {
         if (max_pixel_depth <= 32)
            max_pixel_depth = 32;

         else
            max_pixel_depth = 64;
      }
   }
#endif

#ifdef PNG_READ_GRAY_TO_RGB_SUPPORTED
   if ((png_ptr->transformations & PNG_GRAY_TO_RGB) != 0)
   {
      if (
#ifdef PNG_READ_EXPAND_SUPPORTED
          (png_ptr->num_trans != 0 &&
          (png_ptr->transformations & PNG_EXPAND) != 0) ||
#endif
#ifdef PNG_READ_FILLER_SUPPORTED
          (png_ptr->transformations & (PNG_FILLER)) != 0 ||
#endif
          png_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
      {
         if (max_pixel_depth <= 16)
            max_pixel_depth = 32;

         else
            max_pixel_depth = 64;
      }

      else
      {
         if (max_pixel_depth <= 8)
         {
            if (png_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA)
               max_pixel_depth = 32;

            else
               max_pixel_depth = 24;
         }

         else if (png_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA)
            max_pixel_depth = 64;

         else
            max_pixel_depth = 48;
      }
   }
#endif

#if defined(PNG_READ_USER_TRANSFORM_SUPPORTED) && \
defined(PNG_USER_TRANSFORM_PTR_SUPPORTED)
   if ((png_ptr->transformations & PNG_USER_TRANSFORM) != 0)
   {
      unsigned int user_pixel_depth = png_ptr->user_transform_depth *
         png_ptr->user_transform_channels;

      if (user_pixel_depth > max_pixel_depth)
         max_pixel_depth = user_pixel_depth;
   }
#endif

   /* This value is stored in png_struct and double checked in the row read
    * code.
    */
   png_ptr->maximum_pixel_depth = (png_byte)max_pixel_depth;
   png_ptr->transformed_pixel_depth = 0; /* calculated on demand */

   /* Align the width on the next larger 8 pixels.  Mainly used
    * for interlacing
    */
   row_bytes = ((png_ptr->width + 7) & ~((png_uint_32)7));
   /* Calculate the maximum bytes needed, adding a byte and a pixel
    * for safety's sake
    */
   row_bytes = PNG_ROWBYTES(max_pixel_depth, row_bytes) +
       1 + ((max_pixel_depth + 7) >> 3U);

#ifdef PNG_MAX_MALLOC_64K
   if (row_bytes > (png_uint_32)65536L)
      png_error(png_ptr, "This image requires a row greater than 64KB");
#endif

   if (row_bytes + 48 > png_ptr->old_big_row_buf_size)
   {
      png_free(png_ptr, png_ptr->big_row_buf);
      png_free(png_ptr, png_ptr->big_prev_row);

      if (png_ptr->interlaced != 0)
         png_ptr->big_row_buf = (png_bytep)png_calloc(png_ptr,
             row_bytes + 48);

      else
         png_ptr->big_row_buf = (png_bytep)png_malloc(png_ptr, row_bytes + 48);

      png_ptr->big_prev_row = (png_bytep)png_malloc(png_ptr, row_bytes + 48);

#ifdef PNG_ALIGNED_MEMORY_SUPPORTED
      /* Use 16-byte aligned memory for row_buf with at least 16 bytes
       * of padding before and after row_buf; treat prev_row similarly.
       * NOTE: the alignment is to the start of the pixels, one beyond the start
       * of the buffer, because of the filter byte.  Prior to libpng 1.5.6 this
       * was incorrect; the filter byte was aligned, which had the exact
       * opposite effect of that intended.
       */
      {
         png_bytep temp = png_ptr->big_row_buf + 32;
         size_t extra = (size_t)temp & 0x0f;
         png_ptr->row_buf = temp - extra - 1/*filter byte*/;

         temp = png_ptr->big_prev_row + 32;
         extra = (size_t)temp & 0x0f;
         png_ptr->prev_row = temp - extra - 1/*filter byte*/;
      }
#else
      /* Use 31 bytes of padding before and 17 bytes after row_buf. */
      png_ptr->row_buf = png_ptr->big_row_buf + 31;
      png_ptr->prev_row = png_ptr->big_prev_row + 31;
#endif
      png_ptr->old_big_row_buf_size = row_bytes + 48;
   }

#ifdef PNG_MAX_MALLOC_64K
   if (png_ptr->rowbytes > 65535)
      png_error(png_ptr, "This image requires a row greater than 64KB");

#endif
   if (png_ptr->rowbytes > (PNG_SIZE_MAX - 1))
      png_error(png_ptr, "Row has too many bytes to allocate in memory");

   memset(png_ptr->prev_row, 0, png_ptr->rowbytes + 1);

   png_debug1(3, "width = %u,", png_ptr->width);
   png_debug1(3, "height = %u,", png_ptr->height);
   png_debug1(3, "iwidth = %u,", png_ptr->iwidth);
   png_debug1(3, "num_rows = %u,", png_ptr->num_rows);
   png_debug1(3, "rowbytes = %lu,", (unsigned long)png_ptr->rowbytes);
   png_debug1(3, "irowbytes = %lu",
       (unsigned long)PNG_ROWBYTES(png_ptr->pixel_depth, png_ptr->iwidth) + 1);

   /* The sequential reader needs a buffer for IDAT, but the progressive reader
    * does not, so free the read buffer now regardless; the sequential reader
    * reallocates it on demand.
    */
   if (png_ptr->read_buffer != NULL)
   {
      png_bytep buffer = png_ptr->read_buffer;

      png_ptr->read_buffer_size = 0;
      png_ptr->read_buffer = NULL;
      png_free(png_ptr, buffer);
   }

   /* Finally claim the zstream for the inflate of the IDAT data, use the bits
    * value from the stream (note that this will result in a fatal error if the
    * IDAT stream has a bogus deflate header window_bits value, but this should
    * not be happening any longer!)
    */
   if (png_inflate_claim(png_ptr, png_IDAT) != Z_OK)
      png_error(png_ptr, png_ptr->zstream.msg);

   png_ptr->flags |= PNG_FLAG_ROW_INIT;
}

#ifdef PNG_READ_APNG_SUPPORTED
/* This function is to be called after the main IDAT set has been read and
 * before a new IDAT is read. It resets some parts of png_ptr
 * to make them usable by the read functions again */
void /* PRIVATE */
png_read_reset(png_structp png_ptr)
{
    png_ptr->mode &= ~PNG_HAVE_IDAT;
    png_ptr->mode &= ~PNG_AFTER_IDAT;
    png_ptr->row_number = 0;
    png_ptr->pass = 0;
}

void /* PRIVATE */
png_read_reinit(png_structp png_ptr, png_infop info_ptr)
{
    png_ptr->width = info_ptr->next_frame_width;
    png_ptr->height = info_ptr->next_frame_height;
    png_ptr->rowbytes = PNG_ROWBYTES(png_ptr->pixel_depth,png_ptr->width);
    png_ptr->info_rowbytes = PNG_ROWBYTES(info_ptr->pixel_depth,
        png_ptr->width);
    if (png_ptr->prev_row != NULL)
        memset(png_ptr->prev_row, 0, png_ptr->rowbytes + 1);
}

#ifdef PNG_PROGRESSIVE_READ_SUPPORTED
/* same as png_read_reset() but for the progressive reader */
void /* PRIVATE */
png_progressive_read_reset(png_structp png_ptr)
{
#ifdef PNG_READ_INTERLACING_SUPPORTED
    /* Arrays to facilitate easy interlacing - use pass (0 - 6) as index */

    /* Start of interlace block */
    static PNG_CONST png_byte png_pass_start[] = {0, 4, 0, 2, 0, 1, 0};

    /* Offset to next interlace block */
    static PNG_CONST png_byte png_pass_inc[] = {8, 8, 4, 4, 2, 2, 1};

    /* Start of interlace block in the y direction */
    static PNG_CONST png_byte png_pass_ystart[] = {0, 0, 4, 0, 2, 0, 1};

    /* Offset to next interlace block in the y direction */
    static PNG_CONST png_byte png_pass_yinc[] = {8, 8, 8, 4, 4, 2, 2};

    if (png_ptr->interlaced != 0)
    {
        if ((png_ptr->transformations & PNG_INTERLACE) == 0)
            png_ptr->num_rows = (png_ptr->height + png_pass_yinc[0] - 1 -
                                png_pass_ystart[0]) / png_pass_yinc[0];
        else
            png_ptr->num_rows = png_ptr->height;

        png_ptr->iwidth = (png_ptr->width +
                           png_pass_inc[png_ptr->pass] - 1 -
                           png_pass_start[png_ptr->pass]) /
                           png_pass_inc[png_ptr->pass];
    }
    else
#endif /* READ_INTERLACING */
    {
        png_ptr->num_rows = png_ptr->height;
        png_ptr->iwidth = png_ptr->width;
    }
    png_ptr->flags &= ~PNG_FLAG_ZSTREAM_ENDED;
    if (inflateReset(&(png_ptr->zstream)) != Z_OK)
        png_error(png_ptr, "inflateReset failed");
    png_ptr->zstream.avail_in = 0;
    png_ptr->zstream.next_in = 0;
    png_ptr->zstream.next_out = png_ptr->row_buf;
    png_ptr->zstream.avail_out = (uInt)PNG_ROWBYTES(png_ptr->pixel_depth,
        png_ptr->iwidth) + 1;
}
#endif /* PROGRESSIVE_READ */
#endif /* READ_APNG */
#endif /* READ */
