/*
 * runtime_mod.c — Implementation of the runtime stdlib module.
 * Thin wrappers exposing compiler-managed arena statistics and
 * execution state that already live in the core runtime.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "runtime_mod.h"

int64_t gray_runtime_arena_usage(void) {
    return (int64_t)gray_arena_usage(gray_default_arena);
}

int64_t gray_runtime_heap_usage(void) {
    return (int64_t)gray_arena_usage(gray_heap_arena);
}

int64_t gray_runtime_total_usage(void) {
    return gray_runtime_arena_usage() + gray_runtime_heap_usage();
}

int64_t gray_runtime_peak_usage(void) {
    return (int64_t)(gray_default_arena->peak_bytes + gray_heap_arena->peak_bytes);
}

int64_t gray_runtime_alloc_count(void) {
    return (int64_t)gray_total_alloc_count;
}

int64_t gray_runtime_arena_blocks(void) {
    return (int64_t)gray_arena_block_count(gray_default_arena);
}

int64_t gray_runtime_heap_blocks(void) {
    return (int64_t)gray_arena_block_count(gray_heap_arena);
}

int64_t gray_runtime_arena_limit(void) {
    return (int64_t)gray_default_arena->maximum_bytes;
}

int64_t gray_runtime_call_depth(void) {
    return (int64_t)gray_call_depth;
}

GrayString gray_runtime_caller_name(void) {
    return gray_string_lit(gray_call_depth > 1 ? gray_call_names[gray_call_depth - 1] : "");
}

GrayString gray_runtime_current_name(void) {
    return gray_string_lit(gray_call_depth > 0 ? gray_call_names[gray_call_depth] : "");
}

int64_t gray_runtime_call_limit(void) {
    return GRAY_MAX_CALL_DEPTH;
}
