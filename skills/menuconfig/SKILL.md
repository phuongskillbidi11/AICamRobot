# Skill: Menuconfig

## Purpose
Configure project-level parameters — WiFi credentials, camera module selection, AI model — through the interactive Kconfig menu. All user-configurable values live here; they are never hardcoded in source files.

## When to use
- Setting WiFi SSID/password before the first flash
- Switching the camera hardware target (e.g., from ESP-S3-EYE to ESP-S3-WROOM-CAM)
- Changing face recognition model quantization (8-bit vs 16-bit)
- Adjusting mDNS hostname, AP SSID/password, retry count

## Files involved
- `components/modules/Kconfig` — defines all `ESP-WHO Configuration` options
- `sdkconfig` — generated file storing current values (do not edit manually)
- `sdkconfig.defaults` — baseline defaults applied before menuconfig
- `sdkconfig.defaults.esp32s3` — S3-specific defaults (PSRAM, flash, camera module)

## Step-by-step instructions

### 1. Open menuconfig
```bash
. ~/esp/esp-idf/export.sh
cd ~/esp/AICamRobot
idf.py menuconfig
```
Navigate with arrow keys; Enter to select; `?` for help; `Q` to save and quit.

### 2. WiFi Configuration
Path: `ESP-WHO Configuration` → `Wi-Fi Configuration`

| Key | Purpose | Default |
|-----|---------|---------|
| `ESP_WIFI_SSID` | STA network to join | _(empty = off)_ |
| `ESP_WIFI_PASSWORD` | STA password | _(empty)_ |
| `ESP_WIFI_AP_SSID` | AP to create | `ESP32-Camera` |
| `ESP_WIFI_AP_PASSWORD` | AP password (empty = open) | _(empty)_ |
| `SERVER_IP` | AP interface IP | `192.168.4.1` |
| `ESP_HOST_NAME` | mDNS hostname | _(empty = auto from MAC)_ |
| `ESP_MAXIMUM_RETRY` | STA reconnect attempts | `5` |

**Modes selected at runtime** (`app_wifi_main`):
- Both STA and AP SSID set → `WIFI_MODE_APSTA`
- Only AP SSID set → `WIFI_MODE_AP`
- Only STA SSID set → `WIFI_MODE_STA`
- Neither set → WiFi disabled

### 3. Camera Module Selection
Path: `ESP-WHO Configuration` → `Camera Configuration` → `Select Camera Pinout`

Supported choices and their `#define`:
| Choice | Config symbol |
|--------|--------------|
| ESP32-S3-WROOM CAM | `CAMERA_MODULE_ESP_S3_WROOM_CAM` ← **current board** |
| ESP-S3-EYE DevKit | `CAMERA_MODULE_ESP_S3_EYE` |
| AI-Thinker ESP32-CAM | `CAMERA_MODULE_AI_THINKER` |
| Custom | `CAMERA_MODULE_CUSTOM` (exposes individual pin fields) |

### 4. AI Model Configuration
Path: `ESP-WHO Configuration` → `Model Configuration` → `Face Recognition`

| Option | Values |
|--------|--------|
| Face Recognition Model | `mfn v1` (only option) |
| Quantization | `S8` (8-bit, default) or `S16` (16-bit, higher accuracy, more RAM) |

These control which header is included in `who_human_face_recognition.cpp`:
- S8 → `face_recognition_112_v1_s8.hpp`
- S16 → `face_recognition_112_v1_s16.hpp`

### 5. Apply changes
After exiting menuconfig, rebuild:
```bash
idf.py build
```
Changes to `sdkconfig` are picked up automatically. You do not need `fullclean` unless you switch IDF target.

## Advanced / cross-references
- `sdkconfig` is generated and should be committed to version control so that all team members use the same build configuration.
- To script non-interactive config changes: `idf.py -DSDKCONFIG_DEFAULTS=... build` or directly append `CONFIG_*=y` to `sdkconfig.defaults` and delete `sdkconfig` to force regeneration.
- See **skill: hardware-pins** for the exact GPIO mapping produced by the selected camera module.
- See **skill: wifi-network** for runtime WiFi behavior details.
