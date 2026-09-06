/* Export surface: verifies zip64j.dll loads and exports the Zip* half of the
 * 統合アーカイバ surface. The UnZip* half lives in unzip64.dll and is not
 * re-exported here.
 *
 * Link-loads zip64j.dll via LoadLibrary, resolves each exported name, and
 * calls a small representative subset to confirm the x64 calling convention
 * matches.
 */

#include <windows.h>
#include <stdio.h>

#include "../include/zip64j.h"

typedef WORD  (WINAPI *FnZipGetVersion)(void);
typedef BOOL  (WINAPI *FnZipGetRunning)(void);
typedef BOOL  (WINAPI *FnZipQueryFunctionList)(int);
typedef WORD  (WINAPI *FnZipQueryEncryption)(void);

static const char *k_exports[] = {
    /* Zip */
    "ZipGetVersion", "ZipGetRunning", "Zip", "ZipW",
    "ZipConfigDialog", "ZipConfigDialogW",
    "ZipQueryFunctionList", "ZipQueryEncryption",
};

int main(void)
{
    HMODULE h = LoadLibraryA("build/dist/zip64j.dll");
    if (!h) {
        printf("[FAIL] LoadLibrary(zip64j.dll) err=%lu\n", GetLastError());
        return 1;
    }
    printf("[OK]   LoadLibrary(zip64j.dll) -> %p\n", (void *)h);

    int missing = 0;
    const int n = (int)(sizeof(k_exports) / sizeof(k_exports[0]));
    for (int i = 0; i < n; i++) {
        if (!GetProcAddress(h, k_exports[i])) {
            printf("[FAIL] missing export: %s\n", k_exports[i]);
            missing++;
        }
    }
    printf("       %d/%d exports resolved\n", n - missing, n);

    FnZipGetVersion   ZipGetVersion_   = (FnZipGetVersion)   GetProcAddress(h, "ZipGetVersion");
    FnZipGetRunning   ZipGetRunning_   = (FnZipGetRunning)   GetProcAddress(h, "ZipGetRunning");
    FnZipQueryFunctionList ZipQFL_     = (FnZipQueryFunctionList) GetProcAddress(h, "ZipQueryFunctionList");
    FnZipQueryEncryption   ZipQEnc_    = (FnZipQueryEncryption)   GetProcAddress(h, "ZipQueryEncryption");

    int fails = missing;

    if (ZipGetVersion_) {
        WORD v = ZipGetVersion_();
        printf("       ZipGetVersion   -> 0x%04X\n", v);
        if (v == 0) { printf("[FAIL] version must be non-zero\n"); fails++; }
    }
    if (ZipGetRunning_) {
        printf("       ZipGetRunning   -> %d\n", ZipGetRunning_());
    }
    if (ZipQFL_) {
        /* Return value is not asserted here — this test only confirms the
         * call resolves and does not crash. */
        printf("       ZipQueryFunctionList(1) -> %d\n", ZipQFL_(1));
    }
    if (ZipQEnc_) {
        printf("       ZipQueryEncryption -> 0x%04X\n", ZipQEnc_());
    }

    FreeLibrary(h);

    if (fails) {
        printf("\n[FAIL] %d issues\n", fails);
        return 1;
    }
    printf("\n[PASS] export surface\n");
    return 0;
}
