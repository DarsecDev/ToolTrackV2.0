#!/usr/bin/env python3
"""
Servidor de demo local de ToolTrack.

Sirve la web real de `data/` y simula la API REST, el WebSocket y la foto del
nodo de visión. No sustituye al firmware: sirve para ver y probar la interfaz
sin ESP32, XIAO ni sensores.

Uso:
    python3 tools/mock_server.py
    python3 tools/mock_server.py --auto 0
    python3 tools/mock_server.py --host 0.0.0.0 --port 8080 --auto 5

Contraseña de administrador de la demo: tooltrack
"""

from __future__ import annotations

import argparse
import asyncio
import io
import random
import time
import uuid
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import uvicorn
from fastapi import FastAPI, Request, Response, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT / "data"

AUTO_INTERVAL = 8.0
AUTH_TOKEN = "tooltrack-demo-session"
DEMO_PASSWORD = "tooltrack"
BOOT_ID = uuid.uuid4().hex[:8].upper()
STARTED_AT = time.time()
SESSION_USER = "Demo"
NEXT_EVENT_ID = 1
NEXT_USER_ID = 4
SIMULATION_COUNT = 0
CLIENTS: set[WebSocket] = set()

TOOL_NAMES = [
    "Llave inglesa",
    "Desarmador plano",
    "Desarmador Phillips",
    "Pinza de corte",
    "Martillo",
    "Cutter",
    "Llave Allen",
    "Multímetro",
    "Taladro",
    "Sierra",
    "Cinta métrica",
    "Pistola de calor",
]

USERS: list[dict[str, Any]] = [
    {"id": 1, "uid": "1A212828", "name": "Benjamin V.", "active": True,
     "createdAt": 0},
    {"id": 2, "uid": "A97EC6B2", "name": "Arturo F.", "active": True,
     "createdAt": 0},
    {"id": 3, "uid": "C29A713D", "name": "Mario M.", "active": True,
     "createdAt": 0},
]

SETTINGS: dict[str, Any] = {
    "maxEvents": 1000,
    "cameraEnabled": True,
    "telegramEnabled": False,
    "telegramToken": "",
    "telegramChatId": "",
    "sessionTimeoutMin": 0,
}

TOOLS: list[dict[str, Any]] = []
EVENTS: list[dict[str, Any]] = []


def epoch() -> int:
    return int(time.time())


def iso_time(value: int) -> str:
    return datetime.fromtimestamp(value, timezone.utc).astimezone().isoformat(
        timespec="seconds"
    )


def slot_for(tool_id: int) -> str:
    if tool_id < 0 or tool_id >= 12:
        return "--"
    return f"{chr(65 + tool_id // 3)}{tool_id % 3 + 1}"


def make_tool(tool_id: int) -> dict[str, Any]:
    return {
        "id": tool_id,
        "slot": slot_for(tool_id),
        "name": TOOL_NAMES[tool_id],
        "expectedClass": f"tool_{tool_id + 1}",
        "active": True,
        "present": True,
        "owner": "",
        "takenAt": 0,
        "cameraPresent": True,
        "cameraConfidence": 0,
    }


def reset_demo() -> None:
    global TOOLS, EVENTS, NEXT_EVENT_ID, NEXT_USER_ID, SIMULATION_COUNT
    TOOLS = [make_tool(i) for i in range(12)]
    EVENTS = []
    NEXT_EVENT_ID = 1
    NEXT_USER_ID = 4
    SIMULATION_COUNT = 0
    USERS[:] = [
        {"id": 1, "uid": "1A212828", "name": "Benjamin V.", "active": True,
         "createdAt": 0},
        {"id": 2, "uid": "A97EC6B2", "name": "Arturo F.", "active": True,
         "createdAt": 0},
        {"id": 3, "uid": "C29A713D", "name": "Mario M.", "active": True,
         "createdAt": 0},
    ]


def tool_public(tool: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": tool["id"],
        "slot": tool["slot"],
        "name": tool["name"],
        "expectedClass": tool["expectedClass"],
        "active": tool["active"],
        "present": tool["present"],
        "owner": tool["owner"],
        "takenAt": tool["takenAt"],
        "cameraPresent": tool["cameraPresent"],
        "cameraConfidence": tool["cameraConfidence"],
    }


def tool_admin(tool: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": tool["id"],
        "name": tool["name"],
        "expectedClass": tool["expectedClass"],
        "active": tool["active"],
    }


def snapshot_payload() -> dict[str, Any]:
    taken = sum(1 for tool in TOOLS if not tool["present"])
    return {
        "type": "snapshot",
        "data": {
            "system": "Activo (demo)",
            "firmware": "0.1.0-demo",
            "uptime": int(time.time() - STARTED_AT),
            "toolsTaken": taken,
            "toolCount": len(TOOLS),
            "cameraOnline": True,
            "timeValid": True,
            "epoch": epoch(),
            "sessionUser": SESSION_USER,
            "tools": [
                {
                    "id": tool["id"],
                    "slot": tool["slot"],
                    "name": tool["name"],
                    "present": tool["present"],
                    "owner": tool["owner"],
                    "takenAt": tool["takenAt"],
                    "cameraPresent": tool["cameraPresent"],
                    "cameraConfidence": tool["cameraConfidence"],
                }
                for tool in TOOLS
            ],
        },
    }


def status_payload() -> dict[str, Any]:
    taken = sum(1 for tool in TOOLS if not tool["present"])
    now = epoch()
    return {
        "system": "Activo (demo)",
        "firmware": "0.1.0-demo",
        "uptime": int(time.time() - STARTED_AT),
        "bootId": BOOT_ID,
        "time": {
            "epoch": now,
            "valid": True,
            "iso": iso_time(now),
            "bootEpoch": int(STARTED_AT),
        },
        "wifi": {
            "connected": True,
            "ssid": "ToolTrack-Demo",
            "rssi": -48,
            "ip": "127.0.0.1",
        },
        "camera": {
            "online": True,
            "lastSeen": now,
        },
        "power": {
            "acPresent": True,
            "upsOnBattery": False,
        },
        "toolsTaken": taken,
        "toolsKnown": len(TOOLS),
        "toolCount": len(TOOLS),
        "sessionUser": SESSION_USER,
        "heap": 248 * 1024,
        "minHeap": 196 * 1024,
        "psram": 6 * 1024 * 1024,
        "eventsCount": len(EVENTS),
    }


def tools_payload() -> dict[str, Any]:
    active_takings = [
        {
            "toolId": tool["id"],
            "slot": tool["slot"],
            "name": tool["name"],
            "owner": tool["owner"],
            "takenAt": tool["takenAt"],
        }
        for tool in TOOLS
        if not tool["present"]
    ]
    return {
        "tools": [tool_public(tool) for tool in TOOLS],
        "activeTakings": active_takings,
    }


def append_event(
    action: str,
    tool_id: int,
    source: str = "ir",
    confidence: int = 0,
    user: str | None = None,
    returned_by: str = "",
) -> dict[str, Any]:
    global NEXT_EVENT_ID
    now = epoch()
    event = {
        "id": NEXT_EVENT_ID,
        "bootId": BOOT_ID,
        "uptimeMs": int((time.time() - STARTED_AT) * 1000),
        "epoch": now,
        "timeValid": True,
        "timeSource": "ntp",
        "iso": iso_time(now),
        "toolId": tool_id,
        "slot": slot_for(tool_id),
        "action": action,
        "source": source,
        "confidence": confidence,
        "user": SESSION_USER if user is None else user,
        "returnedBy": returned_by,
    }
    NEXT_EVENT_ID += 1
    EVENTS.insert(0, event)
    while len(EVENTS) > int(SETTINGS["maxEvents"]):
        EVENTS.pop()
    return event


async def broadcast(payload: dict[str, Any]) -> None:
    if not CLIENTS:
        return
    dead: list[WebSocket] = []
    for client in list(CLIENTS):
        try:
            await client.send_json(payload)
        except Exception:
            dead.append(client)
    for client in dead:
        CLIENTS.discard(client)


async def broadcast_snapshot() -> None:
    await broadcast(snapshot_payload())


async def broadcast_tool(tool: dict[str, Any]) -> None:
    await broadcast(
        {
            "type": "tool_changed",
            "data": {
                "toolId": tool["id"],
                "slot": tool["slot"],
                "name": tool["name"],
                "present": tool["present"],
                "owner": tool["owner"],
                "takenAt": tool["takenAt"],
                "cameraPresent": tool["cameraPresent"],
                "cameraConfidence": tool["cameraConfidence"],
                "source": "ir",
            },
        }
    )


async def simulate_change() -> dict[str, Any]:
    global SIMULATION_COUNT
    SIMULATION_COUNT += 1
    tool = random.choice(TOOLS)

    if tool["present"]:
        tool["present"] = False
        tool["owner"] = SESSION_USER
        tool["takenAt"] = epoch()
        action = "taken"
        returned_by = ""
    else:
        tool["present"] = True
        tool["owner"] = ""
        tool["takenAt"] = 0
        action = "returned"
        returned_by = SESSION_USER

    confidence = random.randint(82, 99)
    tool["cameraConfidence"] = confidence
    tool["cameraPresent"] = tool["present"]

    event_action = action
    if SIMULATION_COUNT % 7 == 0:
        tool["cameraPresent"] = not tool["present"]
        event_action = "mismatch"

    event = append_event(
        event_action,
        tool["id"],
        source="camera" if event_action == "mismatch" else "ir",
        confidence=confidence if event_action == "mismatch" else 0,
        returned_by=returned_by,
    )

    await broadcast_snapshot()
    await broadcast_tool(tool)
    await broadcast({"type": "event", "data": event})
    if event_action == "mismatch":
        await broadcast(
            {
                "type": "alert",
                "data": {"code": "ir_camera_mismatch", "toolId": tool["id"]},
            }
        )
    return event


async def auto_loop() -> None:
    await asyncio.sleep(2)
    while True:
        await simulate_change()
        await asyncio.sleep(AUTO_INTERVAL)


@asynccontextmanager
async def lifespan(_app: FastAPI):
    reset_demo()
    task = asyncio.create_task(auto_loop()) if AUTO_INTERVAL > 0 else None
    try:
        yield
    finally:
        if task is not None:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass


app = FastAPI(title="ToolTrack Demo", lifespan=lifespan)


def authenticated(request: Request) -> bool:
    return request.cookies.get("TT_SESSION") == AUTH_TOKEN


def auth_error() -> JSONResponse:
    return JSONResponse(status_code=401, content={"error": "Sesión requerida"})


def png_or_jpeg_bytes() -> bytes:
    width, height = 960, 720
    image = Image.new("RGB", (width, height), (17, 20, 26))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=18)
    small = ImageFont.load_default(size=14)

    left, top, right, bottom = 40, 40, width - 40, height - 40
    gap = 12
    cell_w = (right - left - gap * 2) / 3
    cell_h = (bottom - top - gap * 3) / 4

    for tool in TOOLS:
        index = tool["id"]
        row, column = divmod(index, 3)
        x = left + column * (cell_w + gap)
        y = top + row * (cell_h + gap)
        w = cell_w
        h = cell_h
        accent = (54, 211, 153) if tool["present"] else (239, 108, 108)

        draw.rounded_rectangle(
            (x, y, x + w, y + h), radius=10, outline=(66, 74, 90), width=2,
            fill=(24, 29, 38)
        )
        draw.rectangle((x + 2, y + 2, x + 10, y + h - 2), fill=accent)
        draw.text((x + 18, y + 10), tool["slot"], fill=(230, 235, 245),
                  font=font)
        draw.text((x + 18, y + 36), tool["name"], fill=(160, 170, 190),
                  font=small)
        status = "presente" if tool["present"] else "fuera"
        draw.text((x + 18, y + h - 28), status, fill=accent, font=small)

        if tool["present"]:
            draw.rounded_rectangle(
                (x + w * 0.58, y + h * 0.32, x + w * 0.84, y + h * 0.78),
                radius=8, fill=(42, 48, 60), outline=(92, 102, 120), width=2
            )

    draw.text((40, 12), "ToolTrack · vista simulada", fill=(210, 220, 235),
              font=small)

    output = io.BytesIO()
    image.save(output, format="JPEG", quality=88)
    return output.getvalue()


def photo_meta_payload() -> dict[str, Any]:
    width, height = 960, 720
    left, top, right, bottom = 40, 40, width - 40, height - 40
    gap = 12
    cell_w = (right - left - gap * 2) / 3
    cell_h = (bottom - top - gap * 3) / 4
    slots = []
    for tool in TOOLS:
        row, column = divmod(tool["id"], 3)
        x = left + column * (cell_w + gap)
        y = top + row * (cell_h + gap)
        slots.append(
            {
                "slot": tool["slot"],
                "toolId": tool["id"],
                "present": tool["present"],
                "confidence": tool["cameraConfidence"],
                "bbox": {
                    "x": round(x, 1),
                    "y": round(y, 1),
                    "width": round(cell_w, 1),
                    "height": round(cell_h, 1),
                },
            }
        )
    return {
        "modelVersion": "demo-stub",
        "processingMs": 42,
        "capturedAt": epoch(),
        "width": width,
        "height": height,
        "slots": slots,
    }


@app.get("/")
async def index() -> FileResponse:
    return FileResponse(DATA_DIR / "index.html")


@app.get("/app.js")
async def app_js() -> FileResponse:
    return FileResponse(DATA_DIR / "app.js", media_type="application/javascript")


@app.get("/styles.css")
async def styles_css() -> FileResponse:
    return FileResponse(DATA_DIR / "styles.css", media_type="text/css")


@app.get("/favicon.ico")
async def favicon() -> Response:
    return Response(status_code=204)


@app.get("/api/auth/session")
async def auth_session(request: Request) -> dict[str, Any]:
    logged = authenticated(request)
    return {
        "authenticated": logged,
        "mustChangePassword": False,
        "expiresIn": 3600 if logged else 0,
    }


@app.post("/api/auth/login")
async def auth_login(request: Request) -> JSONResponse:
    body = await request.json()
    if body.get("password") != DEMO_PASSWORD:
        return JSONResponse(status_code=401,
                            content={"error": "Contraseña incorrecta"})
    response = JSONResponse(
        {"ok": True, "expiresIn": 3600, "mustChangePassword": False}
    )
    response.set_cookie(
        "TT_SESSION", AUTH_TOKEN, httponly=True, samesite="lax", max_age=3600
    )
    return response


@app.post("/api/auth/logout")
async def auth_logout() -> JSONResponse:
    response = JSONResponse({"ok": True})
    response.delete_cookie("TT_SESSION")
    return response


@app.post("/api/auth/change-password")
async def auth_change_password(request: Request) -> JSONResponse:
    if not authenticated(request):
        return auth_error()
    return JSONResponse({"ok": True})


@app.get("/api/status")
async def api_status() -> dict[str, Any]:
    return status_payload()


@app.get("/api/tools")
async def api_tools() -> dict[str, Any]:
    return tools_payload()


@app.get("/api/events")
async def api_events(limit: int = 50, offset: int = 0) -> dict[str, Any]:
    limit = max(1, min(limit, 100))
    offset = max(0, offset)
    return {
        "total": len(EVENTS),
        "limit": limit,
        "offset": offset,
        "events": EVENTS[offset: offset + limit],
    }


@app.get("/api/events/export.csv")
async def api_events_csv(request: Request) -> Response:
    if not authenticated(request):
        return auth_error()
    lines = [
        "id,fecha_iso,epoch,herramienta,slot,accion,usuario,devuelto_por,fuente,confianza"
    ]
    for event in reversed(EVENTS):
        lines.append(
            ",".join(
                [
                    str(event["id"]),
                    event["iso"],
                    str(event["epoch"]),
                    str(event["toolId"] + 1),
                    event["slot"],
                    event["action"],
                    event["user"],
                    event["returnedBy"],
                    event["source"],
                    str(event["confidence"]),
                ]
            )
        )
    return Response(
        "\n".join(lines) + "\n",
        media_type="text/csv",
        headers={
            "Content-Disposition": 'attachment; filename="tooltrack_eventos.csv"'
        },
    )


@app.get("/api/photo")
async def api_photo() -> Response:
    return Response(
        png_or_jpeg_bytes(),
        media_type="image/jpeg",
        headers={"Cache-Control": "no-store"},
    )


@app.get("/api/photo/meta")
async def api_photo_meta() -> dict[str, Any]:
    return photo_meta_payload()


@app.get("/api/admin/users")
async def admin_users(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    return {"users": USERS}


@app.post("/api/admin/users")
async def admin_add_user(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    body = await request.json()
    uid = "".join(
        character
        for character in str(body.get("uid", "")).upper()
        if character in "0123456789ABCDEF"
    )
    name = str(body.get("name", "")).strip()
    if len(uid) < 8 or len(uid) > 14 or len(uid) % 2 or not name:
        return JSONResponse(status_code=400,
                            content={"error": "UID o nombre inválido"})
    if any(user["uid"] == uid for user in USERS):
        return JSONResponse(status_code=400,
                            content={"error": "UID duplicado"})
    global NEXT_USER_ID
    USERS.append(
        {
            "id": NEXT_USER_ID,
            "uid": uid,
            "name": name,
            "active": True,
            "createdAt": epoch(),
        }
    )
    NEXT_USER_ID += 1
    return {"ok": True}


@app.put("/api/admin/users")
async def admin_update_user(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    body = await request.json()
    user_id = int(body.get("id", 0))
    uid = "".join(
        character
        for character in str(body.get("uid", "")).upper()
        if character in "0123456789ABCDEF"
    )
    name = str(body.get("name", "")).strip()
    if not name:
        return JSONResponse(status_code=400,
                            content={"error": "Nombre inválido"})
    for user in USERS:
        if user["id"] == user_id:
            user.update(
                uid=uid,
                name=name,
                active=bool(body.get("active", True)),
            )
            return {"ok": True}
    return JSONResponse(status_code=404,
                        content={"error": "Usuario no encontrado"})


@app.delete("/api/admin/users")
async def admin_delete_user(request: Request, id: int) -> Any:
    if not authenticated(request):
        return auth_error()
    for index, user in enumerate(USERS):
        if user["id"] == id:
            USERS.pop(index)
            return {"ok": True}
    return JSONResponse(status_code=404,
                        content={"error": "Usuario no encontrado"})


@app.get("/api/admin/tools")
async def admin_tools(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    return {"tools": [tool_admin(tool) for tool in TOOLS]}


@app.put("/api/admin/tools")
async def admin_update_tool(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    body = await request.json()
    tool_id = int(body.get("id", -1))
    if tool_id < 0 or tool_id >= len(TOOLS):
        return JSONResponse(status_code=400,
                            content={"error": "Herramienta inválida"})
    name = str(body.get("name", "")).strip()
    expected = str(body.get("expectedClass", "")).strip()
    if not name or len(name) >= 32 or len(expected) >= 20:
        return JSONResponse(status_code=400,
                            content={"error": "Datos inválidos"})
    tool = TOOLS[tool_id]
    tool["name"] = name
    tool["expectedClass"] = expected
    tool["active"] = bool(body.get("active", True))
    return {"ok": True}


@app.get("/api/admin/settings")
async def admin_settings(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    return SETTINGS


@app.put("/api/admin/settings")
async def admin_update_settings(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    body = await request.json()
    previous = int(SETTINGS["maxEvents"])
    max_events = int(body.get("maxEvents", previous))
    if max_events < 50 or max_events > 1000:
        return JSONResponse(status_code=400,
                            content={"error": "maxEvents fuera de rango"})
    SETTINGS.update(
        {
            "maxEvents": max_events,
            "cameraEnabled": bool(
                body.get("cameraEnabled", SETTINGS["cameraEnabled"])
            ),
            "telegramEnabled": bool(
                body.get("telegramEnabled", SETTINGS["telegramEnabled"])
            ),
            "telegramToken": str(
                body.get("telegramToken", SETTINGS["telegramToken"])
            ),
            "telegramChatId": str(
                body.get("telegramChatId", SETTINGS["telegramChatId"])
            ),
            "sessionTimeoutMin": min(
                1440,
                max(0, int(body.get("sessionTimeoutMin", 0))),
            ),
        }
    )
    while len(EVENTS) > max_events:
        EVENTS.pop()
    return {"ok": True, "rebootRequired": previous != max_events}


@app.post("/api/admin/history/clear")
async def admin_clear_history(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    EVENTS.clear()
    await broadcast_snapshot()
    return {"ok": True}


@app.post("/api/admin/camera/analyze")
async def admin_camera_analyze(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    for tool in TOOLS:
        tool["cameraConfidence"] = random.randint(85, 99)
        tool["cameraPresent"] = tool["present"]
    await broadcast_snapshot()
    await broadcast(
        {
            "type": "camera_status",
            "data": {"online": True, "lastSeen": epoch()},
        }
    )
    return JSONResponse(status_code=202, content={"ok": True})


@app.post("/api/admin/restart")
async def admin_restart(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    return {"ok": True}


@app.post("/api/admin/wifi/reset")
async def admin_wifi_reset(request: Request) -> Any:
    if not authenticated(request):
        return auth_error()
    return {"ok": True}


@app.post("/api/demo/simulate")
async def demo_simulate() -> dict[str, Any]:
    event = await simulate_change()
    return {"ok": True, "event": event}


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    CLIENTS.add(websocket)
    await websocket.send_json(snapshot_payload())
    try:
        while True:
            text = await websocket.receive_text()
            if "ping" in text:
                await websocket.send_json({"type": "pong"})
    except WebSocketDisconnect:
        pass
    finally:
        CLIENTS.discard(websocket)


def main() -> None:
    global AUTO_INTERVAL
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument(
        "--auto",
        type=float,
        default=8.0,
        help="Segundos entre eventos simulados; 0 desactiva la simulación",
    )
    args = parser.parse_args()
    AUTO_INTERVAL = max(0.0, args.auto)
    uvicorn.run(app, host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
