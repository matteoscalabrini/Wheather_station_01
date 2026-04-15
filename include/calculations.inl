static void taskDelayMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}

static void takeMutex(SemaphoreHandle_t mutex) {
    if (mutex != nullptr) xSemaphoreTake(mutex, portMAX_DELAY);
}

static void giveMutex(SemaphoreHandle_t mutex) {
    if (mutex != nullptr) xSemaphoreGive(mutex);
}

static float clampPercent(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

static float computeDewPointC(float temperatureC, float humidityPct) {
    const float humidity = humidityPct < 1.0f ? 1.0f : humidityPct;
    const float gamma = logf(humidity / 100.0f) +
        (17.62f * temperatureC) / (243.12f + temperatureC);
    return (243.12f * gamma) / (17.62f - gamma);
}

static float computeHeatIndexC(float temperatureC, float humidityPct) {
    if (temperatureC < 26.7f || humidityPct < 40.0f) return temperatureC;

    const float temperatureF = (temperatureC * 9.0f / 5.0f) + 32.0f;
    const float humidity = humidityPct;
    float heatIndexF =
        -42.379f +
        2.04901523f * temperatureF +
        10.14333127f * humidity -
        0.22475541f * temperatureF * humidity -
        0.00683783f * temperatureF * temperatureF -
        0.05481717f * humidity * humidity +
        0.00122874f * temperatureF * temperatureF * humidity +
        0.00085282f * temperatureF * humidity * humidity -
        0.00000199f * temperatureF * temperatureF * humidity * humidity;

    if (humidity < 13.0f && temperatureF >= 80.0f && temperatureF <= 112.0f) {
        const float adjust =
            ((13.0f - humidity) / 4.0f) *
            sqrtf((17.0f - fabsf(temperatureF - 95.0f)) / 17.0f);
        heatIndexF -= adjust;
    } else if (humidity > 85.0f && temperatureF >= 80.0f && temperatureF <= 87.0f) {
        const float adjust =
            ((humidity - 85.0f) / 10.0f) *
            ((87.0f - temperatureF) / 5.0f);
        heatIndexF += adjust;
    }

    return (heatIndexF - 32.0f) * 5.0f / 9.0f;
}

static const char *forecastDisplayLabel(ForecastCode code) {
    switch (code) {
        case ForecastCode::Improving: return "BETTER";
        case ForecastCode::Stable: return "STABLE";
        case ForecastCode::Unsettled: return "UNSETTLD";
        case ForecastCode::RainLikely: return "RAIN";
        case ForecastCode::StormWatch: return "STORM";
        case ForecastCode::Waiting:
        default: return "LEARN";
    }
}

static const char *forecastStatusLabel(ForecastCode code) {
    switch (code) {
        case ForecastCode::Improving: return "Improving";
        case ForecastCode::Stable: return "Stable";
        case ForecastCode::Unsettled: return "Unsettled";
        case ForecastCode::RainLikely: return "Rain Likely";
        case ForecastCode::StormWatch: return "Storm Watch";
        case ForecastCode::Waiting:
        default: return "Collecting History";
    }
}

static bool isHumidForecastPattern(const WeatherSample &weather) {
    return weather.humidityPct >= BoardConfig::kForecastHumidAirPct ||
        (weather.temperatureC - weather.dewPointC) <= BoardConfig::kForecastNearSaturationDeltaC;
}

static ForecastState classifyForecast(float delta3hHpa, const WeatherSample &weather) {
    ForecastState forecast = {ForecastCode::Stable, delta3hHpa, true};
    const bool humidPattern = isHumidForecastPattern(weather);

    if (delta3hHpa <= BoardConfig::kForecastStormWatchDeltaHpa) {
        forecast.code = humidPattern ? ForecastCode::StormWatch : ForecastCode::RainLikely;
    } else if (delta3hHpa <= BoardConfig::kForecastRainLikelyDeltaHpa) {
        forecast.code = humidPattern ? ForecastCode::RainLikely : ForecastCode::Unsettled;
    } else if (delta3hHpa <= BoardConfig::kForecastUnsettledDeltaHpa) {
        forecast.code = ForecastCode::Unsettled;
    } else if (delta3hHpa >= BoardConfig::kForecastImprovingDeltaHpa) {
        forecast.code = ForecastCode::Improving;
    }

    return forecast;
}

static void recordForecastHistory(const WeatherSample &weather, uint32_t nowMs) {
    if (gForecastLastSampleMs != 0 &&
        (uint32_t)(nowMs - gForecastLastSampleMs) < BoardConfig::kForecastSampleMs) {
        return;
    }

    gForecastHistory[gForecastHistoryNext] = {nowMs, weather.pressureHpa};
    gForecastHistoryNext = (gForecastHistoryNext + 1U) % kForecastHistoryCapacity;
    if (gForecastHistoryCount < kForecastHistoryCapacity) ++gForecastHistoryCount;
    gForecastLastSampleMs = nowMs;
}

static bool findForecastReferencePoint(uint32_t targetMs, ForecastHistoryPoint &point) {
    bool found = false;
    for (size_t i = 0; i < gForecastHistoryCount; ++i) {
        const ForecastHistoryPoint &candidate = gForecastHistory[i];
        if (candidate.timestampMs > targetMs) continue;
        if (!found || candidate.timestampMs > point.timestampMs) {
            point = candidate;
            found = true;
        }
    }
    return found;
}

static ForecastState computeForecast(const WeatherSample &weather, uint32_t nowMs) {
    ForecastState forecast = {ForecastCode::Waiting, 0.0f, false};
    if (nowMs < BoardConfig::kForecastLookbackMs) return forecast;

    ForecastHistoryPoint reference = {};
    const uint32_t targetMs = nowMs - BoardConfig::kForecastLookbackMs;
    if (!findForecastReferencePoint(targetMs, reference)) return forecast;

    return classifyForecast(weather.pressureHpa - reference.pressureHpa, weather);
}

static float computeBatteryPercent(float voltageV) {
    const float emptyV = BoardConfig::kBatteryPercentEmptyVoltageV;
    const float fullV = BoardConfig::kBatteryPercentFullVoltageV;
    if (fullV <= emptyV) return -1.0f;
    return clampPercent((voltageV - emptyV) * 100.0f / (fullV - emptyV));
}

static float normalizeRelativeWindDeg(float directionDeg) {
    float relativeDeg = fmodf(directionDeg - BoardConfig::kWindRelativeFrontOffsetDeg, 360.0f);
    if (relativeDeg < 0.0f) relativeDeg += 360.0f;
    return relativeDeg;
}

static const char *windRelativeLabel(float directionDeg) {
    static const char *kLabels[] = {
        "REAR", "REAR-L", "LEFT", "FRONT-L",
        "FRONT", "FRONT-R", "RIGHT", "REAR-R"
    };

    const float relativeDeg = normalizeRelativeWindDeg(directionDeg);
    uint8_t index = (uint8_t)((relativeDeg + 22.5f) / 45.0f);
    if (index >= 8) index = 0;
    return kLabels[index];
}

static const char *rs485ConfigLabel(uint32_t config) {
    if (config == SERIAL_8N1) return "8N1";
    if (config == SERIAL_8E1) return "8E1";
    if (config == SERIAL_8O1) return "8O1";
    return "custom";
}

static float floatDelta(float current, float previous) {
    return fabsf(current - previous);
}

static float circularAngleDelta(float currentDeg, float previousDeg) {
    float delta = floatDelta(currentDeg, previousDeg);
    if (delta > 180.0f) delta = 360.0f - delta;
    return delta;
}

static TelemetryState copyTelemetry() {
    TelemetryState snapshot;
    takeMutex(gTelemetryMutex);
    snapshot = gTelemetry;
    giveMutex(gTelemetryMutex);
    return snapshot;
}

static void updateWeatherTelemetry(const WeatherSample &weather, bool online, uint8_t address) {
    takeMutex(gTelemetryMutex);
    gTelemetry.weather = weather;
    gTelemetry.bme280Online = online;
    gTelemetry.bme280Address = address;
    giveMutex(gTelemetryMutex);
}

static void updatePowerTelemetry(const PowerSample &solar, bool solarOnline,
                                 const PowerSample &battery, bool batteryOnline,
                                 float batteryPercent) {
    takeMutex(gTelemetryMutex);
    gTelemetry.solar = solar;
    gTelemetry.solarOnline = solarOnline;
    gTelemetry.battery = battery;
    gTelemetry.batteryOnline = batteryOnline;
    gTelemetry.batteryPercent = batteryPercent;
    giveMutex(gTelemetryMutex);
}

static void updateWindTelemetry(const WindSample &wind, bool speedOnline, bool dirOnline) {
    takeMutex(gTelemetryMutex);
    gTelemetry.wind = wind;
    gTelemetry.windSpeedOnline = speedOnline;
    gTelemetry.windDirOnline = dirOnline;
    giveMutex(gTelemetryMutex);
}

static void updateSensorSamples(const WeatherSample &weather,
                                const ForecastState &forecast,
                                const PowerSample &solar,
                                const PowerSample &battery,
                                float batteryPercent) {
    takeMutex(gTelemetryMutex);
    gTelemetry.weather = weather;
    gTelemetry.forecast = forecast;
    gTelemetry.solar = solar;
    gTelemetry.battery = battery;
    gTelemetry.batteryPercent = batteryPercent;
    giveMutex(gTelemetryMutex);
}
