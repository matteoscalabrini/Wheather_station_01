#pragma once

#include <Adafruit_BME280.h>
#include <Adafruit_INA219.h>
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "app_types.h"
#include "board_config.h"

constexpr uint8_t kNumDisplays = 9;
constexpr uint8_t kNumDisplayBuses = 5;
constexpr uint8_t kSensorBusIndex = 5;
constexpr uint16_t kSoftwareI2cDelayUs = 4;
constexpr uint32_t kDisplayHeartbeatMs = BoardConfig::kDisplayHeartbeatMs;
constexpr uint32_t kSensorSampleMs = BoardConfig::kSensorSampleMs;
constexpr uint32_t kWindSampleMs = BoardConfig::kWindSampleMs;
constexpr uint32_t kCommandPollMs = BoardConfig::kCommandPollMs;
constexpr uint32_t kI2cMaintenanceMs = BoardConfig::kI2cMaintenanceMs;
constexpr uint16_t kDisplayMaskAll = (1U << kNumDisplays) - 1U;
constexpr uint16_t kDisplayMaskWeather = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3);
constexpr uint16_t kDisplayMaskWind = (1U << 4) | (1U << 5);
constexpr uint16_t kDisplayMaskSolar = (1U << 6);
constexpr uint16_t kDisplayMaskBattery = (1U << 7) | (1U << 8);
constexpr BaseType_t kDisplayTaskCore = 0;
constexpr BaseType_t kWorkerTaskCore = 1;
constexpr UBaseType_t kTaskPriority = 1;
constexpr uint32_t kDisplayTaskStack = 8192;
constexpr uint32_t kSensorTaskStack = 6144;
constexpr uint32_t kCommsTaskStack = 8192;
constexpr uint32_t kMaintenanceTaskStack = 6144;
constexpr size_t kForecastHistoryCapacity =
    (BoardConfig::kForecastLookbackMs / BoardConfig::kForecastSampleMs) + 4U;

extern const DisplaySlot kDisplaySlots[kNumDisplays];
extern const uint8_t kBusSda[kSensorBusIndex + 1];
extern const uint8_t kBusScl[kSensorBusIndex + 1];

extern TelemetryState gTelemetry;
extern SemaphoreHandle_t gTelemetryMutex;
extern SemaphoreHandle_t gDisplayBusMutex;
extern SemaphoreHandle_t gSensorBusMutex;
extern TaskHandle_t gDisplayTaskHandle;
extern TaskHandle_t gSensorTaskHandle;
extern TaskHandle_t gCommsTaskHandle;
extern TaskHandle_t gMaintenanceTaskHandle;

extern String gSerialLine;
extern uint8_t gWindSpeedAddrActive;
extern uint8_t gWindDirAddrActive;
extern uint32_t gRs485BaudActive;
extern uint32_t gRs485SerialConfigActive;
extern bool gRs485InvertActive;

extern Adafruit_BME280 gBme280;
extern Adafruit_INA219 gIna219Solar;
extern Adafruit_INA219 gIna219Battery;

extern U8G2 *gDisplays[kNumDisplays];
extern DisplayRuntimeState gDisplayRuntime[kNumDisplays];

extern ForecastHistoryPoint gForecastHistory[kForecastHistoryCapacity];
extern size_t gForecastHistoryCount;
extern size_t gForecastHistoryNext;
extern uint32_t gForecastLastSampleMs;

extern const DrawFunc kDrawFuncs[kNumDisplays];
