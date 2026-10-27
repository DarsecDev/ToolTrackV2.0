# Desarrollo

## Requisitos

- VS Code con la extensión **PlatformIO IDE**.
- Python 3.8+ (PlatformIO lo usa internamente).
- Git.
- Drivers USB:
  - ESP32-S3 DevKitC-1: USB nativo (no requiere CP210x).
  - XIAO ESP32-S3 Sense: USB-C nativo.

## Primer uso

```bash
cp include/secrets.example.h include/secrets.h
```

Editar `include/secrets.h` con la contraseña inicial. El token de Telegram
existe como reserva, pero el envío no está implementado; déjalo vacío. Ese
archivo **no se sube** al repositorio.

## Entornos

| Entorno | Placa | Uso |
|---|---|---|
| `main` | ESP32-S3-DevKitC-1 N16R8 | Placa maestra |
| `camera` | XIAO ESP32-S3 Sense | Nodo de visión |

## Comandos

```bash
# Compilar
pio run -e main
pio run -e camera

# Flashear
pio run -e main -t upload
pio run -e camera -t upload

# Monitor serial
pio device monitor -e main
pio device monitor -e camera

# Sistema de archivos (web en la placa maestra)
pio run -e main -t buildfs
pio run -e main -t uploadfs

# Verificar sintaxis de la web sin flashear
node --check data/app.js

# Limpiar
pio run -e main -t clean
```

> `src/camera/` ya implementa el servidor HTTP del nodo de visión, la captura,
> las ROI y el contrato `/analyze`. El clasificador sigue siendo un stub hasta
> integrar Edge Impulse; `ENABLE_WIFI=true` y el portal es `ToolTrack-Cam`.
> Para el nodo de visión, LittleFS solo guarda `rois.json`, `last.jpg` y
> `last_result.json`; la web no se sube a la cámara.

La placa maestra ejecuta el análisis de cámara en `cameraTask` (núcleo 0, 12 KB
de pila). El `loop()` no espera las peticiones HTTP del nodo de visión: notifica
la tarea, sigue atendiendo RFID/IR/cerradura y procesa el resultado cuando
llega. El health se consulta cada 60 s dentro de la misma tarea.

Scripts de apoyo:

```bash
./tools/flash_main.sh
./tools/flash_camera.sh
```

## Demo local sin hardware

`tools/mock_server.py` sirve la web de `data/` y simula la API REST, el
WebSocket, el histórico, los usuarios y la foto del armario. Sirve para
desarrollar y presentar la interfaz sin placas.

```bash
python3 -m pip install -r tools/requirements-demo.txt
./tools/run_mock.sh
```

- URL: `http://127.0.0.1:8000`
- Contraseña de administrador: `tooltrack` (sin cambio obligatorio)
- `--auto 8`: evento simulado cada 8 s (por defecto).
- `--auto 0`: sin simulación automática.
- `--host 0.0.0.0 --port 8080`: accesible desde la red local.
- `POST /api/demo/simulate`: fuerza un evento; solo existe en la demo.

El estado es en memoria: al detener el servidor se pierden usuarios, eventos y
ajustes simulados. La demo no habla con el ESP32 ni con la cámara.

## Pruebas nativas

Las pruebas de host compilan la lógica real de `src/main/` con un stub mínimo de
`Arduino.h`, sin PlatformIO ni hardware:

```bash
./test/native/run_tests.sh
```

Cubren:

- Mapeo de las 12 ranuras y nombres de acciones/fuentes.
- Pines usados y pines prohibidos del ESP32-S3.
- Estado de herramientas, cámara y sesión.
- Antirrebote y flancos de los 12 IR.
- Fail-safe, temporizadores de cerradura y alertas, y desbordamiento de
  `millis()`.

El script devuelve código distinto de cero si falla una comprobación. GitHub
Actions lo ejecuta antes de compilar `main` y `camera`, junto con
`node --check data/app.js`.

Pendiente de pruebas: rotación/serialización de `storage.cpp`, parser de
`/analyze` y recuperación de cámara offline.

## Estructura de compilación

- `src/main/` solo se compila en `main`.
- `src/camera/` solo se compila en `camera`.
- `include/` es compartido.
- `data/` se sube a LittleFS únicamente en `main` cuando se ejecuta `buildfs`.

## Particiones

- `partitions/main_16mb.csv`: 2 apps de 4 MB (layout compatible con OTA, aunque
  OTA está desactivado) + ~7.9 MB LittleFS.
- `partitions/camera_8mb.csv`: app de 4 MB + ~3.9 MB LittleFS.

Si se cambia el esquema de particiones, actualizar también
`board_build.partitions` y volver a flashear con `-t upload` (y `uploadfs` si
cambia el tamaño del sistema de archivos).

## PSRAM

- La placa maestra usa **N16R8**: flash QIO de 16 MB y PSRAM **OPI** de 8 MB.
- `board_build.arduino.memory_type = qio_opi` es obligatorio.
- GPIO 33–37 quedan ocupados por la PSRAM octal.
- La XIAO S3 Sense ya tiene `qio_opi` en su definición de placa.

## Solución de problemas

### No aparece el puerto serie

- Usar un cable USB de datos (no solo carga).
- En la DevKitC-1, conectar el puerto **USB** (no el UART).
- Instalar reglas udev si es Linux:
  `sudo usermod -aG dialout $USER` y cerrar sesión.

### La placa entra en modo descarga

- Mantener presionado **BOOT**, presionar **RESET**, soltar **BOOT** y volver a
  flashear.

### Error de PSRAM o reinicios

- Verificar que la placa sea realmente N16R8.
- Confirmar `memory_type = qio_opi`.
- Revisar que no se esté usando GPIO 33–37.

### La cámara no inicializa

- Verificar que el módulo de cámara esté bien conectado a la XIAO Sense.
- Confirmar que la placa es la versión **Sense** (con PSRAM).
- Revisar el monitor serial para el código de error de `esp_camera_init`.

### LittleFS no monta

- Ejecutar `pio run -e main -t uploadfs`.
- Si persiste, borrar la flash:
  `pio run -e main -t erase` y volver a flashear.

### WiFiManager no abre el portal

- Esperar 180 s o reiniciar la placa.
- Buscar la red `ToolTrack-Setup` en la maestra o `ToolTrack-Cam` en el nodo de
  visión.
- Si el router cambió de contraseña, usar Administración → Reset WiFi en la
  maestra. El nodo de visión no tiene reset por web: hay que volver a flashear
  o borrar su NVS.

## Git

```bash
git status
git add .
git commit -m "feat(main): descripción corta"
git push origin main
```

Ramas:

- `main`: demo y versiones estables.
- `dev`: integración.
- `feature/nombre`: trabajo en curso.

Convención de commits:

| Prefijo | Uso |
|---|---|
| `feat` | Nueva funcionalidad |
| `fix` | Corrección |
| `docs` | Documentación |
| `refactor` | Reestructuración sin cambio funcional |
| `test` | Pruebas |
| `chore` | Tareas de mantenimiento |

## Publicar en GitHub

```bash
git remote add origin git@github.com:TU_USUARIO/tooltrack.git
git branch -M main
git push -u origin main
```

Para las releases, adjuntar:

- `.pio/build/main/firmware.bin`
- `.pio/build/main/littlefs.bin`
- `.pio/build/camera/firmware.bin`
