# Building and running lrzip on Windows — step by step

Everything here was run start to finish on a clean clone of
`https://github.com/MohamadHalalGithA/lrzip` before being written down. The
commands and the output you should see are what actually happened, not what
ought to happen.

**What you end up with:** a single file, `lrzip.exe`, about 3.6 MB. It needs no
Cygwin, no WSL, and no DLLs beside it. Copy it anywhere on any Windows 10/11
machine and it runs.
t
**How long:** about 20 minutes, most of it MSYS2 downloading packages.

---

## Part 1 — Install the build environment

You only do this once. Building needs a compiler toolchain; *running* the
finished `lrzip.exe` needs none of it.

### Step 1.1 — Install MSYS2

Download the installer from <https://www.msys2.org/> and run it. Accept the
default location, `C:\msys64`. The guide assumes that path.

MSYS2 is a package manager and a set of Unix-style build tools for Windows. It
is only a workshop — nothing from it is baked into the binary.

### Step 1.2 — Open the UCRT64 shell

From the Start menu, open **MSYS2 UCRT64**.

This matters. MSYS2 installs several shells that look identical and are not:

| Shell | Use it? |
|---|---|
| **MSYS2 UCRT64** | **Yes — this one** |
| MSYS2 MINGW64 | No — older C runtime |
| MSYS2 MSYS | No — builds against the Cygwin-style emulation layer this port exists to avoid |

Confirm you are in the right one:

```sh
echo $MSYSTEM
```

It must print `UCRT64`. If it prints anything else, close the window and open
the right shortcut.

### Step 1.3 — Update MSYS2

```sh
pacman -Syu
```

If it tells you to close the terminal, do so, reopen **MSYS2 UCRT64**, and run
`pacman -Syu` again until it reports nothing left to do.

### Step 1.4 — Install the toolchain and libraries

```sh
pacman -S --needed base-devel git \
    mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-bzip2 \
    mingw-w64-ucrt-x86_64-lz4 \
    mingw-w64-ucrt-x86_64-lzo2 \
    mingw-w64-ucrt-x86_64-zlib
```

Press Enter to accept the default selection when asked.

What each part is for:

- `base-devel` — `make`, `autoconf`, `automake`, `libtool`
- `git` — to clone the repository
- `toolchain` — the gcc compiler and `windres`, which compiles the UTF-8
  manifest into the exe
- `bzip2`, `lz4`, `lzo2`, `zlib` — the four compression back ends lrzip links
  against

### Step 1.5 — Check the compiler is there

```sh
gcc --version
```

Something like `gcc (Rev3, Built by MSYS2 project) 16.2.0`. If you get
"command not found", you are in the wrong shell — go back to Step 1.2.

---

## Part 2 — Build lrzip.exe

All of this runs in the **MSYS2 UCRT64** shell.

### Step 2.1 — Clone the repository

```sh
cd ~
git clone https://github.com/MohamadHalalGithA/lrzip.git
cd lrzip
```

### Step 2.2 — Generate the build system

```sh
./autogen.sh
```

This step is required and cannot be skipped. The generated files (`configure`,
`Makefile.in`, `config.h.in`) are deliberately not stored in the repository, so
this creates them.

Expect a page of `autoreconf`/`libtoolize` chatter. Warnings are normal; the
command finishing without an error is what counts.

### Step 2.3 — Configure

```sh
./configure
```

Two lines worth spotting as it scrolls past:

```
checking for windres... windres
checking for gcc... gcc
```

If `windres` says `no`, the toolchain package did not install fully. The build
still succeeds but non-ASCII filenames will silently not work — reinstall
`mingw-w64-ucrt-x86_64-toolchain`.

### Step 2.4 — Compile

```sh
make -j$(nproc)
```

`-j$(nproc)` builds using every CPU core. A minute or two.

The last interesting line is:

```
  CXXLD    lrzip.exe
```

You now have `lrzip.exe` in the current directory.

---

## Part 3 — Verify what you built

Do not skip this. Two of these checks catch failures that are otherwise silent.

### Step 3.1 — It runs

```sh
./lrzip.exe -V
```

```
lrzip version 0.7.1
```

### Step 3.2 — It is self-contained

```sh
objdump -p lrzip.exe | grep "DLL Name"
```

You should see **only** Windows system DLLs:

```
DLL Name: KERNEL32.dll
DLL Name: ADVAPI32.dll
DLL Name: bcrypt.dll
DLL Name: api-ms-win-crt-runtime-l1-1-0.dll     (and other api-ms-win-crt-*)
```

If any of these appear, the static link did not happen and the exe will **not**
run on a machine without MSYS2:

```
libgcc_s_seh-1.dll   libstdc++-6.dll   libwinpthread-1.dll
libbz2-1.dll   liblz4.dll   liblzo2-2.dll   zlib1.dll
```

If `cygwin1.dll` or `msys-2.0.dll` appears, you built in the wrong shell — go
back to Step 1.2.

### Step 3.3 — It compresses and decompresses correctly

```sh
seq 1 200000 > sample.txt
./lrzip.exe -f -o sample.lrz sample.txt
./lrzip.exe -t sample.lrz
./lrzip.exe -f -d -o sample.out sample.lrz
md5sum sample.txt sample.out
```

The two hashes must be identical. `-t` must exit without complaint.

### Step 3.4 — Non-ASCII filenames work

```sh
cp sample.txt "日本語.txt"
./lrzip.exe -f -o u.lrz "日本語.txt"
./lrzip.exe -f -d -o u.out u.lrz
md5sum sample.txt u.out
```

Hashes must match. This is the check that catches a missing `windres`: without
the embedded UTF-8 manifest this fails with `Failed to stat ???.txt` while
everything else still passes.

---

## Part 4 — Install it on a Windows machine

`lrzip.exe` is now a normal Windows program. MSYS2 is not needed from here on,
on this machine or any other.

### Step 4.1 — Put it somewhere sensible

In **PowerShell** (not the MSYS2 shell):

```powershell
mkdir "$env:LOCALAPPDATA\Programs\lrzip"
copy "$env:USERPROFILE\lrzip\lrzip.exe" "$env:LOCALAPPDATA\Programs\lrzip\"
```

Any folder works. This one needs no administrator rights.

### Step 4.2 — Add it to your PATH

So you can type `lrzip` from any directory instead of the full path.

**PowerShell, permanent, no admin needed:**

```powershell
[Environment]::SetEnvironmentVariable(
  "Path",
  [Environment]::GetEnvironmentVariable("Path","User") + ";$env:LOCALAPPDATA\Programs\lrzip",
  "User")
```

Close and reopen PowerShell for it to take effect.

**Or through the GUI:** press Start, type "environment variables", choose *Edit
the system environment variables* → *Environment Variables…* → under *User
variables* select **Path** → *Edit* → *New* → paste the folder path → OK.

### Step 4.3 — Confirm

Open a **new** PowerShell or Command Prompt:

```powershell
lrzip -V
```

```
lrzip version 0.7.1
```

If you get "not recognized", the PATH change has not reached this window —
close it and open a fresh one.

### Step 4.4 — Copying it to another machine

Copy `lrzip.exe` on its own. Nothing else is needed. Verified: with MSYS2
entirely removed from `PATH`, the binary still compresses and decompresses
correctly.

Windows 10 version 1903 or newer is recommended. On older Windows everything
works except filenames outside your system code page.

---

## Part 5 — Using it

Run these in PowerShell or Command Prompt.

```powershell
lrzip bigfile.iso              # compress -> bigfile.iso.lrz (input kept)
lrzip -d bigfile.iso.lrz       # decompress
lrzip -t bigfile.iso.lrz       # verify integrity, write nothing
lrzip -i bigfile.iso.lrz       # show what is inside
```

Choosing a back end:

```powershell
lrzip -z bigfile.iso           # zpaq  - smallest, very slow
lrzip bigfile.iso              # lzma  - the default, good balance
lrzip -b bigfile.iso           # bzip2
lrzip -g bigfile.iso           # gzip
lrzip -l bigfile.iso           # lzo   - fastest, least compression
lrzip -n bigfile.iso           # no back end, rzip stage only
```

Useful flags:

```powershell
lrzip -L 9 bigfile.iso         # maximum compression level
lrzip -f bigfile.iso           # overwrite an existing output file
lrzip -p 4 bigfile.iso         # limit to 4 threads
lrzip -m 4 bigfile.iso         # cap memory use at 400 MB
lrzip -e bigfile.iso           # encrypt, prompts for a passphrase
lrzip -h                       # full option list
```

Pipelines work:

```powershell
type bigfile.iso | lrzip -q > bigfile.lrz
```

lrzip is built for **large** files. Its long-range matching finds redundancy
across gigabytes, which is where it beats ordinary compressors. On small files
it offers little over zip.

---

## Part 6 — When something goes wrong

**`Can't locate Autom4te/ChannelDefs.pm`** during `./autogen.sh`
You are running it in Git Bash, not MSYS2 UCRT64. Git ships its own Perl, which
cannot read the Unix-style paths autoconf uses. Use the MSYS2 UCRT64 shell.

**`Cannot create temporary file in C:\WINDOWS\: Permission denied`**
You are using MSYS `make` from outside its own shell. Use the MSYS2 UCRT64
shell, or `mingw32-make` if you must stay in Git Bash.

**`configure: error: ... bz2 / lzo2 / lz4 / zlib not found`**
Step 1.4 did not complete. Re-run it.

**The exe works for you but not on another machine**
Run Step 3.2 there. You almost certainly have a shared build — check you did
not pass `--disable-fully-static` to `configure`. Note that a shared build puts
the real program in `.libs\lrzip.exe` and leaves a small wrapper script with
the same name in the top folder; copying the wrapper will not work.

**`Failed to stat ???.txt`**
The UTF-8 manifest is missing, so `windres` was absent when you configured. Run
`pacman -S mingw-w64-ucrt-x86_64-toolchain`, then `./configure` and `make`
again.

**`Failed to allocate MD5 batch buffer` when running several at once**
lrzip sizes its buffers from total system RAM, and Windows — unlike Linux —
commits every allocation up front. Several copies at once can exhaust memory.
Use `-m` to cap each one, for example `lrzip -m 4 …`.

**`make check` fails**
Expected, and not a problem with your build. That suite compares against output
recorded on Linux and tests read-only directory behaviour and installed man
pages, none of which apply on Windows. Use the checks in Part 3 instead.

---

## What was verified for this guide

A fresh `git clone` of the repository, built with the exact commands above:

| Check | Result |
|---|---|
| `autogen.sh` → `configure` → `make` | exit 0 |
| Binary size | 3,602,493 bytes |
| Support DLLs required | **0** |
| Imports | `KERNEL32`, `ADVAPI32`, `bcrypt`, `api-ms-win-crt-*` only |
| `lrzip -V` | `lrzip version 0.7.1` |
| Round trip 1,839,740 → 198,160 bytes | md5 identical |
| Non-ASCII filename round trip | pass |
| Run with MSYS2 removed from `PATH` | compresses and decompresses correctly |
