# Plan de 10 días

Inicio: 26 de octubre de 2026. Entrega objetivo: expo/feria en 10 días.

## Día 0 — Fase 0: base del proyecto

- [x] Repositorio Git local.
- [x] `platformio.ini` con entornos `main` y `camera`.
- [x] Tablas de particiones para 16 MB y 8 MB.
- [x] Pinout y configuración central.
- [x] Esqueletos de firmware compilables.
- [x] Web base en `data/`.
- [x] Documentación inicial.
- [ ] Verificar compilación de ambos entornos.
- [ ] Crear repositorio remoto en GitHub y primer push.
- [ ] Confirmar pedido de XIAO, tira LED y mini UPS.

## Día 1 — Núcleo de almacenamiento y configuración

- [ ] `storage`: NVS, LittleFS, escritura atómica de JSON.
- [ ] `users.json`, `tools.json`, `settings.json` con valores por defecto.
- [ ] `events.jsonl` con rotación a 1000 eventos.
- [ ] Estructura de eventos con `bootId`, `uptimeMs` y `epoch`.
- [ ] Pruebas unitarias de rotación y serialización (host o serial).

## Día 2 — RFID, IR y actuadores

- [ ] `rfid`: lectura RC522, UID a hexadecimal, búsqueda de usuario.
- [ ] `ir_sensors`: antirrebote, detección de flancos, estado inicial sin
      eventos falsos.
- [ ] `actuators`: cerradura, LED verde/rojo, buzzer y tira LED no
      bloqueantes.
- [ ] Sesión de usuario y registro de accesos.
- [ ] Manejo de herramienta tomada sin sesión.

## Día 3 — Web, API y tiempo

- [ ] `webserver`: `/api/status`, `/api/tools`, `/api/events`, `/api/photo`.
- [ ] WebSocket con snapshot y eventos.
- [ ] Autenticación de administrador y CRUD de usuarios.
- [ ] Configuración de herramientas y exportación CSV.
- [ ] NTP y reconstrucción de la hora al reconectar.
- [ ] Interfaz web conectada a datos reales.

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

- [ ] `camera_link` en la placa maestra.
- [ ] Disparo tras RFID + IR estable.
- [ ] Descarga de la última foto y metadatos.
- [ ] Comparación IR/cámara y alertas de discrepancia.
- [ ] Superposición de cajas en la web.

## Día 8 — Robustez y extras

- [ ] Estado de hardware en la web (uptime, heap, RSSI, cámara).
- [ ] Recuperación ante fallos de cámara o WiFi.
- [ ] Pruebas de tomada/devolución y de ranura incorrecta.
- [ ] Telegram si el tiempo lo permite (primera función a recortar).
- [ ] Optimización de memoria y tiempos.

## Día 9 — Documentación y ensayo

- [ ] Completar `docs/` y README.
- [ ] Manual de operación y guion de demo.
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
