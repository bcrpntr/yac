#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// SM-2 spaced repetition quality ratings
enum class ReviewQuality : uint8_t {
  AGAIN = 0,  // Complete failure — reset interval
  HARD = 1,   // Correct but with difficulty
  GOOD = 2,   // Correct with some effort
  EASY = 3,   // Effortless recall
};

// A single flashcard with front/back text and SM-2 scheduling state
struct Flashcard {
  std::string front;
  std::string back;

  // SM-2 scheduling state
  float easeFactor = 2.5f;    // EF (minimum 1.3)
  uint16_t interval = 0;      // Days until next review (0 = new/unseen)
  uint8_t repetitions = 0;    // Consecutive correct reviews
  uint32_t nextReviewDay = 0; // Day counter when card becomes due (0 = new)
};

// A deck of flashcards with file I/O and spaced repetition scheduling
class FlashcardDeck {
 public:
  FlashcardDeck() = default;

  // Load deck from tab-separated text file (front\tback per line)
  // First line can be "# Title" to set deck title
  bool loadTextFile(const std::string& path);

  // Load/save native .xfc format (includes progress)
  bool load(const std::string& path);
  bool save(const std::string& path) const;

  // Load Anki .apkg (stub — not implemented, returns false)
  bool loadAnki(const std::string& path) { (void)path; return false; }

  // Save progress to .xfc file alongside the source file
  bool saveProgress() const;

  // Review a card with given quality rating at the given day
  void reviewCard(size_t cardIndex, ReviewQuality quality, uint32_t currentDay);

  // Get indices of cards due for review (nextReviewDay <= currentDay, or new cards)
  std::vector<size_t> getDueCardIndices(uint32_t currentDay) const;

  // Accessors
  const std::string& getTitle() const { return title; }
  const std::vector<Flashcard>& getCards() const { return cards; }
  size_t getCardCount() const { return cards.size(); }
  size_t getDueCount(uint32_t currentDay) const;
  size_t getNewCount() const;

 private:
  std::string title = "Untitled";
  std::string sourcePath;  // Original file path (for deriving progress file path)
  std::vector<Flashcard> cards;

  std::string getProgressPath() const;
  bool loadProgress();
  bool saveProgressFile(const std::string& path) const;
};
