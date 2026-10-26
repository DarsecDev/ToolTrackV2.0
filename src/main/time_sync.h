#pragma once

#include <Arduino.h>

namespace tt {
namespace timesync {

void begin();
void update();
bool valid();
uint32_t nowEpoch();
uint32_t bootEpoch();
String isoTime(uint32_t epoch);
String localTime(uint32_t epoch);

}  // namespace timesync
}  // namespace tt
