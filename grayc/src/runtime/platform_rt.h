/*
 * platform_rt.h — Portability shims for the runtime and standard library,
 * which are linked into every compiled Grayscale program.
 *
 * This is deliberately separate from util/platform.h. That header serves the
 * compiler binary; this one serves user programs, and only runtime/ and
 * stdlib/ are on the include path when a program is built, so the two cannot
 * share a file.
 *
 * Keep this header free of <windows.h>: it defines ERROR, min, and max as
 * macros, which collide with stdlib code. Include it inside a .c file when a
 * module genuinely needs the Win32 API.
 *
 * Author:  Aristomedes (@Aristomedes)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_PLATFORM_RT_H
#define GRAY_PLATFORM_RT_H

#ifdef _WIN32
#define GRAY_RUNTIME_WINDOWS 1
#else
#define GRAY_RUNTIME_WINDOWS 0
#endif

#if GRAY_RUNTIME_WINDOWS

#include <direct.h>
#include <io.h>
#include <stdio.h>

/* Windows has no permission bits, so _mkdir takes no mode. Wrap it so callers
 * can keep passing the POSIX mode they would use everywhere else. */
#define gray_runtime_mkdir(path, mode) _mkdir(path)

#define gray_runtime_isatty(file_descriptor)      _isatty(file_descriptor)
#define gray_runtime_stderr_fileno() _fileno(stderr)

#else

#include <sys/stat.h>
#include <unistd.h>

#define gray_runtime_mkdir(path, mode) mkdir((path), (mode))

#define gray_runtime_isatty(file_descriptor)      isatty(file_descriptor)
#define gray_runtime_stderr_fileno() STDERR_FILENO

#endif

#endif
