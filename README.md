# ToolTrack v2

Armario inteligente de control de inventario de herramientas.
Registra qué herramienta se toma, cuándo y quién la toma, y verifica la
devolución con sensores infrarrojos y visión artificial.

> **Estado:** Fase 0 completada — esqueleto del proyecto, configuración de
> PlatformIO, pinout, web base y documentación. La lógica se implementa en la
> Fase 1.

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
   Web + WebSocket + Telegram
```

- **Placa maestra:** ESP32-S3-DevKitC-1 **N16R8** (16 MB flash, 8 MB PSRAM).
- **Nodo de visión:** Seeed XIAO ESP32-S3 Sense.
- **Detección híbrida:** los IR deciden presencia; la cámara identifica la
  herramienta y valida que esté en la ranura correcta.
- **Energía:** 12 V desde un mini UPS de router; buck 12→5 V para las placas.
- **Almacenamiento:** NVS para configuración, LittleFS para eventos y web.
- **Hora:** NTP con reconstrucción de eventos al recuperar la conexión.

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

# 4. Flashear (con la placa conectada)
pio run -e main -t upload
pio run -e camera -t upload

# 5. Subir la web a LittleFS (solo placa maestra)
pio run -e main -t buildfs
pio run -e main -t uploadfs

# 6. Monitor serial
pio device monitor -e main
```

La primera vez, la placa maestra crea el portal WiFi **`ToolTrack-Setup`**.
Conéctate desde el teléfono, elige la red del taller y escribe la contraseña.

## Estructura

```
├── include/            # configuración y secretos (secrets.h no se sube)
├── src/main/           # firmware de la placa maestra
├── src/camera/         # firmware del nodo de visión
├── data/               # interfaz web para LittleFS
├── partitions/         # tablas de particiones personalizadas
├── docs/               # arquitectura, hardware, API, IA y operación
├── tools/              # scripts de apoyo (flasheo, dataset, calibración)
└── legacy/             # código original del proyecto (solo referencia)
```

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
