/* Unicode (W) API roundtrip with Japanese filenames.
 *
 * Exercises:
 *   ZipW                    - zip a file whose name contains CP932 kanji
 *   UnZipOpenArchiveW       - open the resulting archive via W path
 *   UnZipFindFirstW / NextW - enumerate the W INDIVIDUALINFOW
 *   UnZipGetFileNameW       - read back the W name
 *   UnZipW                  - extract via W cmdline
 *
 * Japanese chars are expressed with \uXXXX escapes so the source compiles
 * under /utf-8 without relying on BOM or build-system locale.
 *
 * Layout under %TEMP%\zip64j_unicode\:
 *   in\日本語.txt          - source file (ASCII content, Japanese name)
 *   arc\smoke6.zip         - archive
 *   out\*                  - extraction target
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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
    WCHAR   szFileName[FNAME_MAX32 + 1];
    WCHAR   dummy1[3];
    WCHAR   szAttribute[8];
    WCHAR   szMode[8];
} INDIVIDUALINFOW, *LPINDIVIDUALINFOW;

typedef HANDLE HARC;

typedef int   (WINAPI *FnZipW)         (HWND, LPCWSTR, LPWSTR, DWORD);
typedef int   (WINAPI *FnUnZipW)       (HWND, LPCWSTR, LPWSTR, DWORD);
typedef HARC  (WINAPI *FnOpenW)        (HWND, LPCWSTR, DWORD);
typedef int   (WINAPI *FnClose)        (HARC);
typedef int   (WINAPI *FnFindFirstW)   (HARC, LPCWSTR, LPINDIVIDUALINFOW);
typedef int   (WINAPI *FnFindNextW)    (HARC, LPINDIVIDUALINFOW);
typedef int   (WINAPI *FnGetFileNameW) (HARC, LPWSTR, int);

/* 日本語.txt — nihongo.txt */
static const WCHAR kJpName[] = L"\u65E5\u672C\u8A9E.txt";

static int write_file_w(LPCWSTR path, const char *content)
{
    FILE *fp = _wfopen(path, L"wb");
    if (!fp) return -1;
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    return 0;
}

int main(void)
{
    WCHAR tmp[MAX_PATH], scratch[MAX_PATH];
    WCHAR in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    WCHAR jp_path[MAX_PATH], zip_path[MAX_PATH];
    WCHAR extracted_path[MAX_PATH];
    char  abs_zip64j[MAX_PATH], abs_unzip64[MAX_PATH];
    WCHAR cmdline[2048];
    WCHAR wout[4096];

    HMODULE hz = NULL, hu = NULL;
    FnZipW        ZipW_ = NULL;
    FnUnZipW      UnZipW_ = NULL;
    FnOpenW       OpenW_ = NULL;
    FnClose       Close_ = NULL;
    FnFindFirstW  FindFirstW_ = NULL;
    FnFindNextW   FindNextW_ = NULL;
    FnGetFileNameW GetFileNameW_ = NULL;

    HARC harc = NULL;
    INDIVIDUALINFOW info;
    int rc;
    int entries = 0;
    int exit_code = 1;

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathW(MAX_PATH, tmp);
    _snwprintf(scratch, MAX_PATH, L"%szip64j_unicode", tmp);
    _snwprintf(in_dir,  MAX_PATH, L"%s\\in",  scratch);
    _snwprintf(arc_dir, MAX_PATH, L"%s\\arc", scratch);
    _snwprintf(out_dir, MAX_PATH, L"%s\\out", scratch);
    _snwprintf(jp_path, MAX_PATH, L"%s\\%s", in_dir, kJpName);
    _snwprintf(zip_path, MAX_PATH, L"%s\\smoke6.zip", arc_dir);
    _snwprintf(extracted_path, MAX_PATH, L"%s\\%s", out_dir, kJpName);

    CreateDirectoryW(scratch, NULL);
    CreateDirectoryW(in_dir, NULL);
    CreateDirectoryW(arc_dir, NULL);
    CreateDirectoryW(out_dir, NULL);
    if (write_file_w(jp_path, "japanese-content\n") != 0) {
        printf("[FAIL] write jp file\n"); return 1;
    }
    DeleteFileW(zip_path);
    DeleteFileW(extracted_path);

    /* ---- 1. Zip via ZipW ------------------------------------------- */
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    ZipW_ = (FnZipW)GetProcAddress(hz, "ZipW");
    if (!ZipW_) { printf("[FAIL] GetProcAddress ZipW\n"); goto cleanup; }

    SetCurrentDirectoryW(in_dir);
    _snwprintf(cmdline, 2048, L"\"%s\" \"%s\"", zip_path, kJpName);
    wout[0] = L'\0';
    rc = ZipW_(NULL, cmdline, wout, sizeof(wout) / sizeof(wout[0]));
    wprintf(L"       ZipW() -> rc=%d\n       output(W): %.200ls\n", rc, wout);
    if (rc != 0) { printf("[FAIL] ZipW rc=%d\n", rc); goto cleanup; }
    FreeLibrary(hz); hz = NULL;

    /* ---- 2. Resolve unzip64 W symbols ------------------------------ */
    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }

    UnZipW_       = (FnUnZipW)      GetProcAddress(hu, "UnZipW");
    OpenW_        = (FnOpenW)       GetProcAddress(hu, "UnZipOpenArchiveW");
    Close_        = (FnClose)       GetProcAddress(hu, "UnZipCloseArchive");
    FindFirstW_   = (FnFindFirstW)  GetProcAddress(hu, "UnZipFindFirstW");
    FindNextW_    = (FnFindNextW)   GetProcAddress(hu, "UnZipFindNextW");
    GetFileNameW_ = (FnGetFileNameW)GetProcAddress(hu, "UnZipGetFileNameW");

    if (!UnZipW_ || !OpenW_ || !Close_ || !FindFirstW_ || !FindNextW_ || !GetFileNameW_) {
        printf("[FAIL] missing W export(s)\n"); goto cleanup;
    }

    /* ---- 3. Enumerate via W --------------------------------------- */
    harc = OpenW_(NULL, zip_path, 0);
    if (!harc) { printf("[FAIL] UnZipOpenArchiveW NULL\n"); goto cleanup; }
    wprintf(L"       UnZipOpenArchiveW(\"%s\") -> %p\n", zip_path, harc);

    memset(&info, 0, sizeof(info));
    rc = FindFirstW_(harc, L"*", &info);
    while (rc == 0) {
        WCHAR namebuf[FNAME_MAX32 + 1] = {0};
        GetFileNameW_(harc, namebuf, FNAME_MAX32);
        wprintf(L"       entry(W): info.szFileName=\"%ls\" GetNameW=\"%ls\" attr=\"%ls\" mode=\"%ls\"\n",
                info.szFileName, namebuf, info.szAttribute, info.szMode);
        if (wcscmp(info.szFileName, kJpName) != 0) {
            wprintf(L"[FAIL] info.szFileName != expected \"%ls\"\n", kJpName);
            goto cleanup;
        }
        if (wcscmp(namebuf, kJpName) != 0) {
            wprintf(L"[FAIL] GetFileNameW result != expected\n");
            goto cleanup;
        }
        entries++;
        rc = FindNextW_(harc, &info);
    }
    if (entries != 1) { printf("[FAIL] entries=%d (want 1)\n", entries); goto cleanup; }

    rc = Close_(harc); harc = NULL;
    if (rc != 0) { printf("[FAIL] Close rc=%d\n", rc); goto cleanup; }

    /* ---- 4. Extract via UnZipW ----------------------------------- */
    _snwprintf(cmdline, 2048, L"-o \"%s\" -d \"%s\"", zip_path, out_dir);
    wout[0] = L'\0';
    rc = UnZipW_(NULL, cmdline, wout, sizeof(wout) / sizeof(wout[0]));
    wprintf(L"       UnZipW() -> rc=%d\n       output(W): %.200ls\n", rc, wout);
    if (rc != 0) { printf("[FAIL] UnZipW rc=%d\n", rc); goto cleanup; }

    /* Verify the extracted file exists and contents match. */
    {
        FILE *fp = _wfopen(extracted_path, L"rb");
        char buf[64];
        size_t n;
        if (!fp) { wprintf(L"[FAIL] extracted file missing: %s\n", extracted_path); goto cleanup; }
        n = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        buf[n] = '\0';
        if (strcmp(buf, "japanese-content\n") != 0) {
            printf("[FAIL] extracted content mismatch: %s\n", buf); goto cleanup;
        }
    }

    printf("\n[PASS] Unicode W roundtrip\n");
    exit_code = 0;

cleanup:
    if (harc && Close_) Close_(harc);
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
