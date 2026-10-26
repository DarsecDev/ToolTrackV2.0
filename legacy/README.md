# Código original (solo referencia)

Aquí se conservan los archivos `.ino` del proyecto original entregado en
`ProyectoFinal_ToolTrack.zip`, para consultar la lógica y el cableado
histórico.

**No forman parte de la compilación de PlatformIO.** La versión nueva está en
`src/main/` y `src/camera/`.

## Versiones incluidas

| Carpeta | Descripción |
|---|---|
| `ToolTrackV1.0` | Primera versión con dashboard embebido |
| `ToolTrack_Complete` | Versión intermedia |
| `Tooltrack_Complete.v4` | Versión con histórico |
| `ToolTrack_Complete.v5` | Versión más reciente de esa rama |
| `tooltrack_spiffs` | Base más completa: SPIFFS + API + web |
| `ToolTrack_LockSinServo` | Prueba de cerradura sin servo |
| `Arduino/InventaryControl-version sin servo` | Control de inventario sin servo |
| `Arduino/ToolTrack_Lock-version con servo` | Cerradura con servo |

## Problemas conocidos del código original

- `logEvent()` no recorta el historial: el bloque de rotación está vacío.
- Usa `millis()/1000` como fecha; se reinicia en cada corte.
- Al arrancar, los IR se inicializan en `false` y pueden registrar
  devoluciones falsas.
- Los usuarios están hardcodeados; no existe el menú de administrador que
  menciona el informe.
- Las herramientas no tienen nombre ni configuración.
- El buffer JSON de 8 KB no soporta 100 eventos.
- Usa SPIFFS (obsoleto) en lugar de LittleFS.
- No hay autenticación ni WebSocket.
- El pinout asume GPIO 23 y GPIO 4, que no existen en un DevKit de 30 pines.
