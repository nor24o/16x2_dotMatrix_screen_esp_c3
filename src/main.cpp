#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <soc/gpio_struct.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sleep.h>
#include "esp_wifi.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// ==========================================
// 0. CONFIGURATION & ORIENTATION
// ==========================================
#define ENABLE_POWER_SENSE 0 // 0 = USB Debug, 1 = Battery Backup Mode

Preferences preferences;
volatile bool FLIP_H = false;
volatile bool FLIP_V = false;
volatile int BRIGHTNESS_US = 250;
const int MIN_BRIGHTNESS_US = 10;
const int MAX_BRIGHTNESS_US = 1600;

bool settingsPendingSave = false;
unsigned long lastSettingChangeTime = 0;

// ==========================================
// 1. PIN DEFINITIONS & REGISTER MACROS
// ==========================================
#define SENSE_PIN 1

#define DATA_PIN 4
#define CLK_PIN 21
#define STR_PIN 3
#define OE_PIN 7
#define ADDR_A 2
#define ADDR_B 6
#define ADDR_C 0
#define G1_BLANK 5

#define DATA_HIGH()   GPIO.out_w1ts.val = (1UL << DATA_PIN)
#define DATA_LOW()    GPIO.out_w1tc.val = (1UL << DATA_PIN)
#define CLK_HIGH()    GPIO.out_w1ts.val = (1UL << CLK_PIN)
#define CLK_LOW()     GPIO.out_w1tc.val = (1UL << CLK_PIN)
#define STR_HIGH()    GPIO.out_w1ts.val = (1UL << STR_PIN)
#define STR_LOW()     GPIO.out_w1tc.val = (1UL << STR_PIN)
#define OE_HIGH()     GPIO.out_w1ts.val = (1UL << OE_PIN)
#define OE_LOW()      GPIO.out_w1tc.val = (1UL << OE_PIN)
#define ADDR_A_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_A)
#define ADDR_A_LOW()  GPIO.out_w1tc.val = (1UL << ADDR_A)
#define ADDR_B_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_B)
#define ADDR_B_LOW()  GPIO.out_w1tc.val = (1UL << ADDR_B)
#define ADDR_C_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_C)
#define ADDR_C_LOW()  GPIO.out_w1tc.val = (1UL << ADDR_C)
#define BLANK_HIGH()  GPIO.out_w1ts.val = (1UL << G1_BLANK)
#define BLANK_LOW()   GPIO.out_w1tc.val = (1UL << G1_BLANK)

const uint8_t MATRIX_PINS[] = {
  DATA_PIN, CLK_PIN, STR_PIN, OE_PIN,
  ADDR_A, ADDR_B, ADDR_C, G1_BLANK
};

// ==========================================
// 2. DISPLAY BUFFER (1 Row x 14 Characters)
// ==========================================
const bool ENABLE_GAPS = true;
const uint8_t TOTAL_MODULES = 14;
const uint8_t MODULE_WIDTH = 5;
const uint8_t ROWS = 8;
const uint8_t PHYS_COLS = TOTAL_MODULES * MODULE_WIDTH; // 70
const uint8_t BUFFER_BYTES = 9;                         // 72 bits capacity

const uint8_t GAP_WIDTH = 1;
const uint16_t VIRTUAL_MOD_WIDTH = (ENABLE_GAPS ? (MODULE_WIDTH + GAP_WIDTH) : MODULE_WIDTH);
const uint16_t VIRTUAL_COLS = TOTAL_MODULES * VIRTUAL_MOD_WIDTH;

uint8_t drawBuffer[ROWS][BUFFER_BYTES];
volatile uint8_t displayBuffer[ROWS][BUFFER_BYTES];

const int REFRESH_INTERVAL_US = 2000;

hw_timer_t *timer = NULL;
TaskHandle_t displayTaskHandle = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool displayRunning = false;

// ==========================================
// 3. AP & WEB SERVER CONFIGURATION
// ==========================================
const char *AP_SSID = "MATRIX-CLOCK";
const char *AP_PASS = "matrix1234";
const uint8_t AP_CHAN = 6;

WebServer server(80);
bool apActive = true;
bool clientConnectedOnce = false;
unsigned long apStartTime = 0;
const unsigned long AP_TIMEOUT_MS = 5 * 60 * 1000;
char currentDisplayStr[16] = "              ";

// ==========================================
// 4. HTML INTERFACE
// ==========================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Matrix Clock</title>
  <style>
    :root { --bg: #0f172a; --card: #1e293b; --accent: #3b82f6; --text: #f8fafc; --muted: #94a3b8; }
    * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); margin: 0; padding: 24px 14px; display: flex; justify-content: center; }
    .card { background: var(--card); border-radius: 16px; padding: 22px; width: 100%; max-width: 380px; box-shadow: 0 10px 25px rgba(0,0,0,0.4); }
    h1 { font-size: 20px; font-weight: 700; margin: 0 0 4px 0; text-align: center; }
    .subtitle { text-align: center; font-size: 13px; color: var(--muted); margin-bottom: 18px; }
    .clock-badge { background: #0f172a; border-radius: 12px; padding: 14px; text-align: center; margin-bottom: 18px; border: 1px solid #334155; }
    .clock-time { font-family: monospace; font-size: 30px; font-weight: bold; letter-spacing: 2px; color: #38bdf8; }
    .clock-label { font-size: 11px; text-transform: uppercase; color: var(--muted); margin-top: 4px; }
    .btn { background: var(--accent); color: white; border: none; border-radius: 10px; padding: 12px; width: 100%; font-size: 14px; font-weight: 600; cursor: pointer; transition: background 0.2s, transform 0.1s; }
    .btn:active { transform: scale(0.98); }
    .btn-secondary { background: #334155; color: #cbd5e1; font-size: 12px; padding: 6px 12px; border-radius: 8px; border: none; cursor: pointer; }
    .btn-secondary:hover { background: #475569; }
    .section-title { font-size: 11px; text-transform: uppercase; letter-spacing: 1px; color: var(--muted); margin: 20px 0 8px; }
    .control-row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #334155; }
    .control-row:last-child { border-bottom: none; }
    .switch { position: relative; display: inline-block; width: 44px; height: 24px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider-toggle { position: absolute; cursor: pointer; inset: 0; background-color: #475569; border-radius: 24px; transition: .25s; }
    .slider-toggle:before { position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; border-radius: 50%; transition: .25s; }
    input:checked + .slider-toggle { background-color: var(--accent); }
    input:checked + .slider-toggle:before { transform: translateX(20px); }
    .range-wrap { margin-top: 8px; }
    .range-header { display: flex; justify-content: space-between; font-size: 14px; margin-bottom: 6px; }
    input[type=range] { width: 100%; height: 6px; border-radius: 3px; background: #334155; outline: none; -webkit-appearance: none; cursor: pointer; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: var(--accent); cursor: pointer; }
    #toast { text-align: center; margin-top: 14px; font-size: 13px; font-weight: 500; min-height: 20px; }
    .toast-ok { color: #4ade80; }
    .toast-err { color: #f87171; }
  </style>
</head>
<body>
  <div class='card'>
    <h1>Matrix Controller</h1>
    <div class='subtitle'>ESP32-C3 LED Display</div>

    <div class='clock-badge'>
      <div id='clockDisplay' class='clock-time'>--:--:--</div>
      <div class='clock-label'>Device Time</div>
    </div>

    <button class='btn' onclick='syncBrowserTime()'>Sync Browser Time</button>

    <div class='section-title'>Orientation</div>
    <div class='control-row'>
      <span>Flip Horizontal</span>
      <label class='switch'><input type='checkbox' id='fliph' onchange='onSettingChange()'><span class='slider-toggle'></span></label>
    </div>
    <div class='control-row'>
      <span>Flip Vertical</span>
      <label class='switch'><input type='checkbox' id='flipv' onchange='onSettingChange()'><span class='slider-toggle'></span></label>
    </div>
    <div class='control-row'>
      <span>Rotate 180&deg;</span>
      <button class='btn-secondary' onclick='rotate180()'>Toggle 180&deg;</button>
    </div>

    <div class='section-title'>Brightness</div>
    <div class='range-wrap'>
      <div class='range-header'>
        <span>Level</span>
        <span id='brightVal'>--%</span>
      </div>
      <input type='range' id='brightness' min='10' max='1500' step='10' oninput='onBrightnessSlide()' onchange='onSettingChange()'>
    </div>

    <div id='toast'></div>
  </div>

  <script>
    let sendTimeout = null;
    let isSending = false;

    window.onload = function() {
      loadSettings(true);
      setInterval(pollClock, 2000);
    };

    function showToast(msg, isOk = true) {
      const t = document.getElementById('toast');
      t.innerText = msg;
      t.className = isOk ? 'toast-ok' : 'toast-err';
      clearTimeout(t.timer);
      t.timer = setTimeout(() => { t.innerText = ''; }, 3000);
    }

    function loadSettings(autoSyncTime = false) {
      fetch('/api/settings')
        .then(r => r.json())
        .then(d => {
          document.getElementById('fliph').checked = d.fliph;
          document.getElementById('flipv').checked = d.flipv;
          document.getElementById('brightness').value = d.brightness;
          document.getElementById('brightVal').innerText = Math.round((d.brightness / 1500) * 100) + '%';
          if (d.time) document.getElementById('clockDisplay').innerText = d.time;
          if (autoSyncTime) syncBrowserTime(true);
        })
        .catch(() => showToast('Connecting to device...', false));
    }

    function pollClock() {
      fetch('/api/settings')
        .then(r => r.json())
        .then(d => {
          if (d.time) document.getElementById('clockDisplay').innerText = d.time;
        })
        .catch(() => {});
    }

    function onBrightnessSlide() {
      const val = document.getElementById('brightness').value;
      document.getElementById('brightVal').innerText = Math.round((val / 1500) * 100) + '%';
      clearTimeout(sendTimeout);
      sendTimeout = setTimeout(onSettingChange, 120);
    }

    function rotate180() {
      const h = document.getElementById('fliph');
      const v = document.getElementById('flipv');
      const nextState = !(h.checked && v.checked);
      h.checked = nextState;
      v.checked = nextState;
      onSettingChange();
    }

    function onSettingChange() {
      clearTimeout(sendTimeout);
      if (isSending) {
        sendTimeout = setTimeout(onSettingChange, 100);
        return;
      }
      isSending = true;
      const payload = {
        fliph: document.getElementById('fliph').checked,
        flipv: document.getElementById('flipv').checked,
        brightness: parseInt(document.getElementById('brightness').value)
      };
      fetch('/api/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      })
      .then(r => r.json())
      .then(d => {
        isSending = false;
        showToast('Settings saved');
      })
      .catch(() => {
        isSending = false;
        showToast('Save failed', false);
      });
    }

    function syncBrowserTime(silent = false) {
      const now = new Date();
      const localEpoch = Math.floor(now.getTime() / 1000) - (now.getTimezoneOffset() * 60);
      fetch('/api/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ epoch: localEpoch })
      })
      .then(r => r.json())
      .then(d => {
        if (d.time) document.getElementById('clockDisplay').innerText = d.time;
        if (!silent) showToast('Time synced: ' + d.time);
      })
      .catch(() => {
        if (!silent) showToast('Sync failed', false);
      });
    }
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// 5. FONT DATA
// ==========================================
const uint8_t FONT_TEXT[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E}
};

// ==========================================
// 6. GRAPHICS ENGINE
// ==========================================
void clearBuffer() { 
  memset((void *)drawBuffer, 0, sizeof(drawBuffer)); 
}

void show() {
  portENTER_CRITICAL(&timerMux);
  memcpy((void *)displayBuffer, (void *)drawBuffer, sizeof(displayBuffer));
  portEXIT_CRITICAL(&timerMux);
}

int getPhysicalX(int vx) {
  if (!ENABLE_GAPS) return vx;
  int modIdx = vx / VIRTUAL_MOD_WIDTH;
  int offset = vx % VIRTUAL_MOD_WIDTH;
  if (offset >= MODULE_WIDTH) return -1;
  return (modIdx * MODULE_WIDTH) + offset;
}

void setPixel(int virtualX, int y, uint8_t state) {
  if (virtualX < 0 || virtualX >= VIRTUAL_COLS || y < 0 || y >= ROWS) return;
  int physX = getPhysicalX(virtualX);
  if (physX == -1) return;

  if (FLIP_H) physX = (PHYS_COLS - 1) - physX;
  if (FLIP_V) y = (ROWS - 1) - y;

  int byteIdx = physX / 8;
  int bitIdx = physX % 8;
  if (byteIdx < BUFFER_BYTES) {
    if (state) drawBuffer[y][byteIdx] |= (1 << bitIdx);
    else drawBuffer[y][byteIdx] &= ~(1 << bitIdx);
  }
}

uint8_t getCharIdx(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
  if (c >= '0' && c <= '9') return c - '0' + 27;
  return 0;
}

void drawChar(uint8_t modIndex, char c) {
  if (modIndex >= TOTAL_MODULES) return;
  int startX = modIndex * VIRTUAL_MOD_WIDTH;
  if (c >= 'a' && c <= 'z') c -= 32;

  if (c == ':') {
    setPixel(startX + 1, 2, 1);
    setPixel(startX + 1, 5, 1);
    setPixel(startX + 2, 2, 1);
    setPixel(startX + 2, 5, 1);
    return;
  }
  if (c == '.') {
    setPixel(startX + 1, 7, 1);
    setPixel(startX + 2, 7, 1);
    return;
  }

  uint8_t idx = getCharIdx(c);
  if (idx == 0 && c != ' ') return;
  for (int col = 0; col < 5; col++) {
    uint8_t data = FONT_TEXT[idx][col];
    for (int r = 0; r < 8; r++) {
      if (data & (1 << r)) setPixel(startX + col, r, 1);
    }
  }
}

void drawString(const char *str) {
  strncpy(currentDisplayStr, str, sizeof(currentDisplayStr) - 1);
  currentDisplayStr[sizeof(currentDisplayStr) - 1] = '\0';
  clearBuffer();
  int len = strlen(str);
  for (int i = 0; i < len && i < TOTAL_MODULES; i++) {
    drawChar(i, str[i]);
  }
  show();
}

void runScreenSweep() {
  for (int x = 0; x < VIRTUAL_COLS; x++) {
    clearBuffer();
    for (int y = 0; y < ROWS; y++) {
      setPixel(x, y, 1);
    }
    show();
    delay(8);
  }
  clearBuffer();
  show();
}

// ==========================================
// 7. MULTIPLEXING ENGINE (FLICKER-FREE)
// ==========================================
void IRAM_ATTR onTimer() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(displayTaskHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void displayRefreshTask(void *pvParameters) {
  uint8_t currentRow = 0;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    portENTER_CRITICAL(&timerMux);
    OE_HIGH(); // Blank display during latch & row switch to avoid ghosting

    for (int b = 0; b < BUFFER_BYTES; b++) {
      uint8_t data = displayBuffer[currentRow][b];
      for (int i = 0; i < 8; i++) {
        CLK_LOW();
        if (data & 1) DATA_HIGH();
        else DATA_LOW();
        data >>= 1;
        asm volatile("nop");
        CLK_HIGH();
        asm volatile("nop");
      }
    }

    uint8_t addr = 7 - currentRow;
    (addr & 0x01) ? ADDR_A_HIGH() : ADDR_A_LOW();
    (addr & 0x02) ? ADDR_B_HIGH() : ADDR_B_LOW();
    (addr & 0x04) ? ADDR_C_HIGH() : ADDR_C_LOW();

    STR_HIGH();
    asm volatile("nop; nop;");
    STR_LOW();
    delayMicroseconds(2);

    portEXIT_CRITICAL(&timerMux);

    // Safe clamped pulse width: ensure task finishes well before the next timer tick
    int currentPulse = BRIGHTNESS_US;
    if (currentPulse > (REFRESH_INTERVAL_US - 200)) {
      currentPulse = REFRESH_INTERVAL_US - 200;
    }

    if (currentPulse > 0) {
      OE_LOW(); // Turn ON LEDs for pulse duration
      delayMicroseconds(currentPulse);
      OE_HIGH(); // Turn OFF LEDs
    }

    currentRow = (currentRow + 1) & 0x07;
  }
}

void startDisplay() {
  if (displayRunning) return;
  displayRunning = true;

  clearBuffer();
  show();

  // Priority 5 ensures matrix multiplexing runs above background tasks and Wi-Fi processing
  xTaskCreate(displayRefreshTask, "DisplayTask", 4096, NULL, 5, &displayTaskHandle);
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, REFRESH_INTERVAL_US, true, 0);
}

// ==========================================
// 8. SETTINGS PERSISTENCE (NVS)
// ==========================================
void loadSavedSettings() {
  preferences.begin("matrix", true);
  FLIP_H = preferences.getBool("fliph", false);
  FLIP_V = preferences.getBool("flipv", false);
  BRIGHTNESS_US = preferences.getInt("bright", 250);
  preferences.end();

  if (BRIGHTNESS_US < MIN_BRIGHTNESS_US) BRIGHTNESS_US = MIN_BRIGHTNESS_US;
  if (BRIGHTNESS_US > MAX_BRIGHTNESS_US) BRIGHTNESS_US = MAX_BRIGHTNESS_US;

  Serial.printf("Loaded settings -> FLIP_H: %d, FLIP_V: %d, Brightness: %d us\n",
                FLIP_H, FLIP_V, BRIGHTNESS_US);
}

void scheduleSettingsSave() {
  settingsPendingSave = true;
  lastSettingChangeTime = millis();
}

void commitSettingsIfNeeded() {
  // Commit to flash after 1.5s of inactivity to prevent flash wear and display pauses
  if (settingsPendingSave && (millis() - lastSettingChangeTime >= 1500)) {
    settingsPendingSave = false;
    preferences.begin("matrix", false);
    preferences.putBool("fliph", FLIP_H);
    preferences.putBool("flipv", FLIP_V);
    preferences.putInt("bright", BRIGHTNESS_US);
    preferences.end();
    Serial.println("Settings saved to NVS Flash.");
  }
}

// ==========================================
// 9. CLOCK ENGINE
// ==========================================
void updateClockDisplay(bool force = false) {
  static unsigned long lastViewUpdate = 0;
  static bool colonState = true;

  if (force || (millis() - lastViewUpdate >= 500)) {
    lastViewUpdate = millis();
    colonState = !colonState;

    time_t now;
    time(&now);
    struct tm *ti = gmtime(&now);

    char lineBuf[16];
    char monthStr[4];
    strftime(monthStr, sizeof(monthStr), "%b", ti);
    for (int i = 0; i < 3; i++) monthStr[i] = toupper(monthStr[i]);

    snprintf(lineBuf, sizeof(lineBuf), " %02d%c%02d  %02d %s ",
             ti->tm_hour,
             colonState ? ':' : ' ',
             ti->tm_min,
             ti->tm_mday,
             monthStr);

    drawString(lineBuf);
  }
}

// ==========================================
// 10. WEB SERVER HANDLERS
// ==========================================
void disableWiFi() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  apActive = false;
  Serial.println("AP Timed Out -> WiFi Powered Down.");
}

void handleRoot() {
  clientConnectedOnce = true;
  server.send(200, "text/html", INDEX_HTML);
}

void handleGetSettings() {
  clientConnectedOnce = true;
  JsonDocument doc;
  doc["fliph"] = (bool)FLIP_H;
  doc["flipv"] = (bool)FLIP_V;
  doc["brightness"] = (int)BRIGHTNESS_US;

  time_t now;
  time(&now);
  struct tm *ti = gmtime(&now);
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
  doc["time"] = timeBuf;
  doc["epoch"] = (unsigned long)now;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePostSettings() {
  clientConnectedOnce = true;

  if (server.hasArg("plain")) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (!err) {
      if (!doc["fliph"].isNull()) FLIP_H = doc["fliph"].as<bool>();
      if (!doc["flipv"].isNull()) FLIP_V = doc["flipv"].as<bool>();
      if (!doc["brightness"].isNull()) {
        int b = doc["brightness"].as<int>();
        BRIGHTNESS_US = constrain(b, MIN_BRIGHTNESS_US, MAX_BRIGHTNESS_US);
      }
      if (!doc["epoch"].isNull()) {
        time_t receivedEpoch = (time_t)doc["epoch"].as<unsigned long>();
        struct timeval tv = { .tv_sec = receivedEpoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
      }
    }
  }

  // Also support URL/Form parameters
  if (server.hasArg("fliph")) FLIP_H = (server.arg("fliph") == "1" || server.arg("fliph") == "true");
  if (server.hasArg("flipv")) FLIP_V = (server.arg("flipv") == "1" || server.arg("flipv") == "true");
  if (server.hasArg("brightness")) {
    BRIGHTNESS_US = constrain(server.arg("brightness").toInt(), MIN_BRIGHTNESS_US, MAX_BRIGHTNESS_US);
  }
  if (server.hasArg("epoch")) {
    time_t receivedEpoch = (time_t)server.arg("epoch").toInt();
    struct timeval tv = { .tv_sec = receivedEpoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
  }

  scheduleSettingsSave();
  updateClockDisplay(true);
  handleGetSettings();
}

// Backward-compatible endpoints
void handleStatus() {
  handleGetSettings();
}

void handleFlip() {
  clientConnectedOnce = true;
  if (server.hasArg("h")) FLIP_H = (server.arg("h") == "1");
  if (server.hasArg("v")) FLIP_V = (server.arg("v") == "1");
  scheduleSettingsSave();
  updateClockDisplay(true);
  server.send(200, "text/plain", "OK");
}

void handleSync() {
  clientConnectedOnce = true;
  if (server.hasArg("epoch")) {
    time_t receivedEpoch = (time_t)server.arg("epoch").toInt();
    struct timeval tv = { .tv_sec = receivedEpoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    updateClockDisplay(true);

    struct tm *ti = gmtime(&receivedEpoch);
    char reply[50];
    snprintf(reply, sizeof(reply), "Synced: %02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
    server.send(200, "text/plain", reply);
  } else {
    server.send(400, "text/plain", "Missing epoch parameter");
  }
}

// ==========================================
// 11. POWER LOSS & DEEP SLEEP ROUTINE
// ==========================================
void enterLowPowerSleep() {
#if ENABLE_POWER_SENSE
  OE_HIGH();
  BLANK_HIGH();
  for (uint8_t pin : MATRIX_PINS) {
    pinMode(pin, INPUT);
  }

  if (timer) timerStop(timer);
  if (displayTaskHandle) vTaskSuspend(displayTaskHandle);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  esp_deep_sleep_enable_gpio_wakeup(1ULL << SENSE_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
#endif
}

void IRAM_ATTR onPowerLost() {
#if ENABLE_POWER_SENSE
  enterLowPowerSleep();
#endif
}

// ==========================================
// 12. SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== MATRIX CLOCK BOOT ===");

#if ENABLE_POWER_SENSE
  pinMode(SENSE_PIN, INPUT);
  if (digitalRead(SENSE_PIN) == LOW) {
    enterLowPowerSleep();
  }
  attachInterrupt(digitalPinToInterrupt(SENSE_PIN), onPowerLost, FALLING);
#endif

  // 1. Initialize Matrix Output Pins blanked
  for (uint8_t pin : MATRIX_PINS) {
    pinMode(pin, OUTPUT);
  }
  OE_HIGH();
  BLANK_HIGH();

  // 2. Load NVS persistent settings
  loadSavedSettings();

  // 3. Start Matrix Display Engine (Flicker-Free, High-Priority Task)
  startDisplay();
  runScreenSweep();
  drawString(" MATRIX CLOCK ");
  delay(700);
  updateClockDisplay(true);

  // 4. SoftAP bring-up
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);

  Serial.println("Starting SoftAP...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  delay(200);

  bool ok = WiFi.softAP(AP_SSID, AP_PASS, AP_CHAN);
  Serial.printf("softAP returned: %s\n", ok ? "true" : "false");
  delay(200);

  esp_wifi_set_max_tx_power(34);
  Serial.printf("AP IP: %s | SSID: %s | Chan: %d\n", 
                WiFi.softAPIP().toString().c_str(), 
                WiFi.softAPSSID().c_str(), 
                WiFi.channel());

  // 5. Setup Web Server Endpoints
  server.enableCORS(true);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/flip", HTTP_POST, handleFlip);
  server.on("/sync", HTTP_POST, handleSync);
  server.begin();
  apStartTime = millis();

  Serial.println("=== SYSTEM INITIALIZED AND RUNNING ===");
}

void loop() {
#if ENABLE_POWER_SENSE
  if (digitalRead(SENSE_PIN) == LOW) {
    enterLowPowerSleep();
  }
#endif

  if (apActive) {
    server.handleClient();

    uint8_t stations = WiFi.softAPgetStationNum();
    if (stations > 0) {
      clientConnectedOnce = true;
    }

    // 5-minute AP timeout only if no client connected
    if (!clientConnectedOnce && (millis() - apStartTime >= AP_TIMEOUT_MS)) {
      disableWiFi();
    }
  }

  // Periodic clock update (every 500ms)
  updateClockDisplay();

  // Deferred NVS Flash commit (zero display stutter)
  commitSettingsIfNeeded();

  delay(5);
}
