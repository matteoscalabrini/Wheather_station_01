static void sensorTask(void *parameter) {
    (void)parameter;
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
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

        if (bmeOnline) {
            const WeatherSample measured = readWeatherSample();
            if (isWeatherSampleValid(measured)) {
                weather = measured;
                const uint32_t nowMs = millis();
                recordForecastHistory(weather, nowMs);
                forecast = computeForecast(weather, nowMs);
            } else {
                bmeOnline = false;
                forecast = {ForecastCode::Waiting, 0.0f, false};
            }
        }

        if (solarOnline) {
            const PowerSample measured = readPower(gIna219Solar);
            if (isPowerSampleValid(measured)) solar = measured;
            else solarOnline = false;
        }

        if (batteryOnline) {
            const PowerSample measured = readPower(gIna219Battery);
            if (isPowerSampleValid(measured)) battery = measured;
            else batteryOnline = false;
        }

        const float batteryPercent = batteryOnline ?
            computeBatteryPercent(battery.loadVoltageV) : -1.0f;

        updateSensorSamples(weather, bmeOnline, bmeAddress, forecast, solar, solarOnline,
                            battery, batteryOnline, batteryPercent);
        giveMutex(gSensorBusMutex);

        updateSolarPowerPolicy(solar, solarOnline);
        esp_task_wdt_reset();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(activeSensorSampleMs()));
    }
}

static void maintenanceTask(void *parameter) {
    (void)parameter;

    for (;;) {
        maintainDisplayConnections();
        maintainSensorConnections();
        esp_task_wdt_reset();
        taskDelayMs(activeI2cMaintenanceMs());
    }
}

static void commsTask(void *parameter) {
    (void)parameter;
    TickType_t lastWindWake = xTaskGetTickCount();

    for (;;) {
        readSerial();

        if ((xTaskGetTickCount() - lastWindWake) >= pdMS_TO_TICKS(activeWindSampleMs())) {
            bool speedOnline = false;
            bool dirOnline = false;
            const WindSample wind = pollWindSensors(speedOnline, dirOnline);
            updateWindTelemetry(wind, speedOnline, dirOnline);
            lastWindWake = xTaskGetTickCount();
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(kCommandPollMs));
    }
}

static void displayTask(void *parameter) {
    (void)parameter;
    uint32_t pendingMask = kDisplayMaskAll;

    for (;;) {
        if (pendingMask == 0) {
            if (xTaskNotifyWait(0, UINT32_MAX, &pendingMask,
                                pdMS_TO_TICKS(activeDisplayHeartbeatMs())) != pdPASS) {
                pendingMask = kDisplayMaskAll;
            }
        }

        if (gDisplaysForcedOff) {
            pendingMask = 0;
            esp_task_wdt_reset();
            continue;
        }

        const TelemetryState snapshot = copyTelemetry();
        renderDisplayMask((uint16_t)pendingMask, snapshot);
        pendingMask = 0;
        esp_task_wdt_reset();
    }
}

static void networkTask(void *parameter) {
    (void)parameter;

    for (;;) {
        applyNetworkPolicy();
        esp_task_wdt_reset();
        taskDelayMs(BoardConfig::kWifiPolicyPollMs);
    }
}
