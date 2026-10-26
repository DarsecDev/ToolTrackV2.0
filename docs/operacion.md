# Manual de operación

> **Estado:** Fase 1 implementada. Este manual describe el comportamiento del
> firmware y de la web actuales. La validación con el armario, la cerradura y la
> cámara físicos está pendiente.

## Alcance

ToolTrack registra el uso de 12 herramientas distribuidas en una matriz 4x3.
La placa maestra lee tarjetas RFID y sensores IR, controla la cerradura y los
indicadores, y publica el estado por WiFi. La cámara es una verificación
adicional: **la presencia oficial siempre la decide el IR**.

## Roles

| Rol | Acceso |
|---|---|
| Usuario | Tarjeta RFID para abrir el armario y tomar o devolver herramientas |
| Administrador | Panel web con contraseña: usuarios, herramientas, ajustes y sistema |

No hay roles intermedios. El administrador no abre la cerradura desde la web.

## Primer arranque

1. Alimenta la placa maestra con 12 V a través del UPS y el buck 12→5 V.
2. Si no hay credenciales WiFi, la placa crea el portal **`ToolTrack-Setup`**
   durante 120 segundos.
3. Conéctate al portal desde el teléfono y configura la red del taller.
4. Si el portal expira, reinicia la placa para volver a abrirlo.
5. Abre `http://tooltrack.local` o la IP que muestre el monitor serial.
6. Entra en **Administración** con la contraseña inicial `tooltrack`.
7. La web exige cambiarla antes de mostrar el panel.
8. Revisa los usuarios de ejemplo y ajusta sus UID a las tarjetas reales.
9. Revisa los nombres y clases esperadas de las 12 herramientas.

La contraseña inicial se configura en `include/secrets.h`. No se debe dejar
`tooltrack` en la versión de la expo.

## Flujo del usuario

1. Acercar la tarjeta al lector RC522.
2. **Acceso autorizado:**
   - La cerradura se abre durante 3 segundos.
   - El LED verde se enciende y suena un beep corto.
   - La tira LED se enciende durante 60 segundos.
   - El usuario queda como sesión activa.
3. Tomar o devolver una herramienta.
4. El cambio de IR se registra y la web se actualiza por WebSocket.
5. Cuando los IR se estabilizan, la placa maestra pide un análisis a la cámara.
6. Al pasar otra tarjeta válida, la sesión cambia al nuevo usuario.

La sesión no se cierra al cerrar la puerta porque no hay sensor de puerta.
Permanece activa hasta que pase otra tarjeta o venza el tiempo de inactividad
configurado.

### Tomada sin sesión

Si se retira una herramienta sin una sesión activa:

- Se registra con el usuario **“Sin sesión”**.
- Se genera una alerta en la web.
- La cerradura no se abre y no se autoriza el acceso.

### Acceso denegado

- El LED rojo y el buzzer parpadean durante 3 segundos.
- Se registra el UID intentado como acceso denegado.

## Flujo del administrador

1. Abrir `http://tooltrack.local`.
2. Pestaña **Administración** → iniciar sesión.
3. Cambiar la contraseña inicial en el primer acceso.
4. Gestionar las secciones:

| Sección | Acciones |
|---|---|
| **Usuarios** | Crear, editar, activar/desactivar y eliminar tarjetas RFID |
| **Herramientas** | Nombre visible, clase esperada y activación de cada ranura |
| **Ajustes** | Límite de eventos, cierre de sesión, cámara y campos de Telegram |
| **Sistema** | Diagnóstico, forzar análisis, exportar CSV, borrar histórico, reset WiFi y reiniciar |

### Usuarios

- El UID se escribe en hexadecimal, sin espacios.
- El UID se normaliza a mayúsculas.
- Un usuario inactivo no abre la cerradura.
- No se puede duplicar un UID.
- Los cambios se aplican en cuanto se guardan; no hace falta reiniciar.

### Herramientas

- El nombre es el texto visible en la web y en el histórico.
- La clase esperada debe coincidir con la etiqueta del modelo de visión.
- Una ranura inactiva se sigue leyendo por IR, pero no se usa para comparar con
  la cámara.
- La clase esperada admite hasta 19 caracteres.

### Ajustes

- **Límite de eventos:** de 50 a 1000. El cambio de tamaño requiere reiniciar.
- **Cierre de sesión:** minutos de inactividad; `0` lo desactiva.
- **Verificación por cámara:** activa o desactiva el análisis posterior al IR.
- **Telegram:** los campos se conservan, pero el envío **no está implementado**
  en esta versión.

### Sistema

- **Forzar análisis de cámara:** encola una captura aunque no haya cambiado un IR.
- **Exportar histórico:** descarga un CSV. Requiere sesión de administrador.
- **Borrar histórico:** elimina los eventos del equipo; no se puede deshacer.
  Exporta antes de borrar.
- **Borrar WiFi:** elimina las credenciales, reinicia y vuelve a abrir el portal
  `ToolTrack-Setup`. No borra usuarios, herramientas ni histórico.
- **Reiniciar equipo:** reinicia el firmware sin borrar datos.

## Indicadores

| Indicador | Significado |
|---|---|
| LED verde encendido | Acceso autorizado; se apaga al cerrar la cerradura (3 s) |
| LED rojo intermitente | Acceso denegado, discrepancia IR/cámara o tomada sin sesión |
| Beep corto | Acceso correcto |
| Beep intermitente | Alerta |
| Tira LED encendida | Armario en uso o cámara capturando; se apaga a los 60 s |
| Cámara “En línea” | El nodo respondió al último chequeo |
| Cámara “Sin conexión” | El IR sigue decidiendo; se registra el evento y se alerta |

No hay un LED rojo fijo de “sistema bloqueado” en la versión actual.

## Histórico

- Se muestran los eventos más recientes primero.
- La búsqueda y el filtro se aplican sobre los eventos ya cargados; usa
  **Cargar más** para traer páginas anteriores.
- El límite por defecto es 1000 eventos; al superarlo se descartan los más
  antiguos.
- La hora se muestra en la zona horaria del navegador.
- Los eventos sin NTP se muestran sin hora absoluta hasta que la placa
  sincroniza. La reconstrucción cubre el arranque actual.
- El CSV se descarga desde **Administración → Sistema**.

## Operación sin router

El router no está conectado al UPS. Si se corta la red eléctrica o el router:

- La placa maestra sigue alimentada por el UPS y continúa leyendo RFID e IR.
- La cerradura, los LEDs, el buzzer y la tira LED siguen funcionando.
- Los eventos se guardan en LittleFS con `uptimeMs`.
- La web, la cámara y NTP no están disponibles. Telegram no está implementado.
- Al volver la red y sincronizar NTP, los eventos de ese mismo arranque
  recuperan su hora absoluta.
- Si el equipo se apaga por completo antes de sincronizar, los eventos de ese
  arranque quedan sin hora absoluta.

## Solución de problemas

**No abre con mi tarjeta.**
Verifica que el UID esté dado de alta, activo y escrito en hexadecimal. Revisa
la alimentación y el cableado SPI del RC522.

**La web no carga.**
Comprueba que el router esté encendido y que la placa esté conectada. Si el
router se apagó, el armario sigue registrando; la web vuelve al restaurar la red.
Si el portal no aparece, reinicia la placa.

**Olvidé la contraseña de administrador.**
No hay recuperación desde la web. Vuelve a flashear la placa con
`SECRET_DEFAULT_ADMIN_PASSWORD` y restaura los datos, o borra la flash (esto
elimina usuarios, herramientas e histórico).

**La cámara aparece desconectada.**
El sistema sigue funcionando con los IR. Revisa la alimentación de la XIAO, la
red WiFi y que el nodo esté flasheado. Comprueba `http://tooltrack-cam.local/health`
o la IP que muestre su monitor serial.

**La cámara está en línea pero no identifica herramientas.**
Es el comportamiento esperado hasta integrar el modelo de Edge Impulse:
`/analyze` devuelve confianza 0 y las ranuras como desconocidas. La decisión
sigue siendo del IR.

**La cámara detecta mal una herramienta.**
La decisión final es del IR. Revisa la iluminación, la limpieza de la lente, la
ROI y la etiqueta del modelo. La discrepancia queda registrada.

**Necesito calibrar las zonas de la cámara.**
Captura `http://tooltrack-cam.local/capture.jpg`, ejecuta
`python3 tools/calibrate_rois.py foto.jpg --out rois.json` y sube el archivo con
`curl -X PUT --data-binary @rois.json http://tooltrack-cam.local/rois`. Luego
verifica con `/analyze` y una segunda foto.

**No puedo iniciar sesión.**
Tras cinco intentos fallidos hay un bloqueo de 60 segundos. La sesión de
administrador vence a la hora; vuelve a entrar.

**Se tomó una herramienta sin pasar tarjeta.**
Queda registrada como “Sin sesión” y se genera una alerta. Revisa el histórico y
las grabaciones si están disponibles.

**¿Cómo reinicio el WiFi?**
Desde **Administración → Sistema → Borrar WiFi**. La placa reinicia y abre el
portal `ToolTrack-Setup`. No hay un botón físico dedicado para borrar solo el
WiFi; el botón de reset de la placa reinicia, no borra credenciales.

## Mantenimiento

- Revisar la alineación de la cámara cada vez que se mueva el armario.
- Limpiar la lente y los sensores IR.
- Verificar la autonomía del UPS una vez al mes.
- Exportar y archivar el histórico antes de la expo y antes de borrarlo.
- Revisar los logs seriales si hay reinicios inesperados.
- Cambiar la contraseña inicial y las tarjetas de ejemplo antes de la demo.
- No actualizar el firmware por OTA: esta versión se actualiza por USB.
