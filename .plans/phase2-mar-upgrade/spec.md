# Phase 2 Spec: True Mouth Aspect Ratio

## Problem Statement

Phase 1 delivers a proxy MAR from 5 horizontal landmarks (mouth corners only).
It has no vertical signal — it cannot measure lip separation.
Phase 2 fixes this with a **real vertical measurement**.

Two independent tracks:

| Track | Method | Effort | Accuracy |
|---|---|---|---|
| **2a — Pixel MAR** | Analyse RGB565 frame pixels in mouth ROI | 2 hours | Medium-high |
| **2b — Custom model** | Train + deploy 6-pt lip landmark CNN | 3–5 days | High |

Track 2a is implemented in this commit. Track 2b is documented here for offline execution.

---

## Track 2a — Pixel-Based Vertical MAR

### Principle

After face detection we have the RGB565 frame buffer (`frame->buf`) and mouth corner positions from the 5-point landmarks. We define a narrow vertical strip in the centre of the mouth region and measure the luminance profile top-to-bottom.

When the mouth is **open**:
- A darker band appears between the lips (oral cavity)
- OR bright teeth appear surrounded by darker lip borders
- Either way: high **vertical contrast** in the strip

When the mouth is **closed**:
- Uniform lip/skin tone → low vertical contrast

**Key metric:** `pixel_mar = f(darkness_ratio, contrast_ratio)` where both are computed relative to the ROI's own mean luminance (lighting-independent).

### ROI Definition

```
mouth corners: ml=(kp[2],kp[3]),  mr=(kp[8],kp[9])
mw = mr_x - ml_x                      # mouth width in pixels

Horizontal strip: centre ±(mw/6) around mouth midpoint
Vertical extent:  mouth_cy ± 0.375*mw  (captures lips + small gap)

Clamp all coords to [0, img_w/img_h)
```

### Luminance Extraction (RGB565)

```c
uint16_t px = buf[y * width + x];
// Green channel dominates luminance (~59% of Rec.601 Y)
uint8_t g = (px >> 5) & 0x3F;   // 6-bit value, 0-63
```

If the camera driver outputs big-endian RGB565, swap bytes:
`uint16_t px = __builtin_bswap16(buf[y * width + x]);`

### Score Computation

```
Per row y in ROI: row_mean = average green value across strip width
mean_all  = mean of all row_mean values
min_row   = minimum row_mean
max_row   = maximum row_mean

darkness  = 1 - min_row / mean_all    # 0=no dark, 1=pitch black row
contrast  = (max_row - min_row) / mean_all

pixel_mar = darkness * 0.55 + contrast * 0.45
mouth_open = pixel_mar > 0.35  (empirical, needs calibration)
```

### Limitations

- Works on RGB565 (PIXFORMAT_RGB565). If frame is JPEG, pixel_mar is skipped (fallback to Phase 1 proxy).
- Sensitive to lip makeup, dark beard, or very bright teeth (can inflate contrast).
- Score is relative to ROI mean so it's lighting-independent for moderate changes; extreme backlight may still cause issues.

---

## Track 2b — Custom 6-Point Lip Landmark Model

### Architecture

```
Input:  48 × 48 × 1  (grayscale face-crop, normalized to [-1, 1])

Conv2D(16, 3×3, stride=1) → BN → ReLU → MaxPool(2×2)   # 24×24×16
Conv2D(32, 3×3, stride=1) → BN → ReLU → MaxPool(2×2)   # 12×12×32
Conv2D(64, 3×3, stride=1) → BN → ReLU → MaxPool(2×2)   #  6× 6×64
Flatten → Dense(128) → ReLU → Dense(12)                 # 6 pts × 2 coords

Parameters: ~350K FP32 → ~87KB INT8
Inference:  ~3–5 ms per face on ESP32-S3 @ 240 MHz
```

### 6 Lip Points

```
pt0: outer left corner      (already from Phase 1: kp[2,3])
pt1: outer right corner     (already from Phase 1: kp[8,9])
pt2: outer top center       (Cupid's bow peak, upper lip center)
pt3: outer bottom center    (lower lip center)
pt4: inner top center       (inner upper lip)
pt5: inner bottom center    (inner lower lip)
```

True MAR = dist(pt4, pt5) / dist(pt0, pt1) = inner_gap / mouth_width

### Training Dataset

**WFLW** (Wider Face Alignment in the Wild):
- 10,000 faces, 98 landmarks per face
- Download: http://mmlab.ie.cuhk.edu.hk/projects/WFLW.html
- WFLW lip indices for our 6 points:
  - pt0 (outer left): index 76
  - pt1 (outer right): index 82
  - pt2 (outer top center): index 87 (or average of 79–87 range)
  - pt3 (outer bottom center): index 91 (or average of 91–95 range)
  - pt4 (inner top center): index 88
  - pt5 (inner bottom center): index 94

See `train_lip_model.py` in this directory for the full training script.

### ESP-DL Conversion Pipeline

```bash
# Step 1: Export trained model to ONNX
# (handled in train_lip_model.py)

# Step 2: INT8 quantization (requires onnxruntime, onnx, numpy)
cd components/esp-dl/tools/tvm
pip install -r requirements.txt
python esp_quantize_onnx.py \
  --input  ../../models/lip_landmark_6pt.onnx \
  --output ../../models/lip_landmark_6pt_int8.onnx \
  --calib  ../../models/calib_faces.npy \
  --per_channel 0

# Step 3: Convert to C++ source (requires platform-specific utils.so in convert_tool)
cd ../convert_tool
python convert.py \
  -t esp32s3 \
  -i ../../models/lip_npy/ \
  -j ../../models/lip_config.json \
  -n LipLandmark6Pt \
  -o ../../../../components/modules/ai/lip_model/ \
  -q 0
```

Step 3 generates `LipLandmark6Pt.hpp` + `LipLandmark6Pt.cpp` with the model weights as C arrays.

### Integration Wrapper (stub — needs model files first)

```cpp
// who_lip_landmark.hpp
#pragma once
#include "dl_detect_define.hpp"
#include "esp_camera.h"
#include <vector>

class LipLandmark6Pt {
    void *model;
public:
    LipLandmark6Pt();
    ~LipLandmark6Pt();
    // Returns {pt0..pt5} as 12 ints [x0,y0,...,x5,y5] in full-image coords
    std::vector<int> infer(const uint16_t *rgb565, int img_h, int img_w,
                           const dl::detect::result_t &face);
};
```

In `who_human_face_detection.cpp`, after detection:
```cpp
LipLandmark6Pt lip_model;  // static or singleton
for (auto &r : detect_results) {
    auto pts = lip_model.infer((uint16_t*)frame->buf, frame->height, frame->width, r);
    // pts[4,5]=inner_top, pts[10,11]=inner_bottom
    float inner_gap = hypot(pts[4]-pts[10], pts[5]-pts[11]);
    float mouth_w   = hypot(pts[0]-pts[2], pts[1]-pts[3]);
    float true_mar  = (mouth_w > 0) ? inner_gap / mouth_w : 0;
    // store in parallel structure for JSON
}
```

### Memory Budget

- INT8 model weights in flash: ~87KB (comfortable, factory partition = 3840KB)
- PSRAM for model inference scratch: ~64KB per face
- Stack for inference: 8KB additional — increase `xTaskCreatePinnedToCore` stack from 4KB to 12KB
