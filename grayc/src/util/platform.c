/*
 * platform.c — Implementation of the portability layer declared in platform.h.
 *
 * Every OS-specific include and #ifdef the compiler needs lives here. Adding a
 * platform means filling in the branches in this file, not touching main.c.
 *
 * Author:  Aristomedes (@Aristomedes)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "platform.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if GRAY_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>

#define gray_sys_access  _access
#define gray_sys_getcwd  _getcwd
#define gray_sys_unlink  _unlink
#define gray_sys_isatty  _isatty
#define gray_sys_fileno  _fileno
#define gray_sys_open    _open
#define gray_sys_close   _close
#define gray_sys_dup     _dup
#define gray_sys_dup2    _dup2
#define gray_sys_getpid  _getpid
#define GRAY_R_OK        4
#define GRAY_WRONLY_FLAG _O_WRONLY

#else /* POSIX */

#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#define gray_environ (*_NSGetEnviron())
#else
extern char **environ;
#define gray_environ environ
#endif

#define gray_sys_access  access
#define gray_sys_getcwd  getcwd
#define gray_sys_unlink  unlink
#define gray_sys_isatty  isatty
#define gray_sys_fileno  fileno
#define gray_sys_open    open
#define gray_sys_close   close
#define gray_sys_dup     dup
#define gray_sys_dup2    dup2
#define gray_sys_getpid  getpid
#define GRAY_R_OK        R_OK
#define GRAY_WRONLY_FLAG O_WRONLY

#endif

/* Matches PATH_BUF_SIZE in main.c — the static buffers handed back by
 * gray_self_dir() and gray_temp_dir() must hold anything a caller can pass on
 * to snprintf into its own PATH_BUF_SIZE buffer. */
#define GRAY_PATH_BUF 2048

/* --- Console --- */

void gray_enable_vt_mode(void) {
#if GRAY_OS_WINDOWS
    const DWORD handles[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); i++) {
        HANDLE h = GetStdHandle(handles[i]);
        DWORD mode = 0;
        if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) continue;
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

bool gray_stdout_is_tty(void) {
    return gray_sys_isatty(gray_sys_fileno(stdout)) != 0;
}

bool gray_stderr_is_tty(void) {
    return gray_sys_isatty(gray_sys_fileno(stderr)) != 0;
}

/* --- Paths --- */

bool gray_is_path_sep(char c) {
#if GRAY_OS_WINDOWS
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

const char *gray_path_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (gray_is_path_sep(*p)) base = p + 1;
    }
    return base;
}

char *gray_path_rsep(char *path) {
    char *sep = NULL;
    for (char *p = path; *p; p++) {
        if (gray_is_path_sep(*p)) sep = p;
    }
    return sep;
}

bool gray_path_is_root(const char *path) {
    if (!path || !*path) return true;

#if GRAY_OS_WINDOWS
    /* "C:", "C:\", "C:/" — a drive with no component below it. */
    if (isalpha((unsigned char)path[0]) && path[1] == ':') {
        if (path[2] == '\0') return true;
        return gray_is_path_sep(path[2]) && path[3] == '\0';
    }
    /* UNC "\\server\share" — the share itself has no parent. */
    if (gray_is_path_sep(path[0]) && gray_is_path_sep(path[1])) {
        const char *p = path + 2;
        int components = 0;
        while (*p) {
            while (*p && !gray_is_path_sep(*p)) p++;
            components++;
            while (gray_is_path_sep(*p)) p++;
        }
        return components <= 2;
    }
#endif

    /* "/" (or a run of separators) with nothing after it. */
    const char *p = path;
    while (gray_is_path_sep(*p)) p++;
    return *p == '\0';
}

int gray_path_join(char *dst, size_t n, const char *a, const char *b) {
    size_t alen = strlen(a);
    while (alen > 0 && gray_is_path_sep(a[alen - 1])) alen--;
    while (gray_is_path_sep(*b)) b++;

    if (alen == 0) {
        /* `a` was empty, or was nothing but separators (a root). */
        if (*a) return snprintf(dst, n, GRAY_PATH_SEP_STR "%s", b);
        return snprintf(dst, n, "%s", b);
    }
    if (*b == '\0') return snprintf(dst, n, "%.*s", (int)alen, a);
    return snprintf(dst, n, "%.*s" GRAY_PATH_SEP_STR "%s", (int)alen, a, b);
}

bool gray_path_is_absolute(const char *path) {
    if (!path || !*path) return false;
#if GRAY_OS_WINDOWS
    /* "C:\..." or "C:/..." */
    if (isalpha((unsigned char)path[0]) && path[1] == ':' && gray_is_path_sep(path[2])) return true;
    /* UNC "\\server\share\..." */
    if (gray_is_path_sep(path[0]) && gray_is_path_sep(path[1])) return true;
    return false;
#else
    return path[0] == '/';
#endif
}

char *gray_realpath(const char *path) {
#if GRAY_OS_WINDOWS
    /* _fullpath() happily canonicalizes paths that do not exist, whereas
     * realpath() reports ENOENT. Callers rely on NULL meaning "no such path",
     * so check first and keep the two platforms behaving alike. */
    if (gray_sys_access(path, 0) != 0) return NULL;
    return _fullpath(NULL, path, 0);
#else
    return realpath(path, NULL);
#endif
}

bool gray_realpath_into(const char *path, char *buf, size_t n) {
    char *resolved = gray_realpath(path);
    if (!resolved) return false;
    bool ok = strlen(resolved) < n;
    if (ok) memcpy(buf, resolved, strlen(resolved) + 1);
    free(resolved);
    return ok;
}

bool gray_path_equal(const char *a, const char *b) {
#if GRAY_OS_WINDOWS
    /* NTFS is case-insensitive, and either separator may appear. */
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (gray_is_path_sep(ca)) ca = GRAY_PATH_SEP;
        if (gray_is_path_sep(cb)) cb = GRAY_PATH_SEP;
        ca = (char)tolower((unsigned char)ca);
        cb = (char)tolower((unsigned char)cb);
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
#else
    return strcmp(a, b) == 0;
#endif
}

/* --- Filesystem --- */

bool gray_file_readable(const char *path) {
    return gray_sys_access(path, GRAY_R_OK) == 0;
}

#if GRAY_OS_WINDOWS
#define GRAY_STAT       struct _stat
#define gray_sys_stat   _stat
#define GRAY_IS_DIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#define GRAY_IS_FILE(m) (((m) & _S_IFMT) == _S_IFREG)
#else
#define GRAY_STAT       struct stat
#define gray_sys_stat   stat
#define GRAY_IS_DIR(m)  S_ISDIR(m)
#define GRAY_IS_FILE(m) S_ISREG(m)
#endif

bool gray_is_file(const char *path) {
    GRAY_STAT st;
    return gray_sys_stat(path, &st) == 0 && GRAY_IS_FILE(st.st_mode);
}

bool gray_is_dir(const char *path) {
    GRAY_STAT st;
    return gray_sys_stat(path, &st) == 0 && GRAY_IS_DIR(st.st_mode);
}

bool gray_remove_file(const char *path) {
    return gray_sys_unlink(path) == 0;
}

bool gray_getcwd(char *buf, size_t n) {
#if GRAY_OS_WINDOWS
    if (n > (size_t)INT_MAX) n = (size_t)INT_MAX;
    return _getcwd(buf, (int)n) != NULL;
#else
    return getcwd(buf, n) != NULL;
#endif
}

bool gray_scandir(const char *dir_path, gray_dir_visitor fn, void *ctx) {
#if GRAY_OS_WINDOWS
    char pattern[GRAY_PATH_BUF];
    int len = snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    if (len < 0 || (size_t)len >= sizeof(pattern)) return false;

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    do {
        const char *name = fd.cFileName;
        if (name[0] == '.' && (name[1] == '\0' ||
            (name[1] == '.' && name[2] == '\0')))
            continue;
        if (!fn(name, ctx)) break;
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return true;
#else
    DIR *d = opendir(dir_path);
    if (!d) return false;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
            (name[1] == '.' && name[2] == '\0')))
            continue;
        if (!fn(name, ctx)) break;
    }

    closedir(d);
    return true;
#endif
}

bool gray_write_file_mode(const char *path, const void *data, size_t len) {
#if GRAY_OS_WINDOWS
    FILE *f = fopen(path, "wb");
    if (!f) return false;
#else
    /* Create with explicit 0644 rather than letting the process umask decide. */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return false;
    }
#endif
    bool ok = len == 0 || fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    return ok;
}

/* --- Self and temp locations --- */

const char *gray_self_dir(const char *argv0) {
    static char buf[GRAY_PATH_BUF];

#if GRAY_OS_WINDOWS
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
    /* 0 is failure; sizeof(buf) means the path was truncated. */
    if (len > 0 && len < sizeof(buf)) {
        char *sep = gray_path_rsep(buf);
        if (sep) {
            *sep = '\0';
            return buf;
        }
    }
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        char *resolved = gray_realpath(buf);
        if (resolved) {
            snprintf(buf, sizeof(buf), "%s", resolved);
            free(resolved);
            char *sep = gray_path_rsep(buf);
            if (sep) {
                *sep = '\0';
                return buf;
            }
        }
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        char *sep = gray_path_rsep(buf);
        if (sep) {
            *sep = '\0';
            return buf;
        }
    }
#endif

    /* Fallback: resolve argv[0] against the filesystem. */
    if (argv0) {
        char *resolved = gray_realpath(argv0);
        if (resolved) {
            snprintf(buf, sizeof(buf), "%s", resolved);
            free(resolved);
            char *sep = gray_path_rsep(buf);
            if (sep) {
                *sep = '\0';
                return buf;
            }
        }
    }

    return NULL;
}

const char *gray_temp_dir(void) {
    static char buf[GRAY_PATH_BUF];
    static bool resolved = false;
    if (resolved) return buf;

#if GRAY_OS_WINDOWS
    DWORD len = GetTempPathA((DWORD)sizeof(buf), buf);
    if (len == 0 || len >= sizeof(buf)) {
        snprintf(buf, sizeof(buf), "."); /* last resort: the current directory */
    } else {
        /* GetTempPathA always leaves a trailing backslash; drop it. */
        while (len > 0 && gray_is_path_sep(buf[len - 1])) buf[--len] = '\0';
        if (len == 0) snprintf(buf, sizeof(buf), ".");
    }
#else
    const char *env = getenv("TMPDIR");
    if (!env || !*env) env = "/tmp";
    size_t len = strlen(env);
    while (len > 1 && gray_is_path_sep(env[len - 1])) len--;
    snprintf(buf, sizeof(buf), "%.*s", (int)len, env);
#endif

    resolved = true;
    return buf;
}

int gray_temp_path(char *dst, size_t n, const char *prefix, const char *suffix) {
    static unsigned counter = 0;
    char name[256];
    snprintf(name, sizeof(name), "%s%d-%u%s", prefix, (int)gray_sys_getpid(), counter++,
        suffix ? suffix : "");
    return gray_path_join(dst, n, gray_temp_dir(), name);
}

FILE *gray_tmpfile(void) {
#if GRAY_OS_WINDOWS
    /* tmpfile() on Windows creates its file in the root of the current drive,
     * which fails without administrator rights. Place it in the real temp
     * directory instead; the "D" mode flag keeps tmpfile()'s delete-on-close
     * contract. */
    char dir[GRAY_PATH_BUF];
    char path[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s", gray_temp_dir());
    if (GetTempFileNameA(dir, "gray", 0, path) == 0) return NULL;
    FILE *f = fopen(path, "w+bD");
    if (!f) DeleteFileA(path); /* GetTempFileNameA already created it */
    return f;
#else
    return tmpfile();
#endif
}

/* --- Process spawning --- */

#if GRAY_OS_WINDOWS

static int spawn_impl(const char *const *argv, bool search_path) {
    intptr_t rc = search_path ? _spawnvp(_P_WAIT, argv[0], argv)
                              : _spawnv(_P_WAIT, argv[0], argv);
    /* _P_WAIT yields the child's exit code directly; -1 means it never ran. */
    return rc == -1 ? -1 : (int)rc;
}

#else

static int spawn_impl(const char *const *argv, bool search_path) {
    pid_t pid = 0;
    /* posix_spawn takes a non-const argv purely for historical reasons; it does
     * not modify the strings. */
    char *const *args = (char *const *)argv;

    int err = search_path ? posix_spawnp(&pid, argv[0], NULL, NULL, args, gray_environ)
                          : posix_spawn(&pid, argv[0], NULL, NULL, args, gray_environ);
    if (err != 0) {
        errno = err;
        return -1;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

#endif

int gray_spawn_path(const char *const *argv) {
    return spawn_impl(argv, true);
}

int gray_spawn_exact(const char *const *argv) {
    return spawn_impl(argv, false);
}

int gray_spawn_quiet(const char *const *argv) {
    int devnull = gray_sys_open(GRAY_NULL_DEVICE, GRAY_WRONLY_FLAG);
    if (devnull < 0) return gray_spawn_path(argv);

    fflush(stdout);
    fflush(stderr);

    int saved_out = gray_sys_dup(1);
    int saved_err = gray_sys_dup(2);
    gray_sys_dup2(devnull, 1);
    gray_sys_dup2(devnull, 2);

    int rc = spawn_impl(argv, true);

    if (saved_out >= 0) {
        gray_sys_dup2(saved_out, 1);
        gray_sys_close(saved_out);
    }
    if (saved_err >= 0) {
        gray_sys_dup2(saved_err, 2);
        gray_sys_close(saved_err);
    }
    gray_sys_close(devnull);
    return rc;
}

/* --- Toolchain discovery --- */

void gray_ensure_tool_dir_on_path(const char *cmd) {
#if GRAY_OS_WINDOWS
    /* First whitespace-delimited token — the same split argv_push_command
     * applies to multi-word compiler commands. */
    char head[GRAY_PATH_BUF];
    size_t n = strcspn(cmd, " \t");
    if (n == 0 || n >= sizeof(head)) return;
    memcpy(head, cmd, n);
    head[n] = '\0';

    char *sep = gray_path_rsep(head);
    if (!sep) return; /* bare command name — PATH already resolves it */
    *sep = '\0';
    if (!*head) return;

    const char *old_path = getenv("PATH");
    if (!old_path) old_path = "";

    /* Skip when already the front entry so repeat calls do not grow PATH. */
    size_t dir_len = strlen(head);
    if (strncmp(old_path, head, dir_len) == 0 &&
        (old_path[dir_len] == ';' || old_path[dir_len] == '\0')) {
        return;
    }

    size_t new_len = dir_len + 1 + strlen(old_path) + 1;
    char *new_path = malloc(new_len);
    if (!new_path) return;
    snprintf(new_path, new_len, "%s;%s", head, old_path);
    /* _putenv_s updates the CRT's view; SetEnvironmentVariableA updates the
     * block child processes inherit. The two are separate on Windows, so
     * both are needed (same pattern as gray_os_set_env). */
    _putenv_s("PATH", new_path);
    SetEnvironmentVariableA("PATH", new_path);
    free(new_path);
#else
    (void)cmd;
#endif
}

const char *gray_find_cc_fallback(void) {
#if GRAY_OS_WINDOWS
    /* The same well-known install locations scripts/common.ps1 probes, in the
     * same order. MSYS2 and the common MinGW distributions deliberately do not
     * add themselves to PATH, so a working toolchain that grayc cannot see is
     * the common case on Windows, not an edge case. */
    static const char *const fixed[] = {
        "C:\\msys64\\ucrt64\\bin\\gcc.exe",
        "C:\\msys64\\mingw64\\bin\\gcc.exe",
        "C:\\msys64\\clang64\\bin\\clang.exe",
        "C:\\mingw64\\bin\\gcc.exe",
        "C:\\ProgramData\\mingw64\\mingw64\\bin\\gcc.exe",
        NULL, /* %ProgramFiles%\LLVM\bin\clang.exe, built below */
    };
    static char found[GRAY_PATH_BUF];
    char candidate[GRAY_PATH_BUF];

    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        if (fixed[i]) {
            snprintf(candidate, sizeof(candidate), "%s", fixed[i]);
        } else {
            const char *pf = getenv("ProgramFiles");
            if (!pf || !*pf) continue;
            if (gray_path_join(candidate, sizeof(candidate), pf,
                               "LLVM\\bin\\clang.exe") >= (int)sizeof(candidate)) {
                continue;
            }
        }
        if (!gray_file_readable(candidate)) continue;

        /* cc1.exe and the driver's other helpers load their DLLs via PATH
         * from beside the compiler binary, so spawning by absolute path alone
         * fails with exit 1 and no diagnostic. Prepend the bin directory;
         * children of this process inherit it, which is the point. */
        gray_ensure_tool_dir_on_path(candidate);

        const char *probe[] = {candidate, "--version", NULL};
        if (gray_spawn_quiet(probe) == 0) {
            snprintf(found, sizeof(found), "%s", candidate);
            return found;
        }
        /* Probe failed; the dead PATH entry left behind is harmless. */
    }
    return NULL;
#else
    return NULL;
#endif
}
