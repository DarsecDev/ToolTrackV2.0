#include "time_sync.h"

#include <time.h>

namespace tt {
namespace timesync {

namespace {
bool s_valid = false;
uint32_t s_bootEpoch = 0;
constexpr uint32_t MIN_VALID_EPOCH = 1700000000UL;  // 2023-11-14
constexpr uint32_t RESYNC_MS = 60UL * 60UL * 1000UL;
uint32_t s_lastCheck = 0;
}  // namespace

void begin() {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  Serial.println("[NTP] Sincronización iniciada");
  update();
}

void update() {
  const uint32_t now = millis();
  if (now - s_lastCheck < 1000) return;
  s_lastCheck = now;

  const time_t current = time(nullptr);
  if (current >= static_cast<time_t>(MIN_VALID_EPOCH)) {
    if (!s_valid) {
      s_valid = true;
      s_bootEpoch = static_cast<uint32_t>(current) - (millis() / 1000);
      Serial.printf("[NTP] Hora válida: %u (arranque: %u)\n",
                    static_cast<unsigned>(current),
                    static_cast<unsigned>(s_bootEpoch));
    }
  }
}

bool valid() { return s_valid; }

uint32_t nowEpoch() {
  if (!s_valid) return 0;
  const time_t current = time(nullptr);
  return current >= 0 ? static_cast<uint32_t>(current) : 0;
}

uint32_t bootEpoch() { return s_valid ? s_bootEpoch : 0; }

String isoTime(uint32_t epoch) {
  if (epoch == 0) return String("");

  time_t value = static_cast<time_t>(epoch);
  struct tm parts {};
  gmtime_r(&value, &parts);

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
  return String(buffer);
}

String localTime(uint32_t epoch) {
  if (epoch == 0) return String("");

  time_t value = static_cast<time_t>(epoch);
  struct tm parts {};
  localtime_r(&value, &parts);

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &parts);
  return String(buffer);
}

}  // namespace timesync
}  // namespace tt
