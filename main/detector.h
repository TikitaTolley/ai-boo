#pragma once
#include <stdint.h>

constexpr int DETECTOR_SIZE = 96;  // The model sees a 96x96 grayscale picture.

struct DetectorStats {
  int score;             // Latest person score, 0-100, or -1 before the first run
  uint32_t inferenceMs;  // Time the latest run took
  uint32_t runs;
};

bool setupDetector();
// Grabs a frame, runs the model and returns the person score 0-100, or -1 on failure.
int detectPerson();
DetectorStats detectorStats();
// Copies the latest model input (96x96 grayscale) into out.
void copyDetectorInput(uint8_t *out);
