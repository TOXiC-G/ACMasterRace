# ESP32 AC Controller - REST API Specification

This document defines the HTTP JSON API contract between the single-file web client (`index.html`) and the ESP32-C3 firmware.

---

## 1. General Conventions

- **Protocol**: HTTP/1.1 (plain HTTP over local Wi-Fi, port 80).
- **Format**: All payloads and responses are `application/json; charset=utf-8`.
- **URL Base**: Relative `/api/...`.
- **Error Format**: On non-2xx status codes, the ESP32 returns:
  ```json
  { "error": "Descriptive error message" }
  ```
- **Timeout**: The client applies a 5000 ms timeout to all fetch requests.

---

## 2. Endpoints

### 2.1 Get Device State
- **Route**: `GET /api/state`
- **Description**: Returns the complete current state of the AC controller, including device clock, active sleep timer, AC configuration, and Wi-Fi signal. Polled by the UI every 5 seconds when the browser tab is visible.
- **Response `200 OK`**:
  ```json
  {
    "power": "on",
    "confirmed": false,
    "time": "14:32:05",
    "date": "2026-10-02",
    "timer": {
      "remaining_s": 1780,
      "action": "off"
    },
    "ac": {
      "temp": 24,
      "mode": "cool",
      "fan": "auto"
    },
    "last_cmd": {
      "action": "on",
      "at": "14:02:15",
      "source": "manual"
    },
    "wifi_rssi": -58,
    "protocol": {
      "id": 0,
      "name": "Voltas"
    }
  }
  ```
  *(Note: `confirmed` reflects the hardware feedback sensor (`isPowerCommandVerified()`). It is `false` when no sensor is installed, and becomes `true` when physical AC state matches assumed state. `timer` is `null` when no sleep timer is active. `last_cmd` is `null` if no commands have been executed yet. `protocol` is `null` if no AC brand protocol has been selected yet).*

---

### 2.2 Toggle Power
- **Route**: `POST /api/power`
- **Description**: Transmits an IR command to power the AC unit on or off. Updates the internal assumed state and appends an entry to the command log.
- **Redundancy & Retries**: Fires the initial IR command immediately and returns HTTP 200. If physical power is unverified, background retries continue non-blockingly at 1-second intervals up to **5 total attempts** (`kMaxPowerAttempts = 5`), ensuring reliable delivery without blocking HTTP requests.
- **Sleep Timer Interaction**: If power is toggled `"off"` manually while a sleep timer is running, the timer is cancelled and cleared immediately.
- **Request Body**:
  ```json
  { "on": true }
  ```
- **Response `200 OK`**: Complete state object (same schema as `GET /api/state`).

---

### 2.3 Synchronize State (No IR)
- **Route**: `POST /api/sync`
- **Description**: Fixes/synchronizes the firmware's assumed power state without transmitting any IR signal (e.g. if the AC was turned on/off using a physical remote control).
- **Request Body**:
  ```json
  { "power": "on" }
  ```
  *(Values: `"on"` | `"off"`)*
- **Response `200 OK`**: Complete state object (same schema as `GET /api/state`).

---

### 2.4 Set Sleep Timer
- **Route**: `POST /api/timer`
- **Description**: Starts a sleep timer. Immediately turns ON the AC (via IR) if it is currently OFF, then triggers power `"off"` once the timer reaches zero. Defaults to `action: "off"`.
- **Request Body**:
  ```json
  {
    "minutes": 30,
    "action": "off"
  }
  ```
- **Response `200 OK`**: Complete state object.

---

### 2.5 Extend Sleep Timer
- **Route**: `POST /api/timer/extend`
- **Description**: Adds additional minutes to the active sleep timer.
- **Request Body**:
  ```json
  { "minutes": 15 }
  ```
- **Response `200 OK`**: Complete state object.

---

### 2.6 Cancel Sleep Timer
- **Route**: `DELETE /api/timer`
- **Description**: Cancels and clears the active sleep timer.
- **Response `200 OK`**: Complete state object (with `"timer": null`).

---

### 2.7 Update AC Settings
- **Route**: `POST /api/ac`
- **Description**: Transmits an IR command to adjust AC parameters. Accepts any partial subset of fields.
- **Request Body**:
  ```json
  {
    "temp": 25,
    "mode": "cool",
    "fan": "med"
  }
  ```
  - `temp`: Integer `16` to `30` (°C).
  - `mode`: `"cool"` | `"fan"` | `"dry"`.
  - `fan`: `"auto"` | `"low"` | `"med"` | `"high"`.
- **Response `200 OK`**: Complete state object.

---

### 2.8 Get Schedules
- **Route**: `GET /api/schedules`
- **Description**: Returns all configured recurring schedules.
- **Response `200 OK`**:
  ```json
  [
    {
      "id": 1,
      "enabled": true,
      "on": "22:30",
      "off": "06:00",
      "days": [1, 1, 1, 1, 1, 0, 0]
    },
    {
      "id": 2,
      "enabled": false,
      "on": "14:00",
      "off": null,
      "days": [0, 0, 0, 0, 0, 1, 1]
    }
  ]
  ```
  - `days`: 7-element array corresponding to Monday through Sunday (`[Mon, Tue, Wed, Thu, Fri, Sat, Sun]`), where `1` = active and `0` = inactive.
  - `on` / `off`: String in `"HH:MM"` 24-hour format, or `null`.

---

### 2.9 Create or Update Schedule
- **Route**: `POST /api/schedules`
- **Description**: If `id` is omitted, creates a new schedule. If `id` is present, updates the existing schedule with that ID.
- **Request Body**:
  ```json
  {
    "id": 1,
    "enabled": true,
    "on": "23:00",
    "off": "06:30",
    "days": [1, 1, 1, 1, 1, 0, 0]
  }
  ```
- **Response `200 OK`**: The saved schedule object (including its assigned `id`).

---

### 2.10 Delete Schedule
- **Route**: `DELETE /api/schedules/{id}`
- **Description**: Deletes the schedule specified by the path parameter `{id}`.
- **Response `200 OK`**:
  ```json
  { "ok": true }
  ```

---

### 2.11 Get Command Log
- **Route**: `GET /api/log`
- **Description**: Returns the last 10 commands executed by the controller, newest first.
- **Response `200 OK`**:
  ```json
  [
    { "at": "14:30:10", "date": "2026-10-02", "action": "off", "source": "timer" },
    { "at": "13:00:00", "date": "2026-10-02", "action": "on", "source": "manual" },
    { "at": "08:00:22", "date": "2026-10-02", "action": "off", "source": "schedule" }
  ]
  ```
  - `source`: `"manual"` | `"timer"` | `"schedule"`.

---

### 2.12 Get Supported IR Protocols
- **Route**: `GET /api/ir/protocols`
- **Description**: Returns the ordered list of supported AC IR protocols (most common/likely brands first).
- **Response `200 OK`**:
  ```json
  [
    { "id": 0, "name": "Voltas" },
    { "id": 1, "name": "Voltas (alt)" },
    { "id": 2, "name": "Gree" },
    { "id": 3, "name": "Daikin" },
    { "id": 4, "name": "LG" },
    { "id": 5, "name": "Hitachi" },
    { "id": 6, "name": "Carrier" },
    { "id": 7, "name": "Midea" },
    { "id": 8, "name": "Samsung" },
    { "id": 9, "name": "Panasonic" },
    { "id": 10, "name": "Haier" },
    { "id": 11, "name": "Whirlpool" }
  ]
  ```

---

### 2.13 Get IR State & Auto-Scan Status
- **Route**: `GET /api/ir`
- **Description**: Returns the currently active AC IR protocol and real-time auto-scan status.
- **Response `200 OK`**:
  ```json
  {
    "protocol": {
      "id": 0,
      "name": "Voltas"
    },
    "scan": {
      "running": false,
      "paused": false,
      "index": 0,
      "total": 12,
      "current": {
        "id": 0,
        "name": "Voltas"
      },
      "interval_s": 4,
      "next_in_s": 3
    }
  }
  ```
  *(Note: `protocol` and `scan.current` are `null` if none selected or scan is idle).*

---

### 2.14 Set Active IR Protocol
- **Route**: `POST /api/ir/protocol`
- **Description**: Persists the selected protocol as the active AC remote configuration for future power, timer, and schedule commands.
- **Request Body**:
  ```json
  { "id": 0 }
  ```
- **Response `200 OK`**: Returns updated IR state (same schema as `GET /api/ir`).

---

### 2.15 Send Test IR Signal
- **Route**: `POST /api/ir/test`
- **Description**: Emits an immediate IR packet using the specified protocol to test communication with the AC unit. Does NOT alter the assumed power state of the controller.
- **Request Body**:
  ```json
  {
    "id": 0,
    "power": "on",
    "temp": 24,
    "mode": "cool",
    "fan": "auto"
  }
  ```
- **Response `200 OK`**:
  ```json
  { "ok": true }
  ```

---

### 2.16 Control IR Auto-Scan
- **Route**: `POST /api/ir/scan`
- **Description**: Controls the background auto-scanning routine that tests each supported protocol sequentially.
- **Supported Action Payloads**:
  - Start: `{"action": "start", "interval_s": 4}` (interval between 3 and 10 seconds)
  - Stop: `{"action": "stop"}`
  - Pause: `{"action": "pause"}`
  - Resume: `{"action": "resume"}`
  - Step forward: `{"action": "next"}` (advances to next protocol and transmits ON immediately)
  - Step backward: `{"action": "prev"}` (steps back to previous protocol and transmits ON immediately)
  - Re-send: `{"action": "resend"}` (re-transmits ON for the current protocol)
- **Response `200 OK`**: Returns updated IR state (same schema as `GET /api/ir`).

---

## 3. Assumptions & Firmware Notes

1. **Unidirectional Infrared (IR)**:
   - Consumer split AC units typically lack two-way communication. All states are considered "assumed" because the controller has no feedback loop from the AC hardware.
   - The UI shows "Assumed on" / "Assumed off" as a subtle status indicator.
2. **Initial Setup (No Protocol Selected)**:
   - When `state.protocol` is `null`, the web UI displays a prominent "Setup needed — choose an AC protocol" chip under the power button.
   - If the user attempts to toggle power without an active protocol, the firmware should return HTTP 400 with `{"error": "No AC protocol configured. Select one in IR Setup."}`. The UI displays this error message in a toast without locking the interface.
3. **IR Polling Policy**:
   - The client polls `GET /api/ir` every 1 second ONLY while the "IR Setup" card is expanded AND `scan.running` is `true`.
   - When the card is collapsed, when the scan stops, or when the tab is backgrounded (`visibilitychange`), IR polling halts completely to conserve microcontroller resources.
4. **Time Synchronization**:
   - The ESP32 is assumed to synchronize time via NTP (Network Time Protocol) on boot.
   - The device clock displayed in the header uses the time string provided by the ESP32 in `GET /api/state`. The UI interpolates seconds locally between 5-second polling intervals.
   - The client renders times using the user's localized browser format (12h AM/PM or 24h) dynamically.
5. **Timer Countdown Resynchronization**:
   - `timer.remaining_s` is reported by the firmware. The client runs a local 1-second interval ticker and re-synchronizes on every 5-second poll to prevent drift.
6. **Offline Handling**:
   - If 2 consecutive polls or commands fail to connect (network down or ESP32 unreachable), the UI displays a full-width red alert banner and disables interactive controls (except "Retry now") until connectivity is restored.
7. **Memory & PROGMEM Serving**:
   - `index.html` is completely self-contained in a single file with inline CSS and JS.
   - Uncompressed size is ~65 KB, and gzips to ~16 KB, easily fitting into ESP32-C3 flash memory.
