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
    const bool sunPowerAvailable = powerW > gSettings.solarSunMinPowerW;

    if (previousMode == SolarLightMode::Sun &&
        voltageV >= gSettings.solarSunExitVoltageV &&
        sunPowerAvailable) {
        return SolarLightMode::Sun;
    }

    if (previousMode == SolarLightMode::Dark &&
        voltageV <= gSettings.solarDarkExitVoltageV) {
        return SolarLightMode::Dark;
    }

    if (voltageV <= gSettings.solarDarkEnterVoltageV) return SolarLightMode::Dark;
    if (voltageV >= gSettings.solarSunEnterVoltageV && sunPowerAvailable) {
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

    const uint64_t wakeUs = (uint64_t)gSettings.solarDeepSleepWakeMs * 1000ULL;
    esp_sleep_enable_timer_wakeup(wakeUs);
    Serial.printf("Solar mode dark: entering deep sleep for %lu ms\n",
        (unsigned long)gSettings.solarDeepSleepWakeMs);
    Serial.flush();
    esp_deep_sleep_start();
}

static uint32_t darkWakePostEveryWakeCount() {
    if (gSettings.solarDeepSleepWakeMs == 0) return 1;
    uint32_t wakeCount = gSettings.serverPostDarkMs / gSettings.solarDeepSleepWakeMs;
    if ((gSettings.serverPostDarkMs % gSettings.solarDeepSleepWakeMs) != 0) ++wakeCount;
    return wakeCount == 0 ? 1 : wakeCount;
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
        gDarkTimerWakeCount = 0;
        gDarkWakePostOnly = false;
        gDarkWakePostDue = false;
        gDarkTimerWakeEvaluated = false;
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

    if (gBootedFromTimerWake) {
        if (!gDarkTimerWakeEvaluated) {
            ++gDarkTimerWakeCount;
            const uint32_t postEveryWakeCount = darkWakePostEveryWakeCount();
            gDarkWakePostDue = gSettings.serverPostEnabled &&
                strlen(gSettings.postUrl) > 0 &&
                strlen(gSettings.wifiSsid) > 0 &&
                (gDarkTimerWakeCount % postEveryWakeCount) == 0;
            gDarkWakePostOnly = gDarkWakePostDue;
            gDarkTimerWakeEvaluated = true;
        }
        if (!gDarkWakePostDue) {
            enterSolarDeepSleep();
        }
        gDisplaysForcedOff = true;
        return;
    }

    if (gBootedFromTimerWake ||
        hasElapsedMs(nowMs, gSolarDarkSinceMs, gSettings.solarDarkDeepSleepDelayMs)) {
        enterSolarDeepSleep();
    }
}
