# Manual de operación

> Estado: borrador de la Fase 0. Se completará cuando la Fase 1 esté
> implementada.

## Roles

| Rol | Acceso |
|---|---|
| Usuario | Tarjeta RFID para abrir el armario y tomar/devuelve herramientas |
| Administrador | Panel web con contraseña: usuarios, herramientas y ajustes |

No hay usuarios con permisos intermedios en la versión actual.

## Flujo del usuario

1. Acercar la tarjeta al lector.
2. **Acceso autorizado:**
   - La cerradura se abre durante 3 segundos.
   - LED verde encendido y un beep corto.
   - La tira LED se enciende.
   - El usuario queda como sesión activa.
3. Tomar o devolver herramientas.
4. La web muestra el cambio en tiempo real.
5. La cámara verifica la operación cuando los sensores se estabilizan.
6. Al pasar otra tarjeta, la sesión cambia al nuevo usuario.

### Acceso denegado

- LED rojo y buzzer intermitentes durante 3 segundos.
- Se registra el intento con el UID leído.

## Flujo del administrador

1. Abrir `http://tooltrack.local`.
2. Pestaña **Administración** → iniciar sesión.
3. Cambiar la contraseña inicial `tooltrack` en el primer ingreso.
4. Gestionar:
   - **Usuarios:** alta, baja y edición de tarjetas RFID.
   - **Herramientas:** nombre y clase esperada de cada ranura.
   - **Ajustes:** Telegram, límite de histórico, reinicio y reset de WiFi.
   - **Histórico:** filtrar y exportar a CSV.

## Indicadores

| Indicador | Significado |
|---|---|
| LED verde fijo | Acceso autorizado / cerradura abierta |
| LED rojo fijo | Sistema bloqueado |
| LED rojo intermitente | Acceso denegado o alerta |
| Beep corto | Acceso correcto |
| Beep intermitente | Error o alerta |
| Tira LED encendida | Armario en uso o cámara capturando |

## Preguntas frecuentes

**No abre con mi tarjeta.**
Verificar que el UID esté dado de alta y activo en el panel de administración.

**La web no carga.**
Verificar que el router esté encendido y que el armario esté conectado. Si el
router se apagó, el armario sigue registrando por IR y reconstruirá la hora al
volver la red.

**La cámara aparece desconectada.**
El sistema sigue funcionando con los IR. Revisar la alimentación de la XIAO y
la señal WiFi.

**Se tomó una herramienta sin pasar tarjeta.**
Queda registrada como “Sin sesión” y se genera una alerta.

**¿Cómo reinicio el WiFi?**
Desde Administración → Ajustes → Reset WiFi, o manteniendo el botón de reset
de la placa y volviendo a configurar el portal `ToolTrack-Setup`.

## Mantenimiento

- Revisar la alineación de la cámara cada vez que se mueva el armario.
- Limpiar la lente y los sensores IR.
- Verificar la autonomía del UPS una vez al mes.
- Exportar y archivar el histórico antes de la expo.
- Revisar los logs seriales si hay reinicios inesperados.
