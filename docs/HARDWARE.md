# Compatible hardware

SmallTV-class 240×240 desk clock with an **ESP8266** on board.

> ⚠️ **SmallTV-Pro uses an ESP32** — this firmware is ESP8266-only.
> Flashing custom firmware is at your own risk; keep a backup of the flash
> ([FLASHING.md](FLASHING.md)).

## Tested units

| Device | Environment | Notes |
|---|---|---|
| SD PRO (smalltv-mod pinout) | `sdpro` | CS=15, DC=0, RST=2 |
| SD PRO alternate revision | `sdpro_alt` | CS=15, DC=2, RST=4 |
| NodeMCU + 1.3" ST7789 240×240 | `sdpro` | DIY, see wiring below |

The GeekMagic **SmallTV / SmallTV-Ultra** family is electrically the same
class of device (ESP8266 + ST7789); images built for `sdpro` work on clones
with the smalltv-mod pinout.

## Platform

| Part | Value |
|---|---|
| MCU | ESP8266, ESP-12E module |
| Flash | 4 MB, DIO mode |
| Display | ST7789, 240×240, RGB565 |
| Bus | SPI, 40 MHz |
| Filesystem | LittleFS (~1 MB region) |
| Backlight | GPIO5, **active LOW** (P-MOSFET) |

## Pinout (env `sdpro`)

| Signal | GPIO | Notes |
|---|---|---|
| MOSI | 13 | SPI data |
| SCLK | 14 | SPI clock |
| CS | 15 | chip select |
| DC | 0 | data/command (boot-strap pin) |
| RST | 2 | display reset (boot-strap pin) |
| BL | 5 | backlight, LOW = on |

`sdpro_alt`: CS=15, DC=2, RST=4.

## Firmware fingerprint

The OTA path only accepts images built by this project. The file is checked
for a build fingerprint (chip, flash size, display pins) before anything is
written — mismatched builds are rejected with the old firmware untouched.

## Build-time knobs

Everything that matters for a new board revision lives in
[`platformio.ini`](../platformio.ini): pin defines, SPI frequency, backlight
polarity. Add another `[env:...]` block and the CI matrix picks it up
automatically.
