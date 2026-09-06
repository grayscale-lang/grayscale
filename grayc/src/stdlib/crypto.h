/*
 * crypto.h — Public interface for the crypto stdlib module.
 * Declares SHA-256, MD5 hashing, and cryptographic random hex
 * generation with no OpenSSL dependency.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_CRYPTO_H
#define GRAY_CRYPTO_H

#include "../runtime/runtime.h"

/*@man sha256
 *@module crypto
 *@group Hashing
 *@sig sha256(data string) -> string
 *@desc Compute the SHA-256 hash of data and return it as a hex string.
 *@example
 *   import @crypto
 *   mut hash string = crypto.sha256("hello")
 *   println(hash)
 *@end
 */
GrayString gray_crypto_sha256(GrayArena *arena, GrayString data);

/*@man md5
 *@module crypto
 *@group Hashing
 *@sig md5(data string) -> string
 *@desc Compute the MD5 hash of data and return it as a hex string. WARNING: MD5 is cryptographically broken. Do not use for security purposes. Use sha256 instead.
 *@example
 *   import @crypto
 *   mut hash string = crypto.md5("hello")
 *@end
 */
/* WARNING: MD5 is a cryptographically broken hash function. It has known
 * collision vulnerabilities and must not be used for password hashing,
 * certificate fingerprinting, integrity verification in security contexts,
 * or HMAC construction. Use crypto.sha256() for any security purpose.
 * crypto.md5() is provided only for legacy compatibility and non-security
 * checksum use cases (e.g. cache keys, non-sensitive deduplication). */
GrayString gray_crypto_md5(GrayArena *arena, GrayString data);

/*@man random_hex
 *@module crypto
 *@group Random
 *@sig random_hex(length int) -> string
 *@desc Generate a cryptographically random hex string of the given length.
 *@example
 *   import @crypto
 *   mut token string = crypto.random_hex(32)
 *   println(token)
 *@end
 */
GrayString gray_crypto_random_hex(GrayArena *arena, int64_t length);

/*@man sha1
 *@module crypto
 *@group Hashing
 *@sig sha1(data string) -> string
 *@desc Compute the SHA-1 hash of data and return it as a hex string. WARNING: SHA-1 is broken for collision resistance and must not be used for signatures or integrity in security contexts. It ships because HMAC-SHA1 and TOTP still require it.
 *@example
 *   import @crypto
 *   println(crypto.sha1("hello"))
 *@end
 */
GrayString gray_crypto_sha1(GrayArena *arena, GrayString data);

/*@man sha512
 *@module crypto
 *@group Hashing
 *@sig sha512(data string) -> string
 *@desc Compute the SHA-512 hash of data and return it as a hex string.
 *@example
 *   import @crypto
 *   println(crypto.sha512("hello"))
 *@end
 */
GrayString gray_crypto_sha512(GrayArena *arena, GrayString data);

/*@man hmac_sha256
 *@module crypto
 *@group Hashing
 *@sig hmac_sha256(key string, data string) -> string
 *@desc Compute the RFC 2104 HMAC of data under key using SHA-256, returned as a hex string.
 *@example
 *   import @crypto
 *   println(crypto.hmac_sha256("secret", "message"))
 *@end
 */
GrayString gray_crypto_hmac_sha256(GrayArena *arena, GrayString key, GrayString data);

/*@man hmac_sha1
 *@module crypto
 *@group Hashing
 *@sig hmac_sha1(key string, data string) -> string
 *@desc Compute the RFC 2104 HMAC of data under key using SHA-1, returned as a hex string. Needed by totp.
 *@example
 *   import @crypto
 *   println(crypto.hmac_sha1("secret", "message"))
 *@end
 */
GrayString gray_crypto_hmac_sha1(GrayArena *arena, GrayString key, GrayString data);

/*@man constant_time_equal
 *@module crypto
 *@group Comparison
 *@sig constant_time_equal(a string, b string) -> bool
 *@desc Compare a and b without an early return on the first mismatch, so timing does not reveal how much of a secret matched. A length difference is folded into the result.
 *@example
 *   import @crypto
 *   if crypto.constant_time_equal(token, expected) { println("ok") }
 *@end
 */
bool gray_crypto_constant_time_equal(GrayString a, GrayString b);

/*@man crc32
 *@module crypto
 *@group Checksums
 *@sig crc32(data string) -> uint
 *@desc Compute the IEEE CRC-32 checksum of data (reflected, polynomial 0xEDB88320), returned as a uint. This is a checksum, not a cryptographic hash.
 *@example
 *   import @crypto
 *   println(crypto.crc32("123456789"))
 *@end
 */
uint64_t gray_crypto_crc32(GrayString data);

/*@man entropy
 *@module crypto
 *@group Analysis
 *@sig entropy(data string) -> float
 *@desc Return the Shannon entropy of data in bits per byte, computed over its byte histogram. An empty string returns 0.0; the result ranges from 0.0 to 8.0.
 *@example
 *   import @crypto
 *   println(crypto.entropy("aaaaaaaa"))
 *@end
 */
double gray_crypto_entropy(GrayString data);

/*@man totp
 *@module crypto
 *@group Authentication
 *@sig totp(secret string, timestamp int, digits int) -> string
 *@desc Compute an RFC 6238 time-based one-time password from the raw shared secret bytes, using SHA-1 and a 30-second time step. `digits` is typically 6 or 8; the result is zero-padded to that width. Panics if `digits` is outside 1..9. Base32 decoding of the secret is the caller's responsibility.
 *@example
 *   import @crypto, @time
 *   println(crypto.totp(secret, time.now(), 6))
 *@end
 */
GrayString gray_crypto_totp(GrayArena *arena, GrayString secret, int64_t timestamp, int64_t digits);

#endif
