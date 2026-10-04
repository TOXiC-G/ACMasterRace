#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include <time.h>

#include "config.h"
#include "types.h"
#include "storage.h"
#include "ir_controller.h"
#include "schedules.h"
#include "web_server.h"

// Master Global State instance
GlobalState g_state;

// HTTP Web Server on port 80
WebServer g_server(80);

// Wi-Fi Reconnect State Machine
static bool s_wifi_connected = false;
static uint32_t s_last_wifi_retry_ms = 0;
static uint32_t s_wifi_retry_interval_ms = 2000;
static const uint32_t kMaxWifiRetryIntervalMs = 60000;

static void setupWatchdog() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = kWdtTimeoutMs,
    .idle_core_mask = (1 << 0),
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&twdt_config);
  esp_task_wdt_add(NULL);
#else
  esp_task_wdt_init(kWdtTimeoutMs / 1000, true);
  esp_task_wdt_add(NULL);
#endif
  Serial.printf("[WDT] Task Watchdog configured (%u ms timeout)\n", kWdtTimeoutMs);
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);

#if USE_STATIC_IP
  IPAddress local_ip(STATIC_IP_ADDR);
  IPAddress gateway(STATIC_GATEWAY);
  IPAddress subnet(STATIC_SUBNET);
  IPAddress dns1(STATIC_DNS1);
  IPAddress dns2(STATIC_DNS2);
  if (!WiFi.config(local_ip, gateway, subnet, dns1, dns2)) {
    Serial.println("[WIFI] Static IP configuration failed, using DHCP");
  } else {
    Serial.printf("[WIFI] Configured Static IP: %s\n", local_ip.toString().c_str());
  }
#endif

  Serial.printf("[WIFI] Connecting to SSID '%s'...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  s_last_wifi_retry_ms = millis();
}

static bool s_ota_initialized = false;
static void setupOTA() {
  if (s_ota_initialized) return;

  ArduinoOTA.setHostname(DEVICE_HOSTNAME);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.printf("[OTA] Start updating %s\n", type.c_str());
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update successful! Rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int last_pct = 999;
    unsigned int pct = progress / (total / 100);
    if (pct % 10 == 0 && pct != last_pct) {
      last_pct = pct;
      Serial.printf("[OTA] Progress: %u%%\n", pct);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  s_ota_initialized = true;
  Serial.printf("[OTA] Ready! Wi-Fi flash with: pio run -t upload --upload-port %s.local\n", DEVICE_HOSTNAME);
}

static void handleWiFiReconnect() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!s_wifi_connected) {
      s_wifi_connected = true;
      s_wifi_retry_interval_ms = 2000; // Reset backoff

      Serial.println();
      Serial.printf("[WIFI] Connected! IP: %s | RSSI: %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());

      // Start mDNS responder
      if (MDNS.begin(DEVICE_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] Responder started: http://%s.local\n", DEVICE_HOSTNAME);
      } else {
        Serial.println("[mDNS] Error starting mDNS responder");
      }

      // Initialize Over-The-Air (OTA) updates
      setupOTA();
    }
  } else {
    if (s_wifi_connected) {
      s_wifi_connected = false;
      Serial.println("[WIFI] Connection lost! Initiating auto-reconnect backoff...");
      s_last_wifi_retry_ms = millis();
    }

    uint32_t now = millis();
    if (now - s_last_wifi_retry_ms >= s_wifi_retry_interval_ms) {
      s_last_wifi_retry_ms = now;
      Serial.printf("[WIFI] Retrying connection (interval %u ms)...\n", s_wifi_retry_interval_ms);
      WiFi.reconnect();

      // Exponential backoff up to 60s
      s_wifi_retry_interval_ms *= 2;
      if (s_wifi_retry_interval_ms > kMaxWifiRetryIntervalMs) {
        s_wifi_retry_interval_ms = kMaxWifiRetryIntervalMs;
      }
    }
  }
}

void setup() {
  Serial.begin(kSerialBaudRate);
  delay(1000); // Allow USB CDC / Serial to stabilize

  Serial.println();
  Serial.println("==========================================");
  Serial.println("   ESP32-C3 AC Controller Starting Up     ");
  Serial.println("==========================================");

  // 1. Initialize Task Watchdog
  setupWatchdog();

  // 2. Load persisted settings from NVS
  initStorage(g_state);

  // 3. Initialize IR transmitter and protocols table
  initIr(g_state);

  // 4. Initialize Wi-Fi
  connectWiFi();

  // 5. Configure NTP (IST: UTC + 5:30)
  configTime(kGmtOffsetSec, kDaylightOffsetSec, kNtpServer1, kNtpServer2);
  Serial.printf("[NTP] Configured servers %s, %s (IST UTC+5:30)\n", kNtpServer1, kNtpServer2);

  // 6. Setup HTTP server and REST endpoints
  initWebServer(g_server, g_state);
  g_server.begin();
  Serial.println("[HTTP] Web server started on port 80");
}

void loop() {
  // Feed watchdog
  esp_task_wdt_reset();

  // Process HTTP requests
  g_server.handleClient();

  // Process Over-The-Air updates over Wi-Fi
  if (s_wifi_connected) {
    ArduinoOTA.handle();
  }

  // Monitor and handle Wi-Fi reconnect non-blockingly
  handleWiFiReconnect();

  // Check persisted timer recovery once NTP time is available
  checkTimerOnBoot(g_state);

  // Non-blocking timer countdown ticker
  updateTimerLoop(g_state);

  // Non-blocking schedules evaluation per second
  evaluateSchedules(g_state);

  // Non-blocking auto-scan ticker
  updateAutoScanLoop(g_state);

  // Non-blocking power command retry loop with phototransistor feedback check
  updatePowerRetryLoop(g_state);
}
