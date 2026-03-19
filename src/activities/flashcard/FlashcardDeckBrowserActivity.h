#pragma once
#include <Flashcard.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <string>
#include <vector>

#include "../ActivityWithSubactivity.h"

class FlashcardDeckBrowserActivity final : public ActivityWithSubactivity {
 public:
  struct DeckInfo {
    std::string path;
    std::string title;
    size_t totalCards = 0;
    size_t dueCards = 0;
    size_t newCards = 0;
  };

  explicit FlashcardDeckBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : ActivityWithSubactivity("FlashcardDeckBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  std::vector<DeckInfo> decks;
  int selectorIndex = 0;
  bool isLoading = true;
  std::string statusMessage;
  bool dayAdvanceTriggered = false;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void scanForDecks();
  void scanDirectory(const std::string& path, bool includeTxt = false);
  void openDeck(const DeckInfo& deck);
  void onReviewComplete();
};
