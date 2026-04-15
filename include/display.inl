static bool displayNeedsRefresh(uint8_t index, const TelemetryState &current,
                                const TelemetryState &previous) {
    switch (index) {
        case 0:
            return current.bme280Online != previous.bme280Online ||
                current.bme280Address != previous.bme280Address ||
                floatDelta(current.weather.temperatureC, previous.weather.temperatureC) >=
                    BoardConfig::kDisplayTempDeltaC ||
                floatDelta(current.weather.heatIndexC, previous.weather.heatIndexC) >=
                    BoardConfig::kDisplayTempDeltaC;
        case 1:
            return current.bme280Online != previous.bme280Online ||
                floatDelta(current.weather.humidityPct, previous.weather.humidityPct) >=
                    BoardConfig::kDisplayHumidityDeltaPct;
        case 2:
            return current.bme280Online != previous.bme280Online ||
                floatDelta(current.weather.pressureHpa, previous.weather.pressureHpa) >=
                    BoardConfig::kDisplayPressureDeltaHpa;
        case 3:
            return current.bme280Online != previous.bme280Online ||
                current.forecast.ready != previous.forecast.ready ||
                current.forecast.code != previous.forecast.code ||
                floatDelta(current.forecast.delta3hHpa, previous.forecast.delta3hHpa) >=
                    BoardConfig::kDisplayPressureDeltaHpa;
        case 4:
            return current.windSpeedOnline != previous.windSpeedOnline ||
                current.wind.beaufort != previous.wind.beaufort ||
                floatDelta(current.wind.speedMs, previous.wind.speedMs) >=
                    BoardConfig::kDisplayWindSpeedDeltaMs;
        case 5:
            return current.windDirOnline != previous.windDirOnline ||
                circularAngleDelta(normalizeRelativeWindDeg(current.wind.directionDeg),
                                   normalizeRelativeWindDeg(previous.wind.directionDeg)) >=
                    BoardConfig::kDisplayWindAngleDeltaDeg ||
                strcmp(windRelativeLabel(current.wind.directionDeg),
                       windRelativeLabel(previous.wind.directionDeg)) != 0;
        case 6:
            return current.solarOnline != previous.solarOnline ||
                floatDelta(current.solar.powerW, previous.solar.powerW) >=
                    BoardConfig::kDisplayPowerDeltaW ||
                floatDelta(current.solar.loadVoltageV, previous.solar.loadVoltageV) >=
                    BoardConfig::kDisplayVoltageDeltaV;
        case 7:
            return current.batteryOnline != previous.batteryOnline ||
                floatDelta(current.battery.powerW, previous.battery.powerW) >=
                    BoardConfig::kDisplayPowerDeltaW ||
                floatDelta(current.battery.loadVoltageV, previous.battery.loadVoltageV) >=
                    BoardConfig::kDisplayVoltageDeltaV;
        case 8:
            return current.batteryOnline != previous.batteryOnline ||
                floatDelta(current.batteryPercent, previous.batteryPercent) >=
                    BoardConfig::kDisplayBatteryPercentDeltaPct ||
                floatDelta(current.battery.loadVoltageV, previous.battery.loadVoltageV) >=
                    BoardConfig::kDisplayVoltageDeltaV;
        default:
            return true;
    }
}

static void resetDisplayRuntimeState(uint8_t index) {
    gDisplayRuntime[index] = {};
}

static void primeDisplayRuntimeState(const TelemetryState &snapshot) {
    const uint32_t nowMs = millis();
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        resetDisplayRuntimeState(i);
        if (!snapshot.displayOnline[i]) continue;
        gDisplayRuntime[i].lastRendered = snapshot;
        gDisplayRuntime[i].lastActivityMs = nowMs;
        gDisplayRuntime[i].hasRendered = true;
        gDisplayRuntime[i].powerSave = false;
    }
}

static void setDisplayOnline(uint8_t index, bool online) {
    takeMutex(gTelemetryMutex);
    gTelemetry.displayOnline[index] = online;
    giveMutex(gTelemetryMutex);
}

static void drawCenteredText(U8G2 &display, int16_t y, const char *text) {
    int16_t x = (display.getDisplayWidth() - display.getStrWidth(text)) / 2;
    display.drawStr(x > 0 ? x : 0, y, text);
}

static void drawRightAlignedText(U8G2 &display, int16_t xRight, int16_t y, const char *text) {
    int16_t x = xRight - display.getStrWidth(text);
    display.drawStr(x > 0 ? x : 0, y, text);
}

static void drawWindow(U8G2 &display, const char *title, const char *status) {
    display.drawFrame(2, 14, 124, 112);
    display.drawBox(3, 15, 122, 10);
    display.setDrawColor(0);
    display.setFont(u8g2_font_5x8_tf);
    display.drawStr(7, 23, title);
    drawRightAlignedText(display, 121, 23, status);
    display.setDrawColor(1);
}

static void drawSingleMetricDisplay(U8G2 &display, const char *title, const char *status,
                                    const char *label, const char *value) {
    drawWindow(display, title, status);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredText(display, 43, label);
    display.setFont(u8g2_font_logisoso16_tf);
    drawCenteredText(display, 82, value);
}

static void drawDualMetricDisplay(U8G2 &display, const char *title, const char *status,
                                  const char *topLabel, const char *topValue,
                                  const char *bottomLabel, const char *bottomValue) {
    drawWindow(display, title, status);
    display.drawHLine(8, 70, 112);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredText(display, 39, topLabel);
    drawCenteredText(display, 87, bottomLabel);
    display.setFont(u8g2_font_logisoso16_tf);
    drawCenteredText(display, 62, topValue);
    drawCenteredText(display, 110, bottomValue);
}

static void drawTemperature(U8G2 &display, const TelemetryState &state) {
    char temperature[24];
    char heatIndex[24];
    snprintf(temperature, sizeof(temperature), "%.1f C", state.weather.temperatureC);
    snprintf(heatIndex, sizeof(heatIndex), "%.1f C", state.weather.heatIndexC);
    drawDualMetricDisplay(display, "ENV TEMP", state.bme280Online ? "LIVE" : "SIM",
                          "TEMPERATURE", temperature, "FEELS LIKE", heatIndex);
}

static void drawHumidity(U8G2 &display, const TelemetryState &state) {
    char value[24];
    snprintf(value, sizeof(value), "%.1f %%", state.weather.humidityPct);
    drawSingleMetricDisplay(display, "ENV HUM", state.bme280Online ? "LIVE" : "SIM",
                            "HUMIDITY", value);
}

static void drawPressure(U8G2 &display, const TelemetryState &state) {
    char value[24];
    snprintf(value, sizeof(value), "%.1f hPa", state.weather.pressureHpa);
    drawSingleMetricDisplay(display, "ENV PRES", state.bme280Online ? "LIVE" : "SIM",
                            "PRESSURE", value);
}

static void drawForecast(U8G2 &display, const TelemetryState &state) {
    char outlook[24];
    char trend[24];

    snprintf(outlook, sizeof(outlook), "%s", forecastDisplayLabel(state.forecast.code));
    if (state.forecast.ready) {
        snprintf(trend, sizeof(trend), "%+.1f HPA", state.forecast.delta3hHpa);
    } else {
        snprintf(trend, sizeof(trend), "3H WAIT");
    }

    drawDualMetricDisplay(display, "FORECAST", state.bme280Online ? "LIVE" : "SIM",
                          "OUTLOOK", outlook, "TREND", trend);
}

static void drawWindSpeed(U8G2 &display, const TelemetryState &state) {
    char speed[24];
    char beaufort[24];
    if (state.windSpeedOnline) {
        snprintf(speed, sizeof(speed), "%.1f m/s", state.wind.speedMs);
        snprintf(beaufort, sizeof(beaufort), "BFT %u", state.wind.beaufort);
    } else {
        snprintf(speed, sizeof(speed), "-- m/s");
        snprintf(beaufort, sizeof(beaufort), "BFT --");
    }
    drawDualMetricDisplay(display, "WIND SPD", state.windSpeedOnline ? "LIVE" : "OFFLINE",
                          "SPEED", speed, "BEAUFORT", beaufort);
}

static void drawWindDirection(U8G2 &display, const TelemetryState &state) {
    char heading[24];
    char direction[24];
    if (state.windDirOnline) {
        snprintf(heading, sizeof(heading), "%.0f deg", normalizeRelativeWindDeg(state.wind.directionDeg));
        snprintf(direction, sizeof(direction), "%s", windRelativeLabel(state.wind.directionDeg));
    } else {
        snprintf(heading, sizeof(heading), "-- deg");
        snprintf(direction, sizeof(direction), "--");
    }
    drawDualMetricDisplay(display, "WIND DIR", state.windDirOnline ? "LIVE" : "OFFLINE",
                          "ANGLE", heading, "SECTOR", direction);
}

static void drawSolarPower(U8G2 &display, const TelemetryState &state) {
    char power[24];
    char voltage[24];
    if (state.solarOnline) {
        snprintf(power, sizeof(power), "%.2f W", state.solar.powerW);
        snprintf(voltage, sizeof(voltage), "%.2f V", state.solar.loadVoltageV);
    } else {
        snprintf(power, sizeof(power), "-- W");
        snprintf(voltage, sizeof(voltage), "-- V");
    }
    drawDualMetricDisplay(display, "SOLAR", state.solarOnline ? "LIVE" : "OFFLINE",
                          "POWER", power, "VOLTAGE", voltage);
}

static void drawBatteryPower(U8G2 &display, const TelemetryState &state) {
    char power[24];
    char voltage[24];
    if (state.batteryOnline) {
        snprintf(power, sizeof(power), "%.2f W", state.battery.powerW);
        snprintf(voltage, sizeof(voltage), "%.2f V", state.battery.loadVoltageV);
    } else {
        snprintf(power, sizeof(power), "-- W");
        snprintf(voltage, sizeof(voltage), "-- V");
    }
    drawDualMetricDisplay(display, "BATTERY", state.batteryOnline ? "LIVE" : "OFFLINE",
                          "POWER", power, "VOLTAGE", voltage);
}

static void drawBatteryLevel(U8G2 &display, const TelemetryState &state) {
    char percent[24];
    char voltage[24];
    if (state.batteryOnline && state.batteryPercent >= 0.0f) {
        snprintf(percent, sizeof(percent), "%.0f %%", state.batteryPercent);
        snprintf(voltage, sizeof(voltage), "%.2f V", state.battery.loadVoltageV);
    } else {
        snprintf(percent, sizeof(percent), "-- %%");
        snprintf(voltage, sizeof(voltage), "-- V");
    }
    drawDualMetricDisplay(display, "BAT LVL", state.batteryOnline ? "LIVE" : "OFFLINE",
                          "PERCENT", percent, "VOLTAGE", voltage);
}

static void drawPowerFlow(U8G2 &display, const TelemetryState &state) {
    char solar[24];
    char battery[24];
    if (state.solarOnline) snprintf(solar, sizeof(solar), "%.2f W", state.solar.powerW);
    else snprintf(solar, sizeof(solar), "-- W");
    if (state.batteryOnline) snprintf(battery, sizeof(battery), "%.2f W", state.battery.powerW);
    else snprintf(battery, sizeof(battery), "-- W");
    drawDualMetricDisplay(display, "POWER", (state.solarOnline || state.batteryOnline) ? "LIVE" : "OFFLINE",
                          "SOLAR", solar, "BATTERY", battery);
}

const DrawFunc kDrawFuncs[kNumDisplays] = {
    drawTemperature, drawHumidity, drawPressure, drawForecast,
    drawWindSpeed, drawWindDirection, drawSolarPower, drawBatteryPower,
    drawBatteryLevel,
};

static bool initDisplay(uint8_t index) {
    const DisplaySlot &slot = kDisplaySlots[index];
    U8G2 &display = *gDisplays[index];
    if (!probeSoftwareI2c(kBusSda[slot.busIndex], kBusScl[slot.busIndex], slot.i2cAddress)) return false;
    display.setI2CAddress(slot.i2cAddress << 1);
    if (!display.begin()) return false;
    display.setPowerSave(0);
    display.clearBuffer();
    display.sendBuffer();
    return true;
}

static void renderDisplay(uint8_t index, const TelemetryState &snapshot) {
    if (!snapshot.displayOnline[index]) return;
    U8G2 &display = *gDisplays[index];
    display.firstPage();
    do {
        kDrawFuncs[index](display, snapshot);
    } while (display.nextPage());
}

static void renderDisplaySlice(uint8_t index, const TelemetryState &snapshot) {
    takeMutex(gDisplayBusMutex);
    if (!snapshot.displayOnline[index]) {
        resetDisplayRuntimeState(index);
        giveMutex(gDisplayBusMutex);
        return;
    }

    DisplayRuntimeState &runtime = gDisplayRuntime[index];
    const uint32_t nowMs = millis();
    bool shouldRefresh = true;

    if (BoardConfig::kDisplayRedrawOnChangeOnly && runtime.hasRendered) {
        shouldRefresh = displayNeedsRefresh(index, snapshot, runtime.lastRendered);
    }

    if (shouldRefresh) {
        if (BoardConfig::kDisplayUsePowerSave && runtime.powerSave) {
            gDisplays[index]->setPowerSave(0);
            runtime.powerSave = false;
        }
        renderDisplay(index, snapshot);
        runtime.lastRendered = snapshot;
        runtime.lastActivityMs = nowMs;
        runtime.hasRendered = true;
    } else if (BoardConfig::kDisplayUsePowerSave &&
               BoardConfig::kDisplayIdleTimeoutMs > 0 &&
               runtime.hasRendered &&
               !runtime.powerSave &&
               (uint32_t)(nowMs - runtime.lastActivityMs) >= BoardConfig::kDisplayIdleTimeoutMs) {
        gDisplays[index]->setPowerSave(1);
        runtime.powerSave = true;
    }
    giveMutex(gDisplayBusMutex);
}

static void renderDisplayFrame(const TelemetryState &snapshot) {
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        renderDisplaySlice(i, snapshot);
        taskDelayMs(1);
    }
}

static uint8_t countOnlineDisplays(const TelemetryState &snapshot) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        if (snapshot.displayOnline[i]) ++count;
    }
    return count;
}
