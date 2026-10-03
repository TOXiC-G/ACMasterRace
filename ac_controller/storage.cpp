#include "storage.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include "ir_controller.h"

static Preferences s_prefs;
static bool s_timer_boot_checked = false;

void initStorage(GlobalState& state) {
  s_prefs.begin("ac_ctrl", false);

  state.power_on = s_prefs.getBool("power", false);
  state.ac.temp  = s_prefs.getUChar("temp", kDefaultTemp);
  state.ac.mode  = s_prefs.getString("mode", kDefaultMode);
  state.ac.fan   = s_prefs.getString("fan", kDefaultFan);
  state.active_protocol_id = s_prefs.getInt("proto_id", -1);
  state.next_schedule_id   = s_prefs.getUInt("sched_next_id", 1);
  if (state.next_schedule_id == 0) state.next_schedule_id = 1;

  // Validate bounds
  if (state.ac.temp < kMinTemp || state.ac.temp > kMaxTemp) {
    state.ac.temp = kDefaultTemp;
  }
  if (state.ac.mode != "cool" && state.ac.mode != "fan" && state.ac.mode != "dry") {
    state.ac.mode = kDefaultMode;
  }
  if (state.ac.fan != "auto" && state.ac.fan != "low" && state.ac.fan != "med" && state.ac.fan != "high") {
    state.ac.fan = kDefaultFan;
  }

  // Load schedules
  state.schedule_count = 0;
  String schedJson = s_prefs.getString("schedules", "[]");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, schedJson);
  if (!err && doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
      if (state.schedule_count >= kMaxSchedules) break;
      Schedule& s = state.schedules[state.schedule_count];
      s.id = obj["id"] | state.next_schedule_id;
      s.enabled = obj["enabled"] | true;
      s.on = obj["on"].is<const char*>() ? obj["on"].as<String>() : "";
      s.off = obj["off"].is<const char*>() ? obj["off"].as<String>() : "";
      s.last_fired_on_minute_id = -1;
      s.last_fired_off_minute_id = -1;

      JsonArray days = obj["days"];
      for (int d = 0; d < 7; d++) {
        s.days[d] = (days && d < (int)days.size()) ? (days[d].as<int>() != 0) : false;
      }
      state.schedule_count++;
    }
  }

  // Timer is initially inactive until checked against NTP
  state.timer.active = false;
  state.timer.remaining_s = 0;
  state.timer.action = "off";
  state.timer.epoch_end = 0;
  state.timer.last_tick_ms = millis();

  // Scan state initially idle
  state.scan.running = false;
  state.scan.paused = false;
  state.scan.index = 0;
  state.scan.interval_s = kDefaultScanIntervalS;
  state.scan.next_in_s = 0;
  state.scan.passes = 0;
  state.scan.last_tick_ms = millis();

  state.log_count = 0;
  state.has_last_cmd = false;

  Serial.printf("[NVS] Loaded: Power=%s, Temp=%d, Mode=%s, Fan=%s, ProtoID=%d, Schedules=%d\n",
                state.power_on ? "ON" : "OFF", state.ac.temp, state.ac.mode.c_str(),
                state.ac.fan.c_str(), state.active_protocol_id, state.schedule_count);
}

void saveAssumedPower(bool power_on) {
  if (s_prefs.getBool("power", false) != power_on) {
    s_prefs.putBool("power", power_on);
  }
}

void saveAcSettings(const AcSettings& ac) {
  if (s_prefs.getUChar("temp", 0) != ac.temp) {
    s_prefs.putUChar("temp", ac.temp);
  }
  if (s_prefs.getString("mode", "") != ac.mode) {
    s_prefs.putString("mode", ac.mode);
  }
  if (s_prefs.getString("fan", "") != ac.fan) {
    s_prefs.putString("fan", ac.fan);
  }
}

void saveActiveProtocol(int protocol_id) {
  if (s_prefs.getInt("proto_id", -1) != protocol_id) {
    s_prefs.putInt("proto_id", protocol_id);
  }
}

void saveTimerStorage(uint32_t epoch_end, const String& action) {
  s_prefs.putUInt("timer_epoch", epoch_end);
  s_prefs.putString("timer_act", action);
}

void clearTimerStorage() {
  s_prefs.putUInt("timer_epoch", 0);
  s_prefs.putString("timer_act", "");
}

void saveSchedulesStorage(const Schedule schedules[], int count, uint32_t next_id) {
  s_prefs.putUInt("sched_next_id", next_id);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = schedules[i].id;
    obj["enabled"] = schedules[i].enabled;
    if (schedules[i].on.length() > 0) obj["on"] = schedules[i].on;
    else obj["on"] = nullptr;
    if (schedules[i].off.length() > 0) obj["off"] = schedules[i].off;
    else obj["off"] = nullptr;

    JsonArray days = obj["days"].to<JsonArray>();
    for (int d = 0; d < 7; d++) {
      days.add(schedules[i].days[d] ? 1 : 0);
    }
  }

  String jsonStr;
  serializeJson(doc, jsonStr);
  s_prefs.putString("schedules", jsonStr);
}

void checkTimerOnBoot(GlobalState& state) {
  if (s_timer_boot_checked) return;
  if (!isNtpSynced()) return; // Must have valid clock before comparing epoch

  s_timer_boot_checked = true;
  uint32_t saved_epoch = s_prefs.getUInt("timer_epoch", 0);
  if (saved_epoch == 0) return;

  String action = s_prefs.getString("timer_act", "off");
  time_t now = time(nullptr);

  clearTimerStorage();

  if (now >= (time_t)saved_epoch) {
    long lateSec = (long)(now - (time_t)saved_epoch);
    if (lateSec <= 300) {
      Serial.printf("[TIMER] Catch-up firing reboot timer (%ld sec late)\n", lateSec);
      executePowerCommand(state, (action == "on"), "timer");
    } else {
      Serial.printf("[TIMER] Dropping expired timer from before reboot (%ld sec late)\n", lateSec);
    }
  } else {
    state.timer.active = true;
    state.timer.remaining_s = (uint32_t)(saved_epoch - now);
    state.timer.action = action;
    state.timer.epoch_end = saved_epoch;
    state.timer.last_tick_ms = millis();
    saveTimerStorage(saved_epoch, action);
    Serial.printf("[TIMER] Resumed pending timer (%u sec remaining)\n", state.timer.remaining_s);
  }
}

bool isNtpSynced() {
  time_t now = time(nullptr);
  return (now >= 1700000000); // 2023+
}

bool getFormattedTimeAndDate(String& timeStr, String& dateStr) {
  if (!isNtpSynced()) return false;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;

  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  timeStr = tBuf;

  char dBuf[16];
  snprintf(dBuf, sizeof(dBuf), "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  dateStr = dBuf;
  return true;
}
