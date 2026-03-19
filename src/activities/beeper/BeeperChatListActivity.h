#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string>
#include <vector>
#include "../ActivityWithSubactivity.h"

struct BeeperChat {
  std::string id;
  std::string title;
  std::string previewText;
  std::string previewSender;
  int unreadCount = 0;
};

class BeeperChatListActivity final : public ActivityWithSubactivity {
 public:
  explicit BeeperChatListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : ActivityWithSubactivity("BeeperChatList", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  std::vector<BeeperChat> chats;
  int selectorIndex = 0;
  bool isLoading = true;
  bool fetchFailed = false;
  bool refreshTriggered = false;
  bool needsFetch = false;
  std::string statusMessage;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  bool fetchChats();
  void openChat(const BeeperChat& chat);
  void onMessageViewComplete();
};
