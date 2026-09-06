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
#include "../runtime/array.h"

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

/*@man to_bytes
 *@module uuid
 *@group Conversion
 *@sig to_bytes(id UUID) -> [byte]
 *@desc Returns the UUID's 16 raw bytes in big-endian (network) order.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.generate()
 *   mut raw [byte] = uuid.to_bytes(id)
 *@end
 */

/*@man from_bytes
 *@module uuid
 *@group Conversion
 *@sig from_bytes(bytes [byte]) -> UUID
 *@desc Builds a UUID from 16 raw bytes in big-endian order. The bytes are used verbatim — no version or variant bits are forced. Panics if fewer than 16 bytes are given.
 *@example
 *   import @uuid
 *   mut id UUID = uuid.from_bytes(uuid.to_bytes(uuid.generate()))
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

/*@man version
 *@module uuid
 *@group Inspection
 *@sig version(id UUID) -> int
 *@desc Returns the UUID's version number from its version nibble (1 through 8 for the RFC-defined versions). The nil UUID reports 0.
 *@example
 *   import @uuid
 *   println(uuid.version(uuid.generate()))            // 4
 *   println(uuid.version(uuid.generate_time_ordered())) // 7
 *@end
 */

/*@man timestamp
 *@module uuid
 *@group Inspection
 *@sig timestamp(id UUID) -> (int, bool)
 *@desc Extracts the embedded creation time as Unix milliseconds. The second value is true for a version 1 or version 7 UUID and false for any other version, where the first value is 0. Always destructure the result.
 *@example
 *   import @uuid
 *   mut ms, ok = uuid.timestamp(uuid.generate_time_ordered())
 *   if ok { println(ms) }
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

/* Layout must match the {int64_t v0; bool v1;} tuple codegen emits for a
 * multi-return stdlib call. */
typedef struct {
    int64_t v0;
    bool v1;
} GrayUuidTimestamp;

GrayUUID gray_uuid_generate(GrayArena *arena);
GrayArray gray_uuid_to_bytes(GrayArena *arena, GrayUUID id);
GrayUUID gray_uuid_from_bytes(GrayArena *arena, GrayArray *bytes);
int64_t gray_uuid_version(GrayUUID id);
GrayUuidTimestamp gray_uuid_timestamp(GrayUUID id);
GrayString gray_uuid_generate_compact(GrayArena *arena, GrayUUID id);
GrayUUID gray_uuid_generate_random(GrayArena *arena);
GrayUUID gray_uuid_generate_time_ordered(GrayArena *arena);
bool gray_uuid_is_valid(GrayString str);
GrayUUID gray_uuid_parse(GrayArena *arena, GrayString str);
GrayString gray_uuid_to_string(GrayUUID id);
GrayUUID gray_uuid_nil(void);

#endif
