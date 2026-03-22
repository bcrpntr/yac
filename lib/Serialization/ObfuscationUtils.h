#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Credential obfuscation utilities using the ESP32's unique hardware MAC address.
 *
 * Primary encryption: AES-256-GCM with hardware-derived key. Provides authenticated
 * encryption (confidentiality + integrity), far superior to the legacy XOR scheme.
 *
 * Secondary (legacy): XOR obfuscation with 6-byte eFuse MAC key for binary file
 * migration only. Not used for JSON storage.
 *
 * Key derivation: per-device key is derived from the hardware MAC via PBKDF2-HMAC-SHA256
 * with a fixed salt. On production devices this should be replaced with a key stored in
 * eFuse or ESP32 NVS with flash encryption enabled.
 */
namespace obfuscation {

// XOR obfuscate/deobfuscate in-place using hardware MAC key (symmetric operation)
void xorTransform(std::string& data);

// Legacy overload for binary migration (uses the old per-store hardcoded keys)
void xorTransform(std::string& data, const uint8_t* key, size_t keyLen);

// Obfuscate a plaintext string: XOR with hardware key, then base64-encode for JSON storage
String obfuscateToBase64(const std::string& plaintext);

// Decode base64 and de-obfuscate back to plaintext.
// Returns empty string on invalid base64 input; sets *ok to false if decode fails.
std::string deobfuscateFromBase64(const char* encoded, bool* ok = nullptr);

// AES-256-GCM: Encrypt plaintext. Key is derived from hardware MAC.
// Output format: base64(nonce || ciphertext || tag). Returns empty string on failure.
String aes256GcmEncrypt(const std::string& plaintext);

// AES-256-GCM: Decrypt ciphertext produced by aes256GcmEncrypt.
// Returns empty string on decryption failure (bad key, corrupted data, or authentication failure).
// Sets *ok to false if decode or auth fails; leaves *ok unchanged on other errors.
std::string aes256GcmDecrypt(const char* encoded, bool* ok = nullptr);

// Self-test: verifies round-trip obfuscation with hardware key. Logs PASS/FAIL.
void selfTest();

}  // namespace obfuscation
