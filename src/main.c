#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_http_client.h"
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
#define UDP_PORT     8888

static volatile bool s_sta_connected = false;
static volatile bool s_ap_fallback_active = false;
static volatile int  s_sta_retry_count = 0;

static volatile app_state_t s_current_state = STATE_BOOT_IP;
static volatile bool s_init_next_anim = true;
static volatile bool s_weather_refresh_req = true;

static inline uint32_t millis_now(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void app_trigger_state(app_state_t new_state) {
    if (settings_is_night_time()) {
        if (new_state == STATE_WIPE_TO_TEMPS || new_state == STATE_SHOW_TEMPS || new_state == STATE_TEMPS) {
            new_state = STATE_NIGHT_TEMPS;
        } else if (new_state == STATE_WIPE_TO_MSG || new_state == STATE_SHOW_MSG || new_state == STATE_MESSAGE) {
            new_state = STATE_NIGHT_MSG;
        }
    }
    s_current_state = new_state;
    s_init_next_anim = true;
}

app_state_t app_get_state(void) {
    return s_current_state;
}

void app_trigger_weather_refresh(void) {
    s_weather_refresh_req = true;
}

// ==========================================
// 1. WEATHER HELPERS & OPEN-METEO CLIENT
// ==========================================
static void normalize_city_name(const char *in, char *out, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < max_len; i++) {
        unsigned char c = (unsigned char)in[i];
        unsigned char n = (unsigned char)in[i + 1];
        if (c == 0xC4 && (n == 0x83 || n == 0x82)) { out[j++] = 'A'; i++; continue; } // ă, Ă
        if (c == 0xC3 && (n == 0xA2 || n == 0x82 || n == 0xAE || n == 0x8E)) {
            out[j++] = (n == 0xAE || n == 0x8E) ? 'I' : 'A';
            i++;
            continue;
        }
        if (c == 0xC8 && (n == 0x99 || n == 0x98 || n == 0x9B || n == 0x9A)) {
            out[j++] = (n == 0x99 || n == 0x98) ? 'S' : 'T';
            i++;
            continue;
        }
        if (c == 0xC5 && (n == 0x9F || n == 0x9E || n == 0xA3 || n == 0xA2)) {
            out[j++] = (n == 0x9F || n == 0x9E) ? 'S' : 'T';
            i++;
            continue;
        }
        char up = (char)toupper(c);
        if ((up >= 'A' && up <= 'Z') || (up >= '0' && up <= '9') || up == ' ' || up == '-') {
            out[j++] = up;
        }
    }
    out[j] = '\0';
}

static const char *get_weather_condition(int code) {
    if (code == 0) return "CLEAR";
    if (code == 1 || code == 2) return "PARTLY CLOUDY";
    if (code == 3) return "OVERCAST";
    if (code >= 45 && code <= 48) return "FOGGY";
    if (code >= 51 && code <= 67) return "RAIN";
    if (code >= 71 && code <= 77) return "SNOW";
    if (code >= 80 && code <= 82) return "SHOWERS";
    if (code >= 95 && code <= 99) return "THUNDERSTORM";
    return "UNKNOWN";
}

static const char *get_short_weather_condition(int code) {
    if (code == 0) return "CLR";
    if (code == 1 || code == 2) return "P.CLD";
    if (code == 3) return "OVCST";
    if (code >= 45 && code <= 48) return "FOG";
    if (code >= 51 && code <= 67) return "RAIN";
    if (code >= 71 && code <= 77) return "SNOW";
    if (code >= 80 && code <= 82) return "SHWR";
    if (code >= 95 && code <= 99) return "STORM";
    return "UNK";
}

typedef struct {
    char *buf;
    int   len;
    int   cap;
} http_buf_t;

static esp_err_t http_evt_handler(esp_http_client_event_t *evt) {
    http_buf_t *hb = (http_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && hb && hb->buf) {
        int copy_len = evt->data_len;
        if (hb->len + copy_len >= hb->cap) {
            copy_len = (hb->cap - 1) - hb->len;
        }
        if (copy_len > 0) {
            memcpy(hb->buf + hb->len, evt->data, copy_len);
            hb->len += copy_len;
            hb->buf[hb->len] = '\0';
        }
    }
    return ESP_OK;
}

static bool http_get_string(const char *url, char *out_buf, int cap) {
    out_buf[0] = '\0';
    http_buf_t hb = { .buf = out_buf, .len = 0, .cap = cap };
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 5000,
        .event_handler = http_evt_handler,
        .user_data = &hb,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return (err == ESP_OK && status == 200 && hb.len > 0);
}

static void url_encode(const char *src, char *dst, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 4 < max_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_') {
            dst[j++] = (char)c;
        } else if (c == ' ') {
            dst[j++] = '%'; dst[j++] = '2'; dst[j++] = '0';
        }
    }
    dst[j] = '\0';
}

static void fetch_weather(void) {
    if (!s_sta_connected) return;

    static char resp_buf[4096];
    char city_enc[64];
    url_encode(g_cfg.city, city_enc, sizeof(city_enc));

    char geo_url[160];
    snprintf(geo_url, sizeof(geo_url),
             "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=10", city_enc);

    if (!http_get_string(geo_url, resp_buf, sizeof(resp_buf))) {
        ESP_LOGW(TAG, "Geo lookup failed for %s", g_cfg.city);
        return;
    }

    // Search results array for matching country_code and region
    const char *p = strstr(resp_buf, "\"results\":");
    if (!p) {
        char clean[32];
        normalize_city_name(g_cfg.city, clean, sizeof(clean));
        snprintf(g_live_weather_str, sizeof(g_live_weather_str), "CITY %s NOT FOUND   ", clean);
        snprintf(g_night_weather_str, sizeof(g_night_weather_str), "NOT FOUND");
        return;
    }

    float lat = 0.0f, lon = 0.0f;
    char official_name[32] = "";
    bool match_found = false;

    const char *cur = p;
    while ((cur = strstr(cur, "\"name\":\"")) != NULL) {
        const char *next_obj = strstr(cur + 8, "\"name\":\"");
        size_t block_len = next_obj ? (size_t)(next_obj - cur) : strlen(cur);

        char name_raw[48] = "";
        const char *np = cur + 8;
        size_t ni = 0;
        while (*np && *np != '"' && ni + 1 < sizeof(name_raw)) name_raw[ni++] = *np++;
        name_raw[ni] = '\0';

        // Check country_code within this result object
        char cc_pat[32];
        snprintf(cc_pat, sizeof(cc_pat), "\"country_code\":\"%s\"", g_cfg.country);
        const char *cc_pos = strstr(cur, cc_pat);
        if (cc_pos && (size_t)(cc_pos - cur) < block_len) {
            bool region_ok = true;
            if (g_cfg.region[0] != '\0') {
                const char *adm = strstr(cur, "\"admin1\":\"");
                if (adm && (size_t)(adm - cur) < block_len) {
                    char adm_val[48] = "";
                    const char *ap = adm + 10;
                    size_t ai = 0;
                    while (*ap && *ap != '"' && ai + 1 < sizeof(adm_val)) adm_val[ai++] = *ap++;
                    adm_val[ai] = '\0';
                    if (strstr(adm_val, g_cfg.region) == NULL) region_ok = false;
                }
            }
            if (region_ok) {
                const char *lat_p = strstr(cur, "\"latitude\":");
                const char *lon_p = strstr(cur, "\"longitude\":");
                if (lat_p && lon_p) {
                    lat = strtof(lat_p + 11, NULL);
                    lon = strtof(lon_p + 12, NULL);
                    normalize_city_name(name_raw, official_name, sizeof(official_name));
                    match_found = true;
                    break;
                }
            }
        }
        cur += 8;
    }

    if (!match_found) {
        char clean[32];
        normalize_city_name(g_cfg.city, clean, sizeof(clean));
        snprintf(g_live_weather_str, sizeof(g_live_weather_str), "CITY %s IN %s NOT FOUND   ", clean, g_cfg.country);
        snprintf(g_night_weather_str, sizeof(g_night_weather_str), "NOT FOUND");
        return;
    }

    char w_url[200];
    snprintf(w_url, sizeof(w_url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,weather_code",
             lat, lon);

    if (http_get_string(w_url, resp_buf, sizeof(resp_buf))) {
        const char *cur_sec = strstr(resp_buf, "\"current\":{");
        if (cur_sec) {
            const char *tp = strstr(cur_sec, "\"temperature_2m\":");
            const char *hp = strstr(cur_sec, "\"relative_humidity_2m\":");
            const char *wp = strstr(cur_sec, "\"weather_code\":");
            if (tp && hp && wp) {
                float temp = strtof(tp + 17, NULL);
                int hum = atoi(hp + 23);
                int wmo = atoi(wp + 15);
                int t_round = (int)roundf(temp);
                const char *cond = get_weather_condition(wmo);
                const char *scond = get_short_weather_condition(wmo);

                snprintf(g_live_weather_str, sizeof(g_live_weather_str),
                         "%s: %s %dC   HUMIDITY %d%%   ", official_name, cond, t_round, hum);

                char short_city[3] = { official_name[0], official_name[1] ? official_name[1] : ' ', '\0' };
                snprintf(g_night_weather_str, sizeof(g_night_weather_str),
                         "%s:%s T%dC H%d%%", short_city, scond, t_round, hum);

                ESP_LOGI(TAG, "Weather updated: %s", g_live_weather_str);
            }
        }
    }
}

static void weather_task(void *pvParameters) {
    uint32_t last_fetch_ms = 0;
    const uint32_t interval_ms = 30 * 60 * 1000;

    while (1) {
        if (s_sta_connected) {
            uint32_t now_ms = millis_now();
            if (s_weather_refresh_req || (now_ms - last_fetch_ms >= interval_ms)) {
                s_weather_refresh_req = false;
                last_fetch_ms = now_ms;
                fetch_weather();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ==========================================
// 2. UDP PORT 8888 COMMAND LISTENER
// ==========================================
static uint8_t parse_json_icon(const char *json, const char *key, uint8_t fallback) {
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return fallback;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        char name[24];
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < sizeof(name)) name[i++] = *p++;
        name[i] = '\0';
        return matrix_parse_icon_name(name);
    } else if (*p >= '0' && *p <= '9') {
        int v = atoi(p);
        if (v >= 0 && v < ICON_COUNT) return (uint8_t)v;
    }
    return fallback;
}

static void udp_server_task(void *pvParameters) {
    char rx_buf[256];
    struct sockaddr_in serv_addr, client_addr;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        vTaskDelete(NULL);
        return;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(UDP_PORT);

    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP JSON Command Server listening on port %d", UDP_PORT);

    while (1) {
        socklen_t socklen = sizeof(client_addr);
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
                           (struct sockaddr *)&client_addr, &socklen);
        if (len <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        rx_buf[len] = '\0';

        const char *reply = NULL;
        if (strstr(rx_buf, "\"cmd\":\"message\"") || strstr(rx_buf, "\"cmd\": \"message\"")) {
            const char *tp = strstr(rx_buf, "\"text\":\"");
            if (tp) {
                tp += 8;
                size_t i = 0;
                while (*tp && *tp != '"' && i + 1 < sizeof(g_cfg.custom_msg)) g_cfg.custom_msg[i++] = *tp++;
                g_cfg.custom_msg[i] = '\0';
            }
            g_cfg.show_msg = true;
            app_trigger_state(STATE_WIPE_TO_MSG);
            settings_schedule_save();
            reply = "{\"status\":\"MSG_RECEIVED\"}";
        } else if (strstr(rx_buf, "\"cmd\":\"temps\"") || strstr(rx_buf, "\"cmd\": \"temps\"")) {
            const char *p1 = strstr(rx_buf, "\"t1\":"); if (p1) g_cfg.ext_temps[0] = strtof(p1 + 5, NULL);
            const char *p2 = strstr(rx_buf, "\"t2\":"); if (p2) g_cfg.ext_temps[1] = strtof(p2 + 5, NULL);
            const char *p3 = strstr(rx_buf, "\"t3\":"); if (p3) g_cfg.ext_temps[2] = strtof(p3 + 5, NULL);
            const char *p4 = strstr(rx_buf, "\"t4\":"); if (p4) g_cfg.ext_temps[3] = strtof(p4 + 5, NULL);

            g_cfg.ext_temp_icons[0] = parse_json_icon(rx_buf, "i1", g_cfg.ext_temp_icons[0]);
            g_cfg.ext_temp_icons[1] = parse_json_icon(rx_buf, "i2", g_cfg.ext_temp_icons[1]);
            g_cfg.ext_temp_icons[2] = parse_json_icon(rx_buf, "i3", g_cfg.ext_temp_icons[2]);
            g_cfg.ext_temp_icons[3] = parse_json_icon(rx_buf, "i4", g_cfg.ext_temp_icons[3]);

            app_trigger_state(STATE_WIPE_TO_TEMPS);
            settings_schedule_save();
            reply = "{\"status\":\"TEMPS_RECEIVED\"}";
        } else if (strstr(rx_buf, "\"cmd\":\"info\"") || strstr(rx_buf, "\"cmd\": \"info\"")) {
            char info_buf[440];
            int ilen = snprintf(info_buf, sizeof(info_buf),
                "{\"ip\":\"%s\",\"city\":\"%s\",\"country\":\"%s\",\"region\":\"%s\","
                "\"bright\":%d,\"nbright\":%d,\"nstart\":%d,\"nend\":%d,"
                "\"gmtoff\":%ld,\"dstoff\":%d,\"showClock\":%s,\"showTypewriter\":%s,"
                "\"showSparkle\":%s,\"showScanner\":%s,\"showTemps\":%s,\"showMsg\":%s,"
                "\"i1\":%d,\"i2\":%d,\"i3\":%d,\"i4\":%d}",
                g_ip_str, g_cfg.city, g_cfg.country, g_cfg.region,
                g_cfg.bright, g_cfg.nbright, g_cfg.nstart, g_cfg.nend,
                g_cfg.gmtoff, g_cfg.dstoff,
                g_cfg.show_clock ? "true" : "false",
                g_cfg.show_typewriter ? "true" : "false",
                g_cfg.show_sparkle ? "true" : "false",
                g_cfg.show_scanner ? "true" : "false",
                g_cfg.show_temps ? "true" : "false",
                g_cfg.show_msg ? "true" : "false",
                g_cfg.ext_temp_icons[0], g_cfg.ext_temp_icons[1],
                g_cfg.ext_temp_icons[2], g_cfg.ext_temp_icons[3]);
            sendto(sock, info_buf, ilen, 0, (struct sockaddr *)&client_addr, socklen);
        }
        if (reply) {
            sendto(sock, reply, strlen(reply), 0, (struct sockaddr *)&client_addr, socklen);
        }
    }
}

// ==========================================
// 3. NON-BLOCKING ANIMATIONS & STATE MACHINE
// ==========================================
static bool anim_scroll(const char *txt, bool init) {
    static uint32_t prev_ms = 0;
    static int x = 0;
    int len = strlen(txt);
    if (init) {
        x = VIRTUAL_COLS;
        prev_ms = millis_now();
        return false;
    }
    uint32_t now = millis_now();
    if (now - prev_ms >= 20) {
        prev_ms = now;
        matrix_clear();
        matrix_draw_text_at(x, txt);
        matrix_show();
        x--;
        if (x <= -(len * VIRTUAL_MOD_WIDTH)) return true;
    }
    return false;
}

static bool anim_time_weather(uint32_t duration_ms, const char *weather_msg, bool init) {
    static uint32_t start_ms = 0;
    static uint32_t prev_scroll_ms = 0;
    static uint32_t prev_blink_ms = 0;
    static int scroll_x = 0;
    static bool blink_state = true;

    int screen1_start = matrix_get_virtual_mod_start(1, 0);
    bool night_mode = settings_is_night_time();

    if (init) {
        start_ms = millis_now();
        prev_scroll_ms = start_ms;
        prev_blink_ms = start_ms;
        scroll_x = VIRTUAL_COLS;
        blink_state = true;
        return false;
    }

    uint32_t now_ms = millis_now();
    if (now_ms - start_ms >= duration_ms) {
        if (night_mode) start_ms = now_ms;
        else return true;
    }

    bool needs_redraw = false;
    if (now_ms - prev_blink_ms >= 500) {
        prev_blink_ms = now_ms;
        blink_state = !blink_state;
        needs_redraw = true;
    }

    if (!night_mode) {
        if (now_ms - prev_scroll_ms >= 30) {
            prev_scroll_ms = now_ms;
            scroll_x--;
            int len = strlen(weather_msg);
            if (scroll_x < screen1_start - (len * VIRTUAL_MOD_WIDTH)) {
                scroll_x = VIRTUAL_COLS;
            }
            needs_redraw = true;
        }
    }

    if (needs_redraw) {
        matrix_clear();

        time_t now;
        time(&now);
        struct tm ti;
        gmtime_r(&now, &ti);

        char month_str[4];
        strftime(month_str, sizeof(month_str), "%b", &ti);
        for (int i = 0; i < 3; i++) month_str[i] = (char)toupper((unsigned char)month_str[i]);

        char top_line[16];
        snprintf(top_line, sizeof(top_line), "%02d %s %02d%c%02d",
                 ti.tm_mday, month_str, ti.tm_hour, blink_state ? ':' : ' ', ti.tm_min);
        for (int i = 0; i < 12; i++) {
            matrix_draw_char(0, i + 2, top_line[i]);
        }

        if (night_mode) {
            matrix_draw_string(1, 0, g_night_weather_str);
        } else {
            matrix_draw_text_on_screen(1, scroll_x, weather_msg);
        }
        matrix_show();
    }
    return false;
}

static bool anim_typewriter(const char *msg, bool init) {
    static uint32_t prev_ms = 0;
    static int char_idx = 0;
    static bool waiting = false;
    int len = strlen(msg);

    if (init) {
        matrix_clear();
        matrix_show();
        char_idx = 0;
        waiting = false;
        prev_ms = millis_now();
        return false;
    }
    uint32_t now = millis_now();
    if (waiting) {
        if (now - prev_ms >= 3000) return true;
    } else if (now - prev_ms >= 80) {
        prev_ms = now;
        int start_mod = (TOTAL_MODULES > len) ? (TOTAL_MODULES - len) / 2 : 0;
        int start_x = start_mod * VIRTUAL_MOD_WIDTH;
        char buf[2] = { msg[char_idx], '\0' };
        matrix_draw_text_at(start_x + (char_idx * VIRTUAL_MOD_WIDTH), buf);
        matrix_show();
        char_idx++;
        if (char_idx >= len) waiting = true;
    }
    return false;
}

static bool anim_sparkle(uint32_t speed_ms, bool init) {
    static uint32_t prev_ms = 0;
    static int count = 0;
    if (init) {
        count = 0;
        prev_ms = millis_now();
        return false;
    }
    uint32_t now = millis_now();
    if (now - prev_ms >= speed_ms) {
        prev_ms = now;
        int rx = (int)(esp_random() % VIRTUAL_COLS);
        int ry = (int)(esp_random() % MATRIX_ROWS);
        matrix_set_pixel(rx, ry, 0);
        matrix_show();
        count++;
        if (count >= 800) {
            matrix_clear();
            matrix_show();
            return true;
        }
    }
    return false;
}

static bool anim_scanner(int loops, bool init) {
    static uint32_t prev_ms = 0;
    static int cur_loop = 0;
    static int pos = 0;
    static int dir = 1;
    if (init) {
        cur_loop = 0;
        pos = 0;
        dir = 1;
        prev_ms = millis_now();
        return false;
    }
    uint32_t now = millis_now();
    if (now - prev_ms >= 5) {
        prev_ms = now;
        matrix_clear();
        for (int w = 0; w < 4; w++) {
            for (int h = 0; h < 8; h++) {
                matrix_set_pixel(pos + w, h, 1);
            }
        }
        matrix_show();
        pos += (dir * 2);
        if (dir == 1 && pos >= VIRTUAL_COLS - 4) {
            dir = -1;
            pos = VIRTUAL_COLS - 4;
        } else if (dir == -1 && pos <= 0) {
            dir = 1;
            pos = 0;
            cur_loop++;
            if (cur_loop >= loops) {
                matrix_clear();
                matrix_show();
                return true;
            }
        }
    }
    return false;
}

static bool anim_external_temps(uint32_t duration_ms, bool init) {
    static uint32_t start_ms = 0;
    if (init) {
        matrix_clear();
        matrix_draw_4temps_custom(g_cfg.ext_temps[0], g_cfg.ext_temps[1],
                                  g_cfg.ext_temps[2], g_cfg.ext_temps[3],
                                  g_cfg.ext_temp_icons[0], g_cfg.ext_temp_icons[1],
                                  g_cfg.ext_temp_icons[2], g_cfg.ext_temp_icons[3]);
        matrix_show();
        start_ms = millis_now();
        return false;
    }
    return (millis_now() - start_ms >= duration_ms);
}

static bool anim_night_temps(uint32_t duration_ms, bool init) {
    static uint32_t start_ms = 0;
    static uint32_t prev_blink_ms = 0;
    static bool blink_state = true;
    static int phase = 0;
    static uint32_t prev_phase_ms = 0;

    uint32_t now_ms = millis_now();
    if (init) {
        start_ms = now_ms;
        prev_blink_ms = now_ms;
        prev_phase_ms = now_ms;
        blink_state = true;
        phase = 0;
        matrix_clear_screen(1);
        return false;
    }

    if (now_ms - start_ms >= duration_ms) {
        matrix_clear_screen(1);
        matrix_show();
        return true;
    }

    bool needs_redraw = false;
    if (now_ms - prev_blink_ms >= 500) {
        prev_blink_ms = now_ms;
        blink_state = !blink_state;
        needs_redraw = true;
    }

    bool has_t3_or_t4 = (g_cfg.ext_temps[2] > -90.0f && g_cfg.ext_temps[2] < 99.0f) ||
                        (g_cfg.ext_temps[3] > -90.0f && g_cfg.ext_temps[3] < 99.0f);
    if (has_t3_or_t4 && (now_ms - prev_phase_ms >= 4000)) {
        prev_phase_ms = now_ms;
        phase = (phase == 0) ? 1 : 0;
        needs_redraw = true;
    }

    if (needs_redraw) {
        // Redraw Screen 0 (Clock: day month hour min)
        matrix_clear_screen(0);
        time_t now;
        time(&now);
        struct tm ti;
        gmtime_r(&now, &ti);

        char month_str[4];
        strftime(month_str, sizeof(month_str), "%b", &ti);
        for (int i = 0; i < 3; i++) month_str[i] = (char)toupper((unsigned char)month_str[i]);

        char top_line[16];
        snprintf(top_line, sizeof(top_line), "%02d %s %02d%c%02d",
                 ti.tm_mday, month_str, ti.tm_hour, blink_state ? ':' : ' ', ti.tm_min);
        for (int i = 0; i < 12; i++) {
            matrix_draw_char(0, i + 2, top_line[i]);
        }

        // Redraw Screen 1 (Bottom row: temps + custom icons)
        if (phase == 0 || !has_t3_or_t4) {
            matrix_draw_night_temps(g_cfg.ext_temps[0], g_cfg.ext_temp_icons[0],
                                    g_cfg.ext_temps[1], g_cfg.ext_temp_icons[1]);
        } else {
            matrix_draw_night_temps(g_cfg.ext_temps[2], g_cfg.ext_temp_icons[2],
                                    g_cfg.ext_temps[3], g_cfg.ext_temp_icons[3]);
        }
        matrix_show();
    }
    return false;
}

static bool anim_night_msg(uint32_t duration_ms, const char *msg, bool init) {
    static uint32_t start_ms = 0;
    static uint32_t prev_scroll_ms = 0;
    static uint32_t prev_blink_ms = 0;
    static int scroll_x = 0;
    static bool blink_state = true;

    int screen1_start = matrix_get_virtual_mod_start(1, 0);
    uint32_t now_ms = millis_now();

    if (init) {
        start_ms = now_ms;
        prev_scroll_ms = now_ms;
        prev_blink_ms = now_ms;
        scroll_x = VIRTUAL_COLS;
        blink_state = true;
        matrix_clear_screen(1);
        return false;
    }

    if (now_ms - start_ms >= duration_ms) {
        matrix_clear_screen(1);
        matrix_show();
        return true;
    }

    bool needs_redraw = false;
    if (now_ms - prev_blink_ms >= 500) {
        prev_blink_ms = now_ms;
        blink_state = !blink_state;
        needs_redraw = true;
    }

    if (now_ms - prev_scroll_ms >= 25) {
        prev_scroll_ms = now_ms;
        scroll_x--;
        int len = strlen(msg);
        if (scroll_x < screen1_start - (len * VIRTUAL_MOD_WIDTH)) {
            scroll_x = VIRTUAL_COLS;
        }
        needs_redraw = true;
    }

    if (needs_redraw) {
        // Redraw Screen 0 (Clock: day month hour min)
        matrix_clear_screen(0);
        time_t now;
        time(&now);
        struct tm ti;
        gmtime_r(&now, &ti);

        char month_str[4];
        strftime(month_str, sizeof(month_str), "%b", &ti);
        for (int i = 0; i < 3; i++) month_str[i] = (char)toupper((unsigned char)month_str[i]);

        char top_line[16];
        snprintf(top_line, sizeof(top_line), "%02d %s %02d%c%02d",
                 ti.tm_mday, month_str, ti.tm_hour, blink_state ? ':' : ' ', ti.tm_min);
        for (int i = 0; i < 12; i++) {
            matrix_draw_char(0, i + 2, top_line[i]);
        }

        // Redraw Screen 1 (Scrolled message)
        matrix_clear_screen(1);
        matrix_draw_text_on_screen(1, scroll_x, msg);
        matrix_show();
    }
    return false;
}

static bool anim_wipe_reverse(uint32_t speed_ms, bool init) {
    static uint32_t prev_ms = 0;
    static int col = 0;
    if (init) {
        col = VIRTUAL_COLS - 1;
        prev_ms = millis_now();
        return false;
    }
    uint32_t now = millis_now();
    if (now - prev_ms >= speed_ms) {
        prev_ms = now;
        for (int r = 0; r < MATRIX_ROWS; r++) matrix_set_pixel(col, r, 0);
        matrix_show();
        col--;
        if (col < 0) return true;
    }
    return false;
}

static bool anim_custom_msg_duration(uint32_t duration_ms, const char *msg, bool init) {
    static uint32_t start_ms = 0;
    if (init) {
        start_ms = millis_now();
        anim_scroll(msg, true);
        return false;
    }
    if (anim_scroll(msg, false)) {
        anim_scroll(msg, true);
    }
    return (millis_now() - start_ms >= duration_ms);
}

static void playlist_task(void *pvParameters) {
    while (1) {
        matrix_set_brightness(settings_is_night_time() ? g_cfg.nbright : g_cfg.bright);
        settings_commit_if_needed();

        switch (s_current_state) {
            case STATE_BOOT_IP:
                if (s_init_next_anim) {
                    anim_scroll(g_ip_str, true);
                    s_init_next_anim = false;
                }
                if (anim_scroll(g_ip_str, false)) {
                    s_current_state = STATE_CLOCK;
                    s_init_next_anim = true;
                }
                break;

            case STATE_CLOCK:
                if (!g_cfg.show_clock) {
                    s_current_state = STATE_TYPEWRITER;
                    s_init_next_anim = true;
                    break;
                }
                if (s_init_next_anim) {
                    anim_time_weather(15000, g_live_weather_str, true);
                    s_init_next_anim = false;
                }
                if (anim_time_weather(15000, g_live_weather_str, false)) {
                    s_current_state = STATE_TYPEWRITER;
                    s_init_next_anim = true;
                }
                break;

            case STATE_TYPEWRITER:
                if (!g_cfg.show_typewriter) {
                    s_current_state = STATE_SPARKLE;
                    s_init_next_anim = true;
                    break;
                }
                if (s_init_next_anim) {
                    anim_typewriter("ESP32 OK", true);
                    s_init_next_anim = false;
                }
                if (anim_typewriter("ESP32 OK", false)) {
                    s_current_state = STATE_SPARKLE;
                    s_init_next_anim = true;
                }
                break;

            case STATE_SPARKLE:
                if (!g_cfg.show_sparkle) {
                    s_current_state = STATE_SCANNER;
                    s_init_next_anim = true;
                    break;
                }
                if (s_init_next_anim) {
                    anim_sparkle(2, true);
                    s_init_next_anim = false;
                }
                if (anim_sparkle(2, false)) {
                    s_current_state = STATE_SCANNER;
                    s_init_next_anim = true;
                }
                break;

            case STATE_SCANNER:
                if (!g_cfg.show_scanner) {
                    s_current_state = STATE_TEMPS;
                    s_init_next_anim = true;
                    break;
                }
                if (s_init_next_anim) {
                    anim_scanner(2, true);
                    s_init_next_anim = false;
                }
                if (anim_scanner(2, false)) {
                    s_current_state = STATE_TEMPS;
                    s_init_next_anim = true;
                }
                break;

            case STATE_TEMPS:
                if (!g_cfg.show_temps) {
                    s_current_state = STATE_MESSAGE;
                    s_init_next_anim = true;
                    break;
                }
                if (s_init_next_anim) {
                    anim_external_temps(8000, true);
                    s_init_next_anim = false;
                }
                if (anim_external_temps(8000, false)) {
                    s_current_state = STATE_MESSAGE;
                    s_init_next_anim = true;
                }
                break;

            case STATE_MESSAGE:
                if (!g_cfg.show_msg) {
                    s_current_state = STATE_CLOCK;
                    s_init_next_anim = true;
                    break;
                }
                if (s_init_next_anim) {
                    anim_scroll(g_cfg.custom_msg, true);
                    s_init_next_anim = false;
                }
                if (anim_scroll(g_cfg.custom_msg, false)) {
                    s_current_state = STATE_CLOCK;
                    s_init_next_anim = true;
                }
                break;

            case STATE_WIPE_TO_TEMPS:
                if (s_init_next_anim) {
                    anim_wipe_reverse(5, true);
                    s_init_next_anim = false;
                }
                if (anim_wipe_reverse(5, false)) {
                    s_current_state = STATE_SHOW_TEMPS;
                    s_init_next_anim = true;
                }
                break;

            case STATE_SHOW_TEMPS:
                if (s_init_next_anim) {
                    anim_external_temps(5000, true);
                    s_init_next_anim = false;
                }
                if (anim_external_temps(5000, false)) {
                    s_current_state = STATE_WIPE_TO_CLOCK;
                    s_init_next_anim = true;
                }
                break;

            case STATE_WIPE_TO_MSG:
                if (s_init_next_anim) {
                    anim_wipe_reverse(5, true);
                    s_init_next_anim = false;
                }
                if (anim_wipe_reverse(5, false)) {
                    s_current_state = STATE_SHOW_MSG;
                    s_init_next_anim = true;
                }
                break;

            case STATE_SHOW_MSG:
                if (s_init_next_anim) {
                    anim_custom_msg_duration((uint32_t)g_cfg.custom_msg_duration_ms, g_cfg.custom_msg, true);
                    s_init_next_anim = false;
                }
                if (anim_custom_msg_duration((uint32_t)g_cfg.custom_msg_duration_ms, g_cfg.custom_msg, false)) {
                    s_current_state = STATE_WIPE_TO_CLOCK;
                    s_init_next_anim = true;
                }
                break;

            case STATE_WIPE_TO_CLOCK:
                if (s_init_next_anim) {
                    anim_wipe_reverse(5, true);
                    s_init_next_anim = false;
                }
                if (anim_wipe_reverse(5, false)) {
                    s_current_state = STATE_CLOCK;
                    s_init_next_anim = true;
                }
                break;

            case STATE_NIGHT_TEMPS:
                if (s_init_next_anim) {
                    anim_night_temps(8000, true);
                    s_init_next_anim = false;
                }
                if (anim_night_temps(8000, false)) {
                    s_current_state = STATE_CLOCK;
                    s_init_next_anim = true;
                }
                break;

            case STATE_NIGHT_MSG:
                if (s_init_next_anim) {
                    anim_night_msg((uint32_t)g_cfg.custom_msg_duration_ms, g_cfg.custom_msg, true);
                    s_init_next_anim = false;
                }
                if (anim_night_msg((uint32_t)g_cfg.custom_msg_duration_ms, g_cfg.custom_msg, false)) {
                    s_current_state = STATE_CLOCK;
                    s_init_next_anim = true;
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
    }
}

// ==========================================
// 4. WI-FI INITIALIZATION
// ==========================================
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
    snprintf(g_ip_str, sizeof(g_ip_str), "AP: 192.168.4.1");
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_sta_connected = false;
            s_sta_retry_count++;
            if (s_sta_retry_count >= 6 && !s_ap_fallback_active) {
                enable_ap_fallback();
            }
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(g_ip_str, sizeof(g_ip_str), "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_sta_connected = true;
        s_sta_retry_count = 0;
        s_weather_refresh_req = true;
        ESP_LOGI(TAG, "Connected! %s", g_ip_str);
    }
}

static void wifi_init_fast_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t sta_config = {
        .sta = {
            .ssid = STA_SSID,
            .password = STA_PASS,
            .threshold = { .authmode = WIFI_AUTH_WPA2_PSK },
            .pmf_cfg = { .capable = true, .required = false },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

void app_main(void) {
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "  MATRIX CLOCK: FULL FEATURES ESP-IDF v3  ");
    ESP_LOGI(TAG, "==========================================");

    settings_init();
    settings_load_all();

    matrix_init();
    matrix_clear();
    matrix_draw_text_at(18, "WIFI...");
    matrix_show();

    // Set initial clock time if unset (2026-10-28 18:05:00 local)
    time_t now;
    time(&now);
    if (now < 1700000000) {
        struct timeval tv = { .tv_sec = 1793210700, .tv_usec = 0 };
        settimeofday(&tv, NULL);
    }

    wifi_init_fast_sta();
    webserver_start();

    xTaskCreate(playlist_task,   "PlaylistTask", 4096, NULL, 3, NULL);
    xTaskCreate(weather_task,    "WeatherTask",  6144, NULL, 2, NULL);
    xTaskCreate(udp_server_task, "UdpServer",    4096, NULL, 2, NULL);
}
