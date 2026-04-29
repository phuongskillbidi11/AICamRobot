# Skill: LCD Display

## Purpose
Render camera frames (optionally AI-annotated) on an onboard LCD panel using the ESP-IDF LCD driver and a Board Support Package (BSP).

## When to use
- Adding local display output to the pipeline (e.g., show live feed + detection boxes on screen)
- Debugging the LCD when it shows only the wallpaper or stays blank
- Understanding why the LCD stage is conditionally compiled (`#if __has_include("bsp/display.h")`)

## Files involved
- `components/modules/lcd/who_lcd.c` — `register_lcd()`, draw task
- `components/modules/lcd/who_lcd.h` — public API
- `components/modules/lcd/logo_en_240x240_lcd.h` — boot wallpaper bitmap (240×240 RGB565)
- `managed_components/espressif__esp32_s3_eye_noglib/` — BSP that provides `bsp/display.h` for ESP-S3-EYE

## Conditional compilation
The entire LCD implementation is guarded:
```c
#if __has_include("bsp/display.h")
// ... full implementation
#endif
```
If the active board BSP does not provide `bsp/display.h`, `register_lcd()` compiles to a no-op. This means LCD is only active when using a board with a BSP that exposes the display API (e.g., ESP-S3-EYE via `espressif__esp32_s3_eye_noglib`).

## API
```c
// Insert into pipeline between AI and HTTP stages
esp_err_t register_lcd(
    QueueHandle_t frame_i,    // input queue
    QueueHandle_t frame_o,    // output queue (forward to next stage, or NULL)
    bool return_fb            // if true, return frame to camera driver after display
);

void app_lcd_draw_wallpaper();   // display boot logo
void app_lcd_set_color(int color); // fill screen with solid color
```

## Pipeline integration example
```c
// In app_main.cpp
static QueueHandle_t xQueueAIFrame  = NULL;
static QueueHandle_t xQueueLcdFrame = NULL;
static QueueHandle_t xQueueHttpFrame = NULL;

xQueueAIFrame   = xQueueCreate(2, sizeof(camera_fb_t *));
xQueueLcdFrame  = xQueueCreate(2, sizeof(camera_fb_t *));
xQueueHttpFrame = xQueueCreate(2, sizeof(camera_fb_t *));

register_camera(PIXFORMAT_RGB565, FRAMESIZE_QVGA, 2, xQueueAIFrame);
register_human_face_detection(xQueueAIFrame, NULL, NULL, xQueueLcdFrame);
register_lcd(xQueueLcdFrame, xQueueHttpFrame, false);
register_httpd(xQueueHttpFrame, NULL, true);
```

## Draw task internals
```c
// Runs on core 0, priority 5, stack 4 KB
static void task_process_handler(void *arg) {
    while (true) {
        if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY)) {
            esp_lcd_panel_draw_bitmap(panel_handle, 0, 0,
                frame->width, frame->height, (uint16_t *)frame->buf);
            // forward or return frame
        }
    }
}
```

## Startup sequence
`register_lcd()` performs:
1. `bsp_display_new()` — initialize panel and I/O handle
2. `esp_lcd_panel_disp_on_off(true)` — power on panel
3. `bsp_display_backlight_on()` — turn on backlight
4. `app_lcd_set_color(0x000000)` — black screen
5. 200 ms delay
6. `app_lcd_draw_wallpaper()` — show 240×240 boot logo
7. 200 ms delay
8. Spawn `task_process_handler`

## Advanced / cross-references
- The 240×240 wallpaper (`logo_en_240x240_lcd.h`) is raw RGB565 data. Replace the array to change the boot logo.
- Frame data must be RGB565; passing JPEG frames to `esp_lcd_panel_draw_bitmap` will display garbage.
- To use LCD without HTTP (embedded display only), pass `frame_o = NULL` and `return_fb = true`.
- See **skill: architecture** for where to insert the LCD stage in the queue chain.
- See **skill: hardware-pins** if the LCD panel SPI/I2C pins need adjusting.
