#pragma once

#include <stdint.h>

/**
 * Configuración central del firmware maestro (ESP32-S3 DevKitC-1 N16R8).
 *
 * Reglas de pines:
 *  - GPIO 19 y 20: USB nativo (no usar).
 *  - GPIO 26 a 37: flash y PSRAM octal (no usar).
 *  - GPIO 43 y 44: UART0 / consola (no usar para periféricos).
 *  - GPIO 0, 45 y 46: pines de arranque (evitarlos salvo necesidad).
 */
namespace cfg {

// ------------------------------------------------------------
//  Identidad
// ------------------------------------------------------------
constexpr char PROJECT_NAME[]     = "ToolTrack";
constexpr char FIRMWARE_VERSION[] = "0.1.0";
constexpr char HARDWARE_REVISION[] = "v2-dev";

// Hostnames mDNS
constexpr char MAIN_HOSTNAME[]   = "tooltrack";
constexpr char CAMERA_HOSTNAME[] = "tooltrack-cam";

// ------------------------------------------------------------
//  Matriz de herramientas 4x3
// ------------------------------------------------------------
constexpr uint8_t TOOL_COUNT = 12;
constexpr uint8_t GRID_ROWS  = 4;
constexpr uint8_t GRID_COLS  = 3;

// ------------------------------------------------------------
//  Sensores infrarrojos (12)
//  LOW  = herramienta presente
//  HIGH = herramienta ausente
// ------------------------------------------------------------
constexpr uint8_t PIN_IR[TOOL_COUNT] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
};
constexpr bool     IR_ACTIVE_LOW     = true;
constexpr uint32_t IR_DEBOUNCE_MS    = 80;
constexpr uint32_t IR_STABLE_MS      = 1500;

// ------------------------------------------------------------
//  Lector RFID RC522 (SPI)
// ------------------------------------------------------------
constexpr uint8_t  PIN_RFID_SCK  = 13;
constexpr uint8_t  PIN_RFID_MISO = 14;
constexpr uint8_t  PIN_RFID_MOSI = 15;
constexpr uint8_t  PIN_RFID_SS   = 16;
constexpr uint8_t  PIN_RFID_RST  = 17;
constexpr uint32_t RFID_SCAN_MS  = 50;

// ------------------------------------------------------------
//  Actuadores
// ------------------------------------------------------------
constexpr uint8_t  PIN_LOCK       = 18;  // MOSFET cerradura 12 V: HIGH = cerrada (fail-safe)
constexpr uint8_t  PIN_LED_GREEN  = 21;
constexpr uint8_t  PIN_LED_RED    = 47;
constexpr uint8_t  PIN_BUZZER     = 39;
constexpr uint8_t  PIN_LED_STRIP  = 40;  // MOSFET de la lámpara LED 12 V
constexpr uint8_t  PIN_AC_SENSE   = 41;  // Detección de corte de red (opcional)

constexpr uint32_t LOCK_PULSE_MS    = 3000;  // tiempo que permanece abierta
constexpr uint32_t LED_STRIP_ON_MS  = 60000;
constexpr uint32_t BEEP_MS          = 150;
constexpr uint32_t ALERT_BLINK_MS   = 250;
constexpr uint32_t ALERT_TOTAL_MS   = 3000;

// ------------------------------------------------------------
//  Nodo de visión
// ------------------------------------------------------------
constexpr bool     FEATURE_CAMERA      = true;
constexpr char     CAMERA_BASE_URL[]   = "http://tooltrack-cam.local";
constexpr uint16_t CAMERA_HTTP_PORT    = 80;
constexpr uint32_t CAMERA_TIMEOUT_MS   = 8000;
constexpr uint32_t CAMERA_SETTLE_MS    = 1500;  // espera de IR estable antes de capturar
constexpr uint8_t  CAMERA_MIN_CONFIDENCE = 60;  // porcentaje mínimo para validar

// ------------------------------------------------------------
//  Almacenamiento
// ------------------------------------------------------------
constexpr uint16_t MAX_EVENTS        = 1000;
constexpr char     EVENTS_FILE[]     = "/events.jsonl";
constexpr char     USERS_FILE[]      = "/users.json";
constexpr char     TOOLS_FILE[]      = "/tools.json";
constexpr char     SETTINGS_FILE[]   = "/settings.json";
constexpr char     LAST_PHOTO_FILE[] = "/last.jpg";
constexpr char     LAST_RESULT_FILE[] = "/last_result.json";

// ------------------------------------------------------------
//  Funciones opcionales
// ------------------------------------------------------------
constexpr bool FEATURE_TELEGRAM = false;  // se activa cuando haya tiempo
constexpr bool FEATURE_AC_SENSE = false;  // requiere hardware del PCB
constexpr bool AC_SENSE_ACTIVE_HIGH = true;  // HIGH = red presente
constexpr bool FEATURE_OTA      = false;  // actualización por USB en esta versión

// ------------------------------------------------------------
//  Seguridad
// ------------------------------------------------------------
constexpr uint32_t ADMIN_SESSION_MS = 60UL * 60UL * 1000UL;  // 1 hora
constexpr uint8_t  ADMIN_MAX_ATTEMPTS = 5;
constexpr uint32_t ADMIN_LOCKOUT_MS = 60UL * 1000UL;

}  // namespace cfg
