#pragma once

#include <Arduino.h>
#include <time.h>
#include "types.h"

// Initialize Preferences (NVS) and load persisted state into GlobalState
void initStorage(GlobalState& state);

// Targeted savers (write only on change to limit flash wear)
void saveAssumedPower(bool power_on);
void saveAcSettings(const AcSettings& ac);
void saveActiveProtocol(int protocol_id);
void saveTimerStorage(uint32_t epoch_end, const String& action);
void clearTimerStorage();
void saveSchedulesStorage(const Schedule schedules[], int count, uint32_t next_id);

// Timer reboot check (to be invoked after NTP sync)
void checkTimerOnBoot(GlobalState& state);

// NTP time and date format helper
bool isNtpSynced();
bool getFormattedTimeAndDate(String& timeStr, String& dateStr);
