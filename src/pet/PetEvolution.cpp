#include "PetEvolution.h"

#include <I18n.h>
#include <Logging.h>

namespace {

// Determine reading-based variant at an evolution branching point.
// Returns 0=Scholar, 1=Balanced, 2=Wild.
// Called after state.stage is already advanced; stageIdx = index of the new stage.
static uint8_t determineVariant(const PetState& state) {
  // BUGFIX: use stageIdx (the new stage being entered) to look up the evolution
  // requirement, not prevStageIdx (the previous stage). prevStageIdx was incorrect
  // for HATCHLING (stageIdx=1): EVOLUTION[0] vs EVOLUTION[1].
  uint8_t stageIdx = static_cast<uint8_t>(state.stage);
  uint16_t minPages = PetConfig::EVOLUTION[stageIdx].minPages;
  uint16_t scholarThreshold = minPages + (minPages / 2);  // 1.5x

  // Scholar: active reader with streaks and at least one book finished
  if (state.currentStreak >= 7 &&
      state.booksFinished >= 1 &&
      state.totalPagesRead >= scholarThreshold) {
    return 0;  // Scholar
  }

  // Wild: barely reading, no streak maintenance
  if (state.currentStreak < 3 &&
      state.totalPagesRead <= minPages + 50) {
    return 2;  // Wild
  }

  return 1;  // Balanced (default)
}

}  // namespace

namespace PetEvolution {

void checkEvolution(PetState& state) {
  uint8_t stageIdx = static_cast<uint8_t>(state.stage);
  if (stageIdx >= static_cast<uint8_t>(PetStage::ELDER)) return;

  const auto& req = PetConfig::EVOLUTION[stageIdx];
  if (state.daysAtStage < req.minDays ||
      state.totalPagesRead < req.minPages ||
      state.hunger < req.minAvgHunger) return;

  // Additional reading gate for Companion → Elder
  if (stageIdx == 3) {
    if (state.currentStreak < 7 || state.booksFinished < 1) return;
  }

  // Advance to next stage
  state.stage = static_cast<PetStage>(stageIdx + 1);
  state.daysAtStage = 0;

  // Assign variant at branching stages (YOUNGSTER and COMPANION)
  if (state.stage == PetStage::YOUNGSTER || state.stage == PetStage::COMPANION) {
    state.evolutionVariant = determineVariant(state);
  }

  LOG_DBG("PET", "Evolved to stage %d variant %d", (int)state.stage, state.evolutionVariant);
}

const char* variantStageName(PetStage stage, uint8_t variant) {
  switch (stage) {
    case PetStage::YOUNGSTER:
      if (variant == 0) return tr(STR_PET_STAGE_SCHOLARLY_YOUNG);
      if (variant == 2) return tr(STR_PET_STAGE_WILD_YOUNGSTER);
      return tr(STR_PET_STAGE_YOUNGSTER);
    case PetStage::COMPANION:
      if (variant == 0) return tr(STR_PET_STAGE_SCHOLAR);
      if (variant == 2) return tr(STR_PET_STAGE_WILD_COMPANION);
      return tr(STR_PET_STAGE_COMPANION);
    case PetStage::EGG:       return tr(STR_PET_STAGE_EGG);
    case PetStage::HATCHLING: return tr(STR_PET_STAGE_HATCHLING);
    case PetStage::ELDER:     return tr(STR_PET_STAGE_ELDER);
    default:                  return "???";
  }
}

const char* typeName(uint8_t type) {
  switch (type) {
    case 0: return tr(STR_PET_TYPE_DEFAULT);
    case 1: return tr(STR_PET_TYPE_CAT);
    case 2: return tr(STR_PET_TYPE_DOG);
    case 3: return tr(STR_PET_TYPE_DRAGON);
    case 4: return tr(STR_PET_TYPE_BUNNY);
    default: return tr(STR_PET_TYPE_DEFAULT);
  }
}

}  // namespace PetEvolution
