#pragma once

#include <Arduino.h>

#include "types.h"

namespace tt {
namespace ir {

struct Change {
  uint8_t toolId;
  bool present;
};

void begin();
bool poll(Change& change);
bool readPresent(uint8_t toolId);
bool initialized();

}  // namespace ir
}  // namespace tt
