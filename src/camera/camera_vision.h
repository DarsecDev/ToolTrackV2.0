#pragma once

#include <Arduino.h>

#include "camera_config.h"

namespace camvision {

struct Roi {
  float x;
  float y;
  float w;
  float h;
};

struct SlotResult {
  bool present;
  int8_t toolId;
  uint8_t confidence;
  uint16_t bbox[4];
  char label[20];
};

struct AnalysisResult {
  bool ok;
  uint32_t processingMs;
  uint32_t capturedAt;
  uint16_t width;
  uint16_t height;
  SlotResult slots[camcfg::TOOL_COUNT];
};

/** Inicializa la cámara y carga (o crea) la configuración de ROI. */
bool begin();

bool cameraReady();
bool roisReady();
bool classifierReady();

/** Configuración de ROI en el mismo formato que tools/calibrate_rois.py. */
String roisJson();
bool setRoisJson(const String& body);

/**
 * Captura un frame, guarda `last.jpg`, ejecuta el clasificador si existe y
 * guarda `last_result.json`. Sin modelo devuelve 12 ranuras desconocidas.
 */
bool analyze(AnalysisResult& result);

/** Captura un frame y actualiza `last.jpg` sin ejecutar el clasificador. */
bool captureLatest();

/** Serializa el resultado con el contrato que consume la placa maestra. */
String resultJson(const AnalysisResult& result);

const char* lastError();

}  // namespace camvision
