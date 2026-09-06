/*
  win32/win32i64.c - Zip 3

  Copyright (c) 1990-2007 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2005-Feb-10 or later
  (the contents of which are also included in zip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/

#include "../zip.h"

/* See win32zip.c — avoid CR macro colliding with winnt.h ARM64 bitfield. */
#undef CR
#include <windows.h>
#define CR 13

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>
/* for LARGE_FILE_SUPPORT but may not be needed */
#include <io.h>


/* --------------------------------------------------- */
/* Large File Support
 *
 * Initial functions by E. Gordon and R. Nausedat
 * 9/10/2003
 *
 * These implement 64-bit file support for Windows.  The
 * defines and headers are in win32/osdep.h.
 *
 * These moved from win32.c by Mike White to avoid conflicts
 * in WiZ of same name functions in UnZip and Zip libraries.
 * 9/25/04 EG
 */

#if defined(LARGE_FILE_SUPPORT) && !defined(__CYGWIN__)

/* 64-bit buffered ftello / fseeko.
 *
 * zip64j: these use the CRT's own buffered 64-bit calls. Upstream reads the
 * position with fgetpos() but moves it with _lseeki64() on the underlying
 * descriptor, relying on fflush() to drop the stream's read buffer first.
 * The UCRT makes fflush() a no-op on a read-only stream, so the descriptor
 * and the FILE* desynchronise and the seek silently has no effect on the
 * next read — which broke every code path that reads an existing archive
 * back (-d, -u, -f, -g). _fseeki64 / _ftelli64 are buffered-stream aware and
 * have shipped with MS C since VS2005, well after this file was written.
 */

zoff_t zftello(stream)
  FILE *stream;
{
  return _ftelli64(stream);
}


int zfseeko(stream, offset, origin)
  FILE *stream;
  zoff_t offset;
  int origin;
{
  return _fseeki64(stream, offset, origin);
}
#endif  /* Win32 LARGE_FILE_SUPPORT */

#if 0
FILE* zfopen(filename,mode)
char *filename;
char *mode;
{
FILE* fTemp;
  
  fTemp = fopen(filename,mode);
  if( fTemp == NULL )
    return NULL;
  
  /* sorry, could not make VC60 and its rtl work properly without setting the file buffer to NULL. the  */
  /* problem seems to be _telli64 which seems to return the max stream position, comments are welcome   */
  setbuf(fTemp,NULL);

  return fTemp;
}
#endif
/* --------------------------------------------------- */
