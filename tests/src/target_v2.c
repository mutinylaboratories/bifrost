/*
 * target_v2.c — Bifrost test binary, version 2
 *
 * Designed to be diffed against target_v1.c. Change summary:
 *
 *   xor_encrypt   — CHANGED: key is now rotated each byte (rolling XOR)
 *   checksum      — CHANGED: FNV-1a replaces the naive sum
 *   find_pattern  — identical to v1 (should match 100%)
 *   encode_base16 — identical to v1 (should match 100%)
 *   zero_buffer   — RENAMED from v1's clear_buffer, body identical
 *                   (should still match despite the new name)
 *   classify      — same behaviour as v1 but branch order swapped
 *                   (matched structurally, similarity < 1.0)
 *   decode_base16 — REMOVED (was in v1)
 *   reverse_bytes — REMOVED (was in v1)
 *   popcount32    — NEW: not present in v1
 *   rotate_left32 — NEW: not present in v1
 */

#include <stddef.h>
#include <stdint.h>

/* XOR encrypt — v2: key rotates each byte (rolling XOR) */
void xor_encrypt(uint8_t* buf, uint8_t key, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] ^= key;
        key = (uint8_t)((key << 1) | (key >> 7));  /* rotate key left by 1 */
    }
}

/* Checksum — v2: FNV-1a 32-bit */
uint32_t checksum(const uint8_t* buf, size_t len)
{
    uint32_t hash = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++) {
        hash ^= buf[i];
        hash *= 0x01000193u;
    }
    return hash;
}

/* Linear pattern search — unchanged from v1 */
int find_pattern(const uint8_t* buf, const uint8_t* pattern, size_t buf_len, size_t pat_len)
{
    if (pat_len == 0 || pat_len > buf_len)
        return -1;
    for (size_t i = 0; i <= buf_len - pat_len; i++) {
        size_t j;
        for (j = 0; j < pat_len; j++) {
            if (buf[i + j] != pattern[j])
                break;
        }
        if (j == pat_len)
            return (int)i;
    }
    return -1;
}

/* Hex encode — unchanged from v1 */
void encode_base16(char* out, const uint8_t* in, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[in[i] & 0xf];
    }
    out[len * 2] = '\0';
}

/* Zero a buffer — RENAMED from v1's clear_buffer; body identical. Should still
   match structurally even though the symbol name changed. */
void zero_buffer(uint8_t* buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)0;
}

/* Classify an integer — same behaviour as v1 but the positive case is tested
   first, changing the basic-block order in the CFG. */
int classify(int x)
{
    if (x > 0)
        return 1;
    else if (x == 0)
        return 0;
    else
        return -1;
}

/* Population count — NEW in v2 */
int popcount32(uint32_t x)
{
    int count = 0;
    while (x) {
        count += (int)(x & 1u);
        x >>= 1;
    }
    return count;
}

/* Rotate left 32-bit — NEW in v2 */
uint32_t rotate_left32(uint32_t x, unsigned int n)
{
    n &= 31u;
    return (x << n) | (x >> (32u - n));
}
