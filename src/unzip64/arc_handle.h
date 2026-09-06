/*
 * arc_handle.h - HARC-backed archive enumeration for unzip64.dll.
 *
 * Sits on top of arc_cdparse and bridges to the 統合アーカイバ-style
 * UnZip* entry-point APIs (UnZipOpenArchive / UnZipFindFirst /
 * UnZipFindNext / accessors). HARC values returned to callers are pointers
 * to zip_arc_t, validated via a magic word before use.
 */

#ifndef ARC_HANDLE_H_INCLUDED
#define ARC_HANDLE_H_INCLUDED

#include "../../include/zip64j.h"
#include "arc_cdparse.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZIP_ARC_MAGIC 0x5A415243UL  /* 'ZARC' (big-endian mnemonic) */

typedef struct zip_arc_s {
    DWORD            magic;
    zip_cd_entry_t  *entries;
    DWORD            count;
    DWORD            current_idx;   /* 0..count — FindNext advances; accessors look at current_idx-1 */
    char             wild_spec[MAX_PATH];
    char             arc_filename[MAX_PATH];
    __int64          arc_filesize;
    __int64          arc_orig_total;
    __int64          arc_comp_total;
    HWND             hwnd_owner;    /* from UnZipOpenArchive; unused for now */
    DWORD            open_mode;     /* dwMode from OpenArchive; unused */
} zip_arc_t;

/* Open / close. */
zip_arc_t *zip_arc_open (const char *filename, HWND hwnd, DWORD mode);
void       zip_arc_close(zip_arc_t *arc);

/* Validate that an HARC came from us (NULL-safe, magic-checked). */
zip_arc_t *zip_arc_validate(HARC h);

/* FindFirst / FindNext implementation. Returns 0 on match, -1 on end or
 * invalid handle. Fills INDIVIDUALINFO via zip_arc_populate_info(). */
int zip_arc_find_first(zip_arc_t *arc, const char *wild, LPINDIVIDUALINFO info);
int zip_arc_find_next (zip_arc_t *arc,                    LPINDIVIDUALINFO info);

/* Unicode (UTF-16) variants. Wildcard is converted via CP_ACP to reuse the
 * existing matcher; filenames themselves are converted lossless-ly to UTF-16
 * for INDIVIDUALINFOW (UTF-8 entries via CP_UTF8, others via CP_ACP). */
int zip_arc_find_first_w(zip_arc_t *arc, LPCWSTR wild, LPINDIVIDUALINFOW info);
int zip_arc_find_next_w (zip_arc_t *arc,               LPINDIVIDUALINFOW info);

/* Populate INDIVIDUALINFO(W) from arc->entries[arc->current_idx - 1]. */
void zip_arc_populate_info  (const zip_arc_t *arc, LPINDIVIDUALINFO  info);
void zip_arc_populate_info_w(const zip_arc_t *arc, LPINDIVIDUALINFOW info);

/* Get the entry current_idx-1 points at, or NULL if not valid. */
const zip_cd_entry_t *zip_arc_current_entry(const zip_arc_t *arc);

/* Convert a raw filename to CP932 / UTF-16. UTF-8 entries (gp_flag bit 11)
 * decode via CP_UTF8 and round-trip via UTF-16; other entries are assumed
 * to be CP932 on Japanese Windows. */
void zip_arc_name_to_cp932(const zip_cd_entry_t *e, char  *out, int out_size);
void zip_arc_name_to_utf16(const zip_cd_entry_t *e, WCHAR *out, int out_cch);

#ifdef __cplusplus
}
#endif

#endif
