#include <Arduino.h>

uint32_t g_fake_millis = 0;
int g_pin_levels[64] = {};

uint32_t millis() { return g_fake_millis; }

void pinMode(uint8_t pin, uint8_t mode) {
  (void)pin;
  (void)mode;
}

int digitalRead(uint8_t pin) {
  if (pin >= sizeof(g_pin_levels) / sizeof(g_pin_levels[0])) return HIGH;
  return g_pin_levels[pin];
}

void digitalWrite(uint8_t pin, uint8_t value) {
  if (pin < sizeof(g_pin_levels) / sizeof(g_pin_levels[0])) {
    g_pin_levels[pin] = value;
  }
}

void SerialStub::print(const char* text) {
  (void)text;
}

void SerialStub::print(char value) { (void)value; }

void SerialStub::print(int value) { (void)value; }

void SerialStub::println() {}

void SerialStub::println(const char* text) { (void)text; }

void SerialStub::println(int value) { (void)value; }

void SerialStub::printf(const char* format, ...) {
  if (std::getenv("TOOLTRACK_TEST_VERBOSE") == nullptr) return;
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
}

SerialStub Serial;
