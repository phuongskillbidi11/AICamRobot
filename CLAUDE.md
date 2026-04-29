# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## ESP-IDF Environment

Always source ESP-IDF before any build/flash/menuconfig commands:
```bash
. ~/esp/esp-idf/export.sh
```

Require ESP-IDF >= v5.5. Earlier versions (v5.2) lack `MALLOC_CAP_SIMD` and will fail to build.

## Build & Flash

```bash
# Build only
idf.py build

# Full build + flash + monitor (recommended)
./deploy.sh [port] [baud] [build|flash|monitor|all]
# Defaults: port=/dev/ttyUSB0, baud=460800, action=all

# Configure WiFi credentials, camera module, AI model
idf.py menuconfig   # → "ESP-WHO Configuration"

# Monitor serial output
idf.py monitor -p /dev/ttyUSB0
```

WiFi SSID/password, camera module selection, and face recognition model parameters are all set through `menuconfig` — they are not hardcoded.

## Architecture

The application is a streaming AI camera with face detection over WiFi. `main/app_main.cpp` is the wiring point: it creates two FreeRTOS queues and chains camera → AI → HTTP streamer.

```
Camera task  →  xQueueAIFrame  →  AI detection task  →  xQueueHttpFrame  →  HTTP MJPEG streamer
```

Each stage receives a `camera_fb_t*` from its input queue, processes it, and forwards to the output queue. Frames are returned to the camera driver only after the full pipeline is done.

**`components/modules/`** contains all application logic, split by subsystem:

- `camera/who_camera.c` — camera init; `register_camera()` spawns the capture task
- `ai/` — detection/recognition modules: `who_human_face_detection`, `who_human_face_recognition`, `who_motion_detection`, `who_color_detection`, `who_cat_face_detection`. Each exposes a `register_*()` function that pins its FreeRTOS task to core 0.
- `web/app_httpd.cpp` — MJPEG stream endpoint + camera control REST API; embeds gzipped HTML UIs for OV2640/OV3660/OV5640 sensors
- `web/app_wifi.c` — dual-mode WiFi (STA + AP fallback)
- `web/app_mdns.c` — mDNS hostname advertisement
- `lcd/` — optional LCD display output

**`components/esp-dl/`** — Espressif deep learning inference library (git submodule). Provides `HumanFaceDetectMSR01`, `HumanFaceDetectMNP01`, and face recognition models.

**`components/esp-code-scanner/`** — QR/barcode scanner (prebuilt `.a` libs for esp32/esp32s2/esp32s3).

**`managed_components/`** — IDF component manager dependencies (esp32-camera driver, mdns, jpeg, etc.). Do not edit these.

## Hardware & Partition Notes

Target: **ESP32-S3-WROOM-CAM** (`CONFIG_CAMERA_MODULE_ESP_S3_WROOM_CAM`). Flash: 16 MB DIO, PSRAM: Octal 80 MHz.

Custom partition layout (`partitions.csv`):
- `factory` app at 0x10000, 3840 KB
- `nvs` at 0x3D0000, 16 KB
- `fr` (face recognition enrollment store) at 0x3E0000, 128 KB

The `fr` partition persists enrolled face IDs across reboots via NVS. Erasing it clears all enrolled faces.

## Hardware Debugging Protocol

Before any register-level or `/dev/mem` work, verify the physical pin exists:
- Check `io_num` in the relevant `who_camera.h` pin block for the active `CAMERA_MODULE_*`
- Confirm against the SoC datasheet — do not assume a peripheral has RTS/CTS just because the driver supports it

When adding new `.c`/`.cpp` source files, update `idf_component_register(SRCS ...)` or `SRC_DIRS` in the relevant `CMakeLists.txt` in the same change.
