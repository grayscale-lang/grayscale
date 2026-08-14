/*
 * reserved.h — Canonical sorted lists of reserved identifiers (type names,
 * builtins, stdlib modules, stdlib structs) that the user may not shadow.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_RESERVED_H
#define GRAY_RESERVED_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static inline int gray_strptr_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* --- Reserved type names (STANDARD.md §2.5) --- */

static const char *const gray_reserved_type_names[] = {
    "Error",
    "SourceLocation",
    "bool",
    "byte",
    "char",
    "f32",
    "f64",
    "float",
    "func",
    "i128",
    "i16",
    "i256",
    "i32",
    "i64",
    "i8",
    "int",
    "map",
    "nil",
    "string",
    "u128",
    "u16",
    "u256",
    "u32",
    "u64",
    "u8",
    "uint",
    "void",
};

#define GRAY_RESERVED_TYPE_NAMES_COUNT \
    ((int)(sizeof(gray_reserved_type_names) / sizeof(gray_reserved_type_names[0])))

static inline bool is_reserved_type_name(const char *name) {
    return bsearch(&name, gray_reserved_type_names,
                   (size_t)GRAY_RESERVED_TYPE_NAMES_COUNT,
                   sizeof(const char *), gray_strptr_cmp) != NULL;
}

/* --- Builtin function names --- */

static const char *const gray_builtin_func_names[] = {
    "addr",
    "assert",
    "c_string",
    "cast",
    "char_count",
    "copy",
    "embed",
    "eprint",
    "eprintln",
    "error",
    "exit",
    "here",
    "input",
    "len",
    "panic",
    "print",
    "println",
    "ref",
    "size_of",
    "sleep_ms",
    "sleep_ns",
    "sleep_s",
    "to_char",
    "type_of",
};

#define GRAY_BUILTIN_FUNC_NAMES_COUNT \
    ((int)(sizeof(gray_builtin_func_names) / sizeof(gray_builtin_func_names[0])))

static inline bool is_reserved_builtin_func_name(const char *name) {
    return bsearch(&name, gray_builtin_func_names,
                   (size_t)GRAY_BUILTIN_FUNC_NAMES_COUNT,
                   sizeof(const char *), gray_strptr_cmp) != NULL;
}

/* --- Standard library module names --- */

static const char *const gray_stdlib_module_names[] = {
    "arrays",
    "atomic",
    "binary",
    "channels",
    "crypto",
    "csv",
    "encoding",
    "fmt",
    "http",
    "io",
    "json",
    "maps",
    "math",
    "mem",
    "net",
    "os",
    "random",
    "regex",
    "server",
    "sqlite",
    "strconv",
    "strings",
    "sync",
    "threads",
    "time",
    "uuid",
};

#define GRAY_STDLIB_MODULE_NAMES_COUNT \
    ((int)(sizeof(gray_stdlib_module_names) / sizeof(gray_stdlib_module_names[0])))

static inline bool is_stdlib_module_name(const char *name) {
    return bsearch(&name, gray_stdlib_module_names,
                   (size_t)GRAY_STDLIB_MODULE_NAMES_COUNT,
                   sizeof(const char *), gray_strptr_cmp) != NULL;
}

/* --- Unified reserved name check (types + builtins + modules) --- */

static inline bool is_reserved_name(const char *name) {
    return is_reserved_type_name(name) ||
           is_reserved_builtin_func_name(name) ||
           is_stdlib_module_name(name);
}

/* --- Reserved stdlib struct names (map to internal C types) --- */

static const char *const gray_reserved_stdlib_struct_names[] = {
    "Arena",
    "Channel",
    "Database",
    "HttpRequest",
    "HttpResponse",
    "Listener",
    "Mutex",
    "Router",
    "Socket",
    "SourceLocation",
    "SpinLock",
    "Thread",
    "UUID",
};

#define GRAY_RESERVED_STDLIB_STRUCT_NAMES_COUNT \
    ((int)(sizeof(gray_reserved_stdlib_struct_names) / sizeof(gray_reserved_stdlib_struct_names[0])))

static inline bool is_reserved_stdlib_struct_name(const char *name) {
    return bsearch(&name, gray_reserved_stdlib_struct_names,
                   (size_t)GRAY_RESERVED_STDLIB_STRUCT_NAMES_COUNT,
                   sizeof(const char *), gray_strptr_cmp) != NULL;
}

#endif /* GRAY_RESERVED_H */
