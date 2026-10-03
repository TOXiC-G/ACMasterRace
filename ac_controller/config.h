#pragma once

#include <Arduino.h>
#include "secrets.h"

// Hardware Pin Configuration
// ESP32-C3 Super Mini pin connected to the IR LED module data pin (S)
// The module includes an on-board resistor and can be driven directly.
const uint16_t kIrLedPin = 4;

// Safety and Timing Constraints
const uint32_t kMinIrIntervalMs = 400;         // Minimum time allowed between IR sends
const uint32_t kRedundantOnThresholdMs = 60000; // Skip redundant ON within 60s
const uint32_t kWdtTimeoutMs = 8000;           // Task Watchdog timeout in ms

// Default AC State
const uint8_t  kDefaultTemp = 24;
const char* const kDefaultMode = "cool";
const char* const kDefaultFan  = "auto";

// Limits
const uint8_t  kMinTemp = 16;
const uint8_t  kMaxTemp = 30;
const uint16_t kMinTimerMinutes = 1;
const uint16_t kMaxTimerMinutes = 720;
const uint8_t  kMinScanIntervalS = 3;
const uint8_t  kMaxScanIntervalS = 10;
const uint8_t  kDefaultScanIntervalS = 4;
const uint8_t  kMaxScanPasses = 3;
const uint8_t  kMaxSchedules = 10;
const uint8_t  kMaxLogs = 10;

// NTP Configuration (IST: UTC + 5:30, No DST)
const long   kGmtOffsetSec      = 19800;  // 5 * 3600 + 30 * 60
const int    kDaylightOffsetSec = 0;
const char* const kNtpServer1   = "pool.ntp.org";
const char* const kNtpServer2   = "time.google.com";

// Serial Port Speed
const uint32_t kSerialBaudRate = 115200;
