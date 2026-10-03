#include "schedules.h"
#include "ir_controller.h"
#include "storage.h"

static uint32_t s_last_eval_ms = 0;
static uint32_t s_last_timer_tick_ms = 0;
static uint32_t s_last_sched_on_ms = 0;

bool validateTimeString(const String& str, int& hour, int& minute) {
  if (str.length() != 5) return false;
  if (str.charAt(2) != ':') return false;

  for (int i = 0; i < 5; i++) {
    if (i == 2) continue;
    if (!isDigit(str.charAt(i))) return false;
  }

  hour = str.substring(0, 2).toInt();
  minute = str.substring(3, 5).toInt();

  return (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59);
}

void evaluateSchedules(GlobalState& state) {
  uint32_t now = millis();
  if (now - s_last_eval_ms < 1000) return;
  s_last_eval_ms = now;

  if (!isNtpSynced()) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  // Day index: Monday=0, Tuesday=1, ..., Sunday=6
  int dayIndex = (timeinfo.tm_wday + 6) % 7;
  int curMinOfDay = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int curMinuteId = timeinfo.tm_yday * 1440 + curMinOfDay;

  for (int i = 0; i < state.schedule_count; i++) {
    Schedule& s = state.schedules[i];
    if (!s.enabled) continue;
    if (!s.days[dayIndex]) continue;

    // Check ON event
    if (s.on.length() == 5) {
      int onH = 0, onM = 0;
      if (validateTimeString(s.on, onH, onM)) {
        if (curMinOfDay == onH * 60 + onM) {
          if (s.last_fired_on_minute_id != curMinuteId) {
            s.last_fired_on_minute_id = curMinuteId;

            // Redundant ON check: skip only if assumed on AND last ON command was < 60s ago
            bool skip = false;
            if (state.power_on) {
              if (s_last_sched_on_ms > 0 && (now - s_last_sched_on_ms < kRedundantOnThresholdMs)) {
                skip = true;
              }
            }

            if (!skip) {
              s_last_sched_on_ms = now;
              Serial.printf("[SCHED] Firing Schedule #%u ON\n", s.id);
              executePowerCommand(state, true, "schedule");
            } else {
              Serial.printf("[SCHED] Skipping redundant ON for Schedule #%u (<60s ago)\n", s.id);
            }
          }
        }
      }
    }

    // Check OFF event (always sent, even if assumed off, so physical unit recovers)
    if (s.off.length() == 5) {
      int offH = 0, offM = 0;
      if (validateTimeString(s.off, offH, offM)) {
        if (curMinOfDay == offH * 60 + offM) {
          if (s.last_fired_off_minute_id != curMinuteId) {
            s.last_fired_off_minute_id = curMinuteId;
            Serial.printf("[SCHED] Firing Schedule #%u OFF\n", s.id);
            executePowerCommand(state, false, "schedule");
          }
        }
      }
    }
  }
}

void updateTimerLoop(GlobalState& state) {
  if (!state.timer.active) return;

  uint32_t now = millis();
  if (now - s_last_timer_tick_ms < 1000) return;
  s_last_timer_tick_ms = now;

  if (state.timer.remaining_s > 0) {
    state.timer.remaining_s--;
  }

  if (state.timer.remaining_s == 0) {
    state.timer.active = false;
    clearTimerStorage();

    bool turn_on = (state.timer.action == "on");
    Serial.printf("[TIMER] Countdown expired, triggering power %s\n", turn_on ? "ON" : "OFF");
    executePowerCommand(state, turn_on, "timer");
  }
}
