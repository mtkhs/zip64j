/*
 * zip64j.h - Public header for zip64j.dll
 *
 * 統合アーカイバAPI仕様 (Unified Archiver API Specification) compliant ZIP DLL
 * for 64-bit Windows. Binary-compatible with zip32j.dll at source level
 * (same INDIVIDUALINFO layout, same ISARC_* constants), with 64-bit size /
 * time extensions ("*_Ex" / "*_64") following tar32.dll / unrar64j.dll
 * conventions, and Unicode (W-suffix) variants added throughout.
 *
 * This header is for applications that link against zip64j.dll.
 */

#ifndef ZIP64J_H_INCLUDED
#define ZIP64J_H_INCLUDED

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------- Calling convention & linkage -------- */

/* All exported entry points use WINAPI. We do NOT use __declspec(dllexport):
 * exports are controlled exclusively through zip64j.def so the external names
 * match the 統合アーカイバ spec verbatim (no name decoration / no leading _). */
#ifndef WINAPI
#  define WINAPI __stdcall
#endif

/* -------- Constants -------- */

/* File-name buffer length. Matches zip32j.dll for source-level compatibility. */
#define FNAME_MAX32  512

/* -------- ISARC_* function-query constants --------
 *
 * Numbers follow the 統合アーカイバ (Unified Archiver) spec: identical across
 * UNZIP32.H (unzip32.dll) and ZIP32J.H (zip32j.dll). Passed to
 * ZipQueryFunctionList / UnZipQueryFunctionList / ZipUnZipQueryFunctionList.
 *
 * Gaps in the numbering are intentional — they match the spec's category
 * blocks (0-15 common, 16-34 archive ops, 40-52 archive-level info,
 * 57-72 entry-level info, 80-81 enum-members callback).
 *
 * Our 64bit / Ex extensions live OUTSIDE the spec-reserved range at 85-91
 * and 100-113 to avoid future collisions if the spec grows.
 */

/* ---- Common (0-15) ---- */
#define ISARC                             0   /* Zip / UnZip (main cmdline) */
#define ISARC_GET_VERSION                 1
#define ISARC_GET_CURSOR_INTERVAL         2
#define ISARC_SET_CURSOR_INTERVAL         3
#define ISARC_GET_BACK_GROUND_MODE        4
#define ISARC_SET_BACK_GROUND_MODE        5
#define ISARC_GET_CURSOR_MODE             6
#define ISARC_SET_CURSOR_MODE             7
#define ISARC_GET_RUNNING                 8

/* ---- Archive operations (16-34) ---- */
#define ISARC_CHECK_ARCHIVE              16
#define ISARC_CONFIG_DIALOG              17
#define ISARC_GET_FILE_COUNT             18
#define ISARC_QUERY_FUNCTION_LIST        19
#define ISARC_HOUT                       20
#define ISARC_STRUCTOUT                  21
#define ISARC_GET_ARC_FILE_INFO          22
#define ISARC_OPEN_ARCHIVE               23
#define ISARC_CLOSE_ARCHIVE              24
#define ISARC_FIND_FIRST                 25
#define ISARC_FIND_NEXT                  26
#define ISARC_EXTRACT                    27
#define ISARC_ADD                        28
#define ISARC_MOVE                       29
#define ISARC_DELETE                     30
#define ISARC_SET_OWNER_WINDOW           31   /* spec: ISARC_SETOWNERWINDOW */
#define ISARC_CLEAR_OWNER_WINDOW         32   /* spec: ISARC_CLEAROWNERWINDOW */
#define ISARC_SET_OWNER_WINDOW_EX        33   /* spec: ISARC_SETOWNERWINDOWEX */
#define ISARC_KILL_OWNER_WINDOW_EX       34   /* spec: ISARC_KILLOWNERWINDOWEX */

/* ---- Archive-level info (40-52) ---- */
#define ISARC_GET_ARC_FILE_NAME          40
#define ISARC_GET_ARC_FILE_SIZE          41
#define ISARC_GET_ARC_ORIGINAL_SIZE      42
#define ISARC_GET_ARC_COMPRESSED_SIZE    43
#define ISARC_GET_ARC_RATIO              44
#define ISARC_GET_ARC_DATE               45
#define ISARC_GET_ARC_TIME               46
#define ISARC_GET_ARC_OS_TYPE            47
#define ISARC_GET_ARC_IS_SFX_FILE        48
#define ISARC_GET_ARC_CREATE_TIME_EX     50
#define ISARC_GET_ARC_ACCESS_TIME_EX     51
#define ISARC_GET_ARC_CREATE_TIME_EX2    52

/* ---- Entry-level info (57-72) ---- */
#define ISARC_GET_FILE_NAME              57
#define ISARC_GET_ORIGINAL_SIZE          58
#define ISARC_GET_COMPRESSED_SIZE        59
#define ISARC_GET_RATIO                  60
#define ISARC_GET_DATE                   61
#define ISARC_GET_TIME                   62
#define ISARC_GET_CRC                    63
#define ISARC_GET_ATTRIBUTE              64
#define ISARC_GET_OS_TYPE                65
#define ISARC_GET_METHOD                 66
#define ISARC_GET_WRITE_TIME             67
#define ISARC_GET_CREATE_TIME            68
#define ISARC_GET_ACCESS_TIME            69
#define ISARC_GET_WRITE_TIME_EX          70
#define ISARC_GET_CREATE_TIME_EX         71
#define ISARC_GET_ACCESS_TIME_EX         72

/* ---- Enum-members callback (80-81) ---- */
#define ISARC_SET_ENUM_MEMBERS_PROC      80
#define ISARC_CLEAR_ENUM_MEMBERS_PROC    81

/* ---- zip64j/unzip64 独自拡張 (spec-reserved range 外) ---- */
#define ISARC_GET_ORIGINAL_SIZE_EX       85  /* 64bit original size  */
#define ISARC_GET_COMPRESSED_SIZE_EX     86  /* 64bit compressed size */
#define ISARC_SET_OWNER_WINDOW_EX64      87  /* same number as 7-zip32.h */
#define ISARC_KILL_OWNER_WINDOW_EX64     88
#define ISARC_OPEN_ARCHIVE2              91  /* OpenArchive with reserved arg */
#define ISARC_QUERY_ENCRYPTION          100  /* zip side: encryption available? */
#define ISARC_GET_WRITE_TIME_64         111
#define ISARC_GET_CREATE_TIME_64        112
#define ISARC_GET_ACCESS_TIME_64        113

/* -------- Error codes (統合アーカイバAPI仕様 0x8000+) --------
 * Mirrors unzip32.dll's UNZIP32.H. Values are stable across 32/64-bit. */
#define ERROR_START                    0x8000

/* Warnings (0x8005..0x8011) — non-fatal, subset callers check explicitly */
#define ERROR_DISK_SPACE               0x8005
#define ERROR_READ_ONLY                0x8006
#define ERROR_USER_SKIP                0x8007
#define ERROR_UNKNOWN_TYPE             0x8008
#define ERROR_METHOD                   0x8009
#define ERROR_PASSWORD_FILE            0x800A
#define ERROR_VERSION                  0x800B
#define ERROR_FILE_CRC                 0x800C
#define ERROR_FILE_OPEN                0x800D
#define ERROR_MORE_FRESH               0x800E
#define ERROR_NOT_EXIST                0x800F
#define ERROR_ALREADY_EXIST            0x8010
#define ERROR_TOO_MANY_FILES           0x8011

/* Errors (0x8012..) */
#define ERROR_MAKEDIRECTORY            0x8012
#define ERROR_CANNOT_WRITE             0x8013
#define ERROR_HUFFMAN_CODE             0x8014
#define ERROR_COMMENT_HEADER           0x8015
#define ERROR_HEADER_CRC               0x8016
#define ERROR_HEADER_BROKEN            0x8017
#define ERROR_ARCHIVE_FILE_OPEN        0x8018
#define ERROR_NOT_ARC_FILE             0x8019
#define ERROR_CANNOT_READ              0x801A
#define ERROR_FILE_STYLE               0x801B
#define ERROR_COMMAND_NAME             0x801C  /* malformed cmdline */
#define ERROR_MORE_HEAP_MEMORY         0x801D
#define ERROR_ENOUGH_MEMORY            0x801E
#define ERROR_ALREADY_RUNNING          0x801F
#define ERROR_USER_CANCEL              0x8020
#define ERROR_HARC_ISNOT_OPENED        0x8021
#define ERROR_NOT_SEARCH_MODE          0x8022
#define ERROR_NOT_SUPPORT              0x8023  /* operation not implemented */
#define ERROR_TIME_STAMP               0x8024
#define ERROR_TMP_OPEN                 0x8025
#define ERROR_LONG_FILE_NAME           0x8026
#define ERROR_ARC_READ_ONLY            0x8027
#define ERROR_SAME_NAME_FILE           0x8028
#define ERROR_NOT_FIND_ARC_FILE        0x8029
#define ERROR_RESPONSE_READ            0x802A
#define ERROR_NOT_FILENAME             0x802B
#define ERROR_INVALID_PATH             0x8049
#define ERROR_END                      ERROR_NOT_FILENAME

/* -------- Data types -------- */

/* Handle to an opened archive. zip32j used HGLOBAL which is a typedef for
 * HANDLE on modern Windows; on x64 this is a full 64-bit handle. */
typedef HGLOBAL HARC;

/* Spec (UNZIP32.H L52-55) uses #pragma pack(1) — match verbatim for binary
 * layout compatibility with 32bit callers that rely on sizeof(INDIVIDUALINFO). */
#pragma pack(push, 1)

/* ANSI (CP932) per-entry info. Layout must match zip32j.dll for source
 * compatibility — do NOT reorder or widen members here. 64-bit sizes are
 * exposed through the separate *_Ex API. */
typedef struct {
    DWORD   dwOriginalSize;     /* 32-bit; truncated for > 4GB entries */
    DWORD   dwCompressedSize;
    DWORD   dwCRC;
    UINT    uFlag;
    UINT    uOSType;
    WORD    wRatio;
    WORD    wDate;
    WORD    wTime;
    char    szFileName[FNAME_MAX32 + 1];    /* CP932 */
    char    dummy1[3];
    char    szAttribute[8];
    char    szMode[8];
} INDIVIDUALINFO, *LPINDIVIDUALINFO;

/* Unicode (UTF-16) variant. Separate struct so ANSI layout stays stable. */
typedef struct {
    DWORD   dwOriginalSize;
    DWORD   dwCompressedSize;
    DWORD   dwCRC;
    UINT    uFlag;
    UINT    uOSType;
    WORD    wRatio;
    WORD    wDate;
    WORD    wTime;
    WCHAR   szFileName[FNAME_MAX32 + 1];
    WCHAR   dummy1[3];
    WCHAR   szAttribute[8];
    WCHAR   szMode[8];
} INDIVIDUALINFOW, *LPINDIVIDUALINFOW;

/* Extraction progress, passed with wm_arcextract. Layouts match UNZIP32.H
 * (EXTRACTINGINFO / EXTRACTINGINFOEX) and 7-zip32.h (EXTRACTINGINFOEX64). */
typedef struct {
    DWORD   dwFileSize;
    DWORD   dwWriteSize;
    char    szSourceFileName[FNAME_MAX32 + 1];  /* name stored in the archive, CP932 */
    char    dummy1[3];
    char    szDestFileName[FNAME_MAX32 + 1];    /* output path */
    char    dummy[3];
} EXTRACTINGINFO, *LPEXTRACTINGINFO;

typedef struct {
    EXTRACTINGINFO exinfo;
    DWORD   dwCompressedSize;
    DWORD   dwCRC;
    UINT    uOSType;
    WORD    wRatio;
    WORD    wDate;
    WORD    wTime;
    char    szAttribute[8];
    char    szMode[8];
} EXTRACTINGINFOEX, *LPEXTRACTINGINFOEX;

typedef struct {
    DWORD   dwStructSize;
    EXTRACTINGINFO exinfo;
    __int64 llFileSize;
    __int64 llCompressedSize;
    __int64 llWriteSize;
    DWORD   dwAttributes;
    DWORD   dwCRC;
    UINT    uOSType;
    WORD    wRatio;
    FILETIME ftCreateTime;
    FILETIME ftAccessTime;
    FILETIME ftWriteTime;
    char    szMode[8];
    char    szSourceFileName[FNAME_MAX32 + 1];
    char    dummy1[3];
    char    szDestFileName[FNAME_MAX32 + 1];
    char    dummy2[3];
} EXTRACTINGINFOEX64, *LPEXTRACTINGINFOEX64;

#pragma pack(pop)

/* -------- Progress notification -------- */

#ifndef WM_ARCEXTRACT
#  define WM_ARCEXTRACT          "wm_arcextract"  /* RegisterWindowMessage name */
#  define ARCEXTRACT_BEGIN       0   /* an entry is about to be extracted */
#  define ARCEXTRACT_INPROCESS   1   /* dwWriteSize / llWriteSize updated */
#  define ARCEXTRACT_END         2   /* once, when UnZip() finishes */
#  define ARCEXTRACT_OPEN        3
#  define ARCEXTRACT_COPY        4
#endif

/* nState is an ARCEXTRACT_* value; lpEis points to EXTRACTINGINFOEX, or to
 * EXTRACTINGINFOEX64 when registered through UnZipSetOwnerWindowEx64.
 * Return 0 to continue, non-zero to cancel (UNZIP32.DLL's default). */
typedef BOOL CALLBACK ARCHIVERPROC(HWND hwnd, UINT uMsg, UINT nState, LPVOID lpEis);
typedef ARCHIVERPROC *LPARCHIVERPROC;

/* -------- Zip API -------- */

WORD  WINAPI ZipGetVersion(void);
BOOL  WINAPI ZipGetRunning(void);
int   WINAPI Zip (HWND hwnd, LPCSTR  szCmdLine, LPSTR  szOutput, DWORD dwSize);
int   WINAPI ZipW(HWND hwnd, LPCWSTR szCmdLine, LPWSTR szOutput, DWORD dwSize);
int   WINAPI ZipConfigDialog (HWND hwnd, LPSTR  szCommandBuf, int iMode);
int   WINAPI ZipConfigDialogW(HWND hwnd, LPWSTR szCommandBuf, int iMode);
BOOL  WINAPI ZipQueryFunctionList(int iFunction);
WORD  WINAPI ZipQueryEncryption(void);

/* -------- UnZip API -------- */

int   WINAPI UnZip (HWND hwnd, LPCSTR  szCmdLine, LPSTR  szOutput, DWORD dwSize);
int   WINAPI UnZipW(HWND hwnd, LPCWSTR szCmdLine, LPWSTR szOutput, DWORD dwSize);
WORD  WINAPI UnZipGetVersion(void);
BOOL  WINAPI UnZipQueryFunctionList(int iFunction);

HARC  WINAPI UnZipOpenArchive (HWND hwnd, LPCSTR  szFileName, DWORD dwMode);
HARC  WINAPI UnZipOpenArchiveW(HWND hwnd, LPCWSTR szFileName, DWORD dwMode);
HARC  WINAPI UnZipOpenArchive2(HWND hwnd, LPCSTR  szFileName, DWORD dwMode, void *pReserved);
int   WINAPI UnZipCloseArchive(HARC harc);

int   WINAPI UnZipFindFirst (HARC harc, LPCSTR  szWildName, LPINDIVIDUALINFO  lpInfo);
int   WINAPI UnZipFindFirstW(HARC harc, LPCWSTR szWildName, LPINDIVIDUALINFOW lpInfo);
int   WINAPI UnZipFindNext  (HARC harc, LPINDIVIDUALINFO  lpInfo);
int   WINAPI UnZipFindNextW (HARC harc, LPINDIVIDUALINFOW lpInfo);

int   WINAPI UnZipGetFileCount(LPCSTR szFileName);
int   WINAPI UnZipCheckArchive(LPCSTR szFileName, int iMode);

int   WINAPI UnZipGetFileName (HARC harc, LPSTR  szBuf, int cchBuf);
int   WINAPI UnZipGetFileNameW(HARC harc, LPWSTR szBuf, int cchBuf);
int   WINAPI UnZipGetMethod   (HARC harc, LPSTR  szBuf, int cchBuf);
DWORD WINAPI UnZipGetOriginalSize   (HARC harc);
BOOL  WINAPI UnZipGetOriginalSizeEx (HARC harc, __int64 *pSize);
DWORD WINAPI UnZipGetCompressedSize   (HARC harc);
BOOL  WINAPI UnZipGetCompressedSizeEx (HARC harc, __int64 *pSize);
DWORD WINAPI UnZipGetArcFileSize   (HARC harc);
BOOL  WINAPI UnZipGetArcFileSizeEx (HARC harc, __int64 *pSize);
DWORD WINAPI UnZipGetArcOriginalSize   (HARC harc);
BOOL  WINAPI UnZipGetArcOriginalSizeEx (HARC harc, __int64 *pSize);
DWORD WINAPI UnZipGetArcCompressedSize   (HARC harc);
BOOL  WINAPI UnZipGetArcCompressedSizeEx (HARC harc, __int64 *pSize);
WORD  WINAPI UnZipGetRatio    (HARC harc);
WORD  WINAPI UnZipGetDate     (HARC harc);
WORD  WINAPI UnZipGetTime     (HARC harc);
DWORD WINAPI UnZipGetCRC      (HARC harc);
int   WINAPI UnZipGetAttribute(HARC harc);
int   WINAPI UnZipGetOSType   (HARC harc);

/* 64bit raw UNIX-epoch seconds ×10^7 (Windows FILETIME units are 100ns).
 * Convention matches tar32.dll UnrarGetWriteTime64 etc. */
BOOL  WINAPI UnZipGetWriteTime64 (HARC harc, __int64 *pTime);
BOOL  WINAPI UnZipGetCreateTime64(HARC harc, __int64 *pTime);
BOOL  WINAPI UnZipGetAccessTime64(HARC harc, __int64 *pTime);

/* FILETIME variant */
BOOL  WINAPI UnZipGetWriteTimeEx (HARC harc, FILETIME *pFileTime);
BOOL  WINAPI UnZipGetCreateTimeEx(HARC harc, FILETIME *pFileTime);
BOOL  WINAPI UnZipGetAccessTimeEx(HARC harc, FILETIME *pFileTime);

int   WINAPI UnZipSetOwnerWindow     (HWND hwnd);
BOOL  WINAPI UnZipClearOwnerWindow   (void);
BOOL  WINAPI UnZipSetOwnerWindowEx   (HWND hwnd, LPARCHIVERPROC lpArcProc);
BOOL  WINAPI UnZipKillOwnerWindowEx  (HWND hwnd);
BOOL  WINAPI UnZipSetOwnerWindowEx64 (HWND hwnd, LPARCHIVERPROC lpArcProc, DWORD dwStructSize);
BOOL  WINAPI UnZipKillOwnerWindowEx64(HWND hwnd);

/* -------- ZipUnZip aliases (forward to UnZip*) -------- */

int   WINAPI ZipUnZip (HWND hwnd, LPCSTR  szCmdLine, LPSTR  szOutput, DWORD dwSize);
int   WINAPI ZipUnZipW(HWND hwnd, LPCWSTR szCmdLine, LPWSTR szOutput, DWORD dwSize);
WORD  WINAPI ZipUnZipGetVersion(void);
HARC  WINAPI ZipUnZipOpenArchive (HWND hwnd, LPCSTR  szFileName, DWORD dwMode);
HARC  WINAPI ZipUnZipOpenArchiveW(HWND hwnd, LPCWSTR szFileName, DWORD dwMode);
int   WINAPI ZipUnZipCloseArchive(HARC harc);
int   WINAPI ZipUnZipFindFirst (HARC harc, LPCSTR  szWildName, LPINDIVIDUALINFO  lpInfo);
int   WINAPI ZipUnZipFindFirstW(HARC harc, LPCWSTR szWildName, LPINDIVIDUALINFOW lpInfo);
int   WINAPI ZipUnZipFindNext  (HARC harc, LPINDIVIDUALINFO  lpInfo);
int   WINAPI ZipUnZipFindNextW (HARC harc, LPINDIVIDUALINFOW lpInfo);
BOOL  WINAPI ZipUnZipQueryFunctionList(int iFunction);

#ifdef __cplusplus
}
#endif

#endif /* ZIP64J_H_INCLUDED */
