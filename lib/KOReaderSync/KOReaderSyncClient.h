#pragma once
#include <string>

/**
 * Progress data from KOReader sync server.
 */
struct KOReaderProgress {
  std::string document;  // Document hash
  std::string progress;  // XPath-like progress string
  float percentage;      // Progress percentage (0.0 to 1.0)
  std::string device;    // Device name
  std::string deviceId;  // Device ID
  int64_t timestamp;     // Unix timestamp of last update
};

/**
 * HTTP client for KOReader sync API.
 *
 * Base URL: https://sync.koreader.rocks:443/
 *
 * API Endpoints:
 *   GET /users/auth - Authenticate (validate credentials)
 *   GET /syncs/progress/:document - Get progress for a document
 *   PUT /syncs/progress - Update progress for a document
 *
 * Authentication:
 *   x-auth-user: username
 *   x-auth-key: MD5 hash of password (computed on-the-fly from stored plaintext)
 *
 * Security notes:
 *   - Credentials are stored in plaintext in /.crosspoint/koreader.json
 *     (required because MD5 hash must be computed on-the-fly for x-auth-key header).
 *     Treat the device storage as sensitive — an attacker with file access can
 *     read the Calibre-Web credentials directly.
 *   - HTTP Basic Auth header is also sent for Calibre-Web compatibility,
 *     using base64(username:password) — also plaintext-equivalent.
 *   - MD5 for auth is used because that's what the KOReader sync server expects;
 *     it is NOT used for password storage or any cryptographic operation beyond
 *     a simple authentication token.
 *   - All communication uses HTTPS (TLS). The ESP32 crt_bundle validates
 *     the server certificate chain. Hostname verification is performed.
 *
 * Conflict resolution:
 *   - After fetching remote progress, the user is shown both local and remote
 *     positions and can choose which to apply (interactive resolution).
 *   - No automatic merge is performed — the user explicitly picks local or remote.
 *   - On conflict, the device with the furthest progress is pre-selected as default.
 */
class KOReaderSyncClient {
 public:
  enum Error { OK = 0, NO_CREDENTIALS, NETWORK_ERROR, AUTH_FAILED, SERVER_ERROR, JSON_ERROR, NOT_FOUND };

  // Last HTTP response code (for debugging network errors)
  static int lastHttpCode;

  /**
   * Authenticate with the sync server (validate credentials).
   * @return OK on success, error code on failure
   */
  static Error authenticate();

  /**
   * Get reading progress for a document.
   * @param documentHash The document hash (from KOReaderDocumentId)
   * @param outProgress Output: the progress data
   * @return OK on success, NOT_FOUND if no progress exists, error code on failure
   */
  static Error getProgress(const std::string& documentHash, KOReaderProgress& outProgress);

  /**
   * Update reading progress for a document.
   * @param progress The progress data to upload
   * @return OK on success, error code on failure
   */
  static Error updateProgress(const KOReaderProgress& progress);

  /**
   * Get human-readable error message.
   */
  static const char* errorString(Error error);
};
