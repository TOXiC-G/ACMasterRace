#pragma once

#include <Arduino.h>
#include "types.h"

// Parse "HH:MM" string and validate bounds (00:00 to 23:59)
bool validateTimeString(const String& str, int& hour, int& minute);

// Evaluate all active schedules against current local time (called once per second)
void evaluateSchedules(GlobalState& state);

// Update live sleep timer countdown (called once per second)
void updateTimerLoop(GlobalState& state);
