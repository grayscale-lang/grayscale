/*
 * constants.h — Shared numeric constants referenced across multiple
 * compiler stages, including diagnostic buffer sizes and time conversions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_CONSTANTS_H
#define GRAY_CONSTANTS_H

/* --- Diagnostic buffer sizes --- */
#define MESSAGE_BUFFER_SIZE         256
#define MESSAGE_BUFFER_LARGE_SIZE        512
#define SOURCE_LINE_MAX      2048
#define TYPE_NAME_MAX        128
#define MAX_IDENTIFIER_LENGTH 255
/* Buffer large enough for "name_name\0" (two max-length identifiers joined) */
#define IDENTIFIER_BUFFER_SIZE            (TYPE_NAME_MAX * 2 + 2)

/* --- Time unit conversions --- */
#define NANOSECONDS_PER_SECOND              1000000000LL
#define NANOSECONDS_PER_MILLISECOND               1000000LL
#define MILLISECONDS_PER_SECOND              1000LL

#endif
