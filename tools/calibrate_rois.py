#!/usr/bin/env python3
"""
Calibración interactiva de las 12 regiones de interés (ROI).

Requiere opencv-python:
    pip install opencv-python

Uso:
    python3 tools/calibrate_rois.py foto.jpg --out rois.json

Se abre una ventana por cada ranura (A1..D3). Con el mouse se dibuja el
rectángulo que contiene la herramienta. Al terminar se guarda un JSON con
coordenadas normalizadas (0 a 1).
"""

import argparse
import json
from pathlib import Path

import cv2

SLOTS = [f"{row}{col}" for row in "ABCD" for col in "123"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="Foto del armario con las herramientas")
    parser.add_argument("--out", default="rois.json", help="Archivo de salida")
    parser.add_argument("--scale", type=float, default=1.0,
                        help="Escala de la ventana (por ejemplo 0.5)")
    args = parser.parse_args()

    image = cv2.imread(args.image)
    if image is None:
        parser.error(f"No se pudo leer {args.image}")

    if args.scale != 1.0:
        image = cv2.resize(image, None, fx=args.scale, fy=args.scale)

    height, width = image.shape[:2]
    rois = {}

    print("Dibuja cada zona con el mouse y pulsa ENTER. ESC cancela.")
    for slot in SLOTS:
        window = f"ToolTrack - {slot}"
        x, y, w, h = cv2.selectROI(window, image, showCrosshair=True,
                                   fromCenter=False)
        cv2.destroyWindow(window)
        if w == 0 or h == 0:
            print(f"[{slot}] selección vacía, se omite")
            continue
        rois[slot] = [
            round(x / width, 4),
            round(y / height, 4),
            round(w / width, 4),
            round(h / height, 4),
        ]
        print(f"[{slot}] {rois[slot]}")

    output = {
        "image_size": [width, height],
        "rois": rois,
    }
    Path(args.out).write_text(json.dumps(output, indent=2) + "\n",
                              encoding="utf-8")
    print(f"Guardado {args.out} con {len(rois)} zonas")
    return 0 if len(rois) == len(SLOTS) else 1


if __name__ == "__main__":
    raise SystemExit(main())
