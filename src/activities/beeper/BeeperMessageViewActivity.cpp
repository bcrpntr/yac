#include "BeeperMessageViewActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WiFi.h>
#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/SemaphoreGuard.h"

void BeeperMessageViewActivity::taskTrampoline(void* param) {
  auto* self = static_cast<BeeperMessageViewActivity*>(param);
  self->displayTaskLoop();
}

void BeeperMessageViewActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  isLoading = true;
  needsFetch = true;
  statusMessage = "Loading messages...";
  messages.clear();
  scrollOffset = 0;
  updateRequired = true;

  xTaskCreate(&BeeperMessageViewActivity::taskTrampoline, "BeeperMsgTask", 16384, this, 1, &displayTaskHandle);
}

void BeeperMessageViewActivity::onExit() {
  Activity::onExit();
  {
    SemaphoreGuard guard(renderingMutex);
    if (displayTaskHandle) { vTaskDelete(displayTaskHandle); displayTaskHandle = nullptr; }
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  messages.clear();
}

bool BeeperMessageViewActivity::fetchMessages() {
  if (SETTINGS.beeperApiUrl[0] == '\0') {
    statusMessage = "API URL not set";
    return false;
  }

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    statusMessage = "WiFi not connected";
    return false;
  }

  // URL-encode chatId (it contains ! and : characters)
  std::string encodedId;
  for (char c : chatId) {
    if (c == '!') encodedId += "%21";
    else if (c == ':') encodedId += "%3A";
    else if (c == '@') encodedId += "%40";
    else encodedId += c;
  }

  std::string url = SETTINGS.beeperApiUrl;
  if (url.find("://") == std::string::npos) {
    url = "http://" + url;
  }
  if (url.back() == '/') url.pop_back();
  url += "/v1/chats/" + encodedId + "/messages";

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
    LOG_ERR("BEEPER", "Messages HTTP %d", httpCode);
    char buf[48];
    snprintf(buf, sizeof(buf), "HTTP error: %d", httpCode);
    statusMessage = buf;
    http.end();
    return false;
  }

  // Filter to only parse fields we need
  JsonDocument filter;
  JsonArray itemsFilter = filter["items"].to<JsonArray>();
  JsonObject itemFilter = itemsFilter.add<JsonObject>();
  itemFilter["text"] = true;
  itemFilter["senderName"] = true;
  itemFilter["timestamp"] = true;
  itemFilter["isSender"] = true;
  itemFilter["type"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                              DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    LOG_ERR("BEEPER", "JSON parse: %s", err.c_str());
    statusMessage = "Parse error";
    return false;
  }

  messages.clear();
  JsonArray items = doc["items"];
  for (JsonObject item : items) {
    const char* type = item["type"] | "TEXT";
    // Skip non-text messages (reactions, state events, etc.)
    if (strcmp(type, "TEXT") != 0 && strcmp(type, "NOTICE") != 0) continue;
    const char* text = item["text"] | "";
    if (text[0] == '\0') continue;

    BeeperMessage msg;
    msg.senderName = item["senderName"] | "";
    msg.text = text;
    msg.isSender = item["isSender"] | false;
    // Parse timestamp — extract HH:MM from ISO string
    const char* ts = item["timestamp"] | "";
    if (strlen(ts) >= 16) {
      msg.timestamp = std::string(ts + 11, 5);  // "HH:MM"
    }
    messages.push_back(std::move(msg));
    if (messages.size() >= 50) break;
  }

  // Reverse so newest messages are at the bottom (API returns newest first)
  std::reverse(messages.begin(), messages.end());
  // Start scrolled to bottom (most recent messages)
  scrollOffset = static_cast<int>(messages.size()) > 0 ? static_cast<int>(messages.size()) - 1 : 0;

  LOG_DBG("BEEPER", "Fetched %zu messages for chat %s", messages.size(), chatId.c_str());
  return true;
}

void BeeperMessageViewActivity::loop() {
  const int msgCount = static_cast<int>(messages.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onComplete();
    return;
  }
  // Scroll up (older messages)
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (scrollOffset > 0) { scrollOffset--; updateRequired = true; }
  }
  // Scroll down (newer messages)
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (scrollOffset < msgCount - 1) { scrollOffset++; updateRequired = true; }
  }
  // Long press Confirm to refresh
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() > 1500) {
    if (!refreshTriggered && !needsFetch) {
      refreshTriggered = true;
      isLoading = true;
      statusMessage = "Refreshing...";
      needsFetch = true;
    }
    return;
  }
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) refreshTriggered = false;
}

std::vector<std::string> BeeperMessageViewActivity::wrapText(const std::string& text, int fontId, int maxWidth) const {
  std::vector<std::string> lines;
  std::string currentLine;
  size_t pos = 0;
  while (pos < text.length()) {
    if (text[pos] == '\n') {
      lines.push_back(currentLine.empty() ? " " : std::move(currentLine));
      currentLine.clear();
      pos++; continue;
    }
    size_t wordEnd = pos;
    while (wordEnd < text.length() && text[wordEnd] != ' ' && text[wordEnd] != '\n') wordEnd++;
    size_t wordLen = wordEnd - pos;
    if (wordLen == 0) { pos++; continue; }
    if (currentLine.empty()) {
      currentLine.append(text, pos, wordLen);
    } else {
      std::string test = currentLine + ' ';
      test.append(text, pos, wordLen);
      if (renderer.getTextWidth(fontId, test.c_str()) <= maxWidth) {
        currentLine = std::move(test);
      } else {
        lines.push_back(std::move(currentLine));
        currentLine.clear();
        currentLine.append(text, pos, wordLen);
      }
    }
    pos = wordEnd;
    while (pos < text.length() && text[pos] == ' ') pos++;
  }
  if (!currentLine.empty()) lines.push_back(std::move(currentLine));
  return lines;
}

void BeeperMessageViewActivity::displayTaskLoop() {
  while (true) {
    if (needsFetch) {
      needsFetch = false;
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      {
        SemaphoreGuard guard(renderingMutex);
        render();
      }
      fetchFailed = !fetchMessages();
      if (fetchFailed && statusMessage == "Loading messages...") statusMessage = "Failed to load";
      isLoading = false;
      updateRequired = true;
    }
    if (updateRequired) {
      updateRequired = false;
      SemaphoreGuard guard(renderingMutex);
      render();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void BeeperMessageViewActivity::render() const {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int titleLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);

  // Header with chat title and separator
  auto header = renderer.truncatedText(UI_10_FONT_ID, chatTitle.c_str(), pageWidth - 40);
  renderer.drawCenteredText(UI_10_FONT_ID, 8, header.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 28, pageWidth, 28);

  if (isLoading || messages.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2,
                              messages.empty() && !isLoading ? "No messages" : statusMessage.c_str());
    renderer.displayBuffer();
    return;
  }

  // Scroll indicator in header area
  {
    char scrollInfo[16];
    snprintf(scrollInfo, sizeof(scrollInfo), "%d/%zu", scrollOffset + 1, messages.size());
    auto sw = renderer.getTextWidth(SMALL_FONT_ID, scrollInfo);
    renderer.drawText(SMALL_FONT_ID, pageWidth - sw - 8, 12, scrollInfo);
  }


  // Content area
  const int contentTop = 34;
  const int contentBottom = pageHeight - 4;
  const int contentHeight = contentBottom - contentTop;
  const int leftMargin = 12;
  const int rightMargin = 12;
  const int maxTextWidth = pageWidth - leftMargin - rightMargin;
  const int msgGap = 10;

  // Pre-compute visible messages working backwards from scrollOffset
  struct RenderedMsg {
    int msgIdx;
    std::vector<std::string> wrappedLines;
    int totalHeight;
  };
  std::vector<RenderedMsg> visible;
  int usedHeight = 0;

  for (int i = scrollOffset; i >= 0; i--) {
    const auto& msg = messages[static_cast<size_t>(i)];
    RenderedMsg rm;
    rm.msgIdx = i;
    rm.wrappedLines = wrapText(msg.text, UI_10_FONT_ID, maxTextWidth);
    rm.totalHeight = smallLineH + static_cast<int>(rm.wrappedLines.size()) * titleLineH + msgGap;
    if (usedHeight + rm.totalHeight > contentHeight && !visible.empty()) break;
    usedHeight += rm.totalHeight;
    visible.push_back(std::move(rm));
  }

  // Render top-to-bottom (oldest visible first)
  int yPos = contentBottom - usedHeight;
  for (int v = static_cast<int>(visible.size()) - 1; v >= 0; v--) {
    const auto& rm = visible[static_cast<size_t>(v)];
    const auto& msg = messages[static_cast<size_t>(rm.msgIdx)];

    // Sender line: bold name + dimmer timestamp
    std::string senderLine = msg.isSender ? "> You" : msg.senderName.empty() ? "Unknown" : msg.senderName;
    if (!msg.timestamp.empty()) senderLine += "  " + msg.timestamp;
    renderer.drawText(SMALL_FONT_ID, leftMargin, yPos, senderLine.c_str(), true, EpdFontFamily::BOLD);
    yPos += smallLineH;

    // Message text — indent "You" messages slightly for visual distinction
    const int textX = msg.isSender ? leftMargin + 10 : leftMargin;
    for (const auto& line : rm.wrappedLines) {
      renderer.drawText(UI_10_FONT_ID, textX, yPos, line.c_str());
      yPos += titleLineH;
    }

    // Thin separator between messages
    if (v > 0) {
      yPos += 2;
      renderer.drawLine(leftMargin, yPos, pageWidth - rightMargin, yPos);
      yPos += msgGap - 2;
    } else {
      yPos += msgGap;
    }
  }

  renderer.displayBuffer();
}
