# ToolTrack v2

Repositorio: https://github.com/DarsecDev/ToolTrackV2.0

Armario inteligente de control de inventario de herramientas. Registra qué
herramienta se toma, cuándo y quién la toma, y verifica la devolución con
sensores infrarrojos y visión artificial.

> **Estado:** Fases 0–2 completas. El nodo de visión ya tiene servidor HTTP,
> captura, ROI y contrato `/analyze`; la inferencia real queda pendiente de
> integrar Edge Impulse con fotos y modelo. La validación en el armario físico
> también está pendiente.

## Arquitectura

```
┌────────────────────┐        WiFi / HTTP        ┌──────────────────────┐
│ ESP32-S3 DevKitC-1 │ ◄───────────────────────► │ XIAO ESP32-S3 Sense  │
│ Placa maestra      │                           │ Nodo de visión       │
│ RFID · 12 IR ·     │                           │ Cámara OV2640        │
│ cerradura · LEDs   │                           │ Modelo Edge Impulse  │
└─────────┬──────────┘                           └──────────────────────┘
          │ WiFi
          ▼
   Web + WebSocket + API REST
```

- **Placa maestra:** ESP32-S3-DevKitC-1 **N16R8** (16 MB flash, 8 MB PSRAM).
- **Nodo de visión:** Seeed XIAO ESP32-S3 Sense.
- **Detección híbrida:** los IR deciden presencia; la cámara identifica la
  herramienta y valida que esté en la ranura correcta. Si discrepan, gana el IR
  y se genera una alerta.
- **Energía:** 12 V desde un mini UPS; buck 12→5 V para las placas y otro
  12→3.3 V para los 12 IR y el RC522. La cerradura es fail-safe y la lámpara es
  de 12 V / 60 mA. El router no está respaldado por el UPS.
- **Almacenamiento:** NVS para secretos y configuración sensible; LittleFS para
  usuarios, herramientas, eventos, última foto y web.
- **Hora:** NTP con reconstrucción de eventos de la sesión de arranque actual.

## Funciones implementadas

### Firmware de la placa maestra

- Lectura RC522 por SPI, UID en hexadecimal y sesión del último usuario válido.
- 12 entradas IR con antirrebote y detección de flancos.
- Cerradura 12 V fail-safe, LED verde/rojo, buzzer y lámpara 12 V no bloqueantes.
- Registro de accesos, tomadas, devoluciones, discrepancias y alertas.
- Historial en PSRAM con respaldo JSONL en LittleFS y rotación configurable.
- Hora por NTP y reconstrucción de eventos sin hora dentro del mismo arranque.
- Cliente HTTP del nodo de visión con mDNS, análisis, foto y comparación IR/cámara.
- Detección opcional de corte de red (desactivada hasta que la PCB entregue GPIO41).

### Firmware del nodo de visión

- WiFiManager con portal `ToolTrack-Cam` y mDNS `tooltrack-cam.local`.
- Servidor HTTP con `/health`, `/capture.jpg`, `/analyze`, `/latest.jpg`,
  `/latest.json` y `/rois`.
- Captura UXGA en PSRAM y respaldo VGA si falla la inicialización.
- ROI normalizadas 4x3 almacenadas en LittleFS (`rois.json`).
- Contrato de análisis con presencia, herramienta, confianza y caja por ranura.
- Interfaz de clasificador preparada; sin modelo devuelve 12 ranuras
  desconocidas, sin inventar detecciones.

### Web y API

- Dashboard con matriz 4x3, estado de cámara, sesión y herramientas fuera.
- Histórico con búsqueda, filtro por herramienta y carga paginada.
- WebSocket para estado, eventos, alertas y cambios de cámara.
- Login de administrador con PBKDF2-SHA256, cookie HttpOnly y sesión de 1 hora.
- Cambio obligatorio de la contraseña inicial.
- Bloqueo temporal tras cinco intentos fallidos.
- CRUD de usuarios RFID y configuración de las 12 herramientas.
- Ajustes de histórico, cámara, sesión y campos reservados de Telegram.
- Exportación CSV, borrado de histórico, reset WiFi y reinicio desde el panel.
- Visor de la última foto con cajas y confianza por ranura.

## Inicio rápido

Requisitos: VS Code + PlatformIO IDE, Python 3 y Git.

```bash
# 1. Configurar secretos locales
cp include/secrets.example.h include/secrets.h
# editar include/secrets.h

# 2. Compilar la placa maestra
pio run -e main

# 3. Compilar el nodo de visión
pio run -e camera

# 4. Flashear la placa maestra
pio run -e main -t upload

# 5. Subir la web a LittleFS
pio run -e main -t uploadfs

# 6. Flashear el nodo de visión cuando esté conectado por USB
pio run -e camera -t upload

# 7. Monitor serial
pio device monitor -e main
```

La primera vez, la placa maestra abre el portal WiFi **`ToolTrack-Setup`** y
el nodo de visión el portal **`ToolTrack-Cam`**. Conéctate desde el teléfono,
elige la red del taller y escribe la contraseña. Si un portal expira, reinicia
la placa correspondiente para volver a abrirlo.

## Primer acceso a la web

1. Abre `http://tooltrack.local` o la IP que aparezca en el monitor serial.
2. Entra en **Administración**.
3. Inicia sesión con la contraseña inicial `tooltrack`.
4. Cambia la contraseña cuando la web lo solicite.
5. Revisa los usuarios de ejemplo y ajusta sus UID a las tarjetas reales.
6. Configura nombres y clases esperadas de las 12 herramientas.

La contraseña inicial se define en `include/secrets.h`
(`SECRET_DEFAULT_ADMIN_PASSWORD`). Cámbiala antes de la expo.

## Uso diario

- El usuario acerca su tarjeta; si está autorizada, la cerradura abre 3 s y
  queda una sesión activa.
- Al tomar o devolver una herramienta, el cambio se registra y aparece en la
  web en tiempo real.
- La cámara analiza después de que los IR se estabilizan. Si la lectura no
  coincide con el IR, se registra una discrepancia y se alerta en la web.
- El histórico se conserva en el equipo aunque el router no esté disponible.
- El administrador puede exportar el CSV desde **Administración → Sistema**.

## Estructura

```
├── include/            # configuración y secretos (secrets.h no se sube)
├── src/main/           # firmware de la placa maestra
├── src/camera/         # nodo de visión: HTTP, cámara, ROI y clasificador
├── data/               # interfaz web para LittleFS
├── partitions/         # tablas de particiones personalizadas
├── docs/               # arquitectura, hardware, API, IA y operación
├── tools/              # scripts de apoyo
└── legacy/             # código original del proyecto (solo referencia)
```

## Verificación

```bash
pio run -e main
pio run -e camera
pio run -e main -t buildfs
node --check data/app.js
```

La compilación de `main` usa aproximadamente 15 % de RAM y 29 % de flash.
La del nodo de visión usa aproximadamente 19 % de RAM y 25 % de flash.
La interfaz se probó con una API simulada en Chromium, incluyendo login, cambio
de contraseña, usuarios, herramientas, ajustes, sistema e histórico. Esa prueba
no sustituye la validación con el hardware real.

## Limitaciones actuales

- El nodo HTTP y las ROI están implementados, pero **no hay modelo de
  inferencia**: `/analyze` devuelve confianza 0 y las 12 ranuras como
  desconocidas. No se simulan detecciones. Mientras tanto, el IR decide.
- No se ha probado con la XIAO, la PCB, los IR ni la cerradura físicos.
- Telegram está desactivado y su envío no está implementado. Los campos se
  conservan para una versión futura.
- OTA está desactivado; la actualización se hace por USB.
- La detección de corte de red está implementada pero desactivada
  (`FEATURE_AC_SENSE=false`) hasta conectar GPIO41.
- La reconstrucción de hora cubre eventos del arranque actual. Si hay un apagón
  total antes de sincronizar NTP, esos eventos quedan sin hora absoluta.
- El router no está en el UPS: durante un corte se pierden web, cámara y NTP;
  RFID, IR, cerradura e historial local continúan. Si el UPS se agota, la
  cerradura fail-safe se abre. Telegram no está implementado.
- Solo hay un administrador global y una sesión de administrador a la vez.
- El nodo de visión no tiene autenticación: debe quedar en la red local del
  taller, no expuesto a Internet.

## Documentación

- [Arquitectura](docs/arquitectura.md)
- [Hardware y pinout](docs/hardware.md)
- [BOM total y consumos](docs/BOM_ToolTrack.xlsx)
- [API y WebSocket](docs/api.md)
- [Visión artificial](docs/ml.md)
- [Manual de operación](docs/operacion.md)
- [Desarrollo](docs/desarrollo.md)
- [Plan de 10 días](docs/plan.md)

## Flujo de trabajo

- `main`: versiones estables y demo.
- `dev`: integración.
- `feature/nombre`: cada funcionalidad nueva.

Commits en formato convencional: `feat(rfid): ...`, `fix(ir): ...`,
`docs(hardware): ...`.

Los secretos (`include/secrets.h`) y los binarios del proyecto original no se
versionan.
