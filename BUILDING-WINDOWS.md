# Building lrzip on Windows

This produces a native `lrzip.exe` — no Cygwin runtime, no WSL, and no support
DLLs to ship beside it.

## What you need

[MSYS2](https://www.msys2.org/). Everything below runs in its **UCRT64** shell,
not the plain MSYS shell and not Git Bash — see [Which shell](#which-shell) for
why that matters.

Open **MSYS2 UCRT64** from the Start menu and install the toolchain and the four
compression libraries lrzip links against:

```sh
pacman -Syu           # then reopen the shell if it asks you to
pacman -S --needed base-devel git \
    mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-bzip2 \
    mingw-w64-ucrt-x86_64-lz4 \
    mingw-w64-ucrt-x86_64-lzo2 \
    mingw-w64-ucrt-x86_64-zlib
```

## Build

```sh
git clone https://github.com/MohamadHalalGithA/lrzip.git
cd lrzip
./autogen.sh
./configure
make -j$(nproc)
```

`autogen.sh` is required: the generated autotools files (`configure`,
`Makefile.in`, `config.h.in`) are deliberately not in the repository.

The result is `lrzip.exe` in the top of the tree.

## Check what you built

```sh
./lrzip.exe -V
objdump -p lrzip.exe | grep 'DLL Name'
```

The import list should contain only Windows system DLLs — `KERNEL32.dll`,
`ADVAPI32.dll`, `bcrypt.dll` and a set of `api-ms-win-crt-*`. If you see
`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`, `libbz2-1.dll`,
`liblz4.dll`, `liblzo2-2.dll` or `zlib1.dll`, the static link did not happen and
the binary will not run on a machine without MSYS2.

There must be no `cygwin1.dll` or `msys-2.0.dll`. Those would mean an emulation
layer rather than a native build.

Quick functional check:

```sh
seq 1 100000 > sample.txt
./lrzip.exe -f -o sample.lrz sample.txt
./lrzip.exe -t sample.lrz
./lrzip.exe -f -d -o sample.out sample.lrz
md5sum sample.txt sample.out      # the two hashes must match
```

## Static linking

The Windows build links everything statically by default, so a downloaded
`lrzip.exe` is one self-contained file of about 3.6 MB. To build against shared
libraries instead — faster links while developing, but the binary then needs
those seven DLLs on `PATH`:

```sh
./configure --disable-fully-static
```

Note that a shared build puts the real executable in `.libs/lrzip.exe` and
leaves a small libtool wrapper script at the top level. Inspect and distribute
the one in `.libs/`; the wrapper's import table is not the program's.

The mechanism is `-all-static`, applied as `lrzip_LDFLAGS` in `Makefile.am`. It
is a libtool flag rather than a compiler one, so it cannot be passed to
`configure` — configure's own test links would fail with it.

## Which shell

Use the **MSYS2 UCRT64** shell. Two things break elsewhere:

- **Git Bash.** `autogen.sh` runs `autoreconf`, whose `#!/usr/bin/perl` resolves
  to Git's own Perl. That is a native Windows program, so the POSIX paths in its
  `@INC` are read as Windows paths and the module load fails with
  `Can't locate Autom4te/ChannelDefs.pm`. The same mismatch breaks `pod2man`
  when the man pages are rebuilt.
- **The plain MSYS shell.** It targets the MSYS runtime, which is the Cygwin
  emulation layer this port exists to avoid.

If you must drive the build from Git Bash, use `mingw32-make` rather than MSYS
`make` — MSYS `make` mangles `TMP` on the way to the native compiler, which then
falls back to `C:\WINDOWS\` and fails with "Cannot create temporary file".

## Requirements at runtime

Windows 10 version 1903 or later is recommended. The binary embeds a manifest
declaring UTF-8 as the process code page, which is what lets filenames outside
the system ANSI code page — Japanese, Cyrillic, emoji — work at all. On older
Windows the setting is ignored and such filenames fail, as they did before the
manifest existed; nothing else changes.

## Known limits

- Temporary files holding decompressed data are created in `%TEMP%` and rely on
  that directory's ACL for confidentiality. Pointing `TMPDIR` at a location
  other users can read would expose their contents.
- Many concurrent instances can exhaust memory where Linux would cope.
  Windows has no overcommit: every allocation is charged against RAM plus
  pagefile immediately, while Linux hands out address space and commits pages
  only when touched. lrzip sizes its buffers from *total* system RAM, so N
  instances ask for N times that. Measured on a 16 GB machine, twelve
  concurrent `lrzip < big > out` pipelines produced one failure ("Failed to
  allocate MD5 batch buffer"); four ran fine. Cap the buffers with `-m` when
  running many at once, or run fewer in parallel.

- `make check` does not pass on Windows, and is not expected to. Its gold
  suite compares against output recorded on Linux and exercises read-only
  directory semantics and installed man pages, neither of which behaves the
  same here. The chunk-filter portion of the suite does pass. CI runs targeted
  round-trip tests instead; see .github/workflows/windows.yml.

- Redirecting compressed output to `/dev/null` fails with the usage message
  rather than compressing. Write to a real file when benchmarking.

- A hard kill (Task Manager, power loss) can leave a temporary file behind.
  Windows cannot delete a file that is still open, so the deletion happens when
  the handle closes rather than immediately as it does on POSIX.
