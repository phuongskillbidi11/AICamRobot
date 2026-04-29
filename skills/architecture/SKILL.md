# Skill: System Architecture

## Purpose
Understand the full data pipeline — from camera capture through AI inference to HTTP streaming — and how the FreeRTOS queue system wires every module together.

## When to use
- Before adding a new processing stage to the pipeline
- When debugging a frame stall or queue deadlock
- When deciding which core to pin a new task to
- When connecting a new module (e.g., QR scanner or LCD) into the pipeline

## Files involved
- `main/app_main.cpp` — entry point; owns queue creation and module wiring
- `components/modules/camera/who_camera.c` — producer; sends `camera_fb_t*` to queue
- `components/modules/ai/who_*.cpp` — consumers/producers; receive frames, annotate, forward
- `components/modules/web/app_httpd.cpp` — final consumer; serves frames over HTTP
- `components/modules/lcd/who_lcd.c` — optional final consumer; draws to display
- `components/modules/web/app_wifi.c` — runs before pipeline; blocks until WiFi is up
- `components/modules/web/app_mdns.c` — background task; advertises the camera on the LAN

## Pipeline overview

```
[app_wifi_main]  ← blocks until STA connected or AP ready

[Camera Task]  (core 1, priority 5)
    esp_camera_fb_get()
         │
         ▼  camera_fb_t*
    xQueueAIFrame (depth 2)
         │
         ▼
[AI Detection Task]  (core 0, priority 5)
    HumanFaceDetectMSR01 + HumanFaceDetectMNP01
    draw_detection_result() annotates frame in-place
         │
         ▼  same camera_fb_t*  (annotated)
    xQueueHttpFrame (depth 2)
         │
         ▼
[HTTP Stream Handler]  (httpd task, any core)
    frame2jpg() → multipart/x-mixed-replace response on port 81
    esp_camera_fb_return(frame)   ← frame returned to camera driver here
```

**Optional LCD stage** (insert between AI and HTTP):
```
    xQueueAIFrame → AI → xQueueLcdFrame → LCD → xQueueHttpFrame → HTTP
```

**Optional event/result side-channels** (all modules support them):
```
xQueueEvent  →  task_event_handler  →  sets gEvent flag
xQueueResult ←  task_process_handler  sends bool (detected/moved/etc.)
```

## Queue design rules
- All queues carry `camera_fb_t*` pointers — **not copies**. Only one stage owns the frame at any time.
- A frame must be either forwarded (`xQueueSend`), returned to the driver (`esp_camera_fb_return`), or freed — never silently dropped.
- `gReturnFB` flag on each module controls whether that module calls `esp_camera_fb_return` or delegates to the next stage.
- Queue depth is 2 throughout. This limits latency but keeps memory use bounded.

## Core pinning summary
| Task | Core | Reason |
|------|------|--------|
| Camera capture | 1 | Keeps DMA/camera ISRs on one core |
| AI inference | 0 | SIMD/vector ops prefer core 0 on S3 |
| LCD draw | 0 | Follows AI stage |
| mDNS query | any | Low priority background task |
| HTTP server | any | lwIP / httpd internal |

## Adding a new stage
1. Create a `register_<stage>(frame_i, event, result, frame_o)` function following the pattern in any `who_*.cpp`.
2. In `app_main.cpp`: create a new `QueueHandle_t`, insert `register_<stage>()` between the two existing queue handles.
3. Pin the new task to core 0 with `xTaskCreatePinnedToCore(..., 0)`.
4. Ensure the new module is listed in `components/modules/CMakeLists.txt` `SRC_DIRS`.

## Advanced / cross-references
- See **skill: camera** for frame buffer internals and PSRAM allocation.
- See **skill: ai-detection** for the two-stage detection pattern and `gEvent` flag.
- See **skill: web-streaming** for the HTTP endpoints that consume `xQueueHttpFrame`.
- See **skill: face-recognition** for the extended AI stage with enroll/delete/recognize state.
