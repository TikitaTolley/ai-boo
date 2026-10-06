#include <Arduino.h>
#include "audio_control.h"

// Camera GPIOs and USB UART0 are left untouched. No ESP32 SD slot is used.
static HardwareSerial audioSerial(2);
static portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
static int pendingTrack = 0;
static bool hasPlayed = false;

static void sendCommand(uint8_t command, uint16_t parameter) {
  uint8_t packet[10] = {0x7e, 0xff, 0x06, command, 0x00,
    static_cast<uint8_t>(parameter >> 8), static_cast<uint8_t>(parameter), 0, 0, 0xef};
  uint16_t checksum = 0;
  for (int i = 1; i <= 6; i++) checksum += packet[i];
  checksum = static_cast<uint16_t>(0 - checksum);
  packet[7] = checksum >> 8;
  packet[8] = checksum & 0xff;
  audioSerial.write(packet, sizeof(packet));
}

void setupAudio() {
  audioSerial.begin(9600, SERIAL_8N1, 13, 14);
  delay(1500);  // Give the DFPlayer time to read its card.
  sendCommand(0x06, 8);  // Modest starting volume, range 0-30.
  Serial.println("[AUDIO] UART ready: GPIO14 TX -> 1k resistor -> DFPlayer RX; optional GPIO13 RX <- DFPlayer TX");
  Serial.printf("[AUDIO] Voice %d selected; Send B for a repeatable manual speaker test.\n", configuredBooTrack());
}

bool queueBoo(int track) {
  if (track < 1 || track > 3) return false;
  portENTER_CRITICAL(&audioMux);
  const bool accepted = pendingTrack == 0 && !hasPlayed;
  if (accepted) {
    pendingTrack = track;
    hasPlayed = true;
  }
  portEXIT_CRITICAL(&audioMux);
  return accepted;
}

void serviceAudio() {
  while (Serial.available() > 0) {
    const char input = Serial.read();
    if (input == 'b' || input == 'B') {
      sendCommand(0x12, configuredBooTrack());
      Serial.printf("[AUDIO] Manual playback command sent for track %d\n", configuredBooTrack());
    }
  }
  portENTER_CRITICAL(&audioMux);
  const int track = pendingTrack;
  pendingTrack = 0;
  portEXIT_CRITICAL(&audioMux);
  if (track != 0) {
    sendCommand(0x12, track);  // Select /mp3/000N.mp3 explicitly.
    Serial.printf("[AUDIO] Playback command sent for track %d\n", track);
  }
}
