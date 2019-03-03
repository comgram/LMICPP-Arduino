#ifdef ARDUINO_ARCH_ESP32
#include "lmicrand.h"

#include <esp_system.h>

// return next random from esp system random generator
uint8_t LmicRandFromAes::uint8() { return esp_random() & 0xFF; }

//! Get random number .
uint16_t LmicRandFromAes::uint16() { return esp_random() & 0xFFFF; }

#endif