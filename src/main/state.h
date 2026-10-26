#pragma once

#include "types.h"

namespace tt {

struct ToolRuntime {
  bool known;
  bool present;
  bool cameraPresent;
  uint8_t cameraConfidence;
  char owner[NAME_MAX_LEN];
  uint32_t takenAtEpoch;
};

struct AppState {
  ToolRuntime tools[TOOL_COUNT];
  char sessionUser[NAME_MAX_LEN];
  char sessionUid[UID_MAX_LEN];
  bool cameraOnline;
  uint32_t cameraLastSeen;
  bool acPresent;
  uint32_t bootId;
};

extern AppState g_state;

void stateInit();
bool stateHasSession();
void stateSetSession(const char* name, const char* uid);
void stateClearSession();
void stateSetToolPresent(uint8_t toolId, bool present, const char* owner,
                         uint32_t epoch);
void stateSetCameraResult(uint8_t toolId, bool present, uint8_t confidence);

}  // namespace tt
