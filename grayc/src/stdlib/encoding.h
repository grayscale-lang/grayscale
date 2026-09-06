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

/*@man base64_url_encode
 *@module encoding
 *@group String Encoding
 *@sig base64_url_encode(s string) -> string
 *@desc Encodes a string to unpadded URL-safe base64 (RFC 4648 section 5): the standard alphabet with `+` and `/` replaced by `-` and `_`, and trailing `=` padding removed. Used for JWTs and query parameters.
 *@example
 *   import @encoding
 *   mut token string = encoding.base64_url_encode("hello?")
 *@end
 */

/*@man base64_url_decode
 *@module encoding
 *@group String Encoding
 *@sig base64_url_decode(s string) -> string
 *@desc Decodes URL-safe base64, with or without trailing padding. Panics on invalid input.
 *@example
 *   import @encoding
 *   mut s string = encoding.base64_url_decode("aGVsbG8_")
 *@end
 */

/*@man html_escape
 *@module encoding
 *@group String Encoding
 *@sig html_escape(s string) -> string
 *@desc Replaces the five HTML-significant characters with entities: `&` `<` `>` `"` and `'`. Safe for use in element text and double- or single-quoted attribute values.
 *@example
 *   import @encoding
 *   println(encoding.html_escape("<a href='x'>"))
 *@end
 */

/*@man html_unescape
 *@module encoding
 *@group String Encoding
 *@sig html_unescape(s string) -> string
 *@desc Reverses html_escape. Resolves the named entities amp, lt, gt, quot, and apos, plus decimal and hexadecimal numeric character references. Unknown entities are left untouched.
 *@example
 *   import @encoding
 *   println(encoding.html_unescape("a &amp; b &#39;c&#39;"))
 *@end
 */

/*@man shell_escape
 *@module encoding
 *@group String Encoding
 *@sig shell_escape(s string) -> string
 *@desc Quotes a string for safe use as a single argument in a POSIX shell command. Returns the argument unchanged when it contains only safe characters, otherwise wraps it in single quotes. Not for cmd.exe.
 *@example
 *   import @encoding
 *   println(encoding.shell_escape("it's here"))   // 'it'\''s here'
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
GrayString gray_encoding_base64_encode(GrayArena *arena, GrayString str);
GrayString gray_encoding_base64_decode(GrayArena *arena, GrayString str);
GrayString gray_encoding_hex_encode(GrayArena *arena, GrayString str);
GrayString gray_encoding_hex_decode(GrayArena *arena, GrayString str);
GrayString gray_encoding_url_encode(GrayArena *arena, GrayString str);
GrayString gray_encoding_url_decode(GrayArena *arena, GrayString str);
GrayString gray_encoding_base64_url_encode(GrayArena *arena, GrayString str);
GrayString gray_encoding_base64_url_decode(GrayArena *arena, GrayString str);
GrayString gray_encoding_html_escape(GrayArena *arena, GrayString str);
GrayString gray_encoding_html_unescape(GrayArena *arena, GrayString str);
GrayString gray_encoding_shell_escape(GrayArena *arena, GrayString str);

/* Byte conversion functions (formerly @bytes module) */
GrayArray gray_encoding_from_string(GrayArena *arena, GrayString str);
GrayString gray_encoding_to_string(GrayArena *arena, GrayArray *bytes);
GrayArray gray_encoding_from_hex(GrayArena *arena, GrayString hex);
GrayString gray_encoding_to_hex(GrayArena *arena, GrayArray *bytes);
GrayArray gray_encoding_from_base64(GrayArena *arena, GrayString b64);
GrayString gray_encoding_to_base64(GrayArena *arena, GrayArray *bytes);

#endif
