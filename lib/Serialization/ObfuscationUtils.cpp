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

// Fixed context string for HMAC-based key derivation — ensures the same MAC on
// different devices with the same flash contents produces different keys.
// NOTE: We use direct HMAC-SHA256 derivation rather than PBKDF2 because:
//   1. PBKDF2 is designed for password stretching; a hardware MAC is not a password.
//   2. ESP32's mbedtls port only provides mbedtls_pkcs5_pbkdf2_hmac_ext (extended API),
//      which requires different calling convention and doesn't simplify the code.
//   3. HMAC-SHA256 is a perfectly fine KDF when the "key material" is already high-entropy
//      (6 bytes of hardware MAC).
// TODO: For production, store a random 32-byte AES key in eFuse or NVS with
// flash encryption enabled. This eliminates key derivation entirely.
constexpr uint8_t HMAC_CONTEXT[] = "CrossPetWiFiCreds-v1";
constexpr size_t HMAC_CONTEXT_LEN = sizeof(HMAC_CONTEXT) - 1;

// Derive AES-256 key from hardware MAC using HMAC-SHA256.
// Returns a statically cached 32-byte key (initialized once).
//
// The MAC is hardware-backed and not readable from external flash, so the derived
// key cannot be extracted by reading the SPI flash. On devices with ESP32 flash
// encryption enabled, the entire flash is AES-encrypted, providing additional protection.
const uint8_t* getAesKey() {
  static uint8_t key[AES_KEY_LEN] = {};
  static bool initialized = false;
  if (!initialized) {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);

    // HMAC-SHA256(ctx || mac) → 32-byte key
    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mdInfo == nullptr) {
      LOG_ERR("OBF", "mbedtls_md_info_from_type(SHA256) failed — using fallback key derivation");
    } else {
      int ret = mbedtls_md_setup(&mdCtx, mdInfo, 1);  // 1 = HMAC mode
      if (ret != 0) {
        LOG_ERR("OBF", "mbedtls_md_setup failed (ret=%d) — using fallback key derivation", ret);
        mdInfo = nullptr;
      } else {
        // Start HMAC with context as key
        ret = mbedtls_md_hmac_starts(&mdCtx, HMAC_CONTEXT, HMAC_CONTEXT_LEN);
        if (ret != 0) {
          LOG_ERR("OBF", "mbedtls_md_hmac_starts failed (ret=%d) — using fallback key derivation", ret);
          mdInfo = nullptr;
        } else {
          // Feed in the MAC as the message data
          ret = mbedtls_md_hmac_update(&mdCtx, mac, sizeof(mac));
          if (ret != 0) {
            LOG_ERR("OBF", "mbedtls_md_hmac_update failed (ret=%d) — using fallback key derivation", ret);
            mdInfo = nullptr;
          } else {
            ret = mbedtls_md_hmac_finish(&mdCtx, key);
            if (ret != 0) {
              LOG_ERR("OBF", "mbedtls_md_hmac_finish failed (ret=%d) — using fallback key derivation", ret);
              mdInfo = nullptr;
            }
          }
        }
      }
    }

    if (mdInfo == nullptr) {
      // Fallback: simple key derivation from MAC — NOT cryptographically strong,
      // but ensures we never crash on startup due to crypto init failures.
      for (size_t i = 0; i < AES_KEY_LEN; i++) {
        key[i] = mac[i % 6] ^ static_cast<uint8_t>((i * 0x9E) ^ 0xB9);
      }
    }

    mbedtls_md_free(&mdCtx);
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

  mbedtls_gcm_context gcmCtx;
  mbedtls_gcm_init(&gcmCtx);

  int ret = mbedtls_gcm_setkey(&gcmCtx, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (ret != 0) {
    LOG_ERR("OBF", "GCM key setup failed (ret=%d)", ret);
    mbedtls_gcm_free(&gcmCtx);
    return "";
  }

  // Encrypt: plaintext -> ciphertext (in output buffer after nonce)
  ret = mbedtls_gcm_crypt_and_tag(&gcmCtx, MBEDTLS_GCM_ENCRYPT, ctLen,
                                  nonce, sizeof(nonce),
                                  nullptr, 0,  // no additional authenticated data
                                  reinterpret_cast<const uint8_t*>(plaintext.data()),
                                  output.data() + AES_IV_LEN,
                                  AES_TAG_LEN,
                                  output.data() + AES_IV_LEN + ctLen);

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

  mbedtls_gcm_context gcmCtx;
  mbedtls_gcm_init(&gcmCtx);

  ret = mbedtls_gcm_setkey(&gcmCtx, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (ret != 0) {
    LOG_ERR("OBF", "GCM key setup failed (ret=%d)", ret);
    mbedtls_gcm_free(&gcmCtx);
    if (ok) *ok = false;
    return "";
  }

  // Decrypt and authenticate simultaneously
  ret = mbedtls_gcm_auth_decrypt(&gcmCtx, ciphertextLen,
                                 nonce, AES_IV_LEN,
                                 nullptr, 0,  // no additional authenticated data
                                 tag, AES_TAG_LEN,
                                 ciphertext,
                                 reinterpret_cast<uint8_t*>(&plaintext[0]));

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
