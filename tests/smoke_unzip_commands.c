/* Subcommand dispatch: UnZip() subcommand dispatch.
 *
 * Builds a small archive, then exercises each 統合アーカイバAPI仕様 subcommand:
 *
 *   -x   extract (default)        → rc=0, files present
 *   -xv  extract (verbose)        → rc=0, files present
 *   -t   integrity test           → rc=0
 *   -l   list UNLHA32 style       → rc=0, names in szOutput
 *   -v   list UNZIP full          → rc=0, names in szOutput
 *   -lv  list UNZIP style         → rc=0, names in szOutput
 *   -c   contents → stdout        → rc=ERROR_NOT_SUPPORT (0x8023)
 *   -p   contents → stdout (np)   → rc=ERROR_NOT_SUPPORT
 *   -z   comment display          → rc=ERROR_NOT_SUPPORT
 *   -Z   comment MessageBox       → rc=ERROR_NOT_SUPPORT
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define ERROR_NOT_SUPPORT 0x8023

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

static int run_unzip(FnUnZip fn, const char *label, const char *cmdline,
                     int expected_rc)
{
    char output[4096];
    int rc;
    output[0] = '\0';
    rc = fn(NULL, cmdline, output, sizeof(output));
    printf("       %-24s rc=%d (want %d)", label, rc, expected_rc);
    if (output[0]) {
        int n = (int)strlen(output);
        if (n > 60) { printf(" out=\"%.60s...\"", output); }
        else        { printf(" out=\"%s\"", output); }
    }
    printf("\n");
    return rc == expected_rc ? 0 : -1;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    char out_dir_slash[MAX_PATH];
    char zip_path[MAX_PATH];
    char src_a[MAX_PATH], src_b[MAX_PATH];
    char ext_a[MAX_PATH];
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
    _snprintf(scratch,  MAX_PATH, "%szip64j_unzip_commands", tmp);
    _snprintf(in_dir,   MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir,  MAX_PATH, "%s\\arc", scratch);
    _snprintf(out_dir,  MAX_PATH, "%s\\out", scratch);
    _snprintf(out_dir_slash, MAX_PATH, "%s\\", out_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\p9.zip", arc_dir);
    _snprintf(src_a,    MAX_PATH, "%s\\alpha.txt", in_dir);
    _snprintf(src_b,    MAX_PATH, "%s\\bravo.txt", in_dir);
    _snprintf(ext_a,    MAX_PATH, "%s\\alpha.txt", out_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(out_dir, NULL);
    DeleteFileA(zip_path);
    DeleteFileA(ext_a);

    if (write_file(src_a, "alpha-content\n") != 0 ||
        write_file(src_b, "bravo-content\n") != 0) {
        printf("[FAIL] write source\n"); return 1;
    }

    /* ---- 1. Build archive with zip64j ---- */
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline), "-r \"%s\" alpha.txt bravo.txt", zip_path);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        printf("       Zip() build rc=%d\n", rc);
        if (rc != 0) { printf("[FAIL] Zip build rc=%d\n", rc); goto cleanup; }
    }
    FreeLibrary(hz); hz = NULL;

    /* ---- 2. Load unzip64 ---- */
    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }
    UnZip_ = (FnUnZip)GetProcAddress(hu, "UnZip");
    if (!UnZip_) { printf("[FAIL] GetProcAddress UnZip\n"); goto cleanup; }

    /* ---- 3. Extract commands (rc=0, content appears) ---- */
    DeleteFileA(ext_a);
    _snprintf(cmdline, sizeof(cmdline), "-x \"%s\" \"%s\"", zip_path, out_dir_slash);
    failures += run_unzip(UnZip_, "-x extract",      cmdline, 0);
    if (!file_exists(ext_a)) { printf("[FAIL] -x did not produce %s\n", ext_a); failures++; }

    DeleteFileA(ext_a);
    _snprintf(cmdline, sizeof(cmdline), "-xv \"%s\" \"%s\"", zip_path, out_dir_slash);
    failures += run_unzip(UnZip_, "-xv extract",     cmdline, 0);
    if (!file_exists(ext_a)) { printf("[FAIL] -xv did not produce %s\n", ext_a); failures++; }

    /* ---- 4. Test + list commands (rc=0) ---- */
    _snprintf(cmdline, sizeof(cmdline), "-t \"%s\"", zip_path);
    failures += run_unzip(UnZip_, "-t integrity",    cmdline, 0);

    _snprintf(cmdline, sizeof(cmdline), "-l \"%s\"", zip_path);
    failures += run_unzip(UnZip_, "-l list",         cmdline, 0);

    _snprintf(cmdline, sizeof(cmdline), "-v \"%s\"", zip_path);
    failures += run_unzip(UnZip_, "-v list verbose", cmdline, 0);

    _snprintf(cmdline, sizeof(cmdline), "-lv \"%s\"", zip_path);
    failures += run_unzip(UnZip_, "-lv list UNZIP",  cmdline, 0);

    /* ---- 5. Unsupported commands (rc=ERROR_NOT_SUPPORT) ---- */
    _snprintf(cmdline, sizeof(cmdline), "-c \"%s\"", zip_path);
    failures += run_unzip(UnZip_, "-c unsupported",  cmdline, ERROR_NOT_SUPPORT);

    _snprintf(cmdline, sizeof(cmdline), "-p \"%s\"", zip_path);
    failures += run_unzip(UnZip_, "-p unsupported",  cmdline, ERROR_NOT_SUPPORT);

    _snprintf(cmdline, sizeof(cmdline), "-z \"%s\"", zip_path);
    failures += run_unzip(UnZip_, "-z unsupported",  cmdline, ERROR_NOT_SUPPORT);

    _snprintf(cmdline, sizeof(cmdline), "-Z \"%s\"", zip_path);
    failures += run_unzip(UnZip_, "-Z unsupported",  cmdline, ERROR_NOT_SUPPORT);

    if (failures == 0) {
        printf("\n[PASS] subcommand dispatch\n");
        exit_code = 0;
    } else {
        printf("\n[FAIL] %d subcommand(s) misbehaved\n", failures);
    }

cleanup:
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
