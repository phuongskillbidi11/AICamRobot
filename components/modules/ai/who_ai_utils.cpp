#include "who_ai_utils.hpp"

#include "esp_log.h"
#include "esp_camera.h"

#include "dl_image.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static char s_det_json[2048] = "{\"count\":0,\"faces\":[]}";
static SemaphoreHandle_t s_det_mutex = NULL;

static const char *TAG = "ai_utils";

// +-------+--------------------+----------+
// |       |       RGB565       |  RGB888  |
// +=======+====================+==========+
// |  Red  | 0b0000000011111000 | 0x0000FF |
// +-------+--------------------+----------+
// | Green | 0b1110000000000111 | 0x00FF00 |
// +-------+--------------------+----------+
// |  Blue | 0b0001111100000000 | 0xFF0000 |
// +-------+--------------------+----------+

void draw_detection_result(uint16_t *image_ptr, int image_height, int image_width, std::list<dl::detect::result_t> &results)
{
    int i = 0;
    for (std::list<dl::detect::result_t>::iterator prediction = results.begin(); prediction != results.end(); prediction++, i++)
    {
        dl::image::draw_hollow_rectangle(image_ptr, image_height, image_width,
                                         DL_MAX(prediction->box[0], 0),
                                         DL_MAX(prediction->box[1], 0),
                                         DL_MAX(prediction->box[2], 0),
                                         DL_MAX(prediction->box[3], 0),
                                         0b1110000000000111);

        if (prediction->keypoint.size() == 10)
        {
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[0], 0), DL_MAX(prediction->keypoint[1], 0), 4, 0b0000000011111000); // left eye
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[2], 0), DL_MAX(prediction->keypoint[3], 0), 4, 0b0000000011111000); // mouth left corner
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[4], 0), DL_MAX(prediction->keypoint[5], 0), 4, 0b1110000000000111); // nose
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[6], 0), DL_MAX(prediction->keypoint[7], 0), 4, 0b0001111100000000); // right eye
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[8], 0), DL_MAX(prediction->keypoint[9], 0), 4, 0b0001111100000000); // mouth right corner
        }
    }
}

void draw_detection_result(uint8_t *image_ptr, int image_height, int image_width, std::list<dl::detect::result_t> &results)
{
    int i = 0;
    for (std::list<dl::detect::result_t>::iterator prediction = results.begin(); prediction != results.end(); prediction++, i++)
    {
        dl::image::draw_hollow_rectangle(image_ptr, image_height, image_width,
                                         DL_MAX(prediction->box[0], 0),
                                         DL_MAX(prediction->box[1], 0),
                                         DL_MAX(prediction->box[2], 0),
                                         DL_MAX(prediction->box[3], 0),
                                         0x00FF00);

        if (prediction->keypoint.size() == 10)
        {
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[0], 0), DL_MAX(prediction->keypoint[1], 0), 4, 0x0000FF); // left eye
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[2], 0), DL_MAX(prediction->keypoint[3], 0), 4, 0x0000FF); // mouth left corner
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[4], 0), DL_MAX(prediction->keypoint[5], 0), 4, 0x00FF00); // nose
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[6], 0), DL_MAX(prediction->keypoint[7], 0), 4, 0xFF0000); // right eye
            dl::image::draw_point(image_ptr, image_height, image_width, DL_MAX(prediction->keypoint[8], 0), DL_MAX(prediction->keypoint[9], 0), 4, 0xFF0000); // mouth right corner
        }
    }
}

void print_detection_result(std::list<dl::detect::result_t> &results)
{
    int i = 0;
    for (std::list<dl::detect::result_t>::iterator prediction = results.begin(); prediction != results.end(); prediction++, i++)
    {
        ESP_LOGI("detection_result", "[%2d]: (%3d, %3d, %3d, %3d)", i, prediction->box[0], prediction->box[1], prediction->box[2], prediction->box[3]);

        if (prediction->keypoint.size() == 10)
        {
            ESP_LOGI("detection_result", "      left eye: (%3d, %3d), right eye: (%3d, %3d), nose: (%3d, %3d), mouth left: (%3d, %3d), mouth right: (%3d, %3d)",
                     prediction->keypoint[0], prediction->keypoint[1],  // left eye
                     prediction->keypoint[6], prediction->keypoint[7],  // right eye
                     prediction->keypoint[4], prediction->keypoint[5],  // nose
                     prediction->keypoint[2], prediction->keypoint[3],  // mouth left corner
                     prediction->keypoint[8], prediction->keypoint[9]); // mouth right corner
        }
    }
}

void *app_camera_decode(camera_fb_t *fb)
{
    if (fb->format == PIXFORMAT_RGB565)
    {
        return (void *)fb->buf;
    }
    else
    {
        uint8_t *image_ptr = (uint8_t *)malloc(fb->height * fb->width * 3 * sizeof(uint8_t));
        if (image_ptr)
        {
            if (fmt2rgb888(fb->buf, fb->len, fb->format, image_ptr))
            {
                return (void *)image_ptr;
            }
            else
            {
                ESP_LOGE(TAG, "fmt2rgb888 failed");
                dl::tool::free_aligned(image_ptr);
            }
        }
        else
        {
            ESP_LOGE(TAG, "malloc memory for image rgb888 failed");
        }
    }
    return NULL;
}

/* Phase 1 proxy thresholds (QVGA/CIF, ~50 cm, indoor lighting). */
#define MOUTH_WR_OPEN  0.62f
#define MOUTH_NR_OPEN  0.17f

/* Phase 2a pixel-MAR threshold. */
#define PIX_MAR_OPEN   0.5f

/* Phase 2b true-MAR threshold (CNN 6-point output). */
#define TRUE_MAR_OPEN  0.20f

/* Compute pixel-based mouth openness from the RGB565 frame buffer.
   Returns a score in [0,1]: high = open, low = closed.
   Uses a narrow vertical strip through the mouth centre and measures
   darkness + contrast relative to the strip's own mean luminance. */
static float compute_mouth_pix_mar(const uint16_t *buf, int img_h, int img_w,
                                   const dl::detect::result_t &r)
{
    if (r.keypoint.size() < 10) return -1.0f;

    int ml_x = r.keypoint[2], ml_y = r.keypoint[3];
    int mr_x = r.keypoint[8], mr_y = r.keypoint[9];
    int mw   = mr_x - ml_x;
    if (mw < 6) return -1.0f;

    /* Narrow vertical strip: centre ±(mw/6) horizontally */
    int cx   = (ml_x + mr_x) / 2;
    int cy   = (ml_y + mr_y) / 2;
    int half_w = mw / 6;
    if (half_w < 1) half_w = 1;
    int x0 = cx - half_w;
    int x1 = cx + half_w;

    /* Vertical extent: cy ± 0.375*mw */
    int half_h = mw * 3 / 8;
    if (half_h < 3) half_h = 3;
    int y0 = cy - half_h;
    int y1 = cy + half_h;

    /* Clamp to image bounds */
    if (x0 < 0) x0 = 0;
    if (x1 >= img_w) x1 = img_w - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= img_h) y1 = img_h - 1;

    int sw     = x1 - x0 + 1;
    int n_rows = y1 - y0 + 1;
    if (sw < 1 || n_rows < 4) return -1.0f;

    /* Per-row mean luminance using the green channel of RGB565.
       Green (6 bits, range 0-63) dominates luminance (~59% Rec.601).
       If the camera outputs big-endian RGB565, swap bytes first:
         uint16_t px = __builtin_bswap16(buf[y*img_w + x]);        */
    uint32_t sum_all = 0;
    uint32_t min_row = 63, max_row = 0;

    for (int y = y0; y <= y1; y++)
    {
        uint32_t row_sum = 0;
        for (int x = x0; x <= x1; x++)
        {
            uint16_t px = buf[y * img_w + x];
            row_sum += (px >> 5) & 0x3F;   /* green channel */
        }
        uint32_t row_mean = row_sum / (uint32_t)sw;
        sum_all += row_mean;
        if (row_mean < min_row) min_row = row_mean;
        if (row_mean > max_row) max_row = row_mean;
    }

    uint32_t mean_all = sum_all / (uint32_t)n_rows;
    if (mean_all < 1) mean_all = 1;

    /* darkness: 0 = no dark row, 1 = pitch-black row relative to mean */
    float darkness  = 1.0f - (float)min_row / (float)mean_all;
    /* contrast: 0 = uniform, >1 = high contrast (teeth + dark gap) */
    float contrast  = (float)(max_row - min_row) / (float)mean_all;

    float mar = darkness * 0.55f + contrast * 0.45f;
    if (mar < 0.0f) mar = 0.0f;
    if (mar > 1.0f) mar = 1.0f;
    return mar;
}

void update_detection_json(std::list<dl::detect::result_t> &results,
                           camera_fb_t *frame,
                           const std::vector<float> *true_mars)
{
    if (!s_det_mutex)
        s_det_mutex = xSemaphoreCreateMutex();

    const bool use_pixels = (frame != nullptr &&
                              frame->format == PIXFORMAT_RGB565 &&
                              frame->buf != nullptr);

    char tmp[2048];
    char *p = tmp;
    p += sprintf(p, "{\"count\":%d,\"faces\":[", (int)results.size());
    bool first = true;
    int face_idx = 0;
    for (auto &r : results)
    {
        if (!first) *p++ = ',';
        first = false;
        p += sprintf(p, "{\"box\":[%d,%d,%d,%d]", r.box[0], r.box[1], r.box[2], r.box[3]);
        if (r.keypoint.size() == 10)
        {
            p += sprintf(p, ",\"lm\":{\"le\":[%d,%d],\"re\":[%d,%d],\"n\":[%d,%d],\"ml\":[%d,%d],\"mr\":[%d,%d]}",
                         r.keypoint[0], r.keypoint[1],
                         r.keypoint[6], r.keypoint[7],
                         r.keypoint[4], r.keypoint[5],
                         r.keypoint[2], r.keypoint[3],
                         r.keypoint[8], r.keypoint[9]);

            /* Phase 1 proxy: landmark-geometry metrics (always computed) */
            float edx = (float)(r.keypoint[6] - r.keypoint[0]);
            float edy = (float)(r.keypoint[7] - r.keypoint[1]);
            float eye_dist = sqrtf(edx * edx + edy * edy);
            if (eye_dist < 1.0f) eye_dist = 1.0f;

            float mdx = (float)(r.keypoint[8] - r.keypoint[2]);
            float mdy = (float)(r.keypoint[9] - r.keypoint[3]);
            float wr  = sqrtf(mdx * mdx + mdy * mdy) / eye_dist;

            int mouth_cy = (r.keypoint[3] + r.keypoint[9]) / 2;
            int face_h   = r.box[3] - r.box[1];
            float nr     = (face_h > 0) ? (float)(mouth_cy - r.keypoint[5]) / face_h : 0.0f;

            /* Phase 2b: CNN true MAR (most reliable, when model loaded) */
            float true_mar = -1.0f;
            if (true_mars && face_idx < (int)true_mars->size())
                true_mar = (*true_mars)[face_idx];

            /* Phase 2a: pixel-based MAR */
            float pix_mar = -1.0f;
            if (use_pixels)
                pix_mar = compute_mouth_pix_mar(
                    (const uint16_t *)frame->buf,
                    (int)frame->height, (int)frame->width, r);

            /* Priority: true_mar > pix_mar > proxy */
            bool  mouth_open;
            float score;
            if (true_mar >= 0.0f)
            {
                mouth_open = (true_mar > TRUE_MAR_OPEN);
                score      = true_mar;
                if (pix_mar >= 0.0f)
                    p += sprintf(p, ",\"mouth\":{\"wr\":%.2f,\"nr\":%.2f,\"mar\":%.2f,\"true_mar\":%.2f,\"open\":%s,\"score\":%.2f}",
                                 wr, nr, pix_mar, true_mar, mouth_open ? "true" : "false", score);
                else
                    p += sprintf(p, ",\"mouth\":{\"wr\":%.2f,\"nr\":%.2f,\"true_mar\":%.2f,\"open\":%s,\"score\":%.2f}",
                                 wr, nr, true_mar, mouth_open ? "true" : "false", score);
            }
            else if (pix_mar >= 0.0f)
            {
                mouth_open = (pix_mar > PIX_MAR_OPEN);
                score      = pix_mar;
                p += sprintf(p, ",\"mouth\":{\"wr\":%.2f,\"nr\":%.2f,\"mar\":%.2f,\"open\":%s,\"score\":%.2f}",
                             wr, nr, pix_mar, mouth_open ? "true" : "false", score);
            }
            else
            {
                /* fallback: Phase 1 geometry proxy */
                float w_sc = (wr - (MOUTH_WR_OPEN - 0.15f)) / 0.30f;
                float y_sc = (nr - (MOUTH_NR_OPEN - 0.06f)) / 0.12f;
                if (w_sc < 0.0f) w_sc = 0.0f;
                if (w_sc > 1.0f) w_sc = 1.0f;
                if (y_sc < 0.0f) y_sc = 0.0f;
                if (y_sc > 1.0f) y_sc = 1.0f;
                score      = w_sc * 0.45f + y_sc * 0.55f;
                mouth_open = (wr > MOUTH_WR_OPEN) || (nr > MOUTH_NR_OPEN && score > 0.45f);
                p += sprintf(p, ",\"mouth\":{\"wr\":%.2f,\"nr\":%.2f,\"open\":%s,\"score\":%.2f}",
                             wr, nr, mouth_open ? "true" : "false", score);
            }
        }
        *p++ = '}';
        face_idx++;
    }
    sprintf(p, "]}");

    if (xSemaphoreTake(s_det_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        strlcpy(s_det_json, tmp, sizeof(s_det_json));
        xSemaphoreGive(s_det_mutex);
    }
}

void get_detection_json(char *buf, size_t len)
{
    if (!s_det_mutex || xSemaphoreTake(s_det_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        strlcpy(buf, "{\"count\":0,\"faces\":[]}", len);
        return;
    }
    strlcpy(buf, s_det_json, len);
    xSemaphoreGive(s_det_mutex);
}