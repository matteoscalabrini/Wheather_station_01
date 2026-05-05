#include "secrets.h"

static void copySetting(char *dest, size_t destSize, const String &value) {
    if (destSize == 0) return;
    const size_t copyLen = value.length() < (destSize - 1) ? value.length() : (destSize - 1);
    memcpy(dest, value.c_str(), copyLen);
    dest[copyLen] = '\0';
}

static void copySetting(char *dest, size_t destSize, const char *value) {
    copySetting(dest, destSize, String(value != nullptr ? value : ""));
}

static void loadDefaultRuntimeSettings() {
    gSettings.solarSunEnterVoltageV = BoardConfig::kSolarSunEnterVoltageV;
    gSettings.solarSunExitVoltageV = BoardConfig::kSolarSunExitVoltageV;
    gSettings.solarSunMinPowerW = BoardConfig::kSolarSunMinPowerW;
    gSettings.solarDarkEnterVoltageV = BoardConfig::kSolarDarkEnterVoltageV;
    gSettings.solarDarkExitVoltageV = BoardConfig::kSolarDarkExitVoltageV;
    gSettings.solarDarkDeepSleepDelayMs = BoardConfig::kSolarDarkDeepSleepDelayMs;
    gSettings.solarDeepSleepWakeMs = BoardConfig::kSolarDeepSleepWakeMs;
    gSettings.serverPostSunMs = SECRET_SERVER_POST_SUN_MS;
    gSettings.serverPostShadowMs = SECRET_SERVER_POST_SHADOW_MS;
    gSettings.serverPostDarkMs = SECRET_SERVER_POST_DARK_MS;
    gSettings.remoteConfigPullMs = BoardConfig::kRemoteConfigPullMs;
    gSettings.remoteFirmwareCheckMs = BoardConfig::kRemoteFirmwareCheckMs;
    gSettings.batteryPercentEmptyVoltageV = BoardConfig::kBatteryPercentEmptyVoltageV;
    gSettings.batteryPercentFullVoltageV = BoardConfig::kBatteryPercentFullVoltageV;
    gSettings.batteryLockoutEnterVoltageV = BoardConfig::kBatteryLockoutEnterVoltageV;
    gSettings.batteryLockoutResumeVoltageV = BoardConfig::kBatteryLockoutResumeVoltageV;
    gSettings.batteryLockoutWakeMs = BoardConfig::kBatteryLockoutWakeMs;
    gSettings.serverPostEnabled = SECRET_SERVER_POST_ENABLED;
    gSettings.wifiApAlways = BoardConfig::kWifiDebugForceApAlways;
    copySetting(gSettings.adminPassword, sizeof(gSettings.adminPassword),
                BoardConfig::kDefaultAdminPassword);
    copySetting(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), "");
    copySetting(gSettings.wifiPassword, sizeof(gSettings.wifiPassword), "");
    copySetting(gSettings.postUrl, sizeof(gSettings.postUrl), SECRET_POST_URL);
    copySetting(gSettings.postToken, sizeof(gSettings.postToken), SECRET_POST_TOKEN);
    copySetting(gSettings.spiffsVersion, sizeof(gSettings.spiffsVersion),
                BoardConfig::kSpiffsVersion);
}

static void loadRuntimeSettings() {
    loadDefaultRuntimeSettings();

    if (!gSettingsPrefs.begin("weather", true)) {
        Serial.println("Settings: using defaults (NVS read failed)");
        return;
    }

    gSettings.solarSunEnterVoltageV =
        gSettingsPrefs.getFloat("sunEnterV", gSettings.solarSunEnterVoltageV);
    gSettings.solarSunExitVoltageV =
        gSettingsPrefs.getFloat("sunExitV", gSettings.solarSunExitVoltageV);
    gSettings.solarSunMinPowerW =
        gSettingsPrefs.getFloat("sunMinW", gSettings.solarSunMinPowerW);
    gSettings.solarDarkEnterVoltageV =
        gSettingsPrefs.getFloat("darkEnterV", gSettings.solarDarkEnterVoltageV);
    gSettings.solarDarkExitVoltageV =
        gSettingsPrefs.getFloat("darkExitV", gSettings.solarDarkExitVoltageV);
    gSettings.solarDarkDeepSleepDelayMs =
        gSettingsPrefs.getUInt("darkDelayMs", gSettings.solarDarkDeepSleepDelayMs);
    gSettings.solarDeepSleepWakeMs =
        gSettingsPrefs.getUInt("sleepWakeMs", gSettings.solarDeepSleepWakeMs);
    gSettings.serverPostSunMs =
        gSettingsPrefs.getUInt("postSunMs", gSettings.serverPostSunMs);
    gSettings.serverPostShadowMs =
        gSettingsPrefs.getUInt("postShadowMs", gSettings.serverPostShadowMs);
    gSettings.serverPostDarkMs =
        gSettingsPrefs.getUInt("postDarkMs", gSettings.serverPostDarkMs);
    gSettings.remoteConfigPullMs =
        gSettingsPrefs.getUInt("remoteCfgMs", gSettings.remoteConfigPullMs);
    gSettings.remoteFirmwareCheckMs =
        gSettingsPrefs.getUInt("fwCheckMs", gSettings.remoteFirmwareCheckMs);
    gSettings.batteryPercentEmptyVoltageV =
        gSettingsPrefs.getFloat("batEmptyV", gSettings.batteryPercentEmptyVoltageV);
    gSettings.batteryPercentFullVoltageV =
        gSettingsPrefs.getFloat("batFullV", gSettings.batteryPercentFullVoltageV);
    gSettings.batteryLockoutEnterVoltageV =
        gSettingsPrefs.getFloat("batLockEnV", gSettings.batteryLockoutEnterVoltageV);
    gSettings.batteryLockoutResumeVoltageV =
        gSettingsPrefs.getFloat("batLockRsV", gSettings.batteryLockoutResumeVoltageV);
    gSettings.batteryLockoutWakeMs =
        gSettingsPrefs.getUInt("batLockWake", gSettings.batteryLockoutWakeMs);
    gSettings.serverPostEnabled =
        gSettingsPrefs.getBool("postEnabled", gSettings.serverPostEnabled);
    gSettings.wifiApAlways =
        gSettingsPrefs.getBool("apAlways", gSettings.wifiApAlways);
    copySetting(gSettings.adminPassword, sizeof(gSettings.adminPassword),
                gSettingsPrefs.getString("adminPass", gSettings.adminPassword));
    copySetting(gSettings.wifiSsid, sizeof(gSettings.wifiSsid),
                gSettingsPrefs.getString("wifiSsid", gSettings.wifiSsid));
    copySetting(gSettings.wifiPassword, sizeof(gSettings.wifiPassword),
                gSettingsPrefs.getString("wifiPass", gSettings.wifiPassword));
    copySetting(gSettings.postUrl, sizeof(gSettings.postUrl),
                gSettingsPrefs.getString("postUrl", gSettings.postUrl));
    copySetting(gSettings.postToken, sizeof(gSettings.postToken),
                gSettingsPrefs.getString("postToken", gSettings.postToken));
    copySetting(gSettings.spiffsVersion, sizeof(gSettings.spiffsVersion),
                gSettingsPrefs.getString("spiffsVer", gSettings.spiffsVersion));
    gSettingsPrefs.end();
}

static bool saveRuntimeSettings() {
    if (!gSettingsPrefs.begin("weather", false)) {
        return false;
    }

    gSettingsPrefs.putFloat("sunEnterV", gSettings.solarSunEnterVoltageV);
    gSettingsPrefs.putFloat("sunExitV", gSettings.solarSunExitVoltageV);
    gSettingsPrefs.putFloat("sunMinW", gSettings.solarSunMinPowerW);
    gSettingsPrefs.putFloat("darkEnterV", gSettings.solarDarkEnterVoltageV);
    gSettingsPrefs.putFloat("darkExitV", gSettings.solarDarkExitVoltageV);
    gSettingsPrefs.putUInt("darkDelayMs", gSettings.solarDarkDeepSleepDelayMs);
    gSettingsPrefs.putUInt("sleepWakeMs", gSettings.solarDeepSleepWakeMs);
    gSettingsPrefs.putUInt("postSunMs", gSettings.serverPostSunMs);
    gSettingsPrefs.putUInt("postShadowMs", gSettings.serverPostShadowMs);
    gSettingsPrefs.putUInt("postDarkMs", gSettings.serverPostDarkMs);
    gSettingsPrefs.putUInt("remoteCfgMs", gSettings.remoteConfigPullMs);
    gSettingsPrefs.putUInt("fwCheckMs", gSettings.remoteFirmwareCheckMs);
    gSettingsPrefs.putFloat("batEmptyV", gSettings.batteryPercentEmptyVoltageV);
    gSettingsPrefs.putFloat("batFullV", gSettings.batteryPercentFullVoltageV);
    gSettingsPrefs.putFloat("batLockEnV", gSettings.batteryLockoutEnterVoltageV);
    gSettingsPrefs.putFloat("batLockRsV", gSettings.batteryLockoutResumeVoltageV);
    gSettingsPrefs.putUInt("batLockWake", gSettings.batteryLockoutWakeMs);
    gSettingsPrefs.putBool("postEnabled", gSettings.serverPostEnabled);
    gSettingsPrefs.putBool("apAlways", gSettings.wifiApAlways);
    gSettingsPrefs.putString("adminPass", gSettings.adminPassword);
    gSettingsPrefs.putString("wifiSsid", gSettings.wifiSsid);
    gSettingsPrefs.putString("wifiPass", gSettings.wifiPassword);
    gSettingsPrefs.putString("postUrl", gSettings.postUrl);
    gSettingsPrefs.putString("postToken", gSettings.postToken);
    gSettingsPrefs.putString("spiffsVer", gSettings.spiffsVersion);
    gSettingsPrefs.end();
    return true;
}

static bool validRuntimeSettings(const RuntimeSettings &settings) {
    return settings.solarSunExitVoltageV < settings.solarSunEnterVoltageV &&
        settings.solarDarkEnterVoltageV < settings.solarDarkExitVoltageV &&
        settings.solarDarkExitVoltageV < settings.solarSunEnterVoltageV &&
        settings.solarSunMinPowerW >= 0.0f &&
        isfinite(settings.batteryPercentEmptyVoltageV) &&
        isfinite(settings.batteryPercentFullVoltageV) &&
        settings.batteryPercentEmptyVoltageV < settings.batteryPercentFullVoltageV &&
        isfinite(settings.batteryLockoutEnterVoltageV) &&
        isfinite(settings.batteryLockoutResumeVoltageV) &&
        settings.batteryLockoutEnterVoltageV > 0.0f &&
        settings.batteryLockoutEnterVoltageV < settings.batteryLockoutResumeVoltageV &&
        settings.solarDeepSleepWakeMs >= 60000UL &&
        settings.batteryLockoutWakeMs >= 60000UL &&
        settings.serverPostSunMs >= 60000UL &&
        settings.serverPostShadowMs >= 60000UL &&
        settings.serverPostDarkMs >= 60000UL &&
        settings.remoteConfigPullMs >= 60000UL &&
        settings.remoteFirmwareCheckMs >= 300000UL &&
        strlen(settings.spiffsVersion) > 0 &&
        strlen(settings.adminPassword) > 0;
}

static bool validRuntimeSettings() {
    return validRuntimeSettings(gSettings);
}

static void repairRuntimeSettingsPreservingIdentity() {
    const RuntimeSettings previous = gSettings;
    loadDefaultRuntimeSettings();

    if (strlen(previous.adminPassword) > 0) {
        copySetting(gSettings.adminPassword, sizeof(gSettings.adminPassword),
                    previous.adminPassword);
    }
    copySetting(gSettings.wifiSsid, sizeof(gSettings.wifiSsid),
                previous.wifiSsid);
    copySetting(gSettings.wifiPassword, sizeof(gSettings.wifiPassword),
                previous.wifiPassword);
    copySetting(gSettings.postUrl, sizeof(gSettings.postUrl),
                previous.postUrl);
    copySetting(gSettings.postToken, sizeof(gSettings.postToken),
                previous.postToken);
    if (strlen(previous.spiffsVersion) > 0) {
        copySetting(gSettings.spiffsVersion, sizeof(gSettings.spiffsVersion),
                    previous.spiffsVersion);
    }
}
