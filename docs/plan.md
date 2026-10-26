# Plan de 10 días

Inicio: 26 de octubre de 2026. Entrega objetivo: expo/feria en 10 días.

## Estado actual

- **Fase 0 — completada:** repositorio local, PlatformIO con entornos `main` y
  `camera`, particiones, pinout, web base y documentación.
- **Fase 1 — completada en firmware y web:** almacenamiento, RFID, 12 IR,
  actuadores, cliente de cámara, API REST, WebSocket, autenticación, panel de
  administración, histórico, exportación CSV, NTP y reconstrucción de hora.
- **Compilación verificada:** `pio run -e main` y `pio run -e camera`.
  `pio run -e main -t buildfs` genera la imagen de LittleFS.
- **UI verificada con API simulada:** matriz 4x3, histórico, login, cambio
  obligatorio de contraseña, usuarios, herramientas, ajustes, sistema y visor
  de foto con cajas de la cámara. Sin errores de JavaScript en Chromium.
- **Pendiente de hardware:** XIAO ESP32-S3 Sense, placa maestra, PCB, tira LED,
  sensores y UPS. La lógica de campo no se ha probado todavía en el armario.
- **Pendiente de Fase 2:** firmware del nodo de visión, calibración de ROI,
  dataset, modelo Edge Impulse e inferencia real.
- **Pendiente administrativo:** crear el repositorio remoto en GitHub y hacer
  el primer push.

### Ruta crítica

1. Pedir la XIAO ESP32-S3 Sense y confirmar los componentes del PCB.
2. Terminar el nodo de visión y validarlo con la cámara real.
3. Montar el armario, calibrar las 12 ROI y recolectar el dataset.
4. Integrar el modelo y ensayar la demo completa.

## Día 0 — Fase 0: base del proyecto

- [x] Repositorio Git local.
- [x] `platformio.ini` con entornos `main` y `camera`.
- [x] Tablas de particiones para 16 MB y 8 MB.
- [x] Pinout y configuración central.
- [x] Esqueletos de firmware compilables.
- [x] Web base en `data/`.
- [x] Documentación inicial.
- [x] Verificar compilación de ambos entornos.
- [ ] Crear repositorio remoto en GitHub y primer push.
- [ ] Confirmar pedido de XIAO, tira LED y mini UPS.

## Día 1 — Núcleo de almacenamiento y configuración

- [x] `storage`: NVS, LittleFS, escritura atómica de JSON.
- [x] `users.json`, `tools.json`, `settings.json` con valores por defecto.
- [x] `events.jsonl` con rotación al límite configurado (hasta 1000).
- [x] Estructura de eventos con `bootId`, `uptimeMs` y `epoch`.
- [x] Historial en PSRAM con respaldo en LittleFS.
- [ ] Pruebas unitarias de rotación y serialización en host o serial.
      La rotación está implementada y compila; falta la prueba automatizada.

## Día 2 — RFID, IR y actuadores

- [x] `rfid`: lectura RC522, UID a hexadecimal, búsqueda de usuario.
- [x] `ir_sensors`: antirrebote, detección de flancos, estado inicial sin
      eventos falsos.
- [x] `actuators`: cerradura, LED verde/rojo, buzzer y tira LED no
      bloqueantes.
- [x] Sesión de usuario y registro de accesos.
- [x] Manejo de herramienta tomada sin sesión.
- [ ] Validar tiempos, polaridades y consumo con el hardware real.

## Día 3 — Web, API y tiempo

- [x] `webserver`: `/api/status`, `/api/tools`, `/api/events`, `/api/photo`.
- [x] WebSocket con snapshot y eventos.
- [x] Autenticación de administrador y CRUD de usuarios.
- [x] Configuración de herramientas y exportación CSV.
- [x] NTP y reconstrucción de la hora al reconectar.
- [x] Interfaz web conectada a datos reales.
- [x] Contraseña inicial `tooltrack` con cambio obligatorio.
- [x] Bloqueo temporal tras cinco intentos fallidos.

## Día 4 — Nodo de visión: puesta en marcha

- [ ] Probar la XIAO S3 Sense y la cámara.
- [ ] Servidor HTTP del nodo: `/health`, `/latest.jpg`, `/analyze`.
- [ ] Captura UXGA y recorte de las 12 ROI.
- [ ] Script de calibración `tools/calibrate_rois.py`.
- [ ] Verificar latencia y memoria con PSRAM.

## Día 5 — Dataset

- [ ] Montar cámara y LED en el armario.
- [ ] Recolectar fotos de las 12 ranuras vacías y ocupadas.
- [ ] Incluir variantes de posición e iluminación.
- [ ] Etiquetar y revisar el dataset.
- [ ] Subir a Edge Impulse.

## Día 6 — Modelo y primera integración

- [ ] Entrenar el modelo de 13 clases.
- [ ] Cuantizar a int8 y validar métricas.
- [ ] Exportar la librería de Arduino e integrarla en PlatformIO.
- [ ] Implementar inferencia por ROI en el nodo.
- [ ] Devolver JSON con presencia, herramienta, confianza y bbox.

## Día 7 — Integración completa

- [x] `camera_link` en la placa maestra (cliente HTTP + mDNS).
- [x] Disparo tras RFID + IR estable, con reintento por cambio de IR.
- [x] Descarga de la última foto y metadatos.
- [x] Comparación IR/cámara y alertas de discrepancia.
- [x] Superposición de cajas en la web.
- [ ] Validar la integración con la XIAO y la cámara físicas.

## Día 8 — Robustez y extras

- [x] Estado de hardware en la web (uptime, heap, PSRAM, RSSI, cámara).
- [x] Recuperación ante fallos de cámara o WiFi (modo degradado).
- [x] Historial local aunque no haya router.
- [ ] Pruebas de tomada/devolución y de ranura incorrecta en hardware.
- [ ] Telegram si el tiempo lo permite (primera función a recortar).
- [x] Optimización básica de memoria: historial en PSRAM y búfer circular.

## Día 9 — Documentación y ensayo

- [x] Documentación de arquitectura, hardware, API y operación de Fase 1.
- [ ] Completar el guion de demo y el manual de mantenimiento final.
- [ ] Prueba completa de 30 minutos con usuarios reales.
- [ ] Exportar binarios y crear release `v1.0.0`.
- [ ] Ensayo de la presentación.

## Día 10 — Colchón

- [ ] Corregir fallos encontrados en el ensayo.
- [ ] Calibrar cámara y sensores en la ubicación final.
- [ ] Cargar el UPS y verificar autonomía.
- [ ] Congelar la versión de la expo.
- [ ] Respaldar el repositorio y los binarios.

## Criterios de aceptación

1. La cerradura abre solo con tarjetas autorizadas.
2. Cada tomada y devolución queda registrada con usuario y hora.
3. La web muestra el estado en menos de 2 segundos tras el cambio.
4. El panel admin permite crear y eliminar usuarios.
5. El histórico se exporta a CSV y no supera el límite configurado.
6. La cámara detecta presencia y herramienta con ≥90 % de accuracy.
7. Una discrepancia IR/cámara genera una alerta visible.
8. El sistema arranca sin generar eventos falsos.
9. La hora de los eventos se reconstruye al volver la red.
10. La demo funciona sin intervención manual durante 30 minutos.

Los criterios 1–5 y 8–9 están implementados en firmware y web, pero deben
confirmarse con el armario físico. Los criterios 6–7 dependen de la Fase 2 y
del entrenamiento del modelo. El criterio 10 requiere el ensayo final.
