/*
 * io.c — Implementation of the io stdlib module.
 * Provides file read/write, path manipulation (join, dirname, basename,
 * extension), directory operations, globbing, and recursive directory
 * walking.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
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
#if GRAY_RT_WINDOWS
#include "../runtime/win32.h"
#else
#include <glob.h>
#endif

#define GRAY_IO_PATH_BUF          4096
#define GRAY_IO_COPY_BUF          8192
#define GRAY_IO_MAX_SEGMENTS      256
#define GRAY_IO_DIR_MODE          0755
#define GRAY_IO_FILE_MODE         0644
#define GRAY_IO_WALK_INITIAL_CAP  32
#define GRAY_IO_MAX_TEMP_PATHS    256

#if GRAY_RT_WINDOWS
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
static bool gray_glob_match(const char *pat, const char *name) {
    const char *star = NULL;
    const char *retry = name;
    while (*name) {
        if (*pat == '?' || tolower((unsigned char)*pat) == tolower((unsigned char)*name)) {
            pat++;
            name++;
        } else if (*pat == '*') {
            star = pat++;
            retry = name;
        } else if (star) {
            pat = star + 1;
            name = ++retry;
        } else {
            return false;
        }
    }
    while (*pat == '*') pat++;
    return *pat == '\0';
}

static int glob(const char *pattern, int flags, void *errfn, glob_t *g) {
    (void)flags;
    (void)errfn;
    g->gl_pathc = 0;
    g->gl_pathv = NULL;

    /* Split off the final component; everything before it is a literal directory. */
    const char *sep = NULL;
    for (const char *p = pattern; *p; p++) {
        if (*p == '/' || *p == '\\') sep = p;
    }

    char dir[GRAY_IO_PATH_BUF];
    const char *leaf;
    if (sep) {
        size_t dlen = (size_t)(sep - pattern);
        if (dlen >= sizeof(dir)) return GLOB_NOMATCH;
        memcpy(dir, pattern, dlen);
        dir[dlen] = '\0';
        if (dlen == 0) {
            dir[0] = *sep;
            dir[1] = '\0';
        }
        leaf = sep + 1;
    } else {
        dir[0] = '.';
        dir[1] = '\0';
        leaf = pattern;
    }

    DIR *d = opendir(dir);
    if (!d) return GLOB_NOMATCH;

    size_t cap = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (!gray_glob_match(leaf, ent->d_name)) continue;

        if (g->gl_pathc == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            char **grown = realloc(g->gl_pathv, ncap * sizeof(char *));
            if (!grown) break;
            g->gl_pathv = grown;
            cap = ncap;
        }

        /* Reuse the caller's own prefix verbatim so their separator style is
         * preserved in the results. */
        char full[GRAY_IO_PATH_BUF];
        if (sep) {
            snprintf(full, sizeof(full), "%.*s%s", (int)(sep - pattern + 1), pattern, ent->d_name);
        } else {
            snprintf(full, sizeof(full), "%s", ent->d_name);
        }
        char *copy = _strdup(full);
        if (!copy) break;
        g->gl_pathv[g->gl_pathc++] = copy;
    }
    closedir(d);

    return g->gl_pathc > 0 ? 0 : GLOB_NOMATCH;
}

static void globfree(glob_t *g) {
    for (size_t i = 0; i < g->gl_pathc; i++) free(g->gl_pathv[i]);
    free(g->gl_pathv);
    g->gl_pathv = NULL;
    g->gl_pathc = 0;
}
#endif /* GRAY_RT_WINDOWS */

/* ---- Temp cleanup registry ---- */

static char *temp_paths[GRAY_IO_MAX_TEMP_PATHS];
static int temp_path_count = 0;
static bool temp_cleanup_registered = false;

static bool remove_dir_recursive(const char *path); /* forward decl */

static void gray_io_temp_cleanup(void) {
    for (int i = 0; i < temp_path_count; i++) {
        struct stat st;
        if (stat(temp_paths[i], &st) == 0) {
            if (S_ISDIR(st.st_mode))
                remove_dir_recursive(temp_paths[i]);
            else
                unlink(temp_paths[i]);
        }
        free(temp_paths[i]);
    }
    temp_path_count = 0;
}

static void temp_registry_add(const char *path) {
    if (!temp_cleanup_registered) {
        atexit(gray_io_temp_cleanup);
        temp_cleanup_registered = true;
    }
    if (temp_path_count < GRAY_IO_MAX_TEMP_PATHS) {
        temp_paths[temp_path_count++] = strdup(path);
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

/* ---- Path manipulation (pure, no I/O) ---- */

GrayString gray_io_path_join(GrayArena *arena, GrayArray parts) {
    if (parts.len == 0) return gray_string_lit(".");
    GrayString result = GRAY_ARRAY_GET(parts, GrayString, 0);
    for (int32_t i = 1; i < parts.len; i++) {
        GrayString seg = GRAY_ARRAY_GET(parts, GrayString, i);
        if (seg.len == 0) continue;
        /* Absolute segment replaces accumulated path */
        if (seg.data[0] == '/' || seg.data[0] == '\\') {
            result = seg;
            continue;
        }
        bool already_separated = result.len > 0 &&
            (result.data[result.len - 1] == '/' || result.data[result.len - 1] == '\\');
        if (already_separated)
            result = gray_string_format(arena, "%.*s%.*s", result.len, result.data, seg.len, seg.data);
        else
            result = gray_string_format(arena, "%.*s/%.*s", result.len, result.data, seg.len, seg.data);
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
    char *buf = gray_arena_alloc(arena, (size_t)last_separator + 1);
    memcpy(buf, path.data, (size_t)last_separator);
    buf[last_separator] = '\0';
    return (GrayString){ buf, (int32_t)last_separator };
}

GrayString gray_io_basename(GrayArena *arena, GrayString path) {
    (void)arena;
    if (path.len == 0) return gray_string_lit(".");
    int end = path.len;
    while (end > 0 && (path.data[end - 1] == '/' || path.data[end - 1] == '\\')) end--;
    if (end == 0) return gray_string_lit("/");
    int start = end;
    while (start > 0 && path.data[start - 1] != '/' && path.data[start - 1] != '\\') start--;
    int32_t len = (int32_t)(end - start);
    char *buf = gray_arena_alloc(arena, (size_t)len + 1);
    memcpy(buf, path.data + start, (size_t)len);
    buf[len] = '\0';
    return (GrayString){ buf, len };
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
    int32_t len = (int32_t)(path.len - dot_position);
    char *buf = gray_arena_alloc(arena, (size_t)len + 1);
    memcpy(buf, path.data + dot_position, (size_t)len);
    buf[len] = '\0';
    return (GrayString){ buf, len };
}

bool gray_io_is_absolute(GrayString path) {
    return path.len > 0 && path.data[0] == '/';
}

GrayString gray_io_normalize(GrayArena *arena, GrayString path) {
    if (path.len == 0) return gray_string_lit(".");
    char *buf = gray_arena_alloc(arena, (size_t)path.len + 1);
    /* Copy input, converting backslashes to forward slashes */
    for (int i = 0; i < path.len; i++) {
        buf[i] = (path.data[i] == '\\') ? '/' : path.data[i];
    }
    buf[path.len] = '\0';
    bool absolute = (buf[0] == '/');

    /* Split into segments */
    char *segments[GRAY_IO_MAX_SEGMENTS];
    int seg_count = 0;
    char *tok = buf;
    while (*tok) {
        while (*tok == '/') tok++;
        if (*tok == '\0') break;
        char *seg_start = tok;
        while (*tok && *tok != '/') tok++;
        if (*tok) { *tok = '\0'; tok++; }
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
    char *out = gray_arena_alloc(arena, (size_t)path.len + 2);
    int pos = 0;
    if (absolute) out[pos++] = '/';
    for (int i = 0; i < seg_count; i++) {
        if (i > 0) out[pos++] = '/';
        int segment_length = (int)strlen(segments[i]);
        memcpy(out + pos, segments[i], (size_t)segment_length);
        pos += segment_length;
    }
    if (pos == 0) {
        out[0] = '.';
        pos = 1;
    }
    out[pos] = '\0';
    return (GrayString){ out, (int32_t)pos };
}

/* ---- Existing file operations ---- */

GrayString gray_io_read_file(GrayArena *arena, GrayString path) {
    validate_path(path);
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode))
        gray_panic_code("P0086", "io.read_file() cannot read a directory; use io.list_dir() or io.walk() to list directory contents");
    FILE *f = fopen(path.data, "rb");
    if (!f) return gray_string_lit("");

    /* Try to size the buffer from the file. Falls back to streaming
     * when the file isn't seekable (pipes, FIFOs, /dev/stdin, /proc,
     * sockets). On those, ftell returns -1, which used to wrap to
     * (size_t)-1 + 1 = 0 and let fread write past the arena slot. */
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        size = ftell(f);
        if (size >= 0) (void)fseek(f, 0, SEEK_SET);
    }

    if (size >= 0 && size <= INT32_MAX) {
        char *buf = gray_arena_alloc(arena, (size_t)size + 1);
        size_t read = fread(buf, 1, (size_t)size, f);
        buf[read] = '\0';
        fclose(f);
        return (GrayString){ buf, (int32_t)read };
    }

    /* Streaming fallback for non-seekable inputs. */
    clearerr(f);
    size_t capacity = 4096;
    size_t len = 0;
    char *buf = gray_arena_alloc(arena, capacity);
    for (;;) {
        if (len == capacity) {
            if (capacity > (size_t)INT32_MAX / 2) {
                fclose(f);
                gray_panic_code("P0053", "io.read_file: input exceeds maximum string length");
            }
            size_t new_capacity = capacity * 2;
            char *new_buffer = gray_arena_alloc(arena, new_capacity);
            memcpy(new_buffer, buf, len);
            buf = new_buffer;
            capacity = new_capacity;
        }
        size_t got = fread(buf + len, 1, capacity - len, f);
        if (got == 0) break;
        len += got;
    }
    if (len == capacity) {
        char *grow = gray_arena_alloc(arena, len + 1);
        memcpy(grow, buf, len);
        buf = grow;
    }
    buf[len] = '\0';
    fclose(f);
    return (GrayString){ buf, (int32_t)len };
}

GrayArray gray_io_read_bytes(GrayArena *arena, GrayString path) {
    validate_path(path);
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode))
        gray_panic_code("P0086", "io.read_bytes() cannot read a directory");
    FILE *f = fopen(path.data, "rb");
    GrayArray arr = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0);
    if (!f) return arr;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++)
            GRAY_ARRAY_PUSH(arena, &arr, &buf[i]);
    }
    fclose(f);
    return arr;
}

GrayArray gray_io_read_lines(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayArray arr = gray_array_new(arena, (int32_t)sizeof(GrayString), 16);
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode))
        gray_panic_code("P0086", "io.read_lines() cannot read a directory");
    GrayString content = gray_io_read_file(arena, path);
    if (content.len == 0) return arr;
    const char *p = content.data;
    const char *end = content.data + content.len;
    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        int32_t len = (int32_t)(nl - p);
        /* Strip trailing \r for Windows line endings */
        if (len > 0 && p[len - 1] == '\r') len--;
        char *linebuf = gray_arena_alloc(arena, (size_t)len + 1);
        memcpy(linebuf, p, (size_t)len);
        linebuf[len] = '\0';
        GrayString line = { linebuf, len };
        GRAY_ARRAY_PUSH(arena, &arr, &line);
        p = nl + 1;
    }
    return arr;
}

bool gray_io_file_exists(GrayString path) {
    validate_path(path);
    return access(path.data, F_OK) == 0;
}

bool gray_io_is_file(GrayString path) {
    validate_path(path);
    struct stat st;
    if (stat(path.data, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

bool gray_io_is_directory(GrayString path) {
    validate_path(path);
    struct stat st;
    if (stat(path.data, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

int64_t gray_io_file_size(GrayString path) {
    validate_path(path);
    struct stat st;
    if (stat(path.data, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

GrayResult_int gray_io_file_size_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    struct stat st;
    if (stat(path.data, &st) != 0)
        return (GrayResult_int){-1, gray_error_new(arena,
            gray_string_format(arena, "cannot stat '%s'", path.data))};
    return (GrayResult_int){(int64_t)st.st_size, NULL};
}

bool gray_io_write_file(GrayString path, GrayString content) {
    validate_path(path);
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode))
        gray_panic_code("P0087", "io.write_file() cannot write to a directory");
    FILE *f = fopen(path.data, "wb");
    if (!f) return false;
    size_t written = fwrite(content.data, 1, (size_t)content.len, f);
    fclose(f);
    return written == (size_t)content.len;
}

bool gray_io_append_file(GrayString path, GrayString content) {
    validate_path(path);
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode))
        gray_panic_code("P0088", "io.append_file() cannot append to a directory");
    FILE *f = fopen(path.data, "ab");
    if (!f) return false;
    size_t written = fwrite(content.data, 1, (size_t)content.len, f);
    fclose(f);
    return written == (size_t)content.len;
}

bool gray_io_write_bytes(GrayString path, GrayArray data) {
    validate_path(path);
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode))
        gray_panic_code("P0087", "io.write_bytes() cannot write to a directory");
    FILE *f = fopen(path.data, "wb");
    if (!f) return false;
    size_t written = fwrite(data.data, 1, (size_t)data.len, f);
    fclose(f);
    return written == (size_t)data.len;
}

bool gray_io_append_bytes(GrayString path, GrayArray data) {
    validate_path(path);
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode))
        gray_panic_code("P0088", "io.append_bytes() cannot append to a directory");
    FILE *f = fopen(path.data, "ab");
    if (!f) return false;
    size_t written = fwrite(data.data, 1, (size_t)data.len, f);
    fclose(f);
    return written == (size_t)data.len;
}

GrayString gray_io_temp_file(GrayArena *arena) {
#if GRAY_RT_WINDOWS
    char tmp[MAX_PATH];
    if (!GetTempPathA(sizeof(tmp), tmp)) return gray_string_lit("");
    char path[MAX_PATH];
    if (!GetTempFileNameA(tmp, "gray", 0, path)) return gray_string_lit("");
    temp_registry_add(path);
    return gray_string_new(arena, path, (int32_t)strlen(path));
#else
    char tmpl[] = "/tmp/gray_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return gray_string_lit("");
    close(fd);
    temp_registry_add(tmpl);
    return gray_string_new(arena, tmpl, (int32_t)strlen(tmpl));
#endif
}

GrayString gray_io_temp_dir(GrayArena *arena) {
#if GRAY_RT_WINDOWS
    char tmp[MAX_PATH];
    if (!GetTempPathA(sizeof(tmp), tmp)) return gray_string_lit("");
    char path[MAX_PATH];
    /* GetTempFileNameA creates a 0-byte file; repurpose the name as a dir. */
    if (!GetTempFileNameA(tmp, "gray", 0, path)) return gray_string_lit("");
    DeleteFileA(path);
    if (!CreateDirectoryA(path, NULL)) return gray_string_lit("");
    temp_registry_add(path);
    return gray_string_new(arena, path, (int32_t)strlen(path));
#else
    char tmpl[] = "/tmp/gray_XXXXXX";
    if (!mkdtemp(tmpl)) return gray_string_lit("");
    temp_registry_add(tmpl);
    return gray_string_new(arena, tmpl, (int32_t)strlen(tmpl));
#endif
}

bool gray_io_delete_file(GrayString path) {
    validate_path(path);
    struct stat st;
    if (stat(path.data, &st) == 0 && S_ISDIR(st.st_mode))
        gray_panic_code("P0077", "io.delete_file() cannot delete a directory; use io.remove_dir() for directories");
    return unlink(path.data) == 0;
}

bool gray_io_rename_file(GrayString old_path, GrayString new_path) {
    validate_path(old_path);
    validate_path(new_path);
    return rename(old_path.data, new_path.data) == 0;
}

/* ---- New file operations ---- */

bool gray_io_copy_file(GrayString src, GrayString dst) {
    validate_path(src);
    validate_path(dst);
    struct stat _st;
    if (stat(src.data, &_st) == 0 && S_ISDIR(_st.st_mode))
        gray_panic_code("P0089", "io.copy_file() cannot copy a directory; use io.walk() to enumerate files and copy them individually");
    FILE *in = fopen(src.data, "rb");
    if (!in) return false;
    int out_fd = open(dst.data, O_WRONLY | O_CREAT | O_TRUNC, GRAY_IO_FILE_MODE);
    if (out_fd < 0) { fclose(in); return false; }
    FILE *out = fdopen(out_fd, "wb");
    if (!out) { close(out_fd); fclose(in); return false; }
    char buf[GRAY_IO_COPY_BUF];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    fclose(out);
    return ok;
}

bool gray_io_move_file(GrayString src, GrayString dst) {
    validate_path(src);
    validate_path(dst);
    if (rename(src.data, dst.data) == 0) return true;
    if (!gray_io_copy_file(src, dst)) return false;
    unlink(src.data);
    return true;
}

/* ---- Directory operations ---- */

GrayArray gray_io_list_dir(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayArray arr = gray_array_new(arena, (int32_t)sizeof(GrayString), 16);
    DIR *d = opendir(path.data);
    if (!d) return arr;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        GrayString name = gray_string_format(arena, "%s", ent->d_name);
        GRAY_ARRAY_PUSH(arena, &arr, &name);
    }
    closedir(d);
    return arr;
}

bool gray_io_make_dir(GrayString path) {
    validate_path(path);
    return gray_rt_mkdir(path.data, GRAY_IO_DIR_MODE) == 0;
}

bool gray_io_make_dir_all(GrayString path) {
    validate_path(path);
    if (path.len == 0) return false;
    char buf[GRAY_IO_PATH_BUF];
    if ((size_t)path.len >= sizeof(buf)) return false;
    memcpy(buf, path.data, (size_t)path.len);
    buf[path.len] = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            gray_rt_mkdir(buf, GRAY_IO_DIR_MODE);
            *p = '/';
        }
    }
    return gray_rt_mkdir(buf, GRAY_IO_DIR_MODE) == 0 || errno == EEXIST;
}

bool gray_io_remove_dir(GrayString path) {
    validate_path(path);
    return rmdir(path.data) == 0;
}

static bool remove_dir_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) return false;
    struct dirent *ent;
    bool ok = true;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[GRAY_IO_PATH_BUF];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        struct stat st;
        if (stat(child, &st) != 0) { ok = false; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (!remove_dir_recursive(child)) ok = false;
        } else {
            if (unlink(child) != 0) ok = false;
        }
    }
    closedir(d);
    if (rmdir(path) != 0) ok = false;
    return ok;
}

bool gray_io_remove_dir_all(GrayString path) {
    validate_path(path);
    return remove_dir_recursive(path.data);
}

static void walk_recursive(GrayArena *arena, const char *base, const char *rel, GrayArray *out) {
    char full[GRAY_IO_PATH_BUF];
    if (rel[0] == '\0') {
        snprintf(full, sizeof(full), "%s", base);
    } else {
        snprintf(full, sizeof(full), "%s/%s", base, rel);
    }
    DIR *d = opendir(full);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        GrayString child_rel;
        if (rel[0] == '\0') {
            child_rel = gray_string_format(arena, "%s", ent->d_name);
        } else {
            child_rel = gray_string_format(arena, "%s/%s", rel, ent->d_name);
        }
        GRAY_ARRAY_PUSH(arena, out, &child_rel);
        char child_full[GRAY_IO_PATH_BUF];
        snprintf(child_full, sizeof(child_full), "%s/%s", full, ent->d_name);
        struct stat st;
        if (stat(child_full, &st) == 0 && S_ISDIR(st.st_mode)) {
            walk_recursive(arena, base, child_rel.data, out);
        }
    }
    closedir(d);
}

GrayArray gray_io_walk(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayArray arr = gray_array_new(arena, (int32_t)sizeof(GrayString), GRAY_IO_WALK_INITIAL_CAP);
    walk_recursive(arena, path.data, "", &arr);
    return arr;
}

GrayArray gray_io_glob(GrayArena *arena, GrayString pattern) {
    validate_path(pattern);
    GrayArray arr = gray_array_new(arena, (int32_t)sizeof(GrayString), 16);
    glob_t gl;
    if (glob(pattern.data, GLOB_NOSORT, NULL, &gl) == 0) {
        for (size_t i = 0; i < gl.gl_pathc; i++) {
            GrayString s = gray_string_format(arena, "%s", gl.gl_pathv[i]);
            GRAY_ARRAY_PUSH(arena, &arr, &s);
        }
        globfree(&gl);
    }
    return arr;
}

/* ---- Tuple-returning (fallible) versions ---- */

GrayResult_string gray_io_read_file_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_string r;
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode)) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot read '%s': is a directory", path.data));
        return r;
    }
    FILE *f = fopen(path.data, "rb");
    if (!f) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot read '%s'", path.data));
        return r;
    }
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        size = ftell(f);
        if (size >= 0) (void)fseek(f, 0, SEEK_SET);
    }
    if (size >= 0 && size <= INT32_MAX) {
        char *buf = gray_arena_alloc(arena, (size_t)size + 1);
        size_t bytes = fread(buf, 1, (size_t)size, f);
        buf[bytes] = '\0';
        fclose(f);
        r.v0 = (GrayString){ buf, (int32_t)bytes };
        r.v1 = NULL;
        return r;
    }
    if (size > INT32_MAX) {
        fclose(f);
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot read '%s': file exceeds maximum string length", path.data));
        return r;
    }
    /* Streaming fallback for non-seekable inputs (pipes, /dev/stdin, etc.) */
    clearerr(f);
    size_t capacity = 4096;
    size_t len = 0;
    char *buf = gray_arena_alloc(arena, capacity);
    for (;;) {
        if (len == capacity) {
            if (capacity > (size_t)INT32_MAX / 2) {
                fclose(f);
                gray_panic_code("P0053", "io.read_file: input exceeds maximum string length");
            }
            size_t new_capacity = capacity * 2;
            char *new_buffer = gray_arena_alloc(arena, new_capacity);
            memcpy(new_buffer, buf, len);
            buf = new_buffer;
            capacity = new_capacity;
        }
        size_t got = fread(buf + len, 1, capacity - len, f);
        if (got == 0) break;
        len += got;
    }
    if (len == capacity) {
        char *grow = gray_arena_alloc(arena, len + 1);
        memcpy(grow, buf, len);
        buf = grow;
    }
    buf[len] = '\0';
    fclose(f);
    r.v0 = (GrayString){ buf, (int32_t)len };
    r.v1 = NULL;
    return r;
}

GrayResult_bool gray_io_write_file_result(GrayArena *arena, GrayString path, GrayString content) {
    validate_path(path);
    GrayResult_bool r;
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode)) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot write '%s': is a directory", path.data));
        return r;
    }
    FILE *f = fopen(path.data, "wb");
    if (!f) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot write '%s'", path.data));
        return r;
    }
    fwrite(content.data, 1, (size_t)content.len, f);
    fclose(f);
    r.v0 = true;
    r.v1 = NULL;
    return r;
}

GrayResult_bool gray_io_delete_file_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_bool r;
    struct stat st;
    if (stat(path.data, &st) == 0 && S_ISDIR(st.st_mode)) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot delete '%s': is a directory; use io.remove_dir() for directories", path.data));
        return r;
    }
    if (unlink(path.data) == 0) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot delete '%s'", path.data));
    }
    return r;
}

GrayResult_bool gray_io_append_file_result(GrayArena *arena, GrayString path, GrayString content) {
    GrayResult_bool r;
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode)) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot append to '%s': is a directory", path.data));
        return r;
    }
    if (gray_io_append_file(path, content)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot append to '%s'", path.data));
    }
    return r;
}

GrayResult_bool gray_io_rename_file_result(GrayArena *arena, GrayString old_path, GrayString new_path) {
    GrayResult_bool r;
    if (gray_io_rename_file(old_path, new_path)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot rename '%s' to '%s'", old_path.data, new_path.data));
    }
    return r;
}

GrayResult_bool gray_io_copy_file_result(GrayArena *arena, GrayString src, GrayString dst) {
    GrayResult_bool r;
    struct stat _st;
    if (stat(src.data, &_st) == 0 && S_ISDIR(_st.st_mode)) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot copy '%s': is a directory", src.data));
        return r;
    }
    if (gray_io_copy_file(src, dst)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot copy '%s' to '%s'", src.data, dst.data));
    }
    return r;
}

GrayResult_bool gray_io_move_file_result(GrayArena *arena, GrayString src, GrayString dst) {
    GrayResult_bool r;
    if (gray_io_move_file(src, dst)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot move '%s' to '%s'", src.data, dst.data));
    }
    return r;
}

GrayResult_array gray_io_list_dir_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_array r;
    DIR *d = opendir(path.data);
    if (!d) {
        r.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot list directory '%s'", path.data));
        return r;
    }
    closedir(d);
    r.v0 = gray_io_list_dir(arena, path);
    r.v1 = NULL;
    return r;
}

GrayResult_bool gray_io_make_dir_result(GrayArena *arena, GrayString path) {
    GrayResult_bool r;
    if (gray_io_make_dir(path)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create directory '%s'", path.data));
    }
    return r;
}

GrayResult_bool gray_io_make_dir_all_result(GrayArena *arena, GrayString path) {
    GrayResult_bool r;
    if (gray_io_make_dir_all(path)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create directories '%s'", path.data));
    }
    return r;
}

GrayResult_bool gray_io_remove_dir_result(GrayArena *arena, GrayString path) {
    GrayResult_bool r;
    if (gray_io_remove_dir(path)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot remove directory '%s'", path.data));
    }
    return r;
}

GrayResult_bool gray_io_remove_dir_all_result(GrayArena *arena, GrayString path) {
    GrayResult_bool r;
    if (gray_io_remove_dir_all(path)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot recursively remove '%s'", path.data));
    }
    return r;
}

GrayResult_array gray_io_walk_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_array r;
    DIR *d = opendir(path.data);
    if (!d) {
        r.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot walk directory '%s'", path.data));
        return r;
    }
    closedir(d);
    r.v0 = gray_io_walk(arena, path);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_io_read_bytes_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_array r;
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode)) {
        r.v0 = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot read '%s': is a directory", path.data));
        return r;
    }
    FILE *f = fopen(path.data, "rb");
    if (!f) {
        r.v0 = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot read '%s'", path.data));
        return r;
    }
    r.v0 = gray_array_new(arena, (int32_t)sizeof(uint8_t), 0);
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++)
            GRAY_ARRAY_PUSH(arena, &r.v0, &buf[i]);
    }
    fclose(f);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_io_read_lines_result(GrayArena *arena, GrayString path) {
    validate_path(path);
    GrayResult_array r;
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode)) {
        r.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot read '%s': is a directory", path.data));
        return r;
    }
    GrayResult_string fr = gray_io_read_file_result(arena, path);
    if (fr.v1 != NULL) {
        r.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 0);
        r.v1 = fr.v1;
        return r;
    }
    r.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 16);
    GrayString content = fr.v0;
    const char *p = content.data;
    const char *end = content.data + content.len;
    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        int32_t len = (int32_t)(nl - p);
        if (len > 0 && p[len - 1] == '\r') len--;
        char *linebuf = gray_arena_alloc(arena, (size_t)len + 1);
        memcpy(linebuf, p, (size_t)len);
        linebuf[len] = '\0';
        GrayString line = { linebuf, len };
        GRAY_ARRAY_PUSH(arena, &r.v0, &line);
        p = nl + 1;
    }
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_io_glob_result(GrayArena *arena, GrayString pattern) {
    validate_path(pattern);
    GrayResult_array r;
    glob_t gl;
    int rc = glob(pattern.data, GLOB_NOSORT, NULL, &gl);
    if (rc != 0 && rc != GLOB_NOMATCH) {
        r.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "glob pattern failed: '%s'", pattern.data));
        return r;
    }
    r.v0 = gray_array_new(arena, (int32_t)sizeof(GrayString), (int32_t)gl.gl_pathc);
    for (size_t i = 0; i < gl.gl_pathc; i++) {
        GrayString s = gray_string_format(arena, "%s", gl.gl_pathv[i]);
        GRAY_ARRAY_PUSH(arena, &r.v0, &s);
    }
    globfree(&gl);
    r.v1 = NULL;
    return r;
}

GrayResult_bool gray_io_write_bytes_result(GrayArena *arena, GrayString path, GrayArray data) {
    validate_path(path);
    GrayResult_bool r;
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode)) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot write '%s': is a directory", path.data));
        return r;
    }
    FILE *f = fopen(path.data, "wb");
    if (!f) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot write '%s'", path.data));
        return r;
    }
    fwrite(data.data, 1, (size_t)data.len, f);
    fclose(f);
    r.v0 = true;
    r.v1 = NULL;
    return r;
}

GrayResult_bool gray_io_append_bytes_result(GrayArena *arena, GrayString path, GrayArray data) {
    validate_path(path);
    GrayResult_bool r;
    struct stat _st;
    if (stat(path.data, &_st) == 0 && S_ISDIR(_st.st_mode)) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena,
            "cannot append to '%s': is a directory", path.data));
        return r;
    }
    if (gray_io_append_bytes(path, data)) {
        r.v0 = true;
        r.v1 = NULL;
    } else {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot append to '%s'", path.data));
    }
    return r;
}

GrayResult_string gray_io_temp_file_result(GrayArena *arena) {
    GrayResult_string r;
#if GRAY_RT_WINDOWS
    char tmp[MAX_PATH];
    if (!GetTempPathA(sizeof(tmp), tmp)) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create temporary file"));
        return r;
    }
    char path[MAX_PATH];
    if (!GetTempFileNameA(tmp, "gray", 0, path)) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create temporary file"));
        return r;
    }
    temp_registry_add(path);
    r.v0 = gray_string_new(arena, path, (int32_t)strlen(path));
#else
    char tmpl[] = "/tmp/gray_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create temporary file"));
        return r;
    }
    close(fd);
    temp_registry_add(tmpl);
    r.v0 = gray_string_new(arena, tmpl, (int32_t)strlen(tmpl));
#endif
    r.v1 = NULL;
    return r;
}

GrayResult_string gray_io_temp_dir_result(GrayArena *arena) {
    GrayResult_string r;
#if GRAY_RT_WINDOWS
    char tmp[MAX_PATH];
    if (!GetTempPathA(sizeof(tmp), tmp)) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create temporary directory"));
        return r;
    }
    char path[MAX_PATH];
    if (!GetTempFileNameA(tmp, "gray", 0, path)) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create temporary directory"));
        return r;
    }
    DeleteFileA(path);
    if (!CreateDirectoryA(path, NULL)) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create temporary directory"));
        return r;
    }
    temp_registry_add(path);
    r.v0 = gray_string_new(arena, path, (int32_t)strlen(path));
#else
    char tmpl[] = "/tmp/gray_XXXXXX";
    if (!mkdtemp(tmpl)) {
        r.v0 = gray_string_lit("");
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot create temporary directory"));
        return r;
    }
    temp_registry_add(tmpl);
    r.v0 = gray_string_new(arena, tmpl, (int32_t)strlen(tmpl));
#endif
    r.v1 = NULL;
    return r;
}
