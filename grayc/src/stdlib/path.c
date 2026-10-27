/*
 * path.c — Implementation of the path stdlib module.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "path.h"
#include "../runtime/platform_rt.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if GRAY_RUNTIME_WINDOWS
#include "../runtime/win32.h"
#include <direct.h>
#else
#include <unistd.h>
#endif

#if GRAY_RUNTIME_WINDOWS
#define PATH_NATIVE_SEPARATOR '\\'
#else
#define PATH_NATIVE_SEPARATOR '/'
#endif

typedef struct { const char *start; int32_t length; } PathElement;

static bool is_separator(char character) {
    return character == '/' || character == '\\';
}

/* Length of the drive letter ("C:") or UNC ("\\server\share") prefix, or 0. */
static int32_t volume_length(const char *path, int32_t length) {
    if (length >= 2 && isalpha((unsigned char)path[0]) && path[1] == ':') return 2;
    if (length >= 5 && is_separator(path[0]) && is_separator(path[1]) &&
        !is_separator(path[2]) && path[2] != '.') {
        int32_t index = 2;
        while (index < length && !is_separator(path[index])) index++;
        if (index >= length - 1) return 0;
        index++;
        if (is_separator(path[index])) return 0;
        while (index < length && !is_separator(path[index])) index++;
        return index;
    }
    return 0;
}

static bool equal_ignoring_case(const char *left, const char *right, int32_t length) {
    for (int32_t i = 0; i < length; i++) {
        char left_char = is_separator(left[i]) ? '/' : (char)tolower((unsigned char)left[i]);
        char right_char = is_separator(right[i]) ? '/' : (char)tolower((unsigned char)right[i]);
        if (left_char != right_char) return false;
    }
    return true;
}

/* Splits the part after the volume into elements, dropping empty and "."
 * elements and resolving ".." lexically. Returns the element count; sets
 * *rooted when the part begins with a separator. */
static int32_t collect_elements(const char *rest, int32_t rest_length, PathElement *elements, bool *rooted) {
    *rooted = rest_length > 0 && is_separator(rest[0]);
    int32_t count = 0;
    int32_t index = 0;
    while (index < rest_length) {
        while (index < rest_length && is_separator(rest[index])) index++;
        int32_t start = index;
        while (index < rest_length && !is_separator(rest[index])) index++;
        int32_t length = index - start;
        if (length == 0 || (length == 1 && rest[start] == '.')) continue;
        if (length == 2 && rest[start] == '.' && rest[start + 1] == '.') {
            if (count > 0 && !(elements[count - 1].length == 2 && elements[count - 1].start[0] == '.' && elements[count - 1].start[1] == '.')) {
                count--;
                continue;
            }
            if (*rooted) continue;
        }
        elements[count].start = rest + start;
        elements[count].length = length;
        count++;
    }
    return count;
}

static GrayString clean_path(GrayArena *arena, const char *path, int32_t length) {
    int32_t volume = volume_length(path, length);
    PathElement *elements = malloc(sizeof(PathElement) * (size_t)(length + 1));
    char *buffer = malloc((size_t)length + 3);
    bool rooted;
    int32_t count = collect_elements(path + volume, length - volume, elements, &rooted);

    int32_t position = 0;
    for (int32_t i = 0; i < volume; i++)
        buffer[position++] = is_separator(path[i]) ? PATH_NATIVE_SEPARATOR : path[i];
    if (rooted) buffer[position++] = PATH_NATIVE_SEPARATOR;
    for (int32_t i = 0; i < count; i++) {
        if (i > 0) buffer[position++] = PATH_NATIVE_SEPARATOR;
        memcpy(buffer + position, elements[i].start, (size_t)elements[i].length);
        position += elements[i].length;
    }
    if (count == 0 && !rooted) {
        if (volume > 2) buffer[position++] = PATH_NATIVE_SEPARATOR;
        else buffer[position++] = '.';
    }
    GrayString result = gray_string_new(arena, buffer, position);
    free(elements);
    free(buffer);
    return result;
}

GrayString gray_path_join_parts(GrayArena *arena, GrayArray parts) {
    int64_t total = 0;
    for (int32_t i = 0; i < parts.len; i++) total += GRAY_ARRAY_GET(parts, GrayString, i).len + 1;
    char *buffer = malloc((size_t)total + 1);
    int32_t position = 0;
    for (int32_t i = 0; i < parts.len; i++) {
        GrayString part = GRAY_ARRAY_GET(parts, GrayString, i);
        if (part.len == 0) continue;
        if (position > 0) buffer[position++] = PATH_NATIVE_SEPARATOR;
        memcpy(buffer + position, part.data, (size_t)part.len);
        position += part.len;
    }
    GrayString result = position == 0 ? gray_string_lit("") : clean_path(arena, buffer, position);
    free(buffer);
    return result;
}

GrayString gray_path_normalize_path(GrayArena *arena, GrayString path) {
    return clean_path(arena, path.data, path.len);
}

GrayPathSplitResult gray_path_split_dir_file(GrayArena *arena, GrayString path) {
    int32_t volume = volume_length(path.data, path.len);
    int32_t split = path.len;
    while (split > volume && !is_separator(path.data[split - 1])) split--;
    GrayPathSplitResult result;
    result.v0 = gray_string_new(arena, path.data, split);
    result.v1 = (GrayString){path.data + split, path.len - split};
    return result;
}

GrayString gray_path_file_extension(GrayString path) {
    for (int32_t i = path.len - 1; i >= 0 && !is_separator(path.data[i]); i--) {
        if (path.data[i] == '.') return (GrayString){path.data + i, path.len - i};
    }
    return gray_string_lit("");
}

bool gray_path_is_absolute(GrayString path) {
    int32_t volume = volume_length(path.data, path.len);
    if (volume > 2) return true;
    return path.len > volume && is_separator(path.data[volume]);
}

GrayResult_path_string gray_path_relative_path(GrayArena *arena, GrayString base, GrayString target) {
    GrayResult_path_string result = {gray_string_lit(""), NULL};
    int32_t base_volume = volume_length(base.data, base.len);
    int32_t target_volume = volume_length(target.data, target.len);
    PathElement *base_elements = malloc(sizeof(PathElement) * (size_t)(base.len + 1));
    PathElement *target_elements = malloc(sizeof(PathElement) * (size_t)(target.len + 1));
    bool base_rooted, target_rooted;
    int32_t base_count = collect_elements(base.data + base_volume, base.len - base_volume, base_elements, &base_rooted);
    int32_t target_count = collect_elements(target.data + target_volume, target.len - target_volume, target_elements, &target_rooted);
    bool ignore_case = base_volume > 0 || target_volume > 0;

    const char *failure = NULL;
    if (base_volume != target_volume || !equal_ignoring_case(base.data, target.data, base_volume))
        failure = "path.relative_path: the paths have different roots";
    else if (base_rooted != target_rooted)
        failure = "path.relative_path: only one of the paths is absolute";

    int32_t common = 0;
    if (!failure) {
        while (common < base_count && common < target_count &&
               base_elements[common].length == target_elements[common].length) {
            bool same = ignore_case
                ? equal_ignoring_case(base_elements[common].start, target_elements[common].start, base_elements[common].length)
                : memcmp(base_elements[common].start, target_elements[common].start, (size_t)base_elements[common].length) == 0;
            if (!same) break;
            common++;
        }
        for (int32_t i = common; i < base_count; i++) {
            if (base_elements[i].length == 2 && base_elements[i].start[0] == '.' && base_elements[i].start[1] == '.')
                failure = "path.relative_path: the base path cannot be resolved against the target";
        }
    }

    if (failure) {
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_lit(failure));
    } else {
        size_t capacity = (size_t)(base_count * 3 + target.len + 2);
        char *buffer = malloc(capacity);
        int32_t position = 0;
        for (int32_t i = common; i < base_count; i++) {
            if (position > 0) buffer[position++] = PATH_NATIVE_SEPARATOR;
            buffer[position++] = '.';
            buffer[position++] = '.';
        }
        for (int32_t i = common; i < target_count; i++) {
            if (position > 0) buffer[position++] = PATH_NATIVE_SEPARATOR;
            memcpy(buffer + position, target_elements[i].start, (size_t)target_elements[i].length);
            position += target_elements[i].length;
        }
        if (position == 0) buffer[position++] = '.';
        result.v0 = gray_string_new(arena, buffer, position);
        free(buffer);
    }
    free(base_elements);
    free(target_elements);
    return result;
}

GrayResult_path_string gray_path_absolute_path(GrayArena *arena, GrayString path) {
    GrayResult_path_string result = {gray_string_lit(""), NULL};
    if (gray_path_is_absolute(path)) {
        result.v0 = clean_path(arena, path.data, path.len);
        return result;
    }
    if (volume_length(path.data, path.len) > 0) {
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput,
            gray_string_lit("path.absolute_path: a drive-relative path cannot be made absolute"));
        return result;
    }
    char working_directory[PATH_MAX];
#if GRAY_RUNTIME_WINDOWS
    char *cwd = _getcwd(working_directory, (int)sizeof(working_directory));
#else
    char *cwd = getcwd(working_directory, sizeof(working_directory));
#endif
    if (!cwd) {
        result.v1 = gray_error_new(arena, gray_errno_code(errno),
            gray_string_lit("path.absolute_path: cannot read the working directory"));
        return result;
    }
    size_t cwd_length = strlen(working_directory);
    char *buffer = malloc(cwd_length + (size_t)path.len + 2);
    memcpy(buffer, working_directory, cwd_length);
    buffer[cwd_length] = PATH_NATIVE_SEPARATOR;
    memcpy(buffer + cwd_length + 1, path.data, (size_t)path.len);
    result.v0 = clean_path(arena, buffer, (int32_t)(cwd_length + 1 + (size_t)path.len));
    free(buffer);
    return result;
}

GrayString gray_path_to_forward_slashes(GrayArena *arena, GrayString path) {
    char *buffer = malloc((size_t)path.len + 1);
    for (int32_t i = 0; i < path.len; i++) buffer[i] = path.data[i] == '\\' ? '/' : path.data[i];
    GrayString result = gray_string_new(arena, buffer, path.len);
    free(buffer);
    return result;
}
