# Hardware y pinout

## Placa maestra — ESP32-S3-DevKitC-1 N16R8

| Función | GPIO | Notas |
|---|---|---|
| IR 1–12 | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 | Entradas digitales |
| RFID SCK | 13 | SPI |
| RFID MISO | 14 | SPI |
| RFID MOSI | 15 | SPI |
| RFID SS | 16 | Chip select |
| RFID RST | 17 | Reset |
| Cerradura 12 V | 18 | MOSFET / relé |
| LED verde | 21 | A través de resistencia |
| LED rojo | 47 | A través de resistencia |
| Buzzer | 39 | Activo o pasivo |
| Tira LED 12 V | 40 | MOSFET logic-level |
| Corte de red (opcional) | 41 | Optoacoplador, entrada digital |
| Libres | 38, 42, 48 | Reserva (38/48 pueden ser RGB onboard) |

> La entrada GPIO41 está implementada en el firmware, pero desactivada con
> `FEATURE_AC_SENSE=false` hasta que la PCB entregue la señal. Se asume
> `HIGH = red presente` (`AC_SENSE_ACTIVE_HIGH=true`). No bloquea la cerradura:
> solo registra `power_lost` / `power_restored` y alerta en la web.

### Pines prohibidos

| GPIO | Motivo |
|---|---|
| 19, 20 | USB nativo (D−/D+) |
| 26–32 | Flash SPI |
| 33–37 | PSRAM octal del N16R8 |
| 43, 44 | UART0 / consola |
| 0, 45, 46 | Pines de arranque (strapping) |

> El N16R8 usa PSRAM **octal**, por eso GPIO 33–37 no están disponibles aunque
> la placa tenga 44 pines.

## Nodo de visión — XIAO ESP32-S3 Sense

La cámara usa pines internos de la placa; no se conecta por headers:

| Señal | GPIO |
|---|---|
| XCLK | 10 |
| SIOD (SDA) | 40 |
| SIOC (SCL) | 39 |
| D0–D7 | 15, 17, 18, 16, 14, 12, 11, 48 |
| VSYNC | 38 |
| HREF | 47 |
| PCLK | 13 |
| PWDN / RESET | — |

Los pines D0–D10 del header quedan libres para futuras señales.

## Árbol de energía

```
Red eléctrica
   │
   ├── Fuente 12 V (con respaldo mini UPS 12 V / 2 A)
   │      ├── Cerradura 12 V (MOSFET)
   │      ├── Tira LED 12 V (MOSFET)
   │      └── Buck 12→5 V
   │             ├── ESP32-S3 DevKitC-1 (5 V)
   │             └── XIAO ESP32-S3 Sense (5 V)
   └── Router WiFi (sin respaldo, según decisión actual)
```

### Consumo estimado

| Elemento | Corriente |
|---|---|
| ESP32-S3 + WiFi | 120–250 mA |
| XIAO S3 + cámara | 200–350 mA |
| Tira LED 12 V (≤5 W) | ≤420 mA |
| Cerradura 12 V (pulso) | 500–1000 mA |
| **Total pico** | **≈1.2–2.0 A** |

> El mini UPS de 12 V / 2 A queda al límite durante el pulso de la cerradura.
> Recomendación para el diseño de PCB: usar una tira LED de bajo consumo,
> añadir un capacitor de reserva en la línea de 12 V y, si se implementa el
> corte de red, evaluar bloquear la cerradura con red ausente. El firmware
> actual **no** bloquea la cerradura; solo registra el evento.

## Componentes

| Componente | Modelo / especificación | Estado |
|---|---|---|
| Microcontrolador | ESP32-S3-DevKitC-1 N16R8 | Por comprar |
| Nodo de visión | Seeed XIAO ESP32-S3 Sense | Por comprar |
| Lector RFID | MFRC522, 13.56 MHz, 3.3 V | Existente |
| Sensores de presencia | 12 × IR digitales, 3.3–5 V | Existente |
| Cerradura | Electromagnética 12 V | Ajustar a 12 V |
| MOSFET de potencia | IRF3205 (o logic-level equivalente) | Existente |
| Regulador | Buck 12→5 V (LM2596 o similar) | Existente |
| Tira LED | Blanca 12 V, bajo consumo, difusa | Por definir |
| UPS | Mini UPS router 12 V / 2 A | Por comprar |
| Buzzer | Activo 3.3/5 V | Existente |
| LEDs indicadores | Verde y rojo | Existente |
| Fuente | 12 V DC | Por definir |

## Requisitos para el diseño de PCB

1. Cerradura y tira LED a 12 V con MOSFET y protecciones (diodo de rueda libre
   en la cerradura).
2. Buck 12→5 V con capacidad ≥2 A para las dos placas.
3. Conector para el mini UPS y señal de “red presente” hacia GPIO 41 mediante
   optoacoplador (3.3 V, activa en alto). Es necesaria para habilitar
   `FEATURE_AC_SENSE`.
4. Borneras o conectores para los 12 IR y el RC522.
5. Reservar GPIO 38/42/48 para futuras señales.
6. No conectar nada a GPIO 19/20/26–37/43/44.

## Advertencias

- El RC522 funciona a **3.3 V**; no alimentarlo con 5 V.
- Los IR pueden alimentarse a 5 V, pero su salida debe ser compatible con
  3.3 V. Verificar que la salida no supere 3.3 V o usar un divisor.
- La tira LED debe ser difusa para evitar reflejos en la cámara.
- La cerradura genera picos inductivos: usar diodo y, si es posible,
  optoacoplador o relé.
- Verificar el sentido lógico de los IR (el código asume LOW = presente).
