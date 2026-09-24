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
static int64_t gray_days_from_civil(int64_t year, int month, int day) {
    year -= month <= 2;
    int64_t era_index = (year >= 0 ? year : year - 399) / 400;
    int64_t year_of_era = year - era_index * 400;                                   /* [0, 399] */
    int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;   /* [0, 365] */
    int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;            /* [0, 146096] */
    return era_index * 146097 + day_of_era - 719468;                             /* days since 1970-01-01 */
}

static time_t gray_timegm(struct tm *time_parts) {
    int64_t days = gray_days_from_civil(time_parts->tm_year + 1900, time_parts->tm_mon + 1, time_parts->tm_mday);
    return (time_t)(days * SECONDS_PER_DAY + time_parts->tm_hour * SECONDS_PER_HOUR +
                     time_parts->tm_min * SECONDS_PER_MINUTE + time_parts->tm_sec);
}

int64_t gray_time_now(void) { return (int64_t)time(NULL); }

int64_t gray_time_now_ms(void) {
    struct timespec time_spec;
    clock_gettime(CLOCK_REALTIME, &time_spec);
    return (int64_t)time_spec.tv_sec * MILLISECONDS_PER_SECOND + (int64_t)time_spec.tv_nsec / NANOSECONDS_PER_MILLISECOND;
}

int64_t gray_time_now_ns(void) {
    struct timespec time_spec;
    clock_gettime(CLOCK_REALTIME, &time_spec);
    return (int64_t)time_spec.tv_sec * NANOSECONDS_PER_SECOND + (int64_t)time_spec.tv_nsec;
}

static struct tm *get_time_parts(int64_t timestamp) {
    time_t time_value = (time_t)timestamp;
    return gmtime(&time_value);
}

int64_t gray_time_year(int64_t timestamp) { return get_time_parts(timestamp)->tm_year + 1900; }
int64_t gray_time_month(int64_t timestamp) { return get_time_parts(timestamp)->tm_mon + 1; }
int64_t gray_time_day(int64_t timestamp) { return get_time_parts(timestamp)->tm_mday; }
int64_t gray_time_hour(int64_t timestamp) { return get_time_parts(timestamp)->tm_hour; }
int64_t gray_time_minute(int64_t timestamp) { return get_time_parts(timestamp)->tm_min; }
int64_t gray_time_second(int64_t timestamp) { return get_time_parts(timestamp)->tm_sec; }
int64_t gray_time_weekday(int64_t timestamp) { return get_time_parts(timestamp)->tm_wday; }

bool gray_time_is_leap_year(int64_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

GrayString gray_time_format(GrayArena *arena, GrayString format, int64_t timestamp) {
    char buffer[MESSAGE_BUFFER_SIZE];
    struct tm *time_parts = get_time_parts(timestamp);
    int length = (int)strftime(buffer, sizeof(buffer), format.data, time_parts);
    return gray_string_new(arena, buffer, length);
}

GrayString gray_time_to_iso(GrayArena *arena, int64_t timestamp) {
    return gray_time_format(arena, gray_string_lit("%Y-%m-%dT%H:%M:%SZ"), timestamp);
}

GrayString gray_time_date(GrayArena *arena, int64_t timestamp) {
    return gray_time_format(arena, gray_string_lit("%Y-%m-%d"), timestamp);
}

GrayString gray_time_to_clock(GrayArena *arena, int64_t timestamp) {
    return gray_time_format(arena, gray_string_lit("%H:%M:%S"), timestamp);
}

/* Parses text against layout and converts the result to a Unix timestamp.
 * Returns true on a full match of a real calendar date.
 *
 * strptime range-checks each field in isolation, so it accepts a day that
 * does not exist in the parsed month (2023-02-29, day zero, Apr 31, ...).
 * gray_timegm, unlike mktime, does not normalize an impossible date into a
 * real one, so those must be rejected explicitly before converting: month
 * in range, then day against the real length of that month/year. */
static bool time_parse_to_timestamp(GrayString text, GrayString layout, int64_t *output) {
    struct tm time_parts;
    memset(&time_parts, 0, sizeof(time_parts));
    char *end_cursor = strptime(text.data, layout.data, &time_parts);
    if (end_cursor == NULL || *end_cursor != '\0') return false;

    int64_t year = time_parts.tm_year + 1900;
    int month = time_parts.tm_mon + 1;
    if (month < 1 || month > 12) return false;
    if (time_parts.tm_mday < 1 || time_parts.tm_mday > gray_time_days_in_month(year, month)) return false;

    *output = (int64_t)gray_timegm(&time_parts);
    return true;
}

int64_t gray_time_parse(GrayString text, GrayString layout) {
    int64_t timestamp;
    if (!time_parse_to_timestamp(text, layout, &timestamp))
        gray_panic_code("P0105", "time.parse: cannot parse '%s' with layout '%s'", text.data, layout.data);
    return timestamp;
}

GrayResult_i64 gray_time_parse_result(GrayString text, GrayString layout) {
    int64_t timestamp;
    if (!time_parse_to_timestamp(text, layout, &timestamp)) {
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ParseFailure, gray_string_format(gray_default_arena,
            "cannot parse '%.*s' with layout '%.*s'", text.len, text.data, layout.len, layout.data));
        return (GrayResult_i64){0, error};
    }
    return (GrayResult_i64){timestamp, NULL};
}

int64_t gray_time_diff(int64_t start, int64_t end_index) { return gray_sub_check(end_index, start, __FILE__, __LINE__); }

int64_t gray_time_since(int64_t start) { return gray_time_diff(start, gray_time_now()); }

int64_t gray_time_tick(void) {
    struct timespec time_spec;
    clock_gettime(CLOCK_MONOTONIC, &time_spec);
    return (int64_t)time_spec.tv_sec * NANOSECONDS_PER_SECOND + (int64_t)time_spec.tv_nsec;
}

int64_t gray_time_elapsed_ms(int64_t start_tick) {
    return (gray_time_tick() - start_tick) / NANOSECONDS_PER_MILLISECOND;
}

static int64_t time_floor_divide(int64_t numerator, int64_t denominator) {
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
static bool time_parse_duration_implementation(GrayString text, int64_t *output) {
    int64_t total = 0;
    int32_t position = 0;
    bool matched_any = false;
    while (position < text.len) {
        if (text.data[position] < '0' || text.data[position] > '9') return false;
        int64_t value = 0;
        while (position < text.len && text.data[position] >= '0' && text.data[position] <= '9') {
            value = value * 10 + (text.data[position] - '0');
            position++;
        }
        if (position >= text.len) return false; /* trailing number with no unit */
        int64_t unit_seconds;
        switch (text.data[position++]) {
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
    *output = total;
    return true;
}

int64_t gray_time_parse_duration(GrayString text) {
    int64_t seconds;
    if (!time_parse_duration_implementation(text, &seconds))
        gray_panic_code("P0127", "time.parse_duration: cannot parse '%s'", text.data);
    return seconds;
}

GrayResult_i64 gray_time_parse_duration_result(GrayString text) {
    int64_t seconds;
    if (!time_parse_duration_implementation(text, &seconds)) {
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ParseFailure,
            gray_string_format(gray_default_arena,
                "cannot parse duration '%.*s'", text.len, text.data));
        return (GrayResult_i64){0, error};
    }
    return (GrayResult_i64){seconds, NULL};
}

/* Space-separated "1h 30m 15s" with zero components omitted. Capped at hours
 * (no days bucket), so 90000 seconds is "25h". A lone zero is "0s". Negative
 * gets a leading minus. Not a strict inverse of parse_duration. */
GrayString gray_time_format_duration(GrayArena *arena, int64_t seconds) {
    if (seconds == 0) return gray_string_lit("0s");
    bool is_negative = seconds < 0;
    int64_t abs_seconds = is_negative ? -seconds : seconds;
    int64_t hours = abs_seconds / SECONDS_PER_HOUR;
    int64_t minutes = (abs_seconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE;
    int64_t secs = abs_seconds % SECONDS_PER_MINUTE;

    char buffer[64];
    int position = 0;
    if (is_negative) buffer[position++] = '-';
    if (hours > 0)
        position += snprintf(buffer + position, sizeof(buffer) - (size_t)position, "%lldh", (long long)hours);
    if (minutes > 0)
        position += snprintf(buffer + position, sizeof(buffer) - (size_t)position, "%s%lldm",
                        hours > 0 ? " " : "", (long long)minutes);
    if (secs > 0)
        position += snprintf(buffer + position, sizeof(buffer) - (size_t)position, "%s%llds",
                        hours > 0 || minutes > 0 ? " " : "", (long long)secs);
    return gray_string_new(arena, buffer, position);
}

int64_t gray_time_add_days(int64_t timestamp, int64_t days)       { return gray_add_check(timestamp, days * SECONDS_PER_DAY, __FILE__, __LINE__); }
int64_t gray_time_add_hours(int64_t timestamp, int64_t hours)     { return gray_add_check(timestamp, hours * SECONDS_PER_HOUR, __FILE__, __LINE__); }
int64_t gray_time_add_seconds(int64_t timestamp, int64_t seconds) { return gray_add_check(timestamp, seconds, __FILE__, __LINE__); }

int64_t gray_time_start_of_day(int64_t timestamp) { return time_floor_divide(timestamp, SECONDS_PER_DAY) * SECONDS_PER_DAY; }
int64_t gray_time_end_of_day(int64_t timestamp)   { return gray_time_start_of_day(timestamp) + SECONDS_PER_DAY - 1; }

int64_t gray_time_days_in_month(int64_t year, int64_t month) {
    if (month < 1 || month > 12) {
        gray_panic_code("P0128", "time.days_in_month: month must be 1-12 (got %lld)", (long long)month);
    }
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && gray_time_is_leap_year(year)) return 29;
    return mdays[month - 1];
}

int64_t gray_time_day_of_year(int64_t timestamp) { return get_time_parts(timestamp)->tm_yday + 1; }

GrayString gray_time_weekday_name(GrayArena *arena, int64_t timestamp) {
    static const char *const names[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    int weekday = get_time_parts(timestamp)->tm_wday;
    if (weekday < 0 || weekday > 6) weekday = 0;
    return gray_string_new(arena, names[weekday], (int32_t)strlen(names[weekday]));
}

GrayString gray_time_month_name(GrayArena *arena, int64_t timestamp) {
    static const char *const names[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    int month = get_time_parts(timestamp)->tm_mon;
    if (month < 0 || month > 11) month = 0;
    return gray_string_new(arena, names[month], (int32_t)strlen(names[month]));
}
