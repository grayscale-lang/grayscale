/*
 * imports.c — Implementation of the import resolver declared in imports.h.
 *
 * Holds the import cache that makes resolution terminate on cyclic and
 * diamond-shaped import graphs, the directory scan that expands a directory
 * import into its .gray files, and the two-pass parse/merge that rewrites
 * imported declarations under their module names.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "imports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "parser.h"
#include "../lexer/lexer.h"
#include "../util/constants.h"
#include "../util/platform.h"
#include "../util/xalloc.h"

#define PATH_BUF_SIZE 2048
#define GRAY_EXT      ".gray"
#define GRAY_EXT_LEN  5


/* Import cache: track already-imported files to avoid duplicates and cycles.
 * Open-addressing hash set keyed on canonical file path. */

/* Bucket count is always a power of two, and the table grows to keep the
 * load factor at or below one half. Linear probing needs a free slot to
 * terminate on, so the table must never fill. */
#define IMPORT_HASH_INIT_BUCKETS 512

#define FNV1A_OFFSET_BASIS 2166136261u
#define FNV1A_PRIME        16777619u

typedef struct {
    const char *path;
    const char *mod;
    const char *from;   /* file whose import statement first pulled this path in */
} ImportHashEntry;

static ImportHashEntry *import_hash = NULL;
static uint32_t import_hash_buckets = 0;
static int imported_file_count = 0;

static uint32_t import_path_hash(const char *s) {
    uint32_t h = FNV1A_OFFSET_BASIS;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * FNV1A_PRIME;
    return h;
}

/* Place an entry during a rehash, where the key is known to be unique. */
static void import_hash_place(ImportHashEntry *table, uint32_t buckets,
                              const char *path, const char *mod, const char *from) {
    uint32_t slot = import_path_hash(path) & (buckets - 1);
    for (uint32_t i = slot; ; i = (i + 1) & (buckets - 1)) {
        if (!table[i].path) {
            table[i].path = path;
            table[i].mod = mod;
            table[i].from = from;
            return;
        }
    }
}

/* Ensure room for one more entry at a load factor of one half or less. */
static void import_hash_reserve(void) {
    if (import_hash &&
        (uint32_t)(imported_file_count + 1) * 2 <= import_hash_buckets)
        return;

    uint32_t new_buckets = import_hash_buckets
        ? import_hash_buckets * 2 : IMPORT_HASH_INIT_BUCKETS;
    ImportHashEntry *new_table = xcalloc(new_buckets, sizeof(ImportHashEntry));
    for (uint32_t i = 0; i < import_hash_buckets; i++) {
        if (import_hash[i].path)
            import_hash_place(new_table, new_buckets,
                import_hash[i].path, import_hash[i].mod, import_hash[i].from);
    }
    free(import_hash);
    import_hash = new_table;
    import_hash_buckets = new_buckets;
}

static bool already_imported(const char *path) {
    if (!import_hash) return false;
    uint32_t slot = import_path_hash(path) & (import_hash_buckets - 1);
    for (uint32_t i = slot; ; i = (i + 1) & (import_hash_buckets - 1)) {
        if (!import_hash[i].path) return false;
        if (strcmp(import_hash[i].path, path) == 0) return true;
    }
}

static const char *imported_by_module(const char *path) {
    if (!import_hash) return NULL;
    uint32_t slot = import_path_hash(path) & (import_hash_buckets - 1);
    for (uint32_t i = slot; ; i = (i + 1) & (import_hash_buckets - 1)) {
        if (!import_hash[i].path) return NULL;
        if (strcmp(import_hash[i].path, path) == 0) return import_hash[i].mod;
    }
}

/* The file whose import statement first pulled `path` in, or NULL. Two imports
 * of one target from the same file are a duplicate; from two different files
 * they are a diamond dependency, which is ordinary and silent. */
static const char *imported_by_file(const char *path) {
    if (!import_hash) return NULL;
    uint32_t slot = import_path_hash(path) & (import_hash_buckets - 1);
    for (uint32_t i = slot; ; i = (i + 1) & (import_hash_buckets - 1)) {
        if (!import_hash[i].path) return NULL;
        if (strcmp(import_hash[i].path, path) == 0) return import_hash[i].from;
    }
}

static void mark_imported_from(const char *path, const char *mod, const char *from) {
    import_hash_reserve();
    uint32_t slot = import_path_hash(path) & (import_hash_buckets - 1);
    for (uint32_t i = slot; ; i = (i + 1) & (import_hash_buckets - 1)) {
        if (!import_hash[i].path) {
            import_hash[i].path = path;
            import_hash[i].mod = mod;
            import_hash[i].from = from;
            imported_file_count++;
            return;
        }
        if (strcmp(import_hash[i].path, path) == 0) return;
    }
}

/* Do two import statements sit in the same file? The entry file's statements
 * carry a NULL token file, which the caller has already resolved to the input
 * path, so a plain string compare is enough. */
static bool same_import_file(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static void mark_imported(const char *path) {
    mark_imported_from(path, NULL, NULL);
}

/* Scan a directory for .gray files. Returns count of files found.
 * Fills paths[] with full file paths (dir_path + "/" + filename). */
static int gray_path_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

struct scan_ctx {
    const char *dir_path;
    Arena *arena;
    char (*paths)[PATH_BUF_SIZE];
    int count;
    int cap;
};

static bool scan_visitor(const char *name, void *arg) {
    struct scan_ctx *ctx = arg;
    if (name[0] == '.') return true; /* skip hidden files */
    size_t nlen = strlen(name);
    if (nlen < GRAY_EXT_LEN + 1 || strcmp(name + nlen - GRAY_EXT_LEN, GRAY_EXT) != 0)
        return true;
    ARENA_GROW(ctx->arena, ctx->paths, ctx->count, ctx->cap);
    gray_path_join(ctx->paths[ctx->count], PATH_BUF_SIZE, ctx->dir_path, name);
    ctx->count++;
    return true;
}

/* Returns the number of .gray files found, or -1 if the directory cannot be
 * read. *out_paths receives an arena-allocated array of that many paths. */
static int scan_gray_files(Arena *arena, const char *dir_path,
                           char (**out_paths)[PATH_BUF_SIZE]) {
    struct scan_ctx ctx = { dir_path, arena, NULL, 0, 0 };
    if (!gray_scandir(dir_path, scan_visitor, &ctx)) return -1;

    /* Sort alphabetically for deterministic import order */
    qsort(ctx.paths, (size_t)ctx.count, PATH_BUF_SIZE, gray_path_cmp);
    *out_paths = ctx.paths;
    return ctx.count;
}

void imports_resolve(Arena *arena, DiagnosticList *diag, AstNode *program,
                     const char *input_file, ImportResolution *out) {
    const char **module_files = NULL;
    const char **module_names = NULL;
    int module_file_count = 0, module_file_cap = 0;
    const char **module_alias_names = NULL;
    const char **module_alias_targets = NULL;
    int module_alias_count = 0, module_alias_cap = 0;

        /* Mark the main file as already imported (prevents circular import loops).
         * Use realpath so that diamond dependencies reaching the main file via
         * different relative paths are still detected as duplicates. */
        const char *entry_real_path;
        {
            char *rp = gray_realpath(input_file);
            entry_real_path = rp ? arena_copy_string(arena, rp) : input_file;
            mark_imported(entry_real_path);
            free(rp);
        }

        /* Derive main file's module name for circular import resolution */
        const char *main_base = gray_path_basename(input_file);
        char main_mod_buf[MSG_BUF_SIZE];
        size_t main_mod_len = strlen(main_base);
        if (main_mod_len > GRAY_EXT_LEN && strcmp(main_base + main_mod_len - GRAY_EXT_LEN, GRAY_EXT) == 0) {
            memcpy(main_mod_buf, main_base, main_mod_len - GRAY_EXT_LEN);
            main_mod_buf[main_mod_len - GRAY_EXT_LEN] = '\0';
        } else {
            snprintf(main_mod_buf, sizeof(main_mod_buf), "%s", main_base);
        }

        /* Determine the directory of the input file */
        char input_dir[PATH_BUF_SIZE];
        strncpy(input_dir, input_file, sizeof(input_dir) - 1);
        input_dir[sizeof(input_dir) - 1] = '\0';
        char *last_sep = gray_path_rsep(input_dir);
        if (last_sep) *(last_sep + 1) = '\0';
        else { input_dir[0] = '.'; input_dir[1] = '/'; input_dir[2] = '\0'; }

        /* Snapshot of original main-program nodes taken before any imports are merged.
         * The outer rewrite pass after each import only needs to update these nodes;
         * imported nodes are already rewritten inline during the rewrite+merge pass.
         * A plain pointer would be invalidated by in-place memmoves, so we copy
         * the AstNode* array into a stable arena allocation here. */
        int main_stmt_snapshot_count = program->data.program.stmt_count;
        AstNode **main_stmt_snapshot = arena_alloc(arena,
            sizeof(AstNode *) * (main_stmt_snapshot_count > 0 ? main_stmt_snapshot_count : 1));
        memcpy(main_stmt_snapshot, program->data.program.stmts,
            sizeof(AstNode *) * main_stmt_snapshot_count);

        /* Seed import queue once from the initial program stmts — O(N), done once.
         * Transitive imports push onto the tail as they are discovered, so the
         * queue drains naturally without re-scanning the growing program AST. */
        AstNode **import_queue = NULL;
        int import_queue_cap = 0;
        int iq_head = 0, iq_tail = 0;
        for (int si = 0; si < program->data.program.stmt_count; si++) {
            if (program->data.program.stmts[si]->kind == NODE_IMPORT_STMT) {
                ARENA_GROW(arena, import_queue, iq_tail, import_queue_cap);
                import_queue[iq_tail++] = program->data.program.stmts[si];
            }
        }

        const char **seen_modules = NULL;
        const char **seen_paths = NULL;
        const char **seen_files = NULL;
        bool *seen_is_stdlib = NULL;
        int seen_cap = 0;
        int seen_count = 0;

        while (iq_head < iq_tail) {
            AstNode *stmt = import_queue[iq_head++];
            /* Line and column below come from this import statement, so the
             * file has to as well. Reporting them against the entry file put
             * the caret on whatever that file happens to have on the line,
             * which for a transitive import is never the import that failed. */
            const char *stmt_file = stmt->token.file ? stmt->token.file : input_file;

            for (int ii = 0; ii < stmt->data.import_stmt.count; ii++) {
                ImportItem *item = &stmt->data.import_stmt.items[ii];
                if (item->is_c_import) continue;

                /* A stdlib import binds a module name just as a local one
                 * does. Recording it here is what makes a local import of the
                 * same name collide, instead of the two silently resolving to
                 * different modules in different phases. */
                if (item->is_stdlib) {
                    const char *std_name = item->alias ? item->alias : item->module;
                    if (!std_name) continue;
                    bool bound = false;
                    for (int sm = 0; sm < seen_count; sm++) {
                        if (strcmp(seen_modules[sm], std_name) != 0) continue;
                        if (!seen_is_stdlib[sm]) {
                            char msg[MSG_BUF_SIZE];
                            snprintf(msg, sizeof(msg),
                                "module name '%s' is already imported; use an alias to distinguish them",
                                std_name);
                            diagnostic_error_message(diag, "E6001", strdup(msg),
                                stmt_file, stmt->token.line, stmt->token.column, 0);
                        } else if (same_import_file(seen_files[sm], stmt_file)) {
                            /* One file importing the same module twice. Reached
                             * from two different files it is a diamond, which is
                             * ordinary and stays silent. */
                            char msg[MSG_BUF_SIZE];
                            snprintf(msg, sizeof(msg),
                                "module '%s' is already imported in this file", std_name);
                            diagnostic_error_help(diag, "E6011", strdup(msg),
                                stmt_file, stmt->token.line, stmt->token.column, 0,
                                "remove the duplicate import");
                        }
                        bound = true;
                        break;
                    }
                    if (bound) continue;
                    if (seen_count >= seen_cap) {
                        seen_cap = GROW_NEXT_CAP(seen_cap);
                        ARENA_GROW_TO(arena, seen_modules, seen_count, seen_cap);
                        ARENA_GROW_TO(arena, seen_paths, seen_count, seen_cap);
                        ARENA_GROW_TO(arena, seen_files, seen_count, seen_cap);
                        ARENA_GROW_TO(arena, seen_is_stdlib, seen_count, seen_cap);
                    }
                    seen_modules[seen_count] = std_name;
                    seen_paths[seen_count] = NULL;
                    seen_files[seen_count] = stmt_file;
                    seen_is_stdlib[seen_count] = true;
                    seen_count++;
                    continue;
                }

                if (!item->path) continue;

                /* Resolve path relative to the file that contains the import.
                 * For imports written directly in the entry file, source_dir is NULL
                 * and we fall back to input_dir. For transitive imports injected from
                 * imported files, source_dir points at the importing file's directory. */
                char import_path[PATH_BUF_SIZE];
                const char *rel = item->path;
                if (rel[0] == '.' && rel[1] == '/') rel += 2;
                const char *base_dir = item->source_dir ? item->source_dir : input_dir;
                snprintf(import_path, sizeof(import_path), "%s%s", base_dir, rel);

                /* Determine import kind: direct .gray file, extensionless file, or directory.
                 * Build a list of actual .gray file paths to import. */
                char (*file_list)[PATH_BUF_SIZE] = NULL;
                int file_count = 0;

                size_t iplen = strlen(import_path);
                if (iplen >= GRAY_EXT_LEN && strcmp(import_path + iplen - GRAY_EXT_LEN, GRAY_EXT) == 0) {
                    /* Case 1: explicit .gray path — direct file import */
                    file_list = arena_alloc(arena, sizeof(char[PATH_BUF_SIZE]));
                    strncpy(file_list[0], import_path, PATH_BUF_SIZE - 1);
                    file_list[0][PATH_BUF_SIZE - 1] = '\0';
                    file_count = 1;
                } else {
                    /* Case 2: try appending .gray (extensionless file import) */
                    char try_file[PATH_BUF_SIZE];
                    snprintf(try_file, sizeof(try_file), "%s.gray", import_path);
                    if (gray_is_file(try_file)) {
                        file_list = arena_alloc(arena, sizeof(char[PATH_BUF_SIZE]));
                        strncpy(file_list[0], try_file, PATH_BUF_SIZE - 1);
                        file_list[0][PATH_BUF_SIZE - 1] = '\0';
                        file_count = 1;
                        /* Update import_path so collision detection uses the resolved path */
                        strncpy(import_path, try_file, sizeof(import_path) - 1);
                        import_path[sizeof(import_path) - 1] = '\0';
                    } else if (gray_is_dir(import_path)) {
                        /* Case 3: directory import — scan for .gray files */

                        /* Self-referential directory import: if the importing file
                         * lives inside the directory it is trying to import, reject. */
                        if (item->source_dir) {
                            char *norm_dir = gray_realpath(import_path);
                            char *norm_src = gray_realpath(item->source_dir);
                            if (norm_dir && norm_src && gray_path_equal(norm_dir, norm_src)) {
                                char msg[MSG_BUF_LARGE];
                                snprintf(msg, sizeof(msg),
                                    "cannot import own module directory '%s'", item->path);
                                diagnostic_error_message(diag, "E6004", strdup(msg),
                                    stmt_file, stmt->token.line, stmt->token.column, 0);
                                free(norm_dir);
                                free(norm_src);
                                continue;
                            }
                            free(norm_dir);
                            free(norm_src);
                        }

                        file_count = scan_gray_files(arena, import_path, &file_list);
                        if (file_count == 0) {
                            char msg[MSG_BUF_LARGE];
                            snprintf(msg, sizeof(msg), "directory '%s' contains no .gray files", item->path);
                            diagnostic_error_message(diag, "E6003", strdup(msg),
                                stmt_file, stmt->token.line, stmt->token.column, 0);
                            continue;
                        }
                    } else {
                        /* Nothing found */
                        char msg[MSG_BUF_LARGE];
                        snprintf(msg, sizeof(msg), "cannot find file or directory '%s'", item->path);
                        diagnostic_error_message(diag, "E6002", strdup(msg),
                            stmt_file, stmt->token.line, stmt->token.column, 0);
                        continue;
                    }
                }

                /* Derive module name from filename/directory (strip directory and .gray) */
                const char *mod_base = gray_path_basename(rel);

                /* For directory imports the path ends with a separator so
                 * mod_base points at the empty string after it.  Back up to
                 * extract the actual directory name (e.g. "engine" from
                 * "src/engine/"). */
                if (mod_base[0] == '\0' && mod_base > rel + 1) {
                    const char *sep = mod_base - 1;
                    const char *prev = sep - 1;
                    while (prev > rel && !gray_is_path_sep(*prev)) prev--;
                    if (gray_is_path_sep(*prev)) prev++;
                    size_t dlen = (size_t)(sep - prev);
                    char dir_name[MSG_BUF_SIZE];
                    memcpy(dir_name, prev, dlen);
                    dir_name[dlen] = '\0';
                    mod_base = arena_copy_string(arena, dir_name);
                }

                char mod_name_buf[MSG_BUF_SIZE];
                size_t mod_len = strlen(mod_base);
                if (mod_len > GRAY_EXT_LEN && strcmp(mod_base + mod_len - GRAY_EXT_LEN, GRAY_EXT) == 0) {
                    memcpy(mod_name_buf, mod_base, mod_len - GRAY_EXT_LEN);
                    mod_name_buf[mod_len - GRAY_EXT_LEN] = '\0';
                } else {
                    snprintf(mod_name_buf, sizeof(mod_name_buf), "%s", mod_base);
                }
                const char *mod_name = item->alias ? item->alias : arena_copy_string(arena, mod_name_buf);

                /* Normalize import_path so diamond deps resolve to the same canonical path */
                char norm_import[PATH_BUF_SIZE];
                {
                    char *rp = gray_realpath(import_path);
                    if (rp) {
                        strncpy(norm_import, rp, sizeof(norm_import) - 1);
                        norm_import[sizeof(norm_import) - 1] = '\0';
                        free(rp);
                    } else {
                        strncpy(norm_import, import_path, sizeof(norm_import) - 1);
                        norm_import[sizeof(norm_import) - 1] = '\0';
                    }
                }

                /* One file importing the same target twice, whatever it called
                 * it. This has to be keyed on the path, not the module name: the
                 * two spellings need not agree — `import ABC "./abc", "./abc.gray"`
                 * names one file twice — and a name-first search misses that
                 * entirely, which left the second name registered and never
                 * populated. */
                bool collision = false;
                for (int sm = 0; sm < seen_count; sm++) {
                    if (seen_is_stdlib[sm] || !seen_paths[sm]) continue;
                    if (strcmp(seen_paths[sm], norm_import) != 0) continue;
                    if (!same_import_file(seen_files[sm], stmt_file)) continue;
                    char msg[MSG_BUF_SIZE];
                    snprintf(msg, sizeof(msg),
                        "module '%s' is already imported in this file", mod_name);
                    diagnostic_error_help(diag, "E6011", strdup(msg),
                        stmt_file, stmt->token.line, stmt->token.column, 0,
                        "remove the duplicate import");
                    collision = true;
                    break;
                }
                /* Module name collision detection. Don't collide with the main
                 * file's own module name. Diamond dependencies (the same file
                 * reached from two different files) are silently deduped rather
                 * than causing a false E6001 error. */
                for (int sm = 0; sm < seen_count && !collision; sm++) {
                    if (strcmp(seen_modules[sm], mod_name) != 0) continue;
                    if (!seen_is_stdlib[sm] && strcmp(seen_paths[sm], norm_import) == 0) {
                        /* Same target, same module name, different importing
                         * file — a diamond dependency. */
                        collision = true;
                        break;
                    }
                    /* Different file, same module name — genuine collision */
                    char msg[MSG_BUF_SIZE];
                    snprintf(msg, sizeof(msg),
                        "module name '%s' is already imported; use an alias to distinguish them",
                        mod_name);
                    diagnostic_error_message(diag, "E6001", strdup(msg),
                        stmt_file, stmt->token.line, stmt->token.column, 0);
                    collision = true;
                    break;
                }
                if (collision) continue;
                if (seen_count >= seen_cap) {
                    seen_cap = GROW_NEXT_CAP(seen_cap);
                    ARENA_GROW_TO(arena, seen_modules, seen_count, seen_cap);
                    ARENA_GROW_TO(arena, seen_paths, seen_count, seen_cap);
                    ARENA_GROW_TO(arena, seen_files, seen_count, seen_cap);
                    ARENA_GROW_TO(arena, seen_is_stdlib, seen_count, seen_cap);
                }
                seen_modules[seen_count] = mod_name;
                seen_paths[seen_count] = arena_copy_string(arena, norm_import);
                seen_files[seen_count] = stmt_file;
                seen_is_stdlib[seen_count] = false;
                seen_count++;

                /* Set the alias if not already set */
                if (!item->alias) item->alias = mod_name;
                if (!item->module) item->module = mod_name;

                /* Process each file in the import (1 for single file, N for directory).
                 * For directory imports, we use a two-pass approach:
                 *   Pass 1: Parse all files, collect ALL declaration names across all files
                 *   Pass 2: Rewrite using the combined mapping, then merge
                 * This ensures sibling references (e.g. logic.gray referencing types.gray's
                 * structs) get properly rewritten to their prefixed names. */

                /* Storage for parsed programs in the directory */
                AstNode **parsed_programs = arena_alloc(arena,
                    sizeof(AstNode *) * (size_t)(file_count > 0 ? file_count : 1));
                const char **parsed_paths = arena_alloc(arena,
                    sizeof(const char *) * (size_t)(file_count > 0 ? file_count : 1));
                int parsed_count = 0;

                /* Parse pass: parse each file, collect names, inject transitive imports */
                for (int fi = 0; fi < file_count; fi++) {
                    const char *cur_file_path = file_list[fi];

                    /* Normalize the path so diamond dependencies (same file
                     * reached via different relative paths) are deduplicated. */
                    char *norm = gray_realpath(cur_file_path);
                    const char *norm_path = norm ? arena_copy_string(arena, norm) : cur_file_path;
                    free(norm);

                    /* The entry file is the program, not a module: its
                     * declarations stay unmangled and are never registered
                     * under a module name, so importing it yields an empty
                     * namespace. Reject the import instead of letting every
                     * qualified reference through it fail later. */
                    if (file_count == 1 && gray_path_equal(norm_path, entry_real_path)) {
                        char msg[MSG_BUF_LARGE];
                        snprintf(msg, sizeof(msg),
                            "cannot import '%s'; it is the program's entry point", item->path);
                        diagnostic_error_help(diag, "E6005", strdup(msg),
                            stmt_file, stmt->token.line, stmt->token.column, 0,
                            "move the shared declarations into a third file and import that from both");
                        continue;
                    }

                    /* Skip if already imported (handles cycles and duplicates) */
                    if (already_imported(norm_path)) {
                        /* If this is a transitive import from inside a directory
                         * module referencing a sibling already pulled in by the
                         * directory import, emit an informational warning. */
                        if (item->source_dir && file_count == 1) {
                            char msg[MSG_BUF_SIZE];
                            snprintf(msg, sizeof(msg),
                                "import of '%s' is redundant; already included by directory import",
                                item->path);
                            diagnostic_warning_message(diag, "W2014", strdup(msg),
                                stmt_file, stmt->token.line, stmt->token.column, 0);
                        } else if (file_count == 1 &&
                                   same_import_file(imported_by_file(norm_path), stmt_file)) {
                            /* The same file already imported this target, under
                             * whatever namespace — importing a directory and
                             * then a file inside it names one module twice. */
                            const char *owner_mod = imported_by_module(norm_path);
                            char msg[MSG_BUF_LARGE], help[MSG_BUF_SIZE];
                            if (owner_mod) {
                                snprintf(msg, sizeof(msg),
                                    "'%s' is already imported in this file as part of module '%s'",
                                    item->path, owner_mod);
                                snprintf(help, sizeof(help),
                                    "use the '%s' namespace, or remove one of the imports",
                                    owner_mod);
                            } else {
                                snprintf(msg, sizeof(msg),
                                    "'%s' is already imported in this file", item->path);
                                snprintf(help, sizeof(help), "remove the duplicate import");
                            }
                            diagnostic_error_help(diag, "E6011", strdup(msg),
                                stmt_file, stmt->token.line, stmt->token.column, 0, help);
                        }
                        continue;
                    }
                    mark_imported_from(norm_path, mod_name, stmt_file);

                    /* Attribute this file to its module. Every file of a
                     * directory import records the same module name, which is
                     * what merges their declarations into one scope. */
                    if (module_file_count >= module_file_cap) {
                        module_file_cap = GROW_NEXT_CAP(module_file_cap);
                        ARENA_GROW_TO(arena, module_files, module_file_count, module_file_cap);
                        ARENA_GROW_TO(arena, module_names, module_file_count, module_file_cap);
                    }
                    module_files[module_file_count] = arena_copy_string(arena, cur_file_path);
                    module_names[module_file_count] = mod_name;
                    module_file_count++;

                    /* Read and parse the imported file */
                    char *imp_source = gray_read_file(cur_file_path, false);
                    if (!imp_source) {
                        char msg[MSG_BUF_LARGE];
                        snprintf(msg, sizeof(msg), "cannot find file or directory '%s'", cur_file_path);
                        diagnostic_error_message(diag, "E6002", strdup(msg),
                            stmt_file, stmt->token.line, stmt->token.column, 0);
                        continue;
                    }

                    Lexer *imp_lexer = lexer_create(arena, imp_source, cur_file_path);
                    Parser *imp_parser = parser_create(arena, imp_lexer, cur_file_path, diag);
                    AstNode *imp_program = parser_parse_program(imp_parser);

                    if (!imp_program || diagnostic_has_errors(diag)) continue;

                    /* Inject transitive import statements into the main program.
                     * Sibling imports (pointing to other files in the same directory)
                     * are NOT injected — instead their module alias is added to the
                     * rewrite mapping so qualified references like types.Item get
                     * rewritten to mylib.Item → resolves as mylib_Item. */
                    {
                        char cur_dir[PATH_BUF_SIZE];
                        strncpy(cur_dir, cur_file_path, sizeof(cur_dir) - 1);
                        cur_dir[sizeof(cur_dir) - 1] = '\0';
                        char *cd_sep = gray_path_rsep(cur_dir);
                        if (cd_sep) *(cd_sep + 1) = '\0';
                        else { cur_dir[0] = '.'; cur_dir[1] = '/'; cur_dir[2] = '\0'; }
                        const char *src_dir = arena_copy_string(arena, cur_dir);

                        /* Normalize the directory being imported for sibling detection */
                        char *norm_import_dir = gray_realpath(import_path);

                        /* Siblings never reach the duplicate check above: they
                         * are turned into alias mappings and dropped instead of
                         * being queued as imports. Track the ones this file has
                         * named so it cannot name one twice either. */
                        const char **seen_siblings = NULL;
                        int seen_sibling_count = 0, seen_sibling_cap = 0;

                        for (int ti = 0; ti < imp_program->data.program.stmt_count; ti++) {
                            AstNode *ts = imp_program->data.program.stmts[ti];
                            if (ts->kind != NODE_IMPORT_STMT) continue;

                            bool all_sibling = true;
                            for (int xi = 0; xi < ts->data.import_stmt.count; xi++) {
                                ImportItem *titem = &ts->data.import_stmt.items[xi];
                                if (titem->is_stdlib || titem->is_c_import) {
                                    all_sibling = false;
                                    continue;
                                }
                                if (!titem->path) continue;

                                /* Resolve the transitive import path */
                                const char *trel = titem->path;
                                if (trel[0] == '.' && trel[1] == '/') trel += 2;
                                char tres[PATH_BUF_SIZE];
                                snprintf(tres, sizeof(tres), "%s%s", src_dir, trel);

                                /* Check if it resolves to a file inside the same directory */
                                size_t trlen = strlen(tres);
                                bool is_sibling = false;
                                /* Try with .gray extension if not already present */
                                char tres_gray[PATH_BUF_SIZE];
                                const char *tres_check = tres;
                                if (trlen < GRAY_EXT_LEN || strcmp(tres + trlen - GRAY_EXT_LEN, GRAY_EXT) != 0) {
                                    snprintf(tres_gray, sizeof(tres_gray), "%s.gray", tres);
                                    tres_check = tres_gray;
                                }
                                char *norm_tres = gray_realpath(tres_check);
                                const char *sibling_path = norm_tres
                                    ? arena_copy_string(arena, norm_tres) : NULL;
                                if (norm_tres && norm_import_dir) {
                                    /* Check if the file's directory matches import_path.
                                     * Both buffers are ours to truncate in place. */
                                    char *tsep = gray_path_rsep(norm_tres);
                                    if (tsep) {
                                        *tsep = '\0';
                                        /* Strip trailing separator from norm_import_dir */
                                        size_t imp_dir_len = strlen(norm_import_dir);
                                        if (imp_dir_len > 0 &&
                                            gray_is_path_sep(norm_import_dir[imp_dir_len - 1]))
                                            norm_import_dir[imp_dir_len - 1] = '\0';
                                        if (gray_path_equal(norm_tres, norm_import_dir)) {
                                            is_sibling = true;
                                        }
                                    }
                                }
                                free(norm_tres);

                                if (is_sibling) {
                                    bool sibling_dup = false;
                                    for (int sx = 0; sx < seen_sibling_count && sibling_path; sx++) {
                                        if (strcmp(seen_siblings[sx], sibling_path) != 0) continue;
                                        char msg[MSG_BUF_LARGE];
                                        snprintf(msg, sizeof(msg),
                                            "'%s' is already imported in this file", titem->path);
                                        diagnostic_error_help(diag, "E6011", strdup(msg),
                                            ts->token.file ? ts->token.file : cur_file_path,
                                            ts->token.line, ts->token.column, 0,
                                            "remove the duplicate import");
                                        sibling_dup = true;
                                        break;
                                    }
                                    if (!sibling_dup && sibling_path) {
                                        if (seen_sibling_count >= seen_sibling_cap) {
                                            seen_sibling_cap = GROW_NEXT_CAP(seen_sibling_cap);
                                            ARENA_GROW_TO(arena, seen_siblings,
                                                seen_sibling_count, seen_sibling_cap);
                                        }
                                        seen_siblings[seen_sibling_count++] = sibling_path;
                                    }
                                    /* Collect sibling alias for compound mapping generation later */
                                    const char *sib_alias = titem->alias;
                                    if (!sib_alias) {
                                        /* Derive alias from path (filename without .gray) */
                                        const char *sib_base = gray_path_basename(trel);
                                        char sib_buf[MSG_BUF_SIZE];
                                        size_t sib_len = strlen(sib_base);
                                        if (sib_len > GRAY_EXT_LEN && strcmp(sib_base + sib_len - GRAY_EXT_LEN, GRAY_EXT) == 0) {
                                            memcpy(sib_buf, sib_base, sib_len - GRAY_EXT_LEN);
                                            sib_buf[sib_len - GRAY_EXT_LEN] = '\0';
                                        } else {
                                            snprintf(sib_buf, sizeof(sib_buf), "%s", sib_base);
                                        }
                                        sib_alias = arena_copy_string(arena, sib_buf);
                                    }
                                    /* Record the sibling's own name as an alias
                                     * of the directory module, so a qualified
                                     * reference through it resolves. */
                                    if (module_alias_count >= module_alias_cap) {
                                        module_alias_cap = GROW_NEXT_CAP(module_alias_cap);
                                        ARENA_GROW_TO(arena, module_alias_names,
                                            module_alias_count, module_alias_cap);
                                        ARENA_GROW_TO(arena, module_alias_targets,
                                            module_alias_count, module_alias_cap);
                                    }
                                    module_alias_names[module_alias_count] = sib_alias;
                                    module_alias_targets[module_alias_count] = mod_name;
                                    module_alias_count++;
                                    /* Null out the sibling import path so it's not injected */
                                    titem->path = NULL;
                                } else {
                                    all_sibling = false;
                                    if (!titem->source_dir) {
                                        titem->source_dir = src_dir;
                                    }
                                }
                            }

                            /* Only inject import stmt if it has non-sibling items */
                            if (!all_sibling) {
                                ARENA_GROW(arena, program->data.program.stmts,
                                    program->data.program.stmt_count, program->data.program.stmt_cap);
                                ARENA_GROW(arena, import_queue, iq_tail, import_queue_cap);
                                import_queue[iq_tail++] = ts;
                                program->data.program.stmts[program->data.program.stmt_count++] = ts;
                            }
                        }
                        free(norm_import_dir);
                    }

                    /* Store the parsed program for the merge pass */
                    parsed_programs[parsed_count] = imp_program;
                    parsed_paths[parsed_count] = cur_file_path;
                    parsed_count++;
                }

                /* Merge pass */
                for (int pi = 0; pi < parsed_count; pi++) {
                    AstNode *imp_program = parsed_programs[pi];

                /* Merge imported declarations into the main program.
                 * Two passes: var_decls first (so they are in scope for
                 * function bodies), then everything else.
                 *
                 * Names are left exactly as written. Which module a
                 * declaration belongs to is recorded by file, and every
                 * reference to it is resolved against the symbol table. */

                /* Pass 1: variable declarations */
                for (int mi = 0; mi < imp_program->data.program.stmt_count; mi++) {
                    AstNode *imp_stmt = imp_program->data.program.stmts[mi];
                    if (imp_stmt->kind != NODE_VAR_DECL) continue;

                    ARENA_GROW(arena, program->data.program.stmts,
                        program->data.program.stmt_count, program->data.program.stmt_cap);
                    int insert_at = 0;
                    for (int k = 0; k < program->data.program.stmt_count; k++) {
                        if (program->data.program.stmts[k]->kind == NODE_IMPORT_STMT ||
                            program->data.program.stmts[k]->kind == NODE_USING_STMT) {
                            insert_at = k + 1;
                        } else break;
                    }
                    memmove(&program->data.program.stmts[insert_at + 1],
                            &program->data.program.stmts[insert_at],
                            sizeof(AstNode *) * (program->data.program.stmt_count - insert_at));
                    program->data.program.stmts[insert_at] = imp_stmt;
                    program->data.program.stmt_count++;
                }

                /* Pass 2: functions, structs, enums */
                for (int mi = 0; mi < imp_program->data.program.stmt_count; mi++) {
                    AstNode *imp_stmt = imp_program->data.program.stmts[mi];
                    /* Skip import/module/var declarations (vars handled in Pass 1).
                     * Using statements are preserved so the typechecker can scope
                     * them per-file and prevent transitive type leaking. */
                    if (imp_stmt->kind == NODE_IMPORT_STMT ||
                        imp_stmt->kind == NODE_MODULE_DECL ||
                        imp_stmt->kind == NODE_VAR_DECL) continue;

                    /* Insert into main program BEFORE existing declarations.
                     * This ensures imported constants/functions are visible to all code. */
                    ARENA_GROW(arena, program->data.program.stmts,
                        program->data.program.stmt_count, program->data.program.stmt_cap);
                    /* Find insertion point: after imports/using/var_decls */
                    int insert_at = 0;
                    for (int k = 0; k < program->data.program.stmt_count; k++) {
                        if (program->data.program.stmts[k]->kind == NODE_IMPORT_STMT ||
                            program->data.program.stmts[k]->kind == NODE_USING_STMT ||
                            program->data.program.stmts[k]->kind == NODE_VAR_DECL) {
                            insert_at = k + 1;
                        } else {
                            break;
                        }
                    }
                    /* Shift existing stmts to make room */
                    memmove(&program->data.program.stmts[insert_at + 1],
                            &program->data.program.stmts[insert_at],
                            sizeof(AstNode *) * (program->data.program.stmt_count - insert_at));
                    program->data.program.stmts[insert_at] = imp_stmt;
                    program->data.program.stmt_count++;
                }
                } /* end for (pi: rewrite+merge pass) */

                /* Mark this import item as fully processed. */
                item->path = NULL;
            }
        } /* end while (iq_head < iq_tail) */

    out->files = module_files;
    out->modules = module_names;
    out->count = module_file_count;
    out->alias_names = module_alias_names;
    out->alias_targets = module_alias_targets;
    out->alias_count = module_alias_count;
}
