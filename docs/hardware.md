# Hardware y pinout

> BOM completa, consumo, conexiones y decisión sobre MOSFETs:
> [`BOM_ToolTrack.xlsx`](BOM_ToolTrack.xlsx).

## Placa maestra — ESP32-S3-DevKitC-1 N16R8

| Función | GPIO | Notas |
|---|---|---|
| IR 1–12 | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 | Entradas digitales; 3.3 V |
| RFID SCK | 13 | SPI |
| RFID MISO | 14 | SPI |
| RFID MOSI | 15 | SPI |
| RFID SS | 16 | Chip select |
| RFID RST | 17 | Reset |
| Cerradura 12 V fail-safe | 18 | MOSFET logic-level; HIGH = cerrada |
| LED verde | 21 | A través de resistencia |
| LED rojo | 47 | A través de resistencia |
| Buzzer | 39 | Activo o pasivo |
| Lámpara 12 V | 40 | MOSFET logic-level; HIGH = encendida |
| Corte de red (opcional) | 41 | Optoacoplador, entrada digital |
| Libres | 42 | Reserva; comparte JTAG con 39–41 |
| No usar | 38, 48 | LED RGB onboard de la placa |

> La entrada GPIO41 está implementada en el firmware, pero desactivada con
> `FEATURE_AC_SENSE=false` hasta que la PCB entregue la señal. Se asume
> `HIGH = red presente` (`AC_SENSE_ACTIVE_HIGH=true`). No bloquea la cerradura:
> solo registra `power_lost` / `power_restored` y alerta en la web.

> **Cerradura fail-safe:** sin energía queda abierta. El GPIO18 debe mantener el
> MOSFET encendido para cerrar. Por eso lleva un pull-up de 10 kΩ a 3.3 V en la
> compuerta y el firmware debe usar `HIGH = cerrada`, `LOW = abierta`.

### Pines prohibidos

| GPIO | Motivo |
|---|---|
| 19, 20 | USB nativo (D−/D+) |
| 26–32 | Flash SPI |
| 33–37 | PSRAM octal del N16R8 |
| 43, 44 | UART0 / consola |
| 0, 45, 46 | Pines de arranque (strapping) |

> El N16R8 usa PSRAM **octal**, por eso GPIO 33–37 no están disponibles aunque
> la placa tenga 44 pines. GPIO3 es strapping: si hay problemas de arranque,
> mover IR3 a GPIO42.

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
   ├── Fuente 12 V / 2 A (3 A recomendable)
   │      └── Mini UPS 12 V / 2 A
   │             ├── Cerradura fail-safe 12 V / 250 mA (MOSFET)
   │             ├── Lámpara 12 V / 60 mA (MOSFET)
   │             ├── Buck 12→5 V / 3 A
   │             │      ├── ESP32-S3 DevKitC-1 (5 V)
   │             │      └── XIAO ESP32-S3 Sense (5 V)
   │             └── Buck 12→3.3 V / 2 A
   │                    ├── 12 sensores IR (3.3 V)
   │                    └── RC522 (3.3 V)
   └── Router WiFi (sin respaldo, según decisión actual)
```

### Consumo estimado

| Elemento | Corriente / potencia |
|---|---|
| ESP32-S3 + WiFi | 250 mA típico / 500 mA pico a 5 V |
| XIAO S3 + cámara | 300 mA típico / 500 mA pico a 5 V |
| 12 IR a 3.3 V | 276 mA (23 mA cada uno) |
| RC522 | 30 mA a 3.3 V |
| LEDs + buzzer | 50 mA a 3.3 V |
| Cerradura fail-safe | 250 mA continuos a 12 V (3 W) |
| Lámpara 12 V | 60 mA (0.72 W) |
| **Total 5 V** | **≈0.55 A típico / 1.0 A pico** |
| **Total 3.3 V externo** | **≈0.306 A** |
| **Total 12 V** | **≈0.66 A típico / 0.87 A pico (7.9–10.4 W)** |

> El UPS de 12 V / 2 A (24 W) queda al **33 % típico** y **43 % pico**; ya no
> está al límite. La cerradura fail-safe consume 3 W continuos, así que la
> autonomía depende de la batería: ≈2 h con 20 Wh y ≈8–9 h con 84 Wh.
> Si la batería se agota, el armario se abre. Verificar que la cerradura sea de
> servicio continuo.

## Componentes

| Componente | Modelo / especificación | Estado |
|---|---|---|
| Microcontrolador | ESP32-S3-DevKitC-1 N16R8 | Por comprar |
| Nodo de visión | Seeed XIAO ESP32-S3 Sense | Por comprar |
| Lector RFID | MFRC522, 13.56 MHz, 3.3 V | Existente |
| Sensores de presencia | 12 × IR digitales, 3.3 V, 23 mA | Existente / verificar |
| Cerradura | Fail-safe 12 V, 250 mA, servicio continuo | Verificar modelo |
| MOSFET de potencia | AO3400A o IRLZ44N, logic-level | Por comprar |
| Buck 5 V | 12→5 V, 3 A | Por comprar / verificar |
| Buck 3.3 V | 12→3.3 V, 2 A | Por comprar |
| Lámpara | LED blanca 12 V, 60 mA, difusa | Por definir |
| UPS | Mini UPS 12 V / 2 A | Por comprar |
| Buzzer | Activo 3.3 V, ≤30 mA | Existente / verificar |
| LEDs indicadores | Verde y rojo, 3.3 V | Existente |
| Fuente | 12 V DC, 2 A mínimo; 3 A recomendable | Por definir |

> **No usar IRF3205 directo desde GPIO18/GPIO40.** No es logic-level: su
> `Rds(on)` está especificado con `Vgs = 10 V` y con 3.3 V no se garantiza que
> conduzca bien. Usar AO3400A, IRLZ44N o IRLB8721.

## Requisitos para el diseño de PCB

1. Cerradura fail-safe y lámpara a 12 V con MOSFET logic-level y protecciones:
   diodo de rueda libre en la cerradura, pull-up de 10 kΩ en la compuerta de la
   cerradura y pull-down de 10 kΩ en la de la lámpara.
2. Buck 12→5 V de 3 A para las dos placas y buck 12→3.3 V de 2 A para los 12 IR
   y el RC522.
3. Conector para el mini UPS y señal de “red presente” hacia GPIO41 mediante
   optoacoplador (3.3 V, activa en alto). Es necesaria para habilitar
   `FEATURE_AC_SENSE`.
4. Borneras o conectores para los 12 IR y el RC522.
5. Reservar GPIO42 para futuras señales; GPIO38/48 son del RGB onboard.
6. No conectar nada a GPIO19/20/26–37/43/44.

## Advertencias

- El RC522 funciona a **3.3 V**; no alimentarlo con 5 V. Usar el buck externo,
  no el pin `3V3` del ESP32.
- Los 12 IR funcionan a **3.3 V**; su salida va directa al GPIO, sin divisor.
  Añadir 100 nF por sensor.
- La lámpara de 12 V debe ser difusa para evitar reflejos en la cámara.
- La cerradura genera picos inductivos: usar diodo de rueda libre y MOSFET
  logic-level. No hace falta relé.
- La cerradura fail-safe abre el armario si se corta la energía o se agota el
  UPS; es una decisión de seguridad aceptada.
- El firmware usa lógica fail-safe: `HIGH = cerrada`, `LOW = abierta`. La
  cerradura queda cerrada al arrancar y se abre solo durante `LOCK_PULSE_MS`.
- Verificar el sentido lógico de los IR (el código asume LOW = presente).
- No conectar USB y el buck de 5 V al mismo tiempo.
