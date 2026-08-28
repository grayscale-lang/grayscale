/*
 * time.c — Implementation of the time stdlib module.
 * Provides current time queries (seconds, milliseconds, nanoseconds),
 * date/time component extraction, formatting, and elapsed-time
 * measurement.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700   /* strptime — hidden by glibc without this */
#endif

#include "time.h"
#include "../util/constants.h"
#include <time.h>
#include <string.h>

#if defined(_WIN32)
/* mingw-w64 implements strptime in libmingwex (linked by default) but never
 * declares it in <time.h>, so GCC 14's -Wimplicit-function-declaration (now an
 * error) rejects the call. Declare it ourselves. */
char *strptime(const char *s, const char *format, struct tm *tm);
#endif

int64_t gray_time_now(void) { return (int64_t)time(NULL); }

int64_t gray_time_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * MS_PER_SEC + (int64_t)ts.tv_nsec / NS_PER_MS;
}

int64_t gray_time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * NS_PER_SEC + (int64_t)ts.tv_nsec;
}

static struct tm *get_tm(int64_t ts) {
    time_t t = (time_t)ts;
    return localtime(&t);
}

int64_t gray_time_year(int64_t ts) { return get_tm(ts)->tm_year + 1900; }
int64_t gray_time_month(int64_t ts) { return get_tm(ts)->tm_mon + 1; }
int64_t gray_time_day(int64_t ts) { return get_tm(ts)->tm_mday; }
int64_t gray_time_hour(int64_t ts) { return get_tm(ts)->tm_hour; }
int64_t gray_time_minute(int64_t ts) { return get_tm(ts)->tm_min; }
int64_t gray_time_second(int64_t ts) { return get_tm(ts)->tm_sec; }
int64_t gray_time_weekday(int64_t ts) { return get_tm(ts)->tm_wday; }

bool gray_time_is_leap_year(int64_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

GrayString gray_time_format(GrayArena *arena, GrayString fmt, int64_t ts) {
    char buf[MSG_BUF_SIZE];
    struct tm *tm = get_tm(ts);
    int len = (int)strftime(buf, sizeof(buf), fmt.data, tm);
    return gray_string_new(arena, buf, len);
}

GrayString gray_time_to_iso(GrayArena *arena, int64_t ts) {
    return gray_time_format(arena, gray_string_lit("%Y-%m-%dT%H:%M:%S"), ts);
}

GrayString gray_time_date(GrayArena *arena, int64_t ts) {
    return gray_time_format(arena, gray_string_lit("%Y-%m-%d"), ts);
}

GrayString gray_time_to_clock(GrayArena *arena, int64_t ts) {
    return gray_time_format(arena, gray_string_lit("%H:%M:%S"), ts);
}

/* Parses s against layout and converts the result to a Unix timestamp.
 * Returns true on a full match of a real calendar date.
 *
 * strptime range-checks each field in isolation, so it accepts a day that
 * does not exist in the parsed month, and mktime is then documented to
 * normalize the impossible combination rather than fail: 2023-02-29 becomes
 * 2023-03-01, and day zero rolls backwards into the previous month. The
 * t == -1 guard does not catch either, since mktime only reports failure for
 * times it cannot represent at all. Compare the calendar fields across the
 * mktime call instead and reject anything it had to move.
 *
 * Only the date fields are compared. tm_hour is legitimately shifted when a
 * local time falls in a DST gap, and the time-of-day fields need no help:
 * strptime range-checks them and no cross-field normalization applies. */
static bool time_parse_to_timestamp(GrayString s, GrayString layout, int64_t *out) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    char *end = strptime(s.data, layout.data, &tm);
    if (end == NULL || *end != '\0') return false;

    int year = tm.tm_year, mon = tm.tm_mon, mday = tm.tm_mday;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) return false;
    if (tm.tm_year != year || tm.tm_mon != mon || tm.tm_mday != mday) return false;

    *out = (int64_t)t;
    return true;
}

int64_t gray_time_parse(GrayString s, GrayString layout) {
    int64_t ts;
    if (!time_parse_to_timestamp(s, layout, &ts))
        gray_panic_code("P0105", "time.parse: cannot parse '%s' with layout '%s'", s.data, layout.data);
    return ts;
}

GrayResult_int gray_time_parse_result(GrayString s, GrayString layout) {
    int64_t ts;
    if (!time_parse_to_timestamp(s, layout, &ts)) {
        GrayError *err = gray_error_new(gray_default_arena, gray_string_format(gray_default_arena,
            "cannot parse '%.*s' with layout '%.*s'", s.len, s.data, layout.len, layout.data));
        return (GrayResult_int){0, err};
    }
    return (GrayResult_int){ts, NULL};
}

int64_t gray_time_diff(int64_t t1, int64_t t2) { return gray_sub_check(t2, t1, __FILE__, __LINE__); }

int64_t gray_time_since(int64_t t) { return gray_time_diff(t, gray_time_now()); }

int64_t gray_time_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * NS_PER_SEC + (int64_t)ts.tv_nsec;
}

int64_t gray_time_elapsed_ms(int64_t start_tick) {
    return (gray_time_tick() - start_tick) / NS_PER_MS;
}
