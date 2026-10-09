<p align="center">
  <img src="assets/banner.svg" alt="RetroTV Pro — ESP8266 crypto clock firmware for SmallTV / SD PRO" width="100%">
</p>

<p align="center">
  <a href="https://github.com/sam8ac/BroClock-Pro/releases/latest"><img src="https://img.shields.io/github/v/release/sam8ac/BroClock-Pro?label=Release&color=c56a90&style=for-the-badge&logo=star" alt="Release"></a>
  <a href="https://github.com/sam8ac/BroClock-Pro/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge" alt="MIT License"></a>
  <a href="https://github.com/sam8ac/BroClock-Pro/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/sam8ac/BroClock-Pro/ci.yml?branch=main&label=Build&style=for-the-badge&logo=github" alt="CI"></a>
  <img src="https://img.shields.io/badge/platform-ESP8266-2b6?style=for-the-badge" alt="ESP8266">
  <img src="https://img.shields.io/badge/PlatformIO-ready-orange?style=for-the-badge&logo=platformio" alt="PlatformIO">
  <a href="https://github.com/sam8ac/BroClock-Pro/releases"><img src="https://img.shields.io/github/downloads/sam8ac/BroClock-Pro/total?label=Downloads&style=for-the-badge" alt="Downloads"></a>
  <a href="https://github.com/sam8ac/BroClock-Pro/issues"><img src="https://img.shields.io/github/issues/sam8ac/BroClock-Pro?style=for-the-badge" alt="Issues"></a>
</p>

<p align="center">
  <b>RetroTV Pro</b> (BroClock Pro) — open-source ESP8266 firmware for
  <b>GeekMagic SmallTV / SmallTV-Ultra</b>-class retro clocks (incl. the SD PRO
  board): crypto & weather desk clock with 8 themes, candlestick charts,
  carousel, holidays, OTA and a 4-language web UI.
</p>

<p align="center">
  <a href="docs/FLASHING.md">Flashing</a> ·
  <a href="docs/HARDWARE.md">Hardware</a> ·
  <a href="docs/HTTP-API.md">HTTP API</a> ·
  <a href="https://github.com/sam8ac/BroClock-Pro/releases">Releases</a> ·
  <a href="https://github.com/sam8ac/BroClock-Pro/issues">Issues</a>
</p>

---

Turn the $20 TV-shaped desk clock from a wallpaper gadget into a real
information device: **Bitcoin & altcoin prices with 48-period charts, live
weather in any language, 8 switchable UI themes, holiday calendars,
night-mode backlight and full control from a phone browser** — all on the
stock ESP8266 (ESP-12E) and ST7789 240×240 display. No cloud, no account, MIT
licensed.

### ✨ Features

| | |
|---|---|
| 💰 **Crypto ticker** | CoinGecko top-250 catalog, up to 4 coins on screen, 48-bar candle/line charts, 60 s refresh, quotes cached on the card |
| 🌤 **Weather** | OpenWeatherMap **or** key-less Open-Meteo, city autocomplete (Cyrillic + Latin), “My location” by browser/IP geolocation, 5-day forecast, hourly feel on the face |
| 🎨 **8 themes** | Classic, Crypto, Photo, and more — pick any subset for the carousel, horizontal thumbs strip with the current theme marked |
| 🔄 **Carousel** | Theme rotation with a 5–900 s interval, per-theme on/off list in the web UI |
| 🌐 **Web UI ×4 languages** | EN / RU / ES / ZH, dark UI, phone-adaptive layout (nav collapses to a wrap bar ≤760 px) |
| ⚙️ **Settings tab** | Carousel & clock format, language, brightness + night mode (hours + dim level), background colour, corner animation, photo theme upload |
| 📅 **Calendar** | Public-holiday highlighting, 40+ countries or auto by external IP (date.nager.at, cached for a year) |
| 🔒 **Safe OTA** | Firmware tab uploads are fingerprint-checked (chip, flash, display pins) — a foreign build is rejected **without writing**, the old firmware keeps running |
| 📡 **HTTP API** | [`/api/status`](docs/HTTP-API.md), `/api/set`, `/api/refresh`, WiFi setup, icon/photo upload — script it with `curl` |
| 🧰 **Runs for weeks** | Watchdog-safe fetch scheduler, heap-gated TLS (no OOM resets), NTP sync, caches survive reboots |

### 🖼 Screenshots

> Photos of the clock face and the web UI land in [`assets/`](assets/).
> Contributions welcome!

### 🧩 Compatible hardware

Tested on **SD PRO** (both pin revisions) — the cheap Cyrillic-market SmallTV
clone — and on NodeMCU + 1.3" ST7789. Electrically it is the
[GeekMagic SmallTV / SmallTV-Ultra](https://github.com/GeekMagicClock/smalltv-ultra)
family: **ESP8266 + ST7789 240×240 @ SPI 40 MHz, 4 MB flash, GPIO5 backlight
(active LOW)**.

> ⚠️ **SmallTV-Pro is ESP32 — not supported.** Flash at your own risk, keep a
> [backup](docs/FLASHING.md#0-backup-recommended) first.

### 🚀 Quick start

**1. Download** the [latest release](https://github.com/sam8ac/BroClock-Pro/releases/latest)
(`SDP_firmware.bin` — app image; `BroClock_Pro_factory.bin` — full 4 MB).

**2. First flash over USB-UART:**

```bash
python -m esptool --port COM5 --baud 460800 write-flash 0x0 SDP_firmware.bin
```

Windows? Just run `tools\flash_broclock.bat` — it finds the CH340 port, writes
and hash-verifies for you.

**3. Bootstrap:** the clock starts a hotspot `BroClock-PRO` (open) →
<http://192.168.4.1> → pick your Wi-Fi → the IP appears on the clock face and
at the top of the web UI.

**4. From then on: OTA** in the **Прошивка / Firmware** tab of the web UI.

Full walkthrough: **[docs/FLASHING.md](docs/FLASHING.md)**

### 🔨 Build from source

```bash
git clone https://github.com/sam8ac/BroClock-Pro.git
cd BroClock-Pro
pio run -e sdpro        # or sdpro_alt for the alternate pin revision
```

Versions are **pinned** (`platformio.ini`) so any machine reproduces the same
image; the SDPRO framework patches (heap rescue, BearSSL 6 s timeout) are
re-applied automatically by `tools/apply_framework_patches.py` on every
build. CI builds both envs on every push.

### 🗂 Web UI map

Tabs: **Темы** (pick carousel themes) · **Настройки** (carousel, clock &
language, backlight, animation, photo, calendar) · **Crypto** · **Погода** ·
**WiFi** · **Прошивка** (OTA).

### 📡 Automation

```bash
curl "http://DEVICE_IP/api/set?key=city&value=Vitebsk"
curl "http://DEVICE_IP/api/refresh"
curl "http://DEVICE_IP/api/status" | jq .market
```

See [docs/HTTP-API.md](docs/HTTP-API.md) for everything, including Home
Assistant-style integrations.

### 🗺 Roadmap

- [x] 8 themes + carousel, 4-language web UI, split Настройки tab
- [x] Dual weather sources, city autocomplete, holidays
- [x] Fingerprint-checked OTA, `flash_broclock.bat` one-click installer
- [ ] MQTT / Home Assistant auto-discovery
- [ ] Native Cyrillic chart labels
- [ ] Theme Marketplace (import from URL)

### 🤝 Contributing

Issues and PRs welcome — see
[.github/PULL_REQUEST_TEMPLATE.md](.github/PULL_REQUEST_TEMPLATE.md) for the
checklist. Localisation is the easiest way to start: the UI vocabulary lives in
one dictionary at the top of `src/main.cpp`.

### ⭐ Related projects

- [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware) — GPLv3 open firmware for GeekMagic / HelloCubic
- [aydarik/geekmagic-tv-esp8266](https://github.com/aydarik/geekmagic-tv-esp8266) — MIT ESP8266 firmware for GeekMagic SmallTV
- [GeekMagicClock/smalltv-ultra](https://github.com/GeekMagicClock/smalltv-ultra) — official Ultra firmware & manuals

### 📜 License

[MIT](LICENSE) © 2026 sam8ac — hardware drivers and artwork in `assets/`,
`data/` and `src/*.h` follow the licenses of their respective sources.

---

<p align="center">
  <sub>Keywords for search: smalltv · geekmagic · GeekMagicClock · SmallTV-Ultra · ESP8266 · ST7789 · retro clock · crypto ticker · weather station · PlatformIO · SD PRO</sub>
</p>
