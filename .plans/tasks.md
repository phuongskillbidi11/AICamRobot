# Tasks: Mouth-Open Detection

## Phase 1 — 5-Point Proxy (Option A)

### T1: Extend detection JSON with mouth metrics
**File:** `components/modules/ai/who_ai_utils.cpp`

In `update_detection_json()`, after building the `"lm"` object per face, compute and append:
- `mouth_width` = Euclidean dist(ml, mr) in pixels
- `inter_eye_dist` = Euclidean dist(le, re)  — used for normalisation
- `mouth_width_ratio` = mouth_width / inter_eye_dist  (float, 2 decimal places)
- `nose_mouth_dy` = y_mouth_avg - y_nose  (pixels, signed)
- `face_height` = box[3] - box[1]
- `nose_mouth_ratio` = nose_mouth_dy / face_height  (float, 2 decimal places)
- `mouth_open` = (mouth_width_ratio > MOUTH_W_THRESH) || (nose_mouth_ratio > MOUTH_Y_THRESH)
- `mouth_score` = blend of both normalised metrics (simple average, 0.0–1.0)

Default thresholds (adjust via menuconfig in future):
```
MOUTH_W_THRESH   = 0.70  (mouth wider than 70% of eye separation → likely open)
MOUTH_Y_THRESH   = 0.52  (mouth dropped more than 52% of face height from nose → likely open)
```

Verify: JSON is valid, no buffer overflow (512-byte limit — add mouth object ~80 chars/face).

### T2: Update frontend detection panel
**File:** `components/modules/web/www/index_ov2640.html`

In the `startDetectionPolling` fetch callback, extend the face string to include:
```
Face 1: box(83,77,164,188)  le(96,120) re(131,115) ...
        Mouth: OPEN  (71%)
```
- If `f.mouth.open == true`: colour label green `#00e676`
- If `f.mouth.open == false`: colour label `#aaa`
- Show score as percentage

Recompress: `cd www && bash compress_pages.sh`

### T3: Calibration note
The thresholds in T1 are empirical. Add a `//` comment in `who_ai_utils.cpp` explaining that they were set for the ESP32-S3-WROOM-CAM at QVGA/CIF resolution. At larger resolutions the pixel distances scale but the ratios remain stable (they are normalised).

---

## Phase 2 — Custom Lip Model (Option B, future)

These tasks are documented here for future execution; they cannot be done inside the IDF build alone.

### T4: Select and export source model
- Download PFLD-lite ONNX (available at github.com/polarisZhao/PFLD-pytorch)
- Strip to 8 lip keypoints (modify output layer) using Python
- Export trimmed model as `lip_landmark_8pt.onnx`

### T5: Quantise with esp-dl tools
```bash
cd components/esp-dl/tools/quantization
python quantize.py --model lip_landmark_8pt.onnx --target esp32s3 --calib_data ./calib_faces/
```
Output: `lip_landmark_8pt_esp32s3_s8.espdl`

### T6: Wrap model in C++ class
- New file: `components/modules/ai/who_lip_landmark.cpp/.hpp`
- Class `LipLandmark8Pt` with `infer(camera_fb_t*, dl::detect::result_t&)` → `std::vector<int>`
- Input: crop face region to 112×112 RGB565, run model
- Output: 8 (x,y) pairs in face-crop coords, mapped back to full-image coords

### T7: Integrate into face detection pipeline
- In `who_human_face_detection.cpp`, after drawing detection results, call `LipLandmark8Pt::infer` for each detected face
- Store in `result_t.keypoint` extended to 16 values (indices 10–25)
- `update_detection_json` already handles variable-length keypoints

### T8: Update JSON schema for 8-point lip
- Extend `lm` object with `lip` array: `[[x0,y0],[x1,y1],...]`
- Compute true MAR: `|bottom_center_y - top_center_y| / |right_corner_x - left_corner_x|`
- Replace proxy `mouth_open` with MAR-based threshold

### T9: Frontend canvas overlay
- Add `<canvas id="overlay">` on top of the MJPEG stream (CSS `position:absolute`)
- On each detection poll, draw 8 lip dots as small circles
- Connect outer lip dots with a polyline

---

## Phase 1 Completion Criteria
- `/detection` JSON includes `mouth` object with `open`, `score`, `width_ratio`, `nose_mouth_ratio`
- Web UI shows "Mouth: OPEN/CLOSED (XX%)" per face in the detection panel
- Tested at 30 cm camera distance with a person clearly opening/closing mouth
- False positive rate acceptable (no OPEN when mouth closed at rest)
