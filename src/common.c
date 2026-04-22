#include "common.h"
#include <stdarg.h>
#include <sys/time.h>

// Global benchmark stats
BenchmarkStats g_benchmark = {0};
static LogLevel current_log_level = LOG_INFO;

// =============================================================================
// HASH UTILITIES
// =============================================================================

void sha256(const uint8_t* data, size_t len, uint8_t hash[32]) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(hash, &ctx);
}

void sha256_multi(uint8_t hash[32], size_t num_inputs, ...) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    
    va_list args;
    va_start(args, num_inputs);
    
    for (size_t i = 0; i < num_inputs; i++) {
        const uint8_t* data = va_arg(args, const uint8_t*);
        size_t len = va_arg(args, size_t);
        SHA256_Update(&ctx, data, len);
    }
    
    va_end(args);
    SHA256_Final(hash, &ctx);
}

void ripemd160(const uint8_t* data, size_t len, uint8_t hash[20]) {
    RIPEMD160_CTX ctx;
    RIPEMD160_Init(&ctx);
    RIPEMD160_Update(&ctx, data, len);
    RIPEMD160_Final(hash, &ctx);
}

void hash160(const uint8_t* data, size_t len, uint8_t hash[20]) {
    uint8_t sha_hash[32];
    sha256(data, len, sha_hash);
    ripemd160(sha_hash, 32, hash);
}

void sha256_truncated(const uint8_t* data, size_t len, uint8_t hash[28]) {
    uint8_t full_hash[32];
    sha256(data, len, full_hash);
    memcpy(hash, full_hash, 28);
}

// =============================================================================
// BLAKE3 UTILITIES (for Proof of Space)
// =============================================================================

void blake3_hash(const uint8_t* data, size_t len, uint8_t hash[32]) {
    blake3(data, len, hash);
}

void blake3_hash_truncated(const uint8_t* data, size_t len, uint8_t* hash, size_t hash_len) {
    blake3_truncated(data, len, hash, hash_len);
}

// =============================================================================
// HEX CONVERSION
// =============================================================================

static const char hex_chars[] = "0123456789abcdef";

char* bytes_to_hex(const uint8_t* bytes, size_t len) {
    char* hex = malloc(len * 2 + 1);
    if (!hex) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len * 2] = '\0';
    return hex;
}

void bytes_to_hex_buf(const uint8_t* bytes, size_t len, char* hex) {
    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len * 2] = '\0';
}

static int hex_char_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint8_t* hex_to_bytes(const char* hex, size_t* out_len) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return NULL;
    
    size_t byte_len = hex_len / 2;
    uint8_t* bytes = malloc(byte_len);
    if (!bytes) return NULL;
    
    for (size_t i = 0; i < byte_len; i++) {
        int high = hex_char_value(hex[i * 2]);
        int low = hex_char_value(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            free(bytes);
            return NULL;
        }
        bytes[i] = (high << 4) | low;
    }
    
    if (out_len) *out_len = byte_len;
    return bytes;
}

bool hex_to_bytes_buf(const char* hex, uint8_t* bytes, size_t byte_len) {
    for (size_t i = 0; i < byte_len; i++) {
        int high = hex_char_value(hex[i * 2]);
        int low = hex_char_value(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = (high << 4) | low;
    }
    
    return true;
}

// =============================================================================
// TIME UTILITIES
// =============================================================================

uint64_t get_current_timestamp(void) {
    return (uint64_t)time(NULL);
}

uint64_t get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void format_timestamp(uint64_t ts, char* buffer, size_t size) {
    time_t t = (time_t)ts;
    struct tm* tm_info = localtime(&t);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// =============================================================================
// COMPARISON UTILITIES
// =============================================================================

int compare_bytes(const uint8_t* a, const uint8_t* b, size_t len) {
    return memcmp(a, b, len);
}

bool is_zero(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (data[i] != 0) return false;
    }
    return true;
}

uint32_t count_leading_zeros(const uint8_t* data, size_t len) {
    uint32_t zeros = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == 0) {
            zeros += 8;
        } else {
            // Count leading zeros in this byte
            uint8_t b = data[i];
            while ((b & 0x80) == 0) {
                zeros++;
                b <<= 1;
            }
            break;
        }
    }
    return zeros;
}

// =============================================================================
// MEMORY UTILITIES
// =============================================================================

void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "FATAL: malloc failed for %zu bytes\n", size);
        exit(1);
    }
    return ptr;
}

void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "FATAL: realloc failed for %zu bytes\n", size);
        exit(1);
    }
    return new_ptr;
}

void secure_free(void* ptr, size_t size) {
    if (ptr) {
        memset(ptr, 0, size);
        free(ptr);
    }
}

// =============================================================================
// STRING UTILITIES
// =============================================================================

void safe_strcpy(char* dest, const char* src, size_t dest_size) {
    if (dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

bool starts_with(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

void trim(char* str) {
    char* start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n')) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n')) {
        str[--len] = '\0';
    }
}

// =============================================================================
// LOGGING
// =============================================================================

void set_log_level(LogLevel level) {
    current_log_level = level;
}

void log_msg(LogLevel level, const char* fmt, ...) {
    if (level < current_log_level) return;
    
    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    const char* colors[] = {"\033[36m", "\033[32m", "\033[33m", "\033[31m"};
    
    char timestamp[32];
    format_timestamp(get_current_timestamp(), timestamp, sizeof(timestamp));
    
    fprintf(stderr, "%s[%s] %s: ", colors[level], level_str[level], timestamp);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\033[0m\n");
}

// =============================================================================
// BENCHMARKING
// =============================================================================

void benchmark_init(void) {
    memset(&g_benchmark, 0, sizeof(g_benchmark));
    g_benchmark.start_time_ms = get_current_time_ms();
    g_benchmark.min_tx_latency_ms = UINT64_MAX;
}

void benchmark_tx_created(void) {
    g_benchmark.tx_created_count++;
}

void benchmark_tx_confirmed(uint64_t latency_ms) {
    g_benchmark.tx_confirmed_count++;
    g_benchmark.total_tx_latency_ms += latency_ms;
    
    if (latency_ms < g_benchmark.min_tx_latency_ms) {
        g_benchmark.min_tx_latency_ms = latency_ms;
    }
    if (latency_ms > g_benchmark.max_tx_latency_ms) {
        g_benchmark.max_tx_latency_ms = latency_ms;
    }
}

void benchmark_block_created(void) {
    g_benchmark.blocks_created++;
    g_benchmark.last_block_time_ms = get_current_time_ms();
}

void benchmark_validator_work(uint64_t work_ms) {
    g_benchmark.total_validator_work_ms += work_ms;
}

void benchmark_report(char* buffer, size_t size) {
    uint64_t elapsed_ms = get_current_time_ms() - g_benchmark.start_time_ms;
    double elapsed_sec = elapsed_ms / 1000.0;
    
    double tx_throughput = elapsed_sec > 0 ? g_benchmark.tx_confirmed_count / elapsed_sec : 0;
    double avg_latency = g_benchmark.tx_confirmed_count > 0 ? 
        (double)g_benchmark.total_tx_latency_ms / g_benchmark.tx_confirmed_count : 0;
    double block_rate = elapsed_sec > 0 ? g_benchmark.blocks_created / elapsed_sec : 0;
    
    snprintf(buffer, size,
        "\n"
        "╔══════════════════════════════════════════════════════════════╗\n"
        "║                    BENCHMARK REPORT                          ║\n"
        "╠══════════════════════════════════════════════════════════════╣\n"
        "║  Runtime:              %10.2f seconds                    ║\n"
        "║                                                              ║\n"
        "║  TRANSACTIONS:                                               ║\n"
        "║    Created:            %10lu                             ║\n"
        "║    Confirmed:          %10lu                             ║\n"
        "║    Throughput:         %10.2f tx/sec                     ║\n"
        "║    Avg Latency:        %10.2f ms                         ║\n"
        "║    Min Latency:        %10lu ms                          ║\n"
        "║    Max Latency:        %10lu ms                          ║\n"
        "║                                                              ║\n"
        "║  BLOCKS:                                                     ║\n"
        "║    Created:            %10lu                             ║\n"
        "║    Block Rate:         %10.2f blocks/sec                 ║\n"
        "║                                                              ║\n"
        "║  VALIDATORS:                                                 ║\n"
        "║    Total Work Time:    %10lu ms                          ║\n"
        "╚══════════════════════════════════════════════════════════════╝\n",
        elapsed_sec,
        g_benchmark.tx_created_count,
        g_benchmark.tx_confirmed_count,
        tx_throughput,
        avg_latency,
        g_benchmark.min_tx_latency_ms == UINT64_MAX ? 0 : g_benchmark.min_tx_latency_ms,
        g_benchmark.max_tx_latency_ms,
        g_benchmark.blocks_created,
        block_rate,
        g_benchmark.total_validator_work_ms
    );
}

void benchmark_reset(void) {
    benchmark_init();
}
