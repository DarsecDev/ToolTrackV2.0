/*
 * ============================================================
 *  CERRADURA ELECTRÓNICA CON RFID - RC522
 *  Adaptado para ESP32 de 30 pines
 * ============================================================
 *  Conexiones RFID-RC522 (SPI):
 *    SDA (SS) -> GPIO 5
 *    SCK      -> GPIO 18
 *    MOSI     -> GPIO 23
 *    MISO     -> GPIO 19
 *    RST      -> GPIO 4
 *    3.3V     -> 3.3V  ¡NO conectar a 5V!
 *    GND      -> GND
 *
 *  Otros componentes:
 *    LED Verde  -> GPIO 26
 *    LED Rojo   -> GPIO 27
 *    Buzzer     -> GPIO 25
 *    Solenoide  -> GPIO 33  (usar transistor o relé, el ESP32
 *                            NO puede manejar la corriente directamente)
 *
 *  Librería necesaria: MFRC522 by GithubCommunity
 * ============================================================
 */

#include <SPI.h>
#include <MFRC522.h>

// ---------- Pines ESP32 ----------
#define PIN_SS        5
#define PIN_RST       4
#define PIN_LED_VERDE 26
#define PIN_LED_ROJO  27
#define PIN_BUZZER    25
#define PIN_SOLENOIDE 33

// ---------- Tiempos ----------
#define TIEMPO_SOLENOIDE    3000
#define TIEMPO_BEEP_OK      1000
#define TIEMPO_ALERTA       3000
#define INTERVALO_PARPADEO   250

// ---------- Objeto RFID ----------
MFRC522 rfid(PIN_SS, PIN_RST);

// ============================================================
//  USUARIOS AUTORIZADOS
// ============================================================
#define NUM_USUARIOS  3
#define LONGITUD_UID  4

byte usuariosAutorizados[NUM_USUARIOS][LONGITUD_UID] = {
  {0x1A, 0x21, 0x28, 0x28},   // Usuario 1
  {0xFA, 0xCE, 0x01, 0x02},   // Usuario 2
  {0xAB, 0xCD, 0xEF, 0x12}    // Usuario 3
};

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);   // ESP32 usa 115200 por defecto
  SPI.begin(18, 19, 23, PIN_SS);  // SCK, MISO, MOSI, SS — explícito para ESP32
  rfid.PCD_Init();

  pinMode(PIN_LED_VERDE,  OUTPUT);
  pinMode(PIN_LED_ROJO,   OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  pinMode(PIN_SOLENOIDE,  OUTPUT);

  digitalWrite(PIN_LED_VERDE,  LOW);
  digitalWrite(PIN_LED_ROJO,   LOW);
  digitalWrite(PIN_BUZZER,     LOW);
  digitalWrite(PIN_SOLENOIDE,  LOW);

  Serial.println("=== Cerradura RFID lista (ESP32) ===");
  Serial.println("Acerca una tarjeta o token...");
}

// ============================================================
//  LOOP PRINCIPAL
// ============================================================
void loop() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  byte uid[LONGITUD_UID];
  for (byte i = 0; i < LONGITUD_UID; i++) {
    uid[i] = rfid.uid.uidByte[i];
  }

  Serial.print("UID detectado: ");
  for (byte i = 0; i < LONGITUD_UID; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i < LONGITUD_UID - 1) Serial.print(":");
  }
  Serial.println();

  if (esUsuarioAutorizado(uid)) {
    Serial.println(">> Acceso CONCEDIDO");
    accesoPermitido();
  } else {
    Serial.println(">> Acceso DENEGADO");
    accesoDenegado();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  Serial.println("Acerca una tarjeta o token...");
}

// ============================================================
//  FUNCIONES
// ============================================================
bool esUsuarioAutorizado(byte* uid) {
  for (byte u = 0; u < NUM_USUARIOS; u++) {
    bool coincide = true;
    for (byte b = 0; b < LONGITUD_UID; b++) {
      if (uid[b] != usuariosAutorizados[u][b]) {
        coincide = false;
        break;
      }
    }
    if (coincide) return true;
  }
  return false;
}

void accesoPermitido() {
  digitalWrite(PIN_LED_VERDE, HIGH);
  digitalWrite(PIN_LED_ROJO,  LOW);

  digitalWrite(PIN_BUZZER, HIGH);
  delay(TIEMPO_BEEP_OK);
  digitalWrite(PIN_BUZZER, LOW);

  digitalWrite(PIN_SOLENOIDE, HIGH);
  delay(TIEMPO_SOLENOIDE);
  digitalWrite(PIN_SOLENOIDE, LOW);

  digitalWrite(PIN_LED_VERDE, LOW);
}

void accesoDenegado() {
  digitalWrite(PIN_LED_VERDE,  LOW);
  digitalWrite(PIN_SOLENOIDE,  LOW);

  unsigned long inicio = millis();
  bool estado = HIGH;

  while (millis() - inicio < TIEMPO_ALERTA) {
    digitalWrite(PIN_LED_ROJO, estado);
    digitalWrite(PIN_BUZZER,   estado);
    estado = !estado;
    delay(INTERVALO_PARPADEO);
  }

  digitalWrite(PIN_LED_ROJO, LOW);
  digitalWrite(PIN_BUZZER,   LOW);
}