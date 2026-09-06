/*
 * arc_cdparse.c - ZIP Central Directory parser.
 *
 * Flow:
 *   1. Scan the last min(filesize, ECREC_MAX_SEARCH) bytes for the EOCD
 *      signature 0x06054b50. Search back-to-front because an archive comment
 *      (up to 64 KiB) may sit between EOCD and EOF.
 *   2. Pull entry count and central-directory offset / size out of EOCD.
 *   3. If any of those fields is saturated (0xFFFF / 0xFFFFFFFF), step back
 *      past the Zip64 EOCD Locator (just before EOCD) to find the Zip64
 *      EOCD Record and read the true 64-bit values.
 *   4. Read the whole CD into memory and walk it, decoding one CDH at a time.
 *      A Zip64 extra field (id 0x0001) may override size / offset values.
 *
 * Field offsets mirror unzip60's unzpriv.h constants (C_* / EC_*) so we stay
 * in sync with a battle-tested source of truth.
 */

#include "arc_cdparse.h"
#include "../common/debug_log.h"

#include <stdlib.h>
#include <string.h>

/* -------- On-disk field offsets (post-signature unless noted) -------- */

/* EOCD Record (end_central_sig = 0x06054b50) — 22 bytes including 4-byte sig. */
#define EOCD_SIG              0x06054b50UL
#define EOCD_SIZE             22
#define EOCD_NUM_THIS_DISK         4   /* 2 bytes */
#define EOCD_DISK_WITH_CD          6   /* 2 */
#define EOCD_ENTRIES_THIS_DISK     8   /* 2 */
#define EOCD_TOTAL_ENTRIES        10   /* 2 */
#define EOCD_SIZE_CENTRAL_DIR     12   /* 4 */
#define EOCD_OFFSET_CENTRAL_DIR   16   /* 4 */
#define EOCD_COMMENT_LENGTH       20   /* 2 */

/* Zip64 EOCD Locator (0x07064b50) — 20 bytes including 4-byte sig. */
#define ZIP64_LOC_SIG         0x07064b50UL
#define ZIP64_LOC_SIZE        20
#define ZIP64_LOC_DISK_EOCD64      4   /* 4 */
#define ZIP64_LOC_OFFSET_EOCD64    8   /* 8 */

/* Zip64 EOCD Record (0x06064b50) — fixed portion is 56 bytes incl. sig.
 * APPNOTE lets this record grow via a "zip64 extensible data sector", but
 * the fixed portion is all we need for enumeration. */
#define ZIP64_EOCD_SIG        0x06064b50UL
#define ZIP64_EOCD_FIXED_SIZE 56
#define ZIP64_EOCD_TOTAL_ENTRIES  32   /* 8 */
#define ZIP64_EOCD_SIZE_CD        40   /* 8 */
#define ZIP64_EOCD_OFFSET_CD      48   /* 8 */

/* Central Directory Header (0x02014b50) — fixed portion is 46 bytes incl. sig. */
#define CDH_SIG               0x02014b50UL
#define CDH_FIXED_SIZE        46
#define CDH_VERSION_MADE_BY        4   /* 2 */
#define CDH_GP_FLAG                8   /* 2 */
#define CDH_METHOD                10   /* 2 */
#define CDH_DOS_TIME              12   /* 2 */
#define CDH_DOS_DATE              14   /* 2 */
#define CDH_CRC32                 16   /* 4 */
#define CDH_COMP_SIZE             20   /* 4 */
#define CDH_ORIG_SIZE             24   /* 4 */
#define CDH_NAME_LEN              28   /* 2 */
#define CDH_EXTRA_LEN             30   /* 2 */
#define CDH_COMMENT_LEN           32   /* 2 */
#define CDH_INT_ATTRS             36   /* 2 */
#define CDH_EXT_ATTRS             38   /* 4 */
#define CDH_LOCAL_OFFSET          42   /* 4 */

/* Zip64 extra field inside a CDH: id 0x0001, followed by a variable set of
 * 8-byte values in a fixed order. Which values are present depends on which
 * of the 32-bit fields held the 0xFFFFFFFF sentinel. */
#define ZIP64_EXTRA_ID        0x0001

/* How far back from EOF we hunt for the EOCD signature. APPNOTE caps the
 * zipfile comment at 64 KiB, so 66 KiB is a safe upper bound. */
#define EOCD_SEARCH_MAX_BYTES (66 * 1024)

/* The whole central directory is slurped into one allocation, so a corrupt or
 * hostile cd_size must not turn into an unbounded malloc. 1 GiB covers roughly
 * 10 million entries at typical CDH sizes. */
#define CD_MAX_BYTES (1024 * 1024 * 1024)

/* -------- Little-endian readers -------- */

static DWORD read_u16(const unsigned char *p) {
    return (DWORD)p[0] | ((DWORD)p[1] << 8);
}
static DWORD read_u32(const unsigned char *p) {
    return (DWORD)p[0] | ((DWORD)p[1] << 8)
         | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}
static __int64 read_u64(const unsigned char *p) {
    DWORD lo = read_u32(p);
    DWORD hi = read_u32(p + 4);
    return (__int64)(((unsigned __int64)hi << 32) | lo);
}

/* -------- File helpers -------- */

static int seek64(FILE *fp, __int64 off, int whence) {
    return _fseeki64(fp, off, whence);
}
static __int64 tell64(FILE *fp) {
    return _ftelli64(fp);
}

static __int64 file_size(FILE *fp) {
    __int64 size;
    if (seek64(fp, 0, SEEK_END) != 0) return -1;
    size = tell64(fp);
    return size;
}

/* -------- EOCD search --------
 *
 * Read up to EOCD_SEARCH_MAX_BYTES from the tail of the file into `buf`,
 * then scan backwards for the signature. Sets *out_eocd_pos (file offset of
 * the 4-byte sig) and *out_tail_off (file offset of first byte of buf).
 * Returns 0 on success.
 */
static int find_eocd(FILE *fp, __int64 fsize,
                     unsigned char *buf, size_t buflen,
                     size_t *out_readlen,
                     __int64 *out_eocd_pos,
                     __int64 *out_tail_off)
{
    __int64 tail_off;
    size_t  readlen;
    size_t  i;

    if (fsize < EOCD_SIZE) return -1;

    readlen = (size_t)((fsize < (__int64)buflen) ? fsize : (__int64)buflen);
    tail_off = fsize - (__int64)readlen;

    if (seek64(fp, tail_off, SEEK_SET) != 0) return -1;
    if (fread(buf, 1, readlen, fp) != readlen) return -1;

    /* Walk backwards looking for the sig. Stop once there isn't enough room
     * left in the buffer for the fixed EOCD portion. */
    if (readlen < EOCD_SIZE) return -1;
    for (i = readlen - EOCD_SIZE + 1; i-- > 0; ) {
        if (read_u32(buf + i) == EOCD_SIG) {
            /* Sanity check: claimed comment length must fit within what we
             * read — otherwise it's a stray matching sig. */
            DWORD comment_len = read_u16(buf + i + EOCD_COMMENT_LENGTH);
            if ((size_t)i + EOCD_SIZE + comment_len <= readlen) {
                *out_readlen   = readlen;
                *out_eocd_pos  = tail_off + (__int64)i;
                *out_tail_off  = tail_off;
                return 0;
            }
        }
    }
    return -1;
}

/* -------- Parse one CDH out of an in-memory CD buffer --------
 *
 * `p` / `end` frame the central directory in memory. `*cursor` advances past
 * the parsed entry. Returns 0 on success and fills `e`; non-zero if the
 * entry is malformed or runs past `end`.
 */
static int parse_one_cdh(const unsigned char *p,
                         const unsigned char *end,
                         size_t *cursor,
                         zip_cd_entry_t *e)
{
    const unsigned char *h;
    DWORD comp32, orig32, offset32;
    DWORD name_len, extra_len, comment_len;
    size_t total_size;

    if (*cursor + CDH_FIXED_SIZE > (size_t)(end - p)) return -1;
    h = p + *cursor;
    if (read_u32(h) != CDH_SIG) return -1;

    memset(e, 0, sizeof(*e));
    e->version_made_by = (WORD)read_u16(h + CDH_VERSION_MADE_BY);
    e->gp_flag         = (WORD)read_u16(h + CDH_GP_FLAG);
    e->method          = (WORD)read_u16(h + CDH_METHOD);
    e->dos_time        = (WORD)read_u16(h + CDH_DOS_TIME);
    e->dos_date        = (WORD)read_u16(h + CDH_DOS_DATE);
    e->crc32           = read_u32(h + CDH_CRC32);
    comp32             = read_u32(h + CDH_COMP_SIZE);
    orig32             = read_u32(h + CDH_ORIG_SIZE);
    name_len           = read_u16(h + CDH_NAME_LEN);
    extra_len          = read_u16(h + CDH_EXTRA_LEN);
    comment_len        = read_u16(h + CDH_COMMENT_LEN);
    e->int_attrs       = (WORD)read_u16(h + CDH_INT_ATTRS);
    e->ext_attrs       = read_u32(h + CDH_EXT_ATTRS);
    offset32           = read_u32(h + CDH_LOCAL_OFFSET);

    e->comp_size            = (__int64)(unsigned __int64)comp32;
    e->orig_size            = (__int64)(unsigned __int64)orig32;
    e->local_header_offset  = (__int64)(unsigned __int64)offset32;

    total_size = (size_t)CDH_FIXED_SIZE + name_len + extra_len + comment_len;
    if (*cursor + total_size > (size_t)(end - p)) return -1;

    /* ---- Filename (raw bytes, NUL-terminated for convenience) ---- */
    {
        const unsigned char *name_ptr = h + CDH_FIXED_SIZE;
        DWORD copy_len = name_len;
        if (copy_len > ZIP_CD_NAME_MAX) copy_len = ZIP_CD_NAME_MAX;
        memcpy(e->name_raw, name_ptr, copy_len);
        e->name_raw[copy_len] = '\0';
        e->name_len = (WORD)copy_len;
    }

    /* ---- Walk the extra-field TLVs, applying Zip64 overrides ---- */
    {
        const unsigned char *xf_ptr = h + CDH_FIXED_SIZE + name_len;
        const unsigned char *xf_end = xf_ptr + extra_len;
        while (xf_ptr + 4 <= xf_end) {
            DWORD xf_id   = read_u16(xf_ptr);
            DWORD xf_size = read_u16(xf_ptr + 2);
            const unsigned char *xf_data = xf_ptr + 4;
            if (xf_data + xf_size > xf_end) break;

            if (xf_id == ZIP64_EXTRA_ID) {
                /* Fields appear in a fixed order, but only when the
                 * matching 32-bit field is 0xFFFFFFFF. Walk through as
                 * many 8-byte values as the data payload contains. */
                const unsigned char *zp = xf_data;
                const unsigned char *ze = xf_data + xf_size;
                if (orig32 == 0xFFFFFFFFUL && zp + 8 <= ze) {
                    e->orig_size = read_u64(zp); zp += 8;
                }
                if (comp32 == 0xFFFFFFFFUL && zp + 8 <= ze) {
                    e->comp_size = read_u64(zp); zp += 8;
                }
                if (offset32 == 0xFFFFFFFFUL && zp + 8 <= ze) {
                    e->local_header_offset = read_u64(zp); zp += 8;
                }
                /* 4-byte disk-start follows; we don't track multi-disk. */
            }
            xf_ptr = xf_data + xf_size;
        }
    }

    *cursor += total_size;
    return 0;
}

/* -------- Entry point -------- */

int zip_cd_parse(FILE *fp, zip_cd_entry_t **out_entries, DWORD *out_count)
{
    __int64 fsize;
    unsigned char *tail_buf = NULL;
    unsigned char *cd_buf   = NULL;
    size_t  tail_readlen = 0;
    __int64 eocd_pos = 0;
    __int64 tail_off = 0;
    __int64 cd_offset = 0;
    __int64 cd_size = 0;
    __int64 total_entries = 0;
    DWORD   disk_with_cd, disk_this;
    zip_cd_entry_t *entries = NULL;
    size_t  cursor;
    DWORD   i;
    int     rc = -1;

    *out_entries = NULL;
    *out_count   = 0;

    fsize = file_size(fp);
    zip64j_log("zip_cd_parse: fsize=%lld", (long long)fsize);
    if (fsize < EOCD_SIZE) { zip64j_log("zip_cd_parse: file too small"); return -1; }

    tail_buf = (unsigned char *)malloc(EOCD_SEARCH_MAX_BYTES);
    if (!tail_buf) return -1;

    if (find_eocd(fp, fsize, tail_buf, EOCD_SEARCH_MAX_BYTES,
                  &tail_readlen, &eocd_pos, &tail_off) != 0) {
        zip64j_log("zip_cd_parse: find_eocd failed (no EOCD sig found in last %u bytes)",
                   EOCD_SEARCH_MAX_BYTES);
        goto done;
    }
    zip64j_log("zip_cd_parse: EOCD at pos=%lld tail_off=%lld readlen=%zu",
               (long long)eocd_pos, (long long)tail_off, tail_readlen);

    /* ---- Pull 32-bit values out of the EOCD ---- */
    {
        const unsigned char *ec = tail_buf + (size_t)(eocd_pos - tail_off);
        disk_this     = read_u16(ec + EOCD_NUM_THIS_DISK);
        disk_with_cd  = read_u16(ec + EOCD_DISK_WITH_CD);
        total_entries = read_u16(ec + EOCD_TOTAL_ENTRIES);
        cd_size       = (__int64)(unsigned __int64)read_u32(ec + EOCD_SIZE_CENTRAL_DIR);
        cd_offset     = (__int64)(unsigned __int64)read_u32(ec + EOCD_OFFSET_CENTRAL_DIR);
        zip64j_log("zip_cd_parse: EOCD disk_this=%lu disk_with_cd=%lu entries=%lld "
                   "cd_size=%lld cd_offset=%lld",
                   (unsigned long)disk_this, (unsigned long)disk_with_cd,
                   (long long)total_entries, (long long)cd_size, (long long)cd_offset);
        if (disk_this != 0 || disk_with_cd != 0) {
            zip64j_log("zip_cd_parse: reject split archive");
            goto done;
        }
    }

    /* ---- Check for Zip64 EOCD Locator unconditionally ----
     *
     * APPNOTE says 32-bit fields SHOULD be 0xFFFF / 0xFFFFFFFF when Zip64 is
     * in use, but real-world archives (observed: 5+ GB Thunderbird backup
     * ZIPs) keep truncated or partial 32-bit values in the EOCD even when
     * Zip64 structures are present. The reliable signal is the Locator sig
     * sitting exactly 20 bytes before EOCD. If we see it, trust Zip64
     * values over the 32-bit EOCD values.
     */
    {
        __int64 loc_pos = eocd_pos - ZIP64_LOC_SIZE;
        unsigned char locbuf[ZIP64_LOC_SIZE];
        int got_locbuf = 0;

        if (loc_pos >= 0) {
            /* Try to read Locator from tail_buf first (zero-seek path). */
            if (loc_pos >= tail_off &&
                (__int64)tail_readlen >= (loc_pos - tail_off) + ZIP64_LOC_SIZE) {
                memcpy(locbuf, tail_buf + (size_t)(loc_pos - tail_off),
                       ZIP64_LOC_SIZE);
                got_locbuf = 1;
            } else if (seek64(fp, loc_pos, SEEK_SET) == 0 &&
                       fread(locbuf, 1, ZIP64_LOC_SIZE, fp) == ZIP64_LOC_SIZE) {
                got_locbuf = 1;
            }
        }

        if (got_locbuf && read_u32(locbuf) == ZIP64_LOC_SIG) {
            __int64 eocd64_off = read_u64(locbuf + ZIP64_LOC_OFFSET_EOCD64);
            unsigned char eocd64buf[ZIP64_EOCD_FIXED_SIZE];

            zip64j_log("zip_cd_parse: Zip64 Locator present; eocd64_off=%lld",
                       (long long)eocd64_off);

            if (seek64(fp, eocd64_off, SEEK_SET) != 0) {
                zip64j_log("zip_cd_parse: seek to eocd64 fail"); goto done;
            }
            if (fread(eocd64buf, 1, ZIP64_EOCD_FIXED_SIZE, fp) != ZIP64_EOCD_FIXED_SIZE) {
                zip64j_log("zip_cd_parse: fread eocd64 short"); goto done;
            }
            if (read_u32(eocd64buf) != ZIP64_EOCD_SIG) {
                zip64j_log("zip_cd_parse: Zip64 EOCD sig mismatch (got 0x%08lx)",
                           (unsigned long)read_u32(eocd64buf));
                goto done;
            }

            total_entries = read_u64(eocd64buf + ZIP64_EOCD_TOTAL_ENTRIES);
            cd_size       = read_u64(eocd64buf + ZIP64_EOCD_SIZE_CD);
            cd_offset     = read_u64(eocd64buf + ZIP64_EOCD_OFFSET_CD);
            zip64j_log("zip_cd_parse: Zip64 values entries=%lld cd_size=%lld cd_offset=%lld",
                       (long long)total_entries, (long long)cd_size, (long long)cd_offset);
        } else {
            zip64j_log("zip_cd_parse: no Zip64 Locator (%s); using 32-bit EOCD values",
                       got_locbuf ? "sig mismatch" : "out of range");
        }
    }

    if (total_entries < 0 || cd_size < 0 || cd_offset < 0) {
        zip64j_log("zip_cd_parse: negative field"); goto done;
    }
    if (total_entries > 0x7FFFFFFF) { zip64j_log("zip_cd_parse: entries too large"); goto done; }
    if (cd_size > CD_MAX_BYTES) {
        zip64j_log("zip_cd_parse: cd_size %lld exceeds %d byte cap",
                   (long long)cd_size, CD_MAX_BYTES);
        goto done;
    }

    /* ---- Slurp the central directory into memory ---- */
    cd_buf = (unsigned char *)malloc((size_t)cd_size);
    if (!cd_buf && cd_size > 0) { zip64j_log("zip_cd_parse: malloc cd_buf fail"); goto done; }
    if (seek64(fp, cd_offset, SEEK_SET) != 0) {
        zip64j_log("zip_cd_parse: seek to cd_offset %lld fail", (long long)cd_offset);
        goto done;
    }
    if (cd_size > 0 && fread(cd_buf, 1, (size_t)cd_size, fp) != (size_t)cd_size) {
        zip64j_log("zip_cd_parse: fread cd_buf %lld fail", (long long)cd_size);
        goto done;
    }

    /* ---- Walk the CD ---- */
    if (total_entries > 0) {
        entries = (zip_cd_entry_t *)malloc(sizeof(zip_cd_entry_t) * (size_t)total_entries);
        if (!entries) { zip64j_log("zip_cd_parse: malloc entries fail"); goto done; }
    }
    cursor = 0;
    for (i = 0; i < (DWORD)total_entries; i++) {
        if (parse_one_cdh(cd_buf, cd_buf + cd_size, &cursor, &entries[i]) != 0) {
            zip64j_log("zip_cd_parse: parse_one_cdh failed at i=%lu cursor=%zu",
                       (unsigned long)i, cursor);
            goto done;
        }
    }

    *out_entries = entries;  entries = NULL;
    *out_count   = (DWORD)total_entries;
    rc = 0;
    zip64j_log("zip_cd_parse: success count=%lu", (unsigned long)*out_count);

done:
    free(tail_buf);
    free(cd_buf);
    free(entries);
    return rc;
}
