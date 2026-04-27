static uint16_t crc16Modbus(const uint8_t *buffer, uint8_t length) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 1U) ? (crc >> 1U) ^ 0xA001U : crc >> 1U;
        }
    }
    return crc;
}

static void beginRs485(uint32_t baud, uint32_t serialConfig, bool invert) {
    Serial2.end();
    Serial2.begin(baud, serialConfig, BoardConfig::kRs485Rx, BoardConfig::kRs485Tx, invert);
    gRs485BaudActive = baud;
    gRs485SerialConfigActive = serialConfig;
    gRs485InvertActive = invert;
    taskDelayMs(10);
}

static bool parseModbusReadResponse(const uint8_t *buffer, uint8_t bufferLen,
                                    uint8_t address, uint8_t count, uint16_t *out) {
    const uint8_t frameLen = (uint8_t)(5 + count * 2);
    if (bufferLen < frameLen) return false;

    for (uint8_t i = 0; i + frameLen <= bufferLen; ++i) {
        if (buffer[i] != address || buffer[i + 1] != 0x03 || buffer[i + 2] != count * 2) continue;
        const uint16_t rxCrc = ((uint16_t)buffer[i + frameLen - 1] << 8) | buffer[i + frameLen - 2];
        if (crc16Modbus(&buffer[i], frameLen - 2) != rxCrc) continue;
        for (uint8_t j = 0; j < count; ++j) {
            out[j] = ((uint16_t)buffer[i + 3 + j * 2] << 8) | buffer[i + 4 + j * 2];
        }
        return true;
    }

    return false;
}

static bool modbusRead(uint8_t address, uint16_t reg, uint8_t count,
                       uint16_t *out, unsigned long timeoutMs = 500) {
    while (Serial2.available()) Serial2.read();

    uint8_t req[8] = {
        address, 0x03,
        (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
        0x00, count, 0x00, 0x00
    };

    const uint16_t crc = crc16Modbus(req, 6);
    req[6] = crc & 0xFF;
    req[7] = crc >> 8;

    if (BoardConfig::kRs485UseDeControl) digitalWrite(BoardConfig::kRs485De, HIGH);
    Serial2.write(req, sizeof(req));
    Serial2.flush();
    if (BoardConfig::kRs485UseDeControl) {
        delayMicroseconds(1200);
        digitalWrite(BoardConfig::kRs485De, LOW);
    }

    const uint8_t frameLen = (uint8_t)(5 + count * 2);
    uint8_t buffer[32];
    uint8_t bufferLen = 0;
    const uint32_t startMs = millis();

    while (bufferLen < sizeof(buffer) && !hasElapsedMs(millis(), startMs, timeoutMs)) {
        if (Serial2.available()) {
            buffer[bufferLen++] = (uint8_t)Serial2.read();
            if (parseModbusReadResponse(buffer, bufferLen, address, count, out)) return true;
            if (bufferLen >= frameLen && bufferLen < sizeof(buffer)) continue;
        } else {
            taskDelayMs(1);
        }
    }

    return parseModbusReadResponse(buffer, bufferLen, address, count, out);
}

static void rs485InterQueryDelay() {
    // Some low-cost sensors need a visibly larger quiet window between
    // back-to-back requests than the protocol minimum, especially when
    // sharing the bus with another node.
    taskDelayMs(BoardConfig::kRs485InterQueryGapMs);
}

static WindSample pollWindSensors(bool &speedOnline, bool &dirOnline) {
    WindSample sample = {};
    uint16_t regs[2];

    if (modbusRead(gWindSpeedAddrActive, 0x0000, 2, regs)) {
        sample.speedMs = regs[0] / 10.0f;
        sample.beaufort = (uint8_t)(regs[1] > 12 ? 12 : regs[1]);
        speedOnline = true;
    } else {
        speedOnline = false;
    }

    rs485InterQueryDelay();

    if (modbusRead(gWindDirAddrActive, 0x0000, 2, regs)) {
        sample.directionDeg = regs[0] / 10.0f;
        dirOnline = true;
    } else {
        dirOnline = false;
    }

    return sample;
}

static bool initRs485() {
    if (BoardConfig::kRs485UseDeControl) {
        pinMode(BoardConfig::kRs485De, OUTPUT);
        digitalWrite(BoardConfig::kRs485De, LOW);
    }

    beginRs485(BoardConfig::kModbusBaud, SERIAL_8N1, BoardConfig::kRs485Invert);

    auto probe = []() -> bool {
        uint16_t reg = 0;
        bool ok = modbusRead(BoardConfig::kWindSpeedAddr, 0x0000, 1, &reg);
        rs485InterQueryDelay();
        ok = modbusRead(BoardConfig::kWindDirAddr, 0x0000, 1, &reg) || ok;
        return ok;
    };

    if (probe()) return true;
    if (!BoardConfig::kRs485AutoDetectInvert) return false;

    beginRs485(BoardConfig::kModbusBaud, SERIAL_8N1, !BoardConfig::kRs485Invert);
    if (probe()) return true;

    beginRs485(BoardConfig::kModbusBaud, SERIAL_8N1, BoardConfig::kRs485Invert);
    return false;
}

static bool classifyModbusAddr(uint8_t address, bool &isDirectionSensor) {
    uint16_t reg = 0;
    if (!modbusRead(address, 0x0000, 1, &reg, 80)) return false;
    rs485InterQueryDelay();
    if (!modbusRead(address, 0x0002, 1, &reg, 80)) return false;
    isDirectionSensor = true;
    return true;
}

static void rs485Sweep() {
    static const uint32_t kBauds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};
    static const uint32_t kConfigs[] = {SERIAL_8N1, SERIAL_8E1, SERIAL_8O1};
    static const bool kInverts[] = {false, true};

    const uint32_t restoreBaud = gRs485BaudActive;
    const uint32_t restoreConfig = gRs485SerialConfigActive;
    const bool restoreInvert = gRs485InvertActive;
    const uint8_t restoreSpeedAddr = gWindSpeedAddrActive;
    const uint8_t restoreDirAddr = gWindDirAddrActive;

    bool foundSpeed = false;
    bool foundDir = false;

    Serial.println("RS485 sweep: scanning...");

    for (uint32_t baud : kBauds) {
        for (uint32_t serialConfig : kConfigs) {
            for (bool invert : kInverts) {
                beginRs485(baud, serialConfig, invert);
                for (uint8_t address = 1; address <= 8; ++address) {
                    bool isDirectionSensor = false;
                    if (!classifyModbusAddr(address, isDirectionSensor)) continue;

                    Serial.printf("  hit addr=0x%02X baud=%lu fmt=%s inv=%s type=%s\n",
                        address, (unsigned long)gRs485BaudActive,
                        rs485ConfigLabel(gRs485SerialConfigActive),
                        gRs485InvertActive ? "on" : "off",
                        isDirectionSensor ? "dir" : "speed");

                    if (isDirectionSensor && !foundDir) {
                        gWindDirAddrActive = address;
                        foundDir = true;
                    } else if (!isDirectionSensor && !foundSpeed) {
                        gWindSpeedAddrActive = address;
                        foundSpeed = true;
                    }

                    if (foundSpeed && foundDir) return;
                }
            }
        }
    }

    beginRs485(restoreBaud, restoreConfig, restoreInvert);
    gWindSpeedAddrActive = restoreSpeedAddr;
    gWindDirAddrActive = restoreDirAddr;
    Serial.println("RS485 sweep: no complete pair found.");
}

static void modbusRawProbe(uint8_t address) {
    while (Serial2.available()) Serial2.read();

    uint8_t req[8] = {address, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    const uint16_t crc = crc16Modbus(req, 6);
    req[6] = crc & 0xFF;
    req[7] = crc >> 8;

    Serial.printf("TX [0x%02X]:", address);
    for (uint8_t i = 0; i < sizeof(req); ++i) Serial.printf(" %02X", req[i]);
    Serial.println();

    if (BoardConfig::kRs485UseDeControl) digitalWrite(BoardConfig::kRs485De, HIGH);
    Serial2.write(req, sizeof(req));
    Serial2.flush();
    if (BoardConfig::kRs485UseDeControl) {
        delayMicroseconds(1200);
        digitalWrite(BoardConfig::kRs485De, LOW);
    }

    uint8_t buffer[32];
    uint8_t length = 0;
    const uint32_t startMs = millis();
    while (length < sizeof(buffer) && !hasElapsedMs(millis(), startMs, 600)) {
        if (Serial2.available()) {
            buffer[length++] = (uint8_t)Serial2.read();
        } else {
            taskDelayMs(1);
        }
    }

    Serial.print("RX:");
    if (!length) {
        Serial.println(" (none)");
        return;
    }

    for (uint8_t i = 0; i < length; ++i) Serial.printf(" %02X", buffer[i]);
    Serial.printf("  (%u bytes)\n", length);
}
