# Skill: Face Recognition

## Purpose
Enroll face identities into flash, persist them across reboots in the dedicated `fr` partition, and perform recognition against enrolled IDs at runtime.

## When to use
- Enabling recognition (not just detection) in the pipeline
- Enrolling, deleting, or clearing stored face IDs
- Debugging recognition failures or `fr` partition corruption
- Choosing between 8-bit (S8) and 16-bit (S16) quantization

## Files involved
- `components/modules/ai/who_human_face_recognition.cpp/.hpp` — recognition task
- `components/modules/web/app_httpd.cpp` — REST endpoints that trigger enroll/recognize/delete
- `partitions.csv` — defines the `fr` data partition at offset `0x3E0000`, size 128 KB
- `sdkconfig` — `CONFIG_MFN_V1`, `CONFIG_S8`/`CONFIG_S16` (set via menuconfig)

## Model selection (menuconfig)
Path: `ESP-WHO Configuration` → `Model Configuration` → `Face Recognition`

| Quantization | Header included | RAM usage | Accuracy |
|-------------|----------------|-----------|----------|
| S8 (8-bit)  | `face_recognition_112_v1_s8.hpp` | Lower | Sufficient for most uses |
| S16 (16-bit)| `face_recognition_112_v1_s16.hpp` | Higher | Better for difficult lighting |

## fr partition
Defined in `partitions.csv`:
```
fr, 32, 32, 0x3E0000, 128K
```
The recognizer loads enrolled IDs at startup:
```cpp
recognizer->set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr");
int count = recognizer->set_ids_from_flash();
```
IDs persist across power cycles. The partition holds up to the number of faces that fit within 128 KB (depends on feature vector size per model).

## Runtime states
The recognition task is driven by a `recognizer_state_t gEvent` enum:

| State | Meaning |
|-------|---------|
| `DETECT` | Default; detect faces but do not enroll or match |
| `ENROLL` | Next detected face → generate feature vector → save to `fr` partition |
| `RECOGNIZE` | Compare detected face against all enrolled IDs |
| `DELETE` | Delete the most recently enrolled ID from flash |

State is changed by HTTP REST commands (see **skill: web-streaming**):
- `?var=face_enroll&val=1` → sets `is_enrolling`, triggers `ENROLL` state
- `?var=face_recognize&val=1` → sets `recognition_enabled`, triggers `RECOGNIZE`
- `?var=face_detect&val=0` → also clears `recognition_enabled`

## Enrollment flow (inside `task_process_handler`)
```cpp
case ENROLL:
    recognizer->enroll_id(
        (uint16_t*)frame->buf,
        {height, width, 3},
        detect_results.front().keypoint,   // 5 landmarks from stage-1 detector
        "",                                 // label (empty = auto ID)
        true                               // save to flash
    );
    // Logs: "ID N is enrolled"
```
The 5-landmark keypoints come from `HumanFaceDetectMNP01`; the detector must have exactly one face in the frame for enrollment to be reliable.

## Recognition result
```cpp
face_info_t recognize_result = recognizer->recognize(...);
// recognize_result.id > 0  → known face, ID number
// recognize_result.id <= 0 → unknown face
// recognize_result.similarity → cosine similarity score
```
Result is overlaid on the frame:
- Green: `"ID N"` for a match
- Red: `"who ?"` for no match

## Clearing all enrolled faces
To erase the entire `fr` partition:
```bash
python3 -m esptool --chip esp32s3 -p /dev/ttyUSB0 \
  erase_region 0x3E0000 0x20000
```
The device will have zero enrolled IDs on next boot.

## Advanced / cross-references
- The `FRAME_DELAY_NUM` constant (16 frames) controls how long the enroll/recognize/delete overlay is shown before returning to `SHOW_STATE_IDLE`.
- `xMutex` (binary semaphore) guards `gEvent` between the HTTP handler task and the AI processing task.
- See **skill: ai-detection** for the underlying two-stage detector that feeds keypoints to the recognizer.
- See **skill: web-streaming** for the HTTP API that controls recognition state.
- See **skill: menuconfig** to switch quantization model.
