# Visión artificial

> **Estado:** el nodo de visión ya captura, expone `/analyze` y gestiona ROI.
> El clasificador es un stub (`modelVersion="none"`): sin modelo devuelve las
> 12 ranuras como desconocidas y confianza 0; no simula detecciones. La
> identificación real queda pendiente del dataset y de Edge Impulse.

## Objetivo

Con una sola cámara se deben responder dos preguntas por cada una de las 12
ranuras:

1. ¿Hay una herramienta? (presencia)
2. ¿Es la herramienta que corresponde a esa ranura? (identificación)

La presencia oficial la decide el sensor IR. La cámara aporta identidad y
verifica la ranura. Si hay discrepancia, gana el IR y se registra una alerta.

## Estrategia

La cámara está fija y las herramientas tienen una ranura asignada. Por eso se
trabaja con **clasificación por regiones de interés (ROI)**:

```
Frame 1600×1200 (UXGA)
┌─────────┬─────────┬─────────┐
│   A1    │   A2    │   A3    │
├─────────┼─────────┼─────────┤
│   B1    │   B2    │   B3    │
├─────────┼─────────┼─────────┤
│   C1    │   C2    │   C3    │
├─────────┼─────────┼─────────┤
│   D1    │   D2    │   D3    │
└─────────┴─────────┴─────────┘
```

Cada ROI se recorta y se redimensiona a la entrada del modelo (96×96 o
160×160). El modelo devuelve una clase entre:

- `empty`
- `tool_1` … `tool_12`

Total: **13 clases**. Con la clase predicha y la confianza se decide:

| Predicción | Confianza | Resultado |
|---|---|---|
| `empty` | ≥ 60 % | Ranura vacía |
| `tool_N` | ≥ 60 % | Herramienta N presente |
| Cualquiera | < 60 % | `unknown`; decide el IR |

Si la herramienta N aparece en una ranura distinta de la esperada, se genera
`mismatch`. Si aparece en dos ranuras a la vez, también.

## Recolección del dataset

Se necesitan fotos reales, con la cámara montada en su posición final.

### Cantidad recomendada

| Estado | Fotos por ranura |
|---|---|
| Ranura vacía | 40–60 |
| Herramienta correcta | 60–100 |
| Variantes de posición | 20–30 |
| Otras herramientas en la ranura (negativos) | 10–20 |

Total aproximado: **1.200–1.800 imágenes**. Con aumentos de datos
(rotaciones, brillo, ruido) se puede reducir a ~800.

### Protocolo

1. Montar la cámara y la lámpara 12 V en su posición definitiva.
2. Fijar la resolución y la calidad de JPEG.
3. Ejecutar `tools/collect_dataset.py` con la etiqueta correspondiente.
   El script descarga frames completos desde `/capture.jpg`; las etiquetas
   `A1_empty`/`A1_tool_1` organizan la recolección por ranura, pero antes de
   entrenar hay que recortar cada ROI y usar las clases finales
   `empty`/`tool_1`…`tool_12`.
4. Variar:
   - herramienta centrada, corrida, rotada;
   - luz ambiental y lámpara 12 V encendida/apagada;
   - puerta abierta y cerrada (si afecta la iluminación).
5. Revisar y descartar fotos borrosas o mal etiquetadas.
6. Subir el dataset a Edge Impulse.

## Entrenamiento en Edge Impulse

1. Crear un proyecto de **Image Classification**.
2. Subir las imágenes a las 13 clases.
3. Configurar entrada 96×96 o 160×160, RGB.
4. Entrenar con transfer learning (MobileNetV2 0.35 o similar).
5. Activar cuantización **int8** para reducir tamaño y latencia.
6. Validar con un conjunto separado (20 %).
7. Exportar como **Arduino library** para ESP32-S3.

### Métricas mínimas para la expo

| Métrica | Objetivo |
|---|---|
| Accuracy global | ≥ 90 % |
| Precision por herramienta | ≥ 85 % |
| Latencia por ranura | ≤ 200 ms |
| Latencia total (12 ranuras) | ≤ 2.5 s |
| Tamaño del modelo | ≤ 1 MB |

## Calibración de las ROI

Las coordenadas se guardan en LittleFS como `rois.json` con valores
normalizados (0–1) para que no dependan de la resolución:

```json
{
  "image_size": [1600, 1200],
  "rois": {
    "A1": [0.04, 0.05, 0.28, 0.27],
    "A2": [0.37, 0.05, 0.28, 0.27],
    "A3": [0.70, 0.05, 0.28, 0.27]
  }
}
```

El archivo real debe contener las 12 claves A1…D3.

Procedimiento:

1. Capturar una foto con el armario en su estado normal.
2. Ejecutar `tools/calibrate_rois.py foto.jpg`.
3. Seleccionar con el mouse las 12 zonas en orden A1…D3.
4. Copiar `rois.json` al nodo de visión (LittleFS o subida por HTTP).
5. Verificar con una segunda foto y el endpoint `/analyze`.

## Integración en el firmware

- La cámara captura en UXGA (1600×1200) para tener suficientes píxeles por
  ranura.
- El frame se mantiene en PSRAM.
- Para cada ROI: recorte, escalado y clasificación.
- El resultado incluye `bbox` en píxeles para que la web dibuje las cajas.
- La última foto y el último JSON se guardan en LittleFS.
- La placa maestra descarga la foto y la sirve en `/api/photo`.

### Punto de integración del modelo

El nodo ya separa la captura (`camera_vision.cpp`) de la inferencia
(`camera_classifier.cpp`). Para integrar el modelo:

1. Exportar la librería de Arduino desde Edge Impulse.
2. Añadirla a `lib_deps` o a `lib/` para el entorno `camera`.
3. Implementar `classifierAvailable()`, `modelName()`, `modelInputSize()` y
   `classify()` en `src/camera/camera_classifier.cpp` usando `run_classifier()`.
4. Mantener las etiquetas `empty` y `tool_1`…`tool_12`.
5. Compilar y probar `/analyze` con el armario y las ROI reales.

Mientras `classifierAvailable()` devuelva `false`, `camera_vision.cpp` no
convierte el frame completo a RGB ni ejecuta inferencia; esto ahorra PSRAM y
evita resultados falsos.

## Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Reflejos del LED | Tira difusa, cámara fuera del eje, luz indirecta |
| Herramientas parecidas | Incluir negativos y varias vistas; subir confianza mínima |
| Cambios de iluminación | Entrenar con luz natural y LED encendido/apagado |
| Cámara desalineada | ROIs normalizadas + recalibración rápida |
| Modelo lento | Input 96×96, int8, PSRAM, una inferencia por ranura |
| Sin cámara | IR decide; alerta de cámara offline |

## Plan de contingencia

Si el modelo no alcanza la precisión necesaria antes de la expo:

1. Mantener presencia por IR como fuente oficial.
2. Usar la cámara solo para verificar “vacío / ocupado” por diferencia de
   imagen (sin identificación).
3. Mostrar la última foto y las cajas en la web.
4. Documentar la identificación como trabajo futuro.
