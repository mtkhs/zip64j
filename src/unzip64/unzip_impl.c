/*
 * unzip_impl.c - Implementation of the UnZip* exports.
 *
 * UnZip() parses a command line and delegates to Wiz_SingleEntryUnzip (from
 * the statically-linked unzip60). The archive-handle API family
 * (UnZipOpenArchive / UnZipFindFirst / accessors) is backed by a self-
 * contained central-directory parser in arc_cdparse.c + arc_handle.c — it
 * does not depend on any unzip60 global state, so multiple HARCs can
 * coexist safely.
 */

#include "../../include/zip64j.h"
#include "unzip60_types.h"
#include "arc_handle.h"
#include "arc_cdparse.h"
#include "../common/debug_log.h"
#include "../zip64j/cmdline.h"

#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* unzip60's windll.c sets this in DllMain. */
extern HANDLE hInst;

/* ---- Per-invocation state (guarded by s_cs) ---- */

static CRITICAL_SECTION s_cs;
static INIT_ONCE s_cs_once = INIT_ONCE_STATIC_INIT;

static BOOL   s_running = FALSE;
static LPSTR  s_output_cursor = NULL;
static DWORD  s_output_left   = 0;
static char   s_password[256]    = {0};  /* captured from -P<pwd> */
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

/* ---- Callbacks consumed by unzip60's Wiz_Init ---- */

static int WINAPI cb_print(LPSTR buf, unsigned long size)
{
    DWORD to_copy;
    if (s_output_cursor == NULL || s_output_left <= 1) return (int)size;
    to_copy = (s_output_left - 1 < size) ? (s_output_left - 1) : (DWORD)size;
    memcpy(s_output_cursor, buf, to_copy);
    s_output_cursor[to_copy] = '\0';
    s_output_cursor += to_copy;
    s_output_left   -= to_copy;
    return (int)size;
}

static void WINAPI cb_sound(void) { }

/* Return IDM_REPLACE_NO so unzip60 skips files that already exist unless
 * -o (noflag) was passed. noflag overrides this callback entirely inside
 * unzip60 so we don't need to distinguish here. */
#define CB_REPLACE_NO 100
static int WINAPI cb_replace(LPSTR efnam, unsigned efbufsiz)
{
    (void)efnam; (void)efbufsiz;
    return CB_REPLACE_NO;
}

/* Info-ZIP password callback. Returns IZ_PW_ENTERED(0) when we have a
 * pre-supplied password from `-P<pwd>`, else IZ_PW_CANCEL(-1) to skip. */
#define IZ_PW_ENTERED_K  0
#define IZ_PW_CANCEL_K  (-1)
static int WINAPI cb_password(LPSTR pwbuf, int bufsiz, LPCSTR promptmsg, LPCSTR entryname)
{
    size_t n;
    (void)promptmsg; (void)entryname;
    if (!s_password_present || bufsiz <= 0 || !pwbuf) return IZ_PW_CANCEL_K;
    n = strlen(s_password);
    if (n + 1 > (size_t)bufsiz) n = (size_t)bufsiz - 1;
    memcpy(pwbuf, s_password, n);
    pwbuf[n] = '\0';
    return IZ_PW_ENTERED_K;
}

static void WINAPI cb_message(z_uint8_mirror ucsize, z_uint8_mirror csize,
                              unsigned cfactor,
                              unsigned mo, unsigned dy, unsigned yr,
                              unsigned hh, unsigned mm,
                              char c, LPCSTR filename, LPCSTR methbuf,
                              unsigned long crc, char fCrypt)
{
    (void)ucsize; (void)csize; (void)cfactor;
    (void)mo; (void)dy; (void)yr; (void)hh; (void)mm;
    (void)c; (void)filename; (void)methbuf; (void)crc; (void)fCrypt;
}

static int WINAPI cb_service(LPCSTR entryname, z_uint8_mirror uncomprsiz)
{
    (void)entryname; (void)uncomprsiz;
    return 0;
}

/* ---- Command-line parser ----
 *
 * Target: unzip32.dll's UNZIP32D.TXT spec (統合アーカイバAPI仕様 UnZip side).
 * Grammar:
 *
 *   [-<command>] [[-<options>...] <archive_file_name>[.ZIP]
 *        [<directory_name>\] [[@<list_name>|<filespec>]...]
 *
 * Command tokens (whole-token match, mutually exclusive; default '-x'):
 *   -c   contents → szOutput (with filename)
 *   -f   freshen (existing files only, if newer)
 *   -l   list (UNLHA32 style)
 *   -lv  list (UNZIP style)
 *   -p   contents → szOutput (no filename)
 *   -t   integrity test
 *   -u   update (existing + new, if newer)
 *   -v   list (UNZIP full style)
 *   -x   extract (default)
 *   -xv  extract (UNZIP standard form)
 *   -z   display zipfile comment
 *   -Z   display zipfile comment via MessageBox
 *
 * Option tokens (may cluster; some are negatable with `--X`):
 *   -a / -C / -i / -j / -n / -o / -s / -U / -V / -X
 *   -P<password>  (rest of token is value, never clustered)
 *   -q / -qq / -qd
 *   -qe.., -qs.., -qU.., -qc.., -qr..  (extended warning-policy sub-options)
 *   -d <dir>       Info-ZIP extract dir (extension, not in spec)
 *
 * A positional arg ending in `\` or `/` = extract directory.
 * After the first non-flag positional, no further -flag tokens are recognised
 * (per spec: "ファイル名以降ではコマンドやオプション指定は使用出来ません").
 *
 * The classified command letter is stored in *cmd_out (upper for 'Z'),
 * with *verbose_out distinguishing -xv/-lv from -x/-l.
 *
 * On success: *arcname_out / *include_out become caller-owned pointers.
 * *exclude_out stays empty under current grammar but exists for the
 * Wiz_SingleEntryUnzip signature. */

/* Map an Info-ZIP unzip60 PK_ / IZ_ return code to a 統合アーカイバAPI仕様 0x8000+
 * error code. PK_OK / PK_COOL (0) and PK_WARN (1, a non-fatal warning) pass
 * through as 0 because the spec has no "warning" concept in the UnZip()
 * return value. */
static int map_pk_to_spec(int pk)
{
    switch (pk) {
    case 0:  /* PK_OK / PK_COOL */
    case 1:  /* PK_WARN  — treat warnings as success */
        return 0;
    case 2:  /* PK_ERR    — error in zipfile */
    case 3:  /* PK_BADERR — severe error in zipfile */
        return ERROR_HEADER_BROKEN;
    case 4:  /* PK_MEM  */
    case 5:  /* PK_MEM2 */
    case 6:  /* PK_MEM3 */
    case 7:  /* PK_MEM4 */
    case 8:  /* PK_MEM5 */
        return ERROR_ENOUGH_MEMORY;
    case 9:  /* PK_NOZIP  — zipfile not found */
        return ERROR_NOT_FIND_ARC_FILE;
    case 10: /* PK_PARAM  — bad parameters */
        return ERROR_COMMAND_NAME;
    case 11: /* PK_FIND   — no files matched */
        return ERROR_NOT_EXIST;
    case 50: /* PK_DISK   — disk full */
        return ERROR_DISK_SPACE;
    case 51: /* PK_EOF    — unexpected EOF */
        return ERROR_HEADER_BROKEN;
    case 80: /* IZ_CTRLC  — user aborted */
        return ERROR_USER_CANCEL;
    case 81: /* IZ_UNSUP  — unsupported compression/encryption */
        return ERROR_METHOD;
    case 82: /* IZ_BADPWD — bad password */
        return ERROR_PASSWORD_FILE;
    case 83: /* IZ_ERRBF  — big-file archive, small-file program */
        return ERROR_HEADER_BROKEN;
    default:
        /* Any non-zero leftover: synthesise a generic "header broken"
         * rather than leak an internal code. */
        return pk ? ERROR_HEADER_BROKEN : 0;
    }
}

/* Resolve archive filename per unzip32 spec: if the given name does not
 * name an existing regular file, try appending `.ZIP` then `.zip`. The
 * caller owns *arcname_ptr; on a successful rename we free the old buffer
 * and replace it with a freshly-allocated one. Silently no-ops when the
 * file already exists or no suffixed variant is found (propagates the
 * original name so Wiz_SingleEntryUnzip fails naturally). */
static int file_is_regular(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void resolve_archive_name(char **arcname_ptr)
{
    const char *suffixes[] = { ".ZIP", ".zip", NULL };
    char       *orig = *arcname_ptr;
    size_t      olen;
    int         i;

    if (!orig) return;
    if (file_is_regular(orig)) return;

    olen = strlen(orig);
    for (i = 0; suffixes[i]; i++) {
        size_t slen = strlen(suffixes[i]);
        char  *trial = (char *)malloc(olen + slen + 1);
        if (!trial) return;
        memcpy(trial, orig, olen);
        memcpy(trial + olen, suffixes[i], slen + 1);
        if (file_is_regular(trial)) {
            zip64j_log("  resolve_archive_name: \"%s\" -> \"%s\"", orig, trial);
            free(orig);
            *arcname_ptr = trial;
            return;
        }
        free(trial);
    }
}

/* Whole-token command match. Returns 1 and fills *cmd/*verbose on match. */
static int classify_command(const char *tok_after_dash, int *cmd, int *verbose)
{
    if (!tok_after_dash) return 0;
    *verbose = 0;
    /* Order matters: check 2-char forms before 1-char. */
    if (strcmp(tok_after_dash, "xv") == 0) { *cmd = 'x'; *verbose = 1; return 1; }
    if (strcmp(tok_after_dash, "lv") == 0) { *cmd = 'l'; *verbose = 1; return 1; }
    if (tok_after_dash[0] != '\0' && tok_after_dash[1] == '\0') {
        switch (tok_after_dash[0]) {
        case 'c': case 'f': case 'l': case 'p': case 't':
        case 'u': case 'v': case 'x': case 'z': case 'Z':
            *cmd = tok_after_dash[0];
            return 1;
        }
    }
    return 0;
}

/* Apply a single-char option to DCL_60. `negate` = 1 when the token started
 * with `--` (unzip32 spec: `--X` negates `-X`). */
static void apply_single_option(DCL_60 *dcl, char ch, int negate, const char *ctx)
{
    switch (ch) {
    case 'o': dcl->noflag = negate ? 0 : 1; break;     /* overwrite w/o prompt */
    case 'n': dcl->noflag = negate ? 1 : 0; break;     /* never overwrite */
    case 'j': dcl->ndflag = negate ? 1 : 0; break;     /* junk paths */
    case 'q': dcl->fQuiet = negate ? 0 : 1; break;
    case 'C': dcl->C_flag = negate ? 0 : 1; break;     /* case-insensitive regex */
    case 'a': dcl->naflag = negate ? 0 : 1; break;     /* MS-DOS text conversion */
    case 'i':  /* progress dialog — we never show one, so no-op either way */
    case 's':  /* keep spaces in filenames (default) — always on */
    case 'U':  /* keep uppercase (no-op for unzip dll per spec) */
    case 'V':  /* keep VMS version numbers (default) */
    case 'X':  /* VMS reserved */
        break;
    default:
        if (!(ch >= '0' && ch <= '9')) {
            zip64j_log("  parse_cmdline: ignoring unknown flag char '%c' in \"%s\"",
                       ch, ctx ? ctx : "?");
        }
        break;
    }
}

static int parse_cmdline(DCL_60 *dcl, const char *cmdline,
                         char **arcname_out,
                         char ***include_out, int *include_cnt_out,
                         char ***exclude_out, int *exclude_cnt_out,
                         char *extract_dir_buf, size_t extract_dir_buf_size,
                         int *cmd_out, int *verbose_out,
                         char *password_buf, size_t password_buf_size,
                         int *password_present_out)
{
    char **argv = NULL;
    int argc;
    int i;
    int rc = -1;
    char *arcname = NULL;
    char **inc = NULL;
    int inc_cnt = 0;
    char **exc = NULL;
    int exc_cnt = 0;
    int cmd = 'x';         /* spec default */
    int cmd_set = 0;
    int cmd_verbose = 0;
    int filename_seen = 0; /* lock flag parsing after first positional */

    memset(dcl, 0, sizeof(*dcl));
    dcl->StructVersID = UZ_DCL_STRUCTVER_60;
    dcl->ndflag = 1;  /* "safe" path usage by default */
    dcl->U_flag = 0;  /* UTF-8 auto */

    argv = zip64j_split_cmdline_with_response(cmdline);
    if (!argv) return -1;

    argc = zip64j_ptrarraylen((void **)argv);
    if (argc < 1) goto done;

    inc = (char **)malloc(sizeof(char *) * (argc + 1));
    exc = (char **)malloc(sizeof(char *) * (argc + 1));
    if (!inc || !exc) goto done;

    for (i = 0; i < argc; i++) {
        char *arg = argv[i];
        size_t alen = strlen(arg);
        int is_flag;
        int negate = 0;
        const char *body;

        zip64j_log("  parse_cmdline arg[%d]=\"%s\"", i, arg);

        /* Spec says "ファイル名以降ではコマンドやオプションは使用できません",
         * but Info-ZIP tradition (and many real callers) place `-d <dir>`
         * after the archive. We enforce the command restriction strictly —
         * `-x/-l/-t/...` only before the first filename — while continuing
         * to accept -options in any position. */
        is_flag = (arg[0] == '-' && alen > 1);

        /* Positional first — `\`/`/`-terminated positional wins before
         * arcname assignment, same as unified-archiver convention. */
        if (!is_flag && alen > 0 &&
            (arg[alen - 1] == '\\' || arg[alen - 1] == '/') &&
            dcl->lpszExtractDir == NULL) {
            strncpy(extract_dir_buf, arg, extract_dir_buf_size - 1);
            extract_dir_buf[extract_dir_buf_size - 1] = '\0';
            dcl->lpszExtractDir = extract_dir_buf;
            filename_seen = 1;  /* extract dir counts as positional */
            continue;
        }

        if (is_flag) {
            body = arg + 1;
            if (*body == '-') { negate = 1; body++; }

            /* Bare "-" or "--" — unzip32 spec doesn't define either; ignore. */
            if (*body == '\0') {
                zip64j_log("  parse_cmdline: ignoring bare \"%s\"", arg);
                continue;
            }

            /* Command (whole-token) — only before any filename and only if
             * the whole body matches. Negation (`--X`) on a command form is
             * not defined; treat as option parsing fallthrough. */
            if (!negate && !cmd_set && !filename_seen) {
                int c, v;
                if (classify_command(body, &c, &v)) {
                    cmd = c;
                    cmd_verbose = v;
                    cmd_set = 1;
                    continue;
                }
            }

            /* Info-ZIP-style `-d <dir>` extension (not in unzip32 spec). */
            if (!negate && strcmp(body, "d") == 0) {
                if (i + 1 >= argc) {
                    zip64j_log("  parse_cmdline: -d missing argument");
                    goto done;
                }
                i++;
                strncpy(extract_dir_buf, argv[i], extract_dir_buf_size - 1);
                extract_dir_buf[extract_dir_buf_size - 1] = '\0';
                dcl->lpszExtractDir = extract_dir_buf;
                continue;
            }

            /* -P<password>: rest of token is password, never clustered.
             * Empty password (`-P`) is treated as "no password". */
            if (!negate && body[0] == 'P') {
                const char *pw = body + 1;
                if (password_buf && password_buf_size > 0) {
                    size_t plen = strlen(pw);
                    if (plen >= password_buf_size) plen = password_buf_size - 1;
                    memcpy(password_buf, pw, plen);
                    password_buf[plen] = '\0';
                    if (password_present_out) *password_present_out = (plen > 0);
                }
                zip64j_log("  parse_cmdline: -P password captured (len=%d)",
                           (int)strlen(pw));
                continue;
            }

            /* -q / -qq / -qd / -qe... / -qs... / -qU... / -qc... / -qr... */
            if (!negate && body[0] == 'q') {
                if (body[1] == '\0' || body[1] == 'q' || body[1] == 'd') {
                    dcl->fQuiet = 1;
                } else {
                    /* Extended warning-policy sub-options: recognised so the
                     * cluster parser doesn't reject them, but unzip60 has no
                     * equivalent knobs — we log and ignore. */
                    zip64j_log("  parse_cmdline: -%s deferred (extended)", body);
                }
                continue;
            }

            /* Generic single-char option cluster. */
            {
                const char *p;
                for (p = body; *p; p++) {
                    apply_single_option(dcl, *p, negate, arg);
                }
            }
            continue;
        }

        /* Non-flag positional. First → archive, rest → include patterns. */
        filename_seen = 1;
        if (arcname == NULL) {
            size_t len = alen + 1;
            arcname = (char *)malloc(len);
            if (!arcname) goto done;
            memcpy(arcname, arg, len);
        } else {
            size_t len = alen + 1;
            char *dup = (char *)malloc(len);
            if (!dup) goto done;
            memcpy(dup, arg, len);
            inc[inc_cnt++] = dup;
        }
    }

    inc[inc_cnt] = NULL;
    exc[exc_cnt] = NULL;

    dcl->lpszZipFN = arcname;
    *arcname_out   = arcname;  arcname = NULL;
    *include_out   = inc;      inc     = NULL;
    *include_cnt_out = inc_cnt;
    *exclude_out   = exc;      exc     = NULL;
    *exclude_cnt_out = exc_cnt;
    if (cmd_out)     *cmd_out     = cmd;
    if (verbose_out) *verbose_out = cmd_verbose;
    rc = 0;

done:
    free(arcname);
    if (inc) { for (i = 0; i < inc_cnt; i++) free(inc[i]); free(inc); }
    if (exc) { for (i = 0; i < exc_cnt; i++) free(exc[i]); free(exc); }
    free(argv);
    return rc;
}

/* ---- Exports ---- */

WORD WINAPI UnZipGetVersion(void)
{
    VS_FIXEDFILEINFO *vfi;
    HRSRC   hres;
    HGLOBAL hmem;
    void   *raw;
    WORD    ver;

    hres = FindResourceA((HMODULE)hInst, MAKEINTRESOURCEA(VS_VERSION_INFO),
                         MAKEINTRESOURCEA(16 /* RT_VERSION */));
    if (!hres) return 0x0100;
    hmem = LoadResource((HMODULE)hInst, hres);
    if (!hmem) return 0x0100;
    raw = LockResource(hmem);
    if (!raw) return 0x0100;
    /* See ZipGetVersion in zip_impl.c for the offset-40 rationale. */
    vfi = (VS_FIXEDFILEINFO *)((char *)raw + 40);
    ver = (WORD)(((vfi->dwFileVersionMS >> 16) * 100) +
                 (vfi->dwFileVersionMS & 0xFFFF));
    if (ver == 0) ver = 0x0100;
    return ver;
}

int WINAPI UnZip(HWND hwnd, LPCSTR szCmdLine, LPSTR szOutput, DWORD dwSize)
{
    USERFUNCTIONS_60 funcs;
    DCL_60 dcl;
    char   extract_dir_buf[MAX_PATH] = "";
    char   password_buf[sizeof(s_password)] = "";
    int    password_present = 0;
    char  *arcname = NULL;
    char **inc_files = NULL;
    int    inc_cnt = 0;
    char **exc_files = NULL;
    int    exc_cnt = 0;
    int    rc = -1;
    int    parse_rc;
    int    cmd = 'x';
    int    cmd_verbose = 0;
    int    i;

    (void)hwnd;

    zip64j_log("UnZip() enter cmdline=\"%s\"", szCmdLine ? szCmdLine : "(null)");

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

    parse_rc = parse_cmdline(&dcl, szCmdLine ? szCmdLine : "",
                             &arcname, &inc_files, &inc_cnt,
                             &exc_files, &exc_cnt,
                             extract_dir_buf, sizeof(extract_dir_buf),
                             &cmd, &cmd_verbose,
                             password_buf, sizeof(password_buf),
                             &password_present);
    if (parse_rc != 0 || dcl.lpszZipFN == NULL) {
        zip64j_log("UnZip() parse_cmdline failed parse_rc=%d arcname=%s",
                   parse_rc, dcl.lpszZipFN ? dcl.lpszZipFN : "(null)");
        rc = ERROR_COMMAND_NAME;
        goto cleanup;
    }

    /* `.ZIP` auto-append per unzip32 spec: if the file doesn't exist as
     * given, try `.ZIP` then `.zip`. Keep arcname and dcl.lpszZipFN in sync. */
    resolve_archive_name(&arcname);
    dcl.lpszZipFN = arcname;

    zip64j_log("UnZip() parsed: cmd='%c' verbose=%d arc=\"%s\" extract_dir=\"%s\" "
               "noflag=%d ndflag=%d fQuiet=%d ntflag=%d nvflag=%d nfflag=%d "
               "C_flag=%d inc_cnt=%d exc_cnt=%d",
               cmd, cmd_verbose,
               arcname ? arcname : "(null)",
               dcl.lpszExtractDir ? dcl.lpszExtractDir : "(null)",
               dcl.noflag, dcl.ndflag, dcl.fQuiet, dcl.ntflag, dcl.nvflag,
               dcl.nfflag, dcl.C_flag, inc_cnt, exc_cnt);
    for (i = 0; i < inc_cnt; i++) zip64j_log("  inc[%d]=\"%s\"", i, inc_files[i]);
    for (i = 0; i < exc_cnt; i++) zip64j_log("  exc[%d]=\"%s\"", i, exc_files[i]);

    /* ---- Command dispatch ----
     * Maps the classified cmd to the right DCL_60 flag combination, or
     * returns ERROR_NOT_SUPPORT for commands we haven't wired yet (c/p/z/Z).
     * Verbose variants (-xv / -lv) share the same flags as their base — the
     * only difference in unzip32 is the progress output format, which we
     * don't differentiate at the DCL level. */
    switch (cmd) {
    case 'x':  /* extract (default + -xv) */
        break;
    case 't':  /* integrity test */
        dcl.ntflag = 1;
        break;
    case 'l':  /* list UNLHA32 style (-l, -lv) */
    case 'v':  /* list UNZIP full style (-v) */
        dcl.nvflag = 1;
        break;
    case 'u':  /* update (existing + new, if newer) */
        dcl.ExtractOnlyNewer = 1;
        break;
    case 'f':  /* freshen (existing files only, if newer) */
        dcl.nfflag = 1;
        dcl.ExtractOnlyNewer = 1;
        break;
    case 'c':  /* contents → szOutput w/ filename */
    case 'p':  /* contents → szOutput w/o filename */
    case 'z':  /* zipfile comment */
    case 'Z':  /* zipfile comment via MessageBox */
        zip64j_log("UnZip() cmd='%c' not implemented — returning ERROR_NOT_SUPPORT", cmd);
        rc = ERROR_NOT_SUPPORT;
        goto cleanup;
    default:
        /* classify_command only emits the 10 letters above; anything else
         * is a parser bug. Fail loudly. */
        zip64j_log("UnZip() internal: unexpected cmd='%c'", cmd);
        rc = ERROR_COMMAND_NAME;
        goto cleanup;
    }

    memset(&funcs, 0, sizeof(funcs));
    funcs.print    = cb_print;
    funcs.sound    = cb_sound;
    funcs.replace  = cb_replace;
    funcs.password = cb_password;
    funcs.SendApplicationMessage = cb_message;
    funcs.ServCallBk             = cb_service;

    /* Publish password for cb_password. s_cs is already held as "running",
     * so concurrent UnZip() callers are blocked. */
    if (password_present) {
        size_t n = strlen(password_buf);
        if (n >= sizeof(s_password)) n = sizeof(s_password) - 1;
        memcpy(s_password, password_buf, n);
        s_password[n] = '\0';
        s_password_present = TRUE;
    } else {
        s_password[0] = '\0';
        s_password_present = FALSE;
    }

    {
        int pk = Wiz_SingleEntryUnzip(inc_cnt, inc_files, exc_cnt, exc_files,
                                      &dcl, &funcs);
        rc = map_pk_to_spec(pk);
        zip64j_log("UnZip() Wiz_SingleEntryUnzip pk=%d -> rc=0x%04X", pk, rc);
    }

cleanup:
    free(arcname);
    if (inc_files) {
        for (i = 0; i < inc_cnt; i++) free(inc_files[i]);
        free(inc_files);
    }
    if (exc_files) {
        for (i = 0; i < exc_cnt; i++) free(exc_files[i]);
        free(exc_files);
    }

    /* Scrub password (both local and static) on every exit. */
    SecureZeroMemory(password_buf, sizeof(password_buf));
    EnterCriticalSection(&s_cs);
    SecureZeroMemory(s_password, sizeof(s_password));
    s_password_present = FALSE;
    s_running       = FALSE;
    s_output_cursor = NULL;
    s_output_left   = 0;
    LeaveCriticalSection(&s_cs);

    zip64j_log("UnZip() exit rc=%d output=\"%s\"",
               rc, (szOutput && dwSize > 0) ? szOutput : "(n/a)");
    return rc;
}

int WINAPI UnZipW(HWND hwnd, LPCWSTR szCmdLine, LPWSTR szOutput, DWORD dwSize)
{
    /* Info-ZIP unzip60 only consumes ANSI paths, so the W entry point
     * round-trips through CP_ACP (CP932 on JP Windows). Characters outside
     * CP_ACP are lossy. */
    char  *cmd_a = NULL, *out_a = NULL;
    int    cmd_len_a, rc = -1;

    zip64j_log("UnZipW() enter");
    if (szOutput && dwSize > 0) szOutput[0] = L'\0';
    if (!szCmdLine) return -1;

    /* Ask WideCharToMultiByte for the required buffer size (includes NUL). */
    cmd_len_a = WideCharToMultiByte(CP_ACP, 0, szCmdLine, -1, NULL, 0, NULL, NULL);
    if (cmd_len_a <= 0) return -1;
    cmd_a = (char *)malloc((size_t)cmd_len_a);
    if (!cmd_a) return -1;
    WideCharToMultiByte(CP_ACP, 0, szCmdLine, -1, cmd_a, cmd_len_a, NULL, NULL);

    if (szOutput && dwSize > 0) {
        /* CP932 encodes ≤ 2 bytes per WCHAR, so dwSize*2+1 is a safe upper bound. */
        DWORD out_bytes = dwSize * 2 + 1;
        out_a = (char *)malloc(out_bytes);
        if (out_a) {
            out_a[0] = '\0';
            rc = UnZip(hwnd, cmd_a, out_a, out_bytes);
            MultiByteToWideChar(CP_ACP, 0, out_a, -1, szOutput, (int)dwSize);
            szOutput[dwSize - 1] = L'\0';
        }
    } else {
        rc = UnZip(hwnd, cmd_a, NULL, 0);
    }

    free(cmd_a);
    free(out_a);
    return rc;
}

BOOL WINAPI UnZipGetRunning(void) { return s_running; }

BOOL WINAPI UnZipQueryFunctionList(int iFunction)
{
    /* TRUE only for APIs actually implemented and exported (see unzip64.def).
     * The 統合アーカイバ spec requires callers (e.g. afxw) to query before
     * using any API, so false positives must be avoided. */
    switch (iFunction) {
    /* Common */
    case ISARC:                          /* UnZip() cmdline */
    case ISARC_GET_VERSION:
    case ISARC_GET_RUNNING:

    /* Archive operations */
    case ISARC_CHECK_ARCHIVE:
    case ISARC_GET_FILE_COUNT:
    case ISARC_QUERY_FUNCTION_LIST:
    case ISARC_OPEN_ARCHIVE:
    case ISARC_CLOSE_ARCHIVE:
    case ISARC_FIND_FIRST:
    case ISARC_FIND_NEXT:
    case ISARC_SET_OWNER_WINDOW:
    case ISARC_CLEAR_OWNER_WINDOW:
    case ISARC_SET_OWNER_WINDOW_EX:
    case ISARC_KILL_OWNER_WINDOW_EX:

    /* Archive-level info */
    case ISARC_GET_ARC_FILE_SIZE:
    case ISARC_GET_ARC_ORIGINAL_SIZE:
    case ISARC_GET_ARC_COMPRESSED_SIZE:

    /* Entry-level info */
    case ISARC_GET_FILE_NAME:
    case ISARC_GET_ORIGINAL_SIZE:
    case ISARC_GET_COMPRESSED_SIZE:
    case ISARC_GET_RATIO:
    case ISARC_GET_DATE:
    case ISARC_GET_TIME:
    case ISARC_GET_CRC:
    case ISARC_GET_ATTRIBUTE:
    case ISARC_GET_OS_TYPE:
    case ISARC_GET_METHOD:
    case ISARC_GET_WRITE_TIME_EX:
    case ISARC_GET_CREATE_TIME_EX:
    case ISARC_GET_ACCESS_TIME_EX:

    /* zip64j / unzip64 独自拡張 (Ex / 64) */
    case ISARC_OPEN_ARCHIVE2:
    case ISARC_GET_ORIGINAL_SIZE_EX:
    case ISARC_GET_COMPRESSED_SIZE_EX:
    case ISARC_GET_WRITE_TIME_64:
    case ISARC_GET_CREATE_TIME_64:
    case ISARC_GET_ACCESS_TIME_64:
        return TRUE;
    default:
        return FALSE;
    }
}

/* ---- Archive-handle API (backed by arc_handle / arc_cdparse) ---- */

HARC WINAPI UnZipOpenArchive(HWND hwnd, LPCSTR szFileName, DWORD dwMode)
{
    zip_arc_t *arc;
    zip64j_log("UnZipOpenArchive(\"%s\", mode=0x%lx)",
               szFileName ? szFileName : "(null)", (unsigned long)dwMode);
    arc = zip_arc_open(szFileName, hwnd, dwMode);
    zip64j_log("UnZipOpenArchive -> %p (count=%u)",
               (void *)arc, arc ? (unsigned)arc->count : 0u);
    return (HARC)arc;
}

HARC WINAPI UnZipOpenArchiveW(HWND hwnd, LPCWSTR szFileName, DWORD dwMode)
{
    char ansi[MAX_PATH];
    if (!szFileName) return NULL;
    if (WideCharToMultiByte(CP_ACP, 0, szFileName, -1, ansi, sizeof(ansi), NULL, NULL) == 0) {
        zip64j_log("UnZipOpenArchiveW: W->A conversion failed");
        return NULL;
    }
    zip64j_log("UnZipOpenArchiveW -> A path \"%s\"", ansi);
    return UnZipOpenArchive(hwnd, ansi, dwMode);
}

HARC WINAPI UnZipOpenArchive2(HWND hwnd, LPCSTR szFileName, DWORD dwMode, void *pReserved)
{
    (void)pReserved;
    zip64j_log("UnZipOpenArchive2 -> OpenArchive");
    return UnZipOpenArchive(hwnd, szFileName, dwMode);
}

int WINAPI UnZipCloseArchive(HARC harc)
{
    zip_arc_t *arc = zip_arc_validate(harc);
    if (!arc) return -1;
    zip_arc_close(arc);
    return 0;
}

int WINAPI UnZipFindFirst(HARC harc, LPCSTR szWildName, LPINDIVIDUALINFO lpInfo)
{
    zip_arc_t *arc = zip_arc_validate(harc);
    int rc;
    if (!arc || !lpInfo) return -1;
    rc = zip_arc_find_first(arc, szWildName, lpInfo);
    zip64j_log("UnZipFindFirst(\"%s\") -> rc=%d hit=\"%s\"",
               szWildName ? szWildName : "(null)", rc,
               (rc == 0) ? lpInfo->szFileName : "");
    return rc;
}

int WINAPI UnZipFindFirstW(HARC harc, LPCWSTR szWildName, LPINDIVIDUALINFOW lpInfo)
{
    zip_arc_t *arc = zip_arc_validate(harc);
    if (!arc || !lpInfo) return -1;
    return zip_arc_find_first_w(arc, szWildName, lpInfo);
}

int WINAPI UnZipFindNext(HARC harc, LPINDIVIDUALINFO lpInfo)
{
    zip_arc_t *arc = zip_arc_validate(harc);
    if (!arc || !lpInfo) return -1;
    return zip_arc_find_next(arc, lpInfo);
}

int WINAPI UnZipFindNextW(HARC harc, LPINDIVIDUALINFOW lpInfo)
{
    zip_arc_t *arc = zip_arc_validate(harc);
    if (!arc || !lpInfo) return -1;
    return zip_arc_find_next_w(arc, lpInfo);
}

int WINAPI UnZipGetFileCount(LPCSTR szFileName)
{
    zip_arc_t *arc = zip_arc_open(szFileName, NULL, 0);
    int count;
    if (!arc) return 0;
    count = (int)arc->count;
    zip_arc_close(arc);
    return count;
}

int WINAPI UnZipCheckArchive(LPCSTR szFileName, int iMode)
{
    /* Light-weight check: can we read a CD? iMode (0: quick / 1: full) is
     * not meaningfully distinguished yet — we always do CD-level parse. */
    zip_arc_t *arc;
    (void)iMode;
    zip64j_log("UnZipCheckArchive(\"%s\", iMode=%d)",
               szFileName ? szFileName : "(null)", iMode);
    arc = zip_arc_open(szFileName, NULL, 0);
    if (!arc) {
        zip64j_log("UnZipCheckArchive -> 0 (open failed)");
        return 0;
    }
    zip_arc_close(arc);
    zip64j_log("UnZipCheckArchive -> 1");
    return 1;
}

/* ---- Per-entry accessors (look at the entry FindNext last returned) ---- */

static const char *method_name(WORD method)
{
    switch (method) {
    case 0:  return "Stored";
    case 1:  return "Shrunk";
    case 6:  return "Imploded";
    case 8:  return "Deflated";
    case 9:  return "Deflate64";
    case 12: return "BZIP2";
    case 14: return "LZMA";
    case 95: return "XZ";
    case 98: return "PPMd";
    default: return "Unknown";
    }
}

int WINAPI UnZipGetFileName(HARC h, LPSTR szBuf, int cchBuf)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc || !szBuf || cchBuf <= 0) return -1;
    e = zip_arc_current_entry(arc);
    if (!e) return -1;
    zip_arc_name_to_cp932(e, szBuf, cchBuf);
    return 0;
}

int WINAPI UnZipGetFileNameW(HARC h, LPWSTR szBuf, int cchBuf)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc || !szBuf || cchBuf <= 0) return -1;
    e = zip_arc_current_entry(arc);
    if (!e) return -1;
    zip_arc_name_to_utf16(e, szBuf, cchBuf);
    return 0;
}

int WINAPI UnZipGetMethod(HARC h, LPSTR szBuf, int cchBuf)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    const char *name;
    if (!arc || !szBuf || cchBuf <= 0) return -1;
    e = zip_arc_current_entry(arc);
    if (!e) return -1;
    name = method_name(e->method);
    strncpy(szBuf, name, cchBuf - 1);
    szBuf[cchBuf - 1] = '\0';
    return 0;
}

DWORD WINAPI UnZipGetOriginalSize(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc) return 0;
    e = zip_arc_current_entry(arc);
    return e ? (DWORD)(e->orig_size & 0xFFFFFFFFULL) : 0;
}

BOOL WINAPI UnZipGetOriginalSizeEx(HARC h, __int64 *pSize)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (pSize) *pSize = 0;
    if (!arc || !pSize) return FALSE;
    e = zip_arc_current_entry(arc);
    if (!e) return FALSE;
    *pSize = e->orig_size;
    return TRUE;
}

DWORD WINAPI UnZipGetCompressedSize(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc) return 0;
    e = zip_arc_current_entry(arc);
    return e ? (DWORD)(e->comp_size & 0xFFFFFFFFULL) : 0;
}

BOOL WINAPI UnZipGetCompressedSizeEx(HARC h, __int64 *pSize)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (pSize) *pSize = 0;
    if (!arc || !pSize) return FALSE;
    e = zip_arc_current_entry(arc);
    if (!e) return FALSE;
    *pSize = e->comp_size;
    return TRUE;
}

DWORD WINAPI UnZipGetArcFileSize(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    if (!arc) return 0;
    return (DWORD)(arc->arc_filesize & 0xFFFFFFFFULL);
}

BOOL WINAPI UnZipGetArcFileSizeEx(HARC h, __int64 *pSize)
{
    zip_arc_t *arc = zip_arc_validate(h);
    if (pSize) *pSize = 0;
    if (!arc || !pSize) return FALSE;
    *pSize = arc->arc_filesize;
    return TRUE;
}

DWORD WINAPI UnZipGetArcOriginalSize(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    if (!arc) return 0;
    return (DWORD)(arc->arc_orig_total & 0xFFFFFFFFULL);
}

BOOL WINAPI UnZipGetArcOriginalSizeEx(HARC h, __int64 *pSize)
{
    zip_arc_t *arc = zip_arc_validate(h);
    if (pSize) *pSize = 0;
    if (!arc || !pSize) return FALSE;
    *pSize = arc->arc_orig_total;
    return TRUE;
}

DWORD WINAPI UnZipGetArcCompressedSize(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    if (!arc) return 0;
    return (DWORD)(arc->arc_comp_total & 0xFFFFFFFFULL);
}

BOOL WINAPI UnZipGetArcCompressedSizeEx(HARC h, __int64 *pSize)
{
    zip_arc_t *arc = zip_arc_validate(h);
    if (pSize) *pSize = 0;
    if (!arc || !pSize) return FALSE;
    *pSize = arc->arc_comp_total;
    return TRUE;
}

WORD WINAPI UnZipGetRatio(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    INDIVIDUALINFO info;
    if (!arc) return 0;
    e = zip_arc_current_entry(arc);
    if (!e) return 0;
    zip_arc_populate_info(arc, &info);
    return info.wRatio;
}

WORD WINAPI UnZipGetDate(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc) return 0;
    e = zip_arc_current_entry(arc);
    return e ? e->dos_date : 0;
}

WORD WINAPI UnZipGetTime(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc) return 0;
    e = zip_arc_current_entry(arc);
    return e ? e->dos_time : 0;
}

DWORD WINAPI UnZipGetCRC(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc) return 0;
    e = zip_arc_current_entry(arc);
    return e ? e->crc32 : 0;
}

int WINAPI UnZipGetAttribute(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc) return 0;
    e = zip_arc_current_entry(arc);
    return e ? (int)(e->ext_attrs & 0xFF) : 0;
}

int WINAPI UnZipGetOSType(HARC h)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (!arc) return 0;
    e = zip_arc_current_entry(arc);
    return e ? (int)(e->version_made_by >> 8) : 0;
}

/* ---- Time accessors — derive FILETIME / 64bit from DOS date/time ---- */

static BOOL dos_to_filetime(WORD dos_date, WORD dos_time, FILETIME *pft)
{
    FILETIME local_ft;
    if (!DosDateTimeToFileTime(dos_date, dos_time, &local_ft)) return FALSE;
    return LocalFileTimeToFileTime(&local_ft, pft);
}

static BOOL entry_to_filetime(HARC h, FILETIME *pft)
{
    zip_arc_t *arc = zip_arc_validate(h);
    const zip_cd_entry_t *e;
    if (pft) { pft->dwLowDateTime = 0; pft->dwHighDateTime = 0; }
    if (!arc || !pft) return FALSE;
    e = zip_arc_current_entry(arc);
    if (!e) return FALSE;
    return dos_to_filetime(e->dos_date, e->dos_time, pft);
}

BOOL WINAPI UnZipGetWriteTimeEx (HARC h, FILETIME *pFT) { return entry_to_filetime(h, pFT); }
BOOL WINAPI UnZipGetCreateTimeEx(HARC h, FILETIME *pFT) { return entry_to_filetime(h, pFT); }
BOOL WINAPI UnZipGetAccessTimeEx(HARC h, FILETIME *pFT) { return entry_to_filetime(h, pFT); }

static BOOL entry_to_int64(HARC h, __int64 *pt)
{
    FILETIME ft;
    ULARGE_INTEGER ul;
    if (pt) *pt = 0;
    if (!pt) return FALSE;
    if (!entry_to_filetime(h, &ft)) return FALSE;
    ul.LowPart  = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    *pt = (__int64)ul.QuadPart;
    return TRUE;
}

BOOL WINAPI UnZipGetWriteTime64 (HARC h, __int64 *pT) { return entry_to_int64(h, pT); }
BOOL WINAPI UnZipGetCreateTime64(HARC h, __int64 *pT) { return entry_to_int64(h, pT); }
BOOL WINAPI UnZipGetAccessTime64(HARC h, __int64 *pT) { return entry_to_int64(h, pT); }

int  WINAPI UnZipSetOwnerWindow(HWND hwnd)              { (void)hwnd; return 0; }
BOOL WINAPI UnZipClearOwnerWindow(void)                 { return TRUE; }
BOOL WINAPI UnZipSetOwnerWindowEx(HWND hwnd, void *pMsg){ (void)hwnd; (void)pMsg; return TRUE; }
BOOL WINAPI UnZipKillOwnerWindowEx(HWND hwnd)           { (void)hwnd; return TRUE; }

/* ---- ZipUnZip aliases — all forward to UnZip* ---- */

int   WINAPI ZipUnZip(HWND hwnd, LPCSTR cmd, LPSTR out, DWORD n)    { return UnZip(hwnd, cmd, out, n); }
int   WINAPI ZipUnZipW(HWND hwnd, LPCWSTR cmd, LPWSTR out, DWORD n) { return UnZipW(hwnd, cmd, out, n); }
WORD  WINAPI ZipUnZipGetVersion(void)                               { return UnZipGetVersion(); }
HARC  WINAPI ZipUnZipOpenArchive(HWND h, LPCSTR fn, DWORD m)        { return UnZipOpenArchive(h, fn, m); }
HARC  WINAPI ZipUnZipOpenArchiveW(HWND h, LPCWSTR fn, DWORD m)      { return UnZipOpenArchiveW(h, fn, m); }
int   WINAPI ZipUnZipCloseArchive(HARC h)                           { return UnZipCloseArchive(h); }
int   WINAPI ZipUnZipFindFirst(HARC h, LPCSTR w, LPINDIVIDUALINFO p)   { return UnZipFindFirst(h, w, p); }
int   WINAPI ZipUnZipFindFirstW(HARC h, LPCWSTR w, LPINDIVIDUALINFOW p){ return UnZipFindFirstW(h, w, p); }
int   WINAPI ZipUnZipFindNext(HARC h, LPINDIVIDUALINFO p)             { return UnZipFindNext(h, p); }
int   WINAPI ZipUnZipFindNextW(HARC h, LPINDIVIDUALINFOW p)           { return UnZipFindNextW(h, p); }
BOOL  WINAPI ZipUnZipQueryFunctionList(int iFunction)               { return UnZipQueryFunctionList(iFunction); }
