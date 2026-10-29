#include <Arduino.h>
#include "esp_heap_caps.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "camera_lock.h"
#include "detector.h"
#include "person_detect_model_data.h"

// Model output order, from Espressif's person_detection example.
constexpr int PERSON_INDEX = 1;
// Working memory for the model. Espressif's example uses 100 KB plus scratch space for ESP-NN.
constexpr size_t ARENA_SIZE = 140 * 1024;

static tflite::MicroInterpreter *interpreter = nullptr;
static uint8_t latestInput[DETECTOR_SIZE * DETECTOR_SIZE];
static DetectorStats stats = {-1, 0, 0};

bool setupDetector() {
  const tflite::Model *model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("[DETECT] Model schema %lu does not match %d\n", model->version(), TFLITE_SCHEMA_VERSION);
    return false;
  }

  // Internal RAM is faster; PSRAM is the fallback if Wi-Fi has taken too much.
  uint8_t *arena = static_cast<uint8_t *>(heap_caps_malloc(ARENA_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const bool inPsram = arena == nullptr;
  if (inPsram) arena = static_cast<uint8_t *>(heap_caps_malloc(ARENA_SIZE, MALLOC_CAP_SPIRAM));
  if (!arena) {
    Serial.println("[DETECT] Not enough memory for the model");
    return false;
  }

  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddAveragePool2D();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddReshape();
  resolver.AddSoftmax();

  static tflite::MicroInterpreter staticInterpreter(model, resolver, arena, ARENA_SIZE);
  interpreter = &staticInterpreter;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("[DETECT] AllocateTensors failed");
    interpreter = nullptr;
    return false;
  }
  Serial.printf("[DETECT] Ready; model memory in %s, %u bytes used\n", inPsram ? "PSRAM" : "internal RAM",
    static_cast<unsigned>(interpreter->arena_used_bytes()));
  return true;
}

// Shrinks the whole RGB565 frame to 96x96 grayscale. The full width is kept rather than cropped to a
// square, so people at the sides of the mouth opening still count; they just look a little narrower.
static bool frameToInput(const camera_fb_t *fb) {
  if (fb->format != PIXFORMAT_RGB565) return false;
  const uint8_t *pixels = fb->buf;
  for (int oy = 0; oy < DETECTOR_SIZE; oy++) {
    const int y0 = oy * fb->height / DETECTOR_SIZE;
    const int y1 = (oy + 1) * fb->height / DETECTOR_SIZE;
    for (int ox = 0; ox < DETECTOR_SIZE; ox++) {
      const int x0 = ox * fb->width / DETECTOR_SIZE;
      const int x1 = (ox + 1) * fb->width / DETECTOR_SIZE;
      uint32_t sum = 0;
      uint32_t count = 0;
      for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
          const size_t i = (static_cast<size_t>(y) * fb->width + x) * 2;
          const uint16_t p = (pixels[i] << 8) | pixels[i + 1];  // Big-endian RGB565, as the camera sends it
          const uint32_t r = (p >> 11) << 3;
          const uint32_t g = ((p >> 5) & 0x3f) << 2;
          const uint32_t b = (p & 0x1f) << 3;
          sum += (r * 77 + g * 150 + b * 29) >> 8;
          count++;
        }
      }
      latestInput[oy * DETECTOR_SIZE + ox] = count ? sum / count : 0;
    }
  }
  return true;
}

int detectPerson() {
  if (!interpreter) return -1;
  camera_fb_t *fb = grabFrame();
  if (!fb) return -1;
  const bool converted = frameToInput(fb);
  releaseFrame(fb);
  if (!converted) return -1;

  TfLiteTensor *input = interpreter->input(0);
  for (int i = 0; i < DETECTOR_SIZE * DETECTOR_SIZE; i++) {
    input->data.int8[i] = static_cast<int8_t>(latestInput[i] - 128);
  }

  const uint32_t startedAt = millis();
  if (interpreter->Invoke() != kTfLiteOk) return -1;
  stats.inferenceMs = millis() - startedAt;

  const TfLiteTensor *output = interpreter->output(0);
  const float person = (output->data.int8[PERSON_INDEX] - output->params.zero_point) * output->params.scale;
  stats.score = constrain(static_cast<int>(person * 100 + 0.5f), 0, 100);
  stats.runs++;
  return stats.score;
}

DetectorStats detectorStats() { return stats; }

void copyDetectorInput(uint8_t *out) { memcpy(out, latestInput, sizeof(latestInput)); }
