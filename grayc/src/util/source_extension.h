/*
 * source_extension.h — The one definition of the file extensions a Grayscale
 * source file may carry. Every check that asks "is this a source file" or
 * strips the extension to derive a name goes through here.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_SOURCE_EXTENSION_H
#define GRAY_SOURCE_EXTENSION_H

#include <stddef.h>
#include <string.h>

#define GRAY_SOURCE_EXTENSION_COUNT 2

static const char *const GRAY_SOURCE_EXTENSIONS[GRAY_SOURCE_EXTENSION_COUNT] = { ".gray", ".grayscale" };

/* Length of the source extension ending `name`, or 0 when it has none. A name
 * that is only an extension has no stem, so it does not count as having one. */
static inline size_t gray_source_extension_length(const char *name) {
    size_t name_length = strlen(name);
    for (int index = 0; index < GRAY_SOURCE_EXTENSION_COUNT; index++) {
        size_t extension_length = strlen(GRAY_SOURCE_EXTENSIONS[index]);
        if (name_length > extension_length &&
            strcmp(name + name_length - extension_length, GRAY_SOURCE_EXTENSIONS[index]) == 0)
            return extension_length;
    }
    return 0;
}

#endif
