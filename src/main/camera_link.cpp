#include "camera_link.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "config.h"
#include "state.h"
#include "time_sync.h"

namespace tt {
namespace camera_link {

namespace {
String s_cachedBase;
uint32_t s_cachedAt = 0;
bool s_resolved = false;

constexpr uint32_t CACHE_MS = 60000;
constexpr uint32_t PHOTO_TIMEOUT_MS = 10000;
}  // namespace

String baseUrl() {
  const uint32_t now = millis();
  if (s_resolved && (now - s_cachedAt) < CACHE_MS) return s_cachedBase;

  s_resolved = true;
  s_cachedAt = now;

  const IPAddress address = MDNS.queryHost(cfg::CAMERA_HOSTNAME, 1500);
  if (address[0] != 0 || address[1] != 0 || address[2] != 0 ||
      address[3] != 0) {
    s_cachedBase = String("http://") + address.toString();
    Serial.printf("[CAM] Nodo de visión en %s\n", s_cachedBase.c_str());
  } else {
    s_cachedBase = cfg::CAMERA_BASE_URL;
  }
  return s_cachedBase;
}

bool health() {
  if (!cfg::FEATURE_CAMERA) return false;
  if (WiFi.status() != WL_CONNECTED) {
    g_state.cameraOnline = false;
    return false;
  }

  HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(baseUrl() + "/health")) {
    g_state.cameraOnline = false;
    return false;
  }

  const int code = http.GET();
  const bool ok = code == HTTP_CODE_OK;
  http.end();

  g_state.cameraOnline = ok;
  if (ok) g_state.cameraLastSeen = timesync::nowEpoch();
  return ok;
}

bool analyze(AnalysisResult& result) {
  result.ok = false;
  result.processingMs = 0;
  result.capturedAt = 0;
  result.modelVersion[0] = '\0';
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    result.slots[i].toolId = i;
    result.slots[i].present = false;
    result.slots[i].predictedTool = -1;
    result.slots[i].confidence = 0;
  }

  if (!cfg::FEATURE_CAMERA) return false;
  if (WiFi.status() != WL_CONNECTED) {
    g_state.cameraOnline = false;
    return false;
  }

  HTTPClient http;
  http.setTimeout(cfg::CAMERA_TIMEOUT_MS);
  if (!http.begin(baseUrl() + "/analyze")) {
    Serial.println("[CAM] No se pudo iniciar la petición");
    g_state.cameraOnline = false;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  const String payload =
      String("{\"requestId\":\"") + String(g_state.bootId, HEX) + "-" +
      String(millis()) + "\"}";

  const int code = http.POST(payload);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[CAM] /analyze respondió %d\n", code);
    http.end();
    g_state.cameraOnline = false;
    return false;
  }

  const String body = http.getString();
  http.end();

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    Serial.printf("[CAM] JSON inválido: %s\n", error.c_str());
    g_state.cameraOnline = false;
    return false;
  }

  result.processingMs = document["processingMs"] | 0;
  result.capturedAt = document["capturedAt"] | timesync::nowEpoch();
  const char* model = document["modelVersion"] | "";
  strncpy(result.modelVersion, model, sizeof(result.modelVersion) - 1);
  result.modelVersion[sizeof(result.modelVersion) - 1] = '\0';

  JsonArray slots = document["slots"].as<JsonArray>();
  for (JsonObject item : slots) {
    const char* slotName = item["slot"] | "";
    if (strlen(slotName) < 2) continue;

    const int row = slotName[0] - 'A';
    const int column = slotName[1] - '1';
    if (row < 0 || row >= cfg::GRID_ROWS || column < 0 ||
        column >= cfg::GRID_COLS) {
      continue;
    }

    const uint8_t toolId = static_cast<uint8_t>(row * cfg::GRID_COLS + column);
    const bool present = item["present"] | false;
    const int predicted = item["toolId"] | -1;

    float rawConfidence = item["confidence"] | 0.0f;
    if (rawConfidence <= 1.0f) rawConfidence *= 100.0f;
    if (rawConfidence < 0) rawConfidence = 0;
    if (rawConfidence > 100) rawConfidence = 100;

    result.slots[toolId].present = present;
    result.slots[toolId].predictedTool = static_cast<int8_t>(predicted);
    result.slots[toolId].confidence =
        static_cast<uint8_t>(rawConfidence + 0.5f);

    stateSetCameraResult(toolId, present,
                         result.slots[toolId].confidence);
  }

  File file = LittleFS.open(cfg::LAST_RESULT_FILE, "w");
  if (file) {
    file.print(body);
    file.close();
  }

  result.ok = true;
  g_state.cameraOnline = true;
  g_state.cameraLastSeen = timesync::nowEpoch();

  Serial.printf("[CAM] Análisis ok: %u ms, modelo %s\n",
                static_cast<unsigned>(result.processingMs),
                result.modelVersion);
  return true;
}

bool fetchPhoto() {
  if (!cfg::FEATURE_CAMERA) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(cfg::CAMERA_TIMEOUT_MS);
  if (!http.begin(baseUrl() + "/latest.jpg")) return false;

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[CAM] /latest.jpg respondió %d\n", code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    return false;
  }

  const String temporary = String(cfg::LAST_PHOTO_FILE) + ".tmp";
  File file = LittleFS.open(temporary, "w");
  if (!file) {
    http.end();
    return false;
  }

  int remaining = http.getSize();
  uint32_t total = 0;
  const uint32_t startedAt = millis();
  uint8_t buffer[1024];

  while (http.connected() && (remaining > 0 || remaining == -1) &&
         (millis() - startedAt) < PHOTO_TIMEOUT_MS) {
    const int available = stream->available();
    if (available > 0) {
      const size_t toRead =
          static_cast<size_t>(available) > sizeof(buffer)
              ? sizeof(buffer)
              : static_cast<size_t>(available);
      const int read = stream->readBytes(buffer, toRead);
      if (read > 0) {
        file.write(buffer, read);
        total += read;
        if (remaining > 0) remaining -= read;
      }
    } else {
      delay(10);
    }
  }

  file.close();
  http.end();

  if (total == 0 || remaining > 0) {
    LittleFS.remove(temporary);
    Serial.println(remaining > 0 ? "[CAM] Foto incompleta"
                                 : "[CAM] Foto vacía");
    return false;
  }

  if (LittleFS.exists(cfg::LAST_PHOTO_FILE)) {
    LittleFS.remove(cfg::LAST_PHOTO_FILE);
  }
  if (!LittleFS.rename(temporary, cfg::LAST_PHOTO_FILE)) {
    Serial.println("[CAM] No se pudo guardar la foto");
    return false;
  }

  Serial.printf("[CAM] Foto guardada (%u bytes)\n",
                static_cast<unsigned>(total));
  return true;
}

}  // namespace camera_link
}  // namespace tt
