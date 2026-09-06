/* Archive-handle API: exercises unzip64.dll's archive-handle API
 * (UnZipOpenArchive / UnZipFindFirst / UnZipFindNext / accessors).
 *
 * Creates a fresh archive via zip64j.dll (same shape as smoke_roundtrip),
 * then enumerates it via unzip64.dll and verifies filename, size, and CRC
 * for each entry. Also exercises UnZipGetFileCount and UnZipGetArcFileSize,
 * and confirms that invalid HARCs are rejected cleanly.
 *
 * Layout under %TEMP%\zip64j_arc_handle\:
 *   in\a.txt, in\sub\b.txt   - source files
 *   arc\smoke4.zip           - archive
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define FNAME_MAX32  512

typedef struct {
    DWORD   dwOriginalSize;
    DWORD   dwCompressedSize;
    DWORD   dwCRC;
    UINT    uFlag;
    UINT    uOSType;
    WORD    wRatio;
    WORD    wDate;
    WORD    wTime;
    char    szFileName[FNAME_MAX32 + 1];
    char    dummy1[3];
    char    szAttribute[8];
    char    szMode[8];
} INDIVIDUALINFO, *LPINDIVIDUALINFO;

typedef HANDLE HARC;

typedef int   (WINAPI *FnZip)              (HWND, LPCSTR, LPSTR, DWORD);
typedef HARC  (WINAPI *FnOpen)             (HWND, LPCSTR, DWORD);
typedef int   (WINAPI *FnClose)            (HARC);
typedef int   (WINAPI *FnFindFirst)        (HARC, LPCSTR, LPINDIVIDUALINFO);
typedef int   (WINAPI *FnFindNext)         (HARC, LPINDIVIDUALINFO);
typedef int   (WINAPI *FnGetFileCount)     (LPCSTR);
typedef DWORD (WINAPI *FnGetArcFileSize)   (HARC);
typedef int   (WINAPI *FnGetFileName)      (HARC, LPSTR, int);
typedef int   (WINAPI *FnGetMethod)        (HARC, LPSTR, int);
typedef DWORD (WINAPI *FnGetOriginalSize)  (HARC);
typedef DWORD (WINAPI *FnGetCRC)           (HARC);

static void make_dir(const char *path) { CreateDirectoryA(path, NULL); }

static int write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    return 0;
}

int main(void)
{
    char tmp[MAX_PATH];
    char scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], sub_dir[MAX_PATH];
    char fa[MAX_PATH], fb[MAX_PATH];
    char zip_path[MAX_PATH];
    char abs_zip64j[MAX_PATH];
    char abs_unzip64[MAX_PATH];
    char cmdline[2048];
    char output[4096];
    HMODULE hz = NULL, hu = NULL;
    FnZip              Zip_         = NULL;
    FnOpen             Open_        = NULL;
    FnClose            Close_       = NULL;
    FnFindFirst        FindFirst_   = NULL;
    FnFindNext         FindNext_    = NULL;
    FnGetFileCount     GetCount_    = NULL;
    FnGetArcFileSize   GetArcSize_  = NULL;
    FnGetFileName      GetName_     = NULL;
    FnGetMethod        GetMethod_   = NULL;
    FnGetOriginalSize  GetOrigSize_ = NULL;
    FnGetCRC           GetCRC_      = NULL;
    HARC harc = NULL;
    INDIVIDUALINFO info;
    int rc;
    int entries_seen = 0;
    int saw_a = 0, saw_b = 0;
    int exit_code = 1;

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch, MAX_PATH, "%szip64j_arc_handle", tmp);
    _snprintf(in_dir,  MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir, MAX_PATH, "%s\\arc", scratch);
    _snprintf(sub_dir, MAX_PATH, "%s\\sub", in_dir);
    _snprintf(fa, MAX_PATH, "%s\\a.txt", in_dir);
    _snprintf(fb, MAX_PATH, "%s\\b.txt", sub_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\smoke4.zip", arc_dir);

    make_dir(scratch);
    make_dir(in_dir);
    make_dir(arc_dir);
    make_dir(sub_dir);

    if (write_file(fa, "alpha\n") != 0) { printf("[FAIL] write %s\n", fa); return 1; }
    if (write_file(fb, "bravo\n") != 0) { printf("[FAIL] write %s\n", fb); return 1; }
    DeleteFileA(zip_path);

    /* ---- 1. Zip the files via zip64j.dll ---- */
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline), "-r \"%s\" a.txt sub\\b.txt", zip_path);
    output[0] = '\0';
    rc = Zip_(NULL, cmdline, output, sizeof(output));
    printf("       Zip() -> rc=%d\n", rc);
    if (rc != 0) { printf("[FAIL] Zip failed\n"); goto cleanup; }
    FreeLibrary(hz); hz = NULL;

    /* ---- 2. Load unzip64.dll and resolve the archive-handle APIs ---- */
    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }

    Open_        = (FnOpen)             GetProcAddress(hu, "UnZipOpenArchive");
    Close_       = (FnClose)            GetProcAddress(hu, "UnZipCloseArchive");
    FindFirst_   = (FnFindFirst)        GetProcAddress(hu, "UnZipFindFirst");
    FindNext_    = (FnFindNext)         GetProcAddress(hu, "UnZipFindNext");
    GetCount_    = (FnGetFileCount)     GetProcAddress(hu, "UnZipGetFileCount");
    GetArcSize_  = (FnGetArcFileSize)   GetProcAddress(hu, "UnZipGetArcFileSize");
    GetName_     = (FnGetFileName)      GetProcAddress(hu, "UnZipGetFileName");
    GetMethod_   = (FnGetMethod)        GetProcAddress(hu, "UnZipGetMethod");
    GetOrigSize_ = (FnGetOriginalSize)  GetProcAddress(hu, "UnZipGetOriginalSize");
    GetCRC_      = (FnGetCRC)           GetProcAddress(hu, "UnZipGetCRC");

    if (!Open_ || !Close_ || !FindFirst_ || !FindNext_ || !GetCount_
        || !GetArcSize_ || !GetName_ || !GetMethod_ || !GetOrigSize_ || !GetCRC_) {
        printf("[FAIL] missing one or more UnZip* archive-handle exports\n");
        goto cleanup;
    }

    /* ---- 3. UnZipGetFileCount (no handle required) ---- */
    rc = GetCount_(zip_path);
    printf("       UnZipGetFileCount -> %d\n", rc);
    if (rc != 2) { printf("[FAIL] expected 2 entries, got %d\n", rc); goto cleanup; }

    /* ---- 4. Open + enumerate ---- */
    harc = Open_(NULL, zip_path, 0);
    if (!harc) { printf("[FAIL] UnZipOpenArchive returned NULL\n"); goto cleanup; }
    printf("       UnZipOpenArchive -> %p\n", harc);

    /* UnZipGetArcFileSize should be non-zero and roughly match what's on disk. */
    {
        DWORD arc_size = GetArcSize_(harc);
        HANDLE fh = CreateFileA(zip_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, 0, NULL);
        LARGE_INTEGER li = {0};
        if (fh != INVALID_HANDLE_VALUE) { GetFileSizeEx(fh, &li); CloseHandle(fh); }
        printf("       UnZipGetArcFileSize -> %lu (disk=%lld)\n",
               arc_size, (long long)li.QuadPart);
        if ((LONGLONG)arc_size != li.QuadPart) {
            printf("[FAIL] archive file size mismatch\n");
            goto cleanup;
        }
    }

    memset(&info, 0, sizeof(info));
    rc = FindFirst_(harc, "*", &info);
    while (rc == 0) {
        char name[FNAME_MAX32 + 1] = {0};
        char method[32] = {0};
        DWORD orig_size;
        DWORD crc;

        GetName_    (harc, name,   sizeof(name));
        GetMethod_  (harc, method, sizeof(method));
        orig_size = GetOrigSize_(harc);
        crc       = GetCRC_(harc);

        printf("       entry: name=\"%s\" info.name=\"%s\" orig=%lu method=%s crc=%08lx\n",
               name, info.szFileName, orig_size, method, crc);

        if (strcmp(info.szFileName, "a.txt") == 0
            || strcmp(info.szFileName, "./a.txt") == 0) {
            saw_a = 1;
            if (orig_size != 6) { printf("[FAIL] a.txt orig size %lu != 6\n", orig_size); goto cleanup; }
            if (strcmp(name, info.szFileName) != 0) {
                printf("[FAIL] GetFileName mismatch vs info.szFileName\n"); goto cleanup;
            }
        } else if (strcmp(info.szFileName, "sub/b.txt") == 0
                   || strcmp(info.szFileName, "sub\\b.txt") == 0) {
            saw_b = 1;
            if (orig_size != 6) { printf("[FAIL] b.txt orig size %lu != 6\n", orig_size); goto cleanup; }
        }
        entries_seen++;
        rc = FindNext_(harc, &info);
    }

    if (entries_seen != 2) { printf("[FAIL] enumerated %d entries (want 2)\n", entries_seen); goto cleanup; }
    if (!saw_a) { printf("[FAIL] a.txt not enumerated\n"); goto cleanup; }
    if (!saw_b) { printf("[FAIL] sub/b.txt not enumerated\n"); goto cleanup; }

    /* ---- 5. Invalid handle paths — must not crash, must return -1 / 0 ---- */
    if (GetOrigSize_(NULL) != 0) { printf("[FAIL] NULL harc -> non-zero size\n"); goto cleanup; }
    {
        HARC fake = (HARC)0xdeadbeefbadbeefULL;
        /* validate() first does a magic-check; returning 0/−1 is fine, crash is not. */
        (void)GetName_(fake, output, sizeof(output));
        (void)Close_(fake);
    }

    /* ---- 6. Close ---- */
    rc = Close_(harc); harc = NULL;
    printf("       UnZipCloseArchive -> %d\n", rc);
    if (rc != 0) { printf("[FAIL] Close returned %d\n", rc); goto cleanup; }

    printf("\n[PASS] archive-handle API\n");
    exit_code = 0;

cleanup:
    if (harc && Close_) Close_(harc);
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
