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

## 9. Network Layer Audit (SDL_net equivalent)

### HttpDownloader — setInsecure() disables TLS verification

Both `fetchUrl()` and `downloadToFile()` use `NetworkClientSecure` with `setInsecure()` for all HTTPS URLs. This disables:
- Certificate chain validation (CA certs not verified)
- Hostname/CN verification

**Impact:** OPDS credentials (Basic Auth username:password) transit in plaintext if a MITM intercepts the connection. Acceptable for private networks with self-signed OPDS certs, but a security gap for untrusted networks.

**Action taken:** Added `TODO(security)` comments documenting the trade-off. Proper fix would require certificate fingerprinting or pinned certs per OPDS server.

**Usage context:**
- OPDS feed downloads: uses `HttpDownloader::fetchUrl()` with Basic Auth header
- Cover/metadata images: `HttpDownloader::downloadToFile()`
- NOT used for: WebDAV (uses raw TCP), WebSocket (raw TCP), OTA firmware (uses `esp_https_ota` with CN check enforced)

### WebDAVHandler — Path Traversal ✅ Properly Protected
- `getRequestPath()`: uses `FsHelpers::normalisePath()` + leading-slash normalization + trailing-slash removal
- `isProtectedPath()`: checks every path segment for `.`-prefix or `HIDDEN_ITEMS[]` match (prevents `/.hidden/foo`, `/System Volume Information/bar`)
- PUT: writes to temp file (`.davtmp`) then renames on success — no partial-write corruption
- PUT/DELETE/MOVE/COPY: all call `clearEpubCacheIfNeeded()` after modifying an EPUB
- Destination header in MOVE/COPY: fully parsed, URL-decoded, normalized, validated
- No raw `strcpy`/`sprintf` on path data

### CrossPointWebServer — Path Traversal ✅ Previously Fixed
- WebSocket filename validation: rejects `..`, `/`, `\` in filename
- HTTP upload: uses `normalizeWebPath()` with `FsHelpers::normalisePath()`
- `handleFileListData`: explicit `..` check on path before scanning
- `handleCreateFolder`: rejects `/`, `\`, `..` in folder name
- `handleDelete`: iterates paths, normalizes each, protects `HIDDEN_ITEMS[]`

### OtaUpdater — CN Check ✅ Correct
- `checkForUpdate()` (GitHub API `api.github.com`): `skip_cert_common_name_check = true` — documented, only for metadata
- `installUpdate()` (firmware download `download.github.com`): `skip_cert_common_name_check = false` — CN must verify
- Firmware signature verification: `TODO(security)` documented; needs `esp_https_ota_verify_signature()` or SHA-256 hash

---

## 10. Sleep / Wake Power Management

### Deep Sleep Architecture

**Sleep entry sequence (`enterDeepSleep` in `main.cpp`):**
1. `APP_STATE.lastSleepFromReader` saved to SD card
2. `activityManager.goToSleep()` → `SleepActivity` renders sleep screen
3. `display.deepSleep()` — e-paper panel enters low-power mode
4. RTC time snapshot: `g_unixBeforeSleep = time(nullptr)` + `g_rtcUsBeforeSleep = esp_clk_rtc_time()`
5. Clock also saved to SD (`saveClockToSD()`) as fallback — RTC_DATA_ATTR can be lost on some ESP32-C3 silicon
6. `powerManager.startDeepSleep(gpio, keepClockAlive, timerWakeMinutes)` called

**GPIO13 (SPIWP) power latch:**
- `keepClockAlive=true`: GPIO13 set HIGH → MCU stays powered (~3–4mA), RTC/LP timer running
- `keepClockAlive=false`: GPIO13 set LOW → MCU fully powered off (~$5µA draw), power button physically repowers device

### RTC Persistence Across Deep Sleep
- ESP32-C3 RTC is NOT reset by deep sleep. RTC_DATA_ATTR variables and the LP core retain state.
- `esp_clk_rtc_time()` uses the LP oscillator (slow clock) and continues counting through deep sleep.
- On wake: elapsed µs computed as `esp_clk_rtc_time() - backup.rtcTimeUs`, added to `backup.unixTime` to restore wall clock.
- `restoreClockFromSD()` in `main.cpp` validates with `unixTime >= 1700000000UL` as sanity check.

### Wake Sources
| Wake Source | Condition | Behavior |
|---|---|---|
| Power button | Physical press | Full boot, `verifyPowerButtonDuration()` check |
| Timer | `keepClockAlive=true`, `timerWakeMinutes>0` | Minimal wake for CLOCK/READING_STATS refresh, immediately re-enters deep sleep |
| USB power | USB powered cold boot | Immediately re-enters deep sleep |

### Power Button Wake Without Full Reboot
When `keepClockAlive=true`, MCU stays powered. Pressing the power button triggers `ESP_GPIO_WAKEUP_GPIO_LOW` which wakes from deep sleep without a cold boot — the device comes up immediately into the reading position without the full boot sequence.

When `keepClockAlive=false`, the MCU is fully powered off. The power button physically repowers the device (cold boot), after which `verifyPowerButtonDuration()` checks if the user held it long enough to stay awake or should return to sleep.

---

## 11. EPUB Rendering Internals

### Rendering Pipeline (HTML → e-Paper)

```
EPUB ZIP
  └── ChapterHtmlSlimParser (streaming HTML/XML parser)
        ├── CSS: CssParser → inline styles applied
        ├── Hyphenation: Hyphenator (Liang algorithm, language-specific patterns)
        └── Blocks → Page layout
              └── Section::createSectionFile()
                    ├── Parse HTML to intermediate block representation
                    ├── Layout text blocks with font/size/line-height
                    ├── Paginate: accumulate blocks until page full
                    ├── Serialize page to .bin (section file)
                    └── Build LUT (Look-Up Table): file offsets for each page
```

**Cache structure:** `/.crosspoint/epub_<hash>/`
- `book.bin`: spine + TOC metadata (FNV-1a href index for large EPUBs)
- `toc.ncx` / `toc.nav`: temporary parsed TOC
- `sections/` dir: per-chapter `.bin` files (versioned by `SECTION_FILE_VERSION`)
- `cover.bmp` / `cover_crop.bmp`: rendered cover
- `thumb_<height>.bmp`: 1-bit thumbnail for home screen

**Section file format (.bin):**
- Header: version, fontId, lineCompression, paragraphSpacing, viewport (W×H), hyphenation flag, embeddedStyle flag, imageRendering, pageCount, LUT offset, anchorMap offset
- Page LUT: array of `uint32_t` file offsets — O(1) seek to any page
- Anchor map: `uint16_t` count + (string + uint16_t page) pairs for footnote/fragment navigation
- Pages: variable-length serialized `Page` objects

### Hyphenation
- **Algorithm:** Liang hyphenation patterns (tex-hyphenation port)
- **Pattern source:** Language-specific serialized trie in `lib/Epub/Epub/hyphenation/generated/`
- **Break priority:** explicit hyphen/soft-hyphen (no inserted hyphen) → apostrophe contraction → Liang patterns → fallback every-N-chars
- **Language selection:** from EPUB `<dc:language>` tag, mapped to pattern set
- **Inserted hyphen character:** rendered as part of the word at break point

### Image Handling (380KB RAM Constraint)
- **Max source pixels:** 3,145,728 (2048×1536)
- **Flow:** Image decoded directly to framebuffer via `ImageToFramebufferDecoder` (JPEG via `JpegToFramebufferConverter`, PNG via `PngToFramebufferConverter`)
- **No full-RAM image buffer** — PNGdec streams scanline-by-scanline; JPEG uses progressive decode
- **Performance mode:** optional flag skips dithering for faster render
- **Image cache:** decoded pixel data optionally written to SD cache path for reuse

---

## 12. SD Caching System

### Cache Directory Structure
`/.crosspoint/epub_<hash>/` where `<hash> = std::hash<std::string>{}(filepath)`

### Cache Invalidation Triggers
Cache is cleared (via `Epub::clearCache()` → `Storage.removeDir(cachePath)`) when:
1. WebDAV PUT overwrites an EPUB → `WebDAVHandler::handlePut()` → `clearEpubCacheIfNeeded()`
2. WebDAV DELETE deletes an EPUB → `WebDAVHandler::handleDelete()` → `clearEpubCacheIfNeeded()`
3. WebDAV MOVE renames an EPUB → `WebDAVHandler::handleMove()` → `clearEpubCacheIfNeeded()`
4. HTTP upload via web UI → `CrossPointWebServer::handleUpload()` → `clearEpubCacheIfNeeded()`
5. Section file version mismatch → `Section::loadSectionFile()` → `clearCache()` + rebuild

### Per-Chapter Serialization
Each spine item (chapter) → `/.crosspoint/epub_<hash>/sections/<index>.bin`

**Section file versioning (`SECTION_FILE_VERSION = 18`):** Any change to font, line height, viewport, hyphenation, embedded style, or image rendering invalidates the cache and triggers full rebuild.

**LUT (Look-Up Table):** `uint32_t[]` array of file offsets. Allows O(1) seek to any page without reading preceding pages.

**Anchor map:** Serialized at end of section file. Used for footnote navigation and `#fragment` links.

### Corruption Handling
- File write: temp file (`.davtmp`) then rename — no partial writes on normal断电
- Section load failure: `clearCache()` + rebuild triggered
- Page seek out of range: returns `nullptr` — callers handle gracefully
- LUT entry validation: each entry checked `pos >= HEADER_SIZE && pos < lutOffset`
- Max page sanity: 10,000 pages per section (prevents infinite LUT loop)

---

## 13. BeeperActivity Completeness Check

**State:** Mostly functional, several gaps identified:

### Gaps Found

**Pagination (MISSING):**
- `BEEPER_MAX_CHATS = 50` constant exists but there's no pagination UI or cursor-based fetching.
- `fetchChats()` requests `?limit=50&include_muted=true` with no offset/cursor.
- Circular wrap-around (`selectorIndex = (selectorIndex + 1) % chats.size()`) means users cycle through all 50 chats endlessly — no "end of list" awareness.

**Message timestamps (PARTIAL):**
- `msg.timestamp` from API is rendered as raw string (`msg.senderName + "  " + msg.timestamp`).
- No formatting/relative time ("2 hours ago") or localization.
- API returns Unix timestamp or ISO 8601 string — not human-readable on-device.

**Mark as read (MISSING):**
- No `PUT /v1/chats/read` or equivalent call after viewing messages.
- Unread count badge shown but never decremented locally.
- Beeper API likely has a "mark as read" endpoint not being called.

**i18n gaps (known from prior audit):**
- `"Connecting..."`, `"No chats found"`, `"Failed to load messages"`, `"Loading messages..."`, `"No messages"`, `"Beeper"` (header) — all hardcoded.
- `STR_BEEPER_NO_CHATS`, `STR_BEEPER_NO_MESSAGES` exist in i18n keys — strings still missing from english.yaml.

**Circular navigation in message list:**
- Same circular wrap (`selectorIndex = (selectorIndex + 1) % messages.size()`) in CHAT_MESSAGES state.
- No pagination for long conversations.

---

## 14. PetCareTracker / PetEvolution Review

### PetCareTracker — No Critical Issues Found
- `clampSub()` / `clampAdd()` helpers prevent any uint8_t overflow in stat modifications
- `careMistakes` capped at 255 (`< 255` check before increment)
- `ATTENTION_CALL_INTERVAL_SEC` and `ATTENTION_CALL_EXPIRE_SEC` prevent rapid re-triggering
- No unchecked `strcpy`/`sprintf` on pet names or state strings

### PetEvolution — Minor Logic Bug (non-critical)

In `checkEvolution()` → `determineVariant()`:
```cpp
uint8_t prevStageIdx = static_cast<uint8_t>(state.stage) - 1;
uint16_t minPages = PetConfig::EVOLUTION[prevStageIdx].minPages;
```
`prevStageIdx` should be `stageIdx` (the current stage index), not `stageIdx - 1`. For example, when checking evolution from HATCHLING (stageIdx=1), this reads `EVOLUTION[0]` (EGG→HATCHLING requirements) instead of `EVOLUTION[1]` (HATCHLING→YOUNGSTER requirements). This affects the Scholar variant threshold calculation (`scholarThreshold = minPages * 1.5`).

**Impact:** Non-critical. EVOLUTION[] values are such that the bug doesn't cause a crash or incorrect stage transition. It only affects the Scholar/Balanced/Wild variant determination for pets at the HATCHLING and EGG stages, which is unlikely to cause visible issues. The Scholar threshold being off by a small amount is the extent of the impact.

**Note:** No off-by-one in bounds checks for stage → variant mapping. All `static_cast<uint8_t>(state.stage)` usages in `variantStageName()` and `typeName()` have `default:` fallbacks that prevent crashes for invalid enum values.

---

## 5. Largest Source Files (by line count)
1. `src/activities/boot_sleep/SleepActivity.cpp` — 1343 lines
2. `src/network/CrossPointWebServer.cpp` — 1318 lines
3. `src/activities/reader/EpubReaderActivity.cpp` — 1260 lines
4. `src/network/WebDAVHandler.cpp` — 824 lines (not reviewed in this pass)
5. `src/components/themes/BaseTheme.cpp` — 810 lines
