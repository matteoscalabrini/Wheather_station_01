# T3 V1.6.1 Weather Station

This project is intentionally narrow:
- one board target
- one pin-map header
- one firmware entry point
- 9× SH1107 128×128 OLEDs across 5 software I2C buses (displays only)
- 1× BME280 and 2× INA219 on a dedicated hardware I2C bus (sensors only)
- RS485 Modbus RTU wind sensors

## I2C Architecture

The firmware drives **6 fixed I2C buses** — buses 0–4 are software I2C buses for displays only, and bus 5 is a dedicated hardware `Wire` bus for the BME280 and both INA219s. The firmware no longer rebinds `Wire` across multiple pin pairs at runtime, and `Wire1` is intentionally left unused.

### Display Buses

| Bus | SDA | SCL | Devices |
| --- | --- | --- | --- |
| 0 (SW) | `32` | `33` | Display 0 (0x3D), Display 1 (0x3C) |
| 1 (SW) | `25` | `14` | Display 2 (0x3D), Display 3 (0x3C) |
| 2 (SW) | `21` | `22` | Display 4 (0x3D), Display 5 (0x3C) |
| 3 (SW) | `5` | `18` | Display 6 (0x3D), Display 7 (0x3C) |
| 4 (SW) | `19` | `23` | Display 8 (0x3C) |

### Sensor Bus

| Bus | SDA | SCL | Devices |
| --- | --- | --- | --- |
| 5 (Wire) | `26` | `27` | BME280 (0x76/0x77), INA219 #1 (0x44), INA219 #2 (0x40) |

Sensors are electrically isolated from the display buses, and every display cable has its own fixed SDA/SCL pair.

Each bus with two displays uses the standard `0x3C`/`0x3D` address pair. Single-display buses use `0x3C`.

## RS485 Wiring

- `RX = GPIO16`
- `TX = GPIO17`
- `DE/RE = GPIO4` (DE control disabled by default)
- Modbus RTU `9600 8N1`
- Auto-detects UART polarity at startup

## Runtime Architecture

The firmware runs on top of Arduino's FreeRTOS support with four pinned tasks:

- `display-task` on core 0 sleeps until telemetry changes, then redraws only the affected OLEDs; a long heartbeat redraw keeps the panels fresh without a constant sweep
- `sensor-task` on core 1 samples the BME280 and both INA219s
- `comms-task` on core 1 handles RS485 polling and serial commands
- `i2c-maint-task` on core 1 retries offline sensors, and probes display presence on a long cadence instead of continuously scanning healthy buses

Sensor values are shared through a mutex-protected telemetry snapshot so display refresh, reconnect logic, and sensor polling do not trample each other.

## Power Saver

Battery-saver mode is enabled by default in `include/board_config.h` via `kEnableBatterySaver`.

Current battery-saver timings:
- CPU clock: `80 MHz`
- display redraw heartbeat: `60000 ms`
- healthy display presence probe: `300000 ms`
- offline display retry: `60000 ms`
- sensor sampling: `15000 ms`
- wind polling: `3000 ms`
- serial command polling: `250 ms`
- sensor/display maintenance pass: `60000 ms`

Display behavior in this profile:
- OLEDs redraw only when their value changed by a meaningful threshold
- OLEDs stay on continuously, but run at a lower contrast and with reduced on-screen chrome to cut panel current
- each refresh wakes only the affected displays instead of sweeping all 9 panels

Thresholds are configurable in `include/board_config.h` for temperature, humidity, pressure, wind, power, voltage, and battery percentage.

## Display Layout — 9 Screens

All displays are `SH1107 128×128` OLEDs. Each screen now shows one primary value, or at most two compact values.

| # | Label | Content |
| --- | --- | --- |
| 0 | ENV TEMP | Temperature + Heat Index |
| 1 | ENV HUM | Humidity |
| 2 | ENV PRES | Pressure |
| 3 | FORECAST | 3h Outlook + Pressure Trend |
| 4 | WIND SPD | Wind Speed + Beaufort |
| 5 | WIND DIR | Relative Angle + Relative Sector |
| 6 | SOLAR | INA219 #1 Solar Power + Voltage |
| 7 | BATTERY | INA219 #2 Battery Power + Voltage |
| 8 | BAT LVL | Battery Percentage + Voltage |

Data split:
- Displays `0–3`: environment (BME280)
- Displays `4–5`: wind (RS485 Modbus)
- Displays `6–8`: solar and battery power

## Sensor Support

### BME280

On dedicated hardware sensor bus 5. Addresses probed: `0x76` (primary), `0x77` (secondary).
Sampling: forced mode, ×1 oversampling, filter off. The sensor sleeps between reads and wakes only for each sample.
Derived values: dew point, heat index, and a fixed-altitude 3-hour pressure-trend forecast.
The forecast screen replaces the old dew-point screen and becomes meaningful after roughly 3 hours of pressure history.
If the BME280 disappears and comes back, the maintenance task reprobes the bus and re-runs driver initialization automatically.

### INA219 Power Monitors

Both on dedicated hardware sensor bus 5 (no display traffic):
- **#1** at `0x44` — solar power monitor
- **#2** at `0x40` — battery power monitor
- Reads: bus voltage, shunt voltage, current, power
- Each INA219 is put into power-save mode between samples
- Battery percentage is derived from INA219 #2 load voltage with a configurable linear mapping in `include/board_config.h`
- If an INA219 drops off the bus, it is marked offline and retried automatically by the maintenance task

### RS485 Wind Sensors

- Wind speed: register `0x0000` = speed ×10, `0x0001` = Beaufort
- Wind direction: register `0x0000` = angle ×10, register `0x0001` = sensor 8-point code
- The firmware display ignores the sensor's cardinal text and derives `FRONT/RIGHT/REAR` sectors from the angle
- `include/board_config.h` exposes `kWindRelativeFrontOffsetDeg` so you can align `0 deg` with the vehicle front
- Auto-probes UART polarity at startup
- `rs485 sweep` scans all baud/format/polarity/address combinations

## Serial Commands

| Command | Description |
| --- | --- |
| `ping` | Board alive check with basic readings |
| `scan` | Scans all 5 display software buses and the hardware sensor bus |
| `status` | Full weather station status dump |
| `rs485` | Show active RS485 config |
| `rs485 normal` | Force non-inverted polarity |
| `rs485 invert` | Force inverted polarity |
| `rs485 auto` | Auto-detect polarity |
| `rs485 sweep` | Full scan of all RS485 parameters |
| `modbus <addr>` | Raw Modbus FC03 probe (diagnostic) |
| `help` | Show available commands |

## Software Structure

| File | Purpose |
| --- | --- |
| `platformio.ini` | PlatformIO environment, library dependencies |
| `include/board_config.h` | Board pin map and all `constexpr` defaults |
| `include/app_types.h` | Shared firmware structs, enums, and draw callback type |
| `include/app_state.h` | Shared constants and global state declarations |
| `include/*.inl` | Subsystem implementations included by `src/main.cpp` |
| `src/main.cpp` | Firmware entry point, global object definitions, setup/loop |

Libraries:
- `U8g2` — SH1107 128×128 OLED driver
- `Adafruit BME280 Library` — BME280 sensor driver
- `Adafruit INA219 Library` — INA219 power monitor driver
- `Adafruit Unified Sensor` — dependency of Adafruit BME280

## Build and Flash

```bash
~/.platformio/penv/bin/pio run              # build
~/.platformio/penv/bin/pio run -t upload    # flash
~/.platformio/penv/bin/pio device monitor -b 115200  # serial
```

## Known Constraints

- Display buses 0–4 are software I2C, so display refresh is slower than a pure hardware-I2C design.
- Display disconnect detection is intentionally slow in the current battery-saver profile. Healthy panels are reprobed every `300000 ms`, while offline panels are retried every `60000 ms`.
- Sensor reconnect is periodic, not instantaneous. In the current battery-saver profile, offline sensors are retried every `60000 ms`.
- Battery percentage is only as accurate as the configured empty/full voltage thresholds in `include/board_config.h`.
- `GPIO16`/`GPIO17` are used for RS485 UART only — no longer shared with I2C bus 3 (bus 3 now uses GPIO 5/18).
- Some third-party 128×128 OLED modules marked as 3.3 V behave more reliably from 5 V.
