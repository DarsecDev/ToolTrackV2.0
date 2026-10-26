# Scripts de apoyo

| Script | Uso |
|---|---|
| `flash_main.sh` | Compila y flashea la placa maestra (`--with-fs` sube la web) |
| `flash_camera.sh` | Compila y flashea el nodo de visión |
| `collect_dataset.py` | Descarga fotos etiquetadas para Edge Impulse |
| `calibrate_rois.py` | Calibra las 12 zonas de la cámara con OpenCV |

## Permisos

```bash
chmod +x tools/*.sh tools/*.py
```

## Dataset

```bash
python3 tools/collect_dataset.py --label A1_empty --count 40
python3 tools/collect_dataset.py --label A1_tool_1 --count 60
```

## Calibración

```bash
pip install opencv-python
python3 tools/calibrate_rois.py foto_armario.jpg --out rois.json
```
