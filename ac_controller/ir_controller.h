#pragma once

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include "types.h"
#include "config.h"

// Initialize IR hardware and protocol list
void initIr(GlobalState& state);

// Protocol table inspection
int getProtocolCount();
const ProtocolEntry* getProtocolById(int id);
const ProtocolEntry* getProtocolByIndex(int index);

// Rate limiting
bool canSendIrNow();
uint32_t getMsUntilIrAllowed();
void markIrSent();

// Send IR AC signal
bool sendIrCommand(int protocol_id, bool power, uint8_t temp, const String& mode, const String& fan);

// High-level power execution (updates state, log, last_cmd, NVS)
bool executePowerCommand(GlobalState& state, bool turn_on, const char* source);

// Auto-scan routines
void startAutoScan(GlobalState& state, uint8_t interval_s);
void stopAutoScan(GlobalState& state);
void pauseAutoScan(GlobalState& state);
void resumeAutoScan(GlobalState& state);
void stepAutoScanNext(GlobalState& state);
void stepAutoScanPrev(GlobalState& state);
void resendAutoScan(GlobalState& state);
void updateAutoScanLoop(GlobalState& state);

// Helper state conversions
stdAc::opmode_t stringToOpmode(const String& mode);
stdAc::fanspeed_t stringToFanspeed(const String& fan);
String opmodeToString(stdAc::opmode_t mode);
String fanspeedToString(stdAc::fanspeed_t fan);
