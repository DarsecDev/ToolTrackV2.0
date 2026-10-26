# Arquitectura

## Nodos

### Placa maestra — ESP32-S3-DevKitC-1 N16R8

Responsabilidades:

- Leer el lector RFID RC522 por SPI.
- Leer los 12 sensores IR de la matriz 4x3.
- Controlar la cerradura de 12 V, los LEDs, el buzzer y la tira LED.
- Mantener la sesión del usuario activo y registrar eventos.
- Servir la web, la API REST y el WebSocket.
- Sincronizar la hora por NTP y reconstruir eventos.
- Orquestar la captura y el análisis del nodo de visión.
- Enviar notificaciones por Telegram (opcional).

### Nodo de visión — XIAO ESP32-S3 Sense

Responsabilidades:

- Inicializar la cámara OV2640.
- Recibir la orden de análisis por HTTP.
- Capturar un frame, recortar las 12 zonas y ejecutar el modelo.
- Devolver el resultado en JSON y conservar la última foto.
- Exponer `/latest.jpg`, `/latest.json` y `/health`.

La comunicación es **WiFi/HTTP** porque la placa maestra no tiene GPIO libres
suficientes para un UART dedicado sin sacrificar los 12 IR. El nodo de visión
no controla actuadores.

## Flujo de una operación

1. El usuario pasa su tarjeta RFID.
2. La placa maestra valida el UID:
   - **Autorizado:** abre la cerradura 3 s, enciende el LED verde, suena el
     buzzer, enciende la tira LED y activa la sesión del usuario.
   - **Denegado:** parpadea el LED rojo y el buzzer; registra el intento.
3. Los cambios de los IR generan eventos de tomada o devolución tras el
   antirrebote.
4. Cuando el RFID se validó y los IR llevan `IR_STABLE_MS` sin cambios, la
   placa maestra pide `POST /analyze` al nodo de visión.
5. El nodo captura, clasifica las 12 zonas y responde con presencia,
   identificación y confianza por ranura.
6. La placa maestra compara IR y cámara:
   - Coinciden: actualiza el estado.
   - Discrepan: **gana el IR** y se registra una alerta.
7. Se guarda el evento, se actualiza la web por WebSocket y, si está activo,
   se envía un mensaje de Telegram.

## Tiempo y energía

- La hora se obtiene por NTP.
- Cada evento guarda `bootId`, `uptimeMs` y `epoch` (puede ser nulo).
- Si el router se cae, el sistema sigue funcionando con IR y guarda `uptimeMs`.
- Al volver la red y sincronizar NTP, se calcula el instante de arranque
  (`bootEpoch = now - uptime`) y se reconstruye la hora de los eventos de esa
  sesión.
- Tras un apagón total (fuera del UPS), los eventos nuevos quedan marcados
  como hora desconocida hasta la próxima sincronización.

## Modo degradado

| Falla | Efecto | Mitigación |
|---|---|---|
| Sin router | Web, cámara, NTP y Telegram no disponibles | IR, cerradura e histórico local siguen |
| Sin cámara | No hay verificación visual | IR decide; la web muestra cámara offline |
| Sin UPS | El equipo se apaga | Al arrancar, los IR se leen sin generar eventos falsos |
| Sin tarjeta válida | No se abre la cerradura | Se registra el intento denegado |

## Almacenamiento

| Dato | Medio | Formato |
|---|---|---|
| WiFi, contraseña admin, ajustes | NVS | clave/valor |
| Usuarios RFID | LittleFS | `users.json` |
| Configuración de herramientas | LittleFS | `tools.json` |
| Eventos | LittleFS | `events.jsonl` (rotación) |
| Última foto y resultado | LittleFS | `last.jpg`, `last_result.json` |
| Web | LittleFS | `index.html`, `app.js`, `styles.css` |

Los archivos JSON se escriben de forma atómica: primero a `.tmp`, luego
`rename`. El histórico es un JSON Lines para permitir escrituras por
añadidura y rotación sin reescribir todo.

## Módulos previstos (Fase 1)

```
src/main/
├── main.cpp
├── storage.*        # NVS, LittleFS, eventos, rotación
├── wifi_setup.*     # WiFiManager + mDNS
├── time_sync.*      # NTP y reconstrucción
├── rfid.*           # RC522, usuarios, sesión
├── ir_sensors.*     # antirrebote y estados
├── actuators.*      # cerradura, LEDs, buzzer, tira LED
├── camera_link.*    # cliente HTTP del nodo de visión
├── webserver.*      # REST, WebSocket, auth
├── telegram.*       # notificaciones
└── event_log.*      # modelo de eventos
```

## Decisiones de diseño

| Decisión | Motivo |
|---|---|
| ESP32-S3 N16R8 | GPIO y PSRAM suficientes; USB nativo |
| Cámara por WiFi | El DevKit clásico no tenía pines; el S3 tampoco necesita UART |
| IR + cámara | El IR es rápido y confiable; la cámara aporta identidad |
| LittleFS en vez de SPIFFS | SPIFFS está obsoleto y es propenso a corrupción |
| Web en `data/` | Editable sin recompilar el firmware |
| WebSocket | Actualización en vivo sin polling agresivo |
| Sesión por usuario | Flujo simple: el último RFID válido queda activo |
