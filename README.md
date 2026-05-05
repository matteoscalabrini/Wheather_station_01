# T3 V1.6.2 Weather Station

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

The firmware runs on top of Arduino's FreeRTOS support with pinned tasks:

- `display-task` on core 0 sleeps until telemetry changes, then redraws only the affected OLEDs; a long heartbeat forces a full redraw to keep unchanged panels visible
- `sensor-task` on core 1 samples the BME280 and both INA219s
- `comms-task` on core 1 handles RS485 polling and serial commands
- `i2c-maint-task` on core 1 retries offline sensors, and probes display presence on a long cadence instead of continuously scanning healthy buses
- `network-task` on core 1 applies the solar-aware WiFi and posting policy

Sensor values are shared through a mutex-protected telemetry snapshot so display refresh, reconnect logic, and sensor polling do not trample each other.

## Power Saver

Battery-saver mode is enabled by default in `include/board_config.h` via `kEnableBatterySaver`.

The battery low-voltage lockout is enabled by default via `kEnableBatteryLowVoltageLockout`. INA219 #2 battery load voltage is checked before the normal boot path initializes displays, RS485, WiFi, or non-battery sensors, and is checked again during regular sensor sampling:

- runtime cutoff: `<= 11.50 V` enters battery lockout deep sleep
- boot/timer wake guard: if battery voltage is below the configured resume threshold, the station immediately returns to deep sleep
- wake interval while locked out: `1 h`
- when the pack reaches `>= 12.80 V`, the lockout latch clears and normal solar-aware behavior resumes
- if INA219 #2 is unavailable during an already-latched battery lockout, the station stays asleep and retries on the next hourly wake
- if INA219 #2 is unavailable on a non-latched boot, battery lockout cannot be evaluated and the station continues normal boot

The solar-voltage power policy is also enabled by default via `kEnableSolarPowerPolicy`. INA219 #1 solar load voltage and power are used as the light/charge proxy:

- `>= 16.0 V` and `> 3.0 W`: sun mode
- `> 10.0 V` and `< 16.0 V`: shadow mode
- `>= 16.0 V` with `<= 3.0 W`: shadow mode
- `<= 10.0 V`: dark mode

The mode uses voltage hysteresis before leaving sun or dark mode, so voltage noise near the thresholds does not rapidly flip the schedule.

CPU frequency follows solar mode and radio state:

| Mode | Radio Off | Radio On |
| --- | ---: | ---: |
| Sun | `160 MHz` | `160 MHz` |
| Shadow | `80 MHz` | `80 MHz` |
| Dark | `80 MHz` | `80 MHz` |
| Unknown | `80 MHz` | `80 MHz` |

Shadow-mode battery-saver timings:
- CPU clock: `80 MHz` between radio bursts and while WiFi/AP is active
- display contrast: `24`
- display redraw heartbeat: `60000 ms`
- healthy display presence probe: `300000 ms`
- offline display retry: `60000 ms`
- sensor sampling: `15000 ms`
- wind polling: `3000 ms`
- serial command polling: `250 ms`
- sensor/display maintenance pass: `60000 ms`

Sun-mode timing changes:
- CPU clock: `160 MHz`
- display contrast: `255`
- display redraw heartbeat: `15000 ms`
- sensor sampling: `5000 ms`
- wind polling: `1000 ms`
- sensor/display maintenance pass: `30000 ms`

Dark-mode behavior:
- CPU clock: `80 MHz`
- display contrast is reduced to `16` during the initial dark grace period
- sensor sampling, wind polling, display heartbeat, and maintenance slow to `60000 ms`
- after `2 h` continuously in dark mode, all OLEDs are placed in power-save mode and the ESP32 enters deep sleep
- deep sleep wake interval is `10 min`
- on timer wake, the firmware reads INA219 #1 before initializing displays, retrying transient invalid solar samples up to 3 times; if solar voltage is still dark, it immediately sleeps again unless a 30-minute server post is due
- if solar voltage wakes into shadow or sun, normal display and polling behavior resumes
- if INA219 #1 is offline or invalid, the firmware does not enter solar-triggered deep sleep

Display behavior in this profile:
- OLEDs redraw when their value changed by a meaningful threshold, plus a forced heartbeat redraw that keeps quiet panels visible
- OLED brightness follows the solar mode: maximum in sun, very low in shadow, lower during the dark grace period, then off in deep sleep
- the OLED layout intentionally minimizes white pixels: no filled backgrounds, frames, bars, or decorative separators in the panel renderer
- telemetry changes wake only the affected displays; the heartbeat intentionally sweeps all online displays

Thresholds are configurable in `include/board_config.h` for solar mode, sleep timing, temperature, humidity, pressure, wind, power, voltage, and battery percentage. Battery percentage uses a 4S Li-ion voltage curve with adjustable 0%/100% bounds in the admin UI.
Battery lockout enter/resume voltages and wake interval also have compile-time defaults in `include/board_config.h` and are adjustable in the admin UI.

## WiFi, Web UI, And Posting

The firmware includes a solar-aware WiFi stack with SPIFFS-hosted web UI and web OTA.

Solar mode network policy:
- In normal configured use, WiFi and the setup AP stay off between scheduled network bursts in every solar mode, including sun mode.
- A network burst starts only for a configured server post, a manual local **Post Now**, or local setup/admin use while the setup AP is intentionally active.
- During a post burst, the station sends telemetry, pulls remote config if due, checks firmware/SPIFFS if due, then turns STA/AP radios off again unless **Debug AP always** is enabled.
- A failed scheduled post is retried after `min(interval / 4, 60000 ms)`; successful posts wait for the full mode interval.
- **Sun mode:** posts use the sun interval, `10 min` by default.
- **Shadow mode:** posts use the shadow interval, `10 min` by default.
- **WiFi recovery (non-dark):** if a scheduled post cannot connect to the saved station WiFi, the setup AP is exposed for `10 min` so WiFi settings can be repaired locally. The recovery AP is limited to `3` consecutive recovery windows, then re-arms after `10 min` or immediately after a successful station connection; it is never used in dark mode.
- **Dark mode before deep sleep:** posts use the dark interval, `30 min` by default.
- **Dark timer wake from deep sleep:** the station reads solar first; if still dark, it posts only on the configured dark cadence, takes a one-shot RS485 wind sample, performs any due remote management in the same WiFi session, then returns to deep sleep. If that dark-wake post fails, the wake counter is rolled back so the next timer wake retries the missed slot.

Debug AP:
- `BoardConfig::kWifiDebugForceApAlways` is the default for the runtime **Debug AP always** setting.
- The default is `false` for power savings.
- When enabled, the setup AP stays available in every solar mode while the ESP32 is awake. Deep sleep still powers WiFi down.
- When disabled, the setup AP is off between network bursts once station WiFi credentials are configured.
- If no station WiFi SSID is configured, the setup AP stays on while awake so the station can still be provisioned.

WiFi settings:
- Leaving the station WiFi password field blank keeps the saved password.
- To use an open upstream WiFi network, check **Open network** in the WiFi settings; scan results marked `OPEN` select this automatically and clear any saved password.
- **Debug AP always** can be toggled locally or from the remote website config.
- **Clear WiFi Cache** clears the saved upstream SSID/password and asks the ESP32 WiFi driver to erase cached STA credentials.
- The dashboard status prefers `STA` when the ESP32 is connected to an upstream WiFi network, even if the setup AP is also active.

Remote website management:
- The station derives the website origin from `postUrl` and authenticates device calls with `postToken`.
- Remote config is fetched from `/api/device/config` only during an already scheduled post WiFi session; the `600000 ms` default is a minimum cadence and does not start an extra WiFi connection by itself.
- Firmware/SPIFFS update checks are fetched from `/api/device/firmware?version=<firmware>&spiffs=<spiffs>` only during an already scheduled post WiFi session; the `3600000 ms` default is a minimum cadence and does not start an extra WiFi connection by itself.
- Remote config can update solar thresholds, sleep/post intervals, battery percentage bounds, battery lockout thresholds/wake interval, `serverPostEnabled`, **Debug AP always**, and the remote pull/check intervals.
- Changing `serverPostDarkMs` or `solarDeepSleepWakeMs` resets the dark-wake counter so the next dark post cadence starts cleanly.
- Remote config does not accept WiFi credentials, post tokens, or admin passwords.
- If the website firmware manifest is enabled and has a version different from `BoardConfig::kFirmwareVersion`, the station downloads the binary URL, adds the bearer token for URLs on the configured website origin, checks size/SHA-256 when provided, installs with OTA, and reboots.
- If the website SPIFFS manifest is enabled and has a version different from the stored SPIFFS version, the station downloads the SPIFFS image, adds the bearer token for URLs on the configured website origin, checks size/SHA-256 when provided, installs it with OTA, stores the new SPIFFS version, and reboots.
- Posted telemetry includes the current firmware version, SPIFFS version, and remote-configurable settings so the website admin page can show current station values and update status.
- Dark timer wake remains burst-only: if the station wakes from deep sleep in dark mode, it posts when due, runs any due remote management in that same WiFi session, and returns to sleep.

Default AP:
- SSID: `WeatherStation-AP`
- Open network, no password
- Captive DNS redirects connected clients to the local dashboard where supported by the phone/computer OS
- The AP is normally available only while no upstream station WiFi SSID is configured, or when **Debug AP always** is enabled.

Default admin password: `admin`.

Web routes:
- `/` — SPIFFS OLED-style dashboard with the 9 display groups and a top-right admin menu
- `/api/status` — public JSON telemetry
- `/api/admin/config` — password-protected settings GET/POST
- `/api/admin/wifi-scan` — password-protected WiFi scan
- `/api/admin/wifi-clear-cache` — password-protected clear of saved station WiFi SSID/password plus ESP32 STA cache reset
- `/api/admin/post-now` — password-protected manual telemetry POST
- `/api/ota/upload` — password-protected firmware OTA
- `/api/ota/upload-spiffs` — password-protected SPIFFS OTA

Settings saved to the ESP32 NVS partition include solar thresholds, battery percentage thresholds, battery lockout thresholds/wake interval, sleep/post intervals, remote management intervals, Debug AP always, WiFi credentials, server POST URL/token, SPIFFS version, and admin password. Normal firmware OTA, SPIFFS OTA, and `pio run -t upload` do not erase NVS. A full chip erase, `pio run -t erase`, or **Clear WiFi Cache** removes saved WiFi credentials. Compile-time defaults remain in `include/board_config.h` and are used when flash settings are missing or invalid, while the invalid-settings repair path preserves WiFi credentials, post URL/token, SPIFFS version, and admin password.

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
- Battery percentage is derived from INA219 #2 load voltage with a 4S Li-ion voltage curve. Defaults are `12.00 V` for 0% and `16.80 V` for 100%; runtime bounds can be adjusted in the admin UI.
- Battery lockout also uses INA219 #2 load voltage. A low-voltage runtime cutoff powers the OLEDs down and puts the ESP32 into deep sleep; boot and timer wakes stay asleep until the configured resume voltage is reached.
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
| `data/` | SPIFFS web UI assets |
| `partitions.csv` | OTA + SPIFFS partition table |
| `src/main.cpp` | Firmware entry point, global object definitions, setup/loop |

Libraries:
- `U8g2` — SH1107 128×128 OLED driver
- `Adafruit BME280 Library` — BME280 sensor driver
- `Adafruit INA219 Library` — INA219 power monitor driver
- `Adafruit Unified Sensor` — dependency of Adafruit BME280
- ESP32 Arduino core services — WiFi, WebServer, HTTPClient, Preferences, SPIFFS, and Update

## Build and Flash

```bash
~/.platformio/penv/bin/pio run              # build
~/.platformio/penv/bin/pio run -t upload    # flash
~/.platformio/penv/bin/pio run -t uploadfs  # flash SPIFFS web UI
~/.platformio/penv/bin/pio device monitor -b 115200  # serial
```

## Known Constraints

- Display buses 0–4 are software I2C, so display refresh is slower than a pure hardware-I2C design.
- Display disconnect detection is intentionally slow in the current battery-saver profile. Healthy panels are reprobed every `300000 ms`, while offline panels are retried every `60000 ms`.
- Sensor reconnect is periodic, not instantaneous. In the current battery-saver profile, offline sensors are retried every `60000 ms`.
- Battery percentage is an estimate from pack voltage, so it will move with load and recovery. It is only as accurate as the configured 0%/100% voltage bounds.
- `GPIO16`/`GPIO17` are used for RS485 UART only — no longer shared with I2C bus 3 (bus 3 now uses GPIO 5/18).
- Some third-party 128×128 OLED modules marked as 3.3 V behave more reliably from 5 V.
- The OTA partition table gives each firmware slot `0x140000` bytes; current firmware fits, but feature growth should watch flash usage.
