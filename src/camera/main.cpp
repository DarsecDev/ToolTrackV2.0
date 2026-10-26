#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_camera.h>

#include "camera_config.h"

namespace {

uint32_t lastCapture = 0;

void printBanner() {
  Serial.println();
  Serial.println("============================================================");
  Serial.printf("  %s %s\n", camcfg::PROJECT_NAME, camcfg::FIRMWARE_VERSION);
  Serial.println("  Nodo de visión (XIAO ESP32-S3 Sense)");
  Serial.println("============================================================");
  Serial.printf("  Flash : %u MB\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)));
  if (psramFound()) {
    Serial.printf("  PSRAM : %u MB\n",
                  static_cast<unsigned>(ESP.getPsramSize() / (1024 * 1024)));
  } else {
    Serial.println("  PSRAM : no detectada (la cámara funcionará en VGA)");
  }
  Serial.println("------------------------------------------------------------");
}

bool mountStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] Error al montar LittleFS");
    return false;
  }
  Serial.printf("[FS] LittleFS: %u KB usados de %u KB\n",
                static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                static_cast<unsigned>(LittleFS.totalBytes() / 1024));
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
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;  // 1600x1200 para recortar 12 zonas
    config.jpeg_quality = camcfg::ANALYSIS_JPEG_QUALITY;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Error de inicialización: 0x%x (%s)\n", err,
                  esp_err_to_name(err));
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    Serial.printf("[CAM] Sensor PID=0x%02x, framesize=%d\n", sensor->id.PID,
                  sensor->status.framesize);
  }

  Serial.println("[CAM] Cámara inicializada");
  return true;
}

void connectWiFi() {
  if (!camcfg::ENABLE_WIFI) {
    Serial.println("[WIFI] Deshabilitado en esta fase");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(camcfg::HOSTNAME);

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(20);
  wifiManager.setHostname(camcfg::HOSTNAME);

  Serial.println("[WIFI] Conectando (portal 'ToolTrack-Cam' si falla)...");
  if (!wifiManager.autoConnect("ToolTrack-Cam")) {
    Serial.println("[WIFI] Sin conexión; el nodo sigue en modo local");
    return;
  }

  Serial.printf("[WIFI] Conectado a '%s' con IP %s\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str());
}

void captureTestFrame() {
  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr) {
    Serial.println("[CAM] No se pudo capturar el frame");
    return;
  }

  Serial.printf("[CAM] Frame %ux%u, %u bytes, heap=%u\n", frame->width,
                frame->height, static_cast<unsigned>(frame->len),
                static_cast<unsigned>(ESP.getFreeHeap()));
  esp_camera_fb_return(frame);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  printBanner();

  mountStorage();
  connectWiFi();

  if (!initCamera()) {
    Serial.println("[CAM] Nodo sin cámara; revisar conexión del módulo");
  }
}

void loop() {
  if ((millis() - lastCapture) >= camcfg::CAPTURE_INTERVAL_MS) {
    lastCapture = millis();
    captureTestFrame();
  }
}
