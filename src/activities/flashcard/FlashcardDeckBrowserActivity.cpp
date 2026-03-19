#include "FlashcardDeckBrowserActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointState.h"
#include "FlashcardReviewActivity.h"
#include "MappedInputManager.h"
#include "fontIds.h"
#include "util/SemaphoreGuard.h"

namespace {
constexpr int PAGE_ITEMS = 8;
bool endsWith(const std::string& s, const char* suffix) {
  size_t sl = strlen(suffix);
  return s.size() >= sl && s.compare(s.size() - sl, sl, suffix) == 0;
}
}  // namespace

void FlashcardDeckBrowserActivity::taskTrampoline(void* param) {
  auto* self = static_cast<FlashcardDeckBrowserActivity*>(param);
  self->displayTaskLoop();
}

void FlashcardDeckBrowserActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  selectorIndex = 0;
  isLoading = true;
  statusMessage = "Scanning...";
  decks.clear();
  updateRequired = true;

  xTaskCreate(&FlashcardDeckBrowserActivity::taskTrampoline, "FCBrowserTask", 8192, this, 1, &displayTaskHandle);
  scanForDecks();
}

void FlashcardDeckBrowserActivity::onExit() {
  ActivityWithSubactivity::onExit();
  {
    SemaphoreGuard guard(renderingMutex);
    if (displayTaskHandle) { vTaskDelete(displayTaskHandle); displayTaskHandle = nullptr; }
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  decks.clear();
}

void FlashcardDeckBrowserActivity::scanForDecks() {
  decks.clear();
  scanDirectory("/flashcards", true);
  isLoading = false;
  if (decks.empty()) statusMessage = "No decks found\nPut .txt files in /flashcards";
  updateRequired = true;
}

void FlashcardDeckBrowserActivity::scanDirectory(const std::string& path, bool includeTxt) {
  FsFile dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  dir.rewindDirectory();

  char name[128];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    if (!entry.isDirectory()) {
      std::string filename(name);
      bool isXfc = endsWith(filename, ".xfc");
      bool isTxt = includeTxt && endsWith(filename, ".txt");
      if (isXfc || isTxt) {
        std::string fullPath = path;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += name;
        FlashcardDeck tmpDeck;
        bool loaded = isXfc ? tmpDeck.load(fullPath) : tmpDeck.loadTextFile(fullPath);
        if (loaded) {
          DeckInfo info;
          info.path = fullPath;
          info.title = tmpDeck.getTitle();
          info.totalCards = tmpDeck.getCardCount();
          info.dueCards = tmpDeck.getDueCount(APP_STATE.flashcardDayCounter);
          info.newCards = tmpDeck.getNewCount();
          decks.push_back(std::move(info));
        }
      }
    }
    entry.close();
  }
  dir.close();
}

void FlashcardDeckBrowserActivity::loop() {
  if (subActivity) { subActivity->loop(); return; }
  if (isLoading) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
    return;
  }

  const int itemCount = static_cast<int>(decks.size());

  // Long press Right to advance day counter
  if (mappedInput.isPressed(MappedInputManager::Button::Right) && mappedInput.getHeldTime() > 1500) {
    if (!dayAdvanceTriggered) {
      dayAdvanceTriggered = true;
      APP_STATE.flashcardDayCounter++;
      APP_STATE.saveToFile();
      isLoading = true;
      statusMessage = "Day advanced! Updating...";
      updateRequired = true;
      scanForDecks();
    }
    return;
  }
  if (!mappedInput.isPressed(MappedInputManager::Button::Right)) dayAdvanceTriggered = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!decks.empty()) openDeck(decks[static_cast<size_t>(selectorIndex)]);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }

  // Navigation
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (itemCount > 0) { selectorIndex = (selectorIndex + itemCount - 1) % itemCount; updateRequired = true; }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (itemCount > 0) { selectorIndex = (selectorIndex + 1) % itemCount; updateRequired = true; }
  }
}

void FlashcardDeckBrowserActivity::openDeck(const DeckInfo& dk) {
  SemaphoreGuard guard(renderingMutex);
  exitActivity();
  enterNewActivity(new FlashcardReviewActivity(renderer, mappedInput, dk.path, [this] { onReviewComplete(); }));
}

void FlashcardDeckBrowserActivity::onReviewComplete() {
  exitActivity();
  isLoading = true;
  statusMessage = "Updating...";
  updateRequired = true;
  scanForDecks();
}

void FlashcardDeckBrowserActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      SemaphoreGuard guard(renderingMutex);
      render();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void FlashcardDeckBrowserActivity::render() const {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  char headerText[48];
  snprintf(headerText, sizeof(headerText), "Flashcards  (Day %u)", APP_STATE.flashcardDayCounter);
  renderer.drawCenteredText(UI_10_FONT_ID, 10, headerText, true, EpdFontFamily::BOLD);

  if (isLoading || decks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    renderer.displayBuffer();
    return;
  }

  const int listTop = 40;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 16;
  const int pageStart = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;

  for (size_t i = static_cast<size_t>(pageStart);
       i < decks.size() && i < static_cast<size_t>(pageStart + PAGE_ITEMS); i++) {
    const int yPos = listTop + static_cast<int>(i % PAGE_ITEMS) * lineH;
    const bool isSelected = (static_cast<int>(i) == selectorIndex);
    const auto& dk = decks[i];

    if (isSelected) renderer.fillRect(0, yPos - 2, pageWidth - 1, lineH);

    auto titleText = renderer.truncatedText(UI_10_FONT_ID, dk.title.c_str(), pageWidth - 130);
    renderer.drawText(UI_10_FONT_ID, 15, yPos, titleText.c_str(), !isSelected);

    char stats[32];
    if (dk.dueCards > 0)
      snprintf(stats, sizeof(stats), "%zu due", dk.dueCards);
    else
      snprintf(stats, sizeof(stats), "%zu total", dk.totalCards);
    auto sw = renderer.getTextWidth(SMALL_FONT_ID, stats);
    renderer.drawText(SMALL_FONT_ID, pageWidth - 15 - sw, yPos + 4, stats, !isSelected);
  }

  renderer.displayBuffer();
}
