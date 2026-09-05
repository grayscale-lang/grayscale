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

#endif
