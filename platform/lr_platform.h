/*
   Platform abstraction layer for lrzip.

   This is the only platform header application code includes. It is pulled in
   first from lrzip_private.h, which every application translation unit reaches
   directly or transitively, so it is the single point where the OS is decided.

   Two jobs:

     1. Include the system headers the application needs, per platform. This is
        why application code no longer includes <termios.h>, <sys/statvfs.h> and
        friends itself -- those includes moved here.

     2. Declare the lr_* interface. Interfaces are shaped by what callers need,
        not by what POSIX happens to expose: lr_free_space() returns one integer
        rather than reproducing struct statvfs, and lr_map_move() is named for
        the caller's intent because Windows cannot offer mremap's mechanism.

   One implementation of each function exists per platform, in platform/posix/
   and platform/win32/. The filenames are parallel, so the build selects a
   directory rather than a file list.
*/

#ifndef LR_PLATFORM_H
#define LR_PLATFORM_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>

/* The application's own i64 lives in lrzip_private.h, which includes this
   header before defining it. The interface therefore needs its own name. */
typedef int64_t lr_i64;

#if defined(_WIN32)
# define LR_PLATFORM_WIN32 1
# include "lr_platform_win32.h"
#else
# define LR_PLATFORM_POSIX 1
# include "lr_platform_posix.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ io
   Largest transfer to attempt in a single read() or write() call.

   Linux silently caps a huge read at 0x7ffff000 and returns a short count, so
   a caller that loops never notices. Windows does not: measured on UCRT64,
   read() with a count of 2.6 GB -- which is what mmap_stdin() asks for on a
   16 GB machine, since the compression window is sized from RAM -- returns -1
   with EINVAL rather than reading what it can.

   Callers that already loop only need to clamp each call to this. SESSION 11 */
#define LR_IO_CHUNK ((size_t)1 << 30)

/* ------------------------------------------------------------------- startup
   Call once, as the first statement in main(), before anything is opened.

   Windows opens files in TEXT mode by default, and that is not cosmetic here.
   Measured on UCRT64: writing the 4 bytes "a\nb\n" produces a 6-byte file,
   because every \n is expanded to \r\n; and reading a buffer containing 0x1A
   stops there, returning 2 of 8 bytes, because the CRT still treats Ctrl-Z as
   end of file. Compressed data contains both bytes constantly, so an archive
   written in text mode is corrupt and one read in text mode ends early.

   lrzip never passes O_BINARY -- it has no reason to, since the flag does not
   exist on POSIX -- so the mode is set process-wide here instead of at forty
   call sites.                                                  SESSION 10 */
void lr_platform_init(void);

/* ------------------------------------------------------------------ console
   Toggle terminal echo around the passphrase prompt. Both return false if
   stdin is not a terminal, which callers may ignore -- there is no echo to
   suppress when input is a pipe or a file.                      SESSION 6 */
bool lr_echo_disable(void);
bool lr_echo_enable(void);

/* --------------------------------------------------------------- filesystem
   Bytes free to this user on the filesystem holding fd; -1 on error.

   Takes a descriptor rather than a path because both call sites hold an
   already-open output file and used fstatvfs(). Windows has no fstatvfs and
   its free-space call is path based, so the win32 side recovers a path from
   the handle -- the awkwardness belongs here, not at the call sites.
                                                                  SESSION 6 */
lr_i64 lr_free_space(int fd);

/* No lr_set_file_time() here, though Session 5 planned one and Session 9 was
   to implement it. UCRT64 supplies <utime.h> and a working utime(), so
   lrzip.c's preserve_times() needs no help: the call compiles and behaves as
   it does on Linux. An lr_* wrapper would have been indirection with one
   implementation. Same reasoning retired the planned lr_time_ms() below.
   Under the settled MinGW-only scope there is no second implementation to
   abstract over; if that scope ever widens, these come back.     SESSION 9 */

/* Discard a temporary file, in two halves, because the platforms do it at
   different moments.

   POSIX unlinks the file while it is still open: the name disappears at once
   and the open descriptors keep working, so the file cannot survive a crash.
   Windows refuses to delete an open file, and the delete-on-close flag cannot
   be used either -- the callers open a second descriptor BY NAME after
   creating the file, which a delete-pending file will not allow.

   So lr_discard_open() removes it now on POSIX and does nothing on Windows,
   and lr_discard_closed() does nothing on POSIX and removes it on Windows
   once the descriptors are closed. Call BOTH: each is a no-op on the platform
   that does not need it. The Windows half means a hard kill can leave a temp
   file behind, which POSIX avoids -- an accepted difference, not an
   oversight.                                                  SESSION 11 */
bool lr_discard_open(const char *path);
bool lr_discard_closed(const char *path);

/* Flush a descriptor's written data to the storage device.       SESSION 7 */
bool lr_fsync(int fd);

/* Apply the permission bits of a POSIX mode to an open file.

   Windows has no POSIX permission model. The only bit it can honour is the
   read-only attribute, which the owner-write bit maps onto; the group and
   other bits have no representation and are discarded.

   fd must be open for writing. The win32 side sets the attribute through the
   handle, which needs FILE_WRITE_ATTRIBUTES access; a read-only descriptor
   fails with ERROR_ACCESS_DENIED. Both call sites pass the output file, which
   is opened O_WRONLY or O_RDWR, so this costs nothing -- but it is a real
   precondition and callers should not assume otherwise.           SESSION 7 */
bool lr_set_mode(int fd, unsigned int mode);

/* Apply a POSIX owner to an open file.

   Returns true on Windows without doing anything, and that is the honest
   answer rather than a papered-over failure: Windows has no uid/gid, and a
   file created by this process is already owned by the user running it, so
   the caller's goal -- output owned like the input -- already holds. Returning
   false would print a warning on every single run for a non-problem.
                                                                  SESSION 7 */
bool lr_set_owner(int fd, unsigned int uid, unsigned int gid);

/* -------------------------------------------------------- system information
   Total physical RAM in bytes, or -1 if it cannot be determined.

   This is not a convenience query. lrzip sizes its compression window from
   available RAM -- handling files larger than memory is the reason the program
   exists -- so a wrong answer here silently degrades every compression, and a
   too-large answer drives the machine into swap.                 SESSION 7 */
lr_i64 lr_physical_ram(void);

/* Online CPUs usable by this process. Never returns less than 1, so callers
   can use the result directly as a thread count.                  SESSION 7 */
int lr_cpu_count(void);

/* No timing section: UCRT64 supplies <sys/time.h> and gettimeofday(), which is
   what main.c, rzip.c and runzip.c call and all they need. See the note beside
   lr_fsync above.                                                 SESSION 9 */

/* -------------------------------------------------------------- pseudorandom
   A full-width 32-bit value for seeding rzip's rolling-hash index table.

   Deliberately 32 bits rather than a random() clone. lrzip never seeds the
   generator, so on glibc hash_index[] is a fixed table, identical on every run
   and every machine, and compressed output is reproducible. Two properties
   must therefore survive the port: every one of the 32 bits must vary, and the
   sequence must stay deterministic. Windows' rand() satisfies neither -- UCRT
   caps RAND_MAX at 32767, so the upstream expression built from two rand()
   calls would leave bits 15 and 31 permanently zero and weaken the hash that
   drives match-finding.                                           SESSION 7 */
uint32_t lr_random32(void);

/* Cryptographically secure random bytes, for salts and IVs. Returns false if
   the system could not supply them, and the caller MUST treat that as fatal:
   get_rand() in util.c fails closed on purpose, because falling back to a
   weak PRNG for a salt is worse than refusing to encrypt.

   Distinct from lr_random32() above in every respect that matters. That one
   must be deterministic and reproducible across platforms; this one must be
   unpredictable. Sharing an implementation between them would break one or
   the other.                                                  SESSION 10 */
bool lr_secure_random(void *buf, size_t len);

/* --------------------------------------------------------------- scheduling
   Priority expressed as a POSIX nice value: LR_PRIO_MIN is the most
   favourable, LR_PRIO_MAX the least. lrzip's -N option is documented in these
   terms and defaults to 19, so the range is part of the user interface and
   cannot be replaced with a Windows priority class without changing what -N
   means.

   The scope is the CALLING THREAD, not the process. That is what the call
   sites need -- stream.c renices each worker as it starts, expecting to affect
   only that worker -- and it is what Linux already does, where nice is a
   per-thread attribute despite PRIO_PROCESS being the name of the flag.

   Windows has no nice values. The backend maps the range onto the seven thread
   priority levels, which is lossy in one direction only: distinct nice values
   can land on the same level, so lr_get_priority() returns a representative
   value for the current level rather than necessarily the one last set. Only
   lrzip's warning message reads it back, so that is enough.

   lr_set_priority() returns false if the level could not be applied; callers
   fall back to leaving priority alone.                           SESSION 9 */
#define LR_PRIO_MIN (-20)
#define LR_PRIO_MAX 19

int  lr_get_priority(void);
bool lr_set_priority(int nice_val);

/* ----------------------------------------------------------- positional I/O
   Read/write at an absolute offset without moving the file pointer.
                                                                  SESSION 9 */
ssize_t lr_pread(int fd, void *buf, size_t count, lr_i64 offset);
ssize_t lr_pwrite(int fd, const void *buf, size_t count, lr_i64 offset);

/* --------------------------------------------------------------- interrupts
   Install a handler for interrupt-style events: Ctrl+C and a termination
   request on both platforms, plus console-close, logoff and shutdown on
   Windows, which has no SIGTERM.

   The handler takes no argument because the caller never used the signal
   number for anything but re-arming, and re-arming is done here instead --
   which signals exist is platform knowledge. SIGTTIN and SIGTTOU have no
   Windows counterpart at all.

   Semantic difference that must not be papered over: on POSIX the handler runs
   on the interrupted thread in signal context, so only async-signal-safe calls
   are legal. On Windows it runs on a separate OS-created thread, so it must be
   thread-safe instead, and repeat events arrive on further new threads rather
   than being held off by a signal mask. The win32 backend refuses re-entry
   explicitly for that reason.

   HAZARD, inherited rather than introduced: lrzip's handler does not set a
   flag and return. It unlinks files, prints, flushes and calls exit(). On
   POSIX that is already outside what a signal handler may safely do; on
   Windows it additionally races the main thread, which keeps running. Making
   this genuinely safe means a flag-and-poll shutdown across lrzip's worker
   loops, which is a redesign rather than a port.        SESSION 8 */
bool lr_install_interrupt_handler(void (*handler)(void));

/* ----------------------------------------------------------- memory locking
   Pin a buffer so the OS cannot page it out. lrzip uses this only on secrets
   -- passphrases, keys, IVs, the AES context -- so that they cannot be written
   to the pagefile and survive there after the process exits. This is a
   confidentiality measure, not a correctness one.

   Best effort by design, matching how the call sites treat it: every one of
   them ignores the result, because failing to lock is a reason to keep going
   with a weaker guarantee rather than to refuse to compress a file. Both
   platforms can refuse -- POSIX on RLIMIT_MEMLOCK, Windows on the process
   working-set quota -- and neither is worth aborting over.

   Locks do not nest on either platform: one unlock releases a page however
   many times it was locked.                                     SESSION 10 */
bool lr_mem_lock(void *addr, size_t len);
bool lr_mem_unlock(void *addr, size_t len);

/* ----------------------------------------------------------- memory mapping
   lr_mapping is opaque. This is the decision most expensive to get wrong: the
   win32 implementation must retain a HANDLE and an alignment delta that POSIX
   has no use for, and a bare void * return would have nowhere to keep them.

   The mode is an enum rather than the `bool writable` Session 5 planned,
   because "writable" hides the distinction that matters most here. rzip_chunk
   maps a chunk writably so lrz_filter_convert_mem() can do branch conversion
   IN PLACE. Those writes must never reach the file: the mapping is of the
   user's INPUT, and a shared writable mapping would rewrite the file being
   compressed. LR_MAP_COPY is copy-on-write (MAP_PRIVATE / FILE_MAP_COPY) and
   is the only writable file mode offered, so that mistake cannot be spelled.

   lr_map_move() replaces the munmap+mmap pair that slides the compression
   window, and lr_map_shrink() replaces mremap(), which lrzip only ever uses to
   shrink. Both are named for the caller's intent because Windows cannot offer
   either mechanism directly.

   Callers MUST re-fetch the pointer with lr_map_ptr() after lr_map_move() --
   the region is remapped, so the address moves. lr_map_shrink() keeps the
   address, since it only releases pages off the end.

   On failure lr_map_create() returns NULL with errno set, using ENOMEM for
   "that was too big". rzip_fd() retries at 90% of the size on ENOMEM, and
   that loop is how lrzip copes with a window larger than the address space
   will give it.                                                 SESSION 10 */
typedef enum {
	LR_MAP_READ,	/* file, read only, shared   -- PROT_READ, MAP_SHARED  */
	LR_MAP_COPY,	/* file, copy on write       -- MAP_PRIVATE, writable  */
	LR_MAP_ANON	/* private RAM, no file       -- MAP_ANONYMOUS          */
} lr_map_mode;

typedef struct lr_mapping lr_mapping;

lr_mapping *lr_map_create(int fd, lr_i64 offset, size_t len, lr_map_mode mode);
void       *lr_map_ptr(lr_mapping *m);
size_t      lr_map_len(lr_mapping *m);

/* Re-point a file mapping at a different part of the same file. */
bool        lr_map_move(lr_mapping *m, lr_i64 offset, size_t len);

/* Release pages off the end of an anonymous mapping. Shrink only. */
bool        lr_map_shrink(lr_mapping *m, size_t new_len);

void        lr_map_destroy(lr_mapping *m);   /* NULL is a no-op */

#ifdef __cplusplus
}
#endif

#endif /* LR_PLATFORM_H */
