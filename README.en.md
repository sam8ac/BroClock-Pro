![BroClock Pro — ESP8266 crypto clock firmware for SmallTV / SD PRO](assets/banner.svg)

[![Release](https://img.shields.io/github/v/release/sam8ac/BroClock-Pro?label=Release&color=c56a90&style=for-the-badge&logo=star)](https://github.com/sam8ac/BroClock-Pro/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)
[![CI](https://img.shields.io/github/actions/workflow/status/sam8ac/BroClock-Pro/ci.yml?branch=main&label=Build&style=for-the-badge&logo=github)](https://github.com/sam8ac/BroClock-Pro/actions/workflows/ci.yml)
![ESP8266](https://img.shields.io/badge/platform-ESP8266-2b6?style=for-the-badge)
![PlatformIO](https://img.shields.io/badge/PlatformIO-ready-orange?style=for-the-badge&logo=platformio)
[![Downloads](https://img.shields.io/github/downloads/sam8ac/BroClock-Pro/total?label=Downloads&style=for-the-badge)](https://github.com/sam8ac/BroClock-Pro/releases)
[![Issues](https://img.shields.io/github/issues/sam8ac/BroClock-Pro?style=for-the-badge)](https://github.com/sam8ac/BroClock-Pro/issues)
![i18n: EN · RU · ES · ZH](https://img.shields.io/badge/i18n-EN%20%C2%B7%20RU%20%C2%B7%20ES%20%C2%B7%20ZH-2b6?style=for-the-badge)

**Page language:** [Русский](README.md) · **English** · [Español](README.es.md) · [中文](README.zh.md)

**RetroTV Pro** (BroClock Pro) — open-source ESP8266 firmware for
**GeekMagic SmallTV / SmallTV-Ultra**-class retro clocks (incl. the SD PRO
board): crypto & weather desk clock with 8 themes, candlestick charts,
carousel, holidays, OTA and a **4-language** web UI.

> 🌐 **Our signature feature — a fully localized interface in 4 languages:
> English · Russian · Spanish · Chinese.**
> Not just the menu: statuses, errors, tooltips, Wi-Fi scanner alerts, photo
> upload and update messages are all translated. Switch with one button in the
> web panel — instantly, no page reload; the choice is stored on the device,
> synced with the on-screen language and works fully offline.

[Install](docs/INSTALL.md) · [Hardware](docs/HARDWARE.md) · [HTTP API](docs/HTTP-API.md) · [Releases](https://github.com/sam8ac/BroClock-Pro/releases) · [Issues](https://github.com/sam8ac/BroClock-Pro/issues)

---

Turn the $20 TV-shaped desk clock from a wallpaper gadget into a real
information device: **Bitcoin & altcoin prices with 48-period charts, live
weather in any language, 8 switchable UI themes, holiday calendars,
night-mode backlight and full control from a phone browser** — all on the
stock ESP8266 (ESP-12E) and ST7789 240×240 display. No cloud, no account, MIT
licensed.

![BroClock Pro — the clock with a BTC chart on screen](assets/device.jpg)

*The clock in action: BTC chart on screen.*

## 💻 Project languages

| Part | Language | Where |
|---|---|---|
| Firmware (all logic, screens, networking) | **C++** (Arduino framework, ESP8266) | [`src/main.cpp`](src/main.cpp) + [`src/*.h`](src) |
| Web UI (embedded in the firmware, served by the device) | **HTML + CSS + JavaScript** | raw strings `PAGE[]`, `SETUP_PAGE[]` in `main.cpp` |
| Asset / font build tools | **Python + Node.js** | [`tools/`](tools) |
| Build system | **PlatformIO**, pinned versions — reproducible | [`platformio.ini`](platformio.ini) |

The web UI loads nothing from the internet — no libraries, no fonts: it is
served by the clock itself and works fully offline.

## 🌐 Multilingual — 4 languages out of the box

**EN · RU · ES · ZH** — the entire interface, including error messages,
tooltips, pop-up alerts and statuses. Switching in the web UI is instant (no
page reload), the choice is remembered on the device and synced with the
on-screen language. Tab titles, settings sections, clock format (12/24), Wi-Fi
scanner alerts, photo upload errors, geolocation and update messages — all
translated.

This README is also available in: [Русский](README.md) · [Español](README.es.md) · [中文](README.zh.md).
Firmware localization is a single dictionary table at the top of
[`src/main.cpp`](src/main.cpp) — adding a language is the easiest contribution.

## ✨ Features

### 💰 Cryptocurrencies
- **CoinGecko top-250** catalog with live search and manual add
- Up to **4 coins** on the main screen + up to **3** on the secondary ticker
- **48-candle** chart or line mode, auto-refresh ~60 s
- Quotes are cached on flash — after power-on/reboot data is visible immediately, no network wait
- **“Refresh data now”** — forced price/candle fetch (bypasses cache and backoffs)
- Fetchers are spread over 4 s slots: the boot batch candle+price+quotes no longer shreds the heap in one go — stable ~18 KB free block, no “heap low” or restarts
- Timezone via POSIX string (**Settings → Clock & language** tab), NTP sync

### 🌤 Weather — two providers, both work
- **Open-Meteo** — no key, no registration; **OpenWeatherMap** — by API key; switch on the fly, no reboot
- **City autocomplete** (up to 5 results): understands **Cyrillic and any Latin script** — `Vitebsk` finds `Vitebsk,BY` just as `Витебск` does
- **“My location”** — browser geolocation or IP lookup; on a clean install the city is auto-filled from the device IP (stored, the form stays editable)
- 5-day forecast + current conditions, refresh every 10 minutes
- City stored as `Name,CC` and accepted by **both** providers
- **Empty city by default** and a “gate”: with no city the device does not hammer API limits, it shows “no city” (in the selected language)

### 🎨 Themes and look
- **8 themes** with a horizontal picker strip and the current one marked
- Carousel: any set of themes, interval 5–900 s
- Night mode: brightness ramp + a separate night level
- Background color (preset shades + a palette), corner animation on Classic
- **Photo theme**: upload an image from the browser, auto-crop to 240×240, up to 256 KB, first GIF frame

![All 8 project themes — GIF cycle](assets/screen.gif)

*All 8 themes in a row (carousel).*

### 🧭 Web interface
- Separate tabs: **Themes · Settings · Crypto · Weather · WiFi · Firmware**; “Settings” is split into visual blocks (carousel / clock, timezone & language / backlight / animation / photo / calendar)
- Responsive layout: on a phone the navigation becomes a row of buttons
- Everything applies instantly, no page reload

![Web interface menus — GIF cycle](<assets/Web interface.gif>)

*Web interface: tab switching.*

### 📡 WiFi and connectivity
- **Captive portal** on first boot: access point `BroClock-PRO` → `192.168.4.1` → pick your network
- Network scanner with signal levels; the saved network connects automatically
- When already connected to the chosen network — SSID is read-only, password hidden, button disabled (“Connected”): you cannot accidentally break a working connection
- One-button “Forget network”

### 🔒 Security and stability
- **OTA with fingerprint verification**: only images of this build family (chip, flash, display pins) are accepted — a foreign firmware **will not flash**, the old one keeps running. A corrupted/renamed file cannot brick the clock
- **Heap gates before TLS** (7550/12000 bytes) + 4-second pauses between fetchers: no OOM restarts even with 4 coins and candles; RAM — 69 %
- **Fast boot**: a 12-second “quiet window” after WiFi connects (DNS/heap settle, the setup page closes instantly), then candles/prices/quotes at 4 s spacing, weather right after the city resolves; 3 minutes of continuous fetch failures → self-restart
- Weather/crypto caches survive reboot; fetch scheduler is timezone/DST-safe, watchdog-safe
- Passwords are never displayed in the UI; WiFi credentials live in ESP8266 storage

### 📅 Calendar and the rest
- Holidays for **40+ countries**, country auto-detected by IP, cached for a year (date.nager.at)

### 🛠 Development & operations
- [Full installation guide](docs/INSTALL.md): 3 OSes, in-browser flashing, backup, rollback, troubleshooting table, FAQ
- **One-click Windows installer** (`tools\flash_broclock.bat`): finds the CH340, flashes and verifies the hash
- **HTTP API** (`/api/status`, `/api/set`, `/api/refresh`, `/api/city`, `/api/wifi`…) — Home Assistant and script integration
- **Reproducible build**: pinned platform/library versions, automatic framework patching (`tools/apply_framework_patches.py`), CI builds both board revisions on every push
- **MIT** license — do whatever you want with the clock

## 🖼 Screenshots

The device photo and GIF demos are embedded above; the full folder is in [`assets/`](assets/PHOTOS.md).

## 🧩 Compatibility

Tested on **SD PRO** (both pin revisions) and NodeMCU + ST7789; electrically
compatible with the [GeekMagic SmallTV / SmallTV-Ultra](https://github.com/GeekMagicClock/smalltv-ultra) family.

> ⚠️ **SmallTV-Pro (ESP32) is not supported.** First flash requires USB-UART,
> afterwards OTA. Full device table and all steps: [docs/INSTALL.md](docs/INSTALL.md).

## 🚀 Quick start

```bash
# 1. download SDP_firmware.bin from Releases and flash
python -m esptool --port COM5 --baud 460800 write-flash 0x0 SDP_firmware.bin

# 2. connect to the BroClock-PRO access point, open http://192.168.4.1
# 3. pick your Wi-Fi network — the clock is on the LAN, IP on screen
# 4. further updates — via the "Firmware" tab
```

On Windows? Run `tools\flash_broclock.bat` next to the `.bin` — it finds the
CH340, flashes and verifies the hash:

![flash_broclock.bat — Windows firmware installer](<assets/Installation using bat.JPG>)

## 🔨 Building from source

```bash
git clone https://github.com/sam8ac/BroClock-Pro.git
cd BroClock-Pro
pio run -e sdpro        # or sdpro_alt for the alternate board revision
```

## 🗺 Roadmap

- [x] 8 themes + carousel, 4-language web UI, Themes/Settings tabs with blocks
- [x] Two weather providers, city autocomplete (ru+en), holidays
- [x] OTA with fingerprint, caches, heap gates, Windows installer
- [ ] MQTT / Home Assistant auto-discovery
- [x] Cyrillic city names (Open-Meteo, `language=ru`)
- [ ] Theme import by URL

## 🤝 Contributing

Issues and PRs welcome — checklist in
[.github/PULL_REQUEST_TEMPLATE.md](.github/PULL_REQUEST_TEMPLATE.md).
Localization is the easiest start: the entire UI dictionary is one table at
the top of `src/main.cpp`.

## ⭐ Similar projects

- [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware) — GPLv3 open firmware for GeekMagic / HelloCubic
- [aydarik/geekmagic-tv-esp8266](https://github.com/aydarik/geekmagic-tv-esp8266) — MIT ESP8266 firmware for GeekMagic SmallTV
- [GeekMagicClock/smalltv-ultra](https://github.com/GeekMagicClock/smalltv-ultra) — official Ultra firmwares and manuals

## 📜 License

[MIT](LICENSE) © 2026 sam8ac

---

*Keywords: smalltv · geekmagic · GeekMagicClock · SmallTV-Ultra · ESP8266 · ST7789 · retro clock · crypto ticker · weather station · PlatformIO · SD PRO · Arduino*
