/*
 * win32.h — The one sanctioned way to pull in <windows.h> from runtime and
 * stdlib code.
 *
 * Include this instead of <windows.h> directly. It undoes three collisions
 * that would otherwise corrupt Grayscale code silently:
 *
 *   GrayString  winuser.h declares GrayStringA/GrayStringW (a GDI text-drawing
 *               call) and defines GrayString as a macro aliasing the ANSI one.
 *               That rewrites every use of GrayString, the core runtime string
 *               type, into GrayStringA. The failure looks like nonsense errors
 *               such as "unknown type name 'GrayStringA'" in files that never
 *               mention Windows. We never call the Win32 function.
 *
 *   ERROR       wingdi.h defines ERROR as 0, colliding with diagnostic
 *               severity enums.
 *
 *   min / max   suppressed via NOMINMAX rather than undefined afterwards,
 *               since they are function-like macros.
 *
 * Author:  Aristomedes (@Aristomedes)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_WIN32_H
#define GRAY_WIN32_H

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#undef GrayString
#undef ERROR

#endif /* _WIN32 */

#endif
