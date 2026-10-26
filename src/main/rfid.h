#pragma once

#include <Arduino.h>

namespace tt {
namespace rfid {

bool begin();
String poll();

}  // namespace rfid
}  // namespace tt
