#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "wallet.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

// =============================================================================
// WALLET CREATION
// =============================================================================

Wallet* wallet_create(void) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!ctx) { free(wallet); return NULL; }
    if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); free(wallet); return NULL; }
    if (EVP_PKEY_keygen(ctx, &wallet->evp_key) <= 0) { EVP_PKEY_CTX_free(ctx); free(wallet); return NULL; }
    EVP_PKEY_CTX_free(ctx);

    size_t pk_len = 32;
    if (EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->public_key, &pk_len) != 1) {
        EVP_PKEY_free(wallet->evp_key); free(wallet); return NULL;
    }

    wallet_derive_address(wallet->public_key, wallet->address);
    address_to_hex(wallet->address, wallet->address_hex);
    wallet->nonce = 0;

    BIO* bio = BIO_new(BIO_s_mem());
    if (bio) {
        PEM_write_bio_PrivateKey(bio, wallet->evp_key, NULL, NULL, 0, NULL, NULL);
        int pem_len = BIO_read(bio, wallet->private_key_pem, sizeof(wallet->private_key_pem) - 1);
        if (pem_len > 0) wallet->private_key_pem[pem_len] = '\0';
        BIO_free(bio);
    }

    return wallet;
}

Wallet* wallet_create_named(const char* name) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));

    safe_strcpy(wallet->name, name, sizeof(wallet->name));

    // Deterministic 32-byte seed from name via BLAKE3
    uint8_t seed[32];
    blake3_hash((const uint8_t*)name, strlen(name), seed);

    // Ed25519 private key from seed — fully deterministic
    wallet->evp_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!wallet->evp_key) { free(wallet); return NULL; }

    size_t pk_len = 32;
    if (EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->public_key, &pk_len) != 1) {
        EVP_PKEY_free(wallet->evp_key); free(wallet); return NULL;
    }

    wallet_derive_address(wallet->public_key, wallet->address);
    address_to_hex(wallet->address, wallet->address_hex);
    wallet->nonce = 0;

    BIO* bio = BIO_new(BIO_s_mem());
    if (bio) {
        PEM_write_bio_PrivateKey(bio, wallet->evp_key, NULL, NULL, 0, NULL, NULL);
        int pem_len = BIO_read(bio, wallet->private_key_pem, sizeof(wallet->private_key_pem) - 1);
        if (pem_len > 0) wallet->private_key_pem[pem_len] = '\0';
        BIO_free(bio);
    }

    return wallet;
}

// =============================================================================
// WALLET PERSISTENCE
// =============================================================================

bool wallet_save(const Wallet* wallet, const char* filepath) {
    if (!wallet || !filepath) return false;

    FILE* f = fopen(filepath, "w");
    if (!f) return false;

    fprintf(f, "NAME:%s\n", wallet->name);
    fprintf(f, "ADDRESS:%s\n", wallet->address_hex);
    fprintf(f, "NONCE:%lu\n", wallet->nonce);
    fprintf(f, "PRIVATE_KEY:\n%s", wallet->private_key_pem);

    fclose(f);
    return true;
}

Wallet* wallet_load(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return NULL;

    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));

    char line[2048];
    bool reading_key = false;
    char pem_buffer[2048] = {0};

    while (fgets(line, sizeof(line), f)) {
        if (reading_key) {
            strcat(pem_buffer, line);
            if (strstr(line, "-----END")) reading_key = false;
            continue;
        }
        if (strncmp(line, "NAME:", 5) == 0) {
            char* v = line + 5; trim(v);
            safe_strcpy(wallet->name, v, sizeof(wallet->name));
        } else if (strncmp(line, "ADDRESS:", 8) == 0) {
            char* v = line + 8; trim(v);
            safe_strcpy(wallet->address_hex, v, sizeof(wallet->address_hex));
            hex_to_address(v, wallet->address);
        } else if (strncmp(line, "NONCE:", 6) == 0) {
            wallet->nonce = strtoull(line + 6, NULL, 10);
        } else if (strncmp(line, "PRIVATE_KEY:", 12) == 0) {
            reading_key = true;
        }
    }
    fclose(f);

    if (strlen(pem_buffer) > 0) {
        safe_strcpy(wallet->private_key_pem, pem_buffer, sizeof(wallet->private_key_pem));
        BIO* bio = BIO_new_mem_buf(pem_buffer, -1);
        if (bio) {
            wallet->evp_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
            BIO_free(bio);
        }
        if (wallet->evp_key) {
            size_t pk_len = 32;
            EVP_PKEY_get_raw_public_key(wallet->evp_key, wallet->public_key, &pk_len);
        }
    }

    return wallet;
}

// =============================================================================
// WALLET ACCESSORS
// =============================================================================

const uint8_t* wallet_get_address(const Wallet* wallet) {
    return wallet ? wallet->address : NULL;
}

const char* wallet_get_address_hex(const Wallet* wallet) {
    return wallet ? wallet->address_hex : NULL;
}

uint64_t wallet_get_next_nonce(Wallet* wallet) {
    if (!wallet) return 0;
    return wallet->nonce++;
}

uint64_t wallet_get_nonce(const Wallet* wallet) {
    return wallet ? wallet->nonce : 0;
}

void wallet_set_nonce(Wallet* wallet, uint64_t nonce) {
    if (wallet) wallet->nonce = nonce;
}

// =============================================================================
// SIGNING AND VERIFICATION (Ed25519 via OpenSSL EVP)
// =============================================================================

bool wallet_sign(const Wallet* wallet, const uint8_t* message, size_t msg_len,
                 uint8_t signature[64]) {
    if (!wallet || !wallet->evp_key || !message || !signature) return false;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) return false;

    if (EVP_DigestSignInit(mdctx, NULL, NULL, NULL, wallet->evp_key) != 1) {
        EVP_MD_CTX_free(mdctx);
        return false;
    }

    size_t siglen = 64;
    bool ok = (EVP_DigestSign(mdctx, signature, &siglen, message, msg_len) == 1
               && siglen == 64);
    EVP_MD_CTX_free(mdctx);
    return ok;
}

bool wallet_verify(const uint8_t public_key[32],
                   const uint8_t* message, size_t msg_len,
                   const uint8_t signature[64]) {
    if (!public_key || !message || !signature) return false;

    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, public_key, 32);
    if (!pkey) return false;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) { EVP_PKEY_free(pkey); return false; }

    bool ok = false;
    if (EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pkey) == 1)
        ok = (EVP_DigestVerify(mdctx, signature, 64, message, msg_len) == 1);

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return ok;
}

void wallet_destroy(Wallet* wallet) {
    if (!wallet) return;
    if (wallet->evp_key) EVP_PKEY_free(wallet->evp_key);
    secure_free(wallet, sizeof(Wallet));
}

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

void wallet_derive_address(const uint8_t pubkey[32], uint8_t address[20]) {
    uint8_t tmp[32];
    blake3_hash(pubkey, 32, tmp);
    memcpy(address, tmp, 20);
}

// Derives the deterministic address a named wallet would have.
// Must match wallet_create_named: BLAKE3(name) → Ed25519 privkey → pubkey → BLAKE3(pubkey)[0:20]
void wallet_name_to_address(const char* name, uint8_t address[20]) {
    uint8_t seed[32];
    blake3_hash((const uint8_t*)name, strlen(name), seed);

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!pkey) { memcpy(address, seed, 20); return; }

    uint8_t pubkey[32];
    size_t pk_len = 32;
    if (EVP_PKEY_get_raw_public_key(pkey, pubkey, &pk_len) != 1) {
        EVP_PKEY_free(pkey); memcpy(address, seed, 20); return;
    }
    EVP_PKEY_free(pkey);
    wallet_derive_address(pubkey, address);
}

bool wallet_is_hex_address(const char* str) {
    if (!str || strlen(str) != 40) return false;
    for (int i = 0; i < 40; i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

bool wallet_parse_address(const char* str, uint8_t address[20]) {
    if (!str || !address) return false;
    if (wallet_is_hex_address(str)) return hex_to_address(str, address);
    wallet_name_to_address(str, address);
    return true;
}

void address_to_hex(const uint8_t address[20], char hex[41]) {
    bytes_to_hex_buf(address, 20, hex);
}

bool hex_to_address(const char* hex, uint8_t address[20]) {
    if (!hex || strlen(hex) != 40) return false;
    return hex_to_bytes_buf(hex, address, 20);
}

void txhash_to_hex(const uint8_t hash[28], char hex[57]) {
    bytes_to_hex_buf(hash, 28, hex);
}

bool address_is_valid(const uint8_t address[20]) {
    return !is_zero(address, 20);
}

bool address_equals(const uint8_t a[20], const uint8_t b[20]) {
    return memcmp(a, b, 20) == 0;
}
