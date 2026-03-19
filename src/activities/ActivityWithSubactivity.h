#pragma once
#include "Activity.h"
#include <memory>

/**
 * Base class for activities that can host a child sub-activity.
 * The sub-activity takes over loop() and render() while active.
 */
class ActivityWithSubactivity : public Activity {
 protected:
  std::unique_ptr<Activity> subActivity;

  void enterNewActivity(Activity* activity) {
    subActivity.reset(activity);
    subActivity->onEnter();
  }

  void exitActivity() {
    if (subActivity) {
      subActivity->onExit();
      subActivity.reset();
      requestUpdate();
    }
  }

 public:
  using Activity::Activity;

  void render(RenderLock&& lock) override {
    if (subActivity) {
      subActivity->render(std::move(lock));
    }
  }

  bool preventAutoSleep() override {
    if (subActivity) return subActivity->preventAutoSleep();
    return Activity::preventAutoSleep();
  }
};
