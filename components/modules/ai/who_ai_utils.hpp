#pragma once

#include <list>
#include <vector>
#include "dl_detect_define.hpp"
#include "esp_camera.h"

/**
 * @brief Draw detection result on RGB565 image.
 * 
 * @param image_ptr     image
 * @param image_height  height of image
 * @param image_width   width of image
 * @param results       detection results
 */
void draw_detection_result(uint16_t *image_ptr, int image_height, int image_width, std::list<dl::detect::result_t> &results);

/**
 * @brief Draw detection result on RGB888 image.
 * 
 * @param image_ptr     image
 * @param image_height  height of image
 * @param image_width   width of image
 * @param results       detection results
 */

void draw_detection_result(uint8_t *image_ptr, int image_height, int image_width, std::list<dl::detect::result_t> &results);

/**
 * @brief Print detection result in terminal
 * 
 * @param results detection results
 */
void print_detection_result(std::list<dl::detect::result_t> &results);

/**
 * @brief Decode fb ,
 *        - if fb->format == PIXFORMAT_RGB565, then return fb->buf
 *        - else, then return a new memory with RGB888, don't forget to free it
 *
 * @param fb
 */
void *app_camera_decode(camera_fb_t *fb);

// Pass frame to enable pixel-based mouth MAR (PIXFORMAT_RGB565 only).
// Pass true_mars to include CNN-based true MAR (Phase 2b); one float per face,
// negative values indicate no result. Falls back gracefully when nullptr.
void update_detection_json(std::list<dl::detect::result_t> &results,
                           camera_fb_t *frame = nullptr,
                           const std::vector<float> *true_mars = nullptr);
void get_detection_json(char *buf, size_t len);
