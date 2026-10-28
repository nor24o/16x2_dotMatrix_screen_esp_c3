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
static char s_sta_ip_str[20] = "";

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            s_stations_connected++;
            ESP_LOGI(TAG, "AP Client joined (AID=%d). Active AP clients: %d", event->aid, s_stations_connected);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            if (s_stations_connected > 0) s_stations_connected--;
            ESP_LOGI(TAG, "AP Client left. Active AP clients: %d", s_stations_connected);
        } else if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Connecting to Home Wi-Fi: %s...", STA_SSID);
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_sta_connected = false;
            ESP_LOGW(TAG, "Home Wi-Fi disconnected. Reconnecting in 3s...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            snprintf(s_sta_ip_str, sizeof(s_sta_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            s_sta_connected = true;
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "Home Wi-Fi CONNECTED! IP: %s", s_sta_ip_str);
            ESP_LOGI(TAG, "Web UI accessible at: http://%s/", s_sta_ip_str);
            ESP_LOGI(TAG, "========================================");
        }
    }
}

static void wifi_init_dual_mode(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

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

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // CRITICAL: Disable modem sleep power-save so Wi-Fi is 100% responsive without ARP/ping packet drops
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Wi-Fi Dual AP+STA initialized -> SoftAP: %s | Home STA: %s", AP_SSID, STA_SSID);
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

        // Screen 0 (Top line): " 22:45  27 OCT  "
        snprintf(line0, sizeof(line0), " %02d%c%02d  %02d %s  ",
                 ti.tm_hour,
                 colon ? ':' : ' ',
                 ti.tm_min,
                 ti.tm_mday,
                 monthStr);

        // Screen 1 (Bottom line): Cycle through IP address, SSID, and seconds
        uint32_t phase = (tick_count / 8) % 3; // Change every 4 seconds (8 * 500ms)
        if (s_sta_connected) {
            if (phase == 0) {
                snprintf(line1, sizeof(line1), "IP:%-13s", s_sta_ip_str);
            } else if (phase == 1) {
                snprintf(line1, sizeof(line1), "AP: 192.168.4.1 ");
            } else {
                snprintf(line1, sizeof(line1), "  SEC:%02d  ONLINE", ti.tm_sec);
            }
        } else {
            if (s_stations_connected > 0 && phase == 1) {
                snprintf(line1, sizeof(line1), "WEB: CONNECTED  ");
            } else if (phase == 0) {
                snprintf(line1, sizeof(line1), "IP: 192.168.4.1 ");
            } else if (phase == 1) {
                snprintf(line1, sizeof(line1), "SSID: MATRIX-CLK");
            } else {
                snprintf(line1, sizeof(line1), "  SEC:%02d  STANDBY", ti.tm_sec);
            }
        }

        matrix_clear();
        matrix_draw_string(0, 0, line0);
        matrix_draw_string(1, 0, line1);
        matrix_show();

        // Deferred NVS Flash commit
        settings_commit_if_needed();

        printf("CLOCK_TICK: %02d:%02d:%02d, clients=%d, sta=%s\n",
               ti.tm_hour, ti.tm_min, ti.tm_sec, s_stations_connected, s_sta_connected ? s_sta_ip_str : "DISCONNECTED");
        fflush(stdout);

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

    // 2. Start hardware matrix multiplexer engine (FreeRTOS Timer)
    matrix_init();

    // 3. Boot sweep animation & greeting
    matrix_screen_sweep();
    matrix_draw_string(0, 0, " MATRIX CLOCK   ");
    matrix_draw_string(1, 0, " ESP-IDF RTOS OK");
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(800));

    // 4. Initialize Wi-Fi in Dual AP+STA Mode
    wifi_init_dual_mode();

    // 5. Start Captive Portal DNS Server (redirects all domains to 192.168.4.1)
    dns_server_start();

    // 6. Start Web Server
    esp_err_t err = webserver_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "webserver_start failed: 0x%x", err);
    }

    // 7. Start background clock & status task
    xTaskCreate(clock_task, "ClockTask", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "All subsystems running.");
}
