#include "settings.h"
#include "matrix.h"
#include <string.h>
#include <time.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "SETTINGS";
static const char *NVS_NAMESPACE = "matrix_cfg";

app_settings_t g_cfg = {
    .bright = 400,
    .nbright = 40,
    .nstart = 22,
    .nend = 7,
    .fliph = false,
    .flipv = false,
    .gmtoff = 7200,
    .dstoff = 3600,
    .city = "Sauca",
    .country = "RO",
    .region = "Satu Mare",
    .show_clock = true,
    .show_typewriter = true,
    .show_sparkle = true,
    .show_scanner = true,
    .show_temps = true,
    .show_msg = false,
    .custom_msg = "HELLO",
    .custom_msg_duration_ms = 10000,
    .ext_temps = {24.5f, 18.2f, 12.0f, -5.5f},
    .ext_temp_icons = {ICON_HOME, ICON_BED, ICON_TREE, ICON_WATER}
};

char g_ip_str[32] = "WAITING FOR IP...";
char g_live_weather_str[128] = "WAITING FOR WEATHER...   ";
char g_night_weather_str[64] = "WAITING...";

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

void settings_load_all(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        int32_t v32;
        uint8_t v8;
        size_t len;

        if (nvs_get_i32(h, "bright", &v32) == ESP_OK) g_cfg.bright = (v32 < 10) ? 10 : ((v32 > 800) ? 800 : v32);
        if (nvs_get_i32(h, "nbright", &v32) == ESP_OK) g_cfg.nbright = (v32 < 5) ? 5 : ((v32 > 800) ? 800 : v32);
        if (nvs_get_i32(h, "nstart", &v32) == ESP_OK) g_cfg.nstart = (v32 < 0) ? 0 : ((v32 > 23) ? 23 : v32);
        if (nvs_get_i32(h, "nend", &v32) == ESP_OK) g_cfg.nend = (v32 < 0) ? 0 : ((v32 > 23) ? 23 : v32);
        if (nvs_get_u8(h, "fliph", &v8) == ESP_OK) g_cfg.fliph = (bool)v8;
        if (nvs_get_u8(h, "flipv", &v8) == ESP_OK) g_cfg.flipv = (bool)v8;

        if (nvs_get_i32(h, "gmtoff", &v32) == ESP_OK) g_cfg.gmtoff = v32;
        if (nvs_get_i32(h, "dstoff", &v32) == ESP_OK) g_cfg.dstoff = v32;

        len = sizeof(g_cfg.city);
        nvs_get_str(h, "city", g_cfg.city, &len);
        len = sizeof(g_cfg.country);
        nvs_get_str(h, "country", g_cfg.country, &len);
        len = sizeof(g_cfg.region);
        nvs_get_str(h, "region", g_cfg.region, &len);

        if (nvs_get_u8(h, "sClk", &v8) == ESP_OK) g_cfg.show_clock = (bool)v8;
        if (nvs_get_u8(h, "sTyp", &v8) == ESP_OK) g_cfg.show_typewriter = (bool)v8;
        if (nvs_get_u8(h, "sSprk", &v8) == ESP_OK) g_cfg.show_sparkle = (bool)v8;
        if (nvs_get_u8(h, "sScan", &v8) == ESP_OK) g_cfg.show_scanner = (bool)v8;
        if (nvs_get_u8(h, "sTmp", &v8) == ESP_OK) g_cfg.show_temps = (bool)v8;
        if (nvs_get_u8(h, "sMsg", &v8) == ESP_OK) g_cfg.show_msg = (bool)v8;

        len = sizeof(g_cfg.custom_msg);
        nvs_get_str(h, "cMsg", g_cfg.custom_msg, &len);
        if (nvs_get_i32(h, "cMsgDur", &v32) == ESP_OK) g_cfg.custom_msg_duration_ms = v32;

        if (nvs_get_u8(h, "iTmp0", &v8) == ESP_OK) g_cfg.ext_temp_icons[0] = (v8 < ICON_COUNT) ? v8 : 0;
        if (nvs_get_u8(h, "iTmp1", &v8) == ESP_OK) g_cfg.ext_temp_icons[1] = (v8 < ICON_COUNT) ? v8 : 0;
        if (nvs_get_u8(h, "iTmp2", &v8) == ESP_OK) g_cfg.ext_temp_icons[2] = (v8 < ICON_COUNT) ? v8 : 0;
        if (nvs_get_u8(h, "iTmp3", &v8) == ESP_OK) g_cfg.ext_temp_icons[3] = (v8 < ICON_COUNT) ? v8 : 0;

        nvs_close(h);
        ESP_LOGI(TAG, "Loaded settings from NVS -> city=%s, bright=%d, nbright=%d", g_cfg.city, g_cfg.bright, g_cfg.nbright);
    } else {
        ESP_LOGI(TAG, "Using default settings.");
    }

    if (!g_cfg.show_clock && !g_cfg.show_typewriter && !g_cfg.show_sparkle &&
        !g_cfg.show_scanner && !g_cfg.show_temps && !g_cfg.show_msg) {
        g_cfg.show_clock = true;
    }

    matrix_set_flip(g_cfg.fliph, g_cfg.flipv);
    matrix_set_brightness(settings_is_night_time() ? g_cfg.nbright : g_cfg.bright);
}

void settings_save_all(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "bright", g_cfg.bright);
        nvs_set_i32(h, "nbright", g_cfg.nbright);
        nvs_set_i32(h, "nstart", g_cfg.nstart);
        nvs_set_i32(h, "nend", g_cfg.nend);
        nvs_set_u8(h, "fliph", g_cfg.fliph ? 1 : 0);
        nvs_set_u8(h, "flipv", g_cfg.flipv ? 1 : 0);

        nvs_set_i32(h, "gmtoff", (int32_t)g_cfg.gmtoff);
        nvs_set_i32(h, "dstoff", g_cfg.dstoff);

        nvs_set_str(h, "city", g_cfg.city);
        nvs_set_str(h, "country", g_cfg.country);
        nvs_set_str(h, "region", g_cfg.region);

        nvs_set_u8(h, "sClk", g_cfg.show_clock ? 1 : 0);
        nvs_set_u8(h, "sTyp", g_cfg.show_typewriter ? 1 : 0);
        nvs_set_u8(h, "sSprk", g_cfg.show_sparkle ? 1 : 0);
        nvs_set_u8(h, "sScan", g_cfg.show_scanner ? 1 : 0);
        nvs_set_u8(h, "sTmp", g_cfg.show_temps ? 1 : 0);
        nvs_set_u8(h, "sMsg", g_cfg.show_msg ? 1 : 0);

        nvs_set_str(h, "cMsg", g_cfg.custom_msg);
        nvs_set_i32(h, "cMsgDur", g_cfg.custom_msg_duration_ms);

        nvs_set_u8(h, "iTmp0", g_cfg.ext_temp_icons[0]);
        nvs_set_u8(h, "iTmp1", g_cfg.ext_temp_icons[1]);
        nvs_set_u8(h, "iTmp2", g_cfg.ext_temp_icons[2]);
        nvs_set_u8(h, "iTmp3", g_cfg.ext_temp_icons[3]);

        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Saved all settings to NVS Flash.");
    }
}

void settings_schedule_save(void) {
    s_pending_save = true;
    s_last_change_us = esp_timer_get_time();
}

void settings_commit_if_needed(void) {
    if (s_pending_save) {
        int64_t now = esp_timer_get_time();
        if ((now - s_last_change_us) >= 1200000) {
            s_pending_save = false;
            settings_save_all();
        }
    }
}

bool settings_is_night_time(void) {
    time_t now;
    time(&now);
    struct tm ti;
    gmtime_r(&now, &ti);

    if (g_cfg.nstart > g_cfg.nend) {
        return (ti.tm_hour >= g_cfg.nstart || ti.tm_hour < g_cfg.nend);
    } else if (g_cfg.nstart < g_cfg.nend) {
        return (ti.tm_hour >= g_cfg.nstart && ti.tm_hour < g_cfg.nend);
    }
    return false;
}
