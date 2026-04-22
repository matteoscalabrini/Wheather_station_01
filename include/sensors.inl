static WeatherSample readWeatherSample() {
    WeatherSample sample = invalidWeatherSample();
    if (!gBme280.takeForcedMeasurement()) return sample;
    sample.temperatureC = gBme280.readTemperature();
    sample.humidityPct = gBme280.readHumidity();
    sample.pressureHpa = gBme280.readPressure() / 100.0f;
    sample.dewPointC = computeDewPointC(sample.temperatureC, sample.humidityPct);
    sample.heatIndexC = computeHeatIndexC(sample.temperatureC, sample.humidityPct);
    return sample;
}

static PowerSample readPower(Adafruit_INA219 &ina) {
    PowerSample sample = invalidPowerSample();
    ina.powerSave(false);
    const bool wakeOk = ina.success();
    taskDelayMs(BoardConfig::kIna219WakeMs);
    sample.shuntVoltageMv = ina.getShuntVoltage_mV();
    const bool shuntOk = ina.success();
    sample.busVoltageV = ina.getBusVoltage_V();
    const bool busOk = ina.success();
    sample.loadVoltageV = sample.busVoltageV + (sample.shuntVoltageMv / 1000.0f);
    sample.currentMa = ina.getCurrent_mA();
    const bool currentOk = ina.success();
    sample.powerW = ina.getPower_mW() / 1000.0f;
    const bool powerOk = ina.success();
    ina.powerSave(true);
    if (!(wakeOk && shuntOk && busOk && currentOk && powerOk && isPowerSampleValid(sample))) {
        return invalidPowerSample();
    }
    return sample;
}

static uint8_t detectBme280Address(TwoWire &bus) {
    if (probeHardwareI2c(bus, BoardConfig::kBme280PrimaryAddress)) {
        return BoardConfig::kBme280PrimaryAddress;
    }
    if (probeHardwareI2c(bus, BoardConfig::kBme280SecondaryAddress)) {
        return BoardConfig::kBme280SecondaryAddress;
    }
    return 0;
}

static bool beginBme280OnBus(uint8_t address, WeatherSample &sample) {
    if (!address) return false;
    if (!gBme280.begin(address, &Wire)) return false;

    gBme280.setSampling(Adafruit_BME280::MODE_FORCED,
        Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::FILTER_OFF,
        Adafruit_BME280::STANDBY_MS_0_5);

    sample = readWeatherSample();
    return isWeatherSampleValid(sample);
}

static bool beginIna219OnBus(Adafruit_INA219 &ina, PowerSample &sample) {
    if (!ina.begin(&Wire)) return false;
    sample = readPower(ina);
    return isPowerSampleValid(sample);
}

static void maintainDisplayConnections() {
    const TelemetryState snapshot = copyTelemetry();
    const uint32_t nowMs = millis();

    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        const DisplaySlot &slot = kDisplaySlots[i];
        const bool wasOnline = snapshot.displayOnline[i];
        bool online = wasOnline;
        const uint32_t probeIntervalMs = wasOnline ?
            BoardConfig::kDisplayOnlineProbeMs : BoardConfig::kDisplayOfflineRetryMs;

        takeMutex(gDisplayBusMutex);
        DisplayRuntimeState &runtime = gDisplayRuntime[i];
        if (!hasElapsedMs(nowMs, runtime.lastProbeMs, probeIntervalMs)) {
            giveMutex(gDisplayBusMutex);
            continue;
        }
        runtime.lastProbeMs = nowMs;
        const bool present = probeSoftwareI2c(kBusSda[slot.busIndex], kBusScl[slot.busIndex],
                                              slot.i2cAddress);
        if (!present) {
            online = false;
        } else if (!wasOnline) {
            online = initDisplay(i);
        }
        if (online != wasOnline) {
            resetDisplayRuntimeState(i);
            if (online) gDisplayRuntime[i].lastProbeMs = nowMs;
        }
        giveMutex(gDisplayBusMutex);

        if (online != wasOnline) setDisplayOnline(i, online);
        taskDelayMs(1);
    }
}

static void maintainSensorConnections() {
    WeatherSample weather = invalidWeatherSample();
    ForecastState forecast = {ForecastCode::Waiting, 0.0f, false};
    PowerSample solar = invalidPowerSample();
    PowerSample battery = invalidPowerSample();
    bool bmeOnline = false;
    bool solarOnline = false;
    bool batteryOnline = false;
    uint8_t bmeAddress = 0;

    takeMutex(gSensorBusMutex);
    takeMutex(gTelemetryMutex);
    weather = gTelemetry.weather;
    forecast = gTelemetry.forecast;
    solar = gTelemetry.solar;
    battery = gTelemetry.battery;
    bmeOnline = gTelemetry.bme280Online;
    solarOnline = gTelemetry.solarOnline;
    batteryOnline = gTelemetry.batteryOnline;
    bmeAddress = gTelemetry.bme280Address;
    giveMutex(gTelemetryMutex);

    if (!bmeOnline) {
        const uint8_t detectedBmeAddress = detectBme280Address(Wire);
        if (!detectedBmeAddress) {
            bmeAddress = 0;
            forecast = {ForecastCode::Waiting, 0.0f, false};
        } else {
            bmeAddress = detectedBmeAddress;
            bmeOnline = beginBme280OnBus(detectedBmeAddress, weather);
            if (bmeOnline) {
                const uint32_t nowMs = millis();
                recordForecastHistory(weather, nowMs);
                forecast = computeForecast(weather, nowMs);
            } else {
                forecast = {ForecastCode::Waiting, 0.0f, false};
            }
        }
    }

    if (!solarOnline && probeHardwareI2c(Wire, BoardConfig::kIna219_1_Address)) {
        solarOnline = beginIna219OnBus(gIna219Solar, solar);
    }

    if (!batteryOnline && probeHardwareI2c(Wire, BoardConfig::kIna219_2_Address)) {
        batteryOnline = beginIna219OnBus(gIna219Battery, battery);
    }

    const float batteryPercent = batteryOnline ?
        computeBatteryPercent(battery.loadVoltageV) : -1.0f;

    updateSensorSamples(weather, bmeOnline, bmeAddress, forecast, solar, solarOnline,
                        battery, batteryOnline, batteryPercent);
    giveMutex(gSensorBusMutex);
}
