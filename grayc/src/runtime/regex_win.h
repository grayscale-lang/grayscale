/*
 * regex_win.h — A POSIX extended-regular-expression engine that works on
 * characters.
 *
 * Windows ships no <regex.h> and there is no Win32 equivalent, so the regex
 * stdlib module uses this engine there. On macOS and Linux it is the fallback
 * for a system with no UTF-8 locale, where libc matches bytes.
 *
 * A character is a UTF-8 codepoint, decoded by the shared decoder in
 * utf8.h: an invalid byte is one U+FFFD character. '.' matches one
 * character, a bracket expression holds codepoints and codepoint ranges, and
 * a quantifier repeats a whole character.
 *
 * Supported: literals, '.', bracket expressions with ranges, negation and the
 * [:alpha:]-style classes (ASCII only, like the chars module), anchors, the
 * '*', '+', '?' and '{n,m}' quantifiers, alternation, grouping, and backslash
 * escapes.
 *
 * Capture groups: compiling counts them into group_count, and executing writes
 * each group's extent to spans[1..nmatch-1] (rm_so == -1 for a group that did
 * not participate). A repeated group reports its last iteration. Matching is
 * leftmost-first rather than POSIX's leftmost-longest, the same rule
 * Perl-style engines use.
 *
 * The includer may define GRAY_REGEX_SPAN_TYPE to name the span type the
 * engine fills (the POSIX build uses regmatch_t, so libc and the engine share
 * one type); otherwise the engine defines its own.
 *
 * Author:  Aristomedes (@Aristomedes)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_REGEX_WIN_H
#define GRAY_REGEX_WIN_H

#include "../stdlib/utf8.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef GRAY_REGEX_SPAN_TYPE
typedef GRAY_REGEX_SPAN_TYPE GrayRegexSpan;
#else
typedef struct {
    long rm_so;
    long rm_eo;
} GrayRegexSpan;
#endif

/* --- Pattern representation ---
 *
 * A pattern compiles to a tree of alternatives, each a sequence of quantified
 * atoms. Matching walks it recursively, which keeps the code small; the
 * quantifier loops bound their own backtracking so a pathological pattern
 * costs time rather than stack. */

typedef enum {
    GRAY_REGEX_CHARACTER,  /* one literal character */
    GRAY_REGEX_ANY,   /* .            */
    GRAY_REGEX_CLASS, /* [...]        */
    GRAY_REGEX_GROUP, /* (...)        */
    GRAY_REGEX_BEGINNING_OF_LINE,   /* ^            */
    GRAY_REGEX_END_OF_LINE    /* $            */
} GrayRegexKind;

typedef struct GrayRegexNode GrayRegexNode;
typedef struct GrayRegexAlternation GrayRegexAlternation;

/* An inclusive range of codepoints above ASCII held by a bracket expression. */
typedef struct {
    int32_t low;
    int32_t high;
} GrayRegexRange;

struct GrayRegexNode {
    GrayRegexKind kind;
    int32_t character;    /* GRAY_REGEX_CHARACTER: the codepoint */
    bool is_negated;      /* GRAY_REGEX_CLASS */
    unsigned char ascii_set[16]; /* GRAY_REGEX_CLASS: bitmap of the 128 ASCII codepoints */
    GrayRegexRange *ranges;      /* GRAY_REGEX_CLASS: codepoints of 128 and above */
    int range_count, range_capacity;
    GrayRegexAlternation *group;        /* GRAY_REGEX_GROUP */
    int group_index;      /* GRAY_REGEX_GROUP: 1-based capture number */
    int minimum_repeat, maximum_repeat; /* quantifier; maximum_repeat < 0 means unbounded */
};

/* One alternative is a sequence of nodes; a pattern is a list of alternatives. */
typedef struct {
    GrayRegexNode *nodes;
    int count;
} GrayRegexSequence;

struct GrayRegexAlternation {
    GrayRegexSequence *sequences;
    int count;
    int first_group, last_group; /* capture numbers opened inside; empty when last < first */
};

typedef struct {
    GrayRegexAlternation *root;
    size_t group_count; /* number of capture groups */
} GrayRegexEngine;

/* --- Characters --- */

/* Decode the character at `position`, which must not be the terminating NUL;
 * returns its byte length. The text is NUL-terminated, so the decoder never
 * looks past the bytes that exist. */
static int gray_regex_decode(const char *position, int32_t *codepoint) {
    size_t available = 0;
    while (available < 4 && position[available]) available++;
    return gray_utf8_decode((const uint8_t *)position, (const uint8_t *)position + available, codepoint);
}

/* --- Parser --- */

typedef struct {
    const char *cursor;
    bool failed;
    int group_count; /* capture groups opened so far */
} GrayRegexParser;

static GrayRegexAlternation *gray_regex_parse_alternation(GrayRegexParser *parser);

static int32_t gray_regex_next_codepoint(GrayRegexParser *parser) {
    int32_t codepoint;
    parser->cursor += gray_regex_decode(parser->cursor, &codepoint);
    return codepoint;
}

static void gray_regex_class_add_range(GrayRegexParser *parser, GrayRegexNode *node, int32_t low, int32_t high) {
    for (int32_t codepoint = low; codepoint <= high && codepoint < 128; codepoint++)
        node->ascii_set[codepoint >> 3] |= (unsigned char)(1u << (codepoint & 7));
    if (high < 128) return;
    if (low < 128) low = 128;
    if (node->range_count == node->range_capacity) {
        int capacity = node->range_capacity ? node->range_capacity * 2 : 4;
        GrayRegexRange *grown = (GrayRegexRange *)realloc(node->ranges, (size_t)capacity * sizeof(GrayRegexRange));
        if (!grown) { parser->failed = true; return; }
        node->ranges = grown;
        node->range_capacity = capacity;
    }
    node->ranges[node->range_count].low = low;
    node->ranges[node->range_count].high = high;
    node->range_count++;
}

static bool gray_regex_class_has(const GrayRegexNode *node, int32_t codepoint) {
    if (codepoint < 128) return (node->ascii_set[codepoint >> 3] & (1u << (codepoint & 7))) != 0;
    for (int i = 0; i < node->range_count; i++) {
        if (codepoint >= node->ranges[i].low && codepoint <= node->ranges[i].high) return true;
    }
    return false;
}

/* Is the ASCII character in the POSIX class `name`? Every class is ASCII only,
 * whatever the locale. Returns false for an unknown name. */
static bool gray_regex_ascii_class_has(const char *name, int character) {
    bool is_upper = character >= 'A' && character <= 'Z';
    bool is_lower = character >= 'a' && character <= 'z';
    bool is_digit = character >= '0' && character <= '9';
    if (!strcmp(name, "alpha")) return is_upper || is_lower;
    if (!strcmp(name, "digit")) return is_digit;
    if (!strcmp(name, "alnum")) return is_upper || is_lower || is_digit;
    if (!strcmp(name, "upper")) return is_upper;
    if (!strcmp(name, "lower")) return is_lower;
    if (!strcmp(name, "space")) return character == ' ' || (character >= '\t' && character <= '\r');
    if (!strcmp(name, "blank")) return character == ' ' || character == '\t';
    if (!strcmp(name, "punct")) return character > ' ' && character < 127 && !(is_upper || is_lower || is_digit);
    if (!strcmp(name, "print")) return character >= ' ' && character < 127;
    if (!strcmp(name, "graph")) return character > ' ' && character < 127;
    if (!strcmp(name, "cntrl")) return character < ' ' || character == 127;
    if (!strcmp(name, "xdigit")) return is_digit || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
    return false;
}

/* Expand the escapes users reach for most often. POSIX ERE does not define
 * \d, \w or \s, but patterns in the wild assume them. */
static bool gray_regex_escape_class(GrayRegexParser *parser, char escape_character, GrayRegexNode *node) {
    const char *name = NULL;
    switch (escape_character) {
    case 'd': name = "digit"; break;
    case 'w': name = "alnum"; break;
    case 's': name = "space"; break;
    default: return false;
    }
    for (int character = 0; character < 128; character++) {
        if (gray_regex_ascii_class_has(name, character) || (escape_character == 'w' && character == '_'))
            gray_regex_class_add_range(parser, node, character, character);
    }
    return true;
}

static int32_t gray_regex_escape_character(int32_t escape_character) {
    switch (escape_character) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'v': return '\v';
    case '0': return '\0';
    default: return escape_character;
    }
}

/* One bracket item's codepoint: a character, or a backslash escape. */
static int32_t gray_regex_bracket_codepoint(GrayRegexParser *parser) {
    if (*parser->cursor == '\\' && parser->cursor[1]) {
        parser->cursor++;
        return gray_regex_escape_character(gray_regex_next_codepoint(parser));
    }
    return gray_regex_next_codepoint(parser);
}

static void gray_regex_parse_bracket(GrayRegexParser *parser, GrayRegexNode *node) {
    node->kind = GRAY_REGEX_CLASS;
    memset(node->ascii_set, 0, sizeof(node->ascii_set));
    node->is_negated = false;

    if (*parser->cursor == '^') {
        node->is_negated = true;
        parser->cursor++;
    }
    /* A ']' first is a literal, per POSIX. */
    bool first = true;

    while (*parser->cursor && (*parser->cursor != ']' || first)) {
        first = false;

        /* [:alpha:] and friends */
        if (parser->cursor[0] == '[' && parser->cursor[1] == ':') {
            const char *close = strstr(parser->cursor + 2, ":]");
            if (close) {
                size_t count = (size_t)(close - (parser->cursor + 2));
                char name[16] = {0};
                if (count < sizeof(name)) memcpy(name, parser->cursor + 2, count);
                for (int character = 0; character < 128; character++) {
                    if (gray_regex_ascii_class_has(name, character))
                        gray_regex_class_add_range(parser, node, character, character);
                }
                parser->cursor = close + 2;
                continue;
            }
        }

        if (*parser->cursor == '\\' && parser->cursor[1] && gray_regex_escape_class(parser, parser->cursor[1], node)) {
            parser->cursor += 2;
            continue;
        }
        int32_t range_low = gray_regex_bracket_codepoint(parser);

        /* range: a-z, but a trailing '-' before ']' is literal */
        if (*parser->cursor == '-' && parser->cursor[1] && parser->cursor[1] != ']') {
            parser->cursor++;
            int32_t range_high = gray_regex_bracket_codepoint(parser);
            gray_regex_class_add_range(parser, node, range_low, range_high);
        } else {
            gray_regex_class_add_range(parser, node, range_low, range_low);
        }
    }

    if (*parser->cursor == ']') parser->cursor++;
    else parser->failed = true;
}

/* Parse one atom plus any quantifier that follows it. */
static bool gray_regex_parse_atom(GrayRegexParser *parser, GrayRegexNode *node) {
    memset(node, 0, sizeof(*node));
    node->minimum_repeat = 1;
    node->maximum_repeat = 1;

    char character = *parser->cursor;
    if (character == '\0' || character == '|' || character == ')') return false;

    if (character == '(') {
        parser->cursor++;
        node->kind = GRAY_REGEX_GROUP;
        node->group_index = ++parser->group_count;
        node->group = gray_regex_parse_alternation(parser);
        if (*parser->cursor == ')') parser->cursor++;
        else parser->failed = true;
    } else if (character == '[') {
        parser->cursor++;
        gray_regex_parse_bracket(parser, node);
    } else if (character == '.') {
        parser->cursor++;
        node->kind = GRAY_REGEX_ANY;
    } else if (character == '^') {
        parser->cursor++;
        node->kind = GRAY_REGEX_BEGINNING_OF_LINE;
        return true; /* anchors take no quantifier */
    } else if (character == '$') {
        parser->cursor++;
        node->kind = GRAY_REGEX_END_OF_LINE;
        return true;
    } else if (character == '\\' && parser->cursor[1]) {
        parser->cursor++;
        node->kind = GRAY_REGEX_CLASS;
        node->is_negated = false;
        if (gray_regex_escape_class(parser, *parser->cursor, node)) {
            parser->cursor++;
        } else {
            free(node->ranges);
            memset(node, 0, sizeof(*node));
            node->minimum_repeat = 1;
            node->maximum_repeat = 1;
            node->kind = GRAY_REGEX_CHARACTER;
            node->character = gray_regex_escape_character(gray_regex_next_codepoint(parser));
        }
    } else {
        node->kind = GRAY_REGEX_CHARACTER;
        node->character = gray_regex_next_codepoint(parser);
    }

    switch (*parser->cursor) {
    case '*': parser->cursor++; node->minimum_repeat = 0; node->maximum_repeat = -1; break;
    case '+': parser->cursor++; node->minimum_repeat = 1; node->maximum_repeat = -1; break;
    case '?': parser->cursor++; node->minimum_repeat = 0; node->maximum_repeat = 1;  break;
    case '{': {
        const char *save = parser->cursor;
        parser->cursor++;
        if (!(*parser->cursor >= '0' && *parser->cursor <= '9')) { parser->cursor = save; break; }
        int repeat_low = 0;
        while (*parser->cursor >= '0' && *parser->cursor <= '9') repeat_low = repeat_low * 10 + (*parser->cursor++ - '0');
        int repeat_high = repeat_low;
        if (*parser->cursor == ',') {
            parser->cursor++;
            if (*parser->cursor >= '0' && *parser->cursor <= '9') {
                repeat_high = 0;
                while (*parser->cursor >= '0' && *parser->cursor <= '9') repeat_high = repeat_high * 10 + (*parser->cursor++ - '0');
            } else {
                repeat_high = -1;
            }
        }
        if (*parser->cursor == '}') { parser->cursor++; node->minimum_repeat = repeat_low; node->maximum_repeat = repeat_high; }
        else parser->cursor = save;
        break;
    }
    default: break;
    }
    return true;
}

static GrayRegexSequence gray_regex_parse_sequence(GrayRegexParser *parser) {
    GrayRegexSequence sequence = {NULL, 0};
    int capacity = 0;
    GrayRegexNode node;
    while (!parser->failed && gray_regex_parse_atom(parser, &node)) {
        if (sequence.count == capacity) {
            capacity = capacity ? capacity * 2 : 8;
            GrayRegexNode *grown = (GrayRegexNode *)realloc(sequence.nodes, (size_t)capacity * sizeof(GrayRegexNode));
            if (!grown) { free(node.ranges); parser->failed = true; break; }
            sequence.nodes = grown;
        }
        sequence.nodes[sequence.count++] = node;
    }
    return sequence;
}

static GrayRegexAlternation *gray_regex_parse_alternation(GrayRegexParser *parser) {
    GrayRegexAlternation *alternation = (GrayRegexAlternation *)calloc(1, sizeof(GrayRegexAlternation));
    if (!alternation) { parser->failed = true; return NULL; }
    alternation->first_group = parser->group_count + 1;
    int capacity = 0;
    for (;;) {
        GrayRegexSequence sequence = gray_regex_parse_sequence(parser);
        if (alternation->count == capacity) {
            capacity = capacity ? capacity * 2 : 4;
            GrayRegexSequence *grown = (GrayRegexSequence *)realloc(alternation->sequences, (size_t)capacity * sizeof(GrayRegexSequence));
            if (!grown) { parser->failed = true; return alternation; }
            alternation->sequences = grown;
        }
        alternation->sequences[alternation->count++] = sequence;
        if (*parser->cursor == '|') { parser->cursor++; continue; }
        break;
    }
    alternation->last_group = parser->group_count;
    return alternation;
}

static void gray_regex_free_alternation(GrayRegexAlternation *alternation);

static void gray_regex_free_sequence(GrayRegexSequence *sequence) {
    for (int i = 0; i < sequence->count; i++) {
        if (sequence->nodes[i].kind == GRAY_REGEX_GROUP) gray_regex_free_alternation(sequence->nodes[i].group);
        free(sequence->nodes[i].ranges);
    }
    free(sequence->nodes);
}

static void gray_regex_free_alternation(GrayRegexAlternation *alternation) {
    if (!alternation) return;
    for (int i = 0; i < alternation->count; i++) gray_regex_free_sequence(&alternation->sequences[i]);
    free(alternation->sequences);
    free(alternation);
}

/* --- Matcher --- */

typedef struct {
    const char *begin; /* start of subject, for '^' */
    bool is_not_beginning_of_line; /* '^' must not match at begin */
    GrayRegexSpan *captures; /* indexed by group number; NULL when nobody reads groups */
} GrayRegexContext;

/* Mark groups first..last as not participating. */
static void gray_regex_clear_captures(GrayRegexContext *context, int first, int last) {
    if (!context->captures) return;
    for (int group_index = first; group_index <= last; group_index++) {
        context->captures[group_index].rm_so = -1;
        context->captures[group_index].rm_eo = -1;
    }
}

static const char *gray_regex_match_alternation(GrayRegexContext *context, GrayRegexAlternation *alternation, const char *scan_position);
static const char *gray_regex_match_sequence(GrayRegexContext *context, GrayRegexSequence *sequence, int index, const char *scan_position);

/* Does one atom match at scan_position, ignoring its quantifier? Returns the position
 * after it, or NULL. */
static const char *gray_regex_match_one(GrayRegexContext *context, GrayRegexNode *node, const char *scan_position) {
    int32_t codepoint;
    switch (node->kind) {
    case GRAY_REGEX_BEGINNING_OF_LINE: return (scan_position == context->begin && !context->is_not_beginning_of_line) ? scan_position : NULL;
    case GRAY_REGEX_END_OF_LINE: return (*scan_position == '\0') ? scan_position : NULL;
    case GRAY_REGEX_ANY:
        if (!*scan_position) return NULL;
        return scan_position + gray_regex_decode(scan_position, &codepoint);
    case GRAY_REGEX_CHARACTER: {
        if (!*scan_position) return NULL;
        int length = gray_regex_decode(scan_position, &codepoint);
        return codepoint == node->character ? scan_position + length : NULL;
    }
    case GRAY_REGEX_CLASS: {
        if (!*scan_position) return NULL;
        int length = gray_regex_decode(scan_position, &codepoint);
        return gray_regex_class_has(node, codepoint) != node->is_negated ? scan_position + length : NULL;
    }
    case GRAY_REGEX_GROUP: return gray_regex_match_alternation(context, node->group, scan_position);
    }
    return NULL;
}

/* Match seq->nodes[idx..] at scan_position. Greedy, with backtracking on the quantifier. */
static const char *gray_regex_match_sequence(GrayRegexContext *context, GrayRegexSequence *sequence, int index, const char *scan_position) {
    if (index == sequence->count) return scan_position;

    GrayRegexNode *node = &sequence->nodes[index];

    /* Fixed single occurrence: the common case, kept allocation-free. */
    if (node->minimum_repeat == 1 && node->maximum_repeat == 1) {
        const char *next = gray_regex_match_one(context, node, scan_position);
        if (!next) return NULL;
        if (node->kind != GRAY_REGEX_GROUP || !context->captures) {
            return gray_regex_match_sequence(context, sequence, index + 1, next);
        }
        GrayRegexSpan previous = context->captures[node->group_index];
        context->captures[node->group_index].rm_so = (long)(scan_position - context->begin);
        context->captures[node->group_index].rm_eo = (long)(next - context->begin);
        const char *rest = gray_regex_match_sequence(context, sequence, index + 1, next);
        if (!rest) {
            gray_regex_clear_captures(context, node->group->first_group, node->group->last_group);
            context->captures[node->group_index] = previous;
        }
        return rest;
    }

    /* Record how far the atom can repeat, then give ground from the longest
     * run back to the minimum until the rest of the sequence fits. */
    enum { GRAY_REGEX_MAX_REPEAT = 8192 };
    const char *stack[GRAY_REGEX_MAX_REPEAT + 1];
    int depth = 0;
    stack[0] = scan_position;
    const char *cursor = scan_position;
    while ((node->maximum_repeat < 0 || depth < node->maximum_repeat) && depth < GRAY_REGEX_MAX_REPEAT) {
        const char *next = gray_regex_match_one(context, node, cursor);
        if (!next || next == cursor) break; /* no progress: stop, or '*' spins */
        cursor = next;
        stack[++depth] = cursor;
    }

    bool records_captures = node->kind == GRAY_REGEX_GROUP && context->captures;
    GrayRegexSpan previous = {-1, -1};
    if (records_captures) previous = context->captures[node->group_index];

    for (int take = depth; take >= node->minimum_repeat; take--) {
        if (records_captures) {
            /* The group reports its last iteration: rerun that iteration so
             * groups nested inside it match it, not a later or failed one. */
            gray_regex_clear_captures(context, node->group->first_group, node->group->last_group);
            if (take == 0) {
                context->captures[node->group_index] = previous;
            } else {
                gray_regex_match_one(context, node, stack[take - 1]);
                context->captures[node->group_index].rm_so = (long)(stack[take - 1] - context->begin);
                context->captures[node->group_index].rm_eo = (long)(stack[take] - context->begin);
            }
        }
        const char *rest = gray_regex_match_sequence(context, sequence, index + 1, stack[take]);
        if (rest) return rest;
    }
    if (records_captures) {
        gray_regex_clear_captures(context, node->group->first_group, node->group->last_group);
        context->captures[node->group_index] = previous;
    }
    return NULL;
}

static const char *gray_regex_match_alternation(GrayRegexContext *context, GrayRegexAlternation *alternation, const char *scan_position) {
    if (!alternation) return NULL;
    for (int i = 0; i < alternation->count; i++) {
        const char *end_cursor = gray_regex_match_sequence(context, &alternation->sequences[i], 0, scan_position);
        if (end_cursor) return end_cursor;
        gray_regex_clear_captures(context, alternation->first_group, alternation->last_group);
    }
    return NULL;
}

/* --- Entry points --- */

/* Compile `pattern`; returns 0, or non-zero for a malformed pattern. */
static int gray_regex_engine_compile(GrayRegexEngine *engine, const char *pattern) {
    GrayRegexParser parser;
    parser.cursor = pattern;
    parser.failed = false;
    parser.group_count = 0;
    engine->root = gray_regex_parse_alternation(&parser);
    engine->group_count = (size_t)parser.group_count;
    if (parser.failed || *parser.cursor != '\0') {
        gray_regex_free_alternation(engine->root);
        engine->root = NULL;
        return 1;
    }
    return 0;
}

/* Search `string` for a match. Returns 0 on a match, non-zero otherwise.
 * A match may only start at a character boundary. */
static int gray_regex_engine_execute(const GrayRegexEngine *engine, const char *string, size_t nmatch,
                                     GrayRegexSpan *spans, bool is_not_beginning_of_line) {
    if (!engine->root) return 1;

    GrayRegexContext context;
    context.begin = string;
    context.is_not_beginning_of_line = is_not_beginning_of_line;
    context.captures = NULL;
    if (nmatch > 1 && spans && engine->group_count > 0) {
        context.captures = (GrayRegexSpan *)malloc((engine->group_count + 1) * sizeof(GrayRegexSpan));
        if (!context.captures) return 1;
        gray_regex_clear_captures(&context, 1, (int)engine->group_count);
    }

    const char *start = string;
    for (;;) {
        const char *end_cursor = gray_regex_match_alternation(&context, engine->root, start);
        if (end_cursor) {
            if (nmatch > 0 && spans) {
                spans[0].rm_so = (long)(start - string);
                spans[0].rm_eo = (long)(end_cursor - string);
            }
            for (size_t group_index = 1; group_index < nmatch && spans; group_index++) {
                if (context.captures && group_index <= engine->group_count) {
                    spans[group_index] = context.captures[group_index];
                } else {
                    spans[group_index].rm_so = -1;
                    spans[group_index].rm_eo = -1;
                }
            }
            free(context.captures);
            return 0;
        }
        if (*start == '\0') break;
        int32_t codepoint;
        start += gray_regex_decode(start, &codepoint);
    }
    free(context.captures);
    return 1;
}

static void gray_regex_engine_release(GrayRegexEngine *engine) {
    gray_regex_free_alternation(engine->root);
    engine->root = NULL;
}

#endif
