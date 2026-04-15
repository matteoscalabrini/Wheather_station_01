static void releaseI2cLine(uint8_t pin) {
    pinMode(pin, INPUT_PULLUP);
}

static void driveI2cLow(uint8_t pin) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

static void softwareI2cDelay() {
    delayMicroseconds(kSoftwareI2cDelayUs);
}

static bool waitForI2cHigh(uint8_t pin, uint32_t timeoutUs = 1000UL) {
    const uint32_t start = micros();
    while (digitalRead(pin) == LOW) {
        if ((uint32_t)(micros() - start) >= timeoutUs) return false;
    }
    return true;
}

static bool startSoftwareI2c(uint8_t sda, uint8_t scl) {
    releaseI2cLine(sda);
    releaseI2cLine(scl);
    softwareI2cDelay();
    if (digitalRead(sda) == LOW || digitalRead(scl) == LOW) return false;
    driveI2cLow(sda);
    softwareI2cDelay();
    driveI2cLow(scl);
    softwareI2cDelay();
    return true;
}

static bool writeSoftwareI2cByte(uint8_t sda, uint8_t scl, uint8_t value) {
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
        if (value & mask) releaseI2cLine(sda);
        else driveI2cLow(sda);
        softwareI2cDelay();
        releaseI2cLine(scl);
        if (!waitForI2cHigh(scl)) return false;
        softwareI2cDelay();
        driveI2cLow(scl);
        softwareI2cDelay();
    }

    releaseI2cLine(sda);
    softwareI2cDelay();
    releaseI2cLine(scl);
    if (!waitForI2cHigh(scl)) return false;
    const bool ack = digitalRead(sda) == LOW;
    softwareI2cDelay();
    driveI2cLow(scl);
    softwareI2cDelay();
    return ack;
}

static void stopSoftwareI2c(uint8_t sda, uint8_t scl) {
    driveI2cLow(sda);
    softwareI2cDelay();
    releaseI2cLine(scl);
    waitForI2cHigh(scl);
    softwareI2cDelay();
    releaseI2cLine(sda);
    softwareI2cDelay();
}

static bool probeSoftwareI2c(uint8_t sda, uint8_t scl, uint8_t address) {
    const bool started = startSoftwareI2c(sda, scl);
    bool ack = false;
    if (started) ack = writeSoftwareI2cByte(sda, scl, (uint8_t)(address << 1));
    stopSoftwareI2c(sda, scl);
    releaseI2cLine(sda);
    releaseI2cLine(scl);
    return started && ack;
}

static bool probeHardwareI2c(TwoWire &bus, uint8_t address) {
    bus.beginTransmission(address);
    return bus.endTransmission() == 0;
}
