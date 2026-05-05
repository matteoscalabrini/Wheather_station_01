#pragma once

#include <Arduino.h>

namespace BoardConfig {

static constexpr char kBoardName[] = "ESP32 Dev Module";
static constexpr char kFirmwareVersion[] = "T3 V1.6.3";
static constexpr char kSpiffsVersion[] = "T3 V1.6.3";

// ─── I2C Bus 0 (Display bus, Software I2C) — SDA=32, SCL=33 ─────────────────
// Devices: up to 2x SH1107 displays
static constexpr uint8_t kI2c0Sda = 32;
static constexpr uint8_t kI2c0Scl = 33;

static constexpr uint8_t kDisplay0Address = 0x3D;
static constexpr uint8_t kDisplay1Address = 0x3C;

// ─── I2C Bus 1 (Display bus, Software I2C) — SDA=25, SCL=14 ─────────────────
// Devices: up to 2x SH1107 displays
static constexpr uint8_t kI2c1Sda = 25;
static constexpr uint8_t kI2c1Scl = 14;

static constexpr uint8_t kDisplay2Address = 0x3D;
static constexpr uint8_t kDisplay3Address = 0x3C;

// ─── I2C Bus 2 (Display bus, Software I2C) — SDA=21, SCL=22 ─────────────────
// Devices: up to 2x SH1107 displays
static constexpr uint8_t kI2c2Sda = 21;
static constexpr uint8_t kI2c2Scl = 22;

static constexpr uint8_t kDisplay4Address = 0x3D;
static constexpr uint8_t kDisplay5Address = 0x3C;

// ─── I2C Bus 3 (Display bus, Software I2C) — SDA=5, SCL=18 ──────────────────
// Devices: up to 2x SH1107 displays
static constexpr uint8_t kI2c3Sda = 5;
static constexpr uint8_t kI2c3Scl = 18;

static constexpr uint8_t kDisplay6Address = 0x3D;
static constexpr uint8_t kDisplay7Address = 0x3C;

// ─── I2C Bus 4 (Display bus, Software I2C) — SDA=19, SCL=23 ─────────────────
// Devices: 1x SH1107 display (9th)
static constexpr uint8_t kI2c4Sda = 19;
static constexpr uint8_t kI2c4Scl = 23;

static constexpr uint8_t kDisplay8Address = 0x3C;

// ─── I2C Bus 5 (Sensor bus, Hardware Wire) — SDA=26, SCL=27 ─────────────────
// Dedicated I2C bus for sensors only — no displays.
// Devices: BME280, INA219 #1, INA219 #2
static constexpr uint8_t kI2c5Sda = 26;
static constexpr uint8_t kI2c5Scl = 27;

static constexpr uint8_t kBme280PrimaryAddress   = 0x76;  // SDO -> GND
static constexpr uint8_t kBme280SecondaryAddress = 0x77;  // SDO -> VCC

static constexpr uint8_t kIna219_1_Address = 0x44;  // Solar power monitor
static constexpr uint8_t kIna219_2_Address = 0x40;  // Battery power monitor

// Battery percentage uses INA219 #2 load voltage with a 4S Li-ion curve.
// Adjust these thresholds if your BMS/cutoff or charge target differs.
static constexpr uint8_t kBatterySeriesCells = 4;
static constexpr float kBatteryPercentEmptyVoltageV = 12.00f;
static constexpr float kBatteryPercentFullVoltageV  = 16.80f;

// Battery low-voltage lockout.
// INA219 #2 load voltage is checked before normal boot work starts and during
// regular sensor sampling. Runtime cutoff enters deep sleep at/below the enter
// threshold; boot and timer wake guard stay asleep until the resume threshold.
static constexpr bool     kEnableBatteryLowVoltageLockout = true;
static constexpr float    kBatteryLockoutEnterVoltageV    = 11.50f;
static constexpr float    kBatteryLockoutResumeVoltageV   = 12.80f;
static constexpr uint32_t kBatteryLockoutWakeMs           = 60UL * 60UL * 1000UL;

// ─── Power Saver Profile ──────────────────────────────────────────────────────
// Battery-oriented profile. When enabled, task wakeups are slower, displays
// are only redrawn on meaningful changes, and OLEDs enter power-save mode
// after extended inactivity.
static constexpr bool     kEnableBatterySaver            = true;
static constexpr uint32_t kCpuFrequencyMhz               = kEnableBatterySaver ? 80UL : 240UL;
static constexpr uint32_t kCpuFreqSunMhz                 = kEnableBatterySaver ? 160UL : 240UL;
// Keep shadow idle at 80 MHz for bus stability (SW-I2C OLED + RS485 polling).
static constexpr uint32_t kCpuFreqShadowIdleMhz          = kEnableBatterySaver ? 80UL : 240UL;
static constexpr uint32_t kCpuFreqShadowBurstMhz         = kEnableBatterySaver ? 80UL : 240UL;
static constexpr uint32_t kCpuFreqDarkMhz                = kEnableBatterySaver ? 80UL : 240UL;
static constexpr uint32_t kCpuFreqUnknownMhz             = kCpuFrequencyMhz;
static constexpr bool     kDisplayRedrawOnChangeOnly     = kEnableBatterySaver;
static constexpr bool     kDisplayUsePowerSave           = false;
static constexpr uint8_t  kDisplayShadowContrastLow      = kEnableBatterySaver ? 24U : 255U;
static constexpr uint8_t  kDisplayDarkGraceContrastLow   = kEnableBatterySaver ? 16U : 255U;
static constexpr uint8_t  kDisplayContrast               = kDisplayShadowContrastLow;
static constexpr uint32_t kDisplayHeartbeatMs            = kEnableBatterySaver ? 60000UL : 5000UL;
static constexpr uint32_t kDisplayOnlineProbeMs          = kEnableBatterySaver ? 5UL * 60UL * 1000UL : 30000UL;
static constexpr uint32_t kDisplayOfflineRetryMs         = kEnableBatterySaver ? 60000UL : 3000UL;
static constexpr uint32_t kSensorSampleMs                = kEnableBatterySaver ? 15000UL : 2000UL;
static constexpr uint32_t kWindSampleMs                  = kEnableBatterySaver ? 3000UL : 1000UL;
static constexpr uint32_t kCommandPollMs                 = kEnableBatterySaver ? 250UL : 20UL;
static constexpr uint32_t kI2cMaintenanceMs              = kEnableBatterySaver ? 60000UL : 3000UL;
static constexpr uint32_t kTaskWatchdogTimeoutS          = 120UL;
static constexpr uint32_t kDisplayIdleTimeoutMs          = 0UL;
static constexpr float    kDisplayTempDeltaC             = 0.3f;
static constexpr float    kDisplayHumidityDeltaPct       = 1.0f;
static constexpr float    kDisplayPressureDeltaHpa       = 0.8f;
static constexpr float    kDisplayDewPointDeltaC         = 0.3f;
static constexpr float    kDisplayWindSpeedDeltaMs       = 0.5f;
static constexpr float    kDisplayWindAngleDeltaDeg      = 15.0f;
static constexpr float    kDisplayPowerDeltaW            = 0.5f;
static constexpr float    kDisplayVoltageDeltaV          = 0.1f;
static constexpr float    kDisplayBatteryPercentDeltaPct = 2.0f;
static constexpr uint32_t kIna219WakeMs                  = 3UL;

// Solar-voltage power policy.
// INA219 #1 load voltage and power are used as the light/charge proxy:
//   sun    >= kSolarSunEnterVoltageV and > kSolarSunMinPowerW
//   shadow between the dark and sun bands
//   dark   <= kSolarDarkEnterVoltageV
// Exit thresholds add hysteresis so clouds and sunrise/sunset edges do not
// rapidly bounce between modes.
static constexpr bool     kEnableSolarPowerPolicy        = true;
static constexpr float    kSolarSunEnterVoltageV         = 16.0f;
static constexpr float    kSolarSunExitVoltageV          = 15.0f;
static constexpr float    kSolarSunMinPowerW             = 3.0f;
static constexpr float    kSolarDarkEnterVoltageV        = 10.0f;
static constexpr float    kSolarDarkExitVoltageV         = 11.0f;
static constexpr uint32_t kSolarDarkDeepSleepDelayMs     = 2UL * 60UL * 60UL * 1000UL;
static constexpr uint32_t kSolarDeepSleepWakeMs          = 10UL * 60UL * 1000UL;
static constexpr uint32_t kServerPostSunMs               = 10UL * 60UL * 1000UL;
static constexpr uint32_t kServerPostShadowMs            = 10UL * 60UL * 1000UL;
static constexpr uint32_t kServerPostDarkMs              = 30UL * 60UL * 1000UL;
static constexpr uint32_t kRemoteConfigPullMs            = 10UL * 60UL * 1000UL;
static constexpr uint32_t kRemoteFirmwareCheckMs         = 60UL * 60UL * 1000UL;
static constexpr uint32_t kWifiPolicyPollMs              = 1000UL;
static constexpr uint32_t kWifiConnectTimeoutMs          = 15000UL;
static constexpr uint32_t kHttpConnectTimeoutMs          = 15000UL;
static constexpr uint16_t kHttpReadTimeoutMs             = 20000U;
static constexpr uint32_t kRemoteFailureRetryMs          = 5UL * 60UL * 1000UL;
// If a scheduled post cannot connect to the saved station WiFi while not in
// dark mode, briefly expose the setup AP so the network settings can be
// repaired locally. Dark mode never uses this recovery AP.
static constexpr bool     kWifiRecoveryApOnShadowStaFailure = true;
static constexpr uint32_t kWifiRecoveryApMs              = 10UL * 60UL * 1000UL;
static constexpr uint8_t  kWifiRecoveryApMaxConsecutive  = 3U;
// After hitting the consecutive recovery-AP cap, wait this long before
// automatically re-arming recovery AP again.
static constexpr uint32_t kWifiRecoveryApRearmMs         = 10UL * 60UL * 1000UL;
// Debug override default: when true, keeps the setup AP available in every
// solar mode while the ESP32 is awake. Keep false for burst-only WiFi.
static constexpr bool     kWifiDebugForceApAlways        = false;
static constexpr char     kWifiApSsid[]                  = "WeatherStation-AP";
static constexpr bool     kWifiApOpen                    = true;
static constexpr char     kWifiApPassword[]              = "";
static constexpr char     kDefaultAdminPassword[]        = "dflonline07";
static constexpr uint8_t  kDisplaySunContrast            = 255U;
static constexpr uint8_t  kDisplayShadowContrast         = kDisplayShadowContrastLow;
static constexpr uint8_t  kDisplayDarkGraceContrast      = kDisplayDarkGraceContrastLow;
static constexpr uint32_t kSensorSampleSunMs             = 5000UL;
static constexpr uint32_t kSensorSampleShadowMs          = kSensorSampleMs;
static constexpr uint32_t kSensorSampleDarkMs            = 60000UL;
static constexpr uint32_t kWindSampleSunMs               = 1000UL;
static constexpr uint32_t kWindSampleShadowMs            = kWindSampleMs;
static constexpr uint32_t kWindSampleDarkMs              = 60000UL;
static constexpr uint32_t kDisplayHeartbeatSunMs         = 15000UL;
static constexpr uint32_t kDisplayHeartbeatShadowMs      = kDisplayHeartbeatMs;
static constexpr uint32_t kDisplayHeartbeatDarkMs        = 60000UL;
static constexpr uint32_t kI2cMaintenanceSunMs           = 30000UL;
static constexpr uint32_t kI2cMaintenanceShadowMs        = kI2cMaintenanceMs;
static constexpr uint32_t kI2cMaintenanceDarkMs          = 60000UL;

// ─── Forecast Profile ────────────────────────────────────────────────────────
// Fixed-altitude short-term outlook using pressure trend history from the
// BME280. Forecast becomes ready after the full lookback window has elapsed.
static constexpr uint32_t kForecastSampleMs              = 60000UL;
static constexpr uint32_t kForecastLookbackMs            = 3UL * 60UL * 60UL * 1000UL;
static constexpr float    kForecastImprovingDeltaHpa     = 1.5f;
static constexpr float    kForecastUnsettledDeltaHpa     = -1.5f;
static constexpr float    kForecastRainLikelyDeltaHpa    = -3.5f;
static constexpr float    kForecastStormWatchDeltaHpa    = -6.0f;
static constexpr float    kForecastHumidAirPct           = 85.0f;
static constexpr float    kForecastNearSaturationDeltaC  = 2.0f;

// ─── RS485 bus (Serial2) ─────────────────────────────────────────────────────
// Wind speed (0x03) + wind direction (0x04) sensors.
// Both sensors: Modbus RTU, 9600 baud, 8N1, no parity.
// Set this to the sensor angle that should be treated as vehicle FRONT.
// Example: if the vane reports 90 deg when wind is hitting the front of the
// vehicle, set this to 90.0f so the relative display shows 0 deg = FRONT.
static constexpr uint8_t  kRs485Rx           = 16;
static constexpr uint8_t  kRs485Tx           = 17;
static constexpr uint8_t  kRs485De           = 4;
static constexpr bool     kRs485UseDeControl = false;
static constexpr uint32_t kModbusBaud        = 9600;
static constexpr bool     kRs485Invert       = false;
static constexpr bool     kRs485AutoDetectInvert = true;
static constexpr uint32_t kRs485InterQueryGapMs = 25UL;
static constexpr uint8_t  kWindSpeedAddr     = 0x03;
static constexpr uint8_t  kWindDirAddr       = 0x04;
static constexpr float    kWindRelativeFrontOffsetDeg = 0.0f;

}  // namespace BoardConfig
