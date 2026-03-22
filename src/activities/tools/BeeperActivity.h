#pragma once
#include "../Activity.h"
#include "util/ButtonNavigator.h"
#include <string>
#include <vector>

// Maximum chats to display
static constexpr int BEEPER_MAX_CHATS = 50;
// Maximum messages to fetch per chat
static constexpr int BEEPER_MAX_MESSAGES = 20;

struct BeeperChat {
  std::string id;
  std::string name;
  std::string network;
  int unreadCount = 0;
};

struct BeeperMessage {
  std::string senderName;
  std::string textContent;
  std::string timestamp;
};

/**
 * BeeperActivity — read-only interface to Beeper Desktop API.
 * Configure API URL in Settings → Beeper (web UI).
 * Lists chats, allows searching, and viewing recent messages.
 */
class BeeperActivity final : public Activity {
 public:
  explicit BeeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Beeper", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class State { LOADING, CHAT_LIST, CHAT_MESSAGES, SEARCH, ERROR };
  enum class FetchState { IDLE, FETCHING, DONE, FAILED };

  State state = State::LOADING;
  FetchState fetchState = FetchState::IDLE;
  ButtonNavigator buttonNavigator;

  std::vector<BeeperChat> chats;
  std::vector<BeeperMessage> messages;
  int selectorIndex = 0;
  int scrollTop = 0;
  std::string statusMessage;
  std::string currentChatId;
  std::string searchQuery;
  std::string apiBaseUrl;

  // Fetch chat list from Beeper API
  void fetchChats();
  // Fetch messages for a given chat ID
  void fetchMessages(const std::string& chatId);
  // Make HTTP GET request and return body as string
  std::string httpGet(const std::string& path);
  // Parse chat list from JSON response
  void parseChats(const std::string& json);
  // Parse messages from JSON response
  void parseMessages(const std::string& json);
  // Navigate up one level
  void goBack();
  // Ensure selected index is visible
  void ensureVisible(int visibleRows);

  const char* getApiBase() const;
};
