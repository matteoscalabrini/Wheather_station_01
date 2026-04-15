static WeatherSample readWeatherSample() {
    WeatherSample sample = {};
    sample.temperatureC = gBme280.readTemperature();
    sample.humidityPct = gBme280.readHumidity();
    sample.pressureHpa = gBme280.readPressure() / 100.0f;
    sample.dewPointC = computeDewPointC(sample.temperatureC, sample.humidityPct);
    sample.heatIndexC = computeHeatIndexC(sample.temperatureC, sample.humidityPct);
    return sample;
}

static PowerSample readPower(Adafruit_INA219 &ina) {
    PowerSample sample = {};
    sample.shuntVoltageMv = ina.getShuntVoltage_mV();
    sample.busVoltageV = ina.getBusVoltage_V();
    sample.loadVoltageV = sample.busVoltageV + (sample.shuntVoltageMv / 1000.0f);
    sample.currentMa = ina.getCurrent_mA();
    sample.powerW = ina.getPower_mW() / 1000.0f;
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

    gBme280.setSampling(Adafruit_BME280::MODE_NORMAL,
        Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::FILTER_OFF,
        Adafruit_BME280::STANDBY_MS_1000);

    sample = readWeatherSample();
    return isfinite(sample.temperatureC) && isfinite(sample.humidityPct) &&
        isfinite(sample.pressureHpa) && isfinite(sample.dewPointC) &&
        isfinite(sample.heatIndexC);
}

static bool beginIna219OnBus(Adafruit_INA219 &ina, PowerSample &sample) {
    if (!ina.begin(&Wire)) return false;
    sample = readPower(ina);
    return isfinite(sample.shuntVoltageMv) && isfinite(sample.busVoltageV) &&
        isfinite(sample.loadVoltageV) && isfinite(sample.currentMa) &&
        isfinite(sample.powerW);
}

static void maintainDisplayConnections() {
    const TelemetryState snapshot = copyTelemetry();

    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        const DisplaySlot &slot = kDisplaySlots[i];
        const bool wasOnline = snapshot.displayOnline[i];
        bool online = wasOnline;

        takeMutex(gDisplayBusMutex);
        const bool present = probeSoftwareI2c(kBusSda[slot.busIndex], kBusScl[slot.busIndex],
                                              slot.i2cAddress);
        if (!present) {
            online = false;
        } else if (!wasOnline) {
            online = initDisplay(i);
        }
        if (online != wasOnline) resetDisplayRuntimeState(i);
        giveMutex(gDisplayBusMutex);

        if (online != wasOnline) setDisplayOnline(i, online);
        taskDelayMs(1);
    }
}

static void maintainSensorConnections() {
    WeatherSample weather = {};
    PowerSample solar = {};
    PowerSample battery = {};
    bool bmeOnline = false;
    bool solarOnline = false;
    bool batteryOnline = false;
    uint8_t bmeAddress = 0;

    takeMutex(gSensorBusMutex);
    takeMutex(gTelemetryMutex);
    weather = gTelemetry.weather;
    solar = gTelemetry.solar;
    battery = gTelemetry.battery;
    bmeOnline = gTelemetry.bme280Online;
    solarOnline = gTelemetry.solarOnline;
    batteryOnline = gTelemetry.batteryOnline;
    bmeAddress = gTelemetry.bme280Address;
    giveMutex(gTelemetryMutex);

    const uint8_t detectedBmeAddress = detectBme280Address(Wire);
    if (!detectedBmeAddress) {
        bmeOnline = false;
    } else {
        const bool addressChanged = detectedBmeAddress != bmeAddress;
        bmeAddress = detectedBmeAddress;
        if (!bmeOnline || addressChanged) {
            bmeOnline = beginBme280OnBus(detectedBmeAddress, weather);
        }
    }

    const bool solarPresent = probeHardwareI2c(Wire, BoardConfig::kIna219_1_Address);
    if (!solarPresent) {
        solarOnline = false;
        solar = {};
    } else if (!solarOnline) {
        solarOnline = beginIna219OnBus(gIna219Solar, solar);
        if (!solarOnline) solar = {};
    }

    const bool batteryPresent = probeHardwareI2c(Wire, BoardConfig::kIna219_2_Address);
    if (!batteryPresent) {
        batteryOnline = false;
        battery = {};
    } else if (!batteryOnline) {
        batteryOnline = beginIna219OnBus(gIna219Battery, battery);
        if (!batteryOnline) battery = {};
    }

    const float batteryPercent = batteryOnline ?
        computeBatteryPercent(battery.loadVoltageV) : -1.0f;

    takeMutex(gTelemetryMutex);
    gTelemetry.weather = weather;
    gTelemetry.bme280Online = bmeOnline;
    gTelemetry.bme280Address = bmeAddress;
    gTelemetry.solar = solar;
    gTelemetry.solarOnline = solarOnline;
    gTelemetry.battery = battery;
    gTelemetry.batteryOnline = batteryOnline;
    gTelemetry.batteryPercent = batteryPercent;
    giveMutex(gTelemetryMutex);
    giveMutex(gSensorBusMutex);
}
