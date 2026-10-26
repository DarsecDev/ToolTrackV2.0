#pragma once

#include <stdint.h>

namespace camvision {

/**
 * Resultado de clasificar una ROI.
 *
 * `label` es una etiqueta del modelo: "empty", "tool_1" ... "tool_12".
 * `confidence` se expresa en porcentaje (0-100).
 */
struct Classification {
  const char* label;
  uint8_t confidence;
};

/** true si el firmware incluye un modelo de inferencia utilizable. */
bool classifierAvailable();

/** Nombre/versión del modelo o "none" si todavía no hay modelo. */
const char* modelName();

/** Lado de la imagen cuadrada que espera el modelo (por ejemplo 96). */
uint16_t modelInputSize();

/**
 * Clasifica una imagen RGB de `width * height * 3` bytes.
 *
 * El stub actual no tiene modelo y devuelve confianza 0. Cuando se integre
 * Edge Impulse, esta función se sustituye por la llamada a run_classifier()
 * sin cambiar el resto del nodo de visión.
 */
Classification classify(const uint8_t* rgb, uint16_t width, uint16_t height);

}  // namespace camvision
