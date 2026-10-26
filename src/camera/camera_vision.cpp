#include "camera_vision.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "camera_classifier.h"

namespace camvision {
namespace {

constexpr uint16_t DEFAULT_WIDE_WIDTH = 1600;
constexpr uint16_t DEFAULT_WIDE_HEIGHT = 1200;
constexpr uint16_t DEFAULT_SMALL_WIDTH = 640;
constexpr uint16_t DEFAULT_SMALL_HEIGHT = 480;
constexpr float ROI_MARGIN = 0.02f;
constexpr float ROI_EPSILON = 0.001f;

bool s_ready = false;
bool s_roisReady = false;
Roi s_rois[camcfg::TOOL_COUNT];
uint16_t s_frameWidth = DEFAULT_WIDE_WIDTH;
uint16_t s_frameHeight = DEFAULT_WIDE_HEIGHT;
char s_lastError[96] = "";

void setError(const char* message) {
  std::snprintf(s_lastError, sizeof(s_lastError), "%s", message ? message : "");
}

void slotName(uint8_t index, char out[3]) {
  out[0] = static_cast<char>('A' + (index / camcfg::GRID_COLS));
  out[1] = static_cast<char>('1' + (index % camcfg::GRID_COLS));
  out[2] = '\0';
}

void setDefaultRois() {
  const float cellW = 1.0f / camcfg::GRID_COLS;
  const float cellH = 1.0f / camcfg::GRID_ROWS;

  for (uint8_t i = 0; i < camcfg::TOOL_COUNT; i++) {
    const uint8_t row = i / camcfg::GRID_COLS;
    const uint8_t column = i % camcfg::GRID_COLS;
    s_rois[i].x = column * cellW + ROI_MARGIN;
    s_rois[i].y = row * cellH + ROI_MARGIN;
    s_rois[i].w = cellW - ROI_MARGIN * 2.0f;
    s_rois[i].h = cellH - ROI_MARGIN * 2.0f;
  }
}

bool writeBytesAtomic(const char* path, const uint8_t* data, size_t length) {
  const String temporary = String(path) + ".tmp";
  File file = LittleFS.open(temporary, "w");
  if (!file) return false;

  const size_t written = file.write(data, length);
  file.close();
  if (written != length) {
    LittleFS.remove(temporary);
    return false;
  }

  LittleFS.remove(path);
  if (!LittleFS.rename(temporary, path)) {
    LittleFS.remove(temporary);
    return false;
  }
  return true;
}

bool writeStringAtomic(const char* path, const String& data) {
  return writeBytesAtomic(path, reinterpret_cast<const uint8_t*>(data.c_str()),
                          data.length());
}

bool saveRois() {
  return writeStringAtomic(camcfg::ROIS_FILE, roisJson());
}

bool loadRois() {
  s_roisReady = false;

  if (!LittleFS.exists(camcfg::ROIS_FILE)) {
    setDefaultRois();
    s_roisReady = true;
    if (!saveRois()) {
      Serial.println("[ROI] No se pudo guardar rois.json; se usan valores por defecto");
    }
    return true;
  }

  File file = LittleFS.open(camcfg::ROIS_FILE, "r");
  if (!file) {
    setDefaultRois();
    s_roisReady = true;
    return false;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();

  JsonObject rois = document["rois"].is<JsonObject>()
                        ? document["rois"].as<JsonObject>()
                        : document.as<JsonObject>();

  if (error || rois.isNull()) {
    Serial.printf("[ROI] rois.json inválido: %s\n", error.c_str());
    setDefaultRois();
    s_roisReady = true;
    return false;
  }

  Roi loaded[camcfg::TOOL_COUNT];
  for (uint8_t i = 0; i < camcfg::TOOL_COUNT; i++) {
    char name[3];
    slotName(i, name);
    JsonArray values = rois[name];
    if (values.isNull() || values.size() != 4) {
      Serial.printf("[ROI] Falta la ROI %s; se usan valores por defecto\n", name);
      setDefaultRois();
      s_roisReady = true;
      return false;
    }

    Roi candidate{};
    candidate.x = values[0] | 0.0f;
    candidate.y = values[1] | 0.0f;
    candidate.w = values[2] | 0.0f;
    candidate.h = values[3] | 0.0f;

    const bool valid = candidate.x >= 0.0f && candidate.y >= 0.0f &&
                       candidate.w > 0.0f && candidate.h > 0.0f &&
                       candidate.x + candidate.w <= 1.0f + ROI_EPSILON &&
                       candidate.y + candidate.h <= 1.0f + ROI_EPSILON;
    if (!valid) {
      Serial.printf("[ROI] ROI %s fuera de rango; se usan valores por defecto\n",
                    name);
      setDefaultRois();
      s_roisReady = true;
      return false;
    }
    loaded[i] = candidate;
  }

  std::memcpy(s_rois, loaded, sizeof(s_rois));
  s_roisReady = true;
  return true;
}

bool initCamera() {
  camera_config_t config{};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_pwdn = camcfg::PIN_PWDN;
  config.pin_reset = camcfg::PIN_RESET;
  config.pin_xclk = camcfg::PIN_XCLK;
  config.pin_sccb_sda = camcfg::PIN_SIOD;
  config.pin_sccb_scl = camcfg::PIN_SIOC;

  config.pin_d0 = camcfg::PIN_D0;
  config.pin_d1 = camcfg::PIN_D1;
  config.pin_d2 = camcfg::PIN_D2;
  config.pin_d3 = camcfg::PIN_D3;
  config.pin_d4 = camcfg::PIN_D4;
  config.pin_d5 = camcfg::PIN_D5;
  config.pin_d6 = camcfg::PIN_D6;
  config.pin_d7 = camcfg::PIN_D7;

  config.pin_vsync = camcfg::PIN_VSYNC;
  config.pin_href = camcfg::PIN_HREF;
  config.pin_pclk = camcfg::PIN_PCLK;

  config.xclk_freq_hz = camcfg::XCLK_FREQ_HZ;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  const bool hasPsram = psramFound();
  config.frame_size = hasPsram ? FRAMESIZE_UXGA : FRAMESIZE_VGA;
  config.jpeg_quality = hasPsram ? camcfg::ANALYSIS_JPEG_QUALITY : 12;
  config.fb_count = hasPsram ? 2 : 1;

  esp_err_t error = esp_camera_init(&config);
  bool wideFrame = hasPsram;
  if (error != ESP_OK && hasPsram) {
    Serial.printf("[CAM] UXGA falló (0x%x); reintentando en VGA\n", error);
    esp_camera_deinit();
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    error = esp_camera_init(&config);
    if (error == ESP_OK) wideFrame = false;
  }

  if (error != ESP_OK) {
    setError("No se pudo inicializar la cámara");
    Serial.printf("[CAM] Error de inicialización: 0x%x (%s)\n", error,
                  esp_err_to_name(error));
    return false;
  }

  s_frameWidth = wideFrame ? DEFAULT_WIDE_WIDTH : DEFAULT_SMALL_WIDTH;
  s_frameHeight = wideFrame ? DEFAULT_WIDE_HEIGHT : DEFAULT_SMALL_HEIGHT;

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    Serial.printf("[CAM] Sensor PID=0x%02x, framesize=%d\n", sensor->id.PID,
                  sensor->status.framesize);
  }

  Serial.printf("[CAM] Cámara inicializada en %ux%u\n", s_frameWidth,
                s_frameHeight);
  return true;
}

bool savePhoto(camera_fb_t* frame) {
  if (frame->format == PIXFORMAT_JPEG) {
    return writeBytesAtomic(camcfg::LAST_PHOTO_FILE, frame->buf, frame->len);
  }

  uint8_t* jpeg = nullptr;
  size_t jpegLength = 0;
  if (!fmt2jpg(frame->buf, frame->len, frame->width, frame->height,
               frame->format, camcfg::ANALYSIS_JPEG_QUALITY, &jpeg,
               &jpegLength)) {
    return false;
  }

  const bool saved =
      writeBytesAtomic(camcfg::LAST_PHOTO_FILE, jpeg, jpegLength);
  free(jpeg);
  return saved;
}

void cropRoi(const uint8_t* rgb, uint16_t width, uint16_t height, const Roi& roi,
             uint8_t* output, uint16_t outputSize) {
  uint32_t x0 = static_cast<uint32_t>(roi.x * width);
  uint32_t y0 = static_cast<uint32_t>(roi.y * height);
  uint32_t x1 = static_cast<uint32_t>((roi.x + roi.w) * width);
  uint32_t y1 = static_cast<uint32_t>((roi.y + roi.h) * height);

  x0 = std::min<uint32_t>(x0, width - 1);
  y0 = std::min<uint32_t>(y0, height - 1);
  x1 = std::max<uint32_t>(x0 + 1, std::min<uint32_t>(x1, width));
  y1 = std::max<uint32_t>(y0 + 1, std::min<uint32_t>(y1, height));

  const uint32_t sourceWidth = x1 - x0;
  const uint32_t sourceHeight = y1 - y0;

  for (uint16_t y = 0; y < outputSize; y++) {
    const uint32_t sourceY = y0 + (static_cast<uint32_t>(y) * sourceHeight) /
                                      outputSize;
    for (uint16_t x = 0; x < outputSize; x++) {
      const uint32_t sourceX = x0 + (static_cast<uint32_t>(x) * sourceWidth) /
                                        outputSize;
      const uint8_t* source = rgb + (sourceY * width + sourceX) * 3;
      uint8_t* destination = output + (static_cast<uint32_t>(y) * outputSize + x) * 3;
      destination[0] = source[0];
      destination[1] = source[1];
      destination[2] = source[2];
    }
  }
}

void applyClassification(const Classification& classification, SlotResult& slot) {
  slot.present = false;
  slot.toolId = -1;
  slot.confidence = 0;
  std::snprintf(slot.label, sizeof(slot.label), "%s",
                classification.label ? classification.label : "unknown");

  if (classification.label == nullptr) return;

  if (std::strcmp(classification.label, "empty") == 0) {
    slot.confidence = classification.confidence;
    return;
  }

  int toolNumber = 0;
  if (std::sscanf(classification.label, "tool_%d", &toolNumber) == 1 &&
      toolNumber >= 1 && toolNumber <= camcfg::TOOL_COUNT) {
    slot.present = true;
    slot.toolId = static_cast<int8_t>(toolNumber - 1);
    slot.confidence = std::min<uint8_t>(classification.confidence, 100);
  }
}

void classifyFrame(camera_fb_t* frame, AnalysisResult& result) {
  const uint16_t inputSize = modelInputSize();
  if (inputSize == 0 || inputSize > 256) {
    Serial.println("[CAM] Tamaño de entrada de modelo inválido");
    return;
  }

  uint8_t* rgb = static_cast<uint8_t*>(
      ps_malloc(static_cast<size_t>(frame->width) * frame->height * 3));
  if (rgb == nullptr) {
    Serial.println("[CAM] Sin PSRAM para convertir el frame a RGB");
    return;
  }

  const bool converted =
      fmt2rgb888(frame->buf, frame->len, frame->format, rgb);
  if (!converted) {
    Serial.println("[CAM] No se pudo convertir el frame a RGB");
    free(rgb);
    return;
  }

  uint8_t* input =
      static_cast<uint8_t*>(malloc(static_cast<size_t>(inputSize) * inputSize * 3));
  if (input == nullptr) {
    Serial.println("[CAM] Sin memoria para la ROI del modelo");
    free(rgb);
    return;
  }

  for (uint8_t i = 0; i < camcfg::TOOL_COUNT; i++) {
    cropRoi(rgb, frame->width, frame->height, s_rois[i], input, inputSize);
    const Classification classification = classify(input, inputSize, inputSize);
    applyClassification(classification, result.slots[i]);
  }

  free(input);
  free(rgb);
}

void setBbox(SlotResult& slot, const Roi& roi, uint16_t width, uint16_t height) {
  slot.bbox[0] = static_cast<uint16_t>(roi.x * width);
  slot.bbox[1] = static_cast<uint16_t>(roi.y * height);
  slot.bbox[2] = static_cast<uint16_t>(roi.w * width);
  slot.bbox[3] = static_cast<uint16_t>(roi.h * height);
}

}  // namespace

bool begin() {
  setError("");
  if (!initCamera()) return false;

  if (!loadRois()) {
    Serial.println("[ROI] Se usarán las ROI por defecto");
  }

  s_ready = true;
  return true;
}

bool cameraReady() {
  return s_ready;
}

bool roisReady() {
  return s_roisReady;
}

bool classifierReady() {
  return classifierAvailable();
}

String roisJson() {
  JsonDocument document;
  document["image_size"][0] = s_frameWidth;
  document["image_size"][1] = s_frameHeight;

  JsonObject rois = document["rois"].to<JsonObject>();
  for (uint8_t i = 0; i < camcfg::TOOL_COUNT; i++) {
    char name[3];
    slotName(i, name);
    JsonArray values = rois[name].to<JsonArray>();
    values.add(s_rois[i].x);
    values.add(s_rois[i].y);
    values.add(s_rois[i].w);
    values.add(s_rois[i].h);
  }

  String output;
  serializeJson(document, output);
  return output;
}

bool setRoisJson(const String& body) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    setError("JSON de ROI inválido");
    return false;
  }

  JsonObject rois = document["rois"].is<JsonObject>()
                        ? document["rois"].as<JsonObject>()
                        : document.as<JsonObject>();
  if (rois.isNull()) {
    setError("Falta el objeto rois");
    return false;
  }

  Roi loaded[camcfg::TOOL_COUNT];
  for (uint8_t i = 0; i < camcfg::TOOL_COUNT; i++) {
    char name[3];
    slotName(i, name);
    JsonArray values = rois[name];
    if (values.isNull() || values.size() != 4) {
      setError("Faltan ROI o tienen un formato inválido");
      return false;
    }

    Roi candidate{};
    candidate.x = values[0] | 0.0f;
    candidate.y = values[1] | 0.0f;
    candidate.w = values[2] | 0.0f;
    candidate.h = values[3] | 0.0f;

    const bool valid = candidate.x >= 0.0f && candidate.y >= 0.0f &&
                       candidate.w > 0.0f && candidate.h > 0.0f &&
                       candidate.x + candidate.w <= 1.0f + ROI_EPSILON &&
                       candidate.y + candidate.h <= 1.0f + ROI_EPSILON;
    if (!valid) {
      setError("ROI fuera de rango");
      return false;
    }
    loaded[i] = candidate;
  }

  Roi previous[camcfg::TOOL_COUNT];
  const bool previousReady = s_roisReady;
  std::memcpy(previous, s_rois, sizeof(s_rois));
  std::memcpy(s_rois, loaded, sizeof(s_rois));
  s_roisReady = true;
  if (!saveRois()) {
    std::memcpy(s_rois, previous, sizeof(s_rois));
    s_roisReady = previousReady;
    setError("No se pudo guardar rois.json");
    return false;
  }

  setError("");
  return true;
}

bool captureLatest() {
  setError("");
  if (!s_ready) {
    setError("Cámara no inicializada");
    return false;
  }

  camera_fb_t* frame = esp_camera_fb_get();
  if (frame == nullptr) {
    setError("No se pudo capturar el frame");
    return false;
  }

  s_frameWidth = frame->width;
  s_frameHeight = frame->height;
  const bool saved = savePhoto(frame);
  esp_camera_fb_return(frame);
  if (!saved) {
    setError("No se pudo guardar latest.jpg");
  }
  return saved;
}

bool analyze(AnalysisResult& result) {
  setError("");
  if (!s_ready) {
    setError("Cámara no inicializada");
    return false;
  }

  const uint32_t started = millis();
  camera_fb_t* frame = esp_camera_fb_get();
  if (frame == nullptr) {
    setError("No se pudo capturar el frame");
    return false;
  }

  s_frameWidth = frame->width;
  s_frameHeight = frame->height;

  if (!savePhoto(frame)) {
    setError("No se pudo guardar latest.jpg");
    esp_camera_fb_return(frame);
    return false;
  }

  result = AnalysisResult{};
  result.width = frame->width;
  result.height = frame->height;

  const time_t now = time(nullptr);
  if (now > 1700000000) {
    result.capturedAt = static_cast<uint32_t>(now);
  }

  for (uint8_t i = 0; i < camcfg::TOOL_COUNT; i++) {
    SlotResult& slot = result.slots[i];
    slot.present = false;
    slot.toolId = -1;
    slot.confidence = 0;
    std::snprintf(slot.label, sizeof(slot.label), "unknown");
    setBbox(slot, s_rois[i], frame->width, frame->height);
  }

  if (classifierAvailable()) {
    classifyFrame(frame, result);
  }

  esp_camera_fb_return(frame);

  result.ok = true;
  result.processingMs = millis() - started;

  const String json = resultJson(result);
  if (!writeStringAtomic(camcfg::LAST_RESULT_FILE, json)) {
    Serial.println("[CAM] No se pudo guardar last_result.json");
  }

  return true;
}

String resultJson(const AnalysisResult& result) {
  JsonDocument document;
  document["ok"] = result.ok;
  document["capturedAt"] = result.capturedAt;
  document["processingMs"] = result.processingMs;
  document["modelVersion"] = modelName();
  document["width"] = result.width;
  document["height"] = result.height;

  JsonArray slots = document["slots"].to<JsonArray>();
  for (uint8_t i = 0; i < camcfg::TOOL_COUNT; i++) {
    const SlotResult& slot = result.slots[i];
    char name[3];
    slotName(i, name);

    JsonObject item = slots.add<JsonObject>();
    item["slot"] = name;
    item["row"] = i / camcfg::GRID_COLS;
    item["column"] = i % camcfg::GRID_COLS;
    item["present"] = slot.present;
    item["toolId"] = slot.toolId;
    item["label"] = slot.label;
    item["confidence"] = static_cast<float>(slot.confidence) / 100.0f;

    JsonArray bbox = item["bbox"].to<JsonArray>();
    bbox.add(slot.bbox[0]);
    bbox.add(slot.bbox[1]);
    bbox.add(slot.bbox[2]);
    bbox.add(slot.bbox[3]);
  }

  String output;
  serializeJson(document, output);
  return output;
}

const char* lastError() {
  return s_lastError;
}

}  // namespace camvision
