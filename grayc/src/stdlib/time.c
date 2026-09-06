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
        GrayError *err = gray_error_new(gray_default_arena, GRAY_ERR_ParseFailure, gray_string_format(gray_default_arena,
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

#define SECONDS_PER_DAY 86400
#define SECONDS_PER_HOUR 3600
#define SECONDS_PER_MINUTE 60

static int64_t time_floordiv(int64_t a, int64_t b) {
    int64_t q = a / b, r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) q--;
    return q;
}

GrayString gray_time_humanize(GrayArena *arena, int64_t seconds) {
    if (seconds == 0) return gray_string_lit("just now");
    bool past = seconds > 0;
    int64_t s = past ? seconds : -seconds;
    static const struct { int64_t size; const char *name; } units[] = {
        {31536000, "year"}, {2592000, "month"}, {604800, "week"},
        {SECONDS_PER_DAY, "day"}, {SECONDS_PER_HOUR, "hour"}, {SECONDS_PER_MINUTE, "minute"}, {1, "second"}
    };
    for (int i = 0; i < 7; i++) {
        if (s >= units[i].size) {
            int64_t n = s / units[i].size;
            const char *plural = n == 1 ? "" : "s";
            return past
                ? gray_string_format(arena, "%lld %s%s ago", (long long)n, units[i].name, plural)
                : gray_string_format(arena, "in %lld %s%s", (long long)n, units[i].name, plural);
        }
    }
    return gray_string_lit("just now"); /* unreachable: s >= 1 */
}

/* Parse "1h30m15s" style durations. Units: s m h d. Returns false on an empty
 * string, a number with no unit, or an unknown unit. */
static bool time_parse_duration_impl(GrayString s, int64_t *out) {
    int64_t total = 0;
    int32_t i = 0;
    bool any = false;
    while (i < s.len) {
        if (s.data[i] < '0' || s.data[i] > '9') return false;
        int64_t num = 0;
        while (i < s.len && s.data[i] >= '0' && s.data[i] <= '9') {
            num = num * 10 + (s.data[i] - '0');
            i++;
        }
        if (i >= s.len) return false; /* trailing number with no unit */
        int64_t mult;
        switch (s.data[i++]) {
            case 's': mult = 1; break;
            case 'm': mult = SECONDS_PER_MINUTE; break;
            case 'h': mult = SECONDS_PER_HOUR; break;
            case 'd': mult = SECONDS_PER_DAY; break;
            default: return false;
        }
        total += num * mult;
        any = true;
    }
    if (!any) return false;
    *out = total;
    return true;
}

int64_t gray_time_parse_duration(GrayString s) {
    int64_t v;
    if (!time_parse_duration_impl(s, &v))
        gray_panic_code("P0127", "time.parse_duration: cannot parse '%s'", s.data);
    return v;
}

GrayResult_int gray_time_parse_duration_result(GrayString s) {
    int64_t v;
    if (!time_parse_duration_impl(s, &v)) {
        GrayError *err = gray_error_new(gray_default_arena, GRAY_ERR_ParseFailure,
            gray_string_format(gray_default_arena,
                "cannot parse duration '%.*s'", s.len, s.data));
        return (GrayResult_int){0, err};
    }
    return (GrayResult_int){v, NULL};
}

/* Space-separated "1h 30m 15s". Capped at hours (no days bucket), so
 * 90000 seconds is "25h 0m 0s". A lone zero is "0s". Negative gets a
 * leading minus. Not a strict inverse of parse_duration. */
GrayString gray_time_format_duration(GrayArena *arena, int64_t seconds) {
    if (seconds == 0) return gray_string_lit("0s");
    bool neg = seconds < 0;
    int64_t s = neg ? -seconds : seconds;
    int64_t h = s / SECONDS_PER_HOUR;
    int64_t m = (s % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE;
    int64_t sec = s % SECONDS_PER_MINUTE;

    char buf[64];
    int pos = 0;
    if (neg) buf[pos++] = '-';
    if (h > 0)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%lldh %lldm %llds",
                        (long long)h, (long long)m, (long long)sec);
    else if (m > 0)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%lldm %llds",
                        (long long)m, (long long)sec);
    else
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%llds", (long long)sec);
    return gray_string_new(arena, buf, pos);
}

int64_t gray_time_add_days(int64_t ts, int64_t n)    { return gray_add_check(ts, n * SECONDS_PER_DAY, __FILE__, __LINE__); }
int64_t gray_time_add_hours(int64_t ts, int64_t n)   { return gray_add_check(ts, n * SECONDS_PER_HOUR, __FILE__, __LINE__); }
int64_t gray_time_add_seconds(int64_t ts, int64_t n) { return gray_add_check(ts, n, __FILE__, __LINE__); }

int64_t gray_time_start_of_day(int64_t ts) { return time_floordiv(ts, SECONDS_PER_DAY) * SECONDS_PER_DAY; }
int64_t gray_time_end_of_day(int64_t ts)   { return gray_time_start_of_day(ts) + SECONDS_PER_DAY - 1; }

int64_t gray_time_days_in_month(int64_t year, int64_t month) {
    if (month < 1 || month > 12) {
        gray_panic_code("P0128", "time.days_in_month: month must be 1-12 (got %lld)", (long long)month);
    }
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && gray_time_is_leap_year(year)) return 29;
    return mdays[month - 1];
}

int64_t gray_time_day_of_year(int64_t ts) { return get_tm(ts)->tm_yday + 1; }

GrayString gray_time_weekday_name(GrayArena *arena, int64_t ts) {
    static const char *const names[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    int w = get_tm(ts)->tm_wday;
    if (w < 0 || w > 6) w = 0;
    return gray_string_new(arena, names[w], (int32_t)strlen(names[w]));
}

GrayString gray_time_month_name(GrayArena *arena, int64_t ts) {
    static const char *const names[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    int mo = get_tm(ts)->tm_mon;
    if (mo < 0 || mo > 11) mo = 0;
    return gray_string_new(arena, names[mo], (int32_t)strlen(names[mo]));
}
