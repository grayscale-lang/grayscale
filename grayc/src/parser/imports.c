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

#define PATH_BUFFER_SIZE 2048
#define GRAY_EXTENSION      ".gray"
#define GRAY_EXTENSION_LENGTH  5


/* Import cache: track already-imported files to avoid duplicates and cycles.
 * Open-addressing hash set keyed on canonical file path. */

/* Bucket count is always a power of two, and the table grows to keep the
 * load factor at or below one half. Linear probing needs a free slot to
 * terminate on, so the table must never fill. */
#define IMPORT_HASH_INITIAL_BUCKETS 512

#define FNV1A_OFFSET_BASIS 2166136261u
#define FNV1A_PRIME        16777619u

typedef struct {
    const char *path;
    const char *module;
    const char *from;   /* file whose import statement first pulled this path in */
    bool is_from_directory; /* true if `path` was pulled in as one file of a directory import */
} ImportHashEntry;

static ImportHashEntry *import_hash = NULL;
static uint32_t import_hash_buckets = 0;
static int imported_file_count = 0;

static uint32_t import_path_hash(const char *path) {
    uint32_t hash = FNV1A_OFFSET_BASIS;
    for (; *path; path++) hash = (hash ^ (uint8_t)*path) * FNV1A_PRIME;
    return hash;
}

/* Place an entry during a rehash, where the key is known to be unique. */
static void import_hash_place(ImportHashEntry *table, uint32_t buckets,
                              const char *path, const char *module, const char *from,
                              bool is_from_directory) {
    uint32_t slot = import_path_hash(path) & (buckets - 1);
    for (uint32_t i = slot; ; i = (i + 1) & (buckets - 1)) {
        if (!table[i].path) {
            table[i].path = path;
            table[i].module = module;
            table[i].from = from;
            table[i].is_from_directory = is_from_directory;
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
        ? import_hash_buckets * 2 : IMPORT_HASH_INITIAL_BUCKETS;
    ImportHashEntry *new_table = xcalloc(new_buckets, sizeof(ImportHashEntry));
    for (uint32_t i = 0; i < import_hash_buckets; i++) {
        if (import_hash[i].path)
            import_hash_place(new_table, new_buckets,
                import_hash[i].path, import_hash[i].module, import_hash[i].from,
                import_hash[i].is_from_directory);
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
        if (strcmp(import_hash[i].path, path) == 0) return import_hash[i].module;
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

/* True when `path` was first pulled in as one file of a directory import,
 * rather than a direct single-file import. Distinguishes a redundant
 * single-file import of something a directory import already covers
 * (STANDARD 8.2, a warning) from a genuine duplicate direct import of the
 * same file (an error). */
static bool imported_from_directory(const char *path) {
    if (!import_hash) return false;
    uint32_t slot = import_path_hash(path) & (import_hash_buckets - 1);
    for (uint32_t i = slot; ; i = (i + 1) & (import_hash_buckets - 1)) {
        if (!import_hash[i].path) return false;
        if (strcmp(import_hash[i].path, path) == 0) return import_hash[i].is_from_directory;
    }
}

static void mark_imported_from(const char *path, const char *module, const char *from,
                               bool is_from_directory) {
    import_hash_reserve();
    uint32_t slot = import_path_hash(path) & (import_hash_buckets - 1);
    for (uint32_t i = slot; ; i = (i + 1) & (import_hash_buckets - 1)) {
        if (!import_hash[i].path) {
            import_hash[i].path = path;
            import_hash[i].module = module;
            import_hash[i].from = from;
            import_hash[i].is_from_directory = is_from_directory;
            imported_file_count++;
            return;
        }
        if (strcmp(import_hash[i].path, path) == 0) return;
    }
}

/* Do two import statements sit in the same file? The entry file's statements
 * carry a NULL token file, which the caller has already resolved to the input
 * path, so a plain string compare is enough. */
static bool same_import_file(const char *left_file, const char *right_file) {
    if (left_file == right_file) return true;
    if (!left_file || !right_file) return false;
    return strcmp(left_file, right_file) == 0;
}

static void mark_imported(const char *path) {
    mark_imported_from(path, NULL, NULL, false);
}

/* qsort comparator over the fixed-width path buffers scan_gray_files fills. */
static int gray_path_compare(const void *left, const void *right) {
    return strcmp((const char *)left, (const char *)right);
}

struct gray_file_scan {
    const char *directory_path;
    Arena *arena;
    char (*paths)[PATH_BUFFER_SIZE];
    int count;
    int capacity;
};

static bool collect_gray_file(const char *name, void *context) {
    struct gray_file_scan *scan = context;
    if (name[0] == '.') return true; /* skip hidden files */
    size_t name_length = strlen(name);
    if (name_length < GRAY_EXTENSION_LENGTH + 1 || strcmp(name + name_length - GRAY_EXTENSION_LENGTH, GRAY_EXTENSION) != 0)
        return true;
    ARENA_GROW(scan->arena, scan->paths, scan->count, scan->capacity);
    gray_path_join(scan->paths[scan->count], PATH_BUFFER_SIZE, scan->directory_path, name);
    scan->count++;
    return true;
}

/* Returns the number of .gray files found, or -1 if the directory cannot be
 * read. *out_paths receives an arena-allocated array of that many paths. */
static int scan_gray_files(Arena *arena, const char *directory_path,
                           char (**out_paths)[PATH_BUFFER_SIZE]) {
    struct gray_file_scan scan = { directory_path, arena, NULL, 0, 0 };
    if (!gray_scandir(directory_path, collect_gray_file, &scan)) return -1;

    /* Sort alphabetically for deterministic import order */
    qsort(scan.paths, (size_t)scan.count, PATH_BUFFER_SIZE, gray_path_compare);
    *out_paths = scan.paths;
    return scan.count;
}

void imports_resolve(Arena *arena, DiagnosticList *diagnostics, AstNode *program,
                     const char *input_file, ImportResolution *out) {
    const char **module_files = NULL;
    const char **module_names = NULL;
    int module_file_count = 0, module_file_capacity = 0;
    const char **module_alias_names = NULL;
    const char **module_alias_targets = NULL;
    int module_alias_count = 0, module_alias_capacity = 0;

        /* Mark the main file as already imported (prevents circular import loops).
         * Use realpath so that diamond dependencies reaching the main file via
         * different relative paths are still detected as duplicates. */
        const char *entry_real_path;
        {
            char *real_path = gray_realpath(input_file);
            entry_real_path = real_path ? arena_copy_string(arena, real_path) : input_file;
            mark_imported(entry_real_path);
            free(real_path);
        }

        /* Derive main file's module name for circular import resolution */
        const char *main_base = gray_path_basename(input_file);
        char main_module_name[MESSAGE_BUFFER_SIZE];
        size_t main_base_length = strlen(main_base);
        if (main_base_length > GRAY_EXTENSION_LENGTH && strcmp(main_base + main_base_length - GRAY_EXTENSION_LENGTH, GRAY_EXTENSION) == 0) {
            memcpy(main_module_name, main_base, main_base_length - GRAY_EXTENSION_LENGTH);
            main_module_name[main_base_length - GRAY_EXTENSION_LENGTH] = '\0';
        } else {
            snprintf(main_module_name, sizeof(main_module_name), "%s", main_base);
        }

        /* Determine the directory of the input file */
        char input_directory[PATH_BUFFER_SIZE];
        strncpy(input_directory, input_file, sizeof(input_directory) - 1);
        input_directory[sizeof(input_directory) - 1] = '\0';
        char *last_separator = gray_path_last_separator(input_directory);
        if (last_separator) *(last_separator + 1) = '\0';
        else { input_directory[0] = '.'; input_directory[1] = '/'; input_directory[2] = '\0'; }

        /* Seed import queue once from the initial program stmts — O(N), done once.
         * Transitive imports push onto the tail as they are discovered, so the
         * queue drains naturally without re-scanning the growing program AST. */
        AstNode **import_queue = NULL;
        int import_queue_capacity = 0;
        int queue_head = 0, queue_tail = 0;
        for (int statement_index = 0; statement_index < program->data.program.statement_count; statement_index++) {
            if (program->data.program.statements[statement_index]->kind == NODE_IMPORT_STATEMENT) {
                ARENA_GROW(arena, import_queue, queue_tail, import_queue_capacity);
                import_queue[queue_tail++] = program->data.program.statements[statement_index];
            }
        }

        const char **seen_modules = NULL;
        const char **seen_paths = NULL;
        const char **seen_files = NULL;
        bool *seen_is_stdlib = NULL;
        int seen_capacity = 0;
        int seen_count = 0;

        while (queue_head < queue_tail) {
            AstNode *import_statement = import_queue[queue_head++];
            /* Line and column below come from this import statement, so the
             * file has to as well. Reporting them against the entry file put
             * the caret on whatever that file happens to have on the line,
             * which for a transitive import is never the import that failed. */
            const char *statement_file = import_statement->token.file ? import_statement->token.file : input_file;

            for (int item_index = 0; item_index < import_statement->data.import_statement.count; item_index++) {
                ImportItem *item = &import_statement->data.import_statement.items[item_index];
                if (item->is_c_import) continue;

                /* A stdlib import binds a module name just as a local one
                 * does. Recording it here is what makes a local import of the
                 * same name collide, instead of the two silently resolving to
                 * different modules in different phases. */
                if (item->is_stdlib) {
                    const char *stdlib_name = item->alias ? item->alias : item->module;
                    if (!stdlib_name) continue;
                    bool is_bound = false;
                    for (int seen_index = 0; seen_index < seen_count; seen_index++) {
                        if (strcmp(seen_modules[seen_index], stdlib_name) != 0) continue;
                        if (!seen_is_stdlib[seen_index]) {
                            char message[MESSAGE_BUFFER_SIZE];
                            snprintf(message, sizeof(message),
                                "module name '%s' is already imported; use an alias to distinguish them",
                                stdlib_name);
                            diagnostic_error_message(diagnostics, "E6001", strdup(message),
                                statement_file, import_statement->token.line, import_statement->token.column, 0);
                        } else if (same_import_file(seen_files[seen_index], statement_file)) {
                            /* One file importing the same module twice. Reached
                             * from two different files it is a diamond, which is
                             * ordinary and stays silent. */
                            char message[MESSAGE_BUFFER_SIZE];
                            snprintf(message, sizeof(message),
                                "module '%s' is already imported in this file", stdlib_name);
                            diagnostic_error_help(diagnostics, "E6011", strdup(message),
                                statement_file, import_statement->token.line, import_statement->token.column, 0,
                                "remove the duplicate import");
                        }
                        is_bound = true;
                        break;
                    }
                    if (is_bound) continue;
                    if (seen_count >= seen_capacity) {
                        seen_capacity = GROW_NEXT_CAPACITY(seen_capacity);
                        ARENA_GROW_TO(arena, seen_modules, seen_count, seen_capacity);
                        ARENA_GROW_TO(arena, seen_paths, seen_count, seen_capacity);
                        ARENA_GROW_TO(arena, seen_files, seen_count, seen_capacity);
                        ARENA_GROW_TO(arena, seen_is_stdlib, seen_count, seen_capacity);
                    }
                    seen_modules[seen_count] = stdlib_name;
                    seen_paths[seen_count] = NULL;
                    seen_files[seen_count] = statement_file;
                    seen_is_stdlib[seen_count] = true;
                    seen_count++;
                    continue;
                }

                if (!item->path) continue;

                /* Resolve path relative to the file that contains the import.
                 * For imports written directly in the entry file, source_directory is NULL
                 * and we fall back to input_directory. For transitive imports injected from
                 * imported files, source_directory points at the importing file's directory. */
                char import_path[PATH_BUFFER_SIZE];
                const char *relative_path = item->path;
                if (relative_path[0] == '.' && relative_path[1] == '/') relative_path += 2;
                const char *base_directory = item->source_directory ? item->source_directory : input_directory;
                snprintf(import_path, sizeof(import_path), "%s%s", base_directory, relative_path);

                /* Determine import kind: direct .gray file, extensionless file, or directory.
                 * Build a list of actual .gray file paths to import. */
                char (*file_list)[PATH_BUFFER_SIZE] = NULL;
                int file_count = 0;
                bool is_directory_import = false;

                size_t import_path_length = strlen(import_path);
                if (import_path_length >= GRAY_EXTENSION_LENGTH && strcmp(import_path + import_path_length - GRAY_EXTENSION_LENGTH, GRAY_EXTENSION) == 0) {
                    /* Case 1: explicit .gray path — direct file import */
                    file_list = arena_allocate(arena, sizeof(char[PATH_BUFFER_SIZE]));
                    strncpy(file_list[0], import_path, PATH_BUFFER_SIZE - 1);
                    file_list[0][PATH_BUFFER_SIZE - 1] = '\0';
                    file_count = 1;
                } else {
                    /* Case 2: try appending .gray (extensionless file import) */
                    char candidate_file[PATH_BUFFER_SIZE];
                    snprintf(candidate_file, sizeof(candidate_file), "%s.gray", import_path);
                    if (gray_is_file(candidate_file)) {
                        file_list = arena_allocate(arena, sizeof(char[PATH_BUFFER_SIZE]));
                        strncpy(file_list[0], candidate_file, PATH_BUFFER_SIZE - 1);
                        file_list[0][PATH_BUFFER_SIZE - 1] = '\0';
                        file_count = 1;
                        /* Update import_path so collision detection uses the resolved path */
                        strncpy(import_path, candidate_file, sizeof(import_path) - 1);
                        import_path[sizeof(import_path) - 1] = '\0';
                    } else if (gray_is_directory(import_path)) {
                        /* Case 3: directory import — scan for .gray files */

                        /* Self-referential directory import: if the importing file
                         * lives inside the directory it is trying to import, reject. */
                        if (item->source_directory) {
                            char *normalized_directory = gray_realpath(import_path);
                            char *normalized_source_directory = gray_realpath(item->source_directory);
                            if (normalized_directory && normalized_source_directory && gray_path_equal(normalized_directory, normalized_source_directory)) {
                                char message[MESSAGE_BUFFER_LARGE_SIZE];
                                snprintf(message, sizeof(message),
                                    "cannot import own module directory '%s'", item->path);
                                diagnostic_error_message(diagnostics, "E6004", strdup(message),
                                    statement_file, import_statement->token.line, import_statement->token.column, 0);
                                free(normalized_directory);
                                free(normalized_source_directory);
                                continue;
                            }
                            free(normalized_directory);
                            free(normalized_source_directory);
                        }

                        file_count = scan_gray_files(arena, import_path, &file_list);
                        is_directory_import = true;
                        if (file_count == 0) {
                            char message[MESSAGE_BUFFER_LARGE_SIZE];
                            snprintf(message, sizeof(message), "directory '%s' contains no .gray files", item->path);
                            diagnostic_error_message(diagnostics, "E6003", strdup(message),
                                statement_file, import_statement->token.line, import_statement->token.column, 0);
                            continue;
                        }
                    } else {
                        /* Nothing found */
                        char message[MESSAGE_BUFFER_LARGE_SIZE];
                        snprintf(message, sizeof(message), "cannot find file or directory '%s'", item->path);
                        diagnostic_error_message(diagnostics, "E6002", strdup(message),
                            statement_file, import_statement->token.line, import_statement->token.column, 0);
                        continue;
                    }
                }

                /* Derive module name from filename/directory (strip directory and .gray) */
                const char *module_base = gray_path_basename(relative_path);

                /* For directory imports the path ends with a separator so
                 * module_base points at the empty string after it.  Back up to
                 * extract the actual directory name (e.g. "engine" from
                 * "src/engine/"). */
                if (module_base[0] == '\0' && module_base > relative_path + 1) {
                    const char *separator = module_base - 1;
                    const char *directory_start = separator - 1;
                    while (directory_start > relative_path && !gray_is_path_separator(*directory_start)) directory_start--;
                    if (gray_is_path_separator(*directory_start)) directory_start++;
                    size_t directory_name_length = (size_t)(separator - directory_start);
                    char directory_name[MESSAGE_BUFFER_SIZE];
                    memcpy(directory_name, directory_start, directory_name_length);
                    directory_name[directory_name_length] = '\0';
                    module_base = arena_copy_string(arena, directory_name);
                }

                char module_name_buffer[MESSAGE_BUFFER_SIZE];
                size_t module_base_length = strlen(module_base);
                if (module_base_length > GRAY_EXTENSION_LENGTH && strcmp(module_base + module_base_length - GRAY_EXTENSION_LENGTH, GRAY_EXTENSION) == 0) {
                    memcpy(module_name_buffer, module_base, module_base_length - GRAY_EXTENSION_LENGTH);
                    module_name_buffer[module_base_length - GRAY_EXTENSION_LENGTH] = '\0';
                } else {
                    snprintf(module_name_buffer, sizeof(module_name_buffer), "%s", module_base);
                }
                const char *module_name = item->alias ? item->alias : arena_copy_string(arena, module_name_buffer);

                /* Normalize import_path so diamond deps resolve to the same canonical path */
                char normalized_import_path[PATH_BUFFER_SIZE];
                {
                    char *real_path = gray_realpath(import_path);
                    if (real_path) {
                        strncpy(normalized_import_path, real_path, sizeof(normalized_import_path) - 1);
                        normalized_import_path[sizeof(normalized_import_path) - 1] = '\0';
                        free(real_path);
                    } else {
                        strncpy(normalized_import_path, import_path, sizeof(normalized_import_path) - 1);
                        normalized_import_path[sizeof(normalized_import_path) - 1] = '\0';
                    }
                }

                /* One file importing the same target twice, whatever it called
                 * it. This has to be keyed on the path, not the module name: the
                 * two spellings need not agree — `import ABC "./abc", "./abc.gray"`
                 * names one file twice — and a name-first search misses that
                 * entirely, which left the second name registered and never
                 * populated. */
                bool has_collision = false;
                for (int seen_index = 0; seen_index < seen_count; seen_index++) {
                    if (seen_is_stdlib[seen_index] || !seen_paths[seen_index]) continue;
                    if (strcmp(seen_paths[seen_index], normalized_import_path) != 0) continue;
                    if (!same_import_file(seen_files[seen_index], statement_file)) continue;
                    char message[MESSAGE_BUFFER_SIZE];
                    snprintf(message, sizeof(message),
                        "module '%s' is already imported in this file", module_name);
                    diagnostic_error_help(diagnostics, "E6011", strdup(message),
                        statement_file, import_statement->token.line, import_statement->token.column, 0,
                        "remove the duplicate import");
                    has_collision = true;
                    break;
                }
                /* Module name collision detection. Don't collide with the main
                 * file's own module name. Diamond dependencies (the same file
                 * reached from two different files) are silently deduped rather
                 * than causing a false E6001 error. */
                for (int seen_index = 0; seen_index < seen_count && !has_collision; seen_index++) {
                    if (strcmp(seen_modules[seen_index], module_name) != 0) continue;
                    if (!seen_is_stdlib[seen_index] && strcmp(seen_paths[seen_index], normalized_import_path) == 0) {
                        /* Same target, same module name, different importing
                         * file — a diamond dependency. */
                        has_collision = true;
                        break;
                    }
                    /* Different file, same module name — genuine collision */
                    char message[MESSAGE_BUFFER_SIZE];
                    snprintf(message, sizeof(message),
                        "module name '%s' is already imported; use an alias to distinguish them",
                        module_name);
                    diagnostic_error_message(diagnostics, "E6001", strdup(message),
                        statement_file, import_statement->token.line, import_statement->token.column, 0);
                    has_collision = true;
                    break;
                }
                if (has_collision) continue;
                if (seen_count >= seen_capacity) {
                    seen_capacity = GROW_NEXT_CAPACITY(seen_capacity);
                    ARENA_GROW_TO(arena, seen_modules, seen_count, seen_capacity);
                    ARENA_GROW_TO(arena, seen_paths, seen_count, seen_capacity);
                    ARENA_GROW_TO(arena, seen_files, seen_count, seen_capacity);
                    ARENA_GROW_TO(arena, seen_is_stdlib, seen_count, seen_capacity);
                }
                seen_modules[seen_count] = module_name;
                seen_paths[seen_count] = arena_copy_string(arena, normalized_import_path);
                seen_files[seen_count] = statement_file;
                seen_is_stdlib[seen_count] = false;
                seen_count++;

                /* Set the alias if not already set */
                if (!item->alias) item->alias = module_name;
                if (!item->module) item->module = module_name;

                /* Process each file in the import (1 for single file, N for directory).
                 * For directory imports, we use a two-pass approach:
                 *   Pass 1: Parse all files, collect ALL declaration names across all files
                 *   Pass 2: Rewrite using the combined mapping, then merge
                 * This ensures sibling references (e.g. logic.gray referencing types.gray's
                 * structs) get properly rewritten to their prefixed names. */

                /* Storage for parsed programs in the directory */
                AstNode **parsed_programs = arena_allocate(arena,
                    sizeof(AstNode *) * (size_t)(file_count > 0 ? file_count : 1));
                const char **parsed_paths = arena_allocate(arena,
                    sizeof(const char *) * (size_t)(file_count > 0 ? file_count : 1));
                int parsed_count = 0;

                /* Parse pass: parse each file, collect names, inject transitive imports */
                for (int file_index = 0; file_index < file_count; file_index++) {
                    const char *current_file_path = file_list[file_index];

                    /* Normalize the path so diamond dependencies (same file
                     * reached via different relative paths) are deduplicated. */
                    char *resolved_path = gray_realpath(current_file_path);
                    const char *normalized_path = resolved_path ? arena_copy_string(arena, resolved_path) : current_file_path;
                    free(resolved_path);

                    /* The entry file is the program, not a module: its
                     * declarations stay unmangled and are never registered
                     * under a module name, so importing it yields an empty
                     * namespace. Reject the import instead of letting every
                     * qualified reference through it fail later. */
                    if (file_count == 1 && gray_path_equal(normalized_path, entry_real_path)) {
                        char message[MESSAGE_BUFFER_LARGE_SIZE];
                        snprintf(message, sizeof(message),
                            "cannot import '%s'; it is the program's entry point", item->path);
                        diagnostic_error_help(diagnostics, "E6005", strdup(message),
                            statement_file, import_statement->token.line, import_statement->token.column, 0,
                            "move the shared declarations into a third file and import that from both");
                        continue;
                    }

                    /* Skip if already imported (handles cycles and duplicates) */
                    if (already_imported(normalized_path)) {
                        /* If this is a transitive import from inside a directory
                         * module referencing a sibling already pulled in by the
                         * directory import, emit an informational warning. */
                        if (item->source_directory && file_count == 1) {
                            char message[MESSAGE_BUFFER_SIZE];
                            snprintf(message, sizeof(message),
                                "import of '%s' is redundant; already included by directory import",
                                item->path);
                            diagnostic_warning_message(diagnostics, "W2014", strdup(message),
                                statement_file, import_statement->token.line, import_statement->token.column, 0);
                        } else if (file_count == 1 && imported_from_directory(normalized_path) &&
                                   same_import_file(imported_by_file(normalized_path), statement_file)) {
                            /* A directory import already covers this file, and this
                             * single-file import directly names it (STANDARD 8.2) —
                             * redundant, not a collision. */
                            char message[MESSAGE_BUFFER_SIZE];
                            snprintf(message, sizeof(message),
                                "import of '%s' is redundant; already included by directory import",
                                item->path);
                            diagnostic_warning_message(diagnostics, "W2015", strdup(message),
                                statement_file, import_statement->token.line, import_statement->token.column, 0);
                        } else if (file_count == 1 &&
                                   same_import_file(imported_by_file(normalized_path), statement_file)) {
                            /* The same file already imported this target, under
                             * whatever namespace — a genuine duplicate direct
                             * import, not one covered by a directory. */
                            const char *owner_module = imported_by_module(normalized_path);
                            char message[MESSAGE_BUFFER_LARGE_SIZE], help[MESSAGE_BUFFER_SIZE];
                            if (owner_module) {
                                snprintf(message, sizeof(message),
                                    "'%s' is already imported in this file as part of module '%s'",
                                    item->path, owner_module);
                                snprintf(help, sizeof(help),
                                    "use the '%s' namespace, or remove one of the imports",
                                    owner_module);
                            } else {
                                snprintf(message, sizeof(message),
                                    "'%s' is already imported in this file", item->path);
                                snprintf(help, sizeof(help), "remove the duplicate import");
                            }
                            diagnostic_error_help(diagnostics, "E6011", strdup(message),
                                statement_file, import_statement->token.line, import_statement->token.column, 0, help);
                        }
                        continue;
                    }
                    mark_imported_from(normalized_path, module_name, statement_file, is_directory_import);

                    /* Attribute this file to its module. Every file of a
                     * directory import records the same module name, which is
                     * what merges their declarations into one scope. */
                    if (module_file_count >= module_file_capacity) {
                        module_file_capacity = GROW_NEXT_CAPACITY(module_file_capacity);
                        ARENA_GROW_TO(arena, module_files, module_file_count, module_file_capacity);
                        ARENA_GROW_TO(arena, module_names, module_file_count, module_file_capacity);
                    }
                    module_files[module_file_count] = arena_copy_string(arena, current_file_path);
                    module_names[module_file_count] = module_name;
                    module_file_count++;

                    /* Read and parse the imported file */
                    char *imported_source = gray_read_file(current_file_path, false);
                    if (!imported_source) {
                        char message[MESSAGE_BUFFER_LARGE_SIZE];
                        snprintf(message, sizeof(message), "cannot find file or directory '%s'", current_file_path);
                        diagnostic_error_message(diagnostics, "E6002", strdup(message),
                            statement_file, import_statement->token.line, import_statement->token.column, 0);
                        continue;
                    }

                    Lexer *imported_lexer = lexer_create(arena, imported_source, current_file_path);
                    Parser *imported_parser = parser_create(arena, imported_lexer, current_file_path, diagnostics);
                    AstNode *imported_program = parser_parse_program(imported_parser);

                    if (!imported_program || diagnostic_has_errors(diagnostics)) continue;

                    /* Inject transitive import statements into the main program.
                     * Sibling imports (pointing to other files in the same directory)
                     * are NOT injected — instead their module alias is added to the
                     * rewrite mapping so qualified references like types.Item get
                     * rewritten to mylib.Item → resolves as mylib_Item. */
                    {
                        char current_directory[PATH_BUFFER_SIZE];
                        strncpy(current_directory, current_file_path, sizeof(current_directory) - 1);
                        current_directory[sizeof(current_directory) - 1] = '\0';
                        char *current_directory_separator = gray_path_last_separator(current_directory);
                        if (current_directory_separator) *(current_directory_separator + 1) = '\0';
                        else { current_directory[0] = '.'; current_directory[1] = '/'; current_directory[2] = '\0'; }
                        const char *importing_directory = arena_copy_string(arena, current_directory);

                        /* Normalize the directory being imported for sibling detection */
                        char *normalized_import_directory = gray_realpath(import_path);

                        /* Siblings never reach the duplicate check above: they
                         * are turned into alias mappings and dropped instead of
                         * being queued as imports. Track the ones this file has
                         * named so it cannot name one twice either. */
                        const char **seen_siblings = NULL;
                        int seen_sibling_count = 0, seen_sibling_capacity = 0;

                        for (int transitive_index = 0; transitive_index < imported_program->data.program.statement_count; transitive_index++) {
                            AstNode *transitive_statement = imported_program->data.program.statements[transitive_index];
                            if (transitive_statement->kind != NODE_IMPORT_STATEMENT) continue;

                            bool is_all_sibling = true;
                            for (int transitive_item_index = 0; transitive_item_index < transitive_statement->data.import_statement.count; transitive_item_index++) {
                                ImportItem *transitive_item = &transitive_statement->data.import_statement.items[transitive_item_index];
                                if (transitive_item->is_c_import) {
                                    /* A local ("./x.h") header is resolved
                                     * relative to the importing file, same as
                                     * an ordinary import — main.c's header
                                     * lookups fall back to the entry file's
                                     * directory when source_directory is unset, so
                                     * a local header must get this file's
                                     * directory here, not be skipped like a
                                     * stdlib import (which never consults
                                     * source_directory at all). */
                                    is_all_sibling = false;
                                    if (!transitive_item->source_directory) transitive_item->source_directory = importing_directory;
                                    continue;
                                }
                                if (transitive_item->is_stdlib) {
                                    is_all_sibling = false;
                                    continue;
                                }
                                if (!transitive_item->path) continue;

                                /* Resolve the transitive import path */
                                const char *transitive_relative_path = transitive_item->path;
                                if (transitive_relative_path[0] == '.' && transitive_relative_path[1] == '/') transitive_relative_path += 2;
                                char transitive_path[PATH_BUFFER_SIZE];
                                snprintf(transitive_path, sizeof(transitive_path), "%s%s", importing_directory, transitive_relative_path);

                                /* Check if it resolves to a file inside the same directory */
                                size_t transitive_path_length = strlen(transitive_path);
                                bool is_sibling = false;
                                /* Try with .gray extension if not already present */
                                char transitive_path_gray[PATH_BUFFER_SIZE];
                                const char *transitive_path_check = transitive_path;
                                if (transitive_path_length < GRAY_EXTENSION_LENGTH || strcmp(transitive_path + transitive_path_length - GRAY_EXTENSION_LENGTH, GRAY_EXTENSION) != 0) {
                                    snprintf(transitive_path_gray, sizeof(transitive_path_gray), "%s.gray", transitive_path);
                                    transitive_path_check = transitive_path_gray;
                                }
                                char *normalized_transitive_path = gray_realpath(transitive_path_check);
                                const char *sibling_path = normalized_transitive_path
                                    ? arena_copy_string(arena, normalized_transitive_path) : NULL;
                                if (normalized_transitive_path && normalized_import_directory) {
                                    /* Check if the file's directory matches import_path.
                                     * Both buffers are ours to truncate in place. */
                                    char *transitive_separator = gray_path_last_separator(normalized_transitive_path);
                                    if (transitive_separator) {
                                        *transitive_separator = '\0';
                                        /* Strip trailing separator from normalized_import_directory */
                                        size_t import_directory_length = strlen(normalized_import_directory);
                                        if (import_directory_length > 0 &&
                                            gray_is_path_separator(normalized_import_directory[import_directory_length - 1]))
                                            normalized_import_directory[import_directory_length - 1] = '\0';
                                        if (gray_path_equal(normalized_transitive_path, normalized_import_directory)) {
                                            is_sibling = true;
                                        }
                                    }
                                }
                                free(normalized_transitive_path);

                                if (is_sibling) {
                                    bool is_sibling_duplicate = false;
                                    for (int sibling_index = 0; sibling_index < seen_sibling_count && sibling_path; sibling_index++) {
                                        if (strcmp(seen_siblings[sibling_index], sibling_path) != 0) continue;
                                        char message[MESSAGE_BUFFER_LARGE_SIZE];
                                        snprintf(message, sizeof(message),
                                            "'%s' is already imported in this file", transitive_item->path);
                                        diagnostic_error_help(diagnostics, "E6011", strdup(message),
                                            transitive_statement->token.file ? transitive_statement->token.file : current_file_path,
                                            transitive_statement->token.line, transitive_statement->token.column, 0,
                                            "remove the duplicate import");
                                        is_sibling_duplicate = true;
                                        break;
                                    }
                                    if (!is_sibling_duplicate && sibling_path) {
                                        if (seen_sibling_count >= seen_sibling_capacity) {
                                            seen_sibling_capacity = GROW_NEXT_CAPACITY(seen_sibling_capacity);
                                            ARENA_GROW_TO(arena, seen_siblings,
                                                seen_sibling_count, seen_sibling_capacity);
                                        }
                                        seen_siblings[seen_sibling_count++] = sibling_path;
                                    }
                                    /* Collect sibling alias for compound mapping generation later */
                                    const char *sibling_alias = transitive_item->alias;
                                    if (!sibling_alias) {
                                        /* Derive alias from path (filename without .gray) */
                                        const char *sibling_base = gray_path_basename(transitive_relative_path);
                                        char sibling_name_buffer[MESSAGE_BUFFER_SIZE];
                                        size_t sibling_base_length = strlen(sibling_base);
                                        if (sibling_base_length > GRAY_EXTENSION_LENGTH && strcmp(sibling_base + sibling_base_length - GRAY_EXTENSION_LENGTH, GRAY_EXTENSION) == 0) {
                                            memcpy(sibling_name_buffer, sibling_base, sibling_base_length - GRAY_EXTENSION_LENGTH);
                                            sibling_name_buffer[sibling_base_length - GRAY_EXTENSION_LENGTH] = '\0';
                                        } else {
                                            snprintf(sibling_name_buffer, sizeof(sibling_name_buffer), "%s", sibling_base);
                                        }
                                        sibling_alias = arena_copy_string(arena, sibling_name_buffer);
                                    }
                                    /* Record the sibling's own name as an alias
                                     * of the directory module, so a qualified
                                     * reference through it resolves. */
                                    if (module_alias_count >= module_alias_capacity) {
                                        module_alias_capacity = GROW_NEXT_CAPACITY(module_alias_capacity);
                                        ARENA_GROW_TO(arena, module_alias_names,
                                            module_alias_count, module_alias_capacity);
                                        ARENA_GROW_TO(arena, module_alias_targets,
                                            module_alias_count, module_alias_capacity);
                                    }
                                    module_alias_names[module_alias_count] = sibling_alias;
                                    module_alias_targets[module_alias_count] = module_name;
                                    module_alias_count++;
                                    /* Null out the sibling import path so it's not injected */
                                    transitive_item->path = NULL;
                                } else {
                                    is_all_sibling = false;
                                    if (!transitive_item->source_directory) {
                                        transitive_item->source_directory = importing_directory;
                                    }
                                }
                            }

                            /* Only inject the import statement if it has non-sibling items */
                            if (!is_all_sibling) {
                                ARENA_GROW(arena, program->data.program.statements,
                                    program->data.program.statement_count, program->data.program.statement_capacity);
                                ARENA_GROW(arena, import_queue, queue_tail, import_queue_capacity);
                                import_queue[queue_tail++] = transitive_statement;
                                program->data.program.statements[program->data.program.statement_count++] = transitive_statement;
                            }
                        }
                        free(normalized_import_directory);
                    }

                    /* Store the parsed program for the merge pass */
                    parsed_programs[parsed_count] = imported_program;
                    parsed_paths[parsed_count] = current_file_path;
                    parsed_count++;
                }

                /* Merge pass */
                for (int parsed_index = 0; parsed_index < parsed_count; parsed_index++) {
                    AstNode *imported_program = parsed_programs[parsed_index];

                /* Merge imported declarations into the main program.
                 * Two passes: var_decls first (so they are in scope for
                 * function bodies), then everything else.
                 *
                 * Names are left exactly as written. Which module a
                 * declaration belongs to is recorded by file, and every
                 * reference to it is resolved against the symbol table. */

                /* Pass 1: variable declarations */
                for (int merge_index = 0; merge_index < imported_program->data.program.statement_count; merge_index++) {
                    AstNode *imported_statement = imported_program->data.program.statements[merge_index];
                    if (imported_statement->kind != NODE_VARIABLE_DECLARATION) continue;

                    ARENA_GROW(arena, program->data.program.statements,
                        program->data.program.statement_count, program->data.program.statement_capacity);
                    int insert_index = 0;
                    for (int position = 0; position < program->data.program.statement_count; position++) {
                        if (program->data.program.statements[position]->kind == NODE_IMPORT_STATEMENT ||
                            program->data.program.statements[position]->kind == NODE_USING_STATEMENT) {
                            insert_index = position + 1;
                        } else break;
                    }
                    memmove(&program->data.program.statements[insert_index + 1],
                            &program->data.program.statements[insert_index],
                            sizeof(AstNode *) * (program->data.program.statement_count - insert_index));
                    program->data.program.statements[insert_index] = imported_statement;
                    program->data.program.statement_count++;
                }

                /* Pass 2: functions, structs, enums */
                for (int merge_index = 0; merge_index < imported_program->data.program.statement_count; merge_index++) {
                    AstNode *imported_statement = imported_program->data.program.statements[merge_index];
                    /* Skip import/module/var declarations (vars handled in Pass 1).
                     * Using statements are preserved so the typechecker can scope
                     * them per-file and prevent transitive type leaking. */
                    if (imported_statement->kind == NODE_IMPORT_STATEMENT ||
                        imported_statement->kind == NODE_MODULE_DECLARATION ||
                        imported_statement->kind == NODE_VARIABLE_DECLARATION) continue;

                    /* Insert into main program BEFORE existing declarations.
                     * This ensures imported constants/functions are visible to all code. */
                    ARENA_GROW(arena, program->data.program.statements,
                        program->data.program.statement_count, program->data.program.statement_capacity);
                    /* Find insertion point: after imports/using/var_decls */
                    int insert_index = 0;
                    for (int position = 0; position < program->data.program.statement_count; position++) {
                        if (program->data.program.statements[position]->kind == NODE_IMPORT_STATEMENT ||
                            program->data.program.statements[position]->kind == NODE_USING_STATEMENT ||
                            program->data.program.statements[position]->kind == NODE_VARIABLE_DECLARATION) {
                            insert_index = position + 1;
                        } else {
                            break;
                        }
                    }
                    /* Shift existing stmts to make room */
                    memmove(&program->data.program.statements[insert_index + 1],
                            &program->data.program.statements[insert_index],
                            sizeof(AstNode *) * (program->data.program.statement_count - insert_index));
                    program->data.program.statements[insert_index] = imported_statement;
                    program->data.program.statement_count++;
                }
                } /* end for (pi: rewrite+merge pass) */

                /* Mark this import item as fully processed. */
                item->path = NULL;
            }
        } /* end while (queue_head < queue_tail) */

    out->files = module_files;
    out->modules = module_names;
    out->count = module_file_count;
    out->alias_names = module_alias_names;
    out->alias_targets = module_alias_targets;
    out->alias_count = module_alias_count;
}
