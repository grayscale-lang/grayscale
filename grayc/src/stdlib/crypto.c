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

GrayString gray_crypto_sha256(GrayArena *arena, GrayString data) {
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    size_t len = (size_t)data.len;
    size_t bits = len * 8;
    size_t padded = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = (uint8_t *)gray_arena_alloc(arena, padded);
    memcpy(msg, data.data, len);
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

    char *hex = gray_arena_alloc(arena, 65);
    for (int i = 0; i < 8; i++)
        snprintf(hex + i*8, 9, "%08x", h[i]);
    hex[64] = '\0';
    GrayString r = { hex, 64 };
    return r;
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

    char *hex = gray_arena_alloc(arena, 33);
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
        char *empty = gray_arena_alloc(arena, 1);
        empty[0] = '\0';
        return (GrayString){empty, 0};
    }

    int64_t nbytes = (length + 1) / 2;
    uint8_t *raw = (uint8_t *)gray_arena_alloc(arena, (size_t)nbytes);

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
    char *hex = gray_arena_alloc(arena, (size_t)length + 1);
    for (int64_t i = 0; i < length; i++) {
        int nibble = (i % 2 == 0) ? ((raw[i / 2] >> 4) & 0x0f) : (raw[i / 2] & 0x0f);
        hex[i] = hex_chars[nibble];
    }
    hex[length] = '\0';
    return (GrayString){hex, (int32_t)length};
}
