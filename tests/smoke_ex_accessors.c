/* Ex / 64 extended accessors: exercises unzip64.dll's Ex / 64 extended accessors.
 *
 * Builds a fresh archive via zip64j.dll, opens via unzip64.dll, and for
 * each entry checks:
 *   - UnZipGetOriginalSizeEx / UnZipGetCompressedSizeEx (64bit size)
 *   - UnZipGetWriteTimeEx   (FILETIME)
 *   - UnZipGetWriteTime64   (__int64, 100ns since 1601)
 *   - UnZipGetCreateTimeEx/64, UnZipGetAccessTimeEx/64 (same source time)
 * Also exercises UnZipGetArc{Original,Compressed}SizeEx, UnZipOpenArchive2,
 * and verifies UnZipQueryFunctionList returns TRUE for the Ex / 64 ISARC_*
 * opcodes.
 *
 * Layout under %TEMP%\zip64j_ex_accessors\:
 *   in\a.txt, in\b.txt   - source files (sizes 6 and 2048 bytes)
 *   arc\smoke5.zip
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define FNAME_MAX32  512

/* ---- ISARC opcodes we verify (must match include/zip64j.h) ---- */
#define ISARC_OPEN_ARCHIVE2              91
#define ISARC_GET_WRITE_TIME_EX          70
#define ISARC_GET_CREATE_TIME_EX         71
#define ISARC_GET_ACCESS_TIME_EX         72
#define ISARC_GET_ORIGINAL_SIZE_EX       85
#define ISARC_GET_COMPRESSED_SIZE_EX     86
#define ISARC_GET_WRITE_TIME_64         111
#define ISARC_GET_CREATE_TIME_64        112
#define ISARC_GET_ACCESS_TIME_64        113

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

typedef int   (WINAPI *FnZip)            (HWND, LPCSTR, LPSTR, DWORD);
typedef HARC  (WINAPI *FnOpen)           (HWND, LPCSTR, DWORD);
typedef HARC  (WINAPI *FnOpen2)          (HWND, LPCSTR, DWORD, void *);
typedef int   (WINAPI *FnClose)          (HARC);
typedef int   (WINAPI *FnFindFirst)      (HARC, LPCSTR, LPINDIVIDUALINFO);
typedef int   (WINAPI *FnFindNext)       (HARC, LPINDIVIDUALINFO);
typedef BOOL  (WINAPI *FnQueryFnList)    (int);
typedef DWORD (WINAPI *FnGetSize32)      (HARC);
typedef BOOL  (WINAPI *FnGetSizeEx)      (HARC, __int64 *);
typedef BOOL  (WINAPI *FnGetTimeEx)      (HARC, FILETIME *);
typedef BOOL  (WINAPI *FnGetTime64)      (HARC, __int64 *);

static int write_file(const char *path, int len)
{
    FILE *fp = fopen(path, "wb");
    int i;
    if (!fp) return -1;
    for (i = 0; i < len; i++) fputc('a' + (i % 26), fp);
    fclose(fp);
    return 0;
}

static void filetime_to_int64(const FILETIME *pft, __int64 *out)
{
    ULARGE_INTEGER u;
    u.LowPart  = pft->dwLowDateTime;
    u.HighPart = pft->dwHighDateTime;
    *out = (__int64)u.QuadPart;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH];
    char fa[MAX_PATH], fb[MAX_PATH];
    char zip_path[MAX_PATH];
    char abs_zip64j[MAX_PATH], abs_unzip64[MAX_PATH];
    char cmdline[2048], output[4096];

    HMODULE hz = NULL, hu = NULL;
    FnZip        Zip_ = NULL;
    FnOpen       Open_ = NULL;
    FnOpen2      Open2_ = NULL;
    FnClose      Close_ = NULL;
    FnFindFirst  FindFirst_ = NULL;
    FnFindNext   FindNext_ = NULL;
    FnQueryFnList QFL_ = NULL;

    FnGetSize32  GetOrig32_ = NULL, GetComp32_ = NULL;
    FnGetSizeEx  GetOrigEx_ = NULL, GetCompEx_ = NULL;
    FnGetSizeEx  GetArcOrigEx_ = NULL, GetArcCompEx_ = NULL, GetArcFileEx_ = NULL;
    FnGetTimeEx  GetWriteEx_ = NULL, GetCreateEx_ = NULL, GetAccessEx_ = NULL;
    FnGetTime64  GetWrite64_ = NULL, GetCreate64_ = NULL, GetAccess64_ = NULL;

    HARC harc = NULL;
    INDIVIDUALINFO info;
    int rc;
    int entries = 0;
    int saw_a = 0, saw_b = 0;
    int exit_code = 1;

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch, MAX_PATH, "%szip64j_ex_accessors", tmp);
    _snprintf(in_dir,  MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir, MAX_PATH, "%s\\arc", scratch);
    _snprintf(fa, MAX_PATH, "%s\\a.txt", in_dir);
    _snprintf(fb, MAX_PATH, "%s\\b.txt", in_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\smoke5.zip", arc_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir, NULL);
    CreateDirectoryA(arc_dir, NULL);
    if (write_file(fa, 6)    != 0) { printf("[FAIL] write %s\n", fa); return 1; }
    if (write_file(fb, 2048) != 0) { printf("[FAIL] write %s\n", fb); return 1; }
    DeleteFileA(zip_path);

    /* ---- 1. Zip -----------------------------------------------------*/
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline), "\"%s\" a.txt b.txt", zip_path);
    output[0] = '\0';
    rc = Zip_(NULL, cmdline, output, sizeof(output));
    if (rc != 0) { printf("[FAIL] Zip rc=%d output=%s\n", rc, output); goto cleanup; }
    FreeLibrary(hz); hz = NULL;

    /* ---- 2. Load unzip64 and resolve symbols ------------------------*/
    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }

    Open_        = (FnOpen)       GetProcAddress(hu, "UnZipOpenArchive");
    Open2_       = (FnOpen2)      GetProcAddress(hu, "UnZipOpenArchive2");
    Close_       = (FnClose)      GetProcAddress(hu, "UnZipCloseArchive");
    FindFirst_   = (FnFindFirst)  GetProcAddress(hu, "UnZipFindFirst");
    FindNext_    = (FnFindNext)   GetProcAddress(hu, "UnZipFindNext");
    QFL_         = (FnQueryFnList)GetProcAddress(hu, "UnZipQueryFunctionList");

    GetOrig32_   = (FnGetSize32) GetProcAddress(hu, "UnZipGetOriginalSize");
    GetComp32_   = (FnGetSize32) GetProcAddress(hu, "UnZipGetCompressedSize");
    GetOrigEx_   = (FnGetSizeEx) GetProcAddress(hu, "UnZipGetOriginalSizeEx");
    GetCompEx_   = (FnGetSizeEx) GetProcAddress(hu, "UnZipGetCompressedSizeEx");
    GetArcFileEx_= (FnGetSizeEx) GetProcAddress(hu, "UnZipGetArcFileSizeEx");
    GetArcOrigEx_= (FnGetSizeEx) GetProcAddress(hu, "UnZipGetArcOriginalSizeEx");
    GetArcCompEx_= (FnGetSizeEx) GetProcAddress(hu, "UnZipGetArcCompressedSizeEx");

    GetWriteEx_  = (FnGetTimeEx) GetProcAddress(hu, "UnZipGetWriteTimeEx");
    GetCreateEx_ = (FnGetTimeEx) GetProcAddress(hu, "UnZipGetCreateTimeEx");
    GetAccessEx_ = (FnGetTimeEx) GetProcAddress(hu, "UnZipGetAccessTimeEx");
    GetWrite64_  = (FnGetTime64) GetProcAddress(hu, "UnZipGetWriteTime64");
    GetCreate64_ = (FnGetTime64) GetProcAddress(hu, "UnZipGetCreateTime64");
    GetAccess64_ = (FnGetTime64) GetProcAddress(hu, "UnZipGetAccessTime64");

    if (!Open_ || !Open2_ || !Close_ || !FindFirst_ || !FindNext_ || !QFL_
        || !GetOrig32_ || !GetComp32_ || !GetOrigEx_ || !GetCompEx_
        || !GetArcFileEx_ || !GetArcOrigEx_ || !GetArcCompEx_
        || !GetWriteEx_ || !GetCreateEx_ || !GetAccessEx_
        || !GetWrite64_ || !GetCreate64_ || !GetAccess64_) {
        printf("[FAIL] missing Ex/64 / Open2 / QueryFnList export\n");
        goto cleanup;
    }

    /* ---- 3. UnZipQueryFunctionList for new opcodes ------------------*/
    {
        const struct { int op; const char *n; } new_ops[] = {
            { ISARC_OPEN_ARCHIVE2,          "OPEN_ARCHIVE2" },
            { ISARC_GET_ORIGINAL_SIZE_EX,   "GET_ORIGINAL_SIZE_EX" },
            { ISARC_GET_COMPRESSED_SIZE_EX, "GET_COMPRESSED_SIZE_EX" },
            { ISARC_GET_WRITE_TIME_EX,      "GET_WRITE_TIME_EX" },
            { ISARC_GET_CREATE_TIME_EX,     "GET_CREATE_TIME_EX" },
            { ISARC_GET_ACCESS_TIME_EX,     "GET_ACCESS_TIME_EX" },
            { ISARC_GET_WRITE_TIME_64,      "GET_WRITE_TIME_64" },
            { ISARC_GET_CREATE_TIME_64,     "GET_CREATE_TIME_64" },
            { ISARC_GET_ACCESS_TIME_64,     "GET_ACCESS_TIME_64" },
        };
        size_t i;
        for (i = 0; i < sizeof(new_ops)/sizeof(new_ops[0]); i++) {
            if (!QFL_(new_ops[i].op)) {
                printf("[FAIL] QueryFunctionList(%s=%d) != TRUE\n",
                       new_ops[i].n, new_ops[i].op);
                goto cleanup;
            }
        }
        printf("       QueryFunctionList: 9 new opcodes all TRUE\n");
    }

    /* ---- 4. UnZipOpenArchive2 (Ex open) -----------------------------*/
    harc = Open2_(NULL, zip_path, 0, NULL);
    if (!harc) { printf("[FAIL] UnZipOpenArchive2 returned NULL\n"); goto cleanup; }
    printf("       UnZipOpenArchive2 -> %p\n", harc);

    /* ---- 5. Archive-wide *_Ex ---------------------------------------*/
    {
        __int64 arc_file = 0, arc_orig = 0, arc_comp = 0;
        if (!GetArcFileEx_(harc, &arc_file) || !GetArcOrigEx_(harc, &arc_orig)
            || !GetArcCompEx_(harc, &arc_comp)) {
            printf("[FAIL] GetArc*SizeEx returned FALSE\n"); goto cleanup;
        }
        printf("       ArcFileSizeEx=%lld ArcOrigEx=%lld ArcCompEx=%lld\n",
               arc_file, arc_orig, arc_comp);
        if (arc_orig != 6 + 2048) {
            printf("[FAIL] ArcOrigEx %lld != %d\n", arc_orig, 6 + 2048);
            goto cleanup;
        }
        if (arc_file <= 0 || arc_file != arc_file /* sanity */) {
            printf("[FAIL] ArcFileSizeEx nonpositive\n"); goto cleanup;
        }
    }

    /* ---- 6. Per-entry *_Ex / *_64 -----------------------------------*/
    memset(&info, 0, sizeof(info));
    rc = FindFirst_(harc, "*", &info);
    while (rc == 0) {
        __int64 orig64 = 0, comp64 = 0;
        DWORD   orig32, comp32;
        FILETIME ftw, ftc, fta;
        __int64 tw64 = 0, tc64 = 0, ta64 = 0;
        __int64 ftw_as_int64 = 0;

        if (!GetOrigEx_(harc, &orig64) || !GetCompEx_(harc, &comp64)) {
            printf("[FAIL] Get{Orig,Comp}Ex FALSE\n"); goto cleanup;
        }
        orig32 = GetOrig32_(harc);
        comp32 = GetComp32_(harc);
        if ((DWORD)(orig64 & 0xFFFFFFFFULL) != orig32
            || (DWORD)(comp64 & 0xFFFFFFFFULL) != comp32) {
            printf("[FAIL] Ex/32 mismatch for %s\n", info.szFileName);
            goto cleanup;
        }

        if (!GetWriteEx_(harc, &ftw) || !GetCreateEx_(harc, &ftc) || !GetAccessEx_(harc, &fta)) {
            printf("[FAIL] Get{Write,Create,Access}TimeEx FALSE\n"); goto cleanup;
        }
        if (!GetWrite64_(harc, &tw64) || !GetCreate64_(harc, &tc64) || !GetAccess64_(harc, &ta64)) {
            printf("[FAIL] Get{Write,Create,Access}Time64 FALSE\n"); goto cleanup;
        }
        filetime_to_int64(&ftw, &ftw_as_int64);
        /* Our impl: *_Ex and *_64 derive from the same DOS date/time, so all
         * three (write/create/access) should agree, and Ex == 64.            */
        if (ftw_as_int64 != tw64) {
            printf("[FAIL] WriteTimeEx (as int64 %lld) != WriteTime64 %lld\n",
                   ftw_as_int64, tw64);
            goto cleanup;
        }
        if (tw64 != tc64 || tw64 != ta64) {
            printf("[FAIL] write/create/access mismatch %lld/%lld/%lld\n",
                   tw64, tc64, ta64);
            goto cleanup;
        }
        if (tw64 <= 0) {
            printf("[FAIL] Time64 nonpositive %lld for %s\n", tw64, info.szFileName);
            goto cleanup;
        }

        printf("       entry \"%s\" origEx=%lld compEx=%lld time64=%lld\n",
               info.szFileName, orig64, comp64, tw64);

        if (strcmp(info.szFileName, "a.txt") == 0) {
            saw_a = 1;
            if (orig64 != 6)    { printf("[FAIL] a.txt orig64 %lld\n", orig64); goto cleanup; }
        } else if (strcmp(info.szFileName, "b.txt") == 0) {
            saw_b = 1;
            if (orig64 != 2048) { printf("[FAIL] b.txt orig64 %lld\n", orig64); goto cleanup; }
        }
        entries++;
        rc = FindNext_(harc, &info);
    }

    if (entries != 2) { printf("[FAIL] entries=%d\n", entries); goto cleanup; }
    if (!saw_a || !saw_b) { printf("[FAIL] missed a.txt or b.txt\n"); goto cleanup; }

    /* ---- 7. Invalid-handle sanity for Ex/64 accessors ---------------*/
    {
        __int64 dummy = 1;
        FILETIME dummy_ft = {1, 1};
        if (GetOrigEx_(NULL, &dummy) != FALSE || dummy != 0) {
            printf("[FAIL] GetOrigEx(NULL) must be FALSE and zero out\n"); goto cleanup;
        }
        if (GetWriteEx_(NULL, &dummy_ft) != FALSE
            || dummy_ft.dwLowDateTime != 0 || dummy_ft.dwHighDateTime != 0) {
            printf("[FAIL] GetWriteEx(NULL) must be FALSE and zero FILETIME\n"); goto cleanup;
        }
    }

    rc = Close_(harc); harc = NULL;
    if (rc != 0) { printf("[FAIL] Close rc=%d\n", rc); goto cleanup; }

    printf("\n[PASS] Ex / 64 accessors + QueryFunctionList\n");
    exit_code = 0;

cleanup:
    if (harc && Close_) Close_(harc);
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
