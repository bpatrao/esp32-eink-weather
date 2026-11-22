# esp32-eink-weather

A battery-friendly Wi-Fi weather display for the **LilyGO TTGO T5 V2.3** (ESP32 + 2.13" black/white e-paper).
It sets itself up from your phone through a QR code, fetches forecasts from [Open-Meteo](https://open-meteo.com) (free, no API key),
and deep-sleeps between updates. The e-paper keeps showing the last image while the board sleeps.

## Quick upload

To flash the firmware onto the board (or update it to the latest version):

```sh
# 1. Install PlatformIO (once)
brew install platformio            # macOS; or: pip install -U platformio

# 2. Get the code (first time), or update it (later)
git clone git@github.com:bpatrao/esp32-eink-weather.git && cd esp32-eink-weather
git pull                           # when updating an existing copy

# 3. Plug the board in with a USB *data* cable, then build + upload
pio run -t upload

# 4. Optional: watch the log (Ctrl+C to quit)
pio device monitor
```

Your saved Wi-Fi and location settings are kept across uploads. See [Building and uploading new versions](#building-and-uploading-new-versions)
for details, choosing a serial port, other builds, and [troubleshooting](#troubleshooting).

## Features

- **Phone setup:** the screen shows a QR code. Scan it, join the board's Wi-Fi, and a settings page opens automatically
  (captive portal). There you choose your Wi-Fi network, location, units and update interval.
- **8 views,** cycled with the button:

  | # | View | Shows |
  |---|---|---|
  | 1 | Home | Current conditions and the next 3 days |
  | 2 | 7 days | Daily icon, high/low and rain chance |
  | 3 | Next 24 hours | Temperature line and rain-chance bars |
  | 4 | Rain next 2 h | "Rain in ~45 min" style summary with 15-minute bars |
  | 5 | Air & UV | European air-quality index (US index with imperial units), PM2.5/PM10, UV now and today's max |
  | 6 | Sun & Moon | Sunrise/sunset, sun position, day length, moon phase |
  | 7 | City photo | The location's Wikipedia photo, dithered, with the temperature on top |
  | 8 | Settings | Wi-Fi, IP and signal, location, units, battery voltage |

- **Battery indicator,** and protection: below 3.3 V it stops updating and asks to be charged.
- **Error handling:** a wrong Wi-Fi password or an unknown city sends the board back to setup and shows the reason.
  Temporary outages keep the last data on screen and retry every 5 minutes.

## Hardware

| | |
|---|---|
| Board | LilyGO TTGO T5 V2.3, marked `T5_V2.3_2.13` / `20190107` |
| Display | 2.13" 250×122 e-paper, GDEH0213B73 controller (GxEPD2 driver `GxEPD2_213_B73`) |
| USB-serial | CP2104 (driver built into recent macOS; on Windows install the Silicon Labs CP210x driver) |
| Button | GPIO 39 (active low). The second button is **RST**. |
| Battery | 3.7 V LiPo on the JST connector; measured on GPIO 35 through a 1:2 divider |

> Other T5 "2.13 inch" boards ship with different panels. If the screen stays blank or looks garbled,
> run the `driver_test` build (see below) and change the driver in `src/display_ui.cpp`.

### Buttons

| Action | Result |
|---|---|
| Short press | Next view. Further presses within 20 s use the fast (partial) refresh. |
| Hold 3 s | Open the setup portal (to change Wi-Fi, location, units or interval) |
| RST | Restart: back to Home with freshly fetched data |

## First-time setup (on the device)

1. Power the board. With no settings saved it shows **Weather setup** and a QR code.
2. Scan the QR code with your phone camera and join the `T5-Weather-XXXX` network.
   The password is printed on the screen and is new each time setup starts.
3. The settings page opens by itself. If it doesn't, browse to `http://192.168.4.1`.
4. Pick your Wi-Fi, enter its password and a location (e.g. `Lisbon`, `Porto, Portugal` or `38.72,-9.14`),
   choose units and the update interval, then tap **Save and connect**.

Settings are stored in the ESP32's flash (NVS). They survive power loss **and firmware uploads**.

## Building and uploading new versions

The project uses [PlatformIO](https://platformio.org). All libraries are downloaded automatically on the first build.

### 1. Install PlatformIO (once)

Use either option:

- **Command line:**
  ```sh
  # macOS (Homebrew)
  brew install platformio
  # or any OS with Python 3
  pip install -U platformio
  ```
- **VS Code:** install the *PlatformIO IDE* extension, then open this folder.
  The build and upload buttons are in the bottom status bar.

### 2. Connect the board

Plug the T5 in with a **USB data cable**. Many cables only charge, and with those the board doesn't show up at all.
Check that a serial port appeared:

```sh
pio device list
# macOS: /dev/cu.usbserial-XXXXXXXX   Linux: /dev/ttyUSB0   Windows: COM3 (or similar)
```

### 3. Build and upload

```sh
git clone git@github.com:bpatrao/esp32-eink-weather.git
cd esp32-eink-weather

pio run -t upload            # build + flash the weather app
pio device monitor           # optional: watch the log (Ctrl+C to quit)
```

PlatformIO picks the port automatically. If you have several serial devices, name the port explicitly:

```sh
pio run -t upload --upload-port /dev/cu.usbserial-XXXXXXXX
```

The board resets itself into the bootloader. You don't need to press any buttons.
After uploading it restarts, connects with the saved settings and shows Home.

### Updating later

```sh
git pull
pio run -t upload
```

### Other builds (environments)

| Command | What it does |
|---|---|
| `pio run -t upload` | The weather app (default `weather` environment) |
| `pio run -e weather_preview -t upload` | Same app, but after each fetch it also dumps every view's frame buffer over serial, for checking layouts without looking at the screen. Convert the dump with `python3 tools/frames_to_png.py <serial.log> <out_dir>`. Flash the normal build afterwards, because dumping on every wake wastes battery. |
| `pio run -e driver_test -t upload` | Draws a test pattern with the panel driver name, to find the right driver for other panel variants |
| `pio run -e probe -t upload` | Prints ADC pin voltages and reports pin changes, for finding battery and button pins on other board revisions |

### Troubleshooting

| Problem | Fix |
|---|---|
| No serial port appears | Use a data cable, try a port directly on the computer (not a hub), and check the board's power switch |
| `Failed to connect to ESP32` | Unplug and replug, then retry. If that still fails, hold **IO0/BOOT** (if your board has one) while the upload starts. Close any open serial monitor first. |
| Uploads fail when an SD card is inserted | GPIO 2 is shared by the SD slot and the boot logic. Remove the card while flashing. |
| Need to wipe all settings | `pio run -t erase`, then upload again. The board starts in setup mode. |
| Faint traces of earlier views | Normal for fast partial refreshes. Every 4th switch and the final view get a full refresh. |

## Project layout

```
platformio.ini        build environments and library versions
src/
  main.cpp            boot flow: buttons, views, refresh schedule, deep sleep
  config.*            settings stored in NVS
  portal.*            setup access point, captive portal and settings page
  weather.*           Open-Meteo forecast, air quality and geocoding
  cityimage.*         Wikipedia photo download, JPEG decode, Atkinson dither, NVS cache
  display_ui.*        all screens, icons and drawing helpers (GxEPD2)
  battery.*           battery voltage and percentage
  driver_test/        panel driver test sketch (separate build)
  probe/              pin probe sketch (separate build)
tools/
  frames_to_png.py    converts weather_preview frame dumps into PNGs
```

## Power

Deep sleep between updates (every 15 min to 3 h, set in the portal). On this board revision the **blue LED is a power LED
that is always on and can't be switched off in software**. It is the largest drain while the board sleeps.
Rough estimate for a 600 mAh battery at 30-minute updates: about a week with the LED, about 3-4 weeks with it removed.

## Data sources and credits

- Weather, air quality and geocoding: [Open-Meteo](https://open-meteo.com) (data licensed under CC BY 4.0)
- City photos: [Wikipedia](https://www.wikipedia.org) / Wikimedia Commons (each photo under its own license, typically CC BY-SA)
- Libraries: [GxEPD2](https://github.com/ZinggJM/GxEPD2), [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library),
  [ArduinoJson](https://arduinojson.org), [JPEGDEC](https://github.com/bitbank2/JPEGDEC)
