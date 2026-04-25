#ifndef WALLET_H
#define WALLET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// WALLET STRUCTURE
// =============================================================================
// Wallet stores keys and derives 20-byte address from public key
// Address = RIPEMD160(SHA256(public_key))
// Also tracks transaction nonce for Ethereum-style nonce management
// =============================================================================

// Forward declaration - EVP_PKEY is defined in openssl/evp.h
typedef struct evp_pkey_st EVP_PKEY;

typedef struct Wallet {
    char name[64];               // Wallet name (e.g., "farmer1")
    uint8_t address[20];         // 20-byte binary address
    char address_hex[41];        // Hex representation of address
    uint8_t public_key[32];      // Ed25519 public key (32 bytes)
    EVP_PKEY* evp_key;           // OpenSSL key handle (Ed25519)
    char private_key_pem[2048];  // PEM-encoded private key
    uint64_t nonce;              // Transaction nonce counter
} Wallet;

// =============================================================================
// WALLET FUNCTIONS
// =============================================================================

// Create new wallet with random keys
Wallet* wallet_create(void);

// Create wallet with specific name (for demo, derives keys from name hash)
Wallet* wallet_create_named(const char* name);

// Load wallet from file
Wallet* wallet_load(const char* filepath);

// Save wallet to file
bool wallet_save(const Wallet* wallet, const char* filepath);

// Get wallet's 20-byte address
const uint8_t* wallet_get_address(const Wallet* wallet);

// Get wallet's address as hex string
const char* wallet_get_address_hex(const Wallet* wallet);

// Get and increment nonce
uint64_t wallet_get_next_nonce(Wallet* wallet);

// Get current nonce without incrementing
uint64_t wallet_get_nonce(const Wallet* wallet);

// Set nonce (for sync with blockchain state)
void wallet_set_nonce(Wallet* wallet, uint64_t nonce);

// Sign message with Ed25519 private key (returns 64-byte signature)
bool wallet_sign(const Wallet* wallet, const uint8_t* message, size_t msg_len,
                 uint8_t signature[64]);

// Verify Ed25519 signature against a 32-byte public key
bool wallet_verify(const uint8_t public_key[32],
                   const uint8_t* message, size_t msg_len,
                   const uint8_t signature[64]);

// Free wallet memory
void wallet_destroy(Wallet* wallet);

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

// Derive 20-byte address from Ed25519 public key: BLAKE3(pubkey)[0:20]
void wallet_derive_address(const uint8_t pubkey[32], uint8_t address[20]);

// Convert name to deterministic address (for demo/testing)
void wallet_name_to_address(const char* name, uint8_t address[20]);

// Check if name looks like a hex address (40 chars)
bool wallet_is_hex_address(const char* str);

// Parse address from name or hex string
bool wallet_parse_address(const char* str, uint8_t address[20]);

// Convert address to hex string
void address_to_hex(const uint8_t address[20], char hex[41]);

// Convert hex string to address
bool hex_to_address(const char* hex, uint8_t address[20]);

// Convert transaction hash to hex string
void txhash_to_hex(const uint8_t hash[28], char hex[57]);

// Check if address is valid (non-zero)
bool address_is_valid(const uint8_t address[20]);

// Compare two addresses
bool address_equals(const uint8_t a[20], const uint8_t b[20]);

#endif // WALLET_H
