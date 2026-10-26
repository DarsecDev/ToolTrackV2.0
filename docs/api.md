# API y WebSocket

> Estado: contrato implementado en la Fase 1 para la placa maestra. El nodo de
> visión (XIAO ESP32-S3 Sense) corresponde a la Fase 2 y se documenta al final
> como contrato objetivo.

Base: `http://tooltrack.local` (o la IP que asigne el router).

## Públicos

### `GET /api/status`

```json
{
  "system": "Activo",
  "firmware": "0.1.0",
  "uptime": 12345,
  "bootId": "b7f3c2",
  "time": {
    "epoch": 1761600000,
    "valid": true,
    "iso": "2026-10-26T18:00:00Z",
    "bootEpoch": 1761587655
  },
  "wifi": {
    "connected": true,
    "ssid": "Taller",
    "rssi": -52,
    "ip": "192.168.1.50"
  },
  "camera": { "online": true, "lastSeen": 1761599990 },
  "power": { "acPresent": true, "upsOnBattery": false },
  "toolsTaken": 2,
  "toolsKnown": 12,
  "toolCount": 12,
  "sessionUser": "Benjamin V.",
  "heap": 182000,
  "minHeap": 141000,
  "psram": 7200000,
  "eventsCount": 245
}
```

`time.valid` indica si hay hora NTP. Si es `false`, los eventos nuevos se
guardan con `epoch: 0` y se reconstruyen al sincronizar.

### `GET /api/tools`

```json
{
  "tools": [
    {
      "id": 0,
      "slot": "A1",
      "name": "Taladro",
      "expectedClass": "drill",
      "active": true,
      "present": true,
      "owner": "",
      "takenAt": 0,
      "cameraPresent": true,
      "cameraConfidence": 93
    }
  ],
  "activeTakings": [
    {
      "toolId": 3,
      "slot": "B1",
      "name": "Llave inglesa",
      "owner": "Benjamin V.",
      "takenAt": 1761599000
    }
  ]
}
```

`present` puede ser `null` si el sensor todavía no tiene una lectura válida.
`owner` queda vacío cuando la herramienta está presente. `cameraConfidence`
se expresa en porcentaje (0–100).

### `GET /api/events`

Parámetros implementados: `limit` (máximo 100, por defecto 50) y `offset`.
Los filtros por herramienta/usuario/fecha se aplican actualmente en la web;
queda pendiente moverlos al firmware si el histórico crece.

```json
{
  "total": 245,
  "limit": 50,
  "offset": 0,
  "events": [
    {
      "id": 245,
      "bootId": "b7f3c2",
      "uptimeMs": 120000,
      "epoch": 1761600000,
      "timeValid": true,
      "timeSource": "ntp",
      "iso": "2026-10-26T18:00:00Z",
      "toolId": 3,
      "slot": "B1",
      "action": "taken",
      "source": "ir",
      "confidence": 0,
      "user": "Benjamin V.",
      "returnedBy": ""
    }
  ]
}
```

Acciones: `taken`, `returned`, `mismatch`, `access_denied`, `access_granted`,
`camera_offline`, `power_lost`, `power_restored`.

Fuentes: `ir`, `camera`, `rfid`, `system`.

`timeSource` puede ser `ntp`, `reconstructed` o `unknown`.

### `GET /api/photo`

Última foto capturada por el nodo de visión (`image/jpeg`). Devuelve `404` si
todavía no hay una imagen.

### `GET /api/photo/meta`

Devuelve el último JSON recibido del nodo de visión. Ejemplo del contrato de la
cámara:

```json
{
  "capturedAt": 1761600000,
  "processingMs": 1250,
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

La web superpone `bbox` sobre la foto usando `width` y `height`.

## Autenticación

La contraseña inicial es `tooltrack` y debe cambiarse en el primer acceso. La
sesión se guarda en una cookie `HttpOnly`; el servidor mantiene un único token
válido en RAM (se invalida al reiniciar). Tras 5 intentos fallidos se bloquea
el acceso durante 60 segundos.

### `POST /api/auth/login`

```json
{ "password": "..." }
```

Respuesta:

```json
{ "ok": true, "expiresIn": 3600, "mustChangePassword": true }
```

Mientras `mustChangePassword` sea `true`, los endpoints `/api/admin/*`
responden `403 {"error":"must_change_password"}`.

### `POST /api/auth/change-password`

```json
{ "current": "...", "new": "..." }
```

La nueva contraseña debe tener al menos 4 caracteres.

### `POST /api/auth/logout`

Cierra la sesión y borra la cookie.

### `GET /api/auth/session`

```json
{ "authenticated": true, "mustChangePassword": false, "expiresIn": 3540 }
```

## Administración (requiere sesión)

| Método | Ruta | Descripción |
|---|---|---|
| GET | `/api/admin/users` | Lista usuarios RFID |
| POST | `/api/admin/users` | Crea usuario con body `{uid, name}` |
| PUT | `/api/admin/users` | Edita con body `{id, uid, name, active}` |
| DELETE | `/api/admin/users?id=1` | Elimina usuario |
| GET | `/api/admin/tools` | Lista configuración de herramientas |
| PUT | `/api/admin/tools` | Edita con body `{id, name, expectedClass, active}` |
| GET | `/api/admin/settings` | Ajustes del sistema |
| PUT | `/api/admin/settings` | Actualiza ajustes |
| GET | `/api/events/export.csv` | Descarga el histórico completo en CSV |
| POST | `/api/admin/history/clear` | Borra el histórico |
| POST | `/api/admin/camera/analyze` | Encola un análisis (`202`) |
| POST | `/api/admin/restart` | Reinicia la placa |
| POST | `/api/admin/wifi/reset` | Borra credenciales WiFi y reinicia |

Cuerpo de `PUT /api/admin/settings`:

```json
{
  "maxEvents": 200,
  "cameraEnabled": true,
  "telegramEnabled": false,
  "telegramToken": "",
  "telegramChatId": "",
  "sessionTimeoutMin": 10
}
```

`maxEvents` admite 50–1000. Cambiarlo devuelve `rebootRequired: true` porque el
búfer circular se dimensiona al arrancar. `sessionTimeoutMin: 0` desactiva el
cierre automático de sesión.

## WebSocket

`ws://tooltrack.local/ws`

El servidor envía un `snapshot` al conectar y luego actualizaciones. El cliente
puede enviar `{"type":"ping"}` y recibe `{"type":"pong"}`.

```json
{
  "type": "snapshot",
  "data": {
    "system": "Activo",
    "firmware": "0.1.0",
    "uptime": 12345,
    "toolsTaken": 2,
    "toolCount": 12,
    "cameraOnline": true,
    "timeValid": true,
    "epoch": 1761600000,
    "sessionUser": "Benjamin V.",
    "tools": []
  }
}
```

```json
{
  "type": "tool_changed",
  "data": {
    "toolId": 3,
    "slot": "B1",
    "name": "Llave inglesa",
    "present": false,
    "owner": "Benjamin V.",
    "takenAt": 1761600000,
    "cameraPresent": false,
    "cameraConfidence": 91,
    "source": "ir"
  }
}
```

```json
{
  "type": "event",
  "data": {
    "id": 246,
    "action": "taken",
    "toolId": 3,
    "slot": "B1",
    "user": "Benjamin V.",
    "returnedBy": "",
    "epoch": 1761600000,
    "timeValid": true,
    "timeSource": "ntp",
    "iso": "2026-10-26T18:00:00Z",
    "source": "ir",
    "confidence": 0
  }
}
```

```json
{ "type": "alert", "data": { "code": "ir_camera_mismatch", "toolId": 3 } }
{ "type": "camera_status", "data": { "online": false, "lastSeen": 1761599990 } }
```

Códigos de alerta: `access_denied`, `taken_without_session`,
`ir_camera_mismatch`, `tool_wrong_slot`, `power_lost`.

## API del nodo de visión (Fase 2)

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

`confidence` puede venir en escala 0–1 o 0–100; la placa maestra la normaliza.
Si un slot no se incluye, se conserva sin cambio.

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
| 403 | Acceso denegado, o `must_change_password` |
| 404 | Recurso inexistente |
| 413 | Cuerpo de petición demasiado grande |
| 429 | Demasiados intentos de login |
| 500 | Error interno |
