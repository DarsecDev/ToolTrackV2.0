#include "camera_classifier.h"

namespace camvision {

bool classifierAvailable() {
  // La inferencia real se añade cuando exista dataset/modelo.
  // No se devuelven detecciones simuladas: sin modelo, todas las ROI quedan
  // como desconocidas y la placa maestra sigue decidiendo con los IR.
  return false;
}

const char* modelName() {
  return "none";
}

uint16_t modelInputSize() {
  return 96;
}

Classification classify(const uint8_t* rgb, uint16_t width, uint16_t height) {
  (void)rgb;
  (void)width;
  (void)height;
  return {"unknown", 0};
}

}  // namespace camvision
