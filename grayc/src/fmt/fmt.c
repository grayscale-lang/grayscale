/*
 * fmt.c — Grayscale source formatter implementation. Lexes the source to
 * build a per-line indentation depth table based on brace nesting, then
 * re-emits each original line with corrected leading whitespace.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "fmt.h"
#include "../lexer/lexer.h"
#include "../lexer/token.h"
#include "../util/arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define FMT_INDENT_WIDTH 4
#define FMT_ARENA_SIZE   (1024 * 1024)  /* 1 MB — enough for any source file */

/* Count the number of newlines in src to find the maximum line number. */
static int count_lines(const char *src) {
    int n = 1;
    for (const char *p = src; *p; p++) {
        if (*p == '\n') n++;
    }
    return n;
}

/* Grows `*arr` (currently `*cap` bools) to fit index `need`, doubling as
 * necessary. Used for the open-brace stack below, whose depth is unbounded
 * in principle (arbitrarily nested literals/blocks). */
static bool brace_stack_reserve(bool **arr, size_t *cap, size_t need) {
    if (need < *cap) return true;
    size_t new_cap = *cap ? *cap * 2 : 64;
    while (new_cap <= need) new_cap *= 2;
    bool *grown = realloc(*arr, new_cap * sizeof(bool));
    if (!grown) return false;
    *arr = grown;
    *cap = new_cap;
    return true;
}

/*
 * Builds two per-line tables (1-indexed, size max_line+2 each):
 *
 *   *out_depth          — brace-nesting depth at the START of that line, for
 *                          normal reindentation. TOK_RBRACE decrements the
 *                          depth BEFORE the line is recorded, so a closing
 *                          brace line gets the outer depth. TOK_LBRACE
 *                          increments AFTER, so the opening-brace line
 *                          itself stays at the current depth and lines
 *                          inside get depth+1. A `{` immediately after `=`
 *                          is a collection literal, not a scope, and is
 *                          excluded from this depth entirely (see below).
 *
 *   *out_is_continuation — true when that line continues an unfinished
 *                          statement from an earlier line, because either:
 *                            - a paren, bracket, or literal `{` opened on an
 *                              earlier line is still unclosed at the start
 *                              of this line (an argument list, index
 *                              expression, or collection literal spanning
 *                              lines), or
 *                            - the previous line's last token, or this
 *                              line's first token, is `&&` or `||` (a
 *                              multi-line boolean condition with no
 *                              bracket at all).
 *                          Brace depth alone has no opinion on how far past
 *                          the enclosing block such a line should sit, so
 *                          the caller preserves it instead of recomputing
 *                          it from scratch.
 *
 * Lines that contain no tokens (blank lines or comment-only lines) inherit
 * both values from the nearest preceding token line.
 *
 * Returns false (and frees nothing for the caller to free) on allocation or
 * lexer failure.
 */
static bool build_depth_tables(const char *src, const char *filename, int max_line,
                               int **out_depth, bool **out_is_continuation) {
    int *table = calloc(max_line + 2, sizeof(int));
    int *group_start = calloc(max_line + 2, sizeof(int));
    int *first_tok = calloc(max_line + 2, sizeof(int));
    int *last_tok = calloc(max_line + 2, sizeof(int));
    bool *is_continuation = calloc(max_line + 2, sizeof(bool));
    if (!table || !group_start || !first_tok || !last_tok || !is_continuation) {
        free(table); free(group_start); free(first_tok); free(last_tok); free(is_continuation);
        return false;
    }

    /* -1 = not yet assigned by a token */
    for (int i = 0; i <= max_line + 1; i++) {
        table[i] = -1; group_start[i] = -1; first_tok[i] = -1; last_tok[i] = -1;
    }

    /* Depth/group-depth in effect immediately after a line's own tokens are
     * processed (so a line opening a brace records depth+1 here, unlike
     * `table[]`, which deliberately keeps the opening-brace line itself at
     * the outer depth). A tokenless line — blank, or comment-only, since the
     * lexer emits no token for a comment — forward-fills from this, not
     * from `table[]`, or it would inherit the outer depth of the line
     * before an opening brace instead of the inner depth its own position
     * is at. */
    int *end_depth = calloc(max_line + 2, sizeof(int));
    int *end_group = calloc(max_line + 2, sizeof(int));
    if (!end_depth || !end_group) {
        free(table); free(group_start); free(first_tok); free(last_tok); free(is_continuation);
        free(end_depth); free(end_group);
        return false;
    }

    Arena *arena = arena_create(FMT_ARENA_SIZE);
    if (!arena) {
        free(table); free(group_start); free(first_tok); free(last_tok); free(is_continuation);
        free(end_depth); free(end_group);
        return false;
    }

    Lexer *lexer = lexer_create(arena, src, filename);
    if (!lexer) {
        arena_destroy(arena);
        free(table); free(group_start); free(first_tok); free(last_tok); free(is_continuation);
        free(end_depth); free(end_group);
        return false;
    }

    int depth = 0;
    int group_depth = 0;
    TokenType prev_type = TOK_EOF; /* neutral: never true mid-stream */
    /* Per open '{', whether it was a collection literal (preceded by '=')
     * rather than a scope — so its matching '}' dedents group_depth instead
     * of depth. Braces nest arbitrarily, so this needs a real stack, not a
     * single flag. */
    bool *brace_is_literal = NULL;
    size_t brace_stack_cap = 0;
    size_t brace_stack_len = 0;

    Token token;
    while ((token = lexer_next_token(lexer)).type != TOK_EOF) {
        if (token.type == TOK_ILLEGAL) break;
        if (token.type == TOK_NEWLINE) continue;

        int line = token.line;
        if (line < 1 || line > max_line) { prev_type = token.type; continue; }

        /* Record both depths for this line, before this token's own effect,
         * if not yet set — mirrors the brace-depth recording below. */
        if (group_start[line] < 0) {
            group_start[line] = group_depth;
        }
        if (first_tok[line] < 0) {
            first_tok[line] = (int)token.type;
        }
        last_tok[line] = (int)token.type;

        /* Closing brace: dedent first, then record. Which counter it
         * dedents depends on whether the matching '{' was a literal. */
        if (token.type == TOK_RBRACE) {
            bool was_literal = false;
            if (brace_stack_len > 0) {
                was_literal = brace_is_literal[--brace_stack_len];
            }
            if (was_literal) {
                if (group_depth > 0) group_depth--;
            } else {
                if (depth > 0) depth--;
            }
        }
        if (token.type == TOK_RPAREN || token.type == TOK_RBRACKET) {
            if (group_depth > 0) group_depth--;
        }

        /* Record depth for this line if not yet set */
        if (table[line] < 0) {
            table[line] = depth;
        }

        /* Opening brace: a literal (preceded by '=') joins group_depth, so
         * its interior is preserved like a paren continuation instead of
         * being treated as a new scope; anything else indents subsequent
         * lines as a block. */
        if (token.type == TOK_LBRACE) {
            bool is_literal = (prev_type == TOK_ASSIGN);
            if (!brace_stack_reserve(&brace_is_literal, &brace_stack_cap, brace_stack_len)) {
                arena_destroy(arena);
                free(table); free(group_start); free(first_tok); free(last_tok); free(is_continuation);
                free(end_depth); free(end_group); free(brace_is_literal);
                return false;
            }
            brace_is_literal[brace_stack_len++] = is_literal;
            if (is_literal) group_depth++;
            else depth++;
        }
        if (token.type == TOK_LPAREN || token.type == TOK_LBRACKET) {
            group_depth++;
        }

        end_depth[line] = depth;
        end_group[line] = group_depth;
        prev_type = token.type;
    }

    arena_destroy(arena);
    free(brace_is_literal);

    /* Forward-fill gaps: blank lines and comment-only lines inherit the
     * depth in effect right after the most recent token-bearing line. */
    int current = 0;
    int current_group = 0;
    for (int i = 1; i <= max_line; i++) {
        if (table[i] >= 0) {
            current = end_depth[i];
            current_group = end_group[i];
        } else {
            table[i] = current;
            group_start[i] = current_group;
        }
    }

    for (int i = 1; i <= max_line; i++) {
        bool leads_with_operator = (first_tok[i] == (int)TOK_AND || first_tok[i] == (int)TOK_OR);
        bool prev_trails_with_operator = i > 1 &&
            (last_tok[i - 1] == (int)TOK_AND || last_tok[i - 1] == (int)TOK_OR);
        is_continuation[i] = group_start[i] > 0 || leads_with_operator || prev_trails_with_operator;
    }

    free(group_start);
    free(first_tok);
    free(last_tok);
    free(end_depth);
    free(end_group);
    *out_depth = table;
    *out_is_continuation = is_continuation;
    return true;
}

/*
 * Returns true if `line` (NUL-terminated, with leading whitespace already
 * stripped) is inside a raw string that started on an earlier line.
 * We track this by counting unpaired backticks across the whole source up to
 * the given line number.  An odd count means we are inside a raw string.
 */
static bool line_is_in_raw_string(const char *src, int target_line) {
    int line = 1;
    bool in_raw = false;
    bool in_str = false;  /* regular double-quoted string */
    const char *p = src;
    while (*p && line < target_line) {
        if (*p == '\n') { line++; p++; continue; }
        if (in_raw) {
            if (*p == '`') in_raw = false;
            p++; continue;
        }
        if (in_str) {
            if (*p == '\\' && *(p + 1)) { p += 2; continue; } /* skip escape */
            if (*p == '"')  { in_str = false; }
            p++; continue;
        }
        if (*p == '`') { in_raw = true; p++; continue; }
        if (*p == '"') { in_str = true; p++; continue; }
        p++;
    }
    return in_raw;
}

/* True if `content` (length content_len) contains a star followed
 * immediately by a slash — the comment-closing sequence — anywhere at or
 * after `start_offset`. The caller passes 2 for a line that opens the
 * comment (so the sequence's own opening slash-star can never be mistaken
 * for a close) and 0 for a pure continuation line. */
static bool block_comment_closes(const char *content, int content_len, int start_offset) {
    for (int i = start_offset; i + 1 < content_len; i++) {
        if (content[i] == '*' && content[i + 1] == '/') return true;
    }
    return false;
}

int gray_fmt_source(const char *src, const char *filename, FILE *out) {
    int max_line = count_lines(src);
    int *depth_table = NULL;
    bool *continuation_table = NULL;
    if (!build_depth_tables(src, filename, max_line, &depth_table, &continuation_table))
        return 1;

    /* Walk the source line by line, re-indenting each one. */
    const char *p = src;
    int line_num = 1;

    /* Tracks an open block comment span across lines. A block comment is
     * reindented as a rigid unit: its first line gets the normal
     * depth-based indentation like any other line, and every continuation
     * line keeps its own original indentation shifted by that same delta —
     * preserving the aligned-asterisk convention (and anything else the
     * comment's author lined up) instead of recomputing each line's
     * indentation independently, which would flatten it. */
    bool in_block_comment = false;
    int block_comment_delta = 0;

    /* The delta the most recently emitted *normal* line's indentation moved
     * by (new - original). Every line continuation_table marks as a
     * continuation — an open paren/bracket/collection literal, or a
     * multi-line boolean chain with no bracket at all — reuses this same
     * delta rather than recomputing its indentation from brace depth, which
     * has no opinion on how far past the enclosing block a continuation
     * should sit. Updated only on normal lines, so a run of continuation
     * lines all shift by whatever the statement that started them moved. */
    int continuation_delta = 0;

    while (*p) {
        /* Find end of this line */
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        int line_len = (int)(p - line_start);

        /* Advance past newline for next iteration */
        if (*p == '\n') p++;

        /* Skip leading whitespace to get the trimmed content */
        const char *content = line_start;
        while (content < line_start + line_len && (*content == ' ' || *content == '\t'))
            content++;
        int content_len = (int)((line_start + line_len) - content);
        int original_leading_len = (int)(content - line_start);

        if (content_len == 0) {
            /* Blank line: emit as-is (no indentation) */
            fputc('\n', out);
        } else if (line_is_in_raw_string(src, line_num)) {
            /* Inside a raw string: preserve the original line verbatim */
            fwrite(line_start, 1, line_len, out);
            fputc('\n', out);
        } else if (in_block_comment) {
            /* Continuation of an open block comment: shift its own original
             * indentation by the same delta the comment's first line moved,
             * rather than recomputing it from brace depth. */
            int new_leading_len = original_leading_len + block_comment_delta;
            if (new_leading_len < 0) new_leading_len = 0;
            for (int i = 0; i < new_leading_len; i++) fputc(' ', out);
            fwrite(content, 1, content_len, out);
            fputc('\n', out);
            if (block_comment_closes(content, content_len, 0)) in_block_comment = false;
        } else if (line_num <= max_line && continuation_table[line_num]) {
            /* Continuation of an unfinished statement: shift its own
             * original indentation by the delta the statement's first line
             * moved, rather than recomputing it from brace depth alone. */
            int new_leading_len = original_leading_len + continuation_delta;
            if (new_leading_len < 0) new_leading_len = 0;
            for (int i = 0; i < new_leading_len; i++) fputc(' ', out);
            fwrite(content, 1, content_len, out);
            fputc('\n', out);
        } else {
            /* Emit corrected indentation + original content */
            int depth = (line_num <= max_line) ? depth_table[line_num] : 0;
            if (depth < 0) depth = 0;
            int new_leading_len = depth * FMT_INDENT_WIDTH;
            for (int i = 0; i < new_leading_len; i++) fputc(' ', out);
            fwrite(content, 1, content_len, out);
            fputc('\n', out);

            if (content_len >= 2 && content[0] == '/' && content[1] == '*') {
                block_comment_delta = new_leading_len - original_leading_len;
                in_block_comment = !block_comment_closes(content, content_len, 2);
            }

            continuation_delta = new_leading_len - original_leading_len;
        }

        line_num++;
    }

    /* Ensure file ends with exactly one newline (already handled above if
     * the source ended with \n; if it didn't, the last fputc above adds one). */

    free(depth_table);
    free(continuation_table);
    return 0;
}
