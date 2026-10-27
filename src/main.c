#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "matrix.h"
#include "settings.h"
#include "webserver.h"

static const char *TAG = "MAIN";

#define AP_SSID      "MATRIX-CLOCK"
#define AP_PASS      "matrix1234"
#define AP_CHANNEL   6
#define AP_MAX_CONN  4

static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_max_tx_power(34);

    ESP_LOGI(TAG, "Wi-Fi SoftAP started -> SSID: %s | Pass: %s | IP: 192.168.4.1", AP_SSID, AP_PASS);
}

static void clock_task(void *pvParameters) {
    bool colon = true;
    char line0[17];
    char line1[17];

    while (1) {
        time_t now;
        time(&now);
        struct tm ti;
        gmtime_r(&now, &ti);

        colon = !colon;

        char monthStr[4];
        strftime(monthStr, sizeof(monthStr), "%b", &ti);
        for (int i = 0; i < 3; i++) monthStr[i] = toupper((unsigned char)monthStr[i]);

        // Screen 0 (Top line): " 22:45  27 OCT  "
        snprintf(line0, sizeof(line0), " %02d%c%02d  %02d %s  ",
                 ti.tm_hour,
                 colon ? ':' : ' ',
                 ti.tm_min,
                 ti.tm_mday,
                 monthStr);

        // Screen 1 (Bottom line): "  SEC:%02d  ESP-IDF "
        snprintf(line1, sizeof(line1), "  SEC:%02d  ESP-IDF ", ti.tm_sec);

        matrix_clear();
        matrix_draw_string(0, 0, line0);
        matrix_draw_string(1, 0, line1);
        matrix_show();

        // Deferred NVS Flash commit
        settings_commit_if_needed();

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "  MATRIX CLOCK: ESP-IDF RTOS v1.0   ");
    ESP_LOGI(TAG, "====================================");

    // 1. Initialize NVS Flash & load persistent preferences
    settings_init();
    bool flip_h = false, flip_v = false;
    int brightness = 400;
    settings_load(&flip_h, &flip_v, &brightness);

    matrix_set_flip(flip_h, flip_v);
    matrix_set_brightness(brightness);

    // 2. Start hardware matrix multiplexer engine (FreeRTOS Task & Timer)
    matrix_init();

    // 3. Boot sweep animation & greeting
    matrix_screen_sweep();
    matrix_draw_string(0, 0, " MATRIX CLOCK   ");
    matrix_draw_string(1, 0, " ESP-IDF RTOS OK");
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(800));

    // 4. Initialize Wi-Fi SoftAP and Async HTTP Server
    wifi_init_softap();
    webserver_start();

    // 5. Start background clock & status task
    xTaskCreate(clock_task, "ClockTask", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "All subsystems running.");
}
