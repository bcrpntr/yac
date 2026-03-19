#include "FlashcardReviewActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <esp_random.h>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/SemaphoreGuard.h"

namespace {
bool endsWith(const std::string& s, const char* suffix) {
  size_t sl = strlen(suffix);
  return s.size() >= sl && s.compare(s.size() - sl, sl, suffix) == 0;
}
void shuffleIndices(std::vector<size_t>& indices, uint32_t seed) {
  srand(seed);
  for (size_t i = indices.size() - 1; i > 0; i--) {
    size_t j = static_cast<size_t>(rand()) % (i + 1);
    std::swap(indices[i], indices[j]);
  }
}
}  // namespace

void FlashcardReviewActivity::taskTrampoline(void* param) {
  auto* self = static_cast<FlashcardReviewActivity*>(param);
  self->displayTaskLoop();
}

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  state = ReviewState::LOADING;
  currentCardIndex = 0;
  cardsReviewed = 0;
  dueCardIndices.clear();
  updateRequired = true;

  xTaskCreate(&FlashcardReviewActivity::taskTrampoline, "FCReviewTask", 8192, this, 1, &displayTaskHandle);
  loadDeck();
}

void FlashcardReviewActivity::onExit() {
  Activity::onExit();
  deck.saveProgress();

  if (state != ReviewState::SESSION_DONE && state != ReviewState::ERROR) {
    APP_STATE.flashcardSessionIndex = static_cast<uint32_t>(currentCardIndex);
    APP_STATE.flashcardSessionSize = static_cast<uint32_t>(originalSessionSize);
    APP_STATE.saveToFile();
  }

  {
    SemaphoreGuard guard(renderingMutex);
    if (displayTaskHandle) { vTaskDelete(displayTaskHandle); displayTaskHandle = nullptr; }
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

uint32_t FlashcardReviewActivity::getCurrentTimestamp() const {
  return APP_STATE.flashcardDayCounter;
}

void FlashcardReviewActivity::loadDeck() {
  bool loaded = false;
  if (endsWith(deckPath, ".xfc")) {
    loaded = deck.load(deckPath);
  } else if (endsWith(deckPath, ".txt")) {
    loaded = deck.loadTextFile(deckPath);
  } else {
    loaded = deck.load(deckPath);
  }

  if (!loaded) {
    state = ReviewState::ERROR;
    errorMessage = "Failed to load deck";
    updateRequired = true;
    return;
  }

  dueCardIndices = deck.getDueCardIndices(getCurrentTimestamp());
  if (dueCardIndices.empty()) {
    APP_STATE.flashcardSessionSize = 0;
    APP_STATE.flashcardSessionIndex = 0;
    state = ReviewState::SESSION_DONE;
    updateRequired = true;
    return;
  }

  if (dueCardIndices.size() > MAX_CARDS_PER_SESSION) {
    dueCardIndices.resize(MAX_CARDS_PER_SESSION);
  }

  bool resuming = false;
  uint32_t shuffleSeed = 0;

  if (APP_STATE.lastFlashcardDeck == deckPath && APP_STATE.flashcardSessionSize > 0 &&
      APP_STATE.flashcardSessionIndex < dueCardIndices.size() && APP_STATE.flashcardShuffleSeed != 0) {
    resuming = true;
    shuffleSeed = APP_STATE.flashcardShuffleSeed;
    currentCardIndex = APP_STATE.flashcardSessionIndex;
    originalSessionSize = APP_STATE.flashcardSessionSize;
  } else {
    shuffleSeed = static_cast<uint32_t>(millis()) ^ static_cast<uint32_t>(esp_random());
    currentCardIndex = 0;
    originalSessionSize = dueCardIndices.size();
  }

  shuffleIndices(dueCardIndices, shuffleSeed);
  state = ReviewState::SHOWING_FRONT;
  updateRequired = true;

  APP_STATE.lastFlashcardDeck = deckPath;
  APP_STATE.flashcardShuffleSeed = shuffleSeed;
  if (!resuming) {
    APP_STATE.flashcardSessionIndex = static_cast<uint32_t>(currentCardIndex);
    APP_STATE.flashcardSessionSize = static_cast<uint32_t>(originalSessionSize);
  }
  APP_STATE.saveToFile();
}

void FlashcardReviewActivity::showNextCard() {
  currentCardIndex++;
  if (currentCardIndex >= dueCardIndices.size()) { finishSession(); return; }
  APP_STATE.flashcardSessionIndex = static_cast<uint32_t>(currentCardIndex);
  APP_STATE.saveToFile();
  state = ReviewState::SHOWING_FRONT;
  updateRequired = true;
}

void FlashcardReviewActivity::revealAnswer() {
  state = ReviewState::SHOWING_BACK;
  updateRequired = true;
}

void FlashcardReviewActivity::rateCard(ReviewQuality quality) {
  size_t cardIdx = dueCardIndices[currentCardIndex];
  deck.reviewCard(cardIdx, quality, getCurrentTimestamp());
  cardsReviewed++;
  if (quality == ReviewQuality::AGAIN) { dueCardIndices.push_back(cardIdx); }
  showNextCard();
}

void FlashcardReviewActivity::finishSession() {
  deck.saveProgress();
  APP_STATE.flashcardSessionSize = 0;
  APP_STATE.flashcardSessionIndex = 0;
  APP_STATE.flashcardShuffleSeed = 0;
  APP_STATE.saveToFile();
  state = ReviewState::SESSION_DONE;
  updateRequired = true;
}

void FlashcardReviewActivity::loop() {
  switch (state) {
    case ReviewState::LOADING:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) onComplete();
      break;
    case ReviewState::ERROR:
    case ReviewState::SESSION_DONE:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasReleased(MappedInputManager::Button::Confirm)) onComplete();
      break;
    case ReviewState::SHOWING_FRONT:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) revealAnswer();
      else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finishSession();
      break;
    case ReviewState::SHOWING_BACK:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) rateCard(ReviewQuality::AGAIN);
      else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) rateCard(ReviewQuality::HARD);
      else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) rateCard(ReviewQuality::GOOD);
      else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) rateCard(ReviewQuality::EASY);
      break;
  }
}

std::vector<std::string> FlashcardReviewActivity::wrapText(const std::string& text, int fontId, int maxWidth) const {
  std::vector<std::string> lines;
  std::string currentLine;
  size_t pos = 0;
  while (pos < text.length()) {
    if (text[pos] == '\n') {
      if (!currentLine.empty()) { lines.push_back(std::move(currentLine)); currentLine.clear(); }
      pos++; continue;
    }
    size_t wordEnd = pos;
    while (wordEnd < text.length() && text[wordEnd] != ' ' && text[wordEnd] != '\n') wordEnd++;
    size_t wordLen = wordEnd - pos;
    if (wordLen == 0) { pos++; continue; }

    if (currentLine.empty()) {
      currentLine.append(text, pos, wordLen);
    } else {
      std::string testLine = currentLine + ' ';
      testLine.append(text, pos, wordLen);
      if (renderer.getTextWidth(fontId, testLine.c_str()) <= maxWidth) {
        currentLine = std::move(testLine);
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

void FlashcardReviewActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      SemaphoreGuard guard(renderingMutex);
      render();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void FlashcardReviewActivity::render() const {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto headerText = renderer.truncatedText(UI_10_FONT_ID, deck.getTitle().c_str(), pageWidth - 40);
  renderer.drawCenteredText(UI_10_FONT_ID, 10, headerText.c_str());

  if (state == ReviewState::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Loading...");
    renderer.displayBuffer();
    return;
  }
  if (state == ReviewState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "Error:");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    renderer.displayBuffer();
    return;
  }

  if (state == ReviewState::SESSION_DONE) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 30, "Session Complete!", true, EpdFontFamily::BOLD);
    char statsText[64];
    snprintf(statsText, sizeof(statsText), "Reviewed %zu cards", cardsReviewed);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, statsText);
    renderer.displayBuffer();
    return;
  }

  // Progress
  char progressText[32];
  snprintf(progressText, sizeof(progressText), "%zu / %zu", currentCardIndex + 1, originalSessionSize);
  const auto progressWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText);
  renderer.drawText(SMALL_FONT_ID, pageWidth - 20 - progressWidth, 10, progressText);

  const Flashcard& card = deck.getCards()[dueCardIndices[currentCardIndex]];
  constexpr int cardMargin = 30;
  constexpr int cardTop = 50;
  const int cardBottom = pageHeight - 80;
  const int cardHeight = cardBottom - cardTop;
  renderer.drawRect(cardMargin, cardTop, pageWidth - 2 * cardMargin, cardHeight);

  const char* label = (state == ReviewState::SHOWING_FRONT) ? "QUESTION" : "ANSWER";
  const std::string& text = (state == ReviewState::SHOWING_FRONT) ? card.front : card.back;
  renderer.drawCenteredText(SMALL_FONT_ID, cardTop + 10, label);

  auto lines = wrapText(text, UI_12_FONT_ID, pageWidth - 2 * cardMargin - 40);
  int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  int totalH = static_cast<int>(lines.size()) * lineHeight;
  int startY = cardTop + (cardHeight - totalH) / 2;
  for (size_t i = 0; i < lines.size() && i < 10; i++) {
    renderer.drawCenteredText(UI_12_FONT_ID, startY + static_cast<int>(i) * lineHeight, lines[i].c_str());
  }

  if (state == ReviewState::SHOWING_FRONT) {
    const auto labels = mappedInput.mapLabels("Finish", "Reveal", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels("Again", "Good", "Hard", "Easy");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
