#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// RAII wrapper for FreeRTOS semaphore — takes on construction, gives on destruction
class SemaphoreGuard {
 public:
  explicit SemaphoreGuard(SemaphoreHandle_t sem, TickType_t timeout = portMAX_DELAY)
      : semaphore(sem), taken(false) {
    if (semaphore) {
      taken = (xSemaphoreTake(semaphore, timeout) == pdTRUE);
    }
  }
  ~SemaphoreGuard() {
    if (taken && semaphore) {
      xSemaphoreGive(semaphore);
    }
  }
  bool acquired() const { return taken; }

  SemaphoreGuard(const SemaphoreGuard&) = delete;
  SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;

 private:
  SemaphoreHandle_t semaphore;
  bool taken;
};
