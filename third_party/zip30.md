# Info-ZIP zip 3.0 (subset, modified for 64-bit MSVC)

This directory contains an **altered version** of Info-ZIP zip 3.0.
Distribution complies with the Info-ZIP license — see `LICENSE`.

## Upstream

- Info-ZIP Zip 3.0 (released 2008-07-05)
- <http://www.info-zip.org/>

## Subset rationale

Only files required to build zip64j.dll (64-bit Windows DLL) are
included. Omitted:

- CLI-only drivers (`zipcloak.c`, `zipnote.c`, `zipsplit.c`)
- OS-specific ports other than `win32/` (acorn, amiga, aosvs, atari,
  atheos, beos, cmsmvs, human68k, macos, msdos, novell, ...)
- `bzip2/` (bzip2 support is disabled in our build)
- CLI main / asm helpers (`crc_i386.S`, `match.S`)
- Build scripts, docs, manpages, proc files

## Modifications

Relative to upstream, these files are altered:

- `win32/win32i64.c`, `win32/win32zip.c`
  Re-ordered `<windows.h>` include and bracketed it with `#undef CR` /
  `#define CR 13`. zip.h defines `CR` as a macro (value 13), but
  winnt.h uses `CR` as an ARM64 bitfield member name; the macro
  expansion breaks the struct definition. The fix pulls `<windows.h>`
  in with the macro undefined, then restores it.

- `zip.c`
  Armed the `setjmp(zipdll_error_return)` target (and re-enabled the
  `retcode` declaration it needs) for builds that define both `WINDLL`
  and `USE_ZIPMAIN`. `ziperr()` ends in
  `longjmp(zipdll_error_return, c)` whenever `WINDLL` is defined, but
  upstream arms the jump target only when `USE_ZIPMAIN` is absent. The
  DLL build defines both, so the `jmp_buf` stayed zeroed and every
  `ZIPERR` path jumped to a null target, terminating the host process
  instead of returning a `ZE_` code to `ZpArchive`.

- `win32/win32i64.c`
  `zftello` / `zfseeko` now call `_ftelli64` / `_fseeki64`. Upstream
  read the position with `fgetpos()` but moved it with `_lseeki64()`
  on the underlying descriptor, relying on `fflush()` to drop the
  stream's read buffer first. The UCRT makes `fflush()` a no-op on a
  read-only stream, so the descriptor and the `FILE*` desynchronise
  and the seek has no effect on the next read — which broke every
  operation that reads an existing archive back (`-d`, `-u`, `-f`,
  `-g`). Info-ZIP applied the equivalent fix to UnZip 6.0 (see the
  `_MSC_VER >= 1400` branch in `unzip60/unzpriv.h`) but not to Zip 3.0.

- `windll/windll.c`
  Added `#undef printf / fprintf / perror` and a comment explaining
  why `_NO_CRT_STDIO_INLINE` is project-wide: UCRT would otherwise
  emit inline copies in every TU and collide with windll.c's overrides
  for the DLL callback redirection.

Each modification is marked with a comment adjacent to the changed code.

## Not a modification, but note

`crc_i386.S` and `match.S` (GAS assembler) are not included — they are
not used by the x64 build (NO_ASM is defined). Upstream has them for
x86-only optimization.
