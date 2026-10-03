#include "web_server.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Update.h>
#include "index_html.h"
#include "ir_controller.h"
#include "storage.h"
#include "schedules.h"

static void setCorsHeaders(WebServer& server) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

static void sendJson(WebServer& server, int statusCode, const JsonDocument& doc) {
  setCorsHeaders(server);
  String response;
  serializeJson(doc, response);
  server.send(statusCode, "application/json; charset=utf-8", response);
}

static void sendJsonError(WebServer& server, int statusCode, const char* message) {
  setCorsHeaders(server);
  JsonDocument doc;
  doc["error"] = message;
  String response;
  serializeJson(doc, response);
  server.send(statusCode, "application/json; charset=utf-8", response);
}

static void buildStateJson(JsonDocument& doc, const GlobalState& state) {
  doc["power"] = state.power_on ? "on" : "off";
  doc["confirmed"] = false;

  String timeStr, dateStr;
  if (getFormattedTimeAndDate(timeStr, dateStr)) {
    doc["time"] = timeStr;
    doc["date"] = dateStr;
  }
  // If NTP has not synced yet, "time" and "date" keys are omitted entirely.

  if (state.timer.active && state.timer.remaining_s > 0) {
    JsonObject t = doc["timer"].to<JsonObject>();
    t["remaining_s"] = state.timer.remaining_s;
    t["action"] = state.timer.action;
  } else {
    doc["timer"] = nullptr;
  }

  JsonObject ac = doc["ac"].to<JsonObject>();
  ac["temp"] = state.ac.temp;
  ac["mode"] = state.ac.mode;
  ac["fan"]  = state.ac.fan;

  if (state.has_last_cmd) {
    JsonObject lc = doc["last_cmd"].to<JsonObject>();
    lc["action"] = state.last_cmd.action;
    lc["at"]     = state.last_cmd.at;
    lc["source"] = state.last_cmd.source;
  } else {
    doc["last_cmd"] = nullptr;
  }

  doc["wifi_rssi"] = WiFi.RSSI();

  const ProtocolEntry* p = getProtocolById(state.active_protocol_id);
  if (p) {
    JsonObject proto = doc["protocol"].to<JsonObject>();
    proto["id"] = p->id;
    proto["name"] = p->name;
  } else {
    doc["protocol"] = nullptr;
  }
}

static void buildIrJson(JsonDocument& doc, const GlobalState& state) {
  const ProtocolEntry* activeProto = getProtocolById(state.active_protocol_id);
  if (activeProto) {
    JsonObject p = doc["protocol"].to<JsonObject>();
    p["id"] = activeProto->id;
    p["name"] = activeProto->name;
  } else {
    doc["protocol"] = nullptr;
  }

  JsonObject scan = doc["scan"].to<JsonObject>();
  scan["running"] = state.scan.running;
  scan["paused"]  = state.scan.paused;
  scan["index"]   = state.scan.index;
  scan["total"]   = getProtocolCount();

  if (state.scan.running) {
    const ProtocolEntry* cur = getProtocolByIndex(state.scan.index);
    if (cur) {
      JsonObject currObj = scan["current"].to<JsonObject>();
      currObj["id"] = cur->id;
      currObj["name"] = cur->name;
    } else {
      scan["current"] = nullptr;
    }
    scan["next_in_s"] = state.scan.next_in_s;
  } else {
    scan["current"] = nullptr;
    scan["next_in_s"] = 0;
  }

  scan["interval_s"] = state.scan.interval_s;
}

void initWebServer(WebServer& server, GlobalState& state) {
  // 1. Serve single-file UI at root ("/")
  server.on("/", HTTP_GET, [&server]() {
    setCorsHeaders(server);
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", (const char*)index_html_gz, index_html_gz_len);
  });

  // 2. GET /api/state
  server.on("/api/state", HTTP_GET, [&server, &state]() {
    JsonDocument doc;
    buildStateJson(doc, state);
    sendJson(server, 200, doc);
  });

  // 3. POST /api/power
  server.on("/api/power", HTTP_POST, [&server, &state]() {
    if (state.active_protocol_id < 0) {
      sendJsonError(server, 400, "No AC protocol configured. Select one in IR Setup.");
      return;
    }

    if (!canSendIrNow()) {
      sendJsonError(server, 429, "Too fast");
      return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err || !doc["on"].is<bool>()) {
      sendJsonError(server, 400, "Invalid JSON body, expected {\"on\": true/false}");
      return;
    }

    bool on = doc["on"].as<bool>();
    executePowerCommand(state, on, "manual");

    // If power is turned off manually, cancel any active timer
    if (!on && state.timer.active) {
      state.timer.active = false;
      state.timer.remaining_s = 0;
      state.timer.epoch_end = 0;
      clearTimerStorage();
    }

    JsonDocument resp;
    buildStateJson(resp, state);
    sendJson(server, 200, resp);
  });

  // 4. POST /api/sync
  server.on("/api/sync", HTTP_POST, [&server, &state]() {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err || !doc["power"].is<const char*>()) {
      sendJsonError(server, 400, "Invalid JSON body, expected {\"power\": \"on\"/\"off\"}");
      return;
    }

    String p = doc["power"].as<String>();
    if (p != "on" && p != "off") {
      sendJsonError(server, 400, "Invalid power value (must be 'on' or 'off')");
      return;
    }

    state.power_on = (p == "on");
    saveAssumedPower(state.power_on);

    JsonDocument resp;
    buildStateJson(resp, state);
    sendJson(server, 200, resp);
  });

  // 5. POST /api/timer
  server.on("/api/timer", HTTP_POST, [&server, &state]() {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      sendJsonError(server, 400, "Malformed JSON body");
      return;
    }

    int minutes = doc["minutes"] | 0;
    if (minutes < kMinTimerMinutes || minutes > kMaxTimerMinutes) {
      sendJsonError(server, 400, "Invalid minutes (1-720)");
      return;
    }

    String action = doc["action"].is<const char*>() ? doc["action"].as<String>() : "off";
    if (action != "on" && action != "off") {
      sendJsonError(server, 400, "Invalid action (must be 'on' or 'off')");
      return;
    }

    // Sleep timer behavior: Turn AC ON immediately if it is currently OFF
    if (action == "off" && !state.power_on) {
      executePowerCommand(state, true, "timer");
    }

    state.timer.active = true;
    state.timer.remaining_s = (uint32_t)minutes * 60;
    state.timer.action = action;
    state.timer.last_tick_ms = millis();

    if (isNtpSynced()) {
      state.timer.epoch_end = time(nullptr) + state.timer.remaining_s;
      saveTimerStorage((uint32_t)state.timer.epoch_end, action);
    } else {
      state.timer.epoch_end = 0;
    }

    JsonDocument resp;
    buildStateJson(resp, state);
    sendJson(server, 200, resp);
  });

  // 6. POST /api/timer/extend
  server.on("/api/timer/extend", HTTP_POST, [&server, &state]() {
    if (!state.timer.active) {
      sendJsonError(server, 400, "No active timer to extend");
      return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      sendJsonError(server, 400, "Malformed JSON body");
      return;
    }

    int minutes = doc["minutes"] | 0;
    if (minutes < kMinTimerMinutes || minutes > kMaxTimerMinutes) {
      sendJsonError(server, 400, "Invalid minutes (1-720)");
      return;
    }

    state.timer.remaining_s += (uint32_t)minutes * 60;
    if (state.timer.remaining_s > (uint32_t)kMaxTimerMinutes * 60) {
      state.timer.remaining_s = (uint32_t)kMaxTimerMinutes * 60;
    }

    if (isNtpSynced()) {
      state.timer.epoch_end = time(nullptr) + state.timer.remaining_s;
      saveTimerStorage((uint32_t)state.timer.epoch_end, state.timer.action);
    }

    JsonDocument resp;
    buildStateJson(resp, state);
    sendJson(server, 200, resp);
  });

  // 7. DELETE /api/timer
  server.on("/api/timer", HTTP_DELETE, [&server, &state]() {
    state.timer.active = false;
    state.timer.remaining_s = 0;
    state.timer.epoch_end = 0;
    clearTimerStorage();

    JsonDocument resp;
    buildStateJson(resp, state);
    sendJson(server, 200, resp);
  });

  // 8. POST /api/ac
  server.on("/api/ac", HTTP_POST, [&server, &state]() {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      sendJsonError(server, 400, "Malformed JSON body");
      return;
    }

    if (doc["temp"].is<int>()) {
      int t = doc["temp"].as<int>();
      if (t < kMinTemp || t > kMaxTemp) {
        sendJsonError(server, 400, "Temperature out of bounds (16-30)");
        return;
      }
      state.ac.temp = t;
    }

    if (doc["mode"].is<const char*>()) {
      String m = doc["mode"].as<String>();
      if (m != "cool" && m != "fan" && m != "dry") {
        sendJsonError(server, 400, "Invalid mode (cool/fan/dry)");
        return;
      }
      state.ac.mode = m;
    }

    if (doc["fan"].is<const char*>()) {
      String f = doc["fan"].as<String>();
      if (f != "auto" && f != "low" && f != "med" && f != "high") {
        sendJsonError(server, 400, "Invalid fan speed (auto/low/med/high)");
        return;
      }
      state.ac.fan = f;
    }

    saveAcSettings(state.ac);

    // If assumed power is on, also transmit IR with updated settings
    if (state.power_on) {
      if (state.active_protocol_id < 0) {
        sendJsonError(server, 400, "No AC protocol configured. Select one in IR Setup.");
        return;
      }
      if (!canSendIrNow()) {
        sendJsonError(server, 429, "Too fast");
        return;
      }
      sendIrCommand(state.active_protocol_id, true, state.ac.temp, state.ac.mode, state.ac.fan);
    }

    JsonDocument resp;
    buildStateJson(resp, state);
    sendJson(server, 200, resp);
  });

  // 9. GET /api/schedules
  server.on("/api/schedules", HTTP_GET, [&server, &state]() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < state.schedule_count; i++) {
      JsonObject obj = arr.add<JsonObject>();
      obj["id"] = state.schedules[i].id;
      obj["enabled"] = state.schedules[i].enabled;
      if (state.schedules[i].on.length() > 0) obj["on"] = state.schedules[i].on;
      else obj["on"] = nullptr;
      if (state.schedules[i].off.length() > 0) obj["off"] = state.schedules[i].off;
      else obj["off"] = nullptr;

      JsonArray days = obj["days"].to<JsonArray>();
      for (int d = 0; d < 7; d++) {
        days.add(state.schedules[i].days[d] ? 1 : 0);
      }
    }
    sendJson(server, 200, doc);
  });

  // 10. POST /api/schedules (Create or Update)
  server.on("/api/schedules", HTTP_POST, [&server, &state]() {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      sendJsonError(server, 400, "Malformed JSON body");
      return;
    }

    String onStr = doc["on"].is<const char*>() ? doc["on"].as<String>() : "";
    String offStr = doc["off"].is<const char*>() ? doc["off"].as<String>() : "";

    if (onStr.length() == 0 && offStr.length() == 0) {
      sendJsonError(server, 400, "Set at least On or Off time");
      return;
    }

    int h = 0, m = 0;
    if (onStr.length() > 0 && !validateTimeString(onStr, h, m)) {
      sendJsonError(server, 400, "Invalid 'on' time format (must be HH:MM)");
      return;
    }
    if (offStr.length() > 0 && !validateTimeString(offStr, h, m)) {
      sendJsonError(server, 400, "Invalid 'off' time format (must be HH:MM)");
      return;
    }

    bool enabled = doc["enabled"].is<bool>() ? doc["enabled"].as<bool>() : true;
    bool daysArr[7] = {false};
    if (doc["days"].is<JsonArray>()) {
      JsonArray dArray = doc["days"].as<JsonArray>();
      for (int d = 0; d < 7 && d < (int)dArray.size(); d++) {
        daysArr[d] = (dArray[d].as<int>() != 0);
      }
    }

    Schedule* target = nullptr;
    if (doc["id"].is<uint32_t>()) {
      uint32_t id = doc["id"].as<uint32_t>();
      for (int i = 0; i < state.schedule_count; i++) {
        if (state.schedules[i].id == id) {
          target = &state.schedules[i];
          break;
        }
      }
      if (!target) {
        sendJsonError(server, 404, "Schedule not found");
        return;
      }
    } else {
      if (state.schedule_count >= kMaxSchedules) {
        sendJsonError(server, 400, "Maximum 10 schedules allowed");
        return;
      }
      target = &state.schedules[state.schedule_count++];
      target->id = state.next_schedule_id++;
    }

    target->enabled = enabled;
    target->on = onStr;
    target->off = offStr;
    target->last_fired_on_minute_id = -1;
    target->last_fired_off_minute_id = -1;
    for (int d = 0; d < 7; d++) {
      target->days[d] = daysArr[d];
    }

    saveSchedulesStorage(state.schedules, state.schedule_count, state.next_schedule_id);

    JsonDocument resp;
    resp["id"] = target->id;
    resp["enabled"] = target->enabled;
    if (target->on.length() > 0) resp["on"] = target->on; else resp["on"] = nullptr;
    if (target->off.length() > 0) resp["off"] = target->off; else resp["off"] = nullptr;
    JsonArray days = resp["days"].to<JsonArray>();
    for (int d = 0; d < 7; d++) {
      days.add(target->days[d] ? 1 : 0);
    }
    sendJson(server, 200, resp);
  });

  // 11. GET /api/log
  server.on("/api/log", HTTP_GET, [&server, &state]() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < state.log_count; i++) {
      JsonObject entry = arr.add<JsonObject>();
      entry["at"]     = state.logs[i].at;
      entry["date"]   = state.logs[i].date;
      entry["action"] = state.logs[i].action;
      entry["source"] = state.logs[i].source;
    }
    sendJson(server, 200, doc);
  });

  // 12. GET /api/ir/protocols
  server.on("/api/ir/protocols", HTTP_GET, [&server]() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    int total = getProtocolCount();
    for (int i = 0; i < total; i++) {
      const ProtocolEntry* p = getProtocolByIndex(i);
      if (p) {
        JsonObject item = arr.add<JsonObject>();
        item["id"]   = p->id;
        item["name"] = p->name;
      }
    }
    sendJson(server, 200, doc);
  });

  // 13. GET /api/ir
  server.on("/api/ir", HTTP_GET, [&server, &state]() {
    JsonDocument doc;
    buildIrJson(doc, state);
    sendJson(server, 200, doc);
  });

  // 14. POST /api/ir/protocol
  server.on("/api/ir/protocol", HTTP_POST, [&server, &state]() {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err || !doc["id"].is<int>()) {
      sendJsonError(server, 400, "Invalid JSON body, expected {\"id\": <number>}");
      return;
    }

    int id = doc["id"].as<int>();
    if (id < 0 || id >= getProtocolCount()) {
      sendJsonError(server, 400, "Invalid protocol ID");
      return;
    }

    state.active_protocol_id = id;
    saveActiveProtocol(id);

    JsonDocument resp;
    buildIrJson(resp, state);
    sendJson(server, 200, resp);
  });

  // 15. POST /api/ir/test
  server.on("/api/ir/test", HTTP_POST, [&server]() {
    if (!canSendIrNow()) {
      sendJsonError(server, 429, "Too fast");
      return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      sendJsonError(server, 400, "Malformed JSON body");
      return;
    }

    int id = doc["id"] | -1;
    if (id < 0 || id >= getProtocolCount()) {
      sendJsonError(server, 400, "Invalid protocol ID");
      return;
    }

    String power = doc["power"].is<const char*>() ? doc["power"].as<String>() : "on";
    if (power != "on" && power != "off") {
      sendJsonError(server, 400, "Invalid power (must be 'on' or 'off')");
      return;
    }

    int temp = doc["temp"] | 24;
    if (temp < kMinTemp || temp > kMaxTemp) {
      sendJsonError(server, 400, "Temperature out of bounds (16-30)");
      return;
    }

    String mode = doc["mode"].is<const char*>() ? doc["mode"].as<String>() : "cool";
    if (mode != "cool" && mode != "fan" && mode != "dry") {
      sendJsonError(server, 400, "Invalid mode (cool/fan/dry)");
      return;
    }

    String fan = doc["fan"].is<const char*>() ? doc["fan"].as<String>() : "auto";
    if (fan != "auto" && fan != "low" && fan != "med" && fan != "high") {
      sendJsonError(server, 400, "Invalid fan speed (auto/low/med/high)");
      return;
    }

    sendIrCommand(id, (power == "on"), temp, mode, fan);

    JsonDocument resp;
    resp["ok"] = true;
    sendJson(server, 200, resp);
  });

  // 16. POST /api/ir/scan
  server.on("/api/ir/scan", HTTP_POST, [&server, &state]() {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err || !doc["action"].is<const char*>()) {
      sendJsonError(server, 400, "Invalid JSON body, expected {\"action\": \"...\"}");
      return;
    }

    String action = doc["action"].as<String>();
    if (action == "start") {
      uint8_t interval_s = doc["interval_s"] | kDefaultScanIntervalS;
      startAutoScan(state, interval_s);
    } else if (action == "stop") {
      stopAutoScan(state);
    } else if (action == "pause") {
      pauseAutoScan(state);
    } else if (action == "resume") {
      resumeAutoScan(state);
    } else if (action == "next") {
      if (!canSendIrNow()) { sendJsonError(server, 429, "Too fast"); return; }
      stepAutoScanNext(state);
    } else if (action == "prev") {
      if (!canSendIrNow()) { sendJsonError(server, 429, "Too fast"); return; }
      stepAutoScanPrev(state);
    } else if (action == "resend") {
      if (!canSendIrNow()) { sendJsonError(server, 429, "Too fast"); return; }
      resendAutoScan(state);
    } else {
      sendJsonError(server, 400, "Unknown scan action");
      return;
    }

    JsonDocument resp;
    buildIrJson(resp, state);
    sendJson(server, 200, resp);
  });

  // 17. GET /update - Browser OTA Upload Page
  server.on("/update", HTTP_GET, [&server]() {
    static const char kUpdateHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-C3 Firmware Update (OTA)</title>
<style>
:root{--bg:#0b0f19;--card:#151c2e;--accent:#7c8cff;--accent-hover:#96a4ff;--text:#e8ecf4;--muted:#8a94a6;--border:rgba(255,255,255,0.08);--success:#10b981;--danger:#ef4444;}
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;}
body{background:var(--bg);color:var(--text);display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px;}
.card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:28px;max-width:440px;width:100%;box-shadow:0 12px 30px rgba(0,0,0,0.5);}
h1{font-size:20px;font-weight:700;margin-bottom:8px;color:var(--text);}
p{font-size:13px;color:var(--muted);margin-bottom:20px;line-height:1.5;}
.dropzone{border:2px dashed var(--border);border-radius:12px;padding:24px;text-align:center;cursor:pointer;transition:border-color .2s;background:rgba(255,255,255,0.02);}
.dropzone:hover{border-color:var(--accent);}
input[type="file"]{display:none;}
.btn{display:inline-block;width:100%;padding:12px;margin-top:16px;background:var(--accent);color:#fff;border:none;border-radius:10px;font-weight:600;font-size:14px;cursor:pointer;transition:background .2s;}
.btn:hover{background:var(--accent-hover);}
.btn:disabled{opacity:0.5;cursor:not-allowed;}
.progress-bar{height:8px;background:rgba(255,255,255,0.08);border-radius:4px;overflow:hidden;margin-top:16px;display:none;}
.progress-fill{height:100%;background:var(--accent);width:0%;transition:width .15s ease;}
.status{margin-top:12px;font-size:12px;text-align:center;color:var(--muted);}
a.back{display:block;text-align:center;margin-top:20px;color:var(--muted);text-decoration:none;font-size:13px;}
a.back:hover{color:var(--text);}
</style>
</head>
<body>
<div class="card">
  <h1>Firmware Update (OTA)</h1>
  <p>Select or drag a compiled <code>firmware.bin</code> file to flash directly over Wi-Fi.</p>
  <div class="dropzone" id="dz" onclick="document.getElementById('f').click()">
    <span id="flabel">Click or drop <strong>.bin</strong> file here</span>
    <input type="file" id="f" accept=".bin" onchange="onFile(this)">
  </div>
  <div class="progress-bar" id="pb"><div class="progress-fill" id="pf"></div></div>
  <div class="status" id="st">Ready</div>
  <button type="button" class="btn" id="btn" disabled onclick="doUpload()">Flash Firmware</button>
  <a href="/" class="back">&larr; Back to AC Controller</a>
</div>
<script>
const $ = (id) => document.getElementById(id);
let selFile = null;
function onFile(input) {
  if (input.files && input.files[0]) {
    selFile = input.files[0];
    $('flabel').textContent = selFile.name + ' (' + (selFile.size / 1024).toFixed(1) + ' KB)';
    $('btn').disabled = false;
  }
}
const dz = $('dz');
dz.ondragover = (e) => { e.preventDefault(); dz.style.borderColor = 'var(--accent)'; };
dz.ondragleave = () => { dz.style.borderColor = 'var(--border)'; };
dz.ondrop = (e) => {
  e.preventDefault(); dz.style.borderColor = 'var(--border)';
  if (e.dataTransfer.files && e.dataTransfer.files[0]) {
    $('f').files = e.dataTransfer.files;
    onFile($('f'));
  }
};
function doUpload() {
  if (!selFile) return;
  $('btn').disabled = true;
  $('pb').style.display = 'block';
  $('st').textContent = 'Uploading... 0%';
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      $('pf').style.width = pct + '%';
      $('st').textContent = 'Uploading: ' + pct + '%';
    }
  };
  xhr.onload = () => {
    if (xhr.status === 200) {
      $('pf').style.background = 'var(--success)';
      $('st').innerHTML = '<strong style="color:var(--success)">Update successful!</strong> Rebooting...';
      let sec = 5;
      setInterval(() => {
        $('st').textContent = 'Reconnecting in ' + sec + 's...';
        if (--sec <= 0) location.href = '/';
      }, 1000);
    } else {
      $('pf').style.background = 'var(--danger)';
      $('st').innerHTML = '<strong style="color:var(--danger)">Update failed!</strong> ' + (xhr.responseText || '');
      $('btn').disabled = false;
    }
  };
  xhr.onerror = () => {
    $('pf').style.background = 'var(--danger)';
    $('st').innerHTML = '<strong style="color:var(--danger)">Network error during upload!</strong>';
    $('btn').disabled = false;
  };
  const formData = new FormData();
  formData.append('update', selFile, selFile.name);
  xhr.send(formData);
}
</script>
</body>
</html>)rawliteral";
    server.send(200, "text/html", kUpdateHtml);
  });

  // 18. POST /update - Handle multipart firmware binary upload
  server.on("/update", HTTP_POST, [&server]() {
    server.sendHeader("Connection", "close");
    if (Update.hasError()) {
      server.send(500, "text/plain", "Update Failed!");
    } else {
      server.send(200, "text/plain", "OK");
      delay(500);
      ESP.restart();
    }
  }, [&server]() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[OTA-WEB] Start: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("[OTA-WEB] Success: %u bytes written. Rebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  // 17. Path-parameter DELETE /api/schedules/{id} and fallback 404 / OPTIONS handler
  server.onNotFound([&server, &state]() {
    setCorsHeaders(server);

    // Handle CORS preflight OPTIONS
    if (server.method() == HTTP_OPTIONS) {
      server.send(204);
      return;
    }

    String uri = server.uri();

    // Check DELETE /api/schedules/{id}
    if (server.method() == HTTP_DELETE && uri.startsWith("/api/schedules/")) {
      String idStr = uri.substring(15);
      if (idStr.length() > 0) {
        uint32_t id = idStr.toInt();
        int foundIdx = -1;
        for (int i = 0; i < state.schedule_count; i++) {
          if (state.schedules[i].id == id) {
            foundIdx = i;
            break;
          }
        }

        if (foundIdx >= 0) {
          for (int i = foundIdx; i < state.schedule_count - 1; i++) {
            state.schedules[i] = state.schedules[i + 1];
          }
          state.schedule_count--;
          saveSchedulesStorage(state.schedules, state.schedule_count, state.next_schedule_id);

          JsonDocument resp;
          resp["ok"] = true;
          sendJson(server, 200, resp);
          return;
        } else {
          sendJsonError(server, 404, "Schedule not found");
          return;
        }
      }
    }

    // 404 Error for unrecognized routes
    if (uri.startsWith("/api/")) {
      sendJsonError(server, 404, "Not found");
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });
}
