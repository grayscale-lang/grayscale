/*
 * defines.h — Declares the pass that applies `-define:NAME=VALUE` overrides
 * to the `#define` constants of a program.
 *
 * Runs between parsing and import resolution, so an override replaces a
 * constant's initializer before anything else is evaluated and every constant
 * initialized from it sees the new value.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_DEFINES_H
#define GRAYC_DEFINES_H

#include "ast.h"
#include "../util/arena.h"
#include "../util/error.h"

/* Apply each "NAME=VALUE" in `overrides` to the top-level constant `NAME` of
 * `program`. A name that is not declared (E4040), a constant without #define
 * (E5066), or a value that does not parse as the constant's type (E3214) is
 * reported to `diagnostics`; the caller stops on errors before type checking. */
void defines_apply(Arena *arena, DiagnosticList *diagnostics, AstNode *program,
                   const char *input_file, const char *const *overrides, int count);

#endif
