// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "app_httpd.hpp"

#include <list>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "app_mdns.h"
#include "sdkconfig.h"

#include "who_camera.h"
#include "who_ai_utils.hpp"

#include "esp_heap_caps.h"
#include "esp_chip_info.h"
/* esp_flash.h removed: flash size is a compile-time constant from Kconfig. */
#include "driver/temperature_sensor.h"
#include "esp_ota_ops.h"

static temperature_sensor_handle_t s_temp_sensor = NULL;

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
static const char *TAG = "camera_httpd";
#endif

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static bool gReturnFB = true;

static int8_t detection_enabled = 0;
static int8_t recognition_enabled = 0;
static int8_t is_enrolling = 0;

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index)
    {
        j->len = 0;
    }
    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
    {
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *frame = NULL;
    esp_err_t res = ESP_OK;

    if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY))
    {
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

        char ts[32];
        snprintf(ts, 32, "%lld.%06ld", frame->timestamp.tv_sec, frame->timestamp.tv_usec);
        httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

        // size_t fb_len = 0;
        if (frame->format == PIXFORMAT_JPEG)
        {
                // fb_len = frame->len;
            res = httpd_resp_send(req, (const char *)frame->buf, frame->len);
        }
        else
        {
            jpg_chunking_t jchunk = {req, 0};
            res = frame2jpg_cb(frame, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
            httpd_resp_send_chunk(req, NULL, 0);
            // fb_len = jchunk.len;
        }

        if (xQueueFrameO)
        {
            xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
        }
        else if (gReturnFB)
        {
            esp_camera_fb_return(frame);
        }
        else
        {
            free(frame);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *frame = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    while (true)
    {
        if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY))
        {
            _timestamp.tv_sec = frame->timestamp.tv_sec;
            _timestamp.tv_usec = frame->timestamp.tv_usec;

            if (frame->format == PIXFORMAT_JPEG)
            {
                _jpg_buf = frame->buf;
                _jpg_buf_len = frame->len;
            }
            else if (!frame2jpg(frame, 80, &_jpg_buf, &_jpg_buf_len))
            {
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
            }
        }
        else
        {
            res = ESP_FAIL;
        }

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }

        if (frame->format != PIXFORMAT_JPEG)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }

        if (xQueueFrameO)
        {
            xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
        }
        else if (gReturnFB)
        {
            esp_camera_fb_return(frame);
        }
        else
        {
            free(frame);
        }

        if (res != ESP_OK)
        {
            break;
        }
    }

    return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf)
{
    char *buf = NULL;
    size_t buf_len = 0;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            *obuf = buf;
            return ESP_OK;
        }
        free(buf);
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char variable[32];
    char value[32];

    if (parse_get(req, &buf) != ESP_OK ||
        httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK ||
        httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK)
    {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int val = atoi(value);
    ESP_LOGI(TAG, "%s = %d", variable, val);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if (!strcmp(variable, "framesize"))
    {
        if (s->pixformat == PIXFORMAT_JPEG)
        {
            res = s->set_framesize(s, (framesize_t)val);
            if (res == 0)
            {
                app_mdns_update_framesize(val);
            }
        }
    }
    else if (!strcmp(variable, "quality"))
        res = s->set_quality(s, val);
    else if (!strcmp(variable, "contrast"))
        res = s->set_contrast(s, val);
    else if (!strcmp(variable, "brightness"))
        res = s->set_brightness(s, val);
    else if (!strcmp(variable, "saturation"))
        res = s->set_saturation(s, val);
    else if (!strcmp(variable, "gainceiling"))
        res = s->set_gainceiling(s, (gainceiling_t)val);
    else if (!strcmp(variable, "colorbar"))
        res = s->set_colorbar(s, val);
    else if (!strcmp(variable, "awb"))
        res = s->set_whitebal(s, val);
    else if (!strcmp(variable, "agc"))
        res = s->set_gain_ctrl(s, val);
    else if (!strcmp(variable, "aec"))
        res = s->set_exposure_ctrl(s, val);
    else if (!strcmp(variable, "hmirror"))
        res = s->set_hmirror(s, val);
    else if (!strcmp(variable, "vflip"))
        res = s->set_vflip(s, val);
    else if (!strcmp(variable, "awb_gain"))
        res = s->set_awb_gain(s, val);
    else if (!strcmp(variable, "agc_gain"))
        res = s->set_agc_gain(s, val);
    else if (!strcmp(variable, "aec_value"))
        res = s->set_aec_value(s, val);
    else if (!strcmp(variable, "aec2"))
        res = s->set_aec2(s, val);
    else if (!strcmp(variable, "dcw"))
        res = s->set_dcw(s, val);
    else if (!strcmp(variable, "bpc"))
        res = s->set_bpc(s, val);
    else if (!strcmp(variable, "wpc"))
        res = s->set_wpc(s, val);
    else if (!strcmp(variable, "raw_gma"))
        res = s->set_raw_gma(s, val);
    else if (!strcmp(variable, "lenc"))
        res = s->set_lenc(s, val);
    else if (!strcmp(variable, "special_effect"))
        res = s->set_special_effect(s, val);
    else if (!strcmp(variable, "wb_mode"))
        res = s->set_wb_mode(s, val);
    else if (!strcmp(variable, "ae_level"))
        res = s->set_ae_level(s, val);
#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    else if (!strcmp(variable, "led_intensity"))
        led_duty = val;
#endif

    else if (!strcmp(variable, "face_detect"))
    {
        detection_enabled = val;
        if (!detection_enabled)
        {
            recognition_enabled = 0;
        }
    }
    else if (!strcmp(variable, "face_enroll"))
        is_enrolling = val;
    else if (!strcmp(variable, "face_recognize"))
    {
        recognition_enabled = val;
        if (recognition_enabled)
        {
            detection_enabled = val;
        }
    }
    else
    {
        res = -1;
    }

    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static int print_reg(char *p, sensor_t *s, uint16_t reg, uint32_t mask)
{
    return sprintf(p, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static char json_response[1024];

    sensor_t *s = esp_camera_sensor_get();
    char *p = json_response;
    *p++ = '{';

    if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID)
    {
        for (int reg = 0x3400; reg < 0x3406; reg += 2)
        {
            p += print_reg(p, s, reg, 0xFFF); //12 bit
        }
        p += print_reg(p, s, 0x3406, 0xFF);

        p += print_reg(p, s, 0x3500, 0xFFFF0); //16 bit
        p += print_reg(p, s, 0x3503, 0xFF);
        p += print_reg(p, s, 0x350a, 0x3FF);  //10 bit
        p += print_reg(p, s, 0x350c, 0xFFFF); //16 bit

        for (int reg = 0x5480; reg <= 0x5490; reg++)
        {
            p += print_reg(p, s, reg, 0xFF);
        }

        for (int reg = 0x5380; reg <= 0x538b; reg++)
        {
            p += print_reg(p, s, reg, 0xFF);
        }

        for (int reg = 0x5580; reg < 0x558a; reg++)
        {
            p += print_reg(p, s, reg, 0xFF);
        }
        p += print_reg(p, s, 0x558a, 0x1FF); //9 bit
    }
    else
    {
        p += print_reg(p, s, 0xd3, 0xFF);
        p += print_reg(p, s, 0x111, 0xFF);
        p += print_reg(p, s, 0x132, 0xFF);
    }

    p += sprintf(p, "\"board\":\"%s\",", CAMERA_MODULE_NAME);
    p += sprintf(p, "\"xclk\":%u,", s->xclk_freq_hz / 1000000);
    p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
    p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p += sprintf(p, "\"quality\":%u,", s->status.quality);
    p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
    p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p += sprintf(p, "\"awb\":%u,", s->status.awb);
    p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p += sprintf(p, "\"aec\":%u,", s->status.aec);
    p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p += sprintf(p, "\"agc\":%u,", s->status.agc);
    p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    p += sprintf(p, ",\"led_intensity\":%u", led_duty);
#else
    p += sprintf(p, ",\"led_intensity\":%d", -1);
#endif
    p += sprintf(p, ",\"face_detect\":%u", detection_enabled);
    p += sprintf(p, ",\"face_enroll\":%u,", is_enrolling);
    p += sprintf(p, "\"face_recognize\":%u", recognition_enabled);
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t mdns_handler(httpd_req_t *req)
{
    size_t json_len = 0;
    const char *json_response = app_mdns_query(&json_len);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, json_len);
}

static esp_err_t xclk_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char _xclk[32];

    if (parse_get(req, &buf) != ESP_OK ||
        httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK)
    {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int xclk = atoi(_xclk);
    ESP_LOGI(TAG, "Set XCLK: %d MHz", xclk);

    sensor_t *s = esp_camera_sensor_get();
    int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t reg_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char _reg[32];
    char _mask[32];
    char _val[32];

    if (parse_get(req, &buf) != ESP_OK ||
        httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK ||
        httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK ||
        httpd_query_key_value(buf, "val", _val, sizeof(_val)) != ESP_OK)
    {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int reg = atoi(_reg);
    int mask = atoi(_mask);
    int val = atoi(_val);
    ESP_LOGI(TAG, "Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);

    sensor_t *s = esp_camera_sensor_get();
    int res = s->set_reg(s, reg, mask, val);
    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t greg_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char _reg[32];
    char _mask[32];

    if (parse_get(req, &buf) != ESP_OK ||
        httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK ||
        httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK)
    {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int reg = atoi(_reg);
    int mask = atoi(_mask);
    sensor_t *s = esp_camera_sensor_get();
    int res = s->get_reg(s, reg, mask);
    if (res < 0)
    {
        return httpd_resp_send_500(req);
    }
    ESP_LOGI(TAG, "Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, res);

    char buffer[20];
    const char *val = itoa(res, buffer, 10);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, val, strlen(val));
}

static int parse_get_var(char *buf, const char *key, int def)
{
    char _int[16];
    if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK)
    {
        return def;
    }
    return atoi(_int);
}

static esp_err_t pll_handler(httpd_req_t *req)
{
    char *buf = NULL;

    if (parse_get(req, &buf) != ESP_OK)
    {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int bypass = parse_get_var(buf, "bypass", 0);
    int mul = parse_get_var(buf, "mul", 0);
    int sys = parse_get_var(buf, "sys", 0);
    int root = parse_get_var(buf, "root", 0);
    int pre = parse_get_var(buf, "pre", 0);
    int seld5 = parse_get_var(buf, "seld5", 0);
    int pclken = parse_get_var(buf, "pclken", 0);
    int pclk = parse_get_var(buf, "pclk", 0);
    free(buf);

    ESP_LOGI(TAG, "Set Pll: bypass: %d, mul: %d, sys: %d, root: %d, pre: %d, seld5: %d, pclken: %d, pclk: %d", bypass, mul, sys, root, pre, seld5, pclken, pclk);
    sensor_t *s = esp_camera_sensor_get();
    int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t win_handler(httpd_req_t *req)
{
    char *buf = NULL;

    if (parse_get(req, &buf) != ESP_OK)
    {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int startX = parse_get_var(buf, "sx", 0);
    int startY = parse_get_var(buf, "sy", 0);
    int endX = parse_get_var(buf, "ex", 0);
    int endY = parse_get_var(buf, "ey", 0);
    int offsetX = parse_get_var(buf, "offx", 0);
    int offsetY = parse_get_var(buf, "offy", 0);
    int totalX = parse_get_var(buf, "tx", 0);
    int totalY = parse_get_var(buf, "ty", 0);
    int outputX = parse_get_var(buf, "ox", 0);
    int outputY = parse_get_var(buf, "oy", 0);
    bool scale = parse_get_var(buf, "scale", 0) == 1;
    bool binning = parse_get_var(buf, "binning", 0) == 1;
    free(buf);

    ESP_LOGI(TAG, "Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);
    sensor_t *s = esp_camera_sensor_get();
    int res = s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);
    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t chartjs_handler(httpd_req_t *req)
{
    extern const unsigned char chart_umd_js_gz_start[] asm("_binary_chart_umd_js_gz_start");
    extern const unsigned char chart_umd_js_gz_end[]   asm("_binary_chart_umd_js_gz_end");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)chart_umd_js_gz_start,
                           chart_umd_js_gz_end - chart_umd_js_gz_start);
}

static esp_err_t bootstrap_css_handler(httpd_req_t *req)
{
    extern const unsigned char bootstrap_min_css_gz_start[] asm("_binary_bootstrap_min_css_gz_start");
    extern const unsigned char bootstrap_min_css_gz_end[]   asm("_binary_bootstrap_min_css_gz_end");
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)bootstrap_min_css_gz_start,
                           bootstrap_min_css_gz_end - bootstrap_min_css_gz_start);
}

static esp_err_t bootstrap_js_handler(httpd_req_t *req)
{
    extern const unsigned char bootstrap_bundle_min_js_gz_start[] asm("_binary_bootstrap_bundle_min_js_gz_start");
    extern const unsigned char bootstrap_bundle_min_js_gz_end[]   asm("_binary_bootstrap_bundle_min_js_gz_end");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)bootstrap_bundle_min_js_gz_start,
                           bootstrap_bundle_min_js_gz_end - bootstrap_bundle_min_js_gz_start);
}

static esp_err_t bootstrap_icons_css_handler(httpd_req_t *req)
{
    extern const unsigned char bootstrap_icons_min_css_gz_start[] asm("_binary_bootstrap_icons_min_css_gz_start");
    extern const unsigned char bootstrap_icons_min_css_gz_end[]   asm("_binary_bootstrap_icons_min_css_gz_end");
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)bootstrap_icons_min_css_gz_start,
                           bootstrap_icons_min_css_gz_end - bootstrap_icons_min_css_gz_start);
}

static esp_err_t bootstrap_icons_font_handler(httpd_req_t *req)
{
    extern const unsigned char bootstrap_icons_woff2_start[] asm("_binary_bootstrap_icons_woff2_start");
    extern const unsigned char bootstrap_icons_woff2_end[]   asm("_binary_bootstrap_icons_woff2_end");
    httpd_resp_set_type(req, "font/woff2");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)bootstrap_icons_woff2_start,
                           bootstrap_icons_woff2_end - bootstrap_icons_woff2_start);
}

static esp_err_t index_handler(httpd_req_t *req)
{
    extern const unsigned char index_ov2640_html_gz_start[] asm("_binary_index_ov2640_html_gz_start");
    extern const unsigned char index_ov2640_html_gz_end[] asm("_binary_index_ov2640_html_gz_end");
    size_t index_ov2640_html_gz_len = index_ov2640_html_gz_end - index_ov2640_html_gz_start;

    extern const unsigned char index_ov3660_html_gz_start[] asm("_binary_index_ov3660_html_gz_start");
    extern const unsigned char index_ov3660_html_gz_end[] asm("_binary_index_ov3660_html_gz_end");
    size_t index_ov3660_html_gz_len = index_ov3660_html_gz_end - index_ov3660_html_gz_start;

    extern const unsigned char index_ov5640_html_gz_start[] asm("_binary_index_ov5640_html_gz_start");
    extern const unsigned char index_ov5640_html_gz_end[] asm("_binary_index_ov5640_html_gz_end");
    size_t index_ov5640_html_gz_len = index_ov5640_html_gz_end - index_ov5640_html_gz_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL)
    {
        if (s->id.PID == OV3660_PID)
        {
            return httpd_resp_send(req, (const char *)index_ov3660_html_gz_start, index_ov3660_html_gz_len);
        }
        else if (s->id.PID == OV5640_PID)
        {
            return httpd_resp_send(req, (const char *)index_ov5640_html_gz_start, index_ov5640_html_gz_len);
        }
        else
        {
            return httpd_resp_send(req, (const char *)index_ov2640_html_gz_start, index_ov2640_html_gz_len);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Camera sensor not found");
        return httpd_resp_send_500(req);
    }
}

static esp_err_t monitor_handler(httpd_req_t *req)
{
    extern const unsigned char monitor_html_gz_start[] asm("_binary_monitor_html_gz_start");
    extern const unsigned char monitor_html_gz_end[] asm("_binary_monitor_html_gz_end");
    size_t monitor_html_gz_len = monitor_html_gz_end - monitor_html_gz_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)monitor_html_gz_start, monitor_html_gz_len);
}

static esp_err_t detection_handler(httpd_req_t *req)
{
    char json[1024];
    get_detection_json(json, sizeof(json));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t system_handler(httpd_req_t *req)
{
    float temp = 0.0f;
    if (s_temp_sensor)
        temperature_sensor_get_celsius(s_temp_sensor, &temp);

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    /* Flash size from Kconfig — avoids calling into the SPI flash driver
       from the HTTP task while the MJPEG stream is concurrently running. */
#if   defined(CONFIG_ESPTOOLPY_FLASHSIZE_128MB)
    const uint32_t flash_mb = 128;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_64MB)
    const uint32_t flash_mb = 64;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_32MB)
    const uint32_t flash_mb = 32;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_16MB)
    const uint32_t flash_mb = 16;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_8MB)
    const uint32_t flash_mb = 8;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_4MB)
    const uint32_t flash_mb = 4;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_2MB)
    const uint32_t flash_mb = 2;
#else
    const uint32_t flash_mb = 0;
#endif

    char json[512];
    snprintf(json, sizeof(json),
        "{\"free_heap\":%lu,\"min_free_heap\":%lu,\"total_heap\":%lu,"
        "\"free_psram\":%lu,\"total_psram\":%lu,"
        "\"cpu_freq_mhz\":%d,\"temperature\":%.1f,\"uptime_sec\":%lld,"
        "\"chip_cores\":%d,\"flash_size_mb\":%lu}",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        (unsigned long)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned long)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
        CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        temp,
        (long long)(esp_timer_get_time() / 1000000LL),
        (int)chip.cores,
        (unsigned long)flash_mb);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

static const char OTA_PAGE[] =
    "<!doctype html>\n"
    "<html lang=\"en\" data-bs-theme=\"auto\">\n"
    "<head>\n"
    "  <meta charset=\"utf-8\">\n"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "  <title>OTA Update &mdash; AI Cam S3</title>\n"
    "  <script>\n"
    "  (() => {\n"
    "    'use strict'\n"
    "    const stored=()=>localStorage.getItem('theme')\n"
    "    const setS=t=>localStorage.setItem('theme',t)\n"
    "    const pref=()=>{const s=stored();return s||(window.matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light')}\n"
    "    const apply=t=>document.documentElement.setAttribute('data-bs-theme',\n"
    "      (t==='auto'&&window.matchMedia('(prefers-color-scheme:dark)').matches)?'dark':t)\n"
    "    apply(pref())\n"
    "    const mark=(t,focus)=>{\n"
    "      const sw=document.querySelector('#bd-theme');if(!sw)return\n"
    "      const ic=document.querySelector('.theme-icon-active use')\n"
    "      const btn=document.querySelector(`[data-bs-theme-value=\"${t}\"]`);if(!btn)return\n"
    "      document.querySelectorAll('[data-bs-theme-value]').forEach(e=>{e.classList.remove('active');e.setAttribute('aria-pressed','false')})\n"
    "      btn.classList.add('active');btn.setAttribute('aria-pressed','true')\n"
    "      if(ic)ic.setAttribute('href',btn.querySelector('svg use').getAttribute('href'))\n"
    "      if(focus)sw.focus()\n"
    "    }\n"
    "    window.matchMedia('(prefers-color-scheme:dark)').addEventListener('change',()=>{\n"
    "      const s=stored();if(s!=='light'&&s!=='dark')apply(pref())\n"
    "    })\n"
    "    window.addEventListener('DOMContentLoaded',()=>{\n"
    "      mark(pref())\n"
    "      document.querySelectorAll('[data-bs-theme-value]').forEach(el=>\n"
    "        el.addEventListener('click',()=>{const t=el.getAttribute('data-bs-theme-value');setS(t);apply(t);mark(t,true)})\n"
    "      )\n"
    "    })\n"
    "  })()\n"
    "  </script>\n"
    "  <link href=\"/bootstrap.min.css\" rel=\"stylesheet\">\n"
    "  <link href=\"/bootstrap-icons.min.css\" rel=\"stylesheet\">\n"
    "  <style>\n"
    "    #drop-zone {\n"
    "      border: 2px dashed var(--bs-border-color);\n"
    "      border-radius: 8px;\n"
    "      padding: 32px 16px;\n"
    "      text-align: center;\n"
    "      cursor: pointer;\n"
    "      transition: background .15s, border-color .15s;\n"
    "    }\n"
    "    #drop-zone.drag-over {\n"
    "      background: var(--bs-primary-bg-subtle);\n"
    "      border-color: var(--bs-primary);\n"
    "    }\n"
    "    #file-input { display: none; }\n"
    "  </style>\n"
    "</head>\n"
    "<body class=\"bg-body-tertiary d-flex flex-column min-vh-100\">\n"
    "  <svg xmlns=\"http://www.w3.org/2000/svg\" class=\"d-none\">\n"
    "    <symbol id=\"check2\" viewBox=\"0 0 16 16\"><path d=\"M13.854 3.646a.5.5 0 0 1 0 .708l-7 7a.5.5 0 0 1-.708 0l-3.5-3.5a.5.5 0 1 1 .708-.708L6.5 10.293l6.646-6.647a.5.5 0 0 1 .708 0z\"/></symbol>\n"
    "    <symbol id=\"circle-half\" viewBox=\"0 0 16 16\"><path d=\"M8 15A7 7 0 1 0 8 1v14zm0 1A8 8 0 1 1 8 0a8 8 0 0 1 0 16z\"/></symbol>\n"
    "    <symbol id=\"moon-stars-fill\" viewBox=\"0 0 16 16\">\n"
    "      <path d=\"M6 .278a.768.768 0 0 1 .08.858 7.208 7.208 0 0 0-.878 3.46c0 4.021 3.278 7.277 7.318 7.277.527 0 1.04-.055 1.533-.16a.787.787 0 0 1 .81.316.733.733 0 0 1-.031.893A8.349 8.349 0 0 1 8.344 16C3.734 16 0 12.286 0 7.71 0 4.266 2.114 1.312 5.124.06A.752.752 0 0 1 6 .278z\"/>\n"
    "      <path d=\"M10.794 3.148a.217.217 0 0 1 .412 0l.387 1.162c.173.518.579.924 1.097 1.097l1.162.387a.217.217 0 0 1 0 .412l-1.162.387a1.734 1.734 0 0 0-1.097 1.097l-.387 1.162a.217.217 0 0 1-.412 0l-.387-1.162A1.734 1.734 0 0 0 9.31 6.593l-1.162-.387a.217.217 0 0 1 0-.412l1.162-.387a1.734 1.734 0 0 0 1.097-1.097l.387-1.162z\"/>\n"
    "    </symbol>\n"
    "    <symbol id=\"sun-fill\" viewBox=\"0 0 16 16\">\n"
    "      <path d=\"M8 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM8 0a.5.5 0 0 1 .5.5v2a.5.5 0 0 1-1 0v-2A.5.5 0 0 1 8 0zm0 13a.5.5 0 0 1 .5.5v2a.5.5 0 0 1-1 0v-2A.5.5 0 0 1 8 13zm8-5a.5.5 0 0 1-.5.5h-2a.5.5 0 0 1 0-1h2a.5.5 0 0 1 .5.5zM3 8a.5.5 0 0 1-.5.5h-2a.5.5 0 0 1 0-1h2A.5.5 0 0 1 3 8zm10.657-5.657a.5.5 0 0 1 0 .707l-1.414 1.415a.5.5 0 1 1-.707-.708l1.414-1.414a.5.5 0 0 1 .707 0zm-9.193 9.193a.5.5 0 0 1 0 .707L3.05 13.657a.5.5 0 0 1-.707-.707l1.414-1.414a.5.5 0 0 1 .707 0zm9.193 2.121a.5.5 0 0 1-.707 0l-1.414-1.414a.5.5 0 0 1 .707-.707l1.414 1.414a.5.5 0 0 1 0 .707zM4.464 4.465a.5.5 0 0 1-.707 0L2.343 3.05a.5.5 0 1 1 .707-.707l1.414 1.414a.5.5 0 0 1 0 .708z\"/>\n"
    "    </symbol>\n"
    "  </svg>\n"
    "  <nav class=\"navbar border-bottom bg-body-tertiary px-3\">\n"
    "    <a href=\"/\" class=\"navbar-brand\">\n"
    "      <i class=\"bi bi-camera-video-fill text-primary me-1\"></i>&larr; AI Cam S3\n"
    "    </a>\n"
    "    <div class=\"ms-auto\">\n"
    "      <div class=\"dropdown\">\n"
    "        <button class=\"btn btn-sm btn-outline-secondary dropdown-toggle d-flex align-items-center gap-1\"\n"
    "          id=\"bd-theme\" type=\"button\" data-bs-toggle=\"dropdown\">\n"
    "          <svg class=\"bi theme-icon-active\" width=\"1em\" height=\"1em\"><use href=\"#circle-half\"></use></svg>\n"
    "          <span class=\"visually-hidden\" id=\"bd-theme-text\">Toggle theme</span>\n"
    "        </button>\n"
    "        <ul class=\"dropdown-menu dropdown-menu-end shadow\">\n"
    "          <li><button type=\"button\" class=\"dropdown-item d-flex align-items-center\" data-bs-theme-value=\"light\">\n"
    "            <svg class=\"bi me-2 opacity-50 theme-icon\" width=\"1em\" height=\"1em\"><use href=\"#sun-fill\"></use></svg>Light\n"
    "            <svg class=\"bi ms-auto d-none\" width=\"1em\" height=\"1em\"><use href=\"#check2\"></use></svg>\n"
    "          </button></li>\n"
    "          <li><button type=\"button\" class=\"dropdown-item d-flex align-items-center\" data-bs-theme-value=\"dark\">\n"
    "            <svg class=\"bi me-2 opacity-50 theme-icon\" width=\"1em\" height=\"1em\"><use href=\"#moon-stars-fill\"></use></svg>Dark\n"
    "            <svg class=\"bi ms-auto d-none\" width=\"1em\" height=\"1em\"><use href=\"#check2\"></use></svg>\n"
    "          </button></li>\n"
    "          <li><button type=\"button\" class=\"dropdown-item d-flex align-items-center active\" data-bs-theme-value=\"auto\">\n"
    "            <svg class=\"bi me-2 opacity-50 theme-icon\" width=\"1em\" height=\"1em\"><use href=\"#circle-half\"></use></svg>Auto\n"
    "            <svg class=\"bi ms-auto d-none\" width=\"1em\" height=\"1em\"><use href=\"#check2\"></use></svg>\n"
    "          </button></li>\n"
    "        </ul>\n"
    "      </div>\n"
    "    </div>\n"
    "  </nav>\n"
    "  <main class=\"flex-grow-1 d-flex align-items-center justify-content-center py-4 px-3\">\n"
    "    <div class=\"card shadow-sm w-100\" style=\"max-width:540px\">\n"
    "      <div class=\"card-header bg-primary text-white\">\n"
    "        <i class=\"bi bi-cloud-upload me-2\"></i>Firmware OTA Update\n"
    "      </div>\n"
    "      <div class=\"card-body\">\n"
    "        <div class=\"d-flex gap-3 mb-3 small text-muted\">\n"
    "          <span><i class=\"bi bi-cpu me-1\"></i>AI Cam S3</span>\n"
    "          <span id=\"part-info\"><i class=\"bi bi-hdd me-1\"></i>Ready</span>\n"
    "        </div>\n"
    "        <div id=\"drop-zone\" onclick=\"document.getElementById('file-input').click()\">\n"
    "          <i class=\"bi bi-file-earmark-binary fs-2 text-muted\"></i>\n"
    "          <p class=\"mb-1 mt-2\">Drag <code>.bin</code> here or click to choose</p>\n"
    "          <p class=\"text-muted small mb-0\" id=\"file-name\">No file selected</p>\n"
    "        </div>\n"
    "        <input type=\"file\" id=\"file-input\" accept=\".bin\">\n"
    "        <button class=\"btn btn-primary w-100 mt-3\" id=\"flash-btn\" disabled>\n"
    "          <i class=\"bi bi-lightning-charge-fill me-1\"></i>Flash Firmware\n"
    "        </button>\n"
    "        <div id=\"upload-progress\" class=\"mt-3 d-none\">\n"
    "          <div class=\"d-flex justify-content-between small mb-1\">\n"
    "            <span>Uploading&hellip;</span>\n"
    "            <span id=\"pct-label\">0%</span>\n"
    "          </div>\n"
    "          <div class=\"progress\" style=\"height:10px\">\n"
    "            <div class=\"progress-bar progress-bar-striped progress-bar-animated\" id=\"upload-bar\" style=\"width:0%\"></div>\n"
    "          </div>\n"
    "        </div>\n"
    "        <div id=\"status-alert\" class=\"alert mt-3 d-none\" role=\"alert\"></div>\n"
    "        <div id=\"reboot-section\" class=\"mt-3 d-none\">\n"
    "          <div class=\"d-flex justify-content-between small mb-1\">\n"
    "            <span class=\"text-success\">\n"
    "              <i class=\"bi bi-check-circle-fill me-1\"></i>\n"
    "              Redirecting in <strong id=\"cd\">10</strong> s&hellip;\n"
    "            </span>\n"
    "          </div>\n"
    "          <div class=\"progress\" style=\"height:6px\">\n"
    "            <div class=\"progress-bar bg-success\" id=\"cd-bar\" style=\"width:100%\"></div>\n"
    "          </div>\n"
    "        </div>\n"
    "      </div>\n"
    "      <div class=\"card-footer text-muted small\">\n"
    "        Upload <code>build/AICamRobot.bin</code> &mdash; the device will reboot after flashing.\n"
    "      </div>\n"
    "    </div>\n"
    "  </main>\n"
    "  <script src=\"/bootstrap.bundle.min.js\"></script>\n"
    "  <script>\n"
    "    const fileInput = document.getElementById('file-input')\n"
    "    const dropZone = document.getElementById('drop-zone')\n"
    "    const fileName = document.getElementById('file-name')\n"
    "    const flashBtn = document.getElementById('flash-btn')\n"
    "    const statusAlert = document.getElementById('status-alert')\n"
    "    const progress = document.getElementById('upload-progress')\n"
    "    const uploadBar = document.getElementById('upload-bar')\n"
    "    const pctLabel = document.getElementById('pct-label')\n"
    "    const rebootSection = document.getElementById('reboot-section')\n"
    "    const cdSpan = document.getElementById('cd')\n"
    "    const cdBar = document.getElementById('cd-bar')\n"
    "    let selectedFile = null\n"
    "    let countdownTimer = null\n"
    "\n"
    "    function resetCountdown() {\n"
    "      if (countdownTimer) {\n"
    "        clearInterval(countdownTimer)\n"
    "        countdownTimer = null\n"
    "      }\n"
    "      cdSpan.textContent = '10'\n"
    "      cdBar.style.width = '100%'\n"
    "      rebootSection.classList.add('d-none')\n"
    "    }\n"
    "\n"
    "    function resetUploadState() {\n"
    "      progress.classList.add('d-none')\n"
    "      uploadBar.style.width = '0%'\n"
    "      pctLabel.textContent = '0%'\n"
    "      statusAlert.classList.add('d-none')\n"
    "      resetCountdown()\n"
    "    }\n"
    "\n"
    "    function showAlert(type, msg) {\n"
    "      statusAlert.className = `alert alert-${type} mt-3`\n"
    "      statusAlert.innerHTML = msg\n"
    "      statusAlert.classList.remove('d-none')\n"
    "    }\n"
    "\n"
    "    function startCountdown(seconds) {\n"
    "      resetCountdown()\n"
    "      rebootSection.classList.remove('d-none')\n"
    "      let remaining = seconds\n"
    "      cdSpan.textContent = remaining\n"
    "      cdBar.style.width = '100%'\n"
    "      countdownTimer = setInterval(() => {\n"
    "        remaining--\n"
    "        cdSpan.textContent = remaining\n"
    "        cdBar.style.width = Math.max(0, Math.round(remaining / seconds * 100)) + '%'\n"
    "        if (remaining <= 0) {\n"
    "          clearInterval(countdownTimer)\n"
    "          countdownTimer = null\n"
    "          location.href = '/'\n"
    "        }\n"
    "      }, 1000)\n"
    "    }\n"
    "\n"
    "    function selectFile(f) {\n"
    "      resetUploadState()\n"
    "      if (!f || !f.name.endsWith('.bin') || f.size <= 0) {\n"
    "        selectedFile = null\n"
    "        fileInput.value = ''\n"
    "        fileName.textContent = 'No file selected'\n"
    "        flashBtn.disabled = true\n"
    "        showAlert('danger', 'Please select a valid .bin firmware file.')\n"
    "        return\n"
    "      }\n"
    "      selectedFile = f\n"
    "      fileName.textContent = `${f.name}  (${(f.size / 1024 / 1024).toFixed(2)} MB)`\n"
    "      flashBtn.disabled = false\n"
    "    }\n"
    "\n"
    "    fileInput.addEventListener('change', () => selectFile(fileInput.files[0]))\n"
    "    dropZone.addEventListener('dragover', e => { e.preventDefault(); dropZone.classList.add('drag-over') })\n"
    "    dropZone.addEventListener('dragleave', () => dropZone.classList.remove('drag-over'))\n"
    "    dropZone.addEventListener('drop', e => {\n"
    "      e.preventDefault()\n"
    "      dropZone.classList.remove('drag-over')\n"
    "      selectFile(e.dataTransfer.files[0])\n"
    "    })\n"
    "\n"
    "    flashBtn.addEventListener('click', () => {\n"
    "      if (!selectedFile) return\n"
    "      flashBtn.disabled = true\n"
    "      statusAlert.classList.add('d-none')\n"
    "      resetCountdown()\n"
    "      progress.classList.remove('d-none')\n"
    "\n"
    "      const xhr = new XMLHttpRequest()\n"
    "      xhr.open('POST', '/ota/upload')\n"
    "      xhr.setRequestHeader('Content-Type', 'application/octet-stream')\n"
    "\n"
    "      xhr.upload.onprogress = e => {\n"
    "        if (!e.lengthComputable) return\n"
    "        const pct = Math.round(e.loaded / e.total * 100)\n"
    "        uploadBar.style.width = pct + '%'\n"
    "        pctLabel.textContent = `${pct}%  -  ${(e.loaded / 1048576).toFixed(1)} / ${(e.total / 1048576).toFixed(1)} MB`\n"
    "      }\n"
    "\n"
    "      xhr.onload = () => {\n"
    "        progress.classList.add('d-none')\n"
    "        flashBtn.disabled = false\n"
    "        if (xhr.status === 200) {\n"
    "          showAlert('success', '&#10003; Flash successful! Device is rebooting&hellip;')\n"
    "          startCountdown(10)\n"
    "        } else {\n"
    "          showAlert('danger', 'Error: ' + xhr.responseText)\n"
    "        }\n"
    "      }\n"
    "\n"
    "      xhr.onerror = () => {\n"
    "        progress.classList.add('d-none')\n"
    "        flashBtn.disabled = false\n"
    "        showAlert('warning', 'Connection lost &mdash; device may be rebooting.')\n"
    "      }\n"
    "\n"
    "      xhr.send(selectedFile)\n"
    "    })\n"
    "  </script>\n"
    "</body>\n"
    "</html>\n";

static esp_err_t ota_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, OTA_PAGE, strlen(OTA_PAGE));
}

static void ota_reboot_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(4096);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    bool write_err = false;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, remaining < 4096 ? remaining : 4096);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { write_err = true; break; }
        if (esp_ota_write(ota_handle, buf, n) != ESP_OK) { write_err = true; break; }
        remaining -= n;
    }
    free(buf);

    if (write_err) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write error");
        return ESP_FAIL;
    }
    if (esp_ota_end(ota_handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid image");
        return ESP_FAIL;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA success, rebooting to partition %s", target->label);
    httpd_resp_sendstr(req, "OK");
    xTaskCreate(ota_reboot_task, "ota_reboot", 1024, NULL, 5, NULL);
    return ESP_OK;
}

void register_httpd(const QueueHandle_t frame_i, const QueueHandle_t frame_o, const bool return_fb)
{
    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    gReturnFB = return_fb;

    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    if (temperature_sensor_install(&temp_cfg, &s_temp_sensor) == ESP_OK)
        temperature_sensor_enable(s_temp_sensor);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 21;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL};

    httpd_uri_t cmd_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL};

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL};

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    httpd_uri_t xclk_uri = {
        .uri = "/xclk",
        .method = HTTP_GET,
        .handler = xclk_handler,
        .user_ctx = NULL};

    httpd_uri_t reg_uri = {
        .uri = "/reg",
        .method = HTTP_GET,
        .handler = reg_handler,
        .user_ctx = NULL};

    httpd_uri_t greg_uri = {
        .uri = "/greg",
        .method = HTTP_GET,
        .handler = greg_handler,
        .user_ctx = NULL};

    httpd_uri_t pll_uri = {
        .uri = "/pll",
        .method = HTTP_GET,
        .handler = pll_handler,
        .user_ctx = NULL};

    httpd_uri_t win_uri = {
        .uri = "/resolution",
        .method = HTTP_GET,
        .handler = win_handler,
        .user_ctx = NULL};

    httpd_uri_t mdns_uri = {
        .uri = "/mdns",
        .method = HTTP_GET,
        .handler = mdns_handler,
        .user_ctx = NULL};

    httpd_uri_t monitor_uri = {
        .uri = "/monitor",
        .method = HTTP_GET,
        .handler = monitor_handler,
        .user_ctx = NULL};

    httpd_uri_t bootstrap_css_uri = {
        .uri = "/bootstrap.min.css",
        .method = HTTP_GET,
        .handler = bootstrap_css_handler,
        .user_ctx = NULL};

    httpd_uri_t bootstrap_js_uri = {
        .uri = "/bootstrap.bundle.min.js",
        .method = HTTP_GET,
        .handler = bootstrap_js_handler,
        .user_ctx = NULL};

    httpd_uri_t bootstrap_icons_css_uri = {
        .uri = "/bootstrap-icons.min.css",
        .method = HTTP_GET,
        .handler = bootstrap_icons_css_handler,
        .user_ctx = NULL};

    httpd_uri_t bootstrap_icons_font_uri = {
        .uri = "/fonts/bootstrap-icons.woff2",
        .method = HTTP_GET,
        .handler = bootstrap_icons_font_handler,
        .user_ctx = NULL};

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);

        httpd_register_uri_handler(camera_httpd, &xclk_uri);
        httpd_register_uri_handler(camera_httpd, &reg_uri);
        httpd_register_uri_handler(camera_httpd, &greg_uri);
        httpd_register_uri_handler(camera_httpd, &pll_uri);
        httpd_register_uri_handler(camera_httpd, &win_uri);

        httpd_register_uri_handler(camera_httpd, &mdns_uri);
        httpd_register_uri_handler(camera_httpd, &monitor_uri);

        httpd_register_uri_handler(camera_httpd, &bootstrap_css_uri);
        httpd_register_uri_handler(camera_httpd, &bootstrap_js_uri);
        httpd_register_uri_handler(camera_httpd, &bootstrap_icons_css_uri);
        httpd_register_uri_handler(camera_httpd, &bootstrap_icons_font_uri);

        httpd_uri_t chartjs_uri = {
            .uri = "/chart.umd.js",
            .method = HTTP_GET,
            .handler = chartjs_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(camera_httpd, &chartjs_uri);

        httpd_uri_t detection_uri = {
            .uri = "/detection",
            .method = HTTP_GET,
            .handler = detection_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(camera_httpd, &detection_uri);

        httpd_uri_t system_uri = {
            .uri = "/api/system",
            .method = HTTP_GET,
            .handler = system_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(camera_httpd, &system_uri);

        httpd_uri_t ota_page_uri = {
            .uri = "/update",
            .method = HTTP_GET,
            .handler = ota_page_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(camera_httpd, &ota_page_uri);

        httpd_uri_t ota_upload_uri = {
            .uri = "/ota/upload",
            .method = HTTP_POST,
            .handler = ota_upload_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(camera_httpd, &ota_upload_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
