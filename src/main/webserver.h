#pragma once

#include <Arduino.h>

#include "types.h"

namespace tt {
namespace webserver {

void begin();
void loop();

void broadcastToolChange(uint8_t toolId);
void broadcastSnapshot();
void broadcastEvent(const Event& event);
void broadcastAlert(const char* code, int8_t toolId);
void broadcastCameraStatus();

bool consumeRestartRequest();
bool consumeForceAnalysis();
bool consumeWifiResetRequest();

}  // namespace webserver
}  // namespace tt
