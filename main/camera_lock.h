#pragma once
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Detection and the web server share one frame buffer, so they take turns.
inline SemaphoreHandle_t cameraMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}

inline camera_fb_t *grabFrame() {
  xSemaphoreTake(cameraMutex(), portMAX_DELAY);
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) xSemaphoreGive(cameraMutex());
  return fb;
}

inline void releaseFrame(camera_fb_t *fb) {
  esp_camera_fb_return(fb);
  xSemaphoreGive(cameraMutex());
}
