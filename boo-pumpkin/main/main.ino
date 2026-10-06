#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_system.h"
#include "audio_control.h"

// Choose 1 (ghost), 2 (demon), or 3 (witch), then upload for the next take.
constexpr int BOO_VOICE = 1;
static_assert(BOO_VOICE >= 1 && BOO_VOICE <= 3, "BOO_VOICE must be 1, 2 or 3");
int configuredBooTrack() { return BOO_VOICE; }

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Enter your WiFi credentials
// ===========================
#include "secrets.h"
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

bool startCameraServer();
static bool cameraReady = false;
static bool serversReady = false;

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.printf("[BOOT] Reset reason: %d; free heap: %u; PSRAM: %s\n", static_cast<int>(esp_reset_reason()), ESP.getFreeHeap(), psramFound() ? "available" : "missing");

  setupAudio();
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH);  // Hold camera powered down for speaker testing.
  Serial.println("[TEST] Speaker only: camera and Wi-Fi disabled. Send B repeatedly to play.");

#if 0  // Temporarily disabled for isolated speaker testing.
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
  // Conservative capture baseline while diagnosing missing frames.
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_RGB565;  // Test 3: uncompressed capture
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.jpeg_quality = 15;
  config.fb_count = 1;

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  Serial.println("[TEST 3] RGB565 instead of JPEG; PSRAM restored; 20 MHz; 320x240; one buffer");

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  Serial.println("[CAMERA] Initialisation succeeded; testing capture at 320x240 with one buffer");
  Serial.printf("[CAMERA] Sensor PID: 0x%04x\n", esp_camera_sensor_get()->id.PID);
  delay(300);
  camera_fb_t *testFrame = esp_camera_fb_get();
  if (testFrame != nullptr) {
    cameraReady = true;
    Serial.printf("[CAMERA] Test frame: %ux%u, %u bytes\n", static_cast<unsigned>(testFrame->width), static_cast<unsigned>(testFrame->height), static_cast<unsigned>(testFrame->len));
    esp_camera_fb_return(testFrame);
  } else {
    Serial.println("[CAMERA] Test frame capture FAILED");
  }
  sensor_t *s = esp_camera_sensor_get();
  int colorbarResult = s->set_colorbar(s, 0);
  Serial.printf("[CAMERA] Live view restored; sensor colour bars disabled; result=%d\n", colorbarResult);
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

  Serial.println("[CAMERA] Flash LED disabled; camera owns PWM channel 0 / timer 0");

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  const unsigned long wifiStartedAt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStartedAt >= 30000) {
      Serial.println("\nWi-Fi connection timed out. Check secrets.h and use a 2.4 GHz network, then reset.");
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  Serial.printf("[WIFI] IP: %s; gateway: %s; subnet: %s; signal: %d dBm\n",
    WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
    WiFi.subnetMask().toString().c_str(), WiFi.RSSI());
  serversReady = startCameraServer();
  if (!serversReady) {
    Serial.println("[HTTP] Server setup failed; inspect messages above");
    return;
  }

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
#endif
}

void loop() {
  serviceAudio();
#if 0  // Camera/Wi-Fi heartbeat disabled during speaker test.
  static unsigned long lastReportAt = 0;
  if (millis() - lastReportAt >= 5000) {
    lastReportAt = millis();
    Serial.printf("[ALIVE] uptime=%lu s; Wi-Fi status=%d (3=connected); IP=%s; RSSI=%d dBm; heap=%u; camera=%s; servers=%s\n",
      millis() / 1000, static_cast<int>(WiFi.status()), WiFi.localIP().toString().c_str(),
      WiFi.RSSI(), ESP.getFreeHeap(), cameraReady ? "startup capture OK" : "startup capture failed", serversReady ? "ready" : "failed");
  }

#endif
  delay(20);
}
