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
#include "dns_server.h"

static const char *TAG = "MAIN";

#define AP_SSID      "MATRIX-CLOCK"
#define AP_PASS      "matrix1234"
#define AP_CHANNEL   6
#define AP_MAX_CONN  4

#define STA_SSID     "Link2"
#define STA_PASS     "Horvat2017"

static volatile uint8_t s_stations_connected = 0;
static volatile bool s_sta_connected = false;
static volatile bool s_ap_fallback_active = false;
static volatile int s_sta_retry_count = 0;
static char s_sta_ip_str[20] = "";

static void enable_ap_fallback(void) {
    if (s_ap_fallback_active) return;
    s_ap_fallback_active = true;

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    dns_server_start();
    ESP_LOGW(TAG, "Fallback SoftAP enabled: %s (192.168.4.1)", AP_SSID);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            s_stations_connected++;
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            if (s_stations_connected > 0) s_stations_connected--;
        } else if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Connecting to Home Wi-Fi: %s...", STA_SSID);
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_sta_connected = false;
            s_sta_retry_count++;
            if (s_sta_retry_count >= 6 && !s_ap_fallback_active) {
                enable_ap_fallback();
            }
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            snprintf(s_sta_ip_str, sizeof(s_sta_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            s_sta_connected = true;
            s_sta_retry_count = 0;
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "Home Wi-Fi CONNECTED! IP: %s", s_sta_ip_str);
            ESP_LOGI(TAG, "Web UI: http://%s/", s_sta_ip_str);
            ESP_LOGI(TAG, "========================================");
        }
    }
}

static void wifi_init_fast_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t sta_config = {
        .sta = {
            .ssid = STA_SSID,
            .password = STA_PASS,
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    // Pure Station mode for 100% radio throughput & zero SoftAP interference on home LAN
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Disable modem power saving completely for <5ms HTTP response times
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

static void clock_task(void *pvParameters) {
    bool colon = true;
    char line0[17];
    char line1[17];
    uint32_t tick_count = 0;

    while (1) {
        time_t now;
        time(&now);
        struct tm ti;
        gmtime_r(&now, &ti);

        colon = !colon;
        tick_count++;

        char monthStr[4];
        strftime(monthStr, sizeof(monthStr), "%b", &ti);
        for (int i = 0; i < 3; i++) monthStr[i] = toupper((unsigned char)monthStr[i]);

        // Screen 0 (Top line): " 17:52  28 OCT  "
        snprintf(line0, sizeof(line0), " %02d%c%02d  %02d %s  ",
                 ti.tm_hour,
                 colon ? ':' : ' ',
                 ti.tm_min,
                 ti.tm_mday,
                 monthStr);

        // Screen 1 (Bottom line): Show LAN IP 75% of the time, seconds 25% of the time
        uint32_t phase = (tick_count / 4) % 4;
        if (s_sta_connected) {
            if (phase < 3) {
                snprintf(line1, sizeof(line1), "IP:%-13s", s_sta_ip_str);
            } else {
                snprintf(line1, sizeof(line1), "  SEC:%02d  ONLINE", ti.tm_sec);
            }
        } else if (s_ap_fallback_active) {
            if (phase < 2) {
                snprintf(line1, sizeof(line1), "AP: 192.168.4.1 ");
            } else {
                snprintf(line1, sizeof(line1), "SSID: MATRIX-CLK");
            }
        } else {
            snprintf(line1, sizeof(line1), "WIFI CONNECTING ");
        }

        matrix_clear();
        matrix_draw_string(0, 0, line0);
        matrix_draw_string(1, 0, line1);
        matrix_show();

        settings_commit_if_needed();

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "  MATRIX CLOCK: ESP-IDF RTOS v2.0   ");
    ESP_LOGI(TAG, "====================================");

    // 1. Initialize NVS Flash & load persistent preferences
    settings_init();
    bool flip_h = false, flip_v = false;
    int brightness = 350;
    settings_load(&flip_h, &flip_v, &brightness);

    matrix_set_flip(flip_h, flip_v);
    matrix_set_brightness(brightness);

    // 2. Start zero-flicker dual-timer hardware matrix multiplexer (104 Hz)
    matrix_init();

    // 3. Set initial clock time if unset (2026-10-28 17:53:00 local)
    time_t now;
    time(&now);
    if (now < 1700000000) {
        struct timeval tv = { .tv_sec = 1793219600, .tv_usec = 0 };
        settimeofday(&tv, NULL);
    }

    // 4. Initialize high-speed Wi-Fi Station mode
    wifi_init_fast_sta();

    // 5. Start Fast Web Server
    esp_err_t err = webserver_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "webserver_start failed: 0x%x", err);
    }

    // 6. Start background clock & status task
    xTaskCreate(clock_task, "ClockTask", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "All subsystems running.");
}
