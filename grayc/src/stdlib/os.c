/*
 * os.c — Implementation of the os stdlib module.
 * Provides access to command-line args, environment variables,
 * working directory, hostname, process execution, and signal handling.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "os.h"
#include "../runtime/platform_rt.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#if GRAY_RT_WINDOWS
#include "../runtime/win32.h"
#include <direct.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#endif

#define GRAY_HOSTNAME_BUF 256
#define GRAY_EXEC_OUTPUT_MAX (64 * 1024 * 1024) /* 64 MiB per stream */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int _os_argc = 0;
static char **_os_argv = NULL;

void gray_os_init(int argc, char **argv) {
    _os_argc = argc;
    _os_argv = argv;
}

GrayArray gray_os_args(GrayArena *arena) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), _os_argc > 0 ? _os_argc : 1);
    for (int i = 0; i < _os_argc; i++) {
        GrayString s = gray_string_new(arena, _os_argv[i], (int32_t)strlen(_os_argv[i]));
        GRAY_ARRAY_PUSH(arena, &arr, &s);
    }
    return arr;
}

GrayString gray_os_get_env(GrayArena *arena, GrayString name) {
    const char *val = getenv(name.data);
    if (!val) return gray_string_lit("");
    return gray_string_new(arena, val, (int32_t)strlen(val));
}

void gray_os_set_env(GrayString name, GrayString value) {
#if GRAY_RT_WINDOWS
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
#if GRAY_RT_WINDOWS
    _putenv_s(name.data, "");
    SetEnvironmentVariableA(name.data, NULL);
#else
    unsetenv(name.data);
#endif
}

GrayString gray_os_cwd(GrayArena *arena) {
    char buf[PATH_MAX];
#if GRAY_RT_WINDOWS
    if (_getcwd(buf, (int)sizeof(buf))) {
#else
    if (getcwd(buf, sizeof(buf))) {
#endif
        return gray_string_new(arena, buf, (int32_t)strlen(buf));
    }
    return gray_string_lit("");
}

GrayString gray_os_hostname(GrayArena *arena) {
    char buf[GRAY_HOSTNAME_BUF];
#if GRAY_RT_WINDOWS
    /* GetComputerNameEx avoids requiring Winsock to be started just for a name. */
    DWORD len = (DWORD)sizeof(buf);
    if (GetComputerNameExA(ComputerNameDnsHostname, buf, &len)) {
        return gray_string_new(arena, buf, (int32_t)len);
    }
#else
    if (gethostname(buf, sizeof(buf)) == 0) {
        return gray_string_new(arena, buf, (int32_t)strlen(buf));
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
#if GRAY_RT_WINDOWS
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

#if GRAY_RT_WINDOWS

GrayOsExecResult gray_os_exec(GrayArena *arena, GrayString cmd, GrayArray args) {
    (void)arena;
    (void)cmd;
    (void)args;
    /* The POSIX implementation drains both pipes with select(), which on
     * Windows only works on sockets, never on anonymous pipes. A correct port
     * needs CreateProcess plus reader threads; until then, fail loudly rather
     * than returning a silently empty result. */
    gray_panic_code("P0104", "os.exec() is not yet supported on Windows");
}

#else

GrayOsExecResult gray_os_exec(GrayArena *arena, GrayString cmd, GrayArray args) {
    GrayOsExecResult fail = {0, gray_string_lit(""), gray_string_lit(""), false};

    /* Build null-terminated argv: argv[0] = cmd, argv[1..n] = args, argv[n+1] = NULL */
    int argc = 1 + args.len;
    char **argv = gray_arena_alloc(arena, sizeof(char *) * (size_t)(argc + 1));
    argv[0] = (char *)cmd.data;
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
        execvp(cmd.data, argv);
        /* execvp failed */
        _exit(127);
    }

    /* Parent: close write ends, then read stdout and stderr concurrently
     * using select() to avoid deadlock when one pipe's buffer fills. */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    char buf[4096];
    size_t out_total = 0, err_total = 0;
    size_t out_cap = sizeof(buf), err_cap = sizeof(buf);
    char *out_buf = gray_arena_alloc(arena, out_cap);
    char *err_buf = gray_arena_alloc(arena, err_cap);
    int out_fd = stdout_pipe[0];
    int err_fd = stderr_pipe[0];
    bool out_done = false, err_done = false;
    bool truncated = false;

    while (!out_done || !err_done) {
        fd_set fds;
        FD_ZERO(&fds);
        if (!out_done) FD_SET(out_fd, &fds);
        if (!err_done) FD_SET(err_fd, &fds);
        int maxfd = (out_fd > err_fd ? out_fd : err_fd) + 1;
        if (select(maxfd, &fds, NULL, NULL, NULL) < 0) break;

        if (!out_done && FD_ISSET(out_fd, &fds)) {
            ssize_t n = read(out_fd, buf, sizeof(buf));
            if (n <= 0) {
                out_done = true;
            } else if (out_total + (size_t)n > GRAY_EXEC_OUTPUT_MAX) {
                kill(pid, SIGKILL);
                out_done = true;
                err_done = true;
                truncated = true;
            } else {
                if (out_total + (size_t)n > out_cap) {
                    size_t new_cap = out_cap * 2 + (size_t)n;
                    char *grown = gray_arena_alloc(arena, new_cap);
                    memcpy(grown, out_buf, out_total);
                    out_buf = grown;
                    out_cap = new_cap;
                }
                memcpy(out_buf + out_total, buf, (size_t)n);
                out_total += (size_t)n;
            }
        }

        if (!err_done && FD_ISSET(err_fd, &fds)) {
            ssize_t n = read(err_fd, buf, sizeof(buf));
            if (n <= 0) {
                err_done = true;
            } else if (err_total + (size_t)n > GRAY_EXEC_OUTPUT_MAX) {
                kill(pid, SIGKILL);
                out_done = true;
                err_done = true;
                truncated = true;
            } else {
                if (err_total + (size_t)n > err_cap) {
                    size_t new_cap = err_cap * 2 + (size_t)n;
                    char *grown = gray_arena_alloc(arena, new_cap);
                    memcpy(grown, err_buf, err_total);
                    err_buf = grown;
                    err_cap = new_cap;
                }
                memcpy(err_buf + err_total, buf, (size_t)n);
                err_total += (size_t)n;
            }
        }
    }

    close(out_fd);
    close(err_fd);

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

    GrayString stdout_str = gray_string_new(arena, out_buf, (int32_t)out_total);
    GrayString stderr_str = gray_string_new(arena, err_buf, (int32_t)err_total);
    GrayOsExecResult r = {(int64_t)exit_code, stdout_str, stderr_str, true};
    return r;
}


#endif /* !GRAY_RT_WINDOWS */
