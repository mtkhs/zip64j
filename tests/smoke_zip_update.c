/* Operations that read an existing archive back and rewrite it.
 *
 *   -d   delete a named entry
 *   -u   update: add new files, refresh ones that changed
 *   -f   freshen: refresh entries already present, add nothing
 *   -g   grow: append to the existing archive
 *
 * All four go through zip30's central-directory reader, so they share a
 * failure mode: if the reader cannot walk the CD, each reports
 * "expected N entries but found 0" and returns ERROR_HEADER_BROKEN.
 *
 * Entry counts are read back through unzip64's UnZipGetFileCount.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FnZip)(HWND, LPCSTR, LPSTR, DWORD);
typedef int (WINAPI *FnGetFileCount)(LPCSTR);
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

static int read_all(const char *path, char *buf, size_t bufsz)
{
    FILE  *fp = fopen(path, "rb");
    size_t n;
    if (!fp) return -1;
    n = fread(buf, 1, bufsz - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return 0;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    char in_slash[MAX_PATH], out_slash[MAX_PATH], zip_path[MAX_PATH];
    char fa[MAX_PATH], fb[MAX_PATH], fc[MAX_PATH], ext_a[MAX_PATH];
    char abs_zip64j[MAX_PATH], abs_unzip64[MAX_PATH];
    char cmdline[4096], output[4096], content[256];
    HMODULE hz = NULL, hu = NULL;
    FnZip Zip_ = NULL;
    FnUnZip UnZip_ = NULL;
    FnGetFileCount GetFileCount_ = NULL;
    int failures = 0;
    int exit_code = 1;

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch,   MAX_PATH, "%szip64j_zip_update", tmp);
    _snprintf(in_dir,    MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir,   MAX_PATH, "%s\\arc", scratch);
    _snprintf(out_dir,   MAX_PATH, "%s\\out", scratch);
    _snprintf(in_slash,  MAX_PATH, "%s\\", in_dir);
    _snprintf(out_slash, MAX_PATH, "%s\\", out_dir);
    _snprintf(zip_path,  MAX_PATH, "%s\\upd.zip", arc_dir);
    _snprintf(fa, MAX_PATH, "%s\\alpha.txt", in_dir);
    _snprintf(fb, MAX_PATH, "%s\\beta.txt",  in_dir);
    _snprintf(fc, MAX_PATH, "%s\\gamma.txt", in_dir);
    _snprintf(ext_a, MAX_PATH, "%s\\alpha.txt", out_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(out_dir, NULL);

    if (write_file(fa, "alpha-v1\n") != 0 || write_file(fb, "bravo\n") != 0) {
        printf("[FAIL] write sources\n");
        return 1;
    }

    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }
    GetFileCount_ = (FnGetFileCount)GetProcAddress(hu, "UnZipGetFileCount");
    UnZip_ = (FnUnZip)GetProcAddress(hu, "UnZip");
    if (!GetFileCount_ || !UnZip_) { printf("[FAIL] GetProcAddress unzip64\n"); goto cleanup; }

    /* ---- base archive: alpha.txt + beta.txt ---- */
    DeleteFileA(zip_path);
    _snprintf(cmdline, sizeof(cmdline),
              "\"%s\" \"%s\" alpha.txt beta.txt", zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : -1;
        printf("       build            rc=%d entries=%d\n", rc, n);
        if (rc != 0 || n != 2) {
            printf("[FAIL] base archive not built (output: %s)\n", output);
            failures++;
            goto verdict;
        }
    }

    /* ---- -d: drop beta.txt ---- */
    _snprintf(cmdline, sizeof(cmdline), "-d \"%s\" beta.txt", zip_path);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : -1;
        printf("       -d beta.txt      rc=%d entries=%d\n", rc, n);
        if (rc != 0 || n != 1) {
            printf("[FAIL] -d should leave 1 entry (output: %s)\n", output);
            failures++;
        }
    }

    /* ---- -g: append gamma.txt to the existing archive ---- */
    if (write_file(fc, "gamma\n") != 0) { printf("[FAIL] write gamma\n"); goto verdict; }
    _snprintf(cmdline, sizeof(cmdline),
              "-g \"%s\" \"%s\" gamma.txt", zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : -1;
        printf("       -g gamma.txt     rc=%d entries=%d\n", rc, n);
        if (rc != 0 || n != 2) {
            printf("[FAIL] -g should bring the archive to 2 entries (output: %s)\n", output);
            failures++;
        }
    }

    /* ---- -u: alpha.txt changed, beta.txt is new to this archive ---- */
    Sleep(2000);   /* DOS timestamps have 2-second resolution */
    if (write_file(fa, "alpha-v2\n") != 0) { printf("[FAIL] rewrite alpha\n"); goto verdict; }
    _snprintf(cmdline, sizeof(cmdline),
              "-u \"%s\" \"%s\" alpha.txt beta.txt", zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : -1;
        printf("       -u               rc=%d entries=%d\n", rc, n);
        if (rc != 0 || n != 3) {
            printf("[FAIL] -u should give 3 entries (output: %s)\n", output);
            failures++;
        }
    }

    /* the updated alpha.txt must be the one that comes back out */
    DeleteFileA(ext_a);
    _snprintf(cmdline, sizeof(cmdline), "-x -o \"%s\" \"%s\"", zip_path, out_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        content[0] = '\0';
        if (file_exists(ext_a)) read_all(ext_a, content, sizeof(content));
        printf("       extract alpha    rc=%d content=\"%s\"\n",
               rc, strcmp(content, "alpha-v2\n") == 0 ? "alpha-v2" : content);
        if (strcmp(content, "alpha-v2\n") != 0) {
            printf("[FAIL] -u did not refresh alpha.txt\n");
            failures++;
        }
    }

    /* ---- -f: refresh only, gamma.txt stays out ---- */
    Sleep(2000);
    if (write_file(fa, "alpha-v3\n") != 0) { printf("[FAIL] rewrite alpha\n"); goto verdict; }
    _snprintf(cmdline, sizeof(cmdline),
              "-f \"%s\" \"%s\" alpha.txt", zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : -1;
        printf("       -f alpha.txt     rc=%d entries=%d\n", rc, n);
        if (rc != 0 || n != 3) {
            printf("[FAIL] -f should keep 3 entries (output: %s)\n", output);
            failures++;
        }
    }

    DeleteFileA(ext_a);
    _snprintf(cmdline, sizeof(cmdline), "-x -o \"%s\" \"%s\"", zip_path, out_slash);
    output[0] = '\0';
    {
        UnZip_(NULL, cmdline, output, sizeof(output));
        content[0] = '\0';
        if (file_exists(ext_a)) read_all(ext_a, content, sizeof(content));
        printf("       extract alpha    content=\"%s\"\n",
               strcmp(content, "alpha-v3\n") == 0 ? "alpha-v3" : content);
        if (strcmp(content, "alpha-v3\n") != 0) {
            printf("[FAIL] -f did not refresh alpha.txt\n");
            failures++;
        }
    }

verdict:
    if (failures == 0) {
        printf("\n[PASS] archive update ops (-d -g -u -f)\n");
        exit_code = 0;
    } else {
        printf("\n[FAIL] archive update ops: %d case(s) failed\n", failures);
    }

cleanup:
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
