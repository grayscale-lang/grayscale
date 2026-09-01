/*
 * uuid.h — Public interface for the uuid stdlib module.
 * Declares UUID v4 generation, parsing, comparison, and
 * string conversion functions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_UUID_H
#define GRAY_UUID_H

#include "../runtime/runtime.h"

/*@man generate
 *@module uuid
 *@group Generation
 *@sig generate() -> UUID
 *@desc Generates a random RFC 4122 version 4 UUID as a 36-character lowercase hyphenated value.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.generate()
 *   println(uuid.to_string(id))
 *@end
 */

/*@man generate_hyphenated
 *@module uuid
 *@group Generation
 *@sig generate_hyphenated() -> UUID
 *@desc Alias for generate. Returns a random version 4 UUID as a 36-character lowercase hyphenated value.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.generate_hyphenated()
 *   println(uuid.to_string(id))
 *@end
 */

/*@man generate_random
 *@module uuid
 *@group Generation
 *@sig generate_random() -> UUID
 *@desc Generates an RFC 4122 version 4 (random) UUID, 36-character lowercase hyphenated. The version nibble is 4.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.generate_random()
 *   println(uuid.to_string(id))
 *@end
 */

/*@man generate_time_ordered
 *@module uuid
 *@group Generation
 *@sig generate_time_ordered() -> UUID
 *@desc Generates an RFC 9562 version 7 (time-ordered) UUID, 36-character lowercase hyphenated. Values sort by creation time.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.generate_time_ordered()
 *   println(uuid.to_string(id))
 *@end
 */

/*@man generate_compact
 *@module uuid
 *@group Conversion
 *@sig generate_compact(id UUID) -> string
 *@desc Returns the UUID with its hyphens stripped, as a 32-character hex string.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.generate()
 *   println(uuid.generate_compact(id))
 *@end
 */

/*@man parse
 *@module uuid
 *@group Conversion
 *@sig parse(s string) -> UUID
 *@desc Validates and normalizes a 36-character hyphenated UUID string to lowercase. Panics on invalid input; gate with is_valid for a non-panicking check.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.parse("550E8400-E29B-41D4-A716-446655440000")
 *   println(uuid.to_string(id))
 *@end
 */

/*@man to_string
 *@module uuid
 *@group Conversion
 *@sig to_string(id UUID) -> string
 *@desc Returns the UUID's canonical 36-character hyphenated string representation.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.generate()
 *   println(uuid.to_string(id))
 *@end
 */

/*@man is_valid
 *@module uuid
 *@group Validation
 *@sig is_valid(s string) -> bool
 *@desc Reports whether s is a well-formed 36-character hyphenated UUID string.
 *@example
 *   import @uuid
 *   if uuid.is_valid("not-a-uuid") == false { println("rejected") }
 *@end
 */

/*@man NIL_UUID
 *@module uuid
 *@group Constants
 *@kind const
 *@sig 00000000-0000-0000-0000-000000000000
 *@desc The all-zero UUID.
 *@end
 */

typedef struct {
    GrayString value;
} GrayUUID;

GrayUUID gray_uuid_generate(GrayArena *arena);
GrayString gray_uuid_generate_compact(GrayArena *arena, GrayUUID id);
GrayUUID gray_uuid_generate_random(GrayArena *arena);
GrayUUID gray_uuid_generate_time_ordered(GrayArena *arena);
bool gray_uuid_is_valid(GrayString s);
GrayUUID gray_uuid_parse(GrayArena *arena, GrayString s);
GrayString gray_uuid_to_string(GrayUUID id);
GrayUUID gray_uuid_nil(void);

#endif
