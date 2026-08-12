/*
 * encoding.h — Public interface for the encoding stdlib module.
 * Declares base64, hex, URL, and byte-array encode/decode functions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_ENCODING_H
#define GRAY_ENCODING_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"

/*@man base64_encode
 *@module encoding
 *@group String Encoding
 *@sig base64_encode(s string) -> string
 *@desc Encodes a string to base64.
 *@example
 *   import @encoding
 *   mut encoded string = encoding.base64_encode("hello")
 *@end
 */

/*@man base64_decode
 *@module encoding
 *@group String Encoding
 *@sig base64_decode(s string) -> string
 *@desc Decodes a base64-encoded string.
 *@example
 *   import @encoding
 *   mut decoded string = encoding.base64_decode("aGVsbG8=")
 *@end
 */

/*@man hex_encode
 *@module encoding
 *@group String Encoding
 *@sig hex_encode(s string) -> string
 *@desc Encodes a string to lowercase hex.
 *@example
 *   import @encoding
 *   mut hex string = encoding.hex_encode("AB")
 *@end
 */

/*@man hex_decode
 *@module encoding
 *@group String Encoding
 *@sig hex_decode(s string) -> string
 *@desc Decodes a hex-encoded string.
 *@example
 *   import @encoding
 *   mut decoded string = encoding.hex_decode("4142")
 *@end
 */

/*@man url_encode
 *@module encoding
 *@group String Encoding
 *@sig url_encode(s string) -> string
 *@desc URL percent-encodes a string.
 *@example
 *   import @encoding
 *   mut encoded string = encoding.url_encode("hello world")
 *@end
 */

/*@man url_decode
 *@module encoding
 *@group String Encoding
 *@sig url_decode(s string) -> string
 *@desc Decodes a URL percent-encoded string.
 *@example
 *   import @encoding
 *   mut decoded string = encoding.url_decode("hello%20world")
 *@end
 */

/*@man from_string
 *@module encoding
 *@group Byte Conversion
 *@sig from_string(s string) -> [byte]
 *@desc Converts a UTF-8 string into a byte array.
 *@example
 *   import @encoding
 *   mut b [byte] = encoding.from_string("hello")
 *@end
 */

/*@man from_hex
 *@module encoding
 *@group Byte Conversion
 *@sig from_hex(hex string) -> [byte]
 *@desc Decodes a hex-encoded string into a byte array.
 *@example
 *   import @encoding
 *   mut b [byte] = encoding.from_hex("48656c6c6f")
 *@end
 */

/*@man from_base64
 *@module encoding
 *@group Byte Conversion
 *@sig from_base64(b64 string) -> [byte]
 *@desc Decodes a base64-encoded string into a byte array.
 *@example
 *   import @encoding
 *   mut b [byte] = encoding.from_base64("SGVsbG8=")
 *@end
 */

/*@man to_string
 *@module encoding
 *@group Byte Conversion
 *@sig to_string(bytes [byte]) -> string
 *@desc Converts a byte array to a UTF-8 string.
 *@example
 *   import @encoding
 *   mut b [byte] = encoding.from_string("hello")
 *   println(encoding.to_string(b))
 *@end
 */

/*@man to_hex
 *@module encoding
 *@group Byte Conversion
 *@sig to_hex(bytes [byte]) -> string
 *@desc Encodes a byte array as a lowercase hex string.
 *@example
 *   import @encoding
 *   mut b [byte] = encoding.from_string("hi")
 *   println(encoding.to_hex(b))
 *@end
 */

/*@man to_base64
 *@module encoding
 *@group Byte Conversion
 *@sig to_base64(bytes [byte]) -> string
 *@desc Encodes a byte array as a base64 string.
 *@example
 *   import @encoding
 *   mut b [byte] = encoding.from_string("hello")
 *   println(encoding.to_base64(b))
 *@end
 */

/* String encoding functions */
GrayString gray_encoding_base64_encode(GrayArena *arena, GrayString s);
GrayString gray_encoding_base64_decode(GrayArena *arena, GrayString s);
GrayString gray_encoding_hex_encode(GrayArena *arena, GrayString s);
GrayString gray_encoding_hex_decode(GrayArena *arena, GrayString s);
GrayString gray_encoding_url_encode(GrayArena *arena, GrayString s);
GrayString gray_encoding_url_decode(GrayArena *arena, GrayString s);

/* Byte conversion functions (formerly @bytes module) */
GrayArray gray_encoding_from_string(GrayArena *arena, GrayString s);
GrayString gray_encoding_to_string(GrayArena *arena, GrayArray *bytes);
GrayArray gray_encoding_from_hex(GrayArena *arena, GrayString hex);
GrayString gray_encoding_to_hex(GrayArena *arena, GrayArray *bytes);
GrayArray gray_encoding_from_base64(GrayArena *arena, GrayString b64);
GrayString gray_encoding_to_base64(GrayArena *arena, GrayArray *bytes);

#endif
