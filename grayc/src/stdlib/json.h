/*
 * json.h — Public interface for the json stdlib module.
 * Declares JSON parsing, stringification, field access, and
 * validation functions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_JSON_H
#define GRAY_JSON_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"
#include "../runtime/map.h"

/* JSON string escaping helpers (used by generated #json struct code) */
size_t json_escaped_len(GrayString s);
void json_append_escaped(char *buf, int *pos, GrayString s);

/*@man encode
 *@module json
 *@group Encoding
 *@sig encode(value T) -> string
 *@desc Encodes a value as a JSON string. Accepts int, float, bool, string, map, and array. For #json structs use stringify() instead.
 *@example
 *   import @json
 *   mut m map[string:string] = {"name": "Alice"}
 *   println(json.encode(m))
 *   println(json.encode(42))
 *   println(json.encode(true))
 *   mut arr [int] = {1, 2, 3}
 *   println(json.encode(arr))
 *@end
 */
/* json.encode(value) — convert map to JSON string */
GrayString gray_json_encode_map(GrayArena *arena, GrayMap *m);

/* json.encode(array) — convert typed arrays to JSON */
GrayString gray_json_encode_array_int(GrayArena *arena, GrayArray *arr);
GrayString gray_json_encode_array_float(GrayArena *arena, GrayArray *arr);
GrayString gray_json_encode_array_string(GrayArena *arena, GrayArray *arr);
GrayString gray_json_encode_array_bool(GrayArena *arena, GrayArray *arr);

/* json.encode(map) — convert typed maps to JSON */
GrayString gray_json_encode_map_int(GrayArena *arena, GrayMap *m);
GrayString gray_json_encode_map_float(GrayArena *arena, GrayMap *m);
GrayString gray_json_encode_map_bool(GrayArena *arena, GrayMap *m);

/*@man stringify
 *@module json
 *@group Encoding
 *@sig stringify(value T) -> string
 *@desc Encodes a #json struct to a JSON string. Also accepts maps and other types as a fallback. Use on structs marked with the #json attribute.
 *@example
 *   import @json
 *   #json
 *   const User struct {
 *       name string
 *       age int
 *   }
 *   do main() {
 *       mut u User = json.parse("{\"name\": \"Alice\", \"age\": 25}")
 *       println(json.stringify(u))
 *   }
 *@end
 */
/* json.stringify — codegen-dispatched to per-struct helper for #json structs */

/*@man decode
 *@module json
 *@group Decoding
 *@sig decode(text string) -> (map[string:string], Error)
 *@desc Parses a JSON object string into a map[string:string]. All values are returned as strings. Always use destructuring (`mut m, err = ...` or `mut m, _ = ...`) — single-variable assignment is a compile error; on invalid input err is non-nil and, with `_`, the map is empty.
 *@example
 *   import @json
 *   mut m, err = json.decode("{\"key\": \"value\"}")
 *   mut m2, _ = json.decode("{\"x\": \"1\"}")
 *@end
 */
/* json.decode(text) — parse JSON string to map */
GrayMap gray_json_decode(GrayArena *arena, GrayString text);

/*@man parse
 *@module json
 *@group Decoding
 *@sig parse(text string) -> T
 *@desc Parses a JSON string directly into a #json struct. The target type is inferred from the variable declaration. The struct must be marked with the #json attribute.
 *@example
 *   import @json
 *   #json
 *   const User struct {
 *       name string
 *       age int
 *   }
 *   do main() {
 *       mut u User = json.parse("{\"name\": \"Alice\", \"age\": 25}")
 *       println(u.name)
 *   }
 *@end
 */
/* json.parse — codegen-dispatched to per-struct helper for #json structs */

/*@man is_valid
 *@module json
 *@group Query
 *@sig is_valid(text string) -> bool
 *@desc Returns true if text is a valid JSON string, false otherwise.
 *@example
 *   import @json
 *   println(json.is_valid("{\"ok\": true}"))
 *   println(json.is_valid("not json"))
 *@end
 */
/* json.is_valid(text) — check if string is valid JSON */
bool gray_json_is_valid(GrayString text);

/*@man pretty_print
 *@module json
 *@group Formatting
 *@sig pretty_print(m map[K:V], indent int) -> string
 *@desc Returns a pretty-printed JSON string from a map, indented by indent spaces per level.
 *@example
 *   import @json
 *   mut m map[string:string] = {"name": "Alice", "city": "NYC"}
 *   println(json.pretty_print(m, 2))
 *@end
 */
/* json.pretty(value, indent) — pretty-print JSON */
GrayString gray_json_pretty_map(GrayArena *arena, GrayMap *m, int64_t indent);

/* json array splitting: returns an GrayArray of GrayString, each being one
 * top-level JSON element from a JSON array string like "[{...},{...}]". */
GrayArray gray_json_split_array(GrayArena *arena, GrayString text);

/* _result variant */
typedef struct { GrayMap v0; GrayError *v1; } GrayResult_map;

GrayResult_map gray_json_decode_result(GrayArena *arena, GrayString text);

#endif
