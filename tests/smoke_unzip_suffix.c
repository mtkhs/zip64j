/* Archive-name suffix: `.ZIP` auto-append.
 *
 * unzip32 spec: if the given archive path does not name an existing file,
 * try appending `.ZIP` before failing. Windows filesystems are case-
 * insensitive, so the `.ZIP` vs `.zip` distinction only matters for the
 * auto-append logic itself (not for which casing is on disk).
 *
 *   base      (file=base.ZIP) -x base <dir>\    → rc=0, extracted
 *   base.ZIP  (exists)        -x base.ZIP <d>\  → rc=0, extracted (no rewrite)
 *   bogus     (no such file)  -x bogus <d>\     → rc != 0
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

static int copy_file(const char *src, const char *dst)
{
    return CopyFileA(src, dst, FALSE) ? 0 : -1;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    char out_dir_slash[MAX_PATH];
    char zip_real[MAX_PATH];           /* arc\base.ZIP */
    char zip_lc[MAX_PATH];             /* arc\base.zip */
    char zip_noext[MAX_PATH];          /* arc\base (no ext) */
    char zip_stem[MAX_PATH];           /* arc\base (to pass to cmdline) */
    char src_a[MAX_PATH], ext_a[MAX_PATH];
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
    _snprintf(scratch,  MAX_PATH, "%szip64j_unzip_suffix", tmp);
    _snprintf(in_dir,   MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir,  MAX_PATH, "%s\\arc", scratch);
    _snprintf(out_dir,  MAX_PATH, "%s\\out", scratch);
    _snprintf(out_dir_slash, MAX_PATH, "%s\\", out_dir);
    _snprintf(zip_real,   MAX_PATH, "%s\\base.ZIP", arc_dir);
    _snprintf(zip_lc,     MAX_PATH, "%s\\base.zip", arc_dir);
    _snprintf(zip_noext,  MAX_PATH, "%s\\base",     arc_dir);
    _snprintf(zip_stem,   MAX_PATH, "%s\\base",     arc_dir);
    _snprintf(src_a,      MAX_PATH, "%s\\alpha.txt", in_dir);
    _snprintf(ext_a,      MAX_PATH, "%s\\alpha.txt", out_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(out_dir, NULL);
    DeleteFileA(zip_real); DeleteFileA(zip_lc); DeleteFileA(zip_noext);

    if (write_file(src_a, "HELLO") != 0) {
        printf("[FAIL] write source\n"); return 1;
    }

    /* ---- 1. Build reference archive at arc\base.ZIP ---- */
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline), "-r \"%s\" alpha.txt", zip_real);
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

    /* ---- 3. Case: stem only, .ZIP variant exists ---- */
    DeleteFileA(ext_a);
    _snprintf(cmdline, sizeof(cmdline), "-x -o \"%s\" \"%s\"", zip_stem, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        int ok = (rc == 0) && file_exists(ext_a);
        printf("       stem→.ZIP    rc=%d extracted=%d %s\n",
               rc, file_exists(ext_a), ok ? "OK" : "FAIL");
        if (!ok) failures++;
    }

    /* ---- 4. Case: full name given directly, no auto-append needed ---- */
    DeleteFileA(ext_a);
    _snprintf(cmdline, sizeof(cmdline), "-x -o \"%s\" \"%s\"", zip_real, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        int ok = (rc == 0) && file_exists(ext_a);
        printf("       explicit.ZIP rc=%d extracted=%d %s\n",
               rc, file_exists(ext_a), ok ? "OK" : "FAIL");
        if (!ok) failures++;
    }

    /* ---- 5. Case: name that does not resolve to any file ---- */
    DeleteFileA(zip_real);
    DeleteFileA(ext_a);
    _snprintf(cmdline, sizeof(cmdline), "-x -o \"%s\" \"%s\"", zip_stem, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        int ok = (rc != 0) && !file_exists(ext_a);
        printf("       no-match     rc=%d extracted=%d %s\n",
               rc, file_exists(ext_a), ok ? "OK" : "FAIL");
        if (!ok) failures++;
    }

    if (failures == 0) {
        printf("\n[PASS] .ZIP auto-append\n");
        exit_code = 0;
    } else {
        printf("\n[FAIL] %d case(s) failed\n", failures);
    }

cleanup:
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
