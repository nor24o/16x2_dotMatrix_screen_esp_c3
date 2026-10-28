#include "webserver.h"
#include "matrix.h"
#include "settings.h"
#include <stdio.h>
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
"<title>Matrix Clock Controller</title>"
"<style>"
":root{--bg:#0f172a;--card:#1e293b;--acc:#3b82f6;--acc2:#10b981;--txt:#f8fafc;--mut:#94a3b8;--brd:#334155}"
"*{box-sizing:border-box;font-family:system-ui,-apple-system,sans-serif}"
"body{background:var(--bg);color:var(--txt);margin:0;padding:16px 10px;display:flex;justify-content:center}"
".wrap{width:100%;max-width:480px;display:flex;flex-direction:column;gap:14px}"
".card{background:var(--card);border-radius:14px;padding:16px;box-shadow:0 8px 20px rgba(0,0,0,.35);border:1px solid var(--brd)}"
"h1{font-size:19px;margin:0 0 2px;text-align:center}"
"h2{font-size:12px;text-transform:uppercase;letter-spacing:1px;color:#38bdf8;margin:0 0 10px}"
".sub{text-align:center;font-size:12px;color:var(--mut);margin-bottom:12px}"
".clk{background:#0f172a;border-radius:10px;padding:12px;text-align:center;margin-bottom:10px;border:1px solid var(--brd)}"
".time{font-family:monospace;font-size:28px;font-weight:700;letter-spacing:2px;color:#38bdf8}"
".wth{font-family:monospace;font-size:12px;color:#4ade80;margin-top:4px;word-break:break-word}"
".btn{background:var(--acc);color:#fff;border:none;border-radius:8px;padding:10px 14px;width:100%;font-size:13px;font-weight:600;cursor:pointer}"
".btn:active{transform:scale(.98)}"
".btn-g{background:var(--acc2)}"
".btn-sm{background:#334155;color:#e2e8f0;font-size:11px;padding:5px 10px;border-radius:6px;border:none;cursor:pointer;font-weight:600}"
".btn-sm:hover{background:#475569}"
".row{display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:1px solid #263349;gap:8px}"
".row:last-child{border-bottom:none}"
".sw{position:relative;display:inline-block;width:42px;height:22px;flex-shrink:0}"
".sw input{opacity:0;width:0;height:0}"
".sl{position:absolute;cursor:pointer;inset:0;background:#475569;border-radius:22px;transition:.2s}"
".sl:before{position:absolute;content:'';height:16px;width:16px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.2s}"
"input:checked+.sl{background:var(--acc)}"
"input:checked+.sl:before{transform:translateX(20px)}"
".rh{display:flex;justify-content:space-between;font-size:13px;margin:6px 0 4px}"
"input[type=range]{width:100%;height:6px;border-radius:3px;background:#334155;outline:none;-webkit-appearance:none;cursor:pointer;margin-bottom:6px}"
"input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:var(--acc)}"
"input[type=text],input[type=number],select{background:#0f172a;color:#fff;border:1px solid var(--brd);border-radius:7px;padding:7px 9px;font-size:13px;width:100%}"
".grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}"
".grid3{display:grid;grid-template-columns:2fr 1fr 2fr;gap:6px;margin-bottom:8px}"
".lbl{font-size:11px;color:var(--mut);margin-bottom:3px;display:block}"
"#toast{position:fixed;bottom:14px;left:50%;transform:translateX(-50%);background:#0f172a;border:1px solid var(--acc);padding:8px 18px;border-radius:20px;font-size:13px;font-weight:600;display:none;z-index:99;box-shadow:0 4px 12px rgba(0,0,0,.5)}"
".ok{color:#4ade80;border-color:#4ade80!important}.err{color:#f87171;border-color:#f87171!important}"
"</style></head><body><div class='wrap'>"

"<div class='card'>"
"<h1>Matrix Clock Controller</h1>"
"<div class='sub' id='ipSub'>ESP-IDF FreeRTOS &bull; 104Hz Zero-Flicker</div>"
"<div class='clk'>"
"<div id='clockDisplay' class='time'>--:--:--</div>"
"<div id='weatherDisplay' class='wth'>LOADING WEATHER...</div>"
"</div>"
"<div class='grid2'>"
"<div><span class='lbl'>GMT Offset (Hours)</span><input type='number' id='gmth' min='-12' max='14' value='2' onchange='saveCfg()'></div>"
"<div><span class='lbl'>Daylight Saving</span><select id='dsth' onchange='saveCfg()'><option value='0'>Winter (+0h)</option><option value='3600'>Summer (+1h)</option></select></div>"
"</div>"
"<button class='btn' onclick='syncTime(false)'>Sync Time from Browser</button>"
"</div>"

"<div class='card'>"
"<h2>Custom Alert &amp; Scrolling Message</h2>"
"<div class='grid2' style='grid-template-columns:3fr 1fr'>"
"<div><span class='lbl'>Message Text</span><input type='text' id='cmsg' maxlength='100' placeholder='HELLO'></div>"
"<div><span class='lbl'>Seconds</span><input type='number' id='cdur' min='2' max='300' value='10'></div>"
"</div>"
"<button class='btn btn-g' onclick='sendMsgNow()'>Send &amp; Show Message Now</button>"
"</div>"

"<div class='card'>"
"<h2>Playlist &amp; Animations</h2>"
"<div class='row'><span>1. Clock &amp; Live Weather</span><div><button class='btn-sm' onclick='jump(1)'>Show</button> <label class='sw'><input type='checkbox' id='sClk' onchange='savePlay()'><span class='sl'></span></label></div></div>"
"<div class='row'><span>2. Typewriter (\"ESP32 OK\")</span><div><button class='btn-sm' onclick='jump(2)'>Show</button> <label class='sw'><input type='checkbox' id='sTyp' onchange='savePlay()'><span class='sl'></span></label></div></div>"
"<div class='row'><span>3. Sparkle Dissolve</span><div><button class='btn-sm' onclick='jump(3)'>Show</button> <label class='sw'><input type='checkbox' id='sSprk' onchange='savePlay()'><span class='sl'></span></label></div></div>"
"<div class='row'><span>4. Cylon Block Scanner</span><div><button class='btn-sm' onclick='jump(4)'>Show</button> <label class='sw'><input type='checkbox' id='sScan' onchange='savePlay()'><span class='sl'></span></label></div></div>"
"<div class='row'><span>5. 4-Quadrant Temperatures</span><div><button class='btn-sm' onclick='jump(5)'>Show</button> <label class='sw'><input type='checkbox' id='sTmp' onchange='savePlay()'><span class='sl'></span></label></div></div>"
"<div class='row'><span>6. Custom Message in Loop</span><div><button class='btn-sm' onclick='jump(6)'>Show</button> <label class='sw'><input type='checkbox' id='sMsg' onchange='savePlay()'><span class='sl'></span></label></div></div>"
"</div>"

"<div class='card'>"
"<h2>Brightness, Night Schedule &amp; Orientation</h2>"
"<div class='rh'><span>Daytime Brightness</span><span id='bVal'>--%</span></div>"
"<input type='range' id='bright' min='10' max='800' step='10' oninput='slideB()' onchange='saveCfg()'>"
"<div class='rh'><span>Nighttime Brightness</span><span id='nbVal'>--%</span></div>"
"<input type='range' id='nbright' min='5' max='400' step='5' oninput='slideNB()' onchange='saveCfg()'>"
"<div class='grid2'>"
"<div><span class='lbl'>Night Start Hour (0-23)</span><input type='number' id='nstart' min='0' max='23' value='22' onchange='saveCfg()'></div>"
"<div><span class='lbl'>Night End Hour (0-23)</span><input type='number' id='nend' min='0' max='23' value='7' onchange='saveCfg()'></div>"
"</div>"
"<div class='row'><span>Flip Horizontal</span><label class='sw'><input type='checkbox' id='fliph' onchange='saveCfg()'><span class='sl'></span></label></div>"
"<div class='row'><span>Flip Vertical</span><label class='sw'><input type='checkbox' id='flipv' onchange='saveCfg()'><span class='sl'></span></label></div>"
"<div class='row'><span>Rotate 180&deg;</span><button class='btn-sm' onclick='rot180()'>Toggle 180&deg;</button></div>"
"</div>"

"<div class='card'>"
"<h2>Open-Meteo Weather Location</h2>"
"<div class='grid3'>"
"<div><span class='lbl'>City</span><input type='text' id='city' maxlength='30'></div>"
"<div><span class='lbl'>Country</span><input type='text' id='country' maxlength='3'></div>"
"<div><span class='lbl'>Region</span><input type='text' id='region' maxlength='30'></div>"
"</div>"
"<button class='btn' onclick='saveWeather()'>Save Location &amp; Fetch Weather</button>"
"</div>"

"<div class='card'>"
"<h2>External 4-Quadrant Temperatures (&deg;C)</h2>"
"<div class='grid2'>"
"<div><span class='lbl'>T1 Top-Left (Sun)</span><input type='number' step='0.1' id='t1' value='24.5'></div>"
"<div><span class='lbl'>T2 Top-Right (Cloud)</span><input type='number' step='0.1' id='t2' value='18.2'></div>"
"<div><span class='lbl'>T3 Bottom-Left (Rain)</span><input type='number' step='0.1' id='t3' value='12.0'></div>"
"<div><span class='lbl'>T4 Bottom-Right (Storm)</span><input type='number' step='0.1' id='t4' value='-5.5'></div>"
"</div>"
"<button class='btn btn-g' onclick='sendTemps()'>Push Temps to Screen Now</button>"
"</div>"

"</div><div id='toast'></div>"
"<script>"
"let tmr=null,busy=false,queued=false,devSec=0;"
"const $=id=>document.getElementById(id);"
"function fmt(s){const d=new Date(s*1000);return d.toISOString().substr(11,8);}"
"setInterval(()=>{if(devSec>0){devSec++;$('clockDisplay').innerText=fmt(devSec);}},1000);"
"function toast(m,ok=true){const t=$('toast');t.innerText=m;t.className=ok?'ok':'err';t.style.display='block';clearTimeout(t.k);t.k=setTimeout(()=>t.style.display='none',2200);}"
"function apply(d){"
"$('fliph').checked=d.fliph;$('flipv').checked=d.flipv;"
"$('bright').value=d.bright;$('bVal').innerText=Math.round((d.bright/800)*100)+'%';"
"$('nbright').value=d.nbright;$('nbVal').innerText=Math.round((d.nbright/800)*100)+'%';"
"$('nstart').value=d.nstart;$('nend').value=d.nend;"
"$('gmth').value=Math.round(d.gmtoff/3600);$('dsth').value=d.dstoff;"
"$('city').value=d.city||'';$('country').value=d.country||'';$('region').value=d.region||'';"
"$('sClk').checked=d.sClk;$('sTyp').checked=d.sTyp;$('sSprk').checked=d.sSprk;"
"$('sScan').checked=d.sScan;$('sTmp').checked=d.sTmp;$('sMsg').checked=d.sMsg;"
"$('cmsg').value=d.cmsg||'';$('cdur').value=Math.round((d.cdur||10000)/1000);"
"if(d.t1!==undefined){$('t1').value=d.t1;$('t2').value=d.t2;$('t3').value=d.t3;$('t4').value=d.t4;}"
"if(d.epoch)devSec=d.epoch;"
"if(d.time)$('clockDisplay').innerText=d.time;"
"if(d.weather)$('weatherDisplay').innerText=d.weather;"
"if(d.ip)$('ipSub').innerText=d.ip+' • 104Hz Zero-Flicker';"
"}"
"window.onload=()=>{fetch('/api/settings').then(r=>r.json()).then(d=>{apply(d);}).catch(()=>toast('Offline',false));};"
"function post(body,msg){"
"return fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})"
".then(r=>r.json()).then(d=>{apply(d);if(msg)toast(msg);}).catch(()=>toast('Error',false));"
"}"
"function slideB(){$('bVal').innerText=Math.round(($('bright').value/800)*100)+'%';clearTimeout(tmr);tmr=setTimeout(saveCfg,70);}"
"function slideNB(){$('nbVal').innerText=Math.round(($('nbright').value/800)*100)+'%';clearTimeout(tmr);tmr=setTimeout(saveCfg,70);}"
"function rot180(){const n=!($('fliph').checked&&$('flipv').checked);$('fliph').checked=n;$('flipv').checked=n;saveCfg();}"
"function saveCfg(){"
"clearTimeout(tmr);"
"post({cmd:'config',fliph:$('fliph').checked,flipv:$('flipv').checked,bright:+$('bright').value,nbright:+$('nbright').value,nstart:+$('nstart').value,nend:+$('nend').value,gmtoff:(+$('gmth').value)*3600,dstoff:+$('dsth').value},'Settings saved');"
"}"
"function savePlay(){"
"post({cmd:'playlist',clock:$('sClk').checked,typewriter:$('sTyp').checked,sparkle:$('sSprk').checked,scanner:$('sScan').checked,temps:$('sTmp').checked,msg:$('sMsg').checked},'Playlist updated');"
"}"
"function sendMsgNow(){"
"post({cmd:'message',text:$('cmsg').value,time:(+$('cdur').value)*1000},'Showing message on screen!');"
"}"
"function saveWeather(){"
"post({cmd:'weather',city:$('city').value,country:$('country').value,region:$('region').value},'Fetching Open-Meteo weather...');"
"setTimeout(()=>fetch('/api/settings').then(r=>r.json()).then(apply),2500);"
"}"
"function sendTemps(){"
"post({cmd:'temps',t1:parseFloat($('t1').value),t2:parseFloat($('t2').value),t3:parseFloat($('t3').value),t4:parseFloat($('t4').value)},'Temps pushed to screen!');"
"}"
"function jump(st){post({cmd:'jump',state:st},'Switched screen mode');}"
"function syncTime(silent){"
"const n=new Date(),ep=Math.floor(n.getTime()/1000)-(n.getTimezoneOffset()*60);"
"post({cmd:'sync',epoch:ep},silent?null:'Browser time synced!');"
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

// Helper to extract JSON string value
static bool json_get_str(const char *json, const char *key, char *out, size_t max_len) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < max_len) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static bool json_get_int(const char *json, const char *key, long *out) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if ((*p >= '0' && *p <= '9') || *p == '-') {
        *out = atol(p);
        return true;
    }
    return false;
}

static bool json_get_float(const char *json, const char *key, float *out) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if ((*p >= '0' && *p <= '9') || *p == '-') {
        *out = (float)atof(p);
        return true;
    }
    return false;
}

static bool json_get_bool(const char *json, const char *key, bool *out) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (strncmp(p, "true", 4) == 0 || *p == '1') { *out = true; return true; }
    if (strncmp(p, "false", 5) == 0 || *p == '0') { *out = false; return true; }
    return false;
}

static esp_err_t settings_get_handler(httpd_req_t *req) {
    set_fast_socket(req);

    time_t now;
    time(&now);
    struct tm ti;
    gmtime_r(&now, &ti);

    char resp[768];
    int len = snprintf(resp, sizeof(resp),
        "{\"fliph\":%s,\"flipv\":%s,\"bright\":%d,\"nbright\":%d,\"nstart\":%d,\"nend\":%d,"
        "\"gmtoff\":%ld,\"dstoff\":%d,\"city\":\"%s\",\"country\":\"%s\",\"region\":\"%s\","
        "\"sClk\":%s,\"sTyp\":%s,\"sSprk\":%s,\"sScan\":%s,\"sTmp\":%s,\"sMsg\":%s,"
        "\"cmsg\":\"%s\",\"cdur\":%d,"
        "\"t1\":%.1f,\"t2\":%.1f,\"t3\":%.1f,\"t4\":%.1f,"
        "\"time\":\"%02d:%02d:%02d\",\"epoch\":%ld,\"weather\":\"%s\",\"ip\":\"%s\"}",
        g_cfg.fliph ? "true" : "false",
        g_cfg.flipv ? "true" : "false",
        g_cfg.bright, g_cfg.nbright, g_cfg.nstart, g_cfg.nend,
        g_cfg.gmtoff, g_cfg.dstoff,
        g_cfg.city, g_cfg.country, g_cfg.region,
        g_cfg.show_clock ? "true" : "false",
        g_cfg.show_typewriter ? "true" : "false",
        g_cfg.show_sparkle ? "true" : "false",
        g_cfg.show_scanner ? "true" : "false",
        g_cfg.show_temps ? "true" : "false",
        g_cfg.show_msg ? "true" : "false",
        g_cfg.custom_msg, g_cfg.custom_msg_duration_ms,
        g_cfg.ext_temps[0], g_cfg.ext_temps[1], g_cfg.ext_temps[2], g_cfg.ext_temps[3],
        ti.tm_hour, ti.tm_min, ti.tm_sec, (long)now,
        g_live_weather_str, g_ip_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, len);
}

static esp_err_t settings_post_handler(httpd_req_t *req) {
    set_fast_socket(req);
    char buf[512];
    int remaining = req->content_len;
    if (remaining >= (int)sizeof(buf)) remaining = sizeof(buf) - 1;

    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    long iv;
    float fv;
    bool bv;
    char cmd[24] = "";
    json_get_str(buf, "cmd", cmd, sizeof(cmd));

    // Config parameters
    if (json_get_bool(buf, "fliph", &bv)) g_cfg.fliph = bv;
    if (json_get_bool(buf, "flipv", &bv)) g_cfg.flipv = bv;
    if (json_get_int(buf, "bright", &iv) || json_get_int(buf, "brightness", &iv)) {
        g_cfg.bright = (iv < 10) ? 10 : ((iv > 800) ? 800 : (int)iv);
    }
    if (json_get_int(buf, "nbright", &iv)) {
        g_cfg.nbright = (iv < 5) ? 5 : ((iv > 800) ? 800 : (int)iv);
    }
    if (json_get_int(buf, "nstart", &iv)) g_cfg.nstart = (iv < 0) ? 0 : ((iv > 23) ? 23 : (int)iv);
    if (json_get_int(buf, "nend", &iv))   g_cfg.nend = (iv < 0) ? 0 : ((iv > 23) ? 23 : (int)iv);
    if (json_get_int(buf, "gmtoff", &iv)) g_cfg.gmtoff = iv;
    if (json_get_int(buf, "dstoff", &iv)) g_cfg.dstoff = (int)iv;

    // Weather location
    bool loc_changed = false;
    if (json_get_str(buf, "city", g_cfg.city, sizeof(g_cfg.city))) loc_changed = true;
    if (json_get_str(buf, "country", g_cfg.country, sizeof(g_cfg.country))) loc_changed = true;
    if (json_get_str(buf, "region", g_cfg.region, sizeof(g_cfg.region))) loc_changed = true;
    if (loc_changed || strcmp(cmd, "weather") == 0) {
        app_trigger_weather_refresh();
    }

    // Playlist toggles
    if (json_get_bool(buf, "clock", &bv))      g_cfg.show_clock = bv;
    if (json_get_bool(buf, "typewriter", &bv)) g_cfg.show_typewriter = bv;
    if (json_get_bool(buf, "sparkle", &bv))    g_cfg.show_sparkle = bv;
    if (json_get_bool(buf, "scanner", &bv))    g_cfg.show_scanner = bv;
    if (json_get_bool(buf, "temps", &bv))      g_cfg.show_temps = bv;
    if (json_get_bool(buf, "msg", &bv))        g_cfg.show_msg = bv;

    if (!g_cfg.show_clock && !g_cfg.show_typewriter && !g_cfg.show_sparkle &&
        !g_cfg.show_scanner && !g_cfg.show_temps && !g_cfg.show_msg) {
        g_cfg.show_clock = true;
    }

    // Custom message trigger
    if (strcmp(cmd, "message") == 0) {
        json_get_str(buf, "text", g_cfg.custom_msg, sizeof(g_cfg.custom_msg));
        if (json_get_int(buf, "time", &iv)) {
            g_cfg.custom_msg_duration_ms = (iv < 1000) ? 1000 : ((iv > 300000) ? 300000 : (int)iv);
        }
        g_cfg.show_msg = true;
        app_trigger_state(STATE_WIPE_TO_MSG);
    }

    // External 4-quadrant temperatures trigger
    if (strcmp(cmd, "temps") == 0) {
        if (json_get_float(buf, "t1", &fv)) g_cfg.ext_temps[0] = fv;
        if (json_get_float(buf, "t2", &fv)) g_cfg.ext_temps[1] = fv;
        if (json_get_float(buf, "t3", &fv)) g_cfg.ext_temps[2] = fv;
        if (json_get_float(buf, "t4", &fv)) g_cfg.ext_temps[3] = fv;
        app_trigger_state(STATE_WIPE_TO_TEMPS);
    }

    // Direct jump to state
    if (strcmp(cmd, "jump") == 0 && json_get_int(buf, "state", &iv)) {
        if (iv >= STATE_CLOCK && iv <= STATE_MESSAGE) {
            app_trigger_state((app_state_t)iv);
        }
    }

    // Time epoch sync
    if (json_get_int(buf, "epoch", &iv) && iv > 100000) {
        struct timeval tv = { .tv_sec = iv, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Clock synced via API to epoch: %ld", iv);
    }

    matrix_set_flip(g_cfg.fliph, g_cfg.flipv);
    matrix_set_brightness(settings_is_night_time() ? g_cfg.nbright : g_cfg.bright);
    settings_schedule_save();

    return settings_get_handler(req);
}

esp_err_t webserver_start(void) {
    if (s_server != NULL) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.task_priority = 6;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 12;
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
    ESP_LOGE(TAG, "Failed to start HTTP server: 0x%x", err);
    return err;
}

void webserver_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
