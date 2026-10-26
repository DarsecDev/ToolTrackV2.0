#!/usr/bin/env python3
"""
Recolector de dataset para el nodo de visión ToolTrack.

Descarga imágenes del endpoint de captura de la cámara y las guarda
etiquetadas para entrenar el modelo en Edge Impulse.

Uso:
    python3 tools/collect_dataset.py --label A1_empty --count 40
    python3 tools/collect_dataset.py --label A1_tool_1 --count 60 --interval 1.5

El endpoint /capture.jpg se implementará en la Fase 2.
"""

import argparse
import re
import time
import urllib.error
import urllib.request
from pathlib import Path


def sanitize(label: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", label).strip("_")


def download(url: str, destination: Path, timeout: float) -> int:
    with urllib.request.urlopen(url, timeout=timeout) as response:
        data = response.read()
        content_type = response.headers.get("Content-Type", "")
    if not content_type.startswith("image/"):
        raise ValueError(f"El endpoint no devolvió una imagen ({content_type})")
    destination.write_bytes(data)
    return len(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://tooltrack-cam.local",
                        help="Base del nodo de visión")
    parser.add_argument("--endpoint", default="/capture.jpg",
                        help="Ruta de captura")
    parser.add_argument("--label", required=True,
                        help="Etiqueta de la clase, por ejemplo A1_empty")
    parser.add_argument("--count", type=int, default=20,
                        help="Número de fotos a capturar")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="Segundos entre capturas")
    parser.add_argument("--out", default="dataset",
                        help="Carpeta de salida")
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    label = sanitize(args.label)
    if not label:
        parser.error("La etiqueta no es válida")

    target = Path(args.out) / label
    target.mkdir(parents=True, exist_ok=True)

    base = args.url.rstrip("/")
    endpoint = args.endpoint if args.endpoint.startswith("/") else f"/{args.endpoint}"

    print(f"Capturando {args.count} imágenes en {target}")
    saved = 0
    for index in range(1, args.count + 1):
        filename = target / f"{label}_{index:04d}.jpg"
        try:
            size = download(f"{base}{endpoint}", filename, args.timeout)
            saved += 1
            print(f"  [{index}/{args.count}] {filename.name} ({size} bytes)")
        except (urllib.error.URLError, ValueError, TimeoutError) as error:
            print(f"  [{index}/{args.count}] error: {error}")
        time.sleep(max(args.interval, 0))

    print(f"Guardadas {saved} imágenes en {target}")
    return 0 if saved else 1


if __name__ == "__main__":
    raise SystemExit(main())
