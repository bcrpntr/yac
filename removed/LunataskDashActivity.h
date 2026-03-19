#pragma once
#include "../Activity.h"

#include <string>

#include "CrossPointSettings.h"

// Cached task counts parsed from Lunatask API /v1/tasks response.
// Names/notes are E2EE and not returned by the API, so we only track metadata.
struct LunataskData {
  // By status
  int later = 0;
  int next = 0;
  int started = 0;
  int waiting = 0;
  int completed = 0;

  // By priority (-2..2 mapped to indices 0..4)
  int priority[5] = {};

  // Eisenhower quadrants (1..4), index 0 = uncategorized
  int eisenhower[5] = {};

  // Scheduled today
  int scheduledToday = 0;

  // Completed in last 7 days
  int completedRecent = 0;

  // Total tasks
  int total = 0;

  // Last update time
  char lastUpdate[20] = "";
};

class LunataskDashActivity final : public Activity {
 public:
  enum State { WIFI_CONNECTING, FETCHING, DISPLAYING, FETCH_ERROR, NO_TOKEN };

  explicit LunataskDashActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Lunatask", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

  // Load cached data from SD for offline display
  static bool loadCache(LunataskData& out);

 private:
  static constexpr const char* CACHE_PATH = "/.crosspoint/lunatask_cache.json";

  State state = WIFI_CONNECTING;
  LunataskData data;
  std::string statusMessage;

  void onWifiConnected();
  void fetchTasks();
  bool parseTasks(const std::string& json);
  void saveCache();
  void renderDashboard();
  void renderStatusCounts(int x, int y, int w);
  void renderEisenhowerGrid(int x, int y, int cellW, int cellH);
  void renderPriorityBars(int x, int y, int w, int h);
};
