#include "ToolsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "ClockActivity.h"
#include "PomodoroActivity.h"
#include "TwentyFortyEightActivity.h"
#include "MinesweeperActivity.h"
#include "SudokuActivity.h"
#include "CaroActivity.h"
#include "ChessActivity.h"
#include "VirtualPetActivity.h"
#include "WeatherActivity.h"
#include "ReadingStatsActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/beeper/BeeperChatListActivity.h"
#include "activities/flashcard/FlashcardDeckBrowserActivity.h"
#include "components/UITheme.h"
#include "CrossPointSettings.h"
#include "fontIds.h"

static constexpr int BASE_MENU_COUNT = 11;

int ToolsActivity::getMenuCount() const {
  return BASE_MENU_COUNT
         + (SETTINGS.opdsServerUrl[0] ? 1 : 0)
         + (SETTINGS.beeperApiUrl[0] ? 1 : 0);
}

void ToolsActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  requestUpdate();
}

void ToolsActivity::loop() {
  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, getMenuCount());
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, getMenuCount());
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (selectorIndex) {
      case 0: activityManager.pushActivity(std::make_unique<ClockActivity>(renderer, mappedInput)); break;
      case 1: activityManager.pushActivity(std::make_unique<WeatherActivity>(renderer, mappedInput)); break;
      case 2: activityManager.pushActivity(std::make_unique<PomodoroActivity>(renderer, mappedInput)); break;
      case 3: activityManager.pushActivity(std::make_unique<VirtualPetActivity>(renderer, mappedInput)); break;
      case 4: activityManager.pushActivity(std::make_unique<ReadingStatsActivity>(renderer, mappedInput)); break;
      case 5: activityManager.pushActivity(std::make_unique<FlashcardDeckBrowserActivity>(renderer, mappedInput)); break;
      default: {
        int dynIdx = 6;
        if (SETTINGS.opdsServerUrl[0]) {
          if (selectorIndex == dynIdx) {
            activityManager.pushActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput));
            break;
          }
          dynIdx++;
        }
        if (SETTINGS.beeperApiUrl[0]) {
          if (selectorIndex == dynIdx) {
            activityManager.pushActivity(std::make_unique<BeeperChatListActivity>(renderer, mappedInput));
            break;
          }
          dynIdx++;
        }
        int gameIdx = selectorIndex - dynIdx;
        switch (gameIdx) {
          case 0: activityManager.pushActivity(std::make_unique<ChessActivity>(renderer, mappedInput)); break;
          case 1: activityManager.pushActivity(std::make_unique<CaroActivity>(renderer, mappedInput)); break;
          case 2: activityManager.pushActivity(std::make_unique<SudokuActivity>(renderer, mappedInput)); break;
          case 3: activityManager.pushActivity(std::make_unique<MinesweeperActivity>(renderer, mappedInput)); break;
          case 4: activityManager.pushActivity(std::make_unique<TwentyFortyEightActivity>(renderer, mappedInput)); break;
        }
        break;
      }
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void ToolsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TOOLS));

  const char* baseLabels[] = {
      tr(STR_CLOCK), tr(STR_WEATHER), tr(STR_POMODORO), tr(STR_VIRTUAL_PET),
      tr(STR_READING_STATS_APP), "Flashcards"};
  const char* gameLabels[] = {
      tr(STR_CHESS), tr(STR_CARO), tr(STR_SUDOKU), tr(STR_MINESWEEPER), tr(STR_2048)};
  const bool hasOpds = SETTINGS.opdsServerUrl[0] != '\0';
  const bool hasBeeper = SETTINGS.beeperApiUrl[0] != '\0';
  const int menuCount = getMenuCount();

  const int menuTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int menuHeight = pageHeight - menuTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(renderer, Rect{0, menuTop, pageWidth, menuHeight}, menuCount, selectorIndex,
               [&](int index) -> std::string {
                 if (index < 6) return baseLabels[index];
                 int dynIdx = 6;
                 if (hasOpds) {
                   if (index == dynIdx) return tr(STR_OPDS_BROWSER);
                   dynIdx++;
                 }
                 if (hasBeeper) {
                   if (index == dynIdx) return "Beeper";
                   dynIdx++;
                 }
                 int gameIdx = index - dynIdx;
                 if (gameIdx >= 0 && gameIdx < 5) return gameLabels[gameIdx];
                 return "";
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
