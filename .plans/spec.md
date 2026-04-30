# Spec: Rich Facial Landmarks for Mouth-Open Detection

## Goal
Detect when a person is speaking (mouth opening/closing) using the ESP32-S3 camera pipeline and display results on the web UI.

---

## Hard Constraint: esp-dl Has No Lip Model

**Finding (verified 2026-04-30):** The `components/esp-dl` submodule contains exactly three model classes in its Model Zoo:
- `HumanFaceDetectMSR01` / `HumanFaceDetectMNP01` — 5-point landmarks only
- `FaceRecognition112V1S8/S16` — feature vector, no landmarks
- `CatFaceDetectMN03` — cat detection

The 5 existing keypoints are: left eye, right eye, nose, mouth-left-corner, mouth-right-corner.

**There is no multi-point lip or face mesh model in esp-dl.** Getting one requires custom model deployment.

---

## Why 5 Points Are Insufficient for MAR

Mouth Aspect Ratio (MAR) = vertical_gap / horizontal_width.
We have horizontal (corners) but **not vertical** (top lip center, bottom lip center).
Without top/bottom lip center points, we cannot compute true MAR.

What we CAN compute from 5 points as a proxy:
- `mouth_width_normalized` = dist(left_corner, right_corner) / inter_eye_dist
- `nose_to_mouth_ratio` = (y_mouth_avg - y_nose) / face_height
  → When mouth opens, chin drops slightly, increasing this ratio

These are noisy proxies — sufficient for wide-open vs. fully-closed but not subtle states.

---

## Three Options

### Option A — 5-Point Geometric Proxy (Implementable today)
Use the existing 5 landmarks to compute derived mouth metrics and output them in the `/detection` JSON. Display an `open/closed` flag and confidence on the web UI.

| | Detail |
|---|---|
| Effort | ~2 hours (JSON + frontend only) |
| Accuracy | Low-medium (~60–70%) — works for clearly open vs. closed |
| New models needed | None |
| Files changed | `who_ai_utils.cpp`, `index_ov2640.html` |

**Metrics added to `/detection` JSON per face:**
```json
{
  "mouth_width_ratio": 0.82,
  "nose_to_mouth_ratio": 0.55,
  "mouth_open": true,
  "mouth_open_score": 0.71
}
```

Thresholds are calibrated at runtime via a config value (or fixed at a sensible default).

### Option B — Custom Lightweight Lip Model (Future, ~3–5 days)
Deploy a custom 8-point lip model via esp-dl's custom model pipeline:
- 4 outer lip points: left-corner, right-corner, top-center, bottom-center
- 4 inner lip points: top-inner, bottom-inner, inner-left, inner-right

Source model candidates (all have pretrained ONNX weights):
1. **PFLD-lite** (106-point, ~300 KB) — trim to 8 lip points at export
2. **MobileNet-landmark-8pt** — purpose-trained, tiny (~60 KB quantized)
3. **MediaPipe-tiny lip** — too heavy (478 points), not suitable

Pipeline:
1. Crop face to bounding box (already available from detection)
2. Resize to 112×112 (standard alignment input)
3. Run lip landmark model → 8 lip (x,y) pairs in face-relative coords
4. Map back to full-image coords
5. Compute MAR = vertical_gap / horizontal_width

**Espressif's quantization toolchain:** `esp-dl/tools/quantization` (Python, requires ONNX input).

### Option C — Binary Mouth-Open Classifier (Future, ~2 days)
Instead of landmark regression, train a tiny binary classifier:
- Input: 28×28 grayscale crop of the lower face
- Output: open (1) / closed (0) probability
- Architecture: 3-layer CNN, ~15 KB quantized

Simpler than full landmark regression, potentially more robust.

---

## Decision

Start with **Option A** to deliver immediate value (the proxy metrics still confirm the detection pipeline works end-to-end). Document Option B as the upgrade path.

Option B requires offline work (model training/conversion toolchain) that cannot be done inside the ESP-IDF project build.

---

## JSON Output Target (Option A)

```json
{
  "count": 1,
  "faces": [
    {
      "box": [83, 77, 164, 188],
      "lm": {
        "le": [96, 120], "re": [131, 115],
        "n":  [106, 136],
        "ml": [99, 162], "mr": [124, 160]
      },
      "mouth": {
        "width_ratio": 0.82,
        "nose_mouth_ratio": 0.54,
        "open": true,
        "score": 0.71
      }
    }
  ]
}
```

## Web UI Target (Option A)

- Existing detection panel shows face coords (already done)
- Add a "mouth" line: `Mouth: OPEN (71%)` or `Mouth: CLOSED (82%)`
- Color-code: green = open, grey = closed
