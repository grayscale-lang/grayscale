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
#define DIAG_INITIAL_CAP     16
#define DIAG_FORMAT_BUF      1024

#include "colors.h"

static const char *color_code(DiagnosticList *diagnostics, const char *code) {
    return diagnostics->use_color ? code : "";
}

/* --- Source line reading --- */

static const char *read_source_line_indexed(const char * const *offsets,
    int line_count, int line_num) {
    if (!offsets || line_num < 1 || line_num > line_count) return NULL;

    const char *line_start = offsets[line_num - 1];
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;

    static char buf[SOURCE_LINE_MAX];
    int len = (int)(line_end - line_start);
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, line_start, len);
    buf[len] = '\0';
    return buf;
}

static void build_line_index(DiagSourceSlot *slot) {
    const char *source = slot->source;
    int count = 1;
    for (const char *p = source; *p; p++) {
        if (*p == '\n') count++;
    }
    slot->line_offsets = xmalloc(sizeof(const char *) * (size_t)count);
    slot->line_offsets[0] = source;
    slot->line_count = 1;
    for (const char *p = source; *p && slot->line_count < count; p++) {
        if (*p == '\n') slot->line_offsets[slot->line_count++] = p + 1;
    }
}


/* --- DiagnosticList management --- */

DiagnosticList *diagnostic_create(void) {
    DiagnosticList *diagnostics = xmalloc(sizeof(DiagnosticList));
    memset(diagnostics, 0, sizeof(DiagnosticList));
    diagnostics->use_color = gray_stderr_is_tty();
    return diagnostics;
}

void diagnostic_destroy(DiagnosticList *diagnostics) {
    free(diagnostics->items);
    for (int i = 0; i < DIAG_FILE_CACHE_SIZE; i++) {
        if (diagnostics->file_cache[i].owned) free((void *)diagnostics->file_cache[i].source);
        free(diagnostics->file_cache[i].line_offsets);
    }
    free(diagnostics);
}

static void diagnostic_add(DiagnosticList *diagnostics, Severity sev, const char *code,
    const char *message, const char *file, int line, int col_start,
    int end_col, const char *help) {

    /* Cap errors at 20 to avoid flooding output */
    if (sev == SEV_ERROR && diagnostics->error_count >= MAX_ERRORS_DISPLAYED) return;

    if (diagnostics->count >= diagnostics->cap) {
        diagnostics->cap = diagnostics->cap ? diagnostics->cap * 2 : DIAG_INITIAL_CAP;
        diagnostics->items = xrealloc(diagnostics->items, sizeof(Diagnostic) * diagnostics->cap);
    }

    Diagnostic *d = &diagnostics->items[diagnostics->count++];
    d->severity = sev;
    d->code = code;
    d->message = message;
    d->file = file;
    d->line = line;
    d->column = col_start;
    d->end_column = end_col;
    d->source_line = NULL;
    d->help = help;

    if (sev == SEV_ERROR) diagnostics->error_count++;
    else if (sev == SEV_WARNING) diagnostics->warning_count++;
}

void diagnostic_error(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col_start, int end_col) {
    diagnostic_add(diagnostics, SEV_ERROR, code, message, file, line, col_start, end_col, NULL);
}

void diagnostic_error_help(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col_start, int end_col, const char *help) {
    diagnostic_add(diagnostics, SEV_ERROR, code, message, file, line, col_start, end_col, help);
}

void diagnostic_warning(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col_start, int end_col) {
    diagnostic_add(diagnostics, SEV_WARNING, code, message, file, line, col_start, end_col, NULL);
}

void diagnostic_warning_help(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col_start, int end_col, const char *help) {
    diagnostic_add(diagnostics, SEV_WARNING, code, message, file, line, col_start, end_col, help);
}

static const char *lookup_or_placeholder(const char *code) {
    const char *msg = gray_error_message(code);
    return msg ? msg : "<unknown error code>";
}

void diagnostic_error_code(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col_start, int end_col) {
    diagnostic_add(diagnostics, SEV_ERROR, code, lookup_or_placeholder(code),
        file, line, col_start, end_col, NULL);
}

void diagnostic_error_code_help(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col_start, int end_col, const char *help) {
    diagnostic_add(diagnostics, SEV_ERROR, code, lookup_or_placeholder(code),
        file, line, col_start, end_col, help);
}

void diagnostic_warning_code(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col_start, int end_col) {
    diagnostic_add(diagnostics, SEV_WARNING, code, lookup_or_placeholder(code),
        file, line, col_start, end_col, NULL);
}

void diagnostic_error_message(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col_start, int end_col) {
    diagnostic_add(diagnostics, SEV_ERROR, code, message, file, line, col_start, end_col, NULL);
}

void diagnostic_warning_message(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col_start, int end_col) {
    diagnostic_add(diagnostics, SEV_WARNING, code, message, file, line, col_start, end_col, NULL);
}

/* strdup is used so the formatted buffer outlives this stack frame;
 * the diagnostic list stores the pointer by reference. The tiny leak
 * per error is acceptable for a short-lived compiler invocation. */
static void emit_code_formatted(DiagnosticList *diagnostics, Severity sev, const char *code,
    const char *file, int line, int col_start, int end_col, va_list ap) {
    const char *tmpl = lookup_or_placeholder(code);
    char buf[DIAG_FORMAT_BUF];
    vsnprintf(buf, sizeof(buf), tmpl, ap);
    diagnostic_add(diagnostics, sev, code, strdup(buf), file, line, col_start, end_col, NULL);
}

void diagnostic_error_code_formatted(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col_start, int end_col, ...) {
    va_list ap;
    va_start(ap, end_col);
    emit_code_formatted(diagnostics, SEV_ERROR, code, file, line, col_start, end_col, ap);
    va_end(ap);
}

void diagnostic_set_source(DiagnosticList *diagnostics, const char *file, const char *source) {
    /* Slot 0 is the primary entry-file slot. Source is caller-owned — never freed here. */
    DiagSourceSlot *s = &diagnostics->file_cache[0];
    free(s->line_offsets);
    s->path = file;
    s->source = source;
    s->owned = false;
    s->line_offsets = NULL;
    s->line_count = 0;
    s->last_use = 0;
    if (source) build_line_index(s);
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
    const char *sev_str;
    const char *sev_color;

    switch (diagnostic_entry->severity) {
    case SEV_ERROR:
        sev_str = "error";
        sev_color = COL_RED;
        break;
    case SEV_WARNING:
        sev_str = "warning";
        sev_color = COL_YELLOW;
        break;
    }

    /* Line 1: severity[code]: message */
    fprintf(stderr, "%s%s%s%s", color_code(diagnostics, COL_BOLD), color_code(diagnostics, sev_color), sev_str, color_code(diagnostics, COL_RESET));
    if (diagnostic_entry->code) {
        fprintf(stderr, "%s%s[%s]%s", color_code(diagnostics, COL_BOLD), color_code(diagnostics, sev_color), diagnostic_entry->code, color_code(diagnostics, COL_RESET));
    }
    fprintf(stderr, "%s: %s%s\n", color_code(diagnostics, COL_BOLD), diagnostic_entry->message, color_code(diagnostics, COL_RESET));

    /* Line 2: --> file:line:column */
    if (diagnostic_entry->file && diagnostic_entry->line > 0) {
        fprintf(stderr, "  %s-->%s %s:%diagnostic_entry:%diagnostic_entry\n",
            color_code(diagnostics, COL_BLUE), color_code(diagnostics, COL_RESET),
            diagnostic_entry->file, diagnostic_entry->line, diagnostic_entry->column);
    }

    /* Lines 3-4: source context with underline */
    const char *src_line = diagnostic_entry->source_line;
    if (!src_line && diagnostic_entry->file) {
        /* Search cache slots for this file */
        DiagSourceSlot *slot = NULL;
        for (int ci = 0; ci < DIAG_FILE_CACHE_SIZE; ci++) {
            DiagSourceSlot *candidate = &diagnostics->file_cache[ci];
            if (candidate->source && candidate->path && strcmp(candidate->path, diagnostic_entry->file) == 0) {
                slot = candidate;
                break;
            }
        }
        if (!slot) {
            /* Cache miss: read from disk and store in the LRU secondary slot */
            const char *content = read_file_to_string(diagnostic_entry->file);
            if (content) {
                int evict = 1;
                for (int ci = 2; ci < DIAG_FILE_CACHE_SIZE; ci++) {
                    if (diagnostics->file_cache[ci].last_use < diagnostics->file_cache[evict].last_use)
                        evict = ci;
                }
                DiagSourceSlot *candidate = &diagnostics->file_cache[evict];
                if (candidate->owned) free((void *)candidate->source);
                free(candidate->line_offsets);
                candidate->path = diagnostic_entry->file;
                candidate->source = content;
                candidate->owned = true;
                candidate->line_offsets = NULL;
                candidate->line_count = 0;
                build_line_index(candidate);
                slot = candidate;
            }
        }
        if (slot) {
            slot->last_use = ++diagnostics->cache_clock;
            src_line = read_source_line_indexed(slot->line_offsets, slot->line_count, diagnostic_entry->line);
        }
    }

    if (src_line && diagnostic_entry->line > 0) {
        /* Line number gutter */
        fprintf(stderr, "   %s|%s\n", color_code(diagnostics, COL_BLUE), color_code(diagnostics, COL_RESET));
        fprintf(stderr, "%s%3d%s %s|%s %s\n",
            color_code(diagnostics, COL_BLUE), diagnostic_entry->line, color_code(diagnostics, COL_RESET),
            color_code(diagnostics, COL_BLUE), color_code(diagnostics, COL_RESET),
            src_line);

        /* Underline */
        int start = diagnostic_entry->column > 0 ? diagnostic_entry->column : 1;
        int end = diagnostic_entry->end_column > 0 ? diagnostic_entry->end_column : start;
        int span_len = end - start + 1;
        if (span_len < 1) span_len = 1;

        fprintf(stderr, "   %s|%s ", color_code(diagnostics, COL_BLUE), color_code(diagnostics, COL_RESET));
        for (int i = 1; i < start; i++) {
            fputc(' ', stderr);
        }
        fprintf(stderr, "%s%s", color_code(diagnostics, COL_BOLD), color_code(diagnostics, sev_color));
        for (int i = 0; i < span_len; i++) {
            fputc('^', stderr);
        }
        fprintf(stderr, "%s\n", color_code(diagnostics, COL_RESET));
    }

    /* Help text */
    if (diagnostic_entry->help) {
        fprintf(stderr, "   %s|%s\n", color_code(diagnostics, COL_BLUE), color_code(diagnostics, COL_RESET));
        fprintf(stderr, "   %s=%s %shelp%s: %s\n",
            color_code(diagnostics, COL_BLUE), color_code(diagnostics, COL_RESET),
            color_code(diagnostics, COL_CYAN), color_code(diagnostics, COL_RESET),
            diagnostic_entry->help);
    }

    fprintf(stderr, "\n");
}

static bool is_warning_suppressed(DiagnosticList *diagnostics, Diagnostic *d) {
    if (d->severity != SEV_WARNING) return false;
    if (diagnostics->suppress_all_warnings) return true;
    if (d->code) {
        for (int i = 0; i < diagnostics->suppressed_count; i++) {
            if (strcmp(d->code, diagnostics->suppressed_codes[i]) == 0) return true;
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
        if (diagnostics->items[i].severity == SEV_WARNING &&
            !is_warning_suppressed(diagnostics, &diagnostics->items[i]))
            warnings++;
    }

    if (errors == 0 && warnings == 0) return;

    fprintf(stderr, "%sgrayscale:%s ", color_code(diagnostics, COL_BOLD), color_code(diagnostics, COL_RESET));

    if (errors > 0) {
        fprintf(stderr, "%s%s%d error%s%s",
            color_code(diagnostics, COL_BOLD), color_code(diagnostics, COL_RED),
            errors, errors == 1 ? "" : "s",
            color_code(diagnostics, COL_RESET));
    }
    if (errors > 0 && warnings > 0) {
        fprintf(stderr, ", ");
    }
    if (warnings > 0) {
        fprintf(stderr, "%s%s%d warning%s%s",
            color_code(diagnostics, COL_BOLD), color_code(diagnostics, COL_YELLOW),
            warnings, warnings == 1 ? "" : "s",
            color_code(diagnostics, COL_RESET));
    }

    if (errors > 0) {
        fprintf(stderr, ". compilation failed.\n");
    } else {
        fprintf(stderr, "\n");
    }

    if (warnings > 0 && !diagnostics->suppress_all_warnings && diagnostics->suppressed_count == 0) {
        fprintf(stderr, "%shint:%s suppress warnings with -q <W1001,W1002,...> or -q 'all'\n",
            color_code(diagnostics, COL_BOLD), color_code(diagnostics, COL_RESET));
    }
}
