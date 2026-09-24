/*
 * io.c — Implementation of the io stdlib module.
 * Provides file read/write, path manipulation (join, dirname, basename,
 * extension), directory operations, globbing, and recursive directory
 * walking.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 *
 * Contributors:
 *  - @SAY-5
 */

#ifdef __APPLE__
#define _DARWIN_C_SOURCE  /* mkdtemp */
#else
#define _DEFAULT_SOURCE   /* mkdtemp */
#endif

#include "io.h"
#include "../runtime/platform_rt.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#if GRAY_RUNTIME_WINDOWS
#include "../runtime/win32.h"
#else
#include <glob.h>
#endif

#define GRAY_IO_PATH_BUFFER_SIZE          4096
#define GRAY_IO_READ_BUFFER_SIZE          4096
#define GRAY_IO_COPY_BUFFER_SIZE          8192
#define GRAY_IO_MAX_SEGMENTS      256
#define GRAY_IO_DIRECTORY_MODE          0755
#define GRAY_IO_FILE_MODE         0644
#define GRAY_IO_WALK_INITIAL_CAP  32
#define GRAY_IO_MAX_TEMPORARY_PATHS    256
#if !GRAY_RUNTIME_WINDOWS
#define GRAY_IO_TEMP_TEMPLATE     "/tmp/gray_XXXXXX"
#endif

#if GRAY_RUNTIME_WINDOWS
/* ---- glob(3) for Windows ----
 *
 * Windows has no glob(3), but MinGW does provide dirent, so the subset the
 * standard library actually exposes is a directory walk plus a matcher:
 * wildcards in the final path component, matched case-insensitively the way
 * NTFS callers expect. Wildcards in directory components are not supported.
 */
#include <ctype.h>

#define GLOB_NOSORT  0
#define GLOB_NOMATCH 3

typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
} glob_t;

/* '*' and '?' matching, iterative with backtracking so a pattern like
 * "*a*b" cannot blow the stack on a long name. */
static bool gray_glob_match(const char *pattern, const char *name) {
    const char *star = NULL;
    const char *retry = name;
    while (*name) {
        if (*pattern == '?' || tolower((unsigned char)*pattern) == tolower((unsigned char)*name)) {
            pattern++;
            name++;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = name;
        } else if (star) {
            pattern = star + 1;
            name = ++retry;
        } else {
            return false;
        }
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0';
}

static int glob(const char *pattern, int flags, void *errfn, glob_t *glob_result) {
    (void)flags;
    (void)errfn;
    glob_result->gl_pathc = 0;
    glob_result->gl_pathv = NULL;

    /* Split off the final component; everything before it is a literal directory. */
    const char *separator = NULL;
    for (const char *cursor = pattern; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') separator = cursor;
    }

    char directory_buffer[GRAY_IO_PATH_BUFFER_SIZE];
    const char *leaf;
    if (separator) {
        size_t directory_length = (size_t)(separator - pattern);
        if (directory_length >= sizeof(directory_buffer)) return GLOB_NOMATCH;
        memcpy(directory_buffer, pattern, directory_length);
        directory_buffer[directory_length] = '\0';
        if (directory_length == 0) {
            directory_buffer[0] = *separator;
            directory_buffer[1] = '\0';
        }
        leaf = separator + 1;
    } else {
        directory_buffer[0] = '.';
        directory_buffer[1] = '\0';
        leaf = pattern;
    }

    DIR *directory_handle = opendir(directory_buffer);
    if (!directory_handle) return GLOB_NOMATCH;

    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(directory_handle)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (!gray_glob_match(leaf, entry->d_name)) continue;

        if (glob_result->gl_pathc == capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 16;
            char **grown = realloc(glob_result->gl_pathv, new_capacity * sizeof(char *));
            if (!grown) break;
            glob_result->gl_pathv = grown;
            capacity = new_capacity;
        }

        /* Reuse the caller's own prefix verbatim so their separator style is
         * preserved in the results. */
        char full[GRAY_IO_PATH_BUFFER_SIZE];
        if (separator) {
            snprintf(full, sizeof(full), "%.*s%s", (int)(separator - pattern + 1), pattern, entry->d_name);
        } else {
            snprintf(full, sizeof(full), "%s", entry->d_name);
        }
        char *copy = _strdup(full);
        if (!copy) break;
        glob_result->gl_pathv[glob_result->gl_pathc++] = copy;
    }
    closedir(directory_handle);

    return glob_result->gl_pathc > 0 ? 0 : GLOB_NOMATCH;
}

static void globfree(glob_t *glob_result) {
    for (size_t i = 0; i < glob_result->gl_pathc; i++) free(glob_result->gl_pathv[i]);
    free(glob_result->gl_pathv);
    glob_result->gl_pathv = NULL;
    glob_result->gl_pathc = 0;
}
#endif /* GRAY_RUNTIME_WINDOWS */

/* ---- Temp cleanup registry ---- */

static char *temporary_paths[GRAY_IO_MAX_TEMPORARY_PATHS];
static int temporary_path_count = 0;
static bool temp_cleanup_registered = false;

static bool remove_directory_recursive(const char *path); /* forward decl */

static void gray_io_temp_cleanup(void) {
    for (int i = 0; i < temporary_path_count; i++) {
        struct stat file_info;
        if (stat(temporary_paths[i], &file_info) == 0) {
            if (S_ISDIR(file_info.st_mode))
                remove_directory_recursive(temporary_paths[i]);
            else
                unlink(temporary_paths[i]);
        }
        free(temporary_paths[i]);
    }
    temporary_path_count = 0;
}

static void temporary_registry_add(const char *path) {
    if (!temp_cleanup_registered) {
        atexit(gray_io_temp_cleanup);
        temp_cleanup_registered = true;
    }
    if (temporary_path_count < GRAY_IO_MAX_TEMPORARY_PATHS) {
        temporary_paths[temporary_path_count++] = strdup(path);
    }
}

/* ---- Path safety ---- */

/* Reject paths with embedded null bytes. GrayString tracks length, but C
 * functions (fopen, unlink, …) stop at '\0'. An attacker could craft a
 * string whose visible prefix passes checks while the C call opens a
 * different file. */
static void validate_path(GrayString path) {
    if (strlen(path.data) != (size_t)path.len)
        gray_panic_code("P0103", "file path contains an embedded null byte");
}

/* True when `path` exists and is a directory. No path validation. */
static bool io_path_is_directory(const char *path) {
    struct stat file_info;
    return stat(path, &file_info) == 0 && S_ISDIR(file_info.st_mode);
}

/* ---- Path manipulation (pure, no I/O) ---- */

GrayString gray_io_path_join(GrayArena *arena, GrayArray parts) {
    if (parts.len == 0) return gray_string_lit(".");
    GrayString result = GRAY_ARRAY_GET(parts, GrayString, 0);
    for (int32_t i = 1; i < parts.len; i++) {
        GrayString segment = GRAY_ARRAY_GET(parts, GrayString, i);
        if (segment.len == 0) continue;
        /* Absolute segment replaces accumulated path */
        if (segment.data[0] == '/' || segment.data[0] == '\\') {
            result = segment;
            continue;
        }
        bool already_separated = result.len > 0 &&
            (result.data[result.len - 1] == '/' || result.data[result.len - 1] == '\\');
        if (already_separated)
            result = gray_string_format(arena, "%.*s%.*s", result.len, result.data, segment.len, segment.data);
        else
            result = gray_string_format(arena, "%.*s/%.*s", result.len, result.data, segment.len, segment.data);
    }
    return result;
}

GrayString gray_io_dirname(GrayArena *arena, GrayString path) {
    if (path.len == 0) return gray_string_lit(".");
    /* Strip trailing slashes (keep at least 1 char so "/" stays "/") */
    int effective_length = path.len;
    while (effective_length > 1 && (path.data[effective_length - 1] == '/' || path.data[effective_length - 1] == '\\')) effective_length--;
    /* Find last separator in the stripped range */
    int last_separator = -1;
    for (int i = effective_length - 1; i >= 0; i--) {
        if (path.data[i] == '/' || path.data[i] == '\\') {
            last_separator = i;
            break;
        }
    }
    if (last_separator < 0) return gray_string_lit(".");
    /* Collapse leading separator: dirname("/foo") -> "/" */
    if (last_separator == 0) return gray_string_lit("/");
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)last_separator + 1);
    memcpy(buffer, path.data, (size_t)last_separator);
    buffer[last_separator] = '\0';
    return (GrayString){ buffer, (int32_t)last_separator };
}

GrayString gray_io_basename(GrayArena *arena, GrayString path) {
    (void)arena;
    if (path.len == 0) return gray_string_lit(".");
    int end_index = path.len;
    while (end_index > 0 && (path.data[end_index - 1] == '/' || path.data[end_index - 1] == '\\')) end_index--;
    if (end_index == 0) return gray_string_lit("/");
    int start = end_index;
    while (start > 0 && path.data[start - 1] != '/' && path.data[start - 1] != '\\') start--;
    int32_t length = (int32_t)(end_index - start);
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    memcpy(buffer, path.data + start, (size_t)length);
    buffer[length] = '\0';
    return (GrayString){ buffer, length };
}

GrayString gray_io_extension(GrayArena *arena, GrayString path) {
    (void)arena;
    int last_separator = -1;
    for (int i = path.len - 1; i >= 0; i--) {
        if (path.data[i] == '/' || path.data[i] == '\\') { last_separator = i; break; }
    }
    int search_start = last_separator + 1;
    int dot_position = -1;
    for (int i = path.len - 1; i >= search_start; i--) {
        if (path.data[i] == '.') { dot_position = i; break; }
    }
    if (dot_position < 0 || dot_position == path.len - 1) return gray_string_lit("");
    /* Dotfiles: leading dot with no other dot is part of the name, not an extension */
    if (dot_position == search_start) return gray_string_lit("");
    int32_t length = (int32_t)(path.len - dot_position);
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    memcpy(buffer, path.data + dot_position, (size_t)length);
    buffer[length] = '\0';
    return (GrayString){ buffer, length };
}

bool gray_io_is_absolute(GrayString path) {
    return path.len > 0 && path.data[0] == '/';
}

GrayString gray_io_normalize(GrayArena *arena, GrayString path) {
    if (path.len == 0) return gray_string_lit(".");
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)path.len + 1);
    /* Copy input, converting backslashes to forward slashes */
    for (int i = 0; i < path.len; i++) {
        buffer[i] = (path.data[i] == '\\') ? '/' : path.data[i];
    }
    buffer[path.len] = '\0';
    bool absolute = (buffer[0] == '/');

    /* Split into segments */
    char *segments[GRAY_IO_MAX_SEGMENTS];
    int seg_count = 0;
    char *cursor = buffer;
    while (*cursor) {
        while (*cursor == '/') cursor++;
        if (*cursor == '\0') break;
        char *seg_start = cursor;
        while (*cursor && *cursor != '/') cursor++;
        if (*cursor) { *cursor = '\0'; cursor++; }
        if (strcmp(seg_start, ".") == 0) continue;
        if (strcmp(seg_start, "..") == 0) {
            if (seg_count > 0 && strcmp(segments[seg_count - 1], "..") != 0) {
                seg_count--;
            } else if (!absolute) {
                segments[seg_count++] = seg_start;
            }
        } else {
            if (seg_count < GRAY_IO_MAX_SEGMENTS) segments[seg_count++] = seg_start;
        }
    }

    /* Rebuild */
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)path.len + 2);
    int position = 0;
    if (absolute) output[position++] = '/';
    for (int i = 0; i < seg_count; i++) {
        if (i > 0) output[position++] = '/';
        int segment_length = (int)strlen(segments[i]);
        memcpy(output + position, segments[i], (size_t)segment_length);
        position += segment_length;
    }
    if (position == 0) {
        output[0] = '.';
        position = 1;
    }
    output[position] = '\0';
    return (GrayString){ output, (int32_t)position };
}

/* ---- Existing file operations ---- */

/* Read an already-opened file into a GrayString. Tries seek-based sizing
 * first, falls back to streaming for non-seekable inputs (pipes, /proc,
 * /dev/stdin). Returns {NULL, -1} if the file exceeds INT32_MAX. Caller
 * is responsible for fclose.
 *
 * Not static: csv.c's gray_csv_read/gray_csv_read_result share this
 * instead of re-deriving the same seek/streaming logic. */
GrayString gray_io_read_file_impl(GrayArena *arena, FILE *file) {
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0) {
        size = ftell(file);
        if (size >= 0) (void)fseek(file, 0, SEEK_SET);
    }

    if (size >= 0 && size <= INT32_MAX) {
        char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)size + 1);
        size_t bytes = fread(buffer, 1, (size_t)size, file);
        buffer[bytes] = '\0';
        return (GrayString){ buffer, (int32_t)bytes };
    }
    if (size > INT32_MAX) {
        return (GrayString){ NULL, -1 };
    }

    /* Streaming fallback for non-seekable inputs. */
    clearerr(file);
    size_t capacity = GRAY_IO_READ_BUFFER_SIZE;
    size_t length = 0;
    char *buffer = gray_arena_alloc_uninitialized(arena, capacity);
    for (;;) {
        if (length == capacity) {
            if (capacity > (size_t)INT32_MAX / 2) {
                return (GrayString){ NULL, -1 };
            }
            size_t new_capacity = capacity * 2;
            char *new_buffer = gray_arena_alloc_uninitialized(arena, new_capacity);
            memcpy(new_buffer, buffer, length);
            buffer = new_buffer;
            capacity = new_capacity;
        }
        size_t bytes_read = fread(buffer + length, 1, capacity - length, file);
        if (bytes_read == 0) break;
        length += bytes_read;
    }
    if (length == capacity) {
        char *grow = gray_arena_alloc_uninitialized(arena, length + 1);
        memcpy(grow, buffer, length);
        buffer = grow;
    }
    buffer[length] = '\0';
    return (GrayString){ buffer, (int32_t)length };
}

GrayString gray_io_read_file(GrayArena *arena, GrayString path) {
    validate_path(path);
    if (io_path_is_directory(path.data))
        gray_panic_code("P0086", "io.read_file() cannot read a directory; use io.list_dir() or io.walk() to list directory contents");
    FILE *file = fopen(path.data, "rb");
    if (!file) return gray_string_lit("");
    GrayString result = gray_io_read_file_impl(arena, file);
    fclose(file);
    if (result.data == NULL)
        gray_panic_code("P0053", "io.read_file: input exceeds maximum string length");
    return result;
}

GrayArray gray_io_read_bytes(GrayArena *arena, GrayString path) {
    validate_path(path);
    if (io_path_is_directory(path.data))
        gray_panic_code("P0086", "io.read_bytes() cannot read a directory");
    FILE *file = fopen(path.data, "rb");
    GrayArray array = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0, GRAY_ELEM_U8);
    if (!file) return array;
    uint8_t buffer[GRAY_IO_READ_BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++)
            GRAY_ARRAY_PUSH(arena, &array, &buffer[i]);
    }
    fclose(file);
    return array;
}

GrayString gray_io_read_stdin_all(GrayArena *arena) {
    GrayString result = gray_io_read_file_impl(arena, stdin);
    if (result.data == NULL)
        gray_panic_code("P0124", "io.read_stdin_all: input exceeds maximum string length");
    return result;
}

GrayArray gray_io_read_stdin_bytes(GrayArena *arena) {
    GrayArray array = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0, GRAY_ELEM_U8);
    uint8_t buffer[GRAY_IO_READ_BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
        for (size_t i = 0; i < bytes_read; i++)
            GRAY_ARRAY_PUSH(arena, &array, &buffer[i]);
    }
    return array;
}

/* Stream lines from f into arr, stripping a trailing LF and (for CRLF) CR.
 * limit > 0 stops after that many lines; limit <= 0 reads to end of file.
 * Streaming keeps `limit` cheap on large files and avoids the read_file
 * max-string-length panic when the caller only wants the first few lines. */
static void io_stream_lines(GrayArena *arena, FILE *file, int64_t limit, GrayArray *array) {
    char *line = NULL;
    size_t capacity = 0;
    ssize_t bytes_read;
    int64_t count = 0;
    while ((limit <= 0 || count < limit) && (bytes_read = getline(&line, &capacity, file)) != -1) {
        size_t length = (size_t)bytes_read;
        if (length > 0 && line[length - 1] == '\n') length--;
        if (length > 0 && line[length - 1] == '\r') length--;
        char *linebuf = gray_arena_alloc_uninitialized(arena, length + 1);
        memcpy(linebuf, line, length);
        linebuf[length] = '\0';
        GrayString line_text = { linebuf, (int32_t)length };
        GRAY_ARRAY_PUSH(arena, array, &line_text);
        count++;
    }
    free(line);
}

GrayArray gray_io_read_lines(GrayArena *arena, GrayString path, int64_t limit) {
    validate_path(path);
    GrayArray array = gray_array_new(arena, (int32_t)sizeof(GrayString), 16, GRAY_ELEM_STRING);
    if (io_path_is_directory(path.data))
        gray_panic_code("P0086", "io.read_lines() cannot read a directory");
    FILE *file = fopen(path.data, "rb");
    if (!file) return array;
    io_stream_lines(arena, file, limit, &array);
    fclose(file);
    return array;
}

bool gray_io_file_exists(GrayString path) {
    validate_path(path);
    return access(path.data, F_OK) == 0;
}

bool gray_io_is_file(GrayString path) {
    validate_path(path);
    struct stat file_info;
    if (stat(path.data, &file_info) != 0) return false;
    return S_ISREG(file_info.st_mode);
}

bool gray_io_is_directory(GrayString path) {
    validate_path(path);
    return io_path_is_directory(path.data);
}

int64_t gray_io_file_size(GrayString path) {
    validate_path(path);
    struct stat file_info;
    if (stat(path.data, &file_info) != 0) return -1;
    return (int64_t)file_info.st_size;
}

GrayResult_i64 gray_io_file_size_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    struct stat file_info;
    if (stat(path.data, &file_info) != 0)
        return (GrayResult_i64){-1, gray_error_new(arena, gray_errno_code(errno),
            gray_string_format(arena, "cannot stat '%s'", path.data))};
    return (GrayResult_i64){(int64_t)file_info.st_size, NULL};
}

bool gray_io_write_file(GrayString path, GrayString content) {
    validate_path(path);
    if (io_path_is_directory(path.data))
        gray_panic_code("P0087", "io.write_file() cannot write to a directory");
    FILE *file = fopen(path.data, "wb");
    if (!file) return false;
    size_t written = fwrite(content.data, 1, (size_t)content.len, file);
    fclose(file);
    return written == (size_t)content.len;
}

bool gray_io_append_file(GrayString path, GrayString content) {
    validate_path(path);
    if (io_path_is_directory(path.data))
        gray_panic_code("P0088", "io.append_file() cannot append to a directory");
    FILE *file = fopen(path.data, "ab");
    if (!file) return false;
    size_t written = fwrite(content.data, 1, (size_t)content.len, file);
    fclose(file);
    return written == (size_t)content.len;
}

bool gray_io_write_bytes(GrayString path, GrayArray data) {
    validate_path(path);
    if (io_path_is_directory(path.data))
        gray_panic_code("P0087", "io.write_bytes() cannot write to a directory");
    FILE *file = fopen(path.data, "wb");
    if (!file) return false;
    size_t written = fwrite(data.data, 1, (size_t)data.len, file);
    fclose(file);
    return written == (size_t)data.len;
}

bool gray_io_append_bytes(GrayString path, GrayArray data) {
    validate_path(path);
    if (io_path_is_directory(path.data))
        gray_panic_code("P0088", "io.append_bytes() cannot append to a directory");
    FILE *file = fopen(path.data, "ab");
    if (!file) return false;
    size_t written = fwrite(data.data, 1, (size_t)data.len, file);
    fclose(file);
    return written == (size_t)data.len;
}

GrayString gray_io_temp_file(GrayArena *arena) {
#if GRAY_RUNTIME_WINDOWS
    char temporary[MAX_PATH];
    if (!GetTempPathA(sizeof(temporary), temporary)) return gray_string_lit("");
    char path[MAX_PATH];
    if (!GetTempFileNameA(temporary, "gray", 0, path)) return gray_string_lit("");
    temporary_registry_add(path);
    return gray_string_new(arena, path, (int32_t)strlen(path));
#else
    char template_buffer[] = GRAY_IO_TEMP_TEMPLATE;
    int file_descriptor = mkstemp(template_buffer);
    if (file_descriptor < 0) return gray_string_lit("");
    close(file_descriptor);
    temporary_registry_add(template_buffer);
    return gray_string_new(arena, template_buffer, (int32_t)strlen(template_buffer));
#endif
}

GrayString gray_io_temp_dir(GrayArena *arena) {
#if GRAY_RUNTIME_WINDOWS
    char temporary[MAX_PATH];
    if (!GetTempPathA(sizeof(temporary), temporary)) return gray_string_lit("");
    char path[MAX_PATH];
    /* GetTempFileNameA creates a 0-byte file; repurpose the name as a dir. */
    if (!GetTempFileNameA(temporary, "gray", 0, path)) return gray_string_lit("");
    DeleteFileA(path);
    if (!CreateDirectoryA(path, NULL)) return gray_string_lit("");
    temporary_registry_add(path);
    return gray_string_new(arena, path, (int32_t)strlen(path));
#else
    char template_buffer[] = GRAY_IO_TEMP_TEMPLATE;
    if (!mkdtemp(template_buffer)) return gray_string_lit("");
    temporary_registry_add(template_buffer);
    return gray_string_new(arena, template_buffer, (int32_t)strlen(template_buffer));
#endif
}

bool gray_io_delete_file(GrayString path) {
    validate_path(path);
    if (io_path_is_directory(path.data))
        gray_panic_code("P0077", "io.delete_file() cannot delete a directory; use io.remove_dir() for directories");
    return unlink(path.data) == 0;
}

bool gray_io_rename_file(GrayString old_path, GrayString new_path) {
    validate_path(old_path);
    validate_path(new_path);
    return rename(old_path.data, new_path.data) == 0;
}

/* ---- New file operations ---- */

bool gray_io_copy_file(GrayString source, GrayString destination) {
    validate_path(source);
    validate_path(destination);
    if (io_path_is_directory(source.data))
        gray_panic_code("P0089", "io.copy_file() cannot copy a directory; use io.walk() to enumerate files and copy them individually");
    FILE *input_file = fopen(source.data, "rb");
    if (!input_file) return false;
    int output_descriptor = open(destination.data, O_WRONLY | O_CREAT | O_TRUNC, GRAY_IO_FILE_MODE);
    if (output_descriptor < 0) { fclose(input_file); return false; }
    FILE *output_file = fdopen(output_descriptor, "wb");
    if (!output_file) { close(output_descriptor); fclose(input_file); return false; }
    char buffer[GRAY_IO_COPY_BUFFER_SIZE];
    size_t bytes_read;
    bool is_valid = true;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), input_file)) > 0) {
        if (fwrite(buffer, 1, bytes_read, output_file) != bytes_read) { is_valid = false; break; }
    }
    fclose(input_file);
    fclose(output_file);
    return is_valid;
}

bool gray_io_move_file(GrayString source, GrayString destination) {
    validate_path(source);
    validate_path(destination);
    if (rename(source.data, destination.data) == 0) return true;
    if (!gray_io_copy_file(source, destination)) return false;
    unlink(source.data);
    return true;
}

/* ---- Directory operations ---- */

/* Read directory entries from an already-opened DIR handle.
 * Caller is responsible for closedir. */
static GrayArray io_list_directory_from(GrayArena *arena, DIR *directory) {
    GrayArray array = gray_array_new(arena, (int32_t)sizeof(GrayString), 16, GRAY_ELEM_STRING);
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        GrayString name = gray_string_format(arena, "%s", entry->d_name);
        GRAY_ARRAY_PUSH(arena, &array, &name);
    }
    return array;
}

GrayArray gray_io_list_dir(GrayArena *arena, GrayString path) {
    validate_path(path);
    DIR *directory = opendir(path.data);
    if (!directory) return gray_array_new(arena, (int32_t)sizeof(GrayString), 16, GRAY_ELEM_STRING);
    GrayArray array = io_list_directory_from(arena, directory);
    closedir(directory);
    return array;
}

bool gray_io_make_dir(GrayString path) {
    validate_path(path);
    return gray_runtime_mkdir(path.data, GRAY_IO_DIRECTORY_MODE) == 0;
}

bool gray_io_make_dir_all(GrayString path) {
    validate_path(path);
    if (path.len == 0) return false;
    char buffer[GRAY_IO_PATH_BUFFER_SIZE];
    if ((size_t)path.len >= sizeof(buffer)) return false;
    memcpy(buffer, path.data, (size_t)path.len);
    buffer[path.len] = '\0';
    for (char *cursor = buffer + 1; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') {
            *cursor = '\0';
            gray_runtime_mkdir(buffer, GRAY_IO_DIRECTORY_MODE);
            *cursor = '/';
        }
    }
    return gray_runtime_mkdir(buffer, GRAY_IO_DIRECTORY_MODE) == 0 || errno == EEXIST;
}

bool gray_io_remove_dir(GrayString path) {
    validate_path(path);
    return rmdir(path.data) == 0;
}

static bool remove_directory_recursive(const char *path) {
    DIR *directory = opendir(path);
    if (!directory) return false;
    struct dirent *entry;
    bool is_valid = true;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[GRAY_IO_PATH_BUFFER_SIZE];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat file_info;
        if (stat(child, &file_info) != 0) { is_valid = false; continue; }
        if (S_ISDIR(file_info.st_mode)) {
            if (!remove_directory_recursive(child)) is_valid = false;
        } else {
            if (unlink(child) != 0) is_valid = false;
        }
    }
    closedir(directory);
    if (rmdir(path) != 0) is_valid = false;
    return is_valid;
}

bool gray_io_remove_dir_all(GrayString path) {
    validate_path(path);
    return remove_directory_recursive(path.data);
}

static void walk_recursive(GrayArena *arena, const char *base, const char *relative_path, GrayArray *output) {
    char full[GRAY_IO_PATH_BUFFER_SIZE];
    if (relative_path[0] == '\0') {
        snprintf(full, sizeof(full), "%s", base);
    } else {
        snprintf(full, sizeof(full), "%s/%s", base, relative_path);
    }
    DIR *directory = opendir(full);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        GrayString child_rel;
        if (relative_path[0] == '\0') {
            child_rel = gray_string_format(arena, "%s", entry->d_name);
        } else {
            child_rel = gray_string_format(arena, "%s/%s", relative_path, entry->d_name);
        }
        GRAY_ARRAY_PUSH(arena, output, &child_rel);
        char child_full[GRAY_IO_PATH_BUFFER_SIZE];
        snprintf(child_full, sizeof(child_full), "%s/%s", full, entry->d_name);
        if (io_path_is_directory(child_full)) {
            walk_recursive(arena, base, child_rel.data, output);
        }
    }
    closedir(directory);
}

GrayArray gray_io_walk(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayArray array = gray_array_new(arena, (int32_t)sizeof(GrayString), GRAY_IO_WALK_INITIAL_CAP, GRAY_ELEM_STRING);
    walk_recursive(arena, path.data, "", &array);
    return array;
}

GrayArray gray_io_glob(GrayArena *arena, GrayString pattern) {
    validate_path(pattern);
    GrayArray array = gray_array_new(arena, (int32_t)sizeof(GrayString), 16, GRAY_ELEM_STRING);
    glob_t glob_result;
    if (glob(pattern.data, GLOB_NOSORT, NULL, &glob_result) == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; i++) {
            GrayString entry = gray_string_format(arena, "%s", glob_result.gl_pathv[i]);
            GRAY_ARRAY_PUSH(arena, &array, &entry);
        }
        globfree(&glob_result);
    }
    return array;
}

/* ---- Tuple-returning (fallible) versions ---- */

GrayResult_string gray_io_read_file_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_string result;
    if (io_path_is_directory(path.data)) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot read '%s': is a directory", path.data));
        return result;
    }
    FILE *file = fopen(path.data, "rb");
    if (!file) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot read '%s'", path.data));
        return result;
    }
    GrayString content = gray_io_read_file_impl(arena, file);
    fclose(file);
    if (content.data == NULL) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, GRAY_ERR_OutOfRange, gray_string_format(arena,
            "cannot read '%s': file exceeds maximum string length", path.data));
        return result;
    }
    result.v0 = content;
    result.v1 = NULL;
    return result;
}

GrayResult_bool gray_io_write_file_result(GrayArena *arena, GrayString path, GrayString content) {
    validate_path(path);
    GrayResult_bool result;
    if (io_path_is_directory(path.data)) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot write '%s': is a directory", path.data));
        return result;
    }
    FILE *file = fopen(path.data, "wb");
    if (!file) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot write '%s'", path.data));
        return result;
    }
    fwrite(content.data, 1, (size_t)content.len, file);
    fclose(file);
    result.v0 = true;
    result.v1 = NULL;
    return result;
}

GrayResult_bool gray_io_delete_file_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_bool result;
    if (io_path_is_directory(path.data)) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot delete '%s': is a directory; use io.remove_dir() for directories", path.data));
        return result;
    }
    GRAY_RESULT_WRAP_BOOL(arena, unlink(path.data) == 0, gray_errno_code(errno),
        gray_string_format(arena, "cannot delete '%s'", path.data));
}

GrayResult_bool gray_io_append_file_result(GrayArena *arena, GrayString path, GrayString content) {
    GrayResult_bool result;
    if (io_path_is_directory(path.data)) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot append to '%s': is a directory", path.data));
        return result;
    }
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_append_file(path, content), gray_errno_code(errno),
        gray_string_format(arena, "cannot append to '%s'", path.data));
}

GrayResult_bool gray_io_rename_file_result(GrayArena *arena, GrayString old_path, GrayString new_path) {
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_rename_file(old_path, new_path), gray_errno_code(errno),
        gray_string_format(arena, "cannot rename '%s' to '%s'", old_path.data, new_path.data));
}

GrayResult_bool gray_io_copy_file_result(GrayArena *arena, GrayString source, GrayString destination) {
    GrayResult_bool result;
    if (io_path_is_directory(source.data)) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot copy '%s': is a directory", source.data));
        return result;
    }
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_copy_file(source, destination), gray_errno_code(errno),
        gray_string_format(arena, "cannot copy '%s' to '%s'", source.data, destination.data));
}

GrayResult_bool gray_io_move_file_result(GrayArena *arena, GrayString source, GrayString destination) {
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_move_file(source, destination), gray_errno_code(errno),
        gray_string_format(arena, "cannot move '%s' to '%s'", source.data, destination.data));
}

GrayResult_array gray_io_list_dir_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_array result;
    DIR *directory = opendir(path.data);
    if (!directory) {
        result.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 0, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot list directory '%s'", path.data));
        return result;
    }
    result.v0 = io_list_directory_from(arena, directory);
    closedir(directory);
    result.v1 = NULL;
    return result;
}

GrayResult_bool gray_io_make_dir_result(GrayArena *arena, GrayString path) {
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_make_dir(path), gray_errno_code(errno),
        gray_string_format(arena, "cannot create directory '%s'", path.data));
}

GrayResult_bool gray_io_make_dir_all_result(GrayArena *arena, GrayString path) {
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_make_dir_all(path), gray_errno_code(errno),
        gray_string_format(arena, "cannot create directories '%s'", path.data));
}

GrayResult_bool gray_io_remove_dir_result(GrayArena *arena, GrayString path) {
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_remove_dir(path), gray_errno_code(errno),
        gray_string_format(arena, "cannot remove directory '%s'", path.data));
}

GrayResult_bool gray_io_remove_dir_all_result(GrayArena *arena, GrayString path) {
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_remove_dir_all(path), gray_errno_code(errno),
        gray_string_format(arena, "cannot recursively remove '%s'", path.data));
}

GrayResult_array gray_io_walk_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_array result;
    if (!io_path_is_directory(path.data)) {
        result.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 0, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot walk directory '%s'", path.data));
        return result;
    }
    result.v0 = gray_io_walk(arena, path);
    result.v1 = NULL;
    return result;
}

GrayResult_array gray_io_read_bytes_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_array result;
    if (io_path_is_directory(path.data)) {
        result.v0 = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0, GRAY_ELEM_U8);
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot read '%s': is a directory", path.data));
        return result;
    }
    FILE *file = fopen(path.data, "rb");
    if (!file) {
        result.v0 = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0, GRAY_ELEM_U8);
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot read '%s'", path.data));
        return result;
    }
    result.v0 = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0, GRAY_ELEM_U8);
    uint8_t buffer[GRAY_IO_READ_BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++)
            GRAY_ARRAY_PUSH(arena, &result.v0, &buffer[i]);
    }
    fclose(file);
    result.v1 = NULL;
    return result;
}

GrayResult_array gray_io_read_lines_result(GrayArena *arena, GrayString path, int64_t limit) {
    validate_path(path);
    GrayResult_array result;
    result.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 16, GRAY_ELEM_STRING);
    if (io_path_is_directory(path.data)) {
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot read '%s': is a directory", path.data));
        return result;
    }
    FILE *file = fopen(path.data, "rb");
    if (!file) {
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena,
            "cannot read '%s'", path.data));
        return result;
    }
    io_stream_lines(arena, file, limit, &result.v0);
    fclose(file);
    result.v1 = NULL;
    return result;
}

GrayResult_array gray_io_glob_result(GrayArena *arena, GrayString pattern) {
    validate_path(pattern);
    GrayResult_array result;
    glob_t glob_result;
    int result_code = glob(pattern.data, GLOB_NOSORT, NULL, &glob_result);
    if (result_code != 0 && result_code != GLOB_NOMATCH) {
        result.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 0, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "glob pattern failed: '%s'", pattern.data));
        return result;
    }
    result.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), (int32_t)glob_result.gl_pathc, GRAY_ELEM_STRING);
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        GrayString entry = gray_string_format(arena, "%s", glob_result.gl_pathv[i]);
        GRAY_ARRAY_PUSH(arena, &result.v0, &entry);
    }
    globfree(&glob_result);
    result.v1 = NULL;
    return result;
}

GrayResult_bool gray_io_write_bytes_result(GrayArena *arena, GrayString path, GrayArray data) {
    validate_path(path);
    GrayResult_bool result;
    if (io_path_is_directory(path.data)) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot write '%s': is a directory", path.data));
        return result;
    }
    FILE *file = fopen(path.data, "wb");
    if (!file) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot write '%s'", path.data));
        return result;
    }
    fwrite(data.data, 1, (size_t)data.len, file);
    fclose(file);
    result.v0 = true;
    result.v1 = NULL;
    return result;
}

GrayResult_bool gray_io_append_bytes_result(GrayArena *arena, GrayString path, GrayArray data) {
    validate_path(path);
    GrayResult_bool result;
    if (io_path_is_directory(path.data)) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena,
            "cannot append to '%s': is a directory", path.data));
        return result;
    }
    GRAY_RESULT_WRAP_BOOL(arena, gray_io_append_bytes(path, data), gray_errno_code(errno),
        gray_string_format(arena, "cannot append to '%s'", path.data));
}

GrayResult_string gray_io_temp_file_result(GrayArena *arena) {
    GrayResult_string result;
#if GRAY_RUNTIME_WINDOWS
    char temporary[MAX_PATH];
    if (!GetTempPathA(sizeof(temporary), temporary)) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot create temporary file"));
        return result;
    }
    char path[MAX_PATH];
    if (!GetTempFileNameA(temporary, "gray", 0, path)) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot create temporary file"));
        return result;
    }
    temporary_registry_add(path);
    result.v0 = gray_string_new(arena, path, (int32_t)strlen(path));
#else
    char template_buffer[] = GRAY_IO_TEMP_TEMPLATE;
    int file_descriptor = mkstemp(template_buffer);
    if (file_descriptor < 0) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot create temporary file"));
        return result;
    }
    close(file_descriptor);
    temporary_registry_add(template_buffer);
    result.v0 = gray_string_new(arena, template_buffer, (int32_t)strlen(template_buffer));
#endif
    result.v1 = NULL;
    return result;
}

GrayResult_string gray_io_temp_dir_result(GrayArena *arena) {
    GrayResult_string result;
#if GRAY_RUNTIME_WINDOWS
    char temporary[MAX_PATH];
    if (!GetTempPathA(sizeof(temporary), temporary)) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot create temporary directory"));
        return result;
    }
    char path[MAX_PATH];
    if (!GetTempFileNameA(temporary, "gray", 0, path)) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot create temporary directory"));
        return result;
    }
    DeleteFileA(path);
    if (!CreateDirectoryA(path, NULL)) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot create temporary directory"));
        return result;
    }
    temporary_registry_add(path);
    result.v0 = gray_string_new(arena, path, (int32_t)strlen(path));
#else
    char template_buffer[] = GRAY_IO_TEMP_TEMPLATE;
    if (!mkdtemp(template_buffer)) {
        result.v0 = gray_string_lit("");
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot create temporary directory"));
        return result;
    }
    temporary_registry_add(template_buffer);
    result.v0 = gray_string_new(arena, template_buffer, (int32_t)strlen(template_buffer));
#endif
    result.v1 = NULL;
    return result;
}
