/*
 * BLAKE3 - Compact Implementation for Blockchain PoS
 * Based on the BLAKE3 reference implementation
 * https://github.com/BLAKE3-team/BLAKE3
 */

#ifndef BLAKE3_H
#define BLAKE3_H

#include <stdint.h>
#include <stddef.h>

#define BLAKE3_KEY_LEN 32
#define BLAKE3_OUT_LEN 32
#define BLAKE3_BLOCK_LEN 64
#define BLAKE3_CHUNK_LEN 1024

// BLAKE3 state
typedef struct {
    uint32_t cv[8];
    uint8_t chunk_buf[BLAKE3_CHUNK_LEN];
    uint8_t cv_stack[54 * 32];  // Stack of chaining values
    uint8_t cv_stack_len;
    uint64_t chunk_counter;
    size_t buf_len;
    uint8_t flags;
} blake3_hasher;

// Initialize hasher
void blake3_hasher_init(blake3_hasher *self);

// Initialize with key
void blake3_hasher_init_keyed(blake3_hasher *self, const uint8_t key[BLAKE3_KEY_LEN]);

// Update with data
void blake3_hasher_update(blake3_hasher *self, const void *input, size_t input_len);

// Finalize and output hash
void blake3_hasher_finalize(const blake3_hasher *self, uint8_t *out, size_t out_len);

// Convenience function: one-shot hashing
void blake3(const uint8_t *input, size_t input_len, uint8_t output[BLAKE3_OUT_LEN]);

// Hash with truncation
void blake3_truncated(const uint8_t *input, size_t input_len, uint8_t *output, size_t out_len);

#endif // BLAKE3_H
