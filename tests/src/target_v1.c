/*
 * target_v1.c — Bifrost test binary, version 1
 *
 * Designed to be diffed against target_v2.c. Each function is a deliberate
 * test case for a different diff scenario:
 *
 *   xor_encrypt   — changed in v2 (key rotation added)
 *   checksum      — changed in v2 (FNV-1a replaces naive sum)
 *   find_pattern  — identical in v2 (tests true-positive match)
 *   encode_base16 — identical in v2 (tests true-positive match)
 *   clear_buffer  — RENAMED to zero_buffer in v2, body identical
 *                   (tests name-independent structural match)
 *   classify      — same behaviour in v2 but branch order swapped
 *                   (tests non-positional basic-block matching)
 *   decode_base16 — REMOVED in v2 (tests deletion detection)
 *   reverse_bytes — REMOVED in v2 (tests deletion detection)
 */

#include <stddef.h>
#include <stdint.h>

/* XOR encrypt with a fixed key — v1: same key byte applied to every byte */
void xor_encrypt(uint8_t* buf, uint8_t key, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] ^= key;
}

/* Checksum — v1: naive byte sum */
uint32_t checksum(const uint8_t* buf, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += buf[i];
    return sum;
}

/* Linear pattern search — unchanged between versions */
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

/* Hex encode — unchanged between versions */
void encode_base16(char* out, const uint8_t* in, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[in[i] & 0xf];
    }
    out[len * 2] = '\0';
}

/* Zero a buffer — RENAMED to zero_buffer in v2, body identical.
   Tests that matching does not depend on the symbol name. */
void clear_buffer(uint8_t* buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)0;
}

/* Classify an integer — v1 tests the negative case first. v2 has identical
   behaviour but tests the positive case first, so the basic-block order in the
   CFG differs. Tests the structural (non-positional) block matcher. */
int classify(int x)
{
    if (x < 0)
        return -1;
    else if (x == 0)
        return 0;
    else
        return 1;
}

/* Hex decode — present in v1, REMOVED in v2 */
int decode_base16(uint8_t* out, const char* in, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        uint8_t hi, lo;
        char c = in[i * 2];
        if      (c >= '0' && c <= '9') hi = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') hi = (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') hi = (uint8_t)(c - 'A' + 10);
        else return -1;

        c = in[i * 2 + 1];
        if      (c >= '0' && c <= '9') lo = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') lo = (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') lo = (uint8_t)(c - 'A' + 10);
        else return -1;

        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* Reverse bytes in place — present in v1, REMOVED in v2 */
void reverse_bytes(uint8_t* buf, size_t len)
{
    size_t lo = 0, hi = len - 1;
    while (lo < hi) {
        uint8_t tmp = buf[lo];
        buf[lo]  = buf[hi];
        buf[hi]  = tmp;
        lo++;
        hi--;
    }
}
