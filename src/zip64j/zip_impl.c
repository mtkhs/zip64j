/*
 * zip_impl.c - Implementation of the Zip* exports.
 *
 * Thread-safety: Zip() is single-instance; a per-DLL critical section
 * serialises concurrent callers and sets s_running for ZipGetRunning.
 */

#include "../../include/zip64j.h"
#include "zip30_types.h"
#include "cmdline.h"
#include "../common/debug_log.h"

#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <mbstring.h>

/* zip30's windll/windll.c declares hCurrentInst but only assigns it inside
 * a `#ifdef ZIPLIB` DllMain that our build does not define. Without this
 * assignment the symbol stays NULL, and FindResourceA(NULL, ...) falls back
 * to the *calling* EXE's resources — so ZipGetVersion would return e.g.
 * afxw.exe's 1.67 instead of our own 1.00. Own the DllMain here and seed
 * hCurrentInst ourselves. */
extern HINSTANCE hCurrentInst;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hCurrentInst = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

/* ---- Per-invocation state (guarded by s_cs) ---- */

static CRITICAL_SECTION s_cs;
static INIT_ONCE s_cs_once = INIT_ONCE_STATIC_INIT;

/* zip30 reads at most IZ_PWLEN (80) characters of key material (crypt.h). */
#define ZIP_PASSWORD_MAX 81

/* zip accepts `mmddyyyy` or `yyyy-mm-dd` for -t / -tt. */
#define ZIP_DATE_MAX 32

static BOOL s_running = FALSE;
static LPSTR  s_output_cursor = NULL;
static DWORD  s_output_left   = 0;
static char   s_password[ZIP_PASSWORD_MAX] = {0};  /* captured from -P<pwd> */
static BOOL   s_password_present = FALSE;

static BOOL CALLBACK init_cs(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&s_cs);
    return TRUE;
}

static void ensure_cs(void)
{
    InitOnceExecuteOnce(&s_cs_once, init_cs, NULL, NULL);
}

/* ---- Print callback — writes into the caller's szOutput buffer. ---- */

static int WINAPI cb_print(LPSTR buf, unsigned long size)
{
    DWORD to_copy;

    if (s_output_cursor == NULL || s_output_left <= 1) {
        return (int)size;
    }
    to_copy = (s_output_left - 1 < size) ? (s_output_left - 1) : (DWORD)size;
    memcpy(s_output_cursor, buf, to_copy);
    s_output_cursor[to_copy] = '\0';
    s_output_cursor += to_copy;
    s_output_left   -= to_copy;
    return (int)size;
}

/* Info-ZIP password callback (zip.h IZ_PW_*). zip30 calls this twice per
 * archive — ZP_PW_ENTER then ZP_PW_VERIFY — and compares the two results, so
 * handing back the same string both times is what makes verification pass.
 * Without a pre-supplied password we cancel: this DLL has no UI to prompt. */
#define IZ_PW_ENTERED_Z  0
#define IZ_PW_CANCEL_Z  (-1)
static int WINAPI cb_password(LPSTR pwbuf, int bufsiz, LPCSTR promptmsg, LPCSTR entryname)
{
    size_t n;
    (void)promptmsg; (void)entryname;
    if (!s_password_present || bufsiz <= 0 || !pwbuf) return IZ_PW_CANCEL_Z;
    n = strlen(s_password);
    if (n + 1 > (size_t)bufsiz) n = (size_t)bufsiz - 1;
    memcpy(pwbuf, s_password, n);
    pwbuf[n] = '\0';
    return IZ_PW_ENTERED_Z;
}

static LPSTR WINAPI cb_comment(LPSTR buf)
{
    /* No interactive comment editor. Leave the buffer as-is. */
    return buf;
}

static int WINAPI cb_service64(LPCSTR filename, unsigned __int64 size)
{
    (void)filename; (void)size;
    return 0;
}

/* ---- Command-line parser ---- */

/* Backing store for the ZPOPT30 members that are pointers: zip30 reads them
 * during ZpArchive, so the storage has to outlive parse_cmdline. Zip() owns
 * one of these on its stack. */
typedef struct {
    char rootdir [MAX_PATH];
    char tempdir [MAX_PATH];
    char date    [ZIP_DATE_MAX];
    char password[ZIP_PASSWORD_MAX];
    int  password_present;
} zip_opt_store;

/* Copy `src` into a fixed buffer, truncating if needed. */
static void store_str(char *dst, size_t dst_size, const char *src)
{
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Populates opt/zcl from a command line. Caller frees zcl->lpszZipFN and
 * zcl->FNV separately (both allocated here). Returns 0 on success, -1 on
 * malformed input or allocation failure. */
static int parse_cmdline(ZPOPT30 *opt, ZCL30 *zcl, const char *cmdline,
                         zip_opt_store *store)
{
    char **argv = NULL;
    int argc;
    int i;
    int rc = -1;
    char *arcname = NULL;
    char **files = NULL;
    int filenum = 0;
    char *rootdir = NULL;

    memset(opt, 0, sizeof(*opt));
    opt->fLevel = '6';
    opt->fVerbose = TRUE;

    argv = zip64j_split_cmdline_with_response(cmdline);
    if (!argv) return -1;

    argc = zip64j_ptrarraylen((void **)argv);
    if (argc < 2) goto done;

    files = (char **)malloc(sizeof(char *) * (argc + 1));
    if (!files) goto done;

    for (i = 0; i < argc; i++) {
        char *arg = argv[i];

        zip64j_log("  parse_cmdline arg[%d]=\"%s\"", i, arg);

        if (arg[0] == '-') {
            char *flag = arg + 1;

            /* ---- Options that take a value ----
             * Handled ahead of the cluster loop because they consume the rest
             * of the token (or the following one) and so cannot be clustered.
             *
             * -P: encryption password. zip32j takes the following token; an
             *     attached tail (`-Pfoo`) is accepted too, since that is the
             *     form the UnZip side of the spec uses and any character is
             *     valid in a password.
             * -b / -t / -tt: value is the following token, as in zip32j. */
            if (flag[0] == 'P') {
                const char *pw = flag + 1;
                if (*pw == '\0') {
                    if (i + 1 >= argc) {
                        zip64j_log("  parse_cmdline: -P missing argument");
                        goto done;
                    }
                    pw = argv[++i];
                }
                store_str(store->password, sizeof(store->password), pw);
                store->password_present = (store->password[0] != '\0');
                opt->fEncrypt = TRUE;
                zip64j_log("  parse_cmdline: -P password captured (len=%d)",
                           (int)strlen(pw));
                continue;
            }

            if (strcmp(flag, "b") == 0) {
                if (i + 1 >= argc) {
                    zip64j_log("  parse_cmdline: -b missing argument");
                    goto done;
                }
                store_str(store->tempdir, sizeof(store->tempdir), argv[++i]);
                opt->szTempDir = store->tempdir;
                opt->fTemp     = TRUE;
                continue;
            }

            if (strcmp(flag, "t") == 0 || strcmp(flag, "tt") == 0) {
                if (i + 1 >= argc) {
                    zip64j_log("  parse_cmdline: -%s missing argument", flag);
                    goto done;
                }
                store_str(store->date, sizeof(store->date), argv[++i]);
                opt->Date = store->date;
                if (flag[1] == 't') opt->fExcludeDate = TRUE;  /* -tt: before date */
                else                opt->fIncludeDate = TRUE;  /* -t:  after date  */
                continue;
            }

            while (*flag) {
                switch (*flag) {
                case 'e': opt->fEncrypt = TRUE; break;
                case 'd': opt->fDeleteEntries = TRUE; break;
                case 'J': opt->fJunkSFX  = TRUE;     break;
                case 'D': opt->fNoDirEntries = TRUE; break;
                case 'q': opt->fQuiet    = TRUE;     break;
                case 'v': opt->fVerbose  = TRUE;     break;
                case 'j': opt->fJunkDir  = TRUE;     break;
                case 'k': opt->fForce    = TRUE;     break;
                case 'm': opt->fMove     = TRUE;     break;
                case 'u': opt->fUpdate   = TRUE;     break;
                case 'f': opt->fFreshen  = TRUE;     break;
                case 'g': opt->fGrow     = TRUE;     break;
                case 'o': opt->fLatestTime = TRUE;   break;
                case 'S': opt->fSystem   = TRUE;     break;
                case 'X': opt->fExtra    = TRUE;     break;
                case '$': opt->fVolume   = TRUE;     break;
                case 'A': opt->fOffsets  = TRUE;     break;
                case '!': opt->fPrivilege= TRUE;     break;
                case 'r': opt->fRecurse = 1;         break;
                case 'R': opt->fRecurse = 2;         break;
                case 'F':
                    if (*(flag + 1) == 'F') { opt->fRepair = 2; flag++; }
                    else                    { opt->fRepair = 1; }
                    break;
                case 'l':
                    if (*(flag + 1) == 'l') { opt->fCRLF_LF = TRUE; flag++; }
                    else                    { opt->fLF_CRLF = TRUE; }
                    break;
                default:
                    if (isdigit((unsigned char)*flag)) {
                        opt->fLevel = *flag;
                    } else {
                        zip64j_log("  parse_cmdline: unknown flag char '%c' in \"%s\"",
                                   *flag, arg);
                        goto done;
                    }
                    break;
                }
                flag++;
            }
            continue;
        }

        if (arcname == NULL) {
            size_t len = strlen(arg) + 1;
            arcname = (char *)malloc(len);
            if (!arcname) goto done;
            memcpy(arcname, arg, len);
        } else if (rootdir == NULL && arg[0] != '\0' &&
                   /* CP932-safe: last char is a real '\\' or '/', not the
                    * trailing byte of a double-byte character. */
                   ((unsigned char *)_mbsrchr((unsigned char *)arg, '\\') ==
                        (unsigned char *)arg + strlen(arg) - 1 ||
                    (unsigned char *)_mbsrchr((unsigned char *)arg, '/') ==
                        (unsigned char *)arg + strlen(arg) - 1)) {
            size_t len = strlen(arg) + 1;
            rootdir = (char *)malloc(len);
            if (!rootdir) goto done;
            memcpy(rootdir, arg, len);
        } else {
            size_t len;
            char *dup;
            char **nf = (char **)realloc(files, sizeof(char *) * (filenum + 1));
            if (!nf) goto done;
            files = nf;
            len = strlen(arg) + 1;
            dup = (char *)malloc(len);
            if (!dup) goto done;
            memcpy(dup, arg, len);
            files[filenum++] = dup;
        }
    }

    {
        char **nf = (char **)realloc(files, sizeof(char *) * (filenum + 1));
        if (!nf) goto done;
        files = nf;
        files[filenum] = NULL;
    }

    if (rootdir && *rootdir != '\0') {
        size_t rl = strlen(rootdir);
        if (rootdir[rl - 1] == '\\' || rootdir[rl - 1] == '/') {
            rootdir[rl - 1] = '\0';
        }
        store_str(store->rootdir, sizeof(store->rootdir), rootdir);
        opt->szRootDir = store->rootdir;
    }

    zcl->argc      = filenum;
    zcl->lpszZipFN = arcname;
    zcl->FNV       = files;
    zcl->lpszAltFNL = NULL;
    arcname = NULL;     /* ownership transferred */
    files   = NULL;
    rc = 0;

done:
    free(arcname);
    if (files) {
        for (i = 0; i < filenum; i++) free(files[i]);
        free(files);
    }
    free(rootdir);
    free(argv);
    return rc;
}

/* ---- Exports ---- */

WORD WINAPI ZipGetVersion(void)
{
    VS_FIXEDFILEINFO *vfi;
    HRSRC  hres;
    HGLOBAL hmem;
    void *raw;
    WORD ver;

    hres = FindResourceA(hCurrentInst, MAKEINTRESOURCEA(VS_VERSION_INFO),
                         MAKEINTRESOURCEA(16 /* RT_VERSION */));
    if (!hres) return 0x0100;

    hmem = LoadResource(hCurrentInst, hres);
    if (!hmem) return 0x0100;

    raw = LockResource(hmem);
    if (!raw) return 0x0100;

    /* VS_FIXEDFILEINFO sits at offset 40: after the WORD-aligned
     * L"VS_VERSION_INFO" key (6 header + 32 wchar + 2 pad). Fixed offset
     * avoids pulling in version.lib for VerQueryValue. */
    vfi = (VS_FIXEDFILEINFO *)((char *)raw + 40);
    ver = (WORD)(((vfi->dwFileVersionMS >> 16) * 100) +
                 (vfi->dwFileVersionMS & 0xFFFF));
    if (ver == 0) ver = 0x0100;
    return ver;
}

BOOL WINAPI ZipGetRunning(void)
{
    return s_running;
}

/* Map zip30 ZE_ return code to 統合アーカイバAPI仕様 0x8000+ error code. */
static int map_ze_to_spec(int ze)
{
    switch (ze) {
    case 0:  /* ZE_OK */
        return 0;
    case 2:  /* ZE_EOF     — unexpected end of zip file */
        return ERROR_HEADER_BROKEN;
    case 3:  /* ZE_FORM    — zip file structure error */
        return ERROR_HEADER_BROKEN;
    case 4:  /* ZE_MEM     — out of memory */
        return ERROR_ENOUGH_MEMORY;
    case 5:  /* ZE_LOGIC   — internal logic error */
        return ERROR_HEADER_BROKEN;
    case 6:  /* ZE_BIG     — entry too large */
        return ERROR_LONG_FILE_NAME;
    case 7:  /* ZE_NOTE    — invalid comment format */
        return ERROR_COMMENT_HEADER;
    case 8:  /* ZE_TEST    — zip test failed */
        return ERROR_FILE_CRC;
    case 9:  /* ZE_ABORT   — user abort */
        return ERROR_USER_CANCEL;
    case 10: /* ZE_TEMP    — temp file error */
        return ERROR_TMP_OPEN;
    case 11: /* ZE_READ    — read/seek error */
        return ERROR_CANNOT_READ;
    case 12: /* ZE_NONE    — nothing to do */
        return ERROR_NOT_EXIST;
    case 13: /* ZE_NAME    — missing/empty zip */
        return ERROR_NOT_FIND_ARC_FILE;
    case 14: /* ZE_WRITE   — error writing */
        return ERROR_CANNOT_WRITE;
    case 15: /* ZE_CREAT   — could not open to write */
        return ERROR_FILE_OPEN;
    case 16: /* ZE_PARMS   — bad command line */
        return ERROR_COMMAND_NAME;
    case 18: /* ZE_OPEN    — could not open file to read */
        return ERROR_FILE_OPEN;
    case 19: /* ZE_COMPERR — compilation option error */
        return ERROR_METHOD;
    case 20: /* ZE_ZIP64   — Zip64 not supported */
        return ERROR_NOT_SUPPORT;
    default:
        return ze ? ERROR_HEADER_BROKEN : 0;
    }
}

int WINAPI Zip(HWND hwnd, LPCSTR szCmdLine, LPSTR szOutput, DWORD dwSize)
{
    ZIPUSERFUNCTIONS30 funcs;
    ZPOPT30 opt;
    ZCL30   zcl;
    zip_opt_store store;
    int     rc = -1;
    int     parse_rc;

    (void)hwnd;

    zip64j_log("Zip() enter cmdline=\"%s\"", szCmdLine ? szCmdLine : "(null)");

    memset(&zcl,   0, sizeof(zcl));
    memset(&opt,   0, sizeof(opt));
    memset(&store, 0, sizeof(store));
    if (szOutput && dwSize > 0) szOutput[0] = '\0';

    ensure_cs();
    EnterCriticalSection(&s_cs);
    if (s_running) {
        LeaveCriticalSection(&s_cs);
        return ERROR_ALREADY_RUNNING;
    }
    s_running = TRUE;
    s_output_cursor = szOutput;
    s_output_left   = dwSize;
    LeaveCriticalSection(&s_cs);

    funcs.print    = cb_print;
    funcs.comment  = cb_comment;
    funcs.password = cb_password;
    funcs.split    = NULL;
    funcs.ServiceApplication64          = cb_service64;
    funcs.ServiceApplication64_No_Int64 = NULL;

    if (ZpInit(&funcs) == 0) {
        /* ZpInit returns 0 = failure per Info-ZIP convention */
        zip64j_log("Zip() ZpInit failed");
        rc = ERROR_ENOUGH_MEMORY;
        goto cleanup;
    }

    parse_rc = parse_cmdline(&opt, &zcl, szCmdLine ? szCmdLine : "", &store);
    if (parse_rc != 0 || zcl.lpszZipFN == NULL) {
        zip64j_log("Zip() parse_cmdline failed parse_rc=%d arcname=%s",
                   parse_rc, zcl.lpszZipFN ? zcl.lpszZipFN : "(null)");
        rc = ERROR_COMMAND_NAME;
        goto cleanup;
    }

    /* zip32j prompts for a missing password through a dialog; this DLL has no
     * UI, so `-e` without `-P` has no way to obtain one. */
    if (opt.fEncrypt && !store.password_present) {
        zip64j_log("Zip() -e given without a password (-P); cannot prompt");
        rc = ERROR_PASSWORD_FILE;
        goto cleanup;
    }

    /* Publish the password for cb_password. s_cs already marks us running, so
     * concurrent Zip() callers are blocked out. */
    if (store.password_present) {
        store_str(s_password, sizeof(s_password), store.password);
        s_password_present = TRUE;
    } else {
        s_password[0] = '\0';
        s_password_present = FALSE;
    }

    zip64j_log("Zip() parsed: arc=\"%s\" rootdir=\"%s\" filenum=%d level='%c' "
               "recurse=%d update=%d freshen=%d move=%d junk=%d nodirent=%d "
               "quiet=%d encrypt=%d delete=%d junksfx=%d "
               "tempdir=\"%s\" date=\"%s\" incdate=%d excdate=%d",
               zcl.lpszZipFN,
               opt.szRootDir ? opt.szRootDir : "(null)",
               zcl.argc, opt.fLevel,
               opt.fRecurse, opt.fUpdate, opt.fFreshen, opt.fMove,
               opt.fJunkDir, opt.fNoDirEntries, opt.fQuiet, opt.fEncrypt,
               opt.fDeleteEntries, opt.fJunkSFX,
               opt.szTempDir ? opt.szTempDir : "(null)",
               opt.Date ? opt.Date : "(null)",
               opt.fIncludeDate, opt.fExcludeDate);
    {
        int i;
        for (i = 0; i < zcl.argc; i++) zip64j_log("  file[%d]=\"%s\"", i, zcl.FNV[i]);
    }

    {
        int ze = ZpArchive(zcl, &opt);
        rc = map_ze_to_spec(ze);
        zip64j_log("Zip() ZpArchive ze=%d -> rc=0x%04X", ze, rc);
    }

cleanup:
    if (zcl.lpszZipFN) free(zcl.lpszZipFN);
    if (zcl.FNV) {
        int i;
        for (i = 0; zcl.FNV[i]; i++) free(zcl.FNV[i]);
        free(zcl.FNV);
    }

    /* Scrub the password (local and static) on every exit path. */
    SecureZeroMemory(store.password, sizeof(store.password));
    EnterCriticalSection(&s_cs);
    SecureZeroMemory(s_password, sizeof(s_password));
    s_password_present = FALSE;
    s_running       = FALSE;
    s_output_cursor = NULL;
    s_output_left   = 0;
    LeaveCriticalSection(&s_cs);

    zip64j_log("Zip() exit rc=%d output=\"%s\"",
               rc, (szOutput && dwSize > 0) ? szOutput : "(n/a)");
    return rc;
}

int WINAPI ZipW(HWND hwnd, LPCWSTR szCmdLine, LPWSTR szOutput, DWORD dwSize)
{
    /* Info-ZIP zip30 only consumes ANSI paths, so the W entry point
     * round-trips through CP_ACP (CP932 on JP Windows). Characters outside
     * CP_ACP are lossy; callers needing full Unicode should call the ANSI
     * entry with paths already in CP932. */
    char *cmd_a = NULL, *out_a = NULL;
    int   cmd_len_a, rc = -1;

    zip64j_log("ZipW() enter");
    if (szOutput && dwSize > 0) szOutput[0] = L'\0';
    if (!szCmdLine) return -1;

    cmd_len_a = WideCharToMultiByte(CP_ACP, 0, szCmdLine, -1, NULL, 0, NULL, NULL);
    if (cmd_len_a <= 0) return -1;
    cmd_a = (char *)malloc((size_t)cmd_len_a);
    if (!cmd_a) return -1;
    WideCharToMultiByte(CP_ACP, 0, szCmdLine, -1, cmd_a, cmd_len_a, NULL, NULL);

    if (szOutput && dwSize > 0) {
        DWORD out_bytes = dwSize * 2 + 1;
        out_a = (char *)malloc(out_bytes);
        if (out_a) {
            out_a[0] = '\0';
            rc = Zip(hwnd, cmd_a, out_a, out_bytes);
            MultiByteToWideChar(CP_ACP, 0, out_a, -1, szOutput, (int)dwSize);
            szOutput[dwSize - 1] = L'\0';
        }
    } else {
        rc = Zip(hwnd, cmd_a, NULL, 0);
    }

    free(cmd_a);
    free(out_a);
    return rc;
}

int WINAPI ZipConfigDialog(HWND hwnd, LPSTR szCommandBuf, int iMode)
{
    (void)hwnd; (void)szCommandBuf; (void)iMode;
    return -1;
}

int WINAPI ZipConfigDialogW(HWND hwnd, LPWSTR szCommandBuf, int iMode)
{
    (void)hwnd; (void)szCommandBuf; (void)iMode;
    return -1;
}

BOOL WINAPI ZipQueryFunctionList(int iFunction)
{
    switch (iFunction) {
    case ISARC_GET_VERSION:
    case ISARC_GET_RUNNING:
    case ISARC:
    case ISARC_QUERY_FUNCTION_LIST:
    case ISARC_QUERY_ENCRYPTION:
        return TRUE;
    default:
        return FALSE;
    }
}

WORD WINAPI ZipQueryEncryption(void)
{
    ZpVer30 v;
    memset(&v, 0, sizeof(v));
    v.structlen = (ulg)sizeof(v);
    ZpVersion(&v);
    return (WORD)(v.fEncryption ? 1 : 0);
}
