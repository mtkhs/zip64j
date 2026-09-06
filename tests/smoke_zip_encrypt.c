/* Encryption round-trip: zip64j creates a ZipCrypto archive with `-P`, then
 * unzip64 reads it back.
 *
 *   -P<pass>            → archive created, and it really is encrypted
 *   extract without -P  → fails, original content not recovered
 *   extract -P<wrong>   → fails, original content not recovered
 *   extract -P<correct> → content matches
 *   -e without -P       → ERROR_PASSWORD_FILE (this DLL cannot prompt)
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../include/zip64j.h"

typedef int (WINAPI *FnZip)(HWND, LPCSTR, LPSTR, DWORD);
typedef int (WINAPI *FnUnZip)(HWND, LPCSTR, LPSTR, DWORD);

static const char *PLAINTEXT = "SECRET_CONTENT_FOR_ROUNDTRIP";

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

/* Bit 0 of the local header's general purpose flag marks an encrypted entry. */
static int archive_is_encrypted(const char *path)
{
    unsigned char hdr[8];
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp) return 0;
    n = fread(hdr, 1, sizeof(hdr), fp);
    fclose(fp);
    if (n != sizeof(hdr)) return 0;
    if (hdr[0] != 'P' || hdr[1] != 'K' || hdr[2] != 0x03 || hdr[3] != 0x04) return 0;
    return (hdr[6] & 0x01) ? 1 : 0;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH];
    char in_dir[MAX_PATH], arc_dir[MAX_PATH], out_dir[MAX_PATH], out_dir_slash[MAX_PATH];
    char zip_path[MAX_PATH], src[MAX_PATH], ext[MAX_PATH];
    char abs_zip64j[MAX_PATH], abs_unzip64[MAX_PATH];
    char cmdline[4096], output[4096], content[256];
    HMODULE hz = NULL, hu = NULL;
    FnZip   Zip_   = NULL;
    FnUnZip UnZip_ = NULL;
    int failures = 0;
    int exit_code = 1;
    const char *pwd = "secret42";

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch,  MAX_PATH, "%szip64j_encrypt", tmp);
    _snprintf(in_dir,   MAX_PATH, "%s\\in",  scratch);
    _snprintf(arc_dir,  MAX_PATH, "%s\\arc", scratch);
    _snprintf(out_dir,  MAX_PATH, "%s\\out", scratch);
    _snprintf(out_dir_slash, MAX_PATH, "%s\\", out_dir);
    _snprintf(zip_path, MAX_PATH, "%s\\enc.zip", arc_dir);
    _snprintf(src,      MAX_PATH, "%s\\alpha.txt", in_dir);
    _snprintf(ext,      MAX_PATH, "%s\\alpha.txt", out_dir);

    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir,  NULL);
    CreateDirectoryA(arc_dir, NULL);
    CreateDirectoryA(out_dir, NULL);
    DeleteFileA(zip_path);

    if (write_file(src, PLAINTEXT) != 0) {
        printf("[FAIL] write source\n");
        return 1;
    }

    hz = LoadLibraryA(abs_zip64j);
    if (!hz) { printf("[FAIL] LoadLibrary zip64j err=%lu\n", GetLastError()); goto cleanup; }
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    if (!Zip_) { printf("[FAIL] GetProcAddress Zip\n"); goto cleanup; }

    hu = LoadLibraryA(abs_unzip64);
    if (!hu) { printf("[FAIL] LoadLibrary unzip64 err=%lu\n", GetLastError()); goto cleanup; }
    UnZip_ = (FnUnZip)GetProcAddress(hu, "UnZip");
    if (!UnZip_) { printf("[FAIL] GetProcAddress UnZip\n"); goto cleanup; }

    /* ---- 1. Create an encrypted archive ---- */
    _snprintf(cmdline, sizeof(cmdline),
              "-P%s \"%s\" \"%s\\\" alpha.txt", pwd, zip_path, in_dir);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        printf("       Zip -P%s        rc=%d created=%d\n", pwd, rc, file_exists(zip_path));
        if (rc != 0 || !file_exists(zip_path)) {
            printf("[FAIL] encrypted archive not created (output: %s)\n", output);
            failures++;
            goto verdict;
        }
        if (!archive_is_encrypted(zip_path)) {
            printf("[FAIL] archive exists but its entries are not encrypted\n");
            failures++;
            goto verdict;
        }
        printf("       gp_flag bit 0 set: entry is encrypted\n");
    }

    /* ---- 2. Extract without a password ---- */
    DeleteFileA(ext);
    _snprintf(cmdline, sizeof(cmdline), "-x -o \"%s\" \"%s\"", zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        content[0] = '\0';
        if (file_exists(ext)) read_all(ext, content, sizeof(content));
        printf("       UnZip no -P      rc=%d recovered=%d\n",
               rc, strcmp(content, PLAINTEXT) == 0);
        if (strcmp(content, PLAINTEXT) == 0) {
            printf("[FAIL] content recovered without a password\n");
            failures++;
        }
    }

    /* ---- 3. Extract with the wrong password ---- */
    DeleteFileA(ext);
    _snprintf(cmdline, sizeof(cmdline),
              "-x -o -Pwrongpw \"%s\" \"%s\"", zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        content[0] = '\0';
        if (file_exists(ext)) read_all(ext, content, sizeof(content));
        printf("       UnZip -Pwrongpw  rc=%d recovered=%d\n",
               rc, strcmp(content, PLAINTEXT) == 0);
        if (strcmp(content, PLAINTEXT) == 0) {
            printf("[FAIL] content recovered with the wrong password\n");
            failures++;
        }
    }

    /* ---- 4. Extract with the correct password ---- */
    DeleteFileA(ext);
    _snprintf(cmdline, sizeof(cmdline),
              "-x -o -P%s \"%s\" \"%s\"", pwd, zip_path, out_dir_slash);
    output[0] = '\0';
    {
        int rc = UnZip_(NULL, cmdline, output, sizeof(output));
        content[0] = '\0';
        if (file_exists(ext)) read_all(ext, content, sizeof(content));
        printf("       UnZip -P%s      rc=%d recovered=%d\n",
               pwd, rc, strcmp(content, PLAINTEXT) == 0);
        if (rc != 0 || strcmp(content, PLAINTEXT) != 0) {
            printf("[FAIL] round-trip failed: got \"%s\"\n", content);
            failures++;
        }
    }

    /* ---- 5. `-e` with no password: cannot prompt, must be refused ---- */
    _snprintf(zip_path, MAX_PATH, "%s\\noprompt.zip", arc_dir);
    DeleteFileA(zip_path);
    _snprintf(cmdline, sizeof(cmdline),
              "-e \"%s\" \"%s\\\" alpha.txt", zip_path, in_dir);
    output[0] = '\0';
    {
        int rc = Zip_(NULL, cmdline, output, sizeof(output));
        printf("       Zip -e (no -P)   rc=0x%04X created=%d\n", rc, file_exists(zip_path));
        if (rc != ERROR_PASSWORD_FILE) {
            printf("[FAIL] expected ERROR_PASSWORD_FILE (0x%04X), got 0x%04X\n",
                   ERROR_PASSWORD_FILE, rc);
            failures++;
        }
        if (file_exists(zip_path)) {
            printf("[FAIL] archive created despite refusing the command\n");
            failures++;
        }
    }

verdict:
    if (failures == 0) {
        printf("\n[PASS] encryption round-trip\n");
        exit_code = 0;
    } else {
        printf("\n[FAIL] encryption round-trip: %d case(s) failed\n", failures);
    }

cleanup:
    if (hu) FreeLibrary(hu);
    if (hz) FreeLibrary(hz);
    return exit_code;
}
