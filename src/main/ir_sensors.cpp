#include "ir_sensors.h"

#include "config.h"

namespace tt {
namespace ir {

namespace {
bool s_raw[TOOL_COUNT] = {};
bool s_stable[TOOL_COUNT] = {};
uint32_t s_lastRawChange[TOOL_COUNT] = {};
bool s_initialized = false;

bool digitalToPresent(uint8_t pin) {
  const bool high = digitalRead(pin) == HIGH;
  return cfg::IR_ACTIVE_LOW ? !high : high;
}
}  // namespace

void begin() {
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    pinMode(cfg::PIN_IR[i], INPUT_PULLUP);
    s_raw[i] = digitalToPresent(cfg::PIN_IR[i]);
    s_stable[i] = s_raw[i];
    s_lastRawChange[i] = millis();
  }
  s_initialized = true;

  Serial.print("[IR] Estado inicial:");
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    char slot[3];
    slotForTool(i, slot);
    Serial.printf(" %s=%s", slot, s_stable[i] ? "P" : "-");
  }
  Serial.println();
}

bool poll(Change& change) {
  if (!s_initialized) return false;

  const uint32_t now = millis();
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    const bool present = digitalToPresent(cfg::PIN_IR[i]);

    if (present != s_raw[i]) {
      s_raw[i] = present;
      s_lastRawChange[i] = now;
    }

    if (s_raw[i] != s_stable[i] &&
        (now - s_lastRawChange[i]) >= cfg::IR_DEBOUNCE_MS) {
      s_stable[i] = s_raw[i];
      change.toolId = i;
      change.present = s_stable[i];

      char slot[3];
      slotForTool(i, slot);
      Serial.printf("[IR] Cambio %s -> %s\n", slot,
                    change.present ? "presente" : "fuera");
      return true;
    }
  }
  return false;
}

bool readPresent(uint8_t toolId) {
  if (toolId >= TOOL_COUNT) return true;
  return digitalToPresent(cfg::PIN_IR[toolId]);
}

bool initialized() { return s_initialized; }

}  // namespace ir
}  // namespace tt
