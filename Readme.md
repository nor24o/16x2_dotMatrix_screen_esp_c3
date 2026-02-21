1. The Configuration Command ("cmd": "config")
This command handles all the background settings. Everything updated here is instantly saved to the ESP32's flash memory so it remembers it after a power outage.

Example Payload:
{"cmd":"config", "bright": 400, "nbright": 40, "nstart": 22, "nend": 7, "gmtoff": 7200, "dstoff": 3600, "city": "Cluj-Napoca", "country": "RO", "region": "Cluj"}

Available Settings:

bright (integer): Daytime brightness. 400 is a good baseline. Higher numbers = brighter.

nbright (integer): Nighttime brightness. 40 makes it very dim so it doesn't light up the whole room.

nstart (integer): The hour (in 24h format) when Night Mode kicks in. (e.g., 22 = 10:00 PM).

nend (integer): The hour when normal Daytime brightness resumes. (e.g., 7 = 7:00 AM).

gmtoff (integer): Your main time zone offset in seconds. (e.g., 7200 is UTC+2).

dstoff (integer): Your Daylight Saving Time offset in seconds. (e.g., 3600 adds 1 hour for summer time).

city (string): City name for the Open-Meteo weather fetch.

country (string): 2-letter country code for the weather fetch.

region (string): Region/County to help the weather API find the exact city.

2. The Playlist Command ("cmd": "playlist")
This command tells the matrix which animations to include in its normal, repeating loop. These are also saved to flash memory.

Example Payload:
{"cmd":"playlist", "clock": true, "typewriter": false, "sparkle": false, "scanner": false, "temps": true, "msg": true}

Available Settings (Send true to enable, false to skip):

clock: The main scrolling time, date, and live weather.

typewriter: The typing "ESP32 OK" effect.

sparkle: The random pixel matrix dissolve effect.

scanner: The moving block / Cylon scanner effect.

temps: The 4-quadrant screen showing the external temperatures.

msg: The scrolling custom text message.

3. The Custom Message Command ("cmd": "message")
Use this to update the text of the scrolling message module.
Note: Sending this command automatically turns the msg playlist toggle to true and instantly jumps the display to show your new message.

Example Payload:
{"cmd":"message", "text": "HAPPY NEW YEAR!"}

Available Settings:

text (string): Any text you want to scroll across the screen.

4. The External Temperatures Command ("cmd": "temps")
Use this to push live sensor data to the matrix.
Note: Because this is dynamic telemetry, it stays in RAM and does not write to flash memory. It also triggers the "Interrupt" behavior: the screen sweeps closed, flashes these values for 5 seconds, sweeps closed again, and resumes the normal playlist.

Example Payload:
{"cmd":"temps", "t1": 25.4, "t2": 18.0, "t3": -4.5, "t4": 9.1}

Available Settings:

t1 (float): Top Left module temperature.

t2 (float): Top Right module temperature.

t3 (float): Bottom Left module temperature.

t4 (float): Bottom Right module temperature.