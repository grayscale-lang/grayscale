/*
 * os.c — Implementation of the os stdlib module.
 * Provides access to command-line args, environment variables,
 * working directory, hostname, process execution, and signal handling.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

/* Must precede every system header: exposes sysconf(_SC_NPROCESSORS_ONLN)
 * and getloadavg(), which <unistd.h> and <stdlib.h> hide under a strict
 * _POSIX_C_SOURCE (glibc needs _DEFAULT_SOURCE for the latter). */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#elif defined(__linux__)
#define _DEFAULT_SOURCE 1
#endif

#include "os.h"
#include "../runtime/platform_rt.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#endif
#if !GRAY_RUNTIME_WINDOWS
#include <sys/statvfs.h>
#endif
#if GRAY_RUNTIME_WINDOWS
#include "../runtime/win32.h"
#include <direct.h>
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>
#endif

#define GRAY_HOSTNAME_BUFFER_SIZE 256
#define GRAY_EXEC_OUTPUT_MAX (64 * 1024 * 1024) /* 64 MiB per stream */
#define GRAY_OS_EXEC_MAX_CLOSED_DESCRIPTORS 65536 /* upper bound on the close loop before exec */

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

/* getenv/setenv/unsetenv take C strings, so an embedded NUL would silently
 * cut the name or value short and act on a different variable. */
static void os_validate_env_string(GrayString string) {
    if (memchr(string.data, '\0', (size_t)string.len))
        gray_panic_code("P0139", "environment variable name or value contains an embedded null byte");
}

/* exec hands the command and each argument to the new process as C strings, so
 * an embedded NUL would silently cut one short and run something other than
 * what was asked for. */
static void os_validate_exec_strings(GrayString command, GrayArray args) {
    bool has_nul = memchr(command.data, '\0', (size_t)command.len) != NULL;
    for (int i = 0; i < args.len && !has_nul; i++) {
        GrayString argument = GRAY_ARRAY_GET(args, GrayString, i);
        has_nul = memchr(argument.data, '\0', (size_t)argument.len) != NULL;
    }
    if (has_nul)
        gray_panic_code("P0141", "os.exec: the command or an argument contains an embedded null byte");
}

/* A failed launch: a zeroed result and a fixed-message error. */
static GrayResult_os_exec os_exec_failure(GrayArena *arena, int64_t code, const char *message) {
    GrayResult_os_exec result = {{0, gray_string_lit(""), gray_string_lit(""), 0}, NULL};
    result.v1 = gray_error_new(arena, code, gray_string_lit(message));
    return result;
}

GrayString gray_os_get_env(GrayArena *arena, GrayString name) {
    os_validate_env_string(name);
    const char *value = getenv(name.data);
    if (!value) return gray_string_lit("");
    return gray_string_new(arena, value, (int32_t)strlen(value));
}

GrayOsLookupEnvResult gray_os_lookup_env(GrayArena *arena, GrayString name) {
    os_validate_env_string(name);
    const char *value = getenv(name.data);
    if (!value) return (GrayOsLookupEnvResult){gray_string_lit(""), false};
    return (GrayOsLookupEnvResult){gray_string_new(arena, value, (int32_t)strlen(value)), true};
}

/* A candidate is an executable regular file. Windows has no execute bit; the
 * PATHEXT extension on the candidate is what makes it executable there. */
static bool os_is_executable_file(const char *path) {
    struct stat file_info;
    if (stat(path, &file_info) != 0 || !S_ISREG(file_info.st_mode)) return false;
#if GRAY_RUNTIME_WINDOWS
    return true;
#else
    return access(path, X_OK) == 0;
#endif
}

static bool os_has_path_separator(GrayString name) {
    for (int32_t i = 0; i < name.len; i++) {
#if GRAY_RUNTIME_WINDOWS
        if (name.data[i] == '/' || name.data[i] == '\\') return true;
#else
        if (name.data[i] == '/') return true;
#endif
    }
    return false;
}

/* Checks `directory`/`name` (or `name` alone when directory is NULL) as is
 * and, on Windows, with each PATHEXT extension appended. Writes the first
 * match into `found`. */
static bool os_find_in_directory(const char *directory, size_t directory_length,
                                 GrayString name, const char *extensions,
                                 char *found, size_t found_size) {
    size_t prefix = 0;
    if (directory) {
        if (directory_length + 1 + (size_t)name.len + 1 > found_size) return false;
        memcpy(found, directory, directory_length);
#if GRAY_RUNTIME_WINDOWS
        found[directory_length] = '\\';
#else
        found[directory_length] = '/';
#endif
        prefix = directory_length + 1;
    }
    if (prefix + (size_t)name.len + 1 > found_size) return false;
    memcpy(found + prefix, name.data, (size_t)name.len);
    size_t base_length = prefix + (size_t)name.len;
    found[base_length] = '\0';
#if GRAY_RUNTIME_WINDOWS
    const char *cursor = extensions;
    while (*cursor) {
        const char *end = strchr(cursor, ';');
        size_t extension_length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (extension_length > 0 && base_length + extension_length + 1 <= found_size) {
            memcpy(found + base_length, cursor, extension_length);
            found[base_length + extension_length] = '\0';
            if (os_is_executable_file(found)) return true;
        }
        if (!end) break;
        cursor = end + 1;
    }
    found[base_length] = '\0';
    /* name already carries a PATHEXT extension */
    for (cursor = extensions; *cursor;) {
        const char *end = strchr(cursor, ';');
        size_t extension_length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (extension_length > 0 && base_length >= extension_length &&
            _strnicmp(found + base_length - extension_length, cursor, extension_length) == 0 &&
            os_is_executable_file(found))
            return true;
        if (!end) break;
        cursor = end + 1;
    }
    return false;
#else
    (void)extensions;
    return os_is_executable_file(found);
#endif
}

GrayOsFindExecutableResult gray_os_find_executable(GrayArena *arena, GrayString name) {
    GrayOsFindExecutableResult not_found = {gray_string_lit(""), false};
    if (name.len == 0 || memchr(name.data, '\0', (size_t)name.len)) return not_found;

    const char *extensions = ".COM;.EXE;.BAT;.CMD";
#if GRAY_RUNTIME_WINDOWS
    const char path_separator = ';';
    const char *pathext = getenv("PATHEXT");
    if (pathext && *pathext) extensions = pathext;
#else
    const char path_separator = ':';
#endif
    char found[PATH_MAX];

    if (os_has_path_separator(name)) {
        if (!os_find_in_directory(NULL, 0, name, extensions, found, sizeof(found))) return not_found;
        return (GrayOsFindExecutableResult){gray_string_new(arena, found, (int32_t)strlen(found)), true};
    }

    const char *path = getenv("PATH");
    if (!path) return not_found;
    const char *cursor = path;
    while (true) {
        const char *end = strchr(cursor, path_separator);
        size_t directory_length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (directory_length > 0 &&
            os_find_in_directory(cursor, directory_length, name, extensions, found, sizeof(found)))
            return (GrayOsFindExecutableResult){gray_string_new(arena, found, (int32_t)strlen(found)), true};
        if (!end) break;
        cursor = end + 1;
    }
    return not_found;
}

GrayOsExecutablePathResult gray_os_executable_path(GrayArena *arena) {
    char path[PATH_MAX];
#if GRAY_RUNTIME_WINDOWS
    DWORD length = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (length == 0 || length >= sizeof(path)) goto failure;
#elif defined(__APPLE__)
    char raw_path[PATH_MAX];
    uint32_t raw_size = (uint32_t)sizeof(raw_path);
    if (_NSGetExecutablePath(raw_path, &raw_size) != 0 || !realpath(raw_path, path)) goto failure;
#elif defined(__linux__)
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0) goto failure;
    path[length] = '\0';
#else
    return (GrayOsExecutablePathResult){gray_string_lit(""),
        gray_error_new(arena, GRAY_ERR_Unsupported,
            gray_string_lit("os.executable_path is not supported on this platform"))};
#endif
    return (GrayOsExecutablePathResult){gray_string_new(arena, path, (int32_t)strlen(path)), NULL};
#if GRAY_RUNTIME_WINDOWS || defined(__APPLE__) || defined(__linux__)
failure:
    return (GrayOsExecutablePathResult){gray_string_lit(""),
        gray_error_new(arena, GRAY_ERR_IoFailure,
            gray_string_lit("cannot determine the path of the running binary"))};
#endif
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
    os_validate_env_string(name);
    os_validate_env_string(value);
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
    os_validate_env_string(name);
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

/* ---- Live system metrics ---- */

#if defined(__APPLE__) || defined(__linux__)
#define GRAY_OS_METRICS_SUPPORTED 1
#else
#define GRAY_OS_METRICS_SUPPORTED 0
#endif

#if !GRAY_OS_METRICS_SUPPORTED
static GrayError *os_metrics_unsupported(GrayArena *arena, const char *function_name) {
    return gray_error_new(arena, GRAY_ERR_Unsupported,
        gray_string_format(arena, "os.%s is not supported on this platform", function_name));
}
#endif

#if GRAY_OS_METRICS_SUPPORTED
/* Reads the cumulative CPU time counters: `busy` and `total` are in
 * scheduler ticks, summed over every logical CPU. */
static bool os_read_cpu_times(uint64_t *busy, uint64_t *total) {
#if defined(__APPLE__)
    host_cpu_load_info_data_t load;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&load, &count) != KERN_SUCCESS)
        return false;
    uint64_t idle = load.cpu_ticks[CPU_STATE_IDLE];
    *total = (uint64_t)load.cpu_ticks[CPU_STATE_USER] + load.cpu_ticks[CPU_STATE_SYSTEM] +
             load.cpu_ticks[CPU_STATE_NICE] + idle;
    *busy = *total - idle;
    return true;
#else
    FILE *file = fopen("/proc/stat", "r");
    if (!file) return false;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    int fields = fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                        &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(file);
    if (fields < 4) return false;
    uint64_t idle_total = idle + iowait;
    *total = user + nice + system + idle_total + irq + softirq + steal;
    *busy = *total - idle_total;
    return true;
#endif
}
#endif

GrayOsCpuUsageResult gray_os_cpu_usage(GrayArena *arena, int64_t interval_ms) {
    if (interval_ms < 1)
        gray_panic_code("P0154", "os.cpu_usage: interval_ms %lld must be at least 1", (long long)interval_ms);
#if GRAY_OS_METRICS_SUPPORTED
    uint64_t busy_before, total_before, busy_after, total_after;
    if (!os_read_cpu_times(&busy_before, &total_before))
        return (GrayOsCpuUsageResult){0.0, gray_error_new(arena, GRAY_ERR_IoFailure,
            gray_string_lit("cannot read CPU time counters"))};
    struct timespec remaining = { (time_t)(interval_ms / 1000), (long)(interval_ms % 1000) * 1000000L };
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {}
    if (!os_read_cpu_times(&busy_after, &total_after))
        return (GrayOsCpuUsageResult){0.0, gray_error_new(arena, GRAY_ERR_IoFailure,
            gray_string_lit("cannot read CPU time counters"))};
    uint64_t total_delta = total_after - total_before;
    if (total_delta == 0) return (GrayOsCpuUsageResult){0.0, NULL};
    double usage = 100.0 * (double)(busy_after - busy_before) / (double)total_delta;
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    return (GrayOsCpuUsageResult){usage, NULL};
#else
    return (GrayOsCpuUsageResult){0.0, os_metrics_unsupported(arena, "cpu_usage")};
#endif
}

GrayOsUsageResult gray_os_memory_usage(GrayArena *arena) {
#if GRAY_OS_METRICS_SUPPORTED
    int64_t total = 0, available = 0;
#if defined(__APPLE__)
    uint64_t memory_size = 0;
    size_t memory_size_length = sizeof(memory_size);
    vm_size_t page_size = 0;
    vm_statistics64_data_t statistics;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (sysctlbyname("hw.memsize", &memory_size, &memory_size_length, NULL, 0) != 0 ||
        host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS ||
        host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&statistics, &count) != KERN_SUCCESS)
        return (GrayOsUsageResult){0, 0, gray_error_new(arena, GRAY_ERR_IoFailure,
            gray_string_lit("cannot read memory counters"))};
    total = (int64_t)memory_size;
    available = (int64_t)(((uint64_t)statistics.free_count + statistics.inactive_count +
                           statistics.speculative_count) * page_size);
#else
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file) return (GrayOsUsageResult){0, 0, gray_error_new(arena, GRAY_ERR_IoFailure,
        gray_string_lit("cannot read memory counters"))};
    char line[256];
    bool found_total = false, found_available = false;
    while (fgets(line, sizeof(line), file) && !(found_total && found_available)) {
        long long kilobytes;
        if (sscanf(line, "MemTotal: %lld kB", &kilobytes) == 1) { total = kilobytes * 1024; found_total = true; }
        else if (sscanf(line, "MemAvailable: %lld kB", &kilobytes) == 1) { available = kilobytes * 1024; found_available = true; }
    }
    fclose(file);
    if (!found_total || !found_available)
        return (GrayOsUsageResult){0, 0, gray_error_new(arena, GRAY_ERR_IoFailure,
            gray_string_lit("cannot read memory counters"))};
#endif
    if (available > total) available = total;
    return (GrayOsUsageResult){total - available, total, NULL};
#else
    return (GrayOsUsageResult){0, 0, os_metrics_unsupported(arena, "memory_usage")};
#endif
}

GrayOsUsageResult gray_os_disk_usage(GrayArena *arena, GrayString path) {
    if (strlen(path.data) != (size_t)path.len)
        gray_panic_code("P0103", "file path contains an embedded null byte");
#if GRAY_OS_METRICS_SUPPORTED
    struct statvfs filesystem;
    if (statvfs(path.data, &filesystem) != 0)
        return (GrayOsUsageResult){0, 0, gray_error_new(arena, gray_errno_code(errno),
            gray_string_format(arena, "cannot read disk usage of '%s'", path.data))};
    int64_t total = (int64_t)filesystem.f_blocks * (int64_t)filesystem.f_frsize;
    int64_t free_bytes = (int64_t)filesystem.f_bfree * (int64_t)filesystem.f_frsize;
    return (GrayOsUsageResult){total - free_bytes, total, NULL};
#else
    return (GrayOsUsageResult){0, 0, os_metrics_unsupported(arena, "disk_usage")};
#endif
}

GrayOsLoadAverageResult gray_os_load_average(GrayArena *arena) {
    GrayArray load = gray_array_new(arena, sizeof(double), 3, GRAY_ELEM_F64);
#if GRAY_OS_METRICS_SUPPORTED
    double samples[3];
    if (getloadavg(samples, 3) != 3)
        return (GrayOsLoadAverageResult){load, gray_error_new(arena, GRAY_ERR_IoFailure,
            gray_string_lit("cannot read load averages"))};
    for (int i = 0; i < 3; i++) GRAY_ARRAY_PUSH(arena, &load, &samples[i]);
    return (GrayOsLoadAverageResult){load, NULL};
#else
    return (GrayOsLoadAverageResult){load, os_metrics_unsupported(arena, "load_average")};
#endif
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

GrayResult_os_exec gray_os_exec_result(GrayArena *arena, GrayString command, GrayArray args) {
    os_validate_exec_strings(command, args);

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
    if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0)) {
        return os_exec_failure(arena, GRAY_ERR_IoFailure, "os.exec: could not create the output pipes");
    }
    if (!CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        return os_exec_failure(arena, GRAY_ERR_IoFailure, "os.exec: could not create the output pipes");
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
        DWORD launch_error = GetLastError();
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        CloseHandle(stderr_read);
        CloseHandle(stderr_write);
        int64_t launch_code = GRAY_ERR_IoFailure;
        if (launch_error == ERROR_FILE_NOT_FOUND || launch_error == ERROR_PATH_NOT_FOUND) launch_code = GRAY_ERR_NotFound;
        else if (launch_error == ERROR_ACCESS_DENIED) launch_code = GRAY_ERR_PermissionDenied;
        return os_exec_failure(arena, launch_code, "os.exec: could not launch the process");
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
    GrayResult_os_exec result = {{(int64_t)exit_code, stdout_text, stderr_text, 0}, NULL};
    return result;
}

#else

GrayResult_os_exec gray_os_exec_result(GrayArena *arena, GrayString command, GrayArray args) {
    os_validate_exec_strings(command, args);

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

    int stdout_pipe[2], stderr_pipe[2], launch_pipe[2];
    if (pipe(stdout_pipe) < 0) {
        return os_exec_failure(arena, gray_errno_code(errno), "os.exec: could not create the output pipes");
    }
    if (pipe(stderr_pipe) < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return os_exec_failure(arena, gray_errno_code(errno), "os.exec: could not create the output pipes");
    }
    /* The child reports a failed execvp through this pipe. It closes on a
     * successful exec, so the parent reads end-of-file when the launch worked. */
    if (pipe(launch_pipe) < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return os_exec_failure(arena, gray_errno_code(errno), "os.exec: could not create the output pipes");
    }
    fcntl(launch_pipe[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        int fork_errno = errno;
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        close(launch_pipe[0]); close(launch_pipe[1]);
        return os_exec_failure(arena, gray_errno_code(fork_errno), "os.exec: could not start the process");
    }

    if (pid == 0) {
        /* Child: redirect stdout and stderr into pipes */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        close(launch_pipe[0]);
        /* The child sees only stdin, stdout and stderr (and the launch pipe,
         * which closes on exec): sockets and files the parent holds stay out. */
        long descriptor_limit = sysconf(_SC_OPEN_MAX);
        if (descriptor_limit < 0 || descriptor_limit > GRAY_OS_EXEC_MAX_CLOSED_DESCRIPTORS)
            descriptor_limit = GRAY_OS_EXEC_MAX_CLOSED_DESCRIPTORS;
        for (int descriptor = STDERR_FILENO + 1; descriptor < descriptor_limit; descriptor++)
            if (descriptor != launch_pipe[1]) close(descriptor);
        execvp(command.data, argv);
        /* execvp failed: report errno to the parent. */
        int exec_errno = errno;
        ssize_t reported = write(launch_pipe[1], &exec_errno, sizeof(exec_errno));
        (void)reported;
        _exit(127);
    }

    /* Parent: close write ends, then read stdout and stderr concurrently
     * using select() to avoid deadlock when one pipe's buffer fills. */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    close(launch_pipe[1]);

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

    int exec_errno = 0;
    ssize_t launch_bytes = read(launch_pipe[0], &exec_errno, sizeof(exec_errno));
    close(launch_pipe[0]);
    if (launch_bytes == (ssize_t)sizeof(exec_errno)) {
        return os_exec_failure(arena, gray_errno_code(exec_errno), "os.exec: could not launch the process");
    }

    int exit_code = 0;
    int terminating_signal = 0;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        terminating_signal = WTERMSIG(status);
        exit_code = 128 + terminating_signal;
    }

    if (truncated) {
        return os_exec_failure(arena, GRAY_ERR_OutOfRange, "os.exec: the process output exceeded the capture limit");
    }

    GrayString stdout_text = gray_string_new(arena, stdout_buffer, (int32_t)stdout_total);
    GrayString stderr_text = gray_string_new(arena, stderr_buffer, (int32_t)stderr_total);
    GrayResult_os_exec result = {{(int64_t)exit_code, stdout_text, stderr_text, (int64_t)terminating_signal}, NULL};
    return result;
}


#endif /* !GRAY_RUNTIME_WINDOWS */
