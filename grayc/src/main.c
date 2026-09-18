/*
 * main.c — Grayscale compiler entry point. Orchestrates the full compilation
 * pipeline: source reading, lexing, parsing, type checking, C code generation,
 * and invoking the system C compiler to produce the final binary.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 *
 * Contributors:
 *  - @SAY-5
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>

#include "util/arena.h"
#include "util/colors.h"
#include "util/error.h"
#include "util/platform.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "parser/imports.h"
#include "typechecker/typechecker.h"
#include "codegen/codegen.h"
#include "fmt/fmt.h"

#ifndef GRAY_VERSION
#define GRAY_VERSION "unknown"
#endif
#define PATH_BUF_SIZE 2048
#define COMPILER_ARENA_SIZE (1024 * 1024)
#define GRAY_EXT      ".gray"
#define GRAY_EXT_LEN  5

/* Wall-clock milliseconds from a monotonic source. clock() would measure only
 * this process's CPU time and miss the C compiler, which runs as a spawned
 * child and accounts for most of the total. */
static double monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void print_usage(void) {
    fprintf(stderr, "Grayscale v%s — Simple to write. Safe to run.\n", GRAY_VERSION);
    fprintf(stderr, "\nUsage:\n");
    fprintf(stderr, "  gray <file.gray> [options]         Compile and run\n");
    fprintf(stderr, "  gray build <file.gray> [options]   Compile to binary\n");
    fprintf(stderr, "  gray check <file.gray>             Type check only\n");
    fprintf(stderr, "  gray version                       Show version\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o <file>       Output binary name (default: based on input filename)\n");
    fprintf(stderr, "  -c              Emit C source only (don't compile)\n");
    fprintf(stderr, "  -O0, -O1, -O2   Optimization level (default: -O2)\n");
    fprintf(stderr, "  -g              Include debug symbols\n");
    fprintf(stderr, "  -v, --verbose   Show compilation commands\n");
    fprintf(stderr, "  --time          Show compilation timing\n");
    fprintf(stderr, "  --quiet         Suppress all warnings\n");
    fprintf(stderr, "  --quiet W1001   Suppress specific warnings (comma-separated)\n");
    fprintf(stderr, "  --arena-limit=<size>  Max arena memory (e.g. 256MB, 1GB; default: 1GB)\n");
    fprintf(stderr, "  --no-color      Disable colored output\n");
    fprintf(stderr, "  --test          Build a test runner from #test functions (used by 'gray test')\n");
    fprintf(stderr, "  -h, --help      Show this help\n");
}


/* Write text content to a file. Binary mode: the generated C must be
 * byte-identical on every platform, so no newline translation. */
static bool write_file(const char *path, const char *content) {
    if (!gray_write_file_mode(path, content, strlen(content))) {
        fprintf(stderr, "gray: cannot write '%s': ", path);
        perror("");
        return false;
    }
    return true;
}

/* Strip the .gray extension from the base name and append the platform's
 * executable suffix, so `gray build foo.gray` yields foo.exe on Windows. */
static char *output_name_from_input(const char *input) {
    const char *base = gray_path_basename(input);

    size_t len = strlen(base);
    if (len > GRAY_EXT_LEN && strcmp(base + len - GRAY_EXT_LEN, GRAY_EXT) == 0) {
        len -= GRAY_EXT_LEN;
    }

    size_t suffix_len = strlen(GRAY_EXE_SUFFIX);
    char *out = malloc(len + suffix_len + 1);
    memcpy(out, base, len);
    memcpy(out + len, GRAY_EXE_SUFFIX, suffix_len + 1);
    return out;
}

/*
 * Find the runtime directory containing runtime.h and std.h.
 *
 * Search order:
 *   1. GRAY_RUNTIME env var (explicit override)
 *   2. Relative to binary: ../lib/grayc (installed layout)
 *   3. Relative to binary: src (development layout — binary is in grayc/)
 *   4. Relative to CWD: grayc/src (running from project root)
 *   5. /usr/local/lib/grayc (system install)
 */
static const char *find_runtime_dir(const char *argv0) {
    static char path[PATH_BUF_SIZE];

    /* 1. Environment variable override */
    const char *env = getenv("GRAY_RUNTIME");
    if (env && gray_file_readable(env)) {
        gray_path_join(path, sizeof(path), env, "runtime/runtime.h");
        if (gray_file_readable(path)) return env;
    }

    /* 2-3. Relative to binary location */
    const char *self_dir = gray_self_dir(argv0);
    if (self_dir) {
        /* Installed layout: binary in /usr/local/bin, runtime in /usr/local/lib/grayc */
        gray_path_join(path, sizeof(path), self_dir, "../lib/grayc/runtime/runtime.h");
        if (gray_file_readable(path)) {
            gray_path_join(path, sizeof(path), self_dir, "../lib/grayc");
            return path;
        }

        /* Development layout: binary in grayc/, runtime in grayc/src/runtime */
        gray_path_join(path, sizeof(path), self_dir, "src/runtime/runtime.h");
        if (gray_file_readable(path)) {
            gray_path_join(path, sizeof(path), self_dir, "src");
            return path;
        }
    }

    /* 4. Walk up from CWD looking for the project root */
    {
        char cwd[PATH_BUF_SIZE];
        if (gray_getcwd(cwd, sizeof(cwd))) {
            char probe[PATH_BUF_SIZE];
            char *dir = cwd;
            while (*dir) {
                gray_path_join(probe, sizeof(probe), dir, "grayc/src/runtime/runtime.h");
                if (gray_file_readable(probe)) {
                    gray_path_join(path, sizeof(path), dir, "grayc/src");
                    return path;
                }
                /* Move to parent. Stop at a filesystem root — on Windows that
                 * is a drive or UNC share, which has no separator to strip and
                 * would otherwise loop forever. */
                if (gray_path_is_root(dir)) break;
                char *sep = gray_path_rsep(dir);
                if (!sep || sep == dir) break;
                *sep = '\0';
            }
        }
    }

    /* 5. System install location */
    if (gray_file_readable("/usr/local/lib/grayc/runtime/runtime.h")) {
        return "/usr/local/lib/grayc";
    }

    return NULL;
}


/* --- C compiler invocation ---
 *
 * The compile command is handed to the C compiler as an argv array rather than
 * a shell string. No shell means no quoting rules to get wrong (paths with
 * spaces just work), no command-line length ceiling, and no way for a path to
 * be reinterpreted as shell syntax. */
#define MAX_CC_ARGS 128

typedef struct {
    const char *v[MAX_CC_ARGS];
    int n;
    bool overflow;
} ArgV;

static void argv_push(ArgV *a, const char *s) {
    if (a->n >= MAX_CC_ARGS - 1) {
        a->overflow = true;
        return;
    }
    a->v[a->n++] = s;
}

/* Push a formatted argument, copied into the arena so it outlives this call. */
static void argv_pushf(ArgV *a, Arena *arena, const char *fmt, ...) {
    char buf[PATH_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    argv_push(a, arena_copy_string(arena, buf));
}

/* Split a compiler command into words, the way the shell used to when this
 * was interpolated into a system() string. `--cc "zig cc -target x86_64-linux-gnu"`
 * has to arrive as four separate arguments. */
static void argv_push_command(ArgV *a, Arena *arena, const char *cmd) {
    for (const char *p = cmd; *p;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        argv_pushf(a, arena, "%.*s", (int)(p - start), start);
    }
}

static void argv_end(ArgV *a) {
    a->v[a->n] = NULL;
}

static void argv_print(const ArgV *a, FILE *out) {
    for (int i = 0; i < a->n; i++) fprintf(out, "%s%s", i ? " " : "", a->v[i]);
    fputc('\n', out);
}

/* Pick the first C compiler present on PATH. The candidate that resolves is
 * the one we go on to invoke — accepting one name and then invoking a different
 * one breaks on any system that has gcc but no cc, which is every Windows
 * install and plenty of minimal Linux images. A filesystem check rather than a
 * `<cc> --version` spawn: the spawn cost ~11ms of C-driver startup on every
 * compile and only additionally proved the binary is not broken, which the
 * real compile reports anyway. */
static bool cc_available(const char *cc) {
    return gray_command_on_path(cc);
}

static const char *detect_cc(void) {
    /* GRAY_CC / CC are checked, not trusted: a stale CC=cc from a profile must
     * not break a system that only has gcc. Multi-word values ("zig cc")
     * cannot go through a single-token lookup — use --cc for those. */
    static const char *const env_names[] = {"GRAY_CC", "CC"};
    for (size_t i = 0; i < sizeof(env_names) / sizeof(env_names[0]); i++) {
        const char *val = getenv(env_names[i]);
        if (!val || !*val || strpbrk(val, " \t")) continue;
        if (cc_available(val)) return val;
    }

    static const char *const candidates[] = {
#if GRAY_OS_WINDOWS
        "gcc", "clang", "cc",
#else
        "cc", "gcc", "clang",
#endif
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (cc_available(candidates[i])) return candidates[i];
    }

    /* Nothing on PATH — check the well-known Windows install locations. */
    return gray_find_cc_fallback();
}

/* A C header path written as a local include: "./x.h" or "../x.h". */
static bool c_header_is_local(const char *path) {
    return path[0] == '.' && (path[1] == '/' ||
           (path[1] == '.' && path[2] == '/'));
}

/* Directory to resolve a local C header import against: item->source_dir,
 * or (for an import written directly in the entry file, where source_dir is
 * NULL) the entry file's own directory. Shared by preflight_c_headers,
 * add_local_c_header_dirs, and append_c_header_includes so all three treat
 * "./x.h" the same way. */
static const char *local_c_header_dir(const ImportItem *item, const char *entry_file,
                                      char *buf, size_t buf_size) {
    if (item->source_dir) return item->source_dir;
    snprintf(buf, buf_size, "%s", entry_file);
    char *sep = gray_path_rsep(buf);
    if (sep) sep[1] = '\0';
    else snprintf(buf, buf_size, "./");
    return buf;
}

/* item->path resolved against its importing file's directory — "./x.h" in
 * sub/mod.gray becomes "sub/./x.h". Used both to check/emit a local header
 * and, critically, as the *dedup key* for one: two different directories
 * each importing their own "./bindings.h" must not collapse into a single
 * check/emission just because the raw spelling is identical (#2729) — the
 * resolved path differs even though the written text doesn't. */
static void resolve_local_c_header_path(const ImportItem *item, const char *entry_file,
                                        char *out, size_t out_size) {
    char dir_buf[PATH_BUF_SIZE];
    const char *dir = local_c_header_dir(item, entry_file, dir_buf, sizeof(dir_buf));
    snprintf(out, out_size, "%s%s", dir, item->path);
}

/* Preflight every distinct C header named by an `extern import` before the
 * real compile. A header that is missing, misspelled, or exists only on
 * another platform otherwise surfaces as the C compiler's own "file not
 * found" against a temp .c path the user never wrote. Reports a diagnostic
 * anchored at the import instead. `cc_cmd` / `cc_is_command` are what the real
 * build will invoke, so a cross-compile target's headers are what is checked.
 * Returns false when at least one header could not be resolved. */
static bool preflight_c_headers(AstNode *program, DiagnosticList *diag, Arena *arena,
                                const char *cc_cmd, bool cc_is_command,
                                const char *entry_file) {
    const char *seen[MAX_CC_ARGS];
    int seen_count = 0;
    bool ok = true;

    for (int si = 0; si < program->data.program.stmt_count; si++) {
        AstNode *stmt = program->data.program.stmts[si];
        if (stmt->kind != NODE_IMPORT_STMT) continue;
        for (int ii = 0; ii < stmt->data.import_stmt.count; ii++) {
            ImportItem *item = &stmt->data.import_stmt.items[ii];
            if (!item->is_c_import || !item->path) continue;

            /* A local header's dedup key must be its resolved path, not the
             * raw spelling: two different directories each importing their
             * own "./bindings.h" are two different files that both need
             * checking, even though the text is identical (#2729). A
             * system header has no directory to resolve against, so the
             * raw name is already the right key. */
            char resolved[PATH_BUF_SIZE];
            bool is_local = c_header_is_local(item->path);
            if (is_local) resolve_local_c_header_path(item, entry_file, resolved, sizeof(resolved));
            const char *key = is_local ? resolved : item->path;

            bool dup = false;
            for (int k = 0; k < seen_count; k++)
                if (strcmp(seen[k], key) == 0) { dup = true; break; }
            if (dup) continue;
            if (seen_count < MAX_CC_ARGS)
                seen[seen_count++] = arena_copy_string(arena, key);

            bool found;
            if (is_local) {
                found = gray_file_readable(resolved);
            } else {
                /* Angle-bracket header: ask the target compiler whether it
                 * can find it. -fsyntax-only stops before codegen. */
                char stub[PATH_BUF_SIZE];
                int sn = gray_temp_path(stub, sizeof(stub), "gray_hdrcheck_", ".c");
                if (sn < 0 || (size_t)sn >= sizeof(stub))
                    continue; /* cannot check — let the real compile report it */
                char body[PATH_BUF_SIZE];
                snprintf(body, sizeof(body),
                    "#include <%s>\nint main(void){return 0;}\n", item->path);
                if (!write_file(stub, body)) { gray_remove_file(stub); continue; }

                ArgV a = {0};
                if (cc_is_command) argv_push_command(&a, arena, cc_cmd);
                else argv_push(&a, cc_cmd);
                argv_push(&a, "-fsyntax-only");
                argv_push(&a, "-x");
                argv_push(&a, "c");
                argv_push(&a, stub);
                argv_end(&a);
                found = !a.overflow && gray_spawn_quiet(a.v) == 0;
                gray_remove_file(stub);
            }

            if (!found) {
                ok = false;
                char help[512];
                snprintf(help, sizeof(help),
                    "'%s' is not available for this target, or the library that "
                    "provides it is not installed. Grayscale has no conditional "
                    "import: only import C headers that are available on every "
                    "target you build for.", item->path);
                diagnostic_error_code_formatted_help(diag, "E6015",
                    item->token.file ? item->token.file : entry_file,
                    item->token.line, item->token.column, 0,
                    arena_copy_string(arena, help), item->path);
            }
        }
    }
    return ok;
}

/* Put the directory of every file that names a local C header ("./x.h" /
 * "../x.h") on the quoted-include search path. The generated C is written to a
 * temp path, so a verbatim `#include "./x.h"` would otherwise be resolved
 * relative to $TMPDIR and never found. -iquote (not -I) keeps this confined to
 * the quoted-include form, matching how the header was written. */
static void add_local_c_header_dirs(ArgV *cc_argv, Arena *arena, AstNode *program,
                                    const char *entry_file) {
    const char *seen[MAX_CC_ARGS];
    int seen_count = 0;

    for (int si = 0; si < program->data.program.stmt_count; si++) {
        AstNode *stmt = program->data.program.stmts[si];
        if (stmt->kind != NODE_IMPORT_STMT) continue;
        for (int ii = 0; ii < stmt->data.import_stmt.count; ii++) {
            ImportItem *item = &stmt->data.import_stmt.items[ii];
            if (!item->is_c_import || !item->path) continue;
            if (!c_header_is_local(item->path)) continue;

            /* Directory of the importing file (mirrors preflight_c_headers). */
            char base[PATH_BUF_SIZE];
            const char *dir = item->source_dir;
            if (!dir) {
                snprintf(base, sizeof(base), "%s", entry_file);
                char *sep = gray_path_rsep(base);
                if (sep) sep[1] = '\0';
                else snprintf(base, sizeof(base), "./");
                dir = base;
            }

            bool dup = false;
            for (int k = 0; k < seen_count; k++)
                if (strcmp(seen[k], dir) == 0) { dup = true; break; }
            if (dup) continue;
            const char *kept = arena_copy_string(arena, dir);
            if (seen_count < MAX_CC_ARGS) seen[seen_count++] = kept;

            argv_push(cc_argv, "-iquote");
            argv_push(cc_argv, kept);
        }
    }
}

/* Append `#include <path>` (angle-bracket header) or `#include "resolved"`
 * (a "./x.h" / "../x.h" local header, resolved against the importing file's
 * directory) for every distinct header named by an `extern import`, mirroring
 * exactly what the real generated .c file includes. Used to build a stub
 * translation unit for probing real C function signatures. */
static void append_c_header_includes(AstNode *program, const char *entry_file,
                                     char *out, size_t out_size) {
    /* Dedup key per seen item: its resolved path for a local header, its raw
     * name for a system one. Compared on demand rather than stored, so this
     * needs no scratch buffer beyond the one line/resolved pair in flight —
     * see the dedup-key comment in preflight_c_headers for why a local
     * header can't be deduped by its raw "./x.h" spelling (#2729). */
    const ImportItem *seen[MAX_CC_ARGS];
    int seen_count = 0;
    size_t used = 0;
    out[0] = '\0';

    for (int si = 0; si < program->data.program.stmt_count; si++) {
        AstNode *stmt = program->data.program.stmts[si];
        if (stmt->kind != NODE_IMPORT_STMT) continue;
        for (int ii = 0; ii < stmt->data.import_stmt.count; ii++) {
            ImportItem *item = &stmt->data.import_stmt.items[ii];
            if (!item->is_c_import || !item->path) continue;

            bool is_local = c_header_is_local(item->path);
            char resolved[PATH_BUF_SIZE];
            if (is_local) resolve_local_c_header_path(item, entry_file, resolved, sizeof(resolved));

            bool dup = false;
            for (int k = 0; k < seen_count; k++) {
                const ImportItem *s = seen[k];
                bool s_local = c_header_is_local(s->path);
                if (s_local != is_local) continue;
                if (is_local) {
                    char s_resolved[PATH_BUF_SIZE];
                    resolve_local_c_header_path(s, entry_file, s_resolved, sizeof(s_resolved));
                    if (strcmp(s_resolved, resolved) == 0) { dup = true; break; }
                } else if (strcmp(s->path, item->path) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            if (seen_count < MAX_CC_ARGS) seen[seen_count++] = item;

            char line[PATH_BUF_SIZE];
            if (is_local) {
                snprintf(line, sizeof(line), "#include \"%s\"\n", resolved);
            } else {
                snprintf(line, sizeof(line), "#include <%s>\n", item->path);
            }
            size_t line_len = strlen(line);
            if (used + line_len < out_size) {
                memcpy(out + used, line, line_len);
                used += line_len;
                out[used] = '\0';
            }
        }
    }
}

/* True if `needle` occurs anywhere in [start, end). `start`/`end` need not be
 * NUL-terminated at `end` — used to search one line of a larger buffer. */
static bool range_contains(const char *start, const char *end, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    for (const char *p = start; p + nlen <= end; p++) {
        if (memcmp(p, needle, nlen) == 0) return true;
    }
    return false;
}

/* Parses a clang `-ast-dump` FunctionDecl type spelling, e.g.
 * "int (int, FILE *)" or "int (const char *, ...)", into a required
 * parameter count and a variadic flag. Counts only top-level commas — a
 * function-pointer parameter's own comma-separated parameter list (nested in
 * its own parens) does not split the outer list. Returns false when `sig`
 * does not have the expected "(...)" shape, so the caller skips validation
 * instead of guessing. */
static bool count_c_params(const char *sig, int *min_params, bool *is_variadic) {
    *min_params = 0;
    *is_variadic = false;

    size_t len = strlen(sig);
    if (len == 0 || sig[len - 1] != ')') return false;

    /* Collect every *top-level* '(' ... ')' group (depth 0 -> 1 -> 0). An
     * ordinary function's type spells as "RT (PARAMS)" — exactly one such
     * group, its own parameter list, no matter what the return type spells
     * inside it. A function whose *return type* is itself a function
     * pointer spells as "RT (*[quals](PARAMS))(INNER)" — e.g. signal's
     * "void (*(int, void (*)(int)))(int)" — two top-level groups, where the
     * first (one level inside the '*' wrapper) is the function's own
     * parameter list and the second, trailing one belongs to the returned
     * function pointer instead. */
    long first_open = -1, first_close = -1, last_open = -1, last_close = -1;
    int group_count = 0;
    int depth = 0;
    for (size_t i = 0; i < len; i++) {
        if (sig[i] == '(') {
            if (depth == 0) {
                last_open = (long)i;
                if (group_count == 0) first_open = (long)i;
            }
            depth++;
        } else if (sig[i] == ')') {
            depth--;
            if (depth == 0) {
                last_close = (long)i;
                if (group_count == 0) first_close = (long)i;
                group_count++;
            }
        }
    }
    if (group_count == 0 || depth != 0) return false;

    const char *params = NULL;
    size_t params_len = 0;

    if (group_count == 2 && last_close == (long)len - 1) {
        const char *gc = sig + first_open + 1;
        size_t gc_len = (size_t)(first_close - first_open - 1);
        size_t i = 0;
        while (i < gc_len && gc[i] == ' ') i++;
        if (i < gc_len && gc[i] == '*') {
            i++;
            while (i < gc_len && gc[i] != '(' && gc[i] != '*') i++;
            if (i < gc_len && gc[i] == '(') {
                size_t inner_open = i;
                int d2 = 0;
                long inner_close = -1;
                for (size_t j = inner_open; j < gc_len; j++) {
                    if (gc[j] == '(') d2++;
                    else if (gc[j] == ')') { d2--; if (d2 == 0) { inner_close = (long)j; break; } }
                }
                if (inner_close >= 0) {
                    params = gc + inner_open + 1;
                    params_len = (size_t)inner_close - inner_open - 1;
                }
            }
        }
    }

    if (!params) {
        /* Ordinary shape: the sole (or, failing the pointer-return check
         * above, the final) top-level group is the parameter list. */
        params = sig + last_open + 1;
        params_len = len - 1 - (size_t)(last_open + 1);
    }

    while (params_len > 0 && params[0] == ' ') { params++; params_len--; }
    while (params_len > 0 && params[params_len - 1] == ' ') params_len--;

    if (params_len == 0 || (params_len == 4 && memcmp(params, "void", 4) == 0)) {
        return true; /* explicitly zero parameters */
    }

    int nest = 0;
    size_t seg_start = 0;
    int count = 0;
    bool variadic = false;
    for (size_t i = 0; i <= params_len; i++) {
        bool at_end = (i == params_len);
        char c = at_end ? ',' : params[i];
        if (!at_end && (c == '(' || c == '[')) { nest++; continue; }
        if (!at_end && (c == ')' || c == ']')) { nest--; continue; }
        if (c == ',' && nest == 0) {
            const char *seg = params + seg_start;
            size_t seg_len = i - seg_start;
            while (seg_len > 0 && seg[0] == ' ') { seg++; seg_len--; }
            while (seg_len > 0 && seg[seg_len - 1] == ' ') seg_len--;
            if (seg_len == 3 && memcmp(seg, "...", 3) == 0) variadic = true;
            else if (seg_len > 0) count++;
            seg_start = i + 1;
        }
    }

    *min_params = count;
    *is_variadic = variadic;
    return true;
}

/* Looks up `name`'s signature in a clang `-Xclang -ast-dump` text dump.
 * Matches whole-word "FunctionDecl ... name '<type>'" lines; when a function
 * has multiple declarations (an implicit builtin plus the header's real
 * prototype, or several redeclarations), the last match wins since clang
 * lists the most complete declaration last. Returns false when the dump has
 * no FunctionDecl for `name` at all (a macro, or a dump this parser cannot
 * make sense of) — the caller then skips validation for that call. */
static bool find_c_function_signature(const char *dump, const char *name,
                                      int *min_params, bool *is_variadic) {
    size_t name_len = strlen(name);
    const char *sig_start = NULL;
    const char *sig_end = NULL;

    const char *p = dump;
    const char *match;
    while ((match = strstr(p, name)) != NULL) {
        p = match + name_len;

        bool left_ok = (match == dump) ||
            !(isalnum((unsigned char)match[-1]) || match[-1] == '_');
        bool right_ok = !(isalnum((unsigned char)*p) || *p == '_');
        if (!left_ok || !right_ok) continue;

        const char *after = p;
        if (*after != ' ') continue;
        after++;
        if (*after != '\'') continue;

        const char *end_quote = strchr(after + 1, '\'');
        if (!end_quote) continue;

        const char *line_start = match;
        while (line_start > dump && line_start[-1] != '\n') line_start--;
        if (!range_contains(line_start, match, "FunctionDecl")) continue;

        sig_start = after + 1; /* last match wins */
        sig_end = end_quote;
    }
    if (!sig_start) return false;

    char sig[512];
    size_t sig_len = (size_t)(sig_end - sig_start);
    if (sig_len >= sizeof(sig)) return false; /* implausibly long; skip rather than guess */
    memcpy(sig, sig_start, sig_len);
    sig[sig_len] = '\0';

    return count_c_params(sig, min_params, is_variadic);
}

/* True if `text` (a captured C compiler stderr) flags `name` as unknown.
 * A bare reference (a constant/macro site) produces clang's "use of
 * undeclared identifier 'name'" or gcc's "'name' undeclared"; a call
 * produces clang's "call to undeclared function 'name'" or gcc's "implicit
 * declaration of function 'name'". Matching all four covers both compilers
 * and both site shapes without needing to know which produced the text. */
static bool c_symbol_flagged_undeclared(const char *text, const char *name) {
    if (!text || !name || !name[0]) return false;
    char needle[300];
    snprintf(needle, sizeof(needle), "identifier '%s'", name);
    if (strstr(text, needle)) return true;
    snprintf(needle, sizeof(needle), "'%s' undeclared", name);
    if (strstr(text, needle)) return true;
    snprintf(needle, sizeof(needle), "undeclared function '%s'", name);
    if (strstr(text, needle)) return true;
    snprintf(needle, sizeof(needle), "declaration of function '%s'", name);
    if (strstr(text, needle)) return true;
    return false;
}

/* Builds a stub translation unit that references every recorded extern
 * site by name, each in the same shape it's actually used: a call site gets
 * a real, parenthesized call with its own argument count (as plain `0`
 * placeholders — only existence is being probed, not types), and a constant
 * site gets a bare reference. The parenthesized form matters for a call: a
 * function-like macro is only expanded by the preprocessor when followed by
 * '(', so probing it bare would misreport a legitimate macro as unknown.
 * Appends to `out` (already holding the '#include' lines); truncates
 * silently on overflow, same as append_c_header_includes. */
static void append_extern_probe_body(const ExternCallSite *calls, int call_count,
                                     char *out, size_t out_size) {
    size_t used = strlen(out);
#define PROBE_APPEND(s) do { \
        size_t _l = strlen(s); \
        if (used + _l < out_size) { memcpy(out + used, (s), _l); used += _l; out[used] = '\0'; } \
    } while (0)
    PROBE_APPEND("static void _gray_extern_probe(void) {\n");
    for (int i = 0; i < call_count; i++) {
        char line[256];
        if (calls[i].is_call) {
            char args[160] = "";
            size_t al = 0;
            for (int p = 0; p < calls[i].arg_count && al + 3 < sizeof(args); p++) {
                const char *piece = (p == 0) ? "0" : ", 0";
                size_t pl = strlen(piece);
                memcpy(args + al, piece, pl); al += pl; args[al] = '\0';
            }
            snprintf(line, sizeof(line), "    (void)(%s(%s));\n", calls[i].func_name, args);
        } else {
            snprintf(line, sizeof(line), "    (void)(%s);\n", calls[i].func_name);
        }
        PROBE_APPEND(line);
    }
    PROBE_APPEND("}\n");
#undef PROBE_APPEND
}

/* Validates every extern.func(...) call and extern.CONST access the type
 * checker recorded against the real C header(s) this program imports, using
 * the target compiler itself as the source of truth (the typechecker has no
 * C header parser). Two checks, both anchored at the call/access site so a
 * problem is a Grayscale diagnostic instead of a raw C compiler error
 * against a temp file:
 *
 *   1. Existence (E5052): a stub referencing every site by name (in the
 *      shape it's actually used) is compiled with -fsyntax-only, and any
 *      name the compiler reports as undeclared is unknown to the imported
 *      headers — most commonly a typo'd function or constant name.
 *   2. Argument count (E5050): asks the target compiler to dump its own AST
 *      for a stub that only includes the headers, and looks up each call's
 *      function there.
 *
 * Both fail open: if the target compiler does not support the flags used
 * here (anything but a clang/gcc-compatible `cc`), or a given symbol's
 * signature is not found in the AST dump (a macro, or a dump shape the
 * parser does not handle), that check is skipped for the affected site(s)
 * and the real compile still catches what's left. */
static void validate_c_extern_signatures(AstNode *program, TypeChecker *checker,
                                         DiagnosticList *diag, Arena *arena,
                                         const char *cc_cmd, bool cc_is_command,
                                         const char *entry_file) {
    int call_count = 0;
    const ExternCallSite *calls = typechecker_get_extern_calls(checker, &call_count);
    if (call_count == 0) return;

    char includes[4096];
    append_c_header_includes(program, entry_file, includes, sizeof(includes));
    if (!includes[0]) return;

    /* Check 1: does each referenced symbol exist at all? */
    {
        char probe_src[16384];
        snprintf(probe_src, sizeof(probe_src), "%s", includes);
        append_extern_probe_body(calls, call_count, probe_src, sizeof(probe_src));

        char probe_stub[PATH_BUF_SIZE];
        probe_stub[0] = '\0';
        int pn = gray_temp_path(probe_stub, sizeof(probe_stub), "gray_existprobe_", ".c");
        if (pn >= 0 && (size_t)pn < sizeof(probe_stub) && write_file(probe_stub, probe_src)) {
            FILE *perr = gray_tmpfile();
            if (perr) {
                ArgV pa = {0};
                if (cc_is_command) argv_push_command(&pa, arena, cc_cmd);
                else argv_push(&pa, cc_cmd);
                argv_push(&pa, "-fsyntax-only");
                add_local_c_header_dirs(&pa, arena, program, entry_file);
                argv_push(&pa, "-x");
                argv_push(&pa, "c");
                argv_push(&pa, probe_stub);
                argv_end(&pa);

                if (!pa.overflow) gray_spawn_capture_stderr(pa.v, perr);
                long elen = ftell(perr);
                if (elen > 0) {
                    rewind(perr);
                    char *errtext = malloc((size_t)elen + 1);
                    if (errtext) {
                        size_t got = fread(errtext, 1, (size_t)elen, perr);
                        errtext[got] = '\0';
                        for (int i = 0; i < call_count; i++) {
                            if (c_symbol_flagged_undeclared(errtext, calls[i].func_name)) {
                                diagnostic_error_code_formatted(diag, "E5052",
                                    calls[i].file ? calls[i].file : entry_file,
                                    calls[i].line, calls[i].column, 0,
                                    calls[i].func_name);
                            }
                        }
                        free(errtext);
                    }
                }
                fclose(perr);
            }
        }
        if (probe_stub[0]) gray_remove_file(probe_stub);
    }

    char stub[PATH_BUF_SIZE];
    int sn = gray_temp_path(stub, sizeof(stub), "gray_sigprobe_", ".c");
    if (sn < 0 || (size_t)sn >= sizeof(stub)) return;
    if (!write_file(stub, includes)) { gray_remove_file(stub); return; }

    FILE *capture = gray_tmpfile();
    if (!capture) { gray_remove_file(stub); return; }

    ArgV a = {0};
    if (cc_is_command) argv_push_command(&a, arena, cc_cmd);
    else argv_push(&a, cc_cmd);
    argv_push(&a, "-Xclang");
    argv_push(&a, "-ast-dump");
    argv_push(&a, "-fsyntax-only");
    add_local_c_header_dirs(&a, arena, program, entry_file);
    argv_push(&a, "-x");
    argv_push(&a, "c");
    argv_push(&a, stub);
    argv_end(&a);

    bool spawned = !a.overflow && gray_spawn_capture_stdout(a.v, capture) == 0;
    gray_remove_file(stub);
    if (!spawned) { fclose(capture); return; }

    long dump_len = ftell(capture);
    if (dump_len <= 0) { fclose(capture); return; }
    rewind(capture);
    char *dump = malloc((size_t)dump_len + 1);
    if (!dump) { fclose(capture); return; }
    size_t got = fread(dump, 1, (size_t)dump_len, capture);
    dump[got] = '\0';
    fclose(capture);

    for (int i = 0; i < call_count; i++) {
        int min_params;
        bool is_variadic;
        if (!find_c_function_signature(dump, calls[i].func_name, &min_params, &is_variadic))
            continue;

        int actual = calls[i].arg_count;
        bool ok = is_variadic ? (actual >= min_params) : (actual == min_params);
        if (ok) continue;

        char expected[32];
        if (is_variadic) snprintf(expected, sizeof(expected), "at least %d", min_params);
        else snprintf(expected, sizeof(expected), "%d", min_params);
        diagnostic_error_code_formatted(diag, "E5050",
            calls[i].file ? calls[i].file : entry_file,
            calls[i].line, calls[i].column, 0,
            calls[i].func_name, expected, actual);
    }

    free(dump);
}

/* Command-line configuration, filled by parse_args() and read-only after. */
typedef struct {
    const char *input_file;
    const char *output_file;
    const char *opt_level;
    const char *cc_override;
    const char *quiet_codes_arg;  /* comma-separated W-codes from -q, or NULL */
    size_t arena_limit;           /* 0 = let codegen use its 1 GB default */
    bool emit_c_only;
    bool check_only;
    bool run_mode;
    bool fmt_mode;
    bool test_mode;               /* --test: emit a test runner instead of calling main() */
    bool verbose;
    bool show_time;
    bool no_color;
    bool debug_symbols;
    bool quiet_all;
} CompilerOptions;

typedef enum {
    ARGS_OK,     /* options parsed; carry on compiling */
    ARGS_DONE,   /* the argument was the whole request (version, help); exit 0 */
    ARGS_ERROR,  /* the arguments were unusable; exit 1 */
} ArgsStatus;

static ArgsStatus parse_args(int argc, char **argv, CompilerOptions *opts) {
    *opts = (CompilerOptions){ .opt_level = "-O2" };

    if (argc < 2) {
        print_usage();
        return ARGS_ERROR;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "version") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("gray %s\n", GRAY_VERSION);
            return ARGS_DONE;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return ARGS_DONE;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts->output_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-c") == 0) {
            opts->emit_c_only = true;
            continue;
        }
        if (strcmp(argv[i], "-O0") == 0) { opts->opt_level = "-O0"; continue; }
        if (strcmp(argv[i], "-O1") == 0) { opts->opt_level = "-O1"; continue; }
        if (strcmp(argv[i], "-O2") == 0) { opts->opt_level = "-O2"; continue; }
        if (strcmp(argv[i], "-O3") == 0) { opts->opt_level = "-O3"; continue; }
        if (strcmp(argv[i], "-g") == 0) {
            opts->debug_symbols = true;
            continue;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--opts->verbose") == 0) {
            opts->verbose = true;
            continue;
        }
        if (strcmp(argv[i], "--time") == 0) {
            opts->show_time = true;
            continue;
        }
        if (strcmp(argv[i], "--no-color") == 0) {
            opts->no_color = true;
            continue;
        }
        if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            /* --quiet / -q with optional next argument for specific codes */
            if (i + 1 < argc && argv[i + 1][0] == 'W') {
                opts->quiet_codes_arg = argv[++i];
            } else if (i + 1 < argc && argv[i + 1][0] == 'E') {
                fprintf(stderr, "gray: '-q' only accepts warning codes (W-prefixed), not error code '%s'\n", argv[i + 1]);
                return ARGS_ERROR;
            } else {
                opts->quiet_all = true;
            }
            continue;
        }
        /* Subcommands */
        if (strcmp(argv[i], "check") == 0 && !opts->input_file) {
            opts->check_only = true;
            continue;
        }
        if (strcmp(argv[i], "build") == 0 && !opts->input_file) {
            /* build is the default — just skip the keyword */
            continue;
        }
        if (strcmp(argv[i], "run") == 0 && !opts->input_file) {
            opts->run_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--fmt") == 0) {
            opts->fmt_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--test") == 0) {
            opts->test_mode = true;
            continue;
        }
        if (strncmp(argv[i], "--arena-limit=", 14) == 0) {
            opts->arena_limit = strtoull(argv[i] + 14, NULL, 10);
            continue;
        }
        if (strcmp(argv[i], "--cc") == 0 && i + 1 < argc) {
            opts->cc_override = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "gray: unknown option '%s'\n", argv[i]);
            return ARGS_ERROR;
        }
        opts->input_file = argv[i];
    }

    if (!opts->input_file) {
        fprintf(stderr, "gray: no input file\n");
        return ARGS_ERROR;
    }
    return ARGS_OK;
}

int main(int argc, char **argv) {
    /* Windows consoles need to be opted into ANSI escape handling before any
     * colored diagnostic is written. No-op everywhere else. */
    gray_enable_vt_mode();

    CompilerOptions opts;
    switch (parse_args(argc, argv, &opts)) {
    case ARGS_DONE:  return 0;
    case ARGS_ERROR: return 1;
    case ARGS_OK:    break;
    }

    /* Read source file */
    char *source = gray_read_file(opts.input_file, true);
    if (!source) return 1;

    /* fmt mode: reformat and write back, then exit */
    if (opts.fmt_mode) {
        FILE *tmp = gray_tmpfile();
        if (!tmp) {
            fprintf(stderr, "gray: fmt: could not create temp file\n");
            free(source);
            return 1;
        }
        int rc = gray_fmt_source(source, opts.input_file, tmp);
        if (rc != 0) {
            fprintf(stderr, "gray: fmt: failed to format '%s'\n", opts.input_file);
            fclose(tmp);
            free(source);
            return 1;
        }
        /* Read formatted output back */
        long fmt_len = ftell(tmp);
        rewind(tmp);
        char *fmt_buf = malloc(fmt_len + 1);
        if (!fmt_buf || (long)fread(fmt_buf, 1, fmt_len, tmp) != fmt_len) {
            fprintf(stderr, "gray: fmt: failed to read formatted output\n");
            fclose(tmp);
            free(source);
            return 1;
        }
        fmt_buf[fmt_len] = '\0';
        fclose(tmp);
        /* Write back to the original file with explicit 0644 permissions */
        if (!gray_write_file_mode(opts.input_file, fmt_buf, (size_t)fmt_len)) {
            fprintf(stderr, "gray: fmt: cannot write '%s'\n", opts.input_file);
            free(fmt_buf);
            free(source);
            return 1;
        }
        free(fmt_buf);
        free(source);
        return 0;
    }

    /* Create compiler arena and diagnostics */
    Arena *arena = arena_create(COMPILER_ARENA_SIZE);
    DiagnosticList *diag = diagnostic_create();
    diagnostic_set_source(diag, opts.input_file, source);
    if (opts.no_color) diag->use_color = false;

    /* Configure warning suppression */
    if (opts.quiet_all) {
        diag->suppress_all_warnings = true;
    } else if (opts.quiet_codes_arg) {
        /* Parse comma-separated warning codes */
        char *codes_buf = strdup(opts.quiet_codes_arg);
        int code_cap = 8;
        diag->suppressed_codes = malloc(sizeof(const char *) * code_cap);
        diag->suppressed_count = 0;
        char *tok = strtok(codes_buf, ",");
        while (tok) {
            /* Validate: must start with W */
            if (tok[0] == 'E') {
                fprintf(stderr, "gray: '-q' only accepts warning codes (W-prefixed), not error code '%s'\n", tok);
                free(codes_buf);
                return 1;
            }
            if (tok[0] != 'W') {
                fprintf(stderr, "gray: unknown warning code '%s'\n", tok);
                free(codes_buf);
                return 1;
            }
            if (diag->suppressed_count >= code_cap) {
                code_cap *= 2;
                void *tmp = realloc(diag->suppressed_codes, sizeof(const char *) * code_cap);
                if (!tmp) {
                    fprintf(stderr, "gray: out of memory\n");
                    free(codes_buf);
                    return 1;
                }
                diag->suppressed_codes = tmp;
            }
            diag->suppressed_codes[diag->suppressed_count++] = strdup(tok);
            tok = strtok(NULL, ",");
        }
        free(codes_buf);
    }

    double t_start = monotonic_ms();

    /* Lex */
    Lexer *lexer = lexer_create(arena, source, opts.input_file);

    /* Parse */
    Parser *parser = parser_create(arena, lexer, opts.input_file, diag);
    AstNode *program = parser_parse_program(parser);

    if (diagnostic_has_errors(diag)) {
        diagnostic_print_all(diag);
        diagnostic_print_summary(diag);
        diagnostic_destroy(diag);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Parse every imported .gray file and merge its declarations into the
     * program. The file -> module attribution collected along the way is the
     * only record of which module a source file belongs to, so it is handed
     * to the type checker below. */
    ImportResolution imports;
    imports_resolve(arena, diag, program, opts.input_file, &imports);


    /* An import that failed to resolve merged no declarations, so every
     * reference to the module it named is about to be reported undefined.
     * Those follow-on errors bury the one that matters and all disappear
     * when it is fixed, so stop here and report the import failure alone. */
    if (diagnostic_has_errors(diag)) {
        diagnostic_print_all(diag);
        diagnostic_print_summary(diag);
        diagnostic_destroy(diag);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Type check */
    TypeChecker *checker = typechecker_create(diag, opts.input_file);
    typechecker_set_test_mode(checker, opts.test_mode);
    typechecker_add_file_module(checker, opts.input_file, NULL, true);
    for (int i = 0; i < imports.count; i++)
        typechecker_add_file_module(checker, imports.files[i], imports.modules[i], false);
    for (int i = 0; i < imports.alias_count; i++)
        typechecker_add_module_alias(checker, imports.alias_names[i], imports.alias_targets[i]);
    typechecker_check(checker, program);

    if (diagnostic_has_errors(diag)) {
        diagnostic_print_all(diag);
        diagnostic_print_summary(diag);
        diagnostic_destroy(diag);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Print warnings even if no errors */
    if (diagnostic_warning_count(diag) > 0 && !diagnostic_has_errors(diag)) {
        diagnostic_print_all(diag);
        diagnostic_print_summary(diag);
    }

    /* Check-only mode: stop after type checking */
    if (opts.check_only) {
        /* extern.func()/extern.CONST validation needs a C compiler; detect
         * one the same way the full build does, but skip the check entirely
         * when none is found (fails open, same as validate_c_extern_signatures
         * itself) — gray check's fast path never requires a C compiler. */
        const char *check_cc_cmd = opts.cc_override ? opts.cc_override : detect_cc();
        if (check_cc_cmd) {
            validate_c_extern_signatures(program, checker, diag, arena, check_cc_cmd,
                                         opts.cc_override != NULL, opts.input_file);
        }

        if (diagnostic_has_errors(diag)) {
            diagnostic_print_all(diag);
            diagnostic_print_summary(diag);
            typechecker_free(checker);
            diagnostic_destroy(diag);
            arena_destroy(arena);
            free(source);
            return 1;
        }

        double t_end = monotonic_ms();
        if (opts.show_time) {
            double ms = t_end - t_start;
            fprintf(stderr, "gray: check completed in %.1fms\n", ms);
        }
        if (diag->use_color)
            fprintf(stderr, "%s%sgray: %s: no errors!%s\n",
                COL_BOLD, COL_GREEN, opts.input_file, COL_RESET);
        else
            fprintf(stderr, "gray: %s: no errors!\n", opts.input_file);
        typechecker_free(checker);
        diagnostic_destroy(diag);
        arena_destroy(arena);
        free(source);
        return 0;
    }

    /* Generate C code */
    CodeGen codegen = codegen_create(opts.input_file);
    codegen.type_table = typechecker_get_table(checker);
    codegen.modules = typechecker_get_modules(checker);
    codegen.arena_limit = opts.arena_limit;
    codegen.test_mode = opts.test_mode;
    codegen_generate(&codegen, program);
    const char *c_code = codegen_result(&codegen);
    double t_frontend_end = monotonic_ms();

    /* Determine output name */
    char *default_output = NULL;
    if (opts.run_mode && !opts.output_file) {
        /* Run mode: use temp file */
        default_output = malloc(PATH_BUF_SIZE);
        gray_temp_path(default_output, PATH_BUF_SIZE, "gray_run_", GRAY_EXE_SUFFIX);
        opts.output_file = default_output;
    } else if (!opts.output_file) {
        default_output = output_name_from_input(opts.input_file);
        opts.output_file = default_output;
    }

    /* Write generated C to a temp file. The name carries the output's base name
     * for readability under --opts.verbose, plus a pid and counter so concurrent
     * builds in different directories cannot collide. */
    char c_prefix[PATH_BUF_SIZE];
    snprintf(c_prefix, sizeof(c_prefix), "gray_%s_", gray_path_basename(opts.output_file));
    char c_file[PATH_BUF_SIZE];
    gray_temp_path(c_file, sizeof(c_file), c_prefix, ".c");

    if (!write_file(c_file, c_code)) {
        codegen_destroy(&codegen);
        typechecker_free(checker);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 1;
    }

    if (opts.emit_c_only) {
        /* Determine C output filename */
        const char *c_out = NULL;
        char *c_out_default = NULL;
        if (opts.output_file && opts.output_file != default_output) {
            /* Explicit -o provided */
            c_out = opts.output_file;
        } else {
            /* Derive from input: foo.gray -> foo.c */
            const char *base = gray_path_basename(opts.input_file);
            size_t blen = strlen(base);
            if (blen > GRAY_EXT_LEN && strcmp(base + blen - GRAY_EXT_LEN, GRAY_EXT) == 0)
                blen -= GRAY_EXT_LEN;
            c_out_default = malloc(blen + 3);
            memcpy(c_out_default, base, blen);
            memcpy(c_out_default + blen, ".c", 3);
            c_out = c_out_default;
        }

        if (!write_file(c_out, c_code)) {
            fprintf(stderr, "gray: failed to write C output: %s\n", c_out);
            free(c_out_default);
            codegen_destroy(&codegen);
            typechecker_free(checker);
            arena_destroy(arena);
            free(source);
            free(default_output);
            return 1;
        }
        printf("Generated: %s\n", c_out);
        free(c_out_default);
        codegen_destroy(&codegen);
        typechecker_free(checker);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 0;
    }


    /* Pick a C compiler (skip detection when --cc overrides) */
    const char *cc_cmd = opts.cc_override;
    if (!cc_cmd) {
        cc_cmd = detect_cc();
        if (!cc_cmd) {
            fprintf(stderr, "gray: no C compiler found.\n");
            fprintf(stderr, "  Install gcc or clang to compile Grayscale programs.\n");
            fprintf(stderr, "  On macOS: xcode-select --install\n");
            fprintf(stderr, "  On Ubuntu: sudo apt install gcc\n");
            fprintf(stderr, "  On Windows: install MinGW-w64, e.g.\n");
            fprintf(stderr, "    winget install BrechtSanders.WinLibs.POSIX.UCRT.Base\n");
            codegen_destroy(&codegen);
            typechecker_free(checker);
            arena_destroy(arena);
            free(source);
            free(default_output);
            return 1;
        }
    }

    /* A compiler chosen by filesystem path (--cc, GRAY_CC/CC, or the
     * well-known-location fallback) may live outside PATH; its helper
     * processes resolve their DLLs via PATH. No-op for bare command names. */
    gray_ensure_tool_dir_on_path(cc_cmd);

    /* Find runtime directory */
    const char *runtime_dir = find_runtime_dir(argv[0]);
    if (!runtime_dir) {
        fprintf(stderr, "gray: cannot find runtime headers.\n");
        fprintf(stderr, "  Searched:\n");
        fprintf(stderr, "    - $GRAY_RUNTIME environment variable\n");
        fprintf(stderr, "    - relative to gray binary\n");
        fprintf(stderr, "    - ./grayc/src/ (project root)\n");
        fprintf(stderr, "    - /usr/local/lib/grayc/\n");
        fprintf(stderr, "  Try: cd <project-root> && make -C grayc install\n");
        codegen_destroy(&codegen);
        typechecker_free(checker);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 1;
    }
#if GRAY_OS_WINDOWS
    /* The compiler is spawned with an argv array, so quoting and spaces are
     * handled for us. The one thing the C runtime cannot round-trip when it
     * re-serializes argv into a Windows command line is an embedded quote. */
    if (strchr(runtime_dir, '"') || strchr(opts.output_file, '"') || strchr(cc_cmd, '"')) {
        fprintf(stderr, "gray: paths must not contain double quotes\n");
        codegen_destroy(&codegen);
        typechecker_free(checker);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 1;
    }
#endif

    /* Preflight C-interop headers so a missing or wrong-platform header is a
     * Grayscale diagnostic anchored at the `extern import`, not a raw C
     * compiler error against a temp file. */
    if (!preflight_c_headers(program, diag, arena, cc_cmd,
                             opts.cc_override != NULL, opts.input_file)) {
        gray_remove_file(c_file);
        diagnostic_print_all(diag);
        diagnostic_print_summary(diag);
        diagnostic_destroy(diag);
        codegen_destroy(&codegen);
        typechecker_free(checker);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 1;
    }

    /* Validate extern.func(...) call sites against the real C signature
     * before ever invoking the real build's cc, so an argument-count
     * mismatch is a Grayscale diagnostic anchored at the call, not a raw C
     * compiler error against a temp file. */
    validate_c_extern_signatures(program, checker, diag, arena, cc_cmd,
                                 opts.cc_override != NULL, opts.input_file);
    if (diagnostic_has_errors(diag)) {
        gray_remove_file(c_file);
        diagnostic_print_all(diag);
        diagnostic_print_summary(diag);
        diagnostic_destroy(diag);
        codegen_destroy(&codegen);
        typechecker_free(checker);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 1;
    }

    /* Compile the generated C code.
     * Try linking against pre-compiled libgrayrt.a first (fast path).
     * Fall back to compiling runtime from source if archive not found. */
    char lib_path[PATH_BUF_SIZE];
    bool has_archive = false;

    /* Check for libgrayrt.a next to the runtime dir, then next to the binary */
    gray_path_join(lib_path, sizeof(lib_path), runtime_dir, "../libgrayrt.a");
    if (gray_file_readable(lib_path)) {
        has_archive = true;
    } else {
        const char *self = gray_self_dir(NULL);
        if (self) {
            gray_path_join(lib_path, sizeof(lib_path), self, "libgrayrt.a");
            if (gray_file_readable(lib_path)) has_archive = true;
        }
    }

    double t_cc_start = monotonic_ms();

    ArgV cc_argv = {0};
    /* Only --cc values are multi-word commands ("zig cc -target ...").
     * Detected compilers are single tokens that may contain spaces
     * (C:\Program Files\LLVM\bin\clang.exe) and must not be word-split. */
    if (opts.cc_override) {
        argv_push_command(&cc_argv, arena, cc_cmd);
    } else {
        argv_push(&cc_argv, cc_cmd);
    }
#if GRAY_OS_WINDOWS
    /* gnu11, not c11: -std=c11 defines __STRICT_ANSI__ on MinGW-w64, which
     * unbinds printf from the ANSI-conforming implementation (%zu breaks on
     * msvcrt) and hides the POSIX-shaped names in <io.h>. These must match
     * how libgrayrt.a is built (see grayc/Makefile STD_FLAGS). */
    argv_push(&cc_argv, "-std=gnu11");
    argv_push(&cc_argv, "-D__USE_MINGW_ANSI_STDIO=1");
    argv_push(&cc_argv, "-D_WIN32_WINNT=0x0601");
#else
    /* Must match how libgrayrt.a is built (see grayc/Makefile STD_FLAGS).
     * Without _POSIX_C_SOURCE, -std=c11 defines __STRICT_ANSI__, which on
     * glibc hides every POSIX name (realpath, strdup, setenv, fdopen, ...)
     * that extern interop reaches for, and can skew feature-gated
     * declarations in the shared runtime headers against the archive. */
    argv_push(&cc_argv, "-std=c11");
    argv_push(&cc_argv, "-D_POSIX_C_SOURCE=200809L");
#endif
    if (opts.debug_symbols) argv_push(&cc_argv, "-g");
    argv_push(&cc_argv, opts.opt_level);
    /* One section per function/variable so the linker's dead-strip pass (added
     * below) can drop the runtime and stdlib code the program never calls —
     * a trivial program links a fraction of libgrayrt.a instead of all of it.
     * Compile-time cost is negligible; there is no LTO. */
    argv_push(&cc_argv, "-ffunction-sections");
    argv_push(&cc_argv, "-fdata-sections");
    /* Marks this translation unit as a grayc-generated program. The stdlib
     * headers whose basename collides with a system header (time.h, io.h,
     * ...) only need to forward to the real header in this context — where
     * their directory is on -isystem and shadows libc — not when they are
     * compiled into libgrayrt.a. */
    argv_push(&cc_argv, "-DGRAY_GENERATED_C=1");
    argv_push(&cc_argv, "-Wall");
    argv_push(&cc_argv, "-Wno-unused-function");
    argv_push(&cc_argv, "-Wno-unused-variable");
    argv_push(&cc_argv, "-Wno-unused-but-set-variable");
    argv_push(&cc_argv, "-Wno-tautological-compare");
    argv_push(&cc_argv, "-Wno-infinite-recursion");
    argv_push(&cc_argv, "-Wno-incompatible-pointer-types-discards-qualifiers");
#if GRAY_OS_WINDOWS
    /* GCC's spelling of the Clang-only flag above. */
    argv_push(&cc_argv, "-Wno-discarded-qualifiers");
#endif
    /* An `extern.` call is emitted with its arguments passed through verbatim —
     * grayc cannot see the C signature to insert a cast. An opaque C handle
     * (FILE*, DIR*, ...) has no Grayscale type to name, so it round-trips as
     * `^byte` (uint8_t*), and a byte buffer passed to a `char*` parameter
     * differs only in signedness. Neither mismatch is expressible away in
     * source. Silence both so C interop compiles clean; on GCC >= 14
     * -Wincompatible-pointer-types is an error by default, so this also keeps
     * it from being a hard build failure. */
    argv_push(&cc_argv, "-Wno-incompatible-pointer-types");
    argv_push(&cc_argv, "-Wno-pointer-sign");
    argv_push(&cc_argv, "-isystem");
    argv_pushf(&cc_argv, arena, "%s" GRAY_PATH_SEP_STR "runtime", runtime_dir);
    argv_push(&cc_argv, "-isystem");
    argv_pushf(&cc_argv, arena, "%s" GRAY_PATH_SEP_STR "stdlib", runtime_dir);
    /* Local C headers ("./x.h") are written relative to the .gray source, not
     * the temp .c handed to the compiler. */
    add_local_c_header_dirs(&cc_argv, arena, program, opts.input_file);
    argv_push(&cc_argv, "-o");
    argv_push(&cc_argv, opts.output_file);
    argv_push(&cc_argv, c_file);

    if (has_archive) {
        argv_push(&cc_argv, lib_path);
    } else {
        /* The runtime sources reach shared headers via "util/..." includes
         * (runtime.c wants util/colors.h); expose the src root so those
         * resolve when building the runtime from source. */
        argv_push(&cc_argv, "-isystem");
        argv_push(&cc_argv, runtime_dir);
        /* Build source list from all runtime and stdlib .c files */
        static const char *runtime_srcs[] = {
            "runtime/runtime.c", "runtime/array.c", "runtime/map.c",
        };
        static const char *stdlib_srcs[] = {
            "stdlib/arrays.c",   "stdlib/binary.c",   "stdlib/builtins.c",
            "stdlib/bytes.c",    "stdlib/channels.c", "stdlib/crypto.c",
            "stdlib/csv.c",      "stdlib/encoding.c", "stdlib/fmt.c",
            "stdlib/http.c",     "stdlib/io.c",       "stdlib/json.c",
            "stdlib/maps.c",     "stdlib/math.c",     "stdlib/mem.c",
            "stdlib/net.c",      "stdlib/os.c",       "stdlib/random.c",
            "stdlib/regex.c",    "stdlib/server.c",   "stdlib/sqlite.c",
            "stdlib/strings.c",  "stdlib/sync.c",     "stdlib/atomic.c",
            "stdlib/threads.c",  "stdlib/runtime_mod.c",
            "stdlib/time.c",     "stdlib/uuid.c", "stdlib/strconv.c"
        };
        for (size_t i = 0; i < sizeof(runtime_srcs) / sizeof(runtime_srcs[0]); i++) {
            argv_pushf(&cc_argv, arena, "%s" GRAY_PATH_SEP_STR "%s", runtime_dir, runtime_srcs[i]);
        }
        for (size_t i = 0; i < sizeof(stdlib_srcs) / sizeof(stdlib_srcs[0]); i++) {
            argv_pushf(&cc_argv, arena, "%s" GRAY_PATH_SEP_STR "%s", runtime_dir, stdlib_srcs[i]);
        }
    }

    /* Drop the sections nothing references (see -ffunction-sections above).
     * Apple ld and GNU ld/lld spell it differently. */
#if defined(__APPLE__)
    argv_push(&cc_argv, "-Wl,-dead_strip");
#else
    argv_push(&cc_argv, "-Wl,--gc-sections");
#endif

    /* Platform link flags. */
    argv_push(&cc_argv, "-lm");
    argv_push(&cc_argv, "-lpthread");
#if GRAY_OS_WINDOWS
    argv_push(&cc_argv, "-lws2_32");  /* Winsock, used by net/http/server */
    /* Self-contained exe: winpthread and libgcc link statically so the binary
     * runs without MinGW's bin directory on PATH. System import libraries
     * (kernel32, msvcrt, ws2_32) stay dynamic — those DLLs ship with the OS. */
    argv_push(&cc_argv, "-static");
#endif
    argv_push(&cc_argv, "-Wl,-w");
    argv_end(&cc_argv);

    if (cc_argv.overflow) {
        fprintf(stderr, "gray: too many arguments to the C compiler\n");
        codegen_destroy(&codegen);
        typechecker_free(checker);
        diagnostic_destroy(diag);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 1;
    }

    if (opts.verbose) {
        fprintf(stderr, "gray: ");
        argv_print(&cc_argv, stderr);
    }

    /* The compiler's own text is captured, not inherited: warnings from a
     * build that succeeded are raw C output the user did not ask for, so they
     * are shown only under --verbose. A failed build still shows everything
     * the compiler said. */
    FILE *cc_err = gray_tmpfile();
    int ret = cc_err ? gray_spawn_capture_stderr(cc_argv.v, cc_err)
                     : gray_spawn_path(cc_argv.v);
    if (ret < 0) {
        fprintf(stderr, "gray: could not run the C compiler '%s'\n", cc_argv.v[0]);
        ret = 1;
    }
    if (cc_err) {
        if (ret != 0 || opts.verbose) {
            char cc_buf[4096];
            size_t got;
            rewind(cc_err);
            while ((got = fread(cc_buf, 1, sizeof(cc_buf), cc_err)) > 0)
                fwrite(cc_buf, 1, got, stderr);
        }
        fclose(cc_err);
    }

    double t_cc_end = monotonic_ms();

    if (ret != 0) {
        fprintf(stderr, "gray: C compilation failed\n");
        bool has_c_import = false;
        for (int si = 0; si < program->data.program.stmt_count; si++) {
            AstNode *s = program->data.program.stmts[si];
            if (s->kind == NODE_IMPORT_STMT) {
                for (int ii = 0; ii < s->data.import_stmt.count; ii++) {
                    if (s->data.import_stmt.items[ii].is_c_import) {
                        has_c_import = true;
                        break;
                    }
                }
            }
            if (has_c_import) break;
        }
        if (has_c_import) {
            fprintf(stderr, "gray: hint: check that all C headers in extern import \"...\" exist and are installed\n");
        }
        fprintf(stderr, "gray: generated C source at %s\n", c_file);
    } else {
        gray_remove_file(c_file);

        double total_ms = t_cc_end - t_start;
        if (!opts.run_mode) {
            const char *out_base = gray_path_basename(opts.output_file);
            if (!opts.no_color && gray_stdout_is_tty()) {
                fprintf(stdout, "\033[32mCompiled '\033[1m%s\033[22m' in %.0fms!\033[0m\n",
                    out_base, total_ms);
            } else {
                fprintf(stdout, "Compiled '%s' in %.0fms!\n", out_base, total_ms);
            }
            fflush(stdout);
        }

        if (opts.show_time) {
            double frontend_ms = t_frontend_end - t_start;
            double setup_ms = t_cc_start - t_frontend_end;
            double cc_ms = t_cc_end - t_cc_start;
            fprintf(stderr, "  frontend:  %.1fms (lex + parse + typecheck + codegen)\n", frontend_ms);
            fprintf(stderr, "  setup:     %.1fms (compiler probe + temp write)\n", setup_ms);
            fprintf(stderr, "  cc:        %.1fms (compile + link)\n", cc_ms);
        }
    }

    /* Run mode: execute the binary and clean up. Spawned without a shell and
     * without a PATH search — the output path comes from user-supplied CLI
     * input, and a bare name must not resolve to some unrelated binary. */
    bool ran_program = false;
    if (ret == 0 && opts.run_mode) {
        const char *run_argv[] = {opts.output_file, NULL};
        int term_signal = 0;
        ret = gray_spawn_exact(run_argv, &term_signal);
        if (ret < 0) {
            fprintf(stderr, "gray: cannot execute '%s'\n", opts.output_file);
            ret = 1;
        } else if (term_signal) {
            fflush(stdout);
            fprintf(stderr, "gray: program crashed: signal %d (%s)\n",
                    term_signal, strsignal(term_signal));
        }
        ran_program = true;
        gray_remove_file(opts.output_file);
    }

    codegen_destroy(&codegen);
    typechecker_free(checker);
    diagnostic_destroy(diag);
    arena_destroy(arena);
    free(source);
    free(default_output);

    /* The program's own status (exit(code), or 128 + signal for a crash) is
     * the process status; a compile-side failure is a flat 1. */
    return ran_program ? ret : (ret != 0 ? 1 : 0);
}
