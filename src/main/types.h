#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace tt {

constexpr uint8_t TOOL_COUNT = 12;
constexpr uint8_t MAX_USERS = 50;
constexpr uint8_t UID_MAX_LEN = 16;
constexpr uint8_t NAME_MAX_LEN = 32;

enum EventAction : uint8_t {
  ACTION_TAKEN = 0,
  ACTION_RETURNED = 1,
  ACTION_MISMATCH = 2,
  ACTION_ACCESS_GRANTED = 3,
  ACTION_ACCESS_DENIED = 4,
  ACTION_CAMERA_OFFLINE = 5,
  ACTION_POWER_LOST = 6,
  ACTION_POWER_RESTORED = 7,
  ACTION_UNKNOWN = 255,
};

enum EventSource : uint8_t {
  SOURCE_IR = 0,
  SOURCE_CAMERA = 1,
  SOURCE_RFID = 2,
  SOURCE_SYSTEM = 3,
};

struct User {
  uint16_t id;
  char uid[UID_MAX_LEN];
  char name[NAME_MAX_LEN];
  bool active;
  uint32_t createdAt;
};

struct ToolConfig {
  uint8_t id;
  char name[NAME_MAX_LEN];
  char expectedClass[20];
  bool active;
};

struct Settings {
  uint16_t maxEvents;
  bool cameraEnabled;
  bool telegramEnabled;
  char telegramToken[64];
  char telegramChatId[24];
  uint16_t sessionTimeoutMin;
};

struct Event {
  uint32_t id;
  uint32_t bootId;
  uint32_t uptimeMs;
  uint32_t epoch;
  bool timeValid;
  int8_t toolId;
  char slot[3];
  EventAction action;
  EventSource source;
  uint8_t confidence;
  char user[NAME_MAX_LEN];
  char returnedBy[NAME_MAX_LEN];
};

inline void slotForTool(int8_t toolId, char out[3]) {
  if (toolId < 0 || toolId >= TOOL_COUNT) {
    out[0] = '-';
    out[1] = '-';
    out[2] = '\0';
    return;
  }
  out[0] = static_cast<char>('A' + (toolId / 3));
  out[1] = static_cast<char>('1' + (toolId % 3));
  out[2] = '\0';
}

inline const char* actionName(EventAction action) {
  switch (action) {
    case ACTION_TAKEN: return "taken";
    case ACTION_RETURNED: return "returned";
    case ACTION_MISMATCH: return "mismatch";
    case ACTION_ACCESS_GRANTED: return "access_granted";
    case ACTION_ACCESS_DENIED: return "access_denied";
    case ACTION_CAMERA_OFFLINE: return "camera_offline";
    case ACTION_POWER_LOST: return "power_lost";
    case ACTION_POWER_RESTORED: return "power_restored";
    default: return "unknown";
  }
}

inline EventAction actionFromName(const char* name) {
  if (name == nullptr) return ACTION_UNKNOWN;
  if (strcmp(name, "taken") == 0) return ACTION_TAKEN;
  if (strcmp(name, "returned") == 0) return ACTION_RETURNED;
  if (strcmp(name, "mismatch") == 0) return ACTION_MISMATCH;
  if (strcmp(name, "access_granted") == 0) return ACTION_ACCESS_GRANTED;
  if (strcmp(name, "access_denied") == 0) return ACTION_ACCESS_DENIED;
  if (strcmp(name, "camera_offline") == 0) return ACTION_CAMERA_OFFLINE;
  if (strcmp(name, "power_lost") == 0) return ACTION_POWER_LOST;
  if (strcmp(name, "power_restored") == 0) return ACTION_POWER_RESTORED;
  return ACTION_UNKNOWN;
}

inline const char* sourceName(EventSource source) {
  switch (source) {
    case SOURCE_IR: return "ir";
    case SOURCE_CAMERA: return "camera";
    case SOURCE_RFID: return "rfid";
    default: return "system";
  }
}

inline EventSource sourceFromName(const char* name) {
  if (name == nullptr) return SOURCE_SYSTEM;
  if (strcmp(name, "ir") == 0) return SOURCE_IR;
  if (strcmp(name, "camera") == 0) return SOURCE_CAMERA;
  if (strcmp(name, "rfid") == 0) return SOURCE_RFID;
  return SOURCE_SYSTEM;
}

}  // namespace tt
