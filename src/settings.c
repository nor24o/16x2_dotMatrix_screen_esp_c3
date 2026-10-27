#include "settings.h"
#include "matrix.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "SETTINGS";
static const char *NVS_NAMESPACE = "matrix_cfg";

static bool s_pending_save = false;
static int64_t s_last_change_us = 0;

void settings_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void settings_load(bool *flip_h, bool *flip_v, int *brightness) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint8_t h = 0, v = 0;
        int32_t b = 400;
        nvs_get_u8(handle, "fliph", &h);
        nvs_get_u8(handle, "flipv", &v);
        nvs_get_i32(handle, "bright", &b);
        nvs_close(handle);

        if (flip_h) *flip_h = (bool)h;
        if (flip_v) *flip_v = (bool)v;
        if (brightness) *brightness = (int)b;
        ESP_LOGI(TAG, "Loaded from NVS -> flip_h: %d, flip_v: %d, bright: %ld", h, v, b);
    } else {
        if (flip_h) *flip_h = false;
        if (flip_v) *flip_v = false;
        if (brightness) *brightness = 400;
        ESP_LOGI(TAG, "No NVS saved settings, using defaults.");
    }
}

void settings_save(bool flip_h, bool flip_v, int brightness) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u8(handle, "fliph", flip_h ? 1 : 0);
        nvs_set_u8(handle, "flipv", flip_v ? 1 : 0);
        nvs_set_i32(handle, "bright", brightness);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Saved settings to NVS Flash.");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
    }
}

void settings_schedule_save(void) {
    s_pending_save = true;
    s_last_change_us = esp_timer_get_time();
}

void settings_commit_if_needed(void) {
    if (s_pending_save) {
        int64_t now = esp_timer_get_time();
        // Save after 1.5 seconds of quiet time
        if ((now - s_last_change_us) >= 1500000) {
            s_pending_save = false;
            bool fh, fv;
            matrix_get_flip(&fh, &fv);
            int br = matrix_get_brightness();
            settings_save(fh, fv, br);
        }
    }
}
