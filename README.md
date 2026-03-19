# Yac Reader

**Yet Another CrossPoint (fork) — a pocket e-reader with tools.**

Yac is a fork of [CrossPet](https://github.com/trilwu/crosspet) / [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) — open-source firmware for the **Xteink X4** e-paper reader, built with **PlatformIO** on **ESP32-C3**.

## What changed from CrossPoint/CrossPet

### Branding
- Renamed throughout: AP SSID → `Yac-Reader`, mDNS hostname → `yac`, BLE → `Yac`, UDP discovery → `yac`
- Web UI titles → "Yac Reader", footers → "Yac Reader • Based on CrossPoint"
- Theme names → "Yac Reader" / "Yac Classic"
- All `CROSSPOINT_VERSION` → `YAC_VERSION` in compiled source
- Internal class names and SD paths (`/.crosspoint/`) preserved for data compatibility

### Removed
- **Lunatask integration** — removed entirely (activity, settings field, I18n entries)

### Weather
- **Today's high/low** — shown below "Feels like" in weather app and on sleep screen (`72°F (H:78 L:55) Partly Cloudy`)
- **Wind speed in mph** — converted from km/h
- **Silent refresh during sleep** — weather cache refreshes every ~15 minutes during sleep screen timer wakes (alongside NTP sync)

### Moon Phase
- **Sleep screen** — replaced Vietnamese lunar date with English moon phase name (e.g., "Waxing Crescent")
- **Clock app** — moon phase line below date, simplified calendar to single-row Gregorian-only cells

### Image Rendering
- **Removed size limits** — `MAX_SOURCE_PIXELS` raised to 64MP, PNG line buffer allocates dynamically based on actual width

### NTP Sync
- **On wake/sleep** — `silentNtpSync()` called on wake (if clock is approximate) and before deep sleep entry
- Forces 1-minute sleep refresh for clock sleep screen mode

### Flashcards (New App)
- **SM-2 spaced repetition** flashcard system accessible from Tools menu
- **Deck browser** — scans `/flashcards/` for `.txt` (tab-separated `front\tback`) and `.xfc` (native JSON with progress) files
- **Review activity** — shows cards one at a time with SM-2 quality ratings (Again/Hard/Good/Easy mapped to the 4 front buttons)
- **Session resume** — deterministic shuffle with saved seed; exit mid-session and resume where you left off
- **Day counter** — long-press Right in deck browser to advance day (since ESP32 has no battery-backed RTC)
- **Session cap** — max 20 cards per session to prevent memory exhaustion
- **Hidden from device file browser** — `/flashcards/` is hidden on-device but visible in the web UI for uploading

### Beeper Integration (New App)
- **Read-only Beeper Desktop API client** accessible from Tools menu
- Connects to [Beeper Desktop API](https://developers.beeper.com/desktop-api) running on your LAN (requires Remote Access enabled in Beeper Desktop)
- **Chat list** — recent chats with title, unread badge, last message preview, separator lines between rows, page indicator
- **Message viewer** — scrollable chat-style layout (newest at bottom), sender names in bold, timestamps, text wrapping, "You" messages indented with `>` prefix, separator lines between messages, scroll position indicator
- **Auto WiFi** — connects to stored WiFi credentials automatically when opening the app
- **Settings** — Beeper API URL and Bearer token configured via web UI (Settings → Beeper)
- **JSON filter parsing** — only parses needed fields from API responses to minimize heap usage on ESP32-C3
- Conditionally shown in Tools menu (only when API URL is configured, same pattern as OPDS)
- Long-press Confirm to refresh chat list or messages
- HTTP runs on a dedicated 16KB FreeRTOS task to avoid stack overflow on the main loop

### Settings Crash Fixes
- **On-device settings** — stack overflow in `SettingsActivity::onEnter()` fixed by switching per-category vectors from value copies to pointer vectors
- **Web UI settings** — stack overflow on `loopTask` when `handleGetSettings()` triggered first-time construction of static vector; fixed by forcing construction early in `setup()` and bumping loop stack to 12KB

### Infrastructure
- **`ActivityWithSubactivity`** — new base class for activities that host child sub-activities (used by flashcards and Beeper)
- **`SemaphoreGuard`** — RAII FreeRTOS semaphore wrapper
- **Flashcard state persistence** — 5 new fields in `CrossPointState` (deck path, day counter, session index/size, shuffle seed) with JSON serialization

---

## Hardware

| Spec | Detail |
|------|--------|
| MCU | ESP32-C3 RISC-V @ 160MHz |
| RAM | ~380KB (no PSRAM) |
| Flash | 16MB |
| Display | 800×480 E-Ink (SSD1677) |
| Storage | SD Card |
| Wireless | WiFi 802.11 b/g/n, BLE 5.0 |

## Inherited Features

Everything below is inherited from CrossPoint/CrossPet:

- **EPUB 2 & 3** with image support, anti-aliased grayscale text rendering
- **3 font families**, 4 sizes, 4 styles (including Bokerlam Vietnamese serif)
- **Multi-language hyphenation**, 4 screen orientations, remappable buttons
- **KOReader Sync** cross-device progress
- **Reading statistics & streaks** — per-session, daily, all-time tracking
- **Virtual Chicken Companion** — grows with reading activity (Egg → Hatchling → Juvenile → Adult → Elder)
- **Sleep screens** — Clock, Reading Stats, Cover, with configurable refresh intervals
- **Tools** — Weather, Clock, Pomodoro, BLE Presenter, 5 mini games
- **WiFi book upload**, Calibre/OPDS browser, OTA updates
- **BLE 5.0** remote control and presenter mode

## Installing

### Web (latest firmware)

1. Connect your Xteink X4 via USB-C and wake/unlock the device
2. Go to https://xteink.dve.al/ and click "Flash CrossPoint firmware"

### Manual

See [Development](#development) below.

## Development

### Prerequisites

* **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
* Python 3.8+
* USB-C cable for flashing the ESP32-C3
* Xteink X4

### Cloning

```
git clone --recursive https://github.com/trilwu/crosspet
```

### Flashing

```sh
pio run --target upload
```

### Serial debugging

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101  # macOS
python3 scripts/debugging_monitor.py                       # Linux
```

## SD Card Layout

```
/
├── flashcards/              # Flashcard decks (.txt tab-separated, .xfc native)
├── .crosspoint/             # Internal cache (preserved from upstream for compat)
│   ├── epub_<hash>/         # Per-book cache (progress, cover, sections)
│   ├── state.json           # App state (includes flashcard session state)
│   ├── reading_stats.bin    # Reading statistics
│   └── weather_cache.json   # Weather data cache
└── *.epub                   # Your books
```

## Flashcard Deck Format

Tab-separated `.txt` files in `/flashcards/`:

```
# My Deck Title
front text	back text
another front	another back
pregunta	answer
```

First line starting with `#` sets the deck title. Each subsequent line is `front<TAB>back`. Progress is saved as a `.xfc` JSON file alongside the source `.txt`.

## Beeper Setup

1. Install [Beeper Desktop](https://www.beeper.com/download) on a computer on the same LAN as the reader
2. Open Beeper Desktop → Settings → Developers → enable **Beeper Desktop API**
3. Under Advanced settings, enable **Remote Access** (binds to `0.0.0.0:23373`)
4. Click "+" next to "Approved connections" to create a Bearer token
5. On the Yac reader web UI → Settings → Beeper: enter `http://<laptop-lan-ip>:23373` and the token
6. On the reader: Tools → Beeper

The laptop's LAN IP must be on the same subnet as the reader's WiFi. Use `ifconfig en0 | grep inet` (macOS) or `ip addr show` (Linux) to find it. VPN/Tailscale IPs won't work unless the reader is also on that network.

**Controls:**
- Up/Down: navigate chat list or scroll messages
- Confirm: open selected chat
- Back: go back / exit
- Long-press Confirm: refresh current view

---

Yac Reader is **not affiliated with Xteink or any manufacturer of the X4 hardware**.

Based on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) and [CrossPet](https://github.com/trilwu/crosspet).
