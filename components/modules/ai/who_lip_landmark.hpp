#pragma once

#include "esp_camera.h"
#include "dl_detect_define.hpp"
#include <vector>

/**
 * 6-point lip landmark regressor (Phase 2b).
 *
 * Crops the mouth ROI from an RGB565 frame, bilinear-resizes to 48×48
 * grayscale, and (when LIP_MODEL_AVAILABLE is defined) runs the quantised
 * INT8 CNN to predict 6 lip landmark coordinates, returning true MAR:
 *
 *   true_mar = |inner_top_y − inner_bottom_y| / |right_x − left_x|
 *
 * Without LIP_MODEL_AVAILABLE the infer() stub returns -1.0f so callers
 * fall back to the Phase-2a pixel MAR without any code change.
 *
 * Point ordering matches train_lip_model.py / WFLW_LIP_IDX:
 *   out[0,1]  = left outer corner
 *   out[2,3]  = right outer corner
 *   out[4,5]  = top outer center  (Cupid's bow / philtrum peak)
 *   out[6,7]  = bottom outer center
 *   out[8,9]  = top inner center  (upper lip, inner edge)
 *   out[10,11]= bottom inner center (lower lip, inner edge)
 */
class LipLandmark6Pt {
public:
    LipLandmark6Pt();
    ~LipLandmark6Pt();

    /** True when model weights are loaded and ready for inference. */
    bool is_loaded() const { return m_loaded; }

    /**
     * Infer true MAR for one face from an RGB565 frame buffer.
     * @param frame  camera frame (must be PIXFORMAT_RGB565)
     * @param face   one detection result with at least 10 keypoints
     * @return true_mar in [0,1], or -1.0f on failure / model not available
     */
    float infer(const camera_fb_t *frame, const dl::detect::result_t &face);

private:
    bool     m_loaded;
    uint8_t *m_crop_buf;  /* PSRAM-backed 48×48 scratch buffer */
};
