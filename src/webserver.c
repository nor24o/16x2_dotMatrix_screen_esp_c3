#include "webserver.h"
#include "matrix.h"
#include "settings.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "WEBSERVER";
static httpd_handle_t s_server = NULL;

static const char INDEX_HTML[] = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"  <meta charset='utf-8'>"
"  <meta name='viewport' content='width=device-width, initial-scale=1'>"
"  <title>Matrix Clock (ESP-IDF RTOS)</title>"
"  <style>"
"    :root { --bg: #0f172a; --card: #1e293b; --accent: #3b82f6; --text: #f8fafc; --muted: #94a3b8; }"
"    * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }"
"    body { background: var(--bg); color: var(--text); margin: 0; padding: 24px 14px; display: flex; justify-content: center; }"
"    .card { background: var(--card); border-radius: 16px; padding: 22px; width: 100%; max-width: 380px; box-shadow: 0 10px 25px rgba(0,0,0,0.4); }"
"    h1 { font-size: 20px; font-weight: 700; margin: 0 0 4px 0; text-align: center; }"
"    .subtitle { text-align: center; font-size: 13px; color: var(--muted); margin-bottom: 18px; }"
"    .clock-badge { background: #0f172a; border-radius: 12px; padding: 14px; text-align: center; margin-bottom: 18px; border: 1px solid #334155; }"
"    .clock-time { font-family: monospace; font-size: 30px; font-weight: bold; letter-spacing: 2px; color: #38bdf8; }"
"    .clock-label { font-size: 11px; text-transform: uppercase; color: var(--muted); margin-top: 4px; }"
"    .btn { background: var(--accent); color: white; border: none; border-radius: 10px; padding: 12px; width: 100%; font-size: 14px; font-weight: 600; cursor: pointer; transition: background 0.2s, transform 0.1s; }"
"    .btn:active { transform: scale(0.98); }"
"    .btn-secondary { background: #334155; color: #cbd5e1; font-size: 12px; padding: 6px 12px; border-radius: 8px; border: none; cursor: pointer; }"
"    .section-title { font-size: 11px; text-transform: uppercase; letter-spacing: 1px; color: var(--muted); margin: 20px 0 8px; }"
"    .control-row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #334155; }"
"    .control-row:last-child { border-bottom: none; }"
"    .switch { position: relative; display: inline-block; width: 44px; height: 24px; }"
"    .switch input { opacity: 0; width: 0; height: 0; }"
"    .slider-toggle { position: absolute; cursor: pointer; inset: 0; background-color: #475569; border-radius: 24px; transition: .25s; }"
"    .slider-toggle:before { position: absolute; content: ''; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; border-radius: 50%; transition: .25s; }"
"    input:checked + .slider-toggle { background-color: var(--accent); }"
"    input:checked + .slider-toggle:before { transform: translateX(20px); }"
"    .range-wrap { margin-top: 8px; }"
"    .range-header { display: flex; justify-content: space-between; font-size: 14px; margin-bottom: 6px; }"
"    input[type=range] { width: 100%; height: 6px; border-radius: 3px; background: #334155; outline: none; -webkit-appearance: none; cursor: pointer; }"
"    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: var(--accent); cursor: pointer; }"
"    #toast { text-align: center; margin-top: 14px; font-size: 13px; font-weight: 500; min-height: 20px; }"
"    .toast-ok { color: #4ade80; }"
"    .toast-err { color: #f87171; }"
"  </style>"
"</head>"
"<body>"
"  <div class='card'>"
"    <h1>Matrix Controller</h1>"
"    <div class='subtitle'>ESP-IDF FreeRTOS &bull; 16x2 Display</div>"
"    <div class='clock-badge'>"
"      <div id='clockDisplay' class='clock-time'>--:--:--</div>"
"      <div class='clock-label'>Device Time</div>"
"    </div>"
"    <button class='btn' onclick='syncBrowserTime()'>Sync Browser Time</button>"
"    <div class='section-title'>Orientation</div>"
"    <div class='control-row'>"
"      <span>Flip Horizontal</span>"
"      <label class='switch'><input type='checkbox' id='fliph' onchange='onSettingChange()'><span class='slider-toggle'></span></label>"
"    </div>"
"    <div class='control-row'>"
"      <span>Flip Vertical</span>"
"      <label class='switch'><input type='checkbox' id='flipv' onchange='onSettingChange()'><span class='slider-toggle'></span></label>"
"    </div>"
"    <div class='control-row'>"
"      <span>Rotate 180&deg;</span>"
"      <button class='btn-secondary' onclick='rotate180()'>Toggle 180&deg;</button>"
"    </div>"
"    <div class='section-title'>Brightness</div>"
"    <div class='range-wrap'>"
"      <div class='range-header'>"
"        <span>Level</span>"
"        <span id='brightVal'>--%</span>"
"      </div>"
"      <input type='range' id='brightness' min='10' max='1300' step='10' oninput='onBrightnessSlide()' onchange='onSettingChange()'>"
"    </div>"
"    <div id='toast'></div>"
"  </div>"
"  <script>"
"    let sendTimeout = null;"
"    let isSending = false;"
"    window.onload = function() {"
"      loadSettings(true);"
"      setInterval(pollClock, 2000);"
"    };"
"    function showToast(msg, isOk = true) {"
"      const t = document.getElementById('toast');"
"      t.innerText = msg;"
"      t.className = isOk ? 'toast-ok' : 'toast-err';"
"      clearTimeout(t.timer);"
"      t.timer = setTimeout(() => { t.innerText = ''; }, 3000);"
"    }"
"    function loadSettings(autoSyncTime = false) {"
"      fetch('/api/settings')"
"        .then(r => r.json())"
"        .then(d => {"
"          document.getElementById('fliph').checked = d.fliph;"
"          document.getElementById('flipv').checked = d.flipv;"
"          document.getElementById('brightness').value = d.brightness;"
"          document.getElementById('brightVal').innerText = Math.round((d.brightness / 1300) * 100) + '%';"
"          if (d.time) document.getElementById('clockDisplay').innerText = d.time;"
"          if (autoSyncTime) syncBrowserTime(true);"
"        })"
"        .catch(() => showToast('Connecting...', false));"
"    }"
"    function pollClock() {"
"      fetch('/api/settings')"
"        .then(r => r.json())"
"        .then(d => {"
"          if (d.time) document.getElementById('clockDisplay').innerText = d.time;"
"        })"
"        .catch(() => {});"
"    }"
"    function onBrightnessSlide() {"
"      const val = document.getElementById('brightness').value;"
"      document.getElementById('brightVal').innerText = Math.round((val / 1300) * 100) + '%';"
"      clearTimeout(sendTimeout);"
"      sendTimeout = setTimeout(onSettingChange, 120);"
"    }"
"    function rotate180() {"
"      const h = document.getElementById('fliph');"
"      const v = document.getElementById('flipv');"
"      const next = !(h.checked && v.checked);"
"      h.checked = next;"
"      v.checked = next;"
"      onSettingChange();"
"    }"
"    function onSettingChange() {"
"      clearTimeout(sendTimeout);"
"      if (isSending) {"
"        sendTimeout = setTimeout(onSettingChange, 100);"
"        return;"
"      }"
"      isSending = true;"
"      const payload = {"
"        fliph: document.getElementById('fliph').checked,"
"        flipv: document.getElementById('flipv').checked,"
"        brightness: parseInt(document.getElementById('brightness').value)"
"      };"
"      fetch('/api/settings', {"
"        method: 'POST',"
"        headers: { 'Content-Type': 'application/json' },"
"        body: JSON.stringify(payload)"
"      })"
"      .then(r => r.json())"
"      .then(d => {"
"        isSending = false;"
"        showToast('Settings saved');"
"      })"
"      .catch(() => {"
"        isSending = false;"
"        showToast('Save failed', false);"
"      });"
"    }"
"    function syncBrowserTime(silent = false) {"
"      const now = new Date();"
"      const localEpoch = Math.floor(now.getTime() / 1000) - (now.getTimezoneOffset() * 60);"
"      fetch('/api/settings', {"
"        method: 'POST',"
"        headers: { 'Content-Type': 'application/json' },"
"        body: JSON.stringify({ epoch: localEpoch })"
"      })"
"      .then(r => r.json())"
"      .then(d => {"
"        if (d.time) document.getElementById('clockDisplay').innerText = d.time;"
"        if (!silent) showToast('Time synced: ' + d.time);"
"      })"
"      .catch(() => {"
"        if (!silent) showToast('Sync failed', false);"
"      });"
"    }"
"  </script>"
"</body>"
"</html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_get_handler(httpd_req_t *req) {
    bool fh, fv;
    matrix_get_flip(&fh, &fv);
    int br = matrix_get_brightness();

    time_t now;
    time(&now);
    struct tm ti;
    gmtime_r(&now, &ti);

    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"fliph\":%s,\"flipv\":%s,\"brightness\":%d,\"time\":\"%02d:%02d:%02d\",\"epoch\":%ld}",
             fh ? "true" : "false",
             fv ? "true" : "false",
             br,
             ti.tm_hour, ti.tm_min, ti.tm_sec,
             (long)now);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf)) remaining = sizeof(buf) - 1;

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Simple robust parsing for JSON or form params
    bool fh, fv;
    matrix_get_flip(&fh, &fv);
    int br = matrix_get_brightness();

    if (strstr(buf, "\"fliph\":true") || strstr(buf, "fliph=1") || strstr(buf, "fliph=true")) {
        fh = true;
    } else if (strstr(buf, "\"fliph\":false") || strstr(buf, "fliph=0") || strstr(buf, "fliph=false")) {
        fh = false;
    }

    if (strstr(buf, "\"flipv\":true") || strstr(buf, "flipv=1") || strstr(buf, "flipv=true")) {
        fv = true;
    } else if (strstr(buf, "\"flipv\":false") || strstr(buf, "flipv=0") || strstr(buf, "flipv=false")) {
        fv = false;
    }

    char *bright_ptr = strstr(buf, "\"brightness\":");
    if (!bright_ptr) bright_ptr = strstr(buf, "brightness=");
    if (bright_ptr) {
        while (*bright_ptr && (*bright_ptr < '0' || *bright_ptr > '9')) bright_ptr++;
        if (*bright_ptr) {
            int b = atoi(bright_ptr);
            if (b >= 10 && b <= 1300) br = b;
        }
    }

    char *epoch_ptr = strstr(buf, "\"epoch\":");
    if (!epoch_ptr) epoch_ptr = strstr(buf, "epoch=");
    if (epoch_ptr) {
        while (*epoch_ptr && (*epoch_ptr < '0' || *epoch_ptr > '9')) epoch_ptr++;
        if (*epoch_ptr) {
            long ep = atol(epoch_ptr);
            if (ep > 100000) {
                struct timeval tv = { .tv_sec = ep, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                ESP_LOGI(TAG, "Clock synced via API to epoch: %ld", ep);
            }
        }
    }

    matrix_set_flip(fh, fv);
    matrix_set_brightness(br);
    settings_schedule_save();

    return settings_get_handler(req);
}

// Backwards compatibility endpoint handlers
static esp_err_t flip_post_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        bool fh, fv;
        matrix_get_flip(&fh, &fv);
        if (httpd_query_key_value(query, "h", val, sizeof(val)) == ESP_OK) {
            fh = (strcmp(val, "1") == 0);
        }
        if (httpd_query_key_value(query, "v", val, sizeof(val)) == ESP_OK) {
            fv = (strcmp(val, "1") == 0);
        }
        matrix_set_flip(fh, fv);
        settings_schedule_save();
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t sync_post_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[32];
        if (httpd_query_key_value(query, "epoch", val, sizeof(val)) == ESP_OK) {
            long ep = atol(val);
            if (ep > 100000) {
                struct timeval tv = { .tv_sec = ep, .tv_usec = 0 };
                settimeofday(&tv, NULL);
            }
        }
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Synced", HTTPD_RESP_USE_STRLEN);
}

esp_err_t webserver_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting HTTP Server on port %d...", config.server_port);
    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_server, &root_uri);

        httpd_uri_t settings_get_uri = {
            .uri = "/api/settings",
            .method = HTTP_GET,
            .handler = settings_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_server, &settings_get_uri);

        httpd_uri_t settings_post_uri = {
            .uri = "/api/settings",
            .method = HTTP_POST,
            .handler = settings_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_server, &settings_post_uri);

        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = settings_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_server, &status_uri);

        httpd_uri_t flip_uri = {
            .uri = "/flip",
            .method = HTTP_POST,
            .handler = flip_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_server, &flip_uri);

        httpd_uri_t sync_uri = {
            .uri = "/sync",
            .method = HTTP_POST,
            .handler = sync_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_server, &sync_uri);

        ESP_LOGI(TAG, "HTTP Server started successfully.");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to start HTTP server!");
    return ESP_FAIL;
}

void webserver_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
