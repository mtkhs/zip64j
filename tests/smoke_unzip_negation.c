/* Option negation: `--X` option negation.
 *
 * Exercises `-o` vs `--o` using overwrite semantics:
 *
 *   no -o            → cb_replace returns CB_REPLACE_NO → preexisting file kept
 *   -o               → noflag=1 → unzip60 overwrites unconditionally
 *   -o --o (in last) → noflag reset to 0 → preexisting file kept
 *
 * Negation also tested with just `--o` alone (noflag stays 0).
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

static int read_file(const char *path, char *buf, size_t bufsz)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp) return -1;
    n = fread(buf, 1, bufsz - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    return (int)n;
}

static int expect_content(const char *label, const char *path, const char *want)
{
    char got[128] = {0};
    if (read_file(path, got, sizeof(got)) < 0) {
        printf("[FAIL] %s: cannot read %s\n", label, path);
        return 1;
    }
    if (strcmp(got, want) != 0) {
        printf("[FAIL] %s: got=\"%s\" want=\"%s\"\n", label, got, want);
        return 1;
    }
    printf("       %-30s OK (content=\"%s\")\n", label, got);
    return 0;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    char out_dir_slash[MAX_PATH];
    char zip_path[MAX_PATH];
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
    _snprintf(scratch,  MAX_PATH, "%szip64j_unzip_negation", tmp);
    _snprintf(in_dir,   MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir,  MAX_PATH, "%s\\arc", scratch);
    _snprintf(out_dir,  MAX_PATH, "%s\\out", scratch);
    _snprintf(out_dir_slash, MAX_PATH, "%s\\", out_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\p10.zip", arc_dir);
    _snprintf(src_a,    MAX_PATH, "%s\\alpha.txt", in_dir);
    _snprintf(ext_a,    MAX_PATH, "%s\\alpha.txt", out_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(out_dir, NULL);
    DeleteFileA(zip_path);

    if (write_file(src_a, "FROM_ZIP") != 0) {
        printf("[FAIL] write source\n"); return 1;
    }

    /* ---- 1. Build archive via zip64j ---- */
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline), "-r \"%s\" alpha.txt", zip_path);
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

    /* ---- 3. No -o : preexisting file kept ---- */
    write_file(ext_a, "PREEXISTING");
    _snprintf(cmdline, sizeof(cmdline), "-x \"%s\" \"%s\"", zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        printf("       -x       rc=%d\n", rc);
    }
    failures += expect_content("no -o (skip existing)", ext_a, "PREEXISTING");

    /* ---- 4. With -o : overwritten ---- */
    write_file(ext_a, "PREEXISTING");
    _snprintf(cmdline, sizeof(cmdline), "-x -o \"%s\" \"%s\"", zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        printf("       -x -o    rc=%d\n", rc);
    }
    failures += expect_content("-o (overwrite)", ext_a, "FROM_ZIP");

    /* ---- 5. -o --o : --o cancels -o, preexisting kept ---- */
    write_file(ext_a, "PREEXISTING");
    _snprintf(cmdline, sizeof(cmdline), "-x -o --o \"%s\" \"%s\"", zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        printf("       -x -o --o rc=%d\n", rc);
    }
    failures += expect_content("-o then --o (negate)", ext_a, "PREEXISTING");

    /* ---- 6. --o alone : noflag stays 0, preexisting kept ---- */
    write_file(ext_a, "PREEXISTING");
    _snprintf(cmdline, sizeof(cmdline), "-x --o \"%s\" \"%s\"", zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        printf("       -x --o   rc=%d\n", rc);
    }
    failures += expect_content("--o alone (no overwrite)", ext_a, "PREEXISTING");

    if (failures == 0) {
        printf("\n[PASS] --X negation\n");
        exit_code = 0;
    } else {
        printf("\n[FAIL] %d case(s) failed\n", failures);
    }

cleanup:
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
