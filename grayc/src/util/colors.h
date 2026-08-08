/*
 * colors.h — Shared ANSI color escape macros used by both the compiler
 * diagnostic system (error.c) and the runtime panic output (runtime.c).
 *
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_COLORS_H
#define GRAY_COLORS_H

#define COL_RESET   "\033[0m"
#define COL_BOLD    "\033[1m"
#define COL_RED     "\033[31m"
#define COL_YELLOW  "\033[33m"
#define COL_BLUE    "\033[34m"
#define COL_CYAN    "\033[36m"

#endif
