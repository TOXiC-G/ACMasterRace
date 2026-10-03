# AGENTS.md — Contributor & AI Agent Guidelines

This document provides architecture, constraints, and development workflows for AI coding assistants and developers contributing to the **ACMasterRace** project.

---

## 1. Project Overview & Architecture

**ACMasterRace** is an embedded IoT air conditioner controller built on an **ESP32-C3 Super Mini** microcontroller. It drives a 38 kHz infrared (IR) LED module to operate air conditioners (specifically calibrated for Indian market units such as Voltas window/split ACs and Gree OEM units).

The device hosts its own single-file web dashboard directly from flash memory and provides a REST API.

```
                           +------------------------+
                           |  Browser / Phone / UI  |
                           +------------------------+
                                       |
                                HTTP / REST API
                                       v
                           +------------------------+
                           | ESP32-C3 (Port 80)     |
                           | WebServer & ArduinoOTA |
                           +------------------------+
                             /         |          \
                            v          v           v
                    +------------+ +--------+ +---------------+
                    | Preferences| | Task   | | IRremoteESP8266
                    | (NVS Flash)| | WDT    | | GPIO 4 (PWM)
                    +------------+ +--------+ +---------------+
                                                     |
                                                     v
                                              +---------------+
                                              | 38 kHz IR LED |
                                              +---------------+
```

---

## 2. Directory & File Structure

| File | Purpose |
| :--- | :--- |
| [`index.html`](file:///h:/Dev/AC/index.html) | Single-file web interface (HTML5, Vanilla CSS, Vanilla ES6 JS). Zero external runtime dependencies. Supports offline mock mode via `?mock=1`. |
| [`build_html.py`](file:///h:/Dev/AC/build_html.py) | Python build script. Reads `index.html`, compresses with gzip level 9, and generates `ac_controller/index_html.h`. |
| [`ac_controller/index_html.h`](file:///h:/Dev/AC/ac_controller/index_html.h) | **Auto-generated** C header containing the gzipped PROGMEM byte array. **Never edit manually.** |
| [`ac_controller/ac_controller.ino`](file:///h:/Dev/AC/ac_controller/ac_controller.ino) | Firmware entry point: `setup()`, non-blocking `loop()`, Wi-Fi reconnect state machine, Watchdog configuration, ArduinoOTA setup. |
| [`ac_controller/config.h`](file:///h:/Dev/AC/ac_controller/config.h) | Hardware pin assignments (`kIrLedPin = 4`), timing constraints, default settings, NTP configuration. |
| [`ac_controller/types.h`](file:///h:/Dev/AC/ac_controller/types.h) | Core data structs: `GlobalState`, `Schedule`, `TimerState`, `ScanState`, `ProtocolEntry`, `LogEntry`. |
| [`ac_controller/storage.h`](file:///h:/Dev/AC/ac_controller/storage.h) / `.cpp` | Non-volatile flash persistence via ESP32 `Preferences` (NVS): state, AC mode/temp/fan, active protocol ID, schedules, sleep timer recovery across reboots. |
| [`ac_controller/ir_controller.h`](file:///h:/Dev/AC/ac_controller/ir_controller.h) / `.cpp` | Protocol selection and IR transmission engine using `IRremoteESP8266` / `IRac`. Manages auto-scanning and minimum interval rate limiting (`kMinIrIntervalMs = 400ms`). **Gree (model 2)** is default (ID 0). |
| [`ac_controller/schedules.h`](file:///h:/Dev/AC/ac_controller/schedules.h) / `.cpp` | Non-blocking second ticker: evaluates 7-day schedule masks and sleep timer countdown. |
| [`ac_controller/web_server.h`](file:///h:/Dev/AC/ac_controller/web_server.h) / `.cpp` | REST API routes (`/api/*`), CORS handlers, and Web OTA firmware upload endpoints (`/update`). |
| [`ac_controller/secrets.h.example`](file:///h:/Dev/AC/ac_controller/secrets.h.example) | Template for local Wi-Fi credentials and static IP config. Local `secrets.h` is git-ignored. |
| [`platformio.ini`](file:///h:/Dev/AC/platformio.ini) | PlatformIO project configuration: ESP32-C3 board settings, USB CDC on boot, `min_spiffs.csv` dual-OTA partition scheme. |
| [`API.md`](file:///h:/Dev/AC/API.md) | Exhaustive documentation of all REST API routes, schemas, request/response examples, and error codes. |
| [`README.md`](file:///h:/Dev/AC/README.md) | User-facing setup, wiring, flashing, OTA, and Tailscale usage instructions. |

---

## 3. Mandatory Development Rules

### Rule 1: Always Re-run `build_html.py` After Editing `index.html`
The firmware embeds the web interface directly as compiled byte arrays in flash memory.
Whenever you modify [`index.html`](file:///h:/Dev/AC/index.html), you **must** execute:
```bash
python build_html.py
```
This updates [`ac_controller/index_html.h`](file:///h:/Dev/AC/ac_controller/index_html.h). If you forget this step, your frontend changes will not appear when flashed to the microcontroller.

### Rule 2: Protect Secrets and Credentials
- `ac_controller/secrets.h` is git-ignored and contains live user credentials. **Never stage, commit, or print the raw contents of `secrets.h`.**
- If adding new configuration constants, add them with placeholder values to both [`secrets.h.example`](file:///h:/Dev/AC/secrets.h.example) and [`ac_controller/secrets.h.example`](file:///h:/Dev/AC/ac_controller/secrets.h.example).

### Rule 3: Non-Blocking Execution in `loop()`
- The ESP32-C3 runs a Task Watchdog Timer (WDT) with an 8-second timeout (`kWdtTimeoutMs = 8000`).
- **Never introduce blocking `delay()` calls** in the main execution paths of `loop()` or web request handlers.
- Use `millis()`-based timestamps for polling, tickers, auto-scans, and retry backoffs.

### Rule 4: Preserve Sleep Timer Semantics
- When setting a sleep timer (`POST /api/timer`):
  1. If the AC is currently off, **immediately transmit IR power ON**.
  2. The timer action is always `"off"` (turning off the unit when countdown reaches zero).
  3. If the user manually toggles power OFF while a sleep timer is active, the timer must be immediately cancelled and erased from NVS.

### Rule 5: Preserve Dual-OTA Partition Scheme
- PlatformIO uses `board_build.partitions = min_spiffs.csv`. This provides two ~1.9 MB application slots (`app0` and `app1`) required for Over-The-Air firmware updates.
- In Arduino IDE, always select `Minimal SPIFFS (Large APPS with OTA)`.
- Never shrink the app partition below the binary size (~1.2 MB compiled with IR and JSON libraries).

---

## 4. Frontend & Testing Conventions

1. **No External Frameworks**:
   - `index.html` uses clean Vanilla CSS and modern ES6 JavaScript. Do not introduce CDN dependencies (React, Vue, Tailwind, Bootstrap, jQuery, font stylesheets, etc.) as the ESP must function in fully offline / air-gapped home Wi-Fi networks.
2. **Local Mock Testing**:
   - You can test any frontend feature locally in a web browser without flashing hardware:
     Open `index.html?mock=1` in your browser.
   - The mock backend implements full in-memory state, logs, schedules, timers, and IR auto-scan simulation. When adding API features to the frontend, update the `mockDb` object accordingly.
3. **Accessibility & Semantics**:
   - Keep semantic HTML tags (`<main>`, `<section>`, `<header>`, `<footer>`, `<dialog>`).
   - Retain `aria-label`, `aria-checked`, `role="radiogroup"`, and `role="listitem"` attributes.

---

## 5. Protocol & Hardware Calibration Notes

- **Default Protocol**: The active protocol defaults to **`Gree (model 2)`** (`decode_type_t::GREE`, model `2 /* YBOFB */`). This protocol is widely used by Indian market Voltas window/split air conditioners.
- **Hardware Pin**: Driven directly via **GPIO 4** (`kIrLedPin = 4`). Can be changed in [`ac_controller/config.h`](file:///h:/Dev/AC/ac_controller/config.h).
- **USB CDC on Boot**: The ESP32-C3 Super Mini requires `-D ARDUINO_USB_MODE=1` and `-D ARDUINO_USB_CDC_ON_BOOT=1` in build flags to enable serial console output over the native USB Type-C port.
