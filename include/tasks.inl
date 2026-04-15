static void sensorTask(void *parameter) {
    (void)parameter;
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        WeatherSample weather = {};
        ForecastState forecast = {ForecastCode::Waiting, 0.0f, false};
        PowerSample solar = {};
        PowerSample battery = {};
        bool bmeOnline = false;
        bool solarOnline = false;
        bool batteryOnline = false;

        takeMutex(gSensorBusMutex);
        takeMutex(gTelemetryMutex);
        weather = gTelemetry.weather;
        forecast = gTelemetry.forecast;
        solar = gTelemetry.solar;
        battery = gTelemetry.battery;
        bmeOnline = gTelemetry.bme280Online;
        solarOnline = gTelemetry.solarOnline;
        batteryOnline = gTelemetry.batteryOnline;
        giveMutex(gTelemetryMutex);

        if (bmeOnline) {
            weather = readWeatherSample();
            const uint32_t nowMs = millis();
            recordForecastHistory(weather, nowMs);
            forecast = computeForecast(weather, nowMs);
        }
        if (solarOnline) solar = readPower(gIna219Solar);
        if (batteryOnline) battery = readPower(gIna219Battery);

        const float batteryPercent = batteryOnline ?
            computeBatteryPercent(battery.loadVoltageV) : -1.0f;

        updateSensorSamples(weather, forecast, solar, battery, batteryPercent);
        giveMutex(gSensorBusMutex);

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(kSensorSampleMs));
    }
}

static void maintenanceTask(void *parameter) {
    (void)parameter;

    for (;;) {
        maintainDisplayConnections();
        maintainSensorConnections();
        taskDelayMs(kI2cMaintenanceMs);
    }
}

static void commsTask(void *parameter) {
    (void)parameter;
    TickType_t lastWindWake = xTaskGetTickCount();

    for (;;) {
        readSerial();

        if ((xTaskGetTickCount() - lastWindWake) >= pdMS_TO_TICKS(kWindSampleMs)) {
            bool speedOnline = false;
            bool dirOnline = false;
            const WindSample wind = pollWindSensors(speedOnline, dirOnline);
            updateWindTelemetry(wind, speedOnline, dirOnline);
            lastWindWake = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(kCommandPollMs));
    }
}

static void displayTask(void *parameter) {
    (void)parameter;
    uint8_t displayIndex = 0;

    for (;;) {
        renderDisplaySlice(displayIndex, copyTelemetry());
        displayIndex = (uint8_t)((displayIndex + 1) % kNumDisplays);
        taskDelayMs(kDisplaySliceMs);
    }
}
