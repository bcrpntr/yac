#include "BeeperActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include "network/HttpDownloader.h"

#include "components/UITheme.h"
#include "CrossPointSettings.h"
#include "fontIds.h"

using namespace ArduinoJson;

static constexpr int VISIBLE_ROWS = 8;

const char* BeeperActivity::getApiBase() const {
  if (apiBaseUrl.empty()) {
    return SETTINGS.beeperApiUrl[0] ? SETTINGS.beeperApiUrl : "http://localhost:23373";
  }
  return apiBaseUrl.c_str();
}

void BeeperActivity::onEnter() {
  Activity::onEnter();
  state = State::LOADING;
  fetchState = FetchState::FETCHING;
  selectorIndex = 0;
  scrollTop = 0;
  chats.clear();
  messages.clear();
  currentChatId.clear();
  statusMessage = tr(STR_CONNECTING);
  requestUpdate();
  fetchChats();
}

void BeeperActivity::onExit() {
  chats.clear();
  messages.clear();
  Activity::onExit();
}

void BeeperActivity::fetchChats() {
  std::string response;
  std::string url = std::string(getApiBase()) + "/v1/chats/list?limit=50&include_muted=true";
  if (!HttpDownloader::fetchUrl(url, response)) {
    statusMessage = tr(STR_CONNECTION_FAILED);
    fetchState = FetchState::FAILED;
    state = State::ERROR;
    requestUpdate();
    return;
  }

  parseChats(response);
  if (chats.empty()) {
    statusMessage = tr(STR_BEEPER_NO_CHATS);
    fetchState = FetchState::DONE;
    state = State::ERROR;
  } else {
    fetchState = FetchState::DONE;
    state = State::CHAT_LIST;
    statusMessage.clear();
  }
  selectorIndex = 0;
  scrollTop = 0;
  requestUpdate();
}

void BeeperActivity::fetchMessages(const std::string& chatId) {
  std::string response;
  char url[256];
  snprintf(url, sizeof(url), "%s/v1/messages/list?chat_id=%s&limit=%d", getApiBase(), chatId.c_str(),
           BEEPER_MAX_MESSAGES);

  if (!HttpDownloader::fetchUrl(url, response)) {
    statusMessage = tr(STR_CONNECTION_FAILED);
    fetchState = FetchState::FAILED;
    requestUpdate();
    return;
  }

  parseMessages(response);
  fetchState = FetchState::DONE;
  selectorIndex = 0;
  scrollTop = 0;
  requestUpdate();
}

std::string BeeperActivity::httpGet(const std::string& path) {
  std::string response;
  std::string url = std::string(getApiBase()) + path;
  HttpDownloader::fetchUrl(url, response);
  return response;
}

void BeeperActivity::parseChats(const std::string& json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return;

  JsonArray items = doc["items"].as<JsonArray>();
  if (items.isNull()) return;

  chats.clear();
  for (const auto& item : items) {
    BeeperChat chat;
    chat.id = item["id"].as<std::string>();
    chat.name = item["name"].as<std::string>();
    chat.network = item["network"].as<std::string>();
    chat.unreadCount = item["unread_count"].as<int>();
    chats.push_back(chat);
  }
}

void BeeperActivity::parseMessages(const std::string& json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return;

  JsonArray items = doc["items"].as<JsonArray>();
  if (items.isNull()) return;

  messages.clear();
  for (const auto& item : items) {
    BeeperMessage msg;
    msg.senderName = item["sender_name"].as<std::string>();
    msg.textContent = item["text_content"].as<std::string>();
    msg.timestamp = item["timestamp"].as<std::string>();
    messages.push_back(msg);
  }
}

void BeeperActivity::goBack() {
  if (state == State::CHAT_MESSAGES) {
    messages.clear();
    state = State::CHAT_LIST;
    selectorIndex = 0;
    scrollTop = 0;
    requestUpdate();
  } else if (state == State::SEARCH) {
    state = State::CHAT_LIST;
    selectorIndex = 0;
    scrollTop = 0;
    requestUpdate();
  } else {
    finish();
  }
}

void BeeperActivity::ensureVisible(int visibleRows) {
  if (selectorIndex < scrollTop) scrollTop = selectorIndex;
  if (selectorIndex >= scrollTop + visibleRows) scrollTop = selectorIndex - visibleRows + 1;
}

void BeeperActivity::loop() {
  if (fetchState == FetchState::FETCHING) return;

  buttonNavigator.onNext([this] {
    if (state == State::CHAT_LIST && !chats.empty()) {
      selectorIndex = (selectorIndex + 1) % chats.size();
      ensureVisible(VISIBLE_ROWS);
      requestUpdate();
    } if (state == State::CHAT_MESSAGES && !messages.empty()) {
      selectorIndex = (selectorIndex + 1) % messages.size();
      ensureVisible(VISIBLE_ROWS);
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this] {
    if (state == State::CHAT_LIST && !chats.empty()) {
      selectorIndex = (selectorIndex - 1 + chats.size()) % chats.size();
      ensureVisible(VISIBLE_ROWS);
      requestUpdate();
    } else if (state == State::CHAT_MESSAGES && !messages.empty()) {
      selectorIndex = (selectorIndex - 1 + messages.size()) % messages.size();
      ensureVisible(VISIBLE_ROWS);
      requestUpdate();
    }
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (state == State::CHAT_LIST && !chats.empty()) {
      currentChatId = chats[selectorIndex].id;
      state = State::CHAT_MESSAGES;
      fetchState = FetchState::FETCHING;
      statusMessage = tr(STR_CONNECTING);
      requestUpdate();
      fetchMessages(currentChatId);
    }
  }
}

void BeeperActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int rowHeight = 36;
  const int visibleRows = (pageHeight - metrics.topPadding - metrics.headerHeight - metrics.buttonHintsHeight) / rowHeight;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BEEPER));

  if (fetchState == FetchState::FETCHING) {
    int tw = renderer.getTextWidth(UI_10_FONT_ID, statusMessage.c_str());
    renderer.drawText(UI_10_FONT_ID, (pageWidth - tw) / 2, pageHeight / 2, statusMessage.c_str(), true);
    renderer.displayBuffer();
    return;
  }

  if (state == State::ERROR) {
    int tw = renderer.getTextWidth(UI_12_FONT_ID, statusMessage.c_str());
    renderer.drawText(UI_12_FONT_ID, (pageWidth - tw) / 2, pageHeight / 2, statusMessage.c_str(), true);
    const char* lbl = "[Back]";
    int lw = renderer.getTextWidth(SMALL_FONT_ID, lbl);
    renderer.drawText(SMALL_FONT_ID, (pageWidth - lw) / 2, pageHeight / 2 + 30, lbl, true);
    renderer.displayBuffer();
    return;
  }

  if (state == State::CHAT_LIST) {
    // Draw chat list
    int y = metrics.topPadding + metrics.headerHeight + 8;
    int count = 0;
    for (size_t i = scrollTop; i < chats.size() && count < visibleRows; i++, count++) {
      const auto& chat = chats[i];
      bool selected = (int)i == selectorIndex;

      if (selected) {
        renderer.fillRect(0, y, pageWidth, rowHeight, true);
      }

      // Network badge
      renderer.drawText(SMALL_FONT_ID, 4, y + 4, chat.network.c_str(), false, EpdFontFamily::BOLD);

      // Chat name
      auto name = renderer.truncatedText(UI_12_FONT_ID, chat.name.c_str(), pageWidth - 80);
      renderer.drawText(UI_12_FONT_ID, 4, y + 18, name.c_str(), selected);

      // Unread badge
      if (chat.unreadCount > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "(%d)", chat.unreadCount);
        int bw = renderer.getTextWidth(UI_10_FONT_ID, buf);
        renderer.drawText(UI_10_FONT_ID, pageWidth - bw - 4, y + 4, buf, selected);
      }

      y += rowHeight;
    }

    // Scroll indicator
    if (chats.size() > (size_t)visibleRows) {
      int indicatorH = visibleRows * rowHeight;
      int scrollY = metrics.topPadding + metrics.headerHeight + 8 + (scrollTop * indicatorH / chats.size());
      renderer.fillRect(pageWidth - 4, scrollY, 4, indicatorH / chats.size(), false);
    }

  } else if (state == State::CHAT_MESSAGES) {
    // Draw message list
    int y = metrics.topPadding + metrics.headerHeight + 8;
    int count = 0;
    for (size_t i = scrollTop; i < messages.size() && count < visibleRows; i++, count++) {
      const auto& msg = messages[i];
      bool selected = (int)i == selectorIndex;

      if (selected) {
        renderer.fillRect(0, y, pageWidth, rowHeight, true);
      }

      // Sender + time
      char meta[128];
      snprintf(meta, sizeof(meta), "%s  %s", msg.senderName.c_str(), msg.timestamp.c_str());
      auto metaStr = renderer.truncatedText(SMALL_FONT_ID, meta, pageWidth - 8);
      renderer.drawText(SMALL_FONT_ID, 4, y + 2, metaStr.c_str(), selected);

      // Message text
      auto text = renderer.truncatedText(UI_10_FONT_ID, msg.textContent.c_str(), pageWidth - 8);
      renderer.drawText(UI_10_FONT_ID, 4, y + 18, text.c_str(), selected);

      y += rowHeight;
    }

    if (messages.empty()) {
      const char* noMsg = tr(STR_BEEPER_NO_MESSAGES);
      int tw = renderer.getTextWidth(UI_10_FONT_ID, noMsg);
      renderer.drawText(UI_10_FONT_ID, (pageWidth - tw) / 2, pageHeight / 2, noMsg, true);
    }
  }

  // Button hints
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
