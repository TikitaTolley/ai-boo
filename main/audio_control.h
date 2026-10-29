#pragma once
void setupAudio();
void serviceAudio();
// Quiet mode still counts and logs detection boos but keeps the speaker silent; evenWhenQuiet overrides it.
bool queueBoo(int track, bool evenWhenQuiet = false);
int configuredBooTrack();
int booCount();
unsigned long msSinceLastBoo();
void setVolume(int level);  // 0-30
int currentVolume();
void setQuiet(bool on);
bool isQuiet();
