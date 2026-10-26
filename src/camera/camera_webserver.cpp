#include "camera_webserver.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "camera_classifier.h"
#include "camera_config.h"
#include "camera_vision.h"

namespace camws {
namespace {

constexpr size_t MAX_BODY = 8192;

AsyncWebServer s_server(camcfg::HTTP_PORT);
bool s_analyzing = false;

void sendJson(AsyncWebServerRequest* request, int code, const String& body) {
  AsyncWebServerResponse* response =
      request->beginResponse(code, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void sendError(AsyncWebServerRequest* request, int code, const char* message) {
  JsonDocument document;
  document["error"] = message;
  if (code >= 500 && camvision::lastError()[0] != '\0') {
    document["detail"] = camvision::lastError();
  }

  String output;
  serializeJson(document, output);
  sendJson(request, code, output);
}

void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t length,
                 size_t index, size_t total) {
  if (index == 0) {
    String* body = new String();
    body->reserve(total < MAX_BODY + 1 ? total : MAX_BODY + 1);
    request->_tempObject = body;
  }

  String* body = static_cast<String*>(request->_tempObject);
  if (body == nullptr) return;

  if (body->length() <= MAX_BODY) {
    const size_t room = MAX_BODY + 1 - body->length();
    const size_t copy = length < room ? length : room;
    body->concat(reinterpret_cast<const char*>(data), copy);
  }
}

String takeBody(AsyncWebServerRequest* request) {
  String* body = static_cast<String*>(request->_tempObject);
  if (body == nullptr) return String();

  const String value = *body;
  delete body;
  request->_tempObject = nullptr;
  return value;
}

String healthJson() {
  JsonDocument document;
  const bool cameraReady = camvision::cameraReady();
  document["ok"] = cameraReady;
  document["firmware"] = camcfg::FIRMWARE_VERSION;
  document["role"] = "camera";
  document["uptime"] = millis() / 1000;
  document["camera"] = cameraReady;
  document["rois"] = camvision::roisReady();
  document["modelAvailable"] = camvision::classifierReady();
  document["modelVersion"] = camvision::modelName();
  document["lastPhoto"] = LittleFS.exists(camcfg::LAST_PHOTO_FILE);
  document["lastResult"] = LittleFS.exists(camcfg::LAST_RESULT_FILE);
  document["wifiConnected"] = WiFi.status() == WL_CONNECTED;
  document["ip"] = WiFi.localIP().toString();
  document["rssi"] = WiFi.RSSI();
  document["heap"] = ESP.getFreeHeap();
  document["psram"] = ESP.getFreePsram();

  String output;
  serializeJson(document, output);
  return output;
}

}  // namespace

void begin() {
  s_server.on("/health", HTTP_GET, [](AsyncWebServerRequest* request) {
    sendJson(request, camvision::cameraReady() ? 200 : 503, healthJson());
  });

  s_server.on("/analyze", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (s_analyzing) {
      sendError(request, 409, "Ya hay un análisis en curso");
      return;
    }
    if (!camvision::cameraReady()) {
      sendError(request, 503, "Cámara no disponible");
      return;
    }

    s_analyzing = true;
    camvision::AnalysisResult result;
    const bool ok = camvision::analyze(result);
    s_analyzing = false;

    if (!ok) {
      sendError(request, 500, "No se pudo analizar el frame");
      return;
    }

    sendJson(request, 200, camvision::resultJson(result));
  });

  s_server.on("/capture.jpg", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (s_analyzing) {
      sendError(request, 409, "Hay un análisis en curso");
      return;
    }
    if (!camvision::cameraReady()) {
      sendError(request, 503, "Cámara no disponible");
      return;
    }

    s_analyzing = true;
    const bool ok = camvision::captureLatest();
    s_analyzing = false;
    if (!ok) {
      sendError(request, 500, "No se pudo capturar la foto");
      return;
    }

    AsyncWebServerResponse* response =
        request->beginResponse(LittleFS, camcfg::LAST_PHOTO_FILE, "image/jpeg");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  s_server.on("/latest.jpg", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!LittleFS.exists(camcfg::LAST_PHOTO_FILE)) {
      sendError(request, 404, "Sin foto disponible");
      return;
    }
    AsyncWebServerResponse* response =
        request->beginResponse(LittleFS, camcfg::LAST_PHOTO_FILE, "image/jpeg");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  s_server.on("/latest.json", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!LittleFS.exists(camcfg::LAST_RESULT_FILE)) {
      sendError(request, 404, "Sin análisis disponible");
      return;
    }
    AsyncWebServerResponse* response = request->beginResponse(
        LittleFS, camcfg::LAST_RESULT_FILE, "application/json");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  s_server.on("/rois", HTTP_GET, [](AsyncWebServerRequest* request) {
    sendJson(request, 200, camvision::roisJson());
  });

  s_server.on(
      "/rois", HTTP_PUT,
      [](AsyncWebServerRequest* request) {
        if (s_analyzing) {
          sendError(request, 409, "Hay un análisis en curso");
          return;
        }

        const String body = takeBody(request);
        if (body.length() == 0) {
          sendError(request, 400, "Cuerpo JSON vacío");
          return;
        }
        if (body.length() > MAX_BODY) {
          sendError(request, 413, "Cuerpo demasiado grande");
          return;
        }
        if (!camvision::setRoisJson(body)) {
          sendError(request, 400, "ROI inválidas");
          return;
        }
        sendJson(request, 200, camvision::roisJson());
      },
      nullptr, collectBody);

  s_server.onNotFound([](AsyncWebServerRequest* request) {
    sendError(request, 404, "No encontrado");
  });

  s_server.begin();
  Serial.printf("[WEB] Nodo de visión escuchando en el puerto %u\n",
                static_cast<unsigned>(camcfg::HTTP_PORT));
}

}  // namespace camws
