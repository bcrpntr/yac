#include "Flashcard.h"
#include <HalStorage.h>
#include <Logging.h>
#include <ArduinoJson.h>
#include <cstring>
#include <algorithm>

// ── SM-2 Algorithm ────────────────────────────────────────────────────────────

void FlashcardDeck::reviewCard(size_t cardIndex, ReviewQuality quality, uint32_t currentDay) {
  if (cardIndex >= cards.size()) return;
  Flashcard& c = cards[cardIndex];

  int q = static_cast<int>(quality);

  if (q < 2) {
    // Failed — reset
    c.repetitions = 0;
    c.interval = 1;
  } else {
    if (c.repetitions == 0) {
      c.interval = 1;
    } else if (c.repetitions == 1) {
      c.interval = 6;
    } else {
      c.interval = static_cast<uint16_t>(c.interval * c.easeFactor);
      if (c.interval < 1) c.interval = 1;
    }
    c.repetitions++;
  }

  // Update ease factor: EF' = EF + (0.1 - (3 - q) * (0.08 + (3 - q) * 0.02))
  // Mapped from SM-2's 0-5 scale to our 0-3 scale (multiply q by 5/3)
  float q5 = q * 5.0f / 3.0f;  // Map 0-3 to 0-5 range
  c.easeFactor += (0.1f - (5.0f - q5) * (0.08f + (5.0f - q5) * 0.02f));
  if (c.easeFactor < 1.3f) c.easeFactor = 1.3f;

  c.nextReviewDay = currentDay + c.interval;
}

// ── Due card queries ──────────────────────────────────────────────────────────

std::vector<size_t> FlashcardDeck::getDueCardIndices(uint32_t currentDay) const {
  std::vector<size_t> due;
  for (size_t i = 0; i < cards.size(); i++) {
    if (cards[i].nextReviewDay <= currentDay) {
      due.push_back(i);
    }
  }
  return due;
}

size_t FlashcardDeck::getDueCount(uint32_t currentDay) const {
  size_t count = 0;
  for (const auto& c : cards) {
    if (c.nextReviewDay <= currentDay) count++;
  }
  return count;
}

size_t FlashcardDeck::getNewCount() const {
  size_t count = 0;
  for (const auto& c : cards) {
    if (c.repetitions == 0) count++;
  }
  return count;
}

// ── Text file loading (tab-separated: front\tback) ───────────────────────────

bool FlashcardDeck::loadTextFile(const std::string& path) {
  sourcePath = path;
  cards.clear();

  FsFile file;
  if (!Storage.openFileForRead("FC", path, file)) {
    LOG_ERR("FC", "Failed to open: %s", path.c_str());
    return false;
  }

  // Derive title from filename
  size_t slashPos = path.rfind('/');
  size_t dotPos = path.rfind('.');
  if (slashPos != std::string::npos && dotPos != std::string::npos && dotPos > slashPos) {
    title = path.substr(slashPos + 1, dotPos - slashPos - 1);
  } else {
    title = path;
  }

  char line[512];
  while (file.available()) {
    int len = 0;
    while (file.available() && len < (int)sizeof(line) - 1) {
      char ch = file.read();
      if (ch == '\n') break;
      if (ch == '\r') continue;
      line[len++] = ch;
    }
    line[len] = '\0';

    if (len == 0) continue;

    // Skip comments and title lines
    if (line[0] == '#') {
      if (cards.empty()) {
        // Use first comment as title
        const char* t = line + 1;
        while (*t == ' ') t++;
        if (*t) title = t;
      }
      continue;
    }

    // Find tab separator
    char* tab = strchr(line, '\t');
    if (!tab) continue;  // Skip lines without tab

    *tab = '\0';
    Flashcard card;
    card.front = line;
    card.back = tab + 1;
    if (!card.front.empty() && !card.back.empty()) {
      cards.push_back(std::move(card));
    }
  }
  file.close();

  LOG_DBG("FC", "Loaded %zu cards from %s", cards.size(), path.c_str());

  // Try to load saved progress
  loadProgress();
  return !cards.empty();
}

// ── Native .xfc format (JSON with progress) ──────────────────────────────────

bool FlashcardDeck::load(const std::string& path) {
  sourcePath = path;
  String content = Storage.readFile(path.c_str());
  if (content.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, content)) return false;

  title = doc["title"] | "Untitled";
  cards.clear();

  JsonArray arr = doc["cards"];
  for (JsonObject obj : arr) {
    Flashcard c;
    c.front = obj["f"] | "";
    c.back = obj["b"] | "";
    c.easeFactor = obj["ef"] | 2.5f;
    c.interval = obj["iv"] | (uint16_t)0;
    c.repetitions = obj["rp"] | (uint8_t)0;
    c.nextReviewDay = obj["nr"] | (uint32_t)0;
    if (!c.front.empty()) cards.push_back(std::move(c));
  }

  LOG_DBG("FC", "Loaded .xfc: %zu cards from %s", cards.size(), path.c_str());
  return !cards.empty();
}

bool FlashcardDeck::save(const std::string& path) const {
  return saveProgressFile(path);
}

// ── Progress persistence ──────────────────────────────────────────────────────

std::string FlashcardDeck::getProgressPath() const {
  // Store progress as .xfc next to the source file
  size_t dotPos = sourcePath.rfind('.');
  if (dotPos != std::string::npos) {
    return sourcePath.substr(0, dotPos) + ".xfc";
  }
  return sourcePath + ".xfc";
}

bool FlashcardDeck::saveProgress() const {
  if (sourcePath.empty() || cards.empty()) return false;
  std::string progPath = getProgressPath();
  // If source is already .xfc, save in place
  if (sourcePath == progPath) {
    return saveProgressFile(sourcePath);
  }
  return saveProgressFile(progPath);
}

bool FlashcardDeck::saveProgressFile(const std::string& path) const {
  JsonDocument doc;
  doc["title"] = title;
  JsonArray arr = doc["cards"].to<JsonArray>();
  for (const auto& c : cards) {
    JsonObject obj = arr.add<JsonObject>();
    obj["f"] = c.front;
    obj["b"] = c.back;
    obj["ef"] = c.easeFactor;
    obj["iv"] = c.interval;
    obj["rp"] = c.repetitions;
    obj["nr"] = c.nextReviewDay;
  }
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path.c_str(), json);
}

bool FlashcardDeck::loadProgress() {
  std::string progPath = getProgressPath();
  if (progPath == sourcePath) return false;  // Already loaded as .xfc
  if (!Storage.exists(progPath.c_str())) return false;

  String content = Storage.readFile(progPath.c_str());
  if (content.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, content)) return false;

  JsonArray arr = doc["cards"];
  if (arr.isNull()) return false;

  // Match progress by index (assumes card order hasn't changed)
  size_t count = std::min(cards.size(), (size_t)arr.size());
  for (size_t i = 0; i < count; i++) {
    JsonObject obj = arr[i];
    cards[i].easeFactor = obj["ef"] | 2.5f;
    cards[i].interval = obj["iv"] | (uint16_t)0;
    cards[i].repetitions = obj["rp"] | (uint8_t)0;
    cards[i].nextReviewDay = obj["nr"] | (uint32_t)0;
  }

  LOG_DBG("FC", "Loaded progress for %zu cards from %s", count, progPath.c_str());
  return true;
}
