# Phase 2 Tests

## Track 2a — Pixel MAR

### ST1 — JSON includes `mar` field
```bash
curl http://<device-ip>/detection
```
When face detected, `mouth` object must contain `"mar"` key with value in [0, 1].

### ST2 — Pixel MAR responds to mouth state
| State | Expected `mar` | Expected `open` |
|---|---|---|
| Closed, neutral | < 0.25 | false |
| Slightly open (2–4 mm) | 0.25–0.40 | false→true border |
| Clearly open | > 0.40 | true |
| Wide open / yawning | > 0.60 | true |

Procedure: hold face at 0.5 m, curl `/detection` in terminal at 2 Hz.

### ST3 — Pixel MAR beats Phase 1 proxy on smiling
Phase 1 failure case: closed-mouth wide smile → `wr` > 0.62 → false OPEN.
With pixel MAR: a wide smile with closed lips should give `mar` < 0.25 (no dark gap).
Accept: pixel_mar < 0.30 for 8/10 closed-smile samples.

### ST4 — Frame format guard
If camera switches to JPEG mode (e.g., high-resolution capture), `mar` field must be
absent from JSON (not `0` — absent, so the frontend falls back to `wr`/`nr`).
Test: set framesize > 5 in web UI, re-read `/detection`, verify no `mar` field crash.

### ST5 — ROI bounds never overflow
With face box touching image edge (e.g., partial face), no crash.
Test: hold face at image edge, run 100 detection cycles, monitor serial log.

### ST6 — Latency budget
Total inference time per frame must not increase by more than 2 ms.
Pixel MAR processes at most ~(mw/3) × (mw*0.75) ≈ 400 pixels at QVGA.
At 240 MHz: < 0.05 ms. This test is trivially satisfied.

### ST7 — Web UI displays updated mouth line
Stream panel shows: `Mouth: OPEN (XX%)  [pix-mar: X.XX  wr: X.XX]`
Must update at detection poll rate (200 ms).

---

## Track 2b — Custom Lip Model

### ST8 — Training convergence
After 60 epochs on WFLW train set: validation NME < 6% (normalised by inter-ocular distance).
(NME = mean Euclidean error / inter-ocular distance)

### ST9 — ONNX export correctness
```python
import onnxruntime as ort
sess = ort.InferenceSession('lip_landmark_6pt.onnx')
dummy = np.zeros((1,1,48,48), dtype=np.float32)
out = sess.run(None, {'input': dummy})
assert out[0].shape == (1, 12)  # 6 points × 2 coords
```

### ST10 — INT8 model NME within 1% of FP32
Quantization should degrade NME by ≤ 1 percentage point.
Run WFLW test set through quantized ONNX, compare.

### ST11 — ESP32-S3 inference < 10ms per face
Add timing instrumentation:
```c
int64_t t0 = esp_timer_get_time();
auto pts = lip_model.infer(...);
int64_t elapsed = esp_timer_get_time() - t0;  // microseconds
```
Accept: elapsed < 10000 μs (10 ms).

### ST12 — True MAR is accurate
With 6-point model:
- Closed mouth: `true_mar` < 0.15
- Open 1 cm: `true_mar` ≈ 0.25–0.40
- Open 2 cm: `true_mar` ≈ 0.40–0.60
Measure with physical ruler for calibration.
