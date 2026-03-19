#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <functional>
#include <string>
#include <vector>
#include "../Activity.h"

struct BeeperMessage {
  std::string senderName;
  std::string text;
  std::string timestamp;
  bool isSender = false;  // true if sent by the authenticated user
};

class BeeperMessageViewActivity final : public Activity {
 public:
  explicit BeeperMessageViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::string& chatId, const std::string& chatTitle,
                                     const std::function<void()>& onComplete)
      : Activity("BeeperMessageView", renderer, mappedInput),
        chatId(chatId), chatTitle(chatTitle), onComplete(onComplete) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  std::string chatId;
  std::string chatTitle;
  std::vector<BeeperMessage> messages;
  int scrollOffset = 0;  // Index of first visible message
  bool isLoading = true;
  bool fetchFailed = false;
  bool needsFetch = false;
  bool refreshTriggered = false;
  std::string statusMessage;
  const std::function<void()> onComplete;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  bool fetchMessages();
  std::vector<std::string> wrapText(const std::string& text, int fontId, int maxWidth) const;
};
