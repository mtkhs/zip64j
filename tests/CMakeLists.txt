# Smoke tests for zip64j / unzip64 DLLs.
#
# Each executable loads the built DLLs from build/dist and exercises one slice
# of the 統合アーカイバAPI仕様 surface. Executables are dropped directly under
# ${CMAKE_BINARY_DIR} so they can find the DLLs via the root
# CMAKE_RUNTIME_OUTPUT_DIRECTORY.

set(SMOKE_TESTS
    smoke_exports           # DLL export surface + x64 calling convention
    smoke_zip_create        # Zip() produces a real archive on disk
    smoke_roundtrip         # zip64j → unzip64, extracted files match
    smoke_arc_handle        # UnZipOpenArchive / FindFirst / FindNext / accessors
    smoke_ex_accessors      # *_Ex / *64 size and time accessors
    smoke_unicode           # ZipW / UnZip*W with Japanese filenames
    smoke_afxw_cmdline      # afxw-style UnZip command line
    smoke_zip64_eocd        # Zip64 locator without 0xFFFFFFFF sentinel EOCD
    smoke_unzip_commands    # UnZip() subcommand dispatch
    smoke_unzip_negation    # `--X` option negation
    smoke_unzip_password    # `-P` on extract
    smoke_unzip_case_match  # `-C` case-insensitive filename match
    smoke_unzip_suffix      # `.ZIP` auto-append on the archive name
    smoke_zip_encrypt       # `-P` encryption roundtrip
    smoke_zip_options       # -J / -t / -tt / -b
    smoke_zip_update        # -d / -g / -u / -f (archive read-back)
)

foreach(T IN LISTS SMOKE_TESTS)
    add_executable(${T} ${T}.c)
    add_dependencies(${T} dist)
    set_target_properties(${T} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endforeach()
