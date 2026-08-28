/*
 * imports.h — Declares the import resolver: the pass that walks a program's
 * import statements, parses every .gray file they reach, mangles the imported
 * declarations under their module names, and merges them into the program.
 *
 * Runs between parsing and type checking. The file-to-module attribution it
 * collects along the way is the only record of which module a given source
 * file belongs to, so it is handed to the type checker afterwards.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_IMPORTS_H
#define GRAYC_IMPORTS_H

#include "ast.h"
#include "../util/arena.h"
#include "../util/error.h"

/* What the resolver learned about module membership while merging.
 * All arrays are arena-allocated and valid for the arena's lifetime. */
typedef struct {
    /* Parallel arrays: files[i] belongs to module modules[i]. */
    const char **files;
    const char **modules;
    int count;

    /* Parallel arrays: a sibling import inside a directory module names a
     * file, so alias_names[i] resolves to directory module alias_targets[i]. */
    const char **alias_names;
    const char **alias_targets;
    int alias_count;
} ImportResolution;

/* Resolve every import reachable from `program`, merging the declarations it
 * pulls in directly into `program`. Diagnostics for unresolvable imports,
 * module name collisions, and duplicate imports go to `diag`; the caller is
 * expected to stop on errors before type checking. */
void imports_resolve(Arena *arena, DiagnosticList *diag, AstNode *program,
                     const char *entry_file, ImportResolution *out);

#endif
