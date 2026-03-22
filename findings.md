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

## 5. Largest Source Files (by line count)
1. `src/activities/boot_sleep/SleepActivity.cpp` — 1343 lines
2. `src/network/CrossPointWebServer.cpp` — 1318 lines (audited above)
3. `src/activities/reader/EpubReaderActivity.cpp` — 1260 lines
4. `src/network/WebDAVHandler.cpp` — 824 lines (not reviewed in this pass)
5. `src/components/themes/BaseTheme.cpp` — 810 lines
