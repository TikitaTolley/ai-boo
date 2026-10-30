#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoOTA.h>
#include "audio_control.h"
#include "board_config.h"
#include "camera_lock.h"
#include "detector.h"
#include "web_server.h"
#include "secrets.h"

// Choose 1 (ghost), 2 (demon), or 3 (witch).
constexpr int BOO_VOICE = 1;
static_assert(BOO_VOICE >= 1 && BOO_VOICE <= 3, "BOO_VOICE must be 1, 2 or 3");
int configuredBooTrack() { return BOO_VOICE; }

// The camera board stands upside down in the pumpkin. The OV3660 already needs vflip in normal
// orientation, so upside down is vflip off + mirror on. Check with /capture and swap if wrong.
constexpr int CAMERA_VFLIP = 0;
constexpr int CAMERA_HMIRROR = 1;

// Join Wi-Fi for /status, frame collection and uploads. Without Wi-Fi the pumpkin still detects and boos.
constexpr bool USE_WIFI = true;
constexpr unsigned long WIFI_TIMEOUT_MS = 10000;

static bool startCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA;
  // JPEG capture fails on this OV3660; RGB565 works.
  config.pixel_format = PIXFORMAT_RGB565;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.fb_count = 1;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, CAMERA_VFLIP);
  s->set_hmirror(s, CAMERA_HMIRROR);
  if (s->id.PID == OV3660_PID) {
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  logEvent("[CAMERA] Ready, sensor PID 0x%04x", s->id.PID);
  return true;
}

static WiFiMulti wifiMulti;

static void startWifi() {
  // Joins whichever network in secrets.h is in range.
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
#ifdef HOTSPOT_SSID
  wifiMulti.addAP(HOTSPOT_SSID, HOTSPOT_PASSWORD);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (wifiMulti.run(WIFI_TIMEOUT_MS) != WL_CONNECTED) {
    logEvent("[WIFI] Not connected; running without the web pages");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }
  // Uploads over Wi-Fi: pick "boo-pumpkin" under Tools > Port in Arduino IDE.
  ArduinoOTA.setHostname("boo-pumpkin");
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.begin();
  if (startWebServer()) {
    logEvent("[WIFI] On %s; status at http://%s/ or http://boo-pumpkin.local/", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    logEvent("[HTTP] Web server failed to start");
  }
}

// A person must score at least this (0-100) on this many runs in a row before the boo plays.
// Raise PERSON_THRESHOLD if it boos at empty scenes, lower it if it misses people.
constexpr int PERSON_THRESHOLD = 70;
constexpr int RUNS_TO_CONFIRM = 2;

static bool detecting = false;

void setup() {
  Serial.begin(115200);
  Serial.println();
  cameraMutex();
  setupAudio();
  const bool cameraReady = startCamera();
  if (USE_WIFI) startWifi();
  detecting = cameraReady && setupDetector();
  if (!detecting) logEvent("[DETECT] Not running; check the messages above");
  Serial.println("[READY] Send B to play the boo");
}

void loop() {
  ArduinoOTA.handle();
  serviceAudio();
  if (!detecting) {
    delay(20);
    return;
  }

  // One boo per visit: the count has to drop below the threshold before the next person can trigger it.
  static int personRuns = 0;
  const int score = detectPerson();
  if (score >= PERSON_THRESHOLD) {
    if (++personRuns == RUNS_TO_CONFIRM && queueBoo(configuredBooTrack())) {
      logEvent("[BOO] Person %d%%", score);
    }
  } else if (score >= 0) {
    personRuns = 0;
  }
}
