# Scripts de apoyo

| Script | Uso |
|---|---|
| `flash_main.sh` | Compila y flashea la placa maestra (`--with-fs` sube la web) |
| `flash_camera.sh` | Compila y flashea el nodo de visión |
| `run_mock.sh` | Servidor de demo local sin hardware |
| `mock_server.py` | API, WebSocket y foto simuladas para la web |
| `collect_dataset.py` | Descarga fotos etiquetadas para Edge Impulse |
| `calibrate_rois.py` | Calibra las 12 zonas de la cámara con OpenCV |

## Demo sin hardware

```bash
python3 -m pip install -r tools/requirements-demo.txt
./tools/run_mock.sh
```

Abre `http://127.0.0.1:8000`; la contraseña de administrador es `tooltrack`.
El servidor sirve la web real de `data/` y simula el firmware: estado, 12
herramientas, eventos, histórico, usuarios, ajustes, CSV, foto con cajas y
WebSocket.

```bash
./tools/run_mock.sh --auto 0
./tools/run_mock.sh --auto 3 --port 8080
./tools/run_mock.sh --host 0.0.0.0 --port 8080
```

`--auto` es el intervalo en segundos entre eventos simulados; `0` lo desactiva.
`POST /api/demo/simulate` fuerza un evento. Los datos se guardan en memoria y
se pierden al detener el proceso.

## Pruebas de host

```bash
./test/native/run_tests.sh
```

Compila la lógica de `src/main/` con un stub de Arduino y ejecuta las
comprobaciones de pines, estado, IR y actuadores sin placas.

## Permisos

```bash
chmod +x tools/*.sh tools/*.py
```

## Dataset

```bash
python3 tools/collect_dataset.py --label A1_empty --count 40
python3 tools/collect_dataset.py --label A1_tool_1 --count 60
```

El script descarga frames completos desde `/capture.jpg`; el recorte de ROI y
las clases finales (`empty`/`tool_N`) se preparan antes de subir el dataset a
Edge Impulse.

## Calibración

```bash
pip install opencv-python
python3 tools/calibrate_rois.py foto_armario.jpg --out rois.json
```
