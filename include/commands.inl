static void printHelp() {
    Serial.println("\nCommands:");
    Serial.println("  ping / scan / status / help");
    Serial.println("  rs485 [normal|invert|auto|sweep]");
    Serial.println("  modbus <addr>");
    Serial.println();
}

static void scanI2cBuses() {
    takeMutex(gDisplayBusMutex);
    for (uint8_t bus = 0; bus < kNumDisplayBuses; ++bus) {
        Serial.printf("I2C bus %u (SW SDA%u/SCL%u): [DISPLAY]\n", bus, kBusSda[bus], kBusScl[bus]);
        uint8_t found = 0;
        for (uint8_t address = 0x03; address < 0x78; ++address) {
            if (probeSoftwareI2c(kBusSda[bus], kBusScl[bus], address)) {
                Serial.printf("  0x%02X\n", address);
                ++found;
            }
        }
        if (!found) Serial.println("  (none)");
    }
    giveMutex(gDisplayBusMutex);

    takeMutex(gSensorBusMutex);
    Serial.printf("I2C bus %u (HW SDA%u/SCL%u): [SENSOR]\n",
        kSensorBusIndex, kBusSda[kSensorBusIndex], kBusScl[kSensorBusIndex]);
    uint8_t found = 0;
    for (uint8_t address = 0x03; address < 0x78; ++address) {
        if (probeHardwareI2c(Wire, address)) {
            Serial.printf("  0x%02X\n", address);
            ++found;
        }
    }
    if (!found) Serial.println("  (none)");
    giveMutex(gSensorBusMutex);
}

static void printStatus() {
    const TelemetryState snapshot = copyTelemetry();
    char batteryPercent[16];

    if (snapshot.batteryPercent >= 0.0f) snprintf(batteryPercent, sizeof(batteryPercent), "%.0f%%", snapshot.batteryPercent);
    else snprintf(batteryPercent, sizeof(batteryPercent), "--");

    Serial.println("\n== Weather Station Status ==");
    Serial.printf("  board:         %s\n", BoardConfig::kBoardName);
    Serial.printf("  displays:      %u/%u online\n", countOnlineDisplays(snapshot), kNumDisplays);
    Serial.printf("  bme280:        %s @ 0x%02X\n",
        snapshot.bme280Online ? "online" : "offline", snapshot.bme280Address);
    Serial.printf("  solar ina219:  %s @ 0x%02X  power=%.2fW voltage=%.2fV\n",
        snapshot.solarOnline ? "online" : "offline", BoardConfig::kIna219_1_Address,
        snapshot.solar.powerW, snapshot.solar.loadVoltageV);
    Serial.printf("  solar mode:    %s%s\n", solarLightModeLabel(gSolarLightMode),
        gDisplaysForcedOff ? " displays-off" : "");
    Serial.printf("  battery ina219:%s @ 0x%02X  power=%.2fW voltage=%.2fV battery=%s\n",
        snapshot.batteryOnline ? "online" : "offline", BoardConfig::kIna219_2_Address,
        snapshot.battery.powerW, snapshot.battery.loadVoltageV,
        batteryPercent);
    Serial.printf("  rs485:         baud=%lu fmt=%s inv=%s\n",
        (unsigned long)gRs485BaudActive, rs485ConfigLabel(gRs485SerialConfigActive),
        gRs485InvertActive ? "on" : "off");
    Serial.printf("  wind speed:    %s (0x%02X) %.1fm/s BFT%u\n",
        snapshot.windSpeedOnline ? "online" : "offline", gWindSpeedAddrActive,
        snapshot.wind.speedMs, snapshot.wind.beaufort);
    Serial.printf("  wind dir:      %s (0x%02X) %.0fdeg %s\n",
        snapshot.windDirOnline ? "online" : "offline", gWindDirAddrActive,
        normalizeRelativeWindDeg(snapshot.wind.directionDeg),
        windRelativeLabel(snapshot.wind.directionDeg));
    Serial.printf("  weather:       %.1fC  %.1f%%  %.1fhPa  dew %.1fC\n",
        snapshot.weather.temperatureC, snapshot.weather.humidityPct,
        snapshot.weather.pressureHpa, snapshot.weather.dewPointC);
    if (snapshot.forecast.ready) {
        Serial.printf("  forecast:      %s  3h trend=%+.1fhPa\n",
            forecastStatusLabel(snapshot.forecast.code), snapshot.forecast.delta3hHpa);
    } else {
        Serial.println("  forecast:      collecting 3h pressure history");
    }
    Serial.println();
}

static void handleCommand(String line) {
    line.trim();
    if (!line.length()) return;

    if (line.equalsIgnoreCase("ping")) {
        const TelemetryState snapshot = copyTelemetry();
        if (snapshot.batteryPercent >= 0.0f) {
            Serial.printf("pong board=%s temp=%.1fC battery=%.0f%%\n",
                BoardConfig::kBoardName, snapshot.weather.temperatureC, snapshot.batteryPercent);
        } else {
            Serial.printf("pong board=%s temp=%.1fC battery=--\n",
                BoardConfig::kBoardName, snapshot.weather.temperatureC);
        }
    } else if (line.equalsIgnoreCase("help")) {
        printHelp();
    } else if (line.equalsIgnoreCase("scan")) {
        scanI2cBuses();
    } else if (line.equalsIgnoreCase("status")) {
        printStatus();
    } else if (line.equalsIgnoreCase("rs485")) {
        Serial.printf("RS485: baud=%lu fmt=%s inv=%s\n",
            (unsigned long)gRs485BaudActive, rs485ConfigLabel(gRs485SerialConfigActive),
            gRs485InvertActive ? "on" : "off");
    } else if (line.equalsIgnoreCase("rs485 normal")) {
        beginRs485(gRs485BaudActive, gRs485SerialConfigActive, false);
    } else if (line.equalsIgnoreCase("rs485 invert")) {
        beginRs485(gRs485BaudActive, gRs485SerialConfigActive, true);
    } else if (line.equalsIgnoreCase("rs485 auto")) {
        initRs485();
    } else if (line.equalsIgnoreCase("rs485 sweep")) {
        rs485Sweep();
    } else if (line.startsWith("modbus ")) {
        const uint8_t address = (uint8_t)line.substring(7).toInt();
        if (address == 0 || address > 247) Serial.println("Usage: modbus <addr>  (1-247)");
        else modbusRawProbe(address);
    } else {
        Serial.println("Unknown command.");
        printHelp();
    }
}

static void readSerial() {
    while (Serial.available()) {
        const char ch = (char)Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (gSerialLine.length()) {
                handleCommand(gSerialLine);
                gSerialLine = "";
            }
        } else {
            gSerialLine += ch;
        }
    }
}
