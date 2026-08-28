/*
 * error.h — Public interface for the compiler diagnostic system, providing
 * structured error/warning reporting with source context and help text.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_ERROR_H
#define GRAYC_ERROR_H

#include <stdbool.h>

/* Number of source-file slots in the diagnostic source cache.
 * Slot 0 is the primary entry file (set via diagnostic_set_source, never evicted).
 * Slots 1+ are LRU-managed for imported files read on-demand. */
#define DIAG_FILE_CACHE_SIZE 4

typedef struct {
    const char *path;
    const char *source;
    const char **line_offsets; /* line_offsets[i-1] = start of line i */
    int line_count;
    bool owned;                /* true if source was allocated by diag (disk read) */
    unsigned int last_use;     /* LRU clock value; 0 = empty slot */
} DiagSourceSlot;

typedef enum {
    SEV_ERROR,
    SEV_WARNING,
} Severity;

typedef struct {
    Severity severity;
    const char *code;       /* "E3018", "W2001", etc. */
    const char *message;    /* Primary error message */
    const char *file;
    int line;               /* 1-indexed */
    int column;             /* 1-indexed start column */
    int end_column;         /* 1-indexed end column (0 = same as column) */
    const char *source_line;/* The actual source line (optional, will be read if NULL) */
    const char *help;       /* Optional help/suggestion text */
} Diagnostic;

typedef struct {
    Diagnostic *items;
    int count;
    int cap;

    /* Cached counts — incremented in diagnostic_add for O(1) queries */
    int error_count;
    int warning_count;

    /* Multi-slot source cache: slot 0 = entry file (caller-owned, never evicted);
     * slots 1..DIAG_FILE_CACHE_SIZE-1 = LRU-managed disk-read files. */
    DiagSourceSlot file_cache[DIAG_FILE_CACHE_SIZE];
    unsigned int cache_clock;

    /* Options */
    bool use_color;

    /* Warning suppression (-q / --quiet) */
    bool suppress_all_warnings;
    const char **suppressed_codes;
    int suppressed_count;
} DiagnosticList;

/* Create/destroy */
DiagnosticList *diagnostic_create(void);
void diagnostic_destroy(DiagnosticList *diagnostics);

/* Add diagnostics */
void diagnostic_error(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col, int end_col);

void diagnostic_error_help(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col, int end_col, const char *help);

void diagnostic_warning(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col, int end_col);

/* Code-aware emission. Every new emission site should pick one of:
 *
 *   diagnostic_error_code   / diagnostic_warning_code   — registry message, no args.
 *   diagnostic_error_code_formatted  / diagnostic_warning_code_formatted  — registry template + args.
 *   diagnostic_error_message    / diagnostic_warning_message    — intentional custom message
 *                                             (same shape as the old
 *                                             diagnostic_error; used when the
 *                                             code is categorical and
 *                                             the site adds context,
 *                                             e.g. most parser syntax
 *                                             errors). */
void diagnostic_error_code(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col, int end_col);

/* Same shape as diagnostic_error_code, plus an actionable hint shown as `= help:`
 * under the diagnostic. Use this when the registry message states the
 * problem and the hint tells the user how to fix it. */
void diagnostic_error_code_help(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col, int end_col, const char *help);

/* Registry template + args, plus an actionable hint shown as `= help:`.
 * The formatted counterpart of diagnostic_error_code_help. */
void diagnostic_error_code_formatted_help(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col_start, int end_col, const char *help, ...);

void diagnostic_error_code_formatted(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col, int end_col, ...);

void diagnostic_error_message(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col, int end_col);

void diagnostic_warning_code(DiagnosticList *diagnostics, const char *code,
    const char *file, int line, int col, int end_col);

void diagnostic_warning_message(DiagnosticList *diagnostics, const char *code, const char *message,
    const char *file, int line, int col, int end_col);

/* Set the source text for a file (avoids re-reading from disk) */
void diagnostic_set_source(DiagnosticList *diagnostics, const char *file, const char *source);

/* Query */
bool diagnostic_has_errors(DiagnosticList *diagnostics);
int diagnostic_error_count(DiagnosticList *diagnostics);
int diagnostic_warning_count(DiagnosticList *diagnostics);

/* Render all diagnostics to stderr */
void diagnostic_print_all(DiagnosticList *diagnostics);

/* Print summary line: "grayc: 2 errors, 1 warning" */
void diagnostic_print_summary(DiagnosticList *diagnostics);

#endif
