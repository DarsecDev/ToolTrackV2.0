#pragma once

#include <Arduino.h>

namespace tt {
namespace actuators {

void begin();
void update();

void unlock(uint32_t durationMs);
void denyAlert();
void beep(uint16_t durationMs);
void ledStripOn(uint32_t durationMs);
void ledStripOff();
bool lockOpen();

}  // namespace actuators
}  // namespace tt
