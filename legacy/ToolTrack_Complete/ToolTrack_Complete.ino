/*
 * ============================================================
 *  TOOLTRACK + CERRADURA RFID — SIN DELAY (PRO)
 * ============================================================
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include "web.h"

// WIFI
const char* WIFI_SSID = "ELO-FOREST";
const char* WIFI_PASSWORD = "Elo.forest1935.2024";

// RFID USERS
struct RFIDUser { const char* uid; const char* name; };
const RFIDUser USERS[] = {
  { "1A212828", "Benjamin V." },
  { "A97EC6B2", "Arturo F." },
  { "C29A713D", "Mario M." }
};
const int NUM_USERS = sizeof(USERS) / sizeof(USERS[0]);

// PINES
#define RFID_SS_PIN   5
#define RFID_RST_PIN  27

#define PIN_LED_VERDE 26
#define PIN_LED_ROJO  25
#define PIN_BUZZER    33
#define PIN_SOLENOIDE 12

#define LED_BUILTIN   2

const uint8_t IR_PINS[12] = {
  13, 14, 16, 17,
  21, 22, 32, 34,
  35, 4, 36, 39
};

// TIEMPOS
#define TIEMPO_SOLENOIDE 3000
#define TIEMPO_BEEP_OK   1000
#define TIEMPO_ALERTA    3000
#define INTERVALO_PARPADEO 250
#define PENDING_TIMEOUT_MS 10000
#define DEBOUNCE_MS 80

// OBJETOS
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
AsyncWebServer server(80);

// ESTADOS
bool sistemaDesbloqueado = false;

// CONTROL NO BLOQUEANTE
bool solenoideActivo = false;
unsigned long solenoideInicio = 0;

bool alertaActiva = false;
unsigned long alertaInicio = 0;
unsigned long ultimoParpadeo = 0;
bool estadoAlerta = false;

bool beepActivo = false;
unsigned long beepInicio = 0;

// IR
bool lastIR[12];
unsigned long debounceMs[12];

// RFID pendiente
char pendingUID[20] = "";
char pendingUser[32] = "";
unsigned long pendingMs = 0;

// ============================================================
// UTILIDADES
// ============================================================
String uidToString(byte* buf, byte len) {
  String s = "";
  for (byte i = 0; i < len; i++) {
    if (buf[i] < 0x10) s += "0";
    s += String(buf[i], HEX);
  }
  s.toUpperCase();
  return s;
}

const char* findUser(const String& uid) {
  for (int i = 0; i < NUM_USERS; i++)
    if (uid == USERS[i].uid) return USERS[i].name;
  return nullptr;
}

// ============================================================
// ACCESO
// ============================================================
void accesoPermitido(const char* user) {
  digitalWrite(PIN_LED_VERDE, HIGH);
  digitalWrite(PIN_LED_ROJO, LOW);

  // beep
  beepActivo = true;
  beepInicio = millis();
  digitalWrite(PIN_BUZZER, HIGH);

  // solenoide
  solenoideActivo = true;
  solenoideInicio = millis();
  digitalWrite(PIN_SOLENOIDE, HIGH);

  sistemaDesbloqueado = true;
}

void accesoDenegado() {
  alertaActiva = true;
  alertaInicio = millis();
  ultimoParpadeo = millis();
  estadoAlerta = true;

  sistemaDesbloqueado = false;
}

// ============================================================
// RFID
// ============================================================
void handleRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid = uidToString(rfid.uid.uidByte, rfid.uid.size);
  const char* user = findUser(uid);

  if (user) {
    strncpy(pendingUID, uid.c_str(), sizeof(pendingUID)-1);
    strncpy(pendingUser, user, sizeof(pendingUser)-1);
    pendingMs = millis();

    accesoPermitido(user);

  } else {
    accesoDenegado();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ============================================================
// IR
// ============================================================
void handleIR() {
  if (!sistemaDesbloqueado) return;

  for (int i = 0; i < 12; i++) {

    bool reading = (digitalRead(IR_PINS[i]) == LOW);

    if (reading == lastIR[i]) {
      debounceMs[i] = millis();
      continue;
    }

    if ((millis() - debounceMs[i]) < DEBOUNCE_MS) continue;

    lastIR[i] = reading;

    if (!reading) {
      if ((millis() - pendingMs) < PENDING_TIMEOUT_MS) {
        pendingUID[0] = '\0';
        pendingUser[0] = '\0';
      }
    }
  }
}

// ============================================================
// UPDATE NO BLOQUEANTE
// ============================================================
void updateSistema() {

  // SOLENOIDE
  if (solenoideActivo && millis() - solenoideInicio > TIEMPO_SOLENOIDE) {
    digitalWrite(PIN_SOLENOIDE, LOW);
    digitalWrite(PIN_LED_VERDE, LOW);
    solenoideActivo = false;
  }

  // BEEP
  if (beepActivo && millis() - beepInicio > TIEMPO_BEEP_OK) {
    digitalWrite(PIN_BUZZER, LOW);
    beepActivo = false;
  }

  // ALERTA
  if (alertaActiva) {

    if (millis() - ultimoParpadeo > INTERVALO_PARPADEO) {
      estadoAlerta = !estadoAlerta;
      digitalWrite(PIN_LED_ROJO, estadoAlerta);
      digitalWrite(PIN_BUZZER, estadoAlerta);
      ultimoParpadeo = millis();
    }

    if (millis() - alertaInicio > TIEMPO_ALERTA) {
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      alertaActiva = false;
    }
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {

  Serial.begin(115200);

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SOLENOIDE, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  for (int i = 0; i < 12; i++) {

    if (IR_PINS[i] >= 34)
      pinMode(IR_PINS[i], INPUT);
    else
      pinMode(IR_PINS[i], INPUT_PULLUP);

    lastIR[i] = digitalRead(IR_PINS[i]);
  }

  SPI.begin(18, 19, 23, RFID_SS_PIN);
  rfid.PCD_Init();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  server.begin();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  handleRFID();
  handleIR();
  updateSistema();
}