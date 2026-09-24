/*
 * error.c — Compiler diagnostic system for reporting errors, warnings,
 * and notes with source context, span underlines, and colored output.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "error.h"
#include "error_codes.h"
#include "constants.h"
#include "platform.h"
#include "xalloc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ERRORS_DISPLAYED 20
#define DIAGNOSTIC_INITIAL_CAPACITY     16
#define DIAGNOSTIC_FORMAT_BUFFER_SIZE      1024

#include "colors.h"

static const char *color_code(DiagnosticList *diagnostics, const char *code) {
    return diagnostics->should_use_color ? code : "";
}

/* --- Source line reading --- */

static const char *read_source_line_indexed(const char * const *offsets,
    int line_count, int line_number) {
    if (!offsets || line_number < 1 || line_number > line_count) return NULL;

    const char *line_start = offsets[line_number - 1];
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;

    static char line_buffer[SOURCE_LINE_MAX];
    int length = (int)(line_end - line_start);
    if (length >= (int)sizeof(line_buffer)) length = (int)sizeof(line_buffer) - 1;
    memcpy(line_buffer, line_start, length);
    line_buffer[length] = '\0';
    return line_buffer;
}

static void build_line_index(DiagnosticSourceSlot *slot) {
    const char *source = slot->source;
    int count = 1;
    for (const char *cursor = source; *cursor; cursor++) {
        if (*cursor == '\n') count++;
    }
    slot->line_offsets = xmalloc(sizeof(const char *) * (size_t)count);
    slot->line_offsets[0] = source;
    slot->line_count = 1;
    for (const char *cursor = source; *cursor && slot->line_count < count; cursor++) {
        if (*cursor == '\n') slot->line_offsets[slot->line_count++] = cursor + 1;
    }
}


/* --- DiagnosticList management --- */

DiagnosticList *diagnostic_create(void) {
    DiagnosticList *diagnostics = xmalloc(sizeof(DiagnosticList));
    memset(diagnostics, 0, sizeof(DiagnosticList));
    diagnostics->should_use_color = gray_stderr_is_terminal();
    return diagnostics;
}

void diagnostic_destroy(DiagnosticList *diagnostics) {
    free(diagnostics->items);
    for (int i = 0; i < DIAGNOSTIC_FILE_CACHE_SIZE; i++) {
        if (diagnostics->file_cache[i].is_owned) free((void *)diagnostics->file_cache[i].source);
        free(diagnostics->file_cache[i].line_offsets);
    }
    free(diagnostics);
}

static void diagnostic_add(DiagnosticList *diagnostics, Severity severity, const char *code,
    const char *message, const char *file, int line, int start_column,
    int end_column, const char *help) {

    /* Cap errors at 20 to avoid flooding output */
    if (severity == SEVERITY_ERROR && diagnostics->error_count >= MAX_ERRORS_DISPLAYED) return;

    if (diagnostics->should_skip_duplicates) {
        for (int i = 0; i < diagnostics->count; i++) {
            const Diagnostic *existing = &diagnostics->items[i];
            if (existing->severity == severity && existing->line == line && existing->column == start_column &&
                strcmp(existing->code, code) == 0 && strcmp(existing->message, message) == 0 &&
                ((!existing->file && !file) || (existing->file && file && strcmp(existing->file, file) == 0)))
                return;
        }
    }

    if (diagnostics->count >= diagnostics->capacity) {
        diagnostics->capacity = diagnostics->capacity ? diagnostics->capacity * 2 : DIAGNOSTIC_INITIAL_CAPACITY;
        diagnostics->items = xrealloc(diagnostics->items, sizeof(Diagnostic) * diagnostics->capacity);
    }

    Diagnostic *diagnostic = &diagnostics->items[diagnostics->count++];
    diagnostic->severity = severity;
    diagnostic->code = code;
    diagnostic->message = message;
    diagnostic->file = file;
    diagnostic->line = line;
    diagnostic->column = start_column;
    diagnostic->end_column = end_column;
    diagnostic->source_line = NULL;
    diagnostic->help = help;

    if (severity == SEVERITY_ERROR) diagnostics->error_count++;
    else if (severity == SEVERITY_WARNING) diagnostics->warning_count++;
}

void diagnostic_error_help(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int start_column, int end_column, const char *help) {
    diagnostic_add(diagnostics, SEVERITY_ERROR, code, message, file, line, start_column, end_column, help);
}

static const char *lookup_or_placeholder(const char *code) {
    const char *message = gray_error_message(code);
    return message ? message : "<unknown error code>";
}

void diagnostic_error_code(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int start_column, int end_column) {
    diagnostic_add(diagnostics, SEVERITY_ERROR, code, lookup_or_placeholder(code),
        file, line, start_column, end_column, NULL);
}

void diagnostic_error_code_help(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int start_column, int end_column, const char *help) {
    diagnostic_add(diagnostics, SEVERITY_ERROR, code, lookup_or_placeholder(code),
        file, line, start_column, end_column, help);
}

void diagnostic_warning_code(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int start_column, int end_column) {
    diagnostic_add(diagnostics, SEVERITY_WARNING, code, lookup_or_placeholder(code),
        file, line, start_column, end_column, NULL);
}

void diagnostic_error_message(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int start_column, int end_column) {
    diagnostic_add(diagnostics, SEVERITY_ERROR, code, message, file, line, start_column, end_column, NULL);
}

void diagnostic_warning_message(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int start_column, int end_column) {
    diagnostic_add(diagnostics, SEVERITY_WARNING, code, message, file, line, start_column, end_column, NULL);
}

/* strdup is used so the formatted buffer outlives this stack frame;
 * the diagnostic list stores the pointer by reference. The tiny leak
 * per error is acceptable for a short-lived compiler invocation. */
static void emit_code_formatted_help(DiagnosticList *diagnostics, Severity severity, const char *code,
    const char *file, int line, int start_column, int end_column, const char *help, va_list arguments) {
    const char *message_template = lookup_or_placeholder(code);
    char formatted_message[DIAGNOSTIC_FORMAT_BUFFER_SIZE];
    vsnprintf(formatted_message, sizeof(formatted_message), message_template, arguments);
    diagnostic_add(diagnostics, severity, code, strdup(formatted_message), file, line, start_column, end_column, help);
}

static void emit_code_formatted(DiagnosticList *diagnostics, Severity severity, const char *code,
    const char *file, int line, int start_column, int end_column, va_list arguments) {
    emit_code_formatted_help(diagnostics, severity, code, file, line, start_column, end_column, NULL, arguments);
}

void diagnostic_error_code_formatted(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int start_column, int end_column, ...) {
    va_list arguments;
    va_start(arguments, end_column);
    emit_code_formatted(diagnostics, SEVERITY_ERROR, code, file, line, start_column, end_column, arguments);
    va_end(arguments);
}

void diagnostic_error_code_formatted_help(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int start_column, int end_column, const char *help, ...) {
    va_list arguments;
    va_start(arguments, help);
    emit_code_formatted_help(diagnostics, SEVERITY_ERROR, code, file, line, start_column, end_column, help, arguments);
    va_end(arguments);
}

void diagnostic_set_source(DiagnosticList *diagnostics, const char *file, const char *source) {
    /* Slot 0 is the primary entry-file slot. Source is caller-owned — never freed here. */
    DiagnosticSourceSlot *slot = &diagnostics->file_cache[0];
    free(slot->line_offsets);
    slot->path = file;
    slot->source = source;
    slot->is_owned = false;
    slot->line_offsets = NULL;
    slot->line_count = 0;
    slot->last_use = 0;
    if (source) build_line_index(slot);
}

bool diagnostic_has_errors(DiagnosticList *diagnostics) {
    return diagnostic_error_count(diagnostics) > 0;
}

int diagnostic_error_count(DiagnosticList *diagnostics) {
    return diagnostics->error_count;
}

int diagnostic_warning_count(DiagnosticList *diagnostics) {
    return diagnostics->warning_count;
}

/* --- Rendering --- */

static void print_diagnostic(DiagnosticList *diagnostics, Diagnostic *diagnostic_entry) {
    const char *severity_label;
    const char *severity_color;

    switch (diagnostic_entry->severity) {
    case SEVERITY_ERROR:
        severity_label = "error";
        severity_color = COLOR_RED;
        break;
    case SEVERITY_WARNING:
        severity_label = "warning";
        severity_color = COLOR_YELLOW;
        break;
    }

    /* Line 1: severity[code]: message */
    fprintf(stderr, "%s%s%s%s", color_code(diagnostics, COLOR_BOLD), color_code(diagnostics, severity_color), severity_label, color_code(diagnostics, COLOR_RESET));
    if (diagnostic_entry->code) {
        fprintf(stderr, "%s%s[%s]%s", color_code(diagnostics, COLOR_BOLD), color_code(diagnostics, severity_color), diagnostic_entry->code, color_code(diagnostics, COLOR_RESET));
    }
    fprintf(stderr, "%s: %s%s\n", color_code(diagnostics, COLOR_BOLD), diagnostic_entry->message, color_code(diagnostics, COLOR_RESET));

    /* Line 2: --> file:line:column */
    if (diagnostic_entry->file && diagnostic_entry->line > 0) {
        fprintf(stderr, "  %s-->%s %s:%d:%d\n",
            color_code(diagnostics, COLOR_BLUE), color_code(diagnostics, COLOR_RESET),
            diagnostic_entry->file, diagnostic_entry->line, diagnostic_entry->column);
    }

    /* Lines 3-4: source context with underline */
    const char *source_line_text = diagnostic_entry->source_line;
    if (!source_line_text && diagnostic_entry->file) {
        /* Search cache slots for this file */
        DiagnosticSourceSlot *slot = NULL;
        for (int cache_index = 0; cache_index < DIAGNOSTIC_FILE_CACHE_SIZE; cache_index++) {
            DiagnosticSourceSlot *candidate = &diagnostics->file_cache[cache_index];
            if (candidate->source && candidate->path && strcmp(candidate->path, diagnostic_entry->file) == 0) {
                slot = candidate;
                break;
            }
        }
        if (!slot) {
            /* Cache miss: read from disk and store in the LRU secondary slot */
            const char *content = read_file_to_string(diagnostic_entry->file);
            if (content) {
                int evicted_index = 1;
                for (int cache_index = 2; cache_index < DIAGNOSTIC_FILE_CACHE_SIZE; cache_index++) {
                    if (diagnostics->file_cache[cache_index].last_use < diagnostics->file_cache[evicted_index].last_use)
                        evicted_index = cache_index;
                }
                DiagnosticSourceSlot *candidate = &diagnostics->file_cache[evicted_index];
                if (candidate->is_owned) free((void *)candidate->source);
                free(candidate->line_offsets);
                candidate->path = diagnostic_entry->file;
                candidate->source = content;
                candidate->is_owned = true;
                candidate->line_offsets = NULL;
                candidate->line_count = 0;
                build_line_index(candidate);
                slot = candidate;
            }
        }
        if (slot) {
            slot->last_use = ++diagnostics->cache_clock;
            source_line_text = read_source_line_indexed(slot->line_offsets, slot->line_count, diagnostic_entry->line);
        }
    }

    if (source_line_text && diagnostic_entry->line > 0) {
        /* Line number gutter */
        fprintf(stderr, "   %s|%s\n", color_code(diagnostics, COLOR_BLUE), color_code(diagnostics, COLOR_RESET));
        fprintf(stderr, "%s%3d%s %s|%s %s\n",
            color_code(diagnostics, COLOR_BLUE), diagnostic_entry->line, color_code(diagnostics, COLOR_RESET),
            color_code(diagnostics, COLOR_BLUE), color_code(diagnostics, COLOR_RESET),
            source_line_text);

        /* Underline */
        int start = diagnostic_entry->column > 0 ? diagnostic_entry->column : 1;
        int end = diagnostic_entry->end_column > 0 ? diagnostic_entry->end_column : start;
        int span_length = end - start + 1;
        if (span_length < 1) span_length = 1;

        fprintf(stderr, "   %s|%s ", color_code(diagnostics, COLOR_BLUE), color_code(diagnostics, COLOR_RESET));
        for (int i = 1; i < start; i++) {
            fputc(' ', stderr);
        }
        fprintf(stderr, "%s%s", color_code(diagnostics, COLOR_BOLD), color_code(diagnostics, severity_color));
        for (int i = 0; i < span_length; i++) {
            fputc('^', stderr);
        }
        fprintf(stderr, "%s\n", color_code(diagnostics, COLOR_RESET));
    }

    /* Help text */
    if (diagnostic_entry->help) {
        fprintf(stderr, "   %s|%s\n", color_code(diagnostics, COLOR_BLUE), color_code(diagnostics, COLOR_RESET));
        fprintf(stderr, "   %s=%s %shelp%s: %s\n",
            color_code(diagnostics, COLOR_BLUE), color_code(diagnostics, COLOR_RESET),
            color_code(diagnostics, COLOR_CYAN), color_code(diagnostics, COLOR_RESET),
            diagnostic_entry->help);
    }

    fprintf(stderr, "\n");
}

static bool is_warning_suppressed(DiagnosticList *diagnostics, Diagnostic *diagnostic) {
    if (diagnostic->severity != SEVERITY_WARNING) return false;
    if (diagnostics->should_suppress_all_warnings) return true;
    if (diagnostic->code) {
        for (int i = 0; i < diagnostics->suppressed_count; i++) {
            if (strcmp(diagnostic->code, diagnostics->suppressed_codes[i]) == 0) return true;
        }
    }
    return false;
}

void diagnostic_print_all(DiagnosticList *diagnostics) {
    for (int i = 0; i < diagnostics->count; i++) {
        if (is_warning_suppressed(diagnostics, &diagnostics->items[i])) continue;
        print_diagnostic(diagnostics, &diagnostics->items[i]);
    }
}

void diagnostic_print_summary(DiagnosticList *diagnostics) {
    int errors = diagnostics->error_count;

    /* visible_warnings excludes suppressed entries — checked at render time */
    int warnings = 0;
    for (int i = 0; i < diagnostics->count; i++) {
        if (diagnostics->items[i].severity == SEVERITY_WARNING &&
            !is_warning_suppressed(diagnostics, &diagnostics->items[i]))
            warnings++;
    }

    if (errors == 0 && warnings == 0) return;

    fprintf(stderr, "%sgrayscale:%s ", color_code(diagnostics, COLOR_BOLD), color_code(diagnostics, COLOR_RESET));

    if (errors > 0) {
        fprintf(stderr, "%s%s%d error%s%s",
            color_code(diagnostics, COLOR_BOLD), color_code(diagnostics, COLOR_RED),
            errors, errors == 1 ? "" : "s",
            color_code(diagnostics, COLOR_RESET));
    }
    if (errors > 0 && warnings > 0) {
        fprintf(stderr, ", ");
    }
    if (warnings > 0) {
        fprintf(stderr, "%s%s%d warning%s%s",
            color_code(diagnostics, COLOR_BOLD), color_code(diagnostics, COLOR_YELLOW),
            warnings, warnings == 1 ? "" : "s",
            color_code(diagnostics, COLOR_RESET));
    }

    if (errors > 0) {
        fprintf(stderr, ". compilation failed.\n");
    } else {
        fprintf(stderr, "\n");
    }

    if (warnings > 0 && !diagnostics->should_suppress_all_warnings && diagnostics->suppressed_count == 0) {
        fprintf(stderr, "%shint:%s suppress warnings with -q <W1001,W1002,...> or -q 'all'\n",
            color_code(diagnostics, COLOR_BOLD), color_code(diagnostics, COLOR_RESET));
    }
}
