# Info-ZIP UnZip 6.0 (subset, unmodified)

This directory contains a **subset** of Info-ZIP UnZip 6.0.
Distribution complies with the Info-ZIP license — see `LICENSE`.

## Upstream

- Info-ZIP UnZip 6.0 (released 2009-04-20)
- <http://www.info-zip.org/>

## Subset rationale

Only files required to build unzip64.dll (64-bit Windows DLL) are
included. Omitted:

- CLI-only drivers (`unzipsfx.c`, `funzip.c`, `unzipstb.c`)
- OS-specific ports other than `win32/` (acorn, amiga, aosvs, atari,
  atheos, beos, cmsmvs, human68k, macos, msdos, novell, ...)
- `bzip2/` (bzip2 support is disabled in our build)
- Asm helpers (`crc_i386.S`, `crc_gcc.S`)
- Build scripts, docs, manpages, proc files

## Modifications

None. Files in this directory are byte-identical to the upstream
UnZip 6.0 release. Any macro / header-ordering adjustments required
for the 64-bit MSVC build are applied via compile flags (`/FI
windows.h`) in `cmake/unzip60.cmake`, not via source edits.
