# 16x2 LED Dot-Matrix Screen & Clock (ESP32-C3 Native ESP-IDF FreeRTOS)

A high-performance, zero-flicker smart clock and telemetry display engine engineered for 32 modules of 5x7 LED dot matrices (16 modules wide $\times$ 2 rows high, totaling 1,120 LEDs). Powered by an **Espressif ESP32-C3 RISC-V microcontroller** running native **ESP-IDF v5+ FreeRTOS**, featuring a dual-timer multiplexing engine, sub-40ms web interface, Open-Meteo live weather, custom 5x7 icon system, and intelligent Night Mode scheduling.

---

## Hardware Architecture & Pin Mapping

The physical display consists of two horizontal rows of 16 modules (32 modules total). Shift registers daisy-chain 160 physical column bits across 8 multiplexed rows driven by a 3-to-8 decoder.

```
       [ MODULE 0 ] [ MODULE 1 ] ... [ MODULE 15 ]   <- Screen 0 (Upper Row: 16 modules)
       [ MODULE 16] [ MODULE 17] ... [ MODULE 31 ]   <- Screen 1 (Lower Row: 16 modules)
```

### Physical Wiring Table

| Signal | ESP32-C3 GPIO | Description |
| :--- | :--- | :--- |
| **`DATA_PIN`** | **GPIO 4** | Shift register serial data in |
| **`CLK_PIN`** | **GPIO 6** | Shift register serial clock |
| **`STR_PIN`** | **GPIO 3** | Latch strobe (rising edge) |
| **`OE_PIN`** | **GPIO 7** | Output Enable (active LOW illumination pulse) |
| **`ADDR_A`** | **GPIO 2** | 3-to-8 row decoder bit 0 (LSB) |
| **`ADDR_B`** | **GPIO 21** | 3-to-8 row decoder bit 1 |
| **`ADDR_C`** | **GPIO 0** | 3-to-8 row decoder bit 2 (MSB) |
| **`G1_BLANK`**| **GPIO 5** | Decoder blanking / safety pull-down |

> [!IMPORTANT]
> The hardware wiring uses **GPIO 6** for Clock and **GPIO 21** for Row Address B. On the ESP32-C3, GPIO 21 is multiplexed with UART0 RX; the firmware explicitly detaches and reconfigures GPIO 21 as a standard push-pull output.

---

## Zero-Flicker Hardware Dual-Timer Display Engine

Unlike Arduino implementations that suffer from Wi-Fi interrupt jitter, the display multiplexer operates entirely via two non-blocking **`esp_timer`** callbacks running in IRAM:

1. **`row_timer_callback` (Periodic at 1200 µs = 104.1 Hz frame rate)**:
   - Disables `OE` (blanking).
   - Shifts 20 bytes (160 physical bits) in ~15 µs inside a fast critical section (`portENTER_CRITICAL_SAFE`).
   - Sets the 3-bit row decoder address (`ADDR_A`, `ADDR_B`, `ADDR_C`).
   - Latches data with `STR`.
   - Drives `OE_LOW()` and arms a hardware one-shot timer for the exact brightness pulse width.
2. **`oe_off_timer_callback` (Hardware One-Shot Timer)**:
   - Automatically drives `OE_HIGH()` with sub-microsecond accuracy when the brightness duration expires.
   - **0% busy-wait CPU overhead** and zero display shimmer during heavy network traffic.

---

## Intelligent Night Mode & Custom Telemetry Layout

### Day vs. Night Behavior

- **Daytime Operation**:
  - Operates at normal daytime brightness (`bright`: `10..800` µs).
  - Cycles through all enabled animations in the playlist (Clock & Weather, Typewriter, Sparkle, Cylon Scanner, 4-Quadrant Temps, and Custom Message).
  - Sensor telemetry alerts trigger a full-screen 4-quadrant display across both rows.
- **Nighttime Operation (`nstart` to `nend`)**:
  - Automatically dims to nighttime brightness (`nbright`: `5..400` µs).
  - Locks the **Upper Row (Screen 0)** to the persistent Clock display:
    ```
    DD MMM HH:MM   (e.g., "28 OCT 18:55" with blinking colon)
    ```
  - **Only the Lower Row (Screen 1)** updates with details:
    - Default night display: Compact static weather (e.g., `SA:CLR T10C H79%`).
    - When telemetry or alerts arrive: The upper row **never disappears or wipes**; only the bottom row updates with the sensor temperatures and custom icons (or scrolling alert message), then smoothly reverts.

---

## Built-In 5x7 Custom Icon Library

Each temperature node can be paired with any icon from the 20 built-in custom 5x7 pixel matrix icons:

| ID | Name | Aliases | Recommended Use |
| :---: | :--- | :--- | :--- |
| `0` | **`sun`** | `clear`, `day` | Clear sky / Outdoor day |
| `1` | **`cloud`** | `cld` | Overcast / Partly cloudy |
| `2` | **`rain`** | - | Rain / Precipitation |
| `3` | **`thunder`** | `storm`, `bolt` | Thunderstorm / Storm warning |
| `4` | **`snow`** | `frost`, `ice` | Snow / Freezing / Freezer temp |
| `5` | **`mist`** | `fog` | Fog / High humidity / Mist |
| `6` | **`moon`** | `night` | Night / Clear nighttime |
| `7` | **`home`** | `house`, `indoor`, `in` | Indoor / Living space temperature |
| `8` | **`bed`** | `bedroom` | Master or Guest bedroom |
| `9` | **`living`** | `sofa`, `couch` | Living room / Lounge |
| `10`| **`office`** | `pc`, `desk`, `cpu` | Home office / Server rack / CPU temp |
| `11`| **`water`** | `drop`, `humidity`, `boiler` | Water heater / Boiler / Bathroom / Pool |
| `12`| **`fire`** | `flame`, `heat`, `heater` | Fireplace / Stove / Central heating |
| `13`| **`thermo`** | `temp` | General ambient thermometer |
| `14`| **`heart`** | - | Health / Favorites / Nursery |
| `15`| **`tree`** | `outdoor`, `out`, `garden` | Garden / Yard / Greenhouse / Balcony |
| `16`| **`fan`** | `wind` | HVAC / Ventilation / Fan speed |
| `17`| **`battery`** | `bat` | Solar battery / Power backup |
| `18`| **`bell`** | `alert` | Alarm / Priority notification |
| `19`| **`car`** | `garage` | Garage / Vehicle temp |

---

## Web Controller Dashboard

Access the controller at `http://<device-ip>/` (or fallback SoftAP `http://192.168.4.1/`).

- **Clock & Synchronization**: Live browser clock, instant one-click browser epoch sync, time zone GMT offset (`gmtoff`), and Daylight Saving Time (`dstoff`).
- **Custom Alert Injector**: Push real-time text alerts with custom hold durations (`time` in ms).
- **Playlist & Animation Toggles**: Individual on/off switches for each phase, with instant preview jump buttons.
- **Brightness & Night Schedule**: Dual sliders for day/night brightness, start/end night hours, horizontal/vertical flip, and one-click 180° rotation.
- **Open-Meteo Weather**: Geocoded weather retrieval with automatic Romanian diacritic normalization (`ăâîșț` $\to$ `AAIST`).
- **Sensor Telemetry & Custom Icons**: 4-quadrant temperature inputs paired with icon dropdown selectors.

---

## UDP Port 8888 Command Reference (JSON API)

Send JSON datagrams to `device_ip:8888`. All configuration commands are persisted to NVS flash memory.

### 1. Global Configuration (`"cmd": "config"`)
```json
{
  "cmd": "config",
  "bright": 400,
  "nbright": 40,
  "nstart": 22,
  "nend": 7,
  "gmtoff": 7200,
  "dstoff": 3600,
  "city": "Sauca",
  "country": "RO",
  "region": "Satu Mare",
  "fliph": false,
  "flipv": false
}
```

### 2. Playlist Configuration (`"cmd": "playlist"`)
```json
{
  "cmd": "playlist",
  "clock": true,
  "typewriter": true,
  "sparkle": true,
  "scanner": true,
  "temps": true,
  "msg": false
}
```

### 3. Priority Alert / Custom Message (`"cmd": "message"`)
```json
{
  "cmd": "message",
  "text": "FIRE ALARM DETECTED",
  "time": 8000
}
```
*In Night Mode, this alert scrolls across the bottom row while the top row maintains the persistent clock.*

### 4. Sensor Telemetry with Custom Icons (`"cmd": "temps"`)
You can specify temperatures (`t1`..`t4`) and either icon names or icon integer IDs (`i1`..`i4`):
```json
{
  "cmd": "temps",
  "t1": 21.8,
  "i1": "home",
  "t2": 19.2,
  "i2": "bed",
  "t3": 4.5,
  "i3": "tree",
  "t4": 52.0,
  "i4": "water"
}
```
- **Daytime**: Triggers a 4-quadrant screen sweep displaying all 4 sensors with their custom icons.
- **Nighttime**: Keeps the upper row locked to `DD MMM HH:MM` and renders `T1`+`T2` (and alternates with `T3`+`T4`) on the bottom row without full-screen wipes.

### 5. System Status Query (`"cmd": "info"`)
```json
{"cmd": "info"}
```
Returns:
```json
{
  "ip": "192.168.1.15",
  "city": "Sauca",
  "country": "RO",
  "region": "Satu Mare",
  "bright": 400,
  "nbright": 40,
  "nstart": 22,
  "nend": 7,
  "gmtoff": 7200,
  "dstoff": 3600,
  "showClock": true,
  "showTypewriter": true,
  "showSparkle": true,
  "showScanner": true,
  "showTemps": true,
  "showMsg": false,
  "i1": 7,
  "i2": 8,
  "i3": 15,
  "i4": 11
}
```

---

## REST API Reference

The onboard web server exposes low-overhead REST endpoints:

- **`GET /api/settings`**: Returns full device status, clock, weather string, settings, and telemetry.
- **`POST /api/settings`**: Updates configuration, toggles playlist items, syncs time epoch, or pushes telemetry.
- **`GET /status`**: Lightweight JSON status alias.

---

## Desktop Qt GUI Controller

A Python/PySide6 desktop companion application is included in `test/matrix_control.py`:

```bash
pip install PySide6
python test/matrix_control.py
```
Provides network discovery, live telemetry injection with icon selectors, preset import/export, and automated brightness duty cycle stress testing.

---

## Compilation & Flashing Guide

Built with **PlatformIO** and native **ESP-IDF**:

```bash
# Compile firmware
pio run

# Flash to ESP32-C3 via COM port
python -m esptool --chip esp32c3 --port COM6 --baud 921600 write_flash 0x0 .pio\build\sparkfun_pro_micro_esp32c3\firmware.factory.bin
```

---

## Safety Backup Image

A factory image backup of the original running firmware is preserved in:
`backup/esp32c3_firmware_backup_20261027_223906.bin`
To restore the device to its initial state:
```bash
python -m esptool --chip esp32c3 --port COM6 --baud 921600 write_flash 0x0 backup/esp32c3_firmware_backup_20261027_223906.bin
```
