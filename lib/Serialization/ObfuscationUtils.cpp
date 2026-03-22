#include "ObfuscationUtils.h"

#include <Logging.h>
#include <base64.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/cipher.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include <cstring>
#include <vector>

namespace obfuscation {

namespace {
constexpr size_t HW_KEY_LEN = 6;

// Simple lazy init — no thread-safety concern on single-core ESP32-C3.
const uint8_t* getHwKey() {
  static uint8_t key[HW_KEY_LEN] = {};
  static bool initialized = false;
  if (!initialized) {
    esp_efuse_mac_get_default(key);
    initialized = true;
  }
  return key;
}
}  // namespace

void xorTransform(std::string& data) {
  const uint8_t* key = getHwKey();
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= key[i % HW_KEY_LEN];
  }
}

void xorTransform(std::string& data, const uint8_t* key, size_t keyLen) {
  if (keyLen == 0 || key == nullptr) return;
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= key[i % keyLen];
  }
}

String obfuscateToBase64(const std::string& plaintext) {
  if (plaintext.empty()) return "";
  std::string temp = plaintext;
  xorTransform(temp);
  return base64::encode(reinterpret_cast<const uint8_t*>(temp.data()), temp.size());
}

std::string deobfuscateFromBase64(const char* encoded, bool* ok) {
  if (encoded == nullptr || encoded[0] == '\0') {
    if (ok) *ok = false;
    return "";
  }
  if (ok) *ok = true;
  size_t encodedLen = strlen(encoded);
  // First call: get required output buffer size
  size_t decodedLen = 0;
  int ret = mbedtls_base64_decode(nullptr, 0, &decodedLen, reinterpret_cast<const unsigned char*>(encoded), encodedLen);
  if (ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
    LOG_ERR("OBF", "Base64 decode size query failed (ret=%d)", ret);
    if (ok) *ok = false;
    return "";
  }
  std::string result(decodedLen, '\0');
  ret = mbedtls_base64_decode(reinterpret_cast<unsigned char*>(&result[0]), decodedLen, &decodedLen,
                              reinterpret_cast<const unsigned char*>(encoded), encodedLen);
  if (ret != 0) {
    LOG_ERR("OBF", "Base64 decode failed (ret=%d)", ret);
    if (ok) *ok = false;
    return "";
  }
  result.resize(decodedLen);
  xorTransform(result);
  return result;
}

namespace {
// AES-256-GCM parameters
constexpr size_t AES_KEY_LEN = 32;   // 256 bits
constexpr size_t AES_IV_LEN = 12;    // 96 bits (recommended for GCM)
constexpr size_t AES_TAG_LEN = 16;   // 128 bits (standard GCM tag)
constexpr int PBKDF2_ITERATIONS = 50000;  // OWASP-recommended minimum for SHA256

// Fixed salt for PBKDF2 key derivation — not secret, ensures the same MAC on
// different devices with the same flash contents still produces different keys.
// TODO: Replace with per-device key stored in eFuse or NVS with flash encryption.
// Production: store a random 32-byte key in eFuse; read it here instead of deriving.
constexpr uint8_t PBKDF2_SALT[] = {
    0x43, 0x72, 0x6F, 0x73, 0x73, 0x50, 0x6F, 0x69,
    0x6E, 0x74, 0x57, 0x69, 0x46, 0x69, 0x43, 0x72,
    0x65, 0x64, 0x73, 0x44, 0x65, 0x76, 0x56, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // last 8 bytes = zero, updated with MAC
};
constexpr size_t PBKDF2_SALT_LEN = sizeof(PBKDF2_SALT);

// Derive AES-256 key from hardware MAC using PBKDF2-HMAC-SHA256.
// Returns a statically cached 32-byte key (initialized once).
//
// NOTE: PBKDF2 is used here because it is the standard (RFC 8018) password-based
// key derivation function. It slows down brute-force attacks by requiring many
// iterations. With a 6-byte MAC as the "password", PBKDF2 doesn't add much
// strength — but it is the right primitive if we ever migrate to a real passphrase.
// The real security comes from the MAC being hardware-backed and not readable from
// external flash.
//
// TODO: For production, store a random 32-byte AES key in eFuse or NVS with
// flash encryption enabled. This eliminates the need for key derivation entirely
// and provides true key isolation from software attacks against the flash.
const uint8_t* getAesKey() {
  static uint8_t key[AES_KEY_LEN] = {};
  static bool initialized = false;
  if (!initialized) {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);

    // Build salt: fixed prefix + MAC in last 8 bytes
    uint8_t salt[PBKDF2_SALT_LEN] = {};
    memcpy(salt, PBKDF2_SALT, PBKDF2_SALT_LEN);
    memcpy(salt + PBKDF2_SALT_LEN - 8, mac, 6);

    // Use mbedtls HMAC-SHA256 as the PRF inside PBKDF2
    mbedtls_md_context_t mdCtx;
    mbedtls_md_context_t* pCtx = &mdCtx;
    mbedtls_md_init(pCtx);
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mdInfo == nullptr) {
      LOG_ERR("OBF", "mbedtls_md_info_from_type(SHA256) failed — using fallback key derivation");
    } else {
      int ret = mbedtls_md_setup(pCtx, mdInfo, 1);  // 1 = HMAC mode
      if (ret != 0) {
        LOG_ERR("OBF", "mbedtls_md_setup failed (ret=%d) — using fallback key derivation", ret);
        mdInfo = nullptr;
      } else {
        // Use MAC as HMAC key (PBKDF2 "password")
        ret = mbedtls_md_hmac_starts(pCtx, mac, sizeof(mac));
        if (ret != 0) {
          LOG_ERR("OBF", "mbedtls_md_hmac_starts failed (ret=%d) — using fallback key derivation", ret);
          mdInfo = nullptr;
        }
      }
    }

    if (mdInfo != nullptr) {
      int ret = mbedtls_pkcs5_pbkdf2_hmac(pCtx, mac, sizeof(mac), salt, PBKDF2_SALT_LEN,
                                          PBKDF2_ITERATIONS, AES_KEY_LEN, key);
      if (ret != 0) {
        LOG_ERR("OBF", "PBKDF2 key derivation failed (ret=%d) — using fallback", ret);
        mdInfo = nullptr;
      }
    }

    if (mdInfo == nullptr) {
      // Fallback: simple key derivation from MAC — NOT cryptographically strong,
      // but ensures we never crash on startup due to crypto init failures.
      for (size_t i = 0; i < AES_KEY_LEN; i++) {
        key[i] = mac[i % 6] ^ static_cast<uint8_t>((i * 0x9E) ^ 0xB9);
      }
    }

    mbedtls_md_free(pCtx);
    initialized = true;
  }
  return key;
}

}  // namespace

String aes256GcmEncrypt(const std::string& plaintext) {
  if (plaintext.empty()) return "";

  const uint8_t* key = getAesKey();

  // Generate random 12-byte nonce using ESP32 hardware RNG
  uint8_t nonce[12] = {};
  esp_fill_random(nonce, sizeof(nonce));

  // Buffer: nonce (12) || ciphertext || tag (16)
  const size_t ctLen = plaintext.size();
  std::vector<uint8_t> output(AES_IV_LEN + ctLen + AES_TAG_LEN);
  memcpy(output.data(), nonce, AES_IV_LEN);

  mbedtls_aes_context aesCtx;
  mbedtls_gcm_context gcmCtx;

  mbedtls_aes_init(&aesCtx);
  mbedtls_gcm_init(&gcmCtx);

  int ret = mbedtls_aes_setkey_enc(&aesCtx, key, 256);
  if (ret != 0) {
    LOG_ERR("OBF", "AES key setup failed (ret=%d)", ret);
    mbedtls_aes_free(&aesCtx);
    mbedtls_gcm_free(&gcmCtx);
    return "";
  }

  // Encrypt: plaintext -> ciphertext (in output buffer after nonce)
  // Authenticate: ciphertext (but output buffer layout is nonce|ct|tag, so pass nullptr for auth data initially)
  ret = mbedtls_gcm_crypt_and_tag(&gcmCtx, MBEDTLS_GCM_ENCRYPT, ctLen,
                                  nonce, sizeof(nonce),
                                  nullptr, 0,  // no additional authenticated data
                                  reinterpret_cast<const uint8_t*>(plaintext.data()),
                                  output.data() + AES_IV_LEN,
                                  AES_TAG_LEN,
                                  output.data() + AES_IV_LEN + ctLen);

  mbedtls_aes_free(&aesCtx);
  mbedtls_gcm_free(&gcmCtx);

  if (ret != 0) {
    LOG_ERR("OBF", "AES-GCM encrypt failed (ret=%d)", ret);
    return "";
  }

  // Base64 encode the combined nonce||ciphertext||tag blob
  return base64::encode(output.data(), output.size());
}

std::string aes256GcmDecrypt(const char* encoded, bool* ok) {
  if (encoded == nullptr || encoded[0] == '\0') {
    if (ok) *ok = false;
    return "";
  }

  size_t encodedLen = strlen(encoded);

  // Decode base64
  size_t decodedLen = 0;
  int ret = mbedtls_base64_decode(nullptr, 0, &decodedLen,
                                  reinterpret_cast<const unsigned char*>(encoded), encodedLen);
  if (ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
    LOG_ERR("OBF", "Base64 decode size query failed (ret=%d)", ret);
    if (ok) *ok = false;
    return "";
  }

  std::vector<uint8_t> combined(decodedLen);
  ret = mbedtls_base64_decode(combined.data(), decodedLen, &decodedLen,
                              reinterpret_cast<const unsigned char*>(encoded), encodedLen);
  if (ret != 0) {
    LOG_ERR("OBF", "Base64 decode failed (ret=%d)", ret);
    if (ok) *ok = false;
    return "";
  }

  // Require at least nonce (12) + tag (16) + 1 byte ciphertext
  if (decodedLen < AES_IV_LEN + AES_TAG_LEN + 1) {
    LOG_ERR("OBF", "Decoded data too short for AES-GCM (%zu bytes)", decodedLen);
    if (ok) *ok = false;
    return "";
  }

  size_t ciphertextLen = decodedLen - AES_IV_LEN - AES_TAG_LEN;
  const uint8_t* nonce = combined.data();
  const uint8_t* ciphertext = combined.data() + AES_IV_LEN;
  const uint8_t* tag = combined.data() + AES_IV_LEN + ciphertextLen;

  std::string plaintext(ciphertextLen, '\0');

  const uint8_t* key = getAesKey();

  mbedtls_aes_context aesCtx;
  mbedtls_gcm_context gcmCtx;

  mbedtls_aes_init(&aesCtx);
  mbedtls_gcm_init(&gcmCtx);

  ret = mbedtls_aes_setkey_enc(&aesCtx, key, 256);
  if (ret != 0) {
    LOG_ERR("OBF", "AES key setup failed (ret=%d)", ret);
    mbedtls_aes_free(&aesCtx);
    mbedtls_gcm_free(&gcmCtx);
    if (ok) *ok = false;
    return "";
  }

  // Decrypt and authenticate simultaneously
  ret = mbedtls_gcm_auth_decrypt(&aesCtx, ciphertextLen,
                                 nonce, AES_IV_LEN,
                                 nullptr, 0,  // no additional data
                                 tag, AES_TAG_LEN,
                                 ciphertext,
                                 reinterpret_cast<uint8_t*>(&plaintext[0]));

  mbedtls_aes_free(&aesCtx);
  mbedtls_gcm_free(&gcmCtx);

  if (ret != 0) {
    LOG_ERR("OBF", "AES-GCM auth decrypt failed (ret=%d) — wrong key or tampered data", ret);
    if (ok) *ok = false;
    return "";
  }

  if (ok) *ok = true;
  return plaintext;
}

void selfTest() {
  const char* testInputs[] = {"", "hello", "WiFi P@ssw0rd!", "a"};
  bool allPassed = true;
  for (const char* input : testInputs) {
    String encoded = obfuscateToBase64(std::string(input));
    std::string decoded = deobfuscateFromBase64(encoded.c_str());
    if (decoded != input) {
      LOG_ERR("OBF", "FAIL: \"%s\" -> \"%s\" -> \"%s\"", input, encoded.c_str(), decoded.c_str());
      allPassed = false;
    }
  }
  // Verify obfuscated form differs from plaintext
  String enc = obfuscateToBase64("test123");
  if (enc == "test123") {
    LOG_ERR("OBF", "FAIL: obfuscated output identical to plaintext");
    allPassed = false;
  }
  if (allPassed) {
    LOG_DBG("OBF", "Obfuscation self-test PASSED");
  }
}

}  // namespace obfuscation
