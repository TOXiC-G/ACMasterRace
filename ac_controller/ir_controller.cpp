#include "ir_controller.h"
#include "storage.h"

// IRac instance
static IRac s_irac(kIrLedPin);
static stdAc::state_t s_prev_state;
static bool s_has_prev_state = false;
static uint32_t s_last_ir_send_ms = 0;
static uint32_t s_last_on_cmd_time_ms = 0;

// Up to 64 supported protocols in prioritized order
static const int kMaxSupportedProtocols = 64;
static ProtocolEntry s_protocols[kMaxSupportedProtocols];
static int s_protocol_count = 0;

struct CandidateProtocol {
  decode_type_t protocol;
  int16_t model;
  const char* name;
};

// Candidate list ordered by likelihood for Indian market & Voltas window AC:
// Voltas, Coolix, Gree, Hitachi, LG, Daikin, Carrier, Midea, Samsung,
// Panasonic, Haier, Whirlpool, Electra, TCL, Kelvinator, Fujitsu, Mitsubishi.
static const CandidateProtocol kCandidates[] = {
  // 1. Gree (model 2) - Default
  { decode_type_t::GREE, 2 /* YBOFB */, "Gree (model 2)" },

  // 2. Voltas
  { decode_type_t::VOLTAS, 1 /* kVoltas122LZF - Window AC */, "Voltas" },
  { decode_type_t::VOLTAS, 0 /* kVoltasUnknown */, "Voltas (alt)" },

  // 3. Coolix
  { decode_type_t::COOLIX, -1, "Coolix" },

  // 4. Gree other models
  { decode_type_t::GREE, 1 /* YAW1F */, "Gree" },
  { decode_type_t::GREE, 3 /* YX1FSF */, "Gree (model 3)" },

  // 4. Hitachi
  { decode_type_t::HITACHI_AC, -1, "Hitachi" },
  { decode_type_t::HITACHI_AC1, 1 /* R_LT0541_HTA_A */, "Hitachi (model 1A)" },
  { decode_type_t::HITACHI_AC1, 2 /* R_LT0541_HTA_B */, "Hitachi (model 1B)" },
  { decode_type_t::HITACHI_AC264, -1, "Hitachi (264)" },
  { decode_type_t::HITACHI_AC296, -1, "Hitachi (296)" },
  { decode_type_t::HITACHI_AC344, -1, "Hitachi (344)" },
  { decode_type_t::HITACHI_AC424, -1, "Hitachi (424)" },

  // 5. LG
  { decode_type_t::LG, 1 /* GE6711AR2853M */, "LG" },
  { decode_type_t::LG, 2 /* AKB75215403 */, "LG (model 2)" },
  { decode_type_t::LG, 3 /* AKB74955603 */, "LG (model 3)" },
  { decode_type_t::LG, 4 /* AKB73757604 */, "LG (model 4)" },
  { decode_type_t::LG, 5 /* LG6711A20083V */, "LG (model 5)" },
  { decode_type_t::LG2, -1, "LG2" },

  // 6. Daikin
  { decode_type_t::DAIKIN, -1, "Daikin" },
  { decode_type_t::DAIKIN2, -1, "Daikin 2" },
  { decode_type_t::DAIKIN64, -1, "Daikin (64)" },
  { decode_type_t::DAIKIN128, -1, "Daikin (128)" },
  { decode_type_t::DAIKIN152, -1, "Daikin (152)" },
  { decode_type_t::DAIKIN160, -1, "Daikin (160)" },
  { decode_type_t::DAIKIN176, -1, "Daikin (176)" },
  { decode_type_t::DAIKIN216, -1, "Daikin (216)" },
  { decode_type_t::DAIKIN312, -1, "Daikin (312)" },

  // 7. Carrier
  { decode_type_t::CARRIER_AC64, -1, "Carrier" },

  // 8. Midea
  { decode_type_t::MIDEA, -1, "Midea" },

  // 9. Samsung
  { decode_type_t::SAMSUNG_AC, -1, "Samsung" },

  // 10. Panasonic
  { decode_type_t::PANASONIC_AC, 1 /* kPanasonicLke */, "Panasonic" },
  { decode_type_t::PANASONIC_AC, 2 /* kPanasonicNke */, "Panasonic (NKE)" },
  { decode_type_t::PANASONIC_AC, 3 /* kPanasonicDke */, "Panasonic (DKE)" },
  { decode_type_t::PANASONIC_AC, 4 /* kPanasonicJke */, "Panasonic (JKE)" },
  { decode_type_t::PANASONIC_AC, 5 /* kPanasonicCkp */, "Panasonic (CKP)" },
  { decode_type_t::PANASONIC_AC, 6 /* kPanasonicRkr */, "Panasonic (RKR)" },
  { decode_type_t::PANASONIC_AC32, -1, "Panasonic (32)" },

  // 11. Haier
  { decode_type_t::HAIER_AC, -1, "Haier" },
  { decode_type_t::HAIER_AC160, -1, "Haier (160)" },
  { decode_type_t::HAIER_AC176, 1 /* V9014557_A */, "Haier (176-A)" },
  { decode_type_t::HAIER_AC176, 2 /* V9014557_B */, "Haier (176-B)" },
  { decode_type_t::HAIER_AC_YRW02, -1, "Haier (YRW02)" },

  // 12. Whirlpool
  { decode_type_t::WHIRLPOOL_AC, 1 /* DG11J13A */, "Whirlpool" },
  { decode_type_t::WHIRLPOOL_AC, 2 /* DG11J191 */, "Whirlpool (model 2)" },

  // 13. Electra
  { decode_type_t::ELECTRA_AC, -1, "Electra" },

  // 14. TCL
  { decode_type_t::TCL112AC, 1 /* TAC09CHSD */, "TCL" },
  { decode_type_t::TCL112AC, 2 /* GZ055BE1 */, "TCL (model 2)" },

  // 15. Kelvinator
  { decode_type_t::KELVINATOR, -1, "Kelvinator" },

  // 16. Fujitsu
  { decode_type_t::FUJITSU_AC, 1 /* ARRAH2E */, "Fujitsu" },
  { decode_type_t::FUJITSU_AC, 2 /* ARDB1 */, "Fujitsu (model 2)" },
  { decode_type_t::FUJITSU_AC, 3 /* ARREB1E */, "Fujitsu (model 3)" },
  { decode_type_t::FUJITSU_AC, 4 /* ARJW2 */, "Fujitsu (model 4)" },
  { decode_type_t::FUJITSU_AC, 5 /* ARRY4 */, "Fujitsu (model 5)" },
  { decode_type_t::FUJITSU_AC, 6 /* ARREW4E */, "Fujitsu (model 6)" },

  // 17. Mitsubishi
  { decode_type_t::MITSUBISHI_AC, -1, "Mitsubishi" },
  { decode_type_t::MITSUBISHI112, -1, "Mitsubishi (112)" },
  { decode_type_t::MITSUBISHI136, -1, "Mitsubishi (136)" },
  { decode_type_t::MITSUBISHI_HEAVY_88, -1, "Mitsubishi Heavy (88)" },
  { decode_type_t::MITSUBISHI_HEAVY_152, -1, "Mitsubishi Heavy (152)" }
};

void initIr(GlobalState& state) {
  pinMode(kIrLedPin, OUTPUT);
  digitalWrite(kIrLedPin, LOW);
  s_protocol_count = 0;

  const size_t numCandidates = sizeof(kCandidates) / sizeof(kCandidates[0]);
  for (size_t i = 0; i < numCandidates && s_protocol_count < kMaxSupportedProtocols; i++) {
    if (IRac::isProtocolSupported(kCandidates[i].protocol)) {
      s_protocols[s_protocol_count].id = s_protocol_count;
      s_protocols[s_protocol_count].protocol = kCandidates[i].protocol;
      s_protocols[s_protocol_count].model = kCandidates[i].model;
      s_protocols[s_protocol_count].name = kCandidates[i].name;
      s_protocol_count++;
    }
  }

  Serial.printf("[IR] Initialized with %d supported AC protocols\n", s_protocol_count);

  // Find Gree (model 2) default ID
  int defaultProtoId = 0;
  for (int i = 0; i < s_protocol_count; i++) {
    if (s_protocols[i].protocol == decode_type_t::GREE && s_protocols[i].model == 2) {
      defaultProtoId = s_protocols[i].id;
      break;
    }
  }

  // Validate active_protocol_id from storage; default to Gree (model 2) if not set or out of bounds
  if (state.active_protocol_id < 0 || state.active_protocol_id >= s_protocol_count) {
    state.active_protocol_id = defaultProtoId;
    saveActiveProtocol(state.active_protocol_id);
  }

  Serial.printf("[IR] Active protocol set to #%d (%s)\n",
                state.active_protocol_id,
                (state.active_protocol_id >= 0 && state.active_protocol_id < s_protocol_count)
                  ? s_protocols[state.active_protocol_id].name : "Unknown");
}

int getProtocolCount() {
  return s_protocol_count;
}

const ProtocolEntry* getProtocolById(int id) {
  if (id < 0 || id >= s_protocol_count) return nullptr;
  return &s_protocols[id];
}

const ProtocolEntry* getProtocolByIndex(int index) {
  if (index < 0 || index >= s_protocol_count) return nullptr;
  return &s_protocols[index];
}

bool canSendIrNow() {
  return (millis() - s_last_ir_send_ms) >= kMinIrIntervalMs;
}

uint32_t getMsUntilIrAllowed() {
  uint32_t elapsed = millis() - s_last_ir_send_ms;
  if (elapsed >= kMinIrIntervalMs) return 0;
  return kMinIrIntervalMs - elapsed;
}

void markIrSent() {
  s_last_ir_send_ms = millis();
}

stdAc::opmode_t stringToOpmode(const String& mode) {
  if (mode.equalsIgnoreCase("fan")) return stdAc::opmode_t::kFan;
  if (mode.equalsIgnoreCase("dry")) return stdAc::opmode_t::kDry;
  return stdAc::opmode_t::kCool;
}

String opmodeToString(stdAc::opmode_t mode) {
  switch (mode) {
    case stdAc::opmode_t::kFan: return "fan";
    case stdAc::opmode_t::kDry: return "dry";
    default:                    return "cool";
  }
}

stdAc::fanspeed_t stringToFanspeed(const String& fan) {
  if (fan.equalsIgnoreCase("low"))  return stdAc::fanspeed_t::kLow;
  if (fan.equalsIgnoreCase("med"))  return stdAc::fanspeed_t::kMedium;
  if (fan.equalsIgnoreCase("high")) return stdAc::fanspeed_t::kHigh;
  return stdAc::fanspeed_t::kAuto;
}

String fanspeedToString(stdAc::fanspeed_t fan) {
  switch (fan) {
    case stdAc::fanspeed_t::kLow:    return "low";
    case stdAc::fanspeed_t::kMedium: return "med";
    case stdAc::fanspeed_t::kHigh:   return "high";
    default:                         return "auto";
  }
}

bool sendIrCommand(int protocol_id, bool power, uint8_t temp, const String& mode, const String& fan) {
  const ProtocolEntry* proto = getProtocolById(protocol_id);
  if (!proto) return false;

  stdAc::state_t desired;
  s_irac.initState(&desired);

  desired.protocol = proto->protocol;
  desired.model    = proto->model;
  desired.power    = power;
  desired.mode     = stringToOpmode(mode);
  desired.degrees  = temp;
  desired.celsius  = true;
  desired.fanspeed = stringToFanspeed(fan);
  desired.swingv   = stdAc::swingv_t::kOff;
  desired.swingh   = stdAc::swingh_t::kOff;
  desired.quiet    = false;
  desired.turbo    = false;
  desired.econo    = false;
  desired.light    = false;
  desired.filter   = false;
  desired.clean    = false;
  desired.beep     = false;
  desired.sleep    = -1;
  desired.clock    = -1;

  // Pass previous state pointer if previous state matches same protocol
  const stdAc::state_t* prevPtr = (s_has_prev_state && s_prev_state.protocol == desired.protocol) ? &s_prev_state : nullptr;

  s_irac.sendAc(desired, prevPtr);
  s_prev_state = desired;
  s_has_prev_state = true;
  s_last_ir_send_ms = millis();
  if (power) {
    s_last_on_cmd_time_ms = s_last_ir_send_ms;
  }

  return true;
}

bool executePowerCommand(GlobalState& state, bool turn_on, const char* source) {
  if (state.active_protocol_id < 0) {
    Serial.println("[IR] Error: No protocol configured");
    return false;
  }

  // Ensure minimum interval between sends
  uint32_t waitMs = getMsUntilIrAllowed();
  if (waitMs > 0) {
    delay(waitMs);
  }

  if (!sendIrCommand(state.active_protocol_id, turn_on, state.ac.temp, state.ac.mode, state.ac.fan)) {
    return false;
  }

  state.power_on = turn_on;
  saveAssumedPower(state.power_on);

  // Retrieve current time for logging
  String timeStr, dateStr;
  if (!getFormattedTimeAndDate(timeStr, dateStr)) {
    timeStr = "--:--:--";
    dateStr = "";
  }

  LogEntry entry;
  entry.at     = timeStr;
  entry.date   = dateStr;
  entry.action = turn_on ? "on" : "off";
  entry.source = source;

  // Prepend to command logs (most recent first)
  for (int i = kMaxLogs - 1; i > 0; i--) {
    state.logs[i] = state.logs[i - 1];
  }
  state.logs[0] = entry;
  if (state.log_count < kMaxLogs) {
    state.log_count++;
  }

  state.last_cmd = entry;
  state.has_last_cmd = true;

  Serial.printf("[CMD] Power %s (source: %s, temp: %d, mode: %s, fan: %s)\n",
                turn_on ? "ON" : "OFF", source, state.ac.temp, state.ac.mode.c_str(), state.ac.fan.c_str());

  return true;
}

void startAutoScan(GlobalState& state, uint8_t interval_s) {
  if (interval_s < kMinScanIntervalS) interval_s = kMinScanIntervalS;
  if (interval_s > kMaxScanIntervalS) interval_s = kMaxScanIntervalS;

  state.scan.running = true;
  state.scan.paused = false;
  state.scan.index = 0;
  state.scan.interval_s = interval_s;
  state.scan.next_in_s = interval_s;
  state.scan.passes = 0;
  state.scan.last_tick_ms = millis();

  // Send ON immediately with protocol 0 (temp 24, cool, auto)
  if (canSendIrNow()) {
    sendIrCommand(0, true, 24, "cool", "auto");
  }
  Serial.printf("[SCAN] Started scan at index 0 (interval: %ds)\n", interval_s);
}

void stopAutoScan(GlobalState& state) {
  state.scan.running = false;
  state.scan.paused = false;
  state.scan.next_in_s = 0;
  Serial.println("[SCAN] Stopped");
}

void pauseAutoScan(GlobalState& state) {
  if (!state.scan.running) return;
  state.scan.paused = true;
  Serial.println("[SCAN] Paused");
}

void resumeAutoScan(GlobalState& state) {
  if (!state.scan.running) return;
  state.scan.paused = false;
  state.scan.last_tick_ms = millis();
  Serial.println("[SCAN] Resumed");
}

void stepAutoScanNext(GlobalState& state) {
  if (!state.scan.running) return;
  state.scan.index = (state.scan.index + 1) % s_protocol_count;
  state.scan.next_in_s = state.scan.interval_s;
  state.scan.last_tick_ms = millis();
  sendIrCommand(state.scan.index, true, 24, "cool", "auto");
  Serial.printf("[SCAN] Step NEXT to index %d\n", state.scan.index);
}

void stepAutoScanPrev(GlobalState& state) {
  if (!state.scan.running) return;
  state.scan.index = (state.scan.index - 1 + s_protocol_count) % s_protocol_count;
  state.scan.next_in_s = state.scan.interval_s;
  state.scan.last_tick_ms = millis();
  sendIrCommand(state.scan.index, true, 24, "cool", "auto");
  Serial.printf("[SCAN] Step PREV to index %d\n", state.scan.index);
}

void resendAutoScan(GlobalState& state) {
  if (!state.scan.running) return;
  state.scan.next_in_s = state.scan.interval_s;
  state.scan.last_tick_ms = millis();
  sendIrCommand(state.scan.index, true, 24, "cool", "auto");
  Serial.printf("[SCAN] Resent ON for index %d\n", state.scan.index);
}

void updateAutoScanLoop(GlobalState& state) {
  if (!state.scan.running || state.scan.paused) return;

  uint32_t now = millis();
  if (now - state.scan.last_tick_ms >= 1000) {
    state.scan.last_tick_ms = now;

    if (state.scan.next_in_s > 1) {
      state.scan.next_in_s--;
    } else {
      // Step to next protocol
      state.scan.index = (state.scan.index + 1) % s_protocol_count;
      if (state.scan.index == 0) {
        state.scan.passes++;
        if (state.scan.passes >= kMaxScanPasses) {
          stopAutoScan(state);
          Serial.println("[SCAN] Completed 3 full passes. Stopped.");
          return;
        }
      }
      state.scan.next_in_s = state.scan.interval_s;
      sendIrCommand(state.scan.index, true, 24, "cool", "auto");
      Serial.printf("[SCAN] Auto-step to index %d (pass %d/%d)\n",
                    state.scan.index, state.scan.passes + 1, kMaxScanPasses);
    }
  }
}
