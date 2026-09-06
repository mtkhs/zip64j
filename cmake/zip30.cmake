# Info-ZIP zip 3.0 source list + compile settings.
#
# Exports cache variables ZIP30_SOURCES / ZIP30_INCLUDES / ZIP30_DEFINES so
# zip64j.cmake can absorb them into its own target. zip30 is statically linked
# inside zip64j.dll rather than built as a DLL of its own, matching how the
# 統合アーカイバ ZIP DLLs are distributed.

set(ZIP30_SOURCES
    ${ZIP30_DIR}/api.c
    ${ZIP30_DIR}/crc32.c
    ${ZIP30_DIR}/crypt.c
    ${ZIP30_DIR}/deflate.c
    ${ZIP30_DIR}/fileio.c
    ${ZIP30_DIR}/globals.c
    ${ZIP30_DIR}/trees.c
    ${ZIP30_DIR}/ttyio.c
    ${ZIP30_DIR}/util.c
    ${ZIP30_DIR}/zip.c
    ${ZIP30_DIR}/zipfile.c
    ${ZIP30_DIR}/zipup.c
    ${ZIP30_DIR}/win32/nt.c
    ${ZIP30_DIR}/win32/win32.c
    ${ZIP30_DIR}/win32/win32i64.c
    ${ZIP30_DIR}/win32/win32zip.c
    ${ZIP30_DIR}/windll/windll.c
)

set(ZIP30_INCLUDES
    ${ZIP30_DIR}
    ${ZIP30_DIR}/windll
    ${ZIP30_DIR}/win32
)

# Defines required by zip30 sources (extracted from windll/visualc/dll/zip32z64.dsp):
#   WIN32/_WINDOWS : platform
#   WINDLL         : build as DLL (not CLI)
#   MSDOS          : Info-ZIP historical legacy; some Windows paths still use it
#   NO_ASM         : x64 has no asm helpers
#   USE_ZIPMAIN    : zip.c uses zipmain() entry (not main())
#   _NO_CRT_STDIO_INLINE : windll.c overrides printf/fprintf; UCRT must not emit
#                          inline copies in other TUs (else LNK2005).
set(ZIP30_DEFINES
    WIN32
    _WINDOWS
    WINDLL
    MSDOS
    NO_ASM
    USE_ZIPMAIN
    _NO_CRT_STDIO_INLINE
)

# Silence noisy warnings from Info-ZIP's legacy C style. Applied per-source
# (not per-target) so the zip64j target's /W4 for our own code stays strict.
set(ZIP30_WARN_SUPPRESS
    /wd4013 /wd4018 /wd4101 /wd4102 /wd4133 /wd4146 /wd4244 /wd4267
    /wd4311 /wd4312
)

# /FI "windows.h" forces winnt.h to be processed before any Info-ZIP header,
# so the Windows SDK's ARM64 bitfield "CR" in winnt.h isn't trampled by zip.h's
# `#define CR 13`. winnt.h's include guard prevents re-processing on later
# <windows.h> includes.
set(ZIP30_FORCE_INCLUDE /FI "windows.h")
