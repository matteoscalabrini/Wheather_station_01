#pragma once

#include <stdint.h>

class U8G2;

struct WeatherSample {
    float temperatureC;
    float humidityPct;
    float pressureHpa;
    float dewPointC;
    float heatIndexC;
};

struct WindSample {
    float speedMs;
    float directionDeg;
    uint8_t beaufort;
};

struct PowerSample {
    float shuntVoltageMv;
    float busVoltageV;
    float loadVoltageV;
    float currentMa;
    float powerW;
};

enum class ForecastCode : uint8_t {
    Waiting,
    Improving,
    Stable,
    Unsettled,
    RainLikely,
    StormWatch,
};

enum class SolarLightMode : uint8_t {
    Unknown,
    Dark,
    Shadow,
    Sun,
};

struct ForecastState {
    ForecastCode code;
    float delta3hHpa;
    bool ready;
};

struct TelemetryState {
    WeatherSample weather;
    ForecastState forecast;
    WindSample wind;
    PowerSample solar;
    PowerSample battery;
    bool displayOnline[9];
    bool bme280Online;
    bool solarOnline;
    bool batteryOnline;
    bool windSpeedOnline;
    bool windDirOnline;
    uint8_t bme280Address;
    float batteryPercent;
};

struct DisplaySlot {
    uint8_t busIndex;
    uint8_t i2cAddress;
};

struct DisplayRuntimeState {
    TelemetryState lastRendered;
    uint32_t lastActivityMs;
    uint32_t lastProbeMs;
    bool hasRendered;
    bool powerSave;
};

struct ForecastHistoryPoint {
    uint32_t timestampMs;
    float pressureHpa;
};

using DrawFunc = void (*)(U8G2 &, const TelemetryState &);
