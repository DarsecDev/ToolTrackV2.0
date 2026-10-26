#include "state.h"

#include <cstring>

namespace tt {

AppState g_state{};

void stateInit() {
  std::memset(&g_state, 0, sizeof(g_state));
  g_state.acPresent = true;
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    g_state.tools[i].known = false;
    g_state.tools[i].present = true;
    g_state.tools[i].cameraPresent = true;
    g_state.tools[i].cameraConfidence = 0;
  }
}

bool stateHasSession() {
  return g_state.sessionUser[0] != '\0';
}

void stateSetSession(const char* name, const char* uid) {
  std::strncpy(g_state.sessionUser, name ? name : "",
               sizeof(g_state.sessionUser) - 1);
  g_state.sessionUser[sizeof(g_state.sessionUser) - 1] = '\0';
  std::strncpy(g_state.sessionUid, uid ? uid : "",
               sizeof(g_state.sessionUid) - 1);
  g_state.sessionUid[sizeof(g_state.sessionUid) - 1] = '\0';
}

void stateClearSession() {
  g_state.sessionUser[0] = '\0';
  g_state.sessionUid[0] = '\0';
}

void stateSetToolPresent(uint8_t toolId, bool present, const char* owner,
                         uint32_t epoch) {
  if (toolId >= TOOL_COUNT) return;
  ToolRuntime& tool = g_state.tools[toolId];
  tool.known = true;
  tool.present = present;

  if (present) {
    tool.owner[0] = '\0';
    tool.takenAtEpoch = 0;
    tool.cameraPresent = true;
  } else {
    std::strncpy(tool.owner, owner ? owner : "", sizeof(tool.owner) - 1);
    tool.owner[sizeof(tool.owner) - 1] = '\0';
    tool.takenAtEpoch = epoch;
    tool.cameraPresent = false;
  }
  tool.cameraConfidence = 0;
}

void stateSetCameraResult(uint8_t toolId, bool present, uint8_t confidence) {
  if (toolId >= TOOL_COUNT) return;
  g_state.tools[toolId].cameraPresent = present;
  g_state.tools[toolId].cameraConfidence = confidence;
}

}  // namespace tt
