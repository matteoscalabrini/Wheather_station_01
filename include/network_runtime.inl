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

static void setNetworkMessage(const char *message) {
    copySetting(gNetworkRuntime.lastPostMessage, sizeof(gNetworkRuntime.lastPostMessage),
                message != nullptr ? message : "");
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
    String payload;
    payload.reserve(4096);
    payload = "{";
    payload += "\"board\":" + jsonString(BoardConfig::kBoardName) + ",";
    payload += "\"uptimeMs\":" + String(millis()) + ",";
    payload += "\"solarMode\":" + jsonString(solarLightModeLabel(gSolarLightMode)) + ",";
    payload += "\"displaysForcedOff\":" + jsonBool(gDisplaysForcedOff) + ",";
    payload += "\"wifi\":{";
    payload += "\"enabled\":" + jsonBool(gNetworkRuntime.wifiEnabled) + ",";
    payload += "\"ap\":" + jsonBool(gNetworkRuntime.apEnabled) + ",";
    payload += "\"sta\":" + jsonBool(gNetworkRuntime.staConnected) + ",";
    payload += "\"ip\":" + jsonString(gNetworkRuntime.staConnected ? WiFi.localIP().toString() : String("")) + ",";
    payload += "\"apIp\":" + jsonString(gNetworkRuntime.apEnabled ? WiFi.softAPIP().toString() : String("")) + ",";
    payload += "\"lastPostCode\":" + String(gNetworkRuntime.lastPostHttpCode) + ",";
    payload += "\"lastPostMessage\":" + jsonString(gNetworkRuntime.lastPostMessage);
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
    payload += "\"serverPostEnabled\":" + jsonBool(gSettings.serverPostEnabled) + ",";
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

static bool connectStationBlocking(bool keepAp, bool forceAttempt = true) {
    if (strlen(gSettings.wifiSsid) == 0) {
        setNetworkMessage("wifi_not_configured");
        return false;
    }

    const bool keepApEffective = keepAp || BoardConfig::kWifiDebugForceApAlways;
    WiFi.mode(keepApEffective ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    gNetworkRuntime.wifiEnabled = true;
    WiFi.setHostname("weather-station");

    if (WiFi.status() != WL_CONNECTED || WiFi.SSID() != String(gSettings.wifiSsid)) {
        if (!forceAttempt &&
            !hasElapsedMs(millis(), gNetworkRuntime.lastPolicyMs,
                          BoardConfig::kWifiReconnectRetryMs)) {
            gNetworkRuntime.staConnected = false;
            return false;
        }
        gNetworkRuntime.lastPolicyMs = millis();
        WiFi.begin(gSettings.wifiSsid, gSettings.wifiPassword);
    }

    const uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED &&
           !hasElapsedMs(millis(), startMs, BoardConfig::kWifiConnectTimeoutMs)) {
        handleWebClientIfStarted();
        esp_task_wdt_reset();
        taskDelayMs(100);
    }

    gNetworkRuntime.staConnected = WiFi.status() == WL_CONNECTED;
    if (!gNetworkRuntime.staConnected) {
        setNetworkMessage("wifi_connect_timeout");
    }
    return gNetworkRuntime.staConnected;
}

static void startAccessPointIfNeeded() {
    if (gNetworkRuntime.apEnabled) return;
    WiFi.mode(WIFI_AP_STA);
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
    } else {
        setNetworkMessage("ap_start_failed");
    }
}

static void stopAccessPointIfNeeded() {
    if (!gNetworkRuntime.apEnabled) return;
    gDnsServer.stop();
    WiFi.softAPdisconnect(true);
    gNetworkRuntime.apEnabled = false;
}

static void stopWifiIfAllowed() {
    if (BoardConfig::kWifiDebugForceApAlways) {
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_AP);
        startAccessPointIfNeeded();
        gNetworkRuntime.wifiEnabled = true;
        gNetworkRuntime.staConnected = false;
        return;
    }

    stopAccessPointIfNeeded();
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    gNetworkRuntime.wifiEnabled = false;
    gNetworkRuntime.staConnected = false;
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
    return hasElapsedMs(nowMs, gNetworkRuntime.lastPostAttemptMs, activeServerPostIntervalMs());
}

static bool postTelemetryNow(bool keepApAfterPost) {
    if (!serverPostConfigured()) {
        setNetworkMessage("post_not_configured");
        return false;
    }

    gNetworkRuntime.posting = true;
    gNetworkRuntime.lastPostAttemptMs = millis();
    gNetworkRuntime.lastPostHttpCode = 0;

    if (!connectStationBlocking(keepApAfterPost)) {
        gNetworkRuntime.posting = false;
        if (!keepApAfterPost) stopWifiIfAllowed();
        return false;
    }

    HTTPClient http;
    if (!http.begin(String(gSettings.postUrl))) {
        setNetworkMessage("post_begin_failed");
        gNetworkRuntime.posting = false;
        if (!keepApAfterPost) stopWifiIfAllowed();
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    if (strlen(gSettings.postToken) > 0) {
        http.addHeader("Authorization", String("Bearer ") + gSettings.postToken);
    }
    const String payload = buildTelemetryJson();
    const int code = http.POST(payload);
    gNetworkRuntime.lastPostHttpCode = code;
    if (code > 0 && code < 400) {
        gNetworkRuntime.lastPostSuccessMs = millis();
        setNetworkMessage("post_ok");
    } else {
        setNetworkMessage("post_failed");
    }
    http.end();
    gNetworkRuntime.posting = false;

    if (!keepApAfterPost) {
        stopWifiIfAllowed();
    }
    return code > 0 && code < 400;
}

static void handleConfigPost() {
    if (!requireAdmin()) return;

    float f = 0.0f;
    uint32_t u = 0;
    bool b = false;

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
    if (readBoolArg("serverPostEnabled", b)) gSettings.serverPostEnabled = b;

    if (gWebServer.hasArg("wifiSsid")) {
        copySetting(gSettings.wifiSsid, sizeof(gSettings.wifiSsid), gWebServer.arg("wifiSsid"));
    }
    if (gWebServer.hasArg("wifiPassword") && gWebServer.arg("wifiPassword").length() > 0) {
        copySetting(gSettings.wifiPassword, sizeof(gSettings.wifiPassword), gWebServer.arg("wifiPassword"));
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
    sendJson(200, buildConfigJson(true, true));
}

static void handleWifiScan() {
    if (!requireAdmin()) return;
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
    gWebServer.on("/api/admin/post-now", HTTP_POST, []() {
        if (!requireAdmin()) return;
        const bool ok = postTelemetryNow(gSolarLightMode == SolarLightMode::Sun);
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
    setNetworkMessage("idle");

    gNetworkRuntime.spiffsReady = SPIFFS.begin(true);
    if (!gNetworkRuntime.spiffsReady) {
        Serial.println("SPIFFS mount failed");
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    setupWebRoutes();
}

static void applyNetworkPolicy() {
    const uint32_t nowMs = millis();

    if (gDarkWakePostOnly) {
        postTelemetryNow(false);
        enterSolarDeepSleep();
        return;
    }

    processCaptiveDnsIfNeeded();

    if (BoardConfig::kWifiDebugForceApAlways) {
        startAccessPointIfNeeded();
    }

    if (gSolarLightMode == SolarLightMode::Sun) {
        startAccessPointIfNeeded();
        connectStationBlocking(true, false);
        handleWebClientIfStarted();
        if (scheduledPostDue(nowMs)) {
            postTelemetryNow(true);
        }
        return;
    }

    handleWebClientIfStarted();
    if (scheduledPostDue(nowMs)) {
        postTelemetryNow(false);
        return;
    }

    if (gSolarLightMode == SolarLightMode::Dark ||
        gSolarLightMode == SolarLightMode::Shadow) {
        if (gNetworkRuntime.wifiEnabled || gNetworkRuntime.apEnabled ||
            gNetworkRuntime.staConnected) {
            stopWifiIfAllowed();
        }
    }
}
