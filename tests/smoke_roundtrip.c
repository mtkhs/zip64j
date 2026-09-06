/* Roundtrip: creates an archive via zip64j.dll, extract it via
 * unzip64.dll, and verify the extracted files match the originals.
 *
 * Layout under %TEMP%\zip64j_roundtrip\:
 *   in\a.txt           - source file (content "alpha\n")
 *   in\sub\b.txt       - nested source file (content "bravo\n")
 *   arc\smoke3.zip     - archive
 *   out\a.txt          - extracted file
 *   out\sub\b.txt      - extracted file
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FnZip)  (HWND, LPCSTR, LPSTR, DWORD);
typedef int (WINAPI *FnUnZip)(HWND, LPCSTR, LPSTR, DWORD);

static void make_dir(const char *path) { CreateDirectoryA(path, NULL); }

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

static int read_file(const char *path, char *buf, size_t buflen)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp) return -1;
    n = fread(buf, 1, buflen - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return (int)n;
}

int main(void)
{
    char tmp[MAX_PATH];
    char scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    char sub_dir[MAX_PATH];
    char fa[MAX_PATH], fb[MAX_PATH];
    char zip_path[MAX_PATH];
    char out_a[MAX_PATH], out_b[MAX_PATH];
    char cmdline[2048];
    char output[4096];
    char buf[64];
    char abs_zip64j[MAX_PATH];
    char abs_unzip64[MAX_PATH];
    HMODULE hz, hu;
    FnZip Zip_;
    FnUnZip UnZip_;
    int rc;

    /* Resolve DLL paths relative to CWD up front; SetCurrentDirectory()
     * during the test would otherwise break the lookup. */
    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch, MAX_PATH, "%szip64j_roundtrip", tmp);
    _snprintf(in_dir,  MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir, MAX_PATH, "%s\\arc", scratch);
    _snprintf(out_dir, MAX_PATH, "%s\\out", scratch);
    _snprintf(sub_dir, MAX_PATH, "%s\\sub", in_dir);
    _snprintf(fa, MAX_PATH, "%s\\a.txt",     in_dir);
    _snprintf(fb, MAX_PATH, "%s\\b.txt",     sub_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\smoke3.zip", arc_dir);
    _snprintf(out_a, MAX_PATH, "%s\\a.txt",     out_dir);
    _snprintf(out_b, MAX_PATH, "%s\\sub\\b.txt", out_dir);

    make_dir(scratch);
    make_dir(in_dir);
    make_dir(arc_dir);
    make_dir(out_dir);
    make_dir(sub_dir);

    if (write_file(fa, "alpha\n") != 0) { printf("[FAIL] write %s\n", fa); return 1; }
    if (write_file(fb, "bravo\n") != 0) { printf("[FAIL] write %s\n", fb); return 1; }
    DeleteFileA(zip_path);
    DeleteFileA(out_a);
    DeleteFileA(out_b);

    /* ---- 1. Zip the files ----
     * chdir to in_dir so entries are stored with relative paths. */
    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); return 1; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); return 1; }

    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline),
              "-r \"%s\" a.txt sub\\b.txt", zip_path);
    printf("       zip cmdline (cwd=%s): %s\n", in_dir, cmdline);
    output[0] = '\0';
    rc = Zip_(NULL, cmdline, output, sizeof(output));
    printf("       Zip() -> rc=%d\n", rc);
    if (output[0]) printf("       output: %s\n", output);
    FreeLibrary(hz);

    if (rc != 0) { printf("[FAIL] Zip failed\n"); return 1; }
    if (!file_exists(zip_path)) { printf("[FAIL] archive not created\n"); return 1; }

    /* ---- 2. UnZip the archive ---- */
    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); return 1; }
    UnZip_ = (FnUnZip)GetProcAddress(hu, "UnZip");
    if (!UnZip_) { printf("[FAIL] GetProcAddress UnZip\n"); return 1; }

    _snprintf(cmdline, sizeof(cmdline),
              "-o \"%s\" -d \"%s\"", zip_path, out_dir);
    printf("       unzip cmdline: %s\n", cmdline);
    output[0] = '\0';
    rc = UnZip_(NULL, cmdline, output, sizeof(output));
    printf("       UnZip() -> rc=%d\n", rc);
    if (output[0]) printf("       output: %s\n", output);
    FreeLibrary(hu);

    if (rc != 0) { printf("[FAIL] UnZip failed\n"); return 1; }

    /* ---- 3. Verify extracted content ---- */
    if (!file_exists(out_a)) { printf("[FAIL] %s not extracted\n", out_a); return 1; }
    if (!file_exists(out_b)) { printf("[FAIL] %s not extracted\n", out_b); return 1; }

    memset(buf, 0, sizeof(buf));
    if (read_file(out_a, buf, sizeof(buf)) <= 0 || strcmp(buf, "alpha\n") != 0) {
        printf("[FAIL] %s content mismatch: [%s]\n", out_a, buf); return 1;
    }
    memset(buf, 0, sizeof(buf));
    if (read_file(out_b, buf, sizeof(buf)) <= 0 || strcmp(buf, "bravo\n") != 0) {
        printf("[FAIL] %s content mismatch: [%s]\n", out_b, buf); return 1;
    }

    printf("\n[PASS] zip64j + unzip64 roundtrip\n");
    return 0;
}
