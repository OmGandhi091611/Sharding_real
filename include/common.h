#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/evp.h>
#include "blake3.h"
#include "node.h"

// =============================================================================
// HASH UTILITIES
// =============================================================================

// SHA256 wrapper
void sha256(const uint8_t* data, size_t len, uint8_t hash[32]);

// SHA256 with multiple inputs
void sha256_multi(uint8_t hash[32], size_t num_inputs, ...);

// RIPEMD160 wrapper
void ripemd160(const uint8_t* data, size_t len, uint8_t hash[20]);

// Double hash: RIPEMD160(SHA256(data)) - used for addresses
void hash160(const uint8_t* data, size_t len, uint8_t hash[20]);

// Truncate SHA256 to 28 bytes
void sha256_truncated(const uint8_t* data, size_t len, uint8_t hash[28]);

// =============================================================================
// BLAKE3 UTILITIES (for Proof of Space plotting)
// =============================================================================

// BLAKE3 hash (32 bytes output)
void blake3_hash(const uint8_t* data, size_t len, uint8_t hash[32]);

// BLAKE3 with truncation (for 28-byte plot hashes)
void blake3_hash_truncated(const uint8_t* data, size_t len, uint8_t* hash, size_t hash_len);

// =============================================================================
// HEX CONVERSION
// =============================================================================

// Bytes to hex string (caller must free)
char* bytes_to_hex(const uint8_t* bytes, size_t len);

// Bytes to hex string with provided buffer
void bytes_to_hex_buf(const uint8_t* bytes, size_t len, char* hex);

// Hex string to bytes (caller must free)
uint8_t* hex_to_bytes(const char* hex, size_t* out_len);

// Hex string to bytes with provided buffer
bool hex_to_bytes_buf(const char* hex, uint8_t* bytes, size_t max_len);

// =============================================================================
// TIME UTILITIES
// =============================================================================

// Get current Unix timestamp (seconds)
uint64_t get_current_timestamp(void);

// Get current time in milliseconds
uint64_t get_current_time_ms(void);

// Format timestamp to string
void format_timestamp(uint64_t ts, char* buffer, size_t size);

// =============================================================================
// COMPARISON UTILITIES
// =============================================================================

// Compare two byte arrays (returns <0, 0, >0 like memcmp)
int compare_bytes(const uint8_t* a, const uint8_t* b, size_t len);

// Check if byte array is all zeros
bool is_zero(const uint8_t* data, size_t len);

// Count leading zero bits in byte array
uint32_t count_leading_zeros(const uint8_t* data, size_t len);

// =============================================================================
// MEMORY UTILITIES
// =============================================================================

// Safe malloc with error handling
void* safe_malloc(size_t size);

// Safe realloc with error handling
void* safe_realloc(void* ptr, size_t size);

// Zero and free sensitive data
void secure_free(void* ptr, size_t size);

// =============================================================================
// STRING UTILITIES
// =============================================================================

// Safe string copy
void safe_strcpy(char* dest, const char* src, size_t dest_size);

// String starts with prefix
bool starts_with(const char* str, const char* prefix);

// Trim whitespace
void trim(char* str);

// =============================================================================
// LOGGING
// =============================================================================

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
} LogLevel;

// Set log level
void set_log_level(LogLevel level);

// Log message
void log_msg(LogLevel level, const char* fmt, ...);

#define LOG_DEBUG(fmt, ...) log_msg(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_msg(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_msg(LOG_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_msg(LOG_ERROR, fmt, ##__VA_ARGS__)

// Professional emoji prefixes for different log categories
#define EMOJI_BLOCK     "⛏️  BLOCK"
#define EMOJI_REWARD    "💰 REWARD"
#define EMOJI_TX        "📦 TX"
#define EMOJI_WINNER    "🏆 WINNER"
#define EMOJI_CHALLENGE "📡 CHALLENGE"
#define EMOJI_SUCCESS   "✅ SUCCESS"
#define EMOJI_FAIL      "❌ FAIL"
#define EMOJI_PROOF     "⚡ PROOF"
#define EMOJI_CHAIN     "🔗 CHAIN"
#define EMOJI_WALLET    "💼 WALLET"
#define EMOJI_STATS     "📊 STATS"
#define EMOJI_TIME      "⏱️  TIME"
#define EMOJI_DIFF      "🎯 DIFFICULTY"
#define EMOJI_POOL      "🏊 POOL"
#define EMOJI_FARMER    "🌾 FARMER"
#define EMOJI_WAIT      "⏳ PENDING"
#define EMOJI_SEND      "📤 SEND"
#define EMOJI_RECV      "📥 RECV"
#define EMOJI_COIN      "🪙 COIN"
#define EMOJI_UP        "📈 UP"
#define EMOJI_DOWN      "📉 DOWN"
#define EMOJI_CONNECT   "🔌 CONNECT"
#define EMOJI_START     "🚀 START"
#define EMOJI_STOP      "🛑 STOP"
#define EMOJI_SEARCH    "🔍 SEARCH"
#define EMOJI_CONFIRM   "✔️  CONFIRM"
#define EMOJI_GENESIS   "🌅 GENESIS"

// =============================================================================
// BENCHMARKING
// =============================================================================

typedef struct {
    uint64_t tx_created_count;
    uint64_t tx_confirmed_count;
    uint64_t blocks_created;
    uint64_t total_tx_latency_ms;
    uint64_t min_tx_latency_ms;
    uint64_t max_tx_latency_ms;
    uint64_t start_time_ms;
    uint64_t last_block_time_ms;
    uint32_t active_validators;
    uint64_t total_validator_work_ms;
} BenchmarkStats;

// Global benchmark stats
extern BenchmarkStats g_benchmark;

// Initialize benchmarking
void benchmark_init(void);

// Record transaction creation
void benchmark_tx_created(void);

// Record transaction confirmation with latency
void benchmark_tx_confirmed(uint64_t latency_ms);

// Record block creation
void benchmark_block_created(void);

// Record validator work time
void benchmark_validator_work(uint64_t work_ms);

// Get benchmark report
void benchmark_report(char* buffer, size_t size);

// Reset benchmark stats
void benchmark_reset(void);

#endif // COMMON_H
