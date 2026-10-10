// Arduino-side log storage: internal SPIFFS (always on, 256 KB cap) and an
// optional SD card (cap = the card's real volume size at boot, no hardcoded size).
#pragma once
#include "configs.h"

#ifdef MARAUDER_OLED128X64

// Call once after the SD card (if any) and SPIFFS are mounted.
void logInit();

// Write one record to the internal log and, if an SD card is present, to the SD log.
void marauderLog(const char* tag, const char* message);

// Sizes reported for the UI / CLI.
unsigned long logInternalCapBytes();
unsigned long long logSdCapBytes();   // 0 when no SD card
// True when a card was found at boot and the SD log is actually writing to it.
bool logSdPresent();

#endif  // MARAUDER_OLED128X64
