#pragma once
// /status (live view and recent events), /capture (full frame), /input (what the model sees), /boo (test the speaker),
// /volume?level=0-30, /quiet?on=1|0, /state (one line for scripts), /info (the status text)
bool startWebServer();
// Prints to Serial and keeps the line for /status, since Serial Monitor is out of reach in the pumpkin.
void logEvent(const char *format, ...);
