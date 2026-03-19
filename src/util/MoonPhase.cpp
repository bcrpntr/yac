#include "MoonPhase.h"
#include <cmath>

// Synodic month length in days
static constexpr double SYNODIC_MONTH = 29.53058868;

// Reference new moon: January 6, 2000 18:14 UTC (Julian Day 2451550.26)
static constexpr double REF_NEW_MOON_JD = 2451550.26;

static double julianDay(int day, int month, int year) {
  if (month <= 2) { year--; month += 12; }
  int A = year / 100;
  int B = 2 - A + A / 4;
  return (int)(365.25 * (year + 4716)) + (int)(30.6001 * (month + 1)) + day + B - 1524.5;
}

MoonPhaseInfo getMoonPhase(int day, int month, int year) {
  double jd = julianDay(day, month, year);
  double daysSinceRef = jd - REF_NEW_MOON_JD;
  double cycles = daysSinceRef / SYNODIC_MONTH;
  double age = (cycles - floor(cycles)) * SYNODIC_MONTH;
  if (age < 0) age += SYNODIC_MONTH;

  // Illumination approximation: 0 at new, 1 at full
  double illum = (1.0 - cos(age / SYNODIC_MONTH * 2.0 * M_PI)) / 2.0;

  // Phase index: divide cycle into 8 equal segments
  int idx = (int)(age / SYNODIC_MONTH * 8.0 + 0.5) % 8;

  static const char* NAMES[] = {
    "New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
    "Full Moon", "Waning Gibbous", "Last Quarter", "Waning Crescent"
  };
  static const char* SYMBOLS[] = {
    "\xE2\x97\x8F",  // ● New (dark)
    "\xE2\x97\x91",  // ◑ Waxing Crescent (right-lit, approximation)
    "\xE2\x97\x90",  // ◐ First Quarter (left half lit)
    "\xE2\x97\x91",  // ◑ Waxing Gibbous
    "\xE2\x97\x8B",  // ○ Full (bright)
    "\xE2\x97\x90",  // ◐ Waning Gibbous
    "\xE2\x97\x91",  // ◑ Last Quarter (right half lit)
    "\xE2\x97\x90",  // ◐ Waning Crescent
  };

  MoonPhaseInfo info;
  info.phase = static_cast<MoonPhaseIndex>(idx);
  info.name = NAMES[idx];
  info.symbol = SYMBOLS[idx];
  info.age = (float)age;
  info.illumination = (float)illum;
  return info;
}
