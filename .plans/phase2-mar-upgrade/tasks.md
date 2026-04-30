# Phase 2 Tasks

## Track 2a — Pixel MAR (Implementable Now)

### T1 — Extend `update_detection_json` to accept frame
**File:** `components/modules/ai/who_ai_utils.hpp`

Change signature:
```cpp
void update_detection_json(std::list<dl::detect::result_t> &results,
                           camera_fb_t *frame = nullptr);
```
`frame` defaults to nullptr for backward compatibility.

### T2 — Implement `compute_mouth_pix_mar` in who_ai_utils.cpp
Add a file-static function. Algorithm:
1. Extract mouth corners from keypoints
2. Compute ROI (narrow vertical strip, centre of mouth width)
3. Clamp to image bounds
4. For each ROI row: compute mean green luminance
5. Compute darkness = 1 - min_row/mean_all; contrast = (max-min)/mean_all
6. pixel_mar = 0.55*darkness + 0.45*contrast, clamped [0,1]
7. mouth_open = pixel_mar > PIX_MAR_THRESHOLD (0.35)

### T3 — Wire pixel MAR into JSON output
In `update_detection_json`: if `frame != nullptr && frame->format == PIXFORMAT_RGB565`,
call `compute_mouth_pix_mar`. Store result in the `mouth` JSON object as `"mar"`.
Set `open`/`score` from pixel_mar when available; fallback to Phase 1 proxy otherwise.

New JSON per face:
```json
"mouth": { "wr":0.74, "nr":0.28, "mar":0.43, "open":true, "score":0.89 }
```

### T4 — Pass frame from `who_human_face_detection.cpp`
Change: `update_detection_json(detect_results)` → `update_detection_json(detect_results, frame)`

### T5 — Update web UI detection panel
In `index_ov2640.html`: if `f.mouth.mar` is present, show:
`Mouth: OPEN (89%)  [pix-mar: 0.43  wr: 0.74]`
Replace "score from proxy" with clearer labels.

### T6 — Rebuild + smoke test
```bash
. ~/esp/esp-idf/export.sh && idf.py build
```
Flash, open stream, open/close mouth, verify `/detection` JSON changes.

---

## Track 2b — Custom Lip Model (Offline)

### T7 — Download WFLW dataset
```
http://mmlab.ie.cuhk.edu.hk/projects/WFLW.html
Files needed: WFLW_annotations.tar.gz + WFLW_images.tar.gz
Total: ~2.5 GB
```

### T8 — Train the model
```bash
pip install torch torchvision onnx
python .plans/phase2-mar-upgrade/train_lip_model.py \
  --wflw_root /path/to/WFLW \
  --output    .plans/phase2-mar-upgrade/lip_landmark_6pt.onnx \
  --epochs 60
```
Expected accuracy on WFLW test: NME < 5% (normalised by inter-ocular distance).

### T9 — Prepare calibration data
```bash
# Extract ~200 face crops from WFLW test set as calib_faces.npy
python .plans/phase2-mar-upgrade/train_lip_model.py --mode calib \
  --wflw_root /path/to/WFLW --output calib_faces.npy
```

### T10 — INT8 quantize
```bash
cd components/esp-dl/tools/tvm
python esp_quantize_onnx.py \
  --input  ../../../../.plans/phase2-mar-upgrade/lip_landmark_6pt.onnx \
  --output ../../../../.plans/phase2-mar-upgrade/lip_landmark_6pt_int8.onnx \
  --calib  ../../../../.plans/phase2-mar-upgrade/calib_faces.npy \
  --per_channel 0
```

### T11 — Convert to C++ source
```bash
cd components/esp-dl/tools/convert_tool
python convert.py -t esp32s3 \
  -i ../../../../.plans/phase2-mar-upgrade/lip_npy/ \
  -j ../../../../.plans/phase2-mar-upgrade/lip_config.json \
  -n LipLandmark6Pt \
  -o ../../../../components/modules/ai/lip_model/ -q 0
```
Adds `lip_model/LipLandmark6Pt.hpp` and `lip_model/LipLandmark6Pt.cpp`.

### T12 — Write wrapper class
New file: `components/modules/ai/who_lip_landmark.cpp/.hpp`
See spec.md for class interface. The `infer` method:
1. Crops face region from RGB565 using bounding box
2. Resizes to 48×48 grayscale
3. Normalises to [-1, 1]
4. Runs model
5. Maps output coords back to full image space

### T13 — Update CMakeLists.txt
Add `lip_model` sources to `SRC_DIRS` or `SRCS`.
Bump detection task stack from 4KB to 12KB.

### T14 — Integrate into detection pipeline
In `who_human_face_detection.cpp`, instantiate `LipLandmark6Pt` once (static).
After detection, for each face compute `true_mar` and pass to `update_detection_json`.

### T15 — Update JSON + frontend
Add `"true_mar"` field. Update frontend to prefer `true_mar` over `mar` if present.
