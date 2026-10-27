#include "actuators.h"

#include "config.h"

namespace tt {
namespace actuators {

namespace {
uint32_t s_lockUntil = 0;
uint32_t s_beepUntil = 0;
uint32_t s_stripUntil = 0;
uint32_t s_alertUntil = 0;
uint32_t s_lastBlink = 0;
bool s_alertActive = false;
bool s_blinkState = false;

void setBuzzer(bool on) {
  digitalWrite(cfg::PIN_BUZZER, on ? HIGH : LOW);
}
}  // namespace

void begin() {
  // Cerradura fail-safe: HIGH = cerrada. Se configura antes que el resto de
  // salidas para no dejarla abierta durante el arranque.
  pinMode(cfg::PIN_LOCK, OUTPUT);
  digitalWrite(cfg::PIN_LOCK, HIGH);

  const uint8_t outputs[] = {cfg::PIN_LED_GREEN, cfg::PIN_LED_RED,
                             cfg::PIN_BUZZER, cfg::PIN_LED_STRIP};
  for (uint8_t pin : outputs) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  s_lockUntil = 0;
  s_beepUntil = 0;
  s_stripUntil = 0;
  s_alertActive = false;
  s_alertUntil = 0;
  Serial.println("[ACT] Salidas en estado fail-safe (cerradura cerrada)");
}

void unlock(uint32_t durationMs) {
  const uint32_t now = millis();

  // Cancelar cualquier alerta en curso, incluido el buzzer.
  s_alertActive = false;
  s_alertUntil = 0;
  s_beepUntil = 0;
  setBuzzer(false);

  digitalWrite(cfg::PIN_LED_RED, LOW);
  digitalWrite(cfg::PIN_LOCK, LOW);  // fail-safe: LOW = abierta
  digitalWrite(cfg::PIN_LED_GREEN, HIGH);
  s_lockUntil = now + durationMs;

  beep(cfg::BEEP_MS);
  ledStripOn(cfg::LED_STRIP_ON_MS);
}

void denyAlert() {
  const uint32_t now = millis();
  s_alertActive = true;
  s_alertUntil = now + cfg::ALERT_TOTAL_MS;
  s_lastBlink = now;
  s_blinkState = true;

  digitalWrite(cfg::PIN_LED_GREEN, LOW);
  digitalWrite(cfg::PIN_LED_RED, HIGH);
  setBuzzer(true);
}

void beep(uint16_t durationMs) {
  if (s_alertActive) return;
  setBuzzer(true);
  s_beepUntil = millis() + durationMs;
}

void ledStripOn(uint32_t durationMs) {
  digitalWrite(cfg::PIN_LED_STRIP, HIGH);
  s_stripUntil = millis() + durationMs;
}

void ledStripOff() {
  digitalWrite(cfg::PIN_LED_STRIP, LOW);
  s_stripUntil = 0;
}

bool lockOpen() { return digitalRead(cfg::PIN_LOCK) == LOW; }

void update() {
  const uint32_t now = millis();

  if (s_lockUntil != 0 &&
      static_cast<int32_t>(now - s_lockUntil) >= 0) {
    digitalWrite(cfg::PIN_LOCK, HIGH);  // fail-safe: HIGH = cerrada
    digitalWrite(cfg::PIN_LED_GREEN, LOW);
    s_lockUntil = 0;
  }

  if (s_stripUntil != 0 &&
      static_cast<int32_t>(now - s_stripUntil) >= 0) {
    digitalWrite(cfg::PIN_LED_STRIP, LOW);
    s_stripUntil = 0;
  }

  if (!s_alertActive && s_beepUntil != 0 &&
      static_cast<int32_t>(now - s_beepUntil) >= 0) {
    setBuzzer(false);
    s_beepUntil = 0;
  }

  if (s_alertActive) {
    if (static_cast<int32_t>(now - s_alertUntil) >= 0) {
      s_alertActive = false;
      digitalWrite(cfg::PIN_LED_RED, LOW);
      setBuzzer(false);
      s_beepUntil = 0;
    } else if (now - s_lastBlink >= cfg::ALERT_BLINK_MS) {
      s_lastBlink = now;
      s_blinkState = !s_blinkState;
      digitalWrite(cfg::PIN_LED_RED, s_blinkState ? HIGH : LOW);
      setBuzzer(s_blinkState);
    }
  }
}

}  // namespace actuators
}  // namespace tt
