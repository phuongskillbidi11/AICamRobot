# Skill: QR / Barcode Scanner

## Purpose
Decode QR codes, Code39, and Code128 barcodes from camera frames using the prebuilt `esp-code-scanner` library.

## When to use
- Adding barcode/QR scanning as a processing stage in the pipeline
- Integrating scan results with other application logic (MQTT, HTTP, GPIO trigger)
- Understanding the scanner's image format requirements

## Files involved
- `components/esp-code-scanner/include/esp_code_scanner.h` — public API
- `components/esp-code-scanner/lib/esp32s3/libesp-code-scanner.a` — prebuilt static library for ESP32-S3
- `components/esp-code-scanner/CMakeLists.txt` — links the correct `.a` for the target

## Supported code types
| Type | `esp_code_scanner_symbol_type_t` |
|------|----------------------------------|
| QR Code | `ESP_CODE_SCANNER_SYMBOL_QR` |
| Code 39 | `ESP_CODE_SCANNER_SYMBOL_CODE39` |
| Code 128 | `ESP_CODE_SCANNER_SYMBOL_CODE128` |

## Image format requirement
The scanner accepts **grayscale** images only (`ESP_CODE_SCANNER_IMAGE_GRAY`). RGB565 and YUV422 modes are declared in the header but may not be fully implemented in the prebuilt library — use grayscale to be safe.

To capture grayscale from the camera:
```c
register_camera(PIXFORMAT_GRAYSCALE, FRAMESIZE_QVGA, 2, xQueueScanFrame);
```
Note: grayscale frames cannot be passed to the AI detection modules (which require RGB565). If you need both, use two separate capture configurations or convert in-pipeline.

## API usage

```c
#include "esp_code_scanner.h"

// 1. Create scanner instance
esp_image_scanner_t *scanner = esp_code_scanner_create();

// 2. Configure
esp_code_scanner_config_t config = {
    .mode   = ESP_CODE_SCANNER_MODE_FAST,
    .fmt    = ESP_CODE_SCANNER_IMAGE_GRAY,
    .width  = 320,
    .height = 240,
};
esp_code_scanner_set_config(scanner, config);

// 3. Scan a frame (image_data must be grayscale, width*height bytes)
int num_symbols = esp_code_scanner_scan_image(scanner, frame->buf);

// 4. Read results
if (num_symbols > 0) {
    const esp_code_scanner_symbol_t result = esp_code_scanner_result(scanner);
    // result.type_name  → "QR-Code", "CODE-39", "CODE-128"
    // result.data       → decoded string (null-terminated)
    // result.datalen    → length of data
    // result.next       → next symbol or NULL
}

// 5. Cleanup (when done with scanner instance)
esp_code_scanner_destroy(scanner);
```

## Pipeline integration example

```c
// app_main.cpp — QR scan stage before HTTP
static QueueHandle_t xQueueScanFrame  = xQueueCreate(2, sizeof(camera_fb_t *));
static QueueHandle_t xQueueHttpFrame  = xQueueCreate(2, sizeof(camera_fb_t *));

register_camera(PIXFORMAT_GRAYSCALE, FRAMESIZE_QVGA, 2, xQueueScanFrame);
// Insert custom scan task here that reads xQueueScanFrame,
// calls esp_code_scanner_scan_image, then sends to xQueueHttpFrame
register_httpd(xQueueHttpFrame, NULL, true);
```

The scanner has no built-in `register_*` wrapper; wrap it in a FreeRTOS task following the pattern in `who_motion_detection.cpp`.

## Advanced / cross-references
- The prebuilt library is target-specific: `lib/esp32s3/` for S3, `lib/esp32/` for original ESP32. The `CMakeLists.txt` selects the correct one via `$ENV{IDF_TARGET}`.
- `libnewlib_iconv.a` in `components/esp-code-scanner/lib/` provides character encoding support for QR data.
- Scan accuracy improves with adequate lighting and a minimum barcode width of ~40 pixels at QVGA resolution.
- See **skill: camera** for pixel format and frame size options.
- See **skill: architecture** for how to insert a custom task into the pipeline.
