/* Zip64 EOCD: Zip64 without 0xFFFFFFFF sentinel in the 32-bit EOCD.
 *
 * Regression guard for the 5GB-archive case observed via afxw diagnostics on
 * 2026-04-19: the archive had a Zip64 EOCD Locator + Zip64 EOCD Record, but
 * the ordinary (32-bit) EOCD did NOT contain 0xFFFFFFFF / 0xFFFF sentinel
 * values — it kept a truncated/stale 32-bit view of the CD offset.
 *
 * Pre-fix behavior: zip_cd_parse trusted the 32-bit cd_offset, seeked to a
 * bogus location, and failed to parse any entry.
 *
 * Post-fix: whenever a Zip64 EOCD Locator signature is present 20 bytes
 * before the regular EOCD, we override with Zip64 values.
 *
 * We synthesize such an archive with a single 4-byte "stored" entry plus
 * hand-written Zip64 structures. Creating a real >4GB file would be
 * prohibitive for a smoke test, so we use misleading-but-consistent
 * 32-bit EOCD fields that don't hit sentinels.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef HANDLE HARC;

typedef HARC (WINAPI *FnOpen)   (HWND, LPCSTR, DWORD);
typedef int  (WINAPI *FnClose)  (HARC);

typedef struct {
    DWORD dwOriginalSize, dwCompressedSize, dwCRC;
    UINT  uFlag, uOSType;
    WORD  wRatio, wDate, wTime;
    char  szFileName[513];
    char  dummy[3];
    char  szAttribute[8];
    char  szMode[8];
} INDIVIDUALINFO, *LPINDIVIDUALINFO;

typedef int  (WINAPI *FnFindFirst)(HARC, LPCSTR, LPINDIVIDUALINFO);

static void put_u16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}
static void put_u32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static void put_u64(unsigned char *p, unsigned __int64 v)
{
    put_u32(p, (unsigned long)(v & 0xFFFFFFFFULL));
    put_u32(p + 4, (unsigned long)((v >> 32) & 0xFFFFFFFFULL));
}

/* CRC32 (polynomial 0xEDB88320, stored-entry friendly). */
static unsigned long crc32_calc(const unsigned char *p, size_t n)
{
    static unsigned long table[256];
    static int init = 0;
    unsigned long crc = 0xFFFFFFFFUL;
    size_t i;
    int j;
    if (!init) {
        for (i = 0; i < 256; i++) {
            unsigned long c = (unsigned long)i;
            for (j = 0; j < 8; j++)
                c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = 1;
    }
    for (i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}

/* Build a tiny zip with one stored entry "hello.txt" / content "hi!\n",
 * containing a Zip64 EOCD Locator + Zip64 EOCD Record, but with the
 * ordinary 32-bit EOCD using truncated non-sentinel values.
 *
 * Layout:
 *   [LocalFileHeader][data]
 *   [CDH + Zip64 extra (orig+comp+ofs)]
 *   [Zip64 EOCD Record]
 *   [Zip64 EOCD Locator]
 *   [32-bit EOCD — non-sentinel, but ignored because Locator wins]
 */
static int build_zip(const char *path)
{
    const char *name = "hello.txt";
    const unsigned char data[] = { 'h', 'i', '!', '\n' };
    unsigned long crc = crc32_calc(data, sizeof(data));
    unsigned char buf[1024];
    size_t pos = 0, lh_off, cd_off, eocd64_off, loc_off, eocd_off;
    FILE *fp;

    /* Local File Header */
    lh_off = pos;
    put_u32(buf + pos, 0x04034b50UL); pos += 4;
    put_u16(buf + pos, 20); pos += 2;       /* version needed */
    put_u16(buf + pos, 0);  pos += 2;       /* gp flag */
    put_u16(buf + pos, 0);  pos += 2;       /* method = stored */
    put_u16(buf + pos, 0);  pos += 2;       /* dos time */
    put_u16(buf + pos, 0x21); pos += 2;     /* dos date (1980-01-01) */
    put_u32(buf + pos, crc); pos += 4;
    put_u32(buf + pos, (unsigned long)sizeof(data)); pos += 4; /* comp */
    put_u32(buf + pos, (unsigned long)sizeof(data)); pos += 4; /* orig */
    put_u16(buf + pos, (unsigned)strlen(name)); pos += 2;
    put_u16(buf + pos, 0); pos += 2;                           /* extra */
    memcpy(buf + pos, name, strlen(name));
    pos += strlen(name);
    memcpy(buf + pos, data, sizeof(data));
    pos += sizeof(data);

    /* Central Directory Header with Zip64 extra field that overrides offset
     * with a deliberately huge value (> 4GB). The on-disk offset is `lh_off`
     * (small), but we lie to force Zip64 promotion via the extra field. */
    cd_off = pos;
    put_u32(buf + pos, 0x02014b50UL); pos += 4;
    put_u16(buf + pos, 20); pos += 2;                 /* version made by */
    put_u16(buf + pos, 20); pos += 2;                 /* version needed */
    put_u16(buf + pos, 0);  pos += 2;                 /* gp flag */
    put_u16(buf + pos, 0);  pos += 2;                 /* method */
    put_u16(buf + pos, 0);  pos += 2;
    put_u16(buf + pos, 0x21); pos += 2;
    put_u32(buf + pos, crc); pos += 4;
    put_u32(buf + pos, (unsigned long)sizeof(data)); pos += 4;
    put_u32(buf + pos, (unsigned long)sizeof(data)); pos += 4;
    put_u16(buf + pos, (unsigned)strlen(name)); pos += 2;
    put_u16(buf + pos, 12); pos += 2;                 /* extra len = 4+8 */
    put_u16(buf + pos, 0); pos += 2;                  /* comment len */
    put_u16(buf + pos, 0); pos += 2;                  /* disk start */
    put_u16(buf + pos, 0); pos += 2;                  /* int attrs */
    put_u32(buf + pos, 0); pos += 4;                  /* ext attrs */
    put_u32(buf + pos, 0xFFFFFFFFUL); pos += 4;       /* local offset = sentinel */
    memcpy(buf + pos, name, strlen(name)); pos += strlen(name);
    /* Zip64 extra field: id=0x0001, size=8, one u64 = true offset. */
    put_u16(buf + pos, 0x0001); pos += 2;
    put_u16(buf + pos, 8); pos += 2;
    put_u64(buf + pos, (unsigned __int64)lh_off); pos += 8;

    /* Zip64 EOCD Record */
    eocd64_off = pos;
    put_u32(buf + pos, 0x06064b50UL); pos += 4;
    put_u64(buf + pos, 44); pos += 8;                 /* size of zip64 eocd */
    put_u16(buf + pos, 20); pos += 2;                 /* version made by */
    put_u16(buf + pos, 20); pos += 2;                 /* version needed */
    put_u32(buf + pos, 0); pos += 4;                  /* disk number */
    put_u32(buf + pos, 0); pos += 4;                  /* disk with CD */
    put_u64(buf + pos, 1); pos += 8;                  /* entries this disk */
    put_u64(buf + pos, 1); pos += 8;                  /* total entries */
    put_u64(buf + pos, (unsigned __int64)(eocd64_off - cd_off)); pos += 8; /* CD size */
    put_u64(buf + pos, (unsigned __int64)cd_off); pos += 8;                /* CD offset */

    /* Zip64 EOCD Locator */
    loc_off = pos;
    put_u32(buf + pos, 0x07064b50UL); pos += 4;
    put_u32(buf + pos, 0); pos += 4;                  /* disk with zip64 EOCD */
    put_u64(buf + pos, (unsigned __int64)eocd64_off); pos += 8;
    put_u32(buf + pos, 1); pos += 4;                  /* total disks */

    /* Ordinary 32-bit EOCD — NOTE: we write TRUTHFUL 32-bit values here
     * (i.e. NO 0xFFFF / 0xFFFFFFFF sentinels). That is exactly the
     * real-world pattern that bit us on the 5GB archive: Zip64 structures
     * exist, but the EOCD fields aren't poisoned. A compliant reader must
     * still consult the Zip64 EOCD via the Locator. */
    eocd_off = pos;
    put_u32(buf + pos, 0x06054b50UL); pos += 4;
    put_u16(buf + pos, 0); pos += 2;                  /* disk number */
    put_u16(buf + pos, 0); pos += 2;                  /* disk with CD */
    put_u16(buf + pos, 1); pos += 2;                  /* entries this disk */
    put_u16(buf + pos, 1); pos += 2;                  /* total entries */
    put_u32(buf + pos, (unsigned long)(eocd64_off - cd_off)); pos += 4; /* cd size */
    put_u32(buf + pos, (unsigned long)cd_off); pos += 4;                /* cd offset */
    put_u16(buf + pos, 0); pos += 2;                  /* comment length */

    (void)loc_off;

    fp = fopen(path, "wb");
    if (!fp) return -1;
    fwrite(buf, 1, pos, fp);
    fclose(fp);
    return 0;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH], zip_path[MAX_PATH];
    char abs_unzip64[MAX_PATH];
    HMODULE hu = NULL;
    FnOpen      Open_      = NULL;
    FnClose     Close_     = NULL;
    FnFindFirst FindFirst_ = NULL;
    HARC harc = NULL;
    INDIVIDUALINFO info;
    int exit_code = 1;
    int rc;

    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch, MAX_PATH, "%szip64j_zip64_eocd", tmp);
    _snprintf(zip_path, MAX_PATH, "%s\\zip64-no-sentinel.zip", scratch);
    CreateDirectoryA(scratch, NULL);
    DeleteFileA(zip_path);

    if (build_zip(zip_path) != 0) {
        printf("[FAIL] build_zip\n"); return 1;
    }
    printf("       synthesized archive: %s\n", zip_path);

    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary err=%lu\n", GetLastError()); goto cleanup; }
    Open_      = (FnOpen)      GetProcAddress(hu, "UnZipOpenArchive");
    Close_     = (FnClose)     GetProcAddress(hu, "UnZipCloseArchive");
    FindFirst_ = (FnFindFirst) GetProcAddress(hu, "UnZipFindFirst");
    if (!Open_ || !Close_ || !FindFirst_) { printf("[FAIL] missing exports\n"); goto cleanup; }

    harc = Open_(NULL, zip_path, 0);
    if (!harc) {
        printf("[FAIL] UnZipOpenArchive returned NULL — Zip64 Locator-without-"
               "sentinel handling is broken\n");
        goto cleanup;
    }

    memset(&info, 0, sizeof(info));
    rc = FindFirst_(harc, "*", &info);
    printf("       FindFirst rc=%d name=\"%s\" orig=%lu\n",
           rc, info.szFileName, info.dwOriginalSize);
    if (rc != 0) { printf("[FAIL] FindFirst rc=%d\n", rc); goto cleanup; }
    if (strcmp(info.szFileName, "hello.txt") != 0) {
        printf("[FAIL] name mismatch\n"); goto cleanup;
    }
    if (info.dwOriginalSize != 4) {
        printf("[FAIL] size mismatch\n"); goto cleanup;
    }

    printf("\n[PASS] Zip64 locator without sentinel EOCD\n");
    exit_code = 0;

cleanup:
    if (harc && Close_) Close_(harc);
    if (hu) FreeLibrary(hu);
    return exit_code;
}
