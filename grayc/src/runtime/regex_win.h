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
    GRX_CHAR,  /* one literal character */
    GRX_ANY,   /* .            */
    GRX_CLASS, /* [...]        */
    GRX_GROUP, /* (...)        */
    GRX_BOL,   /* ^            */
    GRX_EOL    /* $            */
} GrxKind;

typedef struct GrxNode GrxNode;
typedef struct GrxAlt GrxAlt;

struct GrxNode {
    GrxKind kind;
    char ch;              /* GRX_CHAR */
    bool negated;         /* GRX_CLASS */
    unsigned char set[32]; /* GRX_CLASS bitmap, 256 bits */
    GrxAlt *group;        /* GRX_GROUP */
    int min, max;         /* quantifier; max < 0 means unbounded */
};

/* One alternative is a sequence of nodes; a pattern is a list of alternatives. */
typedef struct {
    GrxNode *nodes;
    int count;
} GrxSeq;

struct GrxAlt {
    GrxSeq *seqs;
    int count;
};

typedef struct {
    GrxAlt *root;
    bool nosub;
} regex_t;

/* --- Parser --- */

typedef struct {
    const char *p;
    bool failed;
} GrxParser;

static GrxAlt *grx_parse_alt(GrxParser *ps);

static void grx_set_add(unsigned char *set, unsigned char c) {
    set[c >> 3] |= (unsigned char)(1u << (c & 7));
}

static bool grx_set_has(const unsigned char *set, unsigned char c) {
    return (set[c >> 3] & (1u << (c & 7))) != 0;
}

/* Expand the escapes users reach for most often. POSIX ERE does not define
 * \d, \w or \s, but patterns in the wild assume them. */
static bool grx_escape_class(char e, unsigned char *set) {
    switch (e) {
    case 'd':
        for (int c = '0'; c <= '9'; c++) grx_set_add(set, (unsigned char)c);
        return true;
    case 'w':
        for (int c = 0; c < 256; c++) {
            if (isalnum(c) || c == '_') grx_set_add(set, (unsigned char)c);
        }
        return true;
    case 's':
        for (int c = 0; c < 256; c++) {
            if (isspace(c)) grx_set_add(set, (unsigned char)c);
        }
        return true;
    default:
        return false;
    }
}

static char grx_escape_char(char e) {
    switch (e) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'v': return '\v';
    case '0': return '\0';
    default: return e;
    }
}

static void grx_parse_bracket(GrxParser *ps, GrxNode *node) {
    node->kind = GRX_CLASS;
    memset(node->set, 0, sizeof(node->set));
    node->negated = false;

    if (*ps->p == '^') {
        node->negated = true;
        ps->p++;
    }
    /* A ']' first is a literal, per POSIX. */
    bool first = true;

    while (*ps->p && (*ps->p != ']' || first)) {
        first = false;

        /* [:alpha:] and friends */
        if (ps->p[0] == '[' && ps->p[1] == ':') {
            const char *close = strstr(ps->p + 2, ":]");
            if (close) {
                size_t n = (size_t)(close - (ps->p + 2));
                char name[16] = {0};
                if (n < sizeof(name)) memcpy(name, ps->p + 2, n);
                for (int c = 0; c < 256; c++) {
                    bool in = false;
                    if (!strcmp(name, "alpha")) in = isalpha(c);
                    else if (!strcmp(name, "digit")) in = isdigit(c);
                    else if (!strcmp(name, "alnum")) in = isalnum(c);
                    else if (!strcmp(name, "space")) in = isspace(c);
                    else if (!strcmp(name, "upper")) in = isupper(c);
                    else if (!strcmp(name, "lower")) in = islower(c);
                    else if (!strcmp(name, "punct")) in = ispunct(c);
                    else if (!strcmp(name, "xdigit")) in = isxdigit(c);
                    if (in) grx_set_add(node->set, (unsigned char)c);
                }
                ps->p = close + 2;
                continue;
            }
        }

        char lo;
        if (*ps->p == '\\' && ps->p[1]) {
            ps->p++;
            if (grx_escape_class(*ps->p, node->set)) {
                ps->p++;
                continue;
            }
            lo = grx_escape_char(*ps->p++);
        } else {
            lo = *ps->p++;
        }

        /* range: a-z, but a trailing '-' before ']' is literal */
        if (*ps->p == '-' && ps->p[1] && ps->p[1] != ']') {
            ps->p++;
            char hi = (*ps->p == '\\' && ps->p[1]) ? (ps->p++, grx_escape_char(*ps->p++)) : *ps->p++;
            for (int c = (unsigned char)lo; c <= (unsigned char)hi; c++) {
                grx_set_add(node->set, (unsigned char)c);
            }
        } else {
            grx_set_add(node->set, (unsigned char)lo);
        }
    }

    if (*ps->p == ']') ps->p++;
    else ps->failed = true;
}

/* Parse one atom plus any quantifier that follows it. */
static bool grx_parse_atom(GrxParser *ps, GrxNode *node) {
    memset(node, 0, sizeof(*node));
    node->min = 1;
    node->max = 1;

    char c = *ps->p;
    if (c == '\0' || c == '|' || c == ')') return false;

    if (c == '(') {
        ps->p++;
        node->kind = GRX_GROUP;
        node->group = grx_parse_alt(ps);
        if (*ps->p == ')') ps->p++;
        else ps->failed = true;
    } else if (c == '[') {
        ps->p++;
        grx_parse_bracket(ps, node);
    } else if (c == '.') {
        ps->p++;
        node->kind = GRX_ANY;
    } else if (c == '^') {
        ps->p++;
        node->kind = GRX_BOL;
        return true; /* anchors take no quantifier */
    } else if (c == '$') {
        ps->p++;
        node->kind = GRX_EOL;
        return true;
    } else if (c == '\\' && ps->p[1]) {
        ps->p++;
        unsigned char set[32] = {0};
        if (grx_escape_class(*ps->p, set)) {
            node->kind = GRX_CLASS;
            node->negated = false;
            memcpy(node->set, set, sizeof(set));
            ps->p++;
        } else {
            node->kind = GRX_CHAR;
            node->ch = grx_escape_char(*ps->p++);
        }
    } else {
        node->kind = GRX_CHAR;
        node->ch = *ps->p++;
    }

    switch (*ps->p) {
    case '*': ps->p++; node->min = 0; node->max = -1; break;
    case '+': ps->p++; node->min = 1; node->max = -1; break;
    case '?': ps->p++; node->min = 0; node->max = 1;  break;
    case '{': {
        const char *save = ps->p;
        ps->p++;
        if (!isdigit((unsigned char)*ps->p)) { ps->p = save; break; }
        int lo = 0;
        while (isdigit((unsigned char)*ps->p)) lo = lo * 10 + (*ps->p++ - '0');
        int hi = lo;
        if (*ps->p == ',') {
            ps->p++;
            if (isdigit((unsigned char)*ps->p)) {
                hi = 0;
                while (isdigit((unsigned char)*ps->p)) hi = hi * 10 + (*ps->p++ - '0');
            } else {
                hi = -1;
            }
        }
        if (*ps->p == '}') { ps->p++; node->min = lo; node->max = hi; }
        else ps->p = save;
        break;
    }
    default: break;
    }
    return true;
}

static GrxSeq grx_parse_seq(GrxParser *ps) {
    GrxSeq seq = {NULL, 0};
    int cap = 0;
    GrxNode node;
    while (!ps->failed && grx_parse_atom(ps, &node)) {
        if (seq.count == cap) {
            cap = cap ? cap * 2 : 8;
            GrxNode *grown = (GrxNode *)realloc(seq.nodes, (size_t)cap * sizeof(GrxNode));
            if (!grown) { ps->failed = true; break; }
            seq.nodes = grown;
        }
        seq.nodes[seq.count++] = node;
    }
    return seq;
}

static GrxAlt *grx_parse_alt(GrxParser *ps) {
    GrxAlt *alt = (GrxAlt *)calloc(1, sizeof(GrxAlt));
    if (!alt) { ps->failed = true; return NULL; }
    int cap = 0;
    for (;;) {
        GrxSeq seq = grx_parse_seq(ps);
        if (alt->count == cap) {
            cap = cap ? cap * 2 : 4;
            GrxSeq *grown = (GrxSeq *)realloc(alt->seqs, (size_t)cap * sizeof(GrxSeq));
            if (!grown) { ps->failed = true; return alt; }
            alt->seqs = grown;
        }
        alt->seqs[alt->count++] = seq;
        if (*ps->p == '|') { ps->p++; continue; }
        break;
    }
    return alt;
}

static void grx_free_alt(GrxAlt *alt);

static void grx_free_seq(GrxSeq *seq) {
    for (int i = 0; i < seq->count; i++) {
        if (seq->nodes[i].kind == GRX_GROUP) grx_free_alt(seq->nodes[i].group);
    }
    free(seq->nodes);
}

static void grx_free_alt(GrxAlt *alt) {
    if (!alt) return;
    for (int i = 0; i < alt->count; i++) grx_free_seq(&alt->seqs[i]);
    free(alt->seqs);
    free(alt);
}

/* --- Matcher --- */

typedef struct {
    const char *begin; /* start of subject, for '^' */
} GrxCtx;

static const char *grx_match_alt(GrxCtx *ctx, GrxAlt *alt, const char *s);
static const char *grx_match_seq(GrxCtx *ctx, GrxSeq *seq, int idx, const char *s);

/* Does one atom match at s, ignoring its quantifier? Returns the position
 * after it, or NULL. */
static const char *grx_match_one(GrxCtx *ctx, GrxNode *n, const char *s) {
    switch (n->kind) {
    case GRX_BOL: return (s == ctx->begin) ? s : NULL;
    case GRX_EOL: return (*s == '\0') ? s : NULL;
    case GRX_ANY: return *s ? s + 1 : NULL;
    case GRX_CHAR: return (*s == n->ch && *s) ? s + 1 : NULL;
    case GRX_CLASS: {
        if (!*s) return NULL;
        bool in = grx_set_has(n->set, (unsigned char)*s);
        return (in != n->negated) ? s + 1 : NULL;
    }
    case GRX_GROUP: return grx_match_alt(ctx, n->group, s);
    }
    return NULL;
}

/* Match seq->nodes[idx..] at s. Greedy, with backtracking on the quantifier. */
static const char *grx_match_seq(GrxCtx *ctx, GrxSeq *seq, int idx, const char *s) {
    if (idx == seq->count) return s;

    GrxNode *n = &seq->nodes[idx];

    /* Fixed single occurrence: the common case, kept allocation-free. */
    if (n->min == 1 && n->max == 1) {
        const char *next = grx_match_one(ctx, n, s);
        if (!next) return NULL;
        return grx_match_seq(ctx, seq, idx + 1, next);
    }

    /* Record how far the atom can repeat, then give ground from the longest
     * run back to the minimum until the rest of the sequence fits. */
    enum { GRX_MAX_REPEAT = 8192 };
    const char *stack[GRX_MAX_REPEAT + 1];
    int depth = 0;
    stack[0] = s;
    const char *cur = s;
    while ((n->max < 0 || depth < n->max) && depth < GRX_MAX_REPEAT) {
        const char *next = grx_match_one(ctx, n, cur);
        if (!next || next == cur) break; /* no progress: stop, or '*' spins */
        cur = next;
        stack[++depth] = cur;
    }

    for (int take = depth; take >= n->min; take--) {
        const char *rest = grx_match_seq(ctx, seq, idx + 1, stack[take]);
        if (rest) return rest;
    }
    return NULL;
}

static const char *grx_match_alt(GrxCtx *ctx, GrxAlt *alt, const char *s) {
    if (!alt) return NULL;
    for (int i = 0; i < alt->count; i++) {
        const char *end = grx_match_seq(ctx, &alt->seqs[i], 0, s);
        if (end) return end;
    }
    return NULL;
}

/* --- POSIX-shaped entry points --- */

static int regcomp(regex_t *re, const char *pattern, int flags) {
    GrxParser ps;
    ps.p = pattern;
    ps.failed = false;
    re->root = grx_parse_alt(&ps);
    re->nosub = (flags & REG_NOSUB) != 0;
    if (ps.failed || *ps.p != '\0') {
        grx_free_alt(re->root);
        re->root = NULL;
        return REG_BADPAT;
    }
    return 0;
}

static int regexec(const regex_t *re, const char *string, size_t nmatch, regmatch_t *pmatch,
                   int eflags) {
    (void)eflags;
    if (!re->root) return REG_NOMATCH;

    GrxCtx ctx;
    ctx.begin = string;

    for (const char *start = string;; start++) {
        const char *end = grx_match_alt(&ctx, re->root, start);
        if (end) {
            if (nmatch > 0 && pmatch) {
                pmatch[0].rm_so = (long)(start - string);
                pmatch[0].rm_eo = (long)(end - string);
            }
            return 0;
        }
        if (*start == '\0') break;
    }
    return REG_NOMATCH;
}

static void regfree(regex_t *re) {
    grx_free_alt(re->root);
    re->root = NULL;
}

#endif
