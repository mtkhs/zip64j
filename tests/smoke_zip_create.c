/* Archive creation: calls Zip() via zip64j.dll and verify a real .zip file
 * is produced on disk by the underlying zip64.dll.
 *
 * Layout of scratch dir under %TEMP%\zip64j_zip_create\:
 *   in\hello.txt       - source file to be zipped
 *   in\sub\world.txt   - second source file (exercises recursion via -r)
 *   out\smoke2.zip     - archive produced by Zip()
 *
 * Success criteria:
 *   1. Zip() returns 0 (ZE_OK).
 *   2. out\smoke2.zip exists.
 *   3. First 4 bytes are "PK\x03\x04" (local file header signature).
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FnZip)(HWND, LPCSTR, LPSTR, DWORD);

static void make_dir(const char *path)
{
    CreateDirectoryA(path, NULL);
}

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

static int check_zip_magic(const char *path)
{
    FILE *fp = fopen(path, "rb");
    unsigned char buf[4] = { 0 };
    size_t n;
    if (!fp) return 0;
    n = fread(buf, 1, 4, fp);
    fclose(fp);
    return (n == 4 && buf[0] == 'P' && buf[1] == 'K' &&
            buf[2] == 0x03 && buf[3] == 0x04);
}

int main(void)
{
    char tmp[MAX_PATH];
    char scratch[MAX_PATH];
    char in_dir[MAX_PATH], out_dir[MAX_PATH];
    char f1[MAX_PATH], f2_dir[MAX_PATH], f2[MAX_PATH];
    char zip_path[MAX_PATH];
    char cmdline[2048];
    char output[4096];
    HMODULE h;
    FnZip Zip_;
    int rc;

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch, MAX_PATH, "%szip64j_zip_create", tmp);
    _snprintf(in_dir,  MAX_PATH, "%s\\in",  scratch);
    _snprintf(out_dir, MAX_PATH, "%s\\out", scratch);
    _snprintf(f2_dir,  MAX_PATH, "%s\\sub", in_dir);
    _snprintf(f1,  MAX_PATH, "%s\\hello.txt",     in_dir);
    _snprintf(f2,  MAX_PATH, "%s\\world.txt",     f2_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\smoke2.zip", out_dir);

    make_dir(scratch);
    make_dir(in_dir);
    make_dir(out_dir);
    make_dir(f2_dir);

    if (write_file(f1, "hello from zip64j\n") != 0) {
        printf("[FAIL] write_file(%s)\n", f1); return 1;
    }
    if (write_file(f2, "nested file content\n") != 0) {
        printf("[FAIL] write_file(%s)\n", f2); return 1;
    }
    DeleteFileA(zip_path);

    h = LoadLibraryA("build/dist/zip64j.dll");
    if (!h) { printf("[FAIL] LoadLibrary err=%lu\n", GetLastError()); return 1; }
    Zip_ = (FnZip)GetProcAddress(h, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress(Zip)\n"); return 1; }

    /* -r recurses; in\ is the root dir (trailing backslash triggers rootdir
     * detection in parse_cmdline). */
    _snprintf(cmdline, sizeof(cmdline),
              "-r \"%s\" \"%s\\\" *.txt", zip_path, in_dir);
    printf("       cmdline: %s\n", cmdline);

    output[0] = '\0';
    rc = Zip_(NULL, cmdline, output, sizeof(output));
    printf("       Zip() -> rc=%d\n", rc);
    if (output[0]) printf("       output: %s\n", output);

    if (rc != 0) { printf("[FAIL] Zip returned non-zero\n"); FreeLibrary(h); return 1; }
    if (!file_exists(zip_path)) {
        printf("[FAIL] archive not created: %s\n", zip_path);
        FreeLibrary(h); return 1;
    }
    if (!check_zip_magic(zip_path)) {
        printf("[FAIL] %s has no PK\\x03\\x04 signature\n", zip_path);
        FreeLibrary(h); return 1;
    }

    FreeLibrary(h);
    printf("\n[PASS] archive at %s\n", zip_path);
    return 0;
}
