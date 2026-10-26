#pragma once

#include <Arduino.h>

#include "types.h"

namespace tt {
namespace camera_link {

struct AnalysisSlot {
  uint8_t toolId;
  bool present;
  int8_t predictedTool;
  uint8_t confidence;
};

struct AnalysisResult {
  bool ok;
  uint32_t processingMs;
  uint32_t capturedAt;
  char modelVersion[24];
  AnalysisSlot slots[TOOL_COUNT];
};

bool health();
bool analyze(AnalysisResult& result);
bool fetchPhoto();
String baseUrl();

}  // namespace camera_link
}  // namespace tt
