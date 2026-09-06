/* Case-insensitive match: `-C` case-insensitive match.
 *
 * Builds an archive containing "alpha.txt" (lowercase in the entry) and
 * "Beta.txt" (mixed case), then extracts with an uppercase include filter:
 *
 *   -x arc.zip "ALPHA.TXT" dir\              → no match, no file extracted
 *   -x -C arc.zip "ALPHA.TXT" dir\           → match, alpha.txt extracted
 *   -x -C arc.zip "BETA.TXT"  dir\           → match, Beta.txt extracted
 *
 * Relies on Info-ZIP unzip60's DCL_60.C_flag being honoured (case-insensitive
 * entry match when set).
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FnZip)  (HWND, LPCSTR, LPSTR, DWORD);
typedef int (WINAPI *FnUnZip)(HWND, LPCSTR, LPSTR, DWORD);

static int write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    return 0;
}

static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    char out_dir_slash[MAX_PATH];
    char zip_path[MAX_PATH];
    char src_a[MAX_PATH], src_b[MAX_PATH];
    char ext_a[MAX_PATH], ext_b[MAX_PATH];
    char cmdline[2048], output[4096];
    char abs_zip64j[MAX_PATH], abs_unzip64[MAX_PATH];
    HMODULE hz = NULL, hu = NULL;
    FnZip   Zip_   = NULL;
    FnUnZip UnZip_ = NULL;
    int failures = 0;
    int exit_code = 1;

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch,  MAX_PATH, "%szip64j_unzip_case_match", tmp);
    _snprintf(in_dir,   MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir,  MAX_PATH, "%s\\arc", scratch);
    _snprintf(out_dir,  MAX_PATH, "%s\\out", scratch);
    _snprintf(out_dir_slash, MAX_PATH, "%s\\", out_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\p12.zip", arc_dir);
    _snprintf(src_a,    MAX_PATH, "%s\\alpha.txt", in_dir);
    _snprintf(src_b,    MAX_PATH, "%s\\Beta.txt",  in_dir);
    _snprintf(ext_a,    MAX_PATH, "%s\\alpha.txt", out_dir);
    _snprintf(ext_b,    MAX_PATH, "%s\\Beta.txt",  out_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(out_dir, NULL);
    DeleteFileA(zip_path);

    if (write_file(src_a, "a") != 0 ||
        write_file(src_b, "b") != 0) {
        printf("[FAIL] write source\n"); return 1;
    }

    /* ---- 1. Build archive (preserves original case) ---- */
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline), "-r \"%s\" alpha.txt Beta.txt", zip_path);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        if (rc != 0) { printf("[FAIL] Zip build rc=%d\n", rc); goto cleanup; }
    }
    FreeLibrary(hz); hz = NULL;

    /* ---- 2. Load unzip64 ---- */
    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }
    UnZip_ = (FnUnZip)GetProcAddress(hu, "UnZip");
    if (!UnZip_) { printf("[FAIL] GetProcAddress UnZip\n"); goto cleanup; }

    /* ---- 3. Without -C: uppercase include filter misses lowercase entry ---- */
    DeleteFileA(ext_a); DeleteFileA(ext_b);
    _snprintf(cmdline, sizeof(cmdline),
              "-x -o \"%s\" \"%s\" ALPHA.TXT", zip_path, out_dir_slash);
    output[0] = '\0';
    UnZip_(NULL, cmdline, output, sizeof(output));
    if (file_exists(ext_a)) {
        printf("[FAIL] without -C: ALPHA.TXT should NOT match alpha.txt\n");
        failures++;
    } else {
        printf("       no -C ALPHA.TXT               not-extracted (expected)\n");
    }

    /* ---- 4. With -C: uppercase filter matches lowercase entry ---- */
    DeleteFileA(ext_a); DeleteFileA(ext_b);
    _snprintf(cmdline, sizeof(cmdline),
              "-x -o -C \"%s\" \"%s\" ALPHA.TXT", zip_path, out_dir_slash);
    output[0] = '\0';
    UnZip_(NULL, cmdline, output, sizeof(output));
    if (!file_exists(ext_a)) {
        printf("[FAIL] with -C: ALPHA.TXT should match alpha.txt\n");
        failures++;
    } else {
        printf("       -C ALPHA.TXT                  extracted (expected)\n");
    }

    /* ---- 5. -C with BETA.TXT matches Beta.txt ---- */
    DeleteFileA(ext_a); DeleteFileA(ext_b);
    _snprintf(cmdline, sizeof(cmdline),
              "-x -o -C \"%s\" \"%s\" BETA.TXT", zip_path, out_dir_slash);
    output[0] = '\0';
    UnZip_(NULL, cmdline, output, sizeof(output));
    if (!file_exists(ext_b)) {
        printf("[FAIL] with -C: BETA.TXT should match Beta.txt\n");
        failures++;
    } else {
        printf("       -C BETA.TXT                   extracted (expected)\n");
    }

    /* ---- 6. -C --C (negate): no match expected ---- */
    DeleteFileA(ext_a); DeleteFileA(ext_b);
    _snprintf(cmdline, sizeof(cmdline),
              "-x -o -C --C \"%s\" \"%s\" ALPHA.TXT", zip_path, out_dir_slash);
    output[0] = '\0';
    UnZip_(NULL, cmdline, output, sizeof(output));
    if (file_exists(ext_a)) {
        printf("[FAIL] -C then --C: should revert to case-sensitive\n");
        failures++;
    } else {
        printf("       -C --C (negate) ALPHA.TXT     not-extracted (expected)\n");
    }

    if (failures == 0) {
        printf("\n[PASS] -C case-insensitive\n");
        exit_code = 0;
    } else {
        printf("\n[FAIL] %d case(s) failed\n", failures);
    }

cleanup:
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
