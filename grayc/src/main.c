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
#include "util/buf.h"
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
#define PATH_BUFFER_SIZE 2048
#define COMPILER_ARENA_SIZE (1024 * 1024)
#define GRAY_EXTENSION      ".gray"
#define GRAY_EXTENSION_LENGTH  5

/* Wall-clock milliseconds from a monotonic source. clock() would measure only
 * this process's CPU time and miss the C compiler, which runs as a spawned
 * child and accounts for most of the total. */
static double monotonic_milliseconds(void) {
    struct timespec time_spec;
    clock_gettime(CLOCK_MONOTONIC, &time_spec);
    return (double)time_spec.tv_sec * 1000.0 + (double)time_spec.tv_nsec / 1e6;
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

    size_t length = strlen(base);
    if (length > GRAY_EXTENSION_LENGTH && strcmp(base + length - GRAY_EXTENSION_LENGTH, GRAY_EXTENSION) == 0) {
        length -= GRAY_EXTENSION_LENGTH;
    }

    size_t suffix_length = strlen(GRAY_EXECUTABLE_SUFFIX);
    char *output = malloc(length + suffix_length + 1);
    memcpy(output, base, length);
    memcpy(output + length, GRAY_EXECUTABLE_SUFFIX, suffix_length + 1);
    return output;
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
static const char *find_runtime_directory(const char *argv0) {
    static char path[PATH_BUFFER_SIZE];

    /* 1. Environment variable override */
    const char *environment_value = getenv("GRAY_RUNTIME");
    if (environment_value && gray_file_readable(environment_value)) {
        gray_path_join(path, sizeof(path), environment_value, "runtime/runtime.h");
        if (gray_file_readable(path)) return environment_value;
    }

    /* 2-3. Relative to binary location */
    const char *self_directory = gray_self_directory(argv0);
    if (self_directory) {
        /* Installed layout: binary in /usr/local/bin, runtime in /usr/local/lib/grayc */
        gray_path_join(path, sizeof(path), self_directory, "../lib/grayc/runtime/runtime.h");
        if (gray_file_readable(path)) {
            gray_path_join(path, sizeof(path), self_directory, "../lib/grayc");
            return path;
        }

        /* Development layout: binary in grayc/, runtime in grayc/src/runtime */
        gray_path_join(path, sizeof(path), self_directory, "src/runtime/runtime.h");
        if (gray_file_readable(path)) {
            gray_path_join(path, sizeof(path), self_directory, "src");
            return path;
        }
    }

    /* 4. Walk up from CWD looking for the project root */
    {
        char current_directory[PATH_BUFFER_SIZE];
        if (gray_getcwd(current_directory, sizeof(current_directory))) {
            char probe[PATH_BUFFER_SIZE];
            char *directory = current_directory;
            while (*directory) {
                gray_path_join(probe, sizeof(probe), directory, "grayc/src/runtime/runtime.h");
                if (gray_file_readable(probe)) {
                    gray_path_join(path, sizeof(path), directory, "grayc/src");
                    return path;
                }
                /* Move to parent. Stop at a filesystem root — on Windows that
                 * is a drive or UNC share, which has no separator to strip and
                 * would otherwise loop forever. */
                if (gray_path_is_root(directory)) break;
                char *separator = gray_path_last_separator(directory);
                if (!separator || separator == directory) break;
                *separator = '\0';
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
#define MAX_C_COMPILER_ARGUMENTS 128

typedef struct {
    const char *values[MAX_C_COMPILER_ARGUMENTS];
    int count;
    bool has_overflowed;
} ArgumentVector;

static void argument_vector_push(ArgumentVector *arguments, const char *text) {
    if (arguments->count >= MAX_C_COMPILER_ARGUMENTS - 1) {
        arguments->has_overflowed = true;
        return;
    }
    arguments->values[arguments->count++] = text;
}

/* Push a formatted argument, copied into the arena so it outlives this call. */
static void argument_vector_push_formatted(ArgumentVector *arguments, Arena *arena, const char *format, ...) {
    char buffer[PATH_BUFFER_SIZE];
    va_list variadic_arguments;
    va_start(variadic_arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, variadic_arguments);
    va_end(variadic_arguments);
    argument_vector_push(arguments, arena_copy_string(arena, buffer));
}

/* Split a compiler command into words, the way the shell used to when this
 * was interpolated into a system() string. `--cc "zig cc -target x86_64-linux-gnu"`
 * has to arrive as four separate arguments. */
static void argument_vector_push_command(ArgumentVector *arguments, Arena *arena, const char *command) {
    for (const char *cursor = command; *cursor;) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (!*cursor) break;
        const char *start = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') cursor++;
        argument_vector_push_formatted(arguments, arena, "%.*s", (int)(cursor - start), start);
    }
}

static void argument_vector_end(ArgumentVector *arguments) {
    arguments->values[arguments->count] = NULL;
}

static void argument_vector_print(const ArgumentVector *arguments, FILE *output) {
    for (int i = 0; i < arguments->count; i++) fprintf(output, "%s%s", i ? " " : "", arguments->values[i]);
    fputc('\n', output);
}

/* Pick the first C compiler present on PATH. The candidate that resolves is
 * the one we go on to invoke — accepting one name and then invoking a different
 * one breaks on any system that has gcc but no cc, which is every Windows
 * install and plenty of minimal Linux images. A filesystem check rather than a
 * `<cc> --version` spawn: the spawn cost ~11ms of C-driver startup on every
 * compile and only additionally proved the binary is not broken, which the
 * real compile reports anyway. */
static bool c_compiler_available(const char *c_compiler) {
    return gray_command_on_path(c_compiler);
}

static const char *detect_c_compiler(void) {
    /* GRAY_CC / CC are checked, not trusted: a stale CC=cc from a profile must
     * not break a system that only has gcc. Multi-word values ("zig cc")
     * cannot go through a single-token lookup — use --cc for those. */
    static const char *const environment_names[] = {"GRAY_CC", "CC"};
    for (size_t i = 0; i < sizeof(environment_names) / sizeof(environment_names[0]); i++) {
        const char *value = getenv(environment_names[i]);
        if (!value || !*value || strpbrk(value, " \t")) continue;
        if (c_compiler_available(value)) return value;
    }

    static const char *const candidates[] = {
#if GRAY_OS_WINDOWS
        "gcc", "clang", "cc",
#else
        "cc", "gcc", "clang",
#endif
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (c_compiler_available(candidates[i])) return candidates[i];
    }

    /* Nothing on PATH — check the well-known Windows install locations. */
    return gray_find_c_compiler_fallback();
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
                                      char *buffer, size_t buffer_size) {
    if (item->source_directory) return item->source_directory;
    snprintf(buffer, buffer_size, "%s", entry_file);
    char *separator = gray_path_last_separator(buffer);
    if (separator) separator[1] = '\0';
    else snprintf(buffer, buffer_size, "./");
    return buffer;
}

/* item->path resolved against its importing file's directory — "./x.h" in
 * sub/mod.gray becomes "sub/./x.h". Used both to check/emit a local header
 * and, critically, as the *dedup key* for one: two different directories
 * each importing their own "./bindings.h" must not collapse into a single
 * check/emission just because the raw spelling is identical (#2729) — the
 * resolved path differs even though the written text doesn't. */
static void resolve_local_c_header_path(const ImportItem *item, const char *entry_file,
                                        char *output, size_t out_size) {
    char directory_buffer[PATH_BUFFER_SIZE];
    const char *directory = local_c_header_dir(item, entry_file, directory_buffer, sizeof(directory_buffer));
    snprintf(output, out_size, "%s%s", directory, item->path);
}

/* Preflight every distinct C header named by an `extern import` before the
 * real compile. A header that is missing, misspelled, or exists only on
 * another platform otherwise surfaces as the C compiler's own "file not
 * found" against a temp .c path the user never wrote. Reports a diagnostic
 * anchored at the import instead. `cc_cmd` / `cc_is_command` are what the real
 * build will invoke, so a cross-compile target's headers are what is checked.
 * Returns false when at least one header could not be resolved. */
static bool preflight_c_headers(AstNode *program, DiagnosticList *diagnostics, Arena *arena,
                                const char *c_compiler_command, bool cc_is_command,
                                const char *entry_file) {
    const char *seen[MAX_C_COMPILER_ARGUMENTS];
    int seen_count = 0;
    bool is_valid = true;

    for (int statement_index = 0; statement_index < program->data.program.statement_count; statement_index++) {
        AstNode *statement = program->data.program.statements[statement_index];
        if (statement->kind != NODE_IMPORT_STATEMENT) continue;
        for (int item_index = 0; item_index < statement->data.import_statement.count; item_index++) {
            ImportItem *item = &statement->data.import_statement.items[item_index];
            if (!item->is_c_import || !item->path) continue;

            /* A local header's dedup header_key must be its resolved path, not the
             * raw spelling: two different directories each importing their
             * own "./bindings.h" are two different files that both need
             * checking, even though the text is identical (#2729). A
             * system header has no directory to resolve against, so the
             * raw name is already the right header_key. */
            char resolved[PATH_BUFFER_SIZE];
            bool is_local = c_header_is_local(item->path);
            if (is_local) resolve_local_c_header_path(item, entry_file, resolved, sizeof(resolved));
            const char *header_key = is_local ? resolved : item->path;

            bool is_duplicate = false;
            for (int earlier_index = 0; earlier_index < seen_count; earlier_index++)
                if (strcmp(seen[earlier_index], header_key) == 0) { is_duplicate = true; break; }
            if (is_duplicate) continue;
            if (seen_count < MAX_C_COMPILER_ARGUMENTS)
                seen[seen_count++] = arena_copy_string(arena, header_key);

            bool found;
            if (is_local) {
                found = gray_file_readable(resolved);
            } else {
                /* Angle-bracket header: ask the target compiler whether it
                 * can find it. -fsyntax-only stops before codegen. */
                char stub[PATH_BUFFER_SIZE];
                int path_length = gray_temporary_path(stub, sizeof(stub), "gray_hdrcheck_", ".c");
                if (path_length < 0 || (size_t)path_length >= sizeof(stub))
                    continue; /* cannot check — let the real compile report it */
                char body[PATH_BUFFER_SIZE];
                snprintf(body, sizeof(body),
                    "#include <%s>\nint main(void){return 0;}\n", item->path);
                if (!write_file(stub, body)) { gray_remove_file(stub); continue; }

                ArgumentVector arguments = {0};
                if (cc_is_command) argument_vector_push_command(&arguments, arena, c_compiler_command);
                else argument_vector_push(&arguments, c_compiler_command);
                argument_vector_push(&arguments, "-fsyntax-only");
                argument_vector_push(&arguments, "-x");
                argument_vector_push(&arguments, "c");
                argument_vector_push(&arguments, stub);
                argument_vector_end(&arguments);
                found = !arguments.has_overflowed && gray_spawn_quiet(arguments.values) == 0;
                gray_remove_file(stub);
            }

            if (!found) {
                is_valid = false;
                char help[512];
                snprintf(help, sizeof(help),
                    "'%s' is not available for this target, or the library that "
                    "provides it is not installed. Grayscale has no conditional "
                    "import: only import C headers that are available on every "
                    "target you build for.", item->path);
                diagnostic_error_code_formatted_help(diagnostics, "E6015",
                    item->token.file ? item->token.file : entry_file,
                    item->token.line, item->token.column, 0,
                    arena_copy_string(arena, help), item->path);
            }
        }
    }
    return is_valid;
}

/* Put the directory of every file that names a local C header ("./x.h" /
 * "../x.h") on the quoted-include search path. The generated C is written to a
 * temp path, so a verbatim `#include "./x.h"` would otherwise be resolved
 * relative to $TMPDIR and never found. -iquote (not -I) keeps this confined to
 * the quoted-include form, matching how the header was written. */
static void add_local_c_header_dirs(ArgumentVector *c_compiler_arguments, Arena *arena, AstNode *program,
                                    const char *entry_file) {
    const char *seen[MAX_C_COMPILER_ARGUMENTS];
    int seen_count = 0;

    for (int statement_index = 0; statement_index < program->data.program.statement_count; statement_index++) {
        AstNode *statement = program->data.program.statements[statement_index];
        if (statement->kind != NODE_IMPORT_STATEMENT) continue;
        for (int item_index = 0; item_index < statement->data.import_statement.count; item_index++) {
            ImportItem *item = &statement->data.import_statement.items[item_index];
            if (!item->is_c_import || !item->path) continue;
            if (!c_header_is_local(item->path)) continue;

            /* Directory of the importing file (mirrors preflight_c_headers). */
            char base[PATH_BUFFER_SIZE];
            const char *directory = item->source_directory;
            if (!directory) {
                snprintf(base, sizeof(base), "%s", entry_file);
                char *separator = gray_path_last_separator(base);
                if (separator) separator[1] = '\0';
                else snprintf(base, sizeof(base), "./");
                directory = base;
            }

            bool is_duplicate = false;
            for (int earlier_index = 0; earlier_index < seen_count; earlier_index++)
                if (strcmp(seen[earlier_index], directory) == 0) { is_duplicate = true; break; }
            if (is_duplicate) continue;
            const char *kept = arena_copy_string(arena, directory);
            if (seen_count < MAX_C_COMPILER_ARGUMENTS) seen[seen_count++] = kept;

            argument_vector_push(c_compiler_arguments, "-iquote");
            argument_vector_push(c_compiler_arguments, kept);
        }
    }
}

/* Every distinct C header named by an `extern import` anywhere in the program,
 * in first-seen order. The dedup key per item is its resolved path for a local
 * header, its raw name for a system one — see the dedup-key comment in
 * preflight_c_headers for why a local header can't be deduped by its raw
 * "./x.h" spelling (#2729). Returns the number of items stored. */
static int collect_distinct_c_headers(AstNode *program, const char *entry_file,
                                      const ImportItem **output, int maximum_count) {
    int count = 0;
    for (int statement_index = 0; statement_index < program->data.program.statement_count; statement_index++) {
        AstNode *statement = program->data.program.statements[statement_index];
        if (statement->kind != NODE_IMPORT_STATEMENT) continue;
        for (int item_index = 0; item_index < statement->data.import_statement.count; item_index++) {
            ImportItem *item = &statement->data.import_statement.items[item_index];
            if (!item->is_c_import || !item->path) continue;

            bool is_local = c_header_is_local(item->path);
            char resolved[PATH_BUFFER_SIZE];
            if (is_local) resolve_local_c_header_path(item, entry_file, resolved, sizeof(resolved));

            bool is_duplicate = false;
            for (int earlier_index = 0; earlier_index < count; earlier_index++) {
                const ImportItem *candidate_item = output[earlier_index];
                bool is_candidate_local = c_header_is_local(candidate_item->path);
                if (is_candidate_local != is_local) continue;
                if (is_local) {
                    char resolved_path[PATH_BUFFER_SIZE];
                    resolve_local_c_header_path(candidate_item, entry_file, resolved_path, sizeof(resolved_path));
                    if (strcmp(resolved_path, resolved) == 0) { is_duplicate = true; break; }
                } else if (strcmp(candidate_item->path, item->path) == 0) {
                    is_duplicate = true;
                    break;
                }
            }
            if (is_duplicate) continue;
            if (count < maximum_count) output[count++] = item;
        }
    }
    return count;
}

/* Append `#include <path>` (angle-bracket header) or `#include "resolved"`
 * (a "./x.h" / "../x.h" local header, resolved against the importing file's
 * directory) for each of `headers`, mirroring exactly what the real generated
 * .c file includes. Used to build a stub translation unit for probing real C
 * function signatures. */
static void append_c_header_includes(const ImportItem *const *headers, int count,
                                     const char *entry_file, char *output, size_t out_size) {
    size_t used = 0;
    output[0] = '\0';

    for (int i = 0; i < count; i++) {
        const ImportItem *item = headers[i];
        char line[PATH_BUFFER_SIZE];
        if (c_header_is_local(item->path)) {
            char resolved[PATH_BUFFER_SIZE];
            char canonical[PATH_BUFFER_SIZE];
            resolve_local_c_header_path(item, entry_file, resolved, sizeof(resolved));
            /* Canonical absolute path, as codegen emits it: a stub written to
             * a temp dir cannot resolve a cwd-relative path. */
            const char *target = gray_realpath_into(resolved, canonical, sizeof(canonical))
                ? canonical : resolved;
            snprintf(line, sizeof(line), "#include \"%s\"\n", target);
        } else {
            snprintf(line, sizeof(line), "#include <%s>\n", item->path);
        }
        size_t line_length = strlen(line);
        if (used + line_length < out_size) {
            memcpy(output + used, line, line_length);
            used += line_length;
            output[used] = '\0';
        }
    }
}

/* True if `needle` occurs anywhere in [start, end). `start`/`end` need not be
 * NUL-terminated at `end` — used to search one line of a larger buffer. */
static bool range_contains(const char *start, const char *end_cursor, const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0) return true;
    for (const char *cursor = start; cursor + needle_length <= end_cursor; cursor++) {
        if (memcmp(cursor, needle, needle_length) == 0) return true;
    }
    return false;
}

/* What a C function hands back, as far as a Grayscale declaration or cast
 * can tell: the return type's kind, not its exact width. C_RETURN_UNKNOWN is a
 * spelling this does not recognise (a typedef name); it is never rejected. */
typedef enum {
    C_RETURN_UNKNOWN, C_RETURN_VOID, C_RETURN_INTEGER, C_RETURN_FLOATING_POINT, C_RETURN_POINTER, C_RETURN_AGGREGATE
} CReturnClass;

/* How many leading parameters of a C function have their kind recorded. */
#define C_SIGNATURE_PARAMETERS 16

typedef struct {
    int minimum_parameters;
    bool is_variadic;
    CReturnClass return_class;
    char return_text[128];
    int parameter_count;                        /* entries of parameter_* filled in */
    CReturnClass parameter_class[C_SIGNATURE_PARAMETERS];
    char parameter_text[C_SIGNATURE_PARAMETERS][64];
} CFunctionSignature;

/* Classifies a C return type spelled the way clang's AST dump prints it
 * ("unsigned long", "void *", "enum E", "struct tm"). */
static CReturnClass classify_c_return(const char *spelling) {
    if (strchr(spelling, '*') || strchr(spelling, '[')) return C_RETURN_POINTER;

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s", spelling);
    bool saw_integer_keyword = false, saw_floating_point_keyword = false, saw_void_keyword = false;
    for (char *save = NULL, *word = strtok_r(buffer, " ", &save); word; word = strtok_r(NULL, " ", &save)) {
        if (strcmp(word, "const") == 0 || strcmp(word, "volatile") == 0) continue;
        if (strcmp(word, "struct") == 0 || strcmp(word, "union") == 0) return C_RETURN_AGGREGATE;
        if (strcmp(word, "enum") == 0 || strcmp(word, "_Bool") == 0 || strcmp(word, "unsigned") == 0 ||
            strcmp(word, "signed") == 0 || strcmp(word, "char") == 0 || strcmp(word, "short") == 0 ||
            strcmp(word, "int") == 0) { saw_integer_keyword = true; continue; }
        if (strcmp(word, "long") == 0) continue;
        if (strcmp(word, "float") == 0 || strcmp(word, "double") == 0) { saw_floating_point_keyword = true; continue; }
        if (strcmp(word, "void") == 0) { saw_void_keyword = true; continue; }
        return C_RETURN_UNKNOWN;
    }
    if (saw_void_keyword) return C_RETURN_VOID;
    if (saw_floating_point_keyword) return C_RETURN_FLOATING_POINT;
    return saw_integer_keyword || strstr(spelling, "long") ? C_RETURN_INTEGER : C_RETURN_UNKNOWN;
}

/* Classifies a C typedef name by its TypedefDecl line in a clang AST dump,
 * which spells the type as written ('struct div_t') and, when that differs, the
 * fully resolved type after a colon ('__darwin_size_t':'unsigned long'). The
 * written spelling is tried first, then the resolved one. */
static CReturnClass classify_c_typedef(const char *dump, const char *name) {
    size_t name_length = strlen(name);
    for (const char *cursor = dump; (cursor = strstr(cursor, name)) != NULL; cursor += name_length) {
        if (cursor == dump || cursor[-1] != ' ' || cursor[name_length] != ' ' || cursor[name_length + 1] != '\'') continue;
        const char *line_start = cursor;
        while (line_start > dump && line_start[-1] != '\n') line_start--;
        if (!range_contains(line_start, cursor, "TypedefDecl")) continue;

        const char *spelling = cursor + name_length + 2;
        const char *spelling_end = strchr(spelling, '\'');
        if (!spelling_end) continue;
        for (int pass = 0; pass < 2; pass++) {
            char text[128];
            size_t text_length = (size_t)(spelling_end - spelling);
            if (text_length >= sizeof(text)) return C_RETURN_UNKNOWN;
            memcpy(text, spelling, text_length);
            text[text_length] = '\0';
            CReturnClass return_class = classify_c_return(text);
            if (return_class != C_RETURN_UNKNOWN) return return_class;
            if (pass == 1 || spelling_end[1] != ':' || spelling_end[2] != '\'') break;
            spelling = spelling_end + 3;
            spelling_end = strchr(spelling, '\'');
            if (!spelling_end) break;
        }
        return C_RETURN_UNKNOWN;
    }
    return C_RETURN_UNKNOWN;
}

/* True when a C result of class `rc` may be declared as, or cast to,
 * `asserted`. A declaration only accepts the matching family — integer kinds
 * (i64, u64, u8, char, bool) among themselves, f64, pointer — because C
 * would otherwise truncate silently or reject the initializer. cast() is the
 * explicit conversion, so it also crosses families where C allows it. A void
 * or aggregate result fits nothing. */
static bool c_return_fits(CReturnClass return_class, const GrayType *asserted, bool via_cast) {
    if (return_class == C_RETURN_UNKNOWN) return true;
    if (return_class == C_RETURN_VOID || return_class == C_RETURN_AGGREGATE) return false;

    if (asserted->kind == TYPE_KIND_FLOATING_POINT)
        return return_class == C_RETURN_FLOATING_POINT || (via_cast && return_class == C_RETURN_INTEGER);
    if (asserted->kind == TYPE_KIND_POINTER)
        return return_class == C_RETURN_POINTER || (via_cast && return_class == C_RETURN_INTEGER);
    return return_class == C_RETURN_INTEGER || via_cast;
}

/* True when a Grayscale argument cannot be converted by C to a parameter of
 * class `param`: a string, nil or pointer where C wants a number, or a number
 * where C wants a pointer. Any other argument kind, or a parameter of a kind
 * this does not recognise, is left to the C compiler. */
static bool c_argument_kind_mismatch(CReturnClass parameter_class, const GrayType *argument_type) {
    bool is_argument_number = argument_type->kind == TYPE_KIND_SIGNED_INTEGER || argument_type->kind == TYPE_KIND_UNSIGNED_INTEGER || argument_type->kind == TYPE_KIND_FLOATING_POINT ||
                         argument_type->kind == TYPE_KIND_BOOL || argument_type->kind == TYPE_KIND_CHAR;
    bool is_argument_pointer = argument_type->kind == TYPE_KIND_STRING || argument_type->kind == TYPE_KIND_POINTER || argument_type->kind == TYPE_KIND_NIL;
    if (parameter_class == C_RETURN_INTEGER || parameter_class == C_RETURN_FLOATING_POINT) return is_argument_pointer;
    if (parameter_class == C_RETURN_POINTER) return is_argument_number;
    return false;
}

/* Parses a clang `-ast-dump` FunctionDecl type spelling, e.g.
 * "int (int, FILE *)" or "int (const char *, ...)", into a required
 * parameter count and a variadic flag. Counts only top-level commas — a
 * function-pointer parameter's own comma-separated parameter list (nested in
 * its own parens) does not split the outer list. Returns false when `sig`
 * does not have the expected "(...)" shape, so the caller skips validation
 * instead of guessing. */
static bool count_c_parameters(const char *signature_text, CFunctionSignature *output) {
    int *minimum_parameters = &output->minimum_parameters;
    bool *is_variadic = &output->is_variadic;
    *minimum_parameters = 0;
    *is_variadic = false;
    output->return_class = C_RETURN_UNKNOWN;
    output->return_text[0] = '\0';
    output->parameter_count = 0;

    size_t length = strlen(signature_text);
    if (length == 0 || signature_text[length - 1] != ')') return false;

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
    for (size_t i = 0; i < length; i++) {
        if (signature_text[i] == '(') {
            if (depth == 0) {
                last_open = (long)i;
                if (group_count == 0) first_open = (long)i;
            }
            depth++;
        } else if (signature_text[i] == ')') {
            depth--;
            if (depth == 0) {
                last_close = (long)i;
                if (group_count == 0) first_close = (long)i;
                group_count++;
            }
        }
    }
    if (group_count == 0 || depth != 0) return false;

    const char *parameter_list = NULL;
    size_t parameter_list_length = 0;

    if (group_count == 2 && last_close == (long)length - 1) {
        const char *gcc_cursor = signature_text + first_open + 1;
        size_t gcc_length = (size_t)(first_close - first_open - 1);
        size_t i = 0;
        while (i < gcc_length && gcc_cursor[i] == ' ') i++;
        if (i < gcc_length && gcc_cursor[i] == '*') {
            i++;
            while (i < gcc_length && gcc_cursor[i] != '(' && gcc_cursor[i] != '*') i++;
            if (i < gcc_length && gcc_cursor[i] == '(') {
                size_t inner_open = i;
                int inner_depth = 0;
                long inner_close = -1;
                for (size_t j = inner_open; j < gcc_length; j++) {
                    if (gcc_cursor[j] == '(') inner_depth++;
                    else if (gcc_cursor[j] == ')') { inner_depth--; if (inner_depth == 0) { inner_close = (long)j; break; } }
                }
                if (inner_close >= 0) {
                    parameter_list = gcc_cursor + inner_open + 1;
                    parameter_list_length = (size_t)inner_close - inner_open - 1;
                    output->return_class = C_RETURN_POINTER;
                    snprintf(output->return_text, sizeof(output->return_text), "function pointer");
                }
            }
        }
    }

    if (!parameter_list) {
        /* Ordinary shape: the sole (or, failing the pointer-return check
         * above, the final) top-level group is the parameter list. */
        parameter_list = signature_text + last_open + 1;
        parameter_list_length = length - 1 - (size_t)(last_open + 1);

        /* Everything before the parameter list is the return type. */
        size_t return_length = (size_t)last_open;
        while (return_length > 0 && signature_text[return_length - 1] == ' ') return_length--;
        if (return_length > 0 && return_length < sizeof(output->return_text)) {
            memcpy(output->return_text, signature_text, return_length);
            output->return_text[return_length] = '\0';
            output->return_class = classify_c_return(output->return_text);
        }
    }

    while (parameter_list_length > 0 && parameter_list[0] == ' ') { parameter_list++; parameter_list_length--; }
    while (parameter_list_length > 0 && parameter_list[parameter_list_length - 1] == ' ') parameter_list_length--;

    if (parameter_list_length == 0 || (parameter_list_length == 4 && memcmp(parameter_list, "void", 4) == 0)) {
        return true; /* explicitly zero parameters */
    }

    int nest = 0;
    size_t seg_start = 0;
    int count = 0;
    bool variadic = false;
    for (size_t i = 0; i <= parameter_list_length; i++) {
        bool is_at_end = (i == parameter_list_length);
        char character = is_at_end ? ',' : parameter_list[i];
        if (!is_at_end && (character == '(' || character == '[')) { nest++; continue; }
        if (!is_at_end && (character == ')' || character == ']')) { nest--; continue; }
        if (character == ',' && nest == 0) {
            const char *segment = parameter_list + seg_start;
            size_t segment_length = i - seg_start;
            while (segment_length > 0 && segment[0] == ' ') { segment++; segment_length--; }
            while (segment_length > 0 && segment[segment_length - 1] == ' ') segment_length--;
            if (segment_length == 3 && memcmp(segment, "...", 3) == 0) variadic = true;
            else if (segment_length > 0) {
                if (count < C_SIGNATURE_PARAMETERS && segment_length < sizeof(output->parameter_text[0])) {
                    memcpy(output->parameter_text[count], segment, segment_length);
                    output->parameter_text[count][segment_length] = '\0';
                    output->parameter_class[count] = classify_c_return(output->parameter_text[count]);
                    output->parameter_count = count + 1;
                }
                count++;
            }
            seg_start = i + 1;
        }
    }

    *minimum_parameters = count;
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
static bool find_c_function_signature(const char *dump, const char *name, CFunctionSignature *output) {
    size_t name_length = strlen(name);
    const char *signature_start = NULL;
    const char *signature_end = NULL;

    const char *cursor = dump;
    const char *match;
    while ((match = strstr(cursor, name)) != NULL) {
        cursor = match + name_length;

        bool is_left_boundary = (match == dump) ||
            !(isalnum((unsigned char)match[-1]) || match[-1] == '_');
        bool is_right_boundary = !(isalnum((unsigned char)*cursor) || *cursor == '_');
        if (!is_left_boundary || !is_right_boundary) continue;

        const char *after = cursor;
        if (*after != ' ') continue;
        after++;
        if (*after != '\'') continue;

        const char *end_quote = strchr(after + 1, '\'');
        if (!end_quote) continue;

        const char *line_start = match;
        while (line_start > dump && line_start[-1] != '\n') line_start--;
        if (!range_contains(line_start, match, "FunctionDecl")) continue;

        signature_start = after + 1; /* last match wins */
        signature_end = end_quote;
    }
    if (!signature_start) return false;

    char signature[512];
    size_t signature_length = (size_t)(signature_end - signature_start);
    if (signature_length >= sizeof(signature)) return false; /* implausibly long; skip rather than guess */
    memcpy(signature, signature_start, signature_length);
    signature[signature_length] = '\0';

    if (!count_c_parameters(signature, output)) return false;
    /* A return type spelled as a typedef name classifies as unknown until the
     * typedef itself is looked up. */
    if (output->return_class == C_RETURN_UNKNOWN && output->return_text[0])
        output->return_class = classify_c_typedef(dump, output->return_text);
    for (int i = 0; i < output->parameter_count; i++) {
        if (output->parameter_class[i] == C_RETURN_UNKNOWN)
            output->parameter_class[i] = classify_c_typedef(dump, output->parameter_text[i]);
    }
    return true;
}

/* gcc quotes symbols with curly quotes in a UTF-8 locale ("\xe2\x80\x98name\xe2\x80\x99");
 * clang, and gcc in the C locale, use ASCII ones. Folds the curly form to ASCII
 * in place so message matching sees one spelling whatever locale the user has. */
static void normalize_c_quotes(char *text) {
    char *output = text;
    for (const unsigned char *input = (const unsigned char *)text; *input;) {
        if (input[0] == 0xE2 && input[1] == 0x80 && (input[2] == 0x98 || input[2] == 0x99)) {
            *output++ = '\'';
            input += 3;
        } else {
            *output++ = (char)*input++;
        }
    }
    *output = '\0';
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
                                     char *output, size_t out_size) {
    size_t used = strlen(output);
#define PROBE_APPEND(text) do { \
        size_t written_length = strlen(text); \
        if (used + written_length < out_size) { memcpy(output + used, (text), written_length); used += written_length; output[used] = '\0'; } \
    } while (0)
    PROBE_APPEND("static void _gray_extern_probe(void) {\n");
    for (int i = 0; i < call_count; i++) {
        char line[256];
        if (calls[i].is_call) {
            char argument_list[160] = "";
            size_t argument_list_length = 0;
            for (int parameter_index = 0; parameter_index < calls[i].argument_count && argument_list_length + 3 < sizeof(argument_list); parameter_index++) {
                const char *piece = (parameter_index == 0) ? "0" : ", 0";
                size_t parameter_list_length = strlen(piece);
                memcpy(argument_list + argument_list_length, piece, parameter_list_length); argument_list_length += parameter_list_length; argument_list[argument_list_length] = '\0';
            }
            snprintf(line, sizeof(line), "    (void)(%s(%s));\n", calls[i].function_name, argument_list);
        } else {
            snprintf(line, sizeof(line), "    (void)(%s);\n", calls[i].function_name);
        }
        PROBE_APPEND(line);
    }
    PROBE_APPEND("}\n");
#undef PROBE_APPEND
}

/* Compiles a stub that includes only `headers` with -fsyntax-only. Returns
 * true when the compiler rejects it; `err` receives the compiler's stderr. */
static bool c_headers_fail_to_compile(AstNode *program, Arena *arena, const char *c_compiler_command,
                                      bool cc_is_command, const char *entry_file,
                                      const ImportItem *const *headers, int count,
                                      char *error_output, size_t error_size) {
    error_output[0] = '\0';
    char source[4096];
    append_c_header_includes(headers, count, entry_file, source, sizeof(source));

    char stub[PATH_BUFFER_SIZE];
    int path_length = gray_temporary_path(stub, sizeof(stub), "gray_hdrconflict_", ".c");
    if (path_length < 0 || (size_t)path_length >= sizeof(stub)) return false;
    if (!write_file(stub, source)) { gray_remove_file(stub); return false; }

    FILE *capture = gray_tmpfile();
    if (!capture) { gray_remove_file(stub); return false; }

    ArgumentVector arguments = {0};
    if (cc_is_command) argument_vector_push_command(&arguments, arena, c_compiler_command);
    else argument_vector_push(&arguments, c_compiler_command);
    argument_vector_push(&arguments, "-fsyntax-only");
    add_local_c_header_dirs(&arguments, arena, program, entry_file);
    argument_vector_push(&arguments, "-x");
    argument_vector_push(&arguments, "c");
    argument_vector_push(&arguments, stub);
    argument_vector_end(&arguments);

    bool failed = !arguments.has_overflowed && gray_spawn_capture_stderr(arguments.values, capture) != 0;
    gray_remove_file(stub);

    long length = ftell(capture);
    if (failed && length > 0) {
        rewind(capture);
        size_t bytes_read = fread(error_output, 1, error_size - 1 < (size_t)length ? error_size - 1 : (size_t)length, capture);
        error_output[bytes_read] = '\0';
        normalize_c_quotes(error_output);
    }
    fclose(capture);
    return failed;
}

/* Copies the quoted name following `marker` in `text` (clang's
 * "conflicting types for 'name'") into `out`. False when absent. */
static bool extract_quoted_after(const char *text, const char *marker, char *output, size_t out_size) {
    const char *cursor = strstr(text, marker);
    if (!cursor) return false;
    cursor += strlen(marker);
    if (*cursor != '\'') return false;
    cursor++;
    const char *end_cursor = strchr(cursor, '\'');
    if (!end_cursor || (size_t)(end_cursor - cursor) >= out_size) return false;
    memcpy(output, cursor, (size_t)(end_cursor - cursor));
    output[end_cursor - cursor] = '\0';
    return true;
}

/* Every `extern import` in the program, from every file, is #included into one
 * generated C translation unit, so two headers that declare the same C symbol
 * differently cannot both be imported even from different files. Detects that
 * by compiling the headers together, then narrows to the offending pair so the
 * diagnostic names both headers and the symbol. Fails open — a header that
 * does not compile on its own, or a compiler error this does not recognise,
 * is left for the real compile. Returns true when a conflict was reported. */
static bool report_c_header_conflicts(AstNode *program, DiagnosticList *diagnostics, Arena *arena,
                                      const char *c_compiler_command, bool cc_is_command,
                                      const char *entry_file) {
    const ImportItem *headers[MAX_C_COMPILER_ARGUMENTS];
    int count = collect_distinct_c_headers(program, entry_file, headers, MAX_C_COMPILER_ARGUMENTS);
    if (count < 2) return false;

    char error_output[8192];
    if (!c_headers_fail_to_compile(program, arena, c_compiler_command, cc_is_command, entry_file,
                                   headers, count, error_output, sizeof(error_output)))
        return false;

    for (int i = 0; i < count; i++)
        if (c_headers_fail_to_compile(program, arena, c_compiler_command, cc_is_command, entry_file,
                                      &headers[i], 1, error_output, sizeof(error_output)))
            return false;

    for (int i = 1; i < count; i++) {
        for (int j = 0; j < i; j++) {
            const ImportItem *pair[2] = { headers[j], headers[i] };
            if (!c_headers_fail_to_compile(program, arena, c_compiler_command, cc_is_command, entry_file,
                                           pair, 2, error_output, sizeof(error_output)))
                continue;

            char symbol[256];
            if (!extract_quoted_after(error_output, "conflicting types for ", symbol, sizeof(symbol)) &&
                !extract_quoted_after(error_output, "redefinition of ", symbol, sizeof(symbol)))
                return false;

            const ImportItem *later = headers[i];
            const ImportItem *earlier = headers[j];
            const char *earlier_file = earlier->token.file ? earlier->token.file : entry_file;
            char help[PATH_BUFFER_SIZE + 256];
            snprintf(help, sizeof(help),
                "every 'extern import' in the program shares one C namespace; '%s' is "
                "imported in %s. Import only one of the two headers, or rename the symbol "
                "in one of them.", earlier->path, earlier_file);
            diagnostic_error_code_formatted_help(diagnostics, "E6016",
                later->token.file ? later->token.file : entry_file,
                later->token.line, later->token.column, 0,
                arena_copy_string(arena, help), later->path, earlier->path, symbol);
            return true;
        }
    }
    return false;
}

/* Asks a clang-compatible compiler for its AST dump of a stub that only
 * includes the headers. Returns the dump as a malloc'd string, or NULL when
 * the compiler rejects the flags (gcc) or produces nothing. */
static char *capture_clang_ast_dump(AstNode *program, Arena *arena, const char *c_compiler_command,
                                    bool cc_is_command, const char *entry_file,
                                    const char *includes) {
    char stub[PATH_BUFFER_SIZE];
    int path_length = gray_temporary_path(stub, sizeof(stub), "gray_sigprobe_", ".c");
    if (path_length < 0 || (size_t)path_length >= sizeof(stub)) return NULL;
    if (!write_file(stub, includes)) { gray_remove_file(stub); return NULL; }

    FILE *capture = gray_tmpfile();
    if (!capture) { gray_remove_file(stub); return NULL; }

    ArgumentVector arguments = {0};
    if (cc_is_command) argument_vector_push_command(&arguments, arena, c_compiler_command);
    else argument_vector_push(&arguments, c_compiler_command);
    argument_vector_push(&arguments, "-Xclang");
    argument_vector_push(&arguments, "-ast-dump");
    argument_vector_push(&arguments, "-fsyntax-only");
    add_local_c_header_dirs(&arguments, arena, program, entry_file);
    argument_vector_push(&arguments, "-x");
    argument_vector_push(&arguments, "c");
    argument_vector_push(&arguments, stub);
    argument_vector_end(&arguments);

    bool spawned = !arguments.has_overflowed && gray_spawn_capture_stdout(arguments.values, capture) == 0;
    gray_remove_file(stub);
    if (!spawned) { fclose(capture); return NULL; }

    long dump_length = ftell(capture);
    if (dump_length <= 0) { fclose(capture); return NULL; }
    rewind(capture);
    char *dump = malloc((size_t)dump_length + 1);
    if (!dump) { fclose(capture); return NULL; }
    size_t bytes_read = fread(dump, 1, (size_t)dump_length, capture);
    dump[bytes_read] = '\0';
    fclose(capture);
    return dump;
}

/* Compiles `src` as C with -fsyntax-only, the program's local header dirs and
 * `flags` (NULL-terminated). Returns the compiler's exit code, or -1 when it
 * could not be run. `err_out`, when non-NULL, receives the compiler's stderr as
 * a malloc'd string, or NULL when there is none. */
static int run_c_probe(AstNode *program, Arena *arena, const char *c_compiler_command, bool cc_is_command,
                       const char *entry_file, const char *source, const char *const *flags,
                       char **out_error) {
    if (out_error) *out_error = NULL;
    char stub[PATH_BUFFER_SIZE];
    int path_length = gray_temporary_path(stub, sizeof(stub), "gray_sigprobe_", ".c");
    if (path_length < 0 || (size_t)path_length >= sizeof(stub)) return -1;
    if (!write_file(stub, source)) { gray_remove_file(stub); return -1; }

    FILE *capture = gray_tmpfile();
    if (!capture) { gray_remove_file(stub); return -1; }

    ArgumentVector arguments = {0};
    if (cc_is_command) argument_vector_push_command(&arguments, arena, c_compiler_command);
    else argument_vector_push(&arguments, c_compiler_command);
    argument_vector_push(&arguments, "-fsyntax-only");
    for (int i = 0; flags[i]; i++) argument_vector_push(&arguments, flags[i]);
    add_local_c_header_dirs(&arguments, arena, program, entry_file);
    argument_vector_push(&arguments, "-x");
    argument_vector_push(&arguments, "c");
    argument_vector_push(&arguments, stub);
    argument_vector_end(&arguments);

    int status = arguments.has_overflowed ? -1 : gray_spawn_capture_stderr(arguments.values, capture);
    gray_remove_file(stub);

    long length = ftell(capture);
    if (out_error && length > 0) {
        rewind(capture);
        char *text = malloc((size_t)length + 1);
        if (text) {
            size_t bytes_read = fread(text, 1, (size_t)length, capture);
            text[bytes_read] = '\0';
            normalize_c_quotes(text);
            *out_error = text;
        }
    }
    fclose(capture);
    return status;
}

/* Copies `name`'s prototype out of a gcc `-aux-info` listing (one line per
 * declaration: a file:line comment, then "extern int putc (int, FILE *);") into
 * `out`, spelled as clang's AST dump spells the function's type: the function
 * name dropped, leaving "int (int, FILE *)", "void *(size_t)" or
 * "void (*(int, void (*)(int)))(int)". The last declaration wins, as in
 * find_c_function_signature. */
static bool find_auxiliary_info_signature(const char *auxiliary_info, const char *name, char *output, size_t out_size) {
    size_t name_length = strlen(name);
    bool found = false;
    const char *cursor = auxiliary_info;
    const char *match;
    while ((match = strstr(cursor, name)) != NULL) {
        cursor = match + name_length;
        if (match > auxiliary_info && (isalnum((unsigned char)match[-1]) || match[-1] == '_')) continue;
        if (cursor[0] != ' ' || cursor[1] != '(') continue;

        const char *line_start = match;
        while (line_start > auxiliary_info && line_start[-1] != '\n') line_start--;
        const char *declaration = strstr(line_start, "*/ ");
        if (!declaration || declaration >= match) continue;
        declaration += 3;
        for (bool more = true; more;) {
            more = false;
            static const char *const storage[] = { "extern ", "static ", "inline " };
            for (size_t storage_index = 0; storage_index < sizeof(storage) / sizeof(storage[0]); storage_index++) {
                size_t storage_length = strlen(storage[storage_index]);
                if (strncmp(declaration, storage[storage_index], storage_length) == 0) { declaration += storage_length; more = true; }
            }
        }

        const char *end_cursor = strchr(cursor, '\n');
        if (!end_cursor) end_cursor = cursor + strlen(cursor);
        if (end_cursor[-1] != ';') continue;
        end_cursor--;

        size_t head = (size_t)(match - declaration);
        size_t tail = (size_t)(end_cursor - (cursor + 1));
        if (head + tail >= out_size) continue;
        memcpy(output, declaration, head);
        memcpy(output + head, cursor + 1, tail);
        output[head + tail] = '\0';
        found = true;
    }
    return found;
}

/* The `__builtin_classify_type` result for each kind of C type a typedef name
 * can stand for, paired with a spelling classify_c_return recognises. */
static const struct { int type_class; const char *spelling; } C_TYPE_CLASSES[] = {
    { 0, "void" }, { 1, "long" }, { 3, "int" }, { 4, "_Bool" },
    { 5, "void *" }, { 8, "double" }, { 12, "struct probed" }, { 13, "union probed" },
};
#define C_TYPE_CLASS_COUNT ((int)(sizeof(C_TYPE_CLASSES) / sizeof(C_TYPE_CLASSES[0])))
#define C_TYPEDEF_PROBE_MAX 64

/* Records `text` in `names` when it is a bare identifier classify_c_return could
 * not place — a typedef name still to be resolved. */
static void note_unresolved_typedef(char (*names)[64], int *count, CReturnClass return_class, const char *text) {
    if (return_class != C_RETURN_UNKNOWN || !text[0] || *count >= C_TYPEDEF_PROBE_MAX) return;
    for (const char *cursor = text; *cursor; cursor++)
        if (!isalnum((unsigned char)*cursor) && *cursor != '_') return;
    for (int i = 0; i < *count; i++)
        if (strcmp(names[i], text) == 0) return;
    snprintf(names[(*count)++], 64, "%s", text);
}

/* gcc has no AST dump, so this builds a stand-in that find_c_function_signature
 * reads unchanged: a "FunctionDecl name 'type'" line per called function, from
 * gcc's `-aux-info` prototypes, and a "TypedefDecl name 'type'" line per
 * typedef name those types mention. aux-info leaves typedefs unresolved, so
 * each is classified by asking the compiler which `__builtin_classify_type`
 * value it has: the one _Static_assert that fails names it. Returns a malloc'd
 * string, or NULL when the compiler has no -aux-info. */
static char *synthesize_gcc_ast_dump(AstNode *program, Arena *arena, const char *c_compiler_command,
                                     bool cc_is_command, const char *entry_file,
                                     const char *includes, const ExternCallSite *calls,
                                     int call_count) {
    char auxiliary_path[PATH_BUFFER_SIZE];
    int auxiliary_path_length = gray_temporary_path(auxiliary_path, sizeof(auxiliary_path), "gray_auxinfo_", ".txt");
    if (auxiliary_path_length < 0 || (size_t)auxiliary_path_length >= sizeof(auxiliary_path)) return NULL;
    const char *auxiliary_flags[] = { "-aux-info", auxiliary_path, NULL };
    int status = run_c_probe(program, arena, c_compiler_command, cc_is_command, entry_file, includes,
                             auxiliary_flags, NULL);
    char *auxiliary_info = status == 0 ? gray_read_file(auxiliary_path, false) : NULL;
    gray_remove_file(auxiliary_path);
    if (!auxiliary_info) return NULL;

    StringBuffer dump = buffer_create(4096);
    char typedef_names[C_TYPEDEF_PROBE_MAX][64];
    int typedef_count = 0;
    for (int i = 0; i < call_count; i++) {
        bool seen = false;
        for (int j = 0; j < i && !seen; j++)
            seen = strcmp(calls[j].function_name, calls[i].function_name) == 0;
        char signature[512];
        if (seen || !find_auxiliary_info_signature(auxiliary_info, calls[i].function_name, signature, sizeof(signature))) continue;
        append_format_to_buffer(&dump, "FunctionDecl %s '%s'\n", calls[i].function_name, signature);

        CFunctionSignature parsed;
        if (!count_c_parameters(signature, &parsed)) continue;
        note_unresolved_typedef(typedef_names, &typedef_count, parsed.return_class, parsed.return_text);
        for (int index = 0; index < parsed.parameter_count; index++)
            note_unresolved_typedef(typedef_names, &typedef_count, parsed.parameter_class[index],
                                    parsed.parameter_text[index]);
    }
    free(auxiliary_info);

    if (typedef_count > 0) {
        StringBuffer probe = buffer_create(4096);
        append_string_to_buffer(&probe, includes);
        for (int index = 0; index < typedef_count; index++)
            for (int character = 0; character < C_TYPE_CLASS_COUNT; character++)
                append_format_to_buffer(&probe,
                    "_Static_assert(__builtin_classify_type(*(%s *)0) != %d, \"gray_td_%d_%d_\");\n",
                    typedef_names[index], C_TYPE_CLASSES[character].type_class, index, character);

        const char *no_flags[] = { NULL };
        char *error_output = NULL;
        run_c_probe(program, arena, c_compiler_command, cc_is_command, entry_file, probe.data, no_flags, &error_output);
        buffer_destroy(&probe);
        for (int index = 0; error_output && index < typedef_count; index++) {
            for (int character = 0; character < C_TYPE_CLASS_COUNT; character++) {
                char marker[32];
                snprintf(marker, sizeof(marker), "gray_td_%d_%d_", index, character);
                if (!strstr(error_output, marker)) continue;
                append_format_to_buffer(&dump, "TypedefDecl %s '%s'\n", typedef_names[index],
                                        C_TYPE_CLASSES[character].spelling);
                break;
            }
        }
        free(error_output);
    }
    return dump.data;
}

/* First reports any two imported C headers that declare the same symbol
 * differently (E6016). Then validates every extern.func(...) call and
 * extern.CONST access the type
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
                                         DiagnosticList *diagnostics, Arena *arena,
                                         const char *c_compiler_command, bool cc_is_command,
                                         const char *entry_file) {
    if (report_c_header_conflicts(program, diagnostics, arena, c_compiler_command, cc_is_command, entry_file))
        return;

    int call_count = 0;
    const ExternCallSite *calls = typechecker_get_extern_calls(checker, &call_count);
    if (call_count == 0) return;
    TypeTable *type_table = typechecker_get_table(checker);

    const ImportItem *headers[MAX_C_COMPILER_ARGUMENTS];
    int header_count = collect_distinct_c_headers(program, entry_file, headers, MAX_C_COMPILER_ARGUMENTS);
    char includes[4096];
    append_c_header_includes(headers, header_count, entry_file, includes, sizeof(includes));
    if (!includes[0]) return;

    /* Check 1: does each referenced symbol exist at all? */
    {
        char probe_source[16384];
        snprintf(probe_source, sizeof(probe_source), "%s", includes);
        append_extern_probe_body(calls, call_count, probe_source, sizeof(probe_source));

        char probe_stub[PATH_BUFFER_SIZE];
        probe_stub[0] = '\0';
        int probe_path_length = gray_temporary_path(probe_stub, sizeof(probe_stub), "gray_existprobe_", ".c");
        if (probe_path_length >= 0 && (size_t)probe_path_length < sizeof(probe_stub) && write_file(probe_stub, probe_source)) {
            FILE *perr = gray_tmpfile();
            if (perr) {
                ArgumentVector probe_arguments = {0};
                if (cc_is_command) argument_vector_push_command(&probe_arguments, arena, c_compiler_command);
                else argument_vector_push(&probe_arguments, c_compiler_command);
                argument_vector_push(&probe_arguments, "-fsyntax-only");
                add_local_c_header_dirs(&probe_arguments, arena, program, entry_file);
                argument_vector_push(&probe_arguments, "-x");
                argument_vector_push(&probe_arguments, "c");
                argument_vector_push(&probe_arguments, probe_stub);
                argument_vector_end(&probe_arguments);

                if (!probe_arguments.has_overflowed) gray_spawn_capture_stderr(probe_arguments.values, perr);
                long error_length = ftell(perr);
                if (error_length > 0) {
                    rewind(perr);
                    char *errtext = malloc((size_t)error_length + 1);
                    if (errtext) {
                        size_t bytes_read = fread(errtext, 1, (size_t)error_length, perr);
                        errtext[bytes_read] = '\0';
                        normalize_c_quotes(errtext);
                        for (int i = 0; i < call_count; i++) {
                            if (c_symbol_flagged_undeclared(errtext, calls[i].function_name)) {
                                diagnostic_error_code_formatted(diagnostics, "E5052",
                                    calls[i].file ? calls[i].file : entry_file,
                                    calls[i].line, calls[i].column, 0,
                                    calls[i].function_name);
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

    char *dump = capture_clang_ast_dump(program, arena, c_compiler_command, cc_is_command, entry_file, includes);
    if (!dump)
        dump = synthesize_gcc_ast_dump(program, arena, c_compiler_command, cc_is_command, entry_file, includes,
                                       calls, call_count);
    if (!dump) return;

    for (int i = 0; i < call_count; i++) {
        CFunctionSignature signature;
        if (!find_c_function_signature(dump, calls[i].function_name, &signature))
            continue;

        int actual = calls[i].argument_count;
        bool is_valid = signature.is_variadic ? (actual >= signature.minimum_parameters) : (actual == signature.minimum_parameters);
        if (!is_valid) {
            char expected[32];
            if (signature.is_variadic) snprintf(expected, sizeof(expected), "at least %d", signature.minimum_parameters);
            else snprintf(expected, sizeof(expected), "%d", signature.minimum_parameters);
            diagnostic_error_code_formatted(diagnostics, "E5050",
                calls[i].file ? calls[i].file : entry_file,
                calls[i].line, calls[i].column, 0,
                calls[i].function_name, expected, actual);
        }

        if (is_valid && calls[i].node && calls[i].node->kind == NODE_CALL_EXPRESSION && type_table) {
            for (int argument_index = 0; argument_index < actual && argument_index < signature.parameter_count; argument_index++) {
                AstNode *argument = calls[i].node->data.call.arguments[argument_index];
                GrayType *argument_type = type_table_get(type_table, argument);
                if (!argument_type || !c_argument_kind_mismatch(signature.parameter_class[argument_index], argument_type)) continue;
                diagnostic_error_code_formatted(diagnostics, "E5054",
                    argument->token.file ? argument->token.file : (calls[i].file ? calls[i].file : entry_file),
                    argument->token.line, argument->token.column, 0,
                    argument_index + 1, calls[i].function_name, type_name(argument_type), signature.parameter_text[argument_index]);
            }
        }

        if (calls[i].asserted_type &&
            !c_return_fits(signature.return_class, calls[i].asserted_type, calls[i].is_asserted_via_cast)) {
            const char *help = signature.return_class == C_RETURN_VOID
                ? "this C function returns nothing; call it as a statement"
                : c_return_fits(signature.return_class, calls[i].asserted_type, true)
                    ? "declare the result with a type of the same kind, or convert it explicitly with cast()"
                    : "declare the result with a type of the same kind as the C return type";
            diagnostic_error_code_formatted_help(diagnostics, "E5053",
                calls[i].file ? calls[i].file : entry_file,
                calls[i].line, calls[i].column, 0, help,
                calls[i].function_name, signature.return_text, type_name(calls[i].asserted_type));
        }
    }

    free(dump);
}

/* Command-line configuration, filled by parse_arguments() and read-only after. */
typedef struct {
    const char *input_file;
    const char *output_file;
    const char *optimization_level;
    const char *c_compiler_override;
    const char *quiet_codes_argument;  /* comma-separated W-codes from -q, or NULL */
    size_t arena_limit;           /* 0 = let codegen use its 1 GB default */
    bool should_emit_c_only;
    bool is_check_only;
    bool is_run_mode;
    bool is_format_mode;
    bool is_test_mode;               /* --test: emit a test runner instead of calling main() */
    bool is_verbose;
    bool should_show_time;
    bool is_color_disabled;
    bool should_emit_debug_symbols;
    bool is_quiet_all;
} CompilerOptions;

typedef enum {
    ARGUMENTS_OK,     /* options parsed; carry on compiling */
    ARGUMENTS_DONE,   /* the argument was the whole request (version, help); exit 0 */
    ARGUMENTS_ERROR,  /* the arguments were unusable; exit 1 */
} ArgumentsStatus;

static ArgumentsStatus parse_arguments(int argc, char **argv, CompilerOptions *options) {
    *options = (CompilerOptions){ .optimization_level = "-O2" };

    if (argc < 2) {
        print_usage();
        return ARGUMENTS_ERROR;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "version") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("gray %s\n", GRAY_VERSION);
            return ARGUMENTS_DONE;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return ARGUMENTS_DONE;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            options->output_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-c") == 0) {
            options->should_emit_c_only = true;
            continue;
        }
        if (strcmp(argv[i], "-O0") == 0) { options->optimization_level = "-O0"; continue; }
        if (strcmp(argv[i], "-O1") == 0) { options->optimization_level = "-O1"; continue; }
        if (strcmp(argv[i], "-O2") == 0) { options->optimization_level = "-O2"; continue; }
        if (strcmp(argv[i], "-O3") == 0) { options->optimization_level = "-O3"; continue; }
        if (strcmp(argv[i], "-g") == 0) {
            options->should_emit_debug_symbols = true;
            continue;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--opts->verbose") == 0) {
            options->is_verbose = true;
            continue;
        }
        if (strcmp(argv[i], "--time") == 0) {
            options->should_show_time = true;
            continue;
        }
        if (strcmp(argv[i], "--no-color") == 0) {
            options->is_color_disabled = true;
            continue;
        }
        if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            /* --quiet / -q with optional next argument for specific codes */
            if (i + 1 < argc && argv[i + 1][0] == 'W') {
                options->quiet_codes_argument = argv[++i];
            } else if (i + 1 < argc && argv[i + 1][0] == 'E') {
                fprintf(stderr, "gray: '-q' only accepts warning codes (W-prefixed), not error code '%s'\n", argv[i + 1]);
                return ARGUMENTS_ERROR;
            } else {
                options->is_quiet_all = true;
            }
            continue;
        }
        /* Subcommands */
        if (strcmp(argv[i], "check") == 0 && !options->input_file) {
            options->is_check_only = true;
            continue;
        }
        if (strcmp(argv[i], "build") == 0 && !options->input_file) {
            /* build is the default — just skip the keyword */
            continue;
        }
        if (strcmp(argv[i], "run") == 0 && !options->input_file) {
            options->is_run_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--fmt") == 0) {
            options->is_format_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--test") == 0) {
            options->is_test_mode = true;
            continue;
        }
        if (strncmp(argv[i], "--arena-limit=", 14) == 0) {
            options->arena_limit = strtoull(argv[i] + 14, NULL, 10);
            continue;
        }
        if (strcmp(argv[i], "--cc") == 0 && i + 1 < argc) {
            options->c_compiler_override = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "gray: unknown option '%s'\n", argv[i]);
            return ARGUMENTS_ERROR;
        }
        options->input_file = argv[i];
    }

    if (!options->input_file) {
        fprintf(stderr, "gray: no input file\n");
        return ARGUMENTS_ERROR;
    }
    return ARGUMENTS_OK;
}

int main(int argc, char **argv) {
    /* Windows consoles need to be opted into ANSI escape handling before any
     * colored diagnostic is written. No-op everywhere else. */
    gray_enable_virtual_terminal_mode();

    CompilerOptions options;
    switch (parse_arguments(argc, argv, &options)) {
    case ARGUMENTS_DONE:  return 0;
    case ARGUMENTS_ERROR: return 1;
    case ARGUMENTS_OK:    break;
    }

    /* Read source file */
    char *source = gray_read_file(options.input_file, true);
    if (!source) return 1;

    /* fmt mode: reformat and write back, then exit */
    if (options.is_format_mode) {
        FILE *temporary_file = gray_tmpfile();
        if (!temporary_file) {
            fprintf(stderr, "gray: fmt: could not create temp file\n");
            free(source);
            return 1;
        }
        int exit_code = gray_fmt_source(source, options.input_file, temporary_file);
        if (exit_code != 0) {
            fprintf(stderr, "gray: fmt: failed to format '%s'\n", options.input_file);
            fclose(temporary_file);
            free(source);
            return 1;
        }
        /* Read formatted output back */
        long format_length = ftell(temporary_file);
        rewind(temporary_file);
        char *format_buffer = malloc(format_length + 1);
        if (!format_buffer || (long)fread(format_buffer, 1, format_length, temporary_file) != format_length) {
            fprintf(stderr, "gray: fmt: failed to read formatted output\n");
            fclose(temporary_file);
            free(source);
            return 1;
        }
        format_buffer[format_length] = '\0';
        fclose(temporary_file);
        /* Write back to the original file with explicit 0644 permissions */
        if (!gray_write_file_mode(options.input_file, format_buffer, (size_t)format_length)) {
            fprintf(stderr, "gray: fmt: cannot write '%s'\n", options.input_file);
            free(format_buffer);
            free(source);
            return 1;
        }
        free(format_buffer);
        free(source);
        return 0;
    }

    /* Create compiler arena and diagnostics */
    Arena *arena = arena_create(COMPILER_ARENA_SIZE);
    DiagnosticList *diagnostics = diagnostic_create();
    diagnostic_set_source(diagnostics, options.input_file, source);
    if (options.is_color_disabled) diagnostics->should_use_color = false;

    /* Configure warning suppression */
    if (options.is_quiet_all) {
        diagnostics->should_suppress_all_warnings = true;
    } else if (options.quiet_codes_argument) {
        /* Parse comma-separated warning codes */
        char *codes_buffer = strdup(options.quiet_codes_argument);
        int code_capacity = 8;
        diagnostics->suppressed_codes = malloc(sizeof(const char *) * code_capacity);
        diagnostics->suppressed_count = 0;
        char *code_token = strtok(codes_buffer, ",");
        while (code_token) {
            /* Validate: must start with W */
            if (code_token[0] == 'E') {
                fprintf(stderr, "gray: '-q' only accepts warning codes (W-prefixed), not error code '%s'\n", code_token);
                free(codes_buffer);
                return 1;
            }
            if (code_token[0] != 'W') {
                fprintf(stderr, "gray: unknown warning code '%s'\n", code_token);
                free(codes_buffer);
                return 1;
            }
            if (diagnostics->suppressed_count >= code_capacity) {
                code_capacity *= 2;
                void *temporary_file = realloc(diagnostics->suppressed_codes, sizeof(const char *) * code_capacity);
                if (!temporary_file) {
                    fprintf(stderr, "gray: out of memory\n");
                    free(codes_buffer);
                    return 1;
                }
                diagnostics->suppressed_codes = temporary_file;
            }
            diagnostics->suppressed_codes[diagnostics->suppressed_count++] = strdup(code_token);
            code_token = strtok(NULL, ",");
        }
        free(codes_buffer);
    }

    double start_time = monotonic_milliseconds();

    /* Lex */
    Lexer *lexer = lexer_create(arena, source, options.input_file);

    /* Parse */
    Parser *parser = parser_create(arena, lexer, options.input_file, diagnostics);
    AstNode *program = parser_parse_program(parser);

    if (diagnostic_has_errors(diagnostics)) {
        diagnostic_print_all(diagnostics);
        diagnostic_print_summary(diagnostics);
        diagnostic_destroy(diagnostics);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Parse every imported .gray file and merge its declarations into the
     * program. The file -> module attribution collected along the way is the
     * only record of which module a source file belongs to, so it is handed
     * to the type checker below. */
    ImportResolution imports;
    imports_resolve(arena, diagnostics, program, options.input_file, &imports);


    /* An import that failed to resolve merged no declarations, so every
     * reference to the module it named is about to be reported undefined.
     * Those follow-on errors bury the one that matters and all disappear
     * when it is fixed, so stop here and report the import failure alone. */
    if (diagnostic_has_errors(diagnostics)) {
        diagnostic_print_all(diagnostics);
        diagnostic_print_summary(diagnostics);
        diagnostic_destroy(diagnostics);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Type check */
    TypeChecker *checker = typechecker_create(diagnostics, options.input_file);
    typechecker_set_test_mode(checker, options.is_test_mode);
    typechecker_add_file_module(checker, options.input_file, NULL, true);
    for (int i = 0; i < imports.count; i++)
        typechecker_add_file_module(checker, imports.files[i], imports.modules[i], false);
    for (int i = 0; i < imports.alias_count; i++)
        typechecker_add_module_alias(checker, imports.alias_names[i], imports.alias_targets[i]);
    typechecker_check(checker, program);

    if (diagnostic_has_errors(diagnostics)) {
        diagnostic_print_all(diagnostics);
        diagnostic_print_summary(diagnostics);
        diagnostic_destroy(diagnostics);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Print warnings even if no errors */
    if (diagnostic_warning_count(diagnostics) > 0 && !diagnostic_has_errors(diagnostics)) {
        diagnostic_print_all(diagnostics);
        diagnostic_print_summary(diagnostics);
    }

    /* Check-only mode: stop after type checking */
    if (options.is_check_only) {
        /* extern.func()/extern.CONST validation needs a C compiler; detect
         * one the same way the full build does, but skip the check entirely
         * when none is found (fails open, same as validate_c_extern_signatures
         * itself) — gray check's fast path never requires a C compiler. */
        const char *check_c_compiler_command = options.c_compiler_override ? options.c_compiler_override : detect_c_compiler();
        if (check_c_compiler_command) {
            validate_c_extern_signatures(program, checker, diagnostics, arena, check_c_compiler_command,
                                         options.c_compiler_override != NULL, options.input_file);
        }

        if (diagnostic_has_errors(diagnostics)) {
            diagnostic_print_all(diagnostics);
            diagnostic_print_summary(diagnostics);
            typechecker_free(checker);
            diagnostic_destroy(diagnostics);
            arena_destroy(arena);
            free(source);
            return 1;
        }

        double end_time = monotonic_milliseconds();
        if (options.should_show_time) {
            double milliseconds = end_time - start_time;
            fprintf(stderr, "gray: check completed in %.1fms\n", milliseconds);
        }
        if (diagnostics->should_use_color)
            fprintf(stderr, "%s%sgray: %s: no errors!%s\n",
                COLOR_BOLD, COLOR_GREEN, options.input_file, COLOR_RESET);
        else
            fprintf(stderr, "gray: %s: no errors!\n", options.input_file);
        typechecker_free(checker);
        diagnostic_destroy(diagnostics);
        arena_destroy(arena);
        free(source);
        return 0;
    }

    /* Generate C code */
    CodeGen codegen = codegen_create(options.input_file);
    codegen.type_table = typechecker_get_table(checker);
    codegen.modules = typechecker_get_modules(checker);
    codegen.arena_limit = options.arena_limit;
    codegen.is_test_mode = options.is_test_mode;
    codegen_generate(&codegen, program);
    const char *c_source = codegen_result(&codegen);
    double frontend_end_time = monotonic_milliseconds();

    /* Determine output name */
    char *default_output = NULL;
    if (options.is_run_mode && !options.output_file) {
        /* Run mode: use temp file */
        default_output = malloc(PATH_BUFFER_SIZE);
        gray_temporary_path(default_output, PATH_BUFFER_SIZE, "gray_run_", GRAY_EXECUTABLE_SUFFIX);
        options.output_file = default_output;
    } else if (!options.output_file) {
        default_output = output_name_from_input(options.input_file);
        options.output_file = default_output;
    }

    /* Write generated C to a temp file. The name carries the output's base name
     * for readability under --options.verbose, plus a pid and counter so concurrent
     * builds in different directories cannot collide. */
    char c_path_prefix[PATH_BUFFER_SIZE];
    snprintf(c_path_prefix, sizeof(c_path_prefix), "gray_%s_", gray_path_basename(options.output_file));
    char c_file_path[PATH_BUFFER_SIZE];
    gray_temporary_path(c_file_path, sizeof(c_file_path), c_path_prefix, ".c");

    if (!write_file(c_file_path, c_source)) {
        codegen_destroy(&codegen);
        typechecker_free(checker);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 1;
    }

    if (options.should_emit_c_only) {
        /* Determine C output filename */
        const char *c_output_path = NULL;
        char *default_c_output_path = NULL;
        if (options.output_file && options.output_file != default_output) {
            /* Explicit -o provided */
            c_output_path = options.output_file;
        } else {
            /* Derive from input: foo.gray -> foo.c */
            const char *base = gray_path_basename(options.input_file);
            size_t base_length = strlen(base);
            if (base_length > GRAY_EXTENSION_LENGTH && strcmp(base + base_length - GRAY_EXTENSION_LENGTH, GRAY_EXTENSION) == 0)
                base_length -= GRAY_EXTENSION_LENGTH;
            default_c_output_path = malloc(base_length + 3);
            memcpy(default_c_output_path, base, base_length);
            memcpy(default_c_output_path + base_length, ".c", 3);
            c_output_path = default_c_output_path;
        }

        if (!write_file(c_output_path, c_source)) {
            fprintf(stderr, "gray: failed to write C output: %s\n", c_output_path);
            free(default_c_output_path);
            codegen_destroy(&codegen);
            typechecker_free(checker);
            arena_destroy(arena);
            free(source);
            free(default_output);
            return 1;
        }
        printf("Generated: %s\n", c_output_path);
        free(default_c_output_path);
        codegen_destroy(&codegen);
        typechecker_free(checker);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 0;
    }


    /* Pick a C compiler (skip detection when --cc overrides) */
    const char *c_compiler_command = options.c_compiler_override;
    if (!c_compiler_command) {
        c_compiler_command = detect_c_compiler();
        if (!c_compiler_command) {
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
    gray_ensure_tool_directory_on_path(c_compiler_command);

    /* Find runtime directory */
    const char *runtime_directory = find_runtime_directory(argv[0]);
    if (!runtime_directory) {
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
    if (strchr(runtime_directory, '"') || strchr(options.output_file, '"') || strchr(c_compiler_command, '"')) {
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
    if (!preflight_c_headers(program, diagnostics, arena, c_compiler_command,
                             options.c_compiler_override != NULL, options.input_file)) {
        gray_remove_file(c_file_path);
        diagnostic_print_all(diagnostics);
        diagnostic_print_summary(diagnostics);
        diagnostic_destroy(diagnostics);
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
    validate_c_extern_signatures(program, checker, diagnostics, arena, c_compiler_command,
                                 options.c_compiler_override != NULL, options.input_file);
    if (diagnostic_has_errors(diagnostics)) {
        gray_remove_file(c_file_path);
        diagnostic_print_all(diagnostics);
        diagnostic_print_summary(diagnostics);
        diagnostic_destroy(diagnostics);
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
    char lib_path[PATH_BUFFER_SIZE];
    bool has_archive = false;

    /* Check for libgrayrt.a next to the runtime dir, then next to the binary.
     * The archive is built for the host; a --cc compiler (a cross target) needs
     * the runtime compiled from source for its own target instead. */
    gray_path_join(lib_path, sizeof(lib_path), runtime_directory, "../libgrayrt.a");
    if (options.c_compiler_override) {
        /* fall through to the from-source build below */
    } else if (gray_file_readable(lib_path)) {
        has_archive = true;
    } else {
        const char *self = gray_self_directory(NULL);
        if (self) {
            gray_path_join(lib_path, sizeof(lib_path), self, "libgrayrt.a");
            if (gray_file_readable(lib_path)) has_archive = true;
        }
    }

    double c_compiler_start_time = monotonic_milliseconds();

    ArgumentVector c_compiler_arguments = {0};
    /* Only --cc values are multi-word commands ("zig cc -target ...").
     * Detected compilers are single tokens that may contain spaces
     * (C:\Program Files\LLVM\bin\clang.exe) and must not be word-split. */
    if (options.c_compiler_override) {
        argument_vector_push_command(&c_compiler_arguments, arena, c_compiler_command);
    } else {
        argument_vector_push(&c_compiler_arguments, c_compiler_command);
    }
#if GRAY_OS_WINDOWS
    /* gnu11, not c11: -std=c11 defines __STRICT_ANSI__ on MinGW-w64, which
     * unbinds printf from the ANSI-conforming implementation (%zu breaks on
     * msvcrt) and hides the POSIX-shaped names in <io.h>. These must match
     * how libgrayrt.a is built (see grayc/Makefile STD_FLAGS). */
    argument_vector_push(&c_compiler_arguments, "-std=gnu11");
    argument_vector_push(&c_compiler_arguments, "-D__USE_MINGW_ANSI_STDIO=1");
    argument_vector_push(&c_compiler_arguments, "-D_WIN32_WINNT=0x0601");
#else
    /* Must match how libgrayrt.a is built (see grayc/Makefile STD_FLAGS).
     * Without _POSIX_C_SOURCE, -std=c11 defines __STRICT_ANSI__, which on
     * glibc hides every POSIX name (realpath, strdup, setenv, fdopen, ...)
     * that extern interop reaches for, and can skew feature-gated
     * declarations in the shared runtime headers against the archive. */
    argument_vector_push(&c_compiler_arguments, "-std=c11");
    argument_vector_push(&c_compiler_arguments, "-D_POSIX_C_SOURCE=200809L");
    /* Darwin's -D_POSIX_C_SOURCE strict mode hides BSD names (u_int, ...) that
     * its own system headers use; vendored sqlite3.c, built from source for a
     * mac target, includes those headers. Inert on glibc. */
    argument_vector_push(&c_compiler_arguments, "-D_DARWIN_C_SOURCE");
#endif
    if (options.should_emit_debug_symbols) argument_vector_push(&c_compiler_arguments, "-g");
    argument_vector_push(&c_compiler_arguments, options.optimization_level);
    /* One section per function/variable so the linker's dead-strip pass (added
     * below) can drop the runtime and stdlib code the program never calls —
     * a trivial program links a fraction of libgrayrt.a instead of all of it.
     * Compile-time cost is negligible; there is no LTO. */
    argument_vector_push(&c_compiler_arguments, "-ffunction-sections");
    argument_vector_push(&c_compiler_arguments, "-fdata-sections");
    /* Marks this translation unit as a grayc-generated program. The stdlib
     * headers whose basename collides with a system header (time.h, io.h,
     * ...) only need to forward to the real header in this context — where
     * their directory is on -isystem and shadows libc — not when they are
     * compiled into libgrayrt.a. */
    argument_vector_push(&c_compiler_arguments, "-DGRAY_GENERATED_C=1");
    argument_vector_push(&c_compiler_arguments, "-Wall");
    argument_vector_push(&c_compiler_arguments, "-Wno-unused-function");
    argument_vector_push(&c_compiler_arguments, "-Wno-unused-variable");
    argument_vector_push(&c_compiler_arguments, "-Wno-unused-but-set-variable");
    argument_vector_push(&c_compiler_arguments, "-Wno-tautological-compare");
    argument_vector_push(&c_compiler_arguments, "-Wno-infinite-recursion");
    argument_vector_push(&c_compiler_arguments, "-Wno-incompatible-pointer-types-discards-qualifiers");
#if GRAY_OS_WINDOWS
    /* GCC's spelling of the Clang-only flag above. */
    argument_vector_push(&c_compiler_arguments, "-Wno-discarded-qualifiers");
#endif
    /* An `extern.` call is emitted with its arguments passed through verbatim —
     * grayc cannot see the C signature to insert a cast. An opaque C handle
     * (FILE*, DIR*, ...) has no Grayscale type to name, so it round-trips as
     * `^u8` (uint8_t*), and a byte buffer passed to a `char*` parameter
     * differs only in signedness. Neither mismatch is expressible away in
     * source. Silence both so C interop compiles clean; on GCC >= 14
     * -Wincompatible-pointer-types is an error by default, so this also keeps
     * it from being a hard build failure. */
    argument_vector_push(&c_compiler_arguments, "-Wno-incompatible-pointer-types");
    argument_vector_push(&c_compiler_arguments, "-Wno-pointer-sign");
    argument_vector_push(&c_compiler_arguments, "-isystem");
    argument_vector_push_formatted(&c_compiler_arguments, arena, "%s" GRAY_PATH_SEPARATOR_STRING "runtime", runtime_directory);
    argument_vector_push(&c_compiler_arguments, "-isystem");
    argument_vector_push_formatted(&c_compiler_arguments, arena, "%s" GRAY_PATH_SEPARATOR_STRING "stdlib", runtime_directory);
    argument_vector_push(&c_compiler_arguments, "-isystem");
    argument_vector_push(&c_compiler_arguments, runtime_directory);
    /* Local C headers ("./x.h") are written relative to the .gray source, not
     * the temp .c handed to the compiler. */
    add_local_c_header_dirs(&c_compiler_arguments, arena, program, options.input_file);
    argument_vector_push(&c_compiler_arguments, "-o");
    argument_vector_push(&c_compiler_arguments, options.output_file);
    argument_vector_push(&c_compiler_arguments, c_file_path);

    if (has_archive) {
        argument_vector_push(&c_compiler_arguments, lib_path);
    } else {
        /* Build source list from all runtime and stdlib .c files. Mirrors
         * RT_SRC in grayc/Makefile; atomic_builtin.c stands in for the
         * per-architecture assembly, which is written for the host. */
        static const char *runtime_srcs[] = {
            "runtime/runtime.c", "runtime/array.c", "runtime/map.c",
            "runtime/test.c", "runtime/atomic_builtin.c",
        };
        static const char *stdlib_srcs[] = {
            "stdlib/arrays.c",   "stdlib/binary.c",   "stdlib/builtins.c",
            "stdlib/chars.c",    "stdlib/channels.c", "stdlib/crypto.c",
            "stdlib/csv.c",      "stdlib/encoding.c", "stdlib/fmt.c",
            "stdlib/http.c",     "stdlib/io.c",       "stdlib/json.c",
            "stdlib/maps.c",     "stdlib/math.c",     "stdlib/mem.c",
            "stdlib/net.c",      "stdlib/os.c",       "stdlib/random.c",
            "stdlib/regex.c",    "stdlib/server.c",   "stdlib/sqlite.c",
            "stdlib/strings.c",  "stdlib/sync.c",     "stdlib/atomic.c",
            "stdlib/threads.c",  "stdlib/runtime_mod.c",
            "stdlib/time.c",     "stdlib/uuid.c",     "stdlib/strconv.c",
            "vendor/sqlite3.c"
        };
        for (size_t i = 0; i < sizeof(runtime_srcs) / sizeof(runtime_srcs[0]); i++) {
            argument_vector_push_formatted(&c_compiler_arguments, arena, "%s" GRAY_PATH_SEPARATOR_STRING "%s", runtime_directory, runtime_srcs[i]);
        }
        /* The vendored SQLite amalgamation is not part of the extracted
         * runtime a release binary carries; without it sqlite.c cannot build. */
        char vendor_probe[PATH_BUFFER_SIZE];
        gray_path_join(vendor_probe, sizeof(vendor_probe), runtime_directory, "vendor/sqlite3.c");
        bool has_vendor = gray_file_readable(vendor_probe);
        for (size_t i = 0; i < sizeof(stdlib_srcs) / sizeof(stdlib_srcs[0]); i++) {
            if (!has_vendor && (strcmp(stdlib_srcs[i], "stdlib/sqlite.c") == 0 ||
                                strcmp(stdlib_srcs[i], "vendor/sqlite3.c") == 0))
                continue;
            argument_vector_push_formatted(&c_compiler_arguments, arena, "%s" GRAY_PATH_SEPARATOR_STRING "%s", runtime_directory, stdlib_srcs[i]);
        }
    }

    /* Drop the sections nothing references (see -ffunction-sections above).
     * Apple ld and GNU ld/lld spell it differently. */
#if defined(__APPLE__)
    argument_vector_push(&c_compiler_arguments, "-Wl,-dead_strip");
#else
    argument_vector_push(&c_compiler_arguments, "-Wl,--gc-sections");
#endif

    /* Platform link flags. */
    argument_vector_push(&c_compiler_arguments, "-lm");
    argument_vector_push(&c_compiler_arguments, "-lpthread");
#if GRAY_OS_WINDOWS
    argument_vector_push(&c_compiler_arguments, "-lws2_32");  /* Winsock, used by net/http/server */
    /* Self-contained exe: winpthread and libgcc link statically so the binary
     * runs without MinGW's bin directory on PATH. System import libraries
     * (kernel32, msvcrt, ws2_32) stay dynamic — those DLLs ship with the OS. */
    argument_vector_push(&c_compiler_arguments, "-static");
#endif
    argument_vector_push(&c_compiler_arguments, "-Wl,-w");
    argument_vector_end(&c_compiler_arguments);

    if (c_compiler_arguments.has_overflowed) {
        fprintf(stderr, "gray: too many arguments to the C compiler\n");
        codegen_destroy(&codegen);
        typechecker_free(checker);
        diagnostic_destroy(diagnostics);
        arena_destroy(arena);
        free(source);
        free(default_output);
        return 1;
    }

    if (options.is_verbose) {
        fprintf(stderr, "gray: ");
        argument_vector_print(&c_compiler_arguments, stderr);
    }

    /* The compiler's own text is captured, not inherited: warnings from a
     * build that succeeded are raw C output the user did not ask for, so they
     * are shown only under --verbose. A failed build still shows everything
     * the compiler said. */
    FILE *c_compiler_errors = gray_tmpfile();
    int compiler_status = c_compiler_errors ? gray_spawn_capture_stderr(c_compiler_arguments.values, c_compiler_errors)
                     : gray_spawn_path(c_compiler_arguments.values);
    if (compiler_status < 0) {
        fprintf(stderr, "gray: could not run the C compiler '%s'\n", c_compiler_arguments.values[0]);
        compiler_status = 1;
    }
    if (c_compiler_errors) {
        if (compiler_status != 0 || options.is_verbose) {
            char c_compiler_buffer[4096];
            size_t bytes_read;
            rewind(c_compiler_errors);
            while ((bytes_read = fread(c_compiler_buffer, 1, sizeof(c_compiler_buffer), c_compiler_errors)) > 0)
                fwrite(c_compiler_buffer, 1, bytes_read, stderr);
        }
        fclose(c_compiler_errors);
    }

    double c_compiler_end_time = monotonic_milliseconds();

    if (compiler_status != 0) {
        fprintf(stderr, "gray: C compilation failed\n");
        bool has_c_import = false;
        for (int statement_index = 0; statement_index < program->data.program.statement_count; statement_index++) {
            AstNode *statement = program->data.program.statements[statement_index];
            if (statement->kind == NODE_IMPORT_STATEMENT) {
                for (int item_index = 0; item_index < statement->data.import_statement.count; item_index++) {
                    if (statement->data.import_statement.items[item_index].is_c_import) {
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
        fprintf(stderr, "gray: generated C source at %s\n", c_file_path);
    } else {
        gray_remove_file(c_file_path);

        double total_ms = c_compiler_end_time - start_time;
        if (!options.is_run_mode) {
            const char *out_base = gray_path_basename(options.output_file);
            if (!options.is_color_disabled && gray_stdout_is_terminal()) {
                fprintf(stdout, "\033[32mCompiled '\033[1m%s\033[22m' in %.0fms!\033[0m\n",
                    out_base, total_ms);
            } else {
                fprintf(stdout, "Compiled '%s' in %.0fms!\n", out_base, total_ms);
            }
            fflush(stdout);
        }

        if (options.should_show_time) {
            double frontend_ms = frontend_end_time - start_time;
            double setup_ms = c_compiler_start_time - frontend_end_time;
            double cc_ms = c_compiler_end_time - c_compiler_start_time;
            fprintf(stderr, "  frontend:  %.1fms (lex + parse + typecheck + codegen)\n", frontend_ms);
            fprintf(stderr, "  setup:     %.1fms (compiler probe + temp write)\n", setup_ms);
            fprintf(stderr, "  cc:        %.1fms (compile + link)\n", cc_ms);
        }
    }

    /* Run mode: execute the binary and clean up. Spawned without a shell and
     * without a PATH search — the output path comes from user-supplied CLI
     * input, and a bare name must not resolve to some unrelated binary. */
    bool ran_program = false;
    if (compiler_status == 0 && options.is_run_mode) {
        const char *run_argument_vector[] = {options.output_file, NULL};
        int term_signal = 0;
        compiler_status = gray_spawn_exact(run_argument_vector, &term_signal);
        if (compiler_status < 0) {
            fprintf(stderr, "gray: cannot execute '%s'\n", options.output_file);
            compiler_status = 1;
        } else if (term_signal) {
            fflush(stdout);
            fprintf(stderr, "gray: program crashed: signal %d (%s)\n",
                    term_signal, strsignal(term_signal));
        }
        ran_program = true;
        gray_remove_file(options.output_file);
    }

    codegen_destroy(&codegen);
    typechecker_free(checker);
    diagnostic_destroy(diagnostics);
    arena_destroy(arena);
    free(source);
    free(default_output);

    /* The program's own status (exit(code), or 128 + signal for a crash) is
     * the process status; a compile-side failure is a flat 1. */
    return ran_program ? compiler_status : (compiler_status != 0 ? 1 : 0);
}
