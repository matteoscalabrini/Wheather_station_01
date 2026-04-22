#include <math.h>
#include <string.h>

#include "app_state.h"

const DisplaySlot kDisplaySlots[kNumDisplays] = {
    {0, BoardConfig::kDisplay0Address}, {0, BoardConfig::kDisplay1Address},
    {1, BoardConfig::kDisplay2Address}, {1, BoardConfig::kDisplay3Address},
    {2, BoardConfig::kDisplay4Address}, {2, BoardConfig::kDisplay5Address},
    {3, BoardConfig::kDisplay6Address}, {3, BoardConfig::kDisplay7Address},
    {4, BoardConfig::kDisplay8Address},
};

const uint8_t kBusSda[kSensorBusIndex + 1] = {
    BoardConfig::kI2c0Sda, BoardConfig::kI2c1Sda, BoardConfig::kI2c2Sda,
    BoardConfig::kI2c3Sda, BoardConfig::kI2c4Sda, BoardConfig::kI2c5Sda
};

const uint8_t kBusScl[kSensorBusIndex + 1] = {
    BoardConfig::kI2c0Scl, BoardConfig::kI2c1Scl, BoardConfig::kI2c2Scl,
    BoardConfig::kI2c3Scl, BoardConfig::kI2c4Scl, BoardConfig::kI2c5Scl
};

TelemetryState gTelemetry = {
    {20.5f, 48.0f, 1012.0f, 9.3f, 20.5f},
    {ForecastCode::Waiting, 0.0f, false},
    {},
    {},
    {},
    {},
    false,
    false,
    false,
    false,
    false,
    0,
    -1.0f,
};

SemaphoreHandle_t gTelemetryMutex = nullptr;
SemaphoreHandle_t gDisplayBusMutex = nullptr;
SemaphoreHandle_t gSensorBusMutex = nullptr;
TaskHandle_t gDisplayTaskHandle = nullptr;

String gSerialLine;
uint8_t gWindSpeedAddrActive = BoardConfig::kWindSpeedAddr;
uint8_t gWindDirAddrActive = BoardConfig::kWindDirAddr;
uint32_t gRs485BaudActive = BoardConfig::kModbusBaud;
uint32_t gRs485SerialConfigActive = SERIAL_8N1;
bool gRs485InvertActive = BoardConfig::kRs485Invert;

Adafruit_BME280 gBme280;
Adafruit_INA219 gIna219Solar(BoardConfig::kIna219_1_Address);
Adafruit_INA219 gIna219Battery(BoardConfig::kIna219_2_Address);

static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp0(U8G2_R0, BoardConfig::kI2c0Scl, BoardConfig::kI2c0Sda, U8X8_PIN_NONE);
static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp1(U8G2_R0, BoardConfig::kI2c0Scl, BoardConfig::kI2c0Sda, U8X8_PIN_NONE);
static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp2(U8G2_R0, BoardConfig::kI2c1Scl, BoardConfig::kI2c1Sda, U8X8_PIN_NONE);
static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp3(U8G2_R0, BoardConfig::kI2c1Scl, BoardConfig::kI2c1Sda, U8X8_PIN_NONE);
static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp4(U8G2_R0, BoardConfig::kI2c2Scl, BoardConfig::kI2c2Sda, U8X8_PIN_NONE);
static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp5(U8G2_R0, BoardConfig::kI2c2Scl, BoardConfig::kI2c2Sda, U8X8_PIN_NONE);
static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp6(U8G2_R0, BoardConfig::kI2c3Scl, BoardConfig::kI2c3Sda, U8X8_PIN_NONE);
static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp7(U8G2_R0, BoardConfig::kI2c3Scl, BoardConfig::kI2c3Sda, U8X8_PIN_NONE);
static U8G2_SH1107_PIMORONI_128X128_1_SW_I2C gDisp8(U8G2_R0, BoardConfig::kI2c4Scl, BoardConfig::kI2c4Sda, U8X8_PIN_NONE);

U8G2 *gDisplays[kNumDisplays] = {
    &gDisp0, &gDisp1, &gDisp2, &gDisp3, &gDisp4,
    &gDisp5, &gDisp6, &gDisp7, &gDisp8,
};

DisplayRuntimeState gDisplayRuntime[kNumDisplays] = {};
ForecastHistoryPoint gForecastHistory[kForecastHistoryCapacity] = {};
size_t gForecastHistoryCount = 0;
size_t gForecastHistoryNext = 0;
uint32_t gForecastLastSampleMs = 0;

#include "calculations.inl"
#include "i2c_soft.inl"
#include "display.inl"
#include "rs485.inl"
#include "sensors.inl"
#include "commands.inl"
#include "tasks.inl"

void setup() {
    Serial.begin(115200);
    if (getCpuFrequencyMhz() != BoardConfig::kCpuFrequencyMhz) {
        if (!setCpuFrequencyMhz(BoardConfig::kCpuFrequencyMhz)) {
            Serial.printf("CPU frequency request %lu MHz rejected; keeping %lu MHz\n",
                (unsigned long)BoardConfig::kCpuFrequencyMhz,
                (unsigned long)getCpuFrequencyMhz());
        }
    }
    taskDelayMs(250);

    gTelemetryMutex = xSemaphoreCreateMutex();
    gDisplayBusMutex = xSemaphoreCreateMutex();
    gSensorBusMutex = xSemaphoreCreateMutex();

    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        const bool online = initDisplay(i);
        setDisplayOnline(i, online);
    }

    takeMutex(gSensorBusMutex);
    Wire.begin(BoardConfig::kI2c5Sda, BoardConfig::kI2c5Scl);
    Wire.setClock(100000UL);

    gTelemetry.bme280Address = detectBme280Address(Wire);
    if (gTelemetry.bme280Address) {
        gTelemetry.bme280Online = beginBme280OnBus(gTelemetry.bme280Address, gTelemetry.weather);
    }

    if (probeHardwareI2c(Wire, BoardConfig::kIna219_1_Address)) {
        gTelemetry.solarOnline = beginIna219OnBus(gIna219Solar, gTelemetry.solar);
    }

    if (probeHardwareI2c(Wire, BoardConfig::kIna219_2_Address)) {
        gTelemetry.batteryOnline = beginIna219OnBus(gIna219Battery, gTelemetry.battery);
        if (gTelemetry.batteryOnline) {
            gTelemetry.batteryPercent = computeBatteryPercent(gTelemetry.battery.loadVoltageV);
        }
    }
    giveMutex(gSensorBusMutex);

    if (gTelemetry.bme280Online) {
        const uint32_t nowMs = millis();
        recordForecastHistory(gTelemetry.weather, nowMs);
        gTelemetry.forecast = computeForecast(gTelemetry.weather, nowMs);
    }

    initRs485();
    {
        bool speedOnline = false;
        bool dirOnline = false;
        gTelemetry.wind = pollWindSensors(speedOnline, dirOnline);
        gTelemetry.windSpeedOnline = speedOnline;
        gTelemetry.windDirOnline = dirOnline;
    }

    Serial.printf("\n%s Weather Station\n", BoardConfig::kBoardName);
    Serial.printf("Sensor bus: SDA%d/SCL%d\n", BoardConfig::kI2c5Sda, BoardConfig::kI2c5Scl);
    printStatus();
    printHelp();
    {
        const TelemetryState snapshot = copyTelemetry();
        renderDisplayFrame(snapshot);
        primeDisplayRuntimeState(snapshot);
    }

    xTaskCreatePinnedToCore(sensorTask, "sensor-task", kSensorTaskStack,
        nullptr, kTaskPriority, nullptr, kWorkerTaskCore);
    xTaskCreatePinnedToCore(commsTask, "comms-task", kCommsTaskStack,
        nullptr, kTaskPriority, nullptr, kWorkerTaskCore);
    xTaskCreatePinnedToCore(maintenanceTask, "i2c-maint-task", kMaintenanceTaskStack,
        nullptr, kTaskPriority, nullptr, kWorkerTaskCore);
    xTaskCreatePinnedToCore(displayTask, "display-task", kDisplayTaskStack,
        nullptr, kTaskPriority, &gDisplayTaskHandle, kDisplayTaskCore);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
