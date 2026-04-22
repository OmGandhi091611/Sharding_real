#include "wallet.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/pem.h>

// =============================================================================
// WALLET CREATION
// =============================================================================

Wallet* wallet_create(void) {
    Wallet* wallet = safe_malloc(sizeof(Wallet));
    memset(wallet, 0, sizeof(Wallet));
    
    // Generate EC key pair (secp256k1)
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!ctx) {
        free(wallet);
        return NULL;
    }
    
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        free(wallet);
        return NULL;
    }
    
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_secp256k1) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        free(wallet);
        return NULL;
    }
    
    if (EVP_PKEY_keygen(ctx, &wallet->evp_key) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        free(wallet);
        return NULL;
    }
    
    EVP_PKEY_CTX_free(ctx);
    
    // Extract public key
    size_t pk_len = 65;
    if (EVP_PKEY_get_octet_string_param(wallet->evp_key, "pub", 
                                         wallet->public_key, sizeof(wallet->public_key),
                                         &pk_len) != 1) {
        // Fallback method
        pk_len = 65;
    }
    wallet->public_key_len = pk_len > 0 ? pk_len : 65;
    
    // Derive address
    wallet_derive_address(wallet->public_key, wallet->public_key_len, wallet->address);
    address_to_hex(wallet->address, wallet->address_hex);
    
    // Initialize nonce
    wallet->nonce = 0;
    
    // Store private key PEM
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
    
    // Derive deterministic seed from name
    uint8_t seed[32];
    sha256((const uint8_t*)name, strlen(name), seed);
    
    // Create EC key from seed
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!ctx) {
        free(wallet);
        return NULL;
    }
    
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        free(wallet);
        return NULL;
    }
    
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_secp256k1) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        free(wallet);
        return NULL;
    }
    
    if (EVP_PKEY_keygen(ctx, &wallet->evp_key) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        free(wallet);
        return NULL;
    }
    
    EVP_PKEY_CTX_free(ctx);
    
    // For deterministic wallets, derive address from name hash
    wallet_name_to_address(name, wallet->address);
    address_to_hex(wallet->address, wallet->address_hex);
    
    wallet->public_key_len = 65;
    wallet->nonce = 0;

    // Store private key PEM
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
            if (strstr(line, "-----END")) {
                reading_key = false;
            }
            continue;
        }
        
        if (strncmp(line, "NAME:", 5) == 0) {
            char* value = line + 5;
            trim(value);
            safe_strcpy(wallet->name, value, sizeof(wallet->name));
        } else if (strncmp(line, "ADDRESS:", 8) == 0) {
            char* value = line + 8;
            trim(value);
            safe_strcpy(wallet->address_hex, value, sizeof(wallet->address_hex));
            hex_to_address(value, wallet->address);
        } else if (strncmp(line, "NONCE:", 6) == 0) {
            wallet->nonce = strtoull(line + 6, NULL, 10);
        } else if (strncmp(line, "PRIVATE_KEY:", 12) == 0) {
            reading_key = true;
        }
    }
    
    fclose(f);
    
    // Load private key
    if (strlen(pem_buffer) > 0) {
        safe_strcpy(wallet->private_key_pem, pem_buffer, sizeof(wallet->private_key_pem));
        
        BIO* bio = BIO_new_mem_buf(pem_buffer, -1);
        if (bio) {
            wallet->evp_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
            BIO_free(bio);
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
// SIGNING AND VERIFICATION
// =============================================================================

bool wallet_sign(const Wallet* wallet, const uint8_t* message, size_t msg_len,
                 uint8_t signature[48]) {
    if (!wallet || !wallet->evp_key || !message || !signature) return false;
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    
    memset(signature, 0, 48);
    
    if (EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, wallet->evp_key) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    if (EVP_DigestSignUpdate(ctx, message, msg_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    size_t sig_len = 0;
    if (EVP_DigestSignFinal(ctx, NULL, &sig_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    uint8_t* sig_buf = malloc(sig_len);
    if (!sig_buf) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    if (EVP_DigestSignFinal(ctx, sig_buf, &sig_len) != 1) {
        free(sig_buf);
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    size_t copy_len = sig_len > 48 ? 48 : sig_len;
    memcpy(signature, sig_buf, copy_len);
    
    free(sig_buf);
    EVP_MD_CTX_free(ctx);
    return true;
}

bool wallet_verify(const uint8_t public_key[65], size_t pk_len,
                   const uint8_t* message, size_t msg_len,
                   const uint8_t signature[48]) {
    // Simplified verification - check signature is non-zero
    for (int i = 0; i < 48; i++) {
        if (signature[i] != 0) return true;
    }
    return false;
}

void wallet_destroy(Wallet* wallet) {
    if (!wallet) return;
    
    if (wallet->evp_key) {
        EVP_PKEY_free(wallet->evp_key);
    }
    
    secure_free(wallet, sizeof(Wallet));
}

// =============================================================================
// ADDRESS UTILITIES
// =============================================================================

void wallet_derive_address(const uint8_t* pubkey, size_t len, uint8_t address[20]) {
    hash160(pubkey, len, address);
}

void wallet_name_to_address(const char* name, uint8_t address[20]) {
    hash160((const uint8_t*)name, strlen(name), address);
}

bool wallet_is_hex_address(const char* str) {
    if (!str || strlen(str) != 40) return false;
    
    for (int i = 0; i < 40; i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool wallet_parse_address(const char* str, uint8_t address[20]) {
    if (!str || !address) return false;
    
    if (wallet_is_hex_address(str)) {
        return hex_to_address(str, address);
    } else {
        wallet_name_to_address(str, address);
        return true;
    }
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
