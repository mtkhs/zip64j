# Build zip64j.dll — 統合アーカイバAPI仕様 ZIP DLL (64bit).
#
# Contains Info-ZIP zip 3.0 statically linked. zip30's DllMain (in windll.c)
# is our DLL entry point. zip64j's own exports (Zip* / UnZip* / ZipUnZip*) are
# controlled by zip64j.def — zip30's Zp_* entry points remain internal.

set(ZIP64J_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/zip64j")
set(ZIP64J_INC "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(ZIP64J_OWN_SOURCES
    ${ZIP64J_SRC}/cmdline.c
    ${ZIP64J_SRC}/zip_impl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/debug_log.c
)

add_library(zip64j SHARED
    ${ZIP64J_OWN_SOURCES}
    ${ZIP30_SOURCES}
    ${ZIP64J_SRC}/zip64j.rc
)

target_include_directories(zip64j PRIVATE
    ${ZIP64J_INC}
    ${ZIP30_INCLUDES}
)

target_compile_definitions(zip64j PRIVATE
    _USRDLL
    # Tags this DLL's lines in the shared diagnostic log (src/common/debug_log.c).
    ZIP64J_LOG_TAG="zip64j"
    # zip30 side (WIN32 / _WINDOWS / WINDLL / MSDOS / NO_ASM / USE_ZIPMAIN /
    # _NO_CRT_STDIO_INLINE). Applied target-wide because zip30 sources share
    # api.h / zip.h between this TU and zip30's.
    ${ZIP30_DEFINES}
)

# UNICODE / _UNICODE are scoped to our own code ONLY. zip30 uses the ANSI TCHAR
# macros (wvsprintf → wvsprintfA, etc.); compiling it with UNICODE defined
# silently routes wvsprintf through wvsprintfW, which writes WCHARs into the
# ANSI LPSTR buffer and corrupts cb_print output.
set_source_files_properties(${ZIP64J_OWN_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "UNICODE;_UNICODE"
)

# Apply /wd suppressions + /FI windows.h only to zip30 sources; keep /W4 for
# our own code so local regressions get caught.
if(MSVC)
    set_source_files_properties(${ZIP30_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${ZIP30_WARN_SUPPRESS};${ZIP30_FORCE_INCLUDE}"
    )
    # /W4 on zip64j's own code only.
    foreach(src ${ZIP64J_OWN_SOURCES})
        set_source_files_properties(${src} PROPERTIES COMPILE_OPTIONS "/W4")
    endforeach()
endif()

set_target_properties(zip64j PROPERTIES
    OUTPUT_NAME "zip64j"
    PREFIX ""
    SUFFIX ".dll"
)

# Same linker recipe as the old zip30.cmake target:
#   legacy_stdio_definitions.lib : out-of-line stdio for sprintf/sscanf/etc.
#   /DEF : pin the 統合アーカイバ export surface (zip64j.def). windll32.def
#          is NOT applied — Zp_* exports stay internal.
#   /FORCE:MULTIPLE /IGNORE:4006 : windll.c overrides perror; ucrt.lib also
#          ships perror. First-found wins (windll.obj's version).
if(MSVC)
    target_link_libraries(zip64j PRIVATE legacy_stdio_definitions.lib)
    set_property(TARGET zip64j APPEND PROPERTY
        LINK_FLAGS "/DEF:\"${ZIP64J_SRC}/zip64j.def\" /FORCE:MULTIPLE /IGNORE:4006"
    )
endif()
