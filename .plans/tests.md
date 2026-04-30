# Tests: Mouth-Open Detection

## Phase 1 Success Criteria

### ST1 — JSON structure
`curl http://<device-ip>/detection` while face visible returns:
```json
{
  "count": 1,
  "faces": [{
    "box": [...],
    "lm": { "le": [...], "re": [...], "n": [...], "ml": [...], "mr": [...] },
    "mouth": {
      "width_ratio": <float 0.5–1.5>,
      "nose_mouth_ratio": <float 0.3–0.8>,
      "open": <bool>,
      "score": <float 0.0–1.0>
    }
  }]
}
```
Acceptance: valid JSON, no missing fields, `open` is bool, `score` is 0.0–1.0.

### ST2 — Buffer safety
JSON for 3 simultaneous faces fits within 512-byte buffer.
Check: each face adds at most `"mouth":{"width_ratio":1.23,"nose_mouth_ratio":0.54,"open":true,"score":0.71},` ≈ 80 chars.
3 faces × (120 base + 80 mouth) = 600 chars → **EXCEEDS 512**. 

Action: increase `s_det_json` buffer in `who_ai_utils.cpp` to 1024 bytes when adding mouth fields.

### ST3 — Correct metric values
Manual calibration test:
| State | Expected `open` | Expected `score` |
|---|---|---|
| Mouth closed at rest | false | < 0.40 |
| Mouth slightly open | true | 0.45–0.65 |
| Wide open (yawn) | true | > 0.70 |

Procedure: trigger each state in front of camera, read `/detection` JSON via curl.

### ST4 — No false positives at rest
Sit still with mouth closed for 30 seconds, monitor `/detection`.
Accept: `open: false` in ≥ 85% of frames.

### ST5 — Latency unchanged
Measure frame rate before and after adding mouth computation.
Accept: ≤ 1 fps reduction (computation is pure arithmetic, < 0.1 ms per face).

### ST6 — Web UI display
Open `http://<device-ip>/` in browser, start stream, check detection panel.
Accept: "Mouth: OPEN (XX%)" or "Mouth: CLOSED (XX%)" visible per face, color changes between states.

---

## Phase 2 Success Criteria (future)

### ST7 — True MAR accuracy
With 8-point lip model, MAR threshold should give ≥ 90% accuracy on a 20-sample test set (10 open, 10 closed).

### ST8 — Lip model latency budget
8-point lip inference on a 112×112 crop must complete in < 30 ms per face on ESP32-S3 at 240 MHz.
(This leaves ~37 ms budget for a 27 fps pipeline with 2 faces.)

### ST9 — Canvas overlay correctness
8 lip dots rendered on canvas align with visible lip position (within ±5 pixels at QVGA resolution).

---

## Known Limitations of Phase 1 Proxy

1. **Weak vertical signal**: mouth corners drop slightly when opening but the main change (lip separation) is invisible with 5 points. The proxy works best for clearly open vs. clearly closed.
2. **Pose sensitivity**: metrics are ratio-based so they are normalised for scale, but strong yaw/pitch head rotation will distort them.
3. **Lighting**: the detection bounding box changes with lighting, affecting `face_height` normalisation slightly.
4. **Smiling**: a wide smile increases `mouth_width_ratio` and may trigger `open: true` even with lips closed. No fix without more landmark points.
