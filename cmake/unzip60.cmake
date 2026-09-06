# Info-ZIP UnZip 6.0 source list + compile settings.
#
# Mirror of cmake/zip30.cmake for the UnZip side. Sources and defines taken
# from Info-ZIP UnZip 6.0 配布物の windll/vc8/dll/unz32dll.vcproj (Release x64)。
#
# Exports UNZIP60_SOURCES / UNZIP60_INCLUDES / UNZIP60_DEFINES so unzip64.cmake
# can absorb them into its DLL target.

set(UNZIP60_SOURCES
    ${UNZIP60_DIR}/api.c
    ${UNZIP60_DIR}/crc32.c
    ${UNZIP60_DIR}/crypt.c
    ${UNZIP60_DIR}/explode.c
    ${UNZIP60_DIR}/extract.c
    ${UNZIP60_DIR}/fileio.c
    ${UNZIP60_DIR}/globals.c
    ${UNZIP60_DIR}/inflate.c
    ${UNZIP60_DIR}/list.c
    ${UNZIP60_DIR}/match.c
    ${UNZIP60_DIR}/process.c
    ${UNZIP60_DIR}/ubz2err.c
    ${UNZIP60_DIR}/unreduce.c
    ${UNZIP60_DIR}/unshrink.c
    ${UNZIP60_DIR}/zipinfo.c
    ${UNZIP60_DIR}/win32/nt.c
    ${UNZIP60_DIR}/win32/win32.c
    ${UNZIP60_DIR}/win32/win32i64.c
    ${UNZIP60_DIR}/windll/windll.c
)

set(UNZIP60_INCLUDES
    ${UNZIP60_DIR}
    ${UNZIP60_DIR}/windll
    ${UNZIP60_DIR}/win32
)

# Defines from unz32dll.vcproj Release x64:
#   WIN32/_WINDOWS : platform
#   WINDLL         : build as DLL (not CLI)
#   DLL            : enable DLL-specific code paths
#   USE_EF_UT_TIME : extended-timestamp support
# ASM_CRC intentionally omitted (no asm helpers on x64).
set(UNZIP60_DEFINES
    WIN32
    _WINDOWS
    WINDLL
    DLL
    USE_EF_UT_TIME
)

# Same /wd set as zip30 — unzip60's legacy C style triggers the same warnings.
set(UNZIP60_WARN_SUPPRESS
    /wd4013 /wd4018 /wd4101 /wd4102 /wd4133 /wd4146 /wd4244 /wd4267
    /wd4311 /wd4312
)

# /FI "windows.h" preempts the winnt.h CR bitfield vs unzpriv.h's
# `#define CR 13` macro conflict. Same fix as zip30.cmake.
set(UNZIP60_FORCE_INCLUDE /FI "windows.h")
