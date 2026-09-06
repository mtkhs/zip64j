/* Password on extract: `-P<password>`.
 *
 * Creates a ZipCrypto-encrypted archive via 7z.exe, then exercises unzip64's
 * UnZip() with three variants:
 *
 *   no -P           → cb_password returns IZ_PW_CANCEL → extract fails, file absent
 *   -P<wrong>       → CRC mismatch → extract fails, file absent
 *   -P<correct>     → decryption succeeds → file extracted with correct content
 *
 * Requires 7z.exe on PATH or at C:\Program Files\7-Zip\7z.exe. Skipped with
 * a warning if neither is present.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

static int find_7z(char *out, size_t outsz)
{
    const char *candidates[] = {
        "C:\\Program Files\\7-Zip\\7z.exe",
        "C:\\Program Files (x86)\\7-Zip\\7z.exe",
        NULL
    };
    int i;
    for (i = 0; candidates[i]; i++) {
        if (file_exists(candidates[i])) {
            strncpy(out, candidates[i], outsz - 1);
            out[outsz - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH];
    char out_dir_slash[MAX_PATH];
    char zip_path[MAX_PATH];
    char src_a[MAX_PATH], ext_a[MAX_PATH];
    char cmdline[4096], output[4096];
    char abs_unzip64[MAX_PATH];
    char sevenz[MAX_PATH];
    HMODULE hu = NULL;
    FnUnZip UnZip_ = NULL;
    int failures = 0;
    int exit_code = 1;
    const char *pwd = "secret42";

    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    if (!find_7z(sevenz, sizeof(sevenz))) {
        printf("[SKIP] 7z.exe not found; cannot build encrypted fixture\n");
        return 0;
    }

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch,  MAX_PATH, "%szip64j_unzip_password", tmp);
    _snprintf(in_dir,   MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir,  MAX_PATH, "%s\\arc", scratch);
    _snprintf(out_dir,  MAX_PATH, "%s\\out", scratch);
    _snprintf(out_dir_slash, MAX_PATH, "%s\\", out_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\p11.zip", arc_dir);
    _snprintf(src_a,    MAX_PATH, "%s\\alpha.txt", in_dir);
    _snprintf(ext_a,    MAX_PATH, "%s\\alpha.txt", out_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(out_dir, NULL);
    DeleteFileA(zip_path);

    if (write_file(src_a, "SECRET_CONTENT") != 0) {
        printf("[FAIL] write source\n"); return 1;
    }

    /* ---- 1. Build encrypted archive via 7z (ZipCrypto, not AES) ---- */
    _snprintf(cmdline, sizeof(cmdline),
              "\"\"%s\" a -tzip -p%s -mem=ZipCrypto \"%s\" \"%s\" > NUL\"",
              sevenz, pwd, zip_path, src_a);
    {
        int rc = system(cmdline);
        if (rc != 0 || !file_exists(zip_path)) {
            printf("[FAIL] 7z build rc=%d zip=%s\n", rc, zip_path);
            return 1;
        }
        printf("       built encrypted fixture %s\n", zip_path);
    }

    /* ---- 2. Load unzip64 ---- */
    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }
    UnZip_ = (FnUnZip)GetProcAddress(hu, "UnZip");
    if (!UnZip_) { printf("[FAIL] GetProcAddress UnZip\n"); goto cleanup; }

    /* ---- 3. No -P: should fail, file not produced ---- */
    DeleteFileA(ext_a);
    _snprintf(cmdline, sizeof(cmdline), "-x -o \"%s\" \"%s\"", zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        printf("       no -P           rc=%d extracted=%d\n", rc, file_exists(ext_a));
        if (rc == 0 && file_exists(ext_a)) {
            printf("[FAIL] no -P should NOT succeed on encrypted zip\n");
            failures++;
        }
    }
    DeleteFileA(ext_a);

    /* ---- 4. Wrong password: should fail ---- */
    _snprintf(cmdline, sizeof(cmdline),
              "-x -o -Pwrongpw \"%s\" \"%s\"", zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        printf("       -Pwrongpw       rc=%d extracted=%d\n", rc, file_exists(ext_a));
        if (rc == 0 && file_exists(ext_a)) {
            /* unzip60 may extract then report CRC error; check content */
            char buf[64] = {0};
            FILE *fp = fopen(ext_a, "rb");
            if (fp) { fread(buf, 1, sizeof(buf) - 1, fp); fclose(fp); }
            if (strcmp(buf, "SECRET_CONTENT") == 0) {
                printf("[FAIL] -Pwrongpw should NOT recover original content\n");
                failures++;
            }
        }
    }
    DeleteFileA(ext_a);

    /* ---- 5. Correct password: should succeed ---- */
    _snprintf(cmdline, sizeof(cmdline),
              "-x -o -P%s \"%s\" \"%s\"", pwd, zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        printf("       -P%-13s rc=%d extracted=%d\n", pwd, rc, file_exists(ext_a));
        if (rc != 0 || !file_exists(ext_a)) {
            printf("[FAIL] correct -P should succeed\n");
            failures++;
        } else {
            char buf[64] = {0};
            FILE *fp = fopen(ext_a, "rb");
            if (fp) { fread(buf, 1, sizeof(buf) - 1, fp); fclose(fp); }
            if (strcmp(buf, "SECRET_CONTENT") != 0) {
                printf("[FAIL] content mismatch: got=\"%s\"\n", buf);
                failures++;
            } else {
                printf("       content verified: \"%s\"\n", buf);
            }
        }
    }

    if (failures == 0) {
        printf("\n[PASS] -P password\n");
        exit_code = 0;
    } else {
        printf("\n[FAIL] %d case(s) failed\n", failures);
    }

cleanup:
    if (hu) FreeLibrary(hu);
    return exit_code;
}
