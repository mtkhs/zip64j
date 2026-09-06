/*
 * unzip60_types.h - Mirror of the Info-ZIP UnZip 6.0 DLL public ABI.
 *
 * Same role as zip30_types.h: replicates the DCL / USERFUNCTIONS / callback
 * typedefs so unzip_impl.c can call Wiz_Init / Wiz_SingleEntryUnzip without
 * pulling in unzpriv.h (which redefines CR and drags in a large header chain).
 *
 * Layouts MUST stay binary-identical to windll/structs.h under
 * WIN32/WINDLL/DLL/USE_EF_UT_TIME.
 */

#ifndef UNZIP64_UNZIP60_TYPES_H
#define UNZIP64_UNZIP60_TYPES_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned __int64 z_uint8_mirror;

/* ---- Callback types ---- */

typedef int  (WINAPI DLLPRNT_60)    (LPSTR, unsigned long);
typedef int  (WINAPI DLLPASSWORD_60)(LPSTR pwbuf, int bufsiz,
                                     LPCSTR promptmsg, LPCSTR entryname);
typedef int  (WINAPI DLLSERVICE_60) (LPCSTR entryname, z_uint8_mirror uncomprsiz);
typedef int  (WINAPI DLLSERVICE_I32_60)(LPCSTR entryname,
                                        unsigned long ucsz_lo, unsigned long ucsz_hi);
typedef void (WINAPI DLLSND_60)     (void);
typedef int  (WINAPI DLLREPLACE_60) (LPSTR efnam, unsigned efbufsiz);
typedef void (WINAPI DLLMESSAGE_60) (z_uint8_mirror ucsize, z_uint8_mirror csize,
                                     unsigned cfactor,
                                     unsigned mo, unsigned dy, unsigned yr,
                                     unsigned hh, unsigned mm,
                                     char c, LPCSTR filename, LPCSTR methbuf,
                                     unsigned long crc, char fCrypt);
typedef void (WINAPI DLLMESSAGE_I32_60)(unsigned long ucsiz_l, unsigned long ucsiz_h,
                                        unsigned long csiz_l, unsigned long csiz_h,
                                        unsigned cfactor,
                                        unsigned mo, unsigned dy, unsigned yr,
                                        unsigned hh, unsigned mm,
                                        char c, LPCSTR filename, LPCSTR methbuf,
                                        unsigned long crc, char fCrypt);

/* ---- USERFUNCTIONS (Z_UINT8_DEFINED layout — __int64 totals) ---- */

typedef struct {
    DLLPRNT_60           *print;
    DLLSND_60            *sound;
    DLLREPLACE_60        *replace;
    DLLPASSWORD_60       *password;
    DLLMESSAGE_60        *SendApplicationMessage;
    DLLSERVICE_60        *ServCallBk;
    DLLMESSAGE_I32_60    *SendApplicationMessage_i32;
    DLLSERVICE_I32_60    *ServCallBk_i32;
    z_uint8_mirror        TotalSizeComp;
    z_uint8_mirror        TotalSize;
    z_uint8_mirror        NumMembers;
    unsigned              CompFactor;
    WORD                  cchComment;
} USERFUNCTIONS_60, *LPUSERFUNCTIONS_60;

/* ---- DCL (UZ_DCL_STRUCTVER 0x600) ---- */

#define UZ_DCL_STRUCTVER_60 0x600

typedef struct {
    unsigned StructVersID;
    int ExtractOnlyNewer;
    int SpaceToUnderscore;
    int PromptToOverwrite;
    int fQuiet;
    int ncflag;
    int ntflag;
    int nvflag;
    int nfflag;
    int nzflag;
    int ndflag;       /* 0=junk paths / 1=safe / 2=unsafe */
    int noflag;       /* always overwrite */
    int naflag;
    int nZIflag;
    int B_flag;
    int C_flag;
    int D_flag;
    int U_flag;
    int fPrivilege;
    LPSTR lpszZipFN;
    LPSTR lpszExtractDir;
} DCL_60, *LPDCL_60;

/* ---- Return codes (subset — full list in unzip.h) ---- */
#define PK_OK_60        0
#define PK_PARAM_60     10
#define PK_MEM_60       4
#define PK_NOZIP_60     9
#define PK_BADERR_60    80

/* ---- Entry points (static-linked from unzip60 inside this DLL) ---- */
extern int WINAPI Wiz_SingleEntryUnzip(int ifnc, char **ifnv,
                                       int xfnc, char **xfnv,
                                       LPDCL_60 lpDCL,
                                       LPUSERFUNCTIONS_60 lpUserFunc);

#ifdef __cplusplus
}
#endif

#endif /* UNZIP64_UNZIP60_TYPES_H */
