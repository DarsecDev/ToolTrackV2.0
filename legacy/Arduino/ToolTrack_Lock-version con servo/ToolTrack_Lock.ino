/*
 * ============================================================
 *  CERRADURA ELECTRÓNICA CON RFID - RC522
 * ============================================================
 *  Conexiones RFID-RC522 (SPI):
 *    SDA   -> Pin 10  (SS)
 *    SCK   -> Pin 13
 *    MOSI  -> Pin 11
 *    MISO  -> Pin 12
 *    RST   -> Pin 9
 *    3.3V  -> 3.3V  ¡NO conectar a 5V!
 *
 *  Otros componentes:
 *    LED Verde   -> Pin 7
 *    LED Rojo    -> Pin 6
 *    Buzzer      -> Pin 5
 *    Solenoide   -> Pin 4
 *
 *  Librería necesaria: MFRC522 by GithubCommunity
 * ============================================================
 */

#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

// ---------- Pines ----------
#define PIN_SS        10
#define PIN_RST        9
#define PIN_LED_VERDE  7
#define PIN_LED_ROJO   6
#define PIN_BUZZER     5
#define PIN_SOLENOIDE  4
#define SERVO_PIN     3
#define SERVO_LOCK     0
#define SERVO_OPEN    90

// ---------- Tiempos ----------
#define TIEMPO_SOLENOIDE    3000  // ms que el solenoide permanece abierto
#define TIEMPO_BEEP_OK      1000  // ms del beep largo en acceso correcto
#define TIEMPO_ALERTA       3000  // ms totales de alerta en acceso denegado
#define INTERVALO_PARPADEO   250  // ms ON/OFF en modo alerta (2 parpadeos/s)

// ---------- Objeto SERVO ---------
Servo servoLock;

// ---------- Objeto RFID ----------
MFRC522 rfid(PIN_SS, PIN_RST);

// ============================================================
//  USUARIOS AUTORIZADOS
//  Para agregar un usuario:
//    1. Lee su tarjeta con el Monitor Serie (9600 baudios)
//    2. Copia el UID que aparece
//    3. Pégalo como nueva fila en el arreglo
//    4. Actualiza NUM_USUARIOS
// ============================================================
#define NUM_USUARIOS  3
#define LONGITUD_UID  4

byte usuariosAutorizados[NUM_USUARIOS][LONGITUD_UID] = {
  {0x1A, 0x21, 0x28, 0x28},   // Usuario 1 <- reemplaza con tu UID
  {0xFA, 0xCE, 0x01, 0x02},   // Usuario 2 <- reemplaza con tu UID
  {0xFA, 0xCE, 0x01, 0x02}    // Usuario 3 <- reemplaza con tu UID
};

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  servoLock.attach(SERVO_PIN);

  servoLock.write(SERVO_LOCK);

  pinMode(PIN_LED_VERDE,  OUTPUT);
  pinMode(PIN_LED_ROJO,   OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  pinMode(PIN_SOLENOIDE,  OUTPUT);

  // Estado inicial: todo apagado
  digitalWrite(PIN_LED_VERDE,  LOW);
  digitalWrite(PIN_LED_ROJO,   LOW);
  digitalWrite(PIN_BUZZER,     LOW);
  digitalWrite(PIN_SOLENOIDE,  LOW);

  Serial.println("=== Cerradura RFID lista ===");
  Serial.println("Acerca una tarjeta o token...");
}

// ============================================================
//  LOOP PRINCIPAL
// ============================================================
void loop() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  // Leer UID
  byte uid[LONGITUD_UID];
  for (byte i = 0; i < LONGITUD_UID; i++) {
    uid[i] = rfid.uid.uidByte[i];
  }

  // Imprimir UID en Monitor Serie (para registrar nuevas tarjetas)
  Serial.print("UID detectado: ");
  for (byte i = 0; i < LONGITUD_UID; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i < LONGITUD_UID - 1) Serial.print(":");
  }
  Serial.println();

  // Verificar acceso
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

  // Beep largo 1 segundo
  digitalWrite(PIN_BUZZER, HIGH);
  delay(TIEMPO_BEEP_OK);
  digitalWrite(PIN_BUZZER, LOW);
 
  // Activar solenoide 3 segundos (cerradura abierta)
  digitalWrite(PIN_SOLENOIDE, HIGH);
  servoLock.write(SERVO_OPEN);
  delay(TIEMPO_SOLENOIDE);
  digitalWrite(PIN_SOLENOIDE, LOW);
  servoLock.write(SERVO_LOCK);

  digitalWrite(PIN_LED_VERDE, LOW);
}

void accesoDenegado() {
  digitalWrite(PIN_LED_VERDE,  LOW);
  digitalWrite(PIN_SOLENOIDE,  LOW);  // cerradura sigue cerrada

  unsigned long inicio = millis();
  bool estado = HIGH;

  // LED rojo y buzzer parpadean juntos durante 3 segundos
  while (millis() - inicio < TIEMPO_ALERTA) {
    digitalWrite(PIN_LED_ROJO, estado);
    digitalWrite(PIN_BUZZER,   estado);
    estado = !estado;
    delay(INTERVALO_PARPADEO);
  }

  digitalWrite(PIN_LED_ROJO, LOW);
  digitalWrite(PIN_BUZZER,   LOW);
}