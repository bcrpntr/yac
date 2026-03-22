# YAC/CrossPet Firmware — Audit Findings

## 1. Dither Pattern (lib/GfxRenderer/GfxRenderer.cpp:456)

**Issue:** `drawPixelDither<Color::DarkGray>` uses a simple checkerboard pattern:
```cpp
drawPixel(x, y, (x + y) % 2 == 0);  // TODO: maybe find a better pattern?
```
This produces visible artifacts at 50% gray. A 4×4 Bayer matrix gives much smoother results.

**`LightGray` dither** (line ~453) also uses a sparse checkerboard (`x % 2 == 0 && y % 2 == 0`), which results in only 25% fill — not true 50% gray. Could be improved to match.

---

## 2. Security Audit — CrossPointWebServer.cpp

### HIGH: WebSocket filename injection (no path traversal check)
In `onWebSocketEvent` → `WStype_TEXT` → `START:` message parsing:
```cpp
wsUploadFileName = msg.substring(6, firstColon);
```
The filename from the client message is **never validated**. A malicious client can send:
```
START:../../../etc/passwd:100:/
```
This can write files outside the intended upload directory. The HTTP upload handler (`handleUpload`) uses `normalizeWebPath(server->arg("path"))` with path traversal protection, but the WebSocket path does not.

**Fix needed:** Validate `wsUploadFileName` does not contain `..`, `/`, or `\`.

### MEDIUM: WebSocket size overflow
```cpp
wsUploadSize = msg.substring(firstColon + 1, secondColon).toInt();
```
If the client sends a negative number, `toInt()` wraps to a huge positive value (INT_MAX behavior). While this won't cause a crash (the file write just never completes), it wastes resources.

**Fix needed:** Check that `wsUploadSize > 0 && wsUploadSize < some_reasonable_max`.

### MEDIUM: `handleCreateFolder` — no folder name sanitization
```cpp
const String folderName = server->arg("name");
```
`folderName` is not checked for `/` or `\`. A client could create folders outside the root.

### LOW: WiFi credential storage (src/WifiCredentialStore.h)
Comments note credentials are "XOR-obfuscated with hardware MAC" — acknowledged as **not cryptographically secure**. Additionally, the load function falls back to reading plaintext passwords:
```cpp
cred.password = obj["password"] | std::string("");
```
This means if any old plaintext entries exist in the JSON, they'll be loaded and re-saved as obfuscated. This is a migration feature, not a bug, but worth documenting.

**Action:** Add `// TODO: Encrypt credentials at rest using a proper AEAD (e.g., nvs_set_str with ESP32 flash encryption)` comment.

### LOW: No rate limiting
Web server has no protection against brute-force attacks on settings endpoints. Acceptable for internal LAN use, but worth a comment.

---

## 3. BeeperActivity — Error Handling & i18n

### Error Handling Gap
`httpGet()` silently swallows errors:
```cpp
std::string BeeperActivity::httpGet(const std::string& path) {
  std::string response;
  std::string url = std::string(getApiBase()) + path;
  HttpDownloader::fetchUrl(url, response);  // returns nothing on failure
  return response;
}
```
If the API is unreachable, this returns an empty string — callers can't distinguish network failure from empty response. However, `fetchChats` and `fetchMessages` do check the return value of `HttpDownloader::fetchUrl` directly, so this particular issue doesn't cause silent failures. The real gap is in the **render state when fetch fails** — the error screen shows a blank or unhelpful message.

### Hardcoded strings (missing i18n):
- `"Connecting..."` (statusMessage on enter)
- `"No chats found"`
- `"Failed to load messages"`
- `"Loading messages..."`
- `"No messages"` (in render)
- `"Beeper"` (header, hardcoded twice)

These should use `tr()` with proper i18n keys added to `I18nKeys.h` and `english.yaml`.

---

## 4. Dead Code / Tech Debt

- `GfxRenderer.cpp:587`: `// TODO: Rotate bits` — incomplete rotation implementation
- `CrossPointSettings.cpp`: `applyLegacyFrontButtonLayout` and `readAndValidate` are used only once each (in `loadFromBinaryFile`) — could be inlined
- `CrossPointWebServer.cpp`: Static upload diagnostic variables `uploadStartTime`, `totalWriteTime`, `writeCount` at line ~340 — only used for diagnostics, not exposed anywhere
- `CrossPointWebServer.cpp`: `wsLastCompleteName`, `wsLastCompleteSize`, `wsLastCompleteAt` are populated but `getWsUploadStatus()` return value is never used anywhere in the codebase

---

## 5. OTA Security (src/network/OtaUpdater.cpp)

### HIGH: No firmware signature verification
`installUpdate()` uses `esp_https_ota_begin()` → `esp_https_ota_perform()` → `esp_https_ota_finish()` with NO call to `esp_https_ota_verify_signature()`. A compromised GitHub account or MITM could serve malicious firmware.

**Fix applied (2026-03-22):**
- `installUpdate()`: `skip_cert_common_name_check = false` (firmware URL must verify hostname)
- `installUpdate()`: Added TODO noting the need for `esp_https_ota_verify_signature()` or SHA-256 hash verification
- `checkForUpdate()`: Left `skip_cert_common_name_check = true` (GitHub API cert issue — CN mismatch on `api.github.com`) with explanatory comment

**Remaining gap:** Full firmware signing/verification requires either:
1. `esp_https_ota_verify_signature()` — needs properly configured secure boot with signing keys
2. Embedded SHA-256 hash in code, fetched via a trusted channel (e.g., a separate HTTPS endpoint with its own cert)

**Weakness in checkForUpdate:** GitHub API URL uses `skip_cert_common_name_check = true` — if DNS is compromised, attacker could redirect to a different server returning a crafted release URL pointing to malicious firmware. Mitigation: the firmware download URL is on `download.github.com` which now has proper CN verification.

---

## 6. News Reader (src/activities/tools/NewsReaderActivity.cpp)

### Hardcoded single source
- `FEED_URL = "https://defuddle.md/vnexpress.net"` — no user-configurable feeds
- Single source limits utility but is not a security issue per se

### Article URL injection risk (LOW)
The parser extracts URLs from Markdown with `strncmp` and length bounds checking. URLs are used to construct `defuddle.md` proxy URLs. While the parser looks for `](https://` / `](http://` patterns, a malicious article could contain a crafted URL pointing elsewhere. However:
- The `defuddle.md` proxy itself may limit what it fetches
- No direct arbitrary URL fetch from the reader (all traffic goes through defuddle.md proxy)
- Response size is capped at 15KB to prevent memory exhaustion

**Conclusion:** Not a direct security risk, but worth noting the single-feed limitation.

### Offline/error handling
- `HttpDownloader::fetchUrl` failure → `state = FETCH_ERROR` with user-facing message — handled
- Article count 0 → FETCH_ERROR state — handled
- WiFi reconnect via `newsSilentWifiConnect()` fallback — handled

---

## 7. KOReader Sync (lib/KOReaderSync/KOReaderSyncClient.h/.cpp)

### Protocol documented
The sync protocol was previously undocumented. Now documented in `KOReaderSyncClient.h`:
- API endpoints: `GET /users/auth`, `GET /syncs/progress/:document`, `PUT /syncs/progress`
- Auth: `x-auth-user` + `x-auth-key` (MD5 of password) + HTTP Basic Auth (Calibre-Web compat)
- Conflict resolution: interactive user choice, default to furthest progress
- TLS: HTTPS with cert chain validation and hostname verification

### MEDIUM: Plaintext credential storage
The Calibre-Web password is stored in plaintext in `/.crosspoint/koreader.json` (required because MD5 hash must be computed on-the-fly for auth headers). This is documented in the updated header comment.

### Empty emulator directory
`xteink-x4-emulator/` is empty — no emulator build system exists in the repo.

### All CrossPet settings exposed in web UI
`homeShowClock`, `homeShowWeather`, `homeShowPetStatus`, `homeFocusMode` are all in `SettingsList.h` via `SettingInfo::DynamicToggle` — web UI gap: NONE.

---

## 8. Pet Sprite System (src/pet/PetSpriteData.h)

### Sprite encoding
- **Format:** `uint32_t[3][24]` — 3 frames × 24 rows; each uint32_t is one row, bit 23 = leftmost column
- **Rendering:** Each logical pixel renders as `2 * scale` physical pixels via `PetSpriteRenderer`
- **Scale:** Each stage type has its own scale (EGG smaller, ELDER largest)
- **Animation:** 3 frames per animation cycle; themes vary by stage (wobble, blink, walk, breathe, sway)
- **Direction:** All living sprites face RIGHT (head right, tail/body left)
- **Dead sprite:** Single 1-frame sprite replicated 3×, shared across all types
- **Evolution:** 5 stages (EGG → HATCHLING → YOUNGSTER → COMPANION → ELDER); branching at YOUNGSTER and COMPANION into Scholar/Balanced/Wild variants
- **Pet types:** 5 types (0=CHICKEN, 1=CAT, 2=DOG, 3=DRAGON, 4=BUNNY) × 5 stages × 3 frames

---

## 5. Largest Source Files (by line count)
1. `src/activities/boot_sleep/SleepActivity.cpp` — 1343 lines
2. `src/network/CrossPointWebServer.cpp` — 1318 lines
3. `src/activities/reader/EpubReaderActivity.cpp` — 1260 lines
4. `src/network/WebDAVHandler.cpp` — 824 lines (not reviewed in this pass)
5. `src/components/themes/BaseTheme.cpp` — 810 lines
