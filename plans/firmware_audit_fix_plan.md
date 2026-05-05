# Firmware Audit — Fix Plan

Tracking document for the conflicts and bugs found in the May 2026 firmware audit
of power profiles, server posts, and wakeup paths. Items are grouped by severity
and ordered for landing impact-first / footprint-low changes early.

Status, 2026-05-04: firmware and README implementation items are landed.
Bench/field verification items remain open where physical hardware or server
logs are required.

Status, 2026-05-05: 40 MHz shadow CPU experiments are considered closed-out
as unstable on this hardware mix (sensor/display stalls). Shadow-mode CPU floor
remains 80 MHz; optimization focus shifted to radio duty cycle, polling cadence,
and display power.

Each item lists:
- **Problem** — what's wrong and where (clickable file:line links).
- **Why it matters** — observable symptom in the field.
- **Approach** — proposed fix (subject to revision before code is written).
- **Implementation checklist** — concrete steps.
- **Verification checklist** — how we know it works.

---

## Phase 0 — Pre-flight

- [ ] Confirm bench rig: ESP32 dev board, INA219 #1 + #2 attached, BME280 attached, station WiFi reachable, an `archipelagoweatherstation` deploy reachable on the post URL.
- [ ] Snapshot current `gSettings` from `/api/admin/config` so settings can be restored after `pio run -t erase` cycles during testing.
- [ ] Capture a baseline serial log of a fresh boot in shadow mode and a fresh boot from a dark timer wake (cover INA219 #1 with tape) so regressions are easy to spot.
- [x] Verify the build is green after implementation: `~/.platformio/penv/bin/pio run`.

---

## Phase 1 — Critical bugs (highest field impact)

### 1. Transient INA219 #1 read on dark timer-wake collapses to full power-on boot

- **Problem.** [include/power_policy.inl:215-222](../include/power_policy.inl#L215-L222) clears `gBootedFromTimerWake` whenever the first solar sample is `Unknown`. A single flaky read at 3 AM bypasses the dark-wake-post-only path, brings up all 9 displays + RS485 + every task, and the station only re-enters deep sleep after `solarDarkDeepSleepDelayMs` (2 h).
- **Why it matters.** Battery-draining wake storms triggered by a sensor glitch — exactly the failure mode that's hardest to debug remotely.
- **Approach.** When booted from a timer wake, retry the solar read in a tight loop (≤ 3 attempts, ≤ 50 ms each) before allowing `updateSolarPowerPolicy` to consume the timer-wake context. Only fall through to the "Unknown → continue normal boot" path if the retries also fail.

**Implementation**
- [x] In [src/main.cpp](../src/main.cpp) `setup()`, before line 160's `updateSolarPowerPolicy(...)` call, add a retry helper that re-reads INA219 #1 up to N=3 times (with `taskDelayMs(20)` between attempts) when `gBootedFromTimerWake == true` and the first sample is invalid.
- [x] Keep the existing single-shot path for the non-timer-wake boot case.
- [x] Move the helper into [include/sensors.inl](../include/sensors.inl) (e.g. `readPowerWithRetries(Adafruit_INA219&, uint8_t attempts, uint32_t gapMs)`) so the same logic can be reused by the maintenance task if we ever need it.
- [x] Leave the `Unknown` early-return in `updateSolarPowerPolicy` unchanged — this is the last-resort fallback, but the retry should make it much rarer at boot.

**Verification**
- [ ] Bench: cover INA219 #1 with tape (or yank its power) and confirm the dark-wake path still triggers correctly when the sensor is genuinely absent (full boot is acceptable).
- [ ] Bench: simulate a transient by detaching INA219 #1 only between deep sleep and the boot window — confirm the retry recovers and `gDarkWakePostOnly` is still set when post-due.
- [ ] Field log: count the number of "Solar light mode: dark" log lines per 24 h before and after the fix, expect a drop with no associated post drops.

---

### 2. Dark-wake posts ship NaN wind values, may trigger false alerts on the server

- **Problem.** [src/main.cpp:177-186](../src/main.cpp#L177-L186) skips RS485 init and the wind poll whenever `gDarkWakePostOnly == true`. The dark-wake post still calls `buildTelemetryJson()`, which serializes `wind.speedMs == NaN` as `null` and `windSpeedOnline == false`.
- **Why it matters.** Server-side alert rules in `archipelagoweatherstation/src/lib/notifications.ts` may interpret extended `windSpeedOnline=false` as a sensor-down condition and fire spurious notifications on dark cycles.
- **Approach.** Either (a) actually do a one-shot RS485 poll inside the dark-wake post path, or (b) tag the payload with a `darkWakeSnapshot: true` flag the server can use to suppress sensor-down alerts. Recommended: do (a) since the cost is one ~3 s burst per 30 min in dark mode and the data is otherwise gone.

**Implementation**
- [x] In `setup()`, drop the `if (!gDarkWakePostOnly)` guard around the RS485 init + wind poll block at lines 178-186, OR move the block into a small helper called from both branches.
- [ ] Validate that `initRs485()` followed by a single `pollWindSensors()` doesn't add more than ~3 s of wall-clock to the dark-wake path (RS485 baud 9600, two registers).
- [x] Decide whether to keep RS485 initialized for the network task's lifetime or tear it down after the poll (it'll deep-sleep regardless within seconds, so leaving it up is fine).

**Verification**
- [ ] Inspect the JSON posted on a dark wake (server logs) — `wind.speedMs` and `windSpeedOnline` should be live values.
- [ ] Confirm no notification regressions in `notifications.ts` rule output for back-to-back dark posts.

---

### 3. `/api/admin/post-now` re-entrancy through `handleWebClientIfStarted()`

- **Problem.** [include/network_runtime.inl:625-630](../include/network_runtime.inl#L625-L630) calls `handleWebClientIfStarted()` while spinning waiting for STA. If the user is on the AP and POSTs to `/api/admin/post-now` during that window, the request handler at [include/network_runtime.inl:1445-1449](../include/network_runtime.inl#L1445-L1449) re-enters `postTelemetryNow()` recursively. `gNetworkRuntime.posting` gets stomped, two `HTTPClient` instances exist, and `WiFi.begin()` is called again on top of an in-flight connection.
- **Why it matters.** Hard to reproduce, easy to trip with a curious admin tapping the button. Side effects range from a confusing failure message to a half-stuck WiFi state.
- **Approach.** Guard the entry of `postTelemetryNow()` with a check on `gNetworkRuntime.posting`. Return a synthetic "post_already_running" message to the admin without touching state.

**Implementation**
- [x] In [include/network_runtime.inl](../include/network_runtime.inl) `postTelemetryNow()`, immediately after the `serverPostConfigured()` check, return early if `gNetworkRuntime.posting == true`. Set a synthetic `post_already_running` message so the admin UI sees what happened.
- [x] Confirm no other caller besides `applyNetworkPolicy` and the `/api/admin/post-now` handler invokes this — verified during the audit, but re-grep.
- [x] Consider also guarding `pullRemoteConfigNow()` and `checkRemoteFirmwareNow()` similarly, since they're invoked from `syncRemoteManagementIfDue()` and could in principle be triggered manually in a future feature.

**Verification**
- [ ] Bench: in shadow mode with an AP up, click "Post Now" rapidly twice; confirm only one upstream POST hits the server.
- [ ] Bench: while a real post is in flight (block the upstream WiFi for 15 s), hit `/api/admin/post-now`; confirm the synthetic "already running" response.

---

### 4. `scheduleFirmwareRetrySoon()` underflows in the first hour after boot

- **Problem.** [include/network_runtime.inl:76-81](../include/network_runtime.inl#L76-L81) computes `lastFirmwareCheckMs = millis() - (intervalMs - retryMs)`. With defaults (1 h interval, 5 min retry), the subtrahend is ~3.3 M ms. If `millis() < 3.3 M`, the `uint32_t` wraps and the next firmware check is delayed by ~49.7 days instead of ~5 min.
- **Why it matters.** The "retry firmware check soon" path is broken for any boot where the firmware fetch fails within the first hour of uptime — i.e., every cold boot if the website is briefly down.
- **Approach.** Clamp the subtraction so `lastFirmwareCheckMs` cannot underflow.

**Implementation**
- [x] Replace the body of `scheduleFirmwareRetrySoon()` with a clamped variant: if `millis() < (intervalMs - retryMs)`, set `lastFirmwareCheckMs = 1` (avoiding 0 so `hasElapsedMs`'s "sinceMs == 0 → fire" shortcut isn't accidentally re-armed); otherwise compute the existing value.
- [x] Sanity-check that no other `gNetworkRuntime.last*Ms` field has the same pattern.

**Verification**
- [ ] Add or extend an existing unit-style serial command (or a one-off serial print) that logs `lastFirmwareCheckMs` and the next-fire delta after a forced failure. Confirm the next fire lands at ~5 min, not ~49 days.

---

### 5. Recovery AP arms back-to-back, defeating shadow-mode power savings

- **Problem.** `kWifiRecoveryApMs == kServerPostShadowMs == 10 min` ([include/board_config.h:122,136](../include/board_config.h#L122-L136)). Each failed scheduled post in shadow mode launches the recovery AP for 10 min; immediately after expiry the next scheduled post fails the same way and re-launches it. The AP is effectively always on whenever upstream WiFi is unreachable in shadow.
- **Why it matters.** Defeats the burst-only WiFi design when the user's home WiFi is down for hours; consumes more battery than necessary.
- **Approach.** Track consecutive recovery-AP launches across deep-sleep boundaries and back off (e.g. don't re-arm after N consecutive failures within the same shadow window, or stretch the next allowable launch to `recoveryApBackoffMs`). Recovery AP should remain a quick repair tool, not a permanent state.

**Implementation**
- [x] Add `RTC_DATA_ATTR uint8_t gRecoveryApConsecutiveLaunches` and `RTC_DATA_ATTR uint32_t gRecoveryApLastEndMs` (or equivalent) so backoff state survives the shadow-mode spin but resets on cold boot.
- [x] In `startRecoveryApAfterStaFailure()`, refuse to launch if the consecutive-launch counter exceeds a threshold (suggest 3) until a successful STA connect resets it.
- [x] In `connectStationBlocking()` or wherever STA flips to connected, zero the counter.
- [x] Decide whether to expose the threshold as a `BoardConfig::kWifiRecoveryApMaxConsecutive` constant or hard-code it. Suggest constant.

**Verification**
- [ ] Bench: configure an unreachable upstream SSID, force shadow mode (or just keep the panel covered), wait through 4–5 post intervals; confirm the AP only comes up the first 3 times then stays off.
- [ ] Bench: with the same setup, fix the upstream SSID; confirm STA connects and the counter resets so the recovery AP is available again on the next failure.

---

## Phase 2 — Conflicts & design smells (medium impact)

### 6. `lastPostAttemptMs` is set before the post succeeds — no fast retry on transient failure

- **Problem.** [include/network_runtime.inl:1173](../include/network_runtime.inl#L1173) updates the timestamp at the start of `postTelemetryNow()`. A connect timeout pushes the next attempt out by a full interval (10 or 30 min) regardless of whether the failure was transient.
- **Approach.** Introduce a separate `nextPostAllowedMs` field that defaults to interval but is shortened on failure (e.g. `min(60 s, interval / 4)`). Keep `lastPostAttemptMs` for telemetry.

**Implementation**
- [x] Extend `NetworkRuntimeState` in [include/app_types.h](../include/app_types.h) with `uint32_t nextPostAllowedMs`.
- [x] In `postTelemetryNow()`, on success set `nextPostAllowedMs = lastPostAttemptMs + interval`; on failure set it to `lastPostAttemptMs + min(interval/4, 60_000)`.
- [x] Update `scheduledPostDue()` to check `nextPostAllowedMs` instead of computing from interval.
- [x] Confirm `gDarkWakePostOnly` doesn't need this (dark wakes are already coarse-grained).

**Verification**
- [ ] Bench: block upstream WiFi for one shadow post window; confirm the next attempt fires at ~2 min, not 10 min.
- [ ] Bench: confirm successful posts still respect the full interval.

---

### 7. `WiFi.persistent(false)` should be set before any lockout-triggered WiFi ops

- **Problem.** [src/main.cpp:131-142](../src/main.cpp#L131-L142) can call `enterBatteryLockoutDeepSleep()` (which calls `WiFi.disconnect()` and friends) before [include/network_runtime.inl:1505](../include/network_runtime.inl#L1505) sets `WiFi.persistent(false)`. Each lockout cycle writes to NVS WiFi storage.
- **Approach.** Move `WiFi.persistent(false)` and `WiFi.setAutoReconnect(false)` to the very top of `setup()`, immediately after `Serial.begin`.

**Implementation**
- [x] In [src/main.cpp](../src/main.cpp) `setup()`, add `WiFi.persistent(false); WiFi.setAutoReconnect(false); WiFi.mode(WIFI_OFF);` right after `Serial.begin(115200);` and the optional CPU clock fixup.
- [x] Remove the duplicate calls from `initializeNetworkRuntime()` if they become redundant, or leave them as belt-and-braces (cheap).

**Verification**
- [ ] Bench: trigger a battery lockout and confirm subsequent boots still know the upstream SSID/password from `gSettings`, not from the WiFi driver's NVS cache.
- [ ] Confirm "Clear WiFi Cache" still wipes the right thing.

---

### 8. `gSettings` strings can theoretically be torn between webserver writes and other-task reads

- **Problem.** Today `gSettings` is only mutated on the network task and only its numeric fields are read elsewhere, but the layout is fragile — any future task that reads `gSettings.postUrl` etc. would race.
- **Approach.** Lightweight: add a comment + `static_assert`-friendly invariant. Heavyweight: add `gSettingsMutex` and gate writes/reads.

**Implementation (minimum viable)**
- [x] Add a comment block at the top of `RuntimeSettings` in [include/app_types.h](../include/app_types.h) documenting that all writes happen on the network task and other tasks must not read the string fields without explicit synchronization.
- [x] Audit current readers (grep `gSettings\.` across all `.inl` and `.cpp`); current string reads are limited to the network runtime plus startup/dark-wake policy code, and the comment documents the synchronization rule for future task readers.

**Implementation (if/when a non-network task needs them)**
- [ ] Add `gSettingsMutex` and convert string accesses behind it.

**Verification**
- [x] No runtime test required for the comment-only change. For the mutex change, run the standard smoke test to confirm no deadlocks under POST-Now spam.

---

### 9. BME280 is initialized before the dark-wake-only gate

- **Problem.** [src/main.cpp:148-160](../src/main.cpp#L148-L160) initializes BME280 before `updateSolarPowerPolicy()` decides whether this is a dark-wake-only boot. BME280 init + a forced measurement happen on every dark wake even when we're only going to post.
- **Approach.** Read INA219 #1 first, run the dark-wake gate, then conditionally init BME280.

**Implementation**
- [x] Refactor `setup()` to: (1) Wire begin, (2) INA219 #2 + battery lockout, (3) INA219 #1 + solar policy, (4) BME280 init only if `!gDarkWakePostOnly`.
- [x] Make sure `gTelemetry.bme280Online == false` posts cleanly through `buildTelemetryJson()` — confirmed by the existing offline path.

**Verification**
- [ ] Time `setup()` to first network task creation in both branches. Dark-wake-only should drop by ~100-300 ms.
- [ ] Confirm BME280 still works on normal boots and reconnects via `maintainSensorConnections`.

---

### 10. Failed dark-wake posts lose their slot for a full interval

- **Problem.** [include/network_runtime.inl:1515-1519](../include/network_runtime.inl#L1515-L1519) sleeps regardless of post result. A failed post means the next post is 30 min later (next post-due wake), turning a momentary upstream outage into a 60-min gap.
- **Approach.** On a failed dark-wake post, decrement `gDarkTimerWakeCount` so the *next* timer wake re-tries.

**Implementation**
- [x] In `applyNetworkPolicy`, capture the return value of `postTelemetryNow()` (boolean success). If false, decrement `gDarkTimerWakeCount` (saturating at zero) before calling `enterSolarDeepSleep()`.
- [x] Make sure this interacts cleanly with #6's `nextPostAllowedMs` — pick one source of truth for "should I retry sooner."

**Verification**
- [ ] Bench: in dark mode, block upstream WiFi for one post slot; confirm the next 10-min wake retries the post instead of sleeping silently.

---

### 11. Remote-config changes to `serverPostDarkMs` / `solarDeepSleepWakeMs` don't reset the wake counter

- **Problem.** `gDarkTimerWakeCount` (`RTC_DATA_ATTR`) survives mid-cycle settings changes, so the modulo math may emit one extra or one missing post on the transition.
- **Approach.** Zero the counter in `applyRemoteConfigPayload()` whenever either field changes.

**Implementation**
- [x] In [include/network_runtime.inl](../include/network_runtime.inl) `applyRemoteConfigPayload()`, after `gSettings = next; saveRuntimeSettings()`, reset `gDarkTimerWakeCount = 0` and `gDarkTimerWakeEvaluated = false` if either of those two fields differs from `previous`.
- [x] Apply the same logic in `handleConfigPost()` for the admin UI path.

**Verification**
- [ ] Bench: from the admin UI, change `serverPostDarkMs` from 30 min to 60 min; confirm the next dark wake doesn't immediately fire a post that would have been "due" under the old ratio.

---

### 12. Watchdog headroom during slow OTAs

- **Problem.** `kTaskWatchdogTimeoutS = 120` and `performHttpOtaUpdate()`'s outer loop calls `esp_task_wdt_reset()` on every successful read but lets the inner "wait for data" path run up to 15 s before timing out — fine, but the math is tight.
- **Approach.** Add an unconditional `esp_task_wdt_reset()` in the polling-with-no-data branch.

**Implementation**
- [x] In [include/network_runtime.inl](../include/network_runtime.inl) `performHttpOtaUpdate()`, inside the `else` branch where `available == 0`, call `esp_task_wdt_reset()` once per loop iteration.

**Verification**
- [ ] Stress-test OTA over a deliberately slow link (e.g. tc-throttled). Confirm no watchdog reset across multi-MB downloads.

---

### 13. Per-solar-mode CPU clock scaling (with aggressive shadow-mode limiting)

- **Problem.** [include/board_config.h:82](../include/board_config.h#L82) sets `kCpuFrequencyMhz` as a single compile-time constant tied to `kEnableBatterySaver`. The CPU runs at one fixed speed across sun, shadow, and dark — leaving easy power on the table in shadow (where the radio is off most of the time) and clipping headroom in sun.
- **Why it matters.** In shadow mode, between the 10-min post bursts, the firmware is doing very little real work (a sensor sample every 15 s, an OLED heartbeat every 60 s, RS485 every 3 s). At 80 MHz the CPU is wildly underutilized for that workload but still drawing ~20-30 mA. Dropping to 40 MHz between bursts can cut active-CPU power roughly in half on the hours where solar is marginal — exactly the regime where battery margin matters most.
- **Approach.** Make the CPU clock a function of `gSolarLightMode` and the current WiFi state. Apply the clock change from the same place that already reacts to mode transitions (`updateSolarPowerPolicy`). Hard rule: the radio requires ≥ 80 MHz, so any code path that brings WiFi up must clock up first and any code path that tears WiFi down can clock back down after.

  Proposed per-mode clocks (4 S Li-ion ESP32 dev module, current workload):

  | Mode    | Idle (radio off) | Burst (radio on) |
  | ------- | ---------------- | ---------------- |
  | Sun     | 160 MHz          | 160 MHz          |
  | Shadow  | 40 MHz           | 80 MHz           |
  | Dark    | 80 MHz           | 80 MHz           |
  | Unknown | 80 MHz           | 80 MHz           |

  Sun is bumped from 80 → 160 MHz to give the OLED redraw heartbeat (5–15 s in sun) and forecast math more headroom; we don't need 240 MHz and won't pay the extra current. Shadow idle drops to 40 MHz where the ESP32 must keep the radio off. Dark stays at 80 MHz because the dark-wake post path is brief enough that the clock-flip overhead would dominate.

**Implementation**

- [x] In [include/board_config.h](../include/board_config.h) add per-mode constants (e.g. `kCpuFreqSunMhz`, `kCpuFreqShadowIdleMhz`, `kCpuFreqShadowBurstMhz`, `kCpuFreqDarkMhz`, `kCpuFreqUnknownMhz`) inside `BoardConfig`. Keep `kCpuFrequencyMhz` as the cold-boot default until the first `updateSolarPowerPolicy` call settles the real mode.
- [x] Add a helper `static uint32_t cpuFreqForSolarMode(SolarLightMode mode, bool wifiActive)` to [include/power_policy.inl](../include/power_policy.inl). Centralize the "WiFi must run at ≥ 80 MHz" rule here so callers can't get it wrong.
- [x] Add a thin wrapper `static void applyCpuFrequencyForMode(SolarLightMode mode, bool wifiActive)` that calls `setCpuFrequencyMhz()` only when the requested value differs from `getCpuFrequencyMhz()` (avoid PLL churn on every poll).
- [x] Hook the wrapper into `updateSolarPowerPolicy()` in [include/power_policy.inl](../include/power_policy.inl), right next to the existing `applyDisplayContrastForSolarMode(...)` call. Pass `wifiActive = gNetworkRuntime.wifiEnabled || gNetworkRuntime.apEnabled || gNetworkRuntime.staConnected` so the helper picks the right tier.
- [x] In [include/network_runtime.inl](../include/network_runtime.inl), call `applyCpuFrequencyForMode(gSolarLightMode, true)` at the top of `connectStationBlocking()`, `startAccessPointIfNeeded()`, and the dark-wake `postTelemetryNow()` entry; call `applyCpuFrequencyForMode(gSolarLightMode, false)` at the tail of `stopWifiIfAllowed()` and after the recovery AP closes. The helper short-circuits if no change is needed, so these calls are cheap.
- [ ] Confirm RS485 (`Serial2`) baud-rate generation still works at 40 MHz. The 9600 baud divisor is well within range, but the auto-detect-polarity code in [include/rs485.inl](../include/rs485.inl) should be re-tested at the lower clock to make sure timing is still tight enough to read replies.
- [ ] Confirm software-I²C bit-banging in [include/i2c_soft.inl](../include/i2c_soft.inl) still meets SH1107 timing at 40 MHz. The current `kSoftwareI2cDelayUs` is 4 µs; at 40 MHz a `delayMicroseconds` is still accurate but the implicit per-instruction time stretches. A quick scope check or empirical "displays still draw correctly" verification on the bench is sufficient.
- [ ] Make sure the watchdog timeout (`kTaskWatchdogTimeoutS = 120 s`) still has enough margin at 40 MHz. The display task's heaviest path (full-frame redraw of 9 OLEDs over software I²C) at 40 MHz is the worst case — verify it stays well under 60 s wall-clock so we keep ≥ 2× headroom.
- [x] Decide whether dark mode benefits from a 40 MHz idle window between the boot moment and `enterSolarDeepSleep()`. Probably not (the awake window is < 1 s); leave dark at 80 MHz to keep the boot path simple.
- [x] Update [README.md](../README.md) "Power Saver" section to reflect the per-mode CPU clocks (currently it lists "CPU clock: `80 MHz`" only under shadow-mode timings).

**Verification**

- [ ] Bench: log `getCpuFrequencyMhz()` after each solar-mode transition and after each WiFi up/down event. Confirm the table above is honored.
- [ ] Bench: in shadow mode, measure INA219 #2 current draw with the clock at 40 MHz vs. 80 MHz across a full 10-min between-burst window. Expect a meaningful drop (~10-20 mA). Capture before/after in the PR.
- [ ] Bench: in shadow mode, force a `/api/admin/post-now` and confirm the clock comes up to 80 MHz before the first `WiFi.begin()` call, and drops back to 40 MHz after `stopWifiIfAllowed()`. Add temporary serial logging if needed; remove before merging.
- [ ] Bench: in sun mode at 160 MHz, confirm the 9-OLED full redraw (heartbeat) still completes within the heartbeat interval and that no display goes blank at the higher clock.
- [ ] Bench: simulate a fast Sun→Shadow→Sun transition (cover/uncover the panel rapidly). Confirm we don't thrash the PLL — the "only change if different" guard should suppress redundant calls.
- [ ] Smoke: run `~/.platformio/penv/bin/pio run` and `pio run -t upload`; let the station run for 24 h spanning at least one sun→shadow→dark transition; review the serial log for any anomalies tied to clock changes (Wire timeouts, U8g2 corruption, RS485 misreads).

---

### 14. Cut display power in shadow + dark-grace (no-off policy, maximize black pixels)

- **Problem.** SH1107 OLED current scales roughly linearly with the number of lit pixels *and* with the contrast register value. Today shadow contrast is `96/255` and dark-grace is `48/255` ([include/board_config.h:85,145-146](../include/board_config.h#L85)) — both well above zero, and every white pixel costs power. The current rendering style is tuned for sun-readable contrast, not for shadow-mode efficiency: filled blocks, bold glyphs, and white labels add up across 9 panels.
- **Constraints (per user).** Panels stay on continuously through shadow mode — no panel-off, no duty cycling, no blanking. Dark-mode behavior (2 h grace → deep sleep with `setAllDisplaysPowerSave(true)`) stays unchanged.
- **Why it matters.** With 9 panels at 96/255 contrast and a typical "label + primary value + secondary value" white layout, the displays are likely the largest single load on the battery during shadow stretches — comparable to or larger than the CPU savings from item #13. Halving the white-pixel count *and* dropping contrast another step compounds: `current ≈ contrast × white_pixel_count`, so a 2× drop in each is roughly a 4× drop in display current.
- **Approach.** Two complementary levers, both shipped together:
  1. **Contrast:** drop shadow contrast from 96 → ~24-32 and dark-grace from 48 → ~16. Add a per-mode floor that is still readable in indirect ambient light.
  2. **UI redesign:** rework each of the 9 screens in [include/display.inl](../include/display.inl) to maximize black pixels. Remove solid white fills, frames, filled bars, decorative borders. Keep one bold primary value, render labels and secondary values in a thin font. Goal: pixel-on ratio ≤ ~10 % per panel.

**Implementation — contrast tier**

- [x] Add new constants in [include/board_config.h](../include/board_config.h): `kDisplayShadowContrastLow` (target ~24), `kDisplayDarkGraceContrastLow` (target ~16), and a sun-mode value if we want to detune sun slightly too. Keep the existing `kDisplayContrast`/`kDisplaySunContrast`/`kDisplayShadowContrast`/`kDisplayDarkGraceContrast` symbols around as fallbacks until the dust settles, then remove them.
- [x] Update `displayContrastForSolarMode()` in [include/power_policy.inl:34-45](../include/power_policy.inl#L34-L45) to return the new lower-floor values for Shadow and Dark. Sun stays at 255.
- [x] Confirm `applyDisplayContrastForSolarMode()` ([include/power_policy.inl:105-122](../include/power_policy.inl#L105-L122)) remains the single chokepoint for contrast changes — every code path that flips panels back on must route through it (already the case after the dark→shadow transition; spot-check after the refactor).
- [x] Confirm the new contrast tier propagates to panels that come back online via `maintainDisplayConnections()` in [include/sensors.inl](../include/sensors.inl) — they should pick up the active solar-mode contrast on `initDisplay`, not snap back to 255.

**Implementation — UI redesign for low-white-pixel rendering**

- [x] Audit every draw function in [include/display.inl](../include/display.inl) for white-pixel sources: `drawBox`, `drawRBox`, `drawFrame`, large bold glyph runs, separator lines, axis ticks, filled icons, "battery bar"-style indicators. Enumerate the offenders in the PR description so the diff is reviewable.
- [x] Replace any solid white fill with either nothing or a single-pixel dotted/outlined cue (a single-pixel rounded outline is much cheaper than a filled rectangle).
- [x] Switch the primary value to a tall but *thin* U8g2 font (1-pixel stroke, no anti-aliasing fill). U8g2 ships several "thin" / "logisoso" thin variants; pick one that holds at the lower contrast floor.
- [x] Render labels (e.g. "ENV TEMP") in a small thin font. Drop any bold or filled label backgrounds.
- [x] Render secondary values (e.g. heat index, sector code) in the same thin font. Avoid stacked dual-bold layouts.
- [x] Forecast screen (display 3): forecast text label and 3 h trend should both be thin-font. No filled-bar trend indicator.
- [x] Wind-direction screen (display 5): the FRONT/RIGHT/REAR sector should be a thin label, not a filled arrow or filled sector wedge.
- [x] Battery + solar power screens (displays 6, 7, 8): no filled gauge bars. If a "fuel gauge" visual is wanted, draw it as a 1-pixel outlined frame with a thin tick at the current value rather than a filled bar.
- [ ] Verify each screen's primary number is still legible at arm's length at the new contrast floor on the bench. Use 24/255 as the reference.

**Implementation — guardrails against regressions**

- [x] Add a one-line comment at the top of [include/display.inl](../include/display.inl) stating the design rule: "Shadow + dark contrast floors are aggressive; rendering must minimize white pixels. No filled rectangles or bold backgrounds without an explicit reason." This deters future contributors from re-adding filled UI elements.
- [x] Make sure the heartbeat-driven full sweep at 60 s in shadow uses the same low-pixel layout (it does, since all redraws go through the same draw funcs — just confirm).
- [x] On the dark→shadow transition where `gDisplaysForcedOff` is cleared and `setAllDisplaysPowerSave(false)` runs ([include/power_policy.inl:240-243](../include/power_policy.inl#L240-L243)), confirm the next redraw applies both the new contrast AND the new layout. The existing `requestDisplayRefresh(kDisplayMaskAll)` call should already trigger that.
- [x] **Out of scope (flag for later):** the SPIFFS web dashboard at [data/index.html](../data/index.html) / [data/app.css](../data/app.css) is a separate code path served only when a user is actively viewing the dashboard over WiFi; it doesn't drive panel power. Don't bundle a CSS dark-theme pass into this PR. Open a follow-up if the user wants the web UI to match.

**Verification**

- [ ] Bench: measure INA219 #2 idle current in shadow mode with the OLD layout @ 96 contrast vs. the NEW layout @ the new shadow floor. Capture both numbers in the PR description. Target: ≥ 30 % drop in display-attributable current.
- [ ] Bench: photograph each of the 9 panels at the new contrast floor under (a) shaded indoor light and (b) bright daylight through a window. Confirm primary values are legible in both. Attach to the PR.
- [ ] Bench: run a full sun→shadow→dark cycle on the bench (cover the panel slowly). Confirm contrast tier transitions correctly and no panel goes dim-then-blank when it shouldn't.
- [ ] Bench: confirm the dark-mode "after 2 h grace, deep sleep" path is unchanged — `setAllDisplaysPowerSave(true)` still fires, OLEDs still enter internal power-save, ESP32 still deep-sleeps.
- [ ] Bench: confirm the dark-wake-only post path doesn't accidentally redraw panels at the new contrast (it shouldn't initialize panels at all when `gDarkWakePostOnly == true`).
- [x] Update [README.md](../README.md) "Power Saver" section: reflect the new shadow/dark-grace contrast values *and* add a one-line note on the "minimize white pixels" UI policy so future changes don't silently reintroduce filled blocks.

---

## Phase 3 — Smaller items

- [x] [README.md](../README.md): update the CPU-clock description to match the new per-mode/runtime clock policy.
- [x] [include/calculations.inl:13-15](../include/calculations.inl#L13-L15): add a comment to `hasElapsedMs` stating that `sinceMs == 0` is the "fire on first call" sentinel, so future maintainers don't accidentally use 0 as a real timestamp.
- [x] Audit grep: `grep -n 'last[A-Z][a-zA-Z]*Ms = 0' include/ src/` to confirm no field is ever zeroed in a way that would unintentionally trigger an immediate fire. Only `lastWifiAttemptMs` is cleared by the WiFi-cache handler and it is not used with `hasElapsedMs`.
- [x] Confirm `include/secrets.h` remains in `.gitignore` and rotate `SECRET_POST_TOKEN` if there's any chance the current value was shared. (The audit confirmed it's not currently tracked by git.)

---

## Phase 4 — End-to-end verification

After landing Phase 1 + 2 changes:

- [x] Run `~/.platformio/penv/bin/pio run` — clean build, no new warnings.
- [ ] Bench cycle:
  - [ ] Cold boot in shadow mode → first post fires within ~10 s and lands on the server.
  - [ ] Force sun → confirm post interval = `serverPostSunMs`, AP off between bursts.
  - [ ] Force dark → 2 h grace, then deep sleep, then 3× 10-min wakes silent, 4th wake posts and sleeps.
  - [ ] Pull INA219 #1 mid-shadow → mode goes Unknown, `gDarkWakePostOnly` stays false, normal operation continues.
  - [ ] Pull INA219 #2 below the lockout-enter threshold → station latches, deep-sleeps for 1 h, wakes and re-checks.
  - [ ] Push remote-config change for `serverPostShadowMs` from the website admin UI → confirmed applied via `/api/status` after the next post.
  - [ ] Trigger a website-side firmware update with a SHA-256 → device downloads, verifies, installs, reboots, posts new version.
- [ ] Compare 24 h power consumption (from INA219 #2 history) before and after the fixes — expect a measurable drop in shadow mode, especially on flaky upstream WiFi.
- [x] Update [README.md](../README.md) to reflect any user-visible changes (recovery AP backoff, post retry timing).
- [x] Update this plan: check items as they land, and add follow-ups discovered during verification.
