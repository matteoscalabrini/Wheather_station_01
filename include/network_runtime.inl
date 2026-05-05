static String jsonBool(bool value) {
    return value ? "true" : "false";
}

static String jsonString(const String &value) {
    String out = "\"";
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

static String jsonFloat(float value, uint8_t digits = 2) {
    if (!isfinite(value)) return "null";
    return String(value, (unsigned int)digits);
}

static void setWifiMessage(const char *message) {
    copySetting(gNetworkRuntime.wifiMessage, sizeof(gNetworkRuntime.wifiMessage),
                message != nullptr ? message : "");
}

static void setPostMessage(const char *message) {
    copySetting(gNetworkRuntime.lastPostMessage, sizeof(gNetworkRuntime.lastPostMessage),
                message != nullptr ? message : "");
}

static void setRemoteConfigMessage(const char *message) {
    copySetting(gNetworkRuntime.remoteConfigMessage, sizeof(gNetworkRuntime.remoteConfigMessage),
                message != nullptr ? message : "");
}

static void setFirmwareMessage(const char *message) {
    copySetting(gNetworkRuntime.firmwareMessage, sizeof(gNetworkRuntime.firmwareMessage),
                message != nullptr ? message : "");
}

static void configureHttpClient(HTTPClient &http) {
    http.setConnectTimeout((int32_t)BoardConfig::kHttpConnectTimeoutMs);
    http.setTimeout(BoardConfig::kHttpReadTimeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
}

struct ScopedBoolFlag {
    explicit ScopedBoolFlag(bool &target) : flag(target) {
        flag = true;
    }

    ~ScopedBoolFlag() {
        flag = false;
    }

    bool &flag;
};

static const char *managementHttpFailureMessage(const char *prefix, int code) {
    if (code == 401 || code == 403) {
        return strcmp(prefix, "firmware") == 0 ? "firmware_unauthorized" :
            "remote_config_unauthorized";
    }
    if (code == 404) {
        return strcmp(prefix, "firmware") == 0 ? "firmware_not_found" :
            "remote_config_not_found";
    }
    if (code == HTTPC_ERROR_READ_TIMEOUT || code == HTTP_CODE_GATEWAY_TIMEOUT) {
        return strcmp(prefix, "firmware") == 0 ? "firmware_timeout" :
            "remote_config_timeout";
    }
    if (code <= 0) {
        return strcmp(prefix, "firmware") == 0 ? "firmware_transport_failed" :
            "remote_config_transport_failed";
    }
    return strcmp(prefix, "firmware") == 0 ? "firmware_http_failed" :
        "remote_config_http_failed";
}

static void scheduleFirmwareRetrySoon() {
    const uint32_t intervalMs = gSettings.remoteFirmwareCheckMs;
    const uint32_t retryMs = BoardConfig::kRemoteFailureRetryMs;
    if (intervalMs <= retryMs) return;
    const uint32_t backdateMs = intervalMs - retryMs;
    const uint32_t nowMs = millis();
    gNetworkRuntime.lastFirmwareCheckMs = nowMs <= backdateMs ? 1UL : nowMs - backdateMs;
}

static const char *wifiStatusLabel(wl_status_t status) {
    switch (status) {
        case WL_CONNECTED: return "connected";
        case WL_NO_SSID_AVAIL: return "ssid_unavailable";
        case WL_CONNECT_FAILED: return "connect_failed";
        case WL_CONNECTION_LOST: return "connection_lost";
        case WL_DISCONNECTED: return "disconnected";
        case WL_IDLE_STATUS: return "idle";
        case WL_SCAN_COMPLETED: return "scan_completed";
        default: return "unknown";
    }
}

static bool localAccessPointShouldStayOn() {
    return gSettings.wifiApAlways || strlen(gSettings.wifiSsid) == 0;
}

static bool recoveryApAllowedForCurrentMode() {
    return BoardConfig::kWifiRecoveryApOnShadowStaFailure &&
        BoardConfig::kWifiRecoveryApMs > 0 &&
        gSolarLightMode != SolarLightMode::Dark &&
        strlen(gSettings.wifiSsid) > 0;
}

static bool recoveryApWindowActive(uint32_t nowMs) {
    if (!gNetworkRuntime.recoveryApActive) return false;
    if (!recoveryApAllowedForCurrentMode()) {
        gRecoveryApLastEndMs = nowMs;
        gNetworkRuntime.recoveryApActive = false;
        return false;
    }
    if ((uint32_t)(nowMs - gNetworkRuntime.recoveryApStartedMs) >=
        BoardConfig::kWifiRecoveryApMs) {
        gRecoveryApLastEndMs = nowMs;
        gNetworkRuntime.recoveryApActive = false;
        return false;
    }
    return true;
}

static uint32_t recoveryApRemainingMs(uint32_t nowMs) {
    if (!recoveryApWindowActive(nowMs)) return 0;
    const uint32_t elapsedMs = (uint32_t)(nowMs - gNetworkRuntime.recoveryApStartedMs);
    return elapsedMs >= BoardConfig::kWifiRecoveryApMs ?
        0 : BoardConfig::kWifiRecoveryApMs - elapsedMs;
}

static bool accessPointShouldStayOn(uint32_t nowMs) {
    return localAccessPointShouldStayOn() || recoveryApWindowActive(nowMs);
}

static void clearRecoveryAp() {
    if (gNetworkRuntime.recoveryApActive) gRecoveryApLastEndMs = millis();
    gNetworkRuntime.recoveryApActive = false;
    gNetworkRuntime.recoveryApStartedMs = 0;
}

static void resetRecoveryApBackoff() {
    clearRecoveryAp();
    gRecoveryApConsecutiveLaunches = 0;
    gRecoveryApLastEndMs = 0;
}

static void sendJson(int code, const String &payload) {
    gWebServer.sendHeader("Access-Control-Allow-Origin", "*");
    gWebServer.send(code, "application/json", payload);
}

static void sendText(int code, const String &payload) {
    gWebServer.sendHeader("Access-Control-Allow-Origin", "*");
    gWebServer.send(code, "text/plain", payload);
}

static bool adminAuthorized() {
    String password;
    if (gWebServer.hasArg("password")) {
        password = gWebServer.arg("password");
    } else if (gWebServer.hasHeader("X-Admin-Password")) {
        password = gWebServer.header("X-Admin-Password");
    }
    return password.equals(gSettings.adminPassword);
}

static bool requireAdmin() {
    if (adminAuthorized()) return true;
    sendJson(401, "{\"success\":false,\"error\":\"unauthorized\"}");
    return false;
}

static String contentTypeForPath(const String &path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".ico")) return "image/x-icon";
    return "text/plain";
}

static bool serveSpiffsPath(const String &path) {
    if (!gNetworkRuntime.spiffsReady || !SPIFFS.exists(path)) return false;
    File file = SPIFFS.open(path, "r");
    if (!file) return false;
    gWebServer.streamFile(file, contentTypeForPath(path));
    file.close();
    return true;
}

static void ensureWebServerStarted() {
    if (gNetworkRuntime.webReady) return;
    gWebServer.begin();
    gNetworkRuntime.webReady = true;
}

static void handleWebClientIfStarted() {
    if (!gNetworkRuntime.webReady) return;
    gWebServer.handleClient();
}

static void processCaptiveDnsIfNeeded() {
    if (!gNetworkRuntime.apEnabled) return;
    gDnsServer.processNextRequest();
}

static String buildDisplayValueJson(uint8_t index, const TelemetryState &state) {
    String payload = "{";
    switch (index) {
        case 0:
            payload += "\"label\":\"ENV TEMP\",";
            payload += "\"primary\":" + jsonFloat(state.weather.temperatureC, 1) + ",";
            payload += "\"primaryUnit\":\"C\",";
            payload += "\"secondary\":" + jsonFloat(state.weather.heatIndexC, 1) + ",";
            payload += "\"secondaryLabel\":\"FEELS LIKE\",";
            payload += "\"online\":" + jsonBool(state.bme280Online);
            break;
        case 1:
            payload += "\"label\":\"ENV HUM\",";
            payload += "\"primary\":" + jsonFloat(state.weather.humidityPct, 1) + ",";
            payload += "\"primaryUnit\":\"%\",";
            payload += "\"online\":" + jsonBool(state.bme280Online);
            break;
        case 2:
            payload += "\"label\":\"ENV PRES\",";
            payload += "\"primary\":" + jsonFloat(state.weather.pressureHpa, 1) + ",";
            payload += "\"primaryUnit\":\"hPa\",";
            payload += "\"online\":" + jsonBool(state.bme280Online);
            break;
        case 3:
            payload += "\"label\":\"FORECAST\",";
            payload += "\"primary\":" + jsonString(forecastDisplayLabel(state.forecast.code)) + ",";
            payload += "\"secondary\":" + jsonFloat(state.forecast.delta3hHpa, 1) + ",";
            payload += "\"secondaryUnit\":\"hPa\",";
            payload += "\"online\":" + jsonBool(state.bme280Online && state.forecast.ready);
            break;
        case 4:
            payload += "\"label\":\"WIND SPD\",";
            payload += "\"primary\":" + jsonFloat(state.wind.speedMs, 1) + ",";
            payload += "\"primaryUnit\":\"m/s\",";
            payload += "\"secondary\":" + String(state.wind.beaufort) + ",";
            payload += "\"secondaryLabel\":\"BFT\",";
            payload += "\"online\":" + jsonBool(state.windSpeedOnline);
            break;
        case 5:
            payload += "\"label\":\"WIND DIR\",";
            payload += "\"primary\":" + jsonFloat(normalizeRelativeWindDeg(state.wind.directionDeg), 0) + ",";
            payload += "\"primaryUnit\":\"deg\",";
            payload += "\"secondary\":" + jsonString(windRelativeLabel(state.wind.directionDeg)) + ",";
            payload += "\"online\":" + jsonBool(state.windDirOnline);
            break;
        case 6:
            payload += "\"label\":\"SOLAR\",";
            payload += "\"primary\":" + jsonFloat(state.solar.powerW, 2) + ",";
            payload += "\"primaryUnit\":\"W\",";
            payload += "\"secondary\":" + jsonFloat(state.solar.loadVoltageV, 2) + ",";
            payload += "\"secondaryUnit\":\"V\",";
            payload += "\"online\":" + jsonBool(state.solarOnline);
            break;
        case 7:
            payload += "\"label\":\"BATTERY\",";
            payload += "\"primary\":" + jsonFloat(state.battery.powerW, 2) + ",";
            payload += "\"primaryUnit\":\"W\",";
            payload += "\"secondary\":" + jsonFloat(state.battery.loadVoltageV, 2) + ",";
            payload += "\"secondaryUnit\":\"V\",";
            payload += "\"online\":" + jsonBool(state.batteryOnline);
            break;
        case 8:
        default:
            payload += "\"label\":\"BAT LVL\",";
            payload += "\"primary\":" + jsonFloat(state.batteryPercent, 0) + ",";
            payload += "\"primaryUnit\":\"%\",";
            payload += "\"secondary\":" + jsonFloat(state.battery.loadVoltageV, 2) + ",";
            payload += "\"secondaryUnit\":\"V\",";
            payload += "\"online\":" + jsonBool(state.batteryOnline && state.batteryPercent >= 0.0f);
            break;
    }
    payload += "}";
    return payload;
}

static String buildTelemetryJson() {
    const TelemetryState state = copyTelemetry();
    const uint32_t nowMs = millis();
    const bool recoveryAp = recoveryApWindowActive(nowMs);
    const uint32_t recoveryRemainingMs = recoveryApRemainingMs(nowMs);
    String payload;
    payload.reserve(6144);
    payload = "{";
    payload += "\"board\":" + jsonString(BoardConfig::kBoardName) + ",";
    payload += "\"firmwareVersion\":" + jsonString(BoardConfig::kFirmwareVersion) + ",";
    payload += "\"spiffsVersion\":" + jsonString(gSettings.spiffsVersion) + ",";
    payload += "\"uptimeMs\":" + String(millis()) + ",";
    payload += "\"cpuFrequencyMhz\":" + String(getCpuFrequencyMhz()) + ",";
    payload += "\"solarMode\":" + jsonString(solarLightModeLabel(gSolarLightMode)) + ",";
    payload += "\"displaysForcedOff\":" + jsonBool(gDisplaysForcedOff) + ",";
    payload += "\"wifi\":{";
    payload += "\"enabled\":" + jsonBool(gNetworkRuntime.wifiEnabled) + ",";
    payload += "\"ap\":" + jsonBool(gNetworkRuntime.apEnabled) + ",";
    payload += "\"sta\":" + jsonBool(gNetworkRuntime.staConnected) + ",";
    payload += "\"recoveryAp\":" + jsonBool(recoveryAp) + ",";
    payload += "\"recoveryApRemainingMs\":" + String(recoveryRemainingMs) + ",";
    payload += "\"recoveryApLaunches\":" + String(gRecoveryApConsecutiveLaunches) + ",";
    payload += "\"recoveryApMaxLaunches\":" + String(BoardConfig::kWifiRecoveryApMaxConsecutive) + ",";
    payload += "\"configured\":" + jsonBool(strlen(gSettings.wifiSsid) > 0) + ",";
    payload += "\"targetSsid\":" + jsonString(gSettings.wifiSsid) + ",";
    payload += "\"passwordSet\":" + jsonBool(strlen(gSettings.wifiPassword) > 0) + ",";
    payload += "\"ssid\":" + jsonString(gNetworkRuntime.staConnected ? WiFi.SSID() : String("")) + ",";
    payload += "\"ip\":" + jsonString(gNetworkRuntime.staConnected ? WiFi.localIP().toString() : String("")) + ",";
    payload += "\"apIp\":" + jsonString(gNetworkRuntime.apEnabled ? WiFi.softAPIP().toString() : String("")) + ",";
    payload += "\"rssi\":" + String(gNetworkRuntime.staConnected ? WiFi.RSSI() : 0) + ",";
    payload += "\"status\":" + String(WiFi.status()) + ",";
    payload += "\"statusLabel\":" + jsonString(wifiStatusLabel(WiFi.status())) + ",";
    payload += "\"message\":" + jsonString(gNetworkRuntime.wifiMessage) + ",";
    payload += "\"lastWifiAttemptMs\":" + String(gNetworkRuntime.lastWifiAttemptMs) + ",";
    payload += "\"posting\":" + jsonBool(gNetworkRuntime.posting) + ",";
    payload += "\"lastPostAttemptMs\":" + String(gNetworkRuntime.lastPostAttemptMs) + ",";
    payload += "\"nextPostAllowedMs\":" + String(gNetworkRuntime.nextPostAllowedMs) + ",";
    payload += "\"lastPostSuccessMs\":" + String(gNetworkRuntime.lastPostSuccessMs) + ",";
    payload += "\"lastPostCode\":" + String(gNetworkRuntime.lastPostHttpCode) + ",";
    payload += "\"lastPostMessage\":" + jsonString(gNetworkRuntime.lastPostMessage) + ",";
    payload += "\"lastRemoteConfigPullMs\":" + String(gNetworkRuntime.lastRemoteConfigPullMs) + ",";
    payload += "\"remoteConfigHttpCode\":" + String(gNetworkRuntime.remoteConfigHttpCode) + ",";
    payload += "\"remoteConfigMessage\":" + jsonString(gNetworkRuntime.remoteConfigMessage) + ",";
    payload += "\"lastFirmwareCheckMs\":" + String(gNetworkRuntime.lastFirmwareCheckMs) + ",";
    payload += "\"firmwareHttpCode\":" + String(gNetworkRuntime.firmwareHttpCode) + ",";
    payload += "\"otaHttpCode\":" + String(gNetworkRuntime.otaHttpCode) + ",";
    payload += "\"firmwareMessage\":" + jsonString(gNetworkRuntime.firmwareMessage);
    payload += "},";
    payload += "\"config\":{";
    payload += "\"solarSunEnterVoltageV\":" + jsonFloat(gSettings.solarSunEnterVoltageV, 2) + ",";
    payload += "\"solarSunExitVoltageV\":" + jsonFloat(gSettings.solarSunExitVoltageV, 2) + ",";
    payload += "\"solarSunMinPowerW\":" + jsonFloat(gSettings.solarSunMinPowerW, 2) + ",";
    payload += "\"solarDarkEnterVoltageV\":" + jsonFloat(gSettings.solarDarkEnterVoltageV, 2) + ",";
    payload += "\"solarDarkExitVoltageV\":" + jsonFloat(gSettings.solarDarkExitVoltageV, 2) + ",";
    payload += "\"solarDarkDeepSleepDelayMs\":" + String(gSettings.solarDarkDeepSleepDelayMs) + ",";
    payload += "\"solarDeepSleepWakeMs\":" + String(gSettings.solarDeepSleepWakeMs) + ",";
    payload += "\"serverPostSunMs\":" + String(gSettings.serverPostSunMs) + ",";
    payload += "\"serverPostShadowMs\":" + String(gSettings.serverPostShadowMs) + ",";
    payload += "\"serverPostDarkMs\":" + String(gSettings.serverPostDarkMs) + ",";
    payload += "\"remoteConfigPullMs\":" + String(gSettings.remoteConfigPullMs) + ",";
    payload += "\"remoteFirmwareCheckMs\":" + String(gSettings.remoteFirmwareCheckMs) + ",";
    payload += "\"batteryPercentEmptyVoltageV\":" + jsonFloat(gSettings.batteryPercentEmptyVoltageV, 2) + ",";
    payload += "\"batteryPercentFullVoltageV\":" + jsonFloat(gSettings.batteryPercentFullVoltageV, 2) + ",";
    payload += "\"batteryLockoutEnterVoltageV\":" + jsonFloat(gSettings.batteryLockoutEnterVoltageV, 2) + ",";
    payload += "\"batteryLockoutResumeVoltageV\":" + jsonFloat(gSettings.batteryLockoutResumeVoltageV, 2) + ",";
    payload += "\"batteryLockoutWakeMs\":" + String(gSettings.batteryLockoutWakeMs) + ",";
    payload += "\"batteryLockoutLatched\":" + jsonBool(gBatteryLockoutLatched) + ",";
    payload += "\"serverPostEnabled\":" + jsonBool(gSettings.serverPostEnabled) + ",";
    payload += "\"wifiApAlways\":" + jsonBool(gSettings.wifiApAlways);
    payload += "},";
    payload += "\"sensors\":{";
    payload += "\"bme280Online\":" + jsonBool(state.bme280Online) + ",";
    payload += "\"solarOnline\":" + jsonBool(state.solarOnline) + ",";
    payload += "\"batteryOnline\":" + jsonBool(state.batteryOnline) + ",";
    payload += "\"windSpeedOnline\":" + jsonBool(state.windSpeedOnline) + ",";
    payload += "\"windDirOnline\":" + jsonBool(state.windDirOnline);
    payload += "},";
    payload += "\"displays\":[";
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        if (i > 0) payload += ",";
        payload += buildDisplayValueJson(i, state);
    }
    payload += "]";
    payload += "}";
    return payload;
}

static String buildConfigJson(bool includeSuccess = false, bool success = true) {
    String payload = "{";
    if (includeSuccess) payload += "\"success\":" + jsonBool(success) + ",";
    payload += "\"solarSunEnterVoltageV\":" + jsonFloat(gSettings.solarSunEnterVoltageV, 2) + ",";
    payload += "\"solarSunExitVoltageV\":" + jsonFloat(gSettings.solarSunExitVoltageV, 2) + ",";
    payload += "\"solarSunMinPowerW\":" + jsonFloat(gSettings.solarSunMinPowerW, 2) + ",";
    payload += "\"solarDarkEnterVoltageV\":" + jsonFloat(gSettings.solarDarkEnterVoltageV, 2) + ",";
    payload += "\"solarDarkExitVoltageV\":" + jsonFloat(gSettings.solarDarkExitVoltageV, 2) + ",";
    payload += "\"solarDarkDeepSleepDelayMs\":" + String(gSettings.solarDarkDeepSleepDelayMs) + ",";
    payload += "\"solarDeepSleepWakeMs\":" + String(gSettings.solarDeepSleepWakeMs) + ",";
    payload += "\"serverPostSunMs\":" + String(gSettings.serverPostSunMs) + ",";
    payload += "\"serverPostShadowMs\":" + String(gSettings.serverPostShadowMs) + ",";
    payload += "\"serverPostDarkMs\":" + String(gSettings.serverPostDarkMs) + ",";
    payload += "\"remoteConfigPullMs\":" + String(gSettings.remoteConfigPullMs) + ",";
    payload += "\"remoteFirmwareCheckMs\":" + String(gSettings.remoteFirmwareCheckMs) + ",";
    payload += "\"batteryPercentEmptyVoltageV\":" + jsonFloat(gSettings.batteryPercentEmptyVoltageV, 2) + ",";
    payload += "\"batteryPercentFullVoltageV\":" + jsonFloat(gSettings.batteryPercentFullVoltageV, 2) + ",";
    payload += "\"batteryLockoutEnterVoltageV\":" + jsonFloat(gSettings.batteryLockoutEnterVoltageV, 2) + ",";
    payload += "\"batteryLockoutResumeVoltageV\":" + jsonFloat(gSettings.batteryLockoutResumeVoltageV, 2) + ",";
    payload += "\"batteryLockoutWakeMs\":" + String(gSettings.batteryLockoutWakeMs) + ",";
    payload += "\"serverPostEnabled\":" + jsonBool(gSettings.serverPostEnabled) + ",";
    payload += "\"wifiApAlways\":" + jsonBool(gSettings.wifiApAlways) + ",";
    payload += "\"wifiSsid\":" + jsonString(gSettings.wifiSsid) + ",";
    payload += "\"wifiPasswordSet\":" + jsonBool(strlen(gSettings.wifiPassword) > 0) + ",";
    payload += "\"postUrl\":" + jsonString(gSettings.postUrl) + ",";
    payload += "\"postTokenSet\":" + jsonBool(strlen(gSettings.postToken) > 0) + ",";
    payload += "\"adminPasswordSet\":" + jsonBool(strlen(gSettings.adminPassword) > 0);
    payload += "}";
    return payload;
}

static bool readFloatArg(const char *name, float &value) {
    if (!gWebServer.hasArg(name)) return false;
    value = gWebServer.arg(name).toFloat();
    return isfinite(value);
}

static bool readUIntArg(const char *name, uint32_t &value) {
    if (!gWebServer.hasArg(name)) return false;
    const long parsed = gWebServer.arg(name).toInt();
    if (parsed < 0) return false;
    value = (uint32_t)parsed;
    return true;
}

static bool readBoolArg(const char *name, bool &value) {
    if (!gWebServer.hasArg(name)) return false;
    String raw = gWebServer.arg(name);
    raw.trim();
    raw.toLowerCase();
    if (raw == "1" || raw == "true" || raw == "on" || raw == "yes") {
        value = true;
        return true;
    }
    if (raw == "0" || raw == "false" || raw == "off" || raw == "no") {
        value = false;
        return true;
    }
    return false;
}

static bool jsonValueStart(const String &payload, const char *key, int &start) {
    const String pattern = String("\"") + key + "\"";
    const int keyIndex = payload.indexOf(pattern);
    if (keyIndex < 0) return false;
    const int colonIndex = payload.indexOf(':', keyIndex + pattern.length());
    if (colonIndex < 0) return false;
    start = colonIndex + 1;
    while (start < (int)payload.length() && isspace((unsigned char)payload[start])) ++start;
    return start < (int)payload.length();
}

static bool jsonReadStringField(const String &payload, const char *key, String &value) {
    int start = 0;
    if (!jsonValueStart(payload, key, start) || payload[start] != '"') return false;
    value = "";
    bool escaped = false;
    for (int i = start + 1; i < (int)payload.length(); ++i) {
        const char c = payload[i];
        if (escaped) {
            switch (c) {
                case '"':
                case '\\':
                case '/':
                    value += c;
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    value += c;
                    break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') return true;
        value += c;
    }
    return false;
}

static bool jsonReadTokenField(const String &payload, const char *key, String &value) {
    int start = 0;
    if (!jsonValueStart(payload, key, start)) return false;
    int end = start;
    while (end < (int)payload.length()) {
        const char c = payload[end];
        if (c == ',' || c == '}' || c == ']' || isspace((unsigned char)c)) break;
        ++end;
    }
    if (end <= start) return false;
    value = payload.substring(start, end);
    value.trim();
    return value.length() > 0;
}

static bool jsonReadFloatField(const String &payload, const char *key, float &value) {
    String token;
    if (!jsonReadTokenField(payload, key, token)) return false;
    value = token.toFloat();
    return isfinite(value);
}

static bool jsonReadUIntField(const String &payload, const char *key, uint32_t &value) {
    String token;
    if (!jsonReadTokenField(payload, key, token)) return false;
    if (token.length() == 0 || token[0] == '-') return false;
    char *end = nullptr;
    const unsigned long parsed = strtoul(token.c_str(), &end, 10);
    if (end == token.c_str()) return false;
    value = (uint32_t)parsed;
    return true;
}

static bool jsonReadBoolField(const String &payload, const char *key, bool &value) {
    String token;
    if (!jsonReadTokenField(payload, key, token)) return false;
    token.toLowerCase();
    if (token == "true") {
        value = true;
        return true;
    }
    if (token == "false") {
        value = false;
        return true;
    }
    return false;
}

static String urlEncode(const String &value) {
    static const char *kHex = "0123456789ABCDEF";
    String out;
    out.reserve(value.length() * 3);
    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t c = (uint8_t)value[i];
        const bool safe =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~';
        if (safe) {
            out += (char)c;
        } else {
            out += '%';
            out += kHex[(c >> 4) & 0x0F];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

static String remoteBaseUrlFromPostUrl() {
    String url = String(gSettings.postUrl);
    url.trim();
    if (!url.length()) return "";

    const int apiIndex = url.indexOf("/api/");
    if (apiIndex > 0) return url.substring(0, apiIndex);

    const int schemeIndex = url.indexOf("://");
    const int originStart = schemeIndex >= 0 ? schemeIndex + 3 : 0;
    const int pathIndex = url.indexOf('/', originStart);
    if (pathIndex > 0) return url.substring(0, pathIndex);
    if (url.endsWith("/")) url.remove(url.length() - 1);
    return url;
}

static String resolveRemoteUrl(const String &urlOrPath) {
    String value = urlOrPath;
    value.trim();
    if (value.startsWith("http://") || value.startsWith("https://")) return value;
    const String base = remoteBaseUrlFromPostUrl();
    if (!base.length()) return "";
    if (value.startsWith("/")) return base + value;
    return base + "/" + value;
}

static bool urlUsesRemoteManagementOrigin(const String &url) {
    const String base = remoteBaseUrlFromPostUrl();
    if (!base.length()) return false;
    return url == base || url.startsWith(base + "/");
}

static bool remoteManagementConfigured() {
    return strlen(gSettings.wifiSsid) > 0 &&
        strlen(gSettings.postUrl) > 0 &&
        strlen(gSettings.postToken) > 0 &&
        remoteBaseUrlFromPostUrl().length() > 0;
}

static bool stringIsSha256Hex(const String &value) {
    if (value.length() != 64) return false;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        const bool ok = (c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F');
        if (!ok) return false;
    }
    return true;
}

static String sha256Hex(const uint8_t digest[32]) {
    static const char *kHex = "0123456789abcdef";
    String out;
    out.reserve(64);
    for (uint8_t i = 0; i < 32; ++i) {
        out += kHex[(digest[i] >> 4) & 0x0F];
        out += kHex[digest[i] & 0x0F];
    }
    return out;
}

static bool connectStationBlocking(bool keepAp) {
    applyCpuFrequencyForMode(gSolarLightMode, true);

    if (strlen(gSettings.wifiSsid) == 0) {
        gNetworkRuntime.lastWifiStatus = WiFi.status();
        setWifiMessage("wifi_not_configured");
        return false;
    }

    const bool keepApEffective = keepAp || localAccessPointShouldStayOn();
    if (!keepApEffective && gNetworkRuntime.apEnabled) {
        gDnsServer.stop();
        WiFi.softAPdisconnect(true);
        gNetworkRuntime.apEnabled = false;
    }
    WiFi.mode(keepApEffective ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    gNetworkRuntime.wifiEnabled = true;
    WiFi.setHostname("weather-station");

    if (WiFi.status() != WL_CONNECTED || WiFi.SSID() != String(gSettings.wifiSsid)) {
        gNetworkRuntime.lastWifiAttemptMs = millis();
        gNetworkRuntime.lastWifiStatus = WiFi.status();
        setWifiMessage("wifi_connecting");
        if (strlen(gSettings.wifiPassword) == 0) {
            WiFi.begin(gSettings.wifiSsid);
        } else {
            WiFi.begin(gSettings.wifiSsid, gSettings.wifiPassword);
        }
    }

    const uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED &&
           !hasElapsedMs(millis(), startMs, BoardConfig::kWifiConnectTimeoutMs)) {
        handleWebClientIfStarted();
        esp_task_wdt_reset();
        taskDelayMs(100);
    }

    gNetworkRuntime.staConnected = WiFi.status() == WL_CONNECTED;
    gNetworkRuntime.lastWifiStatus = WiFi.status();
    if (!gNetworkRuntime.staConnected) {
        setWifiMessage("wifi_connect_timeout");
    } else {
        resetRecoveryApBackoff();
        setWifiMessage("wifi_connected");
    }
    return gNetworkRuntime.staConnected;
}

static void startAccessPointIfNeeded() {
    applyCpuFrequencyForMode(gSolarLightMode, true);
    if (gNetworkRuntime.apEnabled) return;
    WiFi.mode(gNetworkRuntime.staConnected ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);
    bool ok = false;
    if (BoardConfig::kWifiApOpen || strlen(BoardConfig::kWifiApPassword) < 8) {
        ok = WiFi.softAP(BoardConfig::kWifiApSsid);
    } else {
        ok = WiFi.softAP(BoardConfig::kWifiApSsid, BoardConfig::kWifiApPassword);
    }
    gNetworkRuntime.apEnabled = ok;
    gNetworkRuntime.wifiEnabled = true;
    if (ok) {
        gDnsServer.start(53, "*", WiFi.softAPIP());
        ensureWebServerStarted();
        if (!gNetworkRuntime.staConnected && strlen(gSettings.wifiSsid) == 0) {
            setWifiMessage("ap_ready");
        }
    } else {
        setWifiMessage("ap_start_failed");
    }
}

static bool startRecoveryApAfterStaFailure() {
    if (!recoveryApAllowedForCurrentMode()) return false;
    const uint32_t nowMs = millis();
    if (recoveryApWindowActive(nowMs)) {
        startAccessPointIfNeeded();
        setWifiMessage(gNetworkRuntime.apEnabled ? "ap_recovery_wifi_failed" :
            "ap_start_failed");
        return gNetworkRuntime.apEnabled;
    }
    if (BoardConfig::kWifiRecoveryApMaxConsecutive > 0 &&
        gRecoveryApConsecutiveLaunches >= BoardConfig::kWifiRecoveryApMaxConsecutive &&
        gRecoveryApLastEndMs > 0 &&
        hasElapsedMs(nowMs, gRecoveryApLastEndMs, BoardConfig::kWifiRecoveryApRearmMs)) {
        gRecoveryApConsecutiveLaunches = 0;
    }
    if (BoardConfig::kWifiRecoveryApMaxConsecutive == 0 ||
        gRecoveryApConsecutiveLaunches >= BoardConfig::kWifiRecoveryApMaxConsecutive) {
        setWifiMessage("ap_recovery_backoff");
        return false;
    }
    gNetworkRuntime.recoveryApActive = true;
    gNetworkRuntime.recoveryApStartedMs = nowMs;
    startAccessPointIfNeeded();
    if (!gNetworkRuntime.apEnabled) {
        clearRecoveryAp();
        return false;
    }
    ++gRecoveryApConsecutiveLaunches;
    setWifiMessage("ap_recovery_wifi_failed");
    return true;
}

static void stopAccessPointIfNeeded() {
    if (!gNetworkRuntime.apEnabled) return;
    gDnsServer.stop();
    WiFi.softAPdisconnect(true);
    gNetworkRuntime.apEnabled = false;
}

static void stopWifiIfAllowed() {
    const uint32_t nowMs = millis();
    if (accessPointShouldStayOn(nowMs)) {
        if (gNetworkRuntime.apEnabled) {
            if (gNetworkRuntime.staConnected) WiFi.disconnect(true, false);
            gNetworkRuntime.wifiEnabled = true;
            gNetworkRuntime.staConnected = false;
            gNetworkRuntime.lastWifiStatus = WiFi.status();
            setWifiMessage(localAccessPointShouldStayOn() ? "ap_only" : "ap_recovery");
            return;
        }
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_AP);
        startAccessPointIfNeeded();
        gNetworkRuntime.wifiEnabled = true;
        gNetworkRuntime.staConnected = false;
        gNetworkRuntime.lastWifiStatus = WiFi.status();
        setWifiMessage(localAccessPointShouldStayOn() ? "ap_only" : "ap_recovery");
        return;
    }

    clearRecoveryAp();
    stopAccessPointIfNeeded();
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    gNetworkRuntime.wifiEnabled = false;
    gNetworkRuntime.staConnected = false;
    gNetworkRuntime.lastWifiStatus = WiFi.status();
    setWifiMessage("wifi_off");
    applyCpuFrequencyForMode(gSolarLightMode, false);
}

static bool serverPostConfigured() {
    return gSettings.serverPostEnabled &&
        strlen(gSettings.postUrl) > 0 &&
        strlen(gSettings.wifiSsid) > 0;
}

static uint32_t activeServerPostIntervalMs() {
    switch (gSolarLightMode) {
        case SolarLightMode::Sun: return gSettings.serverPostSunMs;
        case SolarLightMode::Dark: return gSettings.serverPostDarkMs;
        case SolarLightMode::Shadow:
        case SolarLightMode::Unknown:
        default: return gSettings.serverPostShadowMs;
    }
}

static bool scheduledPostDue(uint32_t nowMs) {
    if (!serverPostConfigured()) return false;
    return hasReachedMs(nowMs, gNetworkRuntime.nextPostAllowedMs);
}

static void scheduleNextPostAfterAttempt(bool success) {
    const uint32_t intervalMs = activeServerPostIntervalMs();
    const uint32_t retryMs = min(intervalMs / 4UL, 60000UL);
    const uint32_t delayMs = success ? intervalMs : retryMs;
    gNetworkRuntime.nextPostAllowedMs = gNetworkRuntime.lastPostAttemptMs + delayMs;
}

static bool applyJsonFloatSetting(const String &payload, const char *key, float &target) {
    float value = 0.0f;
    if (!jsonReadFloatField(payload, key, value)) return false;
    target = value;
    return true;
}

static bool applyJsonUIntSetting(const String &payload, const char *key, uint32_t &target) {
    uint32_t value = 0;
    if (!jsonReadUIntField(payload, key, value)) return false;
    target = value;
    return true;
}

static bool applyJsonBoolSetting(const String &payload, const char *key, bool &target) {
    bool value = false;
    if (!jsonReadBoolField(payload, key, value)) return false;
    target = value;
    return true;
}

static bool runtimeSettingsEqual(const RuntimeSettings &a, const RuntimeSettings &b) {
    return a.solarSunEnterVoltageV == b.solarSunEnterVoltageV &&
        a.solarSunExitVoltageV == b.solarSunExitVoltageV &&
        a.solarSunMinPowerW == b.solarSunMinPowerW &&
        a.solarDarkEnterVoltageV == b.solarDarkEnterVoltageV &&
        a.solarDarkExitVoltageV == b.solarDarkExitVoltageV &&
        a.solarDarkDeepSleepDelayMs == b.solarDarkDeepSleepDelayMs &&
        a.solarDeepSleepWakeMs == b.solarDeepSleepWakeMs &&
        a.serverPostSunMs == b.serverPostSunMs &&
        a.serverPostShadowMs == b.serverPostShadowMs &&
        a.serverPostDarkMs == b.serverPostDarkMs &&
        a.remoteConfigPullMs == b.remoteConfigPullMs &&
        a.remoteFirmwareCheckMs == b.remoteFirmwareCheckMs &&
        a.batteryPercentEmptyVoltageV == b.batteryPercentEmptyVoltageV &&
        a.batteryPercentFullVoltageV == b.batteryPercentFullVoltageV &&
        a.batteryLockoutEnterVoltageV == b.batteryLockoutEnterVoltageV &&
        a.batteryLockoutResumeVoltageV == b.batteryLockoutResumeVoltageV &&
        a.batteryLockoutWakeMs == b.batteryLockoutWakeMs &&
        a.serverPostEnabled == b.serverPostEnabled &&
        a.wifiApAlways == b.wifiApAlways &&
        strcmp(a.adminPassword, b.adminPassword) == 0 &&
        strcmp(a.wifiSsid, b.wifiSsid) == 0 &&
        strcmp(a.wifiPassword, b.wifiPassword) == 0 &&
        strcmp(a.postUrl, b.postUrl) == 0 &&
        strcmp(a.postToken, b.postToken) == 0 &&
        strcmp(a.spiffsVersion, b.spiffsVersion) == 0;
}

static void refreshBatteryPercentDisplayIfNeeded(const RuntimeSettings &previous) {
    if (previous.batteryPercentEmptyVoltageV == gSettings.batteryPercentEmptyVoltageV &&
        previous.batteryPercentFullVoltageV == gSettings.batteryPercentFullVoltageV) {
        return;
    }

    takeMutex(gTelemetryMutex);
    gTelemetry.batteryPercent = gTelemetry.batteryOnline ?
        computeBatteryPercent(gTelemetry.battery.loadVoltageV) : -1.0f;
    giveMutex(gTelemetryMutex);
    takeMutex(gDisplayBusMutex);
    gDisplayRuntime[8].hasRendered = false;
    giveMutex(gDisplayBusMutex);
    requestDisplayRefresh(1U << 8);
}

static bool darkWakeCadenceChanged(const RuntimeSettings &previous,
                                   const RuntimeSettings &next) {
    return previous.serverPostDarkMs != next.serverPostDarkMs ||
        previous.solarDeepSleepWakeMs != next.solarDeepSleepWakeMs;
}

static void resetDarkWakeCadenceState() {
    gDarkTimerWakeCount = 0;
    gDarkTimerWakeEvaluated = false;
    gDarkWakePostDue = false;
    gDarkWakePostOnly = false;
}

static bool applyRemoteConfigPayload(const String &payload) {
    bool success = true;
    if (jsonReadBoolField(payload, "success", success) && !success) {
        setRemoteConfigMessage("remote_config_rejected");
        return false;
    }

    RuntimeSettings next = gSettings;
    bool anyField = false;
    anyField = applyJsonFloatSetting(payload, "solarSunEnterVoltageV", next.solarSunEnterVoltageV) || anyField;
    anyField = applyJsonFloatSetting(payload, "solarSunExitVoltageV", next.solarSunExitVoltageV) || anyField;
    anyField = applyJsonFloatSetting(payload, "solarSunMinPowerW", next.solarSunMinPowerW) || anyField;
    anyField = applyJsonFloatSetting(payload, "solarDarkEnterVoltageV", next.solarDarkEnterVoltageV) || anyField;
    anyField = applyJsonFloatSetting(payload, "solarDarkExitVoltageV", next.solarDarkExitVoltageV) || anyField;
    anyField = applyJsonUIntSetting(payload, "solarDarkDeepSleepDelayMs", next.solarDarkDeepSleepDelayMs) || anyField;
    anyField = applyJsonUIntSetting(payload, "solarDeepSleepWakeMs", next.solarDeepSleepWakeMs) || anyField;
    anyField = applyJsonUIntSetting(payload, "serverPostSunMs", next.serverPostSunMs) || anyField;
    anyField = applyJsonUIntSetting(payload, "serverPostShadowMs", next.serverPostShadowMs) || anyField;
    anyField = applyJsonUIntSetting(payload, "serverPostDarkMs", next.serverPostDarkMs) || anyField;
    anyField = applyJsonUIntSetting(payload, "remoteConfigPullMs", next.remoteConfigPullMs) || anyField;
    anyField = applyJsonUIntSetting(payload, "remoteFirmwareCheckMs", next.remoteFirmwareCheckMs) || anyField;
    anyField = applyJsonFloatSetting(payload, "batteryPercentEmptyVoltageV", next.batteryPercentEmptyVoltageV) || anyField;
    anyField = applyJsonFloatSetting(payload, "batteryPercentFullVoltageV", next.batteryPercentFullVoltageV) || anyField;
    anyField = applyJsonFloatSetting(payload, "batteryLockoutEnterVoltageV", next.batteryLockoutEnterVoltageV) || anyField;
    anyField = applyJsonFloatSetting(payload, "batteryLockoutResumeVoltageV", next.batteryLockoutResumeVoltageV) || anyField;
    anyField = applyJsonUIntSetting(payload, "batteryLockoutWakeMs", next.batteryLockoutWakeMs) || anyField;
    anyField = applyJsonBoolSetting(payload, "serverPostEnabled", next.serverPostEnabled) || anyField;
    anyField = applyJsonBoolSetting(payload, "wifiApAlways", next.wifiApAlways) || anyField;

    if (!anyField) {
        setRemoteConfigMessage("remote_config_empty");
        return true;
    }
    if (!validRuntimeSettings(next)) {
        setRemoteConfigMessage("remote_config_invalid");
        return false;
    }
    if (runtimeSettingsEqual(next, gSettings)) {
        setRemoteConfigMessage("remote_config_current");
        return true;
    }

    const RuntimeSettings previous = gSettings;
    gSettings = next;
    if (!saveRuntimeSettings()) {
        gSettings = previous;
        setRemoteConfigMessage("remote_config_save_failed");
        return false;
    }

    if (darkWakeCadenceChanged(previous, gSettings)) resetDarkWakeCadenceState();
    refreshBatteryPercentDisplayIfNeeded(previous);
    setRemoteConfigMessage("remote_config_saved");
    return true;
}

static bool pullRemoteConfigNow() {
    if (gNetworkRuntime.remoteConfigPulling) {
        setRemoteConfigMessage("remote_config_already_running");
        return false;
    }
    ScopedBoolFlag remoteConfigGuard(gNetworkRuntime.remoteConfigPulling);
    gNetworkRuntime.lastRemoteConfigPullMs = millis();
    gNetworkRuntime.remoteConfigHttpCode = 0;
    if (!remoteManagementConfigured()) {
        setRemoteConfigMessage("remote_config_not_ready");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        setRemoteConfigMessage("remote_config_no_wifi");
        return false;
    }

    const String endpoint = remoteBaseUrlFromPostUrl() + "/api/device/config";
    HTTPClient http;
    if (!http.begin(endpoint)) {
        setRemoteConfigMessage("remote_config_begin_failed");
        return false;
    }
    configureHttpClient(http);
    http.addHeader("Authorization", String("Bearer ") + gSettings.postToken);
    setRemoteConfigMessage("remote_config_fetching");
    const int code = http.GET();
    gNetworkRuntime.remoteConfigHttpCode = code;
    if (code <= 0 || code >= 400) {
        setRemoteConfigMessage(managementHttpFailureMessage("remote_config", code));
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();
    return applyRemoteConfigPayload(payload);
}

static bool performHttpOtaUpdate(const String &urlOrPath, const String &expectedSha256,
                                 uint32_t expectedSize, int command, const char *installedVersion) {
    const bool spiffsUpdate = command == U_SPIFFS;
    const char *downloadMsg = spiffsUpdate ? "spiffs_downloading" : "firmware_downloading";
    const char *urlInvalidMsg = spiffsUpdate ? "spiffs_url_invalid" : "firmware_url_invalid";
    const char *beginMsg = spiffsUpdate ? "spiffs_begin_failed" : "firmware_begin_failed";
    const char *notFoundMsg = spiffsUpdate ? "spiffs_not_found" : "firmware_not_found";
    const char *httpFailedMsg = spiffsUpdate ? "spiffs_http_failed" : "firmware_http_failed";
    const char *sizeMismatchMsg = spiffsUpdate ? "spiffs_size_mismatch" : "firmware_size_mismatch";
    const char *updateBeginMsg = spiffsUpdate ? "spiffs_update_begin_failed" : "firmware_update_begin_failed";
    const char *shaMismatchMsg = spiffsUpdate ? "spiffs_sha_mismatch" : "firmware_sha_mismatch";
    const char *downloadFailedMsg = spiffsUpdate ? "spiffs_download_failed" : "firmware_download_failed";
    const char *endFailedMsg = spiffsUpdate ? "spiffs_update_end_failed" : "firmware_update_end_failed";
    const char *rebootMsg = spiffsUpdate ? "spiffs_rebooting" : "firmware_rebooting";

    String otaUrl = resolveRemoteUrl(urlOrPath);
    if (!otaUrl.length()) {
        setFirmwareMessage(urlInvalidMsg);
        return false;
    }

    HTTPClient http;
    String otaAttemptUrl = otaUrl;
    bool attemptedAuthFallback = false;
    int code = 0;
    bool connected = false;
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (!http.begin(otaAttemptUrl)) {
            setFirmwareMessage(beginMsg);
            return false;
        }
        configureHttpClient(http);
        http.setReuse(false);
        if (strlen(gSettings.postToken) > 0 && urlUsesRemoteManagementOrigin(otaAttemptUrl)) {
            http.addHeader("Authorization", String("Bearer ") + gSettings.postToken);
        }

        setFirmwareMessage(downloadMsg);
        code = http.GET();
        gNetworkRuntime.otaHttpCode = code;
        if (code > 0 && code < 400) {
            connected = true;
            break;
        }

        const bool retryable = code == HTTPC_ERROR_CONNECTION_REFUSED ||
            code == HTTPC_ERROR_CONNECTION_LOST ||
            code == HTTPC_ERROR_SEND_HEADER_FAILED ||
            code == HTTPC_ERROR_READ_TIMEOUT ||
            code == HTTP_CODE_GATEWAY_TIMEOUT;
        if (!retryable || attempt >= 2) {
            if (code == 404) {
                setFirmwareMessage(notFoundMsg);
            } else if (code == HTTPC_ERROR_READ_TIMEOUT || code == HTTP_CODE_GATEWAY_TIMEOUT) {
                setFirmwareMessage(spiffsUpdate ? "spiffs_timeout" : "firmware_timeout");
            } else if (code <= 0) {
                setFirmwareMessage(spiffsUpdate ? "spiffs_transport_failed" : "firmware_transport_failed");
            } else {
                setFirmwareMessage(code == 401 || code == 403 ?
                    (spiffsUpdate ? "spiffs_unauthorized" : "firmware_unauthorized") :
                    httpFailedMsg);
            }
            http.end();
            return false;
        }

        http.end();
        // If tokenized artifact URL trips transport errors, retry once with
        // direct device-auth artifact URL (same origin, shorter query string).
        if (!attemptedAuthFallback &&
            urlUsesRemoteManagementOrigin(otaUrl) &&
            otaUrl.indexOf("/api/device/artifact") >= 0) {
            otaAttemptUrl = remoteBaseUrlFromPostUrl() +
                String(spiffsUpdate ? "/api/device/artifact?type=spiffs" :
                                      "/api/device/artifact?type=firmware");
            attemptedAuthFallback = true;
        }
        taskDelayMs(250);
        esp_task_wdt_reset();
    }
    if (!connected) {
        setFirmwareMessage(spiffsUpdate ? "spiffs_transport_failed" : "firmware_transport_failed");
        return false;
    }

    const int contentLength = http.getSize();
    if (expectedSize > 0 && contentLength > 0 && (uint32_t)contentLength != expectedSize) {
        setFirmwareMessage(sizeMismatchMsg);
        http.end();
        return false;
    }

    const size_t updateSize = expectedSize > 0 ? expectedSize :
        (contentLength > 0 ? (size_t)contentLength : UPDATE_SIZE_UNKNOWN);
    if (spiffsUpdate && gNetworkRuntime.spiffsReady) {
        SPIFFS.end();
        gNetworkRuntime.spiffsReady = false;
    }
    if (!Update.begin(updateSize, command)) {
        setFirmwareMessage(updateBeginMsg);
        http.end();
        if (spiffsUpdate) gNetworkRuntime.spiffsReady = SPIFFS.begin(true);
        return false;
    }

    uint8_t buffer[1024];
    uint8_t digest[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

    WiFiClient *stream = http.getStreamPtr();
    size_t total = 0;
    uint32_t lastDataMs = millis();
    bool failed = false;

    while (http.connected() || stream->available()) {
        const size_t available = stream->available();
        if (available > 0) {
            const size_t toRead = available < sizeof(buffer) ? available : sizeof(buffer);
            const int bytesRead = stream->readBytes(buffer, toRead);
            if (bytesRead <= 0) {
                failed = true;
                break;
            }
            const size_t written = Update.write(buffer, (size_t)bytesRead);
            if (written != (size_t)bytesRead) {
                failed = true;
                break;
            }
            mbedtls_sha256_update_ret(&sha, buffer, (size_t)bytesRead);
            total += (size_t)bytesRead;
            lastDataMs = millis();
            esp_task_wdt_reset();
            if (updateSize != UPDATE_SIZE_UNKNOWN && total >= updateSize) break;
            continue;
        }

        if (updateSize != UPDATE_SIZE_UNKNOWN && total >= updateSize) break;
        if (hasElapsedMs(millis(), lastDataMs, 15000UL)) {
            failed = true;
            break;
        }
        esp_task_wdt_reset();
        handleWebClientIfStarted();
        taskDelayMs(10);
    }

    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (!failed && expectedSize > 0 && total != expectedSize) {
        failed = true;
    }
    if (!failed && contentLength > 0 && total != (size_t)contentLength) {
        failed = true;
    }

    String normalizedSha = expectedSha256;
    normalizedSha.trim();
    normalizedSha.toLowerCase();
    if (!failed && stringIsSha256Hex(normalizedSha)) {
        const String actualSha = sha256Hex(digest);
        if (actualSha != normalizedSha) {
            failed = true;
            setFirmwareMessage(shaMismatchMsg);
        }
    }

    if (failed) {
        if (strlen(gNetworkRuntime.firmwareMessage) == 0 ||
            strcmp(gNetworkRuntime.firmwareMessage, downloadMsg) == 0) {
            setFirmwareMessage(downloadFailedMsg);
        }
        Update.abort();
        http.end();
        if (spiffsUpdate) gNetworkRuntime.spiffsReady = SPIFFS.begin(true);
        return false;
    }

    if (!Update.end(true)) {
        setFirmwareMessage(endFailedMsg);
        http.end();
        if (spiffsUpdate) gNetworkRuntime.spiffsReady = SPIFFS.begin(true);
        return false;
    }

    http.end();
    if (spiffsUpdate && installedVersion != nullptr && installedVersion[0] != '\0') {
        copySetting(gSettings.spiffsVersion, sizeof(gSettings.spiffsVersion), installedVersion);
        saveRuntimeSettings();
    }
    setFirmwareMessage(rebootMsg);
    taskDelayMs(500);
    ESP.restart();
    return true;
}

static bool checkRemoteFirmwareNow() {
    if (gNetworkRuntime.firmwareChecking) {
        setFirmwareMessage("firmware_already_running");
        return false;
    }
    ScopedBoolFlag firmwareGuard(gNetworkRuntime.firmwareChecking);
    gNetworkRuntime.lastFirmwareCheckMs = millis();
    gNetworkRuntime.firmwareHttpCode = 0;
    gNetworkRuntime.otaHttpCode = 0;
    if (!remoteManagementConfigured()) {
        setFirmwareMessage("firmware_not_ready");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        setFirmwareMessage("firmware_no_wifi");
        return false;
    }

    const String endpoint = remoteBaseUrlFromPostUrl() +
        "/api/device/firmware?version=" + urlEncode(BoardConfig::kFirmwareVersion) +
        "&spiffs=" + urlEncode(gSettings.spiffsVersion);
    HTTPClient http;
    if (!http.begin(endpoint)) {
        setFirmwareMessage("firmware_check_begin_failed");
        scheduleFirmwareRetrySoon();
        return false;
    }
    configureHttpClient(http);
    http.addHeader("Authorization", String("Bearer ") + gSettings.postToken);
    setFirmwareMessage("firmware_checking");
    const int code = http.GET();
    gNetworkRuntime.firmwareHttpCode = code;
    if (code <= 0 || code >= 400) {
        setFirmwareMessage(managementHttpFailureMessage("firmware", code));
        http.end();
        scheduleFirmwareRetrySoon();
        return false;
    }

    const String payload = http.getString();
    http.end();

    bool firmwareUpdateAvailable = false;
    bool firmwareEnabled = false;
    String firmwareVersion;
    String firmwareUrl;
    String firmwareSha256;
    uint32_t firmwareSize = 0;
    jsonReadBoolField(payload, "firmwareUpdateAvailable", firmwareUpdateAvailable);
    jsonReadBoolField(payload, "firmwareEnabled", firmwareEnabled);
    jsonReadStringField(payload, "firmwareVersion", firmwareVersion);
    jsonReadStringField(payload, "firmwareUrl", firmwareUrl);
    jsonReadStringField(payload, "firmwareSha256", firmwareSha256);
    jsonReadUIntField(payload, "firmwareSize", firmwareSize);

    if (firmwareUpdateAvailable && firmwareEnabled && firmwareVersion.length() &&
        firmwareVersion != String(BoardConfig::kFirmwareVersion)) {
        if (!firmwareUrl.length()) {
            setFirmwareMessage("firmware_url_missing");
            scheduleFirmwareRetrySoon();
            return false;
        }
        const bool ok = performHttpOtaUpdate(firmwareUrl, firmwareSha256, firmwareSize, U_FLASH, nullptr);
        if (!ok) scheduleFirmwareRetrySoon();
        return ok;
    }

    bool spiffsUpdateAvailable = false;
    bool spiffsEnabled = false;
    String spiffsVersion;
    String spiffsUrl;
    String spiffsSha256;
    uint32_t spiffsSize = 0;
    jsonReadBoolField(payload, "spiffsUpdateAvailable", spiffsUpdateAvailable);
    jsonReadBoolField(payload, "spiffsEnabled", spiffsEnabled);
    jsonReadStringField(payload, "spiffsVersion", spiffsVersion);
    jsonReadStringField(payload, "spiffsUrl", spiffsUrl);
    jsonReadStringField(payload, "spiffsSha256", spiffsSha256);
    jsonReadUIntField(payload, "spiffsSize", spiffsSize);

    if (spiffsUpdateAvailable && spiffsEnabled && spiffsVersion.length() &&
        spiffsVersion != String(gSettings.spiffsVersion)) {
        if (!spiffsUrl.length()) {
            setFirmwareMessage("spiffs_url_missing");
            scheduleFirmwareRetrySoon();
            return false;
        }
        const bool ok = performHttpOtaUpdate(spiffsUrl, spiffsSha256, spiffsSize, U_SPIFFS,
                                             spiffsVersion.c_str());
        if (!ok) scheduleFirmwareRetrySoon();
        return ok;
    }

    setFirmwareMessage("firmware_current");
    return true;
}

static void syncRemoteManagementIfDue() {
    // Remote config and OTA checks are intentionally batched into an existing
    // post WiFi session. Their timers never start a separate radio wake.
    if (gOtaUpload.inProgress || WiFi.status() != WL_CONNECTED) return;
    if (!remoteManagementConfigured()) {
        setRemoteConfigMessage("remote_config_not_ready");
        setFirmwareMessage("firmware_not_ready");
        return;
    }

    const uint32_t nowMs = millis();
    if (hasElapsedMs(nowMs, gNetworkRuntime.lastRemoteConfigPullMs,
                     gSettings.remoteConfigPullMs)) {
        pullRemoteConfigNow();
    }

    if (hasElapsedMs(nowMs, gNetworkRuntime.lastFirmwareCheckMs,
                     gSettings.remoteFirmwareCheckMs)) {
        checkRemoteFirmwareNow();
    }
}

static bool postTelemetryNow(bool keepApAfterPost) {
    if (!serverPostConfigured()) {
        setPostMessage("post_not_configured");
        return false;
    }
    if (gNetworkRuntime.posting) {
        setPostMessage("post_already_running");
        return false;
    }

    applyCpuFrequencyForMode(gSolarLightMode, true);
    ScopedBoolFlag postingGuard(gNetworkRuntime.posting);
    gNetworkRuntime.lastPostAttemptMs = millis();
    gNetworkRuntime.lastPostHttpCode = 0;
    setPostMessage("post_connecting_wifi");

    if (!connectStationBlocking(keepApAfterPost)) {
        setPostMessage("post_wifi_failed");
        scheduleNextPostAfterAttempt(false);
        if (!startRecoveryApAfterStaFailure() && !accessPointShouldStayOn(millis())) {
            stopWifiIfAllowed();
        }
        return false;
    }

    HTTPClient http;
    if (!http.begin(String(gSettings.postUrl))) {
        setPostMessage("post_begin_failed");
        scheduleNextPostAfterAttempt(false);
        if (!keepApAfterPost && !accessPointShouldStayOn(millis())) stopWifiIfAllowed();
        return false;
    }
    configureHttpClient(http);
    http.addHeader("Content-Type", "application/json");
    if (strlen(gSettings.postToken) > 0) {
        http.addHeader("Authorization", String("Bearer ") + gSettings.postToken);
    }
    setPostMessage("post_sending");
    const String payload = buildTelemetryJson();
    const int code = http.POST(payload);
    gNetworkRuntime.lastPostHttpCode = code;
    if (code > 0 && code < 400) {
        gNetworkRuntime.lastPostSuccessMs = millis();
        setPostMessage("post_ok");
    } else {
        setPostMessage(code > 0 ? "post_http_failed" : "post_transport_failed");
    }
    const bool ok = code > 0 && code < 400;
    scheduleNextPostAfterAttempt(ok);
    http.end();

    syncRemoteManagementIfDue();

    if (!keepApAfterPost && !accessPointShouldStayOn(millis())) {
        stopWifiIfAllowed();
    }
    return ok;
}

static void handleConfigPost() {
    if (!requireAdmin()) return;

    float f = 0.0f;
    uint32_t u = 0;
    bool b = false;
    bool wifiSettingsChanged = false;
    bool batteryPercentSettingsChanged = false;
    const RuntimeSettings previous = gSettings;

    if (readFloatArg("solarSunEnterVoltageV", f)) gSettings.solarSunEnterVoltageV = f;
    if (readFloatArg("solarSunExitVoltageV", f)) gSettings.solarSunExitVoltageV = f;
    if (readFloatArg("solarSunMinPowerW", f)) gSettings.solarSunMinPowerW = f;
    if (readFloatArg("solarDarkEnterVoltageV", f)) gSettings.solarDarkEnterVoltageV = f;
    if (readFloatArg("solarDarkExitVoltageV", f)) gSettings.solarDarkExitVoltageV = f;
    if (readUIntArg("solarDarkDeepSleepDelayMs", u)) gSettings.solarDarkDeepSleepDelayMs = u;
    if (readUIntArg("solarDeepSleepWakeMs", u)) gSettings.solarDeepSleepWakeMs = u;
    if (readUIntArg("serverPostSunMs", u)) gSettings.serverPostSunMs = u;
    if (readUIntArg("serverPostShadowMs", u)) gSettings.serverPostShadowMs = u;
    if (readUIntArg("serverPostDarkMs", u)) gSettings.serverPostDarkMs = u;
    if (readUIntArg("remoteConfigPullMs", u)) gSettings.remoteConfigPullMs = u;
    if (readUIntArg("remoteFirmwareCheckMs", u)) gSettings.remoteFirmwareCheckMs = u;
    if (readFloatArg("batteryPercentEmptyVoltageV", f)) {
        batteryPercentSettingsChanged =
            batteryPercentSettingsChanged || f != gSettings.batteryPercentEmptyVoltageV;
        gSettings.batteryPercentEmptyVoltageV = f;
    }
    if (readFloatArg("batteryPercentFullVoltageV", f)) {
        batteryPercentSettingsChanged =
            batteryPercentSettingsChanged || f != gSettings.batteryPercentFullVoltageV;
        gSettings.batteryPercentFullVoltageV = f;
    }
    if (readFloatArg("batteryLockoutEnterVoltageV", f)) {
        gSettings.batteryLockoutEnterVoltageV = f;
    }
    if (readFloatArg("batteryLockoutResumeVoltageV", f)) {
        gSettings.batteryLockoutResumeVoltageV = f;
    }
    if (readUIntArg("batteryLockoutWakeMs", u)) gSettings.batteryLockoutWakeMs = u;
    if (readBoolArg("serverPostEnabled", b)) gSettings.serverPostEnabled = b;
    if (readBoolArg("wifiApAlways", b)) gSettings.wifiApAlways = b;

    if (gWebServer.hasArg("wifiSsid")) {
        const String nextSsid = gWebServer.arg("wifiSsid");
        wifiSettingsChanged = wifiSettingsChanged || nextSsid != String(gSettings.wifiSsid);
        copySetting(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), nextSsid);
    }
    const bool hasNewWifiPassword =
        gWebServer.hasArg("wifiPassword") && gWebServer.arg("wifiPassword").length() > 0;
    bool clearWifiPassword = false;
    readBoolArg("clearWifiPassword", clearWifiPassword);
    if (hasNewWifiPassword) {
        const String nextPassword = gWebServer.arg("wifiPassword");
        wifiSettingsChanged = wifiSettingsChanged || nextPassword != String(gSettings.wifiPassword);
        copySetting(gSettings.wifiPassword, sizeof(gSettings.wifiPassword), nextPassword);
    } else if (clearWifiPassword) {
        wifiSettingsChanged = wifiSettingsChanged || strlen(gSettings.wifiPassword) > 0;
        copySetting(gSettings.wifiPassword, sizeof(gSettings.wifiPassword), "");
    }
    if (gWebServer.hasArg("postUrl")) {
        copySetting(gSettings.postUrl, sizeof(gSettings.postUrl), gWebServer.arg("postUrl"));
    }
    if (gWebServer.hasArg("postToken") && gWebServer.arg("postToken").length() > 0) {
        copySetting(gSettings.postToken, sizeof(gSettings.postToken), gWebServer.arg("postToken"));
    }
    if (gWebServer.hasArg("clearPostToken")) {
        copySetting(gSettings.postToken, sizeof(gSettings.postToken), "");
    }
    if (gWebServer.hasArg("newAdminPassword") && gWebServer.arg("newAdminPassword").length() > 0) {
        copySetting(gSettings.adminPassword, sizeof(gSettings.adminPassword),
                    gWebServer.arg("newAdminPassword"));
    }

    if (!validRuntimeSettings()) {
        sendJson(400, "{\"success\":false,\"error\":\"invalid_settings\"}");
        loadRuntimeSettings();
        return;
    }
    if (!saveRuntimeSettings()) {
        sendJson(500, "{\"success\":false,\"error\":\"settings_save_failed\"}");
        return;
    }
    if (darkWakeCadenceChanged(previous, gSettings)) resetDarkWakeCadenceState();
    if (wifiSettingsChanged) {
        WiFi.disconnect(false, false);
        gNetworkRuntime.staConnected = false;
        gNetworkRuntime.lastWifiStatus = WiFi.status();
        setWifiMessage("wifi_settings_saved");
    }
    if (batteryPercentSettingsChanged) {
        takeMutex(gTelemetryMutex);
        gTelemetry.batteryPercent = gTelemetry.batteryOnline ?
            computeBatteryPercent(gTelemetry.battery.loadVoltageV) : -1.0f;
        giveMutex(gTelemetryMutex);
        takeMutex(gDisplayBusMutex);
        gDisplayRuntime[8].hasRendered = false;
        giveMutex(gDisplayBusMutex);
        requestDisplayRefresh(1U << 8);
    }
    sendJson(200, buildConfigJson(true, true));
}

static void handleWifiClearCache() {
    if (!requireAdmin()) return;

    copySetting(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), "");
    copySetting(gSettings.wifiPassword, sizeof(gSettings.wifiPassword), "");
    resetRecoveryApBackoff();
    if (!saveRuntimeSettings()) {
        sendJson(500, "{\"success\":false,\"error\":\"settings_save_failed\"}");
        return;
    }

    WiFi.disconnect(false, true);
    gNetworkRuntime.staConnected = false;
    gNetworkRuntime.lastWifiAttemptMs = 0;
    gNetworkRuntime.lastWifiStatus = WiFi.status();
    setWifiMessage("wifi_cache_cleared");
    sendJson(200, "{\"success\":true,\"message\":\"wifi_cache_cleared\"}");
}

static void handleWifiScan() {
    if (!requireAdmin()) return;
    applyCpuFrequencyForMode(gSolarLightMode, true);
    WiFi.mode(gNetworkRuntime.apEnabled ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    const int count = WiFi.scanNetworks(false, true);
    String payload = "{\"success\":true,\"networks\":[";
    for (int i = 0; i < count; ++i) {
        if (i > 0) payload += ",";
        payload += "{";
        payload += "\"ssid\":" + jsonString(WiFi.SSID(i)) + ",";
        payload += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        payload += "\"secure\":" + jsonBool(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        payload += "}";
    }
    payload += "]}";
    WiFi.scanDelete();
    sendJson(200, payload);
}

static void handleOtaResponse() {
    if (gOtaUpload.unauthorized) {
        sendJson(401, "{\"success\":false,\"error\":\"unauthorized\"}");
        return;
    }
    if (gOtaUpload.success) {
        sendJson(200, "{\"success\":true,\"message\":\"ota_ok_rebooting\"}");
        taskDelayMs(500);
        ESP.restart();
        return;
    }
    sendJson(500, "{\"success\":false,\"error\":\"ota_failed\"}");
}

static void handleOtaUpload(uint8_t command) {
    HTTPUpload &upload = gWebServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        gOtaUpload = {};
        gOtaUpload.inProgress = true;
        copySetting(gOtaUpload.target, sizeof(gOtaUpload.target), command == U_SPIFFS ? "spiffs" : "flash");
        if (!adminAuthorized()) {
            gOtaUpload.unauthorized = true;
            gOtaUpload.inProgress = false;
            return;
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
            gOtaUpload.failed = true;
            gOtaUpload.inProgress = false;
            copySetting(gOtaUpload.message, sizeof(gOtaUpload.message), "begin_failed");
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!gOtaUpload.inProgress || gOtaUpload.failed || gOtaUpload.unauthorized) return;
        const size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            Update.abort();
            gOtaUpload.failed = true;
            gOtaUpload.inProgress = false;
            copySetting(gOtaUpload.message, sizeof(gOtaUpload.message), "write_failed");
            return;
        }
        gOtaUpload.bytesWritten += written;
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (!gOtaUpload.inProgress || gOtaUpload.failed || gOtaUpload.unauthorized) return;
        if (!Update.end(true)) {
            gOtaUpload.failed = true;
            gOtaUpload.inProgress = false;
            copySetting(gOtaUpload.message, sizeof(gOtaUpload.message), "end_failed");
            return;
        }
        gOtaUpload.success = true;
        gOtaUpload.inProgress = false;
        copySetting(gOtaUpload.message, sizeof(gOtaUpload.message), "ok");
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        gOtaUpload.failed = true;
        gOtaUpload.inProgress = false;
        copySetting(gOtaUpload.message, sizeof(gOtaUpload.message), "aborted");
    }
}

static void setupWebRoutes() {
    static const char *kHeaderKeys[] = {"X-Admin-Password"};
    gWebServer.collectHeaders(kHeaderKeys, 1);

    gWebServer.on("/", HTTP_GET, []() {
        if (!serveSpiffsPath("/index.html")) {
            sendText(503, "Weather Station UI unavailable. Upload SPIFFS image.");
        }
    });
    gWebServer.on("/api/status", HTTP_GET, []() {
        sendJson(200, buildTelemetryJson());
    });
    gWebServer.on("/api/admin/config", HTTP_GET, []() {
        if (!requireAdmin()) return;
        sendJson(200, buildConfigJson(true, true));
    });
    gWebServer.on("/api/admin/config", HTTP_POST, handleConfigPost);
    gWebServer.on("/api/admin/wifi-scan", HTTP_POST, handleWifiScan);
    gWebServer.on("/api/admin/wifi-clear-cache", HTTP_POST, handleWifiClearCache);
    gWebServer.on("/api/admin/post-now", HTTP_POST, []() {
        if (!requireAdmin()) return;
        const bool ok = postTelemetryNow(accessPointShouldStayOn(millis()));
        sendJson(ok ? 200 : 500, buildTelemetryJson());
    });
    gWebServer.on("/api/admin/reboot", HTTP_POST, []() {
        if (!requireAdmin()) return;
        sendJson(200, "{\"success\":true,\"message\":\"rebooting\"}");
        taskDelayMs(500);
        ESP.restart();
    });
    gWebServer.on(
        "/api/ota/upload", HTTP_POST, handleOtaResponse,
        []() { handleOtaUpload(U_FLASH); });
    gWebServer.on(
        "/api/ota/upload-spiffs", HTTP_POST, handleOtaResponse,
        []() { handleOtaUpload(U_SPIFFS); });

    auto redirect = []() {
        gWebServer.sendHeader("Location", "/", true);
        gWebServer.send(302, "text/plain", "redirect");
    };
    gWebServer.on("/generate_204", HTTP_GET, redirect);
    gWebServer.on("/gen_204", HTTP_GET, redirect);
    gWebServer.on("/hotspot-detect.html", HTTP_GET, redirect);
    gWebServer.on("/connecttest.txt", HTTP_GET, redirect);
    gWebServer.on("/ncsi.txt", HTTP_GET, redirect);

    gWebServer.onNotFound([]() {
        const String path = gWebServer.uri();
        if (path.startsWith("/api/")) {
            sendJson(404, "{\"success\":false,\"error\":\"not_found\"}");
            return;
        }
        if (serveSpiffsPath(path)) {
            return;
        }
        if (gNetworkRuntime.apEnabled) {
            gWebServer.sendHeader("Location", "/", true);
            gWebServer.send(302, "text/plain", "redirect");
            return;
        }
        sendText(404, "not_found");
    });
}

static void initializeNetworkRuntime() {
    gNetworkRuntime = {};
    gNetworkRuntime.lastPostHttpCode = 0;
    gNetworkRuntime.lastWifiStatus = WiFi.status();
    setWifiMessage("idle");
    setPostMessage("idle");
    setRemoteConfigMessage("idle");
    setFirmwareMessage("idle");

    gNetworkRuntime.spiffsReady = SPIFFS.begin(true);
    if (!gNetworkRuntime.spiffsReady) {
        Serial.println("SPIFFS mount failed");
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_OFF);
    setupWebRoutes();
}

static void applyNetworkPolicy() {
    const uint32_t nowMs = millis();
    const bool keepAp = accessPointShouldStayOn(nowMs);

    if (gDarkWakePostOnly) {
        const bool posted = postTelemetryNow(false);
        if (!posted && gDarkTimerWakeCount > 0) --gDarkTimerWakeCount;
        enterSolarDeepSleep();
        return;
    }

    if (keepAp) {
        startAccessPointIfNeeded();
        handleWebClientIfStarted();
        processCaptiveDnsIfNeeded();
    }

    if (scheduledPostDue(nowMs)) {
        postTelemetryNow(keepAp);
        return;
    }

    if (!accessPointShouldStayOn(millis()) &&
        (gNetworkRuntime.wifiEnabled || gNetworkRuntime.apEnabled ||
         gNetworkRuntime.staConnected)) {
        stopWifiIfAllowed();
    }
}
