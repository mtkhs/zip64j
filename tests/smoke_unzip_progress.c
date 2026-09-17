/* Progress notification: UnZipSetOwnerWindow / Ex / Ex64 and the matching
 * Clear / Kill.
 *
 * Archive: small.txt (6 bytes) + big.bin (1 MiB of poorly compressible
 * bytes, so it is written in several blocks). For each registration kind:
 *
 *   - BEGIN once per entry, then INPROCESS with a write size that never
 *     decreases and ends at the entry size; END once, after everything else
 *   - callback vs window message delivery, EXTRACTINGINFOEX64 64bit fields
 *   - a non-zero return cancels the extraction (ERROR_USER_CANCEL)
 *   - registration rules: one slot, hwnd must match, refused while running
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../include/zip64j.h"

typedef int  (WINAPI *FnZip)  (HWND, LPCSTR, LPSTR, DWORD);
typedef int  (WINAPI *FnUnZip)(HWND, LPCSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *FnSet)  (HWND);
typedef BOOL (WINAPI *FnClear)(void);
typedef BOOL (WINAPI *FnSetEx)(HWND, LPARCHIVERPROC);
typedef BOOL (WINAPI *FnKill) (HWND);
typedef BOOL (WINAPI *FnSetEx64)(HWND, LPARCHIVERPROC, DWORD);
typedef BOOL (WINAPI *FnQFL)  (int);

static FnUnZip   UnZip_;
static FnSet     Set_;
static FnClear   Clear_;
static FnSetEx   SetEx_;
static FnKill    Kill_, KillEx64_;
static FnSetEx64 SetEx64_;

static UINT  g_msg;
static HWND  g_other;       /* a second window for registration checks */

/* What the notification checker saw during one UnZip(). */
static struct {
    BOOL    ex64;           /* lpEis is EXTRACTINGINFOEX64 */
    int     cancel_at;      /* return 1 at this INPROCESS count (0 = never) */
    int     begins, inprocs, ends, errors;
    __int64 size, written;  /* entry being extracted */
    BOOL    nested_checked;
} g;

static int fail(const char *what)
{
    printf("[FAIL] %s\n", what);
    return 1;
}

static int record(UINT state, LPVOID lpEis)
{
    __int64 size, written;
    const char *src, *dst;

    if (g.ex64) {
        LPEXTRACTINGINFOEX64 e = (LPEXTRACTINGINFOEX64)lpEis;
        if (e->dwStructSize != sizeof(*e)) g.errors++;
        size = e->llFileSize; written = e->llWriteSize;
        src = e->szSourceFileName; dst = e->szDestFileName;
    } else {
        LPEXTRACTINGINFO e = (LPEXTRACTINGINFO)lpEis;
        size = e->dwFileSize; written = e->dwWriteSize;
        src = e->szSourceFileName; dst = e->szDestFileName;
    }
    if (g.ends) g.errors++;                        /* nothing after END */

    switch (state) {
    case ARCEXTRACT_BEGIN:
        if (g.begins && g.written != g.size) g.errors++;  /* previous entry incomplete */
        if (written != 0 || src[0] == '\0' || strstr(dst, src) == NULL) g.errors++;
        g.begins++;
        g.size = size;
        g.written = 0;
        if (!g.nested_checked) {
            /* The registration cannot change while UnZip() runs. */
            g.nested_checked = TRUE;
            if (SetEx_(g_other, NULL) || Kill_(g_other) || Clear_()) g.errors++;
        }
        return 0;
    case ARCEXTRACT_INPROCESS:
        if (written < g.written || written > g.size || size != g.size) g.errors++;
        g.written = written;
        g.inprocs++;
        return g.cancel_at != 0 && g.inprocs == g.cancel_at;
    case ARCEXTRACT_END:
        g.ends++;
        return 0;
    default:
        g.errors++;
        return 0;
    }
}

static BOOL CALLBACK arc_proc(HWND hwnd, UINT msg, UINT state, LPVOID lpEis)
{
    (void)hwnd;
    if (msg != g_msg) g.errors++;
    return record(state, lpEis);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g_msg) return record((UINT)wp, (LPVOID)lp);
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* Runs UnZip and checks the notification sequence for a full extraction. */
static int extract(const char *label, BOOL ex64, const char *cmdline,
                   int cancel_at, int want_rc)
{
    char output[1024];
    int rc;

    memset(&g, 0, sizeof(g));
    g.ex64 = ex64;
    g.cancel_at = cancel_at;
    rc = UnZip_(NULL, cmdline, output, sizeof(output));
    printf("       %-22s rc=0x%X begins=%d inprocs=%d ends=%d errors=%d\n",
           label, rc, g.begins, g.inprocs, g.ends, g.errors);
    if (rc != want_rc || g.errors || g.ends != 1 || !g.nested_checked) return 1;
    if (cancel_at) return g.inprocs == cancel_at ? 0 : 1;
    return (g.begins == 2 && g.written == g.size && g.inprocs > 4) ? 0 : 1;
}

int main(void)
{
    char tmp[MAX_PATH], scratch[MAX_PATH], in_dir[MAX_PATH], out_dir[MAX_PATH];
    char zip_path[MAX_PATH], path[MAX_PATH], cmdline[2048], output[1024];
    char abs_zip64j[MAX_PATH], abs_unzip64[MAX_PATH];
    WNDCLASSA wc;
    HWND owner;
    HMODULE hz, hu;
    FnZip Zip_;
    FnQFL QFL_;
    FILE *fp;
    unsigned seed = 1;
    int i;

    if (sizeof(EXTRACTINGINFOEX64) != 0x862) return fail("sizeof(EXTRACTINGINFOEX64) != 0x862");

    GetFullPathNameA("build/dist/zip64j.dll",  MAX_PATH, abs_zip64j,  NULL);
    GetFullPathNameA("build/dist/unzip64.dll", MAX_PATH, abs_unzip64, NULL);

    GetTempPathA(MAX_PATH, tmp);
    _snprintf(scratch,  MAX_PATH, "%szip64j_unzip_progress", tmp);
    _snprintf(in_dir,   MAX_PATH, "%s\\in",  scratch);
    _snprintf(out_dir,  MAX_PATH, "%s\\out\\", scratch);
    _snprintf(zip_path, MAX_PATH, "%s\\progress.zip", scratch);
    CreateDirectoryA(scratch, NULL);
    CreateDirectoryA(in_dir, NULL);
    CreateDirectoryA(out_dir, NULL);
    DeleteFileA(zip_path);

    /* ---- 1. Archive ---- */
    _snprintf(path, MAX_PATH, "%s\\small.txt", in_dir);
    if (!(fp = fopen(path, "wb"))) return fail("write small.txt");
    fputs("small\n", fp);
    fclose(fp);
    _snprintf(path, MAX_PATH, "%s\\big.bin", in_dir);
    if (!(fp = fopen(path, "wb"))) return fail("write big.bin");
    for (i = 0; i < (1 << 20); i++) {
        seed = seed * 1103515245u + 12345u;
        fputc((int)(seed >> 16) & 0xFF, fp);
    }
    fclose(fp);

    hz = LoadLibraryA(abs_zip64j);
    if (!hz) return fail("LoadLibrary zip64j");
    Zip_ = (FnZip)GetProcAddress(hz, "Zip");
    SetCurrentDirectoryA(in_dir);
    _snprintf(cmdline, sizeof(cmdline), "\"%s\" small.txt big.bin", zip_path);
    if (Zip_(NULL, cmdline, output, sizeof(output)) != 0) return fail("Zip");
    FreeLibrary(hz);

    hu = LoadLibraryA(abs_unzip64);
    if (!hu) return fail("LoadLibrary unzip64");
    UnZip_    = (FnUnZip)  GetProcAddress(hu, "UnZip");
    Set_      = (FnSet)    GetProcAddress(hu, "UnZipSetOwnerWindow");
    Clear_    = (FnClear)  GetProcAddress(hu, "UnZipClearOwnerWindow");
    SetEx_    = (FnSetEx)  GetProcAddress(hu, "UnZipSetOwnerWindowEx");
    Kill_     = (FnKill)   GetProcAddress(hu, "UnZipKillOwnerWindowEx");
    SetEx64_  = (FnSetEx64)GetProcAddress(hu, "UnZipSetOwnerWindowEx64");
    KillEx64_ = (FnKill)   GetProcAddress(hu, "UnZipKillOwnerWindowEx64");
    QFL_      = (FnQFL)    GetProcAddress(hu, "UnZipQueryFunctionList");
    if (!UnZip_ || !Set_ || !Clear_ || !SetEx_ || !Kill_ || !SetEx64_ || !KillEx64_ || !QFL_)
        return fail("GetProcAddress");
    if (!QFL_(ISARC_SET_OWNER_WINDOW_EX64) || !QFL_(ISARC_KILL_OWNER_WINDOW_EX64))
        return fail("QueryFunctionList(Ex64)");

    g_msg = RegisterWindowMessageA(WM_ARCEXTRACT);
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "smoke_unzip_progress";
    RegisterClassA(&wc);
    owner   = CreateWindowA(wc.lpszClassName, "owner", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    g_other = CreateWindowA(wc.lpszClassName, "other", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!owner || !g_other) return fail("CreateWindow");

    _snprintf(cmdline, sizeof(cmdline), "-x -o --i \"%s\" \"%s\"", zip_path, out_dir);

    /* ---- 2. Notification sequence per registration kind ---- */
    if (!SetEx_(owner, arc_proc)) return fail("SetOwnerWindowEx");
    if (extract("Ex callback", FALSE, cmdline, 0, 0)) return fail("Ex callback sequence");
    if (extract("Ex cancel", FALSE, cmdline, 3, ERROR_USER_CANCEL)) return fail("Ex callback cancel");
    if (!Kill_(owner)) return fail("KillOwnerWindowEx");

    if (!SetEx64_(owner, arc_proc, sizeof(EXTRACTINGINFOEX64))) return fail("SetOwnerWindowEx64");
    if (extract("Ex64 callback", TRUE, cmdline, 0, 0)) return fail("Ex64 callback sequence");
    if (!KillEx64_(owner)) return fail("KillOwnerWindowEx64");

    if (!Set_(owner)) return fail("SetOwnerWindow");
    if (extract("window message", FALSE, cmdline, 0, 0)) return fail("window message sequence");
    if (extract("message cancel", FALSE, cmdline, 3, ERROR_USER_CANCEL)) return fail("window message cancel");
    if (!Clear_()) return fail("ClearOwnerWindow");

    /* ---- 3. Registration rules ---- */
    if (Set_(NULL))                              return fail("Set(NULL) accepted");
    if (SetEx64_(owner, arc_proc, 0x800))        return fail("Ex64 with wrong struct size accepted");
    if (Clear_())                                return fail("Clear with nothing registered returned TRUE");
    if (!Set_(owner) || !SetEx_(owner, arc_proc)) return fail("re-register same hwnd");
    if (Set_(g_other) || SetEx_(g_other, NULL))  return fail("register over another hwnd");
    if (Kill_(g_other) || KillEx64_(NULL))       return fail("Kill with another hwnd");
    if (!KillEx64_(owner))                       return fail("KillOwnerWindowEx64 of an Ex registration");
    if (Kill_(owner))                            return fail("Kill after the slot is empty");

    FreeLibrary(hu);
    DestroyWindow(owner);
    DestroyWindow(g_other);
    printf("\n[PASS] progress notification\n");
    return 0;
}
