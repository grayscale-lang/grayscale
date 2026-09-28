/*
 * fmt.h — Public interface for the Grayscale source formatter. Declares the
 * gray_fmt_source function that re-indents Grayscale source files while
 * preserving all content verbatim.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_FMT_H
#define GRAYC_FMT_H

#include <stdio.h>

/*
 * gray_fmt_source:
 *   Format the Grayscale source in `source` (NUL-terminated) and write the result to
 *   `output`. Returns 0 on success, non-zero if the source could not be lexed.
 *
 *   Strategy: lex the source to build a per-line indentation depth table,
 *   then re-emit each original source line with corrected leading whitespace.
 *   All content (comments, string literals, operators) is preserved verbatim —
 *   only the leading indentation of each line is touched.
 */
int gray_fmt_source(const char *source, const char *filename, FILE *output);

#endif
