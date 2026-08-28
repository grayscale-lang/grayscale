/*
 * platform.h — Thin portability layer isolating the OS-specific calls the
 * compiler makes: path manipulation, filesystem queries, executable and temp
 * file location, console capabilities, and child process spawning.
 *
 * The rest of the compiler must not use <unistd.h>, <dirent.h>, <sys/stat.h>,
 * <sys/wait.h>, or platform #ifdefs directly — route through this header so
 * there is exactly one place per concern to port.
 *
 * NOTE: this header deliberately does not include <windows.h>. That header
 * defines ERROR, min, and max as macros, which collide with the compiler's
 * own Severity enum and helpers. Windows headers stay inside platform.c.
 *
 * Author:  Aristomedes (@Aristomedes)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_PLATFORM_H
#define GRAYC_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef _WIN32
#define GRAY_OS_WINDOWS   1
#define GRAY_PATH_SEP     '\\'
#define GRAY_PATH_SEP_STR "\\"
#define GRAY_EXE_SUFFIX   ".exe"
#define GRAY_NULL_DEVICE  "NUL"
#else
#define GRAY_OS_WINDOWS   0
#define GRAY_PATH_SEP     '/'
#define GRAY_PATH_SEP_STR "/"
#define GRAY_EXE_SUFFIX   ""
#define GRAY_NULL_DEVICE  "/dev/null"
#endif

/* --- Strings --- */

/* strndup, which MinGW-w64 declares only under a POSIX or GNU feature-test
 * macro that the Windows branch of the Makefile deliberately does not set
 * (see the -std=gnu11 comment there). Without a declaration GCC assumes an
 * int return and truncates the pointer on Win64. Implemented portably rather
 * than #ifdef'd, so both platforms run the same code. Copies at most `n`
 * bytes, stopping early at a NUL, and always NUL-terminates. Returns NULL on
 * allocation failure. Caller owns the result and must free() it. */
char *gray_strndup(const char *s, size_t n);

/* --- Console --- */

/* Opt the console into interpreting ANSI escape sequences. No-op off Windows,
 * and on Windows consoles that refuse (output redirected to a file, or a
 * pre-Windows-10 console). Safe to call more than once. */
void gray_enable_vt_mode(void);

/* True when the stream is an interactive terminal, i.e. color is worth
 * emitting. Check the stream you are actually writing to — redirecting one
 * without the other is routine. */
bool gray_stdout_is_tty(void);
bool gray_stderr_is_tty(void);

/* --- Paths ---
 *
 * All path helpers are separator-agnostic: '/' is a separator everywhere, and
 * '\\' is additionally a separator on Windows. Paths the compiler *builds* use
 * GRAY_PATH_SEP, but paths it *reads* may mix both. */

bool gray_is_path_sep(char c);

/* Final component of a path. Returns a pointer into `path`, never NULL, and
 * never the empty string unless `path` itself is empty. */
const char *gray_path_basename(const char *path);

/* Last separator in a mutable path, or NULL. Truncating there yields the
 * parent directory. */
char *gray_path_rsep(char *path);

/* True when `path` has no parent to walk up to: "/" on POSIX, and additionally
 * "C:", "C:\\", "C:/", or a bare UNC share root on Windows. Guards
 * parent-directory walks against looping forever at a drive root. */
bool gray_path_is_root(const char *path);

/* Join `a` and `b` with exactly one GRAY_PATH_SEP between them, collapsing a
 * trailing separator on `a` and a leading one on `b`. Returns the length that
 * would have been written (snprintf semantics), so >= n means truncation. */
int gray_path_join(char *dst, size_t n, const char *a, const char *b);

/* True for a path that does not depend on the current directory: "/..." on
 * POSIX, plus "C:\...", "C:/...", and "\\server\share" on Windows. */
bool gray_path_is_absolute(const char *path);

/* Canonicalize `path` into a freshly malloc'd string the caller frees, or NULL
 * on failure. Resolves symlinks on POSIX; on Windows resolves "." and ".." and
 * makes the path absolute, but does not resolve symlinks or reparse points. */
char *gray_realpath(const char *path);

/* gray_realpath() into a caller-supplied buffer. Returns false if the path
 * cannot be resolved or does not fit. */
bool gray_realpath_into(const char *path, char *buf, size_t n);

/* Compare two paths for equality, honoring the filesystem's case sensitivity
 * and separator conventions. Use instead of strcmp() on canonicalized paths. */
bool gray_path_equal(const char *a, const char *b);

/* --- Filesystem --- */

bool gray_file_readable(const char *path);
bool gray_is_file(const char *path);
bool gray_is_dir(const char *path);
bool gray_remove_file(const char *path);
bool gray_getcwd(char *buf, size_t n);

/* Write `len` bytes to `path`, truncating it, creating it 0644 if absent.
 * Binary mode: bytes land on disk exactly as given on every platform. */
bool gray_write_file_mode(const char *path, const void *data, size_t len);

/* Read a whole file into a malloc'd NUL-terminated buffer, or NULL on failure.
 * Handles non-seekable inputs (pipes, FIFOs, /dev/stdin) that a stat-then-read
 * cannot. `report` prints why the file could not be opened; a caller that says
 * so itself passes false. The caller frees the result. */
char *gray_read_file(const char *path, bool report);

/* --- Directory scanning --- */

/* Callback invoked for each entry in a directory. `name` is the bare filename
 * (not the full path), excluding "." and "..". Return true to continue
 * iteration, false to stop early. */
typedef bool (*gray_dir_visitor)(const char *name, void *ctx);

/* Iterate over entries in `dir_path`, calling `fn` for each one (excluding
 * "." and ".."). Returns true on success, false if the directory cannot be
 * opened. */
bool gray_scandir(const char *dir_path, gray_dir_visitor fn, void *ctx);

/* --- Self and temp locations --- */

/* Directory containing the running executable, without a trailing separator.
 * Returns a pointer to a static buffer, or NULL if it cannot be determined.
 * `argv0` is used only as a last-resort fallback. */
const char *gray_self_dir(const char *argv0);

/* System temp directory, without a trailing separator. Never NULL. */
const char *gray_temp_dir(void);

/* Build a collision-resistant path in the temp directory of the form
 * <tmp>/<prefix><pid>-<n><suffix>. Does not create the file. snprintf
 * semantics: >= n means truncation. */
int gray_temp_path(char *dst, size_t n, const char *prefix, const char *suffix);

/* Open an anonymous read/write temp file that is removed when closed. */
FILE *gray_tmpfile(void);

/* --- Process spawning ---
 *
 * All three take a NULL-terminated argv (argv[0] is the program) and wait for
 * the child. They return the child's exit code, or -1 if it could not be
 * spawned. No shell is involved, so arguments containing spaces need no
 * quoting and cannot be reinterpreted as shell syntax. */

/* Resolve argv[0] against PATH. */
int gray_spawn_path(const char *const *argv);

/* Use argv[0] verbatim as a filesystem path — no PATH search. */
int gray_spawn_exact(const char *const *argv);

/* Like gray_spawn_path, but with the child's stdout and stderr discarded. */
int gray_spawn_quiet(const char *const *argv);

/* --- Toolchain discovery --- */

/* Windows only: probe well-known MinGW/MSYS2/LLVM install locations for a C
 * compiler that is not on PATH. On success returns an absolute path in a
 * static buffer and prepends its bin directory to PATH — the compiler's
 * helper processes (cc1.exe) load their DLLs via PATH from beside the driver,
 * so the absolute path alone is not enough. Returns NULL when nothing is
 * found, and always NULL on POSIX. */
const char *gray_find_cc_fallback(void);

/* If the first token of `cmd` is a filesystem path to an executable, prepend
 * its directory to PATH so the helper processes it spawns can load DLLs that
 * live beside it (cc1.exe resolves its DLLs via PATH). No-op on POSIX, for
 * bare command names, and when the directory already leads PATH. */
void gray_ensure_tool_dir_on_path(const char *cmd);

#endif
