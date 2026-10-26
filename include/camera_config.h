#pragma once

#include <stdint.h>

/**
 * Configuración del nodo de visión (Seeed XIAO ESP32-S3 Sense).
 *
 * La cámara usa pines internos de la placa; los pines D0-D10 quedan libres
 * para futuras señales (por ejemplo, sincronización o sensor de puerta).
 */
namespace camcfg {

constexpr char PROJECT_NAME[]     = "ToolTrack Camera";
constexpr char FIRMWARE_VERSION[] = "0.1.0";
constexpr char HOSTNAME[]         = "tooltrack-cam";
constexpr uint16_t HTTP_PORT      = 80;

constexpr bool ENABLE_WIFI = true;
constexpr uint32_t WIFI_PORTAL_TIMEOUT_MS = 180000;

constexpr uint8_t TOOL_COUNT = 12;
constexpr uint8_t GRID_ROWS  = 4;
constexpr uint8_t GRID_COLS  = 3;

constexpr char LAST_PHOTO_FILE[]  = "/last.jpg";
constexpr char LAST_RESULT_FILE[] = "/last_result.json";
constexpr char ROIS_FILE[]        = "/rois.json";

// ------------------------------------------------------------
//  Pines internos de la cámara OV2640 (XIAO ESP32-S3 Sense)
// ------------------------------------------------------------
constexpr int PIN_PWDN     = -1;
constexpr int PIN_RESET    = -1;
constexpr int PIN_XCLK     = 10;
constexpr int PIN_SIOD     = 40;
constexpr int PIN_SIOC     = 39;
constexpr int PIN_D7       = 48;
constexpr int PIN_D6       = 11;
constexpr int PIN_D5       = 12;
constexpr int PIN_D4       = 14;
constexpr int PIN_D3       = 16;
constexpr int PIN_D2       = 18;
constexpr int PIN_D1       = 17;
constexpr int PIN_D0       = 15;
constexpr int PIN_VSYNC    = 38;
constexpr int PIN_HREF     = 47;
constexpr int PIN_PCLK     = 13;

constexpr int XCLK_FREQ_HZ = 20000000;

// ------------------------------------------------------------
//  Análisis
// ------------------------------------------------------------
constexpr uint16_t ANALYSIS_JPEG_QUALITY = 10;
constexpr uint32_t CAPTURE_INTERVAL_MS   = 5000;
constexpr uint8_t  MIN_CONFIDENCE        = 60;  // porcentaje

}  // namespace camcfg
