#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  std::string openEpubPath;
  uint8_t lastSleepImage = UINT8_MAX;  // UINT8_MAX = unset sentinel
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;

  // Flashcard spaced repetition state
  std::string lastFlashcardDeck;        // Path of last opened flashcard deck
  uint32_t flashcardDayCounter = 0;     // Day counter for SM-2 scheduling (no RTC needed)
  uint32_t flashcardSessionIndex = 0;   // Resume position in current session
  uint32_t flashcardSessionSize = 0;    // Total cards in current session
  uint32_t flashcardShuffleSeed = 0;    // Seed for deterministic shuffle (resume support)

  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
