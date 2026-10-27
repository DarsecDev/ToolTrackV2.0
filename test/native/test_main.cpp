#include <cstdint>
#include <cstdio>
#include <cstring>

#include "actuators.h"
#include "config.h"
#include "ir_sensors.h"
#include "state.h"
#include "types.h"

extern uint32_t g_fake_millis;
extern int g_pin_levels[64];

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* expression, const char* file, int line) {
  g_checks++;
  if (condition) return;
  g_failures++;
  std::printf("FALLO %s:%d: %s\n", file, line, expression);
}

#define CHECK(expression) \
  check((expression), #expression, __FILE__, __LINE__)

void resetPins(int level = HIGH) {
  for (int& pin : g_pin_levels) pin = level;
  g_fake_millis = 0;
}

void advance(uint32_t milliseconds) { g_fake_millis += milliseconds; }

const char* expectedSlot(uint8_t toolId) {
  static const char* slots[tt::TOOL_COUNT] = {
      "A1", "A2", "A3", "B1", "B2", "B3",
      "C1", "C2", "C3", "D1", "D2", "D3",
  };
  return toolId < tt::TOOL_COUNT ? slots[toolId] : "--";
}

void testTypes() {
  char slot[3];
  for (uint8_t i = 0; i < tt::TOOL_COUNT; i++) {
    tt::slotForTool(i, slot);
    CHECK(std::strcmp(slot, expectedSlot(i)) == 0);
  }

  tt::slotForTool(-1, slot);
  CHECK(std::strcmp(slot, "--") == 0);
  tt::slotForTool(tt::TOOL_COUNT, slot);
  CHECK(std::strcmp(slot, "--") == 0);

  const tt::EventAction actions[] = {
      tt::ACTION_TAKEN,       tt::ACTION_RETURNED,
      tt::ACTION_MISMATCH,    tt::ACTION_ACCESS_GRANTED,
      tt::ACTION_ACCESS_DENIED, tt::ACTION_CAMERA_OFFLINE,
      tt::ACTION_POWER_LOST,  tt::ACTION_POWER_RESTORED,
  };
  for (tt::EventAction action : actions) {
    CHECK(tt::actionFromName(tt::actionName(action)) == action);
  }
  CHECK(tt::actionFromName("no-existe") == tt::ACTION_UNKNOWN);
  CHECK(tt::actionFromName(nullptr) == tt::ACTION_UNKNOWN);
  CHECK(std::strcmp(tt::actionName(tt::ACTION_UNKNOWN), "unknown") == 0);

  const tt::EventSource sources[] = {
      tt::SOURCE_IR, tt::SOURCE_CAMERA, tt::SOURCE_RFID, tt::SOURCE_SYSTEM,
  };
  for (tt::EventSource source : sources) {
    CHECK(tt::sourceFromName(tt::sourceName(source)) == source);
  }
  CHECK(tt::sourceFromName("no-existe") == tt::SOURCE_SYSTEM);
  CHECK(tt::sourceFromName(nullptr) == tt::SOURCE_SYSTEM);
}

void testConfigPins() {
  auto forbidden = [](uint8_t pin) {
    return pin == 0 || pin == 19 || pin == 20 ||
           (pin >= 26 && pin <= 37) || pin == 43 || pin == 44 ||
           pin == 45 || pin == 46 || pin == 48;
  };

  for (uint8_t i = 0; i < tt::TOOL_COUNT; i++) {
    CHECK(!forbidden(cfg::PIN_IR[i]));
    for (uint8_t j = i + 1; j < tt::TOOL_COUNT; j++) {
      CHECK(cfg::PIN_IR[i] != cfg::PIN_IR[j]);
    }
  }

  const uint8_t otherPins[] = {
      cfg::PIN_RFID_SCK, cfg::PIN_RFID_MISO, cfg::PIN_RFID_MOSI,
      cfg::PIN_RFID_SS,  cfg::PIN_RFID_RST,  cfg::PIN_LOCK,
      cfg::PIN_LED_GREEN, cfg::PIN_LED_RED, cfg::PIN_BUZZER,
      cfg::PIN_LED_STRIP, cfg::PIN_AC_SENSE,
  };
  for (uint8_t pin : otherPins) {
    CHECK(!forbidden(pin));
    for (uint8_t irPin : cfg::PIN_IR) CHECK(pin != irPin);
  }
}

void testState() {
  tt::stateInit();
  CHECK(!tt::stateHasSession());

  for (uint8_t i = 0; i < tt::TOOL_COUNT; i++) {
    CHECK(!tt::g_state.tools[i].known);
    CHECK(tt::g_state.tools[i].present);
    CHECK(tt::g_state.tools[i].cameraPresent);
    CHECK(tt::g_state.tools[i].cameraConfidence == 0);
  }

  tt::stateSetSession("Ana", "A1B2C3D4");
  CHECK(tt::stateHasSession());
  CHECK(std::strcmp(tt::g_state.sessionUser, "Ana") == 0);
  CHECK(std::strcmp(tt::g_state.sessionUid, "A1B2C3D4") == 0);
  tt::stateClearSession();
  CHECK(!tt::stateHasSession());
  CHECK(tt::g_state.sessionUser[0] == '\0');

  tt::stateSetToolPresent(0, false, "Ana", 1234);
  CHECK(tt::g_state.tools[0].known);
  CHECK(!tt::g_state.tools[0].present);
  CHECK(std::strcmp(tt::g_state.tools[0].owner, "Ana") == 0);
  CHECK(tt::g_state.tools[0].takenAtEpoch == 1234);
  CHECK(!tt::g_state.tools[0].cameraPresent);

  tt::stateSetCameraResult(0, true, 87);
  CHECK(tt::g_state.tools[0].cameraPresent);
  CHECK(tt::g_state.tools[0].cameraConfidence == 87);

  tt::stateSetToolPresent(0, true, "Ignorado", 9999);
  CHECK(tt::g_state.tools[0].present);
  CHECK(tt::g_state.tools[0].owner[0] == '\0');
  CHECK(tt::g_state.tools[0].takenAtEpoch == 0);
  CHECK(tt::g_state.tools[0].cameraPresent);

  // Un índice fuera de rango no debe tocar la última herramienta.
  tt::stateSetToolPresent(tt::TOOL_COUNT, false, "Fuera", 1);
  tt::stateSetCameraResult(tt::TOOL_COUNT, false, 1);
  CHECK(tt::g_state.tools[tt::TOOL_COUNT - 1].present);
}

void testIrDebounce() {
  resetPins(HIGH);
  tt::ir::begin();
  CHECK(tt::ir::initialized());

  tt::ir::Change change{};
  CHECK(!tt::ir::poll(change));
  for (uint8_t i = 0; i < tt::TOOL_COUNT; i++) {
    CHECK(!tt::ir::readPresent(i));
  }

  const uint8_t pin = cfg::PIN_IR[0];
  g_pin_levels[pin] = LOW;
  CHECK(!tt::ir::poll(change));
  advance(cfg::IR_DEBOUNCE_MS - 1);
  CHECK(!tt::ir::poll(change));
  advance(1);
  CHECK(tt::ir::poll(change));
  CHECK(change.toolId == 0);
  CHECK(change.present);
  CHECK(tt::ir::readPresent(0));
  CHECK(!tt::ir::poll(change));

  g_pin_levels[pin] = HIGH;
  CHECK(!tt::ir::poll(change));
  advance(cfg::IR_DEBOUNCE_MS - 1);
  CHECK(!tt::ir::poll(change));
  advance(1);
  CHECK(tt::ir::poll(change));
  CHECK(change.toolId == 0);
  CHECK(!change.present);
  CHECK(!tt::ir::readPresent(0));
  CHECK(!tt::ir::poll(change));

  // Dos sensores cambian juntos: poll entrega uno por llamada.
  g_pin_levels[cfg::PIN_IR[1]] = LOW;
  g_pin_levels[cfg::PIN_IR[2]] = LOW;
  CHECK(!tt::ir::poll(change));
  advance(cfg::IR_DEBOUNCE_MS);
  CHECK(tt::ir::poll(change));
  const uint8_t first = change.toolId;
  CHECK(first == 1 || first == 2);
  CHECK(change.present);
  CHECK(tt::ir::poll(change));
  const uint8_t second = change.toolId;
  CHECK(second == 1 || second == 2);
  CHECK(first != second);
  CHECK(change.present);
  CHECK(!tt::ir::poll(change));

  CHECK(tt::ir::readPresent(tt::TOOL_COUNT));
}

void testActuatorsFailSafe() {
  resetPins(HIGH);
  tt::actuators::begin();

  CHECK(g_pin_levels[cfg::PIN_LOCK] == HIGH);
  CHECK(g_pin_levels[cfg::PIN_LED_GREEN] == LOW);
  CHECK(g_pin_levels[cfg::PIN_LED_RED] == LOW);
  CHECK(g_pin_levels[cfg::PIN_BUZZER] == LOW);
  CHECK(g_pin_levels[cfg::PIN_LED_STRIP] == LOW);
  CHECK(!tt::actuators::lockOpen());

  g_fake_millis = 1000;
  tt::actuators::unlock(cfg::LOCK_PULSE_MS);
  CHECK(tt::actuators::lockOpen());
  CHECK(g_pin_levels[cfg::PIN_LED_GREEN] == HIGH);
  CHECK(g_pin_levels[cfg::PIN_LED_RED] == LOW);
  CHECK(g_pin_levels[cfg::PIN_BUZZER] == HIGH);
  CHECK(g_pin_levels[cfg::PIN_LED_STRIP] == HIGH);

  g_fake_millis = 1000 + cfg::LOCK_PULSE_MS - 1;
  tt::actuators::update();
  CHECK(tt::actuators::lockOpen());

  g_fake_millis = 1000 + cfg::LOCK_PULSE_MS;
  tt::actuators::update();
  CHECK(!tt::actuators::lockOpen());
  CHECK(g_pin_levels[cfg::PIN_LOCK] == HIGH);
  CHECK(g_pin_levels[cfg::PIN_LED_GREEN] == LOW);

  g_fake_millis = 5000;
  tt::actuators::denyAlert();
  CHECK(g_pin_levels[cfg::PIN_LED_RED] == HIGH);
  CHECK(g_pin_levels[cfg::PIN_LED_GREEN] == LOW);
  CHECK(g_pin_levels[cfg::PIN_BUZZER] == HIGH);

  g_fake_millis = 5000 + cfg::ALERT_TOTAL_MS - 1;
  tt::actuators::update();
  CHECK(g_pin_levels[cfg::PIN_LED_RED] == HIGH ||
        g_pin_levels[cfg::PIN_LED_RED] == LOW);

  g_fake_millis = 5000 + cfg::ALERT_TOTAL_MS;
  tt::actuators::update();
  CHECK(g_pin_levels[cfg::PIN_LED_RED] == LOW);
  CHECK(g_pin_levels[cfg::PIN_BUZZER] == LOW);

  g_fake_millis = 8000;
  tt::actuators::ledStripOn(1000);
  CHECK(g_pin_levels[cfg::PIN_LED_STRIP] == HIGH);
  g_fake_millis = 9000;
  tt::actuators::update();
  CHECK(g_pin_levels[cfg::PIN_LED_STRIP] == LOW);

  // El temporizador debe funcionar aunque millis() cruce el desbordamiento.
  g_fake_millis = 0xFFFFFF00u;
  tt::actuators::unlock(3000);
  CHECK(tt::actuators::lockOpen());
  g_fake_millis = 0xFFFFFF00u + 2999u;
  tt::actuators::update();
  CHECK(tt::actuators::lockOpen());
  g_fake_millis = 0xFFFFFF00u + 3000u;
  tt::actuators::update();
  CHECK(!tt::actuators::lockOpen());
}

}  // namespace

int main() {
  testTypes();
  testConfigPins();
  testState();
  testIrDebounce();
  testActuatorsFailSafe();

  std::printf("%d comprobaciones, %d fallos\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
