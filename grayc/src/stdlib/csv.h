/*
 * csv.h — Public interface for the csv stdlib module.
 * Declares CSV parsing, formatting, and header extraction functions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_CSV_H
#define GRAY_CSV_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"
#include "io.h" /* GrayResult_bool, GrayResult_array */

/*@man parse
 *@module csv
 *@group Parsing
 *@sig parse(data string) -> [[string]]
 *@desc Parses a CSV-formatted string and returns an array of rows, where each row is an array of string fields. Returns a single value; do not use destructuring.
 *@example
 *   import @csv
 *   mut rows = csv.parse("a,b,c\n1,2,3")
 *   for_each row in rows {
 *       println(row)
 *   }
 *@end
 */

/*@man encode
 *@module csv
 *@group Formatting
 *@sig encode(rows [[string]]) -> string
 *@desc Converts an array of rows (each row an array of strings) into a CSV-formatted string. Returns a single value; do not use destructuring.
 *@example
 *   import @csv
 *   mut data = {{"Alice", "30"}, {"Bob", "25"}}
 *   mut out = csv.encode(data)
 *   println(out)
 *@end
 */

/*@man headers
 *@module csv
 *@group Parsing
 *@sig headers(rows [[string]]) -> [string]
 *@desc Extracts and returns the first row (header row) from parsed CSV data as a flat string array. Returns a single value; do not use destructuring.
 *@example
 *   import @csv
 *   mut rows = csv.parse("name,age\nAlice,30")
 *   mut h = csv.headers(rows)
 *   println(h)
 *@end
 */

/*@man read_file
 *@module csv
 *@group File I/O
 *@sig read_file(path string) -> ([[string]], Error)
 *@desc Reads the CSV file at path and returns parsed rows as an array of string arrays. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @csv
 *   mut rows, err = csv.read_file("data.csv")
 *   if err != nil { println("failed: ${err}") }
 *   mut rows, _ = csv.read_file("data.csv")
 *@end
 */

/*@man write_file
 *@module csv
 *@group File I/O
 *@sig write_file(path string, rows [[string]]) -> (bool, Error)
 *@desc Writes an array of rows (each row an array of strings) to a CSV file at path. Creates the file if it does not exist; overwrites if it does. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @csv
 *   mut data = {{"name", "age"}, {"Alice", "30"}}
 *   mut ok, err = csv.write_file("out.csv", data)
 *   if err != nil { println("failed: ${err}") }
 *@end
 */

/*@man parse_delimited
 *@module csv
 *@group Parsing
 *@sig parse_delimited(data string, delimiter char) -> [[string]]
 *@desc Like parse, but splits fields on the given single-character delimiter (a tab or a semicolon, say). RFC 4180 quoting rules still apply. Returns a single value; do not use destructuring.
 *@example
 *   import @csv
 *   mut rows = csv.parse_delimited("a\tb\tc\n1\t2\t3", '\t')
 *@end
 */

/*@man detect_delimiter
 *@module csv
 *@group Parsing
 *@sig detect_delimiter(sample string) -> char
 *@desc Inspects the first line of sample and returns whichever of comma, semicolon, tab, or pipe appears most often, defaulting to comma when none is found.
 *@example
 *   import @csv
 *   mut d char = csv.detect_delimiter("a;b;c\n1;2;3")
 *@end
 */

/*@man to_maps
 *@module csv
 *@group Records
 *@sig to_maps(data [[string]]) -> [map[string:string]]
 *@desc Treats row 0 as the header and returns rows 1..N as maps keyed by header name. A short row omits the missing keys; a long row drops the extra fields. Data with 0 or 1 rows returns an empty array.
 *@example
 *   import @csv
 *   mut rows = csv.parse("name,age\nAda,36")
 *   mut records = csv.to_maps(rows)
 *@end
 */

/*@man from_maps
 *@module csv
 *@group Records
 *@sig from_maps(rows [map[string:string]]) -> [[string]]
 *@desc Inverse of to_maps. The header row is the union of all keys in first-seen order across rows; a key missing from a row becomes an empty cell. Empty input returns an empty array.
 *@example
 *   import @csv
 *   mut rows = csv.from_maps(records)
 *@end
 */

/*@man column
 *@module csv
 *@group Records
 *@sig column(data [[string]], name string) -> [string]
 *@desc Returns the values under header name, one per data row (the header cell itself is excluded). Panics if name is not in the header.
 *@example
 *   import @csv
 *   mut ages = csv.column(rows, "age")
 *@end
 */

/*@man select
 *@module csv
 *@group Records
 *@sig select(data [[string]], names [string]) -> [[string]]
 *@desc Projects the named columns in the given order, keeping the header row. Panics if any name is not in the header.
 *@example
 *   import @csv
 *   mut slim = csv.select(rows, {"name", "age"})
 *@end
 */

/*@man filter_rows
 *@module csv
 *@group Records
 *@sig filter_rows(data [[string]], predicate func([string]) -> bool) -> [[string]]
 *@desc Keeps row 0 (the header) unconditionally and each later row for which predicate returns true. The header is never passed to predicate.
 *@example
 *   import @csv
 *   do adult(row [string]) -> bool { return strconv.to_int(row[1], 10) >= 18 }
 *   mut adults = csv.filter_rows(rows, ()adult)
 *@end
 */

/*@man sort_by_column
 *@module csv
 *@group Records
 *@sig sort_by_column(data [[string]], name string) -> [[string]]
 *@desc Returns a new array with data rows stably sorted in ascending lexicographic order by the value in column name. The header row stays first. Panics if name is not in the header.
 *@example
 *   import @csv
 *   mut sorted = csv.sort_by_column(rows, "name")
 *@end
 */

/*@man to_json
 *@module csv
 *@group Formatting
 *@sig to_json(data [[string]]) -> string
 *@desc Converts data (via to_maps) to a compact JSON array of objects. Every value is a JSON string, since CSV has no types.
 *@example
 *   import @csv
 *   println(csv.to_json(rows))
 *@end
 */

/*@man to_markdown
 *@module csv
 *@group Formatting
 *@sig to_markdown(data [[string]]) -> string
 *@desc Renders data as a GitHub-flavored Markdown table: row 0 becomes the header plus a '---' separator row, and a pipe in a cell is escaped with a preceding backslash. No column alignment; a trailing newline is included.
 *@example
 *   import @csv
 *   println(csv.to_markdown(rows))
 *@end
 */

/* Parse CSV string into array of arrays of strings */
GrayArray gray_csv_parse(GrayArena *arena, GrayString csv_string);
GrayArray gray_csv_parse_delimited(GrayArena *arena, GrayString csv_string, int32_t delimiter);
int32_t   gray_csv_detect_delimiter(GrayString sample);

/* [[string]] <-> [map[string:string]] record views */
GrayArray  gray_csv_to_maps(GrayArena *arena, GrayArray *data);
GrayArray  gray_csv_from_maps(GrayArena *arena, GrayArray *rows);
GrayArray  gray_csv_column(GrayArena *arena, GrayArray *data, GrayString name);
GrayArray  gray_csv_select(GrayArena *arena, GrayArray *data, GrayArray *names);
GrayArray  gray_csv_sort_by_column(GrayArena *arena, GrayArray *data, GrayString name);
GrayString gray_csv_to_json(GrayArena *arena, GrayArray *data);
GrayString gray_csv_to_markdown(GrayArena *arena, GrayArray *data);

/* Convert array of arrays of strings to CSV string */
GrayString gray_csv_stringify(GrayArena *arena, GrayArray *data);

/* Extract the first row (headers) from parsed CSV data */
GrayArray gray_csv_headers(GrayArena *arena, GrayArray *data);

/* Read CSV file */
GrayArray gray_csv_read(GrayArena *arena, GrayString path);

/* Write CSV to file */
bool gray_csv_write(GrayArena *arena, GrayString path, GrayArray *data);

/* _result variants */
GrayResult_array gray_csv_read_result(GrayArena *arena, GrayString path);
GrayResult_bool gray_csv_write_result(GrayArena *arena, GrayString path, GrayArray *data);

#endif
