# Build unzip64.dll — 統合アーカイバAPI仕様 UnZip DLL (64bit).
#
# Contains Info-ZIP unzip 6.0 statically linked. unzip60's DllMain (in
# windll/windll.c) is our DLL entry point. unzip64's own exports (UnZip* /
# ZipUnZip*) are controlled by unzip64.def — unzip60's Wiz_* entry points
# remain internal.
#
# Mirrors cmake/zip64j.cmake structure.

set(UNZIP64_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/unzip64")
set(UNZIP64_INC "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(UNZIP64_OWN_SOURCES
    ${UNZIP64_SRC}/unzip_impl.c
    ${UNZIP64_SRC}/arc_cdparse.c
    ${UNZIP64_SRC}/arc_handle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/debug_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/zip64j/cmdline.c
)

# Our code that reads unzip60's Uz_Globs; built with the unzip60 settings.
set(UNZIP64_UNZIP60_SOURCES
    ${UNZIP64_SRC}/unzip60_run.c
)

add_library(unzip64 SHARED
    ${UNZIP64_OWN_SOURCES}
    ${UNZIP64_UNZIP60_SOURCES}
    ${UNZIP60_SOURCES}
    ${UNZIP64_SRC}/unzip64.rc
)

target_include_directories(unzip64 PRIVATE
    ${UNZIP64_INC}
    ${UNZIP60_INCLUDES}
)

target_compile_definitions(unzip64 PRIVATE
    _USRDLL
    # Tags this DLL's lines in the shared diagnostic log (src/common/debug_log.c).
    ZIP64J_LOG_TAG="unzip64"
    ${UNZIP60_DEFINES}
)

# UNICODE / _UNICODE scoped to our own code only — unzip60 uses ANSI TCHAR
# macros. Same lesson as zip64j.cmake: applying UNICODE target-wide reroutes
# wvsprintf to wvsprintfW and corrupts ANSI output buffers.
set_source_files_properties(${UNZIP64_OWN_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "UNICODE;_UNICODE"
)

if(MSVC)
    set_source_files_properties(${UNZIP60_SOURCES} ${UNZIP64_UNZIP60_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${UNZIP60_WARN_SUPPRESS};${UNZIP60_FORCE_INCLUDE}"
    )
    foreach(src ${UNZIP64_OWN_SOURCES})
        set_source_files_properties(${src} PROPERTIES COMPILE_OPTIONS "/W4")
    endforeach()
endif()

set_target_properties(unzip64 PROPERTIES
    OUTPUT_NAME "unzip64"
    PREFIX ""
    SUFFIX ".dll"
)

# Unlike zip30, unzip60 does NOT override printf/fprintf globally — output
# routing goes through G.lpUserFunctions->print set by Wiz_Init. So we do NOT
# need legacy_stdio_definitions.lib / _NO_CRT_STDIO_INLINE / /FORCE:MULTIPLE
# here; only the /DEF: pin.
if(MSVC)
    set_property(TARGET unzip64 APPEND PROPERTY
        LINK_FLAGS "/DEF:\"${UNZIP64_SRC}/unzip64.def\""
    )
endif()

target_link_libraries(unzip64 PRIVATE shlwapi)
