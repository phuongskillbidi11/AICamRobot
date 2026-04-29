# Skill: Hardware Pin Mapping

## Purpose
Reference the GPIO assignments for the active camera module and understand how pin macros are resolved at compile time.

## When to use
- Verifying physical connectivity before register-level debugging
- Switching to a different camera board
- Adding a custom camera module with non-standard wiring
- Checking whether a peripheral (UART, I2C, SPI) conflicts with camera pins

## Files involved
- `components/modules/camera/who_camera.h` — all `#define CAMERA_PIN_*` macros, per module
- `sdkconfig` / `sdkconfig.defaults.esp32s3` — selects active module via `CONFIG_CAMERA_MODULE_*`
- `components/modules/Kconfig` — defines the `CAMERA_MODULE` choice and custom pin fields

## Active board: ESP32-S3-WROOM-CAM
Set by `CONFIG_CAMERA_MODULE_ESP_S3_WROOM_CAM=y` in `sdkconfig.defaults.esp32s3`.

| Signal | GPIO |
|--------|------|
| PWDN | −1 (not used) |
| RESET | −1 (software reset) |
| XCLK | 15 |
| SIOD (SDA) | 4 |
| SIOC (SCL) | 5 |
| D0 (Y2) | 11 |
| D1 (Y3) | 9 |
| D2 (Y4) | 8 |
| D3 (Y5) | 10 |
| D4 (Y6) | 12 |
| D5 (Y7) | 18 |
| D6 (Y8) | 17 |
| D7 (Y9) | 16 |
| VSYNC | 6 |
| HREF | 7 |
| PCLK | 13 |
| XCLK freq | 10 MHz |

## All supported modules (for reference)

| Module | SIOD | SIOC | XCLK | VSYNC | HREF | PCLK | PWDN | RESET |
|--------|------|------|------|-------|------|------|------|-------|
| ESP-S3-WROOM-CAM | 4 | 5 | 15 | 6 | 7 | 13 | −1 | −1 |
| ESP-S3-EYE | 4 | 5 | 15 | 6 | 7 | 13 | −1 | −1 |
| WROVER-KIT | 26 | 27 | 21 | 25 | 23 | 22 | −1 | −1 |
| ESP-EYE | 18 | 23 | 4 | 5 | 27 | 25 | −1 | −1 |
| AI-Thinker | 26 | 27 | 0 | 25 | 23 | 22 | 32 | −1 |
| M5Stack PSRAM | 25 | 23 | 27 | 22 | 26 | 21 | −1 | 15 |
| ESP-S2-Kaluga | 8 | 7 | 1 | 2 | 3 | 33 | −1 | −1 |

## Custom module
Set `CONFIG_CAMERA_MODULE_CUSTOM=y` via menuconfig (path: `ESP-WHO Configuration → Camera Configuration → Select Camera Pinout → Custom Camera Pinout`). Each pin becomes individually configurable in menuconfig.

## How pin macros resolve
In `who_camera.h`, the active `CONFIG_CAMERA_MODULE_*` preprocessor symbol selects a `#elif` block that defines all `CAMERA_PIN_*` macros. `who_camera.c` then uses these macros to populate `camera_config_t`:
```c
config.pin_d0     = CAMERA_PIN_D0;
config.pin_xclk   = CAMERA_PIN_XCLK;
// ... etc.
config.xclk_freq_hz = XCLK_FREQ_HZ;  // always 10 MHz
```

## Before any GPIO-level debugging
1. Confirm `io_num` from this table — do not assume a signal exists just because the driver supports it.
2. Check for conflicts: GPIO 4, 5 (SIOD/SIOC) are also commonly used for I2C peripherals. GPIO 6–13 form the camera data bus — none are available for other use.
3. For UART RS-485 on this board: confirm the target UART instance has a physical RTS pin before attempting DE/RE control. Not all UART peripherals have RTS wired out on this module.

## Advanced / cross-references
- ESP-EYE and ESP32-CAM-BOARD require IO13/IO14 to be pulled-up as inputs due to JTAG conflicts — `who_camera.c` handles this automatically for those modules.
- Switching modules requires changing `CONFIG_CAMERA_MODULE_*` in menuconfig, then rebuilding — no source changes needed.
- See **skill: menuconfig** to switch camera modules interactively.
- See **skill: camera** for how these pin macros feed into `camera_config_t`.
