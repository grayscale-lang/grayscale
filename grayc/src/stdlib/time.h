/*
 * time.h — Public interface for the time stdlib module.
 * Declares current time queries, date/time component extraction,
 * formatting, and elapsed-time measurement functions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_TIME_H
#define GRAY_TIME_H

#include "../runtime/runtime.h"

/* Basename collides with the system <time.h>: in a grayc-generated program
 * this directory shadows libc, so step past it so `extern import "time.h"`
 * reaches the real header. See math.h for the full rationale. */
#ifdef GRAY_GENERATED_C
#  ifdef __has_include_next
#    if __has_include_next(<time.h>)
#      include_next <time.h>
#    endif
#  endif
#endif

/* Current time */

/*@man now
 *@module time
 *@group Current Time
 *@sig now() -> int
 *@desc Returns the current Unix timestamp in seconds.
 *@example
 *   import @time
 *   println(time.now())
 *@end
 */
int64_t gray_time_now(void);        /* Unix timestamp (seconds) */

/*@man now_ms
 *@module time
 *@group Current Time
 *@sig now_ms() -> int
 *@desc Returns the current Unix timestamp in milliseconds.
 *@example
 *   import @time
 *   println(time.now_ms())
 *@end
 */
int64_t gray_time_now_ms(void);     /* Milliseconds since epoch */

/*@man now_ns
 *@module time
 *@group Current Time
 *@sig now_ns() -> int
 *@desc Returns the current Unix timestamp in nanoseconds.
 *@example
 *   import @time
 *   println(time.now_ns())
 *@end
 */
int64_t gray_time_now_ns(void);     /* Nanoseconds since epoch */

/* Components */

/*@man year
 *@module time
 *@group Components
 *@sig year(timestamp int) -> int
 *@desc Returns the year from a Unix timestamp.
 *@example
 *   import @time
 *   println(time.year(time.now()))
 *@end
 */
int64_t gray_time_year(int64_t ts);

/*@man month
 *@module time
 *@group Components
 *@sig month(timestamp int) -> int
 *@desc Returns the month (1–12) from a Unix timestamp.
 *@example
 *   import @time
 *   println(time.month(time.now()))
 *@end
 */
int64_t gray_time_month(int64_t ts);

/*@man day
 *@module time
 *@group Components
 *@sig day(timestamp int) -> int
 *@desc Returns the day of the month from a Unix timestamp.
 *@example
 *   import @time
 *   println(time.day(time.now()))
 *@end
 */
int64_t gray_time_day(int64_t ts);

/*@man hour
 *@module time
 *@group Components
 *@sig hour(timestamp int) -> int
 *@desc Returns the hour (0–23) from a Unix timestamp.
 *@example
 *   import @time
 *   println(time.hour(time.now()))
 *@end
 */
int64_t gray_time_hour(int64_t ts);

/*@man minute
 *@module time
 *@group Components
 *@sig minute(timestamp int) -> int
 *@desc Returns the minute (0–59) from a Unix timestamp.
 *@example
 *   import @time
 *   println(time.minute(time.now()))
 *@end
 */
int64_t gray_time_minute(int64_t ts);

/*@man second
 *@module time
 *@group Components
 *@sig second(timestamp int) -> int
 *@desc Returns the second (0–59) from a Unix timestamp.
 *@example
 *   import @time
 *   println(time.second(time.now()))
 *@end
 */
int64_t gray_time_second(int64_t ts);

/*@man weekday
 *@module time
 *@group Components
 *@sig weekday(timestamp int) -> int
 *@desc Returns the day of the week from a Unix timestamp. 0 = Sunday, 1 = Monday, ..., 6 = Saturday.
 *@example
 *   import @time
 *   println(time.weekday(time.now()))
 *@end
 */
int64_t gray_time_weekday(int64_t ts);

/*@man is_leap_year
 *@module time
 *@group Components
 *@sig is_leap_year(year int) -> bool
 *@desc Returns true if year is a leap year (divisible by 4, except centuries not divisible by 400).
 *@example
 *   import @time
 *   println(time.is_leap_year(2024))
 *@end
 */
bool gray_time_is_leap_year(int64_t year);

/* Formatting */

/*@man format
 *@module time
 *@group Formatting
 *@sig format(fmt string, timestamp int) -> string
 *@desc Formats a Unix timestamp using a format string. Uses strftime-style directives: %Y (year), %m (month), %d (day), %H (hour), %M (minute), %S (second).
 *@example
 *   import @time
 *   println(time.format("%Y-%m-%d", time.now()))
 *@end
 */
GrayString gray_time_format(GrayArena *arena, GrayString fmt, int64_t ts);

/*@man to_iso
 *@module time
 *@group Formatting
 *@sig to_iso(timestamp int) -> string
 *@desc Returns the timestamp as an ISO 8601 string (e.g. "2025-06-01T14:30:00Z").
 *@example
 *   import @time
 *   println(time.to_iso(time.now()))
 *@end
 */
GrayString gray_time_to_iso(GrayArena *arena, int64_t ts);

/*@man date
 *@module time
 *@group Formatting
 *@sig date(timestamp int) -> string
 *@desc Returns the date portion of a Unix timestamp as "YYYY-MM-DD".
 *@example
 *   import @time
 *   println(time.date(time.now()))
 *@end
 */
GrayString gray_time_date(GrayArena *arena, int64_t ts);

/*@man to_clock
 *@module time
 *@group Formatting
 *@sig to_clock(timestamp int) -> string
 *@desc Returns the time portion of a Unix timestamp as "HH:MM:SS".
 *@example
 *   import @time
 *   println(time.to_clock(time.now()))
 *@end
 */
GrayString gray_time_to_clock(GrayArena *arena, int64_t ts);

/* Parsing */

/*@man parse
 *@module time
 *@group Parsing
 *@sig parse(s string, layout string) -> (int, Error)
 *@desc Parses a time string into a Unix timestamp using a layout string with strftime-style directives (%Y, %m, %d, %H, %M, %S). Always use destructuring (`mut ts, err = ...` or `mut ts, _ = ...`) — single-variable assignment is a compile error. When s does not match layout, err is non-nil and, with `_`, ts is 0.
 *@example
 *   import @time
 *   mut ts, _ = time.parse("2025-06-01", "%Y-%m-%d")
 *   mut val, err = time.parse("not a date", "%Y-%m-%d")
 *@end
 */
int64_t gray_time_parse(GrayString s, GrayString layout);
GrayResult_int gray_time_parse_result(GrayString s, GrayString layout);

/* Arithmetic */

/*@man diff
 *@module time
 *@group Arithmetic
 *@sig diff(t1 int, t2 int) -> int
 *@desc Returns the difference between two Unix timestamps in seconds as t2 - t1. The result is negative if t1 is after t2.
 *@example
 *   import @time
 *   mut start int = time.now()
 *   mut end int = time.now()
 *   mut delta int = time.diff(start, end)
 *   println(delta)
 *@end
 */
int64_t gray_time_diff(int64_t t1, int64_t t2);

/*@man since
 *@module time
 *@group Arithmetic
 *@sig since(t int) -> int
 *@desc Returns the number of seconds elapsed from t to now. Equivalent to time.diff(t, time.now()).
 *@example
 *   import @time
 *   mut start int = time.now()
 *   mut elapsed int = time.since(start)
 *   println(elapsed)
 *@end
 */
int64_t gray_time_since(int64_t t);

/* Performance */

/*@man tick
 *@module time
 *@group Performance
 *@sig tick() -> int
 *@desc Returns a high-resolution timestamp in nanoseconds for performance measurement. Use with elapsed_ms() to measure durations.
 *@example
 *   import @time
 *   mut start int = time.tick()
 *   mut elapsed int = time.elapsed_ms(start)
 *   println(elapsed)
 *@end
 */
int64_t gray_time_tick(void);

/*@man elapsed_ms
 *@module time
 *@group Performance
 *@sig elapsed_ms(start_tick int) -> int
 *@desc Returns the number of milliseconds elapsed since the tick value returned by tick().
 *@example
 *   import @time
 *   mut start int = time.tick()
 *   mut elapsed int = time.elapsed_ms(start)
 *   println(elapsed)
 *@end
 */
int64_t gray_time_elapsed_ms(int64_t start_tick);

/*@man humanize
 *@module time
 *@group Formatting
 *@sig humanize(seconds int) -> string
 *@desc Renders a signed delta in seconds as a relative phrase. A positive value is in the past ("2 days ago"), a negative value is in the future ("in 1 hour"), and 0 is "just now". Only the largest whole unit is shown (second, minute, hour, day, week, month = 30 days, year = 365 days).
 *@example
 *   import @time
 *   println(time.humanize(90))
 *   println(time.humanize(-3700))
 *@end
 */
GrayString gray_time_humanize(GrayArena *arena, int64_t seconds);

/*@man parse_duration
 *@module time
 *@group Parsing
 *@sig parse_duration(s string) -> (int, Error)
 *@desc Parses a duration string like "1h30m", "90s", "2d", or "1h30m15s" into a total number of seconds. Units are s, m, h, d. A number with no unit, an unknown unit, or an empty string yields a non-nil error and a value of 0. Always use destructuring.
 *@example
 *   import @time
 *   mut secs, err = time.parse_duration("1h30m")
 *@end
 */
int64_t gray_time_parse_duration(GrayString s);
GrayResult_int gray_time_parse_duration_result(GrayString s);

/*@man format_duration
 *@module time
 *@group Formatting
 *@sig format_duration(seconds int) -> string
 *@desc Renders a duration in seconds as "1h 30m 15s". Components are capped at hours (no days), so 90000 is "25h 0m 0s". Zero components are omitted unless the whole value is zero, which is "0s". A negative value gets a leading minus.
 *@example
 *   import @time
 *   println(time.format_duration(5415))
 *@end
 */
GrayString gray_time_format_duration(GrayArena *arena, int64_t seconds);

/*@man add_days
 *@module time
 *@group Arithmetic
 *@sig add_days(timestamp int, n int) -> int
 *@desc Returns timestamp shifted by n days (n may be negative). Pure integer arithmetic on the Unix value; no calendar or DST logic.
 *@example
 *   import @time
 *   mut tomorrow int = time.add_days(time.now(), 1)
 *@end
 */
int64_t gray_time_add_days(int64_t timestamp, int64_t n);

/*@man add_hours
 *@module time
 *@group Arithmetic
 *@sig add_hours(timestamp int, n int) -> int
 *@desc Returns timestamp shifted by n hours (n may be negative).
 *@example
 *   import @time
 *   mut later int = time.add_hours(time.now(), -2)
 *@end
 */
int64_t gray_time_add_hours(int64_t timestamp, int64_t n);

/*@man add_seconds
 *@module time
 *@group Arithmetic
 *@sig add_seconds(timestamp int, n int) -> int
 *@desc Returns timestamp shifted by n seconds (n may be negative).
 *@example
 *   import @time
 *   mut soon int = time.add_seconds(time.now(), 30)
 *@end
 */
int64_t gray_time_add_seconds(int64_t timestamp, int64_t n);

/*@man start_of_day
 *@module time
 *@group Arithmetic
 *@sig start_of_day(timestamp int) -> int
 *@desc Returns the Unix timestamp of 00:00:00 UTC on the same day as timestamp.
 *@example
 *   import @time
 *   mut midnight int = time.start_of_day(time.now())
 *@end
 */
int64_t gray_time_start_of_day(int64_t timestamp);

/*@man end_of_day
 *@module time
 *@group Arithmetic
 *@sig end_of_day(timestamp int) -> int
 *@desc Returns the Unix timestamp of 23:59:59 UTC on the same day as timestamp (start_of_day + 86399).
 *@example
 *   import @time
 *   mut last int = time.end_of_day(time.now())
 *@end
 */
int64_t gray_time_end_of_day(int64_t timestamp);

/*@man days_in_month
 *@module time
 *@group Components
 *@sig days_in_month(year int, month int) -> int
 *@desc Returns the number of days in the given month (1-12) of the given year, accounting for leap years. Panics if month is outside 1-12.
 *@example
 *   import @time
 *   println(time.days_in_month(2024, 2))
 *@end
 */
int64_t gray_time_days_in_month(int64_t year, int64_t month);

/*@man day_of_year
 *@module time
 *@group Components
 *@sig day_of_year(timestamp int) -> int
 *@desc Returns the day of the year for timestamp, from 1 to 366.
 *@example
 *   import @time
 *   println(time.day_of_year(time.now()))
 *@end
 */
int64_t gray_time_day_of_year(int64_t timestamp);

/*@man weekday_name
 *@module time
 *@group Components
 *@sig weekday_name(timestamp int) -> string
 *@desc Returns the English weekday name for timestamp ("Sunday" through "Saturday"), consistent with weekday() numbering (0 = Sunday).
 *@example
 *   import @time
 *   println(time.weekday_name(time.now()))
 *@end
 */
GrayString gray_time_weekday_name(GrayArena *arena, int64_t timestamp);

/*@man month_name
 *@module time
 *@group Components
 *@sig month_name(timestamp int) -> string
 *@desc Returns the English month name for timestamp ("January" through "December").
 *@example
 *   import @time
 *   println(time.month_name(time.now()))
 *@end
 */
GrayString gray_time_month_name(GrayArena *arena, int64_t timestamp);

#endif
