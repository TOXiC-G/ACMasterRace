# ESP32-C3 Air Conditioner Controller Firmware

Firmware for an ESP32-C3 (Super Mini class) microcontroller to control a window air conditioner (e.g. Voltas Vertis Plus) using an infrared (IR) LED module. The microcontroller hosts a single-file mobile-friendly web UI (`index.html`) directly from internal flash memory and implements a REST API.

---

## 1. Hardware & Wiring

### Components
- **Microcontroller**: ESP32-C3 Super Mini (or standard ESP32-C3 Dev Module)
- **IR Transmitter Module**: Standard 38 kHz 3-pin IR LED transmitter breakout (with on-board current-limiting resistor)
- **Power**: 5V USB-C power supply or adapter

### Wiring Diagram
The IR transmitter module typically has 3 pins labeled `S` (Signal), `+` (or middle pin), and `-` (Ground).

| IR Module Pin | ESP32-C3 Super Mini Pin | Description |
| :--- | :--- | :--- |
| **S** (Signal) | **GPIO 4** | IR PWM carrier output |
| **-** (GND) | **GND** | Ground reference |
| **Middle Pin / +** | **Unused / NC** | *Leave disconnected* (the module drives the LED directly between Signal and GND through its onboard SMD resistor) |

> **Note**: Pin GPIO 4 can be modified if needed in [`ac_controller/config.h`](file:///h:/Dev/AC/ac_controller/config.h) (`kIrLedPin`).

---

## 2. Serving the Web UI (`build_html.py`)

The web UI is contained entirely in [`index.html`](file:///h:/Dev/AC/index.html). The firmware embeds and serves it directly as a gzipped PROGMEM byte array with `Content-Encoding: gzip`.

Whenever you modify [`index.html`](file:///h:/Dev/AC/index.html), re-generate the header:

```bash
python build_html.py
```

This reads [`index.html`](file:///h:/Dev/AC/index.html), compresses it with gzip (compress level 9, shrinking ~65 KB down to ~15.5 KB), and writes [`ac_controller/index_html.h`](file:///h:/Dev/AC/ac_controller/index_html.h).

---

## 3. Wi-Fi & Credentials Configuration

Copy the example secrets file to create your local [`secrets.h`](file:///h:/Dev/AC/ac_controller/secrets.h):

```bash
cp secrets.h.example ac_controller/secrets.h
```

Edit [`ac_controller/secrets.h`](file:///h:/Dev/AC/ac_controller/secrets.h):
```cpp
#pragma once

// Wi-Fi Credentials (2.4 GHz only)
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Hostname for mDNS (http://ac.local)
#define DEVICE_HOSTNAME "ac"

// Optional Static IP (0 = DHCP, 1 = Static IP)
#define USE_STATIC_IP 0
#define STATIC_IP_ADDR    192, 168, 1, 150
#define STATIC_GATEWAY    192, 168, 1, 1
#define STATIC_SUBNET     255, 255, 255, 0
#define STATIC_DNS1       1, 1, 1, 1
#define STATIC_DNS2       8, 8, 8, 8
```

---

## 4. Building and Flashing

### Option A: PlatformIO (CLI or VS Code)
1. **Initial USB Flash** (configures OTA dual-partition):
   ```bash
   pio run -t upload
   ```
2. **Subsequent Flashes (Wireless over Wi-Fi!)**:
   Once the ESP is on your network, upload wirelessly without plugging in USB:
   ```bash
   pio run -t upload --upload-port ac.local
   ```
   *(or replace `ac.local` with device IP address)*

### Option B: Arduino IDE (v2.x)
1. Install **ESP32 by Espressif Systems** via **Boards Manager**.
2. Install required libraries via **Library Manager**:
   - `IRremoteESP8266` (v2.8.6 or newer)
   - `ArduinoJson` (v7.x)
3. Open [`ac_controller/ac_controller.ino`](file:///h:/Dev/AC/ac_controller/ac_controller.ino).
4. Select board and settings in **Tools**:
   - **Board**: `ESP32C3 Dev Module`
   - **USB CDC On Boot**: `Enabled` *(Crucial for ESP32-C3 Super Mini serial output)*
   - **Flash Size**: `4MB (32Mb)`
   - **Partition Scheme**: `Minimal SPIFFS (Large APPS with OTA)` *(Crucial for OTA)*
   - **Upload Speed**: `921600` or `115200`
5. **Initial Flash**: Select the USB COM port and click **Upload**.
6. **Subsequent Wireless Flashes**: Select `ac (ESP32C3) at [IP]` under **Network Ports** in Arduino IDE and click **Upload**.

### Option C: Browser-Based Web OTA (`/update`)
You can also flash firmware without PlatformIO, Arduino IDE, or USB cables:
1. Compile your binary (in PlatformIO: `pio run`, produces `.pio/build/esp32-c3/firmware.bin`; in Arduino IDE: **Sketch** > **Export Compiled Binary**).
2. Open **[http://ac.local/update](http://ac.local/update)** (or click **OTA Update** in the dashboard footer).
3. Drop the `firmware.bin` file and click **Flash Firmware**. The ESP will update and reboot automatically in 5 seconds.

#### ESP32-C3 Super Mini Bootloader Note
If the board fails to enter download mode automatically during initial USB upload:
1. Press and hold the **BOOT** button (labeled `B` or `0` on the Super Mini).
2. Click the **RESET** button (labeled `R`) once while continuing to hold BOOT.
3. Release the **BOOT** button.
4. Retry the upload in Arduino IDE or PlatformIO.

---

## 5. Locating the Device on the Network

Once connected to your Wi-Fi network, the device announces itself via mDNS:
- Web UI: **[http://ac.local](http://ac.local)**
- If your router or OS does not support mDNS, check the Serial monitor output at boot (115200 baud) for the assigned IP address (e.g. `http://192.168.1.105`).

---

## 6. Schedule Day Semantics

In [`API.md`](file:///h:/Dev/AC/API.md), schedules specify `days` as a 7-element array:
`[Mon, Tue, Wed, Thu, Fri, Sat, Sun]` (where `1` = active, `0` = inactive).

- **Independent Event Evaluation**: Each `on` time and `off` time is evaluated independently against local device time.
- **Overnight Schedules**: For example, a schedule set with `on: "22:30"`, `off: "06:00"`, and `days: [1, 1, 1, 1, 1, 0, 0]` (active Monday to Friday):
  - The `22:30` **ON** event fires on Monday, Tuesday, Wednesday, Thursday, and Friday nights.
  - The `06:00` **OFF** event is evaluated against the day of week when 06:00 arrives. If Friday night had an ON event and Saturday 06:00 is not enabled in `days[]`, the 06:00 OFF event on Saturday will not fire unless Saturday is also checked.
- **Recovery & Redundancy**:
  - An **OFF** schedule event is always sent via IR even if the assumed state is already OFF (ensuring physical units recover if missed).
  - An **ON** schedule event is skipped only if the AC is already assumed ON and an identical ON command was sent less than 60 seconds prior.

---

## 7. REST API Endpoints & `curl` Quick Tests

Replace `ac.local` with your device IP if necessary.

### 7.1 Device State
```bash
curl http://ac.local/api/state
```

### 7.2 Toggle Power
```bash
# Power ON
curl -X POST http://ac.local/api/power -H "Content-Type: application/json" -d '{"on": true}'

# Power OFF
curl -X POST http://ac.local/api/power -H "Content-Type: application/json" -d '{"on": false}'
```

### 7.3 Synchronize Assumed Power (No IR Transmitted)
```bash
curl -X POST http://ac.local/api/sync -H "Content-Type: application/json" -d '{"power": "on"}'
```

### 7.4 Sleep Timer
```bash
# Start timer for 30 minutes (action: "off")
curl -X POST http://ac.local/api/timer -H "Content-Type: application/json" -d '{"minutes": 30, "action": "off"}'

# Extend timer by 15 minutes
curl -X POST http://ac.local/api/timer/extend -H "Content-Type: application/json" -d '{"minutes": 15}'

# Cancel active timer
curl -X DELETE http://ac.local/api/timer
```

### 7.5 Adjust AC Settings
```bash
curl -X POST http://ac.local/api/ac -H "Content-Type: application/json" -d '{"temp": 24, "mode": "cool", "fan": "med"}'
```

### 7.6 Schedules
```bash
# List all schedules
curl http://ac.local/api/schedules

# Create a new schedule
curl -X POST http://ac.local/api/schedules -H "Content-Type: application/json" \
  -d '{"enabled": true, "on": "22:30", "off": "06:00", "days": [1, 1, 1, 1, 1, 0, 0]}'

# Update an existing schedule (ID 1)
curl -X POST http://ac.local/api/schedules -H "Content-Type: application/json" \
  -d '{"id": 1, "enabled": false, "on": "22:30", "off": "06:00", "days": [1, 1, 1, 1, 1, 0, 0]}'

# Delete schedule ID 1
curl -X DELETE http://ac.local/api/schedules/1
```

### 7.7 Command Log
```bash
curl http://ac.local/api/log
```

### 7.8 IR Protocols & Setup
```bash
# Get supported AC protocols list
curl http://ac.local/api/ir/protocols

# Get active protocol and auto-scan status
curl http://ac.local/api/ir

# Set active protocol (e.g. ID 0 = Gree (model 2))
curl -X POST http://ac.local/api/ir/protocol -H "Content-Type: application/json" -d '{"id": 0}'

# Send test IR command
curl -X POST http://ac.local/api/ir/test -H "Content-Type: application/json" \
  -d '{"id": 0, "power": "on", "temp": 24, "mode": "cool", "fan": "auto"}'
```

### 7.9 Auto-Scan Controls
```bash
# Start auto-scan (4s interval)
curl -X POST http://ac.local/api/ir/scan -H "Content-Type: application/json" -d '{"action": "start", "interval_s": 4}'

# Pause auto-scan
curl -X POST http://ac.local/api/ir/scan -H "Content-Type: application/json" -d '{"action": "pause"}'

# Resume auto-scan
curl -X POST http://ac.local/api/ir/scan -H "Content-Type: application/json" -d '{"action": "resume"}'

# Step forward / backward / resend
curl -X POST http://ac.local/api/ir/scan -H "Content-Type: application/json" -d '{"action": "next"}'
curl -X POST http://ac.local/api/ir/scan -H "Content-Type: application/json" -d '{"action": "prev"}'
curl -X POST http://ac.local/api/ir/scan -H "Content-Type: application/json" -d '{"action": "resend"}'

# Stop auto-scan
curl -X POST http://ac.local/api/ir/scan -H "Content-Type: application/json" -d '{"action": "stop"}'
```

---

## 8. Using the Web Dashboard

Open **[http://ac.local](http://ac.local)** on any phone, tablet, or PC connected to your local network.

- **Power Control**: Tap the central hero button to toggle power ON or OFF. The button displays live feedback and assumed state.
- **Sleep Timer**:
  - Tap any preset (**15 min**, **30 min**, **60 min**, **120 min**) or enter custom minutes.
  - Automatically turns **ON** the AC immediately if it is currently OFF, counts down, and shuts **OFF** the AC when the timer expires.
  - Tapping power OFF at any time automatically cancels the active timer.
- **Multi-Day Schedules**:
  - Tap **+ Add** to schedule ON and OFF times.
  - Use one-tap day presets: **Everyday** (all 7 days), **Weekdays** (Mon–Fri), or **Weekends** (Sat–Sun).
  - Open any existing schedule to edit, or tap **Duplicate** to clone times to other days.
- **IR Protocol Setup & Auto-Scan**:
  - Default profile is set to **Gree (model 2)** (common OEM protocol for Voltas and others).
  - If using a different brand or model, expand **IR Setup** and click **Start Scan**. The ESP cycles candidate protocols every 4 seconds. When the AC responds, lock the protocol.
- **OTA Updates**: Click **OTA Update** in the footer to upload new `.bin` binaries over Wi-Fi.

---

## 9. Remote Access Anywhere (Tailscale)

To control your AC from outside your home without exposing ports to the public internet:

1. Use an existing machine on your network (Raspberry Pi, PC, or Apple TV) as a **Tailscale Subnet Router**:
   ```bash
   # On Linux / Raspberry Pi:
   sudo tailscale up --advertise-routes=192.168.1.0/24 --accept-routes
   ```
2. In the [Tailscale Admin Console](https://login.tailscale.com/admin/machines), approve the route `192.168.1.0/24`.
3. Open Tailscale on your phone on cellular data and navigate directly to `http://<ESP_LOCAL_IP>` (e.g. `http://192.168.1.150`).

