/*
 * c_split.h — Splits the generated C of one program into several sources that
 * a C compiler can build in parallel.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2026-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_C_SPLIT_H
#define GRAYC_C_SPLIT_H

/* The most sources one program is split into. */
#define MAX_C_UNITS 64

/* Splits `source` into `unit_count` sources, each holding about the same bytes
 * of function bodies. The source that holds `main` also defines every global
 * and the signature table; the others declare the globals `extern`. Every
 * function loses `static` so the sources can call one another.
 *
 * Returns an array of `unit_count` heap strings (free each, then the array),
 * or NULL when `unit_count` is below 2 or the source has a shape this does not
 * know; the caller then compiles the program whole. */
char **split_c_units(const char *source, int unit_count);

#endif
