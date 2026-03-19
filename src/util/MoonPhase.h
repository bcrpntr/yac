#pragma once

/**
 * Moon phase calculation using the synodic cycle.
 * Returns phase index (0-7) and descriptive name.
 */

enum MoonPhaseIndex {
  MOON_NEW = 0,
  MOON_WAXING_CRESCENT,
  MOON_FIRST_QUARTER,
  MOON_WAXING_GIBBOUS,
  MOON_FULL,
  MOON_WANING_GIBBOUS,
  MOON_LAST_QUARTER,
  MOON_WANING_CRESCENT,
};

struct MoonPhaseInfo {
  MoonPhaseIndex phase;
  const char* name;       // e.g. "Full Moon"
  const char* symbol;     // Unicode symbol e.g. "●" "◐" "○" "◑"
  float age;              // days into synodic cycle (0..29.53)
  float illumination;     // approximate 0.0..1.0
};

// Compute moon phase for a given date.
MoonPhaseInfo getMoonPhase(int day, int month, int year);
