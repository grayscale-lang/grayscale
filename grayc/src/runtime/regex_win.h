/*
 * regex_win.h — A POSIX extended-regular-expression engine for Windows.
 *
 * Windows ships no <regex.h> and there is no Win32 equivalent, so this
 * provides the subset of the POSIX interface the regex stdlib module uses:
 * regcomp, regexec, regfree, regex_t, regmatch_t, REG_EXTENDED, and REG_NOSUB.
 * Including it in place of <regex.h> leaves the module's own code unchanged.
 *
 * Supported: literals, '.', bracket expressions with ranges, negation and the
 * [:alpha:]-style classes, anchors, the '*', '+', '?' and '{n,m}' quantifiers,
 * alternation, grouping, and backslash escapes.
 *
 * Deliberately not supported: capture-group extraction. regexec reports the
 * extent of the whole match only, which is all the module asks for -- it never
 * passes an nmatch above 1. Matching is leftmost-first rather than POSIX's
 * leftmost-longest, the same rule Perl-style engines use.
 *
 * Author:  Aristomedes (@Aristomedes)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_REGEX_WIN_H
#define GRAY_REGEX_WIN_H

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define REG_EXTENDED 1
#define REG_NOSUB    2
#define REG_ICASE    4
#define REG_NOTBOL   8  /* regexec eflag: cursor[0] is not beginning-of-line */
#define REG_NOMATCH  1
#define REG_BADPAT   2

typedef struct {
    long rm_so;
    long rm_eo;
} regmatch_t;

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

struct GrayRegexNode {
    GrayRegexKind kind;
    char character;       /* GRAY_REGEX_CHARACTER */
    bool is_negated;      /* GRAY_REGEX_CLASS */
    unsigned char character_set[32]; /* GRAY_REGEX_CLASS bitmap, 256 bits */
    GrayRegexAlternation *group;        /* GRAY_REGEX_GROUP */
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
};

typedef struct {
    GrayRegexAlternation *root;
    bool has_no_submatches;
} regex_t;

/* --- Parser --- */

typedef struct {
    const char *cursor;
    bool failed;
} GrayRegexParser;

static GrayRegexAlternation *gray_regex_parse_alternation(GrayRegexParser *parser);

static void gray_regex_set_add(unsigned char *character_set, unsigned char character) {
    character_set[character >> 3] |= (unsigned char)(1u << (character & 7));
}

static bool gray_regex_set_has(const unsigned char *character_set, unsigned char character) {
    return (character_set[character >> 3] & (1u << (character & 7))) != 0;
}

/* Expand the escapes users reach for most often. POSIX ERE does not define
 * \d, \w or \s, but patterns in the wild assume them. */
static bool gray_regex_escape_class(char escape_character, unsigned char *character_set) {
    switch (escape_character) {
    case 'd':
        for (int character = '0'; character <= '9'; character++) gray_regex_set_add(character_set, (unsigned char)character);
        return true;
    case 'w':
        for (int character = 0; character < 256; character++) {
            if (isalnum(character) || character == '_') gray_regex_set_add(character_set, (unsigned char)character);
        }
        return true;
    case 's':
        for (int character = 0; character < 256; character++) {
            if (isspace(character)) gray_regex_set_add(character_set, (unsigned char)character);
        }
        return true;
    default:
        return false;
    }
}

static char gray_regex_escape_character(char escape_character) {
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

static void gray_regex_parse_bracket(GrayRegexParser *parser, GrayRegexNode *node) {
    node->kind = GRAY_REGEX_CLASS;
    memset(node->character_set, 0, sizeof(node->character_set));
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
                for (int character = 0; character < 256; character++) {
                    bool is_negated = false;
                    if (!strcmp(name, "alpha")) is_negated = isalpha(character);
                    else if (!strcmp(name, "digit")) is_negated = isdigit(character);
                    else if (!strcmp(name, "alnum")) is_negated = isalnum(character);
                    else if (!strcmp(name, "space")) is_negated = isspace(character);
                    else if (!strcmp(name, "upper")) is_negated = isupper(character);
                    else if (!strcmp(name, "lower")) is_negated = islower(character);
                    else if (!strcmp(name, "punct")) is_negated = ispunct(character);
                    else if (!strcmp(name, "xdigit")) is_negated = isxdigit(character);
                    if (is_negated) gray_regex_set_add(node->character_set, (unsigned char)character);
                }
                parser->cursor = close + 2;
                continue;
            }
        }

        char range_low;
        if (*parser->cursor == '\\' && parser->cursor[1]) {
            parser->cursor++;
            if (gray_regex_escape_class(*parser->cursor, node->character_set)) {
                parser->cursor++;
                continue;
            }
            range_low = gray_regex_escape_character(*parser->cursor++);
        } else {
            range_low = *parser->cursor++;
        }

        /* range: a-z, but a trailing '-' before ']' is literal */
        if (*parser->cursor == '-' && parser->cursor[1] && parser->cursor[1] != ']') {
            parser->cursor++;
            char range_high = (*parser->cursor == '\\' && parser->cursor[1]) ? (parser->cursor++, gray_regex_escape_character(*parser->cursor++)) : *parser->cursor++;
            for (int character = (unsigned char)range_low; character <= (unsigned char)range_high; character++) {
                gray_regex_set_add(node->character_set, (unsigned char)character);
            }
        } else {
            gray_regex_set_add(node->character_set, (unsigned char)range_low);
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
        unsigned char character_set[32] = {0};
        if (gray_regex_escape_class(*parser->cursor, character_set)) {
            node->kind = GRAY_REGEX_CLASS;
            node->is_negated = false;
            memcpy(node->character_set, character_set, sizeof(character_set));
            parser->cursor++;
        } else {
            node->kind = GRAY_REGEX_CHARACTER;
            node->character = gray_regex_escape_character(*parser->cursor++);
        }
    } else {
        node->kind = GRAY_REGEX_CHARACTER;
        node->character = *parser->cursor++;
    }

    switch (*parser->cursor) {
    case '*': parser->cursor++; node->minimum_repeat = 0; node->maximum_repeat = -1; break;
    case '+': parser->cursor++; node->minimum_repeat = 1; node->maximum_repeat = -1; break;
    case '?': parser->cursor++; node->minimum_repeat = 0; node->maximum_repeat = 1;  break;
    case '{': {
        const char *save = parser->cursor;
        parser->cursor++;
        if (!isdigit((unsigned char)*parser->cursor)) { parser->cursor = save; break; }
        int repeat_low = 0;
        while (isdigit((unsigned char)*parser->cursor)) repeat_low = repeat_low * 10 + (*parser->cursor++ - '0');
        int repeat_high = repeat_low;
        if (*parser->cursor == ',') {
            parser->cursor++;
            if (isdigit((unsigned char)*parser->cursor)) {
                repeat_high = 0;
                while (isdigit((unsigned char)*parser->cursor)) repeat_high = repeat_high * 10 + (*parser->cursor++ - '0');
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
            if (!grown) { parser->failed = true; break; }
            sequence.nodes = grown;
        }
        sequence.nodes[sequence.count++] = node;
    }
    return sequence;
}

static GrayRegexAlternation *gray_regex_parse_alternation(GrayRegexParser *parser) {
    GrayRegexAlternation *alternation = (GrayRegexAlternation *)calloc(1, sizeof(GrayRegexAlternation));
    if (!alternation) { parser->failed = true; return NULL; }
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
    return alternation;
}

static void gray_regex_free_alternation(GrayRegexAlternation *alternation);

static void gray_regex_free_sequence(GrayRegexSequence *sequence) {
    for (int i = 0; i < sequence->count; i++) {
        if (sequence->nodes[i].kind == GRAY_REGEX_GROUP) gray_regex_free_alternation(sequence->nodes[i].group);
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
    bool is_not_beginning_of_line; /* REG_NOTBOL: '^' must not match at begin */
} GrayRegexContext;

static const char *gray_regex_match_alternation(GrayRegexContext *context, GrayRegexAlternation *alternation, const char *scan_position);
static const char *gray_regex_match_sequence(GrayRegexContext *context, GrayRegexSequence *sequence, int index, const char *scan_position);

/* Does one atom match at scan_position, ignoring its quantifier? Returns the position
 * after it, or NULL. */
static const char *gray_regex_match_one(GrayRegexContext *context, GrayRegexNode *node, const char *scan_position) {
    switch (node->kind) {
    case GRAY_REGEX_BEGINNING_OF_LINE: return (scan_position == context->begin && !context->is_not_beginning_of_line) ? scan_position : NULL;
    case GRAY_REGEX_END_OF_LINE: return (*scan_position == '\0') ? scan_position : NULL;
    case GRAY_REGEX_ANY: return *scan_position ? scan_position + 1 : NULL;
    case GRAY_REGEX_CHARACTER: return (*scan_position == node->character && *scan_position) ? scan_position + 1 : NULL;
    case GRAY_REGEX_CLASS: {
        if (!*scan_position) return NULL;
        bool is_negated = gray_regex_set_has(node->character_set, (unsigned char)*scan_position);
        return (is_negated != node->is_negated) ? scan_position + 1 : NULL;
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
        return gray_regex_match_sequence(context, sequence, index + 1, next);
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

    for (int take = depth; take >= node->minimum_repeat; take--) {
        const char *rest = gray_regex_match_sequence(context, sequence, index + 1, stack[take]);
        if (rest) return rest;
    }
    return NULL;
}

static const char *gray_regex_match_alternation(GrayRegexContext *context, GrayRegexAlternation *alternation, const char *scan_position) {
    if (!alternation) return NULL;
    for (int i = 0; i < alternation->count; i++) {
        const char *end_cursor = gray_regex_match_sequence(context, &alternation->sequences[i], 0, scan_position);
        if (end_cursor) return end_cursor;
    }
    return NULL;
}

/* --- POSIX-shaped entry points --- */

static int regcomp(regex_t *regex, const char *pattern, int flags) {
    GrayRegexParser parser;
    parser.cursor = pattern;
    parser.failed = false;
    regex->root = gray_regex_parse_alternation(&parser);
    regex->has_no_submatches = (flags & REG_NOSUB) != 0;
    if (parser.failed || *parser.cursor != '\0') {
        gray_regex_free_alternation(regex->root);
        regex->root = NULL;
        return REG_BADPAT;
    }
    return 0;
}

static int regexec(const regex_t *regex, const char *string, size_t nmatch, regmatch_t *pmatch,
                   int eflags) {
    if (!regex->root) return REG_NOMATCH;

    GrayRegexContext context;
    context.begin = string;
    context.is_not_beginning_of_line = (eflags & REG_NOTBOL) != 0;

    for (const char *start = string;; start++) {
        const char *end_cursor = gray_regex_match_alternation(&context, regex->root, start);
        if (end_cursor) {
            if (nmatch > 0 && pmatch) {
                pmatch[0].rm_so = (long)(start - string);
                pmatch[0].rm_eo = (long)(end_cursor - string);
            }
            return 0;
        }
        if (*start == '\0') break;
    }
    return REG_NOMATCH;
}

static void regfree(regex_t *regex) {
    gray_regex_free_alternation(regex->root);
    regex->root = NULL;
}

#endif
