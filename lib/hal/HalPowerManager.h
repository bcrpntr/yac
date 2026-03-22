#pragma once

#include <Arduino.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode

 public:
  static constexpr int LOW_POWER_FREQ = 10;                    // MHz
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the currentLockMode
  // keepClockAlive: when true, GPIO13 stays HIGH so RTC/LP timer keep running (costs ~3-4mA on battery)
  // timerWakeMinutes: if > 0 and keepClockAlive, enables periodic timer wakeup for sleep screen refresh
  //
  // === Deep Sleep Architecture ===
  //
  // Sleep entry (enterDeepSleep in main.cpp):
  //   1. APP_STATE.lastSleepFromReader is saved to SD
  //   2. SleepActivity renders the sleep screen (if keepClockAlive=true, screen stays powered)
  //   3. display.deepSleep() puts the panel in low-power mode
  //   4. RTC time snapshot saved: g_unixBeforeSleep + esp_clk_rtc_time() → g_rtcUsBeforeSleep
  //   5. Clock also persisted to SD as fallback (RTC_DATA_ATTR can be lost on some ESP32-C3 chips)
  //   6. GPIO13 (SPIWP) set HIGH if keepClockAlive, LOW otherwise
  //      - keepClockAlive=HIGH: MCU stays powered (~3-4mA), RTC keeps time, timer wake works
  //      - keepClockAlive=LOW:  MCU fully powered off (~$5µA), power button physically repowers device
  //
  // RTC persistence across deep sleep:
  //   - ESP32's RTC is NOT reset by deep sleep. RTC_DATA_ATTR variables and the LP core retain state.
  //   - esp_clk_rtc_time() continues counting through deep sleep (LP oscillator).
  //   - On wake: g_rtcUsBeforeSleep is subtracted from current esp_clk_rtc_time() to get elapsed µs,
  //     which is added to g_unixBeforeSleep to restore wall-clock time (accounting for sleep duration).
  //
  // Wake sources:
  //   - Power button (GPIO wakeup): user presses button → device powers on → normal boot → verifyPowerButtonDuration()
  //   - Timer wake (keepClockAlive only): periodic wake for CLOCK/READING_STATS screen refresh,
  //     then immediately re-enters deep sleep. Full boot sequence skipped.
  //   - USB power detection: if USB powered a cold boot, immediately re-enters deep sleep.
  //
  // Power button wake (without full reboot):
  //   - When keepClockAlive=false, MCU is fully powered off during sleep. Power button physically
  //     repowers the device (no wake-from-sleep, a full cold boot). After verifying power button
  //     duration, device resumes reading or returns to sleep.
  //   - When keepClockAlive=true, MCU stays powered. Power button triggers GPIO wakeup from deep sleep.
  void startDeepSleep(HalGPIO& gpio, bool keepClockAlive = false, uint32_t timerWakeMinutes = 0) const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
