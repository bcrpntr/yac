#pragma once
#include <Flashcard.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

constexpr size_t MAX_CARDS_PER_SESSION = 20;

class FlashcardReviewActivity final : public Activity {
 public:
  enum class ReviewState {
    LOADING,
    SHOWING_FRONT,
    SHOWING_BACK,
    SESSION_DONE,
    ERROR
  };

  explicit FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::string& deckPath, const std::function<void()>& onComplete)
      : Activity("FlashcardReview", renderer, mappedInput), deckPath(deckPath), onComplete(onComplete) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  std::string deckPath;
  FlashcardDeck deck;
  std::vector<size_t> dueCardIndices;
  size_t currentCardIndex = 0;
  size_t cardsReviewed = 0;
  size_t originalSessionSize = 0;

  ReviewState state = ReviewState::LOADING;
  std::string errorMessage;

  const std::function<void()> onComplete;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

  void loadDeck();
  void showNextCard();
  void revealAnswer();
  void rateCard(ReviewQuality quality);
  void finishSession();
  uint32_t getCurrentTimestamp() const;
  std::vector<std::string> wrapText(const std::string& text, int fontId, int maxWidth) const;
};
