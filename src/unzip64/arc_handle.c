/*
 * arc_handle.c - HARC / FindFirst / FindNext / populate INDIVIDUALINFO.
 *
 * The zip_arc_t lifecycle:
 *   OpenArchive  -> malloc + zip_cd_parse + compute aggregate sizes
 *   FindFirst    -> store wildcard, reset current_idx, call FindNext
 *   FindNext     -> scan forward through entries until wildcard matches,
 *                   fill INDIVIDUALINFO from the hit
 *   accessors    -> read arc->entries[arc->current_idx - 1]
 *   CloseArchive -> clear magic, free entries, free arc
 *
 * After FindNext returns, current_idx points to the *next* entry to scan,
 * so per-entry accessors read entries[current_idx - 1] (the one just
 * returned) — this mirrors the 統合アーカイバAPI仕様 cursor convention.
 */

#include "arc_handle.h"
#include "../common/debug_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <shlwapi.h>   /* PathMatchSpecA */

#pragma comment(lib, "shlwapi.lib")

/* -------- Validation & lifecycle -------- */

zip_arc_t *zip_arc_validate(HARC h)
{
    zip_arc_t *a;
    if (h == NULL) return NULL;
    a = (zip_arc_t *)h;
    if (IsBadReadPtr(a, sizeof(DWORD))) return NULL;
    if (a->magic != ZIP_ARC_MAGIC) return NULL;
    return a;
}

zip_arc_t *zip_arc_open(const char *filename, HWND hwnd, DWORD mode)
{
    zip_arc_t *arc = NULL;
    FILE      *fp  = NULL;
    zip_cd_entry_t *entries = NULL;
    DWORD      count = 0;
    __int64    fsize = 0;
    DWORD      i;

    if (!filename || !*filename) {
        zip64j_log("zip_arc_open: empty filename");
        return NULL;
    }

    fp = fopen(filename, "rb");
    if (!fp) {
        zip64j_log("zip_arc_open: fopen(\"%s\") failed errno=%d", filename, errno);
        return NULL;
    }

    if (zip_cd_parse(fp, &entries, &count) != 0) {
        zip64j_log("zip_arc_open: zip_cd_parse failed for \"%s\"", filename);
        fclose(fp);
        return NULL;
    }
    zip64j_log("zip_arc_open: \"%s\" parsed count=%u", filename, (unsigned)count);

    /* Grab the physical file size for UnZipGetArcFileSize*. */
    if (_fseeki64(fp, 0, SEEK_END) == 0) fsize = _ftelli64(fp);
    fclose(fp);

    arc = (zip_arc_t *)calloc(1, sizeof(zip_arc_t));
    if (!arc) {
        free(entries);
        return NULL;
    }
    arc->magic        = ZIP_ARC_MAGIC;
    arc->entries      = entries;
    arc->count        = count;
    arc->current_idx  = 0;
    arc->hwnd_owner   = hwnd;
    arc->open_mode    = mode;
    arc->arc_filesize = fsize;

    strncpy(arc->arc_filename, filename, MAX_PATH - 1);
    arc->arc_filename[MAX_PATH - 1] = '\0';

    /* Default wildcard — matches everything. */
    strcpy(arc->wild_spec, "*");

    /* Aggregate sizes across all entries. */
    for (i = 0; i < count; i++) {
        arc->arc_orig_total += entries[i].orig_size;
        arc->arc_comp_total += entries[i].comp_size;
    }

    return arc;
}

void zip_arc_close(zip_arc_t *arc)
{
    if (!arc) return;
    arc->magic = 0;   /* poison magic before free — catches use-after-close */
    free(arc->entries);
    free(arc);
}

/* -------- Filename conversion -------- */

void zip_arc_name_to_cp932(const zip_cd_entry_t *e, char *out, int out_size)
{
    if (out_size <= 0) return;
    out[0] = '\0';

    if (e->gp_flag & 0x0800) {
        /* UTF-8 → UTF-16 → CP932 */
        WCHAR wbuf[FNAME_MAX32 + 1];
        int wlen;

        wlen = MultiByteToWideChar(CP_UTF8, 0, e->name_raw, -1, wbuf,
                                   sizeof(wbuf) / sizeof(wbuf[0]));
        if (wlen <= 0) {
            /* Fall back: copy raw bytes as-is, best effort. */
            strncpy(out, e->name_raw, out_size - 1);
            out[out_size - 1] = '\0';
            return;
        }
        WideCharToMultiByte(932, 0, wbuf, -1, out, out_size, "?", NULL);
        out[out_size - 1] = '\0';
    } else {
        /* Non-UTF-8 entry: assume CP932 bytes directly (Japanese Windows). */
        int copy_len = (e->name_len < (WORD)(out_size - 1))
                     ? e->name_len : (WORD)(out_size - 1);
        memcpy(out, e->name_raw, copy_len);
        out[copy_len] = '\0';
    }
}

void zip_arc_name_to_utf16(const zip_cd_entry_t *e, WCHAR *out, int out_cch)
{
    UINT cp = (e->gp_flag & 0x0800) ? CP_UTF8 : 932;
    int n;
    if (out_cch <= 0) return;
    out[0] = L'\0';
    n = MultiByteToWideChar(cp, 0, e->name_raw, -1, out, out_cch);
    if (n <= 0) {
        /* Decoding failed — fall back to CP932 with best-effort replacement. */
        MultiByteToWideChar(932, 0, e->name_raw, -1, out, out_cch);
    }
    out[out_cch - 1] = L'\0';
}

/* -------- Derived value helpers -------- */

/* Ratio in permille (統合アーカイバAPI仕様: UnZipGetRatio returns ratio1*10 + ratio2). */
static WORD compute_ratio_permille(__int64 orig, __int64 comp)
{
    if (orig <= 0) return 0;
    if (comp >= orig) return 0;
    return (WORD)(((orig - comp) * 1000) / orig);
}

/* DOS-style 4-char attribute string "arhs", '-' for cleared bits. Only
 * meaningful for DOS/FAT and Windows/NTFS hosts; non-DOS hosts get "-----". */
static void format_dos_attr(DWORD ext_attrs, WORD version_made_by,
                            char *out, int out_size)
{
    DWORD host = (version_made_by >> 8) & 0xFF;
    DWORD dos_attr = ext_attrs & 0xFF;  /* low byte = DOS attr bits for DOS/NTFS hosts */
    int is_dos_host = (host == 0 || host == 10 || host == 11);  /* FAT, NTFS, VFAT */

    if (out_size < 5) {
        if (out_size > 0) out[0] = '\0';
        return;
    }
    if (!is_dos_host) {
        strcpy(out, "----");
        return;
    }
    out[0] = (dos_attr & 0x20) ? 'a' : '-';  /* archive */
    out[1] = (dos_attr & 0x01) ? 'r' : '-';  /* read-only */
    out[2] = (dos_attr & 0x02) ? 'h' : '-';  /* hidden */
    out[3] = (dos_attr & 0x04) ? 's' : '-';  /* system */
    out[4] = '\0';
}

/* -------- INDIVIDUALINFO population -------- */

void zip_arc_populate_info(const zip_arc_t *arc, LPINDIVIDUALINFO info)
{
    const zip_cd_entry_t *e;

    memset(info, 0, sizeof(*info));
    if (!arc || arc->current_idx == 0 || arc->current_idx > arc->count) return;
    e = &arc->entries[arc->current_idx - 1];

    /* 32-bit size fields — truncate; *_Ex APIs expose the full 64-bit. */
    info->dwOriginalSize   = (DWORD)(e->orig_size & 0xFFFFFFFFULL);
    info->dwCompressedSize = (DWORD)(e->comp_size & 0xFFFFFFFFULL);
    info->dwCRC            = e->crc32;
    info->uFlag            = e->gp_flag;
    info->uOSType          = (UINT)(e->version_made_by >> 8);
    info->wRatio           = compute_ratio_permille(e->orig_size, e->comp_size);
    info->wDate            = e->dos_date;
    info->wTime            = e->dos_time;

    zip_arc_name_to_cp932(e, info->szFileName, sizeof(info->szFileName));
    format_dos_attr(e->ext_attrs, e->version_made_by,
                    info->szAttribute, sizeof(info->szAttribute));
    strcpy(info->szMode, "-zip-");
}

void zip_arc_populate_info_w(const zip_arc_t *arc, LPINDIVIDUALINFOW info)
{
    const zip_cd_entry_t *e;
    char attr_a[8], mode_a[8];

    memset(info, 0, sizeof(*info));
    if (!arc || arc->current_idx == 0 || arc->current_idx > arc->count) return;
    e = &arc->entries[arc->current_idx - 1];

    info->dwOriginalSize   = (DWORD)(e->orig_size & 0xFFFFFFFFULL);
    info->dwCompressedSize = (DWORD)(e->comp_size & 0xFFFFFFFFULL);
    info->dwCRC            = e->crc32;
    info->uFlag            = e->gp_flag;
    info->uOSType          = (UINT)(e->version_made_by >> 8);
    info->wRatio           = compute_ratio_permille(e->orig_size, e->comp_size);
    info->wDate            = e->dos_date;
    info->wTime            = e->dos_time;

    zip_arc_name_to_utf16(e, info->szFileName,
                          sizeof(info->szFileName) / sizeof(info->szFileName[0]));

    /* szAttribute / szMode are ASCII-only so plain mbstowcs via CP_ACP is fine. */
    format_dos_attr(e->ext_attrs, e->version_made_by, attr_a, sizeof(attr_a));
    strcpy(mode_a, "-zip-");
    MultiByteToWideChar(CP_ACP, 0, attr_a, -1, info->szAttribute,
                        sizeof(info->szAttribute) / sizeof(info->szAttribute[0]));
    MultiByteToWideChar(CP_ACP, 0, mode_a, -1, info->szMode,
                        sizeof(info->szMode) / sizeof(info->szMode[0]));
}

const zip_cd_entry_t *zip_arc_current_entry(const zip_arc_t *arc)
{
    if (!arc || arc->current_idx == 0 || arc->current_idx > arc->count) return NULL;
    return &arc->entries[arc->current_idx - 1];
}

/* -------- Wildcard matching & FindFirst/Next -------- */

static int entry_matches(const zip_cd_entry_t *e, const char *spec)
{
    char name[FNAME_MAX32 + 1];

    if (!spec || !*spec || strcmp(spec, "*") == 0) return 1;
    zip_arc_name_to_cp932(e, name, sizeof(name));
    return PathMatchSpecA(name, spec) ? 1 : 0;
}

int zip_arc_find_first(zip_arc_t *arc, const char *wild, LPINDIVIDUALINFO info)
{
    if (!arc) return -1;

    if (wild && *wild) {
        strncpy(arc->wild_spec, wild, sizeof(arc->wild_spec) - 1);
        arc->wild_spec[sizeof(arc->wild_spec) - 1] = '\0';
    } else {
        strcpy(arc->wild_spec, "*");
    }
    arc->current_idx = 0;
    return zip_arc_find_next(arc, info);
}

int zip_arc_find_next(zip_arc_t *arc, LPINDIVIDUALINFO info)
{
    if (!arc) return -1;

    while (arc->current_idx < arc->count) {
        const zip_cd_entry_t *e = &arc->entries[arc->current_idx];
        arc->current_idx++;   /* advance first so accessors see this entry as current */
        if (entry_matches(e, arc->wild_spec)) {
            if (info) zip_arc_populate_info(arc, info);
            return 0;
        }
    }
    return -1;
}

/* ---- Unicode (W) variants ------------------------------------------------
 * Wildcard is narrowed via CP_ACP so the existing CP932-based matcher in
 * entry_matches() can be reused. Typical wildcards are "*" or ASCII patterns,
 * so CP_ACP is adequate. If a caller ever needs non-CP932 wildcards, this is
 * where we'd switch to a PathMatchSpecW path. */

int zip_arc_find_first_w(zip_arc_t *arc, LPCWSTR wild_w, LPINDIVIDUALINFOW info)
{
    char wild_a[MAX_PATH];
    int rc;
    INDIVIDUALINFO dummy_a;

    if (!arc) return -1;

    if (wild_w && *wild_w) {
        if (WideCharToMultiByte(CP_ACP, 0, wild_w, -1, wild_a, sizeof(wild_a),
                                NULL, NULL) == 0) {
            strcpy(wild_a, "*");
        }
    } else {
        strcpy(wild_a, "*");
    }
    /* Reuse the A path for matching; populate W info afterward. */
    rc = zip_arc_find_first(arc, wild_a, &dummy_a);
    if (rc == 0 && info) zip_arc_populate_info_w(arc, info);
    return rc;
}

int zip_arc_find_next_w(zip_arc_t *arc, LPINDIVIDUALINFOW info)
{
    INDIVIDUALINFO dummy_a;
    int rc = zip_arc_find_next(arc, &dummy_a);
    if (rc == 0 && info) zip_arc_populate_info_w(arc, info);
    return rc;
}
