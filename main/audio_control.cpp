#include <Arduino.h>
#include <Preferences.h>
#include "audio_control.h"
#include "web_server.h"

// Camera GPIOs and USB UART0 are left untouched. No ESP32 SD slot is used.
static HardwareSerial audioSerial(2);
static portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
static Preferences settings;
static int pendingTrack = 0;
static bool pendingAudible = false;
static int pendingVolume = -1;
static unsigned long lastBooAt = 0;
static bool hasPlayed = false;
static int boos = 0;
// Volume and quiet mode survive a power cut.
static int volume = 8;
static bool quiet = false;
constexpr unsigned long BOO_COOLDOWN_MS = 8000;  // Gap before the next person can trigger it.
// Replies arrive only if DFPlayer TX is wired to GPIO13; they explain silent boos.
static uint8_t reply[10];
static int replyLength = 0;
static bool heardFromPlayer = false;
static bool reportedSilence = false;
static unsigned long queriedAt = 0;
static unsigned long recheckAt = 0;  // A boo clip lasts about 2 s, so 3 s later it should report stopped

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
  settings.begin("boo", false);
  volume = settings.getInt("volume", 8);
  quiet = settings.getBool("quiet", false);
  audioSerial.begin(9600, SERIAL_8N1, 13, 14);
  delay(1500);  // Give the DFPlayer time to read its card.
  sendCommand(0x06, volume);  // Range 0-30.
  delay(100);
  sendCommand(0x48, 0);  // Ask how many files are on the card; the reply proves the player is listening.
  queriedAt = millis();
  Serial.println("[AUDIO] UART ready: GPIO14 TX -> 1k resistor -> DFPlayer RX; optional GPIO13 RX <- DFPlayer TX");
  Serial.printf("[AUDIO] Voice %d, volume %d/30%s\n", configuredBooTrack(), volume, quiet ? ", quiet mode" : "");
}

bool queueBoo(int track, bool evenWhenQuiet) {
  if (track < 1 || track > 3) return false;
  portENTER_CRITICAL(&audioMux);
  const bool accepted = pendingTrack == 0 && (!hasPlayed || millis() - lastBooAt >= BOO_COOLDOWN_MS);
  if (accepted) {
    pendingTrack = track;
    pendingAudible = evenWhenQuiet || !quiet;
    lastBooAt = millis();
    hasPlayed = true;
    boos++;
  }
  portEXIT_CRITICAL(&audioMux);
  return accepted;
}

static void describeReply(uint8_t command, uint16_t value) {
  heardFromPlayer = true;
  switch (command) {
    case 0x3a: logEvent("[DFPLAYER] Card inserted"); break;
    case 0x3b: logEvent("[DFPLAYER] Card removed"); break;
    case 0x3d: logEvent("[DFPLAYER] Finished playing track %u", value); break;
    case 0x3f: logEvent("[DFPLAYER] Started up%s", value & 0x02 ? " with the card" : ", but no card found"); break;
    case 0x40: {
      static const char *errors[] = {"?", "busy (card still starting)", "asleep", "serial data garbled",
        "checksum wrong", "track number out of range", "track not found", "?", "card read error"};
      logEvent("[DFPLAYER] Error: %s", value < 9 ? errors[value] : "unknown");
      break;
    }
    case 0x41: break;  // Plain acknowledgement
    case 0x42: logEvent("[DFPLAYER] Status %s", value & 0x01 ? "playing" : "stopped"); break;
    case 0x43: logEvent("[DFPLAYER] Volume %u", value); break;
    case 0x48: logEvent("[DFPLAYER] Listening; %u files on the card", value); break;
    default: logEvent("[DFPLAYER] Reply 0x%02x value %u", command, value);
  }
}

static void readReplies() {
  while (audioSerial.available() > 0) {
    const uint8_t byte = audioSerial.read();
    if (replyLength == 0 && byte != 0x7e) continue;
    reply[replyLength++] = byte;
    if (replyLength == 10) {
      if (reply[9] == 0xef) describeReply(reply[3], (reply[5] << 8) | reply[6]);
      replyLength = 0;
    }
  }
  if (!heardFromPlayer && !reportedSilence && millis() - queriedAt > 3000) {
    reportedSilence = true;
    logEvent("[DFPLAYER] No reply. Either its TX is not wired to GPIO13, or it is not powered or not receiving");
  }
}

void serviceAudio() {
  readReplies();
  while (Serial.available() > 0) {
    const char input = Serial.read();
    if (input == 'b' || input == 'B') {
      sendCommand(0x12, configuredBooTrack());
      Serial.printf("[AUDIO] Manual playback command sent for track %d\n", configuredBooTrack());
    }
  }
  portENTER_CRITICAL(&audioMux);
  const int track = pendingTrack;
  const bool audible = pendingAudible;
  const int newVolume = pendingVolume;
  pendingTrack = 0;
  pendingVolume = -1;
  portEXIT_CRITICAL(&audioMux);
  if (newVolume >= 0) sendCommand(0x06, newVolume);
  if (track != 0 && audible) {
    sendCommand(0x12, track);  // Select /mp3/000N.mp3 explicitly.
    Serial.printf("[AUDIO] Playback command sent for track %d\n", track);
    delay(200);
    sendCommand(0x42, 0);  // Ask whether it actually started playing
    recheckAt = millis() + 3000;
  }
  if (recheckAt && millis() >= recheckAt) {
    recheckAt = 0;
    logEvent("[AUDIO] 3 s after the boo: stopped = it played through, playing = it is stuck");
    sendCommand(0x42, 0);
  }
}

void setVolume(int level) {
  volume = constrain(level, 0, 30);
  settings.putInt("volume", volume);
  portENTER_CRITICAL(&audioMux);
  pendingVolume = volume;  // Sent from the loop, so the web server never writes to the DFPlayer itself.
  portEXIT_CRITICAL(&audioMux);
}

int currentVolume() { return volume; }

void setQuiet(bool on) {
  quiet = on;
  settings.putBool("quiet", quiet);
}

bool isQuiet() { return quiet; }

int booCount() { return boos; }

unsigned long msSinceLastBoo() { return millis() - lastBooAt; }
