static const char *solarLightModeLabel(SolarLightMode mode) {
    switch (mode) {
        case SolarLightMode::Sun: return "sun";
        case SolarLightMode::Shadow: return "shadow";
        case SolarLightMode::Dark: return "dark";
        case SolarLightMode::Unknown:
        default: return "unknown";
    }
}

static SolarLightMode classifySolarLightMode(float voltageV, float powerW,
                                             SolarLightMode previousMode) {
    if (!isfinite(voltageV) || !isfinite(powerW)) return SolarLightMode::Unknown;
    const bool sunPowerAvailable = powerW > BoardConfig::kSolarSunMinPowerW;

    if (previousMode == SolarLightMode::Sun &&
        voltageV >= BoardConfig::kSolarSunExitVoltageV &&
        sunPowerAvailable) {
        return SolarLightMode::Sun;
    }

    if (previousMode == SolarLightMode::Dark &&
        voltageV <= BoardConfig::kSolarDarkExitVoltageV) {
        return SolarLightMode::Dark;
    }

    if (voltageV <= BoardConfig::kSolarDarkEnterVoltageV) return SolarLightMode::Dark;
    if (voltageV >= BoardConfig::kSolarSunEnterVoltageV && sunPowerAvailable) {
        return SolarLightMode::Sun;
    }
    return SolarLightMode::Shadow;
}

static uint8_t displayContrastForSolarMode(SolarLightMode mode) {
    switch (mode) {
        case SolarLightMode::Sun:
            return BoardConfig::kDisplaySunContrast;
        case SolarLightMode::Dark:
            return BoardConfig::kDisplayDarkGraceContrast;
        case SolarLightMode::Shadow:
        case SolarLightMode::Unknown:
        default:
            return BoardConfig::kDisplayShadowContrast;
    }
}

static uint32_t activeSensorSampleMs() {
    if (!BoardConfig::kEnableSolarPowerPolicy) return BoardConfig::kSensorSampleMs;
    switch (gSolarLightMode) {
        case SolarLightMode::Sun: return BoardConfig::kSensorSampleSunMs;
        case SolarLightMode::Dark: return BoardConfig::kSensorSampleDarkMs;
        case SolarLightMode::Shadow:
        case SolarLightMode::Unknown:
        default: return BoardConfig::kSensorSampleShadowMs;
    }
}

static uint32_t activeWindSampleMs() {
    if (!BoardConfig::kEnableSolarPowerPolicy) return BoardConfig::kWindSampleMs;
    switch (gSolarLightMode) {
        case SolarLightMode::Sun: return BoardConfig::kWindSampleSunMs;
        case SolarLightMode::Dark: return BoardConfig::kWindSampleDarkMs;
        case SolarLightMode::Shadow:
        case SolarLightMode::Unknown:
        default: return BoardConfig::kWindSampleShadowMs;
    }
}

static uint32_t activeDisplayHeartbeatMs() {
    if (!BoardConfig::kEnableSolarPowerPolicy) return BoardConfig::kDisplayHeartbeatMs;
    switch (gSolarLightMode) {
        case SolarLightMode::Sun: return BoardConfig::kDisplayHeartbeatSunMs;
        case SolarLightMode::Dark: return BoardConfig::kDisplayHeartbeatDarkMs;
        case SolarLightMode::Shadow:
        case SolarLightMode::Unknown:
        default: return BoardConfig::kDisplayHeartbeatShadowMs;
    }
}

static uint32_t activeI2cMaintenanceMs() {
    if (!BoardConfig::kEnableSolarPowerPolicy) return BoardConfig::kI2cMaintenanceMs;
    switch (gSolarLightMode) {
        case SolarLightMode::Sun: return BoardConfig::kI2cMaintenanceSunMs;
        case SolarLightMode::Dark: return BoardConfig::kI2cMaintenanceDarkMs;
        case SolarLightMode::Shadow:
        case SolarLightMode::Unknown:
        default: return BoardConfig::kI2cMaintenanceShadowMs;
    }
}

static void setAllDisplaysPowerSave(bool enabled) {
    const TelemetryState snapshot = copyTelemetry();
    takeMutex(gDisplayBusMutex);
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        if (!snapshot.displayOnline[i]) {
            resetDisplayRuntimeState(i);
            continue;
        }
        gDisplays[i]->setPowerSave(enabled ? 1 : 0);
        gDisplayRuntime[i].powerSave = enabled;
    }
    giveMutex(gDisplayBusMutex);
}

static void applyDisplayContrastForSolarMode(SolarLightMode mode, bool force = false) {
    static uint8_t activeContrast = 0;
    static bool hasActiveContrast = false;
    const uint8_t contrast = displayContrastForSolarMode(mode);

    if (!force && hasActiveContrast && activeContrast == contrast) return;

    const TelemetryState snapshot = copyTelemetry();
    takeMutex(gDisplayBusMutex);
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        if (!snapshot.displayOnline[i]) continue;
        gDisplays[i]->setContrast(contrast);
    }
    giveMutex(gDisplayBusMutex);

    activeContrast = contrast;
    hasActiveContrast = true;
}

static void enterSolarDeepSleep() {
    gDisplaysForcedOff = true;
    setAllDisplaysPowerSave(true);

    const uint64_t wakeUs = (uint64_t)BoardConfig::kSolarDeepSleepWakeMs * 1000ULL;
    esp_sleep_enable_timer_wakeup(wakeUs);
    Serial.printf("Solar mode dark: entering deep sleep for %lu ms\n",
        (unsigned long)BoardConfig::kSolarDeepSleepWakeMs);
    Serial.flush();
    esp_deep_sleep_start();
}

static void updateSolarPowerPolicy(const PowerSample &solar, bool solarOnline) {
    if (!BoardConfig::kEnableSolarPowerPolicy) return;

    const uint32_t nowMs = millis();
    const SolarLightMode previousMode = gSolarLightMode;
    SolarLightMode nextMode = SolarLightMode::Unknown;

    if (solarOnline && isPowerSampleValid(solar)) {
        nextMode = classifySolarLightMode(solar.loadVoltageV, solar.powerW, previousMode);
    }

    if (nextMode == SolarLightMode::Unknown) {
        gSolarLightMode = nextMode;
        gSolarDarkSinceMs = 0;
        gDisplaysForcedOff = false;
        gBootedFromTimerWake = false;
        applyDisplayContrastForSolarMode(nextMode);
        return;
    }

    gSolarLightMode = nextMode;
    applyDisplayContrastForSolarMode(nextMode);

    if (nextMode != previousMode) {
        Serial.printf("Solar light mode: %s (%.2f V)\n",
            solarLightModeLabel(nextMode), solar.loadVoltageV);
        requestDisplayRefresh(kDisplayMaskAll);
    }

    if (nextMode != SolarLightMode::Dark) {
        gSolarDarkSinceMs = 0;
        gBootedFromTimerWake = false;
        if (gDisplaysForcedOff) {
            gDisplaysForcedOff = false;
            setAllDisplaysPowerSave(false);
            requestDisplayRefresh(kDisplayMaskAll);
        }
        return;
    }

    if (gSolarDarkSinceMs == 0) {
        gSolarDarkSinceMs = nowMs;
    }

    if (gBootedFromTimerWake ||
        hasElapsedMs(nowMs, gSolarDarkSinceMs, BoardConfig::kSolarDarkDeepSleepDelayMs)) {
        enterSolarDeepSleep();
    }
}
