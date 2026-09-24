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
#include "xalloc.h"

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
#define GRAY_X_OK        0 /* _access rejects the execute mode; Windows has no exec bit */
#define GRAY_PATH_LIST_SEPARATOR ';'
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
#define GRAY_X_OK        X_OK
#define GRAY_PATH_LIST_SEPARATOR ':'
#define GRAY_WRONLY_FLAG O_WRONLY

#endif

/* Matches PATH_BUFFER_SIZE in main.c — the static buffers handed back by
 * gray_self_directory() and gray_temporary_directory() must hold anything a caller can pass on
 * to snprintf into its own PATH_BUFFER_SIZE buffer. */
#define GRAY_PATH_BUFFER_SIZE 2048

/* --- Strings --- */

char *gray_strndup(const char *string, size_t max_length) {
    size_t length = 0;
    while (length < max_length && string[length] != '\0') length++;
    char *out = malloc(length + 1);
    if (!out) return NULL;
    memcpy(out, string, length);
    out[length] = '\0';
    return out;
}

/* --- Console --- */

void gray_enable_virtual_terminal_mode(void) {
#if GRAY_OS_WINDOWS
    const DWORD handles[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); i++) {
        HANDLE handle = GetStdHandle(handles[i]);
        DWORD mode = 0;
        if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) continue;
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

bool gray_stdout_is_terminal(void) {
    return gray_sys_isatty(gray_sys_fileno(stdout)) != 0;
}

bool gray_stderr_is_terminal(void) {
    return gray_sys_isatty(gray_sys_fileno(stderr)) != 0;
}

/* --- Paths --- */

bool gray_is_path_separator(char character) {
#if GRAY_OS_WINDOWS
    return character == '/' || character == '\\';
#else
    return character == '/';
#endif
}

const char *gray_path_basename(const char *path) {
    const char *base = path;
    for (const char *cursor = path; *cursor; cursor++) {
        if (gray_is_path_separator(*cursor)) base = cursor + 1;
    }
    return base;
}

char *gray_path_last_separator(char *path) {
    char *separator = NULL;
    for (char *cursor = path; *cursor; cursor++) {
        if (gray_is_path_separator(*cursor)) separator = cursor;
    }
    return separator;
}

bool gray_path_is_root(const char *path) {
    if (!path || !*path) return true;

#if GRAY_OS_WINDOWS
    /* "C:", "C:\", "C:/" — a drive with no component below it. */
    if (isalpha((unsigned char)path[0]) && path[1] == ':') {
        if (path[2] == '\0') return true;
        return gray_is_path_separator(path[2]) && path[3] == '\0';
    }
    /* UNC "\\server\share" — the share itself has no parent. */
    if (gray_is_path_separator(path[0]) && gray_is_path_separator(path[1])) {
        const char *cursor = path + 2;
        int components = 0;
        while (*cursor) {
            while (*cursor && !gray_is_path_separator(*cursor)) cursor++;
            components++;
            while (gray_is_path_separator(*cursor)) cursor++;
        }
        return components <= 2;
    }
#endif

    /* "/" (or a run of separators) with nothing after it. */
    const char *cursor = path;
    while (gray_is_path_separator(*cursor)) cursor++;
    return *cursor == '\0';
}

int gray_path_join(char *destination, size_t destination_size, const char *base, const char *tail) {
    size_t base_length = strlen(base);
    while (base_length > 0 && gray_is_path_separator(base[base_length - 1])) base_length--;
    while (gray_is_path_separator(*tail)) tail++;

    if (base_length == 0) {
        /* `base` was empty, or was nothing but separators (a root). */
        if (*base) return snprintf(destination, destination_size, GRAY_PATH_SEPARATOR_STRING "%s", tail);
        return snprintf(destination, destination_size, "%s", tail);
    }
    if (*tail == '\0') return snprintf(destination, destination_size, "%.*s", (int)base_length, base);
    return snprintf(destination, destination_size, "%.*s" GRAY_PATH_SEPARATOR_STRING "%s", (int)base_length, base, tail);
}

bool gray_path_is_absolute(const char *path) {
    if (!path || !*path) return false;
#if GRAY_OS_WINDOWS
    /* "C:\..." or "C:/..." */
    if (isalpha((unsigned char)path[0]) && path[1] == ':' && gray_is_path_separator(path[2])) return true;
    /* UNC "\\server\share\..." */
    if (gray_is_path_separator(path[0]) && gray_is_path_separator(path[1])) return true;
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

bool gray_realpath_into(const char *path, char *resolved_buffer, size_t resolved_buffer_size) {
    char *resolved = gray_realpath(path);
    if (!resolved) return false;
    bool can_fit = strlen(resolved) < resolved_buffer_size;
    if (can_fit) memcpy(resolved_buffer, resolved, strlen(resolved) + 1);
    free(resolved);
    return can_fit;
}

bool gray_path_equal(const char *left, const char *right) {
#if GRAY_OS_WINDOWS
    /* NTFS is case-insensitive, and either separator may appear. */
    for (;; left++, right++) {
        char left_character = *left, right_character = *right;
        if (gray_is_path_separator(left_character)) left_character = GRAY_PATH_SEPARATOR;
        if (gray_is_path_separator(right_character)) right_character = GRAY_PATH_SEPARATOR;
        left_character = (char)tolower((unsigned char)left_character);
        right_character = (char)tolower((unsigned char)right_character);
        if (left_character != right_character) return false;
        if (left_character == '\0') return true;
    }
#else
    return strcmp(left, right) == 0;
#endif
}

/* --- Filesystem --- */

bool gray_file_readable(const char *path) {
    return gray_sys_access(path, GRAY_R_OK) == 0;
}

#if GRAY_OS_WINDOWS
#define GRAY_STAT       struct _stat
#define gray_sys_stat   _stat
#define GRAY_IS_DIRECTORY(mode)  (((mode) & _S_IFMT) == _S_IFDIR)
#define GRAY_IS_FILE(mode) (((mode) & _S_IFMT) == _S_IFREG)
#else
#define GRAY_STAT       struct stat
#define gray_sys_stat   stat
#define GRAY_IS_DIRECTORY(mode)  S_ISDIR(mode)
#define GRAY_IS_FILE(mode) S_ISREG(mode)
#endif

bool gray_is_file(const char *path) {
    GRAY_STAT file_status;
    return gray_sys_stat(path, &file_status) == 0 && GRAY_IS_FILE(file_status.st_mode);
}

bool gray_is_directory(const char *path) {
    GRAY_STAT file_status;
    return gray_sys_stat(path, &file_status) == 0 && GRAY_IS_DIRECTORY(file_status.st_mode);
}

bool gray_remove_file(const char *path) {
    return gray_sys_unlink(path) == 0;
}

bool gray_getcwd(char *directory_buffer, size_t directory_buffer_size) {
#if GRAY_OS_WINDOWS
    if (directory_buffer_size > (size_t)INT_MAX) directory_buffer_size = (size_t)INT_MAX;
    return _getcwd(directory_buffer, (int)directory_buffer_size) != NULL;
#else
    return getcwd(directory_buffer, directory_buffer_size) != NULL;
#endif
}

bool gray_scandir(const char *directory_path, gray_directory_visitor visit, void *context) {
#if GRAY_OS_WINDOWS
    char pattern[GRAY_PATH_BUFFER_SIZE];
    int length = snprintf(pattern, sizeof(pattern), "%s\\*", directory_path);
    if (length < 0 || (size_t)length >= sizeof(pattern)) return false;

    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(pattern, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) return false;

    do {
        const char *name = find_data.cFileName;
        if (name[0] == '.' && (name[1] == '\0' ||
            (name[1] == '.' && name[2] == '\0')))
            continue;
        if (!visit(name, context)) break;
    } while (FindNextFileA(find_handle, &find_data));

    FindClose(find_handle);
    return true;
#else
    DIR *directory = opendir(directory_path);
    if (!directory) return false;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        const char *name = entry->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
            (name[1] == '.' && name[2] == '\0')))
            continue;
        if (!visit(name, context)) break;
    }

    closedir(directory);
    return true;
#endif
}

bool gray_write_file_mode(const char *path, const void *data, size_t length) {
#if GRAY_OS_WINDOWS
    FILE *file = fopen(path, "wb");
    if (!file) return false;
#else
    /* Create with explicit 0644 rather than letting the process umask decide. */
    int file_descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_descriptor < 0) return false;
    FILE *file = fdopen(file_descriptor, "wb");
    if (!file) {
        close(file_descriptor);
        return false;
    }
#endif
    bool was_written = length == 0 || fwrite(data, 1, length, file) == length;
    if (fclose(file) != 0) was_written = false;
    return was_written;
}

/* --- Self and temp locations --- */

const char *gray_self_directory(const char *argv0) {
    static char directory_buffer[GRAY_PATH_BUFFER_SIZE];

#if GRAY_OS_WINDOWS
    DWORD length = GetModuleFileNameA(NULL, directory_buffer, (DWORD)sizeof(directory_buffer));
    /* 0 is failure; sizeof(directory_buffer) means the path was truncated. */
    if (length > 0 && length < sizeof(directory_buffer)) {
        char *separator = gray_path_last_separator(directory_buffer);
        if (separator) {
            *separator = '\0';
            return directory_buffer;
        }
    }
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)sizeof(directory_buffer);
    if (_NSGetExecutablePath(directory_buffer, &size) == 0) {
        char *resolved = gray_realpath(directory_buffer);
        if (resolved) {
            snprintf(directory_buffer, sizeof(directory_buffer), "%s", resolved);
            free(resolved);
            char *separator = gray_path_last_separator(directory_buffer);
            if (separator) {
                *separator = '\0';
                return directory_buffer;
            }
        }
    }
#elif defined(__linux__)
    ssize_t length = readlink("/proc/self/exe", directory_buffer, sizeof(directory_buffer) - 1);
    if (length > 0) {
        directory_buffer[length] = '\0';
        char *separator = gray_path_last_separator(directory_buffer);
        if (separator) {
            *separator = '\0';
            return directory_buffer;
        }
    }
#endif

    /* Fallback: resolve argv[0] against the filesystem. */
    if (argv0) {
        char *resolved = gray_realpath(argv0);
        if (resolved) {
            snprintf(directory_buffer, sizeof(directory_buffer), "%s", resolved);
            free(resolved);
            char *separator = gray_path_last_separator(directory_buffer);
            if (separator) {
                *separator = '\0';
                return directory_buffer;
            }
        }
    }

    return NULL;
}

const char *gray_temporary_directory(void) {
    static char directory_buffer[GRAY_PATH_BUFFER_SIZE];
    static bool is_resolved = false;
    if (is_resolved) return directory_buffer;

#if GRAY_OS_WINDOWS
    DWORD length = GetTempPathA((DWORD)sizeof(directory_buffer), directory_buffer);
    if (length == 0 || length >= sizeof(directory_buffer)) {
        snprintf(directory_buffer, sizeof(directory_buffer), "."); /* last resort: the current directory */
    } else {
        /* GetTempPathA always leaves a trailing backslash; drop it. */
        while (length > 0 && gray_is_path_separator(directory_buffer[length - 1])) directory_buffer[--length] = '\0';
        if (length == 0) snprintf(directory_buffer, sizeof(directory_buffer), ".");
    }
#else
    const char *temporary_directory_variable = getenv("TMPDIR");
    if (!temporary_directory_variable || !*temporary_directory_variable) temporary_directory_variable = "/tmp";
    size_t length = strlen(temporary_directory_variable);
    while (length > 1 && gray_is_path_separator(temporary_directory_variable[length - 1])) length--;
    snprintf(directory_buffer, sizeof(directory_buffer), "%.*s", (int)length, temporary_directory_variable);
#endif

    is_resolved = true;
    return directory_buffer;
}

int gray_temporary_path(char *destination, size_t destination_size, const char *prefix, const char *suffix) {
    static unsigned counter = 0;
    char name[256];
    snprintf(name, sizeof(name), "%s%d-%u%s", prefix, (int)gray_sys_getpid(), counter++,
        suffix ? suffix : "");
    return gray_path_join(destination, destination_size, gray_temporary_directory(), name);
}

FILE *gray_tmpfile(void) {
#if GRAY_OS_WINDOWS
    /* tmpfile() on Windows creates its file in the root of the current drive,
     * which fails without administrator rights. Place it in the real temp
     * directory instead; the "D" mode flag keeps tmpfile()'s delete-on-close
     * contract. */
    char directory[GRAY_PATH_BUFFER_SIZE];
    char path[MAX_PATH];
    snprintf(directory, sizeof(directory), "%s", gray_temporary_directory());
    if (GetTempFileNameA(directory, "gray", 0, path) == 0) return NULL;
    FILE *file = fopen(path, "w+bD");
    if (!file) DeleteFileA(path); /* GetTempFileNameA already created it */
    return file;
#else
    return tmpfile();
#endif
}

/* --- Process spawning --- */

#if GRAY_OS_WINDOWS

static int spawn_child(const char *const *argv, bool search_path, int *termination_signal) {
    (void)termination_signal;
    intptr_t exit_code = search_path ? _spawnvp(_P_WAIT, argv[0], argv)
                              : _spawnv(_P_WAIT, argv[0], argv);
    /* _P_WAIT yields the child's exit code directly; -1 means it never ran. */
    return exit_code == -1 ? -1 : (int)exit_code;
}

#else

static int spawn_child(const char *const *argv, bool search_path, int *termination_signal) {
    pid_t process_id = 0;
    /* posix_spawn takes a non-const argv purely for historical reasons; it does
     * not modify the strings. */
    char *const *spawn_arguments = (char *const *)argv;

    int spawn_error = search_path ? posix_spawnp(&process_id, argv[0], NULL, NULL, spawn_arguments, gray_environ)
                          : posix_spawn(&process_id, argv[0], NULL, NULL, spawn_arguments, gray_environ);
    if (spawn_error != 0) {
        errno = spawn_error;
        return -1;
    }

    int status = 0;
    while (waitpid(process_id, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        if (termination_signal) *termination_signal = WTERMSIG(status);
        return 128 + WTERMSIG(status);
    }
    return -1;
}

#endif

int gray_spawn_path(const char *const *argv) {
    return spawn_child(argv, true, NULL);
}

int gray_spawn_exact(const char *const *argv, int *termination_signal) {
    if (termination_signal) *termination_signal = 0;
    return spawn_child(argv, false, termination_signal);
}

int gray_spawn_quiet(const char *const *argv) {
    int null_device = gray_sys_open(GRAY_NULL_DEVICE, GRAY_WRONLY_FLAG);
    if (null_device < 0) return gray_spawn_path(argv);

    fflush(stdout);
    fflush(stderr);

    int saved_stdout = gray_sys_dup(1);
    int saved_stderr = gray_sys_dup(2);
    gray_sys_dup2(null_device, 1);
    gray_sys_dup2(null_device, 2);

    int exit_code = spawn_child(argv, true, NULL);

    if (saved_stdout >= 0) {
        gray_sys_dup2(saved_stdout, 1);
        gray_sys_close(saved_stdout);
    }
    if (saved_stderr >= 0) {
        gray_sys_dup2(saved_stderr, 2);
        gray_sys_close(saved_stderr);
    }
    gray_sys_close(null_device);
    return exit_code;
}

int gray_spawn_capture_stdout(const char *const *argv, FILE *capture) {
    int null_device = gray_sys_open(GRAY_NULL_DEVICE, GRAY_WRONLY_FLAG);
    if (null_device < 0) return -1;

    fflush(stdout);
    fflush(stderr);
    fflush(capture);
    int capture_descriptor = fileno(capture);

    int saved_stdout = gray_sys_dup(1);
    int saved_stderr = gray_sys_dup(2);
    gray_sys_dup2(capture_descriptor, 1);
    gray_sys_dup2(null_device, 2);

    int exit_code = spawn_child(argv, true, NULL);
    fflush(NULL);

    if (saved_stdout >= 0) {
        gray_sys_dup2(saved_stdout, 1);
        gray_sys_close(saved_stdout);
    }
    if (saved_stderr >= 0) {
        gray_sys_dup2(saved_stderr, 2);
        gray_sys_close(saved_stderr);
    }
    gray_sys_close(null_device);
    return exit_code;
}

int gray_spawn_capture_stderr(const char *const *argv, FILE *capture) {
    int null_device = gray_sys_open(GRAY_NULL_DEVICE, GRAY_WRONLY_FLAG);
    if (null_device < 0) return -1;

    fflush(stdout);
    fflush(stderr);
    fflush(capture);
    int capture_descriptor = fileno(capture);

    int saved_stdout = gray_sys_dup(1);
    int saved_stderr = gray_sys_dup(2);
    gray_sys_dup2(null_device, 1);
    gray_sys_dup2(capture_descriptor, 2);

    int exit_code = spawn_child(argv, true, NULL);
    fflush(NULL);

    if (saved_stdout >= 0) {
        gray_sys_dup2(saved_stdout, 1);
        gray_sys_close(saved_stdout);
    }
    if (saved_stderr >= 0) {
        gray_sys_dup2(saved_stderr, 2);
        gray_sys_close(saved_stderr);
    }
    gray_sys_close(null_device);
    return exit_code;
}

/* --- Toolchain discovery --- */

void gray_ensure_tool_directory_on_path(const char *command) {
#if GRAY_OS_WINDOWS
    /* First whitespace-delimited token — the same split argv_push_command
     * applies to multi-word compiler commands. */
    char head[GRAY_PATH_BUFFER_SIZE];
    size_t head_length = strcspn(command, " \t");
    if (head_length == 0 || head_length >= sizeof(head)) return;
    memcpy(head, command, head_length);
    head[head_length] = '\0';

    char *separator = gray_path_last_separator(head);
    if (!separator) return; /* bare command name — PATH already resolves it */
    *separator = '\0';
    if (!*head) return;

    const char *old_path = getenv("PATH");
    if (!old_path) old_path = "";

    /* Skip when already the front entry so repeat calls do not grow PATH. */
    size_t directory_length = strlen(head);
    if (strncmp(old_path, head, directory_length) == 0 &&
        (old_path[directory_length] == ';' || old_path[directory_length] == '\0')) {
        return;
    }

    size_t new_length = directory_length + 1 + strlen(old_path) + 1;
    char *new_path = malloc(new_length);
    if (!new_path) return;
    snprintf(new_path, new_length, "%s;%s", head, old_path);
    /* _putenv_s updates the CRT's view; SetEnvironmentVariableA updates the
     * block child processes inherit. The two are separate on Windows, so
     * both are needed (same pattern as gray_os_set_env). */
    _putenv_s("PATH", new_path);
    SetEnvironmentVariableA("PATH", new_path);
    free(new_path);
#else
    (void)command;
#endif
}

static bool file_is_executable(const char *path) {
    if (gray_sys_access(path, GRAY_X_OK) == 0) return true;
#if GRAY_OS_WINDOWS
    char executable_path[GRAY_PATH_BUFFER_SIZE];
    int written_length = snprintf(executable_path, sizeof(executable_path), "%s.exe", path);
    if (written_length > 0 && written_length < (int)sizeof(executable_path) && gray_sys_access(executable_path, GRAY_X_OK) == 0) return true;
#endif
    return false;
}

bool gray_command_on_path(const char *name) {
    if (!name || !*name) return false;

    /* An explicit path (contains a separator) is checked as given. */
    for (const char *cursor = name; *cursor; cursor++) {
        if (gray_is_path_separator(*cursor)) return file_is_executable(name);
    }

    const char *path = getenv("PATH");
    if (!path) return false;

    char probe[GRAY_PATH_BUFFER_SIZE];
    while (*path) {
        const char *separator = path;
        while (*separator && *separator != GRAY_PATH_LIST_SEPARATOR) separator++;
        size_t directory_length = (size_t)(separator - path);

        if (directory_length == 0) {
            /* An empty PATH entry means the current directory. */
            if (snprintf(probe, sizeof(probe), "%s", name) < (int)sizeof(probe) &&
                file_is_executable(probe))
                return true;
        } else if (directory_length < sizeof(probe)) {
            char directory[GRAY_PATH_BUFFER_SIZE];
            memcpy(directory, path, directory_length);
            directory[directory_length] = '\0';
            if (gray_path_join(probe, sizeof(probe), directory, name) < (int)sizeof(probe) &&
                file_is_executable(probe))
                return true;
        }
        path = *separator ? separator + 1 : separator;
    }
    return false;
}

const char *gray_find_c_compiler_fallback(void) {
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
    static char found[GRAY_PATH_BUFFER_SIZE];
    char candidate[GRAY_PATH_BUFFER_SIZE];

    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        if (fixed[i]) {
            snprintf(candidate, sizeof(candidate), "%s", fixed[i]);
        } else {
            const char *program_files = getenv("ProgramFiles");
            if (!program_files || !*program_files) continue;
            if (gray_path_join(candidate, sizeof(candidate), program_files,
                               "LLVM\\bin\\clang.exe") >= (int)sizeof(candidate)) {
                continue;
            }
        }
        if (!gray_file_readable(candidate)) continue;

        /* cc1.exe and the driver's other helpers load their DLLs via PATH
         * from beside the compiler binary, so spawning by absolute path alone
         * fails with exit 1 and no diagnostic. Prepend the bin directory;
         * children of this process inherit it, which is the point. */
        gray_ensure_tool_directory_on_path(candidate);

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

/* `report` prints why the file could not be opened. A caller that says so
 * itself passes false: the import path reports E6002 against the import
 * statement, and printed both the diagnostic and a raw duplicate of it. */
char *gray_read_file(const char *path, bool report) {
    /* Fast path for regular (seekable) files. */
    char *fast = read_file_to_string(path);
    if (fast) return fast;

    /* Streaming fallback for non-seekable inputs (pipes, FIFOs, /dev/stdin). */
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (report) {
            fprintf(stderr, "gray: cannot open '%s': ", path);
            perror("");
        }
        return NULL;
    }
    clearerr(file);
    size_t capacity = 4096;
    size_t length = 0;
    char *contents = malloc(capacity);
    if (!contents) {
        fprintf(stderr, "gray: out of memory\n");
        fclose(file);
        return NULL;
    }
    for (;;) {
        if (length == capacity) {
            size_t new_capacity = capacity * 2;
            char *grown_contents = realloc(contents, new_capacity);
            if (!grown_contents) {
                free(contents);
                fclose(file);
                fprintf(stderr, "gray: out of memory\n");
                return NULL;
            }
            contents = grown_contents;
            capacity = new_capacity;
        }
        size_t bytes_read = fread(contents + length, 1, capacity - length, file);
        if (bytes_read == 0) break;
        length += bytes_read;
    }
    if (length + 1 > capacity) {
        char *trimmed_contents = realloc(contents, length + 1);
        if (!trimmed_contents) {
            free(contents);
            fclose(file);
            fprintf(stderr, "gray: out of memory\n");
            return NULL;
        }
        contents = trimmed_contents;
    }
    contents[length] = '\0';
    fclose(file);
    return contents;
}
