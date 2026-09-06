/* Zip-side options ported from zip32j.
 *
 *   -t <date>   include only files at or after the date
 *   -tt <date>  include only files before the date
 *   -b <dir>    use the given directory for zip's temporary file
 *   -J          accepted (strips an SFX prefix; nothing to strip here)
 *
 * Entry counts are read back through unzip64's UnZipGetFileCount.
 * -d lives in smoke_zip_update with the other read-back operations.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FnZip)(HWND, LPCSTR, LPSTR, DWORD);
typedef int (WINAPI *FnGetFileCount)(LPCSTR);

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
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], tmp_dir[MAX_PATH];
    char in_slash[MAX_PATH], zip_path[MAX_PATH];
    char fa[MAX_PATH], fb[MAX_PATH];
    char abs_zip64j[MAX_PATH], abs_unzip64[MAX_PATH];
    char cmdline[4096], output[4096];
    HMODULE hz = NULL, hu = NULL;
    FnZip Zip_ = NULL;
    FnGetFileCount GetFileCount_ = NULL;
    int failures = 0;
    int exit_code = 1;

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch,  MAX_PATH, "%szip64j_zip_options", tmp);
    _snprintf(in_dir,   MAX_PATH, "%s\\in",   scratch);
    _snprintf(arc_dir,  MAX_PATH, "%s\\arc",  scratch);
    _snprintf(tmp_dir,  MAX_PATH, "%s\\zt",   scratch);
    _snprintf(in_slash, MAX_PATH, "%s\\",     in_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\opt.zip", arc_dir);
    _snprintf(fa, MAX_PATH, "%s\\alpha.txt", in_dir);
    _snprintf(fb, MAX_PATH, "%s\\beta.txt",  in_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(tmp_dir, NULL);

    if (write_file(fa, "alpha\n") != 0 || write_file(fb, "bravo\n") != 0) {
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
    if (!GetFileCount_) { printf("[FAIL] GetProcAddress UnZipGetFileCount\n"); goto cleanup; }

    /* ---- -b: temporary directory ---- */
    DeleteFileA(zip_path);
    _snprintf(cmdline, sizeof(cmdline),
              "-b \"%s\" \"%s\" \"%s\" alpha.txt beta.txt",
              tmp_dir, zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : -1;
        printf("       -b <dir>         rc=%d entries=%d\n", rc, n);
        if (rc != 0 || n != 2) {
            printf("[FAIL] -b should build a 2-entry archive (output: %s)\n", output);
            failures++;
        }
    }

    /* ---- -t: only files at or after the date ----
     * The sources were written moments ago, so a future date must exclude
     * both and a past date must include both. */
    DeleteFileA(zip_path);
    _snprintf(cmdline, sizeof(cmdline),
              "-t 12312099 \"%s\" \"%s\" alpha.txt beta.txt", zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : 0;
        printf("       -t 12312099      rc=%d entries=%d\n", rc, n);
        if (n != 0) {
            printf("[FAIL] -t with a future date should select nothing\n");
            failures++;
        }
    }

    DeleteFileA(zip_path);
    _snprintf(cmdline, sizeof(cmdline),
              "-t 01011990 \"%s\" \"%s\" alpha.txt beta.txt", zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : -1;
        printf("       -t 01011990      rc=%d entries=%d\n", rc, n);
        if (rc != 0 || n != 2) {
            printf("[FAIL] -t with a past date should select both (output: %s)\n", output);
            failures++;
        }
    }

    /* ---- -tt: only files before the date ---- */
    DeleteFileA(zip_path);
    _snprintf(cmdline, sizeof(cmdline),
              "-tt 01011990 \"%s\" \"%s\" alpha.txt beta.txt", zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : 0;
        printf("       -tt 01011990     rc=%d entries=%d\n", rc, n);
        if (n != 0) {
            printf("[FAIL] -tt with a past date should select nothing\n");
            failures++;
        }
    }

    /* ---- -J: accepted and does not disturb a normal build ---- */
    DeleteFileA(zip_path);
    _snprintf(cmdline, sizeof(cmdline),
              "-J \"%s\" \"%s\" alpha.txt beta.txt", zip_path, in_slash);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        int n  = file_exists(zip_path) ? GetFileCount_(zip_path) : -1;
        printf("       -J               rc=%d entries=%d\n", rc, n);
        if (rc != 0 || n != 2) {
            printf("[FAIL] -J should be accepted (output: %s)\n", output);
            failures++;
        }
    }

    if (failures == 0) {
        printf("\n[PASS] zip options (-J -t -tt -b)\n");
        exit_code = 0;
    } else {
        printf("\n[FAIL] zip options: %d case(s) failed\n", failures);
    }

cleanup:
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
