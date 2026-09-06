/*
 * arc_cdparse.h - ZIP Central Directory parser.
 *
 * Reads EOCD / Zip64 EOCD Locator / Zip64 EOCD Record / Central Directory
 * Headers out of a ZIP file and produces an array of zip_cd_entry_t for the
 * archive-handle API layer to consume.
 *
 * Self-contained: depends only on the C stdlib and windows.h for FILETIME.
 * No ties to unzip60's global state.
 *
 * Field layouts follow PKWARE APPNOTE.txt. Offset values cross-checked
 * against unzip60's unzpriv.h (C_* / TOTAL_ENTRIES_CENTRAL_DIR et al.).
 */

#ifndef ARC_CDPARSE_H_INCLUDED
#define ARC_CDPARSE_H_INCLUDED

#include <windows.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum raw filename bytes kept per entry. APPNOTE allows up to 64 KiB but
 * real-world archives stay well under 1 KiB; oversize names get truncated. */
#define ZIP_CD_NAME_MAX 1024

typedef struct zip_cd_entry_s {
    /* Raw central-directory fields (Zip64 extra field already applied). */
    __int64 orig_size;
    __int64 comp_size;
    __int64 local_header_offset;
    DWORD   crc32;
    WORD    method;
    WORD    dos_date;
    WORD    dos_time;
    DWORD   ext_attrs;        /* 32-bit external file attributes */
    WORD    int_attrs;        /* internal file attributes */
    WORD    version_made_by;  /* high byte = host OS */
    WORD    gp_flag;          /* bit 11 = UTF-8 filename */
    WORD    name_len;         /* raw bytes (not including NUL) */
    char    name_raw[ZIP_CD_NAME_MAX + 1]; /* NUL-terminated, raw encoding */
} zip_cd_entry_t;

/* Parse the central directory of `fp`. On success, *out_entries points to a
 * malloc'd array of *out_count entries (caller frees). Returns 0 on success,
 * non-zero on any failure (unreadable file, missing EOCD, malformed CDH).
 *
 * On failure, *out_entries is NULL and *out_count is 0 — nothing to free.
 */
int zip_cd_parse(FILE *fp, zip_cd_entry_t **out_entries, DWORD *out_count);

#ifdef __cplusplus
}
#endif

#endif
