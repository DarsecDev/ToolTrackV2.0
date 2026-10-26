# API y WebSocket

> Estado: contrato planificado para la Fase 1. Los endpoints marcados con
> **(pendiente)** todavía no están implementados.

Base: `http://tooltrack.local`

## Públicos

### `GET /api/status`

```json
{
  "system": "Activo",
  "firmware": "0.1.0",
  "uptime": 12345,
  "bootId": "b7f3c2",
  "time": { "epoch": 1761600000, "valid": true },
  "wifi": { "connected": true, "ssid": "Taller", "rssi": -52, "ip": "192.168.1.50" },
  "camera": { "online": true, "lastSeen": 1761599990 },
  "power": { "acPresent": true, "upsOnBattery": false },
  "toolsTaken": 2,
  "toolCount": 12
}
```

### `GET /api/tools`

```json
{
  "tools": [
    {
      "id": 0,
      "slot": "A1",
      "name": "Taladro",
      "present": true,
      "owner": null,
      "takenAt": null,
      "source": "ir",
      "confidence": null
    }
  ],
  "activeTakings": [
    { "toolId": 3, "slot": "B1", "owner": "Benjamin V.", "takenAt": 1761599000 }
  ]
}
```

### `GET /api/events`

Parámetros: `limit` (máx. 100), `offset`, `tool`, `user`, `from`, `to`.

```json
{
  "total": 245,
  "events": [
    {
      "id": "evt-000245",
      "bootId": "b7f3c2",
      "uptimeMs": 120000,
      "epoch": 1761600000,
      "timeValid": true,
      "toolId": 3,
      "slot": "B1",
      "action": "taken",
      "user": "Benjamin V.",
      "returnedBy": null,
      "source": "ir",
      "confidence": null
    }
  ]
}
```

Acciones: `taken`, `returned`, `mismatch`, `access_denied`, `access_granted`,
`camera_offline`, `power_lost`, `power_restored`.

### `GET /api/events/export.csv`

Descarga el histórico completo en CSV.

### `GET /api/photo`

Última foto capturada (`image/jpeg`).

### `GET /api/photo/meta`

```json
{
  "capturedAt": 1761600000,
  "modelVersion": "ei-tooltrack-v1",
  "width": 1600,
  "height": 1200,
  "slots": [
    {
      "slot": "A1",
      "present": true,
      "toolId": 0,
      "confidence": 0.93,
      "bbox": [10, 20, 300, 260]
    }
  ]
}
```

## Autenticación

### `POST /api/auth/login`

```json
{ "password": "..." }
```

Respuesta: `200 {"ok": true, "expiresIn": 3600}` y cookie de sesión
`HttpOnly`, `SameSite=Lax`. Tras 5 intentos fallidos se bloquea 60 s.

### `POST /api/auth/logout`

Cierra la sesión.

### `GET /api/auth/session`

```json
{ "authenticated": true, "expiresAt": 1761603600 }
```

## Administración (requiere sesión)

| Método | Ruta | Descripción |
|---|---|---|
| GET | `/api/admin/users` | Lista usuarios RFID |
| POST | `/api/admin/users` | Crea usuario `{uid, name}` |
| PUT | `/api/admin/users/{id}` | Edita `{uid, name, active}` |
| DELETE | `/api/admin/users/{id}` | Elimina usuario |
| GET | `/api/admin/tools` | Lista configuración de herramientas |
| PUT | `/api/admin/tools/{id}` | Edita `{name, expectedClass, active}` |
| GET | `/api/admin/settings` | Ajustes del sistema |
| PUT | `/api/admin/settings` | Actualiza ajustes |
| POST | `/api/admin/history/clear` | Borra el histórico |
| POST | `/api/admin/camera/analyze` | Fuerza un análisis |
| POST | `/api/admin/restart` | Reinicia la placa |
| POST | `/api/admin/wifi/reset` | Borra credenciales WiFi y reinicia |

## WebSocket

`ws://tooltrack.local/ws`

Mensajes del servidor:

```json
{ "type": "snapshot", "data": { "tools": [], "status": {} } }
{ "type": "tool_changed", "data": { "toolId": 3, "present": false, "source": "ir" } }
{ "type": "event", "data": { "action": "taken", "toolId": 3 } }
{ "type": "alert", "data": { "code": "ir_camera_mismatch", "toolId": 3 } }
{ "type": "camera_status", "data": { "online": false } }
```

El cliente puede enviar `{"type":"ping"}` y recibe `{"type":"pong"}`.

## API del nodo de visión

Base: `http://tooltrack-cam.local`

### `GET /health`

```json
{ "ok": true, "firmware": "0.1.0", "model": "ei-tooltrack-v1", "psram": true }
```

### `POST /analyze`

La placa maestra envía `{"requestId":"..."}`. El nodo responde:

```json
{
  "requestId": "...",
  "capturedAt": 1761600000,
  "processingMs": 1250,
  "modelVersion": "ei-tooltrack-v1",
  "width": 1600,
  "height": 1200,
  "slots": [
    { "slot": "A1", "present": true, "toolId": 0, "confidence": 0.93, "bbox": [10, 20, 300, 260] }
  ]
}
```

### `GET /latest.jpg`

Última imagen capturada.

### `GET /latest.json`

Último resultado del análisis.

### `POST /dataset/capture?label=A1_empty`

Solo en modo recolección: guarda una foto etiquetada para entrenar.

## Códigos de error

| Código | Significado |
|---|---|
| 400 | JSON o parámetros inválidos |
| 401 | Sesión requerida o expirada |
| 403 | Acceso denegado |
| 404 | Recurso inexistente |
| 409 | Conflicto (UID duplicado) |
| 429 | Demasiados intentos |
| 500 | Error interno |
| 503 | Cámara no disponible |
