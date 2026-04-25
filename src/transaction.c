/**
 * ============================================================================
 * TRANSACTION.C - Ed25519 Transaction (128 bytes)
 * ============================================================================
 *
 * TRANSACTION STRUCTURE (128 bytes total):
 * ========================================
 * ┌────────────────────────────────────────────────────────────────────────┐
 * │ Field          │ Size    │ Description                                 │
 * ├────────────────┼─────────┼─────────────────────────────────────────────┤
 * │ nonce          │ 8 bytes │ Unique per sender (prevents replay attacks) │
 * │ expiry_block   │ 4 bytes │ Block height after which tx expires         │
 * │ source_address │ 20 bytes│ BLAKE3(Ed25519_pubkey)[0:20]                │
 * │ dest_address   │ 20 bytes│ Recipient's address                         │
 * │ value          │ 8 bytes │ Amount to transfer                          │
 * │ fee            │ 4 bytes │ Transaction fee                             │
 * │ signature      │ 64 bytes│ Ed25519 signature                           │
 * └────────────────┴─────────┴─────────────────────────────────────────────┘
 *
 * PUBLIC KEY SEPARATION (SegWit-inspired):
 *   On-chain TX:  128 bytes (no pubkey stored inside)
 *   Network msg:  128 + 32 = 160 bytes (protobuf carries pubkey separately)
 *   Mempool:      pubkey stored alongside each TX for shard verification
 *
 * VERIFICATION:
 *   transaction_verify()          — basic non-zero sig check (no pubkey)
 *   transaction_verify_ed25519()  — full Ed25519 + address derivation check
 *
 * ============================================================================
 */

#include "transaction.h"
#include "wallet.h"
#include "common.h"
#include "blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// COINBASE ADDRESS CONSTANT
// =============================================================================

// "COINBASE" in first 8 bytes, rest zeros
const uint8_t COINBASE_ADDRESS[20] = {
    'C', 'O', 'I', 'N', 'B', 'A', 'S', 'E',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// =============================================================================
// TRANSACTION CREATION
// =============================================================================

Transaction* transaction_create(const Wallet* wallet,
                                const uint8_t dest_address[20],
                                uint64_t value,
                                uint32_t fee,
                                uint64_t nonce,
                                uint32_t expiry_block) {
    Transaction* tx = safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));

    tx->nonce        = nonce;
    tx->expiry_block = expiry_block;
    memcpy(tx->source_address, wallet->address, 20);
    memcpy(tx->dest_address, dest_address, 20);
    tx->value = value;
    tx->fee   = fee;

    transaction_sign(tx, wallet);
    return tx;
}

// For load generators that have no wallet — signature is zeroed.
Transaction* transaction_create_from_address(const uint8_t source_address[20],
                                             const uint8_t dest_address[20],
                                             uint64_t value, uint32_t fee,
                                             uint64_t nonce, uint32_t expiry_block) {
    Transaction* tx = safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));
    tx->nonce        = nonce;
    tx->expiry_block = expiry_block;
    memcpy(tx->source_address, source_address, 20);
    memcpy(tx->dest_address, dest_address, 20);
    tx->value = value;
    tx->fee   = fee;
    // No wallet: signature stays zero (fails transaction_verify_ed25519)
    return tx;
}

Transaction* transaction_create_coinbase(const uint8_t farmer_address[20],
                                         uint64_t base_reward,
                                         uint64_t total_fees,
                                         uint32_t block_height) {
    Transaction* tx = safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));

    tx->nonce        = (uint64_t)block_height;
    tx->expiry_block = 0;
    memcpy(tx->source_address, COINBASE_ADDRESS, 20);
    memcpy(tx->dest_address, farmer_address, 20);
    tx->value = base_reward + total_fees;
    tx->fee   = 0;
    // Coinbase signature stays zero — verified by TX_IS_COINBASE check

    return tx;
}

// =============================================================================
// TRANSACTION HASH COMPUTATION
// =============================================================================

void transaction_compute_hash(const Transaction* tx, uint8_t hash[TX_HASH_SIZE]) {
    // Covers all fields except signature (signature commits to everything else)
    uint8_t buf[8 + 4 + 20 + 20 + 8 + 4];  // 64 bytes
    size_t off = 0;

    memcpy(buf + off, &tx->nonce,          8);  off += 8;
    memcpy(buf + off, &tx->expiry_block,   4);  off += 4;
    memcpy(buf + off, tx->source_address, 20);  off += 20;
    memcpy(buf + off, tx->dest_address,   20);  off += 20;
    memcpy(buf + off, &tx->value,          8);  off += 8;
    memcpy(buf + off, &tx->fee,            4);  off += 4;

    blake3_hash_truncated(buf, sizeof(buf), hash, TX_HASH_SIZE);
}

char* transaction_get_hash_hex(const Transaction* tx) {
    uint8_t hash[TX_HASH_SIZE];
    transaction_compute_hash(tx, hash);
    return bytes_to_hex(hash, TX_HASH_SIZE);
}

// =============================================================================
// SIGNING AND VERIFICATION
// =============================================================================

bool transaction_sign(Transaction* tx, const Wallet* wallet) {
    if (!tx) return false;

    if (wallet && wallet->evp_key) {
        uint8_t tx_hash[TX_HASH_SIZE];
        transaction_compute_hash(tx, tx_hash);
        return wallet_sign(wallet, tx_hash, TX_HASH_SIZE, tx->signature);
    }

    memset(tx->signature, 0, 64);
    return false;
}

// Basic check: non-zero signature. Full crypto requires transaction_verify_ed25519.
bool transaction_verify(const Transaction* tx) {
    if (!tx) return false;
    if (TX_IS_COINBASE(tx)) return true;
    return !is_zero(tx->signature, 64);
}

// Full Ed25519 verification: checks pubkey→address binding AND signature.
bool transaction_verify_ed25519(const Transaction* tx, const uint8_t pubkey[32]) {
    if (!tx || !pubkey) return false;
    if (TX_IS_COINBASE(tx)) return true;
    if (is_zero(pubkey, 32) || is_zero(tx->signature, 64)) return false;

    // Verify pubkey derives to source_address
    uint8_t derived[20];
    pubkey_to_address(pubkey, derived);
    if (memcmp(derived, tx->source_address, 20) != 0) return false;

    // Verify Ed25519 signature over tx hash
    uint8_t tx_hash[TX_HASH_SIZE];
    transaction_compute_hash(tx, tx_hash);
    return wallet_verify(pubkey, tx_hash, TX_HASH_SIZE, tx->signature);
}

bool transaction_is_expired(const Transaction* tx, uint32_t current_block_height) {
    if (tx->expiry_block == 0) return false;
    return current_block_height > tx->expiry_block;
}

// =============================================================================
// PROTOBUF SERIALIZATION
// =============================================================================

uint8_t* transaction_serialize_pb(const Transaction* tx, size_t* out_len) {
    if (!tx) return NULL;

    Blockchain__Transaction pb_tx = BLOCKCHAIN__TRANSACTION__INIT;

    pb_tx.nonce        = tx->nonce;
    pb_tx.expiry_block = tx->expiry_block;

    pb_tx.source_address.len  = 20;
    pb_tx.source_address.data = (uint8_t*)tx->source_address;

    pb_tx.dest_address.len  = 20;
    pb_tx.dest_address.data = (uint8_t*)tx->dest_address;

    pb_tx.value = tx->value;
    pb_tx.fee   = tx->fee;

    if (!is_zero(tx->signature, 64)) {
        pb_tx.signature.len  = 64;
        pb_tx.signature.data = (uint8_t*)tx->signature;
    }

    size_t size = blockchain__transaction__get_packed_size(&pb_tx);
    uint8_t* buf = safe_malloc(size);
    blockchain__transaction__pack(&pb_tx, buf);

    if (out_len) *out_len = size;
    return buf;
}

Transaction* transaction_deserialize_pb(const uint8_t* data, size_t len) {
    if (!data || len == 0) return NULL;

    Blockchain__Transaction* pb_tx = blockchain__transaction__unpack(NULL, len, data);
    if (!pb_tx) return NULL;

    Transaction* tx = safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));

    tx->nonce        = pb_tx->nonce;
    tx->expiry_block = pb_tx->expiry_block;

    if (pb_tx->source_address.data && pb_tx->source_address.len >= 20)
        memcpy(tx->source_address, pb_tx->source_address.data, 20);

    if (pb_tx->dest_address.data && pb_tx->dest_address.len >= 20)
        memcpy(tx->dest_address, pb_tx->dest_address.data, 20);

    tx->value = pb_tx->value;
    tx->fee   = pb_tx->fee;

    if (pb_tx->signature.data && pb_tx->signature.len >= 64)
        memcpy(tx->signature, pb_tx->signature.data, 64);

    blockchain__transaction__free_unpacked(pb_tx, NULL);
    return tx;
}

// =============================================================================
// LEGACY HEX SERIALIZATION
// =============================================================================

char* transaction_serialize(const Transaction* tx) {
    return bytes_to_hex((const uint8_t*)tx, sizeof(Transaction));
}

Transaction* transaction_deserialize(const char* hex_data) {
    if (strlen(hex_data) != 256) {  // 128 bytes * 2
        return NULL;
    }

    Transaction* tx = safe_malloc(sizeof(Transaction));

    if (!hex_to_bytes_buf(hex_data, (uint8_t*)tx, sizeof(Transaction))) {
        free(tx);
        return NULL;
    }

    return tx;
}

void transaction_destroy(Transaction* tx) {
    if (tx) secure_free(tx, sizeof(Transaction));
}

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

// Ed25519 pubkey (32 bytes) → address: BLAKE3(pubkey)[0:20]
void pubkey_to_address(const uint8_t pubkey[32], uint8_t address[20]) {
    uint8_t tmp[32];
    blake3_hash(pubkey, 32, tmp);
    memcpy(address, tmp, 20);
}
