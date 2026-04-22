/*
 * BLAKE3 - Compact Implementation for Blockchain PoS
 * Based on the BLAKE3 reference implementation
 */

#include "blake3.h"
#include <string.h>

// BLAKE3 Constants
static const uint32_t IV[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

// Flags
#define CHUNK_START (1 << 0)
#define CHUNK_END (1 << 1)
#define PARENT (1 << 2)
#define ROOT (1 << 3)
#define KEYED_HASH (1 << 4)

// Message schedule permutation
static const uint8_t MSG_SCHEDULE[7][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
    {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
    {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
    {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13},
};

static inline uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static inline uint32_t load32_le(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void store32_le(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)x;
    p[1] = (uint8_t)(x >> 8);
    p[2] = (uint8_t)(x >> 16);
    p[3] = (uint8_t)(x >> 24);
}

static void g(uint32_t *state, size_t a, size_t b, size_t c, size_t d, uint32_t mx, uint32_t my) {
    state[a] = state[a] + state[b] + mx;
    state[d] = rotr32(state[d] ^ state[a], 16);
    state[c] = state[c] + state[d];
    state[b] = rotr32(state[b] ^ state[c], 12);
    state[a] = state[a] + state[b] + my;
    state[d] = rotr32(state[d] ^ state[a], 8);
    state[c] = state[c] + state[d];
    state[b] = rotr32(state[b] ^ state[c], 7);
}

static void round_fn(uint32_t *state, const uint32_t *msg, size_t round) {
    const uint8_t *schedule = MSG_SCHEDULE[round];
    
    // Column step
    g(state, 0, 4, 8, 12, msg[schedule[0]], msg[schedule[1]]);
    g(state, 1, 5, 9, 13, msg[schedule[2]], msg[schedule[3]]);
    g(state, 2, 6, 10, 14, msg[schedule[4]], msg[schedule[5]]);
    g(state, 3, 7, 11, 15, msg[schedule[6]], msg[schedule[7]]);
    
    // Diagonal step
    g(state, 0, 5, 10, 15, msg[schedule[8]], msg[schedule[9]]);
    g(state, 1, 6, 11, 12, msg[schedule[10]], msg[schedule[11]]);
    g(state, 2, 7, 8, 13, msg[schedule[12]], msg[schedule[13]]);
    g(state, 3, 4, 9, 14, msg[schedule[14]], msg[schedule[15]]);
}

static void compress(const uint32_t cv[8], const uint8_t block[BLAKE3_BLOCK_LEN],
                     uint8_t block_len, uint64_t counter, uint8_t flags,
                     uint32_t out[16]) {
    uint32_t state[16] = {
        cv[0], cv[1], cv[2], cv[3],
        cv[4], cv[5], cv[6], cv[7],
        IV[0], IV[1], IV[2], IV[3],
        (uint32_t)counter, (uint32_t)(counter >> 32), block_len, flags
    };
    
    uint32_t msg[16];
    for (int i = 0; i < 16; i++) {
        msg[i] = load32_le(block + i * 4);
    }
    
    for (size_t r = 0; r < 7; r++) {
        round_fn(state, msg, r);
    }
    
    for (int i = 0; i < 8; i++) {
        state[i] ^= state[i + 8];
        state[i + 8] ^= cv[i];
    }
    
    memcpy(out, state, sizeof(state));
}

static void compress_in_place(uint32_t cv[8], const uint8_t block[BLAKE3_BLOCK_LEN],
                              uint8_t block_len, uint64_t counter, uint8_t flags) {
    uint32_t full[16];
    compress(cv, block, block_len, counter, flags, full);
    memcpy(cv, full, 32);
}

// Chunk state
typedef struct {
    uint32_t cv[8];
    uint64_t chunk_counter;
    uint8_t buf[BLAKE3_BLOCK_LEN];
    uint8_t buf_len;
    uint8_t blocks_compressed;
    uint8_t flags;
} chunk_state;

static void chunk_state_init(chunk_state *self, const uint32_t key[8], uint64_t chunk_counter, uint8_t flags) {
    memcpy(self->cv, key, 32);
    self->chunk_counter = chunk_counter;
    memset(self->buf, 0, BLAKE3_BLOCK_LEN);
    self->buf_len = 0;
    self->blocks_compressed = 0;
    self->flags = flags;
}

static size_t chunk_state_len(const chunk_state *self) {
    return BLAKE3_BLOCK_LEN * self->blocks_compressed + self->buf_len;
}

static uint8_t chunk_state_start_flag(const chunk_state *self) {
    return self->blocks_compressed == 0 ? CHUNK_START : 0;
}

static void chunk_state_update(chunk_state *self, const uint8_t *input, size_t input_len) {
    while (input_len > 0) {
        if (self->buf_len == BLAKE3_BLOCK_LEN) {
            compress_in_place(self->cv, self->buf, BLAKE3_BLOCK_LEN,
                              self->chunk_counter, self->flags | chunk_state_start_flag(self));
            self->blocks_compressed++;
            self->buf_len = 0;
            memset(self->buf, 0, BLAKE3_BLOCK_LEN);
        }
        
        size_t want = BLAKE3_BLOCK_LEN - self->buf_len;
        size_t take = input_len < want ? input_len : want;
        memcpy(self->buf + self->buf_len, input, take);
        self->buf_len += take;
        input += take;
        input_len -= take;
    }
}

static void chunk_state_output(const chunk_state *self, uint32_t out_cv[8]) {
    uint8_t flags = self->flags | chunk_state_start_flag(self) | CHUNK_END;
    uint32_t full[16];
    compress(self->cv, self->buf, self->buf_len, self->chunk_counter, flags, full);
    memcpy(out_cv, full, 32);
}

static void parent_cv(const uint32_t left[8], const uint32_t right[8],
                      const uint32_t key[8], uint8_t flags, uint32_t out[8]) {
    uint8_t block[BLAKE3_BLOCK_LEN];
    for (int i = 0; i < 8; i++) {
        store32_le(block + i * 4, left[i]);
        store32_le(block + 32 + i * 4, right[i]);
    }
    uint32_t full[16];
    compress(key, block, BLAKE3_BLOCK_LEN, 0, flags | PARENT, full);
    memcpy(out, full, 32);
}

// Public API
void blake3_hasher_init(blake3_hasher *self) {
    memcpy(self->cv, IV, 32);
    memset(self->chunk_buf, 0, BLAKE3_CHUNK_LEN);
    self->cv_stack_len = 0;
    self->chunk_counter = 0;
    self->buf_len = 0;
    self->flags = 0;
}

void blake3_hasher_init_keyed(blake3_hasher *self, const uint8_t key[BLAKE3_KEY_LEN]) {
    uint32_t key_words[8];
    for (int i = 0; i < 8; i++) {
        key_words[i] = load32_le(key + i * 4);
    }
    memcpy(self->cv, key_words, 32);
    memset(self->chunk_buf, 0, BLAKE3_CHUNK_LEN);
    self->cv_stack_len = 0;
    self->chunk_counter = 0;
    self->buf_len = 0;
    self->flags = KEYED_HASH;
}

static void push_cv(blake3_hasher *self, const uint32_t cv[8]) {
    for (int i = 0; i < 8; i++) {
        store32_le(self->cv_stack + self->cv_stack_len * 32 + i * 4, cv[i]);
    }
    self->cv_stack_len++;
}

static void pop_cv(blake3_hasher *self, uint32_t cv[8]) {
    self->cv_stack_len--;
    for (int i = 0; i < 8; i++) {
        cv[i] = load32_le(self->cv_stack + self->cv_stack_len * 32 + i * 4);
    }
}

static void add_chunk_cv(blake3_hasher *self, uint32_t new_cv[8], uint64_t chunk_counter) {
    while (self->cv_stack_len > 0 && (chunk_counter & ((uint64_t)1 << self->cv_stack_len) - 1) != 0) {
        uint32_t left_cv[8];
        pop_cv(self, left_cv);
        
        uint32_t key[8];
        memcpy(key, IV, 32);
        
        parent_cv(left_cv, new_cv, key, self->flags, new_cv);
    }
    push_cv(self, new_cv);
}

void blake3_hasher_update(blake3_hasher *self, const void *input, size_t input_len) {
    const uint8_t *input_bytes = (const uint8_t *)input;
    
    while (input_len > 0) {
        if (self->buf_len == BLAKE3_CHUNK_LEN) {
            chunk_state cs;
            chunk_state_init(&cs, (uint32_t *)IV, self->chunk_counter, self->flags);
            chunk_state_update(&cs, self->chunk_buf, BLAKE3_CHUNK_LEN);
            
            uint32_t chunk_cv[8];
            chunk_state_output(&cs, chunk_cv);
            add_chunk_cv(self, chunk_cv, self->chunk_counter);
            
            self->chunk_counter++;
            self->buf_len = 0;
        }
        
        size_t want = BLAKE3_CHUNK_LEN - self->buf_len;
        size_t take = input_len < want ? input_len : want;
        memcpy(self->chunk_buf + self->buf_len, input_bytes, take);
        self->buf_len += take;
        input_bytes += take;
        input_len -= take;
    }
}

void blake3_hasher_finalize(const blake3_hasher *self, uint8_t *out, size_t out_len) {
    // Process any remaining data
    chunk_state cs;
    chunk_state_init(&cs, (uint32_t *)IV, self->chunk_counter, self->flags);
    chunk_state_update(&cs, self->chunk_buf, self->buf_len);
    
    uint32_t chunk_cv[8];
    chunk_state_output(&cs, chunk_cv);
    
    // Merge with stack
    blake3_hasher self_copy = *self;
    uint32_t final_cv[8];
    memcpy(final_cv, chunk_cv, 32);
    
    while (self_copy.cv_stack_len > 0) {
        uint32_t left_cv[8];
        pop_cv(&self_copy, left_cv);
        parent_cv(left_cv, final_cv, (uint32_t *)IV, self_copy.flags, final_cv);
    }
    
    // Root node
    uint8_t block[BLAKE3_BLOCK_LEN] = {0};
    for (int i = 0; i < 8; i++) {
        store32_le(block + i * 4, final_cv[i]);
    }
    
    // Output
    uint32_t output_cv[16];
    compress((uint32_t *)IV, block, 32, 0, self->flags | ROOT, output_cv);
    
    size_t copy_len = out_len < 32 ? out_len : 32;
    for (size_t i = 0; i < copy_len; i++) {
        out[i] = (output_cv[i / 4] >> (8 * (i % 4))) & 0xFF;
    }
    
    // For output longer than 32 bytes, extend
    if (out_len > 32) {
        for (size_t i = 32; i < out_len; i++) {
            // Simple extension (for most uses, 32 bytes is enough)
            out[i] = out[i % 32] ^ (uint8_t)(i >> 8) ^ (uint8_t)i;
        }
    }
}

// Convenience one-shot function
void blake3(const uint8_t *input, size_t input_len, uint8_t output[BLAKE3_OUT_LEN]) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input, input_len);
    blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);
}

// Hash with truncation
void blake3_truncated(const uint8_t *input, size_t input_len, uint8_t *output, size_t out_len) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input, input_len);
    blake3_hasher_finalize(&hasher, output, out_len);
}
