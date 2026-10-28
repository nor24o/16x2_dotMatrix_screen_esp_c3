#include "webserver.h"
#include "matrix.h"
#include "settings.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "WEBSERVER";
static httpd_handle_t s_server = NULL;

static inline void set_fast_socket(httpd_req_t *req) {
    int fd = httpd_req_to_sockfd(req);
    if (fd >= 0) {
        int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    }
}

static const char INDEX_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Matrix Controller</title>"
"<style>"
":root{--bg:#0f172a;--card:#1e293b;--acc:#3b82f6;--txt:#f8fafc;--mut:#94a3b8}"
"*{box-sizing:border-box;font-family:system-ui,-apple-system,sans-serif}"
"body{background:var(--bg);color:var(--txt);margin:0;padding:20px 12px;display:flex;justify-content:center}"
".card{background:var(--card);border-radius:16px;padding:20px;width:100%;max-width:380px;box-shadow:0 10px 25px rgba(0,0,0,.4)}"
"h1{font-size:20px;margin:0 0 4px;text-align:center}"
".sub{text-align:center;font-size:12px;color:var(--mut);margin-bottom:16px}"
".clk{background:#0f172a;border-radius:12px;padding:14px;text-align:center;margin-bottom:14px;border:1px solid #334155}"
".time{font-family:monospace;font-size:30px;font-weight:700;letter-spacing:2px;color:#38bdf8}"
".lbl{font-size:11px;text-transform:uppercase;color:var(--mut);margin-top:4px}"
".btn{background:var(--acc);color:#fff;border:none;border-radius:10px;padding:12px;width:100%;font-size:14px;font-weight:600;cursor:pointer}"
".btn:active{transform:scale(.98)}"
".btn2{background:#334155;color:#cbd5e1;font-size:12px;padding:6px 12px;border-radius:8px;border:none;cursor:pointer}"
".sec{font-size:11px;text-transform:uppercase;letter-spacing:1px;color:var(--mut);margin:18px 0 6px}"
".row{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid #334155}"
".sw{position:relative;display:inline-block;width:44px;height:24px}"
".sw input{opacity:0;width:0;height:0}"
".sl{position:absolute;cursor:pointer;inset:0;background:#475569;border-radius:24px;transition:.2s}"
".sl:before{position:absolute;content:'';height:18px;width:18px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.2s}"
"input:checked+.sl{background:var(--acc)}"
"input:checked+.sl:before{transform:translateX(20px)}"
".rh{display:flex;justify-content:space-between;font-size:14px;margin:6px 0}"
"input[type=range]{width:100%;height:6px;border-radius:3px;background:#334155;outline:none;-webkit-appearance:none;cursor:pointer}"
"input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:var(--acc)}"
"#toast{text-align:center;margin-top:12px;font-size:13px;font-weight:500;min-height:18px}"
".ok{color:#4ade80}.err{color:#f87171}"
"</style></head><body>"
"<div class='card'>"
"<h1>Matrix Controller</h1>"
"<div class='sub'>ESP-IDF FreeRTOS &bull; 104Hz Zero-Flicker</div>"
"<div class='clk'><div id='clockDisplay' class='time'>--:--:--</div><div class='lbl'>Device Time</div></div>"
"<button class='btn' onclick='syncTime(false)'>Sync Browser Time</button>"
"<div class='sec'>Orientation</div>"
"<div class='row'><span>Flip Horizontal</span><label class='sw'><input type='checkbox' id='fliph' onchange='save()'><span class='sl'></span></label></div>"
"<div class='row'><span>Flip Vertical</span><label class='sw'><input type='checkbox' id='flipv' onchange='save()'><span class='sl'></span></label></div>"
"<div class='row'><span>Rotate 180&deg;</span><button class='btn2' onclick='rot180()'>Toggle 180&deg;</button></div>"
"<div class='sec'>Brightness</div>"
"<div class='rh'><span>Level</span><span id='brightVal'>--%</span></div>"
"<input type='range' id='brightness' min='10' max='800' step='10' oninput='slide()' onchange='save()'>"
"<div id='toast'></div>"
"</div>"
"<script>"
"let tmr=null,busy=false,queued=false,devSec=0;"
"function fmt(s){const d=new Date(s*1000);return d.toISOString().substr(11,8);}"
"setInterval(()=>{if(devSec>0){devSec++;document.getElementById('clockDisplay').innerText=fmt(devSec);}},1000);"
"function toast(m,ok=true){const t=document.getElementById('toast');t.innerText=m;t.className=ok?'ok':'err';clearTimeout(t.k);t.k=setTimeout(()=>t.innerText='',2500);}"
"function apply(d){"
"document.getElementById('fliph').checked=d.fliph;"
"document.getElementById('flipv').checked=d.flipv;"
"document.getElementById('brightness').value=d.brightness;"
"document.getElementById('brightVal').innerText=Math.round((d.brightness/800)*100)+'%';"
"if(d.epoch)devSec=d.epoch;"
"if(d.time)document.getElementById('clockDisplay').innerText=d.time;"
"}"
"window.onload=()=>{"
"fetch('/api/settings').then(r=>r.json()).then(d=>{apply(d);if(d.epoch<1700000000)syncTime(true);}).catch(()=>toast('Offline',false));"
"};"
"function slide(){"
"const v=document.getElementById('brightness').value;"
"document.getElementById('brightVal').innerText=Math.round((v/800)*100)+'%';"
"clearTimeout(tmr);tmr=setTimeout(save,60);"
"}"
"function rot180(){"
"const h=document.getElementById('fliph'),v=document.getElementById('flipv'),n=!(h.checked&&v.checked);"
"h.checked=n;v.checked=n;save();"
"}"
"function save(){"
"clearTimeout(tmr);"
"if(busy){queued=true;return;}"
"busy=true;queued=false;"
"fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({"
"fliph:document.getElementById('fliph').checked,"
"flipv:document.getElementById('flipv').checked,"
"brightness:parseInt(document.getElementById('brightness').value)"
"})}).then(r=>r.json()).then(d=>{busy=false;toast('Saved');if(queued)save();}).catch(()=>{busy=false;toast('Error',false);});"
"}"
"function syncTime(silent){"
"const n=new Date(),ep=Math.floor(n.getTime()/1000)-(n.getTimezoneOffset()*60);"
"fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch:ep})})"
".then(r=>r.json()).then(d=>{apply(d);if(!silent)toast('Synced: '+d.time);}).catch(()=>{if(!silent)toast('Sync error',false);});"
"}"
"</script></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
    set_fast_socket(req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, INDEX_HTML, sizeof(INDEX_HTML) - 1);
}

static esp_err_t favicon_get_handler(httpd_req_t *req) {
    set_fast_socket(req);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    set_fast_socket(req);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t settings_get_handler(httpd_req_t *req) {
    set_fast_socket(req);
    bool fh, fv;
    matrix_get_flip(&fh, &fv);
    int br = matrix_get_brightness();

    time_t now;
    time(&now);
    struct tm ti;
    gmtime_r(&now, &ti);

    char resp[192];
    int len = snprintf(resp, sizeof(resp),
             "{\"fliph\":%s,\"flipv\":%s,\"brightness\":%d,\"time\":\"%02d:%02d:%02d\",\"epoch\":%ld}",
             fh ? "true" : "false",
             fv ? "true" : "false",
             br,
             ti.tm_hour, ti.tm_min, ti.tm_sec,
             (long)now);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, len);
}

static esp_err_t settings_post_handler(httpd_req_t *req) {
    set_fast_socket(req);
    char buf[256];
    int remaining = req->content_len;
    if (remaining >= (int)sizeof(buf)) remaining = sizeof(buf) - 1;

    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

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
            if (b >= 10 && b <= 800) br = b;
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

static esp_err_t flip_post_handler(httpd_req_t *req) {
    set_fast_socket(req);
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
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t sync_post_handler(httpd_req_t *req) {
    set_fast_socket(req);
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
    return httpd_resp_send(req, "Synced", 6);
}

esp_err_t webserver_start(void) {
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.task_priority = 6;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 16;
    config.backlog_conn = 6;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting Fast HTTP Server on port %d...", config.server_port);
    esp_err_t err = httpd_start(&s_server, &config);
    if (err == ESP_OK) {
        const httpd_uri_t uris[] = {
            { .uri = "/",                    .method = HTTP_GET,  .handler = root_get_handler },
            { .uri = "/index.html",          .method = HTTP_GET,  .handler = root_get_handler },
            { .uri = "/favicon.ico",         .method = HTTP_GET,  .handler = favicon_get_handler },
            { .uri = "/api/settings",        .method = HTTP_GET,  .handler = settings_get_handler },
            { .uri = "/api/settings",        .method = HTTP_POST, .handler = settings_post_handler },
            { .uri = "/status",              .method = HTTP_GET,  .handler = settings_get_handler },
            { .uri = "/flip",                .method = HTTP_POST, .handler = flip_post_handler },
            { .uri = "/sync",                .method = HTTP_POST, .handler = sync_post_handler },
            { .uri = "/generate_204",        .method = HTTP_GET,  .handler = captive_redirect_handler },
            { .uri = "/hotspot-detect.html", .method = HTTP_GET,  .handler = captive_redirect_handler },
            { .uri = "/ncsi.txt",            .method = HTTP_GET,  .handler = captive_redirect_handler },
            { .uri = "/connecttest.txt",     .method = HTTP_GET,  .handler = captive_redirect_handler },
        };
        for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
            httpd_register_uri_handler(s_server, &uris[i]);
        }
        ESP_LOGI(TAG, "Fast HTTP Server started (%u bytes HTML).", (unsigned)(sizeof(INDEX_HTML) - 1));
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to start HTTP server: 0x%x (%s)", err, esp_err_to_name(err));
    return err;
}

void webserver_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
