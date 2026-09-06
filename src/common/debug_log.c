/*
 * debug_log.c - implementation of the opt-in log (see debug_log.h).
 */

#include "debug_log.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SENTINEL_NAME "zip64j_debug.on"
#define LOGFILE_NAME  "zip64j_debug.log"

/* Identifies the DLL this copy is linked into; set per target in cmake. */
#ifndef ZIP64J_LOG_TAG
#define ZIP64J_LOG_TAG "?"
#endif

/* A line is formatted whole and written with one fwrite. zip64j.dll and
 * unzip64.dll hold separate critical sections while appending to the same
 * file, so a single write is what keeps their lines from interleaving. */
#define LOG_LINE_MAX 1024

static CRITICAL_SECTION s_log_cs;
static INIT_ONCE        s_log_once = INIT_ONCE_STATIC_INIT;
static int              s_enabled  = 0;
static char             s_log_path[MAX_PATH];

static BOOL CALLBACK init_log(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    char  sentinel[MAX_PATH];
    char  tmp[MAX_PATH];
    DWORD n;

    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&s_log_cs);

    n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) return TRUE;

    if (strlen(tmp) + sizeof(SENTINEL_NAME) >= MAX_PATH) return TRUE;
    strcpy(sentinel, tmp);
    strcat(sentinel, SENTINEL_NAME);

    if (GetFileAttributesA(sentinel) == INVALID_FILE_ATTRIBUTES) return TRUE;

    if (strlen(tmp) + sizeof(LOGFILE_NAME) >= MAX_PATH) return TRUE;
    strcpy(s_log_path, tmp);
    strcat(s_log_path, LOGFILE_NAME);

    s_enabled = 1;
    return TRUE;
}

int zip64j_log_enabled(void)
{
    InitOnceExecuteOnce(&s_log_once, init_log, NULL, NULL);
    return s_enabled;
}

void zip64j_log(const char *fmt, ...)
{
    va_list    ap;
    FILE      *fp;
    SYSTEMTIME st;
    char       line[LOG_LINE_MAX];
    int        len, body;

    if (!zip64j_log_enabled()) return;

    GetLocalTime(&st);
    len = snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d %s t%lu] ",
                   st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                   ZIP64J_LOG_TAG, (unsigned long)GetCurrentThreadId());
    if (len < 0) return;
    if ((size_t)len > sizeof(line) - 2) len = (int)sizeof(line) - 2;

    va_start(ap, fmt);
    body = vsnprintf(line + len, sizeof(line) - (size_t)len - 1, fmt, ap);
    va_end(ap);
    if (body > 0) len += body;
    if ((size_t)len > sizeof(line) - 2) len = (int)sizeof(line) - 2;  /* truncated */
    line[len++] = '\n';

    EnterCriticalSection(&s_log_cs);
    fp = fopen(s_log_path, "ab");
    if (fp) {
        fwrite(line, 1, (size_t)len, fp);
        fclose(fp);
    }
    LeaveCriticalSection(&s_log_cs);
}
