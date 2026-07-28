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

#endif
