# Skill: AI Detection Modules

## Purpose
Run neural-network inference on camera frames to detect faces, motion, color blobs, or cat faces. Each module follows the same FreeRTOS task pattern and can be swapped in/out of the pipeline independently.

## When to use
- Adding or removing a detection feature from the pipeline
- Tuning detection thresholds or enabling/disabling the two-stage detector
- Debugging detection task crashes (stack overflow, queue stall)
- Understanding how detected results are drawn on frames before streaming

## Files involved
| File | Detection type |
|------|---------------|
| `components/modules/ai/who_human_face_detection.cpp/.hpp` | Human face (two-stage) |
| `components/modules/ai/who_human_face_recognition.cpp/.hpp` | Human face + recognition |
| `components/modules/ai/who_motion_detection.cpp/.hpp` | Motion (frame diff) |
| `components/modules/ai/who_color_detection.cpp/.hpp` | Color blob |
| `components/modules/ai/who_cat_face_detection.cpp/.hpp` | Cat face |
| `components/modules/ai/who_ai_utils.cpp/.hpp` | Shared: `draw_detection_result`, `print_detection_result` |
| `components/esp-dl/` | Deep learning inference engine (git submodule) |

## Common module pattern

Every detection module exports one function:
```c
void register_<module>(
    QueueHandle_t frame_i,   // input: receives camera_fb_t*
    QueueHandle_t event,     // optional: bool/enum to pause/resume detection
    QueueHandle_t result,    // optional: sends detection result (bool or struct)
    QueueHandle_t frame_o,   // optional: forwards annotated frame downstream
    bool camera_fb_return    // if true, module returns frame to driver itself
);
```
Internally spawns `task_process_handler` (pinned to core 0) and optionally `task_event_handler` (pinned to core 1).

## Human face detection (two-stage)

```cpp
// In who_human_face_detection.cpp
HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);   // stage 1: proposals
HumanFaceDetectMNP01 detector2(0.4F, 0.3F, 10);          // stage 2: refinement

#define TWO_STAGE_ON 1   // set to 0 for single-stage (faster, less accurate)
```

Stage 1 (`MSR01`) parameters: `(score_threshold, nms_threshold, top_k, resize_scale)`
Stage 2 (`MNP01`) parameters: `(score_threshold, nms_threshold, top_k)`

Input must be `PIXFORMAT_RGB565`. Frame dimensions are passed as `{height, width, 3}`.

Detection results are `std::list<dl::detect::result_t>`. Each result has:
- `box` — bounding box `[x1, y1, x2, y2]`
- `keypoint` — 5 facial landmarks (used by face recognition)
- `score` — confidence

## Motion detection

Compares two consecutive frames:
```cpp
uint32_t moving_point_number = dl::image::get_moving_point_number(
    (uint16_t*)frame1->buf, (uint16_t*)frame2->buf,
    frame1->height, frame1->width,
    8,    // block size
    15    // threshold per block
);
if (moving_point_number > 50) { /* motion detected */ }
```
Draws a 20×20 red rectangle at top-left when triggered. Motion task consumes **two** frames per cycle.

## gEvent flag (pause/resume)
Each module has a `static bool gEvent = true`. When `false`, the task loop idles without dequeuing frames. The `xQueueEvent` side-channel allows external control:
```c
bool pause = false;
xQueueSend(xQueueEvent, &pause, 0);   // pause detection
bool resume = true;
xQueueSend(xQueueEvent, &resume, 0);  // resume detection
```

## Drawing results on frames
`who_ai_utils.cpp` provides:
```cpp
void draw_detection_result(uint16_t *image_ptr, int height, int width,
                           std::list<dl::detect::result_t> &results);
void print_detection_result(std::list<dl::detect::result_t> &results);
```
Annotation is done **in-place** on the RGB565 frame buffer before forwarding.

## Task stack sizes
| Task | Stack |
|------|-------|
| `task_process_handler` (all AI modules) | 4 KB |
| `task_event_handler` | 4 KB |

Increase if you add large local variables or deep call stacks inside the handler.

## Advanced / cross-references
- To run **two detection stages in series** (e.g., face detection then QR scan), chain them with an intermediate queue: `camera → AI-1 queue → AI-2 queue → HTTP`.
- `components/esp-dl` is a git submodule. If the `include/` headers are missing, run `git submodule update --init components/esp-dl`.
- See **skill: face-recognition** for the recognition extension of the face detection module.
- See **skill: architecture** for queue wiring and core pinning rationale.
