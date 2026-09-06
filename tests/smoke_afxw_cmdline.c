/* afxw-style UnZip command line: exercises the unified-archiver (afxw) style UnZip
 * command line.
 *
 * Scenario mirrors the actual afxw capture from 2026-04-19 diagnostics:
 *
 *   -x -o --i "archive.zip" "dest\" "path/inside/archive"
 *
 * Where:
 *   -x          = extract subcommand (no-op for us)
 *   -o          = overwrite
 *   --i         = unknown flag, must be silently tolerated
 *   arg ending \= extract directory (heuristic, no -d prefix)
 *   last arg    = include pattern (single file inside the archive)
 *
 * Passing criteria: UnZip() returns 0 and the single requested file appears
 * in the destination directory.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FnZip)   (HWND, LPCSTR, LPSTR, DWORD);
typedef int (WINAPI *FnUnZip) (HWND, LPCSTR, LPSTR, DWORD);

static int write_file(LPCSTR path, const char *content)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    return 0;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    char out_dir_slash[MAX_PATH];
    char zip_path[MAX_PATH], readme_path[MAX_PATH];
    char cmdline[2048], output[4096];
    char abs_zip64j[MAX_PATH], abs_unzip64[MAX_PATH];
    char extracted_path[MAX_PATH];
    HMODULE hz = NULL, hu = NULL;
    FnZip   Zip_   = NULL;
    FnUnZip UnZip_ = NULL;
    int     rc;
    int     exit_code = 1;

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch,   MAX_PATH, "%szip64j_afxw_cmdline",        tmp);
    _snprintf(in_dir,    MAX_PATH, "%s\\in",                 scratch);
    _snprintf(arc_dir,   MAX_PATH, "%s\\arc",                scratch);
    _snprintf(out_dir,   MAX_PATH, "%s\\out",                scratch);
    _snprintf(out_dir_slash, MAX_PATH, "%s\\",               out_dir);
    _snprintf(zip_path,      MAX_PATH, "%s\\p7.zip",         arc_dir);
    _snprintf(readme_path,   MAX_PATH, "%s\\proj\\README.txt", in_dir);
    _snprintf(extracted_path, MAX_PATH, "%s\\proj\\README.txt", out_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    {
        char subdir[MAX_PATH];
        _snprintf(subdir, MAX_PATH, "%s\\proj", in_dir);
        CreateDirectoryA(subdir, NULL);
    }
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(out_dir, NULL);

    if (write_file(readme_path, "readme-body\n") != 0) {
        printf("[FAIL] write readme\n"); return 1;
    }
    DeleteFileA(zip_path);
    DeleteFileA(extracted_path);

    /* ---- 1. Build an archive with zip64j so UnZip has something to open. */
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline), "-r \"%s\" proj\\README.txt", zip_path);
    output[0] = '\0';
    rc = Zip_(NULL, cmdline, output, sizeof(output));
    printf("       Zip() rc=%d\n", rc);
    if (rc != 0) { printf("[FAIL] Zip rc=%d\n", rc); goto cleanup; }
    FreeLibrary(hz); hz = NULL;

    /* ---- 2. Extract via UnZip using the exact afxw cmdline shape. */
    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }
    UnZip_ = (FnUnZip)GetProcAddress(hu, "UnZip");
    if (!UnZip_) { printf("[FAIL] GetProcAddress UnZip\n"); goto cleanup; }

    _snprintf(cmdline, sizeof(cmdline),
              "-x -o --i \"%s\" \"%s\" \"proj/README.txt\"",
              zip_path, out_dir_slash);
    printf("       afxw-style cmdline: %s\n", cmdline);
    output[0] = '\0';
    rc = UnZip_(NULL, cmdline, output, sizeof(output));
    printf("       UnZip() rc=%d output=%.300s\n", rc, output);
    if (rc != 0) { printf("[FAIL] UnZip rc=%d\n", rc); goto cleanup; }

    /* ---- 3. Verify the extracted file exists & matches. */
    {
        FILE *fp = fopen(extracted_path, "rb");
        char buf[64];
        size_t n;
        if (!fp) { printf("[FAIL] extracted missing: %s\n", extracted_path); goto cleanup; }
        n = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        buf[n] = '\0';
        if (strcmp(buf, "readme-body\n") != 0) {
            printf("[FAIL] extracted content mismatch: \"%s\"\n", buf); goto cleanup;
        }
    }

    printf("\n[PASS] afxw-style UnZip cmdline\n");
    exit_code = 0;

cleanup:
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
