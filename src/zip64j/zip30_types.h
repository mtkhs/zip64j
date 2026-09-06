/*
 * zip30_types.h - Local mirror of Info-ZIP Zip 3.0's public DLL types.
 *
 * The layouts MUST match zip30/api.h verbatim for MSVC x64 with
 * ZIP64_SUPPORT enabled (see cmake/zip30.cmake).
 *
 * We avoid including zip30's api.h directly: it drags a large transitive
 * chain (tailor.h, osdep.h, zip.h ...) and zip.h defines `CR 13`, which
 * clashes with the `CR` bitfield in winnt.h on ARM64 Windows.
 */

#ifndef ZIP64J_ZIP30_TYPES_H
#define ZIP64J_ZIP30_TYPES_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zip 3.0 reuses 'far' as a no-op macro on WIN32. */
#ifndef Far
#  define Far
#endif
#ifndef _far
#  define _far
#endif

typedef unsigned char uch;
typedef unsigned long ulg;

/* ---- DLL callback types (from zip30/api.h, ZIP64_SUPPORT branch) ---- */

typedef int    (WINAPI DLLPRNT)                (LPSTR, unsigned long);
typedef int    (WINAPI DLLPASSWORD)            (LPSTR, int, LPCSTR, LPCSTR);
typedef int    (WINAPI DLLSERVICE64)           (LPCSTR, unsigned __int64);
typedef int    (WINAPI DLLSERVICE64_NO_INT64)  (LPCSTR, unsigned long, unsigned long);
typedef int    (WINAPI DLLSPLIT)               (LPSTR);
typedef LPSTR  (WINAPI DLLCOMMENT)             (LPSTR);

/* ---- ZpVer / _zip_version_type (zip30 layout with fEncryption) ---- */

typedef struct _zip_ver_30 {
    uch major;
    uch minor;
    uch patchlevel;
    uch not_used;
} _zip_version_type_30;

typedef struct _ZpVer_30 {
    ulg structlen;
    ulg flag;
    char betalevel[10];
    char date[20];
    char zlib_version[10];
    BOOL fEncryption;               /* added in zip 3.x */
    _zip_version_type_30 zip;
    _zip_version_type_30 os2dll;
    _zip_version_type_30 windll;
} ZpVer30;

/* ---- ZPOPT (zip 3.0 release layout) ---- */

typedef struct {
    LPSTR  Date;
    LPSTR  szRootDir;
    LPSTR  szTempDir;
    BOOL   fTemp;
    BOOL   fSuffix;
    BOOL   fEncrypt;
    BOOL   fSystem;
    BOOL   fVolume;
    BOOL   fExtra;
    BOOL   fNoDirEntries;
    BOOL   fExcludeDate;
    BOOL   fIncludeDate;
    BOOL   fVerbose;
    BOOL   fQuiet;
    BOOL   fCRLF_LF;
    BOOL   fLF_CRLF;
    BOOL   fJunkDir;
    BOOL   fGrow;
    BOOL   fForce;
    BOOL   fMove;
    BOOL   fDeleteEntries;
    BOOL   fUpdate;
    BOOL   fFreshen;
    BOOL   fJunkSFX;
    BOOL   fLatestTime;
    BOOL   fComment;
    BOOL   fOffsets;
    BOOL   fPrivilege;
    BOOL   fEncryption;             /* read-only */
    LPSTR  szSplitSize;
    LPSTR  szIncludeList;
    long   IncludeListCount;
    char **IncludeList;
    LPSTR  szExcludeList;
    long   ExcludeListCount;
    char **ExcludeList;
    int    fRecurse;                /* 1 => -r, 2 => -R */
    int    fRepair;                 /* 1 => -F, 2 => -FF */
    char   fLevel;                  /* '0'..'9' */
} ZPOPT30, _far *LPZPOPT30;

/* ---- ZCL ---- */

typedef struct {
    int    argc;
    LPSTR  lpszZipFN;
    char **FNV;
    LPSTR  lpszAltFNL;
} ZCL30, _far *LPZCL30;

/* ---- ZIPUSERFUNCTIONS (ZIP64_SUPPORT layout — 6 members) ---- */

typedef struct {
    DLLPRNT               *print;
    DLLCOMMENT            *comment;
    DLLPASSWORD           *password;
    DLLSPLIT              *split;                           /* MUST be NULL unless split-archive callback is wired */
    DLLSERVICE64          *ServiceApplication64;            /* 64bit progress */
    DLLSERVICE64_NO_INT64 *ServiceApplication64_No_Int64;   /* fallback when caller can't pass __int64 */
} ZIPUSERFUNCTIONS30, *LPZIPUSERFUNCTIONS30;

/* ---- Entry points (static-linked from zip30 inside this DLL).
 *
 * Declared here so zip_impl.c can call them without including zip30's api.h
 * (which drags in the zip.h macro chain — CR, etc.). The calls themselves
 * resolve to the statically-linked bodies in api.obj; the ZpVer30 /
 * ZIPUSERFUNCTIONS30 / ZCL30 / ZPOPT30 layouts above MUST stay binary-
 * identical to api.h's ZpVer / ZIPUSERFUNCTIONS / ZCL / ZPOPT. */

extern void WINAPI ZpVersion(ZpVer30 *);
extern int  WINAPI ZpInit   (LPZIPUSERFUNCTIONS30);
extern int  WINAPI ZpArchive(ZCL30, LPZPOPT30);

#ifdef __cplusplus
}
#endif

#endif /* ZIP64J_ZIP30_TYPES_H */
