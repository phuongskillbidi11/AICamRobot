# Skill: Camera Driver

## Purpose
Initialize the camera peripheral, configure frame format/size/buffering, and run the continuous capture task that feeds frames into the pipeline.

## When to use
- Changing capture resolution or pixel format
- Debugging camera init failures (`Camera init failed with error 0x...`)
- Adjusting frame buffer count or PSRAM allocation strategy
- Adding per-sensor image corrections (flip, mirror, brightness, saturation)

## Files involved
- `components/modules/camera/who_camera.c` — `register_camera()` and capture task
- `components/modules/camera/who_camera.h` — pin macros, `register_camera()` declaration
- `managed_components/espressif__esp32-camera/` — Espressif camera driver (do not edit)

## Key configuration (set via `register_camera()` call in `app_main.cpp`)

```c
register_camera(
    PIXFORMAT_RGB565,   // pixel format
    FRAMESIZE_QVGA,     // 320×240
    2,                  // fb_count (double-buffer in PSRAM)
    xQueueAIFrame       // destination queue
);
```

### Fixed hardware settings (from `who_camera.c`)
| Parameter | Value |
|-----------|-------|
| XCLK frequency | 10 MHz (`XCLK_FREQ_HZ`) |
| JPEG quality | 12 (only relevant for PIXFORMAT_JPEG) |
| Frame buffer location | `CAMERA_FB_IN_PSRAM` |
| Grab mode | `CAMERA_GRAB_WHEN_EMPTY` |
| LEDC channel/timer | 0 / 0 |

### Pixel format options (for AI modules, use RGB565)
| Format | AI compatible | Notes |
|--------|--------------|-------|
| `PIXFORMAT_RGB565` | Yes | Required by all `who_*.cpp` AI modules |
| `PIXFORMAT_JPEG` | No | Use for HTTP-only streaming (higher throughput) |
| `PIXFORMAT_GRAYSCALE` | Partial | Required by QR scanner (see skill: qr-scanner) |

### Frame size options (common)
| Symbol | Resolution |
|--------|-----------|
| `FRAMESIZE_QVGA` | 320×240 (default, AI use) |
| `FRAMESIZE_VGA` | 640×480 |
| `FRAMESIZE_SVGA` | 800×600 |
| `FRAMESIZE_HD` | 1280×720 |

## Sensor auto-corrections (applied in `register_camera()`)
| Sensor | Correction |
|--------|-----------|
| OV3660, OV2640 | `set_vflip(1)` — flip vertically |
| GC0308 | `set_hmirror(0)` — disable horizontal mirror |
| GC032A | `set_vflip(1)` |
| OV3660 | Additional: brightness +1, saturation −2 |

## Capture task internals
```c
// Runs on core 1, priority 5, stack 3 KB
static void task_process_handler(void *arg) {
    while (true) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame)
            xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
    }
}
```
The task blocks on `esp_camera_fb_get()` until the hardware has a new frame. `portMAX_DELAY` on `xQueueSend` means it blocks if the downstream queue is full — back-pressure propagates upstream.

## Advanced / cross-references
- Frame buffers are allocated in PSRAM. If `CONFIG_SPIRAM` is not enabled the driver will crash with an allocation error.
- `fb_count=2` enables double-buffering: camera captures into one buffer while the previous is being processed. Increase to 3 if AI processing is slower than capture rate.
- To switch sensors at runtime, call `esp_camera_deinit()` + `esp_camera_init()` with new config.
- See **skill: hardware-pins** for the GPIO assignments that feed into `camera_config_t`.
- See **skill: architecture** for how the output queue connects to downstream modules.
