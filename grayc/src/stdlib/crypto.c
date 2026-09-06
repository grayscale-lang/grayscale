/*
 * crypto.c — Implementation of the crypto stdlib module.
 * Embedded SHA-256 and MD5 hash functions plus CSPRNG-backed
 * random hex generation, with no external dependencies.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

/* Must precede every <stdlib.h> inclusion (also transitive ones via crypto.h)
 * so the CRT declares rand_s, the Windows entropy source. */
#ifdef _WIN32
#define _CRT_RAND_S
#include <stdlib.h>
#endif

#include "crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* arc4random_buf is hidden by _POSIX_C_SOURCE on Apple/BSD — declare explicitly */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
void arc4random_buf(void *buf, size_t nbytes);
#endif

/* ===== SHA-256 ===== */

static uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z) (((x)&(y))^((~(x))&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (ROTR(x,2)^ROTR(x,13)^ROTR(x,22))
#define EP1(x) (ROTR(x,6)^ROTR(x,11)^ROTR(x,25))
#define SIG0(x) (ROTR(x,7)^ROTR(x,18)^((x)>>3))
#define SIG1(x) (ROTR(x,17)^ROTR(x,19)^((x)>>10))

/* Hex-encode `n` bytes of digest into a fresh arena string of length 2n. */
static GrayString crypto_hex(GrayArena *arena, const uint8_t *digest, int n) {
    static const char hc[] = "0123456789abcdef";
    char *hex = gray_arena_alloc_uninitialized(arena, (size_t)n * 2 + 1);
    for (int i = 0; i < n; i++) {
        hex[i * 2]     = hc[(digest[i] >> 4) & 0x0f];
        hex[i * 2 + 1] = hc[digest[i] & 0x0f];
    }
    hex[n * 2] = '\0';
    return (GrayString){ hex, n * 2 };
}

/* SHA-256 core: writes the 32-byte digest to out. Allocates a padded message
 * buffer from the arena (the arena has no free; callers are short-lived). */
static void sha256_raw(GrayArena *arena, const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    uint64_t bits = (uint64_t)len * 8;
    size_t padded = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = (uint8_t *)gray_arena_alloc(arena, padded);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    memset(msg + len + 1, 0, padded - len - 1);
    for (int i = 0; i < 8; i++)
        msg[padded - 1 - i] = (uint8_t)(bits >> (i * 8));

    for (size_t i = 0; i < padded; i += 64) {
        uint32_t w[64];
        for (int j = 0; j < 16; j++)
            w[j] = ((uint32_t)msg[i+j*4]<<24)|((uint32_t)msg[i+j*4+1]<<16)|
                   ((uint32_t)msg[i+j*4+2]<<8)|msg[i+j*4+3];
        for (int j = 16; j < 64; j++)
            w[j] = SIG1(w[j-2]) + w[j-7] + SIG0(w[j-15]) + w[j-16];

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int j = 0; j < 64; j++) {
            uint32_t t1 = hh + EP1(e) + CH(e,f,g) + sha256_k[j] + w[j];
            uint32_t t2 = EP0(a) + MAJ(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)h[i];
    }
}

GrayString gray_crypto_sha256(GrayArena *arena, GrayString data) {
    uint8_t digest[32];
    sha256_raw(arena, (const uint8_t *)data.data, (size_t)data.len, digest);
    return crypto_hex(arena, digest, 32);
}

/* ===== SHA-1 (broken for collision resistance; still needed for HMAC-SHA1
 * and TOTP, so it ships with the same caveat style as md5) ===== */

static void sha1_raw(GrayArena *arena, const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };

    uint64_t bits = (uint64_t)len * 8;
    size_t padded = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = (uint8_t *)gray_arena_alloc(arena, padded);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    memset(msg + len + 1, 0, padded - len - 1);
    for (int i = 0; i < 8; i++)
        msg[padded - 1 - i] = (uint8_t)(bits >> (i * 8));

    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[80];
        for (int j = 0; j < 16; j++)
            w[j] = ((uint32_t)msg[off+j*4]<<24)|((uint32_t)msg[off+j*4+1]<<16)|
                   ((uint32_t)msg[off+j*4+2]<<8)|msg[off+j*4+3];
        for (int j = 16; j < 80; j++) {
            uint32_t v = w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16];
            w[j] = (v << 1) | (v >> 31);
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999; }
            else if (j < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
            uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[j];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)h[i];
    }
}

GrayString gray_crypto_sha1(GrayArena *arena, GrayString data) {
    uint8_t digest[20];
    sha1_raw(arena, (const uint8_t *)data.data, (size_t)data.len, digest);
    return crypto_hex(arena, digest, 20);
}

/* ===== SHA-512 ===== */

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

#define ROTR64(x,n) (((x)>>(n))|((x)<<(64-(n))))

static void sha512_raw(GrayArena *arena, const uint8_t *data, size_t len, uint8_t out[64]) {
    uint64_t h[8] = {
        0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL
    };
    /* 128-byte blocks; 16-byte length field (we only fill the low 8 bytes). */
    size_t padded = ((len + 16) / 128 + 1) * 128;
    uint8_t *msg = (uint8_t *)gray_arena_alloc(arena, padded);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    memset(msg + len + 1, 0, padded - len - 1);
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        msg[padded - 1 - i] = (uint8_t)(bits >> (i * 8));

    for (size_t off = 0; off < padded; off += 128) {
        uint64_t w[80];
        for (int j = 0; j < 16; j++) {
            w[j] = 0;
            for (int b = 0; b < 8; b++)
                w[j] = (w[j] << 8) | msg[off + j*8 + b];
        }
        for (int j = 16; j < 80; j++) {
            uint64_t s0 = ROTR64(w[j-15],1) ^ ROTR64(w[j-15],8) ^ (w[j-15] >> 7);
            uint64_t s1 = ROTR64(w[j-2],19) ^ ROTR64(w[j-2],61) ^ (w[j-2] >> 6);
            w[j] = w[j-16] + s0 + w[j-7] + s1;
        }
        uint64_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int j = 0; j < 80; j++) {
            uint64_t S1 = ROTR64(e,14) ^ ROTR64(e,18) ^ ROTR64(e,41);
            uint64_t ch = (e & f) ^ ((~e) & g);
            uint64_t t1 = hh + S1 + ch + sha512_k[j] + w[j];
            uint64_t S0 = ROTR64(a,28) ^ ROTR64(a,34) ^ ROTR64(a,39);
            uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint64_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    for (int i = 0; i < 8; i++)
        for (int b = 0; b < 8; b++)
            out[i*8 + b] = (uint8_t)(h[i] >> (56 - b*8));
}

GrayString gray_crypto_sha512(GrayArena *arena, GrayString data) {
    uint8_t digest[64];
    sha512_raw(arena, (const uint8_t *)data.data, (size_t)data.len, digest);
    return crypto_hex(arena, digest, 64);
}

/* ===== HMAC (RFC 2104), block size 64 for both SHA-1 and SHA-256 ===== */

typedef void (*crypto_hash_fn)(GrayArena *, const uint8_t *, size_t, uint8_t *);

static void hmac_raw(GrayArena *arena, crypto_hash_fn hash, int digest_len,
                     GrayString key, GrayString data, uint8_t *out) {
    const int BLOCK = 64;
    uint8_t k[64];
    memset(k, 0, sizeof(k));
    if (key.len > BLOCK) {
        hash(arena, (const uint8_t *)key.data, (size_t)key.len, k);
        /* digest_len <= 32 < BLOCK for the hashes we use here */
    } else {
        memcpy(k, key.data, (size_t)key.len);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < BLOCK; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    /* inner = hash(ipad || data) */
    uint8_t *inner_msg = gray_arena_alloc_uninitialized(arena, (size_t)BLOCK + (size_t)data.len);
    memcpy(inner_msg, ipad, BLOCK);
    memcpy(inner_msg + BLOCK, data.data, (size_t)data.len);
    uint8_t inner[64];
    hash(arena, inner_msg, (size_t)BLOCK + (size_t)data.len, inner);
    /* out = hash(opad || inner) */
    uint8_t outer_msg[64 + 64];
    memcpy(outer_msg, opad, BLOCK);
    memcpy(outer_msg + BLOCK, inner, (size_t)digest_len);
    hash(arena, outer_msg, (size_t)BLOCK + (size_t)digest_len, out);
}

GrayString gray_crypto_hmac_sha256(GrayArena *arena, GrayString key, GrayString data) {
    uint8_t mac[32];
    hmac_raw(arena, sha256_raw, 32, key, data, mac);
    return crypto_hex(arena, mac, 32);
}

GrayString gray_crypto_hmac_sha1(GrayArena *arena, GrayString key, GrayString data) {
    uint8_t mac[20];
    hmac_raw(arena, sha1_raw, 20, key, data, mac);
    return crypto_hex(arena, mac, 20);
}

/* ===== Constant-time comparison ===== */

bool gray_crypto_constant_time_equal(GrayString a, GrayString b) {
    int32_t n = a.len > b.len ? a.len : b.len;
    uint32_t diff = (uint32_t)(a.len ^ b.len);
    for (int32_t i = 0; i < n; i++) {
        uint8_t ca = i < a.len ? (uint8_t)a.data[i] : 0;
        uint8_t cb = i < b.len ? (uint8_t)b.data[i] : 0;
        diff |= (uint32_t)(ca ^ cb);
    }
    return diff == 0;
}

/* ===== CRC-32 (IEEE, reflected, polynomial 0xEDB88320) ===== */

static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_ready = true;
}

uint64_t gray_crypto_crc32(GrayString data) {
    if (!crc32_table_ready) crc32_init();
    uint32_t crc = 0xFFFFFFFFu;
    for (int32_t i = 0; i < data.len; i++)
        crc = crc32_table[(crc ^ (uint8_t)data.data[i]) & 0xFF] ^ (crc >> 8);
    return (uint64_t)(crc ^ 0xFFFFFFFFu);
}

/* ===== Shannon entropy (bits per byte) ===== */

double gray_crypto_entropy(GrayString data) {
    if (data.len == 0) return 0.0;
    size_t counts[256] = {0};
    for (int32_t i = 0; i < data.len; i++)
        counts[(uint8_t)data.data[i]]++;
    double total = (double)data.len;
    double h = 0.0;
    for (int i = 0; i < 256; i++) {
        if (counts[i] == 0) continue;
        double p = (double)counts[i] / total;
        h -= p * log2(p);
    }
    return h;
}

/* ===== TOTP (RFC 6238), SHA-1, 30-second step ===== */

GrayString gray_crypto_totp(GrayArena *arena, GrayString secret, int64_t timestamp, int64_t digits) {
    if (digits < 1 || digits > 9) {
        gray_panic_code("P0126", "crypto.totp: digits must be between 1 and 9 (got %lld)",
            (long long)digits);
    }
    uint64_t counter = (uint64_t)(timestamp < 0 ? 0 : timestamp) / 30u;
    uint8_t msg[8];
    for (int i = 0; i < 8; i++)
        msg[i] = (uint8_t)(counter >> (56 - i * 8));

    uint8_t mac[20];
    hmac_raw(arena, sha1_raw, 20, secret, (GrayString){ (const char *)msg, 8 }, mac);

    int offset = mac[19] & 0x0f;
    uint32_t bin = ((uint32_t)(mac[offset] & 0x7f) << 24) |
                   ((uint32_t)mac[offset + 1] << 16) |
                   ((uint32_t)mac[offset + 2] << 8) |
                   (uint32_t)mac[offset + 3];

    uint32_t mod = 1;
    for (int64_t i = 0; i < digits; i++) mod *= 10u;
    uint32_t otp = bin % mod;

    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)digits + 1);
    snprintf(buf, (size_t)digits + 1, "%0*u", (int)digits, otp);
    return (GrayString){ buf, (int32_t)digits };
}

/* ===== MD5 ===== */

#define F(x,y,z) (((x)&(y))|((~(x))&(z)))
#define G(x,y,z) (((x)&(z))|((y)&(~(z))))
#define H(x,y,z) ((x)^(y)^(z))
#define I(x,y,z) ((y)^((x)|(~(z))))
#define ROTL(x,n) (((x)<<(n))|((x)>>(32-(n))))

/* WARNING: MD5 is cryptographically broken. See crypto.h for details. */
GrayString gray_crypto_md5(GrayArena *arena, GrayString data) {
    uint32_t a0=0x67452301, b0=0xefcdab89, c0=0x98badcfe, d0=0x10325476;
    static const uint32_t s[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };

    size_t len = (size_t)data.len;
    size_t padded = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = (uint8_t *)gray_arena_alloc(arena, padded);
    memcpy(msg, data.data, len);
    msg[len] = 0x80;
    memset(msg + len + 1, 0, padded - len - 9);
    uint64_t bits = (uint64_t)len * 8;
    memcpy(msg + padded - 8, &bits, 8);

    for (size_t offset = 0; offset < padded; offset += 64) {
        uint32_t *M = (uint32_t *)(msg + offset);
        uint32_t A=a0, B=b0, C=c0, D=d0;
        for (int i = 0; i < 64; i++) {
            uint32_t f_val, g;
            if (i < 16) { f_val = F(B,C,D); g = (uint32_t)i; }
            else if (i < 32) { f_val = G(B,C,D); g = (5*(uint32_t)i + 1) % 16; }
            else if (i < 48) { f_val = H(B,C,D); g = (3*(uint32_t)i + 5) % 16; }
            else { f_val = I(B,C,D); g = (7*(uint32_t)i) % 16; }
            uint32_t tmp = D; D = C; C = B;
            B = B + ROTL(A + f_val + K[i] + M[g], s[i]);
            A = tmp;
        }
        a0+=A; b0+=B; c0+=C; d0+=D;
    }

    char *hex = gray_arena_alloc_uninitialized(arena, 33);
    uint8_t digest[16];
    memcpy(digest, &a0, 4); memcpy(digest+4, &b0, 4);
    memcpy(digest+8, &c0, 4); memcpy(digest+12, &d0, 4);
    for (int i = 0; i < 16; i++) snprintf(hex + i*2, 3, "%02x", digest[i]);
    hex[32] = '\0';
    GrayString r = { hex, 32 };
    return r;
}

GrayString gray_crypto_random_hex(GrayArena *arena, int64_t length) {
    if (length < 0) {
        gray_panic_code("P0051", "crypto.random_hex: length must be non-negative (got %lld)", (long long)length);
    }
    if (length == 0) {
        char *empty = gray_arena_alloc_uninitialized(arena, 1);
        empty[0] = '\0';
        return (GrayString){empty, 0};
    }

    int64_t nbytes = (length + 1) / 2;
    uint8_t *raw = (uint8_t *)gray_arena_alloc_uninitialized(arena, (size_t)nbytes);

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(raw, (size_t)nbytes);
#elif defined(_WIN32)
    /* rand_s is RtlGenRandom under the hood: CSPRNG, no extra link library. */
    for (int64_t i = 0; i < nbytes; i += (int64_t)sizeof(unsigned int)) {
        unsigned int r;
        if (rand_s(&r) != 0) {
            gray_panic_code("P0052", "crypto.random_hex: failed to read from the system CSPRNG");
        }
        size_t chunk = (size_t)(nbytes - i) < sizeof(r) ? (size_t)(nbytes - i) : sizeof(r);
        memcpy(raw + i, &r, chunk);
    }
#else
    FILE *uf = fopen("/dev/urandom", "rb");
    if (!uf || (int64_t)fread(raw, 1, (size_t)nbytes, uf) != nbytes) {
        if (uf) fclose(uf);
        gray_panic_code("P0052", "crypto.random_hex: failed to read from /dev/urandom");
    }
    fclose(uf);
#endif

    static const char hex_chars[] = "0123456789abcdef";
    char *hex = gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    for (int64_t i = 0; i < length; i++) {
        int nibble = (i % 2 == 0) ? ((raw[i / 2] >> 4) & 0x0f) : (raw[i / 2] & 0x0f);
        hex[i] = hex_chars[nibble];
    }
    hex[length] = '\0';
    return (GrayString){hex, (int32_t)length};
}
