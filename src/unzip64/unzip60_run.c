/*
 * unzip60_run.c - Wiz_SingleEntryUnzip with a per-block progress report.
 *
 * unzip60 raises UZ_ST_IN_PROGRESS before writing each output block, but the
 * status callback Wiz_Init installs (windll.c Wiz_StatReportCB) drops it.
 * unzip60_run repeats Wiz_SingleEntryUnzip's steps and, when a progress
 * function is given, replaces that callback right after Wiz_Init. The
 * replaced callback only drives lpUserFunc->sound / ServCallBk, which
 * unzip_impl.c leaves as no-ops.
 *
 * Built with the unzip60 compile settings because it reads Uz_Globs.
 */

#define UNZIP_INTERNAL
#include "unzip.h"
#include "windll.h"
#include "unzip60_types.h"

static UNZIP60_PROGRESS_FN *s_progress;
static int s_writing;   /* between a reported START_EXTRACT and its FINISH_MEMBER */

static int UZ_EXP report_status(zvoid *pG, int fnflag, ZCONST char *zfn,
                                ZCONST char *efn, ZCONST zvoid *details)
{
    UNZIP60_PROGRESS p;
    __int64 pos;

    (void)zfn; (void)efn; (void)details;
    memset(&p, 0, sizeof(p));

    switch (fnflag) {
    case UZ_ST_START_EXTRACT:
        /* -t writes nothing; UNZIP32.DLL reports nothing for it either. */
        s_writing = !uO.tflag;
        if (!s_writing) return UZ_ST_CONTINUE;
        p.kind         = UNZIP60_PROGRESS_BEGIN;
        p.name_raw     = G.filename_full;
        p.name_is_utf8 = (G.lrec.general_purpose_bit_flag & (1 << 11)) != 0;
        p.dest_path    = G.filename;
        p.size         = G.lrec.ucsize;
        p.comp_size    = G.lrec.csize;
        p.crc          = G.lrec.crc32;
        p.dos_date     = (WORD)(G.lrec.last_mod_dos_datetime >> 16);
        p.dos_time     = (WORD)G.lrec.last_mod_dos_datetime;
        p.os_type      = G.pInfo->hostnum;
        break;
    case UZ_ST_IN_PROGRESS:
        /* Raised by partflush() just before it writes a block, so the file
         * position is the size written so far. G.outfile is only read here:
         * close_outfile() leaves it pointing at a closed FILE. */
        if (!s_writing || G.outfile == NULL) return UZ_ST_CONTINUE;
        pos = _ftelli64(G.outfile);
        if (pos < 0) return UZ_ST_CONTINUE;
        p.kind    = UNZIP60_PROGRESS_WRITE;
        /* -a (LF -> CR/LF) can write more than the entry size. */
        p.written = ((unsigned __int64)pos < G.lrec.ucsize)
                  ? (unsigned __int64)pos : G.lrec.ucsize;
        break;
    case UZ_ST_FINISH_MEMBER:
        if (!s_writing) return UZ_ST_CONTINUE;
        s_writing = 0;
        p.kind    = UNZIP60_PROGRESS_WRITE;
        p.written = G.lrec.ucsize;
        break;
    default:
        return UZ_ST_CONTINUE;
    }
    return s_progress(&p) ? UZ_ST_BREAK : UZ_ST_CONTINUE;
}

int unzip60_run(int ifnc, char **ifnv, int xfnc, char **xfnv,
                LPDCL_60 lpDCL, LPUSERFUNCTIONS_60 lpUserFunc,
                UNZIP60_PROGRESS_FN *progress)
{
    int retcode;
    CONSTRUCTGLOBALS();

    if (!Wiz_Init((zvoid *)&G, (LPUSERFUNCTIONS)lpUserFunc)) {
        DESTROYGLOBALS();
        return PK_BADERR;
    }
    if (progress != NULL) {
        s_progress = progress;
        s_writing  = 0;
        G.statreportcb = report_status;
    }

    if (lpDCL->lpszZipFN == NULL) {
        DESTROYGLOBALS();
        return PK_NOZIP;
    }
    if (!Wiz_SetOpts((zvoid *)&G, (LPDCL)lpDCL)) {
        DESTROYGLOBALS();
        return PK_MEM;
    }

    retcode = Wiz_Unzip((zvoid *)&G, ifnc, ifnv, xfnc, xfnv);

    DESTROYGLOBALS();
    return retcode;
}
