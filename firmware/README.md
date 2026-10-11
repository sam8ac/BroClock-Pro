# Firmware

Ready-to-flash images for **BroClock Pro** (GeekMagic Clock / SmallTV / SmallTV-Ultra / SD PRO).

| File | What it is | When to use |
|---|---|---|
| `BroClock_Pro_firmware.bin` | **Application image** (same as the GitHub Release asset) | Normal install / update: flash at `0x0`, or upload via the web UI **Firmware** tab |
| `BroClock_Pro_factory.bin` | **Full factory image** (4 MB, app + filesystem + blank config) | Complete restore / first flash of a blank board |
| `flash_broclock.bat` | **One-click Windows installer** | Double-click — finds the CH340, flashes, verifies hash |

## Windows — one click

Put `flash_broclock.bat` next to the `.bin` (they already are, in this folder), connect the clock via USB, double-click the `.bat` and choose:

```
[1] Full flash  BroClock_Pro_factory.bin   (resets Wi-Fi, city, themes, photos)
[2] Firmware only  BroClock_Pro_firmware.bin  (keeps settings)
```

## Manual flash (esptool)

```bash
python -m pip install esptool

# Windows
python -m esptool --port COM5 --baud 460800 write-flash 0x0 BroClock_Pro_firmware.bin

# Linux / macOS
python -m esptool --port /dev/ttyUSB0 --baud 460800 write-flash 0x0 BroClock_Pro_firmware.bin
```

## Factory image (clean slate)

```bash
python -m esptool --port COM5 --baud 460800 write-flash 0x0 BroClock_Pro_factory.bin
```

This wipes Wi-Fi credentials, API keys and all settings — use it only when you want a clean start.

## Notes

- Images are **keyless**: no OpenWeatherMap / CoinGecko keys embedded. Weather works via Open-Meteo out of the box; enter your own API keys in the web UI (Settings).
- Always flash at **0x0**.
- Latest release (same binaries): <https://github.com/sam8ac/BroClock-Pro/releases>
