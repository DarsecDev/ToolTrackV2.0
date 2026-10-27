#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Stub mínimo de Arduino para compilar la lógica de ToolTrack en el host.
// No pretende emular la placa: solo permite probar debounce, estado y
// actuadores sin hardware.

#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 2
#endif

uint32_t millis();
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t value);

class SerialStub {
 public:
  void print(const char* text);
  void print(char value);
  void print(int value);
  void println();
  void println(const char* text);
  void println(int value);
  void printf(const char* format, ...);
};

extern SerialStub Serial;
