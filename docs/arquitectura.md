# Arquitectura

## Estado de implementación

La Fase 1 está implementada y compila para la placa maestra. El nodo de visión
tiene servidor HTTP, captura, ROI y contrato de análisis; la inferencia real
queda pendiente de integrar Edge Impulse con un dataset y un modelo.

| Componente | Estado |
|---|---|
| Placa maestra | Implementada; pendiente validar en hardware |
| Web, API REST y WebSocket | Implementados y probados con API simulada |
| RFID, 12 IR y actuadores | Implementados; pendiente validar en hardware |
| Cliente de cámara y foto | Implementado; pendiente probar con la XIAO |
| Nodo de visión (HTTP, cámara y ROI) | Implementado; pendiente probar en hardware |
| Modelo de visión | Pendiente de dataset y Edge Impulse |
| Telegram | No implementado |
| OTA | No implementado; actualización por USB |

## Nodos

### Placa maestra — ESP32-S3-DevKitC-1 N16R8

Responsabilidades:

- Leer el lector RFID RC522 por SPI.
- Leer los 12 sensores IR de la matriz 4x3.
- Controlar la cerradura de 12 V, los LEDs, el buzzer y la tira LED.
- Mantener la sesión del usuario activo y registrar eventos.
- Servir la web, la API REST y el WebSocket.
- Sincronizar la hora por NTP y reconstruir eventos del arranque actual.
- Orquestar la captura y el análisis del nodo de visión.
- Detectar el corte de red si la PCB entrega la señal (función desactivada).
- Enviar notificaciones por Telegram (previsto, no implementado).

### Nodo de visión — XIAO ESP32-S3 Sense

Responsabilidades:

- Inicializar la cámara OV2640.
- Exponer `/health`, `/capture.jpg`, `/analyze`, `/latest.jpg`, `/latest.json`
  y `/rois`.
- Capturar un frame, guardar la última foto y recortar las 12 zonas.
- Ejecutar el modelo cuando esté integrado.
- Devolver el resultado en JSON y conservar la última foto.
- Reportar presencia, herramienta predicha, confianza y caja por ranura.

La comunicación es **WiFi/HTTP** porque la placa maestra no tiene GPIO libres
suficientes para un UART dedicado sin sacrificar los 12 IR. El nodo de visión
no controla actuadores. La maestra resuelve `tooltrack-cam.local` por mDNS y usa
esa URL como respaldo.

El nodo no tiene autenticación: debe quedar en la red local del taller. El
clasificador es una interfaz con implementación stub; sin modelo, `/analyze`
responde las 12 ranuras como desconocidas y no inventa detecciones.

## Flujo de una operación

1. El usuario pasa su tarjeta RFID.
2. La placa maestra valida el UID:
   - **Autorizado:** abre la cerradura 3 s, enciende el LED verde, suena el
     buzzer, enciende la tira LED y activa la sesión del usuario.
   - **Denegado:** parpadea el LED rojo y el buzzer; registra el intento.
3. Los cambios de los IR generan eventos de tomada o devolución tras el
   antirrebote.
4. Cuando los IR llevan `IR_STABLE_MS` sin cambios, la placa maestra pide
   `POST /analyze` al nodo de visión. También se puede forzar desde la web.
5. El nodo captura, clasifica las 12 zonas y responde con presencia,
   identificación y confianza por ranura.
6. La placa maestra compara IR y cámara:
   - Coinciden: actualiza el estado y la confianza.
   - Discrepan: **gana el IR**; se registra una discrepancia, se alerta en la
     web y se activa la alerta física.
7. Se guarda el evento y se actualiza la web por WebSocket.
8. Si la cámara está disponible, se descarga la última foto y se muestra con
   las cajas de las 12 ranuras.

## Tiempo y energía

- La hora se obtiene por NTP.
- Cada evento guarda `bootId`, `uptimeMs` y `epoch` (puede ser nulo).
- Si el router se cae, el sistema sigue funcionando con IR y guarda `uptimeMs`.
- Al volver la red y sincronizar NTP, se calcula el instante de arranque
  (`bootEpoch = now - uptime`) y se reconstruyen los eventos de **ese mismo
  arranque**.
- Si hay un apagón total antes de sincronizar, los eventos de ese arranque
  quedan sin hora absoluta: no se conserva un ancla de tiempo entre reinicios.
- La detección de red presente usa GPIO41 con optoacoplador. Está implementada
  pero desactivada (`FEATURE_AC_SENSE=false`) hasta que la PCB entregue la señal.
- El router no está en el UPS. Durante un corte se pierden web, cámara y NTP;
  RFID, IR, cerradura, LEDs e historial local siguen. Telegram no está
  implementado.

## Modo degradado

| Falla | Efecto | Mitigación |
|---|---|---|
| Sin router | Web, cámara y NTP no disponibles | IR, cerradura e historial local siguen |
| Sin cámara | No hay verificación visual | IR decide; la web muestra cámara offline |
| Sin UPS | El equipo se apaga | Al arrancar, los IR se leen sin generar eventos falsos |
| Sin tarjeta válida | No se abre la cerradura | Se registra el intento denegado |
| Cámara discrepa del IR | La identificación visual no es confiable | Gana el IR y se alerta |

## Almacenamiento

| Dato | Medio | Formato |
|---|---|---|
| WiFi | NVS (WiFiManager) | credenciales |
| Contraseña de administrador | NVS | salt + hash PBKDF2 + flag de cambio |
| Ajustes | LittleFS | `settings.json` |
| Usuarios RFID | LittleFS | `users.json` |
| Configuración de herramientas | LittleFS | `tools.json` |
| Eventos | LittleFS | `events.jsonl` (rotación) |
| Última foto y resultado | LittleFS | `last.jpg`, `last_result.json` |
| ROI de la cámara | LittleFS | `rois.json` |
| Web | LittleFS | `index.html`, `app.js`, `styles.css` |

Los archivos JSON se escriben de forma atómica: primero a `.tmp`, luego
`rename`. El histórico es JSON Lines para permitir escrituras por añadidura y
rotación sin reescribir todo. En RAM se mantiene un búfer circular en PSRAM con
los eventos más recientes.

## Módulos implementados

```
src/main/
├── main.cpp         # arranque, RFID, IR, sesión, energía y cámara
├── types.h          # usuarios, herramientas, ajustes y eventos
├── state.*          # estado en RAM de sesión y herramientas
├── storage.*        # NVS, LittleFS, usuarios, tools, eventos y auth
├── time_sync.*      # NTP, epoch y reconstrucción
├── rfid.*           # RC522 y UID
├── ir_sensors.*     # antirrebote y flancos
├── actuators.*      # cerradura, LEDs, buzzer y tira LED
├── camera_link.*    # cliente HTTP del nodo de visión
└── webserver.*      # REST, WebSocket, auth y archivos estáticos

src/camera/
├── main.cpp              # WiFi, mDNS y arranque del servidor
├── camera_vision.*       # cámara, ROI, foto y resultado
├── camera_classifier.*   # interfaz del modelo; stub sin inferencia
└── camera_webserver.*    # HTTP del nodo de visión
```

## Decisiones de diseño

| Decisión | Motivo |
|---|---|
| ESP32-S3 N16R8 | GPIO y PSRAM suficientes; USB nativo |
| Cámara por WiFi | El DevKit clásico no tenía pines; el S3 tampoco necesita UART |
| IR + cámara | El IR es rápido y confiable; la cámara aporta identidad |
| Gana el IR | La presencia física es más confiable que una inferencia con poca luz |
| LittleFS en vez de SPIFFS | SPIFFS está obsoleto y es propenso a corrupción |
| Web en `data/` | Editable sin recompilar el firmware |
| WebSocket | Actualización en vivo sin polling agresivo |
| Sesión por usuario | Flujo simple: el último RFID válido queda activo |
| Historial en PSRAM | Evita desgastar la flash en cada evento y acelera la web |
| Sin RTC | NTP es suficiente para la expo; se documenta la limitación |
