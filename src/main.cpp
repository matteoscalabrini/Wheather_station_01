#include <math.h>
#include <string.h>

#include "app_state.h"
#include <esp_task_wdt.h>

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
TaskHandle_t gSensorTaskHandle = nullptr;
TaskHandle_t gCommsTaskHandle = nullptr;
TaskHandle_t gMaintenanceTaskHandle = nullptr;
TaskHandle_t gNetworkTaskHandle = nullptr;

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
SolarLightMode gSolarLightMode = SolarLightMode::Unknown;
uint32_t gSolarDarkSinceMs = 0;
bool gDisplaysForcedOff = false;
bool gBootedFromTimerWake = false;
bool gDarkWakePostOnly = false;
bool gDarkWakePostDue = false;
bool gDarkTimerWakeEvaluated = false;
RTC_DATA_ATTR uint32_t gDarkTimerWakeCount = 0;
RuntimeSettings gSettings = {};
NetworkRuntimeState gNetworkRuntime = {};
OtaUploadState gOtaUpload = {};
WebServer gWebServer(80);
DNSServer gDnsServer;
Preferences gSettingsPrefs;

#include "calculations.inl"
#include "settings_runtime.inl"
#include "i2c_soft.inl"
#include "display.inl"
#include "power_policy.inl"
#include "rs485.inl"
#include "sensors.inl"
#include "commands.inl"
#include "network_runtime.inl"
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
    gBootedFromTimerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    loadRuntimeSettings();
    if (!validRuntimeSettings()) {
        Serial.println("Settings invalid; reverting to defaults");
        loadDefaultRuntimeSettings();
        saveRuntimeSettings();
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

    updateSolarPowerPolicy(gTelemetry.solar, gTelemetry.solarOnline);
    initializeNetworkRuntime();

    if (!gDarkWakePostOnly) {
        for (uint8_t i = 0; i < kNumDisplays; ++i) {
            const bool online = initDisplay(i);
            setDisplayOnline(i, online);
        }
        applyDisplayContrastForSolarMode(gSolarLightMode, true);
    }

    if (gTelemetry.bme280Online) {
        const uint32_t nowMs = millis();
        recordForecastHistory(gTelemetry.weather, nowMs);
        gTelemetry.forecast = computeForecast(gTelemetry.weather, nowMs);
    }

    if (!gDarkWakePostOnly) {
        initRs485();
        {
            bool speedOnline = false;
            bool dirOnline = false;
            gTelemetry.wind = pollWindSensors(speedOnline, dirOnline);
            gTelemetry.windSpeedOnline = speedOnline;
            gTelemetry.windDirOnline = dirOnline;
        }
    }

    Serial.printf("\n%s Weather Station\n", BoardConfig::kBoardName);
    Serial.printf("Sensor bus: SDA%d/SCL%d\n", BoardConfig::kI2c5Sda, BoardConfig::kI2c5Scl);
    printStatus();
    printHelp();
    if (!gDarkWakePostOnly) {
        const TelemetryState snapshot = copyTelemetry();
        renderDisplayFrame(snapshot);
        primeDisplayRuntimeState(snapshot);
    } else {
        Serial.println("Dark timer wake: posting telemetry only, then returning to deep sleep");
    }

    if (!gDarkWakePostOnly) {
        xTaskCreatePinnedToCore(sensorTask, "sensor-task", kSensorTaskStack,
            nullptr, kTaskPriority, &gSensorTaskHandle, kWorkerTaskCore);
        xTaskCreatePinnedToCore(commsTask, "comms-task", kCommsTaskStack,
            nullptr, kTaskPriority, &gCommsTaskHandle, kWorkerTaskCore);
        xTaskCreatePinnedToCore(maintenanceTask, "i2c-maint-task", kMaintenanceTaskStack,
            nullptr, kTaskPriority, &gMaintenanceTaskHandle, kWorkerTaskCore);
        xTaskCreatePinnedToCore(displayTask, "display-task", kDisplayTaskStack,
            nullptr, kTaskPriority, &gDisplayTaskHandle, kDisplayTaskCore);
    }
    xTaskCreatePinnedToCore(networkTask, "network-task", kNetworkTaskStack,
        nullptr, kTaskPriority, &gNetworkTaskHandle, kWorkerTaskCore);

    {
        if (esp_task_wdt_init(BoardConfig::kTaskWatchdogTimeoutS, false) == ESP_OK) {
            if (gSensorTaskHandle) esp_task_wdt_add(gSensorTaskHandle);
            if (gCommsTaskHandle) esp_task_wdt_add(gCommsTaskHandle);
            if (gMaintenanceTaskHandle) esp_task_wdt_add(gMaintenanceTaskHandle);
            if (gDisplayTaskHandle) esp_task_wdt_add(gDisplayTaskHandle);
            if (gNetworkTaskHandle) esp_task_wdt_add(gNetworkTaskHandle);
        } else {
            Serial.println("Watchdog init failed");
        }
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
