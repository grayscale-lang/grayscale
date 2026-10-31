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
size_t json_escaped_len(GrayString string);
void json_append_escaped(char *buffer, int *position, GrayString string);

/* Enum field helpers (used by generated #json struct code). raw is the
 * JSON value's original text, kept only to name it in the P0129 panic if
 * it matches no variant of the backing enum. */
int64_t gray_json_enum_from_number(GrayString raw_text, int64_t value,
    const int64_t *variants, int32_t count, const char *type_name);
GrayString gray_json_enum_from_str(GrayString raw_text,
    const GrayString *variants, int32_t count, const char *type_name);

/*@man encode
 *@module json
 *@group Encoding
 *@sig encode(value T) -> string
 *@desc Encodes a value as a JSON string. Accepts any primitive (integers, f32/f64, char, bool, string), a flat array of primitives, a string-keyed map of primitives, or a #json struct or array of #json structs. char is encoded as its codepoint number. Each invalid UTF-8 byte in a string is written as U+FFFD, so the output is always valid JSON.
 *@example
 *   import @json
 *   mut m map[string:string] = {"name": "Alice"}
 *   println(json.encode(m))
 *   println(json.encode(42))
 *   println(json.encode(true))
 *   mut arr [i64] = {1, 2, 3}
 *   println(json.encode(arr))
 *
 *   #json
 *   const User struct {
 *       name string
 *       age i64
 *   }
 *   mut u User = json.parse("{\"name\": \"Alice\", \"age\": 25}")
 *   println(json.encode(u))
 *@end
 */
/* json.encode(value) — convert map to JSON string */
GrayString gray_json_encode_map(GrayArena *arena, GrayMap *map);

/* json.encode(array) — convert typed arrays to JSON. The signed/unsigned/floating-point
 * encoders read each slot at its real width (u8, i16, f32, ...). */
GrayString gray_json_encode_array_signed_integer(GrayArena *arena, GrayArray *array);
GrayString gray_json_encode_array_unsigned_integer(GrayArena *arena, GrayArray *array);
GrayString gray_json_encode_array_floating_point(GrayArena *arena, GrayArray *array);
GrayString gray_json_encode_array_string(GrayArena *arena, GrayArray *array);
GrayString gray_json_encode_string(GrayArena *arena, GrayString string);
GrayString gray_json_encode_array_bool(GrayArena *arena, GrayArray *array);

/* json.encode(map) — convert typed maps to JSON */
GrayString gray_json_encode_map_signed_integer(GrayArena *arena, GrayMap *map);
GrayString gray_json_encode_map_unsigned_integer(GrayArena *arena, GrayMap *map);
GrayString gray_json_encode_map_floating_point(GrayArena *arena, GrayMap *map);
GrayString gray_json_encode_map_bool(GrayArena *arena, GrayMap *map);

/*@man decode
 *@module json
 *@group Decoding
 *@sig decode(text string) -> (map[string:string], Error)
 *@desc Parses a JSON object string into a map[string:string]. All values are returned as strings. Always use destructuring (`mut m, err = ...` or `mut m, _ = ...`) — single-variable assignment is a compile error; on invalid input (including text that is not valid UTF-8) err is non-nil and, with `_`, the map is empty.
 *@example
 *   import @json
 *   mut m, err = json.decode("{\"key\": \"value\"}")
 *   mut m2, _ = json.decode("{\"x\": \"1\"}")
 *@end
 */
/* json.decode(text) — parse JSON string to map */
GrayMap gray_json_decode(GrayArena *arena, GrayString text);

/* json.parse field decoding: like gray_json_decode, and *quoted receives the
 * keys whose JSON value was a quoted string. */
bool gray_json_is_null(GrayString text);
void gray_json_require_document(GrayString text, char opening, const char *file, int line);
GrayMap gray_json_decode_fields(GrayArena *arena, GrayString text, GrayMap *quoted, const char *file, int line);
void gray_json_check_field_quoting(GrayMap *quoted, GrayString key, bool expects_string, const char *expected, const char *file, int line);
bool gray_json_field_as_bool(GrayString value, GrayString key, const char *file, int line);
void gray_json_require_array_field(GrayMap *quoted, GrayString key, GrayString value, const char *file, int line);
GrayString gray_json_element_as_string(GrayArena *arena, GrayString element, GrayString key, const char *file, int line);

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
 *       age i64
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
 *@desc Returns true if text is a valid JSON document, false otherwise. Text that is not valid UTF-8 is not valid. A lone UTF-16 surrogate escape sequence is valid.
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
 *@sig pretty_print(m map[K:V], indent i64) -> string
 *@desc Returns a pretty-printed JSON string from a map, indented by indent spaces per level.
 *@example
 *   import @json
 *   mut m map[string:string] = {"name": "Alice", "city": "NYC"}
 *   println(json.pretty_print(m, 2))
 *@end
 */
/* json.pretty(value, indent) — pretty-print JSON */
GrayString gray_json_pretty_map(GrayArena *arena, GrayMap *map, int64_t indent_size);

/* json array splitting: returns an GrayArray of GrayString, each being one
 * top-level JSON element from a JSON array string like "[{...},{...}]". */
GrayArray gray_json_split_array(GrayArena *arena, GrayString text);
GrayString gray_json_encode_array_wide_integer(GrayArena *arena, GrayArray *array);
GrayString gray_json_unquote(GrayArena *arena, GrayString text);

/* A #json struct's number field, of element kind `kind`: decode its JSON
 * text into *out at that type, panicking on a malformed value or one out of
 * the type's range; render *value as JSON text. */
void gray_json_field_decode(GrayString text, int32_t kind, void *output, const char *file, int line);
GrayString gray_json_number_text(GrayArena *arena, int32_t kind, const void *value);

/* _result variant */
typedef struct { GrayMap v0; GrayError *v1; } GrayResult_map;

GrayResult_map gray_json_decode_result(GrayArena *arena, GrayString text);

#endif
