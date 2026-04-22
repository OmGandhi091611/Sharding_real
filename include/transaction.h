#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Forward declarations
typedef struct Wallet Wallet;

// =============================================================================
// COMPACT TRANSACTION STRUCTURE - 112 bytes total
// =============================================================================
// Layout (Ethereum-inspired with nonce and expiry):
//   nonce              = 8 bytes  - Unique nonce per sender (like Ethereum)
//   expiry_block       = 4 bytes  - Block height after which tx expires (0 = no expiry)
//   source_address[20] = 20 bytes - RIPEMD160(SHA256(pubkey)) or COINBASE marker
//   dest_address[20]   = 20 bytes - RIPEMD160(SHA256(pubkey))
//   value              = 8 bytes  - Amount in smallest units (like satoshis)
//   fee                = 4 bytes  - Fee in smallest units (max ~4 billion)
//   signature[48]      = 48 bytes - BLS signature (compact)
//   TOTAL              = 8 + 4 + 20 + 20 + 8 + 4 + 48 = 112 bytes
// =============================================================================
// Note: Transaction ID is computed on-the-fly when needed as:
//       tx_id = BLAKE3(nonce || expiry_block || source || dest || value || fee)
// =============================================================================

#pragma pack(push, 1)

typedef struct {
    uint64_t nonce;              // 8 bytes:  Unique nonce per sender
    uint32_t expiry_block;       // 4 bytes:  Block height for expiry (0 = no expiry)
    uint8_t source_address[20];  // 20 bytes: Sender address (binary)
    uint8_t dest_address[20];    // 20 bytes: Recipient address (binary)
    uint64_t value;              // 8 bytes:  Amount in smallest units
    uint32_t fee;                // 4 bytes:  Fee in smallest units
    uint8_t signature[48];       // 48 bytes: BLS signature
} Transaction;                   // TOTAL: 112 bytes EXACTLY

#pragma pack(pop)

// Compile-time size check - CRITICAL!
_Static_assert(sizeof(Transaction) == 112, "Transaction must be exactly 112 bytes");

// =============================================================================
// CONSTANTS
// =============================================================================

#define TX_NONCE_LENGTH      8
#define TX_ADDRESS_LENGTH    20

// =============================================================================
// EXPIRY CONSTANTS
// =============================================================================
// Expiry is specified in blocks. With 6-second block time:
//   100 blocks  = ~10 minutes
//   300 blocks  = ~30 minutes
//   600 blocks  = ~1 hour
//   1000 blocks = ~1.6 hours

#define TX_NO_EXPIRY              0     // Never expires (use with caution!)
#define TX_DEFAULT_EXPIRY_BLOCKS  100   // Default: ~10 minutes (recommended)
#define TX_SHORT_EXPIRY_BLOCKS    50    // Short: ~5 minutes (time-sensitive)
#define TX_LONG_EXPIRY_BLOCKS     600   // Long: ~1 hour (congested network)
#define TX_MAX_EXPIRY_BLOCKS      10000 // Max: ~16 hours

// Helper macro to calculate expiry from current block
#define TX_EXPIRY_FROM_NOW(current_block, blocks) ((current_block) + (blocks))
#define TX_SIGNATURE_LENGTH  48
#define TX_TOTAL_SIZE        112
#define TX_HASH_SIZE         28    // Size of computed transaction hash

// Special coinbase marker (first 8 bytes = "COINBASE", rest = 0)
extern const uint8_t COINBASE_ADDRESS[20];

// =============================================================================
// MACROS
// =============================================================================

#define TX_IS_COINBASE(tx) (memcmp((tx)->source_address, COINBASE_ADDRESS, 20) == 0)
#define TX_NO_EXPIRY       0

// =============================================================================
// TRANSACTION FUNCTIONS
// =============================================================================

// Create transaction with nonce (requires Wallet for signing)
Transaction* transaction_create(const Wallet* wallet, 
                                const uint8_t dest_address[20],
                                uint64_t value, 
                                uint32_t fee,
                                uint64_t nonce,
                                uint32_t expiry_block);

// Create transaction from raw addresses (no Wallet). Uses deterministic BLAKE3-based
// signature so no keys needed. Use for load generators / parallel submission only.
Transaction* transaction_create_from_address(const uint8_t source_address[20],
                                             const uint8_t dest_address[20],
                                             uint64_t value, uint32_t fee,
                                             uint64_t nonce, uint32_t expiry_block);

// Create coinbase (reward + all fees from block)
Transaction* transaction_create_coinbase(const uint8_t farmer_address[20], 
                                         uint64_t base_reward,
                                         uint64_t total_fees,
                                         uint32_t block_height);

// Compute transaction hash (ID) - returns 28 bytes
// Hash = BLAKE3(nonce || expiry_block || source || dest || value || fee)[0:28]
void transaction_compute_hash(const Transaction* tx, uint8_t hash[TX_HASH_SIZE]);

// Get transaction hash as hex string (caller must free)
char* transaction_get_hash_hex(const Transaction* tx);

// Verify transaction signature
bool transaction_verify(const Transaction* tx);

// Sign transaction
bool transaction_sign(Transaction* tx, const Wallet* wallet);

// Check if transaction is expired at given block height
bool transaction_is_expired(const Transaction* tx, uint32_t current_block_height);

// =============================================================================
// SERIALIZATION (Protobuf-based)
// =============================================================================

// Serialize transaction to protobuf binary (caller must free, returns size)
uint8_t* transaction_serialize_pb(const Transaction* tx, size_t* out_len);

// Deserialize transaction from protobuf binary
Transaction* transaction_deserialize_pb(const uint8_t* data, size_t len);

// Serialize to hex string (for legacy compatibility)
char* transaction_serialize(const Transaction* tx);

// Deserialize from hex string (for legacy compatibility)
Transaction* transaction_deserialize(const char* hex_data);

// Free transaction
void transaction_destroy(Transaction* tx);

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

// Public key -> 20-byte address
void pubkey_to_address(const uint8_t* pubkey, size_t len, uint8_t address[20]);

// Address -> hex string (40 chars)
void address_to_hex(const uint8_t address[20], char hex[41]);

// Hex string -> address
bool hex_to_address(const char* hex, uint8_t address[20]);

// Transaction hash -> hex string (56 chars)
void txhash_to_hex(const uint8_t hash[TX_HASH_SIZE], char hex[TX_HASH_SIZE * 2 + 1]);

// Check if address is valid
bool address_is_valid(const uint8_t address[20]);

// Compare addresses
bool address_equals(const uint8_t a[20], const uint8_t b[20]);

// Convert wallet name to address (for demo: hash of name)
void wallet_name_to_address(const char* name, uint8_t address[20]);

#endif // TRANSACTION_H
