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

/* Pick the first C compiler that actually runs. The candidate that answers is
 * the one we go on to invoke — probing one name and then invoking a different
 * one breaks on any system that has gcc but no cc, which is every Windows
 * install and plenty of minimal Linux images. */
static bool cc_probe_ok(const char *cc) {
    const char *probe[] = {cc, "--version", NULL};
    return gray_spawn_quiet(probe) == 0;
}

static const char *detect_cc(void) {
    /* GRAY_CC / CC are probed, not trusted: a stale CC=cc from a profile must
     * not break a system that only has gcc. Multi-word values ("zig cc")
     * cannot go through a single-token probe — use --cc for those. */
    static const char *const env_names[] = {"GRAY_CC", "CC"};
    for (size_t i = 0; i < sizeof(env_names) / sizeof(env_names[0]); i++) {
        const char *val = getenv(env_names[i]);
        if (!val || !*val || strpbrk(val, " \t")) continue;
        if (cc_probe_ok(val)) return val;
    }

    static const char *const candidates[] = {
#if GRAY_OS_WINDOWS
        "gcc", "clang", "cc",
#else
        "cc", "gcc", "clang",
#endif
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (cc_probe_ok(candidates[i])) return candidates[i];
    }

    /* Nothing on PATH — check the well-known Windows install locations. */
    return gray_find_cc_fallback();
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

    clock_t t_start = clock();

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
        clock_t t_end = clock();
        if (opts.show_time) {
            double ms = (double)(t_end - t_start) / CLOCKS_PER_SEC * 1000.0;
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

    clock_t t_cc_start = clock();

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
    argv_push(&cc_argv, "-std=c11");
#endif
    if (opts.debug_symbols) argv_push(&cc_argv, "-g");
    argv_push(&cc_argv, opts.opt_level);
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
    argv_push(&cc_argv, "-isystem");
    argv_pushf(&cc_argv, arena, "%s" GRAY_PATH_SEP_STR "runtime", runtime_dir);
    argv_push(&cc_argv, "-isystem");
    argv_pushf(&cc_argv, arena, "%s" GRAY_PATH_SEP_STR "stdlib", runtime_dir);
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

    int ret = gray_spawn_path(cc_argv.v);
    if (ret < 0) {
        fprintf(stderr, "gray: could not run the C compiler '%s'\n", cc_argv.v[0]);
        ret = 1;
    }

    clock_t t_cc_end = clock();

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
            fprintf(stderr, "gray: hint: check that all C headers in import c\"...\" exist and are installed\n");
        }
        fprintf(stderr, "gray: generated C source at %s\n", c_file);
    } else {
        gray_remove_file(c_file);

        double total_ms = (double)(t_cc_end - t_start) / CLOCKS_PER_SEC * 1000.0;
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
            double frontend_ms = (double)(t_cc_start - t_start) / CLOCKS_PER_SEC * 1000.0;
            double cc_ms = (double)(t_cc_end - t_cc_start) / CLOCKS_PER_SEC * 1000.0;
            fprintf(stderr, "  frontend:  %.1fms (lex + parse + typecheck + codegen)\n", frontend_ms);
            fprintf(stderr, "  cc:        %.1fms (compile + link)\n", cc_ms);
        }
    }

    /* Run mode: execute the binary and clean up. Spawned without a shell and
     * without a PATH search — the output path comes from user-supplied CLI
     * input, and a bare name must not resolve to some unrelated binary. */
    if (ret == 0 && opts.run_mode) {
        const char *run_argv[] = {opts.output_file, NULL};
        ret = gray_spawn_exact(run_argv);
        if (ret < 0) {
            fprintf(stderr, "gray: cannot execute '%s'\n", opts.output_file);
            ret = 1;
        }
        gray_remove_file(opts.output_file);
    }

    codegen_destroy(&codegen);
    typechecker_free(checker);
    diagnostic_destroy(diag);
    arena_destroy(arena);
    free(source);
    free(default_output);

    return ret != 0 ? 1 : 0;
}
