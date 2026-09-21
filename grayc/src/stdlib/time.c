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

#define SECONDS_PER_DAY 86400
#define SECONDS_PER_HOUR 3600
#define SECONDS_PER_MINUTE 60

#if defined(_WIN32)
/* mingw-w64 implements strptime in libmingwex (linked by default) but never
 * declares it in <time.h>, so GCC 14's -Wimplicit-function-declaration (now an
 * error) rejects the call. Declare it ourselves. */
char *strptime(const char *s, const char *format, struct tm *tm);
#endif

/* The UTC inverse of mktime. glibc/BSD/macOS provide timegm, but only when
 * a permissive feature-test macro unlocks it, which this file's own
 * _XOPEN_SOURCE (and the Makefile's _POSIX_C_SOURCE) both suppress; there is
 * no standard Windows equivalent at all. Implemented directly instead of
 * chasing per-platform feature macros: Howard Hinnant's days_from_civil,
 * a closed-form Gregorian date -> day-count conversion valid for every
 * proleptic Gregorian year, positive or negative. */
static int64_t gray_days_from_civil(int64_t y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;                                   /* [0, 399] */
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;   /* [0, 365] */
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;            /* [0, 146096] */
    return era * 146097 + doe - 719468;                             /* days since 1970-01-01 */
}

static time_t gray_timegm(struct tm *tm) {
    int64_t days = gray_days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    return (time_t)(days * SECONDS_PER_DAY + tm->tm_hour * SECONDS_PER_HOUR +
                     tm->tm_min * SECONDS_PER_MINUTE + tm->tm_sec);
}

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
    return gmtime(&t);
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
    return gray_time_format(arena, gray_string_lit("%Y-%m-%dT%H:%M:%SZ"), ts);
}

GrayString gray_time_date(GrayArena *arena, int64_t ts) {
    return gray_time_format(arena, gray_string_lit("%Y-%m-%d"), ts);
}

GrayString gray_time_to_clock(GrayArena *arena, int64_t ts) {
    return gray_time_format(arena, gray_string_lit("%H:%M:%S"), ts);
}

/* Parses text against layout and converts the result to a Unix timestamp.
 * Returns true on a full match of a real calendar date.
 *
 * strptime range-checks each field in isolation, so it accepts a day that
 * does not exist in the parsed month (2023-02-29, day zero, Apr 31, ...).
 * gray_timegm, unlike mktime, does not normalize an impossible date into a
 * real one, so those must be rejected explicitly before converting: month
 * in range, then day against the real length of that month/year. */
static bool time_parse_to_timestamp(GrayString text, GrayString layout, int64_t *out) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    char *end = strptime(text.data, layout.data, &tm);
    if (end == NULL || *end != '\0') return false;

    int64_t year = tm.tm_year + 1900;
    int month = tm.tm_mon + 1;
    if (month < 1 || month > 12) return false;
    if (tm.tm_mday < 1 || tm.tm_mday > gray_time_days_in_month(year, month)) return false;

    *out = (int64_t)gray_timegm(&tm);
    return true;
}

int64_t gray_time_parse(GrayString text, GrayString layout) {
    int64_t ts;
    if (!time_parse_to_timestamp(text, layout, &ts))
        gray_panic_code("P0105", "time.parse: cannot parse '%s' with layout '%s'", text.data, layout.data);
    return ts;
}

GrayResult_int gray_time_parse_result(GrayString text, GrayString layout) {
    int64_t ts;
    if (!time_parse_to_timestamp(text, layout, &ts)) {
        GrayError *err = gray_error_new(gray_default_arena, GRAY_ERR_ParseFailure, gray_string_format(gray_default_arena,
            "cannot parse '%.*s' with layout '%.*s'", text.len, text.data, layout.len, layout.data));
        return (GrayResult_int){0, err};
    }
    return (GrayResult_int){ts, NULL};
}

int64_t gray_time_diff(int64_t start, int64_t end) { return gray_sub_check(end, start, __FILE__, __LINE__); }

int64_t gray_time_since(int64_t start) { return gray_time_diff(start, gray_time_now()); }

int64_t gray_time_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * NS_PER_SEC + (int64_t)ts.tv_nsec;
}

int64_t gray_time_elapsed_ms(int64_t start_tick) {
    return (gray_time_tick() - start_tick) / NS_PER_MS;
}

static int64_t time_floordiv(int64_t numerator, int64_t denominator) {
    int64_t quotient = numerator / denominator, remainder = numerator % denominator;
    if (remainder != 0 && ((remainder < 0) != (denominator < 0))) quotient--;
    return quotient;
}

GrayString gray_time_humanize(GrayArena *arena, int64_t seconds) {
    if (seconds == 0) return gray_string_lit("just now");
    bool past = seconds > 0;
    int64_t abs_seconds = past ? seconds : -seconds;
    static const struct { int64_t size; const char *name; } units[] = {
        {31536000, "year"}, {2592000, "month"}, {604800, "week"},
        {SECONDS_PER_DAY, "day"}, {SECONDS_PER_HOUR, "hour"}, {SECONDS_PER_MINUTE, "minute"}, {1, "second"}
    };
    for (int i = 0; i < 7; i++) {
        if (abs_seconds >= units[i].size) {
            int64_t count = abs_seconds / units[i].size;
            const char *plural = count == 1 ? "" : "s";
            return past
                ? gray_string_format(arena, "%lld %s%s ago", (long long)count, units[i].name, plural)
                : gray_string_format(arena, "in %lld %s%s", (long long)count, units[i].name, plural);
        }
    }
    return gray_string_lit("just now"); /* unreachable: abs_seconds >= 1 */
}

/* Parse "1h30m15s" style durations. Units: s m h d. Returns false on an empty
 * string, a number with no unit, or an unknown unit. */
static bool time_parse_duration_impl(GrayString text, int64_t *out) {
    int64_t total = 0;
    int32_t pos = 0;
    bool matched_any = false;
    while (pos < text.len) {
        if (text.data[pos] < '0' || text.data[pos] > '9') return false;
        int64_t value = 0;
        while (pos < text.len && text.data[pos] >= '0' && text.data[pos] <= '9') {
            value = value * 10 + (text.data[pos] - '0');
            pos++;
        }
        if (pos >= text.len) return false; /* trailing number with no unit */
        int64_t unit_seconds;
        switch (text.data[pos++]) {
            case 's': unit_seconds = 1; break;
            case 'm': unit_seconds = SECONDS_PER_MINUTE; break;
            case 'h': unit_seconds = SECONDS_PER_HOUR; break;
            case 'd': unit_seconds = SECONDS_PER_DAY; break;
            default: return false;
        }
        total += value * unit_seconds;
        matched_any = true;
    }
    if (!matched_any) return false;
    *out = total;
    return true;
}

int64_t gray_time_parse_duration(GrayString text) {
    int64_t seconds;
    if (!time_parse_duration_impl(text, &seconds))
        gray_panic_code("P0127", "time.parse_duration: cannot parse '%s'", text.data);
    return seconds;
}

GrayResult_int gray_time_parse_duration_result(GrayString text) {
    int64_t seconds;
    if (!time_parse_duration_impl(text, &seconds)) {
        GrayError *err = gray_error_new(gray_default_arena, GRAY_ERR_ParseFailure,
            gray_string_format(gray_default_arena,
                "cannot parse duration '%.*s'", text.len, text.data));
        return (GrayResult_int){0, err};
    }
    return (GrayResult_int){seconds, NULL};
}

/* Space-separated "1h 30m 15s" with zero components omitted. Capped at hours
 * (no days bucket), so 90000 seconds is "25h". A lone zero is "0s". Negative
 * gets a leading minus. Not a strict inverse of parse_duration. */
GrayString gray_time_format_duration(GrayArena *arena, int64_t seconds) {
    if (seconds == 0) return gray_string_lit("0s");
    bool neg = seconds < 0;
    int64_t abs_seconds = neg ? -seconds : seconds;
    int64_t hours = abs_seconds / SECONDS_PER_HOUR;
    int64_t minutes = (abs_seconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE;
    int64_t secs = abs_seconds % SECONDS_PER_MINUTE;

    char buf[64];
    int pos = 0;
    if (neg) buf[pos++] = '-';
    if (hours > 0)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%lldh", (long long)hours);
    if (minutes > 0)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s%lldm",
                        hours > 0 ? " " : "", (long long)minutes);
    if (secs > 0)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s%llds",
                        hours > 0 || minutes > 0 ? " " : "", (long long)secs);
    return gray_string_new(arena, buf, pos);
}

int64_t gray_time_add_days(int64_t ts, int64_t days)       { return gray_add_check(ts, days * SECONDS_PER_DAY, __FILE__, __LINE__); }
int64_t gray_time_add_hours(int64_t ts, int64_t hours)     { return gray_add_check(ts, hours * SECONDS_PER_HOUR, __FILE__, __LINE__); }
int64_t gray_time_add_seconds(int64_t ts, int64_t seconds) { return gray_add_check(ts, seconds, __FILE__, __LINE__); }

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
    int weekday = get_tm(ts)->tm_wday;
    if (weekday < 0 || weekday > 6) weekday = 0;
    return gray_string_new(arena, names[weekday], (int32_t)strlen(names[weekday]));
}

GrayString gray_time_month_name(GrayArena *arena, int64_t ts) {
    static const char *const names[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    int month = get_tm(ts)->tm_mon;
    if (month < 0 || month > 11) month = 0;
    return gray_string_new(arena, names[month], (int32_t)strlen(names[month]));
}
