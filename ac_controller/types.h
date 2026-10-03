#pragma once

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include "config.h"

// IR Protocol Definition Entry
struct ProtocolEntry {
  int id;
  decode_type_t protocol;
  int16_t model;
  const char* name;
};

// AC Operational Settings
struct AcSettings {
  uint8_t temp;
  String mode;  // "cool", "fan", "dry"
  String fan;   // "auto", "low", "med", "high"
};

// Command Log Entry
struct LogEntry {
  String at;      // "HH:MM:SS"
  String date;    // "YYYY-MM-DD"
  String action;  // "on", "off"
  String source;  // "manual", "timer", "schedule"
};

// Recurring Schedule
struct Schedule {
  uint32_t id;
  bool enabled;
  String on;    // "HH:MM" or ""
  String off;   // "HH:MM" or ""
  bool days[7]; // Mon=0, Tue=1, Wed=2, Thu=3, Fri=4, Sat=5, Sun=6
  int last_fired_on_minute_id;
  int last_fired_off_minute_id;
};

// Sleep Timer State
struct TimerState {
  bool active;
  uint32_t remaining_s;
  String action;      // "off" or "on"
  time_t epoch_end;   // End timestamp (epoch seconds) for persistence
  uint32_t last_tick_ms;
};

// Auto-Scan State
struct ScanState {
  bool running;
  bool paused;
  int index;
  uint8_t interval_s;
  uint8_t next_in_s;
  uint8_t passes;
  uint32_t last_tick_ms;
};

// Master Global State
struct GlobalState {
  bool power_on;
  AcSettings ac;
  int active_protocol_id; // -1 if not selected
  TimerState timer;
  ScanState scan;
  Schedule schedules[kMaxSchedules];
  int schedule_count;
  uint32_t next_schedule_id;
  LogEntry logs[kMaxLogs];
  int log_count;
  LogEntry last_cmd;
  bool has_last_cmd;
};
