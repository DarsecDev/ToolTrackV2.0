# ToolTrack v2

Armario inteligente de control de inventario de herramientas. Registra qué
herramienta se toma, cuándo y quién la toma, y verifica la devolución con
sensores infrarrojos y visión artificial.

> **Estado:** Fase 1 implementada y compilada. La placa maestra, la API, el
> WebSocket y el panel web están completos. El nodo de visión (XIAO ESP32-S3
> Sense) es la siguiente fase: el cliente HTTP de la maestra ya está integrado,
> pero el firmware del nodo todavía no expone `/analyze` ni `/latest.jpg`.
> La validación en el armario físico está pendiente.

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
- **Nodo de visión:** Seeed XIAO ESP32-S3 Sense (Fase 2).
- **Detección híbrida:** los IR deciden presencia; la cámara identifica la
  herramienta y valida que esté en la ranura correcta. Si discrepan, gana el IR
  y se genera una alerta.
- **Energía:** 12 V desde un mini UPS; buck 12→5 V para las placas. El router
  no está respaldado por el UPS.
- **Almacenamiento:** NVS para secretos y configuración sensible; LittleFS para
  usuarios, herramientas, eventos, última foto y web.
- **Hora:** NTP con reconstrucción de eventos de la sesión de arranque actual.

## Funciones implementadas

### Firmware de la placa maestra

- Lectura RC522 por SPI, UID en hexadecimal y sesión del último usuario válido.
- 12 entradas IR con antirrebote y detección de flancos.
- Cerradura 12 V, LED verde/rojo, buzzer y tira LED no bloqueantes.
- Registro de accesos, tomadas, devoluciones, discrepancias y alertas.
- Historial en PSRAM con respaldo JSONL en LittleFS y rotación configurable.
- Hora por NTP y reconstrucción de eventos sin hora dentro del mismo arranque.
- Cliente HTTP del nodo de visión con mDNS, análisis, foto y comparación IR/cámara.
- Detección opcional de corte de red (desactivada hasta que la PCB entregue GPIO41).

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

# 3. Compilar el nodo de visión (prueba de cámara de la Fase 2)
pio run -e camera

# 4. Flashear la placa maestra
pio run -e main -t upload

# 5. Subir la web a LittleFS
pio run -e main -t uploadfs

# 6. Monitor serial
pio device monitor -e main
```

La primera vez, la placa maestra abre el portal WiFi **`ToolTrack-Setup`**.
Conéctate desde el teléfono, elige la red del taller y escribe la contraseña.
Si el portal expira, reinicia la placa para volver a abrirlo.

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
├── src/camera/         # firmware del nodo de visión (prueba de cámara)
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
La interfaz se probó con una API simulada en Chromium, incluyendo login, cambio
de contraseña, usuarios, herramientas, ajustes, sistema e histórico. Esa prueba
no sustituye la validación con el hardware real.

## Limitaciones actuales

- El nodo de visión de la Fase 2 todavía no implementa el servidor HTTP ni la
  inferencia; `src/camera` solo inicializa la cámara y captura un frame de prueba.
- No se ha probado con la XIAO, la PCB, los IR ni la cerradura físicos.
- Telegram está desactivado y su envío no está implementado. Los campos se
  conservan para una versión futura.
- OTA está desactivado; la actualización se hace por USB.
- La detección de corte de red está implementada pero desactivada
  (`FEATURE_AC_SENSE=false`) hasta conectar GPIO41.
- La reconstrucción de hora cubre eventos del arranque actual. Si hay un apagón
  total antes de sincronizar NTP, esos eventos quedan sin hora absoluta.
- El router no está en el UPS: durante un corte se pierden web, cámara y NTP;
  RFID, IR, cerradura e historial local continúan. Telegram no está implementado.
- Solo hay un administrador global y una sesión de administrador a la vez.

## Documentación

- [Arquitectura](docs/arquitectura.md)
- [Hardware y pinout](docs/hardware.md)
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
