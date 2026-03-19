#include "LunataskDashActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HTTPClient.h>
#include <I18n.h>
#include <Logging.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <StreamString.h>
#include <WiFi.h>

#include <ctime>
#include <memory>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ── WiFi helpers (same pattern as WeatherActivity) ─────────────────────────────

static bool trySilentWifi() {
  const auto& ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) return false;
  const auto* cred = WIFI_STORE.findCredential(ssid);
  if (!cred) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  for (int i = 0; i < 100; i++) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  WiFi.disconnect(false);
  return false;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────────

void LunataskDashActivity::onEnter() {
  Activity::onEnter();
  memset(&data, 0, sizeof(data));

  if (SETTINGS.lunataskToken[0] == '\0') {
    state = NO_TOKEN;
    statusMessage = "No Lunatask token. Configure in Web Settings.";
    if (loadCache(data)) {
      state = DISPLAYING;
    }
    requestUpdate(true);
    return;
  }

  state = WIFI_CONNECTING;
  statusMessage = "Connecting...";
  requestUpdate(true);

  if (WiFi.status() == WL_CONNECTED) {
    onWifiConnected();
  } else if (trySilentWifi()) {
    onWifiConnected();
  } else {
    startActivityForResult(
        std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
        [this](const ActivityResult& r) {
          if (r.isCancelled) {
            if (loadCache(data)) {
              state = DISPLAYING;
            } else {
              state = FETCH_ERROR;
              statusMessage = "No WiFi, no cached data.";
            }
            requestUpdate(true);
            return;
          }
          onWifiConnected();
        });
  }
}

void LunataskDashActivity::onExit() {
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  Activity::onExit();
}

void LunataskDashActivity::onWifiConnected() {
  state = FETCHING;
  statusMessage = "Fetching tasks...";
  requestUpdate(true);
  fetchTasks();
}

// ── API fetch ──────────────────────────────────────────────────────────────────

void LunataskDashActivity::fetchTasks() {
  std::string url = "https://api.lunatask.app/v1/tasks";

  // Use HTTPClient directly — HttpDownloader doesn't support custom Authorization headers.
  auto* secureClient = new NetworkClientSecure();
  secureClient->setInsecure();
  std::unique_ptr<NetworkClient> client(secureClient);

  HTTPClient http;
  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPet-ESP32");

  std::string authHeader = "bearer ";
  authHeader += SETTINGS.lunataskToken;
  LOG_DBG("LUNA", "Token length: %zu, auth header length: %zu", strlen(SETTINGS.lunataskToken), authHeader.size());
  http.addHeader("Authorization", authHeader.c_str());

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("LUNA", "Fetch failed: %d", httpCode);
    http.end();
    if (loadCache(data)) {
      state = DISPLAYING;
      statusMessage = "Showing cached data (API error).";
    } else {
      state = FETCH_ERROR;
      char buf[40];
      snprintf(buf, sizeof(buf), "API error: %d", httpCode);
      statusMessage = buf;
    }
    requestUpdate(true);
    return;
  }

  StreamString stream;
  http.writeToStream(&stream);
  http.end();

  std::string body = stream.c_str();
  LOG_DBG("LUNA", "Fetched %zu bytes", body.size());

  if (!parseTasks(body)) {
    if (loadCache(data)) {
      state = DISPLAYING;
    } else {
      state = FETCH_ERROR;
      statusMessage = "Failed to parse response.";
    }
  } else {
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(data.lastUpdate, sizeof(data.lastUpdate), "%02d:%02d %02d/%02d",
             t.tm_hour, t.tm_min, t.tm_mday, t.tm_mon + 1);
    saveCache();
    state = DISPLAYING;
  }

  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  requestUpdate(true);
}

// ── JSON parse ─────────────────────────────────────────────────────────────────

bool LunataskDashActivity::parseTasks(const std::string& json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    LOG_ERR("LUNA", "JSON parse error: %s", err.c_str());
    return false;
  }

  JsonArray tasks = doc["tasks"];
  if (tasks.isNull()) {
    LOG_ERR("LUNA", "No 'tasks' array in response");
    return false;
  }

  memset(&data, 0, sizeof(data));

  time_t now;
  time(&now);
  struct tm today;
  localtime_r(&now, &today);
  char todayStr[12];
  snprintf(todayStr, sizeof(todayStr), "%04d-%02d-%02d",
           today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);
  time_t weekAgo = now - 7 * 86400;

  for (JsonObject task : tasks) {
    data.total++;

    const char* status = task["status"] | "later";
    if (strcmp(status, "later") == 0) data.later++;
    else if (strcmp(status, "next") == 0) data.next++;
    else if (strcmp(status, "started") == 0) data.started++;
    else if (strcmp(status, "waiting") == 0) data.waiting++;
    else if (strcmp(status, "completed") == 0) data.completed++;

    int prio = task["priority"] | 0;
    int prioIdx = prio + 2;
    if (prioIdx >= 0 && prioIdx < 5) data.priority[prioIdx]++;

    int eis = task["eisenhower"] | 0;
    if (eis >= 0 && eis < 5) data.eisenhower[eis]++;

    const char* scheduled = task["scheduled_on"] | "";
    if (scheduled[0] && strncmp(scheduled, todayStr, 10) == 0) {
      data.scheduledToday++;
    }

    const char* completedAt = task["completed_at"] | "";
    if (completedAt[0] && strcmp(status, "completed") == 0) {
      struct tm ct = {};
      if (sscanf(completedAt, "%d-%d-%dT", &ct.tm_year, &ct.tm_mon, &ct.tm_mday) == 3) {
        ct.tm_year -= 1900;
        ct.tm_mon -= 1;
        time_t ctime = mktime(&ct);
        if (ctime >= weekAgo) data.completedRecent++;
      }
    }
  }

  LOG_DBG("LUNA", "Parsed %d tasks: L=%d N=%d S=%d W=%d D=%d", data.total,
          data.later, data.next, data.started, data.waiting, data.completed);
  return true;
}

// ── SD cache ───────────────────────────────────────────────────────────────────

void LunataskDashActivity::saveCache() {
  JsonDocument doc;
  doc["later"] = data.later;
  doc["next"] = data.next;
  doc["started"] = data.started;
  doc["waiting"] = data.waiting;
  doc["completed"] = data.completed;
  doc["scheduledToday"] = data.scheduledToday;
  doc["completedRecent"] = data.completedRecent;
  doc["total"] = data.total;
  doc["lastUpdate"] = data.lastUpdate;

  JsonArray pArr = doc["priority"].to<JsonArray>();
  for (int i = 0; i < 5; i++) pArr.add(data.priority[i]);

  JsonArray eArr = doc["eisenhower"].to<JsonArray>();
  for (int i = 0; i < 5; i++) eArr.add(data.eisenhower[i]);

  std::string json;
  serializeJson(doc, json);

  FsFile f;
  if (Storage.openFileForWrite("LUNA", CACHE_PATH, f)) {
    f.write((const uint8_t*)json.c_str(), json.size());
    f.close();
    LOG_DBG("LUNA", "Cache saved (%zu bytes)", json.size());
  }
}

bool LunataskDashActivity::loadCache(LunataskData& out) {
  String content = Storage.readFile(CACHE_PATH);
  if (content.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, content)) return false;

  memset(&out, 0, sizeof(out));
  out.later = doc["later"] | 0;
  out.next = doc["next"] | 0;
  out.started = doc["started"] | 0;
  out.waiting = doc["waiting"] | 0;
  out.completed = doc["completed"] | 0;
  out.scheduledToday = doc["scheduledToday"] | 0;
  out.completedRecent = doc["completedRecent"] | 0;
  out.total = doc["total"] | 0;

  const char* lu = doc["lastUpdate"] | "";
  strncpy(out.lastUpdate, lu, sizeof(out.lastUpdate) - 1);

  JsonArray pArr = doc["priority"];
  if (!pArr.isNull()) {
    for (int i = 0; i < 5 && i < (int)pArr.size(); i++) out.priority[i] = pArr[i];
  }
  JsonArray eArr = doc["eisenhower"];
  if (!eArr.isNull()) {
    for (int i = 0; i < 5 && i < (int)eArr.size(); i++) out.eisenhower[i] = eArr[i];
  }

  return true;
}

// ── Input loop ─────────────────────────────────────────────────────────────────

void LunataskDashActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && state == DISPLAYING) {
    if (SETTINGS.lunataskToken[0] != '\0') {
      state = WIFI_CONNECTING;
      statusMessage = "Refreshing...";
      requestUpdate(true);
      if (WiFi.status() == WL_CONNECTED || trySilentWifi()) {
        onWifiConnected();
      } else {
        state = FETCH_ERROR;
        statusMessage = "WiFi unavailable.";
        requestUpdate(true);
      }
    }
  }
}

// ── Render ──────────────────────────────────────────────────────────────────────

void LunataskDashActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, "Lunatask");

  if (state == NO_TOKEN || state == FETCH_ERROR || state == WIFI_CONNECTING || state == FETCHING) {
    const int cy = renderer.getScreenHeight() / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, cy - 10, statusMessage.c_str());
    if (state == NO_TOKEN) {
      renderer.drawCenteredText(SMALL_FONT_ID, cy + 20, "Go to WiFi Transfer > Settings page");
    }
    renderer.displayBuffer();
    return;
  }

  renderDashboard();

  const auto labels = mappedInput.mapLabels("Back", "Refresh", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void LunataskDashActivity::renderDashboard() {
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int startY = metrics.topPadding + metrics.headerHeight + 8;
  const int margin = 14;
  const int contentW = pageW - 2 * margin;
  const int lhSmall = renderer.getLineHeight(SMALL_FONT_ID);

  int y = startY;

  // Row 1: Summary stats
  char buf[80];
  snprintf(buf, sizeof(buf), "%d tasks  |  %d today  |  %d done (7d)",
           data.total, data.scheduledToday, data.completedRecent);
  renderer.drawCenteredText(SMALL_FONT_ID, y, buf);
  y += lhSmall + 6;

  // Row 2: Status breakdown
  renderer.drawLine(margin, y, pageW - margin, y);
  y += 4;
  renderer.drawText(SMALL_FONT_ID, margin, y, "STATUS", true, EpdFontFamily::BOLD);
  y += lhSmall + 2;
  renderStatusCounts(margin, y, contentW);
  y += 46;

  // Row 3: Priority bars
  renderer.drawLine(margin, y, pageW - margin, y);
  y += 4;
  renderer.drawText(SMALL_FONT_ID, margin, y, "PRIORITY", true, EpdFontFamily::BOLD);
  y += lhSmall + 4;
  renderPriorityBars(margin, y, contentW, 90);
  y += 96;

  // Row 4: Eisenhower matrix
  renderer.drawLine(margin, y, pageW - margin, y);
  y += 4;
  renderer.drawText(SMALL_FONT_ID, margin, y, "EISENHOWER MATRIX", true, EpdFontFamily::BOLD);
  y += lhSmall + 4;
  const int cellW = (contentW - 4) / 2;
  const int cellH = 52;
  renderEisenhowerGrid(margin, y, cellW, cellH);
  y += cellH * 2 + 8;

  // Footer: last update
  if (data.lastUpdate[0]) {
    snprintf(buf, sizeof(buf), "Updated: %s", data.lastUpdate);
    const int footW = renderer.getTextWidth(SMALL_FONT_ID, buf);
    renderer.drawText(SMALL_FONT_ID, pageW - footW - margin, pageH - lhSmall - 50, buf);
  }
}

void LunataskDashActivity::renderStatusCounts(int x, int y, int w) {
  const int colW = w / 5;

  struct StatusCol { const char* label; int count; };
  StatusCol cols[] = {
      {"Later", data.later},
      {"Next", data.next},
      {"Active", data.started},
      {"Waiting", data.waiting},
      {"Done", data.completed},
  };

  for (int i = 0; i < 5; i++) {
    const int cx = x + i * colW + colW / 2;
    char num[8];
    snprintf(num, sizeof(num), "%d", cols[i].count);
    const int numW = renderer.getTextWidth(UI_12_FONT_ID, num);
    renderer.drawText(UI_12_FONT_ID, cx - numW / 2, y, num, true, EpdFontFamily::BOLD);

    const int lblW = renderer.getTextWidth(SMALL_FONT_ID, cols[i].label);
    renderer.drawText(SMALL_FONT_ID, cx - lblW / 2, y + renderer.getLineHeight(UI_12_FONT_ID) + 2,
                      cols[i].label);
  }
}

void LunataskDashActivity::renderEisenhowerGrid(int x, int y, int cellW, int cellH) {
  struct Quad { const char* label; int count; };
  Quad quads[] = {
      {"Urgent+Imp", data.eisenhower[1]},
      {"Imp, !Urg", data.eisenhower[3]},
      {"Urg, !Imp", data.eisenhower[2]},
      {"Neither", data.eisenhower[4]},
  };

  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 2; col++) {
      int idx = row * 2 + col;
      int cx = x + col * (cellW + 4);
      int cy = y + row * (cellH + 4);

      renderer.drawRect(cx, cy, cellW, cellH);

      char num[8];
      snprintf(num, sizeof(num), "%d", quads[idx].count);
      const int numW = renderer.getTextWidth(UI_12_FONT_ID, num);
      renderer.drawText(UI_12_FONT_ID, cx + cellW / 2 - numW / 2, cy + 6, num, true,
                        EpdFontFamily::BOLD);

      const int lblW = renderer.getTextWidth(SMALL_FONT_ID, quads[idx].label);
      renderer.drawText(SMALL_FONT_ID, cx + cellW / 2 - lblW / 2,
                        cy + 6 + renderer.getLineHeight(UI_12_FONT_ID) + 2, quads[idx].label);
    }
  }
}

void LunataskDashActivity::renderPriorityBars(int x, int y, int w, int h) {
  const char* labels[] = {"Lowest", "Low", "Normal", "High", "Highest"};
  const int barH = 12;
  const int gap = 4;
  const int labelW = 60;
  const int barAreaW = w - labelW - 30;

  int maxCount = 1;
  for (int i = 0; i < 5; i++) {
    if (data.priority[i] > maxCount) maxCount = data.priority[i];
  }

  for (int i = 0; i < 5; i++) {
    const int by = y + i * (barH + gap);
    renderer.drawText(SMALL_FONT_ID, x, by, labels[i]);

    const int barX = x + labelW;
    int barW = (data.priority[i] * barAreaW) / maxCount;
    if (barW < 2 && data.priority[i] > 0) barW = 2;
    renderer.fillRect(barX, by + 1, barW, barH - 2);

    char num[8];
    snprintf(num, sizeof(num), "%d", data.priority[i]);
    renderer.drawText(SMALL_FONT_ID, barX + barW + 4, by, num);
  }
}
