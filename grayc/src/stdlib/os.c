/*
 * os.c — Implementation of the os stdlib module.
 * Provides access to command-line args, environment variables,
 * working directory, hostname, process execution, and signal handling.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

/* Must precede every system header: exposes sysconf(_SC_NPROCESSORS_ONLN),
 * which <unistd.h> hides under a strict _POSIX_C_SOURCE. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "os.h"
#include "../runtime/platform_rt.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#if GRAY_RUNTIME_WINDOWS
#include "../runtime/win32.h"
#include <direct.h>
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#endif

#define GRAY_HOSTNAME_BUFFER_SIZE 256
#define GRAY_EXEC_OUTPUT_MAX (64 * 1024 * 1024) /* 64 MiB per stream */

/* Grow an arena-backed byte buffer so it can take `add` more bytes on top of
 * `total`. The arena has no realloc, so growth is allocate-and-copy; the
 * `* 2 + add` step keeps the doubling from being defeated by a chunk larger
 * than the current capacity. */
#define EXEC_BUFFER_GROWTH(arena, buffer, total, capacity, additional) \
    do { \
        if ((total) + (size_t)(additional) > (capacity)) { \
            size_t exec_buffer_capacity_ = (capacity) * 2 + (size_t)(additional); \
            char *exec_buffer_grown_ = gray_arena_alloc_uninitialized((arena), exec_buffer_capacity_); \
            memcpy(exec_buffer_grown_, (buffer), (total)); \
            (buffer) = exec_buffer_grown_; \
            (capacity) = exec_buffer_capacity_; \
        } \
    } while (0)

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int os_argument_count = 0;
static char **os_argument_values = NULL;

void gray_os_init(int argc, char **argv) {
    os_argument_count = argc;
    os_argument_values = argv;
}

GrayArray gray_os_args(GrayArena *arena) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), os_argument_count > 0 ? os_argument_count : 1, GRAY_ELEM_STRING);
    for (int i = 0; i < os_argument_count; i++) {
        GrayString argument_string = gray_string_new(arena, os_argument_values[i], (int32_t)strlen(os_argument_values[i]));
        GRAY_ARRAY_PUSH(arena, &array, &argument_string);
    }
    return array;
}

GrayString gray_os_get_env(GrayArena *arena, GrayString name) {
    const char *value = getenv(name.data);
    if (!value) return gray_string_lit("");
    return gray_string_new(arena, value, (int32_t)strlen(value));
}

GrayOsLookupEnvResult gray_os_lookup_env(GrayArena *arena, GrayString name) {
    const char *value = getenv(name.data);
    if (!value) return (GrayOsLookupEnvResult){gray_string_lit(""), false};
    return (GrayOsLookupEnvResult){gray_string_new(arena, value, (int32_t)strlen(value)), true};
}

GrayArray gray_os_environ(GrayArena *arena) {
#if GRAY_RUNTIME_WINDOWS
    extern char **_environ;
    char **envp = _environ;
#else
    extern char **environ;
    char **envp = environ;
#endif
    int count = 0;
    for (char **entry = envp; entry && *entry; entry++) count++;
    GrayArray array = gray_array_new(arena, sizeof(GrayString), count > 0 ? count : 1, GRAY_ELEM_STRING);
    for (int i = 0; i < count; i++) {
        GrayString entry_string = gray_string_new(arena, envp[i], (int32_t)strlen(envp[i]));
        GRAY_ARRAY_PUSH(arena, &array, &entry_string);
    }
    return array;
}

void gray_os_set_env(GrayString name, GrayString value) {
#if GRAY_RUNTIME_WINDOWS
    /* _putenv_s updates the CRT's view; SetEnvironmentVariableA updates the
     * block that child processes inherit. The two are separate on Windows, so
     * both are needed for get_env and exec to agree. */
    _putenv_s(name.data, value.data);
    SetEnvironmentVariableA(name.data, value.data);
#else
    setenv(name.data, value.data, 1);
#endif
}

void gray_os_unset_env(GrayString name) {
#if GRAY_RUNTIME_WINDOWS
    _putenv_s(name.data, "");
    SetEnvironmentVariableA(name.data, NULL);
#else
    unsetenv(name.data);
#endif
}

GrayString gray_os_cwd(GrayArena *arena) {
    char buffer[PATH_MAX];
#if GRAY_RUNTIME_WINDOWS
    if (_getcwd(buffer, (int)sizeof(buffer))) {
#else
    if (getcwd(buffer, sizeof(buffer))) {
#endif
        return gray_string_new(arena, buffer, (int32_t)strlen(buffer));
    }
    return gray_string_lit("");
}

GrayString gray_os_home_dir(GrayArena *arena) {
#if GRAY_RUNTIME_WINDOWS
    const char *profile_directory = getenv("USERPROFILE");
#else
    const char *profile_directory = getenv("HOME");
#endif
    if (!profile_directory) return gray_string_lit("");
    return gray_string_new(arena, profile_directory, (int32_t)strlen(profile_directory));
}

GrayString gray_os_hostname(GrayArena *arena) {
    char buffer[GRAY_HOSTNAME_BUFFER_SIZE];
#if GRAY_RUNTIME_WINDOWS
    /* GetComputerNameEx avoids requiring Winsock to be started just for a name. */
    DWORD length = (DWORD)sizeof(buffer);
    if (GetComputerNameExA(ComputerNameDnsHostname, buffer, &length)) {
        return gray_string_new(arena, buffer, (int32_t)length);
    }
#else
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        return gray_string_new(arena, buffer, (int32_t)strlen(buffer));
    }
#endif
    return gray_string_lit("");
}

int64_t gray_os_current_os(void) {
#ifdef __APPLE__
    return 0; /* MAC_OS */
#elif defined(__linux__)
    return 1; /* LINUX */
#elif defined(_WIN32)
    return 2; /* WINDOWS */
#else
    return 3; /* OTHER */
#endif
}

GrayString gray_os_arch(void) {
#if defined(__aarch64__) || defined(__arm64__)
    return gray_string_lit("arm64");
#elif defined(__x86_64__) || defined(__amd64__)
    return gray_string_lit("x86_64");
#elif defined(__i386__)
    return gray_string_lit("x86");
#elif defined(__arm__)
    return gray_string_lit("arm");
#else
    return gray_string_lit("unknown");
#endif
}

int64_t gray_os_pid(void) {
#if GRAY_RUNTIME_WINDOWS
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

int64_t gray_os_cpu_count(void) {
#if GRAY_RUNTIME_WINDOWS
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    long n = (long)system_info.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
#else
    long n = 0;
#endif
    return n > 0 ? (int64_t)n : 1;
}

bool gray_os_is_tty(void) {
#if GRAY_RUNTIME_WINDOWS
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

#if GRAY_RUNTIME_WINDOWS

/* Quote one argument the way the Microsoft C runtime parses argv, so the child
 * sees exactly the string we were handed. Backslashes are only special when
 * they immediately precede a quote, which is why the run length is counted
 * rather than every backslash being doubled. */
static void append_quoted_argument(char *destination, size_t capacity, size_t *length, const char *argument) {
    size_t i = *length;
    bool needs_quotes = (*argument == '\0') || strpbrk(argument, " \t\n\v\"") != NULL;

    if (i < capacity && needs_quotes) destination[i++] = '"';
    for (const char *cursor = argument; *cursor; cursor++) {
        size_t backslashes = 0;
        while (*cursor == '\\') {
            backslashes++;
            cursor++;
        }
        if (*cursor == '\0') {
            /* Trailing run: doubled so the closing quote is not escaped. */
            for (size_t n = 0; n < backslashes * 2 && i < capacity; n++) destination[i++] = '\\';
            break;
        }
        if (*cursor == '"') {
            for (size_t n = 0; n < backslashes * 2 + 1 && i < capacity; n++) destination[i++] = '\\';
        } else {
            for (size_t n = 0; n < backslashes && i < capacity; n++) destination[i++] = '\\';
        }
        if (i < capacity) destination[i++] = *cursor;
    }
    if (i < capacity && needs_quotes) destination[i++] = '"';
    *length = i;
}

/* Drain a pipe to end-of-stream, growing the arena buffer as needed. Each
 * stream gets its own thread because select() on Windows works only on
 * sockets, never on anonymous pipes, so the POSIX approach of multiplexing
 * both reads in one loop has no equivalent. Reading them concurrently is what
 * keeps a child that fills one pipe from deadlocking against the other. */
typedef struct {
    HANDLE pipe;
    GrayArena *arena;
    char *buffer;
    size_t total;
    size_t capacity;
    bool has_overflowed;
} PipeReader;

static DWORD WINAPI drain_pipe(LPVOID param) {
    PipeReader *reader = (PipeReader *)param;
    char chunk[4096];
    DWORD bytes_read = 0;

    while (ReadFile(reader->pipe, chunk, sizeof(chunk), &bytes_read, NULL) && bytes_read > 0) {
        if (reader->total + bytes_read > GRAY_EXEC_OUTPUT_MAX) {
            reader->has_overflowed = true;
            break;
        }
        EXEC_BUFFER_GROWTH(reader->arena, reader->buffer, reader->total, reader->capacity, bytes_read);
        memcpy(reader->buffer + reader->total, chunk, bytes_read);
        reader->total += bytes_read;
    }
    return 0;
}

GrayOsExecResult gray_os_exec(GrayArena *arena, GrayString command, GrayArray args) {
    GrayOsExecResult fail = {0, gray_string_lit(""), gray_string_lit(""), false};

    /* Flush buffered stdout/stderr so it is not interleaved after the child's. */
    fflush(stdout);
    fflush(stderr);

    /* CreateProcess takes one flat command line rather than an argv array, so
     * rebuild it with MSVCRT quoting. */
    char cmdline[32768];
    size_t length = 0;
    append_quoted_argument(cmdline, sizeof(cmdline) - 1, &length, command.data);
    for (int i = 0; i < args.len; i++) {
        GrayString s = GRAY_ARRAY_GET(args, GrayString, i);
        if (length < sizeof(cmdline) - 1) cmdline[length++] = ' ';
        append_quoted_argument(cmdline, sizeof(cmdline) - 1, &length, s.data);
    }
    cmdline[length] = '\0';

    SECURITY_ATTRIBUTES security_attributes;
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.lpSecurityDescriptor = NULL;
    security_attributes.bInheritHandle = TRUE;

    HANDLE stdout_read = NULL, stdout_write = NULL, stderr_read = NULL, stderr_write = NULL;
    if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0)) return fail;
    if (!CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        return fail;
    }
    /* Our read ends must not reach the child, or the pipes never report EOF. */
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup_info;
    memset(&startup_info, 0, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stderr_write;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process_information;
    memset(&process_information, 0, sizeof(process_information));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &startup_info, &process_information)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        CloseHandle(stderr_read);
        CloseHandle(stderr_write);
        return fail;
    }

    /* Close the child's ends here so ReadFile sees EOF when it exits. */
    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    PipeReader stdout_reader = {stdout_read, arena, gray_arena_alloc_uninitialized(arena, 4096), 0, 4096, false};
    PipeReader stderr_reader = {stderr_read, arena, gray_arena_alloc_uninitialized(arena, 4096), 0, 4096, false};

    HANDLE out_thread = CreateThread(NULL, 0, drain_pipe, &stdout_reader, 0, NULL);
    drain_pipe(&stderr_reader);
    if (out_thread) {
        WaitForSingleObject(out_thread, INFINITE);
        CloseHandle(out_thread);
    }

    if (stdout_reader.has_overflowed || stderr_reader.has_overflowed) TerminateProcess(process_information.hProcess, 1);

    WaitForSingleObject(process_information.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process_information.hProcess, &exit_code);

    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    CloseHandle(process_information.hThread);
    CloseHandle(process_information.hProcess);

    GrayString stdout_text = gray_string_new(arena, stdout_reader.buffer, (int32_t)stdout_reader.total);
    GrayString stderr_text = gray_string_new(arena, stderr_reader.buffer, (int32_t)stderr_reader.total);
    GrayOsExecResult reader = {(int64_t)exit_code, stdout_text, stderr_text, true};
    return reader;
}

#else

GrayOsExecResult gray_os_exec(GrayArena *arena, GrayString command, GrayArray args) {
    GrayOsExecResult fail = {0, gray_string_lit(""), gray_string_lit(""), false};

    /* Flush buffered stdout/stderr so it is not interleaved after the child's. */
    fflush(stdout);
    fflush(stderr);

    /* Build null-terminated argv: argv[0] = cmd, argv[1..n] = args, argv[n+1] = NULL */
    int argc = 1 + args.len;
    char **argv = gray_arena_alloc_uninitialized(arena, sizeof(char *) * (size_t)(argc + 1));
    argv[0] = (char *)command.data;
    for (int i = 0; i < args.len; i++) {
        GrayString s = GRAY_ARRAY_GET(args, GrayString, i);
        argv[1 + i] = (char *)s.data;
    }
    argv[argc] = NULL;

    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0) return fail;
    if (pipe(stderr_pipe) < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return fail;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return fail;
    }

    if (pid == 0) {
        /* Child: redirect stdout and stderr into pipes */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        execvp(command.data, argv);
        /* execvp failed */
        _exit(127);
    }

    /* Parent: close write ends, then read stdout and stderr concurrently
     * using select() to avoid deadlock when one pipe's buffer fills. */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    char buffer[4096];
    size_t stdout_total = 0, stderr_total = 0;
    size_t stdout_capacity = sizeof(buffer), stderr_capacity = sizeof(buffer);
    char *stdout_buffer = gray_arena_alloc_uninitialized(arena, stdout_capacity);
    char *stderr_buffer = gray_arena_alloc_uninitialized(arena, stderr_capacity);
    int stdout_descriptor = stdout_pipe[0];
    int stderr_descriptor = stderr_pipe[0];
    bool is_stdout_done = false, is_stderr_done = false;
    bool truncated = false;

    while (!is_stdout_done || !is_stderr_done) {
        fd_set descriptor_set;
        FD_ZERO(&descriptor_set);
        if (!is_stdout_done) FD_SET(stdout_descriptor, &descriptor_set);
        if (!is_stderr_done) FD_SET(stderr_descriptor, &descriptor_set);
        int maxfd = (stdout_descriptor > stderr_descriptor ? stdout_descriptor : stderr_descriptor) + 1;
        if (select(maxfd, &descriptor_set, NULL, NULL, NULL) < 0) break;

        if (!is_stdout_done && FD_ISSET(stdout_descriptor, &descriptor_set)) {
            ssize_t n = read(stdout_descriptor, buffer, sizeof(buffer));
            if (n <= 0) {
                is_stdout_done = true;
            } else if (stdout_total + (size_t)n > GRAY_EXEC_OUTPUT_MAX) {
                kill(pid, SIGKILL);
                is_stdout_done = true;
                is_stderr_done = true;
                truncated = true;
            } else {
                EXEC_BUFFER_GROWTH(arena, stdout_buffer, stdout_total, stdout_capacity, n);
                memcpy(stdout_buffer + stdout_total, buffer, (size_t)n);
                stdout_total += (size_t)n;
            }
        }

        if (!is_stderr_done && FD_ISSET(stderr_descriptor, &descriptor_set)) {
            ssize_t n = read(stderr_descriptor, buffer, sizeof(buffer));
            if (n <= 0) {
                is_stderr_done = true;
            } else if (stderr_total + (size_t)n > GRAY_EXEC_OUTPUT_MAX) {
                kill(pid, SIGKILL);
                is_stdout_done = true;
                is_stderr_done = true;
                truncated = true;
            } else {
                EXEC_BUFFER_GROWTH(arena, stderr_buffer, stderr_total, stderr_capacity, n);
                memcpy(stderr_buffer + stderr_total, buffer, (size_t)n);
                stderr_total += (size_t)n;
            }
        }
    }

    close(stdout_descriptor);
    close(stderr_descriptor);

    int status = 0;
    waitpid(pid, &status, 0);

    int exit_code = 0;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    /* exit_code 127 means execvp failed (command not found / bad path) */
    if (exit_code == 127 || truncated) return fail;

    GrayString stdout_text = gray_string_new(arena, stdout_buffer, (int32_t)stdout_total);
    GrayString stderr_text = gray_string_new(arena, stderr_buffer, (int32_t)stderr_total);
    GrayOsExecResult reader = {(int64_t)exit_code, stdout_text, stderr_text, true};
    return reader;
}


#endif /* !GRAY_RUNTIME_WINDOWS */
