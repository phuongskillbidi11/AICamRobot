# AICamRobot

ESP32-S3-WROOM-CAM firmware providing live MJPEG streaming with AI-powered face detection, face recognition, motion detection, color detection, cat-face detection, and QR/barcode scanning — all accessible over WiFi via a browser.

## Quick start

```bash
# 1. Source ESP-IDF (v5.5+ required)
. ~/esp/esp-idf/export.sh

# 2. Configure WiFi credentials
cd ~/esp/AICamRobot
idf.py menuconfig   # ESP-WHO Configuration → Wi-Fi Configuration

# 3. Build, flash, and monitor
./deploy.sh /dev/ttyUSB0 460800
```

Open `http://<device-ip>` in a browser. Live stream is at port 81 (`http://<device-ip>:81/stream`).

## Skill system

This project uses a **progressive-disclosure skill system** in the `skills/` directory. Each skill covers one subsystem with exact file references, step-by-step instructions, and cross-references.

```bash
# List all skills
./scripts/load_skill.sh list

# Read a skill
./scripts/load_skill.sh show architecture
./scripts/load_skill.sh show build-flash
./scripts/load_skill.sh show face-recognition

# Search across all skills
./scripts/load_skill.sh search "queue"
```

### Available skills

| Skill ID | What it covers |
|----------|---------------|
| `environment-setup` | ESP-IDF installation, sourcing, version requirements |
| `build-flash` | `idf.py build`, `deploy.sh`, esptool flash commands |
| `menuconfig` | WiFi, camera module, AI model Kconfig parameters |
| `architecture` | Camera → AI → HTTP pipeline, FreeRTOS queue wiring |
| `camera` | Camera driver, frame formats, PSRAM buffering |
| `ai-detection` | Face, motion, color, cat-face detection modules |
| `face-recognition` | Enrollment, `fr` flash partition, recognition models |
| `web-streaming` | MJPEG stream (port 81), REST API (port 80), embedded UI |
| `wifi-network` | Dual-mode WiFi (STA + AP), mDNS camera discovery |
| `lcd-display` | Optional LCD output via BSP |
| `qr-scanner` | QR / Code39 / Code128 decoding (esp-code-scanner lib) |
| `hardware-pins` | GPIO mapping for ESP32-S3-WROOM-CAM and other modules |

## Project layout

```
main/               Entry point (app_main.cpp) — pipeline wiring only
components/
  modules/          Application logic (camera, ai, web, lcd)
  esp-dl/           Deep learning inference engine (git submodule)
  esp-code-scanner/ QR/barcode scanner (prebuilt .a libs)
  fb_gfx/           Frame buffer graphics helpers
managed_components/ IDF component manager dependencies (do not edit)
skills/             Skill system (manifest.json + per-skill SKILL.md)
scripts/            load_skill.sh helper
deploy.sh           Build + flash + monitor convenience script
partitions.csv      Custom partition table (factory app, nvs, fr)
sdkconfig.defaults  Baseline config (ESP32-S3, 16 MB flash, Octal PSRAM)
```
