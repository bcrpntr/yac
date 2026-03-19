#include "BeeperChatListActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WiFi.h>

#include "BeeperMessageViewActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/SemaphoreGuard.h"

void BeeperChatListActivity::taskTrampoline(void* param) {
  auto* self = static_cast<BeeperChatListActivity*>(param);
  self->displayTaskLoop();
}

void BeeperChatListActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  selectorIndex = 0;
  isLoading = true;
  fetchFailed = false;
  needsFetch = true;
  statusMessage = "Connecting...";
  chats.clear();
  updateRequired = true;

  xTaskCreate(&BeeperChatListActivity::taskTrampoline, "BeeperListTask", 16384, this, 1, &displayTaskHandle);
}

void BeeperChatListActivity::onExit() {
  ActivityWithSubactivity::onExit();
  {
    SemaphoreGuard guard(renderingMutex);
    if (displayTaskHandle) { vTaskDelete(displayTaskHandle); displayTaskHandle = nullptr; }
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  chats.clear();
}

bool BeeperChatListActivity::fetchChats() {
  if (SETTINGS.beeperApiUrl[0] == '\0') {
    statusMessage = "Beeper API URL not set";
    return false;
  }

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    LOG_DBG("BEEPER", "WiFi not connected, attempting...");
    WiFi.mode(WIFI_STA);
    WiFi.begin();  // Use stored credentials
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
      vTaskDelay(250 / portTICK_PERIOD_MS);
      attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
      statusMessage = "WiFi not connected";
      LOG_ERR("BEEPER", "WiFi connection failed");
      return false;
    }
    LOG_DBG("BEEPER", "WiFi connected: %s", WiFi.localIP().toString().c_str());
  }

  // Build URL — ensure http:// prefix
  std::string url = SETTINGS.beeperApiUrl;
  if (url.find("://") == std::string::npos) {
    url = "http://" + url;
  }
  if (url.back() == '/') url.pop_back();
  url += "/v1/chats";

  HTTPClient http;
  WiFiClient client;
  http.begin(client, url.c_str());
  http.addHeader("Accept", "application/json");
  if (SETTINGS.beeperApiToken[0]) {
    std::string auth = "Bearer ";
    auth += SETTINGS.beeperApiToken;
    http.addHeader("Authorization", auth.c_str());
  }
  http.setTimeout(8000);

  int httpCode = http.GET();
  if (httpCode != 200) {
    LOG_ERR("BEEPER", "HTTP %d from %s", httpCode, url.c_str());
    char buf[48];
    snprintf(buf, sizeof(buf), "HTTP error: %d", httpCode);
    statusMessage = buf;
    http.end();
    return false;
  }

  // Use a JSON filter to only parse the fields we need — saves massive RAM
  JsonDocument filter;
  JsonArray itemsFilter = filter["items"].to<JsonArray>();
  JsonObject itemFilter = itemsFilter.add<JsonObject>();
  itemFilter["id"] = true;
  itemFilter["title"] = true;
  itemFilter["unreadCount"] = true;
  itemFilter["preview"]["text"] = true;
  itemFilter["preview"]["senderName"] = true;

  // Stream-parse the response to avoid buffering entire JSON
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                              DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    LOG_ERR("BEEPER", "JSON parse error: %s", err.c_str());
    statusMessage = "Parse error";
    return false;
  }

  chats.clear();
  JsonArray items = doc["items"];
  for (JsonObject item : items) {
    BeeperChat chat;
    chat.id = item["id"] | "";
    chat.title = item["title"] | "Unnamed";
    chat.unreadCount = item["unreadCount"] | 0;
    JsonObject preview = item["preview"];
    if (!preview.isNull()) {
      chat.previewText = preview["text"] | "";
      chat.previewSender = preview["senderName"] | "";
    }
    if (!chat.id.empty()) chats.push_back(std::move(chat));
    if (chats.size() >= 20) break;  // Limit to 20 chats for memory
  }

  LOG_DBG("BEEPER", "Fetched %zu chats", chats.size());
  return true;
}

void BeeperChatListActivity::loop() {
  if (subActivity) { subActivity->loop(); return; }

  const int itemCount = static_cast<int>(chats.size());

  // Long-press Confirm to refresh
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() > 1500) {
    if (!refreshTriggered && !needsFetch) {
      refreshTriggered = true;
      isLoading = true;
      statusMessage = "Refreshing...";
      selectorIndex = 0;
      needsFetch = true;
    }
    return;
  }
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) refreshTriggered = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!chats.empty()) openChat(chats[static_cast<size_t>(selectorIndex)]);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (itemCount > 0) { selectorIndex = (selectorIndex + itemCount - 1) % itemCount; updateRequired = true; }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (itemCount > 0) { selectorIndex = (selectorIndex + 1) % itemCount; updateRequired = true; }
  }
}

void BeeperChatListActivity::openChat(const BeeperChat& chat) {
  SemaphoreGuard guard(renderingMutex);
  exitActivity();
  enterNewActivity(new BeeperMessageViewActivity(renderer, mappedInput, chat.id, chat.title,
                                                  [this] { onMessageViewComplete(); }));
}

void BeeperChatListActivity::onMessageViewComplete() {
  exitActivity();
  isLoading = true;
  statusMessage = "Updating...";
  needsFetch = true;
}

void BeeperChatListActivity::displayTaskLoop() {
  while (true) {
    // Do HTTP fetches in this task (has its own 12KB stack)
    if (needsFetch) {
      needsFetch = false;
      updateRequired = true;  // Show "Connecting..." first
      vTaskDelay(10 / portTICK_PERIOD_MS);  // Let render happen
      {
        SemaphoreGuard guard(renderingMutex);
        render();
      }
      fetchFailed = !fetchChats();
      if (fetchFailed && statusMessage == "Connecting...") statusMessage = "Failed to connect";
      isLoading = false;
      updateRequired = true;
    }
    if (updateRequired && !subActivity) {
      updateRequired = false;
      SemaphoreGuard guard(renderingMutex);
      render();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void BeeperChatListActivity::render() const {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int titleLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);

  // Header bar
  renderer.drawCenteredText(UI_10_FONT_ID, 8, "Beeper", true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 28, pageWidth, 28);

  if (isLoading || (chats.empty() && !fetchFailed)) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    renderer.displayBuffer();
    return;
  }
  if (chats.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, statusMessage.c_str());
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 15, "Hold Confirm to refresh");
    renderer.displayBuffer();
    return;
  }

  // Each chat row: title line + preview line + gap + separator
  const int rowH = titleLineH + smallLineH + 12;
  constexpr int listTop = 34;
  const int maxVisible = (pageHeight - listTop - 10) / rowH;
  const int pageStart = (selectorIndex / maxVisible) * maxVisible;

  for (size_t i = static_cast<size_t>(pageStart);
       i < chats.size() && i < static_cast<size_t>(pageStart + maxVisible); i++) {
    const int slot = static_cast<int>(i) - pageStart;
    const int yTop = listTop + slot * rowH;
    const bool isSel = (static_cast<int>(i) == selectorIndex);
    const auto& c = chats[i];

    // Selection highlight
    if (isSel) renderer.fillRect(0, yTop, pageWidth, rowH - 2);

    // Title row: chat name on left, unread count on right
    const int titleY = yTop + 2;
    int titleMaxW = pageWidth - 30;
    if (c.unreadCount > 0) {
      char badge[8];
      snprintf(badge, sizeof(badge), "(%d)", c.unreadCount);
      auto bw = renderer.getTextWidth(SMALL_FONT_ID, badge);
      renderer.drawText(SMALL_FONT_ID, pageWidth - 12 - bw, titleY + 2, badge, !isSel, EpdFontFamily::BOLD);
      titleMaxW -= bw + 20;
    }
    auto title = renderer.truncatedText(UI_10_FONT_ID, c.title.c_str(), titleMaxW);
    renderer.drawText(UI_10_FONT_ID, 12, titleY, title.c_str(), !isSel,
                      c.unreadCount > 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    // Preview row: sender + message text
    const int prevY = titleY + titleLineH + 1;
    if (!c.previewText.empty()) {
      std::string preview;
      if (!c.previewSender.empty()) preview = c.previewSender + ": ";
      preview += c.previewText;
      auto prevText = renderer.truncatedText(SMALL_FONT_ID, preview.c_str(), pageWidth - 24);
      renderer.drawText(SMALL_FONT_ID, 12, prevY, prevText.c_str(), !isSel);
    }

    // Separator line (skip for last item)
    if (i < chats.size() - 1 && static_cast<int>(i) < pageStart + maxVisible - 1) {
      renderer.drawLine(12, yTop + rowH - 2, pageWidth - 12, yTop + rowH - 2);
    }
  }

  // Page indicator if more than one page
  const int totalPages = (static_cast<int>(chats.size()) + maxVisible - 1) / maxVisible;
  if (totalPages > 1) {
    char pageInfo[16];
    snprintf(pageInfo, sizeof(pageInfo), "%d/%d", (selectorIndex / maxVisible) + 1, totalPages);
    auto pw = renderer.getTextWidth(SMALL_FONT_ID, pageInfo);
    renderer.drawText(SMALL_FONT_ID, pageWidth - pw - 8, pageHeight - smallLineH - 2, pageInfo);
  }

  renderer.displayBuffer();
}
